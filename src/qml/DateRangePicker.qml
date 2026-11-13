import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// A calendar for a range: the first click is the start, the second the end; times below. Values are epoch seconds (0 = none), local time zone.
ColumnLayout {
    id: pick
    property double opensAt: 0
    property double closesAt: 0
    signal picked(double opensAt, double closesAt)

    spacing: 8
    property date shown: new Date()
    property var startDay: opensAt > 0 ? dayOf(new Date(opensAt * 1000)) : null
    property var endDay: closesAt > 0 ? dayOf(new Date(closesAt * 1000)) : null
    readonly property date today: dayOf(new Date())

    function dayOf(d) { return new Date(d.getFullYear(), d.getMonth(), d.getDate()) }
    function same(a, b) { return a && b && a.getTime() === b.getTime() }
    function minutes(text, fallback) { const m = /^(\d{1,2}):(\d{2})$/.exec(text); return m && +m[1] < 24 && +m[2] < 60 ? +m[1] * 60 + +m[2] : fallback }
    function stamp(day, text, fallback) { if (!day) return 0; const t = minutes(text, fallback); return Math.floor(new Date(day.getFullYear(), day.getMonth(), day.getDate(), Math.floor(t / 60), t % 60).getTime() / 1000) }
    function emit() { picked(stamp(startDay, fromTime.text, 540), stamp(endDay, toTime.text, 1020)) }
    function clickDay(d) {
        if (!startDay || endDay) { startDay = d; endDay = null }
        else if (d < startDay) { endDay = startDay; startDay = d }
        else endDay = d
        emit()
    }
    function label(day, text) { return day ? day.toLocaleDateString(Qt.locale(), "ddd d MMM") + ", " + text : "…" }

    RowLayout {
        Layout.fillWidth: true
        PillButton { iconOnly: true; iconName: "chevron"; rotation: 180; text: "Previous month"; onClicked: pick.shown = new Date(pick.shown.getFullYear(), pick.shown.getMonth() - 1, 1) }
        Text { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; text: pick.shown.toLocaleDateString(Qt.locale(), "MMMM yyyy"); color: theme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
        PillButton { iconOnly: true; iconName: "chevron"; text: "Next month"; onClicked: pick.shown = new Date(pick.shown.getFullYear(), pick.shown.getMonth() + 1, 1) }
    }
    DayOfWeekRow { Layout.fillWidth: true; locale: Qt.locale(); delegate: Text { required property string shortName; text: shortName; horizontalAlignment: Text.AlignHCenter; color: theme.textDim; font.pixelSize: 10 } }
    MonthGrid {
        id: grid
        Layout.fillWidth: true
        Layout.preferredHeight: 190
        month: pick.shown.getMonth()
        year: pick.shown.getFullYear()
        locale: Qt.locale()
        delegate: Item {
            id: cell
            required property var model
            readonly property date d: pick.dayOf(model.date)
            readonly property bool inMonth: model.month === grid.month
            readonly property bool past: d < pick.today
            readonly property bool edge: pick.same(d, pick.startDay) || pick.same(d, pick.endDay)
            readonly property bool between: pick.startDay && pick.endDay && d > pick.startDay && d < pick.endDay
            Rectangle { anchors { fill: parent; margins: 1 } radius: 6; visible: cell.inMonth && (cell.edge || cell.between); color: cell.edge ? theme.accent : Qt.alpha(theme.accent, 0.22) }
            Rectangle { anchors { fill: parent; margins: 1 } radius: 6; visible: cell.inMonth && cell.d.getTime() === pick.today.getTime() && !cell.edge; color: "transparent"; border.width: 1; border.color: theme.accent }
            Text {
                anchors.centerIn: parent
                visible: cell.inMonth
                text: cell.model.day
                font.pixelSize: 12
                color: cell.edge ? "white" : cell.past ? theme.placeholder : theme.text
            }
            MouseArea { anchors.fill: parent; enabled: cell.inMonth && !cell.past; cursorShape: Qt.PointingHandCursor; onClicked: pick.clickDay(cell.d) }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Text { text: "From"; color: theme.textDim; font.pixelSize: 12 }
        TimeBox { id: fromTime; text: "09:00"; onEdited: pick.emit() }
        Text { text: "to"; color: theme.textDim; font.pixelSize: 12 }
        TimeBox { id: toTime; text: "17:00"; onEdited: pick.emit() }
        Item { Layout.fillWidth: true }
        PillButton { text: "Clear"; onClicked: { pick.startDay = null; pick.endDay = null; pick.picked(0, 0) } }
    }
    Text { Layout.fillWidth: true; wrapMode: Text.Wrap; color: theme.text; font.pixelSize: 12
           text: !pick.startDay ? "Pick the first day." : !pick.endDay ? "Opens " + pick.label(pick.startDay, fromTime.text) + ". Now pick the last day." : pick.label(pick.startDay, fromTime.text) + "  →  " + pick.label(pick.endDay, toTime.text) }

    component TimeBox: TextField {
        signal edited
        implicitWidth: 64
        implicitHeight: 28
        horizontalAlignment: Text.AlignHCenter
        inputMethodHints: Qt.ImhPreferNumbers
        maximumLength: 5
        color: theme.text
        font.pixelSize: 12
        background: Rectangle { radius: theme.chipRadius + 2; color: theme.well; border.width: 1; border.color: parent.activeFocus ? theme.accent : theme.hairline }
        onTextEdited: if (/^\d{1,2}:\d{2}$/.test(text)) edited()
    }
}
