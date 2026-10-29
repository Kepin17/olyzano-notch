#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QScreen>
#include <QVariantList>
#include <QWindow>
#include <memory>
#ifdef Q_OS_WIN
class WinAppBar;
#endif
#include <QRectF>
#include <QSizeF>
#include <QPointF>

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
    Q_PROPERTY(bool canReserve READ canReserve CONSTANT) // other windows can be kept clear of a strip at the top (Windows, Wayland)

public:
    // Must be called before the window is first shown; window->screen() picks the monitor.
    void attach(QWindow *window);
    // The app dock's window on one edge of window->screen() (side: bottom | left | right). It never takes
    // focus from the app in front, so a click on the dock can tell which app that was.
    void attachDock(QWindow *window, const QString &side);
    // The status bar: the whole top edge. `reserve` pixels are kept free for it (0: it floats over windows).
    void attachBar(QWindow *window);
    void setBarReserve(QWindow *window, int reserve);
    // The notch above the status bar (both sit on the top edge): in the overlay layer while the bar is on, else the top layer.
    void setAboveBar(QWindow *notchWindow, bool above);
    // Windows/X11: put a tool window back above other always-on-top peers (z-order drifts when apps or the shell re-sort).
    Q_INVOKABLE void raiseOnTop(QWindow *window);
    // Move a dock to another edge. Call while it is hidden: a layer-shell surface takes anchors when mapped.
    void moveDock(QWindow *window, const QString &side);
    bool layerShell() const { return m_layerShell; }
    bool canReserve() const;
    // Keep a strip free at the top for the notch's capsule (ReserveModel decides how much): `mode` is the capsule mode,
    // `barPx` what the status bar already keeps free. Call again whenever any of them changes.
    Q_INVOKABLE void reserve(QWindow *window, const QString &mode, bool on, qreal scale, int barPx);
    DesktopIntegration();
    ~DesktopIntegration() override;

    // Only this rect of `window` receives input; clicks elsewhere fall through to windows below.
    Q_INVOKABLE void setInputRegion(QWindow *window, const QRectF &rect);
    Q_INVOKABLE void setInputRegions(QWindow *window, const QVariantList &rects); // several rectangles at once
    // Let a dock window take the keyboard (while its search field is open) or not.
    Q_INVOKABLE void setKeyboardInteractive(QWindow *window, bool interactive);
    Q_INVOKABLE void grabKeyboard(QWindow *window, bool grab); // the notch window takes every key while the search is open (Wayland: exclusive layer-shell keyboard)
    // Blur what's behind this rounded rect of `window` (KDE Plasma, with KF6WindowSystem); an empty rect stops it.
    Q_INVOKABLE void setBlurRegion(QWindow *window, const QRectF &rect, qreal radius);
    bool canBlur() const;
    // Widget mode. The notch window follows the floating card: `kind` picks the window: a small one, the big one while the panel is open, or the whole screen while the card is dragged. (cx, y): the card's middle and top, in pixels of the screen. Returns the window's rect on that screen.
    Q_INVOKABLE QRectF placeWidget(QWindow *window, int kind, qreal cx, qreal y); // kind: 0 floating, 1 open, 2 dragging (WidgetModel::Kind)
    Q_INVOKABLE void placeDocked(QWindow *window); // back to the top-centre place of the island
    Q_INVOKABLE QSizeF screenSize(QWindow *window) const;
    Q_INVOKABLE QPointF cursorPos() const; // the pointer on the whole desktop (Windows, X11); on Wayland an app cannot know it
    // Visible but out of the pointer's way: clicks go to what is behind (gaming mode keeps the notch on screen, shut).
    Q_INVOKABLE void setClickThrough(QWindow *window, bool on);
    
    Q_INVOKABLE QPointF snapWidget(qreal cx, qreal y, qreal itemW, qreal itemH, QWindow *window) const;
    // Tray actions land on the primary monitor's Notch (page: "" toggles pin, "settings").
    Q_INVOKABLE void toggleFromTray(const QString &page);

private:
    QList<QPointer<QWindow>> m_windows;
    int m_panelTop = 0; // Plasma: the thickness of the top panel, which the room kept free does not need to repeat
    qint64 m_panelAsked = 0;
    int m_stripZone = 0;
    struct ReserveArgs { QPointer<QWindow> window; QString mode; bool on = false; qreal scale = 1; int barPx = 0; } m_lastReserve;
    void askTopPanel();
    QPointer<QWindow> m_strip; // Wayland: the sliver that holds the room kept free at the top (see DesktopIntegration::reserve)
#ifdef Q_OS_WIN
    std::unique_ptr<WinAppBar> m_appBar;
    // The reserved top strip as a picture only (clicks pass through), so the gap under maximised windows is not empty wallpaper.
    QHash<QPointer<QScreen>, QPointer<QWindow>> m_winStrips;
    void updateWinReserveStrip(QWindow *notchWindow, int logicalPx);
#endif
    bool m_layerShell = false;
};
