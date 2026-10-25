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
    signal openShares()
    signal toggleLyrics()
    spacing: 6

    PillButton { text: "Files"; iconName: "folder"; checked: bar.page === "shelf"; onClicked: bar.pick("shelf") }
    PillButton { visible: bar.musicAvailable; text: "Music"; iconName: "music"; checked: bar.page === "music"; onClicked: bar.pick("music") }
    PillButton { text: "Timer"; iconName: "timer"; checked: bar.page === "timer"; onClicked: bar.pick("timer") }
    PillButton { visible: bar.colorAvailable; text: "Color"; iconName: "palette"; checked: bar.page === "color"; onClicked: bar.pick("color") }
    PillButton { visible: bar.shareAvailable; text: bar.shareBusy ? "Sharing" : "Share"; iconName: "share"; checked: bar.shareBusy; onClicked: bar.openShares() }
    Item { Layout.fillWidth: true }
    PillButton { visible: bar.page === "music"; text: "Lyrics"; checked: bar.lyricsOn; onClicked: bar.toggleLyrics() }
    PillButton { visible: bar.page === "shelf"; text: "Note"; iconName: "pen"; onClicked: bar.newNote() }
    PillButton { visible: bar.page === "shelf"; text: "Add"; iconName: "plus"; onClicked: bar.addFiles() } // adding files belongs to Files
}
