import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Tasks: a month calendar on the left (a dot under every day that has something to do, red when it is late) and the list on the
// right, nearest first; a tap on a day shows only that day. "New" opens a small window to add one: a line of words
// ("Buy milk fri 17:00") is still understood. The nearest deadline also shows in the idle capsule, and a note comes 15 minutes
// before and at the deadline.
RowLayout {
    id: page
    required property var shell
    readonly property bool modalOpen: newTask.opened
    visible: shell.page === "tasks"
    Layout.fillWidth: true
    Layout.leftMargin: 14
    Layout.rightMargin: 14
    Layout.minimumHeight: 300
    spacing: 18

    // the month on show (1-12), the day picked to filter by (null: all), and the course picked ("" = all)
    property int viewYear: new Date().getFullYear()
    property int viewMonth: new Date().getMonth() + 1
    property var selectedDay: null
    property string selectedCourse: ""
    function key(y, m, d) { return y * 10000 + m * 100 + d }
    function stepMonth(by) {
        const m = viewMonth - 1 + by
        viewYear += Math.floor(m / 12)
        viewMonth = ((m % 12) + 12) % 12 + 1
    }
    function today() {
        const n = new Date()
        viewYear = n.getFullYear()
        viewMonth = n.getMonth() + 1
    }
    function sameDay(a, b) { return a.getFullYear() === b.getFullYear() && a.getMonth() === b.getMonth() && a.getDate() === b.getDate() }
    onVisibleChanged: if (!visible) newTask.close()

    // which days have open tasks: key -> {n, late}
    readonly property var marks: {
        const m = {}
        for (const t of tasks.list) {
            if (!t.hasDue || t.done)
                continue
            const d = new Date(t.dueMs)
            const k = key(d.getFullYear(), d.getMonth() + 1, d.getDate())
            const e = m[k] || (m[k] = { n: 0, late: false })
            e.n++
            if (t.state === "overdue" || t.state === "critical")
                e.late = true
        }
        return m
    }
    // what the list shows: day filter AND course filter
    readonly property var shown: {
        let out = tasks.list
        if (selectedDay)
            out = out.filter(t => t.hasDue && sameDay(new Date(t.dueMs), selectedDay))
        if (selectedCourse !== "")
            out = out.filter(t => t.course === selectedCourse)
        return out
    }

    // ---- the calendar ----
    ColumnLayout {
        Layout.alignment: Qt.AlignTop
        Layout.preferredWidth: 238
        spacing: 6
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            Text {
                Layout.fillWidth: true
                text: Qt.locale().monthName(page.viewMonth - 1, Locale.LongFormat) + " " + page.viewYear
                color: shell.textPrimary
                font.pixelSize: 14
                font.weight: Font.DemiBold
                TapHandler { onTapped: page.today() }
            }
            IconButton { iconName: "chevron"; rotation: 90; Accessible.name: "Previous month"; onClicked: page.stepMonth(-1) }
            IconButton { iconName: "chevron"; rotation: -90; Accessible.name: "Next month"; onClicked: page.stepMonth(1) }
        }
        GridLayout {
            Layout.fillWidth: true
            columns: 7
            columnSpacing: 0
            rowSpacing: 0
            Repeater {
                model: 7
                Text {
                    Layout.fillWidth: true; Layout.preferredWidth: 1
                    text: Qt.locale().dayName((topbar.firstDay + index) % 7, Locale.ShortFormat).charAt(0).toUpperCase()
                    color: theme.textDim
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                }
            }
            Repeater {
                model: { topbar.firstDay; return topbar.monthGrid(page.viewYear, page.viewMonth) }
                delegate: Item {
                    id: cell
                    required property var modelData
                    readonly property var mark: page.marks[page.key(modelData.year, modelData.month, modelData.day)]
                    readonly property bool chosen: page.selectedDay !== null && page.selectedDay.getFullYear() === modelData.year
                                                   && page.selectedDay.getMonth() + 1 === modelData.month && page.selectedDay.getDate() === modelData.day
                    Layout.fillWidth: true; Layout.preferredWidth: 1
                    Layout.preferredHeight: 33
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 1
                        width: 28; height: 28; radius: 14
                        color: cell.modelData.today ? theme.accent : cellHover.hovered ? theme.hoverFill : "transparent"
                        border.width: cell.chosen ? 1.5 : 0
                        border.color: theme.accent
                        Text {
                            anchors.centerIn: parent
                            text: cell.modelData.day
                            color: cell.modelData.today ? theme.onColor(theme.accent) : cell.modelData.inMonth ? shell.textPrimary : theme.textFaint
                            font.pixelSize: 12
                            font.weight: cell.modelData.today || cell.chosen ? Font.DemiBold : Font.Normal
                        }
                    }
                    Rectangle { // something to do that day
                        visible: cell.mark !== undefined
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 30
                        width: 4; height: 4; radius: 2
                        color: cell.mark && cell.mark.late ? theme.danger : theme.accent
                    }
                    HoverHandler { id: cellHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        onTapped: {
                            const d = new Date(cell.modelData.year, cell.modelData.month - 1, cell.modelData.day)
                            page.selectedDay = cell.chosen ? null : d
                        }
                    }
                }
            }
        }
    }

    // ---- the list ----
    ColumnLayout {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text {
                Layout.fillWidth: true
                text: page.selectedDay ? Qt.formatDate(page.selectedDay, "ddd d MMM") : "UP NEXT"
                color: page.selectedDay ? shell.textPrimary : theme.textDim
                font.pixelSize: page.selectedDay ? 14 : 11
                font.weight: Font.DemiBold
                font.letterSpacing: page.selectedDay ? 0 : 1
            }
            PillButton { visible: page.selectedDay !== null; text: "All"; onClicked: page.selectedDay = null }
            PillButton { text: "New"; iconName: "plus"; checked: true; onClicked: newTask.openFor(page.selectedDay) }
        }

        // Course filter: All + every course already used (free-text on the tasks).
        Flow {
            visible: tasks.courseSuggestions.length > 0
            Layout.fillWidth: true
            spacing: 6
            PillButton { text: "All courses"; checked: page.selectedCourse === ""; onClicked: page.selectedCourse = "" }
            Repeater {
                model: tasks.courseSuggestions
                delegate: PillButton {
                    required property string modelData
                    text: modelData
                    checked: page.selectedCourse === modelData
                    onClicked: page.selectedCourse = page.selectedCourse === modelData ? "" : modelData
                }
            }
        }

        Text {
            visible: page.shown.length === 0
            Layout.fillWidth: true
            Layout.topMargin: 24
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            color: theme.textDim
            font.pixelSize: 12
            text: page.selectedDay ? "Nothing planned for this day." : "No tasks yet. Press New; give it a time and Notch reminds you."
        }

        Flickable {
            visible: page.shown.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(col.implicitHeight, 232)
            contentHeight: col.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; padding: 2 }
            ColumnLayout {
                id: col
                width: parent.width - 8
                spacing: 4
                Repeater {
                    model: page.shown
                    delegate: Rectangle {
                        id: row
                        required property var modelData
                        readonly property color tint: modelData.state === "overdue" ? theme.danger : modelData.state === "critical" ? theme.danger
                                                       : modelData.state === "soon" ? theme.accent
                                                       : modelData.hasDue ? theme.info : theme.textFaint
                        readonly property date due: new Date(modelData.dueMs)
                        Layout.fillWidth: true
                        implicitHeight: 46
                        radius: theme.radiusMd
                        color: rowHover.hovered ? theme.hoverFill : "transparent"
                        HoverHandler { id: rowHover }
                        Rectangle { // the colour of how soon
                            anchors { left: parent.left; top: parent.top; bottom: parent.bottom; topMargin: 4; bottomMargin: 4 }
                            width: 3
                            radius: 2
                            color: row.modelData.done ? theme.textFaint : row.tint
                        }
                        RowLayout {
                            anchors { fill: parent; leftMargin: 14; rightMargin: 6 }
                            spacing: 8
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                spacing: 1
                                RowLayout {
                                    spacing: 6
                                    Layout.fillWidth: true
                                    Rectangle { // course chip, free-text
                                        visible: row.modelData.course !== ""
                                        Layout.maximumWidth: 120
                                        implicitHeight: 18
                                        radius: 9
                                        color: Qt.tint(theme.surface, Qt.rgba(shell.accent.r, shell.accent.g, shell.accent.b, 0.22))
                                        Text {
                                            anchors.centerIn: parent
                                            anchors.margins: 1
                                            text: row.modelData.course
                                            color: shell.accent
                                            font.pixelSize: 10
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                            width: parent.width - 4
                                        }
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: row.modelData.title
                                        color: row.modelData.done ? theme.textFaint : shell.textPrimary
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                        font.strikeout: row.modelData.done
                                        elide: Text.ElideRight
                                    }
                                }
                                Text {
                                    visible: row.modelData.hasDue
                                    Layout.fillWidth: true
                                    color: theme.textDim
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                    text: (page.selectedDay || page.sameDay(row.due, new Date()) ? "" : Qt.formatDate(row.due, "ddd d MMM") + ", ")
                                          + Qt.formatTime(row.due, Qt.locale().timeFormat(Locale.ShortFormat))
                                          + (row.modelData.done ? "" : " · " + row.modelData.remaining)
                                }
                            }
                            IconButton {
                                visible: rowHover.hovered
                                iconName: "trash"
                                Accessible.name: "Delete task"
                                onClicked: tasks.remove(row.modelData.id)
                            }
                            Rectangle { // the check circle
                                Layout.preferredWidth: 20; Layout.preferredHeight: 20
                                radius: 10
                                color: row.modelData.done ? theme.accent : "transparent"
                                border.width: 1.5
                                border.color: row.modelData.done ? theme.accent : theme.textDim
                                LineIcon { anchors.centerIn: parent; visible: row.modelData.done; glyph: "check"; sourceSize: Qt.size(12, 12); color: theme.onColor(theme.accent) }
                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                                TapHandler { onTapped: tasks.toggle(row.modelData.id) }
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

    // ---- the window that adds a task ----
    Popup {
        id: newTask
        parent: page
        x: (page.width - width) / 2
        y: 0
        width: Math.min(400, page.width - 24)
        modal: true
        dim: false
        focus: true
        padding: 16
        // the day the deadline falls on (null: no deadline picked; words in the title may still give one)
        property var day: null
        function openFor(selected) {
            day = selected
            titleField.text = ""
            courseField.text = ""
            const h = new Date(Math.ceil(Date.now() / 3600000) * 3600000)
            hourTumbler.currentIndex = h.getHours()
            minuteTumbler.currentIndex = 0
            open()
        }
        function dayAt(offset) { const d = new Date(); d.setDate(d.getDate() + offset); d.setHours(0, 0, 0, 0); return d }
        readonly property var due: {
            if (!day)
                return null
            const d = new Date(day.getTime())
            d.setHours(hourTumbler.currentIndex, minuteTumbler.currentIndex, 0, 0)
            return d
        }
        function submit() {
            if (titleField.text.trim() === "")
                return
            if (tasks.add(titleField.text, newTask.due ? newTask.due.toISOString() : "", courseField.text))
                close()
        }
        onOpened: titleField.forceActiveFocus()
        Overlay.modal: Item {}
        background: Rectangle { radius: theme.radiusLg; color: Qt.rgba(theme.surface.r, theme.surface.g, theme.surface.b, 1); border.width: 1; border.color: theme.outline } // opaque: text sits on it
        contentItem: ColumnLayout {
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                SectionLabel { Layout.fillWidth: true; text: "New task" }
                IconButton { iconName: "close"; Accessible.name: "Close"; onClicked: newTask.close() }
            }
            TextField {
                id: titleField
                Layout.fillWidth: true
                implicitHeight: 34
                leftPadding: 12
                placeholderText: "What needs doing? e.g. Buy milk fri 17:00"
                placeholderTextColor: theme.placeholder
                color: shell.textPrimary
                selectionColor: shell.accent
                font.pixelSize: 13
                background: Rectangle { radius: theme.radiusMd; color: theme.well; border.width: 1; border.color: titleField.activeFocus ? shell.accent : theme.hairline }
                onAccepted: newTask.submit()
                Keys.onEscapePressed: newTask.close()
            }
            SectionLabel { text: "Deadline" }
            Flow {
                Layout.fillWidth: true
                spacing: 6
                PillButton { text: "None"; checked: newTask.day === null; onClicked: newTask.day = null }
                PillButton { text: "Today"; checked: newTask.day !== null && page.sameDay(newTask.day, newTask.dayAt(0)); onClicked: newTask.day = newTask.dayAt(0) }
                PillButton { text: "Tomorrow"; checked: newTask.day !== null && page.sameDay(newTask.day, newTask.dayAt(1)); onClicked: newTask.day = newTask.dayAt(1) }
                PillButton {
                    visible: page.selectedDay !== null && !page.sameDay(page.selectedDay, newTask.dayAt(0)) && !page.sameDay(page.selectedDay, newTask.dayAt(1))
                    text: page.selectedDay ? Qt.formatDate(page.selectedDay, "ddd d MMM") : ""
                    checked: newTask.day !== null && page.selectedDay !== null && page.sameDay(newTask.day, page.selectedDay)
                    onClicked: newTask.day = page.selectedDay
                }
            }
            RowLayout {
                visible: newTask.day !== null
                Layout.fillWidth: true
                spacing: 8
                Text { text: "Time"; color: theme.textDim; font.pixelSize: 12 }
                Tumbler {
                    id: hourTumbler
                    implicitWidth: 52; implicitHeight: 90; visibleItemCount: 3; model: 24; wrap: true
                    delegate: Text {
                        required property int index
                        text: (index < 10 ? "0" : "") + index
                        color: hourTumbler.currentIndex === index ? shell.textPrimary : theme.textDim
                        font.pixelSize: hourTumbler.currentIndex === index ? 18 : 14
                        font.weight: hourTumbler.currentIndex === index ? Font.DemiBold : Font.Normal
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        opacity: 1.0 - Math.abs(Tumbler.displacement) * 0.5
                    }
                }
                Text { text: ":"; color: shell.textPrimary; font.pixelSize: 18; font.weight: Font.DemiBold }
                Tumbler {
                    id: minuteTumbler
                    implicitWidth: 52; implicitHeight: 90; visibleItemCount: 3; model: 60; wrap: true
                    delegate: Text {
                        required property int index
                        text: (index < 10 ? "0" : "") + index
                        color: minuteTumbler.currentIndex === index ? shell.textPrimary : theme.textDim
                        font.pixelSize: minuteTumbler.currentIndex === index ? 18 : 14
                        font.weight: minuteTumbler.currentIndex === index ? Font.DemiBold : Font.Normal
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        opacity: 1.0 - Math.abs(Tumbler.displacement) * 0.5
                    }
                }
                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    wrapMode: Text.Wrap
                    color: shell.textPrimary
                    font.pixelSize: 12
                    text: newTask.due ? Qt.formatDateTime(newTask.due, "dddd d MMMM, HH:mm") : ""
                }
            }
            Text { // what the words in the title mean
                visible: newTask.day === null && titleField.text.trim() !== ""
                Layout.fillWidth: true
                text: "→ " + tasks.preview(titleField.text)
                color: theme.textDim
                font.pixelSize: 11
                elide: Text.ElideRight
            }
            SectionLabel { text: "Course (optional)" }
            Flow {
                visible: tasks.courseSuggestions.length > 0
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: tasks.courseSuggestions
                    delegate: PillButton {
                        required property string modelData
                        text: modelData
                        checked: courseField.text === modelData
                        onClicked: courseField.text = courseField.text === modelData ? "" : modelData
                    }
                }
            }
            TextField {
                id: courseField
                Layout.fillWidth: true
                implicitHeight: 30
                leftPadding: 12
                placeholderText: "e.g. CS101, Algoritma…"
                placeholderTextColor: theme.placeholder
                color: shell.textPrimary
                selectionColor: shell.accent
                font.pixelSize: 12
                background: Rectangle { radius: theme.radiusMd; color: theme.well; border.width: 1; border.color: courseField.activeFocus ? shell.accent : theme.hairline }
                onAccepted: newTask.submit()
                Keys.onEscapePressed: newTask.close()
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                PillButton { text: "Cancel"; onClicked: newTask.close() }
                PillButton { text: "Add task"; iconName: "plus"; checked: true; enabled: titleField.text.trim() !== ""; onClicked: newTask.submit() }
            }
        }
    }
}
