import QtCore
import QtMultimedia
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import QtQuick.Layouts

// The camera and what is done with it: Mirror (a live look at yourself) and Photo booth (a countdown, then one picture or a strip of four,
// in a template and a filter; the finished picture goes onto the shelf and can be shared at once). The camera is on only while this is loaded.
ColumnLayout {
    id: root
    property var shell: null
    property string mode: "mirror"      // mirror | booth
    property string tmpl: "single"      // single | strip | polaroid | frame
    property string filter: "normal"    // normal | mono | warm | vivid
    property bool mirror: true
    property var frames: []
    property int count: 0               // the countdown number on show, 0: none
    property bool busy: false
    property string resultPath: ""
    property string savedId: ""
    property string problem: ""
    property bool gotFrame: false
    readonly property bool hasCamera: devices.videoInputs.length > 0
    readonly property bool software: memtune.running // the software renderer cannot draw the live filter
    spacing: 8

    MediaDevices { id: devices }
    CameraPermission { id: permission }
    Component.onCompleted: if (permission.status === Qt.Undetermined) permission.request()

    CaptureSession {
        camera: Camera {
            id: camera
            active: permission.status === Qt.Granted && root.hasCamera // never switched off while this is shown: stopping a running V4L2 camera can crash Qt's ffmpeg backend; it ends when the page is closed
            cameraDevice: devices.defaultVideoInput
        }
        imageCapture: ImageCapture {
            id: capture
            onImageSaved: (id, path) => root.shotSaved(path)
            onErrorOccurred: (id, error, message) => { root.busy = false; root.count = 0; root.problem = message }
        }
        videoOutput: video
    }

    // ---- the picture: live, or the finished one ----
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(350, width * 0.75)
        radius: theme.radiusMd
        color: "#000000"
        clip: true
        VideoOutput {
            id: video
            anchors.fill: parent
            visible: root.resultPath === "" && root.hasCamera
            fillMode: VideoOutput.PreserveAspectCrop
            transform: Scale { origin.x: video.width / 2; xScale: root.mirror ? -1 : 1 }
            layer.enabled: !root.software && root.mode === "booth" && root.filter !== "normal"
            layer.effect: MultiEffect {
                saturation: root.filter === "mono" ? -1 : root.filter === "vivid" ? 0.5 : 0
                contrast: root.filter === "vivid" ? 0.12 : 0
                brightness: root.filter === "warm" ? 0.03 : 0
                colorization: root.filter === "warm" ? 0.16 : 0
                colorizationColor: "#ff9d3a"
            }
        }
        Image {
            anchors.fill: parent
            visible: root.resultPath !== ""
            source: root.resultPath !== "" ? "file://" + root.resultPath : ""
            fillMode: Image.PreserveAspectFit
            cache: false
        }
        Text {
            anchors.centerIn: parent
            visible: !root.hasCamera || permission.status === Qt.Denied
            width: parent.width - 40
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            color: "#c8c8cc"
            font.pixelSize: 13
            text: permission.status === Qt.Denied ? "Camera access is turned off for Olyzano Notch (system privacy settings)." : "No camera found."
        }
        Text { // the camera takes a few seconds to wake
            anchors.centerIn: parent
            visible: camera.active && !root.gotFrame && root.resultPath === ""
            text: "Starting the camera…"
            color: "#c8c8cc"
            font.pixelSize: 13
        }
        Connections { target: video.videoSink; function onVideoFrameChanged() { root.gotFrame = true } }
        Text { // the countdown
            anchors.centerIn: parent
            visible: root.count > 0
            text: root.count
            color: "#ffffff"
            font.pixelSize: 120
            font.weight: Font.Light
            style: Text.Outline
            styleColor: "#66000000"
        }
        Rectangle { // the flash
            id: flash
            anchors.fill: parent
            color: "#ffffff"
            opacity: 0
            NumberAnimation on opacity { id: flashAnim; running: false; from: 0.9; to: 0; duration: 380; easing.type: Easing.OutQuad }
        }
        Row { // the camera is on
            anchors { left: parent.left; top: parent.top; margins: 10 }
            spacing: 6
            visible: camera.active
            Rectangle { width: 8; height: 8; radius: 4; color: "#ff453a"; anchors.verticalCenter: parent.verticalCenter }
            Text { text: "Camera on"; color: "#ffffff"; font.pixelSize: 11; font.weight: Font.DemiBold; style: Text.Outline; styleColor: "#80000000" }
        }
        Text { // frame x of y while a strip is taken
            anchors { right: parent.right; top: parent.top; margins: 10 }
            visible: root.busy && photoBooth.shots(root.tmpl) > 1
            text: (root.frames.length + 1) + " / " + photoBooth.shots(root.tmpl)
            color: "#ffffff"
            font.pixelSize: 12
            font.weight: Font.DemiBold
            style: Text.Outline
            styleColor: "#80000000"
        }
    }

    // ---- controls ----
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Segmented {
            Layout.preferredWidth: 220
            model: [["mirror", "Mirror"], ["booth", "Photo booth"]]
            value: root.mode
            onPicked: (v) => { root.mode = v; root.reset() }
        }
        Item { Layout.fillWidth: true }
        PillButton { visible: devices.videoInputs.length > 1; text: "Switch camera"; onClicked: root.nextCamera() }
        PillButton { text: "Flip"; iconName: "undo"; checked: root.mirror; onClicked: root.mirror = !root.mirror; ToolTip.visible: hovered; ToolTip.text: "Mirror the picture left to right" }
    }

    ColumnLayout { // photo booth: template, filter, the shutter
        visible: root.mode === "booth" && root.resultPath === ""
        Layout.fillWidth: true
        spacing: 6
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Text { text: "Template"; color: theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 58 }
            Repeater {
                model: [["single", "Single"], ["strip", "Strip of 4"], ["polaroid", "Polaroid"], ["frame", "Frame"]]
                PillButton { required property var modelData; text: modelData[1]; checked: root.tmpl === modelData[0]; enabled: !root.busy; onClicked: root.tmpl = modelData[0] }
            }
            Item { Layout.fillWidth: true }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Text { text: "Filter"; color: theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 58 }
            Repeater {
                model: [["normal", "Normal"], ["mono", "Black & white"], ["warm", "Warm"], ["vivid", "Vivid"]]
                PillButton { required property var modelData; text: modelData[1]; checked: root.filter === modelData[0]; enabled: !root.busy; onClicked: root.filter = modelData[0] }
            }
            Item { Layout.fillWidth: true }
        }
        RowLayout {
            Layout.fillWidth: true
            Caption { visible: root.software; Layout.fillWidth: true; text: "Low memory mode: the filter shows in the finished picture, not in the preview." }
            Item { visible: !root.software; Layout.fillWidth: true }
            PillButton {
                text: root.busy ? "Taking…" : (photoBooth.shots(root.tmpl) > 1 ? "Take 4 photos" : "Take photo")
                iconName: "camera"
                checked: true
                enabled: !root.busy && root.hasCamera && camera.active && capture.readyForCapture
                onClicked: root.start()
            }
        }
    }
    RowLayout { // the finished picture
        visible: root.resultPath !== ""
        Layout.fillWidth: true
        spacing: 8
        Text { Layout.fillWidth: true; color: root.savedId !== "" ? theme.success : theme.warn; font.pixelSize: 12; text: root.savedId !== "" ? "Saved to Files." : "Could not save it to Files." }
        PillButton { visible: root.savedId !== ""; text: "Share (QR)"; iconName: "share"; onClicked: if (root.shell) root.shell.sharePopup().openFor([root.savedId]) }
        PillButton { text: "Retake"; checked: true; onClicked: root.reset() }
    }
    Text { visible: root.problem !== ""; Layout.fillWidth: true; wrapMode: Text.Wrap; color: theme.danger; font.pixelSize: 12; text: root.problem }

    // ---- taking the pictures ----
    function reset() {
        tick.stop(); pause.stop()
        count = 0; busy = false; frames = []; resultPath = ""; savedId = ""; problem = ""
    }
    function nextCamera() {
        const list = devices.videoInputs
        let i = 0
        for (let k = 0; k < list.length; ++k)
            if (list[k].id === camera.cameraDevice.id) i = k
        camera.cameraDevice = list[(i + 1) % list.length]
    }
    function start() {
        reset()
        busy = true
        countdown(3)
    }
    function countdown(n) {
        count = n
        tick.restart()
    }
    function snap() {
        count = 0
        flashAnim.restart()
        sounds.preview("drop")
        capture.captureToFile(photoBooth.folder() + "/frame-" + frames.length + ".jpg")
    }
    function shotSaved(path) {
        const next = frames.slice()
        next.push(path)
        frames = next
        if (frames.length < photoBooth.shots(tmpl)) {
            pause.restart() // a breath between the pictures of a strip
        } else {
            resultPath = photoBooth.compose(tmpl, filter, mirror, frames, theme.accent)
            busy = false
            if (resultPath === "") { problem = "The picture could not be made."; return }
            savedId = photoBooth.keep(resultPath)
        }
    }
    Timer { id: tick; interval: 1000; onTriggered: { if (root.count > 1) { root.count--; tick.restart() } else root.snap() } }
    Timer { id: pause; interval: 1400; onTriggered: root.countdown(3) }
    Component.onDestruction: photoBooth.cleanup()
}
