import QtQuick

// Mouse wheel / touchpad scroll a horizontal Flickable (ListView, …) sideways. Touchpads report
// pixels, wheels report angle (120 per notch); either axis scrolls. Stops at both ends.
WheelHandler {
    id: scroller
    required property Flickable flickable
    target: null
    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad

    function scrollBy(dx) {
        const f = flickable
        const max = Math.max(f.originX, f.originX + f.contentWidth - f.width)
        f.contentX = Math.max(f.originX, Math.min(max, f.contentX + dx))
    }

    onWheel: (event) => {
        const p = event.pixelDelta, a = event.angleDelta
        const d = (p.x !== 0 || p.y !== 0) ? (Math.abs(p.x) > Math.abs(p.y) ? p.x : p.y)
                                           : (Math.abs(a.x) > Math.abs(a.y) ? a.x : a.y)
        scroller.scrollBy(-d)
    }
}
