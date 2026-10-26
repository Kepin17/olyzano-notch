import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Your tasks and their deadlines. One line adds a task: "Buy milk tomorrow 17:00", "Call Ana @ in 45m". The list is
// ordered by deadline; the nearest one also shows in the idle capsule, and a note comes 15 minutes before and at the deadline.
ColumnLayout {
    id: page
    required property var shell
    visible: shell.page === "tasks"
    Layout.fillWidth: true
    Layout.leftMargin: 12
    Layout.rightMargin: 12
    spacing: 8

    // The deadline picked by hand (a Date), or null: then the words typed in the field ("tomorrow 17:00") are read.
    property var due: null
    property bool picking: false
    function add() {
        if (tasks.add(field.text, page.due ? page.due.toISOString() : "")) {
            field.text = ""
            page.due = null
            page.picking = false
        }
    }
    function shift(days, hours, minutes) { // the picker's arrows; the first touch starts from the next full hour
        const d = page.due ? new Date(page.due.getTime()) : new Date(Math.ceil(Date.now() / 3600000) * 3600000)
        d.setDate(d.getDate() + days)
        d.setHours(d.getHours() + hours, d.getMinutes() + minutes, 0, 0)
        page.due = d
    }
    function setIn(ms) { page.due = new Date(Math.ceil((Date.now() + ms) / 60000) * 60000) }
    function setAt(h, plusDays) {
        const d = new Date()
        d.setDate(d.getDate() + plusDays)
        d.setHours(h, 0, 0, 0)
        if (d.getTime() <= Date.now() && plusDays === 0) d.setDate(d.getDate() + 1)
        page.due = d
    }
    onVisibleChanged: if (visible) field.forceActiveFocus()

    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        TextField {
            id: field
            Layout.fillWidth: true
            implicitHeight: 30
            leftPadding: 12
            rightPadding: 12
            topPadding: 0
            bottomPadding: 0
            verticalAlignment: TextInput.AlignVCenter
            placeholderText: "New task, e.g. Buy milk fri 17:00 (or pick a date below)"
            placeholderTextColor: theme.placeholder
            color: shell.textPrimary
            selectionColor: shell.accent
            font.pixelSize: 12
            background: Rectangle {
                radius: theme.radiusMd
                color: theme.surface
                border.width: 1
                border.color: field.activeFocus ? shell.accent : "transparent"
            }
            onAccepted: page.add()
            Keys.onEscapePressed: shell.page = "shelf"
        }
        PillButton { text: "Add"; iconName: "plus"; enabled: field.text.trim() !== ""; onClicked: page.add() }
    }
    Flow { // a deadline in one tap, or pick any date and time
        Layout.fillWidth: true
        spacing: 6
        PillButton { text: "In 1 hour"; onClicked: page.setIn(3600000) }
        PillButton { text: "This evening"; onClicked: page.setAt(18, 0) }
        PillButton { text: "Tomorrow 9:00"; onClicked: page.setAt(9, 1) }
        PillButton { text: page.due ? Qt.formatDateTime(page.due, "ddd d MMM, HH:mm") : "Pick date & time"; iconName: "timer"; checked: page.picking || page.due !== null; onClicked: { if (!page.due) page.shift(0, 0, 0); page.picking = !page.picking } }
        PillButton { visible: page.due !== null; text: "No deadline"; onClicked: { page.due = null; page.picking = false } }
    }
    Rectangle { // the picker: any day and time
        visible: page.picking && page.due !== null
        Layout.fillWidth: true
        implicitHeight: pickRow.implicitHeight + 16
        radius: theme.radiusMd
        color: theme.surface
        component Step: RowLayout {
            id: st
            property string label
            property string value
            signal down()
            signal up()
            spacing: 2
            Text { text: st.label; color: theme.textDim; font.pixelSize: 11; Layout.rightMargin: 2 }
            IconButton { iconName: "minus"; Accessible.name: "Earlier"; onClicked: st.down() }
            Text { text: st.value; color: shell.textPrimary; font.pixelSize: 13; font.weight: Font.Medium; Layout.minimumWidth: 44; horizontalAlignment: Text.AlignHCenter }
            IconButton { iconName: "plus"; Accessible.name: "Later"; onClicked: st.up() }
        }
        RowLayout {
            id: pickRow
            anchors.fill: parent
            anchors.margins: 8
            spacing: 6
            Step { label: "Week"; value: page.due ? Qt.formatDate(page.due, "d MMM") : ""; onDown: page.shift(-7, 0, 0); onUp: page.shift(7, 0, 0) }
            Step { label: "Day"; value: page.due ? Qt.formatDate(page.due, "ddd d") : ""; onDown: page.shift(-1, 0, 0); onUp: page.shift(1, 0, 0) }
            Step { label: "Hour"; value: page.due ? Qt.formatTime(page.due, "HH") : ""; onDown: page.shift(0, -1, 0); onUp: page.shift(0, 1, 0) }
            Step { label: "Min"; value: page.due ? Qt.formatTime(page.due, "mm") : ""; onDown: page.shift(0, 0, -5); onUp: page.shift(0, 0, 5) }
        }
    }
    Text {
        Layout.fillWidth: true
        visible: field.text.trim() !== ""
        text: "→ " + (page.due ? field.text.trim() + " · " + Qt.formatDateTime(page.due, "dddd d MMMM, HH:mm") : tasks.preview(field.text))
        color: theme.textDim
        font.pixelSize: 11
        elide: Text.ElideRight
    }

    Text {
        visible: tasks.list.length === 0
        Layout.fillWidth: true
        Layout.topMargin: 14
        horizontalAlignment: Text.AlignHCenter
        text: "No tasks yet. Add one above; give it a time and Notch reminds you."
        color: theme.textDim
        font.pixelSize: 12
        wrapMode: Text.Wrap
    }

    Flickable {
        visible: tasks.list.length > 0
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(col.implicitHeight, 190)
        contentHeight: col.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ColumnLayout {
            id: col
            width: parent.width
            spacing: 4
            Repeater {
                model: tasks.list
                Rectangle {
                    id: row
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 34
                    radius: theme.radiusMd
                    color: rowHover.hovered ? theme.surfaceHover : theme.surface
                    readonly property color tint: modelData.state === "overdue" ? theme.danger : modelData.state === "soon" ? theme.accent : theme.textDim
                    HoverHandler { id: rowHover }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 8
                        spacing: 10
                        Rectangle { // the check circle
                            Layout.preferredWidth: 18; Layout.preferredHeight: 18
                            radius: 9
                            color: row.modelData.done ? theme.accent : "transparent"
                            border.width: 1.5
                            border.color: row.modelData.done ? theme.accent : row.tint
                            LineIcon { anchors.centerIn: parent; visible: row.modelData.done; glyph: "check"; sourceSize: Qt.size(11, 11); color: theme.onColor(theme.accent) }
                            TapHandler { onTapped: tasks.toggle(row.modelData.id) }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: row.modelData.title
                            color: row.modelData.done ? theme.textFaint : shell.textPrimary
                            font.pixelSize: 13
                            font.strikeout: row.modelData.done
                            elide: Text.ElideRight
                        }
                        Text {
                            visible: row.modelData.hasDue && !row.modelData.done
                            text: row.modelData.remaining
                            color: row.tint
                            font.pixelSize: 11
                            font.weight: row.modelData.state === "none" || row.modelData.state === "later" ? Font.Normal : Font.DemiBold
                        }
                        IconButton {
                            visible: rowHover.hovered
                            iconName: "trash"
                            Accessible.name: "Delete task"
                            onClicked: tasks.remove(row.modelData.id)
                        }
                    }
                }
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        visible: tasks.list.length > 0
        Text {
            Layout.fillWidth: true
            text: tasks.openCount + " open" + (tasks.overdueCount > 0 ? " · " + tasks.overdueCount + " overdue" : "")
            color: tasks.overdueCount > 0 ? theme.danger : theme.textDim
            font.pixelSize: 11
        }
        PillButton { text: "Clear done"; onClicked: tasks.clearDone() }
    }
}
