#pragma once

#include <QHostAddress>
#include <QObject>
#include <QTcpServer>
#include <QTimer>
#include <QUrl>

// Local sharing: a short-lived web server on one LAN address that offers a single file under an
// unguessable path (http://<ip>:<port>/<token>/). Plain HTTP, meant for the user's own network.
// It stops after the first complete download, when the time is up, or on stop(). Supports Range.
class ShareServer : public QObject
{
    Q_OBJECT
public:
    explicit ShareServer(QObject *parent = nullptr);

    // Empty string on success, else a user-facing reason.
    QString start(const QHostAddress &address, const QString &filePath, const QString &fileName,
                  const QString &mimeType, int ttlSeconds);
    void stop();
    bool running() const { return m_server.isListening(); }
    QUrl url() const { return m_url; }

    // IPv4 addresses of this machine on real networks (no loopback, containers or VMs), best first.
    static QList<QHostAddress> lanAddresses();

signals:
    void finished(const QString &reason); // "downloaded" | "expired" | "stopped"

private:
    friend class ShareConnection;
    void onConnection();
    void finish(const QString &reason);

    QTcpServer m_server;
    QTimer m_expire;
    QUrl m_url;
    QString m_token, m_path, m_name, m_mime;
    qint64 m_size = 0;
};
