import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The plan the agent wrote (plan.md): read it, then Revise, Reject or Accept. `tab` holds whether it is open.
Rectangle { // the plan the agent wrote: read it, then decide
    id: planCard
    required property var tab
    signal revise()
    signal acceptAuto()
    Layout.fillWidth: true
    readonly property bool pending: codingAgent.planStatus === "pending"
    visible: codingAgent.planStatus !== ""
    Layout.preferredHeight: planCol.implicitHeight + 20
    Layout.fillHeight: tab.planOpen // reading the plan: it takes the room the conversation had
    radius: theme.radiusMd
    color: theme.well
    border.width: 1
    border.color: Qt.alpha(theme.accent, 0.5)
    ColumnLayout {
        id: planCol
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            LineIcon { glyph: "pen"; sourceSize: Qt.size(15, 15); color: theme.accentText }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Text {
                    text: planCard.pending ? "Plan ready" : codingAgent.planStatus === "accepted" ? "Plan accepted" : "Plan rejected"
                    color: theme.text; font.pixelSize: 13; font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    text: planCard.pending ? "Saved as plan.md. Read it, then choose, or just comment in the chat."
                        : codingAgent.planStatus === "accepted" ? "The agent is working from it. plan.md stays readable."
                        : "Kept as plan.md. Reopen it if you change your mind."
                    color: theme.textFaint; font.pixelSize: 11; elide: Text.ElideRight
                }
            }
            PillButton {
                visible: codingAgent.planStatus === "rejected"
                text: "Reopen"
                iconName: "undo"
                enabled: !tab.locked
                onClicked: codingAgent.reopenPlan()
            }
            PillButton {
                text: tab.planOpen ? "Hide plan.md" : "Read plan.md"
                iconName: "eye"
                onClicked: tab.planOpen = !tab.planOpen
            }
        }
        Rectangle { // the plan as a document: a column of text on its own page, like a Markdown preview
            visible: tab.planOpen
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 80
            Layout.preferredHeight: Math.min(320, planDoc.implicitHeight + 40)
            radius: theme.radiusSm
            color: theme.dark ? "#0f0f11" : "#fafafc"
            border.width: 1
            border.color: theme.hairline
            clip: true
            ScrollView {
                id: planView
                anchors.fill: parent
                contentWidth: availableWidth
                Item {
                    width: planView.availableWidth
                    implicitWidth: planView.availableWidth
                    implicitHeight: planDoc.implicitHeight + 40 // a ScrollView takes the content size from this, not from height
                    AgentCopyText {
                        id: planDoc
                        x: Math.max(20, (parent.width - 700) / 2)
                        y: 20
                        width: Math.min(700, parent.width - 40)
                        textFormat: TextEdit.RichText
                        font.pixelSize: 14
                        text: tab.renderDoc(codingAgent.plan)
                        onLinkActivated: link => codingAgent.openLink(link)
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: planCard.pending
            spacing: 6
            PillButton {
                text: "Revise"
                iconName: "pen"
                implicitHeight: 30
                enabled: !tab.locked
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: "Tell the agent what to change; it writes a new plan"
                onClicked: planCard.revise()
            }
            PillButton {
                text: "Reject"
                iconName: "close"
                implicitHeight: 30
                enabled: !tab.locked
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: "Drop this plan; nothing is changed"
                onClicked: codingAgent.rejectPlan()
            }
            Item { Layout.fillWidth: true }
            PillButton {
                text: "Accept · Manual"
                iconName: "check"
                primary: true
                implicitHeight: 30
                enabled: !tab.locked
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: "Start the work; you approve each edit and command"
                onClicked: codingAgent.acceptPlan("manual")
            }
            PillButton {
                text: "Accept · Auto"
                iconName: "play"
                implicitHeight: 30
                enabled: !tab.locked
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: "Start the work; edits and commands run without asking"
                onClicked: planCard.acceptAuto()
            }
        }
    }
}
