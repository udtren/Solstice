/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisAbrParser.h"

#include <QtEndian>

#include <cstring>

namespace
{
/// A bounds-checked big-endian reader over a byte array
class Reader
{
public:
    Reader(const QByteArray &data, qint64 begin = 0, qint64 end = -1)
        : m_data(data)
        , m_pos(begin)
        , m_end(end < 0 ? data.size() : qMin<qint64>(end, data.size()))
    {
    }

    bool ok() const
    {
        return m_ok;
    }
    const QByteArray &dataRef() const
    {
        return m_data;
    }
    /// a copy of @p length bytes at @p begin, within this reader's range
    QByteArray bytes(qint64 begin, qint64 length) const
    {
        if (begin < 0 || length < 0 || begin + length > m_end) {
            return QByteArray();
        }
        return m_data.mid(int(begin), int(length));
    }
    bool peekIs(qint64 pos, const char *signature) const
    {
        const qint64 length = qint64(qstrlen(signature));
        return pos >= 0 && pos + length <= m_end && qstrncmp(m_data.constData() + pos, signature, uint(length)) == 0;
    }
    qint64 pos() const
    {
        return m_pos;
    }
    qint64 end() const
    {
        return m_end;
    }
    qint64 left() const
    {
        return m_end - m_pos;
    }

    bool skip(qint64 bytes)
    {
        if (!m_ok || bytes < 0 || bytes > left()) {
            m_ok = false;
            return false;
        }
        m_pos += bytes;
        return true;
    }

    void seek(qint64 pos)
    {
        if (pos < 0 || pos > m_end) {
            m_ok = false;
        } else {
            m_pos = pos;
        }
    }

    const char *take(qint64 bytes)
    {
        if (!m_ok || bytes < 0 || bytes > left()) {
            m_ok = false;
            return nullptr;
        }
        const char *data = m_data.constData() + m_pos;
        m_pos += bytes;
        return data;
    }

    quint8 u8()
    {
        const char *data = take(1);
        return data ? quint8(*data) : 0;
    }
    quint16 u16()
    {
        const char *data = take(2);
        return data ? qFromBigEndian<quint16>(data) : 0;
    }
    quint32 u32()
    {
        const char *data = take(4);
        return data ? qFromBigEndian<quint32>(data) : 0;
    }
    qint32 i32()
    {
        return qint32(u32());
    }

private:
    const QByteArray &m_data;
    qint64 m_pos;
    qint64 m_end;
    bool m_ok = true;
};

qint64 paddedTo4(qint64 value)
{
    return (value + 3) & ~qint64(3);
}

/// PackBits rows, each with its compressed length first
bool decodeRle(Reader &reader, int height, int rowBytes, char *out)
{
    QVector<quint16> rowLengths(height);
    for (int i = 0; i < height; i++) {
        rowLengths[i] = reader.u16();
    }
    if (!reader.ok()) {
        return false;
    }
    for (int y = 0; y < height; y++) {
        const char *row = reader.take(rowLengths[y]);
        if (!row) {
            return false;
        }
        char *dst = out + qint64(y) * rowBytes;
        int written = 0;
        int i = 0;
        while (i < rowLengths[y] && written < rowBytes) {
            const int n = qint8(row[i++]);
            if (n >= 0) {
                const int count = qMin(n + 1, qMin(rowBytes - written, rowLengths[y] - i));
                memcpy(dst + written, row + i, count);
                written += count;
                i += n + 1;
            } else if (n != -128) {
                if (i >= rowLengths[y]) {
                    break;
                }
                const int count = qMin(-n + 1, rowBytes - written);
                memset(dst + written, row[i++], count);
                written += count;
            }
        }
    }
    return true;
}

/// Reads the tip rectangle, depth, compression and pixels that end every
/// sampled brush
bool readTipImage(Reader &reader,
                  KisAbrParser::Sample *sample,
                  qint64 *totalBytes,
                  QStringList *warnings,
                  bool *budgetExceeded)
{
    const qint32 top = reader.i32();
    const qint32 left = reader.i32();
    const qint32 bottom = reader.i32();
    const qint32 right = reader.i32();
    const quint16 depth = reader.u16();
    const quint8 compression = reader.u8();
    if (!reader.ok()) {
        warnings->append(QStringLiteral("a brush tip is cut short"));
        return false;
    }

    const qint64 width = qint64(right) - left;
    const qint64 height = qint64(bottom) - top;
    if (width <= 0 || height <= 0 || width > KisAbrParser::maxEdge || height > KisAbrParser::maxEdge) {
        warnings->append(QStringLiteral("a brush tip of %1 x %2 pixels is skipped").arg(width).arg(height));
        return false;
    }
    if (depth != 8 && depth != 16) {
        warnings->append(QStringLiteral("a brush tip of depth %1 is skipped").arg(depth));
        return false;
    }
    if (compression > 1) {
        warnings->append(QStringLiteral("a brush tip with compression %1 is skipped").arg(compression));
        return false;
    }

    const int bytesPerPixel = depth / 8;
    const qint64 size = width * height * bytesPerPixel;
    if (*totalBytes + size > KisAbrParser::maxTotalBytes) {
        warnings->append(QStringLiteral("the brush tips exceed %1 MiB").arg(KisAbrParser::maxTotalBytes >> 20));
        *budgetExceeded = true;
        return false;
    }
    *totalBytes += size;

    QByteArray pixels(int(size), '\0');
    if (compression == 0) {
        const char *raw = reader.take(size);
        if (!raw) {
            warnings->append(QStringLiteral("a brush tip is cut short"));
            return false;
        }
        memcpy(pixels.data(), raw, size);
    } else if (!decodeRle(reader, int(height), int(width) * bytesPerPixel, pixels.data())) {
        warnings->append(QStringLiteral("a compressed brush tip is cut short"));
        return false;
    }

    // gray, inverted: Photoshop stores how much the brush paints
    QImage image(int(width), int(height), QImage::Format_RGB32);
    const uchar *src = reinterpret_cast<const uchar *>(pixels.constData());
    for (int y = 0; y < height; y++) {
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < width; x++) {
            // the high byte of a 16-bit value
            const int coverage = src[(qint64(y) * width + x) * bytesPerPixel];
            const int value = 255 - coverage;
            line[x] = qRgb(value, value, value);
        }
    }
    sample->image = image;
    sample->depth = depth;
    return true;
}

/// Versions 1 and 2: a count, then brushes
void parseLegacy(Reader &reader, KisAbrParser::Contents *contents)
{
    const int count = reader.u16();
    qint64 totalBytes = 0;
    bool budgetExceeded = false;

    for (int i = 0; i < count && reader.ok() && !budgetExceeded; i++) {
        const quint16 type = reader.u16();
        const quint32 length = reader.u32();
        if (!reader.ok() || length > reader.left()) {
            contents->warnings.append(QStringLiteral("brush %1 is cut short").arg(i + 1));
            break;
        }
        const qint64 next = reader.pos() + length;
        Reader brush(reader.dataRef(), reader.pos(), next);
        reader.seek(next);

        if (type == 1) {
            contents->skippedComputedBrushes++;
            continue;
        }
        if (type != 2) {
            contents->warnings.append(QStringLiteral("brush %1 has the unknown type %2").arg(i + 1).arg(type));
            continue;
        }

        KisAbrParser::Sample sample;
        sample.index = i + 1;
        brush.skip(4 + 2); // misc, spacing
        if (contents->version == 2) {
            const quint32 nameLength = brush.u32();
            if (nameLength > 0 && nameLength <= 4096 && brush.ok()) {
                const char *text = brush.take(qint64(nameLength) * 2);
                if (text) {
                    QString name;
                    for (quint32 c = 0; c < nameLength; c++) {
                        name.append(QChar(qFromBigEndian<quint16>(text + c * 2)));
                    }
                    sample.name = name.remove(QChar(0)).trimmed();
                }
            } else if (nameLength > 4096) {
                brush.skip(qint64(nameLength) * 2);
            }
        }
        brush.skip(1 + 8); // anti-aliasing, short bounds
        if (readTipImage(brush, &sample, &totalBytes, &contents->warnings, &budgetExceeded)) {
            contents->samples.append(sample);
        }
    }
}

/// The `samp` section: one entry per tip, each padded to 4 bytes
void parseSamples(Reader section, KisAbrParser::Contents *contents)
{
    qint64 totalBytes = 0;
    bool budgetExceeded = false;
    int index = 0;

    while (section.left() >= 4 && !budgetExceeded) {
        const quint32 length = section.u32();
        if (length == 0) {
            break;
        }
        if (length > section.left()) {
            contents->warnings.append(QStringLiteral("a brush tip entry is cut short"));
            break;
        }
        const qint64 next = paddedTo4(section.pos() + length);
        Reader entry(section.dataRef(), section.pos(), section.pos() + length);

        KisAbrParser::Sample sample;
        sample.index = ++index;
        const quint8 idLength = entry.u8();
        const char *id = entry.take(idLength);
        if (id) {
            sample.id = QString::fromLatin1(id, idLength);
        }
        // short bounds and an unknown short (1), or unknown bytes (2)
        entry.skip(contents->subversion == 1 ? 10 : 264);
        if (readTipImage(entry, &sample, &totalBytes, &contents->warnings, &budgetExceeded)) {
            contents->samples.append(sample);
        }

        if (next > section.end()) {
            break;
        }
        section.seek(next);
    }
}

/// Versions 6 to 10: a subversion, then 8BIM sections
void parseSections(Reader &reader, KisAbrParser::Contents *contents)
{
    while (reader.left() >= 12) {
        const char *signature = reader.take(4);
        if (!signature || qstrncmp(signature, "8BIM", 4) != 0) {
            contents->warnings.append(
                QStringLiteral("the data after offset %1 is not a section").arg(reader.pos() - 4));
            break;
        }
        const QByteArray key(reader.take(4), 4);
        const quint32 length = reader.u32();
        if (!reader.ok() || length > reader.left()) {
            contents->warnings.append(QStringLiteral("the section %1 is cut short").arg(QString::fromLatin1(key)));
            break;
        }
        const qint64 begin = reader.pos();
        const qint64 end = begin + length;

        if (key == "samp") {
            parseSamples(Reader(reader.dataRef(), begin, end), contents);
        } else if (key == "patt") {
            contents->patterns = reader.bytes(begin, length);
        } else if (key == "desc") {
            contents->descriptors = reader.bytes(begin, length);
        } else if (key == "phry") {
            contents->hierarchy = reader.bytes(begin, length);
        }

        // sections are padded to 4 bytes, which some files leave out
        const qint64 padded = paddedTo4(end);
        reader.seek(padded != end && reader.peekIs(padded, "8BIM") ? padded : end);
        if (padded != end && !reader.peekIs(reader.pos(), "8BIM") && padded <= reader.end()) {
            reader.seek(padded);
        }
    }
}
} // namespace

bool KisAbrParser::parse(const QByteArray &data, Contents *contents)
{
    *contents = Contents();
    Reader reader(data);

    contents->version = reader.u16();
    if (!reader.ok()) {
        contents->warnings.append(QStringLiteral("the file is empty"));
        return false;
    }

    if (contents->version == 1 || contents->version == 2) {
        parseLegacy(reader, contents);
    } else if (contents->version >= 6 && contents->version <= 10) {
        contents->subversion = reader.u16();
        if (contents->subversion != 1 && contents->subversion != 2) {
            contents->warnings.append(QStringLiteral("the subversion %1 is not supported").arg(contents->subversion));
            return false;
        }
        parseSections(reader, contents);
    } else {
        contents->warnings.append(QStringLiteral("the version %1 is not supported").arg(contents->version));
        return false;
    }

    return !contents->samples.isEmpty() || !contents->descriptors.isEmpty();
}
