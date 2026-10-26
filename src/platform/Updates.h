#pragma once

#include "platform/UpdateModel.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QSettings>
#include <QTimer>

// Is a newer Olyzano Notch out? As `updates` in QML: it asks GitHub for the latest release (an anonymous HTTPS request that
// shows only the IP address and the app's version), compares it with the version that is running, and tells the person once
// per version. It never downloads or installs anything: "Download" opens the right file in the browser.
// Settings and what it remembers live in notch/updates.conf.
class Updates : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString installed READ installed CONSTANT)
    Q_PROPERTY(QString latest READ latest NOTIFY changed)       // the newest release's version, "" until one was read
    Q_PROPERTY(bool newer READ newer NOTIFY changed)            // it is newer than what runs
    Q_PROPERTY(bool skipped READ skipped NOTIFY changed)        // the person said "skip this version"
    Q_PROPERTY(QString notes READ notes NOTIFY changed)         // what the release says
    Q_PROPERTY(QString page READ page NOTIFY changed)           // the release's page
    Q_PROPERTY(bool checking READ checking NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)       // one line for Settings
    Q_PROPERTY(QString lastChecked READ lastChecked NOTIFY changed)
    Q_PROPERTY(QStringList contributors READ contributors NOTIFY contributorsChanged) // GitHub logins, for the thank-you in About
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY settingsChanged)
    Q_PROPERTY(bool quiet READ quiet WRITE setQuiet NOTIFY settingsChanged) // set from QML: a game, a call or a timer is on, so no toast now

public:
    explicit Updates(QObject *parent = nullptr);

    QStringList contributors() const { return m_contributors; }
    Q_INVOKABLE void loadContributors(); // once a day at most, and only while the update check is on
    QString installed() const { return m_installed; }
    QString latest() const { return m_release.valid() ? m_release.version() : QString(); }
    bool newer() const { return m_release.valid() && UpdateModel::isNewer(m_release.tag, m_installed); }
    bool skipped() const { return m_release.valid() && m_settings.value(QStringLiteral("skipped")).toString() == m_release.tag; }
    QString notes() const { return m_release.notes; }
    QString page() const { return m_release.url; }
    bool checking() const { return m_reply != nullptr; }
    QString status() const;
    QString lastChecked() const;
    bool enabled() const { return m_settings.value(QStringLiteral("enabled"), true).toBool(); }
    void setEnabled(bool on);
    bool quiet() const { return m_quiet; }
    void setQuiet(bool q);

    Q_INVOKABLE void check();        // "Check now": always asks, and says plainly how it went
    Q_INVOKABLE void skip();         // this version: no more toasts (a newer one will speak)
    Q_INVOKABLE void unskip();
    Q_INVOKABLE void remindLater();  // a week
    Q_INVOKABLE void download();     // the file for this system in the browser, else the release page
    QByteArray report() const;       // `notch --check-updates`

signals:
    void changed();
    void contributorsChanged();
    void settingsChanged();
    void updateFound(const QString &version);  // once per version, at a quiet moment
    void checked();                             // a check has ended (well or not)

private:
    void ask(bool manual);
    void onReply(bool manual);
    void maybeAnnounce();
    QString assetUrl() const;

    QNetworkAccessManager m_nam;
    QNetworkReply *m_reply = nullptr;
    QSettings m_settings;
    QStringList m_contributors;
    QNetworkReply *m_peopleReply = nullptr;
    QString m_installed, m_error;
    UpdateModel::Release m_release;
    bool m_quiet = false;
    QTimer m_start, m_tick;
};
