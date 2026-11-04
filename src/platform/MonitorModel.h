#pragma once

#include <QSet>
#include <QString>
#include <QStringList>

// Which monitors show Notch (notch, status bar and dock). Pure, so it is tested without a screen.
namespace MonitorModel {

// The connected monitors that show Notch: all but the ones switched off. If every connected monitor was switched off (one was
// unplugged after another was turned off, say) all of them show: there is never a computer with no Notch at all.
inline QStringList effectiveShown(const QStringList &connected, const QSet<QString> &disabled)
{
    QStringList shown;
    for (const QString &name : connected)
        if (!disabled.contains(name))
            shown << name;
    return shown.isEmpty() ? connected : shown;
}

// A monitor may be switched off unless it is the last one that shows Notch.
inline bool canDisable(const QString &name, const QStringList &connected, const QSet<QString> &disabled)
{
    const QStringList shown = effectiveShown(connected, disabled);
    return !(shown.size() <= 1 && shown.contains(name));
}

// "DP-1 · 2560×1440": what a tile on the map says about its monitor.
inline QString label(const QString &name, int width, int height)
{
    return width > 0 && height > 0 ? QStringLiteral("%1 · %2×%3").arg(name).arg(width).arg(height) : name;
}

// What one monitor does differently from the general settings. 0 always means "as in the general settings".
//   scale: 1 small, 2 normal, 3 large (the capsule size)   mode: 1 capsule, 2 hidden   dock, bar: 1 on, 2 off
inline double scaleFor(int option, double general)
{
    switch (option) {
    case 1: return 0.85;
    case 2: return 1.0;
    case 3: return 1.15;
    }
    return general;
}

inline QString modeFor(int option, const QString &general)
{
    return option == 1 ? QStringLiteral("capsule") : option == 2 ? QStringLiteral("hidden") : general;
}

inline bool flagFor(int option, bool general) { return option == 1 ? true : option == 2 ? false : general; }

} // namespace MonitorModel
