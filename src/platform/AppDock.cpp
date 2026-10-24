#include "platform/AppDock.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileIconProvider>
#include <QIcon>
#include <QPixmap>
#include <QSet>
#include <QStandardPaths>

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
    m_pinned = m_settings.value(QStringLiteral("pinned")).toStringList();
    m_timer.setInterval(2000);
    connect(&m_timer, &QTimer::timeout, this, &AppDock::refresh);
}

void AppDock::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    m_settings.setValue(QStringLiteral("enabled"), enabled);
    emit enabledChanged();
    if (enabled)
        refresh();
}

void AppDock::setWatching(bool watching)
{
    if (!watching) {
        m_timer.stop();
        return;
    }
    refresh();
    m_timer.start();
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
    const QFileInfo exe(app.valid() ? app.command.first() : id);
    if (exe.exists())
        return QFileIconProvider().icon(exe);
    return QFileIconProvider().icon(QFileIconProvider::File);
#else
    QIcon icon;
    if (QDir::isAbsolutePath(app.icon))
        icon = QIcon(app.icon);
    else if (!app.icon.isEmpty())
        icon = QIcon::fromTheme(app.icon);
    return icon.isNull() ? QIcon::fromTheme(QStringLiteral("application-x-executable")) : icon;
#endif
}

void AppDock::refresh()
{
    loadCatalog();
    QString active;
    m_running = runningApps(&active);
    const QVariantList items = [&] {
        QVariantList merged = dockmodel::merge(m_pinned, m_catalog, m_running, active);
        for (QVariant &v : merged) {
            QVariantMap m = v.toMap();
            m[QStringLiteral("iconSource")] = iconSource(m.value(QStringLiteral("id")).toString());
            v = m;
        }
        return merged;
    }();
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
} // namespace

QList<DockApp> AppDock::runningApps(QString *activeId)
{
    QList<Window> windows; // front to back
    EnumWindows(collectWindow, reinterpret_cast<LPARAM>(&windows));

    m_windows.clear();
    QList<DockApp> running;
    for (const Window &w : windows) {
        const QString id = w.exe.toLower();
        if (m_windows.contains(id))
            continue;
        m_windows.insert(id, reinterpret_cast<quintptr>(w.handle));
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
    const HWND foreground = GetForegroundWindow();
    for (const Window &w : windows)
        if (w.handle == foreground)
            *activeId = w.exe.toLower();
    return running;
}

void AppDock::launch(const QString &id)
{
    if (const quintptr handle = m_windows.value(id)) {
        const HWND hwnd = reinterpret_cast<HWND>(handle);
        if (IsIconic(hwnd))
            ShowWindow(hwnd, SW_RESTORE);
        SetForegroundWindow(hwnd);
        return;
    }
    const DockApp app = m_catalog.contains(id) ? m_catalog.value(id) : m_seenRunning.value(id);
    if (app.valid())
        QProcess::startDetached(app.command.first(), app.command.mid(1));
    else if (QFileInfo::exists(id))
        QProcess::startDetached(id, {});
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

// A GUI process of an app shows up in /proc with that app's binary and a display in its environment.
QList<DockApp> AppDock::runningApps(QString *)
{
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

    QList<DockApp> running;
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

void AppDock::launch(const QString &id)
{
    const DockApp app = m_catalog.value(id);
    if (app.valid())
        QProcess::startDetached(app.command.first(), app.command.mid(1));
}

#else

void AppDock::loadCatalog() { m_catalogLoaded = true; }
QList<DockApp> AppDock::runningApps(QString *) { return {}; }
void AppDock::launch(const QString &) {}

#endif
