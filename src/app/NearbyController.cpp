#include "NearbyController.h"

#include "services/FileService.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QUuid>

NearbyController::NearbyController(NotchController *notch, QObject *parent)
    : QAbstractListModel(parent)
    , m_notch(notch)
    , m_settings(notch->settingsFolder() + QStringLiteral("/nearby.conf"), QSettings::IniFormat)
    , m_service(notch->scratchPath(false))
{
    QString id = m_settings.value(QStringLiteral("deviceId")).toString();
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_settings.setValue(QStringLiteral("deviceId"), id);
    }
    m_service.setDevice(id, m_settings.value(QStringLiteral("deviceName")).toString());
    QDir(notch->scratchPath(false) + QStringLiteral("/nearby")).removeRecursively(); // leftovers of a crash

    connect(&m_service, &NearbyService::receivingChanged, this, &NearbyController::receivingChanged);
    connect(&m_service, &NearbyService::transferChanged, this, &NearbyController::onTransfer);
    connect(&m_service, &NearbyService::incoming, this, [this](const QString &id, const QString &from, const QVariantList &files,
                                                               qint64 total, const QString &code, bool lowDisk) {
        emit incomingRequest(id, from, files, double(total), code, lowDisk);
    });
    connect(&m_service, &NearbyService::received, this, [this](const QString &folder, const QStringList &names, const QString &from) {
        m_notch->addReceivedFiles(folder, names, from);
    });
    connect(&m_service, &NearbyService::deviceFound, this, [this](const QVariantMap &d) {
        m_seen.insert(d.value(QStringLiteral("id")).toString(), QDateTime::currentSecsSinceEpoch());
        bool known = false;
        for (QVariant &v : m_devices)
            if (v.toMap().value(QStringLiteral("id")) == d.value(QStringLiteral("id"))) {
                v = d;
                known = true;
            }
        if (!known)
            m_devices.append(d);
        emit devicesChanged();
    });
    m_scan.setInterval(1500);
    connect(&m_scan, &QTimer::timeout, this, &NearbyController::tick);
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] { m_service.stopReceiving(); });
}

QHash<int, QByteArray> NearbyController::roleNames() const
{
    return {{IdRole, "transferId"}, {DirectionRole, "direction"}, {PeerRole, "peer"}, {StateRole, "state"},
            {CodeRole, "code"}, {ProgressRole, "progress"}, {TotalRole, "total"}, {FilesRole, "files"},
            {MessageRole, "message"}, {ActiveRole, "active"}};
}

static bool isActive(const QString &state)
{
    return state != QLatin1String("done") && state != QLatin1String("declined") && state != QLatin1String("failed");
}

QVariant NearbyController::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_ids.size())
        return {};
    const QVariantMap t = m_service.transfer(m_ids.at(index.row()));
    switch (role) {
    case IdRole: return m_ids.at(index.row());
    case DirectionRole: return t.value(QStringLiteral("direction"));
    case PeerRole: return t.value(QStringLiteral("peer"));
    case StateRole: return t.value(QStringLiteral("state"));
    case CodeRole: return t.value(QStringLiteral("code"));
    case ProgressRole: return t.value(QStringLiteral("progress"), 0.0);
    case TotalRole: return t.value(QStringLiteral("total"), 0);
    case FilesRole: return t.value(QStringLiteral("files"), 0);
    case MessageRole: return t.value(QStringLiteral("message"));
    case ActiveRole: return isActive(t.value(QStringLiteral("state")).toString());
    default: return {};
    }
}

int NearbyController::activeCount() const
{
    int n = 0;
    for (const QString &id : m_ids)
        n += isActive(m_service.transfer(id).value(QStringLiteral("state")).toString());
    return n;
}

void NearbyController::setDeviceName(const QString &name)
{
    const QString clean = name.trimmed().left(60);
    if (clean.isEmpty() || clean == m_service.deviceName())
        return;
    m_service.setDevice({}, clean);
    m_settings.setValue(QStringLiteral("deviceName"), clean);
    emit deviceNameChanged();
}

QString NearbyController::startReceiving()
{
    return m_service.startReceiving(10 * 60);
}

void NearbyController::scan()
{
    m_probes = 0;
    m_devices.clear();
    m_seen.clear();
    emit devicesChanged();
    m_scan.start();
    emit scanningChanged();
    tick();
}

void NearbyController::tick()
{
    m_service.probe();
    if (++m_probes >= 7) { // about 10 seconds
        m_scan.stop();
        emit scanningChanged();
    }
}

QString NearbyController::send(const QStringList &itemIds, const QString &deviceId)
{
    NearbyService::Device to;
    for (const QVariant &v : std::as_const(m_devices)) {
        const QVariantMap d = v.toMap();
        if (d.value(QStringLiteral("id")).toString() == deviceId)
            to = {deviceId, d.value(QStringLiteral("name")).toString(), d.value(QStringLiteral("os")).toString(),
                  QHostAddress(d.value(QStringLiteral("address")).toString()), quint16(d.value(QStringLiteral("port")).toInt())};
    }
    const auto sources = m_notch->shareSources(itemIds);
    if (to.port == 0 || sources.isEmpty())
        return {};
    // Two files may share a name (report.pdf from two folders): the receiver sees "report (1).pdf".
    QStringList used;
    QList<ShareCrypto::SourceFile> files;
    for (const auto &s : sources) {
        const QString name = FileService::uniqueAmong(s.name, [&](const QString &n) { return used.contains(n); });
        used << name;
        files.append({s.path, name, s.mimeType.isEmpty() ? QStringLiteral("application/octet-stream") : s.mimeType});
    }
    return m_service.send(to, files);
}

void NearbyController::onTransfer(const QString &id)
{
    const int row = m_ids.indexOf(id);
    if (row < 0) {
        beginInsertRows({}, 0, 0);
        m_ids.prepend(id);
        endInsertRows();
    } else {
        emit dataChanged(index(row), index(row));
    }
    emit countChanged();
}

void NearbyController::dismiss(const QString &id)
{
    const int row = m_ids.indexOf(id);
    if (row < 0 || isActive(m_service.transfer(id).value(QStringLiteral("state")).toString()))
        return;
    beginRemoveRows({}, row, row);
    m_ids.removeAt(row);
    endRemoveRows();
    emit countChanged();
}

void NearbyController::clearFinished()
{
    for (int i = m_ids.size() - 1; i >= 0; --i)
        if (!isActive(m_service.transfer(m_ids.at(i)).value(QStringLiteral("state")).toString())) {
            beginRemoveRows({}, i, i);
            m_ids.removeAt(i);
            endRemoveRows();
        }
    emit countChanged();
}
