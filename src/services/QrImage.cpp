#include "QrImage.h"

#include "qrcodegen.hpp"

#include <QPainter>

QImage renderQr(const QString &text, int sizePx)
{
    qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(text.toUtf8().constData(), qrcodegen::QrCode::Ecc::MEDIUM);
    const int border = 4, modules = qr.getSize() + 2 * border;
    const int scale = qMax(1, sizePx / modules);
    QImage image(modules * scale, modules * scale, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter p(&image);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::black);
    for (int y = 0; y < qr.getSize(); ++y)
        for (int x = 0; x < qr.getSize(); ++x)
            if (qr.getModule(x, y))
                p.drawRect((x + border) * scale, (y + border) * scale, scale, scale);
    return image;
}
