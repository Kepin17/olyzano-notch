#include "platform/Launcher.h"

#include "platform/AppDock.h"

#include <QStandardPaths>

Launcher::Launcher(AppDock *dock, QObject *parent)
    : QObject(parent)
    , m_dock(dock)
    , m_settings(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/notch/launcher.conf"), QSettings::IniFormat)
{
    m_pinned = m_settings.value(QStringLiteral("pinned")).toStringList();
    m_recent = m_settings.value(QStringLiteral("recent")).toStringList();
    m_settings.beginGroup(QStringLiteral("uses"));
    for (const QString &k : m_settings.childKeys())
        m_uses.insert(QString::fromUtf8(QByteArray::fromPercentEncoding(k.toUtf8())), m_settings.value(k).toInt());
    m_settings.endGroup();
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
