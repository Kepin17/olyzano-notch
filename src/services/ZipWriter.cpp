#include "ZipWriter.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QtEndian>

namespace ZipWriter {
namespace {
constexpr qint64 Chunk = 1 << 20;
constexpr quint64 Max32 = 0xFFFFFFFFull;
constexpr quint16 Max16 = 0xFFFF;

const quint32 *crcTable()
{
    static const auto table = [] {
        static quint32 t[256];
        for (quint32 n = 0; n < 256; ++n) {
            quint32 c = n;
            for (int k = 0; k < 8; ++k)
                c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    return table;
}

void le16(QByteArray &b, quint16 v) { b.append(char(v & 0xFF)).append(char(v >> 8)); }
void le32(QByteArray &b, quint32 v) { le16(b, v & 0xFFFF); le16(b, v >> 16); }
void le64(QByteArray &b, quint64 v) { le32(b, quint32(v & 0xFFFFFFFFu)); le32(b, quint32(v >> 32)); }

// MS-DOS time and date, which is what a zip stores
void dosStamp(const QDateTime &when, quint16 *time, quint16 *date)
{
    const QDateTime t = when.isValid() ? when.toLocalTime() : QDateTime::currentDateTime();
    const int year = qBound(1980, t.date().year(), 2107);
    *time = quint16((t.time().hour() << 11) | (t.time().minute() << 5) | (t.time().second() / 2));
    *date = quint16(((year - 1980) << 9) | (t.date().month() << 5) | t.date().day());
}

struct Done {
    QByteArray name;
    quint32 crc;
    quint64 size, offset;
    quint16 time, date;
    bool big; // its sizes did not fit in 32 bits: they are in a Zip64 field
};

bool needs64(const Done &d) { return d.big || d.offset >= Max32; }
}

QString write(const QString &outPath, const QList<Entry> &files, std::atomic<qint64> *progress, const std::atomic_bool *cancel)
{
    if (files.isEmpty())
        return QStringLiteral("a zip needs at least one file");
    if (progress)
        progress->store(0);
    QFile out(outPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QStringLiteral("cannot write the zip file");
    const auto fail = [&out, &outPath](const QString &why) {
        out.close();
        QFile::remove(outPath);
        return why;
    };

    QList<Done> done;
    quint64 offset = 0, written = 0;
    const quint32 *table = crcTable();
    for (const Entry &e : files) {
        const QFileInfo info(e.path);
        if (!info.isFile() || !info.isReadable())
            return fail(QStringLiteral("cannot read \"%1\"").arg(e.name));
        QFile in(e.path);
        if (!in.open(QIODevice::ReadOnly))
            return fail(QStringLiteral("cannot read \"%1\"").arg(e.name));

        Done d{e.name.toUtf8(), 0, quint64(info.size()), offset, 0, 0, quint64(info.size()) >= Max32};
        dosStamp(info.lastModified(), &d.time, &d.date);
        // The checksum and the sizes come after the data (bit 3), so the file is read once. A big file says so in
        // a Zip64 field of its header, and its descriptor then has 8-byte sizes.
        QByteArray header;
        le32(header, 0x04034B50);
        le16(header, d.big ? 45 : 20);
        le16(header, 0x0808); // names are UTF-8; sizes follow the data
        le16(header, 0);      // stored
        le16(header, d.time);
        le16(header, d.date);
        le32(header, 0);
        le32(header, d.big ? 0xFFFFFFFFu : 0);
        le32(header, d.big ? 0xFFFFFFFFu : 0);
        le16(header, quint16(d.name.size()));
        le16(header, d.big ? 20 : 0);
        header.append(d.name);
        if (d.big) {
            le16(header, 0x0001);
            le16(header, 16);
            le64(header, 0);
            le64(header, 0);
        }
        if (out.write(header) != header.size())
            return fail(QStringLiteral("cannot write the zip file"));

        quint32 crc = 0xFFFFFFFFu;
        quint64 total = 0;
        for (QByteArray chunk = in.read(Chunk); !chunk.isEmpty(); chunk = in.read(Chunk)) {
            if (cancel && cancel->load())
                return fail(QStringLiteral("cancelled"));
            for (unsigned char c : chunk)
                crc = table[(crc ^ c) & 0xFF] ^ (crc >> 8);
            if (out.write(chunk) != chunk.size())
                return fail(QStringLiteral("cannot write the zip file"));
            total += quint64(chunk.size());
            written += quint64(chunk.size());
            if (progress)
                progress->store(qint64(written));
        }
        if (total != d.size)
            return fail(QStringLiteral("\"%1\" changed while zipping").arg(e.name));
        d.crc = crc ^ 0xFFFFFFFFu;

        QByteArray tail;
        le32(tail, 0x08074B50);
        le32(tail, d.crc);
        if (d.big) {
            le64(tail, d.size);
            le64(tail, d.size);
        } else {
            le32(tail, quint32(d.size));
            le32(tail, quint32(d.size));
        }
        if (out.write(tail) != tail.size())
            return fail(QStringLiteral("cannot write the zip file"));
        offset += quint64(header.size()) + d.size + quint64(tail.size());
        done.append(d);
    }

    QByteArray central;
    for (const Done &d : done) {
        const bool z64 = needs64(d);
        QByteArray extra;
        if (z64) { // only the fields that are 0xFFFFFFFF in the record, in this order
            if (d.big) { le64(extra, d.size); le64(extra, d.size); }
            if (d.offset >= Max32) le64(extra, d.offset);
        }
        le32(central, 0x02014B50);
        le16(central, z64 ? 45 : 20);
        le16(central, z64 ? 45 : 20);
        le16(central, 0x0808);
        le16(central, 0);
        le16(central, d.time);
        le16(central, d.date);
        le32(central, d.crc);
        le32(central, d.big ? 0xFFFFFFFFu : quint32(d.size));
        le32(central, d.big ? 0xFFFFFFFFu : quint32(d.size));
        le16(central, quint16(d.name.size()));
        le16(central, z64 ? quint16(extra.size() + 4) : 0);
        le16(central, 0); // comment
        le16(central, 0); // disk
        le16(central, 0); // internal attributes
        le32(central, 0); // external attributes
        le32(central, d.offset >= Max32 ? 0xFFFFFFFFu : quint32(d.offset));
        central.append(d.name);
        if (z64) {
            le16(central, 0x0001);
            le16(central, quint16(extra.size()));
            central.append(extra);
        }
    }
    const quint64 directoryOffset = offset, directorySize = quint64(central.size());
    const bool z64End = done.size() >= Max16 || directoryOffset >= Max32 || directorySize >= Max32;
    if (z64End) {
        const quint64 end64 = directoryOffset + directorySize;
        le32(central, 0x06064B50);
        le64(central, 44);
        le16(central, 45);
        le16(central, 45);
        le32(central, 0);
        le32(central, 0);
        le64(central, quint64(done.size()));
        le64(central, quint64(done.size()));
        le64(central, directorySize);
        le64(central, directoryOffset);
        le32(central, 0x07064B50); // locator
        le32(central, 0);
        le64(central, end64);
        le32(central, 1);
    }
    le32(central, 0x06054B50);
    le16(central, 0);
    le16(central, 0);
    le16(central, z64End ? Max16 : quint16(done.size()));
    le16(central, z64End ? Max16 : quint16(done.size()));
    le32(central, z64End ? 0xFFFFFFFFu : quint32(directorySize));
    le32(central, z64End ? 0xFFFFFFFFu : quint32(directoryOffset));
    le16(central, 0);
    if (out.write(central) != central.size())
        return fail(QStringLiteral("cannot write the zip file"));
    out.close();
    return {};
}

}
