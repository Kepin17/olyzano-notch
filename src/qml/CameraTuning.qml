import QtMultimedia
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// What the camera is asked for and how it is adjusted, for the Camera page and the recorder.
// Quality: a camera offers many sizes and speeds and Qt takes the biggest one (often 1080p or more, which costs a lot of processor for a
// picture that is shown 350 px wide). The size chosen here is set once, before the camera starts (changing it later would restart a running
// camera, which can crash Qt's ffmpeg backend), so a change counts the next time the page opens.
// Adjustments (zoom, brightness, contrast) act at once. Zoom is the camera's own; brightness and contrast are drawn over the picture here
// (`tuned`, `effectBrightness`, `effectContrast` for a MultiEffect) and, for a recording, applied again by ffmpeg when it is put together.
ColumnLayout {
    id: tune
    property var camera: null
    property bool open: false
    property bool ready: false           // the size is set: the camera may start
    property string sizeText: ""
    property real brightness: Number(recorder.pref("brightness", 0))   // -0.3 .. 0.3
    property real contrast: Number(recorder.pref("contrast", 1))       // 0.7 .. 1.4
    readonly property bool tuned: Math.abs(brightness) > 0.004 || Math.abs(contrast - 1) > 0.004
    readonly property real effectBrightness: brightness
    readonly property real effectContrast: contrast - 1
    property string quality: String(recorder.pref("quality", "balanced"))
    Connections {
        target: recorder
        function onPrefsChanged() {
            tune.brightness = Number(recorder.pref("brightness", 0))
            tune.contrast = Number(recorder.pref("contrast", 1))
            tune.quality = String(recorder.pref("quality", "balanced"))
        }
    }
    Layout.fillWidth: true
    spacing: 8

    // light 480p, balanced 720p, full 1080p: the format nearest to it that runs at 24 fps or more, as near 30 fps as it goes
    function formatFor(device, quality) {
        if (!device || !device.videoFormats || device.videoFormats.length === 0) return null
        const target = quality === "light" ? 480 : quality === "full" ? 1080 : 720
        let best = null, bestScore = 1e12
        const all = device.videoFormats
        for (let i = 0; i < all.length; ++i) {
            const f = all[i]
            const fps = Math.min(60, f.maxFrameRate)
            const score = Math.abs(f.resolution.height - target) * 1000 + Math.abs(fps - 30) + (fps < 24 ? 1e7 : 0)
            if (score < bestScore) { bestScore = score; best = f }
        }
        return best
    }
    function describe(f) { return f ? f.resolution.width + "×" + f.resolution.height + " · " + Math.round(Math.min(60, f.maxFrameRate)) + " fps" : "" }
    function prepare() { // before the camera is switched on
        if (!camera) return
        const f = formatFor(camera.cameraDevice, tune.quality)
        if (f) camera.cameraFormat = f
        sizeText = describe(camera.cameraFormat)
        ready = true
    }
    Connections { // another camera chosen: it has its own sizes (and starts again anyway)
        target: tune.camera
        function onCameraDeviceChanged() {
            const f = tune.formatFor(tune.camera.cameraDevice, tune.quality)
            if (f) tune.camera.cameraFormat = f
            tune.sizeText = tune.describe(tune.camera.cameraFormat)
        }
    }

    RowLayout {
        Layout.fillWidth: true
        PillButton { text: tune.open ? "Hide adjustments" : "Adjust the camera"; iconName: "gear"; checked: tune.open; onClicked: tune.open = !tune.open }
        Text { Layout.fillWidth: true; elide: Text.ElideRight; color: theme.textDim; font.pixelSize: 11; text: tune.sizeText }
    }
    ColumnLayout {
        visible: tune.open
        Layout.fillWidth: true
        spacing: 8
        SectionLabel { text: "Camera quality" }
        Segmented {
            Layout.fillWidth: true
            model: [["light", "Light  480p"], ["balanced", "Balanced  720p"], ["full", "Full  1080p"]]
            value: tune.quality
            onPicked: (v) => recorder.setPref("quality", v)
        }
        Caption { Layout.fillWidth: true; text: "A smaller picture keeps the processor and the fan quiet, and is plenty for a corner of a recording. A change counts the next time this page opens." }
        ValueSlider {
            visible: !!tune.camera && tune.camera.maximumZoomFactor > 1.05
            title: "Zoom"
            valueText: (tune.camera ? tune.camera.zoomFactor : 1).toFixed(1) + "×"
            from: 1
            to: tune.camera ? Math.min(4, tune.camera.maximumZoomFactor) : 1
            stepSize: 0.1
            value: tune.camera ? tune.camera.zoomFactor : 1
            onMoved: (v) => { if (tune.camera) tune.camera.zoomFactor = v }
        }
        ValueSlider { title: "Brightness"; valueText: (tune.brightness >= 0 ? "+" : "") + Math.round(tune.brightness * 100); from: -0.3; to: 0.3; stepSize: 0.01; value: tune.brightness; onMoved: (v) => recorder.setPref("brightness", v) }
        ValueSlider { title: "Contrast"; valueText: Math.round(tune.contrast * 100) + " %"; from: 0.7; to: 1.4; stepSize: 0.01; value: tune.contrast; onMoved: (v) => recorder.setPref("contrast", v) }
        PillButton { visible: tune.tuned || (!!tune.camera && tune.camera.zoomFactor > 1.01); text: "Reset"; iconName: "undo"; onClicked: { recorder.setPref("brightness", 0); recorder.setPref("contrast", 1); if (tune.camera) tune.camera.zoomFactor = 1 } }
    }
}
