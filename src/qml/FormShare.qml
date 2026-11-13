import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Sharing a form: choose how, then the link and the code to hand out, with the count of answers coming in.
ColumnLayout {
    id: sh
    required property var tab
    readonly property var cur: tab.current
    readonly property bool live: !!(cur && cur.running)
    Layout.fillWidth: true
    spacing: 12

    RowLayout {
        Layout.fillWidth: true
        PillButton { text: "Back"; iconName: "back"; onClicked: sh.tab.view = "list" }
        Item { Layout.fillWidth: true }
    }
    Text { Layout.fillWidth: true; text: sh.tab.draft.title || (sh.cur ? sh.cur.title : ""); textFormat: Text.PlainText; elide: Text.ElideRight; color: theme.text; font.pixelSize: 20; font.weight: Font.DemiBold }

    // ---- not shared yet ----
    ColumnLayout {
        visible: !sh.live
        Layout.fillWidth: true
        spacing: 12
        Text { text: "How do you want to share it?"; color: theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Repeater {
                model: [["local", "On this WiFi", "Anyone on your network scans the code. Works while Notch is open.", "laptop"],
                        ["relay", "Through my server", "Anyone with the link, from anywhere. Answers are sealed.", "server"]]
                delegate: Rectangle {
                    id: way
                    required property var modelData
                    readonly property bool on: sh.tab.shareMode === modelData[0]
                    readonly property bool usable: modelData[0] === "local" || sh.tab.servers.length > 0
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    implicitHeight: wayCol.implicitHeight + 24
                    radius: theme.radiusMd
                    color: on ? theme.accentFaint : theme.well
                    border.width: on ? 2 : 1
                    border.color: on ? theme.accent : theme.hairline
                    opacity: usable ? 1 : 0.5
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: { sh.tab.shareMode = way.modelData[0]; sh.tab.shareError = "" } }
                    Accessible.role: Accessible.RadioButton
                    Accessible.name: modelData[1]
                    Accessible.checked: on
                    ColumnLayout {
                        id: wayCol
                        anchors { fill: parent; margins: 12 }
                        spacing: 6
                        RowLayout { spacing: 8
                            LineIcon { glyph: way.modelData[3]; color: way.on ? theme.accentText : theme.textDim; sourceSize: Qt.size(16, 16) }
                            Text { Layout.fillWidth: true; text: way.modelData[1]; color: theme.text; font.pixelSize: 13; font.weight: Font.DemiBold; elide: Text.ElideRight } }
                        Caption { Layout.fillWidth: true; text: way.modelData[2] }
                    }
                }
            }
        }
        Caption { visible: sh.tab.shareMode === "local"; Layout.fillWidth: true; text: "Not encrypted: use it with people you trust on that network." }
        Caption {
            visible: sh.tab.shareMode === "relay" && sh.tab.goodServers.length === 0
            Layout.fillWidth: true
            color: theme.warn
            text: sh.tab.servers.some(s => s.old) ? "Your server needs an update to take forms (copy the new server/ files, then run docker compose up -d --build)." : "No server of yours is connected yet. Set one up under Settings → Sharing."
        }
        RowLayout {
            Layout.fillWidth: true
            visible: sh.tab.shareMode === "relay" && sh.tab.goodServers.length > 0
            Caption { text: "Server" }
            AgentChoice { Layout.fillWidth: true; model: sh.tab.goodServers.map(s => s.name); onActivated: i => sh.tab.serverId = sh.tab.goodServers[i].id }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: sh.tab.shareMode === "local" && forms.lanAddresses().length > 1
            Caption { text: "On network" }
            AgentChoice { Layout.fillWidth: true; model: forms.lanAddresses(); onActivated: i => sh.tab.address = forms.lanAddresses()[i] }
        }
        FormCard {
            RowLayout {
                spacing: 8
                LineIcon { glyph: "lock"; color: theme.textDim; sourceSize: Qt.size(14, 14) }
                Text { text: "Ask for a PIN (optional)"; color: theme.text; font.pixelSize: 13; font.weight: Font.Medium }
            }
            FormField {
                id: pinField
                Layout.fillWidth: true
                placeholderText: "4 to 12 digits"
                echoMode: TextInput.Password
                inputMethodHints: Qt.ImhDigitsOnly
                onTextEdited: sh.tab.pin = text
            }
        }
        Connections { target: sh.tab; function onPinChanged() { if (sh.tab.pin === "") pinField.clear() } }
        Text { visible: sh.tab.shareError !== ""; Layout.fillWidth: true; text: sh.tab.shareError; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: theme.danger; font.pixelSize: 12 }
        PillButton {
            Layout.alignment: Qt.AlignRight
            text: sh.tab.publishing ? "Sealing…" : "Start sharing"
            iconName: "share"
            primary: true
            enabled: !sh.tab.publishing && (sh.tab.shareMode === "local" || sh.tab.goodServers.length > 0)
            onClicked: sh.tab.startSharing()
        }
    }

    // ---- shared ----
    ColumnLayout {
        visible: sh.live
        Layout.fillWidth: true
        spacing: 12
        FormCard {
            highlight: true
            RowLayout {
                Layout.fillWidth: true
                spacing: 18
                Rectangle {
                    Layout.preferredWidth: 168; Layout.preferredHeight: 168
                    radius: 12
                    color: "#ffffff"
                    Image { anchors.centerIn: parent; width: 150; height: 150; fillMode: Image.PreserveAspectFit; smooth: false; cache: false; source: sh.live ? sh.tab.qrSource(sh.cur.url) : "" }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    RowLayout { spacing: 8
                        Rectangle { implicitWidth: 8; implicitHeight: 8; radius: 4; color: theme.success }
                        Text { text: sh.cur && sh.cur.mode === "relay" ? "Live on your server" : "Live on this WiFi"; color: theme.success; font.pixelSize: 12; font.weight: Font.DemiBold } }
                    RowLayout {
                        spacing: 22
                        ColumnLayout { spacing: 0
                            Text { text: sh.cur ? sh.cur.answers : 0; color: theme.text; font.pixelSize: 26; font.weight: Font.DemiBold }
                            Caption { text: sh.cur ? "of " + sh.cur.max + " answers" : "" } }
                        ColumnLayout { spacing: 0
                            Text { text: sh.cur ? sh.cur.people : 0; color: theme.text; font.pixelSize: 26; font.weight: Font.DemiBold }
                            Caption { text: sh.cur && sh.cur.people === 1 ? "person" : "people" } }
                    }
                    Caption { Layout.fillWidth: true
                        text: sh.cur && sh.cur.mode === "relay" ? "Anyone with the whole link can answer, even when Notch is closed. Answers are fetched when Notch is open." + (sh.cur.maxBytes > 0 ? " The server takes up to " + Math.round(sh.cur.maxBytes / 1048576) + " MB per answer." : "")
                                                                 : "Phones and computers on the same WiFi can scan this. It stops after the last answer or when the time is up." }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Rectangle {
                Layout.fillWidth: true; implicitHeight: 34; radius: theme.radiusSm; color: theme.well; clip: true
                Text { anchors { fill: parent; leftMargin: 12; rightMargin: 12 } verticalAlignment: Text.AlignVCenter; text: sh.cur ? sh.cur.url : ""; textFormat: Text.PlainText; elide: Text.ElideMiddle; color: theme.textDim; font.pixelSize: 12 }
            }
            PillButton { text: "Copy link"; iconName: "copy"; onClicked: notch.copyText(sh.cur.url) }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            PillButton { text: "See answers"; primary: true; onClicked: sh.tab.openAnswers(sh.tab.currentId) }
            Item { Layout.fillWidth: true }
            PillButton { text: "Stop sharing"; iconName: "stop"; onClicked: forms.close(sh.tab.currentId) }
        }
    }
}
