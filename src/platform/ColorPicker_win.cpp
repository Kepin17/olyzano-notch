#include "ColorPicker.h"

#include <QColor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QWidget>
#include <QtMath>

namespace {

constexpr int Cells = 11; // pixels across the magnifier
constexpr int Cell = 10;  // drawn size of one of them

// One screen, frozen as it was when picking started. Plain Qt, so it needs no permission and works on any monitor.
class Overlay : public QWidget
{
public:
    Overlay(ColorPicker *picker, QScreen *screen)
        : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool)
        , m_picker(picker)
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setMouseTracking(true);
        setCursor(Qt::CrossCursor);
        // grabWindow(0) gives the screen in device pixels; m_image is indexed by those.
        m_image = screen->grabWindow(0).toImage();
        m_scale = screen->geometry().width() > 0 ? qreal(m_image.width()) / screen->geometry().width() : 1.0;
        setGeometry(screen->geometry());
        m_pos = mapFromGlobal(QCursor::pos());
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.drawImage(rect(), m_image);
        if (!rect().contains(m_pos))
            return;

        const QPoint px = devicePixel(m_pos);
        const int size = Cells * Cell;
        QPoint at = m_pos + QPoint(24, 24); // below right of the pointer, flipped at the edges
        if (at.x() + size > width())
            at.rx() = m_pos.x() - 24 - size;
        if (at.y() + size + 28 > height())
            at.ry() = m_pos.y() - 24 - size - 28;

        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath clip;
        clip.addRoundedRect(QRectF(at, QSizeF(size, size)), 12, 12);
        p.save();
        p.setClipPath(clip);
        for (int dy = 0; dy < Cells; ++dy)
            for (int dx = 0; dx < Cells; ++dx)
                p.fillRect(at.x() + dx * Cell, at.y() + dy * Cell, Cell, Cell,
                           colorAt(px + QPoint(dx - Cells / 2, dy - Cells / 2)));
        p.restore();
        p.setPen(QPen(Qt::white, 2));
        p.drawRect(at.x() + Cells / 2 * Cell, at.y() + Cells / 2 * Cell, Cell, Cell);
        p.setPen(QPen(QColor(0, 0, 0, 120), 1));
        p.drawPath(clip);

        const QColor c = colorAt(px);
        const QRectF label(at.x(), at.y() + size + 6, size, 22);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(20, 20, 22, 230));
        p.drawRoundedRect(label, 11, 11);
        p.setBrush(c);
        p.drawEllipse(QRectF(label.x() + 6, label.y() + 5, 12, 12));
        p.setPen(Qt::white);
        p.drawText(label.adjusted(22, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, c.name().toUpper());
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        m_pos = e->position().toPoint();
        update();
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton)
            m_picker->finish(colorAt(devicePixel(e->position().toPoint())).name().toUpper());
        else
            m_picker->finish(QString());
    }

    void keyPressEvent(QKeyEvent *e) override
    {
        if (e->key() == Qt::Key_Escape)
            m_picker->finish(QString());
        else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)
            m_picker->finish(colorAt(devicePixel(m_pos)).name().toUpper());
    }

private:
    QPoint devicePixel(QPoint p) const { return QPoint(qFloor(p.x() * m_scale), qFloor(p.y() * m_scale)); }

    QColor colorAt(QPoint px) const
    {
        if (!m_image.rect().contains(px))
            return Qt::black;
        return QColor(m_image.pixel(px));
    }

    ColorPicker *m_picker;
    QImage m_image;
    qreal m_scale = 1.0;
    QPoint m_pos;
};

}

void ColorPicker::pick()
{
    close();
    const QList<QScreen *> screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        auto *overlay = new Overlay(this, screen);
        m_overlays << overlay;
        overlay->show();
    }
    if (m_overlays.isEmpty()) {
        emit failed(tr("No screen to pick a color from"));
        return;
    }
    // The one under the pointer takes the keyboard, so Esc works straight away.
    for (const QPointer<QWidget> &overlay : std::as_const(m_overlays))
        if (overlay->geometry().contains(QCursor::pos())) {
            overlay->activateWindow();
            overlay->setFocus();
        }
}

void ColorPicker::finish(const QString &hex)
{
    close();
    if (!hex.isEmpty())
        emit picked(hex);
}

void ColorPicker::close()
{
    for (const QPointer<QWidget> &overlay : std::as_const(m_overlays))
        if (overlay)
            overlay->close();
    m_overlays.clear();
}
