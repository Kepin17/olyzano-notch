#pragma once

#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QtGlobal>
#include <algorithm>

// Widget mode (the floating card), without windows or the pointer so tests can drive it. The card is described by an
// anchor: `cx` (the middle of the card) and `y` (its top), in pixels of the screen. The notch window follows it:
// a small one while the card floats, a big one while its panel is open (the panel grows from the card's spot).
namespace WidgetModel {

constexpr qreal FloatW = 720, FloatH = 170; // room for the widest capsule (a sung line, at the largest size) and its shadow
constexpr qreal OpenW = 840, OpenH = 620;   // the open panel (the widest page is 780)
constexpr qreal SnapPx = 12;                // a card this close to an edge or the middle line sticks to it
constexpr qreal EdgeGap = 8;                // where it sticks: this far from the screen edge

inline qreal clampTo(qreal v, qreal lo, qreal hi) { return hi < lo ? lo : std::clamp(v, lo, hi); }

enum Kind { Floating = 0, Open = 1, Dragging = 2 };

// The window for an anchor: kept inside the screen. Open: the big one. Dragging: the whole screen, so the card moves
// inside one window instead of the window being moved (a compositor takes a round trip for each move, which stutters).
inline QRectF windowRect(int kind, qreal cx, qreal y, const QSizeF &screen)
{
    if (kind == Dragging)
        return QRectF(0, 0, screen.width(), screen.height());
    const bool open = kind == Open;
    const qreal w = open ? OpenW : FloatW, h = open ? OpenH : FloatH;
    return QRectF(clampTo(cx - w / 2, 0, screen.width() - w), clampTo(y, 0, screen.height() - h), w, h);
}

// Where the card (itemW x itemH) is drawn inside its window, so it does not move on the screen when the window
// changes size.
inline qreal islandX(const QRectF &win, qreal cx, qreal itemW) { return clampTo(cx - win.x() - itemW / 2, 0, win.width() - itemW); }
inline qreal islandY(const QRectF &win, qreal y, qreal itemH) { return clampTo(y - win.y(), 0, win.height() - itemH); }

// An anchor that keeps the card (itemW x itemH) on the screen.
inline QPointF clampAnchor(qreal cx, qreal y, qreal itemW, qreal itemH, const QSizeF &screen)
{
    return {clampTo(cx, itemW / 2, screen.width() - itemW / 2), clampTo(y, 0, screen.height() - itemH)};
}

// After a drag: stay on the screen, and stick to an edge (EdgeGap away) or to the middle line when close.
inline QPointF snap(qreal cx, qreal y, qreal itemW, qreal itemH, const QSizeF &screen)
{
    QPointF p = clampAnchor(cx, y, itemW, itemH, screen);
    const qreal left = p.x() - itemW / 2, right = p.x() + itemW / 2;
    if (qAbs(p.x() - screen.width() / 2) <= SnapPx)
        p.setX(screen.width() / 2);
    else if (left <= EdgeGap + SnapPx)
        p.setX(itemW / 2 + EdgeGap);
    else if (screen.width() - right <= EdgeGap + SnapPx)
        p.setX(screen.width() - itemW / 2 - EdgeGap);
    if (p.y() <= EdgeGap + SnapPx)
        p.setY(EdgeGap);
    else if (screen.height() - (p.y() + itemH) <= EdgeGap + SnapPx)
        p.setY(screen.height() - itemH - EdgeGap);
    return p;
}

// The first place: the middle of the top edge, a little down.
inline QPointF defaultAnchor(const QSizeF &screen) { return {screen.width() / 2, 14}; }

}
