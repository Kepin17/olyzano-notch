#include "DesktopIntegration.h"

#include "models/FileItem.h"
#include "platform/ReserveModel.h"
#include "platform/WidgetModel.h"
#ifdef Q_OS_WIN
#include "platform/WinAppBar.h"
#endif

#include <QCursor>
#include <QDateTime>
#ifdef NOTCH_HAVE_DBUS
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#endif
#include <QGuiApplication>
#include <QPainterPath>
#include <QRegion>
#include <QPainter>
#include <QRasterWindow>
#include <QScreen>

#ifdef NOTCH_HAVE_LAYERSHELL
#include <LayerShellQt/window.h>
#endif
#ifdef NOTCH_HAVE_KWINDOWSYSTEM
#include <KWindowEffects>
#endif

#ifdef NOTCH_HAVE_LAYERSHELL
// A transparent sliver in the Background layer that holds the exclusive zone of "keep other windows clear of the capsule".
// The notch itself is in the Overlay layer, and the compositor arranges the Overlay layer first: a zone claimed there would
// push Plasma's panel (a Top-layer surface) down. A zone in the Background layer is arranged last: the panel stays on the
// screen edge and only the windows are kept below the strip.
class ReserveStrip : public QRasterWindow
{
public:
    ReserveStrip()
    {
        QSurfaceFormat f;
        f.setAlphaBufferSize(8);
        setFormat(f);
        setFlag(Qt::WindowTransparentForInput);
        resize(1, 1);
    }
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.fillRect(QRect(QPoint(0, 0), size()), Qt::transparent);
    }
};
#endif

static void centerOnScreen(QWindow *window)
{
    if (!window->screen())
        return;
    // x keeps clear of a side taskbar; y is the real top of the screen. With "keep windows clear of the capsule" the work
    // area starts below the reserved strip, and the capsule has to stay in that strip, not be pushed down with it.
    const QRect g = window->screen()->availableGeometry();
    window->setPosition(g.x() + (g.width() - window->width()) / 2, window->screen()->geometry().y());
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
            ls->setLayer(LayerShellQt::Window::LayerOverlay); // Plasma's own top panel sits above the Top layer and would hide the capsule
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
    if (!notchWindow)
        return;
#ifdef NOTCH_HAVE_LAYERSHELL
    Q_UNUSED(above)
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) {
        if (auto *ls = LayerShellQt::Window::get(notchWindow))
            ls->setLayer(LayerShellQt::Window::LayerOverlay); // always: Plasma's panel covers the Top layer too
        return;
    }
#endif
    // Windows and X11: two always-on-top windows stack by who was shown or raised last, so the bar can end up over
    // the notch (its menu then cannot be reached). Put the notch back on top; it takes no focus.
    if (above)
        notchWindow->raise();
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

// ---- Widget mode ----

void DesktopIntegration::setClickThrough(QWindow *window, bool on)
{
    if (window)
        window->setFlag(Qt::WindowTransparentForInput, on);
}

QPointF DesktopIntegration::cursorPos() const
{
    return QPointF(QCursor::pos());
}

QSizeF DesktopIntegration::screenSize(QWindow *window) const
{
    return window && window->screen() ? QSizeF(window->screen()->geometry().size()) : QSizeF(1920, 1080);
}

QPointF DesktopIntegration::snapWidget(qreal cx, qreal y, qreal itemW, qreal itemH, QWindow *window) const
{
    return WidgetModel::snap(cx, y, itemW, itemH, screenSize(window));
}

QRectF DesktopIntegration::placeWidget(QWindow *window, int kind, qreal cx, qreal y)
{
    if (!window || !window->screen())
        return {};
    const QRectF r = WidgetModel::windowRect(kind, cx, y, screenSize(window));
    const QRect g = window->screen()->geometry();
#ifdef NOTCH_HAVE_LAYERSHELL
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) {
        if (auto *ls = LayerShellQt::Window::get(window)) {
            ls->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorLeft));
            ls->setMargins(QMargins(qRound(r.x()), qRound(r.y()), 0, 0));
            window->resize(qRound(r.width()), qRound(r.height()));
            return r;
        }
    }
#endif
    const QRect target(g.x() + qRound(r.x()), g.y() + qRound(r.y()), qRound(r.width()), qRound(r.height()));
    if (window->size() == target.size())
        window->setPosition(target.topLeft()); // dragging only moves the card: no resize of a translucent window per step
    else
        window->setGeometry(target);
    return r;
}

void DesktopIntegration::placeDocked(QWindow *window)
{
    if (!window || !window->screen())
        return;
#ifdef NOTCH_HAVE_LAYERSHELL
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) {
        if (auto *ls = LayerShellQt::Window::get(window)) {
            ls->setAnchors(LayerShellQt::Window::AnchorTop);
            ls->setMargins(QMargins());
            window->resize(int(WidgetModel::OpenW), int(WidgetModel::OpenH));
            return;
        }
    }
#endif
    window->resize(int(WidgetModel::OpenW), int(WidgetModel::OpenH));
    centerOnScreen(window);
}

// ---- Keeping windows clear of the capsule ----

DesktopIntegration::DesktopIntegration()
{
#ifdef Q_OS_WIN
    m_appBar = std::make_unique<WinAppBar>();
#endif
}

DesktopIntegration::~DesktopIntegration() = default;

bool DesktopIntegration::canReserve() const
{
#ifdef Q_OS_WIN
    return true;
#else
    return QGuiApplication::platformName().startsWith(QLatin1String("wayland")) && m_layerShell;
#endif
}

// Plasma's top panel already holds its own room, and the notch hangs over it: the strip is only as tall as what sticks out
// below the panel, so the windows sit right under the notch instead of a panel's height lower.
void DesktopIntegration::askTopPanel()
{
#if defined(NOTCH_HAVE_DBUS) && !defined(Q_OS_WIN)
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_panelAsked < 20000)
        return;
    m_panelAsked = now;
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.kde.plasmashell"), QStringLiteral("/PlasmaShell"),
                                                       QStringLiteral("org.kde.PlasmaShell"), QStringLiteral("evaluateScript"));
    call << QStringLiteral("var h = 0; panels().forEach(function (p) { if (p.location == 'top') h = Math.max(h, p.height); }); print(h)");
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call, 3000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<QString> reply = *w;
        w->deleteLater();
        if (!reply.isValid())
            return; // not Plasma: no panel to take into account
        const int h = qBound(0, reply.value().trimmed().toInt(), 400);
        if (h != m_panelTop && m_lastReserve.window) {
            m_panelTop = h;
            const ReserveArgs a = m_lastReserve;
            reserve(a.window, a.mode, a.on, a.scale, a.barPx);
        }
    });
#endif
}

void DesktopIntegration::reserve(QWindow *window, const QString &mode, bool on, qreal scale, int barPx)
{
    if (!window)
        return;
    m_lastReserve = {window, mode, on, scale, barPx};
    if (on)
        askTopPanel();
#ifdef Q_OS_WIN
    if (!window->isVisible()) // the native window may still be replaced while it is being set up; QML asks again once shown
        return;
    m_appBar->set(window, ReserveModel::plan(mode, on, scale, barPx, false).appBar);
#else
#ifdef NOTCH_HAVE_LAYERSHELL
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) {
        if (auto *ls = LayerShellQt::Window::get(window)) {
            const int zone = ReserveModel::plan(mode, on, scale, barPx + m_panelTop, true).notchZone; // the panel's room counts as kept free
            ls->setExclusiveZone(-1); // the notch sits over everything and takes nothing (a panel would be pushed down by it)
            if (zone > 0) {
                if (m_strip && m_stripZone != zone) { // a new height is a new surface: the compositor takes the zone from a fresh commit
                    m_strip->hide();
                    m_strip->deleteLater();
                    m_strip = nullptr;
                }
                if (!m_strip) {
                    auto *strip = new ReserveStrip;
                    strip->QObject::setParent(this);
                    m_strip = strip;
                    if (auto *sl = LayerShellQt::Window::get(strip)) {
                        sl->setScope(QStringLiteral("notch-reserve"));
#ifdef NOTCH_LAYERSHELL_SET_SCREEN
                        sl->setScreen(window->screen());
#endif
                        sl->setLayer(LayerShellQt::Window::LayerBackground);
                        sl->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorLeft
                                                                       | LayerShellQt::Window::AnchorRight));
                        sl->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
                        sl->setMargins(QMargins());
                    }
                    strip->setGeometry(0, 0, window->screen()->geometry().width(), 1);
                }
                if (auto *sl = LayerShellQt::Window::get(m_strip))
                    sl->setExclusiveZone(zone);
                m_stripZone = zone;
                qCInfo(lcNotch) << "Room kept free at the top:" << zone << "px (top panel" << m_panelTop << "px)";
                if (!m_strip->isVisible())
                    m_strip->show();
            } else if (m_strip) {
                m_strip->hide(); // nothing to keep clear: the surface goes, and with it the zone
            }
        }
    }
#endif
#endif
}
