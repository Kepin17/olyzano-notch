#include "platform/Updates.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QUrl>

namespace {
const QUrl LatestUrl(QStringLiteral("https://api.github.com/repos/Kepin17/olyzano-notch/releases/latest"));
const QUrl PeopleUrl(QStringLiteral("https://api.github.com/repos/Kepin17/olyzano-notch/contributors?per_page=50"));
constexpr qint64 DaySecs = 86400;
}

Updates::Updates(QObject *parent)
    : QObject(parent)
    , m_settings(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/notch/updates.conf"), QSettings::IniFormat)
{
    const QByteArray fake = qgetenv("NOTCH_FAKE_VERSION"); // for trying the notice without waiting for a release
    m_installed = !fake.isEmpty() ? QString::fromUtf8(fake) : QCoreApplication::applicationVersion();
    // what was read last time, so "up to date" or "0.13.0 is available" shows before (and without) a new request
    if (m_settings.contains(QStringLiteral("release"))) {
        m_release.tag = m_settings.value(QStringLiteral("tag")).toString();
        m_release.notes = m_settings.value(QStringLiteral("notes")).toString();
        m_release.url = m_settings.value(QStringLiteral("page")).toString();
        const QStringList names = m_settings.value(QStringLiteral("assetNames")).toStringList(), urls = m_settings.value(QStringLiteral("assetUrls")).toStringList();
        for (int i = 0; i < names.size() && i < urls.size(); ++i)
            m_release.assets << qMakePair(names.at(i), urls.at(i));
        if (!m_release.valid())
            m_release = {};
    }
    m_contributors = m_settings.value(QStringLiteral("contributors")).toStringList(); // the thank-you list shows at once, even offline
    m_start.setSingleShot(true);
    m_start.setInterval(20000); // after start-up has settled
    connect(&m_start, &QTimer::timeout, this, [this] { if (enabled()) ask(false); });
    m_tick.setInterval(30 * 60 * 1000); // and then whenever a day has passed, and at quiet moments the notice that was waiting
    connect(&m_tick, &QTimer::timeout, this, [this] {
        if (enabled() && QDateTime::currentSecsSinceEpoch() - m_settings.value(QStringLiteral("lastCheck")).toLongLong() >= DaySecs)
            ask(false);
        else
            maybeAnnounce();
    });
    if (enabled()) {
        m_start.start();
        m_tick.start();
    }
}

void Updates::setEnabled(bool on)
{
    if (on == enabled())
        return;
    m_settings.setValue(QStringLiteral("enabled"), on);
    if (on) {
        m_tick.start();
        ask(false);
    } else {
        m_tick.stop();
        m_start.stop();
    }
    emit settingsChanged();
    emit changed();
}

void Updates::setQuiet(bool q)
{
    if (q == m_quiet)
        return;
    m_quiet = q;
    emit settingsChanged();
    if (!q)
        maybeAnnounce();
}

QString Updates::lastChecked() const
{
    const qint64 at = m_settings.value(QStringLiteral("lastCheck")).toLongLong();
    if (!at)
        return tr("never");
    const qint64 ago = QDateTime::currentSecsSinceEpoch() - at;
    return ago < 90 ? tr("just now") : ago < 3600 ? tr("%1 min ago").arg(ago / 60) : ago < 2 * DaySecs ? tr("%1 h ago").arg(ago / 3600) : tr("%1 days ago").arg(ago / DaySecs);
}

QString Updates::status() const
{
    if (checking())
        return tr("Checking…");
    if (!m_error.isEmpty())
        return m_error;
    if (!m_release.valid())
        return tr("Not checked yet.");
    if (newer())
        return skipped() ? tr("%1 is available (you skipped it).").arg(latest()) : tr("%1 is available.").arg(latest());
    return tr("You have the newest version (%1).").arg(m_installed);
}

void Updates::loadContributors()
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (m_peopleReply || !enabled() || (!m_contributors.isEmpty() && now - m_settings.value(QStringLiteral("contributorsAt")).toLongLong() < DaySecs))
        return;
    QNetworkRequest request(PeopleUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("OlyzanoNotch/%1").arg(m_installed.isEmpty() ? QStringLiteral("dev") : m_installed));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setTransferTimeout(10000);
    m_peopleReply = m_nam.get(request);
    connect(m_peopleReply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = m_peopleReply;
        m_peopleReply = nullptr;
        reply->deleteLater();
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200)
            return; // keep what is cached; "thanks" is not worth an error message
        const QStringList names = UpdateModel::parseContributors(reply->readAll());
        if (names.isEmpty())
            return;
        m_contributors = names;
        m_settings.setValue(QStringLiteral("contributors"), names);
        m_settings.setValue(QStringLiteral("contributorsAt"), QDateTime::currentSecsSinceEpoch());
        emit contributorsChanged();
    });
    emit contributorsChanged();
}

void Updates::check()
{
    ask(true);
}

void Updates::ask(bool manual)
{
    if (m_reply)
        return;
    QNetworkRequest request(LatestUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("OlyzanoNotch/%1").arg(m_installed.isEmpty() ? QStringLiteral("dev") : m_installed));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setTransferTimeout(10000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    // a repeat asks "has it changed?": a 304 answer costs nothing against GitHub's limit
    if (!manual && m_release.valid() && m_settings.contains(QStringLiteral("etag")))
        request.setRawHeader("If-None-Match", m_settings.value(QStringLiteral("etag")).toByteArray());
    m_error.clear();
    m_reply = m_nam.get(request);
    connect(m_reply, &QNetworkReply::finished, this, [this, manual] { onReply(manual); });
    emit changed();
}

void Updates::onReply(bool manual)
{
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    reply->deleteLater();
    const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (code == 304) {
        m_settings.setValue(QStringLiteral("lastCheck"), now);
    } else if (code == 200) {
        const UpdateModel::Release r = UpdateModel::parseRelease(reply->readAll());
        if (r.valid()) {
            m_release = r;
            QStringList names, urls;
            for (const auto &a : r.assets) { names << a.first; urls << a.second; }
            m_settings.setValue(QStringLiteral("release"), true);
            m_settings.setValue(QStringLiteral("tag"), r.tag);
            m_settings.setValue(QStringLiteral("notes"), r.notes);
            m_settings.setValue(QStringLiteral("page"), r.url);
            m_settings.setValue(QStringLiteral("assetNames"), names);
            m_settings.setValue(QStringLiteral("assetUrls"), urls);
            m_settings.setValue(QStringLiteral("etag"), reply->rawHeader("ETag"));
            m_settings.setValue(QStringLiteral("lastCheck"), now);
        } else {
            m_error = tr("GitHub answered, but not with a release.");
        }
    } else if (code == 403 || code == 429) {
        m_error = tr("GitHub is limiting requests from this network right now. Try again later.");
    } else if (code == 404) {
        m_error = tr("No release was found.");
    } else if (reply->error() != QNetworkReply::NoError) {
        m_error = tr("Could not reach GitHub. Are you online?");
    } else {
        m_error = tr("GitHub answered with an unexpected reply (%1).").arg(code);
    }
    if (!manual && !m_error.isEmpty())
        m_error.clear(); // in the background a failure is not worth a word: it simply tries again later
    emit changed();
    emit checked();
    maybeAnnounce();
}

// One notice per version: not when it was skipped or "later" is still running, nor while a game, a call or a timer is on.
void Updates::maybeAnnounce()
{
    if (!enabled() || !newer() || skipped() || m_quiet)
        return;
    if (QDateTime::currentSecsSinceEpoch() < m_settings.value(QStringLiteral("remindAfter")).toLongLong())
        return;
    if (m_settings.value(QStringLiteral("seen")).toString() == m_release.tag)
        return;
    m_settings.setValue(QStringLiteral("seen"), m_release.tag);
    emit updateFound(latest());
}

void Updates::skip()
{
    if (!m_release.valid())
        return;
    m_settings.setValue(QStringLiteral("skipped"), m_release.tag);
    emit changed();
}

void Updates::unskip()
{
    m_settings.remove(QStringLiteral("skipped"));
    m_settings.remove(QStringLiteral("seen"));
    emit changed();
}

void Updates::remindLater()
{
    m_settings.setValue(QStringLiteral("remindAfter"), QDateTime::currentSecsSinceEpoch() + 7 * DaySecs);
    m_settings.remove(QStringLiteral("seen")); // it speaks again after the week
    emit changed();
}

QString Updates::assetUrl() const
{
#ifdef Q_OS_WIN
    const bool portable = !QFileInfo::exists(QCoreApplication::applicationDirPath() + QStringLiteral("/unins000.exe")); // the installer leaves its uninstaller
    return UpdateModel::pickAsset(m_release.assets, QStringLiteral("windows"), portable);
#else
    return UpdateModel::pickAsset(m_release.assets, QStringLiteral("linux"), false);
#endif
}

void Updates::download()
{
    const QString file = assetUrl();
    const QString target = !file.isEmpty() ? file : m_release.url;
    if (target.startsWith(QLatin1String("https://")))
        QDesktopServices::openUrl(QUrl(target));
}

QByteArray Updates::report() const
{
    return QStringLiteral("installed: %1\nlatest: %2\nnewer: %3\nstatus: %4\nfile for this system: %5\npage: %6\n")
        .arg(m_installed, latest().isEmpty() ? QStringLiteral("-") : latest(), newer() ? QStringLiteral("yes") : QStringLiteral("no"), status(),
             assetUrl().isEmpty() ? QStringLiteral("-") : assetUrl(), m_release.url.isEmpty() ? QStringLiteral("-") : m_release.url).toUtf8();
}
