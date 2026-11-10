import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Camera: a mirror, and a photo booth with templates and filters. The page itself is only this shell: the camera (QtMultimedia) lives in
// CameraView.qml, which is loaded when the page is opened and let go of when it closes, so nothing of Qt Multimedia is in memory or running otherwise.
ColumnLayout {
    id: page
    required property var shell
    visible: shell.page === "camera"
    property string section: shell.cameraSection   // camera | record
    readonly property string shown: recorder.engaged ? "record" : section // a recording or a preview in progress keeps its page
    readonly property bool modalOpen: !!view.item && view.item.modalOpen
    function recordView() { return shown === "record" && view.item && view.item.debugRun ? view.item : null }
    Layout.fillWidth: true
    Layout.leftMargin: 12
    Layout.rightMargin: 12
    spacing: 8

    Segmented {
        Layout.fillWidth: true
        model: [["camera", "Camera"], ["record", "Record screen"]]
        value: page.shown
        enabled: !recorder.engaged
        onPicked: (v) => page.section = v
    }
    // The page is as tall as what it holds up to a limit; taller than that (the camera with its adjustments open, on a small screen) it scrolls,
    // so the end of it is never cut off by the edge of the panel.
    Item {
        id: holder
        Layout.fillWidth: true
        // the window is 620 high, and the header, the page bar and the two choices above take about 180 of it: what is left is all there is
        readonly property real room: Math.max(240, page.shell.height - 190)
        Layout.preferredHeight: page.shown === "record" ? Math.min(470, room) : Math.min(view.item ? view.item.implicitHeight : 80, room)
        clip: true
        Flickable {
            id: scroller
            anchors.fill: parent
            contentWidth: width
            contentHeight: view.height + 14
            interactive: contentHeight > height
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: scroller.interactive ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }
            Loader {
                id: view
                width: scroller.width - (scroller.interactive ? 10 : 0)
                height: page.shown === "record" ? holder.height : item ? item.implicitHeight : 80
                active: page.visible || recorder.engaged // (Main.qml keeps this page loaded, out of sight, while the recorder is busy)
                source: page.shown === "record" ? "RecordView.qml" : "CameraView.qml"
                onLoaded: item.shell = page.shell
            }
        }
    }
    Text {
        visible: view.status === Loader.Error
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        color: theme.textDim
        font.pixelSize: 12
        text: "The camera and the recorder need Qt's multimedia module for QML (Debian and Ubuntu: qml6-module-qtmultimedia). Install it and open this page again."
    }
}
