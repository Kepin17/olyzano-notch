#pragma once

#include "platform/PanelModel.h"

#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantList>

// What the Quick Settings card shows and changes, as `quick` in QML. Linux: sound through `pactl` (and its
// `subscribe`, so changes made elsewhere show up), brightness through PowerDevil, the power mode through
// power-profiles-daemon, Bluetooth through BlueZ, Wi-Fi through NetworkManager. Each part says whether it exists
// on this computer (`*Available`); elsewhere none do and the card has nothing to show.
class QuickSettings : public QObject
{
    Q_OBJECT
    // sound
    Q_PROPERTY(bool audioAvailable READ audioAvailable NOTIFY changed)
    Q_PROPERTY(int volume READ volume NOTIFY changed)             // percent of the default output
    Q_PROPERTY(bool muted READ muted NOTIFY changed)
    Q_PROPERTY(int volumeLevel READ volumeLevel NOTIFY changed)   // 0-3: which speaker icon
    Q_PROPERTY(QVariantList sinks READ sinks NOTIFY changed)      // [{name, description, current}]
    Q_PROPERTY(QString sinkName READ sinkName NOTIFY changed)
    Q_PROPERTY(bool inputAvailable READ inputAvailable NOTIFY changed)
    Q_PROPERTY(int inputVolume READ inputVolume NOTIFY changed)   // the microphone, percent
    Q_PROPERTY(bool inputMuted READ inputMuted NOTIFY changed)
    Q_PROPERTY(QVariantList sources READ sources NOTIFY changed)  // [{name, description, current}]
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY changed)
    // screen
    Q_PROPERTY(bool brightnessAvailable READ brightnessAvailable NOTIFY changed)
    Q_PROPERTY(int brightness READ brightness NOTIFY changed)     // percent
    // power
    Q_PROPERTY(bool profilesAvailable READ profilesAvailable NOTIFY changed)
    Q_PROPERTY(QString profile READ profile NOTIFY changed)       // power-saver | balanced | performance
    Q_PROPERTY(QStringList profiles READ profiles NOTIFY changed)
    Q_PROPERTY(QString batteryLeft READ batteryLeft NOTIFY changed) // "2 h 05 min", empty if unknown
    // Bluetooth
    Q_PROPERTY(bool btAvailable READ btAvailable NOTIFY changed)
    Q_PROPERTY(bool btEnabled READ btEnabled NOTIFY changed)
    Q_PROPERTY(QString btConnected READ btConnected NOTIFY changed) // the names of what is connected
    Q_PROPERTY(QVariantList btDevices READ btDevices NOTIFY changed) // [{path, name, connected}]
    // Wi-Fi
    Q_PROPERTY(bool wifiAvailable READ wifiAvailable NOTIFY changed)
    Q_PROPERTY(bool wifiEnabled READ wifiEnabled NOTIFY changed)
    Q_PROPERTY(QString wifiName READ wifiName NOTIFY changed)
    Q_PROPERTY(int wifiBars READ wifiBars NOTIFY changed)         // 0-4
    Q_PROPERTY(QVariantList wifiNetworks READ wifiNetworks NOTIFY changed) // saved networks [{id, name, active}]

public:
    explicit QuickSettings(QObject *parent = nullptr);
    ~QuickSettings() override;

    bool audioAvailable() const { return m_audio; }
    int volume() const { return m_volume; }
    bool muted() const { return m_muted; }
    int volumeLevel() const { return PanelModel::volumeLevel(m_volume, m_muted); }
    QVariantList sinks() const { return m_sinkList; }
    QString sinkName() const { return m_sinkName; }
    bool inputAvailable() const { return !m_sourceList.isEmpty(); }
    int inputVolume() const { return m_inVolume; }
    bool inputMuted() const { return m_inMuted; }
    QVariantList sources() const { return m_sourceList; }
    QString sourceName() const { return m_sourceName; }
    bool brightnessAvailable() const { return m_brightMax > 0; }
    int brightness() const { return m_brightMax > 0 ? qRound(100.0 * m_bright / m_brightMax) : 0; }
    bool profilesAvailable() const { return !m_profiles.isEmpty(); }
    QString profile() const { return m_profile; }
    QStringList profiles() const { return m_profiles; }
    QString batteryLeft() const { return m_batteryLeft; }
    bool btAvailable() const { return m_btAvailable; }
    bool btEnabled() const { return m_btOn; }
    QString btConnected() const { return m_btConnected; }
    QVariantList btDevices() const { return m_btDevices; }
    bool wifiAvailable() const { return m_wifiAvailable; }
    bool wifiEnabled() const { return m_wifiOn; }
    QString wifiName() const { return m_wifiName; }
    int wifiBars() const { return PanelModel::wifiBars(m_wifiStrength); }
    QVariantList wifiNetworks() const { return m_wifiNetworks; }

    Q_INVOKABLE void refresh(); // when the card opens
    Q_INVOKABLE void setVolume(int percent);
    Q_INVOKABLE void toggleMute();
    Q_INVOKABLE void setSink(const QString &name);
    Q_INVOKABLE void setInputVolume(int percent);
    Q_INVOKABLE void toggleInputMute();
    Q_INVOKABLE void setSource(const QString &name);
    Q_INVOKABLE void setBrightness(int percent);
    Q_INVOKABLE void setProfile(const QString &profile);
    Q_INVOKABLE void setBtEnabled(bool on);
    Q_INVOKABLE void toggleBtDevice(const QString &path);
    Q_INVOKABLE void setWifiEnabled(bool on);
    Q_INVOKABLE void connectWifi(const QString &connectionPath);
    Q_INVOKABLE void openSystemSettings();

    QByteArray dump() const; // for `notch --dump-quick`: what it sees, as JSON

signals:
    void changed();

private:
    void readAudio();
    void readSystem();   // brightness, power, Bluetooth, Wi-Fi: D-Bus
    void listenAudio();
    void soon();
private slots:
    void onProps(const QString &, const QVariantMap &, const QStringList &) { soon(); }
    void onBrightness(int) { soon(); }
private:

    bool m_audio = false;
    int m_volume = 0;
    bool m_muted = false;
    QVariantList m_sinkList, m_sourceList;
    int m_inVolume = 0;
    bool m_inMuted = false;
    QString m_sourceName;
    QString m_sinkName;
    int m_bright = 0, m_brightMax = 0;
    QString m_profile, m_batteryLeft;
    QStringList m_profiles;
    bool m_btAvailable = false, m_btOn = false;
    QString m_btConnected, m_btAdapter;
    QVariantList m_btDevices;
    bool m_wifiAvailable = false, m_wifiOn = false;
    QString m_wifiName, m_wifiDevice;
    int m_wifiStrength = 0;
    QVariantList m_wifiNetworks;

    QProcess m_subscribe;
    QTimer m_debounce, m_poll;
};
