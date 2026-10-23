#pragma once

#include <QObject>
#include <QVariantMap>

// Pick a color anywhere on screen through the desktop portal (org.freedesktop.portal.Screenshot
// PickColor). KDE implements it with KWin's picker; no special permission needed.
class ColorPicker : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;
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
