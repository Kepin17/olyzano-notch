#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QWindow>

// All window-system specifics live here. One Notch window per monitor: on Wayland an app
// can't read the global cursor position, so each output gets its own top-edge strip and
// whichever the cursor reaches opens.
// Wayland: wlr-layer-shell via LayerShellQt (compositor anchors the surface top-center of
// its output). Elsewhere: frameless always-on-top window positioned manually (works on X11).
class DesktopIntegration : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool layerShell READ layerShell CONSTANT)

public:
    // Must be called before the window is first shown; window->screen() picks the monitor.
    void attach(QWindow *window);
    bool layerShell() const { return m_layerShell; }

    // Only this rect of `window` receives input; clicks elsewhere fall through to windows below.
    Q_INVOKABLE void setInputRegion(QWindow *window, const QRectF &rect);
    // Tray actions land on the primary monitor's Notch (page: "" toggles pin, "settings").
    Q_INVOKABLE void toggleFromTray(const QString &page);

private:
    QList<QPointer<QWindow>> m_windows;
    bool m_layerShell = false;
};
