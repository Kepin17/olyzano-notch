#pragma once

#include <QImage>
#include <QString>

// QR code for `text`, black on white with the 4-module quiet zone, scaled by whole pixels per module
// so it stays sharp. A null image when the text is too long for a QR code.
QImage renderQr(const QString &text, int sizePx);
