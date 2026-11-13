import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

// Audio: record or import a recording, and get a transcript, who said what, and notes with decisions and tasks. Everything is made on this
// computer. This file holds what the parts share (which recording is open, what is about to be imported, the player) and chooses between the
// list of recordings (AudioList) and one recording (AudioDetail).
Item {
    id: page
    required property var shell
    visible: shell.page === "audio"
    Layout.fillWidth: true
    Layout.maximumWidth: 100000
    Layout.leftMargin: 12
    Layout.rightMargin: 12
    implicitHeight: 500
    onVisibleChanged: if (!visible && player.item) player.item.stop()

    readonly property bool inDetail: audioIntel.current !== 0
    readonly property var info: audioIntel.info
    readonly property bool jobHere: audioIntel.busy && audioIntel.jobMeeting === audioIntel.current
    readonly property bool batchRunning: audioIntel.busy || audioIntel.queued > 0
    readonly property var activeStage: { const st = audioIntel.stages; for (let i = 0; i < st.length; ++i) if (st[i].state === "active") return st[i]; return null }
    readonly property var palette: ["#5b9bd5", "#5fb878", "#e0a030", "#d0607a", "#9a85e0", "#40b0c0"]
    readonly property var templates: [["meeting", "Meeting"], ["lecture", "Lecture"], ["interview", "Interview"], ["general", "General"]]
    readonly property var speakerCounts: [[0, "Auto"], [2, "2"], [3, "3"], [4, "4"], [5, "5"]]
    readonly property alias player: playerLoader
    property var transcriptList: null // set by the transcript, to make it follow the audio again
    property real rate: 1
    property real pendingSeek: -1
    // An answer pointed at a recording and a minute: open it there
    function takeRequest() {
        const r = shell.audioOpen
        if (!r)
            return
        shell.audioOpen = null
        openMeeting(r.id)
        pendingSeek = r.sec >= 0 ? r.sec : -1
    }
    Component.onCompleted: takeRequest()
    Connections { target: shell; function onAudioOpenChanged() { page.takeRequest() } }
    Timer { // the player appears when the recording has loaded
        interval: 300
        repeat: true
        running: page.pendingSeek >= 0
        property int tries: 0
        onTriggered: {
            if (player.item && page.info.audioUrl) { page.playAt(page.pendingSeek); page.pendingSeek = -1; tries = 0 }
            else if (++tries > 20) { page.pendingSeek = -1; tries = 0 }
        }
    }

    property string tab: audioIntel.firstTab.split("+")[0]   // NOTCH_SHOT_TAB=transcript+translate: for pictures, the translate row open
    property bool showHistory: false
    property bool findOpen: false
    property bool translateOpen: audioIntel.firstTab.indexOf("+translate") > 0
    property string findText: ""
    property bool styleOpen: false
    property bool confirmDelete: false
    property string renaming: ""       // the speaker label being renamed
    property bool redetect: false      // the "tell the speakers apart again" row is open
    property int redetectCount: 0
    property bool dropping: false
    // what is about to be imported: one file (what audioIntel.inspect said of it) or several, and the options
    property var pending: ({})
    property var pendingMany: []
    property string pendingTitle: ""
    property string pendingLanguage: audioIntel.language
    property string pendingQuality: audioIntel.quality
    property string pendingTemplate: audioIntel.tmpl
    property int pendingSpeakers: 0
    readonly property int manyOk: { let n = 0; for (let i = 0; i < pendingMany.length; ++i) if (pendingMany[i].ok) ++n; return n }

    function clock(seconds) { return shell.clock(Math.max(0, seconds) * 1000) }
    function modelOf(id) {
        const list = audioIntel.models
        for (let i = 0; i < list.length; ++i)
            if (list[i].id === id)
                return list[i]
        return ({ state: "missing", progress: 0, error: "", sizeText: "", title: id, receivedText: "", speedText: "", etaText: "", partial: false })
    }
    function statusText(s) { return s === "completed" ? "Done" : s === "processing" ? "Working" : s === "queued" ? "Waiting" : s === "failed" ? "Stopped" : "Ready" }
    function statusColor(s) { return s === "completed" ? theme.success : s === "failed" ? theme.danger : s === "processing" || s === "queued" ? theme.accent : theme.textFaint }
    function styleName(id) { for (let i = 0; i < templates.length; ++i) if (templates[i][0] === id) return templates[i][1]; return "Meeting" }
    function friendly(label) { const m = /^SPEAKER_(\d+)$/.exec(label); return m ? "Speaker " + (parseInt(m[1]) + 1) : label }
    function speakerName(label) {
        const list = audioIntel.speakers
        for (let i = 0; i < list.length; ++i)
            if (list[i].label === label)
                return list[i].name !== "" ? list[i].name : friendly(label)
        return friendly(label)
    }
    function follow() { if (transcriptList) transcriptList.following = true }
    function playAt(seconds) {
        if (!player.item)
            return
        tab = "transcript"
        follow()
        player.item.playFrom(seconds)
    }
    function openMeeting(id) { tab = audioIntel.firstTab.split("+")[0]; confirmDelete = false; findOpen = false; redetect = false; styleOpen = false; audioIntel.open(id) }
    function closeDetail() { if (player.item) player.item.stop(); audioIntel.close() }
    function removeCurrent() { if (player.item) player.item.stop(); audioIntel.remove(audioIntel.current, true) }
    function openFiles() { fileDialog.open() }
    function pickMany(urls) {
        const list = []
        for (let i = 0; i < urls.length; ++i)
            list.push(audioIntel.inspect(urls[i].toString()))
        pending = ({})
        pendingMany = list
        resetOptions()
    }
    function pick(path) {
        const i = audioIntel.inspect(path)
        pending = i
        pendingMany = []
        pendingTitle = i.name ? i.name.replace(/\.[^.]*$/, "") : ""
        resetOptions()
    }
    function resetOptions() {
        pendingLanguage = audioIntel.language
        pendingQuality = audioIntel.quality
        pendingTemplate = audioIntel.tmpl
        pendingSpeakers = 0
    }
    function importPending(processIt) {
        if (pendingMany.length > 0) {
            const paths = []
            for (let i = 0; i < pendingMany.length; ++i)
                if (pendingMany[i].ok)
                    paths.push(pendingMany[i].path)
            pendingMany = []
            audioIntel.importMany(paths, pendingLanguage, pendingQuality, pendingTemplate, pendingSpeakers)
        } else {
            audioIntel.importFile(pending.path, pendingTitle, pendingLanguage, pendingQuality, pendingTemplate, processIt, pendingSpeakers)
            pending = ({})
        }
    }

    // Linux plays through the system's default output (QtMultimedia can miss the one you hear); elsewhere QtMultimedia, loaded only here
    Loader {
        id: playerLoader
        visible: false // nothing to draw
        active: page.inDetail && !!page.info.audioUrl
        source: Qt.platform.os === "linux" ? "AudioPlayerLinux.qml" : "AudioPlayer.qml"
        onLoaded: {
            item.source = Qt.binding(() => page.info.audioUrl || "")
            item.rate = Qt.binding(() => page.rate)
            if (item.length !== undefined) {
                item.length = Qt.binding(() => page.info.duration || 0)
                item.mixdown = Qt.binding(() => !!page.info.stereo)
            }
        }
    }

    FileDialog {
        id: fileDialog
        onVisibleChanged: page.shell.dialogs += visible ? 1 : -1
        Component.onDestruction: if (visible) page.shell.dialogs -= 1 // a page closed under an open dialog must not leave the notch below the windows
        title: "Choose recordings"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["Audio (*.mp3 *.wav *.m4a *.flac *.ogg *.opus *.webm *.aac *.mp4)", "All files (*)"]
        currentFolder: StandardPaths.writableLocation(StandardPaths.MusicLocation)
        onAccepted: selectedFiles.length > 1 ? page.pickMany(selectedFiles) : page.pick(selectedFiles[0].toString())
    }

    AudioList { anchors.fill: parent; page: page; visible: !page.inDetail && !audioIntel.live }
    AudioLive { anchors.fill: parent; page: page; visible: !!audioIntel.live && !page.inDetail }
    AudioDetail { anchors { fill: parent; bottomMargin: 18 } page: page; visible: page.inDetail }

    DropArea { // a file dropped anywhere on the page
        anchors.fill: parent
        onEntered: (d) => { if (d.hasUrls) page.dropping = true }
        onExited: page.dropping = false
        onDropped: (d) => {
            page.dropping = false
            if (!d.hasUrls)
                return
            if (page.inDetail)
                page.closeDetail()
            if (d.urls.length > 1) page.pickMany(d.urls); else if (d.urls.length > 0) page.pick(d.urls[0].toString())
        }
    }
}
