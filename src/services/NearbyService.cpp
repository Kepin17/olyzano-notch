#include "NearbyService.h"

#include "services/NearbyCrypto.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QStorageInfo>
#include <QSysInfo>
#include <QTcpSocket>
#include <QUuid>
#include <QtConcurrent>
#include <QtEndian>

namespace {
constexpr int MaxFrame = 1024 * 1024;
constexpr int MaxFiles = 500;
constexpr int AskSeconds = 60;
constexpr int AcceptWaitSeconds = 90;
constexpr int StrikesToBlock = 3;
constexpr int BlockSeconds = 300;

QByteArray b64(const QByteArray &b) { return b.toBase64(); }
QByteArray unb64(const QJsonValue &v) { return QByteArray::fromBase64(v.toString().toLatin1()); }

bool sameBytes(const QByteArray &a, const QByteArray &b)
{
    int diff = a.size() ^ b.size();
    for (int i = 0; i < a.size() && i < b.size(); ++i)
        diff |= a.at(i) ^ b.at(i);
    return diff == 0;
}

QString osName()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_LINUX)
    return QStringLiteral("linux");
#else
    return QStringLiteral("other");
#endif
}
}

// One transfer on one TCP connection, either side. Owns its socket; deletes itself when it is over.
class NearbyLink : public QObject
{
public:
    enum Role { Sender, Receiver };

    NearbyLink(NearbyService *svc, Role role, QTcpSocket *socket, const QString &id)
        : QObject(svc)
        , m_svc(svc)
        , m_role(role)
        , m_socket(socket)
        , m_id(id)
    {
        socket->setParent(this);
        connect(socket, &QTcpSocket::readyRead, this, &NearbyLink::onRead);
        connect(socket, &QTcpSocket::bytesWritten, this, &NearbyLink::pump);
        connect(socket, &QTcpSocket::disconnected, this, [this] {
            if (!m_over)
                end(QStringLiteral("failed"), tr("The other computer closed the connection"), true);
        });
        m_timer.setSingleShot(true);
        connect(&m_timer, &QTimer::timeout, this, [this] { end(QStringLiteral("failed"), tr("Timed out"), m_role == Receiver); });
        m_peer = socket->peerAddress();
    }

    // ---- sending ------------------------------------------------------------------------------
    void beginSend(const NearbyService::Device &to, const QList<ShareCrypto::SourceFile> &files)
    {
        m_files = files;
        QJsonArray list;
        for (const auto &f : files)
            list.append(QJsonObject{{"name", f.name}, {"size", double(QFileInfo(f.path).size())}, {"type", f.mimeType}});
        m_filesJson = QJsonDocument(list).toJson(QJsonDocument::Compact);
        m_keys = NearbyCrypto::generate();
        if (m_keys.pub.isNull())
            return end(QStringLiteral("failed"), tr("No cryptography available"), false);
        m_total = 0;
        for (const auto &f : files)
            m_total += QFileInfo(f.path).size();
        m_peerName = to.name;
        m_step = WaitKey;
        m_svc->update(m_id, {{"peer", to.name}, {"state", "connecting"}, {"total", m_total}, {"files", int(files.size())}});
        m_timer.start(15000);
        connect(m_socket, &QTcpSocket::connected, this, [this] {
            send({{"t", "hello"}, {"v", 1}, {"id", m_svc->m_id}, {"name", m_svc->m_name},
                  {"filesJson", QString::fromUtf8(m_filesJson)}, {"commit", QString::fromLatin1(b64(NearbyCrypto::commitment(m_keys.pub)))}});
        });
        connect(m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
            if (!m_over && m_socket->state() != QAbstractSocket::ConnectedState)
                end(QStringLiteral("failed"), tr("Cannot reach the other computer"), false);
        });
        m_socket->connectToHost(to.address, to.port);
    }

    // ---- receiving ----------------------------------------------------------------------------
    void accept()
    {
        if (m_role != Receiver || m_step != Asking)
            return;
        m_step = ExpectData;
        send({{"t", "accept"}, {"proof", QString::fromLatin1(b64(NearbyCrypto::acceptProof(m_z, m_transcript)))}});
        m_svc->update(m_id, {{"state", "receiving"}});
        m_timer.start(60000);
    }
    void decline()
    {
        if (m_role == Receiver && m_step == Asking) {
            send({{"t", "decline"}});
            end(QStringLiteral("declined"), tr("Declined"), true);
        }
    }
    void cancel() { end(QStringLiteral("failed"), tr("Cancelled"), false); }

    void adopt() // a receiver link that the server just accepted
    {
        m_svc->update(m_id, {{"peer", m_peer.toString()}, {"state", "connecting"}});
        m_timer.start(15000);
    }

private:
    enum Step { Start, WaitKey, WaitAccept, Asking, ExpectReveal, ExpectData, Sending, Finished };

    void send(const QJsonObject &message) { m_socket->write(NearbyService::frame(message)); }

    void end(const QString &state, const QString &message, bool strike)
    {
        if (m_over)
            return;
        m_over = true;
        m_timer.stop();
        if (m_role == Receiver && m_svc->m_incoming == this)
            m_svc->m_incoming = nullptr;
        if (strike)
            m_svc->failed(m_peer);
        m_svc->update(m_id, {{"state", state}, {"message", message}});
        if (m_out.isOpen())
            m_out.close();
        // Only our own work folder, and only once it exists: QDir("") means "the current directory".
        if (!m_dir.isEmpty())
            QDir(m_dir).removeRecursively();
        if (m_socket->state() == QAbstractSocket::ConnectedState && m_socket->bytesToWrite() > 0) {
            // A last message (declined, done) is still on its way: let it out before closing.
            m_socket->disconnectFromHost();
            connect(m_socket, &QTcpSocket::disconnected, this, &QObject::deleteLater);
            QTimer::singleShot(3000, this, &QObject::deleteLater);
        } else {
            m_socket->abort();
            deleteLater();
        }
    }

    void onRead()
    {
        m_buffer += m_socket->readAll();
        for (;;) {
            if (m_left > 0) { // raw bytes of the encrypted files
                const qint64 n = qMin<qint64>(m_left, m_buffer.size());
                if (n == 0)
                    break;
                if (m_out.write(m_buffer.constData(), n) != n)
                    return end(QStringLiteral("failed"), tr("Cannot write the received files"), false);
                m_buffer.remove(0, int(n));
                m_left -= n;
                m_got += n;
                m_svc->update(m_id, {{"progress", double(m_got) / qMax<qint64>(1, m_size)}});
                m_timer.start(60000);
                if (m_left == 0)
                    return finishReceiving();
                break;
            }
            if (m_buffer.size() < 4)
                break;
            const quint32 len = qFromBigEndian<quint32>(m_buffer.constData());
            if (len > quint32(MaxFrame))
                return end(QStringLiteral("failed"), tr("Unexpected data"), true);
            if (m_buffer.size() < int(4 + len))
                break;
            const QJsonObject msg = QJsonDocument::fromJson(m_buffer.mid(4, int(len))).object();
            m_buffer.remove(0, int(4 + len));
            handle(msg);
            if (m_over)
                return;
        }
    }

    void handle(const QJsonObject &m)
    {
        const QString t = m.value("t").toString();
        if (t == "busy")
            return end(QStringLiteral("failed"), tr("The other computer is busy"), false);
        if (m_role == Receiver) {
            if (t == "hello" && m_step == Start)
                return onHello(m);
            if (t == "reveal" && m_step == ExpectReveal)
                return onReveal(m);
            if (t == "data" && m_step == ExpectData)
                return onData(m);
            return end(QStringLiteral("failed"), tr("Unexpected message"), true);
        }
        if (t == "key" && m_step == WaitKey)
            return onKey(m);
        if (t == "accept" && m_step == WaitAccept)
            return onAccept(m);
        if (t == "decline" && m_step == WaitAccept)
            return end(QStringLiteral("declined"), tr("The other computer declined"), false);
        if (t == "done" && m_step == Sending) {
            m_step = Finished;
            return end(m.value("ok").toBool() ? QStringLiteral("done") : QStringLiteral("failed"),
                       m.value("ok").toBool() ? tr("Sent") : tr("The other computer could not open the files"), false);
        }
        end(QStringLiteral("failed"), tr("Unexpected message"), false);
    }

    // ---- receiver steps ----
    void onHello(const QJsonObject &m)
    {
        const QByteArray commit = unb64(m.value("commit"));
        m_filesJson = m.value("filesJson").toString().toUtf8();
        const QJsonArray list = QJsonDocument::fromJson(m_filesJson).array();
        if (m.value("v").toInt() != 1 || commit.size() != 32 || list.isEmpty() || list.size() > MaxFiles)
            return end(QStringLiteral("failed"), tr("Not a valid request"), true);
        m_total = 0;
        QVariantList files;
        for (const QJsonValue &v : list) {
            const QJsonObject f = v.toObject();
            const qint64 size = f.value("size").toVariant().toLongLong();
            if (f.value("name").toString().isEmpty() || size < 0)
                return end(QStringLiteral("failed"), tr("Not a valid request"), true);
            m_total += size;
            files.append(QVariantMap{{"name", f.value("name").toString()}, {"size", size}});
        }
        m_peerName = m.value("name").toString().left(60);
        m_commit = commit;
        m_filesVariant = files;
        m_keys = NearbyCrypto::generate();
        if (m_keys.pub.isNull())
            return end(QStringLiteral("failed"), tr("No cryptography available"), false);
        m_step = ExpectReveal;
        send({{"t", "key"}, {"pub", QString::fromLatin1(b64(m_keys.pub))}, {"name", m_svc->m_name}});
        m_svc->update(m_id, {{"peer", m_peerName}, {"total", m_total}, {"files", int(files.size())}});
    }

    void onReveal(const QJsonObject &m)
    {
        const QByteArray pub = unb64(m.value("pub"));
        if (pub.size() != 32 || !sameBytes(NearbyCrypto::commitment(pub), m_commit))
            return end(QStringLiteral("failed"), tr("The other computer did not match its own request"), true);
        m_z = NearbyCrypto::agree(m_keys.priv, pub);
        if (m_z.isNull())
            return end(QStringLiteral("failed"), tr("Key exchange failed"), true);
        m_transcript = NearbyCrypto::transcript(pub, m_keys.pub, m_filesJson);
        m_code = NearbyCrypto::code(m_z, m_transcript);
        m_step = Asking;
        m_dir = m_svc->m_work + QStringLiteral("/nearby/") + m_id;
        QDir().mkpath(m_dir);
        const bool lowDisk = QStorageInfo(m_dir).bytesAvailable() < m_total * 2 + (64 << 20);
        m_svc->update(m_id, {{"state", "asking"}, {"code", m_code}});
        m_timer.start(AskSeconds * 1000);
        emit m_svc->incoming(m_id, m_peerName, m_filesVariant, m_total, m_code, lowDisk);
    }

    void onData(const QJsonObject &m)
    {
        m_size = m.value("size").toVariant().toLongLong();
        if (m_size <= 0 || m_size > m_total + (1 << 20) + qint64(m_filesVariant.size()) * 64)
            return end(QStringLiteral("failed"), tr("Unexpected size"), true);
        m_out.setFileName(m_dir + QStringLiteral("/in.enc"));
        if (!m_out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return end(QStringLiteral("failed"), tr("Cannot write the received files"), false);
        m_left = m_size;
        m_got = 0;
        if (!m_buffer.isEmpty())
            QMetaObject::invokeMethod(this, &NearbyLink::onRead, Qt::QueuedConnection);
    }

    void finishReceiving()
    {
        m_out.close();
        m_step = Finished;
        m_timer.stop();
        m_svc->update(m_id, {{"state", "receiving"}, {"progress", 1.0}});
        auto *watcher = new QFutureWatcher<QPair<QString, QStringList>>(this);
        const QString in = m_dir + QStringLiteral("/in.enc"), outDir = m_dir + QStringLiteral("/files");
        const QByteArray key = NearbyCrypto::sessionKey(m_z, m_transcript);
        connect(watcher, &QFutureWatcher<QPair<QString, QStringList>>::finished, this, [this, watcher, outDir] {
            const auto result = watcher->result();
            watcher->deleteLater();
            if (m_over)
                return;
            if (!result.first.isEmpty()) { // wrong key or damaged: a stream someone tampered with ends here
                send({{"t", "done"}, {"ok", false}});
                return end(QStringLiteral("failed"), tr("The files were damaged on the way"), true);
            }
            emit m_svc->received(outDir, result.second, m_peerName); // synchronous: the slot moves the files out
            send({{"t", "done"}, {"ok", true}});
            end(QStringLiteral("done"), tr("Received"), false);
        });
        watcher->setFuture(QtConcurrent::run([in, outDir, key] {
            QStringList names;
            const QString why = ShareCrypto::decryptFiles(in, outDir, key, &names);
            return qMakePair(why, names);
        }));
    }

    // ---- sender steps ----
    void onKey(const QJsonObject &m)
    {
        const QByteArray pub = unb64(m.value("pub"));
        if (pub.size() != 32)
            return end(QStringLiteral("failed"), tr("Not a valid answer"), false);
        m_z = NearbyCrypto::agree(m_keys.priv, pub);
        if (m_z.isNull())
            return end(QStringLiteral("failed"), tr("Key exchange failed"), false);
        m_transcript = NearbyCrypto::transcript(m_keys.pub, pub, m_filesJson);
        m_code = NearbyCrypto::code(m_z, m_transcript);
        m_step = WaitAccept;
        send({{"t", "reveal"}, {"pub", QString::fromLatin1(b64(m_keys.pub))}});
        m_svc->update(m_id, {{"state", "waiting"}, {"code", m_code}});
        m_timer.start(AcceptWaitSeconds * 1000);
    }

    void onAccept(const QJsonObject &m)
    {
        if (!sameBytes(unb64(m.value("proof")), NearbyCrypto::acceptProof(m_z, m_transcript)))
            return end(QStringLiteral("failed"), tr("The other computer could not prove the code"), false);
        m_step = Sending;
        m_timer.start(60000);
        m_svc->update(m_id, {{"state", "sending"}, {"progress", 0.0}});
        m_dir = m_svc->m_work + QStringLiteral("/nearby/") + m_id;
        QDir().mkpath(m_dir);
        auto *watcher = new QFutureWatcher<QString>(this);
        const auto files = m_files;
        const QString enc = m_dir + QStringLiteral("/out.enc");
        const QByteArray key = NearbyCrypto::sessionKey(m_z, m_transcript);
        connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, enc] {
            const QString why = watcher->result();
            watcher->deleteLater();
            if (m_over)
                return;
            m_out.setFileName(enc);
            if (!why.isEmpty() || !m_out.open(QIODevice::ReadOnly))
                return end(QStringLiteral("failed"), why.isEmpty() ? tr("Cannot read the files") : why, false);
            m_size = m_out.size();
            m_left = m_size;
            m_got = 0;
            send({{"t", "data"}, {"size", double(m_size)}});
            pump();
        });
        watcher->setFuture(QtConcurrent::run([files, enc, key] { return ShareCrypto::encryptFiles(files, enc, key); }));
    }

    // Streams the encrypted file with back-pressure, so a big transfer never sits in memory.
    void pump()
    {
        if (m_role != Sender || m_step != Sending || !m_out.isOpen())
            return;
        m_timer.start(60000);
        while (m_left > 0 && m_socket->bytesToWrite() < 512 * 1024) {
            const QByteArray chunk = m_out.read(qMin<qint64>(m_left, 256 * 1024));
            if (chunk.isEmpty())
                return end(QStringLiteral("failed"), tr("Cannot read the files"), false);
            m_socket->write(chunk);
            m_left -= chunk.size();
            m_got += chunk.size();
        }
        m_svc->update(m_id, {{"progress", double(m_got - m_socket->bytesToWrite()) / qMax<qint64>(1, m_size)}});
    }

    NearbyService *m_svc;
    Role m_role;
    QTcpSocket *m_socket;
    QString m_id, m_peerName, m_dir, m_code;
    QHostAddress m_peer;
    QTimer m_timer;
    QByteArray m_buffer, m_filesJson, m_commit, m_z, m_transcript;
    NearbyCrypto::KeyPair m_keys;
    QList<ShareCrypto::SourceFile> m_files;
    QVariantList m_filesVariant;
    QFile m_out;
    qint64 m_total = 0, m_size = 0, m_left = 0, m_got = 0;
    Step m_step = Start;
    bool m_over = false;

    friend class NearbyService;
};

// ---- NearbyService ----------------------------------------------------------------------------

NearbyService::NearbyService(const QString &workRoot, QObject *parent)
    : QObject(parent)
    , m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
    , m_name(QSysInfo::machineHostName())
    , m_work(workRoot)
{
    m_stop.setSingleShot(true);
    connect(&m_stop, &QTimer::timeout, this, &NearbyService::stopReceiving);
    connect(&m_server, &QTcpServer::newConnection, this, &NearbyService::onConnection);
    connect(&m_udp, &QUdpSocket::readyRead, this, &NearbyService::onProbe);
    connect(&m_finder, &QUdpSocket::readyRead, this, &NearbyService::onReply);
}

NearbyService::~NearbyService()
{
    stopReceiving();
    for (const auto &link : std::as_const(m_links))
        if (link)
            link->cancel();
}

void NearbyService::setDevice(const QString &id, const QString &name)
{
    if (!id.isEmpty())
        m_id = id;
    if (!name.trimmed().isEmpty())
        m_name = name.trimmed().left(60);
}

QByteArray NearbyService::frame(const QJsonObject &message)
{
    const QByteArray json = QJsonDocument(message).toJson(QJsonDocument::Compact);
    QByteArray out(4, 0);
    qToBigEndian<quint32>(json.size(), out.data());
    return out + json;
}

QString NearbyService::startReceiving(int seconds, quint16 discoveryPort)
{
    stopReceiving();
    if (!m_server.listen(QHostAddress::AnyIPv4, 0))
        return m_server.errorString();
    // Shared and reusable: another program (or a second Notch on this computer) may hold the port too.
    if (!m_udp.bind(QHostAddress::AnyIPv4, discoveryPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        const QString why = m_udp.errorString();
        m_server.close();
        return why;
    }
    m_until = QDateTime::currentSecsSinceEpoch() + seconds;
    m_stop.start(seconds * 1000);
    emit receivingChanged();
    return {};
}

void NearbyService::stopReceiving()
{
    if (!m_server.isListening() && m_until == 0)
        return;
    m_stop.stop();
    m_server.close();
    m_udp.close();
    m_until = 0;
    emit receivingChanged();
}

void NearbyService::onProbe()
{
    while (m_udp.hasPendingDatagrams()) {
        const QNetworkDatagram d = m_udp.receiveDatagram();
        const QJsonObject q = QJsonDocument::fromJson(d.data()).object();
        if (q.value("app").toString() != QLatin1String("notch-nearby") || q.value("q").toString() != QLatin1String("probe")
            || q.value("id").toString() == m_id || blocked(d.senderAddress()))
            continue;
        const QJsonObject here{{"app", "notch-nearby"}, {"a", "here"}, {"v", 1}, {"id", m_id}, {"name", m_name},
                               {"os", osName()}, {"port", int(m_server.serverPort())}};
        m_udp.writeDatagram(QJsonDocument(here).toJson(QJsonDocument::Compact), d.senderAddress(), quint16(d.senderPort()));
    }
}

void NearbyService::probe(const QList<QHostAddress> &targets, quint16 port)
{
    QList<QHostAddress> to = targets;
    if (to.isEmpty()) {
        to << QHostAddress::Broadcast;
        for (const QNetworkInterface &nic : QNetworkInterface::allInterfaces())
            if ((nic.flags() & QNetworkInterface::IsUp) && (nic.flags() & QNetworkInterface::IsRunning)
                && !(nic.flags() & QNetworkInterface::IsLoopBack))
                for (const QNetworkAddressEntry &e : nic.addressEntries())
                    if (e.ip().protocol() == QAbstractSocket::IPv4Protocol && !e.broadcast().isNull())
                        to << e.broadcast();
    }
    const QByteArray ask = QJsonDocument(QJsonObject{{"app", "notch-nearby"}, {"q", "probe"}, {"v", 1}, {"id", m_id}})
                               .toJson(QJsonDocument::Compact);
    for (const QHostAddress &a : to)
        m_finder.writeDatagram(ask, a, port);
}

void NearbyService::onReply()
{
    while (m_finder.hasPendingDatagrams()) {
        const QNetworkDatagram d = m_finder.receiveDatagram();
        const QJsonObject a = QJsonDocument::fromJson(d.data()).object();
        if (a.value("app").toString() != QLatin1String("notch-nearby") || a.value("a").toString() != QLatin1String("here")
            || a.value("id").toString() == m_id || a.value("port").toInt() <= 0)
            continue;
        bool ok = false;
        const quint32 v4 = d.senderAddress().toIPv4Address(&ok);
        emit deviceFound({{"id", a.value("id").toString()},
                          {"name", a.value("name").toString().left(60)},
                          {"os", a.value("os").toString()},
                          {"address", ok ? QHostAddress(v4).toString() : d.senderAddress().toString()},
                          {"port", a.value("port").toInt()}});
    }
}

void NearbyService::onConnection()
{
    while (QTcpSocket *socket = m_server.nextPendingConnection()) {
        if (blocked(socket->peerAddress())) {
            socket->abort();
            socket->deleteLater();
            continue;
        }
        if (m_incoming) { // one at a time
            socket->write(frame({{"t", "busy"}}));
            socket->disconnectFromHost();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            continue;
        }
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto *link = new NearbyLink(this, NearbyLink::Receiver, socket, id);
        m_links.insert(id, link);
        m_incoming = link;
        m_info.insert(id, {{"id", id}, {"direction", "in"}});
        link->adopt();
    }
}

QString NearbyService::send(const Device &to, const QList<ShareCrypto::SourceFile> &files)
{
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_info.insert(id, {{"id", id}, {"direction", "out"}, {"peer", to.name}, {"state", "connecting"}, {"progress", 0.0}});
    auto *link = new NearbyLink(this, NearbyLink::Sender, new QTcpSocket, id);
    m_links.insert(id, link);
    link->beginSend(to, files);
    return id;
}

void NearbyService::accept(const QString &id)
{
    if (auto link = m_links.value(id))
        link->accept();
}

void NearbyService::decline(const QString &id)
{
    if (auto link = m_links.value(id))
        link->decline();
}

void NearbyService::cancel(const QString &id)
{
    if (auto link = m_links.value(id))
        link->cancel();
}

QVariantMap NearbyService::transfer(const QString &id) const
{
    return m_info.value(id);
}

void NearbyService::update(const QString &id, const QVariantMap &changes)
{
    if (!m_info.contains(id))
        return;
    QVariantMap &info = m_info[id];
    for (auto it = changes.begin(); it != changes.end(); ++it)
        info.insert(it.key(), it.value());
    emit transferChanged(id);
}

bool NearbyService::blocked(const QHostAddress &peer) const
{
    return m_strikes.value(peer.toString()).second > QDateTime::currentSecsSinceEpoch();
}

void NearbyService::failed(const QHostAddress &peer)
{
    auto &s = m_strikes[peer.toString()];
    if (++s.first >= StrikesToBlock)
        s = {0, QDateTime::currentSecsSinceEpoch() + BlockSeconds};
}
