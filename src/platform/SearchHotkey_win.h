#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QString>

// Windows: Shift + Space opens the search in the notch. A thread hot key (RegisterHotKey) arrives as a WM_HOTKEY message in this thread's
// queue, which Qt hands to the native event filter. If another program already has the combination, `registered` stays false.
class SearchHotkey : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
public:
    explicit SearchHotkey(QObject *parent = nullptr);
    ~SearchHotkey() override;
    bool setKey(const QString &portableKey, bool on); // "Shift+Space": true when the key is ours now (or off)
    bool registered() const { return m_registered; }
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

signals:
    void pressed();

private:
    bool m_registered = false;
};
