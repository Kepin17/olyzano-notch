#include "platform/AppDock.h"

#include "models/FileItem.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileIconProvider>
#include <QGuiApplication>
#include <QScreen>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QPixmap>
#include <QSet>
#include <QStandardPaths>

#ifdef NOTCH_HAVE_DBUS
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#endif
#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

#ifndef Q_OS_WIN
// The freedesktop trash in the home folder (what Dolphin's Trash shows for the home partition).
static QString homeTrash()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Trash");
}
#endif

static QString configPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/notch/dock.conf");
}

AppDock::AppDock(QObject *parent)
    : QObject(parent), m_settings(configPath(), QSettings::IniFormat)
{
    m_enabled = m_settings.value(QStringLiteral("enabled"), true).toBool();
    m_autoHide = m_settings.value(QStringLiteral("autoHide"), true).toBool();
    m_pinned = m_settings.value(QStringLiteral("pinned")).toStringList();
    m_timer.setInterval(2000);
    connect(&m_timer, &QTimer::timeout, this, &AppDock::refresh);
    startKWin();
#ifndef Q_OS_WIN
    // Watch the trash folder (and its parent, for when the first file ever trashed creates it).
    QDir().mkpath(homeTrash() + QStringLiteral("/files"));
    m_trashWatcher.addPath(homeTrash() + QStringLiteral("/files"));
    connect(&m_trashWatcher, &QFileSystemWatcher::directoryChanged, this, &AppDock::checkTrash);
#endif
    checkTrash();
#ifdef Q_OS_WIN
    m_coverPoll.setInterval(1200);
    connect(&m_coverPoll, &QTimer::timeout, this, &AppDock::checkForeground);
    m_coverPoll.start();
#endif
#ifdef NOTCH_HAVE_DBUS
    // Any sender, any path: apps (Telegram, Thunderbird, Discord, …) broadcast this for their launcher icon.
    QDBusConnection::sessionBus().connect(QString(), QString(), QStringLiteral("com.canonical.Unity.LauncherEntry"),
                                          QStringLiteral("Update"), this, SLOT(launcherEntry(QString, QVariantMap)));
#endif
}

void AppDock::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    m_settings.setValue(QStringLiteral("enabled"), enabled);
    emit enabledChanged();
}

void AppDock::setAutoHide(bool autoHide)
{
    if (m_autoHide == autoHide)
        return;
    m_autoHide = autoHide;
    m_settings.setValue(QStringLiteral("autoHide"), autoHide);
    emit autoHideChanged();
}

void AppDock::storeAppearance(const QString &key, const QVariant &value)
{
    if (m_settings.value(key) == value)
        return;
    m_settings.setValue(key, value);
    emit appearanceChanged();
}

void AppDock::setPosition(const QString &position)
{
    static const QStringList sides = {QStringLiteral("bottom"), QStringLiteral("left"), QStringLiteral("right")};
    if (!sides.contains(position) || position == this->position())
        return;
    storeAppearance(QStringLiteral("position"), position);
    emit positionChanged();
}

void AppDock::setIconSize(int size)
{
    storeAppearance(QStringLiteral("iconSize"), qBound(32, size, 80));
}

void AppDock::setMagnification(double scale)
{
    storeAppearance(QStringLiteral("magnification"), qBound(1.0, scale, 1.25));
}

void AppDock::setBlur(bool blur)
{
    storeAppearance(QStringLiteral("blur"), blur);
}

void AppDock::setAnimation(const QString &animation)
{
    static const QStringList kinds = {QStringLiteral("none"), QStringLiteral("smooth"), QStringLiteral("bouncy")};
    if (kinds.contains(animation))
        storeAppearance(QStringLiteral("animation"), animation);
}

void AppDock::setCoveredScreen(const QString &screen)
{
    if (screen == m_coveredScreen)
        return;
    m_coveredScreen = screen;
    emit coveredScreenChanged();
}

// Windows: the window in front covers its screen's top edge when it's maximized or its top is at the
// top of the work area (the desktop and the taskbar don't count).
void AppDock::checkForeground()
{
#ifdef Q_OS_WIN
    QString covered;
    const HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (fg)
        GetWindowThreadProcessId(fg, &pid);
    if (fg && pid != GetCurrentProcessId() && IsWindowVisible(fg)) {
        wchar_t cls[64];
        const int n = GetClassNameW(fg, cls, 64);
        const QString windowClass = QString::fromWCharArray(cls, qMax(0, n));
        const bool shell = windowClass == QLatin1String("Progman") || windowClass == QLatin1String("WorkerW")
                           || windowClass == QLatin1String("Shell_TrayWnd");
        RECT r;
        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        if (!shell && GetWindowRect(fg, &r) && GetMonitorInfoW(MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST), &mi)
            && (IsZoomed(fg) || r.top <= mi.rcWork.top + 8)) {
            const qreal dpr = QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->devicePixelRatio() : 1.0;
            QScreen *screen = QGuiApplication::screenAt(QPoint(int((r.left + r.right) / 2 / dpr), int((r.top + r.bottom) / 2 / dpr)));
            covered = screen ? screen->name() : QStringLiteral("*");
        }
    }
    setCoveredScreen(covered);
#endif
}

void AppDock::setShowTrash(bool show)
{
    storeAppearance(QStringLiteral("showTrash"), show);
}

void AppDock::checkTrash()
{
#ifdef Q_OS_WIN
    SHQUERYRBINFO info = {};
    info.cbSize = sizeof(info);
    const bool full = SUCCEEDED(SHQueryRecycleBinW(nullptr, &info)) && info.i64NumItems > 0;
#else
    const bool full = !QDir(homeTrash() + QStringLiteral("/files")).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
#endif
    if (full == m_trashFull)
        return;
    m_trashFull = full;
    emit trashChanged();
}

int AppDock::moveToTrash(const QStringList &urls)
{
    int moved = 0;
    for (const QString &u : urls) {
        const QUrl url(u);
        const QString path = url.isLocalFile() ? url.toLocalFile() : u;
        if (QDir::isAbsolutePath(path) && QFileInfo::exists(path) && QFile::moveToTrash(path))
            ++moved;
    }
    checkTrash();
    return moved;
}

void AppDock::openTrash() const
{
#ifdef Q_OS_WIN
    QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("shell:RecycleBinFolder")});
#else
    QDesktopServices::openUrl(QUrl(QStringLiteral("trash:/"))); // Dolphin and other KIO apps open it
#endif
}

void AppDock::emptyTrash()
{
    emit trashEmptied();
#ifdef Q_OS_WIN
    SHEmptyRecycleBinW(nullptr, nullptr, SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
    checkTrash();
#else
    // KDE's ktrash also empties the trash folders on other drives; without it, clear the home trash.
    for (const char *tool : {"ktrash6", "ktrash5"}) {
        const QString ktrash = QStandardPaths::findExecutable(QString::fromLatin1(tool));
        if (!ktrash.isEmpty()) {
            auto *process = new QProcess(this);
            connect(process, &QProcess::finished, this, [this, process] {
                process->deleteLater();
                checkTrash();
            });
            process->start(ktrash, {QStringLiteral("--empty")});
            return;
        }
    }
    for (const QString &sub : {QStringLiteral("/files"), QStringLiteral("/info")}) {
        const QDir dir(homeTrash() + sub);
        for (const QFileInfo &f : dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System)) {
            if (f.isDir() && !f.isSymLink())
                QDir(f.absoluteFilePath()).removeRecursively();
            else
                QFile::remove(f.absoluteFilePath());
        }
    }
    checkTrash();
#endif
}

void AppDock::setShowRunning(bool show)
{
    storeAppearance(QStringLiteral("showRunning"), show);
    refresh();
}

QVariantList AppDock::pinnedApps() const
{
    QVariantList out;
    for (const QVariant &v : m_items)
        if (v.toMap().value(QStringLiteral("pinned")).toBool())
            out << v;
    return out;
}

void AppDock::setBackgroundOpacity(double opacity)
{
    storeAppearance(QStringLiteral("opacity"), qBound(0.2, opacity, 1.0));
}

void AppDock::setWatching(bool watching)
{
    if (!watching) {
        m_timer.stop();
        return;
    }
    refresh();
    if (!m_kwin) // KWin pushes every change itself
        m_timer.start();
}

void AppDock::launchNew(const QString &id)
{
    const DockApp app = m_catalog.contains(id) ? m_catalog.value(id) : m_seenRunning.value(id);
    if (!app.command.isEmpty())
        QProcess::startDetached(app.command.first(), app.command.mid(1), QDir::homePath());
}

void AppDock::pin(const QString &id)
{
    if (id.isEmpty() || m_pinned.contains(id))
        return;
    m_pinned << id;
    m_settings.setValue(QStringLiteral("pinned"), m_pinned);
    refresh();
}

void AppDock::unpin(const QString &id)
{
    if (!m_pinned.removeOne(id))
        return;
    m_settings.setValue(QStringLiteral("pinned"), m_pinned);
    refresh();
}

void AppDock::pinAt(const QString &id, int index)
{
    if (id.isEmpty())
        return;
    QStringList pinned = m_pinned;
    pinned.removeAll(id);
    pinned.insert(qBound(0, index, int(pinned.size())), id);
    if (pinned == m_pinned)
        return;
    m_pinned = pinned;
    m_settings.setValue(QStringLiteral("pinned"), m_pinned);
    refresh();
}

void AppDock::movePinned(int from, int to)
{
    const QStringList moved = dockmodel::move(m_pinned, from, to);
    if (moved == m_pinned)
        return;
    m_pinned = moved;
    m_settings.setValue(QStringLiteral("pinned"), m_pinned);
    refresh();
}

QString AppDock::iconSource(const QString &id)
{
    return QStringLiteral("image://appicon/") + QString::fromLatin1(id.toUtf8().toBase64(QByteArray::Base64UrlEncoding));
}

QVariantList AppDock::installedApps(const QString &filter)
{
    loadCatalog();
    QList<DockApp> apps = m_catalog.values();
    std::sort(apps.begin(), apps.end(), [](const DockApp &a, const DockApp &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    QVariantList out;
    for (const DockApp &app : apps) {
        if (!filter.isEmpty() && !app.name.contains(filter, Qt::CaseInsensitive))
            continue;
        out << QVariantMap{{QStringLiteral("id"), app.id},
                           {QStringLiteral("name"), app.name},
                           {QStringLiteral("iconSource"), iconSource(app.id)},
                           {QStringLiteral("pinned"), m_pinned.contains(app.id)}};
    }
    return out;
}

bool AppDock::openIfFolder(const QString &id) const
{
    if (!isFolder(id))
        return false;
    QDesktopServices::openUrl(QUrl::fromLocalFile(folderPath(id)));
    return true;
}

void AppDock::pinFolder(const QString &pathOrUrl)
{
    const QUrl url(pathOrUrl);
    const QString path = QDir::cleanPath(url.isLocalFile() ? url.toLocalFile() : pathOrUrl);
    if (QFileInfo(path).isDir())
        pin(QStringLiteral("folder:") + path);
}

QString AppDock::downloadsFolder() const
{
    return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
}

QVariantList AppDock::folderEntries(const QString &id, int limit) const
{
    QVariantList out;
    if (!isFolder(id))
        return out;
    const QFileInfoList entries = QDir(folderPath(id)).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
    static const QMimeDatabase mimes;
    for (const QFileInfo &f : entries) {
        // Downloads still in progress aren't something to open yet.
        if (f.suffix() == QLatin1String("part") || f.suffix() == QLatin1String("crdownload"))
            continue;
        out << QVariantMap{{QStringLiteral("name"), f.fileName()},
                           {QStringLiteral("url"), QUrl::fromLocalFile(f.absoluteFilePath()).toString()},
                           {QStringLiteral("iconName"), f.isDir() ? QStringLiteral("folder") : mimes.mimeTypeForFile(f).iconName()},
                           {QStringLiteral("isDir"), f.isDir()}};
        if (out.size() >= limit)
            break;
    }
    return out;
}

void AppDock::launcherEntry(const QString &appUri, const QVariantMap &properties)
{
    QString id = appUri;
    if (id.startsWith(QLatin1String("application://")))
        id = id.mid(14);
    if (!id.endsWith(QLatin1String(".desktop")))
        id += QLatin1String(".desktop");
    QVariantMap &badge = m_badges[id];
    for (auto it = properties.cbegin(); it != properties.cend(); ++it)
        badge.insert(it.key(), it.value());
    refresh();
}

void AppDock::openWith(const QString &id, const QStringList &urls)
{
    QStringList paths;
    for (const QString &u : urls) {
        const QUrl url(u);
        if (url.isLocalFile())
            paths << url.toLocalFile();
        else if (QDir::isAbsolutePath(u))
            paths << u;
    }
    const DockApp app = m_catalog.contains(id) ? m_catalog.value(id) : m_seenRunning.value(id);
    if (paths.isEmpty() || isFolder(id) || app.command.isEmpty())
        return;
    const QStringList args = app.exec.isEmpty() ? app.command + paths : dockmodel::openArguments(app.exec, paths);
    if (!args.isEmpty())
        QProcess::startDetached(args.first(), args.mid(1), QDir::homePath());
}

QIcon AppDock::iconFor(const QString &id) const
{
    if (id == QLatin1String("special:trash") || id == QLatin1String("special:trash-full")) {
        const QIcon themed = QIcon::fromTheme(id == QLatin1String("special:trash") ? QStringLiteral("user-trash")
                                                                                 : QStringLiteral("user-trash-full"));
        return themed.isNull() ? QFileIconProvider().icon(QFileIconProvider::Trashcan) : themed;
    }
    if (isFolder(id)) {
        const QString path = folderPath(id);
#ifdef Q_OS_WIN
        return QFileIconProvider().icon(QFileInfo(path));
#else
        static const QList<QPair<QStandardPaths::StandardLocation, const char *>> named = {
            {QStandardPaths::DownloadLocation, "folder-downloads"}, {QStandardPaths::DocumentsLocation, "folder-documents"},
            {QStandardPaths::PicturesLocation, "folder-pictures"}, {QStandardPaths::MusicLocation, "folder-music"},
            {QStandardPaths::MoviesLocation, "folder-videos"}, {QStandardPaths::DesktopLocation, "user-desktop"},
            {QStandardPaths::HomeLocation, "user-home"}};
        for (const auto &[location, icon] : named)
            if (QStandardPaths::writableLocation(location) == path)
                return QIcon::fromTheme(QString::fromLatin1(icon), QIcon::fromTheme(QStringLiteral("folder")));
        return QIcon::fromTheme(QStringLiteral("folder"));
#endif
    }
    const DockApp app = m_catalog.contains(id) ? m_catalog.value(id) : m_seenRunning.value(id);
#ifdef Q_OS_WIN
    const QFileInfo exe(!app.command.isEmpty() ? app.command.first() : id);
    if (exe.exists())
        return QFileIconProvider().icon(exe);
    return QFileIconProvider().icon(QFileIconProvider::File);
#else
    QIcon icon;
    if (QDir::isAbsolutePath(app.icon))
        icon = QIcon(app.icon);
    else if (!app.icon.isEmpty())
        icon = QIcon::fromTheme(app.icon, QIcon::fromTheme(app.icon.toLower()));
    return icon.isNull() ? QIcon::fromTheme(QStringLiteral("application-x-executable")) : icon;
#endif
}

void AppDock::refresh()
{
    loadCatalog();
#ifdef Q_OS_WIN
    checkTrash();
#endif
    QString active;
    const QList<DockApp> running = runningApps(&active);
    QVariantList items = dockmodel::merge(m_pinned, m_catalog, running, active, showRunning());
    for (QVariant &v : items) {
        QVariantMap m = v.toMap();
        const QString id = m.value(QStringLiteral("id")).toString();
        m[QStringLiteral("iconSource")] = iconSource(id);
        m[QStringLiteral("kind")] = isFolder(id) ? QStringLiteral("folder") : QStringLiteral("app");
        if (isFolder(id)) {
            const QFileInfo dir(folderPath(id));
            m[QStringLiteral("name")] = dir.fileName().isEmpty() ? dir.absoluteFilePath() : dir.fileName();
            m[QStringLiteral("canLaunch")] = dir.isDir();
        }
        // Launcher entries name the desktop file; on Windows (no D-Bus) there are none.
        const QVariantMap badge = m_badges.value(id);
        const bool countShown = badge.value(QStringLiteral("count-visible")).toBool();
        m[QStringLiteral("badge")] = countShown ? badge.value(QStringLiteral("count")).toLongLong() : 0;
        m[QStringLiteral("progress")] = badge.value(QStringLiteral("progress-visible")).toBool()
                                            ? badge.value(QStringLiteral("progress")).toDouble() : -1.0;
        m[QStringLiteral("urgent")] = badge.value(QStringLiteral("urgent")).toBool();
        QVariantList windows;
        for (const QString &w : m_appWindows.value(id))
            windows << QVariantMap{{QStringLiteral("id"), w}, {QStringLiteral("title"), m_windowTitles.value(w)}};
        m[QStringLiteral("windows")] = windows;
        v = m;
    }
    if (items == m_items)
        return;
    m_items = items;
    emit itemsChanged();
}

QPixmap DockIconProvider::requestPixmap(const QString &id, QSize *size, const QSize &requested)
{
    const QSize s = requested.isValid() ? requested : QSize(64, 64);
    if (size)
        *size = s;
    const QString appId = QString::fromUtf8(QByteArray::fromBase64(id.toLatin1(), QByteArray::Base64UrlEncoding));
    return m_dock->iconFor(appId).pixmap(s);
}

// ---- KWin (KDE Plasma 5 and 6) ----

void AppDock::kwinWindows(const QString &json)
{
    m_kwinWindows.clear();
    const qint64 self = QCoreApplication::applicationPid();
    const QJsonArray windows = QJsonDocument::fromJson(json.toUtf8()).array();
    for (const QJsonValue &value : windows) {
        const QJsonObject o = value.toObject();
        KWinWindow w;
        w.id = o.value(QLatin1String("id")).toString();
        w.desktopFile = o.value(QLatin1String("app")).toString();
        w.windowClass = o.value(QLatin1String("cls")).toString();
        w.caption = o.value(QLatin1String("title")).toString();
        w.pid = o.value(QLatin1String("pid")).toInteger();
        w.active = o.value(QLatin1String("active")).toBool();
        w.atTop = o.value(QLatin1String("top")).toBool();
        w.fullScreen = o.value(QLatin1String("fs")).toBool();
        w.covers = w.fullScreen || o.value(QLatin1String("cov")).toBool();
        w.screen = o.value(QLatin1String("scr")).toString();
        if (!w.id.isEmpty() && w.pid != self)
            m_kwinWindows << w;
    }
    // Plasma 5 numbers screens ("#0") instead of naming them: then hide the clock on every screen.
    QString covered;
    const bool wasFullScreen = m_fullScreen, wasCovering = m_covering;
    m_fullScreen = false;
    m_covering = false;
    for (const KWinWindow &w : std::as_const(m_kwinWindows)) {
        if (w.active && w.atTop)
            covered = w.screen.startsWith(u'#') || w.screen.isEmpty() ? QStringLiteral("*") : w.screen;
        m_fullScreen = m_fullScreen || (w.active && w.fullScreen);
        m_covering = m_covering || (w.active && w.covers);
    }
    if (m_fullScreen != wasFullScreen)
        emit fullScreenChanged();
    if (m_covering != wasCovering)
        emit coveringChanged();
    setCoveredScreen(covered);
    refresh();
}

#ifdef NOTCH_HAVE_DBUS
static const QString KWinService = QStringLiteral("org.kde.KWin");
static const QString DockService = QStringLiteral("org.olyzano.Notch");
static const QString WatchPlugin = QStringLiteral("notch-dock-watch");
static const QString ActionPlugin = QStringLiteral("notch-dock-action");

// Runs inside KWin for as long as Notch does: sends the window list back on every change.
// Plasma 6 names things window*, Plasma 5 client*; both are handled.
static const char WatchScript[] = R"JS(
function windows() { return workspace.windowList ? workspace.windowList() : workspace.clientList(); }
function current() { return workspace.activeWindow !== undefined ? workspace.activeWindow : workspace.activeClient; }
// Whether a window reaches the top of its screen's usable area (maximized, full screen or dragged up there):
// the notch's idle clock would sit on top of it there.
var maximizeArea = typeof KWin !== "undefined" && KWin.MaximizeArea !== undefined ? KWin.MaximizeArea : workspace.MaximizeArea;
function atTop(w) {
    if (w.fullScreen)
        return true;
    var g = w.frameGeometry, a = null;
    try { a = workspace.clientArea(maximizeArea, w); } catch (e) {}
    return !!(g && a && g.y <= a.y + 8);
}
// Fills its screen: full screen, or maximised (an app that draws its own title bar looks full screen too).
function covers(w) {
    if (w.fullScreen)
        return true;
    var g = w.frameGeometry, a = null;
    try { a = workspace.clientArea(maximizeArea, w); } catch (e) {}
    return !!(g && a && g.x <= a.x + 8 && g.y <= a.y + 8 && g.width >= a.width - 16 && g.height >= a.height - 16);
}
function screenOf(w) { return w.output ? String(w.output.name) : w.screen !== undefined ? "#" + w.screen : ""; }
// Windows are compared by id: the same window can come back as a different script object.
var lastActiveId = "";
function push(gone) {
    var active = current();
    var activeId = active ? String(active.internalId) : "";
    var goneId = gone ? String(gone.internalId) : "";
    var list = windows();
    var out = [];
    // Clicking Notch's own panel makes it the active window; it is no ordinary window, so the app that was in
    // front before it (and is still there, maybe full screen) stays the one in front.
    var normalActive = false;
    for (var j = 0; j < list.length; ++j)
        if (String(list[j].internalId) === activeId && list[j].normalWindow && !list[j].skipTaskbar)
            normalActive = true;
    if (normalActive)
        lastActiveId = activeId;
    else if (lastActiveId !== "")
        activeId = lastActiveId;
    for (var i = 0; i < list.length; ++i) {
        var w = list[i];
        var id = String(w.internalId);
        if (id === goneId || !w.normalWindow || w.skipTaskbar)
            continue;
        out.push({ id: id, app: String(w.desktopFileName || ""), cls: String(w.resourceClass || ""),
                   title: String(w.caption || ""), pid: w.pid || 0, active: id === activeId,
                   top: atTop(w), fs: !!w.fullScreen, cov: covers(w), scr: screenOf(w) });
    }
    callDBus("@SERVICE@", "/Dock", "org.olyzano.Notch.Dock", "kwinWindows", JSON.stringify(out));
}
// Titles change (tabs, documents): the dock's window list shows them. Moving or resizing only reports
// when the window starts or stops touching the top or being full screen, not on every step of a drag.
function placement(w) { return (atTop(w) ? "t" : "") + (w.fullScreen ? "f" : "") + (covers(w) ? "c" : ""); }
var tops = {};
function watchTitle(w) {
    if (!w)
        return;
    if (w.captionChanged)
        w.captionChanged.connect(function () { push(null); });
    var id = String(w.internalId);
    tops[id] = placement(w);
    var moved = function () {
        var t = placement(w);
        if (t !== tops[id]) {
            tops[id] = t;
            push(null);
        }
    };
    if (w.frameGeometryChanged)
        w.frameGeometryChanged.connect(moved);
    if (w.fullScreenChanged)
        w.fullScreenChanged.connect(moved);
}
var existing = windows();
for (var e = 0; e < existing.length; ++e)
    watchTitle(existing[e]);
(workspace.windowAdded || workspace.clientAdded).connect(function (w) { watchTitle(w); push(null); });
(workspace.windowRemoved || workspace.clientRemoved).connect(function (w) { push(w); });
(workspace.windowActivated || workspace.clientActivated).connect(function () { push(null); });
push(null);
)JS";

// One click's worth of window management. Windows are taken bottom-to-top in stacking order:
// not in front -> raise the topmost; in front alone -> minimize; in front with siblings -> raise the lowest (cycle).
static const char ActionScript[] = R"JS(
var ids = @IDS@;
var mode = "@MODE@";
var list = workspace.stackingOrder || (workspace.windowList ? workspace.windowList() : workspace.clientList());
var mine = [];
for (var i = 0; i < list.length; ++i)
    if (ids.indexOf(String(list[i].internalId)) >= 0)
        mine.push(list[i]);
var active = workspace.activeWindow !== undefined ? workspace.activeWindow : workspace.activeClient;
var activeId = active ? String(active.internalId) : "";
var front = -1;
for (var k = 0; k < mine.length; ++k)
    if (String(mine[k].internalId) === activeId)
        front = k;
function raise(w) {
    if (w.minimized)
        w.minimized = false;
    if (workspace.activeWindow !== undefined)
        workspace.activeWindow = w;
    else
        workspace.activeClient = w;
}
if (mine.length > 0) {
    if (mode === "raise") {
        raise(mine[0]);
    } else if (mode === "close") {
        for (var j = 0; j < mine.length; ++j)
            mine[j].closeWindow();
    } else if (front < 0) {
        raise(mine[mine.length - 1]);
    } else if (mine.length === 1) {
        mine[front].minimized = true;
    } else {
        raise(mine[front === 0 ? 1 : 0]); // the lowest other window: repeated clicks cycle through them
    }
}
)JS";

static QDBusMessage scriptingCall(const QString &method, const QVariantList &args = {})
{
    QDBusMessage msg = QDBusMessage::createMethodCall(KWinService, QStringLiteral("/Scripting"),
                                                      QStringLiteral("org.kde.kwin.Scripting"), method);
    msg.setArguments(args);
    return msg;
}

static QString writeScript(const QString &name, const QString &text)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/notch");
    QDir().mkpath(dir);
    QFile file(dir + u'/' + name);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};
    file.write(text.toUtf8());
    return file.fileName();
}

void AppDock::startKWin()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.interface() || !bus.interface()->isServiceRegistered(KWinService))
        return;
    if (!bus.registerService(DockService)
        || !bus.registerObject(QStringLiteral("/Dock"), this, QDBusConnection::ExportScriptableSlots)) {
        qCWarning(lcNotch) << "Dock: couldn't register on the session bus; running apps come from /proc";
        return;
    }
    const QString path = writeScript(QStringLiteral("dock-watch.js"),
                                     QString::fromUtf8(WatchScript).replace(QStringLiteral("@SERVICE@"), DockService));
    if (path.isEmpty())
        return;
    bus.call(scriptingCall(QStringLiteral("unloadScript"), {WatchPlugin})); // left over from a crash
    const QDBusMessage reply = bus.call(scriptingCall(QStringLiteral("loadScript"), {path, WatchPlugin}));
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().value(0).toInt() < 0) {
        qCWarning(lcNotch) << "Dock: KWin refused the window script; running apps come from /proc";
        return;
    }
    bus.call(scriptingCall(QStringLiteral("start")));
    m_kwin = true;
    qCInfo(lcNotch) << "Dock: window list from KWin";
}

void AppDock::runKWinAction(const QString &mode, const QStringList &windows)
{
    const QString ids = QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(windows)).toJson(QJsonDocument::Compact));
    // Every click gets its own script name and file. KWin removes an unloaded script later, not right away, so
    // reusing one name made every other click's loadScript fail ("already loaded") and the click did nothing.
    const int n = ++m_actions;
    const QString plugin = ActionPlugin + u'-' + QString::number(n);
    const QString path = writeScript(QStringLiteral("dock-action-%1.js").arg(n % 16), QString::fromUtf8(ActionScript)
                                                                                     .replace(QStringLiteral("@IDS@"), ids)
                                                                                     .replace(QStringLiteral("@MODE@"), mode));
    if (path.isEmpty())
        return;
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.asyncCall(scriptingCall(QStringLiteral("loadScript"), {path, plugin}));
    bus.asyncCall(scriptingCall(QStringLiteral("start")));
    m_pendingActions << plugin;
    // It has run by then; unload it so finished scripts don't pile up in KWin.
    QTimer::singleShot(3000, this, [this, plugin] {
        m_pendingActions.removeOne(plugin);
        QDBusConnection::sessionBus().asyncCall(scriptingCall(QStringLiteral("unloadScript"), {plugin}));
    });
}

AppDock::~AppDock()
{
    if (!m_kwin)
        return;
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.call(scriptingCall(QStringLiteral("unloadScript"), {WatchPlugin}));
    for (const QString &plugin : std::as_const(m_pendingActions))
        bus.call(scriptingCall(QStringLiteral("unloadScript"), {plugin}));
}
#else
void AppDock::startKWin() {}
void AppDock::runKWinAction(const QString &, const QStringList &) {}
AppDock::~AppDock() = default;
#endif

#ifdef Q_OS_WIN

void AppDock::loadCatalog()
{
    if (m_catalogLoaded)
        return;
    m_catalogLoaded = true;
    for (const QString &dir : QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation)) {
        QDirIterator it(dir, {QStringLiteral("*.lnk")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QFileInfo lnk(it.next());
            const QString target = QFileInfo(lnk.symLinkTarget()).absoluteFilePath();
            const QString name = lnk.completeBaseName();
            if (!target.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive)
                || name.contains(QLatin1String("uninstall"), Qt::CaseInsensitive))
                continue;
            DockApp app;
            app.id = target.toLower();
            app.name = name;
            app.command = {target};
            app.key = app.id;
            if (!m_catalog.contains(app.id))
                m_catalog.insert(app.id, app);
        }
    }
}

namespace {
struct Window {
    HWND handle;
    QString exe;
    QString title;
};

HWND toHandle(const QString &id) { return reinterpret_cast<HWND>(id.toULongLong()); }

BOOL CALLBACK collectWindow(HWND hwnd, LPARAM lparam)
{
    if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER))
        return TRUE;
    const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((ex & WS_EX_TOOLWINDOW) && !(ex & WS_EX_APPWINDOW))
        return TRUE;
    wchar_t title[256];
    const int titleLength = GetWindowTextW(hwnd, title, 256);
    if (titleLength == 0 || QString::fromWCharArray(title, titleLength) == QLatin1String("Program Manager"))
        return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId())
        return TRUE;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return TRUE;
    wchar_t path[MAX_PATH * 2];
    DWORD length = MAX_PATH * 2;
    if (QueryFullProcessImageNameW(process, 0, path, &length)) {
        const QString exe = QDir::fromNativeSeparators(QString::fromWCharArray(path, int(length)));
        const QString base = QFileInfo(exe).fileName().toLower();
        if (base != QLatin1String("applicationframehost.exe") && base != QLatin1String("textinputhost.exe"))
            reinterpret_cast<QList<Window> *>(lparam)->append({hwnd, exe, QString::fromWCharArray(title, titleLength)});
    }
    CloseHandle(process);
    return TRUE;
}

void raise(HWND hwnd)
{
    if (IsIconic(hwnd))
        ShowWindow(hwnd, SW_RESTORE);
    SetForegroundWindow(hwnd);
}
} // namespace

QList<DockApp> AppDock::runningApps(QString *activeId)
{
    QList<Window> windows; // front to back
    EnumWindows(collectWindow, reinterpret_cast<LPARAM>(&windows));

    m_appWindows.clear();
    m_windowTitles.clear();
    QList<DockApp> running;
    const HWND foreground = GetForegroundWindow();
    for (const Window &w : windows) {
        const QString id = w.exe.toLower();
        if (w.handle == foreground)
            *activeId = id;
        const bool seen = m_appWindows.contains(id);
        const QString handle = QString::number(reinterpret_cast<quintptr>(w.handle));
        m_appWindows[id] << handle;
        m_windowTitles.insert(handle, w.title);
        if (seen)
            continue;
        DockApp app = m_catalog.value(id);
        if (!app.valid()) {
            app.id = id;
            app.name = QFileInfo(w.exe).completeBaseName();
            app.command = {w.exe};
            app.key = id;
            m_seenRunning.insert(id, app);
        }
        running << app;
    }
    return running;
}

void AppDock::activateWindow(const QString &windowId)
{
    raise(toHandle(windowId));
}

void AppDock::activate(const QString &id)
{
    if (openIfFolder(id))
        return;
    refresh(); // the window list may be up to a poll old
    const QStringList windows = m_appWindows.value(id);
    if (windows.isEmpty()) {
        launchNew(id);
        return;
    }
    const HWND foreground = GetForegroundWindow();
    bool inFront = false;
    for (const QString &w : windows)
        inFront = inFront || toHandle(w) == foreground;
    if (!inFront)
        raise(toHandle(windows.first()));
    else if (windows.size() == 1)
        ShowWindow(foreground, SW_MINIMIZE);
    else
        raise(toHandle(windows.last()));
}

void AppDock::close(const QString &id)
{
    for (const QString &w : m_appWindows.value(id))
        PostMessageW(toHandle(w), WM_CLOSE, 0, 0);
}

#elif defined(Q_OS_LINUX)

void AppDock::loadCatalog()
{
    if (m_catalogLoaded)
        return;
    m_catalogLoaded = true;
    // standardLocations lists the user's directory first, so their entries override the system ones.
    for (const QString &dir : QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation)) {
        const QDir root(dir);
        QDirIterator it(dir, {QStringLiteral("*.desktop")}, QDir::Files, QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
        while (it.hasNext()) {
            const QString path = it.next();
            QString id = root.relativeFilePath(path);
            id.replace(u'/', u'-');
            if (m_catalog.contains(id))
                continue;
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
                continue;
            const DockApp app = dockmodel::parseDesktopEntry(id, file.readAll());
            if (app.valid())
                m_catalog.insert(id, app);
        }
    }
}

QList<DockApp> AppDock::runningApps(QString *activeId)
{
    m_appWindows.clear();
    m_windowTitles.clear();
    QList<DockApp> running;

    if (m_kwin) {
        QSet<QString> seen;
        for (const KWinWindow &w : std::as_const(m_kwinWindows)) {
            DockApp app = m_catalog.value(dockmodel::matchWindow(m_catalog, w.desktopFile, w.windowClass));
            if (app.id.isEmpty()) {
                // Not an installed app (e.g. started from a terminal): still shown, under its window class.
                const QString cls = !w.windowClass.isEmpty() ? w.windowClass : w.desktopFile;
                if (cls.isEmpty())
                    continue;
                app.id = QStringLiteral("window:") + cls.toLower();
                app.name = cls.left(1).toUpper() + cls.mid(1);
                app.icon = !w.desktopFile.isEmpty() ? w.desktopFile : cls.toLower();
                m_seenRunning.insert(app.id, app);
            }
            m_appWindows[app.id] << w.id;
            m_windowTitles.insert(w.id, w.caption);
            if (w.active)
                *activeId = app.id;
            if (!seen.contains(app.id)) {
                seen.insert(app.id);
                running << app;
            }
        }
        return running;
    }

    // No KWin: a GUI process of an app shows up in /proc with that app's binary and a display in its environment.
    // Launchers and interpreters that front many apps would mark them all as running.
    static const QSet<QString> ignored = {QStringLiteral("flatpak"), QStringLiteral("snap"), QStringLiteral("env"),
                                          QStringLiteral("sh"), QStringLiteral("bash"), QStringLiteral("gtk-launch"),
                                          QStringLiteral("notch"), QStringLiteral("python3"), QStringLiteral("python")};
    QHash<QString, DockApp> byKey;
    for (const DockApp &app : std::as_const(m_catalog)) {
        if (ignored.contains(app.key))
            continue;
        const auto found = byKey.constFind(app.key);
        if (found == byKey.constEnd() || app.id < found->id)
            byKey.insert(app.key, app);
    }
    QSet<QString> seen;
    const QStringList pids = QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &pid : pids) {
        bool numeric = false;
        pid.toInt(&numeric);
        if (!numeric)
            continue;
        const QString base = QStringLiteral("/proc/") + pid;
        QString key;
        QFile cmdline(base + QStringLiteral("/cmdline"));
        if (cmdline.open(QIODevice::ReadOnly)) {
            QStringList args;
            for (const QByteArray &part : cmdline.read(4096).split('\0'))
                if (!part.isEmpty())
                    args << QString::fromUtf8(part);
            key = dockmodel::binaryName(args);
        }
        if (!byKey.contains(key))
            key = QFileInfo(base + QStringLiteral("/exe")).symLinkTarget().section(u'/', -1).toLower(); // e.g. firefox started via a wrapper
        if (!byKey.contains(key) || seen.contains(key))
            continue;
        QFile environ(base + QStringLiteral("/environ"));
        if (!environ.open(QIODevice::ReadOnly))
            continue;
        const QByteArray env = QByteArray("\0", 1) + environ.readAll(); // entries are NUL-separated
        const auto has = [&](const char *name) { return env.contains(QByteArray("\0", 1) + name); };
        if (!has("WAYLAND_DISPLAY=") && !has("DISPLAY="))
            continue; // a daemon, not a window
        seen.insert(key);
        running << byKey.value(key);
    }
    return running;
}

void AppDock::activateWindow(const QString &windowId)
{
    if (m_kwin)
        runKWinAction(QStringLiteral("raise"), {windowId});
}

void AppDock::activate(const QString &id)
{
    if (openIfFolder(id))
        return;
    const QStringList windows = m_appWindows.value(id);
    if (m_kwin && !windows.isEmpty())
        runKWinAction(QStringLiteral("focus"), windows);
    else
        launchNew(id); // without KWin there is no window to bring back; single-instance apps raise themselves
}

void AppDock::close(const QString &id)
{
    const QStringList windows = m_appWindows.value(id);
    if (m_kwin && !windows.isEmpty())
        runKWinAction(QStringLiteral("close"), windows);
}

#else

void AppDock::loadCatalog() { m_catalogLoaded = true; }
QList<DockApp> AppDock::runningApps(QString *) { return {}; }
void AppDock::activate(const QString &id)
{
    if (!openIfFolder(id))
        launchNew(id);
}
void AppDock::activateWindow(const QString &) {}
void AppDock::close(const QString &) {}

#endif
