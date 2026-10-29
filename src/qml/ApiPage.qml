import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// API tester: craft HTTP requests (collections on the left, builder + response on the right).
// One request card (method · URL · Send), underline tabs, and a response that sits in the same
// surface as the builder — same visual language as the rest of the notch (well / hairline / accent).
ColumnLayout {
    id: page
    required property var shell
    // mounted by DevPage's Loader; that shell controls when this tab is shown
    visible: true
    Layout.fillWidth: true
    Layout.maximumWidth: 100000
    spacing: 0

    property string sidebarTab: "collections"
    property int selectedPort: -1
    property string portCommand: ""
    property var portResult: ({})
    property string detailTab: "params"
    property string responseTab: "body"
    property string selectedId: ""
    property string selectedKind: ""
    property string selectedName: ""
    property bool dirty: false
    // context-menu target (right-click on a sidebar row)
    property string menuId: ""
    property string menuKind: ""
    property string menuName: ""
    property string menuParent: "" // what holds the menu's node: its collection or folder
    property var collapsed: ({})   // folder / collection ids folded shut in the sidebar
    property string dialogPurpose: "rename" // rename | folder | collection: what the name dialog is for
    // the tree as the sidebar shows it: the flat rows from the backend, without whatever sits inside a folded folder
    readonly property var treeRows: {
        const out = []
        let skip = -1
        for (const r of apiTester.collections) {
            if (skip >= 0 && (r.depth || 0) > skip)
                continue
            skip = -1
            out.push(r)
            if ((r.kind === "collection" || r.kind === "folder") && collapsed[r.id])
                skip = r.depth || 0
        }
        return out
    }
    readonly property bool modalOpen: nodeMenu.visible || envMenu.visible || importMenu.visible || exportMenu.visible || methodMenu.visible
                                      || renameDialog.visible || deleteDialog.visible || importDialog.visible || pickDialog.visible || envEditor.visible
    property var draft: ({ method: "GET", url: "", headers: [], query: [], bodyMode: "none", body: "",
                           authType: "none", authToken: "", authUser: "", authPassword: "",
                           preScript: "", testScript: "", name: "New request" })
    property var resp: ({})

    readonly property color methodColor: {
        switch ((draft.method || "GET").toUpperCase()) {
        case "GET": return theme.success
        case "POST": return theme.accent
        case "PUT": return theme.info
        case "PATCH": return theme.warn
        case "DELETE": return theme.danger
        default: return theme.textDim
        }
    }

    function say(text) {
        shell.infoText = text
        infoTimer.restart()
    }
    Timer { id: infoTimer; interval: 1600; onTriggered: { if (shell.infoText.indexOf("API") === 0 || shell.infoText.indexOf("Import") === 0) shell.infoText = "" } }

    function loadRequest(id) {
        const m = apiTester.requestById(id)
        if (!m || m.id === undefined || m.id === "")
            return
        selectedId = id
        selectedKind = "request"
        selectedName = m.name || ""
        draft = m
        dirty = false
    }

    function selectNode(id, kind, name) {
        selectedId = id
        selectedKind = kind
        selectedName = name || ""
        if (kind === "request")
            loadRequest(id)
    }

    function openNodeMenu(id, kind, name, parentId) {
        menuId = id
        menuKind = kind
        menuName = name || ""
        menuParent = parentId || ""
        nodeMenu.popup()
    }

    function toggleFolded(id) {
        const c = Object.assign({}, collapsed)
        if (c[id]) delete c[id]
        else c[id] = true
        collapsed = c
    }
    function unfold(id) {
        if (!collapsed[id]) return
        const c = Object.assign({}, collapsed)
        delete c[id]
        collapsed = c
    }

    // name prompts: a new collection, a new folder inside menuId, or a rename of menuId
    function askName(purpose, initial) {
        dialogPurpose = purpose
        renameArea.text = initial || ""
        renameDialog.open()
        renameArea.forceActiveFocus()
        renameArea.selectAll()
    }
    function newRequestIn(parentId) {
        const rid = apiTester.addRequest(parentId, "")
        if (rid) {
            unfold(parentId)
            loadRequest(rid)
            detailTab = "params"
        }
    }
    function duplicateRequestNow() {
        const copy = apiTester.duplicateNode(selectedId)
        if (copy) {
            loadRequest(copy)
            say("API — request duplicated")
        }
    }

    function deleteMenuTarget() {
        const id = menuId
        const kind = menuKind
        const name = menuName
        apiTester.removeNode(id)
        if (kind === "collection" || kind === "folder") {
            // selection may have been inside the removed tree
            selectedId = ""
            selectedKind = ""
            selectedName = ""
        } else if (selectedId === id) {
            selectedId = ""
            selectedKind = ""
            selectedName = ""
        }
        if (kind === "collection" || kind === "folder")
            say("API — deleted “" + name + "”")
        else
            say("API — request deleted")
        menuId = ""
    }

    function sendDraft() {
        if (!apiTester.busy && draft.url.length > 0)
            apiTester.send(draft)
    }

    function kvAdd(listName) {
        const a = draft[listName].slice()
        a.push({ key: "", value: "", enabled: true })
        draft[listName] = a
        dirty = true
    }

    function kvSet(listName, i, field, val) {
        const a = draft[listName].slice()
        const row = Object.assign({}, a[i])
        row[field] = field === "enabled" ? !row[field] : val
        a[i] = row
        draft[listName] = a
        dirty = true
    }

    function kvRemove(listName, i) {
        const a = draft[listName].slice()
        a.splice(i, 1)
        draft[listName] = a
        dirty = true
    }

    function setDraft(patch) {
        draft = Object.assign({}, draft, patch)
        dirty = true
    }

    Connections {
        target: apiTester
        function onResponseChanged() { page.resp = apiTester.lastResponse }
        function onStatusMessage(text) { page.say(text) }
        function onServiceCommandFinished(result) { page.portResult = result }
    }

    // Small underline tab strip — lighter than Segmented for 5–6 labels.
    component TabBar: Row {
        id: tabs
        property var model: []       // [[value,label],…]
        property var value
        signal picked(var value)
        spacing: 4

        Repeater {
            model: tabs.model
            delegate: AbstractButton {
                required property var modelData
                required property int index
                // Size from the label so tabs never collapse on top of each other.
                padding: 8
                topPadding: 12
                bottomPadding: 8
                contentItem: Text {
                    text: modelData[1]
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: tabs.value === modelData[0] ? theme.accent : parent.parent.hovered ? theme.text : theme.textDim
                    font.pixelSize: 12
                    font.weight: tabs.value === modelData[0] ? Font.DemiBold : Font.Medium
                }
                background: Rectangle {
                    color: parent.hovered && tabs.value !== modelData[0] ? theme.hoverFill : "transparent"
                    radius: theme.radiusSm
                    // underline for the active tab
                    Rectangle {
                        visible: tabs.value === modelData[0]
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 2
                        width: parent.width - 8
                        height: 2
                        radius: 1
                        anchors.horizontalCenter: parent.horizontalCenter
                        color: theme.accent
                        Behavior on color { enabled: activities.animations; ColorAnimation { duration: 120 } }
                    }
                }
                onClicked: { tabs.value = modelData[0]; tabs.picked(modelData[0]) }
            }
        }
    }

    component MethodPill: Rectangle {
        id: methodPill
        property string method: "GET"
        readonly property color col: {
            switch (method.toUpperCase()) {
            case "GET": return theme.success
            case "POST": return theme.accent
            case "PUT": return theme.info
            case "PATCH": return theme.warn
            case "DELETE": return theme.danger
            default: return theme.textDim
            }
        }
        implicitWidth: methodText.implicitWidth + 18
        implicitHeight: 28
        radius: theme.washi ? theme.chipRadius : 8
        color: Qt.alpha(col, 0.12)
        border.width: 1
        border.color: Qt.alpha(col, 0.45)
        HoverHandler { cursorShape: Qt.PointingHandCursor }
        Text {
            id: methodText
            anchors.centerIn: parent
            text: methodPill.method
            color: methodPill.col
            font.pixelSize: 11
            font.weight: Font.Bold
            font.letterSpacing: 0.4
        }
        TapHandler { onTapped: methodMenu.popup() }
    }

    // menu rows in the notch's own colours (a bare MenuItem is drawn in the system style, nearly invisible on the dark card)
    component NodeItem: MenuItem {
        id: mi
        implicitHeight: 32
        leftPadding: 8
        rightPadding: 8
        contentItem: Text {
            text: (mi.checkable && mi.checked ? "✓  " : "") + mi.text
            color: mi.enabled ? theme.text : theme.textFaint
            font.pixelSize: 12
            font.weight: Font.Medium
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        indicator: null
        background: Rectangle {
            radius: theme.radiusSm
            color: mi.highlighted && mi.enabled ? Qt.alpha(theme.accent, 0.22) : "transparent"
            border.width: mi.highlighted && mi.enabled ? 1 : 0
            border.color: Qt.alpha(theme.accent, 0.45)
        }
    }
    component NodeSep: MenuSeparator {
        implicitHeight: 9
        contentItem: Rectangle { implicitHeight: 1; color: Qt.alpha(theme.text, 0.15) }
    }

    component StatusDot: Rectangle {
        property bool ok: false
        property bool bad: false
        width: 7; height: 7; radius: 4
        color: ok ? theme.success : bad ? theme.danger : theme.textFaint
    }

    // ---- sidebar ----
    RowLayout {
        Layout.fillWidth: true
        Layout.preferredHeight: 460
        spacing: 0

        ColumnLayout {
            Layout.preferredWidth: 204
            Layout.maximumWidth: 224
            Layout.fillHeight: true
            spacing: 8

            // env chip — always visible, one place
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 30
                    radius: theme.washi ? theme.chipRadius : 8
                    color: theme.well
                    border.width: 1
                    border.color: theme.hairline
                    HoverHandler { id: envHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: envMenu.popup() }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 8
                        spacing: 6
                        Text {
                            text: "ENV"
                            color: theme.textFaint
                            font.pixelSize: 9
                            font.weight: Font.Bold
                            font.letterSpacing: 1
                        }
                        Text {
                            Layout.fillWidth: true
                            text: {
                                for (const e of apiTester.environments)
                                    if (e.id === apiTester.activeEnvId)
                                        return e.name
                                return "No environment"
                            }
                            color: apiTester.activeEnvId === "" ? theme.textFaint : theme.text
                            font.pixelSize: 12
                            font.weight: Font.Medium
                            elide: Text.ElideRight
                        }
                        LineIcon {
                            Layout.alignment: Qt.AlignVCenter
                            glyph: "chevron"
                            sourceSize: Qt.size(12, 12)
                            color: theme.textDim
                            rotation: -90
                        }
                    }
                }
            }

            TabBar {
                Layout.fillWidth: true
                model: [["collections", "Collections"], ["history", "History"], ["ports", "Ports"]]
                value: page.sidebarTab
                onPicked: (v) => page.sidebarTab = v
            }

            ListView {
                id: sideList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 1
                boundsBehavior: Flickable.StopAtBounds
                model: page.sidebarTab === "collections" ? page.treeRows
                       : page.sidebarTab === "history" ? apiTester.history
                       : apiTester.ports
                ScrollBar.vertical: ScrollBar { padding: 2 }

                // Empty states in the app's voice
                Text {
                    visible: sideList.count === 0 && page.sidebarTab !== "collections"
                    anchors.centerIn: parent
                    width: parent.width - 16
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    color: theme.textDim
                    font.pixelSize: 12
                    text: page.sidebarTab === "history"
                          ? "Nothing sent yet.\nYour last 50 requests show up here."
                          : page.sidebarTab === "ports"
                            ? "No ports watched.\nAdd one below to see if it is listening."
                            : "No collections.\nUse + to start one."
                }

                // No collections yet: say what to do, with the two ways to start
                ColumnLayout {
                    visible: sideList.count === 0 && page.sidebarTab === "collections"
                    anchors.centerIn: parent
                    width: parent.width - 16
                    spacing: 8
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        color: theme.textSoft
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        text: "No collections yet"
                    }
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        color: theme.textDim
                        font.pixelSize: 11
                        text: "A collection keeps related requests together, in folders if you like."
                    }
                    PillButton {
                        Layout.alignment: Qt.AlignHCenter
                        text: "New collection"
                        iconName: "plus"
                        checked: true
                        onClicked: page.askName("collection", "Collection " + (apiTester.collections.length + 1))
                    }
                    PillButton {
                        Layout.alignment: Qt.AlignHCenter
                        text: "Import JSON"
                        iconName: "download"
                        onClicked: importMenu.popup()
                    }
                }

                // One delegate for all three tabs: ports vs collections/history
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    readonly property bool isPort: page.sidebarTab === "ports"
                    readonly property bool isRow: modelData !== null && modelData !== undefined
                    readonly property bool isReq: !isPort && isRow && (modelData.kind === "request" || (modelData.kind === undefined && modelData.method))
                    readonly property bool isColl: !isPort && isRow && modelData.kind === "collection"
                    readonly property bool isFolder: !isPort && isRow && modelData.kind === "folder"
                    readonly property bool isHist: !isPort && isRow && modelData.kind === "history"
                    readonly property bool sel: isRow && (isPort ? page.selectedPort === modelData.port : page.selectedId === modelData.id)
                    visible: isRow
                    width: sideList.width
                    height: !isRow ? 0 : isPort ? 34 : (isReq || isHist ? 32 : 28)
                    radius: theme.washi ? 4 : 7
                    color: sel ? Qt.alpha(theme.accent, 0.14) : rowHov.hovered ? theme.hoverFill : "transparent"
                    HoverHandler { id: rowHov; cursorShape: Qt.PointingHandCursor }

                    // Left accent bar on selection
                    Rectangle {
                        visible: sel
                        width: 2
                        height: parent.height - 10
                        y: 5
                        radius: 1
                        color: theme.accent
                    }

                    // ---------- ports row ----------
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 4
                        spacing: 8
                        visible: row.isPort

                        StatusDot {
                            ok: !!modelData.open
                            bad: !modelData.open && modelData.checkedAt !== ""
                        }
                        Text {
                            text: ":" + modelData.port
                            color: theme.text
                            font.family: "monospace"
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                            Layout.preferredWidth: 48
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.open
                                  ? ((modelData.process || "listening") + (modelData.pid > 0 ? " · " + modelData.pid : ""))
                                  : (modelData.checkedAt ? (modelData.error || "closed") : "not checked")
                            color: modelData.open ? theme.textSoft : theme.textDim
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                        Text {
                            text: modelData.checkedAt || ""
                            color: theme.textFaint
                            font.pixelSize: 9
                            font.family: "monospace"
                        }
                        IconButton {
                            iconName: "close"
                            opacity: 0.55
                            Accessible.name: "Unwatch port"
                            onClicked: apiTester.removePort(modelData.port)
                        }
                    }

                    // ---------- collections / history row ----------
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: (row.isPort ? 0 : (modelData.depth || 0) * 12 + 8)
                        anchors.rightMargin: 8
                        spacing: 7
                        visible: !row.isPort

                        Rectangle {
                            visible: isReq || !!(isHist && modelData.method)
                            implicitWidth: methT.implicitWidth + 8
                            implicitHeight: 15
                            radius: 3
                            color: "transparent"
                            border.width: 1
                            border.color: {
                                const m = modelData.method || ""
                                if (m === "GET") return Qt.alpha(theme.success, 0.55)
                                if (m === "DELETE") return Qt.alpha(theme.danger, 0.55)
                                if (m === "POST") return Qt.alpha(theme.accent, 0.55)
                                return Qt.alpha(theme.info, 0.5)
                            }
                            Text {
                                id: methT
                                anchors.centerIn: parent
                                text: modelData.method || ""
                                font.pixelSize: 8
                                font.weight: Font.Bold
                                font.letterSpacing: 0.3
                                color: {
                                    const m = modelData.method || ""
                                    if (m === "GET") return theme.success
                                    if (m === "DELETE") return theme.danger
                                    if (m === "POST") return theme.accent
                                    return theme.info
                                }
                            }
                        }

                        Item { // fold / unfold: the arrow turns
                            visible: isColl || isFolder
                            implicitWidth: 14; implicitHeight: 20
                            LineIcon {
                                anchors.centerIn: parent
                                glyph: "chevron"
                                sourceSize: Qt.size(11, 11)
                                color: theme.textDim
                                rotation: page.collapsed[modelData.id] ? -90 : 0
                                Behavior on rotation { enabled: activities.animations; NumberAnimation { duration: 110 } }
                            }
                            TapHandler { onTapped: page.toggleFolded(modelData.id) }
                        }
                        LineIcon {
                            visible: isColl || isFolder
                            glyph: "folder"
                            sourceSize: Qt.size(12, 12)
                            color: isColl ? theme.accent : theme.textFaint
                        }

                        Text {
                            Layout.fillWidth: true
                            text: isHist ? (modelData.url || "") : (modelData.name || "")
                            color: isColl ? theme.text
                                        : isHist && modelData.status >= 400 ? theme.danger
                                        : theme.textSoft
                            font.pixelSize: 12
                            font.weight: isColl ? Font.DemiBold : Font.Normal
                            elide: Text.ElideRight
                            maximumLineCount: 1
                        }

                        // actions on the row under the pointer: add into a collection / folder, and the full menu
                        Row {
                            visible: rowHov.hovered && !isHist
                            spacing: 0
                            Item {
                                visible: isColl || isFolder
                                width: 20; height: 20
                                LineIcon { anchors.centerIn: parent; glyph: "plus"; sourceSize: Qt.size(12, 12); color: theme.textDim }
                                HoverHandler { id: plusHov; cursorShape: Qt.PointingHandCursor }
                                TapHandler { onTapped: page.newRequestIn(modelData.id) }
                                ToolTip.visible: plusHov.hovered
                                ToolTip.text: "New request here"
                            }
                            Item {
                                width: 20; height: 20
                                Text { anchors.centerIn: parent; text: "⋯"; color: theme.textDim; font.pixelSize: 14; font.weight: Font.Bold }
                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                                TapHandler { onTapped: page.openNodeMenu(modelData.id, isReq ? "request" : modelData.kind, modelData.name || "", modelData.parentId || "") }
                            }
                        }

                        StatusDot {
                            visible: isHist
                            ok: isHist && modelData.status >= 200 && modelData.status < 300
                            bad: isHist && (modelData.status >= 400 || modelData.status === 0)
                        }
                        Text {
                            visible: isHist && modelData.durationMs > 0
                            text: modelData.durationMs + "ms"
                            color: theme.textFaint
                            font.pixelSize: 10
                            font.family: "monospace"
                        }
                    }

                    TapHandler {
                        enabled: !row.isPort
                        onTapped: {
                            if (isHist) {
                                page.selectedId = modelData.id
                                page.selectedKind = "history"
                                page.selectedName = ""
                            } else if (isReq) {
                                page.loadRequest(modelData.id)
                            } else {
                                page.selectNode(modelData.id, modelData.kind, modelData.name)
                                page.toggleFolded(modelData.id)
                            }
                        }
                    }
                    TapHandler {
                        enabled: row.isPort
                        onTapped: {
                            page.selectedPort = modelData.port
                            page.portResult = {}
                            apiTester.checkPort(modelData.port)
                        }
                    }
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        enabled: !row.isPort && !isHist
                        onTapped: page.openNodeMenu(modelData.id, isReq ? "request" : modelData.kind,
                                                     modelData.name || "", modelData.parentId || "")
                    }
                }
            }

            // add a port to watch (Ports tab)
            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                visible: page.sidebarTab === "ports"
                TextField {
                    id: addPortField
                    Layout.fillWidth: true
                    implicitHeight: 28
                    placeholderText: "Port  e.g. 8080"
                    placeholderTextColor: theme.textFaint
                    color: theme.text
                    font.family: "monospace"
                    font.pixelSize: 12
                    leftPadding: 8
                    rightPadding: 8
                    validator: IntValidator { bottom: 1; top: 65535 }
                    background: Rectangle {
                        radius: 6
                        color: theme.well
                        border.width: 1
                        border.color: addPortField.activeFocus ? Qt.alpha(theme.accent, 0.4) : theme.hairline
                    }
                    onAccepted: {
                        if (text.length > 0) {
                            apiTester.addPort(parseInt(text))
                            page.selectedPort = parseInt(text)
                            text = ""
                        }
                    }
                }
                PillButton {
                    text: "Add"
                    enabled: addPortField.text.length > 0
                    onClicked: {
                        apiTester.addPort(parseInt(addPortField.text))
                        page.selectedPort = parseInt(addPortField.text)
                        addPortField.text = ""
                    }
                }
                IconButton {
                    iconName: "search"
                    Accessible.name: "Recheck all ports"
                    ToolTip.visible: hovered
                    ToolTip.text: "Recheck all"
                    onClicked: apiTester.checkAllPorts()
                }
            }

            // sidebar footer — quiet actions (Collections / History)
            RowLayout {
                Layout.fillWidth: true
                spacing: 2
                visible: page.sidebarTab !== "ports"
                IconButton {
                    iconName: "plus"
                    Accessible.name: "New collection"
                    ToolTip.visible: hovered
                    ToolTip.text: "New collection"
                    onClicked: page.askName("collection", "Collection " + (apiTester.collections.length + 1))
                }
                IconButton {
                    iconName: "folder"
                    Accessible.name: "New request"
                    ToolTip.visible: hovered
                    ToolTip.text: "New request (in the selected collection or folder)"
                    onClicked: {
                        let parent = ""
                        if (page.selectedKind === "collection" || page.selectedKind === "folder")
                            parent = page.selectedId
                        else if (page.selectedKind === "request")
                            parent = (apiTester.collections.find(r => r.id === page.selectedId) || {}).parentId || ""
                        if (parent === "") {
                            for (const c of apiTester.collections)
                                if (c.kind === "collection") { parent = c.id; break }
                        }
                        if (parent === "")
                            page.askName("collection", "Collection 1") // nothing to put it in yet
                        else
                            page.newRequestIn(parent)
                    }
                }
                IconButton {
                    iconName: "download"
                    Accessible.name: "Import"
                    ToolTip.visible: hovered
                    ToolTip.text: "Import JSON"
                    onClicked: importMenu.popup()
                }
                IconButton {
                    iconName: "upload"
                    Accessible.name: "Export"
                    ToolTip.visible: hovered
                    ToolTip.text: "Export / history"
                    enabled: page.selectedId !== "" || page.sidebarTab === "history"
                    onClicked: exportMenu.popup()
                }
                Item { Layout.fillWidth: true }
            }
        }

        // vertical rule
        Rectangle {
            Layout.preferredWidth: 1
            Layout.fillHeight: true
            Layout.topMargin: 2
            Layout.bottomMargin: 2
            color: theme.hairline
        }

        // ---- detail column ----
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 12
            spacing: 0

            // ---- Ports panel (sidebar Tab = Ports) ----
            ColumnLayout {
                id: portsPanel
                visible: page.sidebarTab === "ports"
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 10

                readonly property var entry: {
                    for (const p of apiTester.ports)
                        if (p.port === page.selectedPort)
                            return p
                    return null
                }

                Text {
                    text: page.selectedPort >= 0 ? "Port :" + page.selectedPort : "Local ports"
                    color: theme.text
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }

                Text {
                    visible: page.selectedPort < 0
                    Layout.fillWidth: true
                    color: theme.textDim
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                    text: "Watch a local port to see if anything is listening. Select a row to recheck, stop the process, or run a stop command (docker compose stop, systemctl stop, …)."
                }

                // status card
                Rectangle {
                    visible: page.selectedPort >= 0
                    Layout.fillWidth: true
                    implicitHeight: statusCol.implicitHeight + 24
                    radius: theme.radiusMd
                    color: theme.well
                    border.width: 1
                    border.color: theme.hairline

                    ColumnLayout {
                        id: statusCol
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
                        spacing: 8

                        RowLayout {
                            spacing: 8
                            StatusDot {
                                ok: portsPanel.entry && portsPanel.entry.open
                                bad: portsPanel.entry && !portsPanel.entry.open && portsPanel.entry.checkedAt !== ""
                                width: 9; height: 9; radius: 5
                            }
                            Text {
                                text: !portsPanel.entry ? "—"
                                      : portsPanel.entry.open ? "LISTENING" : (portsPanel.entry.error || "CLOSED")
                                color: !portsPanel.entry ? theme.textDim : portsPanel.entry.open ? theme.success : theme.danger
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                                font.letterSpacing: 0.5
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                visible: !!portsPanel.entry && !!portsPanel.entry.checkedAt
                                text: portsPanel.entry ? "checked " + portsPanel.entry.checkedAt : ""
                                color: theme.textFaint
                                font.pixelSize: 10
                                font.family: "monospace"
                            }
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 12
                            rowSpacing: 4
                            visible: portsPanel.entry && portsPanel.entry.open

                            Text { text: "Process"; color: theme.textDim; font.pixelSize: 11 }
                            Text {
                                text: portsPanel.entry ? (portsPanel.entry.process || "unknown") : ""
                                color: theme.text
                                font.pixelSize: 12
                                font.weight: Font.Medium
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Text { text: "PID"; color: theme.textDim; font.pixelSize: 11 }
                            Text {
                                text: portsPanel.entry && portsPanel.entry.pid > 0 ? String(portsPanel.entry.pid) : "—"
                                color: theme.text
                                font.family: "monospace"
                                font.pixelSize: 12
                            }
                        }

                        RowLayout {
                            spacing: 6
                            PillButton {
                                text: "Recheck"
                                iconName: "search"
                                enabled: page.selectedPort >= 0
                                onClicked: {
                                    page.portResult = {}
                                    apiTester.checkPort(page.selectedPort)
                                }
                            }
                            PillButton {
                                text: "Stop process"
                                iconName: "stop"
                                visible: portsPanel.entry && portsPanel.entry.open && portsPanel.entry.pid > 0
                                enabled: !apiTester.commandRunning
                                onClicked: {
                                    const msg = apiTester.stopService(page.selectedPort)
                                    page.say("API — " + msg)
                                }
                            }
                            Item { Layout.fillWidth: true }
                            IconButton {
                                iconName: "trash"
                                Accessible.name: "Unwatch port"
                                onClicked: {
                                    apiTester.removePort(page.selectedPort)
                                    page.selectedPort = -1
                                    page.portResult = {}
                                }
                            }
                        }
                    }
                }

                // custom stop / control command
                ColumnLayout {
                    visible: page.selectedPort >= 0
                    Layout.fillWidth: true
                    spacing: 6
                    Text {
                        text: "Run a command"
                        color: theme.textDim
                        font.pixelSize: 11
                        font.weight: Font.Medium
                    }
                    Text {
                        Layout.fillWidth: true
                        color: theme.textFaint
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                        text: "Executed in a local shell — e.g. docker compose stop relay, systemctl --user stop myapp, fuser -k " + Math.max(1, page.selectedPort) + "/tcp"
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        TextField {
                            id: cmdField
                            Layout.fillWidth: true
                            implicitHeight: 32
                            text: page.portCommand
                            placeholderText: "docker compose stop …"
                            placeholderTextColor: theme.textFaint
                            color: theme.text
                            font.family: "monospace"
                            font.pixelSize: 12
                            leftPadding: 10
                            rightPadding: 10
                            background: Rectangle {
                                radius: 8
                                color: theme.well
                                border.width: 1
                                border.color: cmdField.activeFocus ? Qt.alpha(theme.accent, 0.45) : theme.hairline
                            }
                            onTextEdited: page.portCommand = text
                            onAccepted: if (text.trim().length) apiTester.runServiceCommand(text.trim())
                        }
                        PillButton {
                            text: apiTester.commandRunning ? "Running…" : "Run"
                            checked: apiTester.commandRunning
                            enabled: !apiTester.commandRunning && cmdField.text.trim().length > 0
                            onClicked: apiTester.runServiceCommand(cmdField.text.trim())
                        }
                    }
                    // last command output
                    Rectangle {
                        visible: (page.portResult.output || "").length > 0 || page.portResult.ok === false
                        Layout.fillWidth: true
                        implicitHeight: Math.min(cmdOut.lineCount, 8) * 14 + 14
                        radius: 6
                        color: theme.well
                        border.width: 1
                        border.color: page.portResult.ok === false ? Qt.alpha(theme.danger, 0.4) : theme.hairline
                        Text {
                            id: cmdOut
                            anchors { fill: parent; margins: 7 }
                            text: page.portResult.output || (page.portResult.ok === false ? "Command failed" : "")
                            color: page.portResult.ok === false ? theme.danger : theme.textSoft
                            font.family: "monospace"
                            font.pixelSize: 11
                            wrapMode: Text.WrapAnywhere
                            maximumLineCount: 8
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            // 1) Request card — method, URL and Send share one surface
            Rectangle {
                visible: page.sidebarTab !== "ports"
                Layout.fillWidth: true
                implicitHeight: 44
                radius: theme.washi ? theme.chipRadius : 10
                color: theme.well
                border.width: 1
                border.color: urlField.activeFocus ? Qt.alpha(theme.accent, 0.5) : theme.hairline
                Behavior on border.color { enabled: activities.animations; ColorAnimation { duration: 120 } }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 6
                    spacing: 8

                    MethodPill {
                        method: page.draft.method || "GET"
                    }

                    Rectangle { width: 1; height: 20; color: theme.hairline }

                    TextField {
                        id: urlField
                        Layout.fillWidth: true
                        height: 40
                        verticalAlignment: TextInput.AlignVCenter
                        text: page.draft.url
                        placeholderText: "https://…  or  {{baseUrl}}/api/v1/ping"
                        placeholderTextColor: theme.textFaint
                        color: theme.text
                        selectionColor: theme.accent
                        font.family: "monospace"
                        font.pixelSize: 12
                        leftPadding: 0
                        rightPadding: 0
                        background: null
                        onTextChanged: {
                            if (page.draft.url !== text)
                                page.setDraft({ url: text })
                        }
                        Keys.onReturnPressed: if (event.modifiers & Qt.ControlModifier) page.sendDraft()
                        Keys.onEnterPressed: if (event.modifiers & Qt.ControlModifier) page.sendDraft()
                    }

                    // dirty pip — only when there is something to save
                    Rectangle {
                        visible: page.dirty && page.selectedKind === "request"
                        width: 6; height: 6; radius: 3
                        color: theme.accent
                        ToolTip.visible: hoverDirty.hovered
                        ToolTip.text: "Unsaved changes"
                        HoverHandler { id: hoverDirty }
                    }

                    Rectangle {
                        id: sendBtn
                        implicitWidth: sendLbl.implicitWidth + 28
                        implicitHeight: 32
                        radius: theme.washi ? theme.chipRadius : 8
                        color: apiTester.busy ? Qt.alpha(theme.danger, 0.14)
                             : sendHov.hovered || sendDown.pressed ? Qt.alpha(theme.accent, 0.22)
                             : Qt.alpha(theme.accent, 0.12)
                        border.width: 1
                        border.color: apiTester.busy ? Qt.alpha(theme.danger, 0.45) : Qt.alpha(theme.accent, 0.45)
                        HoverHandler { id: sendHov; cursorShape: Qt.PointingHandCursor }
                        TapHandler { id: sendDown }
                        Text {
                            id: sendLbl
                            anchors.centerIn: parent
                            text: apiTester.busy ? "Cancel" : "Send"
                            color: apiTester.busy ? theme.danger : theme.accent
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }
                        TapHandler {
                            onTapped: apiTester.busy ? apiTester.cancel() : page.sendDraft()
                        }
                        // brief press feedback
                        scale: sendDown.pressed ? 0.97 : 1
                        Behavior on scale { enabled: activities.animations; NumberAnimation { duration: 80 } }
                    }
                }
            }

            // 2) slim meta row: rename / save / delete for whatever is selected
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                Layout.bottomMargin: 2
                spacing: 6
                visible: page.sidebarTab !== "ports"
                         && (page.selectedKind === "request" || page.selectedKind === "collection"
                             || page.selectedKind === "folder" || page.dirty)

                // Collection / folder: inline rename
                TextField {
                    id: collNameField
                    Layout.fillWidth: true
                    implicitHeight: 26
                    visible: page.selectedKind === "collection" || page.selectedKind === "folder"
                    text: page.selectedName
                    placeholderText: page.selectedKind === "folder" ? "Folder name" : "Collection name"
                    placeholderTextColor: theme.textFaint
                    color: theme.text
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    leftPadding: 0
                    rightPadding: 0
                    background: null
                    enabled: page.selectedId !== ""
                    onEditingFinished: {
                        if (page.selectedId && text && text !== page.selectedName) {
                            apiTester.renameNode(page.selectedId, text)
                            page.selectedName = text
                            page.say("API — renamed")
                        }
                    }
                }

                // Request name
                TextField {
                    Layout.fillWidth: true
                    implicitHeight: 26
                    visible: page.selectedKind === "request"
                    text: page.draft.name || ""
                    placeholderText: "Request name"
                    placeholderTextColor: theme.textFaint
                    color: theme.textSoft
                    font.pixelSize: 12
                    leftPadding: 0
                    rightPadding: 0
                    background: null
                    onEditingFinished: {
                        if (page.selectedId && text) {
                            apiTester.updateRequest(page.selectedId, { name: text })
                            page.draft = Object.assign({}, page.draft, { name: text })
                            page.selectedName = text
                        }
                    }
                }

                Item { Layout.fillWidth: true; visible: page.selectedKind === "request" || page.selectedKind === "collection" || page.selectedKind === "folder" }

                // Collection/folder actions
                PillButton {
                    visible: (page.selectedKind === "collection" || page.selectedKind === "folder") && page.selectedId !== ""
                    text: "Delete"
                    iconName: "trash"
                    onClicked: {
                        menuId = page.selectedId
                        menuKind = page.selectedKind
                        menuName = page.selectedName
                        deleteDialog.open()
                    }
                }

                PillButton {
                    visible: page.selectedKind === "request"
                    text: "Save"
                    iconName: "check"
                    checked: page.dirty
                    enabled: page.dirty
                    onClicked: {
                        apiTester.updateRequest(page.selectedId, page.draft)
                        page.dirty = false
                        page.say("API — request saved")
                    }
                }
                PillButton {
                    visible: page.selectedKind === "request"
                    text: "Duplicate"
                    onClicked: page.duplicateRequestNow()
                }
                PillButton {
                    visible: page.selectedKind === "request"
                    text: "Save as…"
                    onClicked: { pickDialog.mode = "saveas"; pickDialog.open() }
                }
                PillButton {
                    visible: page.selectedKind === "request"
                    text: "Delete"
                    iconName: "trash"
                    onClicked: {
                        menuId = page.selectedId
                        menuKind = "request"
                        menuName = page.draft.name || ""
                        deleteDialog.open()
                    }
                }
            }

            // 3) builder tabs
            TabBar {
                visible: page.sidebarTab !== "ports"
                Layout.fillWidth: true
                Layout.topMargin: 4
                model: [["params", "Params"], ["headers", "Headers"], ["body", "Body"],
                        ["auth", "Auth"], ["scripts", "Scripts"], ["settings", "Settings"]]
                value: page.detailTab
                onPicked: (v) => page.detailTab = v
            }
            Rectangle {
                visible: page.sidebarTab !== "ports"
                Layout.fillWidth: true
                height: 1
                color: theme.hairline
            }

            // 4) builder body
            StackLayout {
                visible: page.sidebarTab !== "ports"
                Layout.fillWidth: true
                Layout.preferredHeight: 132
                Layout.topMargin: 8
                Layout.bottomMargin: 8
                currentIndex: {
                    const t = page.detailTab
                    return t === "params" ? 0 : t === "headers" ? 1 : t === "body" ? 2
                         : t === "auth" ? 3 : t === "scripts" ? 4 : 5
                }

                Component {
                    id: kvEditor
                    ColumnLayout {
                        property string listName
                        spacing: 6

                        Text {
                            visible: (page.draft[listName] || []).length === 0
                            Layout.fillWidth: true
                            color: theme.textFaint
                            font.pixelSize: 12
                            text: listName === "query"
                                  ? "Query parameters are appended to the URL."
                                  : "Request headers, one per line."
                        }

                        ListView {
                            id: kvList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 4
                            boundsBehavior: Flickable.StopAtBounds
                            model: page.draft[listName]
                            ScrollBar.vertical: ScrollBar { padding: 2 }

                            delegate: RowLayout {
                                required property int index
                                required property var modelData
                                width: kvList.width
                                height: 30
                                spacing: 6

                                Rectangle {
                                    width: 16; height: 16; radius: 4
                                    color: modelData.enabled ? Qt.alpha(theme.accent, 0.15) : "transparent"
                                    border.width: 1
                                    border.color: modelData.enabled ? Qt.alpha(theme.accent, 0.5) : theme.hairline
                                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                                    TapHandler { onTapped: page.kvSet(listName, index, "enabled") }
                                    Text {
                                        visible: modelData.enabled
                                        anchors.centerIn: parent
                                        text: "✓"
                                        color: theme.accent
                                        font.pixelSize: 10
                                        font.weight: Font.Bold
                                    }
                                }

                                TextField {
                                    Layout.fillWidth: true
                                    implicitHeight: 28
                                    text: modelData.key
                                    font.family: "monospace"
                                    font.pixelSize: 12
                                    placeholderText: "Key"
                                    placeholderTextColor: theme.textFaint
                                    color: modelData.enabled ? theme.text : theme.textDim
                                    leftPadding: 8; rightPadding: 8
                                    background: Rectangle {
                                        radius: 6
                                        color: theme.well
                                        border.width: 1
                                        border.color: parent.activeFocus ? Qt.alpha(theme.accent, 0.4) : theme.hairline
                                    }
                                    onEditingFinished: page.kvSet(listName, index, "key", text)
                                }
                                TextField {
                                    Layout.fillWidth: true
                                    implicitHeight: 28
                                    text: modelData.value
                                    font.family: "monospace"
                                    font.pixelSize: 12
                                    placeholderText: "Value  ·  {{vars}} ok"
                                    placeholderTextColor: theme.textFaint
                                    color: modelData.enabled ? theme.textSoft : theme.textDim
                                    leftPadding: 8; rightPadding: 8
                                    background: Rectangle {
                                        radius: 6
                                        color: theme.well
                                        border.width: 1
                                        border.color: parent.activeFocus ? Qt.alpha(theme.accent, 0.4) : theme.hairline
                                    }
                                    onEditingFinished: page.kvSet(listName, index, "value", text)
                                }
                                IconButton {
                                    iconName: "close"
                                    Accessible.name: "Remove row"
                                    opacity: 0.7
                                    onClicked: page.kvRemove(listName, index)
                                }
                            }
                        }

                        PillButton {
                            text: "Add row"
                            iconName: "plus"
                            onClicked: page.kvAdd(listName)
                        }
                    }
                }

                Loader {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    sourceComponent: kvEditor
                    onLoaded: item.listName = "query"
                }
                Loader {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    sourceComponent: kvEditor
                    onLoaded: item.listName = "headers"
                }

                // Body
                ColumnLayout {
                    spacing: 6
                    RowLayout {
                        spacing: 4
                        Repeater {
                            model: [["none", "None"], ["json", "JSON"], ["raw", "Raw"], ["form", "Form"]]
                            delegate: PillButton {
                                required property var modelData
                                text: modelData[1]
                                checked: page.draft.bodyMode === modelData[0]
                                onClicked: page.setDraft({ bodyMode: modelData[0] })
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: page.draft.bodyMode !== "none"
                        radius: 8
                        color: theme.well
                        border.width: 1
                        border.color: theme.hairline
                        ScrollView {
                            anchors.fill: parent
                            anchors.margins: 8
                            contentWidth: availableWidth
                            TextArea {
                                width: parent.availableWidth
                                text: page.draft.body
                                wrapMode: TextArea.Wrap
                                color: theme.textSoft
                                font.family: "monospace"
                                font.pixelSize: 12
                                placeholderText: page.draft.bodyMode === "json" ? '{ "key": "value" }' : "Request body…"
                                placeholderTextColor: theme.textFaint
                                background: null
                                padding: 0
                                onTextChanged: {
                                    if (page.draft.body !== text)
                                        page.setDraft({ body: text })
                                }
                            }
                        }
                    }
                    Text {
                        visible: page.draft.bodyMode === "none"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        verticalAlignment: Text.AlignVCenter
                        color: theme.textFaint
                        font.pixelSize: 12
                        text: "No body for this request."
                    }
                }

                // Auth
                ColumnLayout {
                    spacing: 8
                    RowLayout {
                        spacing: 6
                        Repeater {
                            model: [["none", "None"], ["bearer", "Bearer"], ["basic", "Basic"]]
                            delegate: PillButton {
                                required property var modelData
                                text: modelData[1]
                                checked: page.draft.authType === modelData[0]
                                onClicked: page.setDraft({ authType: modelData[0] })
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }
                    TextField {
                        Layout.fillWidth: true
                        visible: page.draft.authType === "bearer"
                        implicitHeight: 32
                        text: page.draft.authToken
                        placeholderText: "Token —  {{relayKey}} works here"
                        placeholderTextColor: theme.textFaint
                        color: theme.text
                        font.family: "monospace"
                        font.pixelSize: 12
                        leftPadding: 10; rightPadding: 10
                        background: Rectangle {
                            radius: 8
                            color: theme.well
                            border.width: 1
                            border.color: parent.activeFocus ? Qt.alpha(theme.accent, 0.45) : theme.hairline
                        }
                        onEditingFinished: page.setDraft({ authToken: text })
                    }
                    RowLayout {
                        visible: page.draft.authType === "basic"
                        spacing: 6
                        TextField {
                            Layout.fillWidth: true
                            implicitHeight: 32
                            text: page.draft.authUser
                            placeholderText: "Username"
                            placeholderTextColor: theme.textFaint
                            color: theme.text
                            leftPadding: 10
                            background: Rectangle { radius: 8; color: theme.well; border.color: theme.hairline; border.width: 1 }
                            onEditingFinished: page.setDraft({ authUser: text })
                        }
                        TextField {
                            Layout.fillWidth: true
                            implicitHeight: 32
                            text: page.draft.authPassword
                            placeholderText: "Password"
                            placeholderTextColor: theme.textFaint
                            color: theme.text
                            echoMode: TextInput.Password
                            leftPadding: 10
                            background: Rectangle { radius: 8; color: theme.well; border.color: theme.hairline; border.width: 1 }
                            onEditingFinished: page.setDraft({ authPassword: text })
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: page.draft.authType === "none"
                        color: theme.textFaint
                        font.pixelSize: 12
                        text: "No Authorization header is sent."
                    }
                }

                // Scripts — two stacked editors
                ColumnLayout {
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Text { text: "Pre-request"; color: theme.textDim; font.pixelSize: 11; font.weight: Font.Medium; Layout.preferredWidth: 78 }
                        TextField {
                            Layout.fillWidth: true
                            implicitHeight: 52
                            text: page.draft.preScript
                            wrapMode: TextArea.Wrap
                            font.family: "monospace"
                            font.pixelSize: 11
                            placeholderText: 'env.set("token", "…")'
                            placeholderTextColor: theme.textFaint
                            color: theme.textSoft
                            leftPadding: 8; rightPadding: 8
                            background: Rectangle { radius: 6; color: theme.well; border.color: theme.hairline; border.width: 1 }
                            onEditingFinished: page.setDraft({ preScript: text })
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Text { text: "Tests"; color: theme.textDim; font.pixelSize: 11; font.weight: Font.Medium; Layout.preferredWidth: 78 }
                        TextField {
                            Layout.fillWidth: true
                            implicitHeight: 52
                            text: page.draft.testScript
                            wrapMode: TextArea.Wrap
                            font.family: "monospace"
                            font.pixelSize: 11
                            placeholderText: 'test("status", () => expect(response.status).toBe(200))'
                            placeholderTextColor: theme.textFaint
                            color: theme.textSoft
                            leftPadding: 8; rightPadding: 8
                            background: Rectangle { radius: 6; color: theme.well; border.color: theme.hairline; border.width: 1 }
                            onEditingFinished: page.setDraft({ testScript: text })
                        }
                    }
                }

                // Settings
                ColumnLayout {
                    spacing: 10
                    RowLayout {
                        spacing: 16
                        Text { text: "Timeout"; color: theme.textDim; font.pixelSize: 12 }
                        TextField {
                            implicitWidth: 84
                            implicitHeight: 28
                            text: String(apiTester.timeoutMs)
                            font.family: "monospace"
                            font.pixelSize: 12
                            color: theme.text
                            leftPadding: 8
                            background: Rectangle { radius: 6; color: theme.well; border.color: theme.hairline; border.width: 1 }
                            onEditingFinished: apiTester.timeoutMs = parseInt(text) || 30000
                        }
                        Text { text: "ms"; color: theme.textFaint; font.pixelSize: 11 }
                    }
                    RowLayout {
                        spacing: 18
                        SettingToggle {
                            Layout.preferredWidth: 200
                            title: "Follow redirects"
                            checked: apiTester.followRedirects
                            onToggled: apiTester.followRedirects = !apiTester.followRedirects
                        }
                        SettingToggle {
                            Layout.preferredWidth: 180
                            title: "Pretty JSON"
                            checked: apiTester.prettyJson
                            onToggled: apiTester.prettyJson = !apiTester.prettyJson
                        }
                        SettingToggle {
                            Layout.preferredWidth: 160
                            title: "Keep history"
                            checked: apiTester.historyEnabled
                            onToggled: apiTester.historyEnabled = !apiTester.historyEnabled
                        }
                    }
                }
            }

            // 5) response — same surface, separated by a rule (no nested card)
            Rectangle {
                visible: page.sidebarTab !== "ports"
                Layout.fillWidth: true
                height: 1
                color: theme.hairline
                Layout.topMargin: 2
                Layout.bottomMargin: 6
            }

            ColumnLayout {
                visible: page.sidebarTab !== "ports"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 150
                spacing: 6

                // response header
                RowLayout {
                    id: respHdr
                    Layout.fillWidth: true
                    spacing: 8

                    readonly property int code: page.resp.status || 0
                    readonly property bool hasResp: (page.resp.status !== undefined && page.resp.status !== null)
                                                    || !!(page.resp.error && page.resp.error.length > 0)
                    readonly property int pass: {
                        let p = 0
                        for (const x of (page.resp.tests || []))
                            if (x.passed) ++p
                        return p
                    }
                    readonly property int total: (page.resp.tests || []).length

                    // status pill
                    Rectangle {
                        visible: respHdr.hasResp && !page.resp.error
                        implicitWidth: statusLbl.implicitWidth + 14
                        implicitHeight: 22
                        radius: 5
                        color: respHdr.code >= 200 && respHdr.code < 300 ? Qt.alpha(theme.success, 0.14)
                             : Qt.alpha(theme.danger, 0.14)
                        border.width: 1
                        border.color: respHdr.code >= 200 && respHdr.code < 300 ? Qt.alpha(theme.success, 0.4)
                                     : Qt.alpha(theme.danger, 0.4)
                        Text {
                            id: statusLbl
                            anchors.centerIn: parent
                            text: respHdr.code + (page.resp.statusText ? " " + page.resp.statusText : "")
                            color: respHdr.code >= 200 && respHdr.code < 300 ? theme.success : theme.danger
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }
                    }

                    Text {
                        visible: !!(page.resp.error && page.resp.error.length > 0)
                        Layout.fillWidth: true
                        text: page.resp.error || ""
                        color: theme.danger
                        font.pixelSize: 12
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                    }

                    Text {
                        visible: respHdr.hasResp && !page.resp.error
                        text: (page.resp.durationMs || 0) + " ms"
                        color: theme.textDim
                        font.pixelSize: 11
                        font.family: "monospace"
                    }

                    // tests badge
                    Rectangle {
                        visible: respHdr.total > 0
                        implicitWidth: testLbl.implicitWidth + 12
                        implicitHeight: 20
                        radius: 4
                        color: respHdr.pass === respHdr.total ? Qt.alpha(theme.success, 0.12) : Qt.alpha(theme.danger, 0.12)
                        Text {
                            id: testLbl
                            anchors.centerIn: parent
                            text: "✓ " + respHdr.pass + "/" + respHdr.total
                            color: respHdr.pass === respHdr.total ? theme.success : theme.danger
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }
                    }

                    Item { Layout.fillWidth: true; visible: respHdr.hasResp }

                    TabBar {
                        visible: respHdr.hasResp
                        model: [["body", "Body"], ["headers", "Headers"], ["tests", "Tests"]]
                        value: page.responseTab
                        onPicked: (v) => page.responseTab = v
                    }

                    IconButton {
                        visible: respHdr.hasResp
                        iconName: "copy"
                        Accessible.name: "Copy response"
                        enabled: (page.resp.bodyPretty || "").length > 0
                        onClicked: {
                            shell.copyText(page.resp.bodyPretty || "")
                            page.say("API — response copied")
                        }
                    }
                }

                // empty state
                Text {
                    visible: !respHdr.hasResp
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    wrapMode: Text.Wrap
                    color: theme.textFaint
                    font.pixelSize: 13
                    text: "No response yet.\nChoose a request on the left, or type a URL and press Send."
                }

                // body / headers / tests
                StackLayout {
                    visible: respHdr.hasResp
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: page.responseTab === "body" ? 0 : page.responseTab === "headers" ? 1 : 2

                    ScrollView {
                        contentWidth: availableWidth
                        clip: true
                        Text {
                            width: parent.availableWidth
                            text: page.resp.bodyPretty || page.resp.body || ""
                            color: theme.textSoft
                            font.family: "monospace"
                            font.pixelSize: 12
                            wrapMode: Text.WrapAnywhere
                        }
                    }

                    ListView {
                        id: resHeaders
                        clip: true
                        model: page.resp.headers || []
                        spacing: 3
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar { padding: 2 }
                        delegate: RowLayout {
                            required property var modelData
                            width: resHeaders.width
                            height: 16
                            Text {
                                text: modelData.key
                                color: theme.textDim
                                font.family: "monospace"
                                font.pixelSize: 11
                                Layout.preferredWidth: Math.min(180, implicitWidth + 4)
                                elide: Text.ElideRight
                            }
                            Text {
                                text: ":"
                                color: theme.textFaint
                                font.family: "monospace"
                                font.pixelSize: 11
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.value
                                color: theme.textSoft
                                font.family: "monospace"
                                font.pixelSize: 11
                                elide: Text.ElideRight
                            }
                        }
                    }

                    ListView {
                        id: resTests
                        clip: true
                        model: page.resp.tests || []
                        spacing: 4
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar { padding: 2 }
                        delegate: RowLayout {
                            required property var modelData
                            width: resTests.width
                            height: 20
                            spacing: 8
                            Text {
                                text: modelData.passed ? "✓" : "✗"
                                color: modelData.passed ? theme.success : theme.danger
                                font.pixelSize: 13
                                font.weight: Font.Bold
                                Layout.preferredWidth: 14
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.name + (modelData.message ? "  ·  " + modelData.message : "")
                                color: modelData.message && !modelData.passed ? theme.danger : theme.textSoft
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                        }
                        Text {
                            visible: (page.resp.tests || []).length === 0
                            anchors.centerIn: parent
                            text: "No assertions ran.\nAdd a Tests script on this request."
                            horizontalAlignment: Text.AlignHCenter
                            color: theme.textFaint
                            font.pixelSize: 12
                        }
                    }
                }

                // script console strip
                Rectangle {
                    visible: (page.resp.scriptLog || []).length > 0
                    Layout.fillWidth: true
                    implicitHeight: Math.min(scriptLogTxt.lineCount, 3) * 14 + 12
                    radius: 6
                    color: theme.well
                    border.width: 1
                    border.color: theme.hairline
                    Text {
                        id: scriptLogTxt
                        anchors { fill: parent; margins: 6 }
                        text: (page.resp.scriptLog || []).join("  ·  ")
                        color: theme.textDim
                        font.family: "monospace"
                        font.pixelSize: 11
                        wrapMode: Text.WrapAnywhere
                        maximumLineCount: 3
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }

    // ================= menus (unchanged behaviour) =================
    // right-click on a collection / folder / request
    DarkMenu {
        id: nodeMenu
        NodeItem {
            visible: page.menuKind === "request"
            height: visible ? implicitHeight : 0
            text: "Open"
            onTriggered: page.loadRequest(page.menuId)
        }
        NodeItem {
            visible: page.menuKind === "collection" || page.menuKind === "folder"
            height: visible ? implicitHeight : 0
            text: "New request"
            onTriggered: page.newRequestIn(page.menuId)
        }
        NodeItem {
            visible: page.menuKind === "collection" || page.menuKind === "folder"
            height: visible ? implicitHeight : 0
            text: "New folder…"
            onTriggered: page.askName("folder", "New folder")
        }
        NodeSep {}
        NodeItem {
            text: "Rename…"
            onTriggered: page.askName("rename", page.menuName)
        }
        NodeItem {
            text: "Duplicate"
            onTriggered: {
                const copy = apiTester.duplicateNode(page.menuId)
                if (copy && page.menuKind === "request")
                    page.loadRequest(copy)
                page.say("API — duplicated")
            }
        }
        NodeItem {
            visible: page.menuKind !== "collection"
            height: visible ? implicitHeight : 0
            text: "Move to…"
            onTriggered: { pickDialog.mode = "move"; pickDialog.open() }
        }
        NodeItem {
            visible: page.menuKind === "collection"
            height: visible ? implicitHeight : 0
            text: "Export to clipboard"
            onTriggered: {
                const json = apiTester.exportPostmanCollection(page.menuId) || apiTester.exportNativeCollection(page.menuId)
                if (json) { shell.copyText(json); page.say("API — exported to clipboard") }
            }
        }
        NodeSep {}
        NodeItem {
            text: "Delete…"
            onTriggered: deleteDialog.open()
        }
    }

    DarkMenu {
        id: methodMenu
        NodeItem { text: "GET"; onTriggered: page.setDraft({ method: "GET" }) }
        NodeItem { text: "POST"; onTriggered: page.setDraft({ method: "POST" }) }
        NodeItem { text: "PUT"; onTriggered: page.setDraft({ method: "PUT" }) }
        NodeItem { text: "PATCH"; onTriggered: page.setDraft({ method: "PATCH" }) }
        NodeItem { text: "DELETE"; onTriggered: page.setDraft({ method: "DELETE" }) }
    }

    DarkMenu {
        id: envMenu
        Instantiator {
            model: apiTester.environments
            delegate: NodeItem {
                required property var modelData
                text: modelData.name
                checkable: true
                checked: modelData.id === apiTester.activeEnvId
                onTriggered: apiTester.activeEnvId = modelData.id
            }
            onObjectAdded: (index, object) => envMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => envMenu.removeItem(object)
        }
        NodeSep {}
        NodeItem {
            text: apiTester.environments.length === 0 ? "New environment…" : "Manage environments…"
            onTriggered: envEditor.open()
        }
    }

    DarkMenu {
        id: importMenu
        NodeItem {
            text: "Paste JSON to import…"
            onTriggered: {
                importArea.text = ""
                importDialog.mode = "collection"
                importDialog.open()
            }
        }
    }

    DarkMenu {
        id: exportMenu
        NodeItem {
            text: "Export selected → clipboard"
            enabled: page.selectedId !== ""
            onTriggered: {
                const json = apiTester.exportPostmanCollection(page.selectedId) || apiTester.exportNativeCollection(page.selectedId)
                if (json) {
                    shell.copyText(json)
                    page.say("API — exported to clipboard")
                } else {
                    page.say("API — nothing to export")
                }
            }
        }
        NodeItem {
            text: "Export native JSON → clipboard"
            enabled: page.selectedId !== ""
            onTriggered: {
                const json = apiTester.exportNativeCollection(page.selectedId)
                if (json) {
                    shell.copyText(json)
                    page.say("API — exported to clipboard")
                }
            }
        }
        NodeSep {}
        NodeItem {
            text: "Export environment → clipboard"
            onTriggered: {
                const json = apiTester.exportEnvironment(apiTester.activeEnvId)
                if (json) {
                    shell.copyText(json)
                    page.say("API — environment exported")
                }
            }
        }
        NodeItem {
            text: "Paste environment JSON…"
            onTriggered: {
                importArea.text = ""
                importDialog.mode = "environment"
                importDialog.open()
            }
        }
        NodeSep {}
        NodeItem {
            text: "Clear history"
            enabled: page.sidebarTab === "history" || true
            onTriggered: {
                apiTester.clearHistory()
                page.say("API — history cleared")
            }
        }
    }

    // ---- rename (collection / folder / request) ----
    Popup {
        id: renameDialog
        modal: true
        dim: false
        Overlay.modal: Item {}
        anchors.centerIn: Overlay.overlay
        width: Math.min(parent ? parent.width - 40 : 400, 420)
        implicitHeight: renameCol.implicitHeight + 28
        padding: 14
        background: Rectangle {
            radius: theme.radiusMd
            color: theme.card
            border.width: 1
            border.color: theme.outline
        }
        ColumnLayout {
            id: renameCol
            anchors.fill: parent
            spacing: 10
            Text {
                text: page.dialogPurpose === "collection" ? "New collection"
                      : page.dialogPurpose === "folder" ? "New folder"
                      : page.menuKind === "collection" ? "Rename collection"
                      : page.menuKind === "folder" ? "Rename folder" : "Rename request"
                color: theme.text
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            TextField {
                id: renameArea
                Layout.fillWidth: true
                implicitHeight: 32
                color: theme.text
                selectionColor: theme.accent
                font.pixelSize: 13
                leftPadding: 10
                rightPadding: 10
                background: Rectangle {
                    radius: 8
                    color: theme.well
                    border.width: 1
                    border.color: renameArea.activeFocus ? Qt.alpha(theme.accent, 0.5) : theme.hairline
                }
                onAccepted: renameBtn.clicked()
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8
                PillButton { text: "Cancel"; onClicked: renameDialog.close() }
                PillButton {
                    id: renameBtn
                    text: page.dialogPurpose === "rename" ? "Rename" : "Create"
                    checked: true
                    enabled: renameArea.text.trim().length > 0
                    onClicked: {
                        const newName = renameArea.text.trim()
                        if (!newName)
                            return
                        if (page.dialogPurpose === "collection") {
                            const id = apiTester.newCollection(newName)
                            page.selectNode(id, "collection", newName)
                        } else if (page.dialogPurpose === "folder") {
                            const id = apiTester.addFolder(page.menuId, newName)
                            page.unfold(page.menuId)
                            if (id) page.selectNode(id, "folder", newName)
                        } else {
                            apiTester.renameNode(page.menuId, newName)
                            if (page.selectedId === page.menuId) {
                                page.selectedName = newName
                                if (page.selectedKind === "request")
                                    page.draft = Object.assign({}, page.draft, { name: newName })
                            }
                            page.say("API — renamed to “" + newName + "”")
                        }
                        renameDialog.close()
                    }
                }
            }
        }
    }

    // ---- delete confirm ----
    Popup {
        id: deleteDialog
        modal: true
        dim: false
        Overlay.modal: Item {}
        anchors.centerIn: Overlay.overlay
        width: Math.min(parent ? parent.width - 40 : 400, 420)
        implicitHeight: delCol.implicitHeight + 28
        padding: 14
        background: Rectangle {
            radius: theme.radiusMd
            color: theme.card
            border.width: 1
            border.color: theme.outline
        }
        ColumnLayout {
            id: delCol
            anchors.fill: parent
            spacing: 10
            Text {
                Layout.fillWidth: true
                text: page.menuKind === "collection" ? "Delete this collection?"
                            : page.menuKind === "folder" ? "Delete this folder and everything in it?"
                            : "Delete this request?"
                color: theme.text
                font.pixelSize: 13
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
            }
            Text {
                Layout.fillWidth: true
                color: theme.textDim
                font.pixelSize: 12
                wrapMode: Text.Wrap
                text: "“" + page.menuName + "”"
                      + (page.menuKind === "collection" || page.menuKind === "folder"
                         ? " and its contents will be removed. This cannot be undone." : " will be removed.")
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8
                PillButton { text: "Cancel"; onClicked: deleteDialog.close() }
                PillButton {
                    text: "Delete"
                    checked: true
                    onClicked: {
                        page.deleteMenuTarget()
                        deleteDialog.close()
                    }
                }
            }
        }
    }

    ApiEnvEditor { id: envEditor }

    // ---- where to: Move to… and Save as… ----
    Popup {
        id: pickDialog
        property string mode: "move" // move | saveas
        property string chosen: ""
        modal: true
        dim: false
        Overlay.modal: Item {}
        anchors.centerIn: Overlay.overlay
        width: Math.min(parent ? parent.width - 40 : 420, 440)
        height: 340
        padding: 14
        onOpened: {
            chosen = ""
            if (apiTester.collections.length === 0) // nothing to put it in yet: a collection is made on the way
                chosen = ""
        }
        background: Rectangle { radius: theme.radiusMd; color: theme.card; border.width: 1; border.color: theme.outline }
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Text {
                text: pickDialog.mode === "move" ? "Move “" + page.menuName + "” to" : "Save this request in"
                color: theme.text
                font.pixelSize: 13
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            ListView {
                id: pickList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 1
                boundsBehavior: Flickable.StopAtBounds
                model: apiTester.collections.filter(r => r.kind !== "request")
                ScrollBar.vertical: ScrollBar { padding: 2 }
                Text {
                    visible: pickList.count === 0
                    anchors.centerIn: parent
                    color: theme.textDim
                    font.pixelSize: 12
                    text: "No collection or folder yet."
                }
                delegate: Rectangle {
                    required property var modelData
                    width: pickList.width
                    height: 28
                    radius: theme.radiusSm
                    color: pickDialog.chosen === modelData.id ? Qt.alpha(theme.accent, 0.16) : ph.hovered ? theme.hoverFill : "transparent"
                    HoverHandler { id: ph; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: pickDialog.chosen = modelData.id }
                    RowLayout {
                        anchors { fill: parent; leftMargin: 8 + (modelData.depth || 0) * 14; rightMargin: 8 }
                        spacing: 7
                        LineIcon { glyph: "folder"; sourceSize: Qt.size(12, 12); color: modelData.kind === "collection" ? theme.accent : theme.textFaint }
                        Text { Layout.fillWidth: true; text: modelData.name; textFormat: Text.PlainText; elide: Text.ElideRight; color: theme.text; font.pixelSize: 12 }
                    }
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8
                PillButton { text: "Cancel"; onClicked: pickDialog.close() }
                PillButton {
                    text: pickDialog.mode === "move" ? "Move" : "Save"
                    checked: true
                    enabled: pickDialog.chosen !== ""
                    onClicked: {
                        if (pickDialog.mode === "move") {
                            if (apiTester.moveNode(page.menuId, pickDialog.chosen)) {
                                page.unfold(pickDialog.chosen)
                                page.say("API — moved")
                            } else {
                                page.say("API — cannot move it there")
                            }
                        } else {
                            const id = apiTester.saveRequestAs(page.draft, pickDialog.chosen, (page.draft.name || "New request") + " copy")
                            if (id) {
                                page.unfold(pickDialog.chosen)
                                page.loadRequest(id)
                                page.say("API — saved a copy")
                            }
                        }
                        pickDialog.close()
                    }
                }
            }
        }
    }

    Popup {
        id: importDialog
        property string mode: "collection"
        modal: true
        dim: false
        Overlay.modal: Item {}
        anchors.centerIn: Overlay.overlay
        width: Math.min(parent ? parent.width - 40 : 520, 560)
        height: 300
        padding: 14
        background: Rectangle {
            radius: theme.radiusMd
            color: theme.card
            border.width: 1
            border.color: theme.outline
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Text {
                text: importDialog.mode === "environment" ? "Paste a Postman Environment" : "Paste a Postman Collection or native JSON"
                color: theme.text
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 8
                color: theme.well
                border.width: 1
                border.color: theme.hairline
                ScrollView {
                    anchors.fill: parent
                    anchors.margins: 8
                    contentWidth: availableWidth
                    TextArea {
                        id: importArea
                        width: parent.availableWidth
                        wrapMode: TextArea.Wrap
                        font.family: "monospace"
                        font.pixelSize: 11
                        color: theme.text
                        placeholderText: "{ … }"
                        placeholderTextColor: theme.textFaint
                        background: null
                        padding: 0
                    }
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8
                PillButton { text: "Cancel"; onClicked: importDialog.close() }
                PillButton {
                    text: "Import"
                    checked: true
                    enabled: importArea.text.trim().length > 0
                    onClicked: {
                        let err = ""
                        if (importDialog.mode === "environment")
                            err = apiTester.importEnvironment(importArea.text)
                        else {
                            err = apiTester.importPostmanCollection(importArea.text)
                            if (err && err.indexOf("Not a Postman") === 0)
                                err = apiTester.importNativeCollection(importArea.text)
                        }
                        page.say(err && err.indexOf("Imported") === 0 ? err : "API — " + (err || "import failed"))
                        if (err && err.indexOf("Imported") === 0)
                            importDialog.close()
                    }
                }
            }
        }
    }
}
