import QtQuick

// The colour of a cover: a 16 x 16 look at the picture, averaged with more weight on vivid, mid-light pixels, then
// lifted to a lightness that reads on a dark card. `color` is `fallback` while there is no picture or it is grey.
Item {
    id: ac
    property url source
    property color fallback: "#4aa3ff"
    property color color: fallback
    width: 16
    height: 16
    visible: false

    Canvas {
        id: cv
        width: 16
        height: 16
        renderTarget: Canvas.Image
        onImageLoaded: requestPaint()
        onPaint: {
            if (ac.source.toString() === "" || !isImageLoaded(ac.source))
                return
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, 16, 16)
            ctx.drawImage(ac.source, 0, 0, 16, 16)
            const d = ctx.getImageData(0, 0, 16, 16).data
            let r = 0, g = 0, b = 0, total = 0
            for (let i = 0; i < d.length; i += 4) {
                const mx = Math.max(d[i], d[i + 1], d[i + 2]), mn = Math.min(d[i], d[i + 1], d[i + 2])
                const sat = mx > 0 ? (mx - mn) / mx : 0
                const light = (mx + mn) / 510
                const w = sat * sat * Math.max(0, 1 - Math.abs(light - 0.5) * 1.6) + 0.002
                r += d[i] * w; g += d[i + 1] * w; b += d[i + 2] * w; total += w
            }
            if (total < 0.5) { // a grey cover has no colour to take
                ac.color = ac.fallback
                return
            }
            const c = Qt.rgba(r / total / 255, g / total / 255, b / total / 255, 1)
            ac.color = Qt.hsla(c.hslHue, Math.min(1, Math.max(c.hslSaturation, 0.5)), Math.max(0.58, Math.min(0.74, c.hslLightness)), 1)
        }
    }
    onSourceChanged: {
        color = fallback
        if (source.toString() !== "")
            cv.loadImage(source)
    }
}
