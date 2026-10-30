#pragma once

#include <QStandardPaths>
#include <QString>

// Canonical paths under the Notch config / data roots. Every feature should go through
// these instead of concatenating GenericConfigLocation by hand, so the layout stays
// consistent and easy to audit.
namespace NotchPaths {

// ~/.config/notch (GenericConfigLocation/notch on every platform)
inline QString baseDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/notch");
}

// ~/.config/notch/<name>.conf
inline QString conf(const QString &name)
{
    return baseDir() + QLatin1Char('/') + name + QStringLiteral(".conf");
}

// ~/.config/notch/dev/ — Dev hub (SSH + Database) config root
inline QString devDir()
{
    return baseDir() + QStringLiteral("/dev");
}

// ~/.local/share/notch/<sub>
inline QString dataDir(const QString &sub = {})
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/notch");
    return sub.isEmpty() ? root : root + QLatin1Char('/') + sub;
}

} // namespace NotchPaths
