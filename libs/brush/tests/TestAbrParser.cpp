/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <simpletest.h>

#include <QBuffer>
#include <QDataStream>
#include <QFile>
#include <QRandomGenerator>

#include <KisAbrParser.h>
#include <kis_abr_brush_collection.h>

/**
 * KisAbrParser (docs/agent/abr-import-plan.md, phase 2): versions, 16-bit
 * and compressed tips, names, and damaged files.
 */
class TestAbrParser : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testBundledFile();
    void testSixteenBitAndRawTips();
    void testVersion2NamesAndComputedBrushes();
    void testDamagedFiles();
};

namespace
{
QByteArray bigEndian(const std::function<void(QDataStream &)> &write)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    write(stream);
    return data;
}

/// the rectangle, depth and compression, then the pixels, of a tip
QByteArray tipBody(int width, int height, int depth, bool rle, const QByteArray &pixels)
{
    return bigEndian([&](QDataStream &s) {
        s << qint32(0) << qint32(0) << qint32(height) << qint32(width) << quint16(depth) << quint8(rle ? 1 : 0);
        if (!rle) {
            s.writeRawData(pixels.constData(), pixels.size());
            return;
        }
        // each row as one literal run
        const int rowBytes = pixels.size() / height;
        for (int y = 0; y < height; y++) {
            s << quint16(rowBytes + 1);
        }
        for (int y = 0; y < height; y++) {
            s << quint8(rowBytes - 1);
            s.writeRawData(pixels.constData() + y * rowBytes, rowBytes);
        }
    });
}

QByteArray section(const char *key, const QByteArray &body)
{
    QByteArray result = bigEndian([&](QDataStream &s) {
        s.writeRawData("8BIM", 4);
        s.writeRawData(key, 4);
        s << quint32(body.size());
    });
    result += body;
    while (result.size() % 4) {
        result += '\0';
    }
    return result;
}

/// a version 6/9 file with two tips: 16-bit RLE and 8-bit raw
QByteArray version6File(int version)
{
    QByteArray tip16;
    for (int i = 0; i < 4 * 3; i++) {
        const quint16 value = quint16(i * 5000);
        tip16 += char(value >> 8);
        tip16 += char(value & 0xff);
    }
    QByteArray tip8;
    for (int i = 0; i < 5 * 2; i++) {
        tip8 += char(i * 20);
    }

    QByteArray samples;
    int n = 0;
    for (const QByteArray &body : {tipBody(4, 3, 16, true, tip16), tipBody(5, 2, 8, false, tip8)}) {
        const QByteArray id = QByteArray("$tip-") + QByteArray::number(++n);
        QByteArray entry;
        entry += char(id.size());
        entry += id;
        entry += QByteArray(264, '\0');
        entry += body;
        samples += bigEndian([&](QDataStream &s) {
            s << quint32(entry.size());
        });
        samples += entry;
        while (samples.size() % 4) {
            samples += '\0';
        }
    }

    QByteArray file = bigEndian([&](QDataStream &s) {
        s << quint16(version) << quint16(2);
    });
    file += section("samp", samples);
    file += section("patt", QByteArray());
    file += section("desc", QByteArray("not read here"));
    return file;
}
} // namespace

void TestAbrParser::testBundledFile()
{
    QFile file(QString(FILES_DATA_DIR) + "/brushes_by_mar_ka_d338ela.abr");
    QVERIFY(file.open(QIODevice::ReadOnly));
    KisAbrParser::Contents contents;
    QVERIFY(KisAbrParser::parse(file.readAll(), &contents));
    QCOMPARE(contents.version, 6);
    QCOMPARE(contents.subversion, 2);
    QCOMPARE(contents.samples.size(), 31);
    QVERIFY(!contents.descriptors.isEmpty());
    QVERIFY(contents.warnings.isEmpty());
    for (const KisAbrParser::Sample &sample : contents.samples) {
        QVERIFY(!sample.id.isEmpty());
        QVERIFY(!sample.image.isNull());
    }

    // the collection names the tips after their presets, and keeps the old
    // file names that identify them
    KisAbrBrushCollection collection(QString(FILES_DATA_DIR) + "/brushes_by_mar_ka_d338ela.abr");
    QVERIFY(collection.load());
    QCOMPARE(collection.brushes().size(), 31);
    int named = 0;
    for (KisAbrBrushSP brush : collection.brushes()) {
        QVERIFY(brush->filename().startsWith("brushes_by_mar_ka_d338ela_"));
        named += brush->name() != brush->filename();
    }
    qInfo() << "tips named after their presets:" << named << "of" << collection.brushes().size();
    QVERIFY(named > 0);
}

void TestAbrParser::testSixteenBitAndRawTips()
{
    for (int version : {6, 9}) {
        KisAbrParser::Contents contents;
        QVERIFY(KisAbrParser::parse(version6File(version), &contents));
        QCOMPARE(contents.version, version);
        QCOMPARE(contents.samples.size(), 2);
        QCOMPARE(contents.descriptors, QByteArray("not read here"));

        const KisAbrParser::Sample &sixteen = contents.samples[0];
        QCOMPARE(sixteen.id, QString("$tip-1"));
        QCOMPARE(sixteen.depth, 16);
        QCOMPARE(sixteen.image.size(), QSize(4, 3));
        // 16-bit value i * 5000 -> high byte, inverted
        for (int i = 0; i < 12; i++) {
            QCOMPARE(qRed(sixteen.image.pixel(i % 4, i / 4)), 255 - ((i * 5000) >> 8));
        }

        const KisAbrParser::Sample &eight = contents.samples[1];
        QCOMPARE(eight.index, 2);
        QCOMPARE(eight.image.size(), QSize(5, 2));
        for (int i = 0; i < 10; i++) {
            QCOMPARE(qRed(eight.image.pixel(i % 5, i / 5)), 255 - i * 20);
        }
    }
}

void TestAbrParser::testVersion2NamesAndComputedBrushes()
{
    const QByteArray pixels(6, char(100));
    const QByteArray sampled = bigEndian([&](QDataStream &s) {
                                   s << quint32(0) << quint16(25);
                                   const QString name = QStringLiteral("Ink tip");
                                   s << quint32(name.size() + 1);
                                   for (QChar c : name) {
                                       s << quint16(c.unicode());
                                   }
                                   s << quint16(0);
                                   s << quint8(1);
                                   s << qint16(0) << qint16(0) << qint16(0) << qint16(0);
                               })
        + tipBody(3, 2, 8, false, pixels);
    const QByteArray computed(14, '\0');

    const QByteArray file = bigEndian([&](QDataStream &s) {
        s << quint16(2) << quint16(2);
        s << quint16(1) << quint32(computed.size());
        s.writeRawData(computed.constData(), computed.size());
        s << quint16(2) << quint32(sampled.size());
        s.writeRawData(sampled.constData(), sampled.size());
    });

    KisAbrParser::Contents contents;
    QVERIFY(KisAbrParser::parse(file, &contents));
    QCOMPARE(contents.skippedComputedBrushes, 1);
    QCOMPARE(contents.samples.size(), 1);
    QCOMPARE(contents.samples[0].name, QString("Ink tip"));
    QCOMPARE(contents.samples[0].index, 2);
    QCOMPARE(qRed(contents.samples[0].image.pixel(0, 0)), 155);
}

void TestAbrParser::testDamagedFiles()
{
    QFile file(QString(FILES_DATA_DIR) + "/brushes_by_mar_ka_d338ela.abr");
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray real = file.readAll().left(200000);

    for (const QByteArray &original : {version6File(6), real}) {
        // cut short anywhere
        const int step = qMax(1, original.size() / 400);
        for (int length = 0; length < original.size(); length += step) {
            KisAbrParser::Contents contents;
            KisAbrParser::parse(original.left(length), &contents);
        }
        // bytes changed anywhere
        QRandomGenerator random(7);
        for (int i = 0; i < 400; i++) {
            QByteArray damaged = original;
            for (int j = 0; j < 4; j++) {
                damaged[random.bounded(damaged.size())] = char(random.bounded(256));
            }
            KisAbrParser::Contents contents;
            KisAbrParser::parse(damaged, &contents);
        }
    }

    // a huge declared tip is refused, not allocated
    QByteArray huge = version6File(6);
    const int rectangle = 4 + 4 + 4 + 1 + 6 + 264; // samp header, length, id
    QDataStream patch(&huge, QIODevice::ReadWrite);
    patch.setByteOrder(QDataStream::BigEndian);
    patch.device()->seek(rectangle + 8);
    patch << qint32(100000) << qint32(100000);
    KisAbrParser::Contents contents;
    KisAbrParser::parse(huge, &contents);
    QVERIFY(!contents.warnings.isEmpty());
}

SIMPLE_TEST_MAIN(TestAbrParser)

#include "TestAbrParser.moc"
