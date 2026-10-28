#include "platform/Updates.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QProcess>
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


// ---- One-click update ----

bool Updates::canInstall() const
{
    if (!newer() || assetUrl().isEmpty())
        return false;
    if (qEnvironmentVariableIsSet("NOTCH_UPDATE_DRYRUN")) // `notch --install-update`: the download and the check run, the install does not
        return true;
#ifdef Q_OS_WIN
    return assetUrl().endsWith(QLatin1String("setup.exe")); // the portable zip has to be unpacked by hand
#else
    // only a copy the package manager put there can be replaced by the package: not a build folder
    return QCoreApplication::applicationFilePath().startsWith(QLatin1String("/usr/"))
           && !QStandardPaths::findExecutable(QStringLiteral("pkexec")).isEmpty() && !QStandardPaths::findExecutable(QStringLiteral("apt-get")).isEmpty();
#endif
}

void Updates::setStage(const QString &stage, double progress)
{
    m_stage = stage;
    m_progress = progress;
    emit installChanged();
}

void Updates::failInstall(const QString &why)
{
    m_installing = false;
    m_stage.clear();
    m_installError = why;
    emit installChanged();
}

void Updates::cancelInstall()
{
    if (m_download)
        m_download->abort(); // its finished() ends the install with "cancelled"
}

void Updates::installNow()
{
    if (m_installing || !canInstall())
        return;
    const QString fileUrl = assetUrl();
#ifdef Q_OS_WIN
    const QString platform = QStringLiteral("windows");
#else
    const QString platform = QStringLiteral("linux");
#endif
    const QString sumsUrl = UpdateModel::pickChecksums(m_release.assets, platform);
    const QString name = QUrl(fileUrl).fileName();
    m_installError.clear();
    if (sumsUrl.isEmpty()) {
        failInstall(tr("This release has no checksum file, so Notch will not install it by itself. Use Download instead."));
        return;
    }
    if (!UpdateModel::trustedHost(QUrl(fileUrl)) || !UpdateModel::trustedHost(QUrl(sumsUrl))) {
        failInstall(tr("The download address is not a GitHub one. Use Download instead."));
        return;
    }
    m_installing = true;
    setStage(tr("Looking up the checksum…"));
    QNetworkRequest sumsReq{QUrl(sumsUrl)};
    sumsReq.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    sumsReq.setTransferTimeout(30000);
    QNetworkReply *sums = m_nam.get(sumsReq);
    connect(sums, &QNetworkReply::finished, this, [this, sums, fileUrl, name] {
        sums->deleteLater();
        const QByteArray body = sums->readAll();
        const QString expected = UpdateModel::parseChecksums(QString::fromUtf8(body)).value(name);
        if (sums->error() != QNetworkReply::NoError || expected.isEmpty() || !UpdateModel::trustedHost(sums->url())) {
            failInstall(tr("Could not read the checksum of %1. Use Download instead.").arg(name));
            return;
        }
        QDir dir(QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/notch-update"));
        dir.removeRecursively();
        dir.mkpath(QStringLiteral("."));
        const QString path = dir.filePath(name);
        auto *file = new QFile(path, this);
        if (!file->open(QIODevice::WriteOnly)) {
            file->deleteLater();
            failInstall(tr("Could not write to the temporary folder."));
            return;
        }
        QNetworkRequest req{QUrl(fileUrl)};
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        setStage(tr("Downloading %1…").arg(name));
        m_download = m_nam.get(req);
        connect(m_download, &QNetworkReply::readyRead, file, [this, file] { file->write(m_download->readAll()); });
        connect(m_download, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
            if (total > 0) {
                m_progress = double(got) / double(total);
                emit installChanged();
            }
        });
        connect(m_download, &QNetworkReply::finished, this, [this, file, path, expected] {
            QNetworkReply *r = m_download;
            m_download = nullptr;
            r->deleteLater();
            file->write(r->readAll());
            file->close();
            file->deleteLater();
            if (r->error() == QNetworkReply::OperationCanceledError) {
                QFile::remove(path);
                failInstall(tr("Cancelled."));
            } else if (r->error() != QNetworkReply::NoError || !UpdateModel::trustedHost(r->url())) {
                QFile::remove(path);
                failInstall(tr("The download failed (%1). Use Download instead.").arg(r->errorString()));
            } else {
                verifyAndRun(path, expected);
            }
        });
    });
}

void Updates::verifyAndRun(const QString &path, const QString &expectedHex)
{
    setStage(tr("Checking the file…"), 1);
    QFile f(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!f.open(QIODevice::ReadOnly) || !hash.addData(&f)) {
        failInstall(tr("Could not read the downloaded file."));
        return;
    }
    if (!UpdateModel::sha256Matches(hash.result().toHex(), expectedHex)) {
        QFile::remove(path);
        failInstall(tr("The downloaded file does not match its checksum, so it was not installed. Try again or use Download."));
        return;
    }
    if (qEnvironmentVariableIsSet("NOTCH_UPDATE_DRYRUN")) {
        QFile::remove(path);
        m_installing = false;
        setStage(tr("Checked and ready (dry run: nothing was installed)"), 1);
        return;
    }
    setStage(tr("Installing…"), 1);
#ifdef Q_OS_WIN
    // The installer closes Notch itself (CloseApplications) and starts the new one; this copy only has to get out of the way.
    if (!QProcess::startDetached(path, UpdateModel::windowsInstallerArgs())) {
        failInstall(tr("Could not start the installer."));
        return;
    }
    QCoreApplication::quit();
#else
    const QStringList cmd = UpdateModel::linuxInstallCommand(path);
    m_installer = new QProcess(this);
    connect(m_installer, &QProcess::finished, this, [this, path](int code, QProcess::ExitStatus status) {
        m_installer->deleteLater();
        m_installer = nullptr;
        if (status == QProcess::NormalExit && code == 0) {
            QFile::remove(path);
            // the package is in place: start the new program once this one has let go of the single-instance lock
            QProcess::startDetached(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), QStringLiteral("sleep 1; exec \"$0\""), QCoreApplication::applicationFilePath()});
            QCoreApplication::quit();
        } else {
            failInstall(code == 126 || code == 127 ? tr("The password prompt was cancelled, so nothing was installed.")
                                                   : tr("The installation did not finish (code %1). Use Download instead.").arg(code));
        }
    });
    connect(m_installer, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (m_installer && m_installer->state() == QProcess::NotRunning)
            failInstall(tr("Could not start the installer."));
    });
    m_installer->start(cmd.first(), cmd.mid(1));
#endif
}
