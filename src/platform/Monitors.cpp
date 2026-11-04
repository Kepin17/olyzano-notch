#include "Monitors.h"

#include "platform/MonitorModel.h"
#include "platform/NotchPaths.h"

#include <QGuiApplication>
#include <QScreen>

Monitors::Monitors(QObject *parent)
    : QObject(parent), m_settings(NotchPaths::conf(QStringLiteral("monitors")), QSettings::IniFormat)
{
    const QStringList off = m_settings.value(QStringLiteral("off")).toStringList();
    m_disabled = QSet<QString>(off.begin(), off.end());
    for (QScreen *screen : QGuiApplication::screens())
        watch(screen);
    connect(qApp, &QGuiApplication::screenAdded, this, [this](QScreen *screen) { watch(screen); emit changed(); });
    connect(qApp, &QGuiApplication::screenRemoved, this, [this] { emit changed(); });
    connect(qApp, &QGuiApplication::primaryScreenChanged, this, [this] { emit changed(); });
}

void Monitors::watch(QScreen *screen)
{
    connect(screen, &QScreen::geometryChanged, this, [this] { emit changed(); });
}

QStringList Monitors::connectedNames() const
{
    QStringList names;
    for (const QScreen *screen : QGuiApplication::screens())
        names << screen->name();
    return names;
}

int Monitors::count() const { return int(QGuiApplication::screens().size()); }

bool Monitors::shown(const QScreen *screen) const
{
    return MonitorModel::effectiveShown(connectedNames(), m_disabled).contains(screen->name());
}

QVariantList Monitors::screens() const
{
    const QStringList shownNames = MonitorModel::effectiveShown(connectedNames(), m_disabled);
    QVariantList out;
    for (const QScreen *screen : QGuiApplication::screens()) {
        const QRect g = screen->geometry();
        const QSize px = (QSizeF(g.size()) * screen->devicePixelRatio()).toSize();
        QString name = MonitorModel::label(screen->name(), px.width(), px.height());
        const QString maker = (screen->manufacturer() + QLatin1Char(' ') + screen->model()).trimmed();
        out.append(QVariantMap{{QStringLiteral("name"), screen->name()}, {QStringLiteral("label"), name}, {QStringLiteral("model"), maker},
                               {QStringLiteral("x"), g.x()}, {QStringLiteral("y"), g.y()}, {QStringLiteral("w"), g.width()}, {QStringLiteral("h"), g.height()},
                               {QStringLiteral("primary"), screen == QGuiApplication::primaryScreen()},
                               {QStringLiteral("shown"), shownNames.contains(screen->name())},
                               {QStringLiteral("options"), options(screen->name())}});
    }
    return out;
}

static QString optionKey(const QString &name, const QString &key)
{
    // a name can hold slashes and backslashes (\\.\DISPLAY1): keep it one key
    return QStringLiteral("screen/") + QString::fromLatin1(name.toUtf8().toPercentEncoding()) + QLatin1Char('.') + key;
}

static const QStringList OptionKeys{QStringLiteral("scale"), QStringLiteral("mode"), QStringLiteral("dock"), QStringLiteral("bar")};

int Monitors::option(const QString &name, const QString &key) const
{
    return m_settings.value(optionKey(name, key), 0).toInt();
}

QVariantMap Monitors::options(const QString &name) const
{
    QVariantMap out;
    for (const QString &key : OptionKeys)
        out.insert(key, option(name, key));
    return out;
}

void Monitors::setOption(const QString &name, const QString &key, int value)
{
    if (!OptionKeys.contains(key) || value < 0 || value > 3 || option(name, key) == value)
        return;
    if (value == 0)
        m_settings.remove(optionKey(name, key));
    else
        m_settings.setValue(optionKey(name, key), value);
    m_settings.sync();
    emit changed();
}

bool Monitors::setShown(const QString &name, bool on)
{
    if (!on && !MonitorModel::canDisable(name, connectedNames(), m_disabled)) {
        emit rejected(name);
        return false;
    }
    if (on)
        m_disabled.remove(name);
    else
        m_disabled.insert(name);
    m_settings.setValue(QStringLiteral("off"), QStringList(m_disabled.begin(), m_disabled.end()));
    m_settings.sync();
    emit changed();
    return true;
}
