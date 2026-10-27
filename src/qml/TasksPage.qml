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
    // The date picker: two months, then the time. `draft` is what the person is choosing (a Date).
    property var draft: new Date()
    property int viewYear: new Date().getFullYear()
    property int viewMonth: new Date().getMonth() + 1 // 1-12
    function openPicker() {
        const d = page.due ? new Date(page.due.getTime()) : new Date(Math.ceil(Date.now() / 3600000) * 3600000)
        page.draft = d
        page.viewYear = d.getFullYear()
        page.viewMonth = d.getMonth() + 1
        hourTumbler.currentIndex = d.getHours()
        minuteTumbler.currentIndex = d.getMinutes()
        page.picking = true
    }
    function stepMonth(by) {
        const m = page.viewMonth - 1 + by
        page.viewYear += Math.floor(m / 12)
        page.viewMonth = ((m % 12) + 12) % 12 + 1
    }
    function pickDay(y, m, d) {
        const n = new Date(page.draft.getTime())
        n.setFullYear(y, m - 1, d)
        page.draft = n
    }
    function applyPicker() {
        const d = new Date(page.draft.getTime())
        d.setHours(hourTumbler.currentIndex, minuteTumbler.currentIndex, 0, 0)
        page.due = d
        page.picking = false
    }
    function setIn(ms) { page.due = new Date(Math.ceil((Date.now() + ms) / 60000) * 60000); page.picking = false }
    function setAt(h, plusDays) {
        const d = new Date()
        d.setDate(d.getDate() + plusDays)
        d.setHours(h, 0, 0, 0)
        if (d.getTime() <= Date.now() && plusDays === 0) d.setDate(d.getDate() + 1)
        page.due = d
        page.picking = false
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
                color: theme.well
                border.width: 1
                border.color: field.activeFocus ? shell.accent : theme.hairline
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
        PillButton { text: page.due ? Qt.formatDateTime(page.due, "ddd d MMM, HH:mm") : "Pick date & time"; iconName: "timer"; checked: page.picking || page.due !== null; onClicked: page.picking ? page.picking = false : page.openPicker() }
        PillButton { visible: page.due !== null; text: "No deadline"; onClicked: { page.due = null; page.picking = false } }
    }
    Rectangle { // the picker: two months to choose the day, then the time
        id: picker
        visible: page.picking
        Layout.fillWidth: true
        implicitHeight: pickBody.implicitHeight + 20
        radius: theme.radiusMd
        color: theme.surface

        component Month: ColumnLayout {
            id: month
            property int year
            property int mon // 1-12
            property bool first: false // has the "previous" arrow
            property bool last: false  // has the "next" arrow
            readonly property var cells: { topbar.firstDay; return topbar.monthGrid(year, mon) }
            readonly property bool sixRows: cells.length > 35 && cells[35].inMonth
            spacing: 4
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            RowLayout {
                Layout.fillWidth: true
                IconButton { visible: month.first; iconName: "chevron"; rotation: 90; Accessible.name: "Previous month"; onClicked: page.stepMonth(-1) }
                Item { visible: !month.first; implicitWidth: 28; implicitHeight: 28 }
                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: Qt.locale().monthName(month.mon - 1, Locale.LongFormat) + " " + month.year
                    color: shell.textPrimary
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                IconButton { visible: month.last; iconName: "chevron"; rotation: -90; Accessible.name: "Next month"; onClicked: page.stepMonth(1) }
                Item { visible: !month.last; implicitWidth: 28; implicitHeight: 28 }
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 7
                columnSpacing: 0
                rowSpacing: 1
                Repeater {
                    model: 7
                    Text {
                        Layout.fillWidth: true; Layout.preferredWidth: 1
                        text: Qt.locale().dayName((topbar.firstDay + index) % 7, Locale.ShortFormat)
                        color: theme.textDim
                        font.pixelSize: 10
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
                Repeater {
                    model: month.cells
                    delegate: Item {
                        id: cell
                        required property var modelData
                        required property int index
                        readonly property bool past: new Date(modelData.year, modelData.month - 1, modelData.day + 1).getTime() <= Date.now() // yesterday and earlier
                        readonly property bool chosen: modelData.inMonth && page.draft.getFullYear() === modelData.year
                                                       && page.draft.getMonth() + 1 === modelData.month && page.draft.getDate() === modelData.day
                        readonly property bool sunday: new Date(modelData.year, modelData.month - 1, modelData.day).getDay() === 0
                        readonly property bool shown: cell.modelData.inMonth && (cell.index < 35 || month.sixRows)
                        clip: true // a cell that is not this month's keeps its place (a hidden one would shift the grid)
                        Layout.fillWidth: true; Layout.preferredWidth: 1
                        Layout.preferredHeight: cell.index >= 35 && !month.sixRows ? 0 : 28
                        Rectangle {
                            visible: cell.shown
                            anchors.centerIn: parent
                            width: 26; height: 26; radius: 13
                            color: cell.chosen ? theme.accent : cellMouse.containsMouse && !cell.past ? theme.surfaceHover : "transparent"
                            border.width: cell.modelData.today && !cell.chosen ? 1.5 : 0
                            border.color: theme.accent
                            Text {
                                anchors.centerIn: parent
                                text: cell.modelData.day
                                color: cell.chosen ? theme.onColor(theme.accent) : cell.past ? theme.textFaint : cell.sunday ? theme.danger : shell.textPrimary
                                font.pixelSize: 12
                                font.weight: cell.chosen || cell.modelData.today ? Font.DemiBold : Font.Normal
                            }
                        }
                        MouseArea {
                            id: cellMouse
                            anchors.fill: parent
                            enabled: !cell.past && cell.shown
                            hoverEnabled: true
                            onClicked: page.pickDay(cell.modelData.year, cell.modelData.month, cell.modelData.day)
                        }
                    }
                }
            }
        }

        component Wheel: Tumbler {
            id: wheel
            property int items: 24
            implicitWidth: 52
            implicitHeight: 96
            visibleItemCount: 3
            model: items
            wrap: true
            delegate: Text {
                required property int index
                text: (index < 10 ? "0" : "") + index
                color: wheel.currentIndex === index ? shell.textPrimary : theme.textDim
                font.pixelSize: wheel.currentIndex === index ? 18 : 14
                font.weight: wheel.currentIndex === index ? Font.DemiBold : Font.Normal
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                opacity: 1.0 - Math.abs(Tumbler.displacement) * 0.5
            }
        }

        ColumnLayout {
            id: pickBody
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                spacing: 16
                Month { year: page.viewYear; mon: page.viewMonth; first: true }
                Month { year: page.viewMonth === 12 ? page.viewYear + 1 : page.viewYear; mon: page.viewMonth === 12 ? 1 : page.viewMonth + 1; last: true }
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: theme.outline }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text { text: "Time"; color: theme.textDim; font.pixelSize: 12 }
                Wheel { id: hourTumbler; items: 24 }
                Text { text: ":"; color: shell.textPrimary; font.pixelSize: 18; font.weight: Font.DemiBold }
                Wheel { id: minuteTumbler; items: 60 }
                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    text: Qt.formatDate(page.draft, "dddd d MMMM yyyy") + ", " + (hourTumbler.currentIndex < 10 ? "0" : "") + hourTumbler.currentIndex
                          + ":" + (minuteTumbler.currentIndex < 10 ? "0" : "") + minuteTumbler.currentIndex
                    color: shell.textPrimary
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                }
                PillButton { text: "Cancel"; onClicked: page.picking = false }
                PillButton { text: "Apply"; checked: true; onClicked: page.applyPicker() }
            }
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
        visible: tasks.list.length === 0 && !page.picking
        Layout.fillWidth: true
        Layout.topMargin: 14
        horizontalAlignment: Text.AlignHCenter
        text: "No tasks yet. Add one above; give it a time and Notch reminds you."
        color: theme.textDim
        font.pixelSize: 12
        wrapMode: Text.Wrap
    }

    Flickable {
        visible: tasks.list.length > 0 && !page.picking
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
        visible: tasks.list.length > 0 && !page.picking
        Text {
            Layout.fillWidth: true
            text: tasks.openCount + " open" + (tasks.overdueCount > 0 ? " · " + tasks.overdueCount + " overdue" : "")
            color: tasks.overdueCount > 0 ? theme.danger : theme.textDim
            font.pixelSize: 11
        }
        PillButton { text: "Clear done"; onClicked: tasks.clearDone() }
    }
}
