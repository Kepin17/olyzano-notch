#include "SearchHotkey_win.h"

#include <QCoreApplication>
#include <QKeySequence>

#include <windows.h>

static constexpr int HotkeyId = 0x4E53; // "NS"

SearchHotkey::SearchHotkey(QObject *parent) : QObject(parent)
{
    QCoreApplication::instance()->installNativeEventFilter(this);
}

SearchHotkey::~SearchHotkey()
{
    setKey(QString(), false);
    QCoreApplication::instance()->removeNativeEventFilter(this);
}

// Qt key -> Windows virtual key: letters, digits, function keys and the usual named ones; anything printable by what VkKeyScan says.
static UINT virtualKey(int qtKey)
{
    if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F24)
        return VK_F1 + (qtKey - Qt::Key_F1);
    switch (qtKey) {
    case Qt::Key_Space: return VK_SPACE;
    case Qt::Key_Return: case Qt::Key_Enter: return VK_RETURN;
    case Qt::Key_Tab: return VK_TAB;
    case Qt::Key_Backspace: return VK_BACK;
    case Qt::Key_Escape: return VK_ESCAPE;
    case Qt::Key_Left: return VK_LEFT;
    case Qt::Key_Right: return VK_RIGHT;
    case Qt::Key_Up: return VK_UP;
    case Qt::Key_Down: return VK_DOWN;
    default: break;
    }
    if (qtKey >= 0x20 && qtKey < 0x7f) { // printable ASCII: the virtual key of that character
        const SHORT v = VkKeyScanW(WCHAR(qtKey >= 'a' && qtKey <= 'z' ? qtKey - 32 : qtKey));
        return v == -1 ? 0 : UINT(v & 0xff);
    }
    return 0;
}

bool SearchHotkey::setKey(const QString &portableKey, bool on)
{
    if (m_registered) {
        UnregisterHotKey(nullptr, HotkeyId);
        m_registered = false;
    }
    if (!on)
        return true;
    const QKeySequence seq(portableKey, QKeySequence::PortableText);
    if (seq.count() != 1)
        return false;
    const QKeyCombination c = seq[0];
    UINT mods = MOD_NOREPEAT;
    if (c.keyboardModifiers() & Qt::MetaModifier) mods |= MOD_WIN;
    if (c.keyboardModifiers() & Qt::ControlModifier) mods |= MOD_CONTROL;
    if (c.keyboardModifiers() & Qt::AltModifier) mods |= MOD_ALT;
    if (c.keyboardModifiers() & Qt::ShiftModifier) mods |= MOD_SHIFT;
    const UINT vk = virtualKey(int(c.key()));
    if (vk == 0)
        return false;
    m_registered = RegisterHotKey(nullptr, HotkeyId, mods, vk) != 0;
    return m_registered;
}

bool SearchHotkey::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *)
{
    if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG")
        return false;
    const MSG *msg = static_cast<MSG *>(message);
    if (msg->message == WM_HOTKEY && int(msg->wParam) == HotkeyId) {
        emit pressed();
        return true;
    }
    return false;
}
