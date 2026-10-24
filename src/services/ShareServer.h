#pragma once

#include <QHash>
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
//   /<token>/auth, /<token>/list   with a PIN or password: check it / the file list (not in the page)
// A PIN or password (header X-Pin) gates the list and every file; 5 wrong tries lock that address out for
// 10 minutes.
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
    // protection: "" | "pin" | "password" (what the page asks for); secret: the PIN / password.
    QString start(const QHostAddress &address, const QList<Entry> &files, int ttlSeconds,
                  const QString &protection = {}, const QString &secret = {});
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
    QByteArray fileList() const;
    int check(const QString &peer, const QString &secret); // 200 ok, 403 wrong, 429 locked out

    QTcpServer m_server;
    QTimer m_expire;
    QUrl m_url;
    QString m_token, m_protection, m_secret;
    QHash<QString, QPair<int, qint64>> m_fails; // peer -> wrong tries, locked until (epoch secs)
    QList<Entry> m_files;
    qint64 m_expiresAt = 0;
};
