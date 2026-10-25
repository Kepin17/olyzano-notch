#include "platform/AppDock.h"

#include "models/FileItem.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileIconProvider>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

QIcon AppDock::iconFor(const QString &id) const
{
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
    QString active;
    const QList<DockApp> running = runningApps(&active);
    QVariantList items = dockmodel::merge(m_pinned, m_catalog, running, active, showRunning());
    for (QVariant &v : items) {
        QVariantMap m = v.toMap();
        m[QStringLiteral("iconSource")] = iconSource(m.value(QStringLiteral("id")).toString());
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
        w.pid = o.value(QLatin1String("pid")).toInteger();
        w.active = o.value(QLatin1String("active")).toBool();
        if (!w.id.isEmpty() && w.pid != self)
            m_kwinWindows << w;
    }
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
function push(gone) {
    var active = current();
    var list = windows();
    var out = [];
    for (var i = 0; i < list.length; ++i) {
        var w = list[i];
        if (w === gone || !w.normalWindow || w.skipTaskbar)
            continue;
        out.push({ id: String(w.internalId), app: String(w.desktopFileName || ""), cls: String(w.resourceClass || ""),
                   pid: w.pid || 0, active: w === active });
    }
    callDBus("@SERVICE@", "/Dock", "org.olyzano.Notch.Dock", "kwinWindows", JSON.stringify(out));
}
(workspace.windowAdded || workspace.clientAdded).connect(function () { push(null); });
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
function raise(w) {
    if (w.minimized)
        w.minimized = false;
    if (workspace.activeWindow !== undefined)
        workspace.activeWindow = w;
    else
        workspace.activeClient = w;
}
if (mine.length > 0) {
    if (mode === "close") {
        for (var j = 0; j < mine.length; ++j)
            mine[j].closeWindow();
    } else if (mine.indexOf(active) < 0) {
        raise(mine[mine.length - 1]);
    } else if (mine.length === 1) {
        active.minimized = true;
    } else {
        raise(mine[0]);
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
    const QString path = writeScript(QStringLiteral("dock-action.js"), QString::fromUtf8(ActionScript)
                                                                           .replace(QStringLiteral("@IDS@"), ids)
                                                                           .replace(QStringLiteral("@MODE@"), mode));
    if (path.isEmpty())
        return;
    // Queued in order on one connection, so KWin unloads the previous click's script before loading this one.
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.asyncCall(scriptingCall(QStringLiteral("unloadScript"), {ActionPlugin}));
    bus.asyncCall(scriptingCall(QStringLiteral("loadScript"), {path, ActionPlugin}));
    bus.asyncCall(scriptingCall(QStringLiteral("start")));
}

AppDock::~AppDock()
{
    if (!m_kwin)
        return;
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.call(scriptingCall(QStringLiteral("unloadScript"), {WatchPlugin}));
    bus.call(scriptingCall(QStringLiteral("unloadScript"), {ActionPlugin}));
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
            reinterpret_cast<QList<Window> *>(lparam)->append({hwnd, exe});
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
    QList<DockApp> running;
    const HWND foreground = GetForegroundWindow();
    for (const Window &w : windows) {
        const QString id = w.exe.toLower();
        if (w.handle == foreground)
            *activeId = id;
        const bool seen = m_appWindows.contains(id);
        m_appWindows[id] << QString::number(reinterpret_cast<quintptr>(w.handle));
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

void AppDock::activate(const QString &id)
{
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

void AppDock::activate(const QString &id)
{
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
void AppDock::activate(const QString &id) { launchNew(id); }
void AppDock::close(const QString &) {}

#endif
