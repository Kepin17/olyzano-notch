import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

// Convert and PDF tools: choose a file of the shelf or any file (the button, or drop it), turn it into another format (PDF ↔ Word, pictures, text,
// sound, video…), or put a watermark such as CONFIDENTIAL DOCUMENT on the pages of a PDF. The result goes straight onto the shelf; the original is never changed.
ColumnLayout {
    id: tools
    required property var shell
    property string pickedId: ""                 // a file of the shelf …
    property string pickedUrl: ""                // … or a file from anywhere (file:// URL)
    property string pickedName: ""
    property var items: []                       // [{id, name, sizeText, addedText, kind: "pdf" | "word" | "other"}]
    property bool dropping: false
    property string action: "convert"             // convert | mark
    property string wmText: "CONFIDENTIAL DOCUMENT"
    property string layoutKind: "diagonal"
    property string color: "gray"
    property string opacityPct: "25"
    property string sizeKind: "auto"
    property string pages: ""
    readonly property var presets: ["CONFIDENTIAL DOCUMENT", "CONFIDENTIAL", "DRAFT", "COPY", "INTERNAL USE ONLY", "DO NOT COPY"]
    readonly property var picked: { if (pickedUrl !== "") return { name: pickedName, kind: kindOfName(pickedName) }; for (const i of items) if (i.id === pickedId) return i; return null }
    readonly property bool isPdf: !!picked && picked.kind === "pdf"
    readonly property bool isImage: !!picked && picked.kind === "image"
    readonly property bool canMark: !!picked && (picked.kind === "pdf" || picked.kind === "word")
    property var targets: []
    property string bgFeather: "1"                 // the softness of the cut edge, in pixels
    property string bgResult: ""                   // the cut-out picture (file URL)
    property string cutoutFor: ""                  // who asked matting for a cut-out: bg | stamp
    property bool signatureAi: false               // the stamp picture has its background taken off by the model
    property string signatureCut: ""
    property string pdfState: "ok"                 // ok | protected | damaged, of the chosen PDF
    readonly property var actions: isImage ? [["convert", "Convert"], ["bg", "Remove background"]] : !canMark ? [["convert", "Convert"]] : isPdf ? [["convert", "Convert"], ["mark", "Watermark"], ["stamp", "Stamp"], ["protect", "Protect"]] : [["convert", "Convert"], ["mark", "Watermark"]]
    // stamp and signature
    property string stampKind: "text"              // text | picture
    property string stampText: "APPROVED"
    property string stampColor: "green"
    property real stampX: 0.75
    property real stampY: 0.85
    property real stampW: 0.30
    property real stampRot: 8                      // degrees, counter-clockwise
    property real stampOpacity: 1.0
    property string stampPages: "last"             // last | first | all | range
    property string stampRange: ""
    property string signatureUrl: ""               // the picture chosen
    property string signatureThumb: ""             // that picture with its white left out
    property string pageUrl: ""                    // the page shown under the stamp
    property real pageAspect: 1.414
    readonly property var stampPresets: ["APPROVED", "PAID", "RECEIVED", "REJECTED", "DRAFT", "VOID"]
    readonly property string stampProblem: stampKind === "picture" ? (signatureThumb === "" ? "Choose a picture: a signature, a logo or a scanned stamp." : "")
                                           : stampText.trim() === "" ? "Write the text of the stamp." : stampText.trim().length > 24 ? "The text is longer than 24 characters." : textLatin(stampText)
    // password
    property string protectMode: "lock"            // lock | unlock
    property string lockPw: ""
    property string lockPw2: ""
    property string unlockPw: ""
    property bool allowPrint: true
    property bool allowCopy: true
    property bool allowChange: true
    readonly property string lockProblem: lockPw.length < 4 ? "At least 4 characters." : lockPw.length > 32 ? "At most 32 characters."
                                           : /[^\x21-\x7e]/.test(lockPw) ? "Use only letters, digits and symbols, without spaces or accents." : lockPw !== lockPw2 ? "The two passwords are not the same." : ""
    readonly property var groups: { const out = []; for (const t of targets) { let g = out.find(x => x.name === t.group); if (!g) { g = { name: t.group, items: [] }; out.push(g) } g.items.push(t) } return out }
    readonly property string textProblem: {
        const t = wmText.trim()
        if (t === "") return "Write the text of the watermark."
        for (let i = 0; i < t.length; ++i)
            if (t.charCodeAt(i) > 255) return "Only Latin letters, digits and punctuation can be used (no “" + t[i] + "”)."
        return ""
    }
    readonly property bool busy: notch.copying > 0
    spacing: 14
    Layout.fillWidth: true

    function kindOfName(n) { const s = n.toLowerCase().split(".").pop(); return s === "pdf" ? "pdf" : ["doc", "docx", "odt", "rtf"].indexOf(s) >= 0 ? "word" : ["png", "jpg", "jpeg", "webp", "bmp", "gif", "tif", "tiff"].indexOf(s) >= 0 ? "image" : "other" }
    function refresh() {
        items = notch.convertibleItems()
        if (pickedId !== "" && pickedUrl === "" && !picked) pickedId = ""
        retarget()
    }
    function textLatin(t) { for (let i = 0; i < t.length; ++i) if (t.charCodeAt(i) > 255) return "Only Latin letters, digits and punctuation can be used (no “" + t[i] + "”)."; return "" }
    function ref() { return pickedUrl !== "" ? pickedUrl : pickedId }
    function previewNumber() { return stampPages === "last" ? 0 : stampPages === "range" ? (parseInt(stampRange) || 1) : 1 }
    function refreshPreview() {
        if (!isPdf || pdfState !== "ok") { pageUrl = ""; return }
        const r = notch.previewPage(ref(), previewNumber())
        pageUrl = r.ok ? r.url : ""
        if (r.ok) pageAspect = r.aspect
    }
    function chooseSignature(url) {
        signatureUrl = url; signatureCut = ""
        signatureThumb = notch.signaturePreview(url)
        if (signatureAi) cutSignature()
    }
    function cutSignature() { if (signatureUrl !== "" && matting.ready) { cutoutFor = "stamp"; matting.cutout(signatureUrl, 1) } }
    function runBg() { bgResult = ""; cutoutFor = "bg"; matting.cutout(ref(), parseInt(bgFeather)) }
    Connections {
        target: matting
        function onCutoutReady(url, error) {
            if (url === "") return
            if (tools.cutoutFor === "bg") tools.bgResult = url
            else if (tools.cutoutFor === "stamp") { tools.signatureCut = url; tools.signatureThumb = notch.signaturePreview(url) }
        }
    }
    function stampNow() {
        const pages = stampPages === "range" ? stampRange.trim() : stampPages
        const opts = JSON.stringify({ picture: stampKind === "picture", text: stampText.trim(), color: stampColor, x: stampX, y: stampY, width: stampW, rotation: stampRot, opacity: stampOpacity, pages: pages, image: (signatureAi && signatureCut !== "" ? signatureCut : signatureUrl).replace("file://", "") })
        if (pickedUrl !== "") notch.runToolOnFiles("stamp", [pickedUrl], opts); else notch.runTool("stamp", [pickedId], opts)
    }
    function lockNow() {
        const opts = JSON.stringify({ user: lockPw, print: allowPrint, copy: allowCopy, change: allowChange })
        lockPw = ""; lockPw2 = ""
        if (pickedUrl !== "") notch.runToolOnFiles("lock", [pickedUrl], opts); else notch.runTool("lock", [pickedId], opts)
    }
    function unlockNow() {
        const pw = unlockPw
        unlockPw = ""
        if (pickedUrl !== "") notch.runToolOnFiles("unlock", [pickedUrl], pw); else notch.runTool("unlock", [pickedId], pw)
    }
    onActionChanged: if (action === "stamp") refreshPreview()
    onStampPagesChanged: refreshPreview()
    function retarget() {
        pdfState = isPdf ? notch.pdfState(ref()) : "ok"
        if (pdfState === "protected") protectMode = "unlock"
        targets = pickedUrl !== "" ? notch.convertTargetsForFile(pickedUrl) : pickedId !== "" ? notch.convertTargets([pickedId]) : []
        if (action === "stamp") refreshPreview()
    }
    function pick(id) { bgResult = ""; pickedUrl = ""; refresh(); pickedId = id; action = "convert"; retarget() }
    function pickFile(url) { bgResult = ""; pickedId = ""; pickedUrl = url; pickedName = decodeURIComponent(url.toString().split("/").pop()); action = "convert"; retarget() }
    function clearPick() { pickedId = ""; pickedUrl = ""; pickedName = ""; targets = []; pdfState = "ok"; pageUrl = ""; lockPw = ""; lockPw2 = ""; unlockPw = "" }
    readonly property string kindText: !picked ? "" : picked.kind === "pdf" ? "PDF document" : picked.kind === "word" ? "Word document" : (picked.name.split(".").pop().toUpperCase() + " file")
    function convert(target) { pickedUrl !== "" ? notch.runToolOnFiles("convert", [pickedUrl], target) : notch.runTool("convert", [pickedId], target) }
    function watermark() {
        const opts = JSON.stringify({ text: wmText.trim(), layout: layoutKind, color: color, opacity: parseInt(opacityPct), size: sizeKind, pages: pages.trim() })
        if (pickedUrl !== "") { notch.runToolOnFiles("watermark", [pickedUrl], opts); return }
        notch.runTool("watermark", [pickedId], opts)
    }
    Component.onCompleted: refresh()

    // ---------------- header ----------------
    ColumnLayout {
        spacing: 2
        Text { text: "Convert"; color: theme.text; font.pixelSize: 18; font.weight: Font.DemiBold }
        Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: "Change a file to another format, turn Word into PDF and back, or mark a PDF as confidential. The new file goes onto your shelf; the original is never changed."; color: theme.textDim; font.pixelSize: 12 }
    }

    FileDialog {
        id: fileDialog
        onVisibleChanged: shell.dialogs += visible ? 1 : -1
        Component.onDestruction: if (visible) shell.dialogs -= 1
        title: "Choose a file to convert"
        fileMode: FileDialog.OpenFile
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        onAccepted: tools.pickFile(selectedFile.toString())
    }

    // ---------------- 1. choose ----------------
    Rectangle { // drop zone
        visible: !tools.picked
        Layout.fillWidth: true
        implicitHeight: 108
        radius: theme.radiusMd
        color: tools.dropping ? Qt.alpha(theme.accent, 0.10) : theme.well
        border.width: 1
        border.color: tools.dropping ? theme.accent : theme.hairline
        ColumnLayout {
            anchors.centerIn: parent
            spacing: 8
            Rectangle { Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: 36; Layout.preferredHeight: 36; radius: 18; color: Qt.alpha(theme.accent, 0.15)
                LineIcon { anchors.centerIn: parent; glyph: "upload"; sourceSize: Qt.size(18, 18); color: theme.accent } }
            Text { Layout.alignment: Qt.AlignHCenter; text: "Drop a file here, or click to choose"; color: theme.text; font.pixelSize: 13; font.weight: Font.Medium }
        }
        HoverHandler { cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: fileDialog.open() }
        DropArea {
            anchors.fill: parent
            onEntered: (d) => { tools.dropping = d.hasUrls }
            onExited: tools.dropping = false
            onDropped: (d) => { tools.dropping = false; if (d.hasUrls) tools.pickFile(d.urls[0].toString()) }
        }
    }
    Text { visible: !tools.picked && tools.items.length > 0; text: "OR PICK FROM YOUR SHELF"; color: theme.textDim; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 1.2 }
    Caption { visible: !tools.picked && tools.items.length === 0; Layout.fillWidth: true; text: "Files on your shelf that can be converted show up here." }
    Repeater {
        model: tools.picked ? [] : tools.items
        delegate: Rectangle {
            id: row
            required property var modelData
            Layout.fillWidth: true
            implicitHeight: 40
            radius: theme.radiusSm + 2
            color: rowHover.hovered ? theme.hoverFill : "transparent"
            HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: tools.pick(row.modelData.id) }
            Accessible.role: Accessible.Button
            Accessible.name: row.modelData.name
            RowLayout {
                anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                spacing: 10
                LineIcon { glyph: "doc"; sourceSize: Qt.size(15, 15); color: theme.textDim }
                Text { Layout.fillWidth: true; text: row.modelData.name; textFormat: Text.PlainText; elide: Text.ElideMiddle; color: theme.text; font.pixelSize: 12 }
                Text { text: row.modelData.sizeText; color: theme.textFaint; font.pixelSize: 11 }
                LineIcon { glyph: "chevron"; sourceSize: Qt.size(10, 10); color: theme.textFaint; opacity: rowHover.hovered ? 1 : 0 }
            }
        }
    }

    // ---------------- 2. the chosen file ----------------
    Rectangle {
        visible: !!tools.picked
        Layout.fillWidth: true
        implicitHeight: 62
        radius: theme.radiusMd
        color: theme.well
        border.width: 1
        border.color: theme.hairline
        RowLayout {
            anchors { fill: parent; leftMargin: 14; rightMargin: 12 }
            spacing: 12
            Rectangle { Layout.preferredWidth: 36; Layout.preferredHeight: 36; radius: 10; color: Qt.alpha(theme.accent, 0.15)
                LineIcon { anchors.centerIn: parent; glyph: "doc"; sourceSize: Qt.size(18, 18); color: theme.accent } }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Text { Layout.fillWidth: true; text: tools.picked ? tools.picked.name : ""; textFormat: Text.PlainText; elide: Text.ElideMiddle; color: theme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
                Text { text: tools.kindText + (tools.picked && tools.picked.sizeText ? " · " + tools.picked.sizeText : ""); color: theme.textDim; font.pixelSize: 11 }
            }
            PillButton { text: "Change"; onClicked: tools.clearPick() }
        }
    }
    Segmented {
        visible: tools.actions.length > 1
        Layout.fillWidth: true
        model: tools.actions
        value: tools.action
        onPicked: (v) => tools.action = v
    }

    Rectangle { // a PDF that asks for a password
        visible: tools.isPdf && tools.pdfState === "protected" && tools.action !== "protect"
        Layout.fillWidth: true
        implicitHeight: lockedRow.implicitHeight + 20
        radius: theme.radiusSm + 2
        color: Qt.alpha(theme.warn, 0.12)
        RowLayout {
            id: lockedRow
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 12 }
            spacing: 10
            Text { Layout.fillWidth: true; wrapMode: Text.Wrap; color: theme.warn; font.pixelSize: 12; text: "This PDF has a password. Open it first to convert it, mark it or stamp it." }
            PillButton { text: "Unlock…"; onClicked: { tools.protectMode = "unlock"; tools.action = "protect" } }
        }
    }

    // ---------------- 3a. convert ----------------
    ColumnLayout {
        visible: !!tools.picked && (tools.action === "convert" || (tools.action === "bg" && !tools.isImage))
        Layout.fillWidth: true
        spacing: 14
        Caption { visible: tools.targets.length === 0; Layout.fillWidth: true; text: "This kind of file cannot be converted." }
        Repeater {
            model: tools.groups
            delegate: ColumnLayout {
                id: grp
                required property var modelData
                Layout.fillWidth: true
                spacing: 8
                Text { text: grp.modelData.name.toUpperCase(); color: theme.textDim; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 1.2 }
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    Repeater {
                        model: grp.modelData.items
                        delegate: Rectangle {
                            id: tile
                            required property var modelData
                            readonly property bool ok: modelData.available && !tools.busy
                            implicitWidth: tileText.implicitWidth + 28
                            implicitHeight: 36
                            radius: theme.radiusSm + 2
                            color: tileHover.hovered && tile.ok ? Qt.alpha(theme.accent, 0.16) : theme.well
                            border.width: 1
                            border.color: tileHover.hovered && tile.ok ? theme.accent : theme.hairline
                            opacity: tile.ok ? 1 : 0.5
                            HoverHandler { id: tileHover; cursorShape: tile.ok ? Qt.PointingHandCursor : Qt.ArrowCursor }
                            TapHandler { enabled: tile.ok; onTapped: tools.convert(tile.modelData.id) }
                            Accessible.role: Accessible.Button
                            Accessible.name: "Convert to " + tile.modelData.label
                            Text { id: tileText; anchors.centerIn: parent; text: tile.modelData.label + (tile.modelData.available ? "" : "  ·  " + tile.modelData.hint); color: theme.text; font.pixelSize: 12 }
                        }
                    }
                }
            }
        }
        Caption {
            visible: tools.isPdf
            Layout.fillWidth: true
            text: "PDF → Word keeps the text as an editable document. Columns, tables and formulas may look different; a scanned PDF is read with OCR and only its words come over."
        }
    }

    // ---------------- 3b. watermark ----------------
    ColumnLayout {
        visible: tools.canMark && tools.action === "mark" && tools.pdfState !== "protected"
        Layout.fillWidth: true
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 16
            Rectangle { // the page, as it will look
                id: sheet
                Layout.preferredWidth: 118
                Layout.preferredHeight: 156
                Layout.alignment: Qt.AlignTop
                color: "#ffffff"
                radius: 4
                clip: true
                border.width: 1
                border.color: theme.hairline
                Column {
                    x: 13; y: 15; spacing: 7
                    Repeater { model: 12; Rectangle { required property int index; width: index % 4 === 3 ? 54 : 92; height: 3; radius: 1; color: "#d9dce2" } }
                }
                Text { id: probe; visible: false; font.pixelSize: 100; font.bold: true; font.family: "Helvetica"; text: tools.wmText.trim() === "" ? " " : tools.wmText.trim() }
                readonly property color paint: tools.color === "red" ? "#cc1a1a" : tools.color === "blue" ? "#1a40b3" : "#737373"
                readonly property real factor: tools.sizeKind === "small" ? 0.6 : tools.sizeKind === "large" ? 1.3 : 1.0
                Repeater {
                    model: tools.layoutKind === "tiled" ? 9 : 1
                    delegate: Text {
                        required property int index
                        readonly property real len: tools.layoutKind === "diagonal" ? Math.min(sheet.width, sheet.height) * 1.1 * sheet.factor
                                                  : tools.layoutKind === "horizontal" ? sheet.width * Math.min(0.95, 0.8 * sheet.factor)
                                                  : Math.min(sheet.width, sheet.height) * 0.3 * sheet.factor
                        text: tools.wmText.trim()
                        textFormat: Text.PlainText
                        font.pixelSize: Math.max(4, 100 * len / Math.max(1, probe.implicitWidth))
                        font.bold: true
                        font.family: "Helvetica"
                        color: sheet.paint
                        opacity: parseInt(tools.opacityPct) / 100
                        x: tools.layoutKind === "tiled" ? (index % 3) * sheet.width / 3 - 6 : (sheet.width - width) / 2
                        y: tools.layoutKind === "tiled" ? Math.floor(index / 3) * sheet.height / 3 + 6 : (sheet.height - height) / 2
                        rotation: tools.layoutKind === "diagonal" ? -45 : tools.layoutKind === "tiled" ? -30 : 0
                        transformOrigin: Item.Center
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 8
                Caption { text: "Text" }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: tools.presets
                        delegate: PillButton { required property string modelData; text: modelData; checked: tools.wmText === modelData; onClicked: tools.wmText = modelData }
                    }
                }
                TextField {
                    id: textField
                    Layout.fillWidth: true
                    implicitHeight: 34
                    leftPadding: 12
                    rightPadding: 12
                    maximumLength: 40
                    text: tools.wmText
                    placeholderText: "Or write your own"
                    placeholderTextColor: theme.placeholder
                    color: theme.text
                    selectionColor: theme.accent
                    font.pixelSize: 12
                    background: Rectangle { radius: 17; color: theme.well; border.width: 1; border.color: textField.activeFocus ? theme.accent : theme.hairline }
                    onTextEdited: tools.wmText = text
                    Binding { target: textField; property: "text"; value: tools.wmText; when: !textField.activeFocus }
                }
                Text { visible: tools.textProblem !== ""; Layout.fillWidth: true; wrapMode: Text.Wrap; color: theme.warn; font.pixelSize: 11; text: tools.textProblem }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.hairline }

        ColumnLayout { Layout.fillWidth: true; spacing: 6
            Caption { text: "Layout" }
            Segmented { Layout.fillWidth: true; model: [["diagonal", "Diagonal"], ["horizontal", "Straight"], ["tiled", "Repeated"]]; value: tools.layoutKind; onPicked: (v) => tools.layoutKind = v } }
        ColumnLayout { Layout.fillWidth: true; spacing: 6
            Caption { text: "Colour" }
            Segmented { Layout.fillWidth: true; model: [["gray", "Grey"], ["red", "Red"], ["blue", "Blue"]]; value: tools.color; onPicked: (v) => tools.color = v } }
        ColumnLayout { Layout.fillWidth: true; spacing: 6
            Caption { text: "Strength" }
            Segmented { Layout.fillWidth: true; model: [["10", "Faint"], ["25", "Light"], ["40", "Medium"], ["60", "Strong"]]; value: tools.opacityPct; onPicked: (v) => tools.opacityPct = v } }
        ColumnLayout { Layout.fillWidth: true; spacing: 6
            Caption { text: "Size" }
            Segmented { Layout.fillWidth: true; model: [["small", "Small"], ["auto", "Fit the page"], ["large", "Large"]]; value: tools.sizeKind; onPicked: (v) => tools.sizeKind = v } }
        ColumnLayout { Layout.fillWidth: true; spacing: 6
            Caption { text: "Pages" }
            TextField {
                id: pagesField
                Layout.fillWidth: true
                implicitHeight: 34
                leftPadding: 12
                rightPadding: 12
                placeholderText: "All pages, or for example 1-3, 5"
                placeholderTextColor: theme.placeholder
                color: theme.text
                selectionColor: theme.accent
                font.pixelSize: 12
                background: Rectangle { radius: 17; color: theme.well; border.width: 1; border.color: pagesField.activeFocus ? theme.accent : theme.hairline }
                onTextEdited: tools.pages = text
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Caption { Layout.fillWidth: true; text: tools.isPdf ? "The watermark cannot be taken off the new file. Forms and links inside the PDF may not stay interactive." : "The document is made a PDF first, then marked." }
            PillButton { text: tools.busy ? "Working…" : "Add watermark"; iconName: "doc"; checked: true; enabled: !tools.busy && tools.textProblem === ""; onClicked: tools.watermark() }
        }
    }

    FileDialog {
        id: signatureDialog
        onVisibleChanged: shell.dialogs += visible ? 1 : -1
        Component.onDestruction: if (visible) shell.dialogs -= 1
        title: "Choose a picture to stamp"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Pictures (*.png *.jpg *.jpeg *.webp *.bmp)"]
        currentFolder: StandardPaths.writableLocation(StandardPaths.PicturesLocation)
        onAccepted: tools.chooseSignature(selectedFile.toString())
    }

    // ---------------- 3e. remove background ----------------
    ColumnLayout {
        visible: tools.isImage && tools.action === "bg"
        Layout.fillWidth: true
        spacing: 14
        Caption {
            Layout.fillWidth: true
            visible: !matting.ready
            text: !matting.supported ? "Taking the background off needs a model that this version of Notch cannot run yet."
                                     : "Taking the background off needs a model on this computer (about 190 MB). Download it in Settings → AI → Removing backgrounds."
        }
        ColumnLayout {
            visible: matting.ready
            Layout.fillWidth: true
            spacing: 12
            ColumnLayout { Layout.fillWidth: true; spacing: 6
                Caption { text: "Edge" }
                Segmented { Layout.fillWidth: true; model: [["0", "Sharp"], ["1", "Smooth"], ["3", "Very smooth"]]; value: tools.bgFeather; onPicked: (v) => tools.bgFeather = v } }
            // the result, on a chequered ground so that the transparent parts show
            Rectangle {
                id: bgSheet
                visible: tools.bgResult !== ""
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 300
                Layout.preferredHeight: 220
                color: "#ffffff"
                radius: 4
                clip: true
                border.width: 1
                border.color: theme.hairline
                Canvas {
                    anchors.fill: parent
                    onPaint: {
                        const g = getContext("2d")
                        g.fillStyle = "#ffffff"; g.fillRect(0, 0, width, height)
                        g.fillStyle = "#dcdfe5"
                        for (let y = 0; y < height; y += 10) for (let x = (y / 10) % 2 === 0 ? 0 : 10; x < width; x += 20) g.fillRect(x, y, 10, 10)
                    }
                }
                Image { anchors.fill: parent; anchors.margins: 6; source: tools.bgResult; fillMode: Image.PreserveAspectFit; cache: false; smooth: true }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Caption { Layout.fillWidth: true; text: matting.busy ? matting.status : matting.lastError !== "" ? matting.lastError : tools.bgResult !== "" ? "Done. Add it to your shelf as a PNG with a transparent background." : "The model finds the main subject and makes the rest transparent. The picture is not changed." }
                PillButton { visible: matting.busy; text: "Stop"; onClicked: matting.cancel() }
                PillButton { visible: !matting.busy && tools.bgResult === ""; text: "Remove background"; iconName: "doc"; checked: true; onClicked: tools.runBg() }
                PillButton { visible: !matting.busy && tools.bgResult !== ""; text: "Try again"; onClicked: tools.runBg() }
                PillButton { visible: !matting.busy && tools.bgResult !== ""; text: "Add to shelf"; iconName: "download"; checked: true; onClicked: { if (matting.addToShelf(tools.bgResult, tools.picked ? tools.picked.name.replace(/\.[^.]*$/, "") : "")) tools.bgResult = "" } }
            }
        }
    }

    // ---------------- 3c. stamp and signature ----------------
    ColumnLayout {
        visible: tools.isPdf && tools.action === "stamp" && tools.pdfState !== "protected"
        Layout.fillWidth: true
        spacing: 14
        Segmented { Layout.fillWidth: true; model: [["text", "Text"], ["picture", "Picture"]]; value: tools.stampKind; onPicked: (v) => { tools.stampKind = v; tools.stampRot = v === "text" ? 8 : 0 } }

        // the page, with the stamp where it will go: click or drag to place it
        Rectangle {
            id: page
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 300
            Layout.preferredHeight: 300 * tools.pageAspect
            color: "#ffffff"
            radius: 4
            clip: true
            border.width: 1
            border.color: theme.hairline
            Image { anchors.fill: parent; source: tools.pageUrl; fillMode: Image.Stretch; cache: false; asynchronous: true }
            Text { id: stampProbe; visible: false; font.pixelSize: 100; font.bold: true; font.family: "Helvetica"; text: tools.stampText.trim() === "" ? " " : tools.stampText.trim() }
            Item {
                id: stampItem
                readonly property real wpt: page.width * tools.stampW
                readonly property real fs: wpt / (stampProbe.implicitWidth / 100 + 0.8)
                width: wpt
                height: tools.stampKind === "picture" ? (sigImage.implicitWidth > 0 ? wpt * sigImage.implicitHeight / sigImage.implicitWidth : wpt * 0.4) : fs * 1.32
                x: tools.stampX * page.width - width / 2
                y: tools.stampY * page.height - height / 2
                rotation: -tools.stampRot
                opacity: tools.stampOpacity
                readonly property color paint: tools.stampColor === "red" ? "#bf1a1a" : tools.stampColor === "blue" ? "#1a40b3" : tools.stampColor === "black" ? "#0d0d0d" : "#1a8033"
                Rectangle {
                    visible: tools.stampKind === "text"
                    anchors.fill: parent
                    color: "transparent"
                    border.width: Math.max(1, stampItem.fs * 0.07)
                    border.color: stampItem.paint
                    Text { x: stampItem.fs * 0.4; anchors.verticalCenter: parent.verticalCenter; text: tools.stampText.trim(); textFormat: Text.PlainText; color: stampItem.paint; font.pixelSize: Math.max(4, stampItem.fs); font.bold: true; font.family: "Helvetica" }
                }
                Image { id: sigImage; visible: tools.stampKind === "picture"; anchors.fill: parent; source: tools.signatureThumb; fillMode: Image.PreserveAspectFit; smooth: true }
            }
            Text { visible: tools.pageUrl === ""; anchors.centerIn: parent; text: "No preview"; color: "#999999"; font.pixelSize: 12 }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.CrossCursor
                function place(m) { tools.stampX = Math.max(0, Math.min(1, m.x / page.width)); tools.stampY = Math.max(0, Math.min(1, m.y / page.height)) }
                onPressed: (m) => place(m)
                onPositionChanged: (m) => { if (pressed) place(m) }
            }
        }
        Caption { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; text: "Click or drag on the page to place it." }

        ColumnLayout { visible: tools.stampKind === "text"; Layout.fillWidth: true; spacing: 8
            Caption { text: "Text" }
            Flow {
                Layout.fillWidth: true
                spacing: 6
                Repeater { model: tools.stampPresets; delegate: PillButton { required property string modelData; text: modelData; checked: tools.stampText === modelData; onClicked: tools.stampText = modelData } }
            }
            TextField {
                id: stampField
                Layout.fillWidth: true
                implicitHeight: 34
                leftPadding: 12
                rightPadding: 12
                maximumLength: 24
                text: tools.stampText
                placeholderText: "Or write your own"
                placeholderTextColor: theme.placeholder
                color: theme.text
                selectionColor: theme.accent
                font.pixelSize: 12
                background: Rectangle { radius: 17; color: theme.well; border.width: 1; border.color: stampField.activeFocus ? theme.accent : theme.hairline }
                onTextEdited: tools.stampText = text
                Binding { target: stampField; property: "text"; value: tools.stampText; when: !stampField.activeFocus }
            }
            Caption { text: "Colour" }
            Segmented { Layout.fillWidth: true; model: [["green", "Green"], ["red", "Red"], ["blue", "Blue"], ["black", "Black"]]; value: tools.stampColor; onPicked: (v) => tools.stampColor = v }
        }
        ColumnLayout { visible: tools.stampKind === "picture"; Layout.fillWidth: true; spacing: 8
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                PillButton { text: tools.signatureThumb === "" ? "Choose a picture…" : "Change picture…"; iconName: "upload"; onClicked: signatureDialog.open() }
                Text { Layout.fillWidth: true; text: tools.signatureUrl === "" ? "" : decodeURIComponent(tools.signatureUrl.split("/").pop()); textFormat: Text.PlainText; elide: Text.ElideMiddle; color: theme.textDim; font.pixelSize: 11 }
            }
            Caption { Layout.fillWidth: true; text: "A signature, a logo or a scanned stamp. A picture with a transparent background (PNG) is used as it is; otherwise the white around it is left out, so the words under it stay readable." }
            SettingToggle {
                visible: matting.supported
                title: "Remove the background with AI"
                caption: matting.ready ? "Cuts the subject out of the picture first, so a photo of a signature on paper works." : "Download the model first: Settings → AI → Removing backgrounds."
                checked: tools.signatureAi
                onToggled: { if (!tools.signatureAi && !matting.ready) return; tools.signatureAi = !tools.signatureAi; if (tools.signatureAi) tools.cutSignature(); else tools.signatureThumb = notch.signaturePreview(tools.signatureUrl) }
            }
            Caption { visible: matting.busy && tools.cutoutFor === "stamp"; text: matting.status }
        }
        ValueSlider { title: "Size"; valueText: Math.round(tools.stampW * 100) + " % of the page width"; from: 0.05; to: 0.60; value: tools.stampW; onMoved: (v) => tools.stampW = v }
        ValueSlider { title: "Tilt"; valueText: Math.round(tools.stampRot) + "°"; from: -45; to: 45; stepSize: 1; value: tools.stampRot; onMoved: (v) => tools.stampRot = v }
        ValueSlider { title: "Strength"; valueText: Math.round(tools.stampOpacity * 100) + " %"; from: 0.2; to: 1.0; value: tools.stampOpacity; onMoved: (v) => tools.stampOpacity = v }
        ColumnLayout { Layout.fillWidth: true; spacing: 6
            Caption { text: "Place at" }
            Flow {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: [["Top left", 0.22, 0.12], ["Top right", 0.78, 0.12], ["Centre", 0.5, 0.5], ["Bottom left", 0.22, 0.88], ["Bottom right", 0.78, 0.88]]
                    delegate: PillButton { required property var modelData; text: modelData[0]; onClicked: { tools.stampX = modelData[1]; tools.stampY = modelData[2] } }
                }
            }
        }
        ColumnLayout { Layout.fillWidth: true; spacing: 6
            Caption { text: "Pages" }
            Segmented { Layout.fillWidth: true; model: [["last", "Last page"], ["first", "First page"], ["all", "All pages"], ["range", "Pages…"]]; value: tools.stampPages; onPicked: (v) => tools.stampPages = v }
            TextField {
                id: rangeField
                visible: tools.stampPages === "range"
                Layout.fillWidth: true
                implicitHeight: 34
                leftPadding: 12
                rightPadding: 12
                placeholderText: "For example 1-3, 5"
                placeholderTextColor: theme.placeholder
                color: theme.text
                selectionColor: theme.accent
                font.pixelSize: 12
                background: Rectangle { radius: 17; color: theme.well; border.width: 1; border.color: rangeField.activeFocus ? theme.accent : theme.hairline }
                onTextEdited: tools.stampRange = text
                onEditingFinished: tools.refreshPreview()
            }
        }
        Text { visible: tools.stampProblem !== ""; Layout.fillWidth: true; wrapMode: Text.Wrap; color: theme.warn; font.pixelSize: 11; text: tools.stampProblem }
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Caption { Layout.fillWidth: true; text: "A picture on the page, not a digital signature that can be verified. The original file stays as it is." }
            PillButton { text: tools.busy ? "Working…" : tools.stampKind === "picture" ? "Add signature" : "Add stamp"; iconName: "doc"; checked: true; enabled: !tools.busy && tools.stampProblem === "" && !(tools.stampPages === "range" && tools.stampRange.trim() === ""); onClicked: tools.stampNow() }
        }
    }

    // ---------------- 3d. password ----------------
    ColumnLayout {
        visible: tools.isPdf && tools.action === "protect"
        Layout.fillWidth: true
        spacing: 14
        Segmented { Layout.fillWidth: true; model: [["lock", "Lock with a password"], ["unlock", "Open a locked PDF"]]; value: tools.protectMode; onPicked: (v) => tools.protectMode = v }

        ColumnLayout {
            visible: tools.protectMode === "lock"
            Layout.fillWidth: true
            spacing: 12
            Caption { visible: tools.pdfState === "protected"; Layout.fillWidth: true; text: "This PDF already has a password. Open it first if you want to lock it again." }
            ColumnLayout { Layout.fillWidth: true; spacing: 6
                Caption { text: "Password" }
                TextField {
                    id: pw1
                    Layout.fillWidth: true
                    implicitHeight: 34
                    leftPadding: 12
                    rightPadding: 12
                    echoMode: TextInput.Password
                    maximumLength: 32
                    text: tools.lockPw
                    placeholderText: "At least 4 characters"
                    placeholderTextColor: theme.placeholder
                    color: theme.text
                    selectionColor: theme.accent
                    font.pixelSize: 12
                    background: Rectangle { radius: 17; color: theme.well; border.width: 1; border.color: pw1.activeFocus ? theme.accent : theme.hairline }
                    onTextEdited: tools.lockPw = text
                }
                TextField {
                    id: pw2
                    Layout.fillWidth: true
                    implicitHeight: 34
                    leftPadding: 12
                    rightPadding: 12
                    echoMode: TextInput.Password
                    maximumLength: 32
                    text: tools.lockPw2
                    placeholderText: "Type it again"
                    placeholderTextColor: theme.placeholder
                    color: theme.text
                    selectionColor: theme.accent
                    font.pixelSize: 12
                    background: Rectangle { radius: 17; color: theme.well; border.width: 1; border.color: pw2.activeFocus ? theme.accent : theme.hairline }
                    onTextEdited: tools.lockPw2 = text
                }
                Text { visible: tools.lockPw !== "" && tools.lockProblem !== ""; Layout.fillWidth: true; wrapMode: Text.Wrap; color: theme.warn; font.pixelSize: 11; text: tools.lockProblem }
            }
            ColumnLayout { Layout.fillWidth: true; spacing: 10
                Caption { text: "Once it is open, it may be" }
                SettingToggle { title: "Printed"; checked: tools.allowPrint; onToggled: tools.allowPrint = !tools.allowPrint }
                SettingToggle { title: "Copied from"; caption: "Selecting and copying its text"; checked: tools.allowCopy; onToggled: tools.allowCopy = !tools.allowCopy }
                SettingToggle { title: "Changed"; caption: "Editing, adding notes and forms"; checked: tools.allowChange; onToggled: tools.allowChange = !tools.allowChange }
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: theme.warn
                font.pixelSize: 11
                text: "If you forget the password nobody can open the file again, Notch included. Keep the original. The lock (128-bit) keeps casual readers out; some programs ignore the print and copy limits, and it is not meant to stop a determined attacker."
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                PillButton { text: tools.busy ? "Working…" : "Lock PDF"; iconName: "lock"; checked: true; enabled: !tools.busy && tools.lockProblem === "" && tools.pdfState === "ok"; onClicked: tools.lockNow() }
            }
        }

        ColumnLayout {
            visible: tools.protectMode === "unlock"
            Layout.fillWidth: true
            spacing: 12
            Caption { Layout.fillWidth: true; text: tools.pdfState === "protected" ? "Type the password of this PDF. The copy has no password; the locked file stays as it is." : tools.pdfState === "ok" ? "This PDF has no password." : "This file cannot be read." }
            TextField {
                id: pwOpen
                Layout.fillWidth: true
                implicitHeight: 34
                leftPadding: 12
                rightPadding: 12
                echoMode: TextInput.Password
                text: tools.unlockPw
                placeholderText: "Password"
                placeholderTextColor: theme.placeholder
                color: theme.text
                selectionColor: theme.accent
                font.pixelSize: 12
                enabled: tools.pdfState === "protected"
                background: Rectangle { radius: 17; color: theme.well; border.width: 1; border.color: pwOpen.activeFocus ? theme.accent : theme.hairline }
                onTextEdited: tools.unlockPw = text
                onAccepted: if (tools.unlockPw !== "" && !tools.busy) tools.unlockNow()
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                PillButton { text: tools.busy ? "Working…" : "Unlock PDF"; iconName: "lock"; checked: true; enabled: !tools.busy && tools.unlockPw !== "" && tools.pdfState === "protected"; onClicked: tools.unlockNow() }
            }
        }
    }
}
