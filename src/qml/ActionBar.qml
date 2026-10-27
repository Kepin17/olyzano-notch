import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// What the open notch can show, one section at a time: Files, Music, Timer, Share, and Add (drop files
// is an action too). Picking one sets the page; the buttons that have nothing to show are left out.
RowLayout {
    id: bar
    required property string page
    property bool musicAvailable: false
    property bool shareAvailable: false
    property bool shareBusy: false
    signal pick(string target)
    signal addFiles()
    signal newNote()
    property bool lyricsOn: false
    property bool colorAvailable: false
    property int tasksBadge: 0 // overdue tasks
    signal openShares()
    signal toggleLyrics()
    spacing: 6
    // With Music, Color and Sharing all there, the words of Note and Add no longer fit the panel: they shrink to icons.
    readonly property bool tight: (musicAvailable ? 1 : 0) + (colorAvailable ? 1 : 0) + (shareAvailable ? 1 : 0) >= 2

    // The pages as one wide control: every page is a labelled, equal part of the track (easy to hit), and a thumb slides
    // to the one in front. Click a part, drag the thumb across, scroll the wheel, or press left/right.
    Item {
        id: slider
        readonly property var pages: [
            { id: "shelf", label: "Files", icon: "folder", on: true },
            { id: "music", label: "Music", icon: "music", on: bar.musicAvailable },
            { id: "timer", label: "Timer", icon: "timer", on: true },
            { id: "tasks", label: bar.tasksBadge > 0 ? "Tasks · " + bar.tasksBadge : "Tasks", icon: "check", on: true },
            { id: "color", label: "Color", icon: "palette", on: bar.colorAvailable }
        ].filter(p => p.on)
        readonly property int current: { for (let i = 0; i < pages.length; ++i) if (pages[i].id === bar.page) return i; return -1 }
        readonly property real zone: width / Math.max(1, pages.length)
        readonly property bool labels: zone >= 70 // a narrow track keeps the name only on the open page
        readonly property bool dragging: pointer.pressed && pointer.moved
        function at(x) { return Math.max(0, Math.min(pages.length - 1, Math.floor(x / zone))) }
        function go(i) { if (i !== current && i >= 0) bar.pick(pages[i].id) }
        function step(d) { go(Math.max(0, Math.min(pages.length - 1, current + d))) }
        Layout.fillWidth: true
        Layout.maximumWidth: 400
        Layout.minimumWidth: 160
        implicitWidth: pages.length * 78
        implicitHeight: 36
        activeFocusOnTab: true
        Accessible.role: Accessible.PageTabList
        Accessible.name: "Page: " + (current >= 0 ? pages[current].label : "")
        Keys.onLeftPressed: step(-1)
        Keys.onRightPressed: step(1)

        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: theme.well
            border.width: 1
            border.color: theme.washi ? theme.outline : theme.hairline
        }
        Rectangle { // the thumb, one part wide
            visible: slider.current >= 0
            y: 3
            width: slider.zone - 6
            height: slider.height - 6
            radius: height / 2
            color: Qt.alpha(theme.accent, pointer.pressed ? 0.28 : 0.18)
            border.width: 1
            border.color: Qt.alpha(theme.accent, 0.5)
            x: 3 + (slider.dragging ? Math.max(0, Math.min(slider.width - slider.zone, pointer.dragX - slider.zone / 2)) : Math.max(0, slider.current) * slider.zone)
            Behavior on x { enabled: activities.animations && !slider.dragging; NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        }
        Repeater { // icon and name of every part (just the icon where the track is narrow)
            model: slider.pages
            Item {
                id: part
                required property var modelData
                required property int index
                readonly property bool open: index === slider.current
                x: index * slider.zone
                width: slider.zone
                height: slider.height
                Row {
                    anchors.centerIn: parent
                    spacing: 5
                    LineIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        glyph: part.modelData.icon
                        sourceSize: Qt.size(14, 14)
                        color: part.open ? theme.accentText : pointerHover.hovered && slider.at(pointerHover.point.position.x) === part.index ? theme.text : theme.textSoft
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: slider.labels || part.open
                        text: part.modelData.label
                        color: part.open ? theme.accentText : theme.textSoft
                        font.pixelSize: 12
                        font.weight: part.open ? Font.DemiBold : Font.Medium
                    }
                }
            }
        }
        HoverHandler { id: pointerHover; cursorShape: Qt.PointingHandCursor }
        MouseArea {
            id: pointer
            anchors.fill: parent
            property real dragX: 0
            property bool moved: false
            function follow(m) { dragX = Math.max(0, Math.min(slider.width, m.x)); slider.go(slider.at(dragX)) }
            onPressed: (m) => { slider.forceActiveFocus(); moved = false; follow(m) }
            onPositionChanged: (m) => { if (pressed) { moved = true; follow(m) } }
            onReleased: moved = false // the thumb settles on the page it ended on
            onWheel: (w) => slider.step((w.angleDelta.y || w.angleDelta.x) < 0 ? 1 : -1)
        }
    }
    PillButton { visible: bar.shareAvailable; text: bar.shareBusy ? "Sharing" : "Share"; iconName: "share"; checked: bar.shareBusy; onClicked: bar.openShares() }
    Item { Layout.fillWidth: true; Layout.preferredWidth: 0 }
    PillButton { visible: bar.page === "music"; text: "Lyrics"; checked: bar.lyricsOn; onClicked: bar.toggleLyrics() }
    PillButton { visible: bar.page === "shelf"; text: "Note"; iconName: "pen"; iconOnly: bar.tight; onClicked: bar.newNote() }
    PillButton { visible: bar.page === "shelf"; text: "Add"; iconName: "plus"; iconOnly: bar.tight; onClicked: bar.addFiles() } // adding files belongs to Files
}
