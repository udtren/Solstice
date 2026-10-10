/*
 *  SPDX-FileCopyrightText: 2010 Boudewijn Rempt <boud@valdyas.org>
 *  SPDX-FileCopyrightText: 2010 Lukáš Tvrdý <lukast.dev@gmail.com>
 *  SPDX-FileCopyrightText: 2007 Eric Lamarque <eric.lamarque@free.fr>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <QtEndian>

#include "kis_abr_brush_collection.h"
#include "kis_abr_brush.h"

#include <QDomElement>
#include <QFile>
#include <QImage>
#include <QPoint>
#include <QColor>
#include <QByteArray>
#include <kis_debug.h>
#include <QString>
#include <QBuffer>
#include <QFileInfo>
#include <KoMD5Generator.h>
#include <klocalizedstring.h>

#include <KoColor.h>
#include <QDomDocument>
#include <QHash>
#include <QTextStream>
#include "KisAbrParser.h"
#include <asl/kis_asl_reader.h>

namespace
{
/// the name of a tip that has none of its own: test.abr -> test_12
QString abr_v1_brush_name(const QString filename, qint32 id)
{
    QString result = filename;
    int pos = filename.lastIndexOf('.');
    result.remove(pos, 4);
    QTextStream(&result) << "_" << id;
    return result;
}

/**
 * Solstice: the names of the presets that use each sampled tip, by the
 * tip's identifier (`sampledData`), from the preset descriptors of version
 * 6 and later (docs/agent/abr-import-plan.md)
 */
QHash<QString, QString> presetNamesBySample(const QByteArray &descriptors)
{
    QHash<QString, QString> names;
    if (descriptors.isEmpty()) {
        return names;
    }
    QByteArray data = descriptors;
    QBuffer buffer(&data);
    buffer.open(QIODevice::ReadOnly);
    const QDomDocument doc = KisAslReader::readFillLayer(buffer);

    const QDomNodeList nodes = doc.elementsByTagName(QStringLiteral("node"));
    for (int i = 0; i < nodes.size(); i++) {
        const QDomElement preset = nodes.at(i).toElement();
        if (preset.attribute(QStringLiteral("type")) != QStringLiteral("Descriptor")) {
            continue;
        }
        QString name;
        QString sample;
        for (QDomElement child = preset.firstChildElement(); !child.isNull(); child = child.nextSiblingElement()) {
            const QString key = child.attribute(QStringLiteral("key"));
            if (key == QStringLiteral("Nm  ")) {
                name = child.attribute(QStringLiteral("value")).trimmed();
            } else if (key == QStringLiteral("Brsh")) {
                for (QDomElement tip = child.firstChildElement(); !tip.isNull(); tip = tip.nextSiblingElement()) {
                    if (tip.attribute(QStringLiteral("key")) == QStringLiteral("sampledData")) {
                        sample = tip.attribute(QStringLiteral("value"));
                    }
                }
            }
        }
        if (!name.isEmpty() && !sample.isEmpty() && !names.contains(sample)) {
            names.insert(sample, name);
        }
    }
    return names;
}
} // namespace

KisAbrBrushCollection::KisAbrBrushCollection(const QString& filename)
    : m_isLoaded(false)
    , m_lastModified()
    , m_filename(filename)
    , m_abrBrushes(new QMap<QString, KisAbrBrushSP>())
{
}

KisAbrBrushCollection::KisAbrBrushCollection(const KisAbrBrushCollection& rhs)
    : m_isLoaded(rhs.m_isLoaded)
    , m_lastModified(rhs.m_lastModified)
{
    m_abrBrushes.reset(new QMap<QString, KisAbrBrushSP>());
    for (auto it = rhs.m_abrBrushes->begin();
         it != rhs.m_abrBrushes->end();
         ++it) {

        m_abrBrushes->insert(it.key(), KisAbrBrushSP(new KisAbrBrush(*it.value(), this)));
    }
}

bool KisAbrBrushCollection::load()
{
    m_isLoaded = true;
    QFile file(filename());
    QFileInfo info(file);
    m_lastModified = info.lastModified();
    // check if the file is open correctly
    if (!file.open(QIODevice::ReadOnly)) {
        warnKrita << "Can't open file " << filename();
        return false;
    }

    bool res = loadFromDevice(&file);
    file.close();

    return res;

}

bool KisAbrBrushCollection::loadFromDevice(QIODevice *dev)
{
    // Solstice: read with KisAbrParser (docs/agent/abr-import-plan.md)
    KisAbrParser::Contents contents;
    const bool parsed = KisAbrParser::parse(dev->readAll(), &contents);
    for (const QString &warning : contents.warnings) {
        warnKrita << "ABR" << filename() << ":" << warning;
    }
    if (!parsed) {
        warnKrita << "ERROR: unable to read the ABR file" << filename() << "version" << contents.version << "subversion"
                  << contents.subversion;
        return false;
    }
    if (contents.samples.isEmpty()) {
        warnKrita << "ERROR: no sample brush found in" << filename();
        return false;
    }

    const QString fileName = QFileInfo(filename()).fileName();
    const QHash<QString, QString> presetNames = presetNamesBySample(contents.descriptors);

    for (const KisAbrParser::Sample &sample : contents.samples) {
        // the resource's file name identifies it, as it always has; the
        // name shown is the tip's own or its preset's when there is one
        const QString key = !sample.name.isEmpty() ? sample.name : abr_v1_brush_name(fileName, sample.index);
        const QString shownName = !sample.name.isEmpty() ? sample.name : presetNames.value(sample.id, key);

        KisAbrBrushSP abrBrush;
        if (m_abrBrushes->contains(key)) {
            abrBrush = m_abrBrushes.data()->operator[](key);
        } else {
            abrBrush = KisAbrBrushSP(new KisAbrBrush(key, this));
            QBuffer buf;
            buf.open(QFile::ReadWrite);
            sample.image.save(&buf, "PNG");
            abrBrush->setMD5Sum(KoMD5Generator::generateHash(buf.data()));
        }

        abrBrush->setBrushTipImage(sample.image);
        abrBrush->setValid(true);
        abrBrush->setName(shownName);
        m_abrBrushes.data()->operator[](key) = abrBrush;
    }

    return true;
}

bool KisAbrBrushCollection::save()
{
    return false;
}

bool KisAbrBrushCollection::saveToDevice(QIODevice */*dev*/) const
{
    return false;
}

bool KisAbrBrushCollection::isLoaded() const
{
    return m_isLoaded;
}

QImage KisAbrBrushCollection::image() const
{
    if (m_abrBrushes->size() > 0) {
        return m_abrBrushes->values().first()->image();
    }
    return QImage();
}

void KisAbrBrushCollection::toXML(QDomDocument& d, QDomElement& e) const
{
    Q_UNUSED(d);
    Q_UNUSED(e);
    // Do nothing...
}

QString KisAbrBrushCollection::defaultFileExtension() const
{
    return QString(".abr");
}
