import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// A meeting while it happens: what is said appears as it is said (You on the right, the others on the left), in the language it was said
// in and, if asked, translated. "Suggest answer" writes a few things you could say next. The recording goes on underneath and is saved
// when you stop.
Item {
    id: root
    required property var page
    readonly property var live: audioIntel.live
    property bool optsOpen: true
    property bool answers: false
    readonly property bool failed: !!live && live.status === "failed"

    ColumnLayout {
        anchors.fill: parent
        anchors.bottomMargin: 18
        spacing: 10

        // ---------------- header ----------------
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 54
            radius: theme.radiusMd
            color: theme.dangerBg
            border.width: 1
            border.color: Qt.alpha(theme.danger, 0.5)
            RowLayout {
                anchors { fill: parent; leftMargin: 14; rightMargin: 8 }
                spacing: 10
                Rectangle { Layout.preferredWidth: 10; Layout.preferredHeight: 10; radius: 5; color: theme.danger
                    SequentialAnimation on opacity { running: audioIntel.recording && activities.animations; loops: Animation.Infinite; NumberAnimation { to: 0.3; duration: 600 } NumberAnimation { to: 1; duration: 600 } } }
                Text { text: root.page.clock(audioIntel.recordSeconds); color: theme.text; font.pixelSize: 14; font.weight: Font.DemiBold; font.family: "monospace" }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 4; radius: 2; color: theme.surface
                    Rectangle { width: parent.width * Math.min(1, audioIntel.recordLevel * 1.4); height: parent.height; radius: 2; color: theme.success } }
                PillButton { text: "Stop and save"; primary: true; onClicked: audioIntel.stopRecording(false) }
                PillButton { text: "Improve"; onClicked: audioIntel.stopRecording(true); Accessible.description: "Stop, then make the full transcript and notes" }
                IconButton { iconName: "trash"; Accessible.name: "Throw the recording away"; onClicked: audioIntel.discardRecording() }
            }
        }

        // ---------------- status ----------------
        Text {
            visible: !!root.live && (root.live.status === "loading" || root.live.notice !== "")
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: 11
            color: root.failed || (root.live && root.live.status === "behind") ? theme.warn : theme.textDim
            text: !root.live ? "" : root.live.notice !== "" ? root.live.notice : "Loading the speech model… the recording is already running."
        }

        Caption { visible: !!root.live && root.live.engine !== ""; Layout.fillWidth: true; text: root.live ? "Speech: " + root.live.engine : "" }

        // ---------------- languages ----------------
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: opts.implicitHeight + 16
            radius: theme.radiusMd
            color: theme.well
            border.width: 1
            border.color: theme.hairline
            ColumnLayout {
                id: opts
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 8 }
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: root.live ? "You: " + languages.label(root.live.mine) + "   ·   Others: " + languages.label(root.live.theirs)
                                          + (root.live.translate ? "   →   " + languages.label(root.live.target) : "") : ""
                        color: theme.textSoft; font.pixelSize: 12; elide: Text.ElideRight
                    }
                    PillButton { text: root.optsOpen ? "Hide" : "Languages"; onClicked: { lines.userOpened = true; root.optsOpen = !root.optsOpen } }
                }
                GridLayout {
                    visible: root.optsOpen && !!root.live
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 10
                    rowSpacing: 8
                    Text { text: "I speak"; color: theme.textDim; font.pixelSize: 12; Layout.alignment: Qt.AlignTop; Layout.topMargin: 6 }
                    LanguagePicker { value: root.live ? root.live.mine : "auto"; onPicked: (c) => root.live.mine = c
                        onMoreRequested: { root.page.shell.settingsCategory = "languages"; root.page.shell.page = "settings" } }
                    Text { text: "Others speak"; color: theme.textDim; font.pixelSize: 12; Layout.alignment: Qt.AlignTop; Layout.topMargin: 6 }
                    LanguagePicker { value: root.live ? root.live.theirs : "auto"; onPicked: (c) => root.live.theirs = c
                        onMoreRequested: { root.page.shell.settingsCategory = "languages"; root.page.shell.page = "settings" } }
                    Text { text: "Translate into"; color: theme.textDim; font.pixelSize: 12; Layout.alignment: Qt.AlignTop; Layout.topMargin: 6 }
                    LanguagePicker { value: root.live ? root.live.target : "en"; allowAuto: false; onPicked: (c) => root.live.target = c
                        onMoreRequested: { root.page.shell.settingsCategory = "languages"; root.page.shell.page = "settings" } }
                    Item { width: 1; height: 1 }
                    PillButton { text: "Translate everything into " + (root.live ? languages.label(root.live.target) : ""); checked: !!root.live && root.live.translate; enabled: !!root.live && root.live.llmReady
                        onClicked: root.live.translate = !root.live.translate }
                    Item { width: 1; height: 1 }
                    Text {
                        visible: !!root.live && !root.live.llmReady
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: theme.textFaint; font.pixelSize: 11
                        text: "Translation and answers need the language model: download it in Settings → AI."
                    }
                }
            }
        }

        // ---------------- the conversation ----------------
        ListView {
            id: lines
            visible: !root.answers
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 80
            clip: true
            spacing: 8
            model: root.live ? root.live.model : null
            property bool following: true
            onMovementStarted: following = false
            onAtYEndChanged: if (atYEnd) following = true
            onCountChanged: {
                if (count > 0 && root.optsOpen && !userOpened) root.optsOpen = false
                if (following) Qt.callLater(positionViewAtEnd)
            }
            property bool userOpened: false
            delegate: Item {
                id: row
                required property int side
                required property string startText
                required property string text
                required property string translation
                required property string language
                required property bool partial
                width: lines.width
                implicitHeight: bubble.implicitHeight
                readonly property bool mine: side === 0
                Rectangle {
                    id: bubble
                    width: Math.min(parent.width * 0.86, Math.max(120, bubbleCol.implicitWidth + 24))
                    x: row.mine ? parent.width - width : 0
                    implicitHeight: bubbleCol.implicitHeight + 16
                    height: implicitHeight
                    radius: theme.radiusMd
                    color: row.mine ? Qt.alpha(theme.accent, 0.16) : theme.surface
                    border.width: 1
                    border.color: row.mine ? Qt.alpha(theme.accent, 0.4) : theme.hairline
                    ColumnLayout {
                        id: bubbleCol
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12; topMargin: 8 }
                        spacing: 3
                        Text { text: (row.mine ? "You" : "Others") + "  ·  " + row.startText + (row.partial ? "  ·  listening…" : ""); color: theme.textFaint; font.pixelSize: 10 }
                        Text { Layout.fillWidth: true; text: row.text; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: row.partial ? theme.textSoft : theme.text; font.pixelSize: 13; lineHeight: 1.15; font.italic: row.partial
                            horizontalAlignment: languages.rtl(row.language) ? Text.AlignRight : Text.AlignLeft }
                        Text { Layout.fillWidth: true; visible: row.translation !== ""; text: row.translation; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: theme.textSoft; font.pixelSize: 12; font.italic: true; lineHeight: 1.15 }
                    }
                }
            }
            Text {
                visible: lines.count === 0
                anchors.centerIn: parent
                width: parent.width - 40
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: theme.textDim; font.pixelSize: 12
                text: root.failed ? "" : "Listening… what is said appears here a moment after it is said."
            }
        }

        // ---------------- suggested answers ----------------
        ScrollView {
            id: answerView
            visible: root.answers
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 80
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: answerView.availableWidth - 10
                spacing: 8
                Text { visible: !!root.live && root.live.suggesting; text: "Thinking of answers…"; color: theme.textDim; font.pixelSize: 12 }
                Text { visible: !!root.live && !root.live.suggesting && root.live.suggestions.length === 0; Layout.fillWidth: true; wrapMode: Text.Wrap; color: theme.textDim; font.pixelSize: 12
                    text: "Nothing yet. Press “Suggest answer” once someone has said something." }
                Repeater {
                    model: root.live ? root.live.suggestions : []
                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: card.implicitHeight + 20
                        radius: theme.radiusMd
                        color: theme.surface
                        border.width: 1
                        border.color: theme.hairline
                        RowLayout {
                            id: card
                            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 12 }
                            spacing: 10
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                Text { text: modelData.tone === "short" ? "Short" : modelData.tone === "detailed" ? "Detailed" : "Polite"; color: theme.textFaint; font.pixelSize: 10; font.weight: Font.DemiBold }
                                Text { Layout.fillWidth: true; text: modelData.text; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: theme.text; font.pixelSize: 13; lineHeight: 1.15 }
                            }
                            PillButton { text: "Copy"; onClicked: audioIntel.copyText(modelData.text) }
                        }
                    }
                }
            }
        }

        // ---------------- bottom: smart answer ----------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            TextField {
                id: note
                visible: root.answers
                Layout.fillWidth: true
                implicitHeight: 32
                placeholderText: "Optional: who you are in this meeting, what you want"
                placeholderTextColor: theme.placeholder
                color: theme.text
                selectionColor: theme.accent
                font.pixelSize: 12
                background: Rectangle { radius: 16; color: theme.well; border.width: 1; border.color: note.activeFocus ? theme.accent : theme.hairline }
                onAccepted: root.live.suggestAnswer(text)
            }
            Item { visible: !root.answers; Layout.fillWidth: true }
            PillButton { visible: root.answers; text: "Conversation"; onClicked: root.answers = false }
            PillButton {
                text: root.answers ? "Suggest again" : "Suggest answer"
                primary: true
                enabled: !!root.live && root.live.llmReady && !root.live.suggesting && lines.count > 0
                onClicked: { root.answers = true; root.live.suggestAnswer(note.text) }
            }
        }
        Caption { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; text: "Only record when everyone who is heard agrees. Everything here stays on this computer." }
    }
}
