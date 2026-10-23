#include "DesktopIntegration.h"

#include "models/FileItem.h"

#include <QGuiApplication>
#include <QRegion>
#include <QScreen>

#ifdef NOTCH_HAVE_LAYERSHELL
#include <LayerShellQt/window.h>
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
            ls->setScreen(window->screen());
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

void DesktopIntegration::setInputRegion(QWindow *window, const QRectF &rect)
{
    if (window)
        window->setMask(QRegion(rect.toAlignedRect()));
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
