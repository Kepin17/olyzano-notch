#pragma once

#include "services/ShareCrypto.h"

#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QPointer>
#include <QTcpServer>
#include <QTimer>
#include <QUdpSocket>
#include <QVariantMap>

class NearbyLink;

// Nearby share: computers on one network find each other and send files after both people compare a code
// (see NearbyCrypto.h for the cryptography). Pure Qt Network, the same on Linux and Windows.
//
//   Receiving is on only for a while (startReceiving): a UDP socket answers "who is there?" probes and a TCP
//   server takes the transfer. Sending: probe() finds the receivers, send() talks to one of them.
//
// A transfer, from either side, is a "transfer id" with a state: connecting, waiting (the code is known and the
// receiver decides), asking (receiver side: the person decides), sending / receiving, then one of done, declined
// or failed. transfer(id) returns {id, direction in|out, peer, state, code, progress 0..1, total, files, message}.
class NearbyService : public QObject
{
    Q_OBJECT
public:
    static constexpr quint16 DiscoveryPort = 47655;
    struct Device {
        QString id, name, os;
        QHostAddress address;
        quint16 port = 0;
    };

    explicit NearbyService(const QString &workRoot, QObject *parent = nullptr);
    ~NearbyService() override;

    void setDevice(const QString &id, const QString &name);
    QString deviceId() const { return m_id; }
    QString deviceName() const { return m_name; }

    // -- receiving. Empty string when it started, else why not.
    QString startReceiving(int seconds, quint16 discoveryPort = DiscoveryPort);
    void stopReceiving();
    bool receiving() const { return m_server.isListening(); }
    qint64 receiveUntil() const { return m_until; } // epoch seconds, 0 when off
    quint16 discoveryPort() const { return m_udp.localPort(); }
    quint16 transferPort() const { return m_server.serverPort(); }
    void accept(const QString &transferId);
    void decline(const QString &transferId);

    // -- sending
    void probe(const QList<QHostAddress> &targets = {}, quint16 port = DiscoveryPort); // no targets: broadcast
    QString send(const Device &to, const QList<ShareCrypto::SourceFile> &files);        // the transfer id
    void cancel(const QString &transferId);

    QVariantMap transfer(const QString &id) const;
    QStringList transferIds() const { return m_info.keys(); }

    static QByteArray frame(const QJsonObject &message); // 4-byte length + JSON

signals:
    void deviceFound(const QVariantMap &device); // {id, name, os, address, port}
    void receivingChanged();
    // Someone wants to send: show it and call accept() or decline().
    void incoming(const QString &transferId, const QString &from, const QVariantList &files, qint64 total,
                  const QString &code, bool lowDisk);
    void transferChanged(const QString &transferId);
    // Files arrived and are decrypted in `folder` (the caller moves them in; the folder is removed right after).
    void received(const QString &folder, const QStringList &names, const QString &from);

private:
    friend class NearbyLink;
    void onProbe();
    void onReply();
    void onConnection();
    void update(const QString &id, const QVariantMap &changes);
    bool blocked(const QHostAddress &peer) const;
    void failed(const QHostAddress &peer); // a declined or broken request counts against that address

    QString m_id, m_name, m_work;
    QTcpServer m_server;
    QUdpSocket m_udp;    // receiving: answers probes
    QUdpSocket m_finder; // sending: asks, and hears the answers
    QTimer m_stop;
    qint64 m_until = 0;
    QHash<QString, QVariantMap> m_info;
    QHash<QString, QPointer<NearbyLink>> m_links;
    QPointer<NearbyLink> m_incoming; // one transfer at a time
    QHash<QString, QPair<int, qint64>> m_strikes; // peer -> failed requests, blocked until (epoch secs)
};
