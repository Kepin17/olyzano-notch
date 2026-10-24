#pragma once

#include <QHostAddress>
#include <QObject>
#include <QTcpServer>
#include <QTimer>
#include <QUrl>

// Local sharing: a short-lived web server on one LAN address that offers files under an unguessable
// path (http://<ip>:<port>/<token>/). Plain HTTP, meant for the user's own network.
//   /<token>/            the download page (server/page.html with the file list filled in)
//   /<token>/f/<n>       file n (Range supported); a complete download counts
//   /<token>/f/<n>?inline=1   the same for previews: images only, never counted
// One file: the share ends after its first complete download. Several: it stays up until the time
// is up or stop(), and each complete download is reported through fileDownloaded().
class ShareServer : public QObject
{
    Q_OBJECT
public:
    explicit ShareServer(QObject *parent = nullptr);

    struct Entry {
        QString path, name, mimeType;
        qint64 size = 0; // filled in by start()
    };

    // Empty string on success, else a user-facing reason.
    QString start(const QHostAddress &address, const QList<Entry> &files, int ttlSeconds);
    void stop();
    bool running() const { return m_server.isListening(); }
    QUrl url() const { return m_url; }

    // IPv4 addresses of this machine on real networks (no loopback, containers or VMs), best first.
    static QList<QHostAddress> lanAddresses();

signals:
    void finished(const QString &reason); // "downloaded" | "expired" | "stopped"
    void fileDownloaded(int index);

private:
    friend class ShareConnection;
    void onConnection();
    void finish(const QString &reason);
    void completed(int index);
    QByteArray page() const;

    QTcpServer m_server;
    QTimer m_expire;
    QUrl m_url;
    QString m_token;
    QList<Entry> m_files;
    qint64 m_expiresAt = 0;
};
