#pragma once

#include "app/NotchController.h"
#include "services/NearbyService.h"

#include <QAbstractListModel>
#include <QSettings>
#include <QTimer>

// Nearby share for QML, as `nearby`: switch receiving on, find computers around, send to one, answer a request.
// Besides the properties it is the list of transfers (sending and receiving) shown in the share popup.
class NearbyController : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool receiving READ receiving NOTIFY receivingChanged)
    Q_PROPERTY(qint64 receiveUntil READ receiveUntil NOTIFY receivingChanged) // epoch seconds, 0 when off
    Q_PROPERTY(QString deviceName READ deviceName WRITE setDeviceName NOTIFY deviceNameChanged)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)       // {id, name, os, address, port}
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY countChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        DirectionRole,
        PeerRole,
        StateRole,
        CodeRole,
        ProgressRole,
        TotalRole,
        FilesRole,
        MessageRole,
        ActiveRole,
    };

    explicit NearbyController(NotchController *notch, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : m_ids.size(); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool receiving() const { return m_service.receiving(); }
    qint64 receiveUntil() const { return m_service.receiveUntil(); }
    QString deviceName() const { return m_service.deviceName(); }
    void setDeviceName(const QString &name);
    QVariantList devices() const { return m_devices; }
    bool scanning() const { return m_scan.isActive(); }
    int activeCount() const;

    Q_INVOKABLE QString startReceiving();   // "" or why not
    Q_INVOKABLE void stopReceiving() { m_service.stopReceiving(); }
    Q_INVOKABLE void scan();                // looks around for about 10 seconds
    Q_INVOKABLE QString send(const QStringList &itemIds, const QString &deviceId); // the transfer id, "" if nothing to send
    Q_INVOKABLE void accept(const QString &transferId) { m_service.accept(transferId); }
    Q_INVOKABLE void decline(const QString &transferId) { m_service.decline(transferId); }
    Q_INVOKABLE void cancel(const QString &transferId) { m_service.cancel(transferId); }
    Q_INVOKABLE void dismiss(const QString &transferId);
    Q_INVOKABLE void clearFinished();

signals:
    void receivingChanged();
    void deviceNameChanged();
    void devicesChanged();
    void scanningChanged();
    void countChanged();
    // Someone wants to send you files: ask. `files`: [{name, size}].
    void incomingRequest(const QString &transferId, const QString &from, const QVariantList &files, double total,
                         const QString &code, bool lowDisk);

private:
    void onTransfer(const QString &id);
    void tick();

    NotchController *m_notch;
    QSettings m_settings;
    NearbyService m_service;
    QStringList m_ids; // transfers, newest first
    QVariantList m_devices;
    QHash<QString, qint64> m_seen; // device id -> last heard (epoch seconds)
    QTimer m_scan;
    int m_probes = 0;
};
