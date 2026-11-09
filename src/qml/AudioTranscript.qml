import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The transcript: who is in it (tap a name to change it), a suggestion when the conversation names someone, and the lines, grouped by
// speaker, following the audio as it plays.
ColumnLayout {
    id: view
    required property var page
    spacing: 8

    // who is in it
    Flow {
        Layout.fillWidth: true
        spacing: 10
        Repeater {
            model: audioIntel.speakers
            delegate: Item {
                id: sp
                required property var modelData
                readonly property color tint: page.palette[modelData.index % page.palette.length]
                readonly property string shown: modelData.name !== "" ? modelData.name : page.friendly(modelData.label)
                width: renameField.visible ? 150 : legend.implicitWidth
                height: 26
                Row {
                    id: legend
                    visible: !renameField.visible
                    spacing: 6
                    anchors.verticalCenter: parent.verticalCenter
                    AudioAvatar { name: sp.shown; tint: sp.tint; size: 20; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: sp.shown; textFormat: Text.PlainText; color: theme.text; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: { page.renaming = sp.modelData.label; renameField.text = sp.modelData.name; renameField.forceActiveFocus() } }
                }
                TextField {
                    id: renameField
                    visible: page.renaming === sp.modelData.label
                    anchors.fill: parent
                    leftPadding: 10
                    topPadding: 0
                    bottomPadding: 0
                    placeholderText: "Name"
                    placeholderTextColor: theme.placeholder
                    color: theme.text
                    selectionColor: theme.accent
                    font.pixelSize: 12
                    maximumLength: 60
                    background: Rectangle { radius: 13; color: theme.surface; border.width: 1; border.color: theme.accent }
                    onAccepted: { audioIntel.renameSpeaker(sp.modelData.label, text); page.renaming = "" }
                    Keys.onEscapePressed: page.renaming = ""
                    onActiveFocusChanged: if (!activeFocus && visible) page.renaming = ""
                }
            }
        }
        Text {
            text: page.redetect ? "Close" : "Detect again"
            color: theme.accent
            font.pixelSize: 12
            height: 26
            verticalAlignment: Text.AlignVCenter
            HoverHandler { cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: page.redetect = !page.redetect }
        }
    }

    // one thing to say at a time: a name the conversation suggests, or that only one speaker was found
    Rectangle {
        id: note
        readonly property var hint: audioIntel.nameHints.length > 0 ? audioIntel.nameHints[0] : null
        readonly property bool lonely: !hint && !!page.info.oneSpeaker && !page.redetect
        visible: (!!hint || lonely) && !page.redetect
        Layout.fillWidth: true
        implicitHeight: noteRow.implicitHeight + 14
        radius: theme.radiusSm + 2
        color: Qt.alpha(lonely ? theme.warn : theme.accent, 0.1)
        RowLayout {
            id: noteRow
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 10 }
            spacing: 8
            Text {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                color: theme.text
                font.pixelSize: 12
                text: note.hint ? (note.hint.certain ? "Is " : "Maybe ") + page.speakerName(note.hint.speaker) + " " + note.hint.name + "?  “" + note.hint.quote + "” " + note.hint.timeText
                                : "Only one speaker was found. If more people talk, say how many."
            }
            PillButton { visible: !!note.hint; text: "Apply"; checked: true; onClicked: audioIntel.applyNameHint(note.hint.speaker, note.hint.name) }
            PillButton { visible: !note.hint; text: "Detect again"; checked: true; onClicked: page.redetect = true }
            IconButton { visible: !!note.hint; iconName: "close"; implicitWidth: 24; implicitHeight: 24; Accessible.name: "Ignore"; onClicked: audioIntel.dismissNameHint(note.hint.speaker, note.hint.name) }
        }
    }
    Rectangle { // detect the speakers again, with the number of people when it is known
        visible: page.redetect
        Layout.fillWidth: true
        implicitHeight: redo.implicitHeight + 14
        radius: theme.radiusSm + 2
        color: theme.well
        RowLayout {
            id: redo
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 10 }
            spacing: 8
            Caption { text: "People" }
            Segmented { visible: !page.info.stereo; Layout.fillWidth: true; model: page.speakerCounts; value: page.redetectCount; onPicked: (v) => page.redetectCount = v }
            Caption { visible: !!page.info.stereo; Layout.fillWidth: true; text: "You and the computer's sound are told apart by channel; more voices in either are found by voice." }
            PillButton { text: "Detect"; checked: true; enabled: !audioIntel.busy; onClicked: { page.redetect = false; audioIntel.redetectSpeakers(audioIntel.current, page.info.stereo ? 0 : page.redetectCount) } }
        }
    }
    TextField {
        id: findField
        visible: page.findOpen
        Layout.fillWidth: true
        implicitHeight: 28
        leftPadding: 12
        placeholderText: "Find in the transcript"
        placeholderTextColor: theme.placeholder
        color: theme.text
        selectionColor: theme.accent
        font.pixelSize: 12
        background: Rectangle { radius: 14; color: theme.well; border.width: 1; border.color: findField.activeFocus ? theme.accent : theme.outline }
        onTextChanged: page.findText = text.toLowerCase()
        onVisibleChanged: if (visible) forceActiveFocus(); else { text = ""; page.findText = "" }
        Keys.onEscapePressed: page.findOpen = false
    }

    // the lines
    Item {
        Layout.fillWidth: true
        Layout.fillHeight: true
        ListView {
            id: list
            anchors.fill: parent
            clip: true
            spacing: 0
            boundsBehavior: Flickable.StopAtBounds
            model: audioIntel.transcript
            readonly property int playingRow: page.player.item ? audioIntel.segmentAt(page.player.item.position) : -1
            property bool following: true // scrolls along with the audio until the person scrolls it themselves
            Component.onCompleted: page.transcriptList = list
            function showPlaying() { if (following && playingRow >= 0 && page.player.item && page.player.item.playing) positionViewAtIndex(playingRow, ListView.Center) }
            onPlayingRowChanged: Qt.callLater(showPlaying)
            onMovementStarted: following = false
            Connections { target: page.player.item; ignoreUnknownSignals: true; function onPlayingChanged() { if (page.player.item && page.player.item.playing) { list.following = true; Qt.callLater(list.showPlaying) } } }
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: Item {
                id: line
                required property int index
                required property int segmentId
                required property real start
                required property string startText
                required property string who
                required property string text
                required property bool edited
                required property bool approx
                required property int speakerIndex
                required property bool sameAsPrev
                property bool editing: false
                readonly property bool shown: page.findText === "" || text.toLowerCase().indexOf(page.findText) >= 0 || person.toLowerCase().indexOf(page.findText) >= 0
                readonly property bool playingNow: list.playingRow === index
                readonly property color tint: page.palette[Math.max(0, speakerIndex) % page.palette.length]
                readonly property string person: page.friendly(who)
                visible: shown
                width: ListView.view.width - 10
                height: shown ? body.implicitHeight + (sameAsPrev ? 4 : 14) : 0
                Rectangle { anchors { fill: parent; topMargin: line.sameAsPrev ? 0 : 4 } radius: theme.radiusSm; color: line.playingNow ? Qt.alpha(theme.accent, 0.12) : rowHover.hovered ? theme.hoverFill : "transparent" }
                Rectangle { visible: line.playingNow; anchors { left: parent.left; top: parent.top; bottom: parent.bottom; topMargin: line.sameAsPrev ? 0 : 4 } width: 3; radius: 1.5; color: theme.accent }
                HoverHandler { id: rowHover }
                RowLayout {
                    id: body
                    anchors { left: parent.left; right: parent.right; top: parent.top; leftMargin: 10; rightMargin: 6; topMargin: line.sameAsPrev ? 2 : 8 }
                    spacing: 10
                    Item { // the speaker's badge; for a line that goes on, only its time
                        Layout.alignment: Qt.AlignTop
                        Layout.preferredWidth: 26
                        Layout.preferredHeight: 22
                        AudioAvatar { visible: !line.sameAsPrev; name: line.person; tint: line.tint; size: 24 }
                        Text { visible: line.sameAsPrev; anchors { right: parent.right; top: parent.top; topMargin: 2 } text: line.startText; color: theme.textFaint; font.pixelSize: 9; font.family: "monospace"; opacity: rowHover.hovered || line.playingNow ? 1 : 0.6
                            HoverHandler { cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: page.playAt(line.start) } }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        RowLayout {
                            visible: !line.sameAsPrev
                            Layout.fillWidth: true
                            spacing: 8
                            Text { text: line.person + (line.approx ? " · approx." : ""); textFormat: Text.PlainText; color: line.tint; font.pixelSize: 12; font.weight: Font.DemiBold }
                            Text { text: line.startText; color: theme.textFaint; font.pixelSize: 10; font.family: "monospace"
                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                                TapHandler { onTapped: page.playAt(line.start) } }
                        }
                        Text {
                            visible: !line.editing
                            Layout.fillWidth: true
                            text: line.text
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            color: theme.text
                            font.pixelSize: 13
                            lineHeight: 1.15
                            TapHandler { acceptedButtons: Qt.LeftButton; gesturePolicy: TapHandler.ReleaseWithinBounds; onDoubleTapped: { line.editing = true; editor.text = line.text; editor.forceActiveFocus() } }
                        }
                        TextArea {
                            id: editor
                            visible: line.editing
                            Layout.fillWidth: true
                            wrapMode: TextEdit.Wrap
                            color: theme.text
                            selectionColor: theme.accent
                            font.pixelSize: 13
                            background: Rectangle { radius: 6; color: theme.surface; border.width: 1; border.color: theme.accent }
                            Keys.onEscapePressed: line.editing = false
                            Keys.onReturnPressed: (e) => { if (e.modifiers & Qt.ShiftModifier) e.accepted = false; else { audioIntel.editSegment(line.segmentId, text); line.editing = false; e.accepted = true } }
                            onActiveFocusChanged: if (!activeFocus && line.editing) { audioIntel.editSegment(line.segmentId, text); line.editing = false }
                        }
                        Text { visible: line.edited && !line.editing; text: "edited"; color: theme.textFaint; font.pixelSize: 10; font.italic: true }
                    }
                    Row { // what can be done with a line, when the pointer is on it
                        Layout.alignment: Qt.AlignTop
                        visible: rowHover.hovered && !line.editing
                        spacing: 2
                        IconButton { iconName: "play"; implicitWidth: 24; implicitHeight: 24; Accessible.name: "Play from here"; onClicked: page.playAt(line.start) }
                        IconButton { iconName: "pen"; implicitWidth: 24; implicitHeight: 24; Accessible.name: "Edit this line"; onClicked: { line.editing = true; editor.text = line.text; editor.forceActiveFocus() } }
                        IconButton { iconName: "trash"; implicitWidth: 24; implicitHeight: 24; Accessible.name: "Delete this line"; onClicked: audioIntel.deleteSegment(line.segmentId) }
                    }
                }
            }
        }
        PillButton { // after scrolling away while it plays
            visible: !list.following && !!page.player.item && page.player.item.playing
            anchors { right: parent.right; bottom: parent.bottom; rightMargin: 16; bottomMargin: 8 }
            text: "Follow the audio"
            iconName: "play"
            checked: true
            onClicked: { list.following = true; list.showPlaying() }
        }
    }
}
