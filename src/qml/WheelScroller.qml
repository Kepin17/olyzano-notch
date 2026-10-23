import QtQuick

// Mouse wheel / touchpad scroll a horizontal Flickable (ListView, …) sideways. Touchpads report
// pixels, wheels report angle (120 per notch); either axis scrolls. Stops at both ends.
// With `step` set (e.g. one tile + spacing), it moves whole steps and stays aligned to them.
WheelHandler {
    id: scroller
    required property Flickable flickable
    property real step: 0
    property real pending: 0 // touchpad pixels not yet worth a whole step
    target: null
    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad

    function clamp(x) {
        const f = flickable
        return Math.max(f.originX, Math.min(Math.max(f.originX, f.originX + f.contentWidth - f.width), x))
    }
    function scrollBy(dx) {
        flickable.contentX = clamp(flickable.contentX + dx)
    }
    // Nearest whole step (used after the scrollbar is dragged).
    function snap() {
        if (step > 0)
            flickable.contentX = clamp(flickable.originX + Math.round((flickable.contentX - flickable.originX) / step) * step)
    }

    onWheel: (event) => {
        const p = event.pixelDelta, a = event.angleDelta
        const pixels = p.x !== 0 || p.y !== 0
        const d = pixels ? (Math.abs(p.x) > Math.abs(p.y) ? p.x : p.y) : (Math.abs(a.x) > Math.abs(a.y) ? a.x : a.y)
        if (step <= 0)
            return scrollBy(-d)
        // One wheel notch = one step; touchpads move a step per ~step/2 pixels swiped.
        pending += pixels ? -d : -d / 120 * step
        const threshold = pixels ? step / 2 : step - 1
        while (Math.abs(pending) >= threshold) {
            const dir = pending > 0 ? 1 : -1
            scrollBy(dir * step)
            pending -= dir * (pixels ? threshold : step)
        }
    }
}
