#pragma once

#include <QAbstractNativeEventFilter>
#include <QHash>
#include <QPointer>
#include <QWindow>

#ifdef Q_OS_WIN

// Windows: keeps a strip at the top of a monitor free, the way the taskbar does: other programs' maximised windows
// start below it. The notch window registers as an "application desktop toolbar" (AppBar) for that strip; the window
// itself keeps its own size and place. One strip per monitor (one window each). Removed when switched off and on exit.
class WinAppBar : public QAbstractNativeEventFilter
{
public:
    WinAppBar();
    ~WinAppBar() override;

    // Keep `logicalPx` free at the top of the monitor `window` is on; 0 gives it back.
    void set(QWindow *window, int logicalPx);
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    struct Entry {
        QPointer<QWindow> window;
        int logicalPx = 0;
        bool registered = false;
    };
    void place(quintptr hwnd, Entry &e);
    void remove(quintptr hwnd, Entry &e);

    QHash<quintptr, Entry> m_entries;
    unsigned m_callback = 0; // the window message the shell sends about AppBar changes
};

#endif
