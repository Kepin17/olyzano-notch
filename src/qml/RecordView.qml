import QtCore
import QtMultimedia
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import QtQuick.Layouts

// The screen recorder: the screen (or one window, or only the camera), the camera laid over it where you choose, the microphone and the sound of
// the computer. The screen and the camera are recorded as two files and put together when you stop (RecordController, ffmpeg); the sound of the
// computer is a third. Nothing is captured until you press Preview or Record; on Wayland the system asks which screen or window to share.
ScrollView {
    id: scroll
    property alias shell: root.shell
    readonly property bool modalOpen: root.modalOpen
    contentWidth: availableWidth
    bottomPadding: 18
    clip: true
    ColumnLayout {
    id: root
    width: scroll.availableWidth - 10
    property var shell: null
    property string source: "screen"      // screen | window | camera
    property int screenIndex: 0
    property bool showCamera: true
    property bool mirror: true
    property string shape: "rounded"      // square | rounded | circle
    property real camX: 1.0               // 0..1 across the free space (RecordMix.h)
    property real camY: 1.0
    property real camSize: 0.22
    property bool micOn: true
    property bool systemOn: true
    property real micVolume: 1.0
    property real systemVolume: 1.0
    property string systemSourceId: ""
    property bool previewing: false
    // the application to record (source "window"): chosen from the open windows, or the one that was in front last (KWin tells; Plasma only)
    property string targetApp: ""
    property string targetTitle: ""
    property string targetWindowId: ""
    property string lastAppName: ""       // the app in front before the notch was opened
    property string lastAppTitle: ""
    property var windowChoices: []        // [{label, app, title, id}]
    property var sourceList: []
    // On Wayland Qt cannot list windows: the system's own window offers screens and windows together, so "Window" uses the screen capture there.
    readonly property bool wayland: Qt.platform.pluginName.indexOf("wayland") >= 0
    // The screen capture is made by the recorder (C++) and never destroyed: Qt's PipeWire capture crashes (pw_thread_loop_stop on a loop that
    // does not exist) when one is stopped or deleted before the system has answered. It is stopped only once a picture has come from it.
    property var screenCap: null
    property bool capRequested: false     // active was set and the capture is neither stopped nor failed
    property bool capLive: false          // a picture has come from it: the choice was made and it is safe to stop
    property bool capStopWanted: false    // stop asked while the system's window was still open: done when the first picture comes
    property bool picking: false          // the system's window for choosing what to share is (probably) open
    property bool camLatched: false       // once the camera has been on it stays on until the page closes (stopping a V4L2 camera can crash Qt's ffmpeg backend)
    // the recording
    property string phase: "idle"         // idle | countdown | recording | finishing | mixing
    property int count: 0
    property int elapsed: 0
    property string session: ""
    property double screenStarted: 0
    property double camStarted: 0
    property double systemStarted: 0
    property bool screenDone: false
    property bool camDone: false
    property string problem: ""
    property string savedNote: ""
    readonly property bool software: memtune.running
    readonly property bool hasCamera: devices.videoInputs.length > 0
    readonly property bool camWanted: source === "camera" || showCamera
    readonly property bool busy: phase !== "idle"
    readonly property bool modalOpen: false
    readonly property var screenList: Qt.application.screens
    spacing: 8

    MediaDevices { id: devices }
    CameraPermission { id: permission }
    onCamWantedChanged: if (camWanted) camLatched = true
    Component.onCompleted: {
        if (permission.status === Qt.Undetermined)
            permission.request()
        sourceList = recorder.soundSources()
        if (sourceList.length > 0) systemSourceId = sourceList[0].id
        if (camWanted) camLatched = true
        tuning.prepare() // the size of the camera's picture is set before it starts
    }
    Component.onDestruction: {
        if (phase === "recording" || phase === "countdown" || phase === "picking") root.stopAll(true)
        recorder.stopSystemSound()
        if (winCap && winCap.active) winCap.active = false
        endPick()
        recorder.publishState("idle", 0, false, "", false)
    }
    // what the notch shows when it is closed (Main.qml) and what keeps this page alive when it is
    onPhaseChanged: publish()
    onElapsedChanged: publish()
    onPreviewingChanged: publish()
    onTargetNameChanged: publish()
    function publish() { recorder.publishState(phase, elapsed, previewing, targetName, captureHeld) }

    // ---- what is captured ----
    AudioInput { id: micA; device: micChoice.currentIndex >= 0 && micChoice.currentIndex < devices.audioInputs.length ? devices.audioInputs[micChoice.currentIndex] : devices.defaultAudioInput }
    AudioInput { id: micB; device: micA.device }
    // The window capture (not Wayland: the screen capture does windows there) is not PipeWire's and is made only when the list of windows is shown.
    Loader {
        id: winLoader
        active: root.source === "window" && !root.wayland // it also gives the list of windows, so it exists as soon as that is shown
        sourceComponent: WindowCapture {
            onActiveChanged: root.captureChanged(active)
            onErrorOccurred: (error, message) => root.captureFailed(message)
        }
    }
    Connections { // the first picture: the system has been answered
        target: screenVideo.videoSink
        function onVideoFrameChanged() { if (root.capRequested && !root.capLive) root.capFrame() }
    }
    Connections {
        target: root.screenCap
        ignoreUnknownSignals: true
        function onErrorOccurred(error, message) { root.captureFailed(message) }
        // The system's window is answered (or closed): the notch must come back up from under the windows at once, not at the first picture,
        // which a still screen may be slow to send. (Stopping is still only done after a picture: capLive.)
        function onActiveChanged() {
            if (root.screenCap.active) {
                root.endPick()
                if (root.phase === "picking") root.startCountdown()
            } else if (root.capRequested && !root.capLive) { // closed without an answer
                root.endPick()
                root.previewing = false
                if (root.phase === "picking") root.stopAll(false)
                root.capRequested = false
                root.screenCap = null
            }
        }
    }
    readonly property var winCap: winLoader.item
    Camera {
        id: camera
        active: tuning.ready && root.camLatched && permission.status === Qt.Granted && root.hasCamera
        cameraDevice: camChoice.currentIndex >= 0 && camChoice.currentIndex < devices.videoInputs.length ? devices.videoInputs[camChoice.currentIndex] : devices.defaultVideoInput
    }
    CaptureSession { // the screen (or the window) with the microphone
        screenCapture: root.source === "screen" || (root.source === "window" && root.wayland) ? root.screenCap : null
        windowCapture: root.source === "window" && !root.wayland ? root.winCap : null
        audioInput: root.micOn && root.source !== "camera" ? micA : null
        recorder: screenRec
        videoOutput: screenVideo
    }
    CaptureSession { // the camera (with the microphone when the camera is all there is)
        camera: camera
        audioInput: root.micOn && root.source === "camera" ? micB : null
        recorder: camRec
        videoOutput: camVideo
    }
    MediaRecorder {
        id: screenRec
        mediaFormat { fileFormat: MediaFormat.MPEG4; videoCodec: MediaFormat.VideoCodec.H264; audioCodec: MediaFormat.AudioCodec.AAC }
        quality: MediaRecorder.NormalQuality
        videoFrameRate: 30
        onRecorderStateChanged: {
            if (recorderState === MediaRecorder.RecordingState && root.screenStarted === 0) root.screenRecording()
            else if (recorderState === MediaRecorder.StoppedState && root.phase === "finishing") { root.screenDone = true; root.maybeMix() }
        }
        onErrorOccurred: (error, message) => root.fail(message)
    }
    MediaRecorder {
        id: camRec
        mediaFormat { fileFormat: MediaFormat.MPEG4; videoCodec: MediaFormat.VideoCodec.H264; audioCodec: MediaFormat.AudioCodec.AAC }
        quality: MediaRecorder.NormalQuality
        videoFrameRate: 30
        onRecorderStateChanged: {
            if (recorderState === MediaRecorder.RecordingState && root.camStarted === 0) root.camStarted = Date.now()
            else if (recorderState === MediaRecorder.StoppedState && root.phase === "finishing") { root.camDone = true; root.maybeMix() }
        }
        onErrorOccurred: (error, message) => root.fail(message)
    }

    // ---- which application ----
    function refreshWindows() {
        const out = []
        for (const it of dock.items)
            if (it.running && it.windows)
                for (const w of it.windows)
                    out.push({ label: it.name + (w.title ? "  —  " + w.title : ""), app: it.name, title: w.title || "", id: w.id })
        windowChoices = out
    }
    Connections { target: dock; function onItemsChanged() { if (root.source === "window") root.refreshWindows() } }
    Connections {
        target: dock
        function onActiveAppChanged() { if (dock.activeAppName !== "") { root.lastAppName = dock.activeAppName; root.lastAppTitle = dock.activeTitle } }
    }
    onSourceChanged: if (source === "window") { dock.setWatching(true); refreshWindows() }
    function chooseWindow(app, title, id) { targetApp = app; targetTitle = title; targetWindowId = id; previewing = false; if (screenMode) releaseCapture() }
    // "the app that is open": the one that was in front, its window found in the list of the open ones
    function chooseActive() {
        if (lastAppName === "") return
        source = "window"
        refreshWindows()
        let hit = windowChoices.find(w => w.app === lastAppName && w.title === lastAppTitle) || windowChoices.find(w => w.app === lastAppName)
        chooseWindow(lastAppName, hit ? hit.title : lastAppTitle, hit ? hit.id : "")
    }
    function raiseTarget() { if (source === "window" && targetWindowId !== "") dock.activateWindow(targetWindowId) } // the system asks for the window to share: this one is in front
    readonly property string targetName: source === "window" ? targetApp : ""

    // ---- the picture ----
    Rectangle {
        id: stage
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(200, width * 9 / 16)
        radius: theme.radiusMd
        color: "#000000"
        clip: true
        VideoOutput { id: screenVideo; anchors.fill: parent; visible: root.source !== "camera" && root.previewing; fillMode: VideoOutput.PreserveAspectFit }
        Item { // the camera: the whole picture when it is all there is, else laid over the screen as it will be in the file
            id: overlay
            readonly property bool alone: root.source === "camera"
            visible: root.camLatched && (alone || root.showCamera)
            readonly property real wide: Math.max(40, stage.width * root.camSize)
            readonly property real margin: stage.width * 0.025
            width: alone ? stage.width : wide
            height: alone ? stage.height : (root.shape === "circle" ? wide : wide * 0.75)
            x: alone ? 0 : margin + (stage.width - width - 2 * margin) * root.camX
            y: alone ? 0 : margin + (stage.height - height - 2 * margin) * root.camY
            Rectangle { id: camMask; anchors.fill: parent; radius: overlay.alone ? 0 : root.shape === "circle" ? width / 2 : root.shape === "rounded" ? width / 10 : 0; visible: false; layer.enabled: true }
            VideoOutput {
                id: camVideo
                anchors.fill: parent
                fillMode: overlay.alone ? VideoOutput.PreserveAspectFit : VideoOutput.PreserveAspectCrop
                transform: Scale { origin.x: camVideo.width / 2; xScale: root.mirror ? -1 : 1 }
                layer.enabled: !root.software && ((!overlay.alone && root.shape !== "square") || tuning.tuned)
                layer.effect: MultiEffect { brightness: tuning.effectBrightness; contrast: tuning.effectContrast; maskEnabled: !overlay.alone && root.shape !== "square"; maskSource: camMask; maskThresholdMin: 0.5; maskSpreadAtMin: 1.0 }
            }
            DragHandler { // drag it anywhere
                enabled: !overlay.alone && !root.busy
                target: null
                onCentroidChanged: if (active) {
                    const p = stage.mapFromItem(null, centroid.scenePosition.x, centroid.scenePosition.y)
                    root.camX = Math.max(0, Math.min(1, (p.x - overlay.width / 2 - overlay.margin) / Math.max(1, stage.width - overlay.width - 2 * overlay.margin)))
                    root.camY = Math.max(0, Math.min(1, (p.y - overlay.height / 2 - overlay.margin) / Math.max(1, stage.height - overlay.height - 2 * overlay.margin)))
                }
            }
        }
        Text {
            anchors.centerIn: parent
            visible: root.source !== "camera" && !root.previewing
            width: parent.width - 40
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            color: "#c8c8cc"
            font.pixelSize: 13
            text: "Press Preview to see " + (root.source === "window" ? "the window" : "the screen") + ". Nothing is captured before that."
        }
        Text {
            anchors.centerIn: parent
            visible: root.source === "camera" && (!root.hasCamera || permission.status === Qt.Denied)
            color: "#c8c8cc"
            font.pixelSize: 13
            text: permission.status === Qt.Denied ? "Camera access is turned off for Olyzano Notch (system privacy settings)." : "No camera found."
        }
        Text { anchors.centerIn: parent; visible: root.phase === "countdown"; text: root.count; color: "#ffffff"; font.pixelSize: 110; font.weight: Font.Light; style: Text.Outline; styleColor: "#66000000" }
        Row {
            anchors { left: parent.left; top: parent.top; margins: 10 }
            spacing: 6
            visible: root.phase === "recording"
            Rectangle { width: 9; height: 9; radius: 4.5; color: "#ff453a"; anchors.verticalCenter: parent.verticalCenter }
            Text { text: root.clock(root.elapsed); color: "#ffffff"; font.pixelSize: 12; font.weight: Font.DemiBold; style: Text.Outline; styleColor: "#80000000" }
        }
    }

    // ---- 1. what to record ----
    Segmented {
        Layout.fillWidth: true
        enabled: !root.busy
        model: [["screen", "Screen"], ["window", "Window"], ["camera", "Camera only"]]
        value: root.source
        onPicked: (v) => { root.releaseCapture(); root.source = v; root.previewing = false }
    }
    RowLayout {
        visible: root.source === "screen" && root.screenList.length > 1
        Layout.fillWidth: true
        spacing: 8
        Caption { text: "Screen" }
        AgentChoice {
            Layout.fillWidth: true
            enabled: !root.busy
            model: root.screenList.map((s, i) => ({ label: (s.name || ("Screen " + (i + 1))) + "  ·  " + s.width + "×" + s.height }))
            textRole: "label"
            currentIndex: root.screenIndex
            onActivated: (i) => { root.releaseCapture(); root.screenIndex = i; root.previewing = false }
        }
    }
    RowLayout {
        visible: root.source === "window" && !root.wayland
        Layout.fillWidth: true
        spacing: 8
        Caption { text: "Window" }
        AgentChoice {
            id: winChoice
            Layout.fillWidth: true
            enabled: !root.busy
            property var wins: []
            model: wins
            textRole: "label"
            onActivated: (i) => { if (root.winCap) root.winCap.capturableWindow = winChoice.wins[i].window; root.previewing = false }
            function refresh() { if (!root.winCap) return; wins = root.winCap.capturableWindows().map(w => ({ label: w.description, window: w })); if (wins.length > 0) { const t = root.targetTitle.toLowerCase(), a = root.targetApp.toLowerCase(); let k = t !== "" ? wins.findIndex(w => w.label.toLowerCase().indexOf(t) >= 0) : -1; if (k < 0 && a !== "") k = wins.findIndex(w => w.label.toLowerCase().indexOf(a) >= 0); currentIndex = Math.max(0, k); root.winCap.capturableWindow = wins[currentIndex].window } }
            Component.onCompleted: refresh()
            Connections { target: root; function onSourceChanged() { if (root.source === "window") Qt.callLater(winChoice.refresh) } }
        }
        PillButton { text: "Refresh"; iconName: "undo"; enabled: !root.busy; onClicked: winChoice.refresh() }
    }
    Caption { visible: root.source === "window" && !root.wayland && winChoice.wins.length === 0; Layout.fillWidth: true; text: "No windows to list right now. Open the window you want, then press Refresh." }
    RowLayout {
        visible: root.source === "window" && root.wayland
        Layout.fillWidth: true
        spacing: 8
        Caption { text: "Application" }
        AgentChoice {
            Layout.fillWidth: true
            enabled: !root.busy
            model: [{ label: "Any window (choose in the system's window)" }].concat(root.windowChoices)
            textRole: "label"
            currentIndex: Math.max(0, root.windowChoices.findIndex(w => w.id === root.targetWindowId) + 1)
            onActivated: (i) => i === 0 ? root.chooseWindow("", "", "") : root.chooseWindow(root.windowChoices[i - 1].app, root.windowChoices[i - 1].title, root.windowChoices[i - 1].id)
        }
        PillButton { text: "Refresh"; iconName: "undo"; enabled: !root.busy; onClicked: root.refreshWindows() }
    }
    Caption {
        visible: root.source === "window" && root.wayland
        Layout.fillWidth: true
        textFormat: Text.PlainText
        text: root.targetApp !== "" ? "«" + root.targetApp + "» is brought to the front, then the system asks which window to share: choose that one. The recording is named after it."
                                     : root.windowChoices.length > 0 ? "Choose an application, or press Preview or Record and choose the window in the system's own window."
                                                                    : "Press Preview or Record and choose the window in the system's own window. (The list of open applications comes from KDE Plasma.)"
    }
    RowLayout {
        Layout.fillWidth: true
        PillButton { visible: root.source !== "camera"; text: root.previewing ? "Stop preview" : "Preview"; iconName: "eye"; enabled: !root.busy
            onClicked: { root.previewing = !root.previewing; if (root.previewing && !root.capLive) root.raiseTarget(); root.setCapture(root.previewing) } }
        PillButton { visible: root.screenMode && root.capLive && !root.previewing && root.phase === "idle"; text: "Release source"; iconName: "close"
            ToolTip.visible: hovered; ToolTip.text: "What you chose to share is kept for a few minutes so Record does not ask again. Release it to choose another."
            onClicked: root.releaseCapture() }
        PillButton { visible: root.lastAppName !== "" && root.phase === "idle"; text: "Record " + root.lastAppName; iconName: "eye"; enabled: !root.busy
            ToolTip.visible: hovered; ToolTip.text: "The application that was in front: " + String(root.lastAppTitle).replace(/&/g, "&amp;").replace(/</g, "&lt;")
            onClicked: root.chooseActive() }
        Item { Layout.fillWidth: true }
    }

    // ---- 2. the camera ----
    SettingToggle {
        visible: root.source !== "camera"
        title: "Show the camera"
        caption: "Laid over the recording where you put it. Drag it in the picture, or use the places below."
        checked: root.showCamera
        enabled: !root.busy && root.hasCamera
        onToggled: root.showCamera = !root.showCamera
    }
    ColumnLayout {
        visible: root.camWanted && root.hasCamera
        Layout.fillWidth: true
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Caption { text: "Camera" }
            AgentChoice {
                id: camChoice
                Layout.fillWidth: true
                enabled: !root.busy
                model: devices.videoInputs.map(d => d.description)
            }
            PillButton { text: "Flip"; iconName: "undo"; checked: root.mirror; enabled: !root.busy; onClicked: root.mirror = !root.mirror; ToolTip.visible: hovered; ToolTip.text: "Mirror the camera left to right" }
        }
        CameraTuning { id: tuning; camera: camera }
        ColumnLayout {
            visible: root.source !== "camera"
            Layout.fillWidth: true
            spacing: 8
            Segmented { Layout.fillWidth: true; enabled: !root.busy; model: [["rounded", "Rounded"], ["square", "Square"], ["circle", "Circle"]]; value: root.shape; onPicked: (v) => root.shape = v }
            ValueSlider { title: "Size"; valueText: Math.round(root.camSize * 100) + " % of the width"; from: 0.1; to: 0.4; stepSize: 0.01; value: root.camSize; enabled: !root.busy; onMoved: (v) => root.camSize = v }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Caption { text: "Place" }
                Repeater {
                    model: [["Top left", 0, 0], ["Top right", 1, 0], ["Bottom left", 0, 1], ["Bottom middle", 0.5, 1], ["Bottom right", 1, 1]]
                    PillButton { required property var modelData; text: modelData[0]; enabled: !root.busy; checked: Math.abs(root.camX - modelData[1]) < 0.02 && Math.abs(root.camY - modelData[2]) < 0.02
                        onClicked: { root.camX = modelData[1]; root.camY = modelData[2] } }
                }
                Item { Layout.fillWidth: true }
            }
        }
    }
    Caption { visible: root.camWanted && !root.hasCamera; Layout.fillWidth: true; text: "No camera found." }

    // ---- 3. the sound ----
    SettingToggle { title: "Microphone"; caption: "Your voice."; checked: root.micOn; enabled: !root.busy; onToggled: root.micOn = !root.micOn }
    RowLayout {
        visible: root.micOn
        Layout.fillWidth: true
        spacing: 8
        Caption { text: "Input" }
        AgentChoice { id: micChoice; Layout.fillWidth: true; enabled: !root.busy; model: devices.audioInputs.map(d => d.description)
            currentIndex: { for (let i = 0; i < devices.audioInputs.length; ++i) if (devices.audioInputs[i].id === devices.defaultAudioInput.id) return i; return 0 } }
    }
    ValueSlider { visible: root.micOn; title: "Microphone volume"; valueText: Math.round(root.micVolume * 100) + " %"; from: 0; to: 2; stepSize: 0.05; value: root.micVolume; enabled: !root.busy; onMoved: (v) => root.micVolume = v }
    SettingToggle {
        visible: recorder.canRecordSystem
        title: "Sound of the computer"
        caption: "What the speakers play: a video, a call, music."
        checked: root.systemOn
        enabled: !root.busy
        onToggled: root.systemOn = !root.systemOn
    }
    Caption { visible: !recorder.canRecordSystem; Layout.fillWidth: true; text: "The sound of the computer needs parec (package pulseaudio-utils)." }
    RowLayout {
        visible: root.systemOn && recorder.canRecordSystem
        Layout.fillWidth: true
        spacing: 8
        Caption { text: "Output" }
        AgentChoice {
            id: systemChoices
            Layout.fillWidth: true
            enabled: !root.busy
            textRole: "name"
            model: root.sourceList
            onActivated: (i) => root.systemSourceId = root.sourceList[i].id
        }
    }
    ValueSlider { visible: root.systemOn && recorder.canRecordSystem; title: "Computer sound volume"; valueText: Math.round(root.systemVolume * 100) + " %"; from: 0; to: 2; stepSize: 0.05; value: root.systemVolume; enabled: !root.busy; onMoved: (v) => root.systemVolume = v }

    // ---- record ----
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Text { Layout.fillWidth: true; elide: Text.ElideRight; color: root.phase === "recording" ? theme.danger : theme.textDim; font.pixelSize: 12
            text: root.phase === "recording" ? "Recording  " + root.clock(root.elapsed) : root.phase === "mixing" ? "Putting it together…" : root.phase === "finishing" ? "Stopping…" : root.phase === "picking" ? "Choose what to share in the window of the system…" : root.phase === "countdown" ? "Starting…" : "" }
        PillButton { visible: root.phase === "mixing"; text: "Stop"; onClicked: recorder.cancel() }
        PillButton {
            visible: root.phase === "idle"
            text: "Record"
            iconName: "play"
            checked: true
            enabled: recorder.canMix && (root.source !== "camera" || (root.hasCamera && permission.status === Qt.Granted))
            onClicked: root.startRecording()
        }
        PillButton { visible: root.phase === "picking" || root.phase === "countdown" || root.phase === "recording"; text: "Stop"; iconName: "stop"; checked: true; onClicked: root.stopAll(false) }
    }
    Caption { visible: !recorder.canMix; Layout.fillWidth: true; text: "Recording needs ffmpeg (package ffmpeg)." }
    Text { visible: root.problem !== ""; Layout.fillWidth: true; wrapMode: Text.Wrap; color: theme.danger; font.pixelSize: 12; text: root.problem; textFormat: Text.PlainText }
    Text { visible: root.savedNote !== ""; Layout.fillWidth: true; wrapMode: Text.Wrap; color: theme.success; font.pixelSize: 12; text: root.savedNote }

    Timer { id: tick; interval: 1000; repeat: true; onTriggered: {
        if (root.phase === "countdown") {
            root.count -= 1
            if (root.count <= 0) { tick.stop(); root.begin() }
        } else if (root.phase === "recording") {
            root.elapsed = Math.round((Date.now() - root.screenStarted) / 1000)
            if (root.elapsed >= 4 * 3600) root.stopAll(false)
        }
    } }
    Timer { id: pickTimer; interval: 120000; onTriggered: { root.endPick(); if (root.phase === "picking") root.stopAll(false) } }
    Timer { id: settle; interval: 700; onTriggered: root.doMix() } // the recorders have closed their files
    Timer { id: guard; interval: 8000; onTriggered: if (root.phase === "finishing") root.doMix() }
    Connections {
        target: recorder
        function onFinished(error) {
            root.phase = "idle"
            if (error !== "") root.problem = error
            else root.savedNote = "Saved to Files."
        }
    }

    function clock(s) { const m = Math.floor(s / 60), h = Math.floor(m / 60); const two = n => (n < 10 ? "0" : "") + n; return (h > 0 ? h + ":" + two(m % 60) : m) + ":" + two(s % 60) }
    // The system asks what to share in a window of its own: the notch steps under the windows and stays open until it is answered.
    function beginPick() {
        if (picking || !shell) return
        picking = true
        shell.dialogs += 1
        pickTimer.restart()
    }
    function endPick() {
        if (!picking) return
        picking = false
        pickTimer.stop()
        if (shell) shell.dialogs -= 1
    }
    readonly property bool screenMode: source === "screen" || (source === "window" && wayland)
    function currentCapture() { return screenMode ? screenCap : source === "window" ? winCap : null }
    function captureActive() { return screenMode ? capLive : (!!winCap && winCap.active) }
    function startScreenCap() {
        if (!screenCap) screenCap = recorder.newScreenCapture()
        if (!screenCap) { fail("Screen capture needs Qt's multimedia module (qml6-module-qtmultimedia)."); return }
        if (root.screenList.length > root.screenIndex) screenCap.screen = root.screenList[root.screenIndex]
        capRequested = true
        screenCap.active = true
    }
    function stopScreenCap() { // only when it is live
        if (!capLive || !screenCap) return
        capLive = false
        capRequested = false
        capStopWanted = false
        screenCap.active = false
    }
    function capFrame() {
        capLive = true
        if (capStopWanted) { Qt.callLater(stopScreenCap); return }
        captureChanged(true)
        if (!previewing && phase === "idle") holdTimer.restart()
    }
    // The system's window asks what to share each time a capture is started, so once it is answered the capture is kept (a few minutes) after
    // Stop preview and after a recording: Record then does not ask again, and the capture is not stopped while the recorder is still closing its file.
    // It is let go when another source is chosen, with "Release", or when nothing has used it for a while.
    Timer { id: holdTimer; interval: 180000; onTriggered: if (root.phase === "idle" && !root.previewing) root.releaseCapture() }
    readonly property bool captureHeld: screenMode && (capRequested || capLive) // the page must stay while this is so (RecordController::engaged)
    onCaptureHeldChanged: publish()
    function holdCapture() {
        if (!screenMode) { releaseCapture(); return }
        endPick()
        if (capLive) holdTimer.restart() // (still being asked: it is kept when it answers)
    }
    function releaseCapture() {
        holdTimer.stop()
        if (screenMode) {
            endPick()
            if (capLive) stopScreenCap()
            else if (capRequested) capStopWanted = true // the system is still being asked: stopping now is what crashes; it ends at the first picture
            return
        }
        const c = winCap
        if (c) c.active = false
        endPick()
    }
    function setCapture(on) { // on: start (or go on with the one that is kept); off: keep it for a while
        if (on) {
            holdTimer.stop()
            if (screenMode) {
                capStopWanted = false
                if (!capRequested) { beginPick(); startScreenCap() } // already running: no window of the system will come, and the notch must not be left under the windows
                return
            }
            const c = winCap
            if (!(c && c.active)) beginPick()
            if (c) c.active = true
            if (!c) endPick()
        } else {
            holdCapture()
        }
    }
    function captureChanged(active) { // the capture started (the choice was made) or ended
        if (active) {
            endPick()
            if (phase === "picking") startCountdown()
        }
    }
    function captureFailed(why) {
        endPick()
        if (screenMode) { capRequested = false; capLive = false; capStopWanted = false; screenCap = null } // that capture is left alone, never stopped or deleted
        previewing = false
        fail(why)
    }
    function fail(why) {
        endPick()
        root.problem = why
        if (phase === "recording" || phase === "countdown") stopAll(true)
    }
    function startRecording() {
        problem = ""; savedNote = ""
        session = recorder.newSession()
        if (session === "") { problem = "A folder for the recording could not be made."; return }
        screenStarted = 0; camStarted = 0; systemStarted = 0; screenDone = false; camDone = false; elapsed = 0
        if (camWanted) camLatched = true
        if (source === "camera") { startCountdown(); return }
        phase = "picking" // the screen or window is chosen first, then the countdown
        if (!capLive) raiseTarget()
        setCapture(true)
        if (captureActive()) startCountdown()
    }
    function startCountdown() {
        count = 3
        phase = "countdown"
        tick.restart()
    }
    function begin() {
        const dir = "file://" + session + "/"
        const mainIsCamera = source === "camera"
        if (mainIsCamera) {
            camRec.outputLocation = dir + "main.mp4"
            camRec.record()
            screenStarted = 0
        } else {
            if (!captureActive()) setCapture(true)
            screenRec.outputLocation = dir + "main.mp4"
            screenRec.record()  // the camera and the sound of the computer start when this has started
        }
        if (mainIsCamera) screenRecording()
    }
    function screenRecording() { // the main recording is running: start the rest at once, and note when each began
        screenStarted = Date.now()
        phase = "recording"
        if (source !== "camera" && showCamera && camera.active) {
            camRec.outputLocation = "file://" + session + "/cam.mp4"
            camRec.record()
        }
        if (systemOn && recorder.canRecordSystem && recorder.startSystemSound(session, systemSourceId))
            systemStarted = Date.now()
        tick.restart()
    }
    function stopAll(abandon) {
        tick.stop()
        if (phase === "countdown" || phase === "picking") { phase = "idle"; recorder.discard(session); if (!previewing) setCapture(false); return }
        phase = abandon ? "idle" : "finishing"
        screenDone = screenRec.recorderState === MediaRecorder.StoppedState
        camDone = camRec.recorderState === MediaRecorder.StoppedState
        if (screenRec.recorderState !== MediaRecorder.StoppedState) screenRec.stop()
        if (camRec.recorderState !== MediaRecorder.StoppedState) camRec.stop()
        recorder.stopSystemSound()
        if (!previewing) setCapture(false)
        if (abandon) { recorder.discard(session); return }
        guard.restart()
        maybeMix()
    }
    function maybeMix() { if (phase === "finishing" && screenDone && camDone) settle.restart() }
    function doMix() {
        if (phase !== "finishing") return
        guard.stop(); settle.stop()
        phase = "mixing"
        const hasCam = source !== "camera" && showCamera && camStarted > 0
        recorder.mix({
            session: session,
            screen: "main.mp4",
            camera: hasCam ? "cam.mp4" : "",
            system: systemStarted > 0 ? "system.wav" : "",
            screenHasSound: micOn,
            cameraOffset: hasCam ? Math.max(0, (camStarted - screenStarted) / 1000) : 0,
            systemOffset: systemStarted > 0 ? Math.max(0, (systemStarted - screenStarted) / 1000) : 0,
            x: camX, y: camY, size: camSize, shape: shape, mirror: mirror,
            micVolume: micVolume, systemVolume: systemVolume,
            brightness: tuning.brightness, contrast: tuning.contrast, app: targetName
        })
    }
}
}
