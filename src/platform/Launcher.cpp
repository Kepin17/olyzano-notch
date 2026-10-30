#include "platform/NotchPaths.h"
#include "platform/Launcher.h"

#include <QCoreApplication>
#include <QKeySequence>

#include "platform/AppDock.h"
#include "platform/ProcessRunner.h"

#include <QStandardPaths>

#ifdef NOTCH_HAVE_DBUS
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#endif

namespace {
// What KWin reads for a lone press of the Windows key: service, path, interface, method.
const QString NotchKey = QStringLiteral("org.olyzano.Notch,/Launcher,org.olyzano.Notch,toggle");

QString kwinMeta()
{
    return QString::fromUtf8(ProcessRunner::captureOut(QStringLiteral("kreadconfig6"),
                                                       {QStringLiteral("--file"), QStringLiteral("kwinrc"),
                                                        QStringLiteral("--group"), QStringLiteral("ModifierOnlyShortcuts"),
                                                        QStringLiteral("--key"), QStringLiteral("Meta")},
                                                       1500)).trimmed();
}
void writeKwinMeta(const QString &value) // empty: back to KWin's own default (Plasma's launcher)
{
    QStringList args{QStringLiteral("--file"), QStringLiteral("kwinrc"), QStringLiteral("--group"), QStringLiteral("ModifierOnlyShortcuts"), QStringLiteral("--key"), QStringLiteral("Meta")};
    if (value.isEmpty())
        args << QStringLiteral("--delete");
    else
        args << value;
    QProcess::execute(QStringLiteral("kwriteconfig6"), args);
#ifdef NOTCH_HAVE_DBUS
    QDBusConnection::sessionBus().send(QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/KWin"), QStringLiteral("org.kde.KWin"), QStringLiteral("reconfigure")));
#endif
}
}

Launcher::Launcher(AppDock *dock, QObject *parent)
    : QObject(parent)
    , m_dock(dock)
    , m_settings(NotchPaths::conf(QStringLiteral("launcher")), QSettings::IniFormat)
{
    m_pinned = m_settings.value(QStringLiteral("pinned")).toStringList();
    m_recent = m_settings.value(QStringLiteral("recent")).toStringList();
    m_settings.beginGroup(QStringLiteral("uses"));
    for (const QString &k : m_settings.childKeys())
        m_uses.insert(QString::fromUtf8(QByteArray::fromPercentEncoding(k.toUtf8())), m_settings.value(k).toInt());
    m_settings.endGroup();
#ifdef NOTCH_HAVE_DBUS
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.isConnected() && bus.registerService(QStringLiteral("org.olyzano.Notch")))
        bus.registerObject(QStringLiteral("/Launcher"), this, QDBusConnection::ExportScriptableSlots);
    if (searchEnabled())
        m_dock->setSearchShortcut(true, searchKey());
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] { m_dock->setSearchShortcut(false, QString()); }); // the shortcut goes with the program
#endif
}

void Launcher::setSearchEnabled(bool on)
{
    if (on == searchEnabled())
        return;
    m_settings.setValue(QStringLiteral("searchEnabled"), on);
#ifdef NOTCH_HAVE_DBUS
    m_dock->setSearchShortcut(on, searchKey());
#endif
    emit searchEnabledChanged();
}

// One key combination, as Qt writes it ("Shift+Space", "Ctrl+Alt+K"); anything that is not exactly one falls back to the default.
QString Launcher::searchKey() const
{
    const QKeySequence k(m_settings.value(QStringLiteral("searchKey"), QStringLiteral("Shift+Space")).toString(), QKeySequence::PortableText);
    return k.count() == 1 ? k.toString(QKeySequence::PortableText) : QStringLiteral("Shift+Space");
}

void Launcher::setSearchKey(const QString &key)
{
    const QKeySequence k(key, QKeySequence::PortableText);
    if (k.count() != 1 || k.toString(QKeySequence::PortableText) == searchKey())
        return;
    m_settings.setValue(QStringLiteral("searchKey"), k.toString(QKeySequence::PortableText));
#ifdef NOTCH_HAVE_DBUS
    if (searchEnabled())
        m_dock->setSearchShortcut(true, searchKey());
#endif
    emit searchEnabledChanged();
}

QString Launcher::searchKeyLabel() const
{
    return searchKey().replace(QLatin1String("Meta"), QLatin1String("Win")).replace(u'+', QLatin1String(" + "));
}

QString Launcher::describeKey(int key, int modifiers) const
{
    switch (key) { // a modifier alone is not a shortcut
    case Qt::Key_Shift: case Qt::Key_Control: case Qt::Key_Alt: case Qt::Key_Meta: case Qt::Key_AltGr: case Qt::Key_Super_L: case Qt::Key_Super_R: case Qt::Key_unknown:
        return {};
    default:
        break;
    }
    if (!(modifiers & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)))
        return {}; // a bare key would steal typing
    return QKeySequence(modifiers | key).toString(QKeySequence::PortableText);
}

bool Launcher::superKeyAvailable() const
{
#ifdef NOTCH_HAVE_DBUS
    return !QStandardPaths::findExecutable(QStringLiteral("kwriteconfig6")).isEmpty() && !QStandardPaths::findExecutable(QStringLiteral("kreadconfig6")).isEmpty();
#else
    return false;
#endif
}

QString Launcher::superKeyStatus() const
{
    if (!superKeyAvailable())
        return QStringLiteral("unavailable");
    const QString v = kwinMeta();
    return v == NotchKey ? QStringLiteral("notch") : v.isEmpty() || v.contains(QLatin1String("activateLauncherMenu")) ? QStringLiteral("plasma") : QStringLiteral("other");
}

QByteArray Launcher::superKeyReport() const
{
    bool answers = false;
#ifdef NOTCH_HAVE_DBUS
    answers = QDBusConnection::sessionBus().interface() && QDBusConnection::sessionBus().interface()->isServiceRegistered(QStringLiteral("org.olyzano.Notch"));
#endif
    return QStringLiteral("windows key: %1\nkwin value: %2\nnotch listening on D-Bus (org.olyzano.Notch): %3\n")
        .arg(superKeyStatus(), kwinMeta().isEmpty() ? QStringLiteral("(default, Plasma's launcher)") : kwinMeta(), answers ? QStringLiteral("yes (this process)") : QStringLiteral("no")).toUtf8();
}

bool Launcher::superKey() const
{
    return superKeyAvailable() && kwinMeta() == NotchKey;
}

void Launcher::setSuperKey(bool on)
{
    if (!superKeyAvailable() || on == superKey())
        return;
    if (on) {
        m_settings.setValue(QStringLiteral("previousMeta"), kwinMeta()); // empty = Plasma's own default
        writeKwinMeta(NotchKey);
    } else {
        writeKwinMeta(m_settings.value(QStringLiteral("previousMeta")).toString());
    }
    emit superKeyChanged();
}

QVariantList Launcher::all()
{
    QVariantList apps = m_dock->launcherApps();
    for (QVariant &v : apps) {
        QVariantMap a = v.toMap();
        a[QStringLiteral("pinned")] = m_pinned.contains(a.value(QStringLiteral("id")).toString());
        v = a;
    }
    return apps;
}

QStringList Launcher::categories()
{
    static const QStringList order{QStringLiteral("Internet"), QStringLiteral("Media"), QStringLiteral("Office"), QStringLiteral("Development"), QStringLiteral("Graphics"),
                                   QStringLiteral("Games"), QStringLiteral("Utilities"), QStringLiteral("System"), QStringLiteral("Other")};
    QSet<QString> have;
    for (const QVariant &v : all())
        have.insert(v.toMap().value(QStringLiteral("category")).toString());
    QStringList out;
    for (const QString &c : order)
        if (have.contains(c))
            out << c;
    return out;
}

QVariantList Launcher::search(const QString &query, const QString &category)
{
    return LauncherModel::search(all(), query, category, m_uses);
}

QVariantList Launcher::pinnedApps()
{
    const QVariantList apps = all();
    QVariantList out;
    for (const QString &id : std::as_const(m_pinned))
        for (const QVariant &v : apps)
            if (v.toMap().value(QStringLiteral("id")).toString() == id) {
                out << v;
                break;
            }
    return out;
}

QVariantList Launcher::recentApps(int count)
{
    const QVariantList apps = all();
    QVariantList out;
    for (const QString &id : std::as_const(m_recent)) {
        if (out.size() >= count)
            break;
        for (const QVariant &v : apps)
            if (v.toMap().value(QStringLiteral("id")).toString() == id) {
                out << v;
                break;
            }
    }
    return out;
}

void Launcher::launch(const QString &id)
{
    m_dock->activate(id);
    m_uses[id] += 1;
    m_recent.removeAll(id);
    m_recent.prepend(id);
    while (m_recent.size() > 12)
        m_recent.removeLast();
    m_settings.setValue(QStringLiteral("recent"), m_recent);
    m_settings.setValue(QStringLiteral("uses/") + QString::fromLatin1(id.toUtf8().toPercentEncoding()), m_uses.value(id));
}

void Launcher::setPinned(const QString &id, bool pinned)
{
    if (pinned == m_pinned.contains(id))
        return;
    if (pinned)
        m_pinned << id;
    else
        m_pinned.removeAll(id);
    m_settings.setValue(QStringLiteral("pinned"), m_pinned);
    emit changed();
}
