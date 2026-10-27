import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The calendar: a glass card under the clock with the month, today ringed, month and year buttons, and "Today".
Rectangle {
    id: card
    property bool open: false
    property int year: new Date().getFullYear()
    property int month: new Date().getMonth() + 1 // 1-12
    readonly property var cells: { topbar.firstDay; return topbar.monthGrid(year, month) }
    readonly property bool numbers: topbar.weekNumbers
    width: numbers ? 336 : 300
    implicitHeight: body.implicitHeight + 28
    height: implicitHeight
    radius: theme.radiusLg
    color: theme.dark ? Qt.rgba(0.09, 0.09, 0.10, Math.min(1, topbar.opacity + 0.25)) : Qt.rgba(1, 1, 1, Math.min(1, topbar.opacity + 0.25))
    border.width: 1
    border.color: theme.dark ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(0, 0, 0, 0.12)
    opacity: open ? 1 : 0
    visible: opacity > 0
    Behavior on opacity { enabled: activities.animations; NumberAnimation { duration: 140 } }
    onOpenChanged: if (open) today()

    function today() { const n = new Date(); year = n.getFullYear(); month = n.getMonth() + 1 }
    function step(by) {
        let m = month - 1 + by
        year += Math.floor(m / 12)
        month = ((m % 12) + 12) % 12 + 1
    }
    readonly property bool onToday: { const n = new Date(); return year === n.getFullYear() && month === n.getMonth() + 1 }

    component Nav: AbstractButton {
        implicitWidth: 28; implicitHeight: 28
        property real turn: 0
        background: Rectangle { radius: 14; color: parent.hovered ? theme.accentSoft : "transparent" }
        contentItem: Item { LineIcon { anchors.centerIn: parent; glyph: "chevron"; sourceSize: Qt.size(14, 14); color: theme.text; rotation: turn } }
    }

    ColumnLayout {
        id: body
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 14 }
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: Qt.locale().monthName(card.month - 1, Locale.LongFormat) + " " + card.year
                color: theme.text
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
            AbstractButton {
                visible: !card.onToday
                implicitHeight: 26
                implicitWidth: todayLabel.implicitWidth + 16
                onClicked: card.today()
                background: Rectangle { radius: 13; color: parent.hovered ? theme.accentSoft : "transparent"; border.width: 1; border.color: theme.outline }
                contentItem: Text { id: todayLabel; text: "Today"; color: theme.text; font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            }
            Nav { turn: 90; onClicked: card.step(-1) }
            Nav { turn: -90; onClicked: card.step(1) }
        }
        GridLayout {
            Layout.fillWidth: true
            columns: 7
            Layout.leftMargin: card.numbers ? 32 : 0
            columnSpacing: 0
            rowSpacing: 2
            Repeater {
                model: 7
                Text {
                    Layout.fillWidth: true; Layout.preferredWidth: 1
                    text: Qt.locale().dayName((topbar.firstDay + index) % 7, Locale.ShortFormat)
                    color: theme.textDim
                    font.pixelSize: 11
                    horizontalAlignment: Text.AlignHCenter
                }
            }
            Repeater {
                model: card.cells
                delegate: Item {
                    id: cell
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true; Layout.preferredWidth: 1
                    Layout.preferredHeight: 32
                    Text { // the week number starts each row
                        visible: card.numbers && cell.index % 7 === 0
                        x: -32
                        width: 32; height: parent.height
                        text: cell.modelData.week
                        color: theme.textFaint
                        font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    Rectangle {
                        anchors.centerIn: parent
                        width: 30; height: 30; radius: 15
                        color: cell.modelData.today ? theme.accent : "transparent"
                        Text {
                            anchors.centerIn: parent
                            text: cell.modelData.day
                            color: cell.modelData.today ? theme.onColor(theme.accent) : cell.modelData.inMonth ? theme.text : theme.textFaint
                            font.pixelSize: 13
                            font.weight: cell.modelData.today ? Font.DemiBold : Font.Normal
                        }
                    }
                }
            }
        }
    }
}
