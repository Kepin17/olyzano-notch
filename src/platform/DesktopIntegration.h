#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QVariantList>
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
    Q_PROPERTY(bool canBlur READ canBlur CONSTANT)

public:
    // Must be called before the window is first shown; window->screen() picks the monitor.
    void attach(QWindow *window);
    // The app dock's window on one edge of window->screen() (side: bottom | left | right). It never takes
    // focus from the app in front, so a click on the dock can tell which app that was.
    void attachDock(QWindow *window, const QString &side);
    // The status bar: the whole top edge. `reserve` pixels are kept free for it (0: it floats over windows).
    void attachBar(QWindow *window);
    void setBarReserve(QWindow *window, int reserve);
    // Move a dock to another edge. Call while it is hidden: a layer-shell surface takes anchors when mapped.
    void moveDock(QWindow *window, const QString &side);
    bool layerShell() const { return m_layerShell; }

    // Only this rect of `window` receives input; clicks elsewhere fall through to windows below.
    Q_INVOKABLE void setInputRegion(QWindow *window, const QRectF &rect);
    Q_INVOKABLE void setInputRegions(QWindow *window, const QVariantList &rects); // several rectangles at once
    // Let a dock window take the keyboard (while its search field is open) or not.
    Q_INVOKABLE void setKeyboardInteractive(QWindow *window, bool interactive);
    // Blur what's behind this rounded rect of `window` (KDE Plasma, with KF6WindowSystem); an empty rect stops it.
    Q_INVOKABLE void setBlurRegion(QWindow *window, const QRectF &rect, qreal radius);
    bool canBlur() const;
    // Tray actions land on the primary monitor's Notch (page: "" toggles pin, "settings").
    Q_INVOKABLE void toggleFromTray(const QString &page);

private:
    QList<QPointer<QWindow>> m_windows;
    bool m_layerShell = false;
};
