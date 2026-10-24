#pragma once

#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <algorithm>

// Pure logic behind the app dock (no OS calls, so tests can run it): what an app is, how a Linux
// .desktop entry becomes one, and how pinned + running apps merge into the list the dock shows.
struct DockApp {
    QString id;      // stable key stored in the pin list: desktop file id (Linux) / exe path (Windows)
    QString name;
    QString icon;    // theme icon name or absolute file path (Linux); unused on Windows
    QStringList command; // program + arguments that open it
    QString key;     // what a running process is matched on: lower-cased binary name (Linux) / exe path
    bool valid() const { return !id.isEmpty() && !name.isEmpty() && !command.isEmpty(); }
};

namespace dockmodel {

// "env FOO=1 /usr/bin/app --flag %U" -> ["env", "FOO=1", "/usr/bin/app", "--flag"]: Exec= field codes are
// placeholders for files/URLs the dock never passes, so they are dropped (and %% is a literal %).
inline QStringList execArguments(const QString &exec)
{
    QStringList out;
    for (QString arg : QProcess::splitCommand(exec)) {
        arg.replace(QStringLiteral("%%"), QStringLiteral("\x01"));
        if (arg.size() == 2 && arg[0] == u'%')
            continue;
        arg.replace(QStringLiteral("\x01"), QStringLiteral("%"));
        out << arg;
    }
    return out;
}

// The binary a command line really launches, skipping `env` and VAR=value prefixes.
inline QString binaryName(const QStringList &args)
{
    for (const QString &arg : args) {
        if (arg == QLatin1String("env") || (arg.contains(u'=') && !arg.startsWith(u'/')))
            continue;
        return QFileInfo(arg).fileName().toLower();
    }
    return {};
}

// A freedesktop .desktop file. Returns an invalid app for anything the dock shouldn't list
// (hidden entries, non-applications, no Exec).
inline DockApp parseDesktopEntry(const QString &id, const QByteArray &contents)
{
    QHash<QString, QString> keys;
    bool inEntry = false;
    for (const QByteArray &raw : contents.split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.startsWith(u'[')) {
            inEntry = line == QLatin1String("[Desktop Entry]");
            continue;
        }
        const int eq = line.indexOf(u'=');
        if (!inEntry || line.startsWith(u'#') || eq <= 0)
            continue;
        keys.insert(line.left(eq).trimmed(), line.mid(eq + 1).trimmed());
    }
    DockApp app;
    if (keys.value(QStringLiteral("Type")) != QLatin1String("Application")
        || keys.value(QStringLiteral("NoDisplay")) == QLatin1String("true")
        || keys.value(QStringLiteral("Hidden")) == QLatin1String("true"))
        return app;
    const QStringList args = execArguments(keys.value(QStringLiteral("Exec")));
    app.id = id;
    app.name = keys.value(QStringLiteral("Name"));
    app.icon = keys.value(QStringLiteral("Icon"));
    app.command = args;
    app.key = binaryName(args);
    if (args.isEmpty() || app.key.isEmpty())
        return {};
    return app;
}

// Pinned apps first, in their saved order (an app that is no longer installed keeps a placeholder so
// pinning survives uninstalling/reinstalling), then running apps that aren't pinned.
// `running` holds one entry per running app, `activeId` the focused one (may be empty).
inline QVariantList merge(const QStringList &pinned, const QHash<QString, DockApp> &catalog,
                          const QList<DockApp> &running, const QString &activeId)
{
    QHash<QString, DockApp> runningById;
    for (const DockApp &app : running)
        runningById.insert(app.id, app);

    auto entry = [&](const DockApp &app, bool isPinned) {
        QVariantMap m;
        m[QStringLiteral("id")] = app.id;
        m[QStringLiteral("name")] = app.name;
        m[QStringLiteral("pinned")] = isPinned;
        m[QStringLiteral("running")] = runningById.contains(app.id);
        m[QStringLiteral("active")] = !activeId.isEmpty() && app.id == activeId;
        return m;
    };

    QVariantList items;
    for (const QString &id : pinned) {
        DockApp app = catalog.value(id, runningById.value(id));
        if (!app.valid()) {
            app.id = id;
            app.name = QFileInfo(id).completeBaseName();
        }
        items << entry(app, true);
    }
    QList<DockApp> rest;
    for (const DockApp &app : running)
        if (!pinned.contains(app.id))
            rest << app;
    std::sort(rest.begin(), rest.end(), [](const DockApp &a, const DockApp &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    for (const DockApp &app : rest)
        items << entry(app, false);
    return items;
}

// Reorder helper behind drag-to-rearrange; out-of-range moves are ignored.
inline QStringList move(QStringList list, int from, int to)
{
    if (from < 0 || from >= list.size() || to < 0 || to >= list.size() || from == to)
        return list;
    list.move(from, to);
    return list;
}

} // namespace dockmodel
