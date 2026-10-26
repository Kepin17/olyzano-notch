#include "platform/WinAppBar.h"

#include <QCoreApplication>
#include <QScreen>

#include <windows.h>
#include <shellapi.h>

WinAppBar::WinAppBar()
    : m_callback(RegisterWindowMessageW(L"OlyzanoNotchAppBar"))
{
    QCoreApplication::instance()->installNativeEventFilter(this);
    QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, QCoreApplication::instance(), [this] {
        for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
            remove(it.key(), it.value());
    });
}

WinAppBar::~WinAppBar()
{
    if (auto *app = QCoreApplication::instance())
        app->removeNativeEventFilter(this);
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
        remove(it.key(), it.value());
}

void WinAppBar::set(QWindow *window, int logicalPx)
{
    if (!window)
        return;
    const quintptr hwnd = window->winId();
    if (!hwnd)
        return;
    Entry &e = m_entries[hwnd];
    e.window = window;
    if (e.logicalPx == logicalPx && (logicalPx == 0 || e.registered))
        return;
    e.logicalPx = logicalPx;
    if (logicalPx > 0) {
        place(hwnd, e);
    } else {
        remove(hwnd, e);
    }
}

void WinAppBar::place(quintptr hwnd, Entry &e)
{
    const HWND h = reinterpret_cast<HWND>(hwnd);
    APPBARDATA abd{};
    abd.cbSize = sizeof(abd);
    abd.hWnd = h;
    if (!e.registered) {
        abd.uCallbackMessage = m_callback;
        if (!SHAppBarMessage(ABM_NEW, &abd))
            return;
        e.registered = true;
    }
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi))
        return;
    const qreal dpr = e.window ? e.window->devicePixelRatio() : 1.0;
    const int physical = qRound(e.logicalPx * dpr);
    abd.uEdge = ABE_TOP;
    abd.rc = mi.rcMonitor;
    abd.rc.bottom = abd.rc.top + physical;
    SHAppBarMessage(ABM_QUERYPOS, &abd);          // the shell may move it below another toolbar
    abd.rc.bottom = abd.rc.top + physical;
    SHAppBarMessage(ABM_SETPOS, &abd);            // and now the work area shrinks
}

void WinAppBar::remove(quintptr hwnd, Entry &e)
{
    if (!e.registered)
        return;
    APPBARDATA abd{};
    abd.cbSize = sizeof(abd);
    abd.hWnd = reinterpret_cast<HWND>(hwnd);
    SHAppBarMessage(ABM_REMOVE, &abd);
    e.registered = false;
}

bool WinAppBar::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *)
{
    if (eventType != "windows_generic_MSG" || m_entries.isEmpty())
        return false;
    const MSG *msg = static_cast<MSG *>(message);
    if (msg->message != m_callback)
        return false;
    if (msg->wParam == ABN_POSCHANGED) { // a resolution, a taskbar or another toolbar changed: ask for the strip again
        const auto it = m_entries.find(reinterpret_cast<quintptr>(msg->hwnd));
        if (it != m_entries.end() && it->registered && it->logicalPx > 0)
            place(it.key(), it.value());
    }
    return false;
}
