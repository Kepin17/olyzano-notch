#pragma once

#include <QObject>
#include <QVariantMap>

#if !defined(NOTCH_HAVE_DBUS) && defined(Q_OS_WIN)
#include <QList>
#include <QPointer>
class QWidget;
// Windows: each screen is grabbed and shown frozen in a full-screen window with a magnifier at the
// pointer; a click picks that pixel, Esc or a right click cancels (no signal, like the portal).
class ColorPicker : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool supported READ supported CONSTANT)

public:
    using QObject::QObject;
    ~ColorPicker() override { close(); }
    bool supported() const { return true; }
    Q_INVOKABLE void pick();
    void finish(const QString &hex); // empty: cancelled

signals:
    void picked(const QString &hex);
    void failed(const QString &error);

private:
    void close();
    QList<QPointer<QWidget>> m_overlays;
};
#elif !defined(NOTCH_HAVE_DBUS)
// No desktop portal here: `supported` is false and the picker button is hidden.
class ColorPicker : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool supported READ supported CONSTANT)

public:
    using QObject::QObject;
    bool supported() const { return false; }
    Q_INVOKABLE void pick() { emit failed(tr("Picking a color from the screen is not available here")); }

signals:
    void picked(const QString &hex);
    void failed(const QString &error);
};
#else
// Pick a color anywhere on screen through the desktop portal (org.freedesktop.portal.Screenshot
// PickColor). KDE implements it with KWin's picker; no special permission needed.
class ColorPicker : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool supported READ supported CONSTANT)

public:
    using QObject::QObject;
    bool supported() const { return true; }
    Q_INVOKABLE void pick();

signals:
    void picked(const QString &hex); // "#3DAEE9"
    void failed(const QString &error);

private slots:
    void onResponse(uint response, const QVariantMap &results);

private:
    void disconnectRequest();
    QString m_requestPath;
};
#endif
