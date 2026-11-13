import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Making a form, in three parts: the questions, how it looks and ends, and when it is open and how much it takes.
ColumnLayout {
    id: ed
    required property var tab
    property string part: tab.editPart   // questions | look | settings
    property bool adding: false         // the palette of question kinds is open
    Layout.fillWidth: true
    spacing: 12

    RowLayout {
        Layout.fillWidth: true
        PillButton { text: "Back"; iconName: "back"; onClicked: ed.tab.view = "list" }
        Item { Layout.fillWidth: true }
        PillButton { text: "Save"; onClicked: ed.tab.save("list") }
        PillButton { text: "Share"; iconName: "share"; primary: true; onClicked: ed.tab.share() }
    }
    TextField {
        id: titleField
        Layout.fillWidth: true
        implicitHeight: 40
        leftPadding: 0
        text: ed.tab.draft.title || ""
        placeholderText: "Name your form"
        placeholderTextColor: theme.placeholder
        color: theme.text
        font.pixelSize: 21
        font.weight: Font.DemiBold
        maximumLength: 120
        selectByMouse: true
        background: Rectangle { color: "transparent"; Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: 1; color: titleField.activeFocus ? theme.accent : theme.hairline } }
        onTextEdited: ed.tab.draft.title = text
    }
    FormField {
        Layout.fillWidth: true
        text: ed.tab.draft.description || ""
        placeholderText: "What is it for? People see this above the questions (optional)"
        onTextEdited: ed.tab.draft.description = text
    }
    Rectangle { // a problem, said plainly
        visible: ed.tab.problem !== ""
        Layout.fillWidth: true
        implicitHeight: warnText.implicitHeight + 20
        radius: theme.radiusSm
        color: theme.dangerBg
        Text { id: warnText; anchors { fill: parent; margins: 10 } text: ed.tab.problem; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: theme.danger; font.pixelSize: 12 }
    }
    AudioTabs {
        Layout.fillWidth: true
        model: [["questions", "Questions"], ["look", "Look & ending"], ["settings", "Settings"]]
        value: ed.part
        onPicked: v => ed.part = v
    }

    // ---------------- questions ----------------
    ColumnLayout {
        visible: ed.part === "questions"
        Layout.fillWidth: true
        spacing: 10
        Repeater {
            model: ed.tab.draft.fields
            delegate: FormCard {
                id: q
                required property var modelData
                required property int index
                readonly property bool isSection: modelData.type === "section"
                property bool showMore: ed.tab.showOn(index) || (modelData.endOn || []).length > 0 // rules are shown when there are some
                color: isSection ? theme.accentFaint : theme.well
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    FormKind { glyph: ed.tab.kindGlyph(q.modelData.type) }
                    Text { Layout.fillWidth: true; text: ed.tab.kindName(q.modelData.type); color: theme.textDim; font.pixelSize: 12; font.weight: Font.Medium }
                    Text { visible: ed.tab.showOn(q.index) || (q.modelData.endOn || []).length > 0; text: "Has a rule"; color: theme.accentText; font.pixelSize: 11; font.weight: Font.DemiBold }
                    Text { visible: !q.isSection; text: "Required"; color: theme.textDim; font.pixelSize: 12 }
                    Toggle { visible: !q.isSection; checked: !!q.modelData.required; Accessible.name: "Required"; onClicked: { ed.tab.field(q.index, "required", !q.modelData.required); ed.tab.restructure(ed.tab.clone(ed.tab.draft.fields)) } }
                    PillButton { iconOnly: true; iconName: "gear"; text: "More"; checked: q.showMore; onClicked: q.showMore = !q.showMore }
                }
                ColumnLayout { // when it is asked
                    visible: q.showMore && ed.tab.earlier(q.index).length > 0
                    Layout.fillWidth: true
                    spacing: 6
                    SettingToggle {
                        title: q.isSection ? "Show this section only when…" : "Ask this only when…"
                        caption: "Depends on an earlier question with choices."
                        checked: ed.tab.showOn(q.index)
                        onToggled: { if (ed.tab.showOn(q.index)) ed.tab.setShow(q.index, "", []); else { const e = ed.tab.earlier(q.index)[0]; ed.tab.setShow(q.index, e.id, [e.options[0]]) } }
                    }
                    AgentChoice {
                        visible: ed.tab.showOn(q.index)
                        Layout.fillWidth: true
                        model: ed.tab.earlier(q.index).map(e => e.label || "(no label)")
                        currentIndex: Math.max(0, ed.tab.earlier(q.index).findIndex(e => e.id === (q.modelData.showIf || {}).field))
                        onActivated: i => { const e = ed.tab.earlier(q.index)[i]; ed.tab.setShow(q.index, e.id, [e.options[0]]) }
                    }
                    Flow {
                        visible: ed.tab.showOn(q.index)
                        Layout.fillWidth: true
                        spacing: 6
                        Repeater {
                            model: { const r = q.modelData.showIf || {}; const e = ed.tab.earlier(q.index).find(x => x.id === r.field); return e ? e.options : [] }
                            delegate: PillButton { required property string modelData; text: modelData; checked: ((q.modelData.showIf || {}).values || []).indexOf(modelData) >= 0; onClicked: ed.tab.toggleShowValue(q.index, modelData) }
                        }
                    }
                }
                ColumnLayout { // a choice that ends the form
                    visible: q.showMore && q.modelData.type === "choice"
                    Layout.fillWidth: true
                    spacing: 6
                    Text { text: "End the form when the answer is…"; color: theme.text; font.pixelSize: 13; font.weight: Font.Medium }
                    Caption { Layout.fillWidth: true; text: "Nothing after this question is asked; they go straight to Send." }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        Repeater {
                            model: q.modelData.options || []
                            delegate: PillButton { required property string modelData; text: modelData; checked: (q.modelData.endOn || []).indexOf(modelData) >= 0; onClicked: ed.tab.toggleEnd(q.index, modelData) }
                        }
                    }
                }
                RowLayout {
                    visible: q.showMore
                    Layout.fillWidth: true
                    spacing: 8
                    PillButton { text: "Move up"; enabled: q.index > 0; onClicked: ed.tab.moveField(q.index, -1) }
                    PillButton { text: "Move down"; enabled: q.index < ed.tab.draft.fields.length - 1; onClicked: ed.tab.moveField(q.index, 1) }
                    Item { Layout.fillWidth: true }
                    PillButton { text: "Remove"; iconName: "trash"; onClicked: ed.tab.removeField(q.index) }
                }
                FormField {
                    Layout.fillWidth: true
                    text: q.modelData.label || ""
                    placeholderText: q.isSection ? "Section title" : "Your question"
                    font.pixelSize: q.isSection ? 15 : 14
                    font.weight: Font.DemiBold
                    maximumLength: 200
                    onTextEdited: ed.tab.field(q.index, "label", text)
                }
                FormField {
                    Layout.fillWidth: true
                    implicitHeight: 30
                    text: q.modelData.help || ""
                    placeholderText: q.isSection ? "A line about this section (optional)" : "A hint under it (optional)"
                    font.pixelSize: 12
                    maximumLength: 500
                    onTextEdited: ed.tab.field(q.index, "help", text)
                }
                ColumnLayout { // the choices, one box each
                    visible: q.modelData.type === "choice" || q.modelData.type === "multi"
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: q.modelData.options || []
                        delegate: RowLayout {
                            id: opt
                            required property string modelData
                            required property int index
                            Layout.fillWidth: true
                            spacing: 8
                            Rectangle { implicitWidth: 14; implicitHeight: 14; radius: q.modelData.type === "multi" ? 4 : 7; color: "transparent"; border.width: 1.5; border.color: theme.textFaint }
                            FormField { Layout.fillWidth: true; implicitHeight: 30; text: opt.modelData; placeholderText: "Choice " + (opt.index + 1); font.pixelSize: 13; maximumLength: 100
                                onTextEdited: ed.tab.renameOption(q.index, opt.index, text) }
                            FormField { implicitWidth: 74; implicitHeight: 30; text: ed.tab.optionLimit(q.index, opt.modelData); placeholderText: "Max"; font.pixelSize: 12; maximumLength: 4; inputMethodHints: Qt.ImhDigitsOnly
                                Accessible.name: "Most answers this choice takes (empty: no limit)"; onTextEdited: ed.tab.setOptionLimit(q.index, ed.tab.draft.fields[q.index].options[opt.index], text) }
                            PillButton { iconOnly: true; iconName: "close"; text: "Remove this choice"; enabled: (q.modelData.options || []).length > 2; onClicked: ed.tab.removeOption(q.index, opt.index) }
                        }
                    }
                    Caption { Layout.fillWidth: true; text: "Max: the most answers a choice takes; when it is full it cannot be picked. Counted on this WiFi only (through your server the answers are sealed and cannot be counted)." }
                    PillButton { text: "Add a choice"; iconName: "plus"; enabled: (q.modelData.options || []).length < 30; onClicked: ed.tab.addOption(q.index) }
                }
                ColumnLayout { // stars and scales: how high, and what the ends mean
                    visible: q.modelData.type === "rating" || q.modelData.type === "scale"
                    Layout.fillWidth: true
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "Up to"; color: theme.textDim; font.pixelSize: 12 }
                        Segmented { Layout.fillWidth: true; model: [[3, "3"], [5, "5"], [7, "7"], [10, "10"]]; value: q.modelData.scaleMax || 5; onPicked: v => { ed.tab.field(q.index, "scaleMax", v); ed.tab.restructure(ed.tab.clone(ed.tab.draft.fields)) } }
                    }
                    RowLayout {
                        visible: q.modelData.type === "scale"
                        Layout.fillWidth: true
                        spacing: 8
                        FormField { Layout.fillWidth: true; Layout.preferredWidth: 1; implicitHeight: 30; text: q.modelData.lowLabel || ""; placeholderText: "Low end, e.g. Not at all"; font.pixelSize: 12; maximumLength: 60; onTextEdited: ed.tab.field(q.index, "lowLabel", text) }
                        FormField { Layout.fillWidth: true; Layout.preferredWidth: 1; implicitHeight: 30; text: q.modelData.highLabel || ""; placeholderText: "High end, e.g. Very much"; font.pixelSize: 12; maximumLength: 60; onTextEdited: ed.tab.field(q.index, "highLabel", text) }
                    }
                }
                RowLayout {
                    visible: q.modelData.type === "file"
                    Layout.fillWidth: true
                    Text { text: "Up to"; color: theme.textDim; font.pixelSize: 12 }
                    Segmented { Layout.fillWidth: true; model: [[1, "1 file"], [3, "3"], [10, "10"], [20, "20"]]; value: q.modelData.maxFiles || 1; onPicked: v => { ed.tab.field(q.index, "maxFiles", v); ed.tab.restructure(ed.tab.clone(ed.tab.draft.fields)) } }
                }
                SettingToggle {
                    visible: q.modelData.type === "short"
                    title: "This is their name"
                    caption: "Used when telling you who answered."
                    checked: !!q.modelData.isName
                    onToggled: { const f = ed.tab.clone(ed.tab.draft.fields); for (let k = 0; k < f.length; ++k) f[k].isName = k === q.index ? !q.modelData.isName : false; ed.tab.restructure(f) }
                }
            }
        }
        Caption { visible: ed.tab.draft.fields.length === 0; Layout.fillWidth: true; text: "No questions yet. Add the first one." }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            PillButton { text: ed.adding ? "Close" : "Add a question"; iconName: ed.adding ? "close" : "plus"; primary: !ed.adding; onClicked: ed.adding = !ed.adding }
            PillButton { text: "Add a section"; onClicked: { ed.adding = false; ed.tab.addField("section") } }
        }
        GridLayout {
            visible: ed.adding
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 8
            rowSpacing: 8
            Repeater {
                model: ed.tab.kinds.filter(k => k[0] !== "section")
                delegate: Rectangle {
                    id: kind
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    implicitHeight: 52
                    radius: theme.radiusMd
                    color: hov.hovered ? theme.surfaceHover : theme.well
                    border.width: 1
                    border.color: hov.hovered ? Qt.alpha(theme.accent, 0.5) : theme.hairline
                    HoverHandler { id: hov; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: { ed.adding = false; ed.tab.addField(kind.modelData[0]) } }
                    Accessible.role: Accessible.Button
                    Accessible.name: "Add " + modelData[1]
                    RowLayout {
                        anchors { fill: parent; margins: 10 }
                        spacing: 10
                        FormKind { glyph: kind.modelData[2] }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Text { Layout.fillWidth: true; text: kind.modelData[1]; color: theme.text; font.pixelSize: 13; font.weight: Font.Medium; elide: Text.ElideRight }
                            Caption { Layout.fillWidth: true; text: kind.modelData[3]; elide: Text.ElideRight; maximumLineCount: 1 }
                        }
                    }
                }
            }
        }
    }

    // ---------------- look & ending ----------------
    ColumnLayout {
        visible: ed.part === "look"
        Layout.fillWidth: true
        spacing: 12
        Text { text: "How people go through it"; color: theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Repeater {
                model: [["single", "All at once", "Every question on one page."], ["sections", "By section", "A new page at each section."], ["slides", "One at a time", "One question per page."]]
                delegate: Rectangle {
                    id: lay
                    required property var modelData
                    readonly property bool on: (ed.tab.draft.layout || "single") === modelData[0]
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    implicitHeight: layCol.implicitHeight + 24
                    radius: theme.radiusMd
                    color: on ? theme.accentFaint : theme.well
                    border.width: on ? 2 : 1
                    border.color: on ? theme.accent : theme.hairline
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: ed.tab.setLayout(lay.modelData[0]) }
                    Accessible.role: Accessible.RadioButton
                    Accessible.name: modelData[1]
                    Accessible.checked: on
                    ColumnLayout {
                        id: layCol
                        anchors { fill: parent; margins: 12 }
                        spacing: 6
                        Item { // a small picture of it
                            Layout.alignment: Qt.AlignHCenter
                            implicitWidth: 64
                            implicitHeight: 42
                            Column { visible: lay.modelData[0] === "single"; anchors.centerIn: parent; spacing: 4
                                Repeater { model: 4; Rectangle { width: index % 2 ? 40 : 56; height: 5; radius: 2.5; color: theme.textFaint } } }
                            Row { visible: lay.modelData[0] === "sections"; anchors.centerIn: parent; spacing: 5
                                Repeater { model: 2; Rectangle { width: 28; height: 38; radius: 5; color: "transparent"; border.width: 1.5; border.color: theme.textFaint
                                    Column { anchors { fill: parent; margins: 5 } spacing: 4; Rectangle { width: 14; height: 4; radius: 2; color: theme.accent } Rectangle { width: parent.width; height: 4; radius: 2; color: theme.textFaint } Rectangle { width: parent.width - 4; height: 4; radius: 2; color: theme.textFaint } } } } }
                            Rectangle { visible: lay.modelData[0] === "slides"; anchors.centerIn: parent; width: 52; height: 40; radius: 6; color: "transparent"; border.width: 1.5; border.color: theme.textFaint
                                Rectangle { x: 5; y: 5; width: 20; height: 3; radius: 1.5; color: theme.accent }
                                Rectangle { x: 5; y: 14; width: 42; height: 7; radius: 3; color: theme.textFaint }
                                Rectangle { x: parent.width - 21; y: parent.height - 12; width: 16; height: 7; radius: 3.5; color: theme.accent } }
                        }
                        Text { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; text: lay.modelData[1]; color: theme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
                        Caption { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; text: lay.modelData[2] }
                    }
                }
            }
        }
        Caption { visible: (ed.tab.draft.layout || "single") === "sections"; Layout.fillWidth: true; text: "Add a section on the Questions tab wherever a new page should start." }

        Text { Layout.topMargin: 6; text: "After they send it"; color: theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
        FormCard {
            FormField { id: endTitle; Layout.fillWidth: true; text: (ed.tab.draft.ending || {}).title || ""; placeholderText: "Thank you"; maximumLength: 120; onTextEdited: ed.tab.setEnding("title", text) }
            TextArea {
                id: endMessage
                Layout.fillWidth: true
                Layout.preferredHeight: 70
                text: (ed.tab.draft.ending || {}).message || ""
                placeholderText: "Your answer was sent. (Say more here, e.g. what happens next.)"
                placeholderTextColor: theme.placeholder
                wrapMode: TextEdit.Wrap
                color: theme.text
                font.pixelSize: 13
                selectByMouse: true
                background: Rectangle { radius: theme.radiusSm; color: theme.surface; border.width: 1; border.color: endMessage.activeFocus ? theme.accent : theme.hairline }
                onTextChanged: if (activeFocus) ed.tab.setEnding("message", text.substring(0, 1000))
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                FormField { id: endLabel; Layout.fillWidth: true; Layout.preferredWidth: 1; text: (ed.tab.draft.ending || {}).buttonLabel || ""; placeholderText: "Button text, e.g. Join the group"; maximumLength: 40; onTextEdited: ed.tab.setEnding("buttonLabel", text) }
                FormField { id: endUrl; Layout.fillWidth: true; Layout.preferredWidth: 1; text: (ed.tab.draft.ending || {}).buttonUrl || ""; placeholderText: "Link: https://…"; maximumLength: 500; onTextEdited: ed.tab.setEnding("buttonUrl", text) }
            }
            Caption { Layout.fillWidth: true; text: "The button is optional. It needs both a text and a link that starts with https:// or http://." }
        }
        Caption { text: "Preview" }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: prev.implicitHeight + 40
            radius: theme.radiusMd
            color: theme.surface
            border.width: 1
            border.color: theme.hairline
            ColumnLayout {
                id: prev
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 24 }
                spacing: 8
                Rectangle { Layout.alignment: Qt.AlignHCenter; implicitWidth: 36; implicitHeight: 36; radius: 18; color: Qt.alpha(theme.success, 0.2)
                    LineIcon { anchors.centerIn: parent; glyph: "check"; color: theme.success; sourceSize: Qt.size(18, 18) } }
                Text { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; text: endTitle.text || "Thank you"; textFormat: Text.PlainText; color: theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                Text { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; text: endMessage.text || "Your answer was sent."; textFormat: Text.PlainText; color: theme.textDim; font.pixelSize: 12 }
                Rectangle {
                    visible: endLabel.text.trim() !== "" && /^https?:\/\/\S+$/.test(endUrl.text.trim())
                    Layout.alignment: Qt.AlignHCenter
                    implicitWidth: btnText.implicitWidth + 36
                    implicitHeight: 32
                    radius: theme.radiusSm
                    color: theme.accent
                    Text { id: btnText; anchors.centerIn: parent; text: endLabel.text; textFormat: Text.PlainText; color: theme.onColor(theme.accent); font.pixelSize: 13; font.weight: Font.DemiBold }
                }
            }
        }
    }

    // ---------------- settings ----------------
    ColumnLayout {
        visible: ed.part === "settings"
        Layout.fillWidth: true
        spacing: 12
        FormCard {
            Text { text: "How long is it open?"; color: theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
            Segmented {
                Layout.fillWidth: true
                model: [[3600, "1 hour"], [86400, "1 day"], [604800, "7 days"], [-1, "Pick dates"]]
                value: (ed.tab.draft.limits.closesAt || 0) > 0 || ed.tab.dates ? -1 : (ed.tab.draft.limits.ttlSeconds || 0)
                onPicked: v => { if (v === -1) ed.tab.dates = true; else { ed.tab.dates = false; ed.tab.setSchedule(0, 0); ed.tab.setLimit("ttlSeconds", v) } }
            }
            DateRangePicker {
                visible: ed.tab.dates || (ed.tab.draft.limits.closesAt || 0) > 0
                Layout.fillWidth: true
                opensAt: ed.tab.draft.limits.opensAt || 0
                closesAt: ed.tab.draft.limits.closesAt || 0
                onPicked: (a, b) => ed.tab.setSchedule(a, b)
            }
            Caption { Layout.fillWidth: true; text: "It starts when you press Share, or on the first day you pick. Nobody can answer after it closes." }
        }
        FormCard {
            Text { text: "Stop when…"; color: theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
            LimitRow { title: "Answers"; value: (ed.tab.draft.limits.maxResponses || 0) >= 1000 ? 0 : (ed.tab.draft.limits.maxResponses || 0); onEdited: v => ed.tab.setLimit("maxResponses", v === 0 ? 1000 : v) }
            LimitRow { title: "People"; value: ed.tab.draft.limits.maxPeople || 0; onEdited: v => ed.tab.setLimit("maxPeople", v) }
            Caption { Layout.fillWidth: true; text: "Move the slider or type a number; 0 means no limit. One person is one phone or computer, so the same person can answer twice only from two devices." }
        }
        FormCard {
            Text { text: "How many answers from one person?"; color: theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
            Segmented { Layout.fillWidth: true; model: [[false, "As many as they like"], [true, "One each"]]; value: !!ed.tab.draft.oneAnswer; onPicked: v => ed.tab.setOneAnswer(v) }
            Caption { Layout.fillWidth: true; text: "Counted by device: people on one network address count as one person. Their browser remembers they answered." }
            SettingToggle {
                title: "People can change their answer"
                caption: "After sending, they get a personal link to change what they sent. Only they can open it."
                checked: !!ed.tab.draft.allowEdit
                onToggled: ed.tab.setAllowEdit(!ed.tab.draft.allowEdit)
            }
        }
        FormCard {
            visible: ed.tab.draft.fields.some(f => f.type === "file" || f.type === "signature")
            SettingToggle {
                title: "Keep the files in Files"
                caption: "They go to a category named like this form, each named after who sent it."
                checked: !!ed.tab.draft.filesToShelf
                onToggled: ed.tab.setFilesToShelf(!ed.tab.draft.filesToShelf)
            }
        }
        FormCard {
            Text { text: "Biggest file"; color: theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
            Segmented { Layout.fillWidth: true; model: [[10, "10 MB"], [25, "25 MB"], [100, "100 MB"], [500, "500 MB"]]; value: ed.tab.draft.limits.maxFileMb || 0; onPicked: v => ed.tab.setLimit("maxFileMb", v) }
            Caption { Layout.fillWidth: true; text: "Only matters if you ask for files." }
        }
    }
}
