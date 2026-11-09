import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The notes of a recording as a document: overview, topics, decisions, tasks, risks, next steps (what the chosen style asks for).
ScrollView {
    id: view
    required property var page
    readonly property var notes: audioIntel.notes
    contentWidth: availableWidth
    clip: true
    ColumnLayout {
        width: view.availableWidth - 10
        spacing: 14

        // the style the notes are written in: one chip that opens the choices
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            Rectangle {
                id: styleChip
                implicitWidth: styleRow.implicitWidth + 20
                implicitHeight: 26
                radius: 13
                color: chipHover.hovered ? theme.hoverFill : "transparent"
                border.width: 1
                border.color: theme.hairline
                HoverHandler { id: chipHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: page.styleOpen = !page.styleOpen }
                Row {
                    id: styleRow
                    anchors.centerIn: parent
                    spacing: 6
                    Text { text: "Style: " + page.styleName(page.info.template); color: theme.text; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
                    LineIcon { glyph: "chevron"; sourceSize: Qt.size(12, 12); color: theme.textDim; rotation: page.styleOpen ? 90 : -90; anchors.verticalCenter: parent.verticalCenter }
                }
            }
            Flow {
                visible: page.styleOpen
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: page.templates
                    delegate: PillButton {
                        required property var modelData
                        text: modelData[1]
                        checked: (page.info.template || "meeting") === modelData[0]
                        onClicked: { page.styleOpen = false; audioIntel.regenerateNotes(audioIntel.current, modelData[0]) }
                    }
                }
            }
        }

        // nothing yet: the notes need the language model and its program
        Rectangle {
            visible: !audioIntel.info.hasNotes && !audioIntel.readiness.summary && audioIntel.readiness.summaryMissing.length > 0
            Layout.fillWidth: true
            implicitHeight: need.implicitHeight + 28
            radius: theme.radiusMd
            color: theme.well
            border.width: 1
            border.color: theme.hairline
            ColumnLayout {
                id: need
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 14 }
                spacing: 10
                Text { text: "Get notes, decisions and tasks"; color: theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
                Caption { Layout.fillWidth: true; text: "Two small downloads, once. The language model writes the notes and a small program runs it. Both stay on this computer; nothing is sent anywhere." }
                Repeater {
                    model: audioIntel.readiness.summaryMissing
                    delegate: ColumnLayout {
                        id: nrow
                        required property string modelData
                        readonly property var info: { audioIntel.models; return page.modelOf(modelData) }
                        Layout.fillWidth: true
                        spacing: 4
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Text { Layout.fillWidth: true; text: nrow.info.title + " · " + nrow.info.sizeText; elide: Text.ElideRight; color: theme.text; font.pixelSize: 12 }
                            PillButton { visible: nrow.info.state !== "downloading" && nrow.info.state !== "verifying"; text: nrow.info.partial ? "Continue" : "Download"; iconName: "download"; checked: true; onClicked: audioIntel.downloadModel(nrow.modelData) }
                            PillButton { visible: nrow.info.state === "downloading"; text: "Cancel"; onClicked: audioIntel.cancelDownload(nrow.modelData) }
                        }
                        Rectangle { visible: nrow.info.state === "downloading"; Layout.fillWidth: true; Layout.preferredHeight: 3; radius: 1.5; color: theme.surface
                            Rectangle { width: parent.width * nrow.info.progress; height: parent.height; radius: 1.5; color: theme.accent } }
                        Caption { visible: nrow.info.state === "downloading"; Layout.fillWidth: true; text: nrow.info.receivedText + (nrow.info.speedText !== "" ? " · " + nrow.info.speedText : "") + (nrow.info.etaText !== "" ? " · " + nrow.info.etaText : "") }
                        Text { visible: nrow.info.error !== ""; Layout.fillWidth: true; text: nrow.info.error; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: theme.danger; font.pixelSize: 11 }
                    }
                }
            }
        }
        Text {
            visible: !audioIntel.info.hasNotes && audioIntel.readiness.summary
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: theme.textDim
            font.pixelSize: 12
            text: "No notes yet. Pick a style above to write them."
        }

        // ---- the document ----
        ColumnLayout {
            visible: !!audioIntel.info.hasNotes
            Layout.fillWidth: true
            spacing: 16

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: !!view.notes.participants && view.notes.participants.length > 0
                    Repeater {
                        model: view.notes.participants || []
                        Rectangle {
                            required property string modelData
                            implicitWidth: pl.implicitWidth + 16
                            implicitHeight: 22
                            radius: 11
                            color: theme.well
                            border.width: 1
                            border.color: theme.hairline
                            Text { id: pl; anchors.centerIn: parent; text: parent.modelData; textFormat: Text.PlainText; color: theme.textSoft; font.pixelSize: 11 }
                        }
                    }
                }
                Text { visible: !!view.notes.executive; Layout.fillWidth: true; text: view.notes.executive || ""; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: theme.text; font.pixelSize: 14; lineHeight: 1.3 }
            }

            AudioSection {
                title: "Topics"
                shown: !!view.notes.topics && view.notes.topics.length > 0
                Repeater { model: view.notes.topics || []; delegate: AudioTopic { required property var modelData; required property int index; page: view.page; topic: modelData; number: index + 1 } }
            }
            AudioSection {
                title: "Concepts"
                shown: !!view.notes.concepts && view.notes.concepts.length > 0
                Repeater { model: view.notes.concepts || []; delegate: AudioTopic { required property var modelData; page: view.page; topic: modelData } }
            }
            AudioSection {
                title: "Questions and answers"
                shown: !!view.notes.qa && view.notes.qa.length > 0
                Repeater { model: view.notes.qa || []; delegate: AudioTopic { required property var modelData; required property int index; page: view.page; topic: modelData; number: index + 1 } }
            }
            AudioSection { title: "Key points"; shown: !!view.notes.keyPoints && view.notes.keyPoints.length > 0; AudioBullets { items: view.notes.keyPoints || [] } }
            AudioSection {
                title: "Decisions"
                shown: !!view.notes.decisions && view.notes.decisions.length > 0
                Repeater {
                    model: view.notes.decisions || []
                    delegate: Rectangle {
                        id: drow
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: dcol.implicitHeight + 18
                        radius: theme.radiusSm + 2
                        color: theme.well
                        Rectangle { anchors { left: parent.left; top: parent.top; bottom: parent.bottom; topMargin: 8; bottomMargin: 8 } width: 3; radius: 1.5; color: theme.accent }
                        ColumnLayout {
                            id: dcol
                            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; leftMargin: 14; rightMargin: 10 }
                            spacing: 3
                            Text { Layout.fillWidth: true; text: drow.modelData.description; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: theme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
                            Text { visible: drow.modelData.reason !== ""; Layout.fillWidth: true; text: drow.modelData.reason; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: theme.textDim; font.pixelSize: 12 }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                Text { visible: drow.modelData.owner !== ""; text: drow.modelData.owner; textFormat: Text.PlainText; color: theme.textSoft; font.pixelSize: 11 }
                                Text { visible: drow.modelData.review; text: "Needs review"; color: theme.warn; font.pixelSize: 10 }
                                Item { Layout.fillWidth: true }
                                AudioTimeLink { page: view.page; seconds: drow.modelData.time }
                            }
                        }
                    }
                }
            }
            AudioSection {
                title: "Action items"
                shown: !!view.notes.actions && view.notes.actions.length > 0
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: table.implicitHeight
                    radius: theme.radiusSm + 2
                    color: theme.well
                    clip: true
                    ColumnLayout {
                        id: table
                        width: parent.width
                        spacing: 0
                        RowLayout { // the head
                            Layout.fillWidth: true
                            Layout.leftMargin: 12
                            Layout.rightMargin: 12
                            Layout.topMargin: 8
                            Layout.bottomMargin: 6
                            spacing: 10
                            Text { Layout.fillWidth: true; text: "TASK"; color: theme.textFaint; font.pixelSize: 9; font.letterSpacing: 1.2 }
                            Text { Layout.preferredWidth: 84; text: "OWNER"; color: theme.textFaint; font.pixelSize: 9; font.letterSpacing: 1.2 }
                            Text { Layout.preferredWidth: 84; text: "DUE"; color: theme.textFaint; font.pixelSize: 9; font.letterSpacing: 1.2 }
                            Item { Layout.preferredWidth: 40 }
                        }
                        Repeater {
                            model: view.notes.actions || []
                            delegate: ColumnLayout {
                                id: arow
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 0
                                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.hairline }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.leftMargin: 12
                                    Layout.rightMargin: 12
                                    Layout.topMargin: 8
                                    Layout.bottomMargin: 8
                                    spacing: 10
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        Text { Layout.fillWidth: true; text: arow.modelData.task; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: arow.modelData.done ? theme.textFaint : theme.text; font.pixelSize: 12; font.strikeout: arow.modelData.done }
                                        Text { visible: arow.modelData.review || arow.modelData.priority !== ""; text: (arow.modelData.priority !== "" ? arow.modelData.priority : "") + (arow.modelData.review ? (arow.modelData.priority !== "" ? " · " : "") + "needs review" : ""); textFormat: Text.PlainText; color: arow.modelData.review ? theme.warn : theme.textDim; font.pixelSize: 10 }
                                    }
                                    Text { Layout.preferredWidth: 84; text: arow.modelData.assignee !== "" ? arow.modelData.assignee : "—"; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: theme.textDim; font.pixelSize: 11 }
                                    Text { Layout.preferredWidth: 84; text: arow.modelData.deadline !== "" ? arow.modelData.deadline : "—"; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: theme.textDim; font.pixelSize: 11 }
                                    AudioTimeLink { Layout.preferredWidth: 40; page: view.page; seconds: arow.modelData.time }
                                }
                            }
                        }
                    }
                }
            }
            AudioSection { title: "Risks"; shown: !!view.notes.risks && view.notes.risks.length > 0; AudioBullets { items: view.notes.risks || [] } }
            AudioSection { title: "Open questions"; shown: !!view.notes.openQuestions && view.notes.openQuestions.length > 0; AudioBullets { items: view.notes.openQuestions || [] } }
            AudioSection { title: "Next steps"; shown: !!view.notes.nextSteps && view.notes.nextSteps.length > 0; AudioBullets { items: view.notes.nextSteps || [] } }
            AudioSection { title: "Notable quotes"; shown: !!view.notes.quotes && view.notes.quotes.length > 0; AudioBullets { items: view.notes.quotes || [] } }
            AudioSection { title: "Other notes"; shown: !!view.notes.notes && view.notes.notes.length > 0; AudioBullets { items: view.notes.notes || [] } }
            Item { Layout.preferredHeight: 8 }
        }
    }
}
