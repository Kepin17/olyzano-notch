#include "DesktopIntegration.h"

#include "models/FileItem.h"

#include <QGuiApplication>
#include <QPainterPath>
#include <QRegion>
#include <QScreen>

#ifdef NOTCH_HAVE_LAYERSHELL
#include <LayerShellQt/window.h>
#endif
#ifdef NOTCH_HAVE_KWINDOWSYSTEM
#include <KWindowEffects>
#endif

static void centerOnScreen(QWindow *window)
{
    if (!window->screen())
        return;
    const QRect g = window->screen()->availableGeometry();
    window->setPosition(g.x() + (g.width() - window->width()) / 2, g.y());
}

void DesktopIntegration::attach(QWindow *window)
{
    m_windows.removeAll(nullptr);
    m_windows << window;
#ifdef NOTCH_HAVE_LAYERSHELL
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) {
        if (auto *ls = LayerShellQt::Window::get(window)) {
            ls->setScope(QStringLiteral("notch"));
#ifdef NOTCH_LAYERSHELL_SET_SCREEN
            ls->setScreen(window->screen());
#endif
            ls->setLayer(LayerShellQt::Window::LayerTop);
            ls->setAnchors(LayerShellQt::Window::AnchorTop);
            ls->setExclusiveZone(-1); // sit on the real screen edge, over any top panel
            ls->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityOnDemand);
            ls->setMargins(QMargins()); // fused to the top edge
            m_layerShell = true;
            qCInfo(lcNotch) << "Using wlr-layer-shell on" << window->screen()->name();
            return;
        }
    }
#endif
    qCInfo(lcNotch) << "Using regular top-level window on" << QGuiApplication::platformName()
                    << window->screen()->name();
    window->setFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    centerOnScreen(window);
    connect(window->screen(), &QScreen::availableGeometryChanged, window, [window] { centerOnScreen(window); });
}

static const char DockSide[] = "notchDockSide";

// On the real edge of the screen (like the notch on the top one), centered along it.
static void placeDock(QWindow *window)
{
    if (!window->screen())
        return;
    const QRect g = window->screen()->geometry();
    const QString side = window->property(DockSide).toString();
    if (side == QLatin1String("left"))
        window->setPosition(g.x(), g.y() + (g.height() - window->height()) / 2);
    else if (side == QLatin1String("right"))
        window->setPosition(g.x() + g.width() - window->width(), g.y() + (g.height() - window->height()) / 2);
    else
        window->setPosition(g.x() + (g.width() - window->width()) / 2, g.y() + g.height() - window->height());
}

#ifdef NOTCH_HAVE_LAYERSHELL
static LayerShellQt::Window *dockLayer(QWindow *window)
{
    return QGuiApplication::platformName().startsWith(QLatin1String("wayland")) ? LayerShellQt::Window::get(window) : nullptr;
}

static LayerShellQt::Window::Anchors dockAnchors(const QString &side)
{
    if (side == QLatin1String("left"))
        return LayerShellQt::Window::AnchorLeft;
    if (side == QLatin1String("right"))
        return LayerShellQt::Window::AnchorRight;
    return LayerShellQt::Window::AnchorBottom;
}
#endif

void DesktopIntegration::attachDock(QWindow *window, const QString &side)
{
    window->setProperty(DockSide, side);
#ifdef NOTCH_HAVE_LAYERSHELL
    if (auto *ls = dockLayer(window)) {
        ls->setScope(QStringLiteral("notch-dock"));
#ifdef NOTCH_LAYERSHELL_SET_SCREEN
        ls->setScreen(window->screen());
#endif
        ls->setLayer(LayerShellQt::Window::LayerTop);
        ls->setAnchors(dockAnchors(side));
        ls->setExclusiveZone(-1); // over any panel on that edge; the dock hides itself
        ls->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        ls->setMargins(QMargins());
        return;
    }
#endif
    window->setFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool | Qt::WindowDoesNotAcceptFocus);
    placeDock(window);
    connect(window->screen(), &QScreen::geometryChanged, window, [window] { placeDock(window); });
    connect(window, &QWindow::widthChanged, window, [window] { placeDock(window); });
    connect(window, &QWindow::heightChanged, window, [window] { placeDock(window); });
}

static void placeBar(QWindow *window)
{
    if (window->screen())
        window->setGeometry(window->screen()->geometry().x(), window->screen()->geometry().y(), window->screen()->geometry().width(), window->height());
}

void DesktopIntegration::attachBar(QWindow *window)
{
#ifdef NOTCH_HAVE_LAYERSHELL
    if (auto *ls = dockLayer(window)) {
        ls->setScope(QStringLiteral("notch-bar"));
#ifdef NOTCH_LAYERSHELL_SET_SCREEN
        ls->setScreen(window->screen());
#endif
        ls->setLayer(LayerShellQt::Window::LayerTop);
        ls->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));
        ls->setExclusiveZone(-1); // over everything until a reserve is asked for
        ls->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        ls->setMargins(QMargins());
        return;
    }
#endif
    window->setFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool | Qt::WindowDoesNotAcceptFocus);
    placeBar(window);
    connect(window->screen(), &QScreen::geometryChanged, window, [window] { placeBar(window); });
}

void DesktopIntegration::setBarReserve(QWindow *window, int reserve)
{
#ifdef NOTCH_HAVE_LAYERSHELL
    if (auto *ls = dockLayer(window)) {
        ls->setExclusiveZone(reserve > 0 ? reserve : -1);
        return;
    }
#endif
    Q_UNUSED(window)
    Q_UNUSED(reserve)
}

void DesktopIntegration::moveDock(QWindow *window, const QString &side)
{
    window->setProperty(DockSide, side);
#ifdef NOTCH_HAVE_LAYERSHELL
    if (auto *ls = dockLayer(window)) {
        ls->setAnchors(dockAnchors(side));
        return;
    }
#endif
    placeDock(window);
}

void DesktopIntegration::setAboveBar(QWindow *notchWindow, bool above)
{
#ifdef NOTCH_HAVE_LAYERSHELL
    if (notchWindow && QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
        if (auto *ls = LayerShellQt::Window::get(notchWindow))
            ls->setLayer(above ? LayerShellQt::Window::LayerOverlay : LayerShellQt::Window::LayerTop);
#else
    Q_UNUSED(notchWindow)
    Q_UNUSED(above)
#endif
}

void DesktopIntegration::setKeyboardInteractive(QWindow *window, bool interactive)
{
    if (!window)
        return;
#ifdef NOTCH_HAVE_LAYERSHELL
    if (auto *ls = dockLayer(window)) {
        ls->setKeyboardInteractivity(interactive ? LayerShellQt::Window::KeyboardInteractivityOnDemand
                                                 : LayerShellQt::Window::KeyboardInteractivityNone);
        if (interactive)
            window->requestActivate();
        return;
    }
#endif
    window->setFlag(Qt::WindowDoesNotAcceptFocus, !interactive);
    if (interactive)
        window->requestActivate();
}

bool DesktopIntegration::canBlur() const
{
#ifdef NOTCH_HAVE_KWINDOWSYSTEM
    return KWindowEffects::isEffectAvailable(KWindowEffects::BlurBehind);
#else
    return false;
#endif
}

void DesktopIntegration::setBlurRegion(QWindow *window, const QRectF &rect, qreal radius)
{
#ifdef NOTCH_HAVE_KWINDOWSYSTEM
    if (!window)
        return;
    if (rect.isEmpty()) {
        KWindowEffects::enableBlurBehind(window, false);
        return;
    }
    QPainterPath path;
    path.addRoundedRect(rect, radius, radius);
    KWindowEffects::enableBlurBehind(window, true, QRegion(path.toFillPolygon().toPolygon()));
#else
    Q_UNUSED(window)
    Q_UNUSED(rect)
    Q_UNUSED(radius)
#endif
}

void DesktopIntegration::setInputRegion(QWindow *window, const QRectF &rect)
{
    if (window)
        window->setMask(QRegion(rect.toAlignedRect()));
}

void DesktopIntegration::setInputRegions(QWindow *window, const QVariantList &rects)
{
    if (!window)
        return;
    QRegion region;
    for (const QVariant &v : rects)
        region += v.toRectF().toAlignedRect();
    window->setMask(region);
}

void DesktopIntegration::toggleFromTray(const QString &page)
{
    m_windows.removeAll(nullptr);
    if (m_windows.isEmpty())
        return;
    QWindow *target = m_windows.constFirst();
    for (QWindow *w : std::as_const(m_windows))
        if (w->screen() == QGuiApplication::primaryScreen())
            target = w;
    QMetaObject::invokeMethod(target, "toggleFromTray", Q_ARG(QVariant, page));
}
