import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// What people sent: one card for each, and the table or the files to take away.
ColumnLayout {
    id: an
    required property var tab
    readonly property var rows: tab.table.rows
    property var preview: null   // the picture shown large
    property string part: tab.answersPart   // answers | summary
    readonly property var sum: { forms.list; return tab.view === "answers" ? forms.summary(tab.currentId) : ({}) }
    Layout.fillWidth: true
    spacing: 12

    RowLayout {
        Layout.fillWidth: true
        PillButton { text: "Back"; iconName: "back"; onClicked: an.tab.view = "list" }
        Item { Layout.fillWidth: true }
        PillButton { text: "Save as table (.csv)"; iconName: "download"; enabled: an.rows.length > 0; onClicked: forms.exportCsv(an.tab.currentId) }
        PillButton { text: "Report (.html)"; iconName: "download"; enabled: an.rows.length > 0; onClicked: forms.exportReport(an.tab.currentId) }
        PillButton { text: "Save all files"; iconName: "download"; enabled: an.rows.some(r => r.files.length > 0); onClicked: forms.saveFiles(an.tab.currentId, 0) }
    }
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 2
        Text { Layout.fillWidth: true; text: an.tab.table.title || ""; textFormat: Text.PlainText; elide: Text.ElideRight; color: theme.text; font.pixelSize: 20; font.weight: Font.DemiBold }
        Caption { text: an.rows.length + (an.rows.length === 1 ? " answer" : " answers") }
    }
    AudioTabs {
        Layout.fillWidth: true
        model: [["answers", "Answers"], ["summary", "Summary"]]
        value: an.part
        onPicked: v => an.part = v
    }
    FormCard {
        visible: an.part === "answers" && an.rows.length === 0
        Text { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; text: "Nothing yet"; color: theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
        Caption { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; text: "Answers show up here as soon as people send them." }
    }
    Repeater {
        model: an.part === "answers" ? an.rows : []
        delegate: FormCard {
            id: ans
            required property var modelData
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Rectangle { implicitWidth: 26; implicitHeight: 22; radius: 11; color: theme.accentSoft
                    Text { anchors.centerIn: parent; text: ans.modelData.n; color: theme.accentText; font.pixelSize: 11; font.weight: Font.DemiBold } }
                Text { Layout.fillWidth: true; text: ans.modelData.time + "  ·  " + ans.modelData.device + (ans.modelData.edited !== "" ? "  ·  changed " + ans.modelData.edited : ""); textFormat: Text.PlainText; elide: Text.ElideRight; color: theme.textDim; font.pixelSize: 11 }
                PillButton { visible: ans.modelData.files.length > 0; text: "Add to Files"; iconName: "download"; onClicked: forms.saveFiles(an.tab.currentId, ans.modelData.n) }
                PillButton { iconOnly: true; iconName: "trash"; text: "Delete this answer"; onClicked: { forms.removeResponse(an.tab.currentId, ans.modelData.n); an.tab.table = forms.responses(an.tab.currentId) } }
            }
            Flow { // what came with it: the pictures to see, the other files by name
                visible: ans.modelData.files.length > 0
                Layout.fillWidth: true
                spacing: 8
                Repeater {
                    model: ans.modelData.files
                    delegate: Item {
                        id: fileTile
                        required property var modelData
                        width: modelData.image ? 84 : Math.min(220, chip.implicitWidth + 20)
                        height: modelData.image ? 84 : 30
                        Accessible.role: Accessible.Button
                        Accessible.name: (modelData.image ? "Picture " : "File ") + modelData.name
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: if (fileTile.modelData.image) an.preview = fileTile.modelData; else notch.open(fileTile.modelData.path) }
                        Rectangle { anchors.fill: parent; radius: theme.radiusSm; color: theme.surface; border.width: 1; border.color: theme.hairline; clip: true
                            Image { visible: fileTile.modelData.image; anchors { fill: parent; margins: 1 } source: fileTile.modelData.image ? fileTile.modelData.url : ""; sourceSize: Qt.size(168, 168); fillMode: Image.PreserveAspectCrop; asynchronous: true; cache: false }
                            Text { id: chip; visible: !fileTile.modelData.image; anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 10 } text: fileTile.modelData.name; textFormat: Text.PlainText; elide: Text.ElideMiddle; color: theme.text; font.pixelSize: 12 }
                        }
                    }
                }
            }
            Repeater {
                model: an.tab.table.columns
                delegate: ColumnLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 1
                    visible: (ans.modelData.cells[modelData.id] || "") !== ""
                    Text { text: modelData.label; textFormat: Text.PlainText; color: theme.textDim; font.pixelSize: 11 }
                    Text { Layout.fillWidth: true; text: ans.modelData.cells[modelData.id] || ""; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: theme.text; font.pixelSize: 13 }
                }
            }
        }
    }

    // ---------------- summary ----------------
    FormCard {
        visible: an.part === "summary" && an.rows.length === 0
        Text { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; text: "Nothing to add up yet"; color: theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
        Caption { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; text: "The summary appears with the first answer." }
    }
    FormCard {
        visible: an.part === "summary" && (an.sum.days || []).length > 0
        Text { text: "Answers each day"; color: theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
        Row {
            id: dayBars
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            spacing: 3
            readonly property var days: an.sum.days || []
            readonly property int peak: Math.max(1, ...days.map(d => d.count))
            Repeater {
                model: dayBars.days
                delegate: Item {
                    required property var modelData
                    width: Math.min(28, Math.max(4, (dayBars.width - (dayBars.days.length - 1) * dayBars.spacing) / Math.max(1, dayBars.days.length)))
                    height: dayBars.height
                    Accessible.name: modelData.day + ": " + modelData.count
                    Rectangle { anchors { bottom: parent.bottom; left: parent.left; right: parent.right } height: Math.max(modelData.count > 0 ? 3 : 1, parent.height * modelData.count / dayBars.peak); radius: 2; color: modelData.count > 0 ? theme.accent : theme.hairline }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Caption { text: (an.sum.days || []).length > 0 ? an.sum.days[0].day : "" }
            Item { Layout.fillWidth: true }
            Caption { text: (an.sum.days || []).length > 1 ? an.sum.days[an.sum.days.length - 1].day : "" }
        }
    }
    Repeater {
        model: an.part === "summary" && an.rows.length > 0 ? (an.sum.questions || []) : []
        delegate: FormCard {
            id: sq
            required property var modelData
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                Text { Layout.fillWidth: true; text: sq.modelData.label; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: theme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
                Caption { text: sq.modelData.answered + (sq.modelData.answered === 1 ? " answer" : " answers") }
            }
            Repeater {
                model: sq.modelData.bars || []
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 10
                    Accessible.name: modelData.label + ": " + modelData.count + " (" + modelData.pct + "%)"
                    Text { Layout.preferredWidth: 110; text: modelData.label; textFormat: Text.PlainText; elide: Text.ElideRight; color: theme.text; font.pixelSize: 12 }
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 12
                        radius: 6
                        color: theme.surface
                        Rectangle { width: Math.max(modelData.count > 0 ? 6 : 0, parent.width * modelData.pct / 100); height: parent.height; radius: 6; color: theme.accent }
                    }
                    Text { Layout.preferredWidth: 78; horizontalAlignment: Text.AlignRight; text: modelData.count + "  ·  " + Math.round(modelData.pct) + "%"; color: theme.textDim; font.pixelSize: 12 }
                }
            }
            RowLayout {
                visible: !!sq.modelData.stats
                Layout.fillWidth: true
                spacing: 24
                Repeater {
                    model: sq.modelData.stats ? [["Average", sq.modelData.stats.avg], ["Total", sq.modelData.stats.sum], ["Smallest", sq.modelData.stats.min], ["Largest", sq.modelData.stats.max]].filter(x => x[1] !== undefined) : []
                    delegate: ColumnLayout {
                        required property var modelData
                        spacing: 0
                        Text { text: Math.round(modelData[1] * 100) / 100; color: theme.text; font.pixelSize: 18; font.weight: Font.DemiBold }
                        Caption { text: modelData[0] }
                    }
                }
            }
        }
    }

    Rectangle { // a picture, large: tap anywhere to put it away
        visible: an.preview !== null
        Layout.fillWidth: true
        implicitHeight: 360
        radius: theme.radiusMd
        color: theme.well
        border.width: 1
        border.color: theme.hairline
        Image { anchors { fill: parent; margins: 12 } source: an.preview ? an.preview.url : ""; fillMode: Image.PreserveAspectFit; asynchronous: true; cache: false; sourceSize: Qt.size(1200, 1200) }
        PillButton { anchors { top: parent.top; right: parent.right; margins: 10 } iconOnly: true; iconName: "close"; text: "Close the picture"; onClicked: an.preview = null }
        Text { anchors { left: parent.left; bottom: parent.bottom; margins: 12 } text: an.preview ? an.preview.name : ""; textFormat: Text.PlainText; color: theme.textDim; font.pixelSize: 11 }
    }
}
