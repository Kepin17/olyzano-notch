#include "ShareServer.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTcpSocket>

// One HTTP exchange with a phone's browser. Owned by its socket.
class ShareConnection : public QObject
{
public:
    ShareConnection(ShareServer *server, QTcpSocket *socket)
        : QObject(socket)
        , m_server(server)
        , m_socket(socket)
    {
        m_idle.setSingleShot(true);
        m_idle.setInterval(30000);
        connect(&m_idle, &QTimer::timeout, socket, &QTcpSocket::abort);
        connect(socket, &QTcpSocket::readyRead, this, &ShareConnection::onRead);
        connect(socket, &QTcpSocket::bytesWritten, this, &ShareConnection::pump);
        connect(socket, &QTcpSocket::disconnected, this, &ShareConnection::onDisconnected);
        m_idle.start();
    }

private:
    void onRead()
    {
        m_idle.start();
        m_request += m_socket->readAll();
        if (m_request.size() > 16 * 1024)
            return m_socket->abort();
        const int end = m_request.indexOf("\r\n\r\n");
        if (end < 0 || m_responded)
            return;
        m_responded = true;
        respond(QString::fromLatin1(m_request.left(end)).split(QStringLiteral("\r\n")));
    }

    void respond(const QStringList &lines)
    {
        const QStringList first = lines.value(0).split(u' ');
        const bool head = first.value(0) == QLatin1String("HEAD");
        if ((!head && first.value(0) != QLatin1String("GET")) || first.size() < 2)
            return reply(405, QStringLiteral("Method Not Allowed"), {}, head);
        const QString path = first.value(1).section(u'?', 0, 0);
        const bool inline_ = first.value(1).section(u'?', 1).split(u'&').contains(QStringLiteral("inline=1"));
        const QString base = QLatin1Char('/') + m_server->m_token + QLatin1Char('/');
        if (path == base)
            return reply(200, QStringLiteral("OK"), m_server->page(), head, QStringLiteral("text/html; charset=utf-8"),
                         {{"Content-Security-Policy", "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; "
                                                      "connect-src 'self'; img-src 'self' blob: data:; frame-ancestors 'none'"}});
        static const QRegularExpression fileRoute(QStringLiteral(R"(^f/(\d{1,4})$)"));
        const auto route = path.startsWith(base) ? fileRoute.match(path.mid(base.size())) : QRegularExpressionMatch();
        const int index = route.hasMatch() ? route.captured(1).toInt() : -1;
        if (index < 0 || index >= m_server->m_files.size())
            return reply(404, QStringLiteral("Not Found"), {}, head);
        const ShareServer::Entry &entry = m_server->m_files.at(index);
        // Previews are only for images, and sandboxed, so a sender's file can never run as a page.
        if (inline_ && !entry.mimeType.startsWith(QLatin1String("image/")))
            return reply(404, QStringLiteral("Not Found"), {}, head);
        const qint64 size = entry.size;

        qint64 start = 0, last = size - 1;
        bool partial = false;
        for (const QString &line : lines) {
            if (!line.startsWith(QLatin1String("Range:"), Qt::CaseInsensitive))
                continue;
            static const QRegularExpression re(QStringLiteral(R"(bytes=(\d*)-(\d*))"));
            const auto m = re.match(line);
            if (!m.hasMatch() || (m.captured(1).isEmpty() && m.captured(2).isEmpty()))
                break;
            if (m.captured(1).isEmpty()) {
                start = qMax<qint64>(0, size - m.captured(2).toLongLong());
            } else {
                start = m.captured(1).toLongLong();
                if (!m.captured(2).isEmpty())
                    last = qMin(last, m.captured(2).toLongLong());
            }
            partial = true;
        }
        if (size > 0 && (start > last || start >= size))
            return reply(416, QStringLiteral("Range Not Satisfiable"), QByteArray(), head, {},
                         {{"Content-Range", "bytes */" + QByteArray::number(size)}});
        m_file.setFileName(entry.path);
        if (!m_file.open(QIODevice::ReadOnly) || !m_file.seek(start))
            return reply(500, QStringLiteral("Internal Server Error"), {}, head);
        m_left = size > 0 ? last - start + 1 : 0;
        m_index = index;
        m_countsAsDownload = !inline_ && !head && last == size - 1 && start == 0 && !partial;
        const QByteArray disposition = (inline_ ? "inline" : "attachment; filename*=UTF-8''" + QUrl::toPercentEncoding(entry.name));
        QList<QPair<QByteArray, QByteArray>> extra{{"Content-Disposition", disposition}, {"Accept-Ranges", "bytes"},
                                                   {"Content-Security-Policy", "default-src 'none'; sandbox"}};
        if (partial)
            extra << qMakePair(QByteArray("Content-Range"),
                               "bytes " + QByteArray::number(start) + "-" + QByteArray::number(last) + "/" + QByteArray::number(size));
        header(partial ? 206 : 200, partial ? QStringLiteral("Partial Content") : QStringLiteral("OK"), m_left,
               entry.mimeType.isEmpty() ? QStringLiteral("application/octet-stream") : entry.mimeType, extra);
        if (head || m_left == 0) {
            m_left = 0;
            m_socket->disconnectFromHost();
            if (!head && m_countsAsDownload) // an empty file: nothing to send, so it is complete
                m_server->completed(m_index);
            m_countsAsDownload = false;
        } else {
            pump();
        }
    }

    void header(int code, const QString &text, qint64 length, const QString &type,
                const QList<QPair<QByteArray, QByteArray>> &extra = {})
    {
        QByteArray h = "HTTP/1.1 " + QByteArray::number(code) + " " + text.toLatin1() + "\r\nContent-Type: " + type.toLatin1()
                       + "\r\nContent-Length: " + QByteArray::number(length)
                       + "\r\nCache-Control: no-store\r\nReferrer-Policy: no-referrer\r\nX-Content-Type-Options: nosniff"
                         "\r\nConnection: close\r\n";
        for (const auto &e : extra)
            h += e.first + ": " + e.second + "\r\n";
        m_socket->write(h + "\r\n");
    }

    void reply(int code, const QString &text, const QByteArray &body, bool head,
               const QString &type = QStringLiteral("text/plain"),
               const QList<QPair<QByteArray, QByteArray>> &extra = {})
    {
        header(code, text, body.size(), type, extra);
        if (!head)
            m_socket->write(body);
        m_socket->disconnectFromHost();
    }

    // Streams the file with back-pressure, so a big file never sits in memory.
    void pump()
    {
        m_idle.start();
        while (m_left > 0 && m_socket->bytesToWrite() < 512 * 1024) {
            const QByteArray chunk = m_file.read(qMin<qint64>(m_left, 256 * 1024));
            if (chunk.isEmpty())
                return m_socket->abort();
            m_socket->write(chunk);
            m_left -= chunk.size();
        }
        if (m_left == 0 && m_file.isOpen()) {
            m_file.close();
            m_socket->disconnectFromHost();
        }
    }

    void onDisconnected()
    {
        // Counted only when every byte of the file went out (not when the phone gave up midway).
        if (m_countsAsDownload && m_left == 0 && m_socket->bytesToWrite() == 0)
            m_server->completed(m_index);
        m_socket->deleteLater();
    }

    ShareServer *m_server;
    QTcpSocket *m_socket;
    QTimer m_idle;
    QByteArray m_request;
    QFile m_file;
    qint64 m_left = 0;
    int m_index = -1;
    bool m_responded = false;
    bool m_countsAsDownload = false;
};

ShareServer::ShareServer(QObject *parent)
    : QObject(parent)
{
    connect(&m_server, &QTcpServer::newConnection, this, &ShareServer::onConnection);
    m_expire.setSingleShot(true);
    connect(&m_expire, &QTimer::timeout, this, [this] { finish(QStringLiteral("expired")); });
}

QString ShareServer::start(const QHostAddress &address, const QList<Entry> &files, int ttlSeconds)
{
    stop();
    if (files.isEmpty())
        return tr("Nothing to share");
    QList<Entry> list = files;
    for (Entry &e : list) {
        const QFileInfo info(e.path);
        if (!info.isFile() || !info.isReadable())
            return tr("Cannot read \"%1\"").arg(e.name);
        e.size = info.size();
    }
    if (!m_server.listen(address, 0))
        return m_server.errorString();
    QByteArray raw(16, 0);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(raw.data()), raw.size() / 4);
    m_token = QString::fromLatin1(raw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    m_files = list;
    m_url = QUrl(QStringLiteral("http://%1:%2/%3/").arg(address.toString()).arg(m_server.serverPort()).arg(m_token));
    m_expiresAt = QDateTime::currentSecsSinceEpoch() + ttlSeconds;
    m_expire.start(ttlSeconds * 1000);
    return {};
}

// The download page: server/page.html (shared with the relay) with this share's file list filled in.
QByteArray ShareServer::page() const
{
    QFile f(QStringLiteral(":/share/page.html"));
    if (!f.open(QIODevice::ReadOnly))
        return "Notch page missing";
    QJsonArray files;
    for (const Entry &e : m_files)
        files.append(QJsonObject{{QStringLiteral("name"), e.name},
                                 {QStringLiteral("type"), e.mimeType},
                                 {QStringLiteral("size"), double(e.size)}});
    const QJsonObject data{{QStringLiteral("mode"), QStringLiteral("local")},
                           {QStringLiteral("files"), files},
                           {QStringLiteral("expires_at"), double(m_expiresAt)},
                           {QStringLiteral("now"), double(QDateTime::currentSecsSinceEpoch())}};
    // "<" never appears raw inside the tag, so no file name can close the script element.
    QByteArray json = QJsonDocument(data).toJson(QJsonDocument::Compact).replace('<', "\\u003c");
    QByteArray html = f.readAll();
    static const QByteArray marker = R"(<script id="share-data" type="application/json">null</script>)";
    return html.replace(marker, R"(<script id="share-data" type="application/json">)" + json + "</script>");
}

void ShareServer::completed(int index)
{
    emit fileDownloaded(index);
    if (m_files.size() == 1)
        finish(QStringLiteral("downloaded"));
}

void ShareServer::stop()
{
    finish(QStringLiteral("stopped"));
}

void ShareServer::onConnection()
{
    while (QTcpSocket *socket = m_server.nextPendingConnection())
        new ShareConnection(this, socket);
}

void ShareServer::finish(const QString &reason)
{
    if (!m_server.isListening())
        return;
    m_server.close();
    m_expire.stop();
    if (reason != QLatin1String("downloaded")) // let the finishing download complete
        for (QTcpSocket *s : m_server.findChildren<QTcpSocket *>())
            s->abort();
    emit finished(reason);
}

QList<QHostAddress> ShareServer::lanAddresses()
{
    QList<QHostAddress> wifi, other;
    static const QStringList virtualPrefixes{QStringLiteral("docker"), QStringLiteral("br-"), QStringLiteral("veth"),
                                             QStringLiteral("virbr"), QStringLiteral("vmnet"), QStringLiteral("tun"),
                                             QStringLiteral("tailscale")};
    for (const QNetworkInterface &nic : QNetworkInterface::allInterfaces()) {
        const auto flags = nic.flags();
        if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::IsRunning)
            || (flags & QNetworkInterface::IsLoopBack))
            continue;
        bool virt = false;
        for (const QString &p : virtualPrefixes)
            virt = virt || nic.name().startsWith(p);
        if (virt)
            continue;
        for (const QNetworkAddressEntry &e : nic.addressEntries())
            if (e.ip().protocol() == QAbstractSocket::IPv4Protocol && !e.ip().isLoopback()
                && !e.ip().isInSubnet(QHostAddress(QStringLiteral("169.254.0.0")), 16))
                (nic.name().startsWith(u'w') ? wifi : other) << e.ip();
    }
    return wifi + other;
}
