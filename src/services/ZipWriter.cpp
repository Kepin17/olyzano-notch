#include "ZipWriter.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QtEndian>

namespace ZipWriter {
namespace {
constexpr qint64 Chunk = 1 << 20;
constexpr quint64 Limit = 0xFFFFFFFFull;

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
    quint32 crc, size, offset;
    quint16 time, date;
};
}

QString write(const QString &outPath, const QList<Entry> &files)
{
    if (files.isEmpty() || files.size() > 0xFFFF)
        return QStringLiteral("a zip holds 1 to 65535 files");
    QFile out(outPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QStringLiteral("cannot write the zip file");

    QList<Done> done;
    quint64 offset = 0;
    const quint32 *table = crcTable();
    for (const Entry &e : files) {
        const QFileInfo info(e.path);
        if (!info.isFile() || !info.isReadable())
            return QStringLiteral("cannot read \"%1\"").arg(e.name);
        if (quint64(info.size()) > Limit || offset + quint64(info.size()) + 1024 > Limit)
            return QStringLiteral("the files are too big for one zip (4 GB)");
        QFile in(e.path);
        if (!in.open(QIODevice::ReadOnly))
            return QStringLiteral("cannot read \"%1\"").arg(e.name);

        quint32 crc = 0xFFFFFFFFu;
        qint64 total = 0;
        for (QByteArray chunk = in.read(Chunk); !chunk.isEmpty(); chunk = in.read(Chunk)) {
            for (unsigned char c : chunk)
                crc = table[(crc ^ c) & 0xFF] ^ (crc >> 8);
            total += chunk.size();
        }
        crc ^= 0xFFFFFFFFu;
        if (total != info.size() || !in.seek(0))
            return QStringLiteral("\"%1\" changed while zipping").arg(e.name);

        Done d{e.name.toUtf8(), crc, quint32(total), quint32(offset), 0, 0};
        dosStamp(info.lastModified(), &d.time, &d.date);
        QByteArray header;
        le32(header, 0x04034B50);
        le16(header, 20);
        le16(header, 0x0800); // names are UTF-8
        le16(header, 0);      // stored
        le16(header, d.time);
        le16(header, d.date);
        le32(header, d.crc);
        le32(header, d.size);
        le32(header, d.size);
        le16(header, quint16(d.name.size()));
        le16(header, 0);
        header.append(d.name);
        if (out.write(header) != header.size())
            return QStringLiteral("cannot write the zip file");
        for (QByteArray chunk = in.read(Chunk); !chunk.isEmpty(); chunk = in.read(Chunk))
            if (out.write(chunk) != chunk.size())
                return QStringLiteral("cannot write the zip file");
        offset += quint64(header.size()) + d.size;
        done.append(d);
    }

    QByteArray central;
    for (const Done &d : done) {
        le32(central, 0x02014B50);
        le16(central, 20);
        le16(central, 20);
        le16(central, 0x0800);
        le16(central, 0);
        le16(central, d.time);
        le16(central, d.date);
        le32(central, d.crc);
        le32(central, d.size);
        le32(central, d.size);
        le16(central, quint16(d.name.size()));
        le16(central, 0); // extra
        le16(central, 0); // comment
        le16(central, 0); // disk
        le16(central, 0); // internal attributes
        le32(central, 0); // external attributes
        le32(central, d.offset);
        central.append(d.name);
    }
    const quint32 directorySize = quint32(central.size());
    le32(central, 0x06054B50);
    le16(central, 0);
    le16(central, 0);
    le16(central, quint16(done.size()));
    le16(central, quint16(done.size()));
    le32(central, directorySize);
    le32(central, quint32(offset));
    le16(central, 0);
    if (offset + quint64(central.size()) > Limit || out.write(central) != central.size())
        return QStringLiteral("cannot write the zip file");
    out.close();
    return {};
}

}
