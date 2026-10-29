import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Database tab: connections on the left; form when offline, browse/query when connected.
ColumnLayout {
    id: tab
    required property var shell
    Layout.fillWidth: true
    Layout.fillHeight: true
    spacing: 0

    readonly property bool modalOpen: deleteDialog.visible
    property string selectedId: ""
    property var draft: ({})
    property bool editing: false
    property string menuId: ""
    property string browseSchema: ""
    property string browseTable: ""
    property string detailTab: "tables"

    function blankDraft() {
        return {
            id: "", name: "", driver: "sqlite", host: "127.0.0.1", port: 3306,
            user: "", database: "", sqlitePath: "", savePassword: false,
            password: "", hasPassword: false
        }
    }
    function say(msg) {
        shell.infoText = msg
        shell.restartInfo()
    }
    function select(id) {
        selectedId = id
        draft = id ? dbManager.connectionById(id) : blankDraft()
        draft.password = ""
        editing = !!id
        browseTable = ""
        browseSchema = (draft && draft.database) || ""
        if (id) {
            say("DB — connecting…")
            dbManager.connectTo(id)
        } else {
            dbManager.disconnectDb()
        }
    }
    function colsOf(r) { return (r && r.columns) ? r.columns : [] }
    function rowsOf(r) { return (r && r.rows) ? r.rows : [] }
    function driversHas(ui) { return dbManager.drivers.indexOf(ui) >= 0 }
    function cellText(row, col) {
        if (!row) return "NULL"
        const v = row[col]
        return (v === null || v === undefined) ? "NULL" : String(v)
    }
    function save() {
        const fields = {
            name: draft.name, driver: draft.driver, host: draft.host,
            port: Number(draft.port) || (draft.driver === "postgres" ? 5432 : 3306),
            user: draft.user, database: draft.database, sqlitePath: draft.sqlitePath,
            savePassword: !!draft.savePassword
        }
        if (draft.password && draft.password.length > 0)
            fields.password = draft.password
        if (editing) {
            if (dbManager.updateConnection(selectedId, fields)) {
                say("DB — saved")
                dbManager.connectTo(selectedId)
            }
        } else {
            const id = dbManager.addConnection(fields)
            if (id) {
                select(id)
                say("DB — added")
            }
        }
    }

    Component.onCompleted: {
        dbManager.statusMessage.connect(msg => tab.say(msg))
        select("")
    }

    Connections {
        target: dbManager
        function onConnectedChanged() {
            if (dbManager.connected) {
                dbModel.reload()
                if (draft.database && draft.driver !== "sqlite") {
                    tab.browseSchema = draft.database
                    dbManager.useDatabase(draft.database)
                } else if (dbModel.count > 0) {
                    tab.browseSchema = dbModel.get(0).name
                    dbManager.useDatabase(tab.browseSchema)
                } else if (draft.driver === "sqlite") {
                    tab.browseSchema = "main"
                }
                tableModel.reload()
            } else {
                dbModel.clear()
                tableModel.clear()
                tab.browseTable = ""
            }
        }
        function onResultReady() {
            if (dbManager.connected && tab.browseSchema !== "")
                tableModel.reload()
        }
    }

    ListModel {
        id: dbModel
        function reload() {
            clear()
            const list = dbManager.schemas()
            for (let i = 0; i < list.length; ++i)
                append({ name: list[i].name })
        }
    }
    ListModel {
        id: tableModel
        function reload() {
            clear()
            const list = dbManager.tables(browseSchema)
            for (let i = 0; i < list.length; ++i)
                append({ name: list[i].name, schema: list[i].schema })
        }
    }

    component Field: ColumnLayout {
        id: fl
        property string label: ""
        property string placeholder: ""
        property string value: ""
        property bool mono: false
        property bool show: true
        property int echoMode: TextInput.Normal
        signal edited(string text)
        visible: show
        Layout.fillWidth: true
        spacing: 4
        Text {
            text: fl.label
            color: theme.textDim
            font.pixelSize: 11
            font.weight: Font.Medium
        }
        TextField {
            id: tf
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            text: fl.value
            placeholderText: fl.placeholder
            placeholderTextColor: theme.textFaint
            color: theme.text
            echoMode: fl.echoMode
            font.family: fl.mono ? "monospace" : ""
            font.pixelSize: 12
            leftPadding: 10
            rightPadding: 10
            background: Rectangle {
                radius: 8
                color: theme.well
                border.width: 1
                border.color: tf.activeFocus ? Qt.alpha(theme.accent, 0.45) : theme.hairline
            }
            onTextEdited: fl.edited(text)
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: 12

        // ---- sidebar: connections ----
        ColumnLayout {
            Layout.preferredWidth: 180
            Layout.maximumWidth: 200
            Layout.fillHeight: true
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 28
                Text {
                    text: "Connections"
                    color: theme.text
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    Layout.alignment: Qt.AlignVCenter
                }
                Item { Layout.fillWidth: true }
                IconButton {
                    iconName: "plus"
                    Accessible.name: "New database connection"
                    onClicked: {
                        selectedId = ""
                        draft = tab.blankDraft()
                        editing = false
                        dbManager.disconnectDb()
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: theme.well
                border.width: 1
                border.color: theme.hairline
                radius: theme.radiusMd

                ListView {
                    id: connList
                    anchors.fill: parent
                    anchors.margins: 4
                    clip: true
                    model: dbManager.connections
                    spacing: 2

                    delegate: Rectangle {
                        id: connRow
                        required property var modelData
                        readonly property bool sel: modelData.id === tab.selectedId
                        width: connList.width
                        height: 44
                        radius: 6
                        color: sel ? Qt.alpha(theme.accent, 0.16) : rowHov.hovered ? Qt.alpha(theme.text, 0.06) : "transparent"
                        HoverHandler { id: rowHov }
                        TapHandler { onTapped: { tab.select(connRow.modelData.id); ctx.close() } }
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: {
                                menuId = connRow.modelData.id
                                tab.select(connRow.modelData.id)
                                ctx.popup()
                            }
                        }
                        RowLayout {
                            anchors {
                                left: parent.left; right: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 10; rightMargin: 8
                            }
                            spacing: 6
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    Layout.fillWidth: true
                                    text: connRow.modelData.name
                                    color: theme.text
                                    font.pixelSize: 12
                                    font.weight: Font.Medium
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: connRow.modelData.label
                                    color: theme.textFaint
                                    font.family: "monospace"
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                }
                            }
                            Rectangle {
                                radius: 4
                                color: Qt.alpha(connRow.modelData.driverAvailable ? theme.success : theme.danger, 0.15)
                                implicitWidth: dChip.implicitWidth + 8
                                implicitHeight: 16
                                Text {
                                    id: dChip
                                    anchors.centerIn: parent
                                    text: connRow.modelData.driver
                                    color: connRow.modelData.driverAvailable ? theme.success : theme.danger
                                    font.pixelSize: 9
                                    font.weight: Font.DemiBold
                                }
                            }
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: dbManager.connections.length === 0
                    text: "No connections.\nPress + to add one."
                    color: theme.textFaint
                    font.pixelSize: 11
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }

        // ---- detail ----
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 28
                Text {
                    text: editing ? (draft.name || "Connection") : "New connection"
                    color: theme.text
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Rectangle {
                    visible: dbManager.connected
                    radius: 4
                    color: Qt.alpha(theme.success, 0.15)
                    implicitWidth: connTag.implicitWidth + 10
                    implicitHeight: 18
                    Text {
                        id: connTag
                        anchors.centerIn: parent
                        text: "connected"
                        color: theme.success
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                    }
                }
                PillButton {
                    visible: dbManager.connected
                    text: "Disconnect"
                    onClicked: dbManager.disconnectDb()
                }
                IconButton {
                    visible: editing
                    iconName: "trash"
                    Accessible.name: "Delete connection"
                    onClicked: deleteDialog.open()
                }
            }

            // ========== FORM (offline) ==========
            Flickable {
                id: formFlick
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: !dbManager.connected
                clip: true
                contentHeight: formCol.implicitHeight + 24
                boundsBehavior: Flickable.StopAtBounds

                ColumnLayout {
                    id: formCol
                    width: formFlick.width
                    spacing: 12

                    Text {
                        text: editing ? "Connection settings" : "New connection"
                        color: theme.text
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: formInner.implicitHeight + 28
                        radius: theme.radiusMd
                        color: theme.well
                        border.width: 1
                        border.color: theme.hairline

                        ColumnLayout {
                            id: formInner
                            anchors {
                                left: parent.left; right: parent.right; top: parent.top
                                margins: 14
                            }
                            spacing: 12

                            Field {
                                label: "Name"
                                placeholder: "local-mysql"
                                value: draft.name || ""
                                onEdited: t => draft.name = t
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Text { text: "Driver"; color: theme.textDim; font.pixelSize: 11; font.weight: Font.Medium }
                                Segmented {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 30
                                    model: [["sqlite", "SQLite"], ["mysql", "MySQL"], ["postgres", "PostgreSQL"]]
                                    value: draft.driver || "sqlite"
                                    onPicked: v => {
                                        draft.driver = v
                                        draft.port = v === "postgres" ? 5432 : v === "mysql" ? 3306 : 0
                                    }
                                }
                                Text {
                                    visible: draft.driver !== "sqlite" && !driversHas(draft.driver)
                                    text: "Qt SQL driver not installed"
                                    color: theme.danger
                                    font.pixelSize: 10
                                }
                            }

                            Field {
                                label: "Host"
                                placeholder: "127.0.0.1"
                                value: draft.host || ""
                                mono: true
                                show: draft.driver !== "sqlite"
                                onEdited: t => draft.host = t
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                visible: draft.driver !== "sqlite"
                                Text { text: "Port"; color: theme.textDim; font.pixelSize: 11; font.weight: Font.Medium }
                                TextField {
                                    id: portField
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 32
                                    text: String(draft.port || (draft.driver === "postgres" ? 5432 : 3306))
                                    color: theme.text
                                    font.family: "monospace"
                                    font.pixelSize: 12
                                    horizontalAlignment: Text.AlignHCenter
                                    leftPadding: 10
                                    rightPadding: 10
                                    validator: IntValidator { bottom: 1; top: 65535 }
                                    background: Rectangle {
                                        radius: 8
                                        color: theme.well
                                        border.width: 1
                                        border.color: portField.activeFocus ? Qt.alpha(theme.accent, 0.45) : theme.hairline
                                    }
                                    onEditingFinished: draft.port = parseInt(text) || 3306
                                }
                            }

                            Field {
                                label: "User"
                                placeholder: "root"
                                value: draft.user || ""
                                mono: true
                                show: draft.driver !== "sqlite"
                                onEdited: t => draft.user = t
                            }
                            Field {
                                label: "Database"
                                placeholder: "app (optional)"
                                value: draft.database || ""
                                mono: true
                                show: draft.driver !== "sqlite"
                                onEdited: t => draft.database = t
                            }
                            Field {
                                label: "Password"
                                placeholder: draft.hasPassword ? "•••••• (unchanged)" : "optional"
                                value: ""
                                show: draft.driver !== "sqlite"
                                echoMode: TextInput.Password
                                onEdited: t => draft.password = t
                            }
                            Field {
                                label: "File path"
                                placeholder: "/path/to/app.db"
                                value: draft.sqlitePath || ""
                                mono: true
                                show: draft.driver === "sqlite"
                                onEdited: t => draft.sqlitePath = t
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        PillButton {
                            text: editing ? "Save" : "Add"
                            checked: true
                            onClicked: tab.save()
                        }
                        PillButton {
                            visible: editing
                            text: "Connect"
                            iconName: "check"
                            enabled: !dbManager.busy && driversHas(draft.driver)
                            onClicked: tab.save()
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            visible: dbManager.busy
                            text: "Connecting…"
                            color: theme.textDim
                            font.pixelSize: 11
                        }
                    }

                    Text {
                        visible: dbManager.lastError.length > 0
                        Layout.fillWidth: true
                        text: dbManager.lastError
                        color: theme.danger
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }

                    Text {
                        visible: !editing
                        Layout.fillWidth: true
                        text: "Pick a connection on the left, or press + to add one."
                        color: theme.textFaint
                        font.pixelSize: 12
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }

            // ========== WORKSPACE (connected) ==========
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: dbManager.connected
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Segmented {
                        Layout.preferredHeight: 28
                        Layout.preferredWidth: implicitWidth
                        model: [["tables", "Browse"], ["query", "Query"]]
                        value: tab.detailTab
                        onPicked: v => tab.detailTab = v
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: tab.browseSchema + (tab.browseTable ? " · " + tab.browseTable : "")
                        color: theme.textDim
                        font.family: "monospace"
                        font.pixelSize: 11
                        elide: Text.ElideRight
                        Layout.maximumWidth: 200
                    }
                    PillButton {
                        text: "Refresh"
                        iconName: "undo"
                        enabled: !dbManager.busy
                        onClicked: {
                            if (tab.browseTable !== "")
                                dbManager.browseTable(tab.browseSchema, tab.browseTable, 50, 0)
                            else
                                tableModel.reload()
                        }
                    }
                }

                // Browse view — Databases | Tables side by side, then values grid
                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: tab.detailTab === "tables"
                    spacing: 10

                    // Databases column
                    Rectangle {
                        Layout.preferredWidth: 150
                        Layout.maximumWidth: 180
                        Layout.fillHeight: true
                        color: theme.well
                        border.width: 1
                        border.color: theme.hairline
                        radius: theme.radiusMd

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 6
                            spacing: 0

                            Text {
                                text: "DATABASES  (" + dbModel.count + ")"
                                color: theme.textDim
                                font.pixelSize: 10
                                font.weight: Font.DemiBold
                                font.letterSpacing: 0.4
                                Layout.fillWidth: true
                                Layout.bottomMargin: 4
                            }
                            ListView {
                                id: dbList
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                model: dbModel
                                spacing: 1
                                delegate: Rectangle {
                                    id: dbRow
                                    required property string name
                                    readonly property bool sel: dbRow.name === tab.browseSchema
                                    width: dbList.width
                                    height: 28
                                    radius: 5
                                    color: sel ? Qt.alpha(theme.accent, 0.2) : dbHov.hovered ? Qt.alpha(theme.text, 0.07) : "transparent"
                                    border.width: sel ? 1 : 0
                                    border.color: Qt.alpha(theme.accent, 0.4)
                                    HoverHandler { id: dbHov }
                                    TapHandler {
                                        onTapped: {
                                            tab.browseSchema = dbRow.name
                                            tab.browseTable = ""
                                            dbManager.useDatabase(dbRow.name)
                                            tableModel.reload()
                                        }
                                    }
                                    Text {
                                        anchors {
                                            left: parent.left; right: parent.right
                                            verticalCenter: parent.verticalCenter
                                            leftMargin: 8
                                        }
                                        text: dbRow.name
                                        color: sel ? theme.accentText : theme.text
                                        font.family: "monospace"
                                        font.pixelSize: 11
                                        font.weight: sel ? Font.DemiBold : Font.Normal
                                        elide: Text.ElideRight
                                    }
                                }
                                Text {
                                    anchors.centerIn: parent
                                    visible: dbModel.count === 0
                                    text: "No databases"
                                    color: theme.textFaint
                                    font.pixelSize: 11
                                }
                            }
                        }
                    }

                    // Tables column
                    Rectangle {
                        Layout.preferredWidth: 150
                        Layout.maximumWidth: 180
                        Layout.fillHeight: true
                        color: theme.well
                        border.width: 1
                        border.color: theme.hairline
                        radius: theme.radiusMd

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 6
                            spacing: 0

                            Text {
                                text: "TABLES  (" + tableModel.count + ")"
                                color: theme.textDim
                                font.pixelSize: 10
                                font.weight: Font.DemiBold
                                font.letterSpacing: 0.4
                                Layout.fillWidth: true
                                Layout.bottomMargin: 4
                            }
                            ListView {
                                id: tablesList
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                model: tableModel
                                spacing: 1
                                delegate: Rectangle {
                                    id: tRow
                                    required property string name
                                    required property string schema
                                    readonly property bool sel: tRow.name === tab.browseTable
                                    width: tablesList.width
                                    height: 28
                                    radius: 5
                                    color: sel ? Qt.alpha(theme.accent, 0.18) : tHov.hovered ? Qt.alpha(theme.text, 0.06) : "transparent"
                                    border.width: sel ? 1 : 0
                                    border.color: Qt.alpha(theme.accent, 0.4)
                                    HoverHandler { id: tHov }
                                    TapHandler {
                                        onTapped: {
                                            tab.browseSchema = tRow.schema
                                            tab.browseTable = tRow.name
                                            dbManager.browseTable(tRow.schema, tRow.name, 50, 0)
                                        }
                                    }
                                    Text {
                                        anchors {
                                            left: parent.left; right: parent.right
                                            verticalCenter: parent.verticalCenter
                                            leftMargin: 8
                                        }
                                        text: tRow.name
                                        color: sel ? theme.accentText : theme.text
                                        font.family: "monospace"
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                    }
                                }
                                Text {
                                    anchors.centerIn: parent
                                    width: parent.width - 12
                                    visible: tableModel.count === 0
                                    text: tab.browseSchema === "" ? "Pick a database" : "No tables"
                                    color: theme.textFaint
                                    font.pixelSize: 11
                                    horizontalAlignment: Text.AlignHCenter
                                    wrapMode: Text.Wrap
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: theme.well
                        border.width: 1
                        border.color: theme.hairline
                        radius: theme.radiusMd

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 6

                            Text {
                                visible: tab.browseTable === ""
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                text: "Select a table to see its rows"
                                color: theme.textFaint
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            Text {
                                visible: tab.browseTable !== "" && dbManager.lastError.length > 0
                                Layout.fillWidth: true
                                text: dbManager.lastError
                                color: theme.danger
                                font.pixelSize: 11
                                wrapMode: Text.Wrap
                            }
                            Text {
                                visible: tab.browseTable !== "" && dbManager.lastError === ""
                                Layout.fillWidth: true
                                text: dbManager.lastResult.rowCount + " rows · " + dbManager.lastResult.durationMs + " ms"
                                color: theme.textDim
                                font.pixelSize: 11
                            }

                            Flickable {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                visible: tab.browseTable !== "" && dbManager.lastError === "" && tab.colsOf(dbManager.lastResult).length > 0
                                clip: true
                                contentWidth: gridCol.implicitWidth
                                contentHeight: gridCol.implicitHeight

                                Column {
                                    id: gridCol
                                    Row {
                                        Repeater {
                                            model: tab.colsOf(dbManager.lastResult)
                                            delegate: Rectangle {
                                                required property var modelData
                                                width: 110
                                                height: 28
                                                color: theme.dark ? "#2a2a2e" : "#e8e8ec"
                                                border.width: 1
                                                border.color: theme.hairline
                                                Text {
                                                    anchors {
                                                        left: parent.left; right: parent.right
                                                        verticalCenter: parent.verticalCenter
                                                        leftMargin: 8
                                                    }
                                                    text: String(modelData)
                                                    color: theme.text
                                                    font.pixelSize: 11
                                                    font.weight: Font.DemiBold
                                                    elide: Text.ElideRight
                                                }
                                            }
                                        }
                                    }
                                    Repeater {
                                        model: tab.rowsOf(dbManager.lastResult)
                                        delegate: Row {
                                            id: dataRow
                                            required property var modelData
                                            required property int index
                                            Repeater {
                                                model: tab.colsOf(dbManager.lastResult)
                                                delegate: Rectangle {
                                                    required property string modelData
                                                    width: 110
                                                    height: 26
                                                    color: dataRow.index % 2 === 0
                                                        ? (theme.dark ? Qt.rgba(1, 1, 1, 0.03) : Qt.rgba(0, 0, 0, 0.02))
                                                        : "transparent"
                                                    border.width: 1
                                                    border.color: theme.hairline
                                                    Text {
                                                        anchors {
                                                            left: parent.left; right: parent.right
                                                            verticalCenter: parent.verticalCenter
                                                            leftMargin: 8
                                                        }
                                                        text: tab.cellText(dataRow.modelData, modelData)
                                                        color: theme.textSoft
                                                        font.family: "monospace"
                                                        font.pixelSize: 11
                                                        elide: Text.ElideRight
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // Query view
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: tab.detailTab === "query"
                    spacing: 8

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 100
                        radius: theme.radiusMd
                        color: theme.well
                        border.width: 1
                        border.color: sqlEdit.activeFocus ? Qt.alpha(theme.accent, 0.4) : theme.hairline

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 6
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: "SQL"; color: theme.textDim; font.pixelSize: 11; font.weight: Font.Medium }
                                Item { Layout.fillWidth: true }
                                Text { text: "Ctrl+Enter"; color: theme.textFaint; font.pixelSize: 10 }
                                PillButton {
                                    text: dbManager.busy ? "Running…" : "Run"
                                    checked: true
                                    enabled: !dbManager.busy
                                    onClicked: dbManager.execute(sqlEdit.text)
                                }
                            }
                            TextArea {
                                id: sqlEdit
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                text: "SELECT 1"
                                color: theme.text
                                font.family: "monospace"
                                font.pixelSize: 12
                                wrapMode: TextEdit.Wrap
                                background: null
                                Keys.onReturnPressed: e => {
                                    if (e.modifiers & Qt.ControlModifier) {
                                        dbManager.execute(text)
                                        e.accepted = true
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        visible: dbManager.lastError.length > 0
                        Layout.fillWidth: true
                        text: dbManager.lastError
                        color: theme.danger
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: theme.radiusMd
                        color: theme.well
                        border.width: 1
                        border.color: theme.hairline

                        Text {
                            anchors.centerIn: parent
                            visible: tab.colsOf(dbManager.lastResult).length === 0
                            text: "Run a query to see results"
                            color: theme.textFaint
                            font.pixelSize: 12
                        }
                    }
                }
            }
        }
    }

    DarkMenu {
        id: ctx
        MenuItem { text: "Connect"; onTriggered: dbManager.connectTo(menuId) }
        MenuItem {
            text: "Duplicate"
            onTriggered: {
                const c = dbManager.connectionById(menuId)
                const id = dbManager.addConnection({
                    name: (c.name || "") + " copy", driver: c.driver, host: c.host, port: c.port,
                    user: c.user, database: c.database, sqlitePath: c.sqlitePath, savePassword: c.savePassword
                })
                if (id)
                    tab.say("DB — duplicated")
            }
        }
        MenuItem {
            text: "Delete"
            onTriggered: {
                selectedId = menuId
                deleteDialog.open()
            }
        }
    }

    Dialog {
        id: deleteDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        title: "Delete connection?"
        standardButtons: Dialog.Ok | Dialog.Cancel
        width: 340
        palette.window: theme.dark ? "#232326" : "#ffffff"
        palette.windowText: theme.dark ? "#f2f2f7" : "#111111"
        palette.text: theme.dark ? "#f2f2f7" : "#111111"
        palette.buttonText: theme.dark ? "#f2f2f7" : "#111111"
        background: Rectangle {
            radius: theme.radiusMd
            color: theme.dark ? "#232326" : "#ffffff"
            border.width: 1
            border.color: Qt.alpha(theme.text, 0.2)
        }
        onAccepted: {
            const wasSelected = selectedId === tab.selectedId
            dbManager.removeConnection(selectedId)
            if (wasSelected)
                tab.select("")
            tab.say("DB — deleted")
        }
        contentItem: Text {
            text: "The connection is removed from the list. Saved password (if any) is dropped too."
            wrapMode: Text.Wrap
            color: theme.dark ? "#f2f2f7" : "#1d1d1f"
            font.pixelSize: 12
        }
    }
}
