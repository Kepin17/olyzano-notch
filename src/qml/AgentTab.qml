import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: tab
    required property var shell
    Layout.fillWidth: true
    Layout.fillHeight: true
    spacing: 6

    readonly property bool modalOpen: workspacePicker.visible || exportPicker.visible || tab.renamingKey !== "" || providerEditor.visible
        || autoConfirm.visible || deleteSessionConfirm.visible || deleteProviderConfirm.visible
        || providerChoice.popup.visible || modeMenu.visible || modelMenu.visible || effortMenu.visible || suggest.visible || clearConfirm.visible
    readonly property bool locked: codingAgent.busy || codingAgent.hasPendingApprovals
    readonly property bool hasKey: !!codingAgent.activeProvider.hasApiKey || codingAgent.activeProvider.authHeader === "none"
    property var draft: ({})
    property string editingProviderId: ""
    property string formError: ""
    property string deleteSessionId: ""
    property string deleteProviderId: ""
    property string autoSessionId: ""
    // A known provider (MiMo, DeepSeek, Claude, OpenAI) only needs a model and a key: its endpoint, protocol and
    // auth are filled in. Everything else shows for a custom endpoint, or under "Advanced".
    property bool known: false
    property bool advanced: false
    readonly property bool full: !known || advanced   // name, address, model
    readonly property bool extras: advanced           // sign-in style, temperature, tokens, context window
    property var addChoices: [] // presets not in the list yet, then "Custom endpoint"

    function indexOfId(items, id) {
        for (let i = 0; i < items.length; ++i)
            if (items[i].id === id) return i
        return -1
    }
    function setDraft(field, value) {
        const next = Object.assign({}, draft)
        next[field] = value
        draft = next
    }
    readonly property var customDraft: ({
        name: "Custom", baseUrl: "", modelId: "", protocol: "openai", authHeader: "Bearer",
        temperature: 0.2, maxTokens: 8192, maxTokensParameter: "max_tokens", modelPresets: []
    })
    function isKnown(p) {
        if (p.customHeadersJson || p.extraBodyJson) return false
        const presets = codingAgent.presets()
        for (let i = 0; i < presets.length; ++i)
            if (presets[i].baseUrl === p.baseUrl && presets[i].protocol === p.protocol && presets[i].authHeader === p.authHeader)
                return true
        return false
    }
    function pickPreset(index) {
        const choice = addChoices[index]
        draft = choice.preset ? Object.assign({}, choice.preset) : Object.assign({}, customDraft)
        known = !!choice.preset
    }
    function editProvider(id) {
        editingProviderId = id
        advanced = false
        if (id) {
            draft = codingAgent.providerById(id)
            known = isKnown(draft)
        } else {
            const have = codingAgent.providers.map(p => p.id)
            const choices = codingAgent.presets().filter(p => have.indexOf(p.id) < 0).map(p => ({ id: p.id, name: p.name, preset: p }))
            choices.push({ id: "custom", name: "Custom AI (any endpoint)", preset: null })
            addChoices = choices
            const custom = choices.length - 1 // the "+" button is for a custom AI first; the known providers are one pick away
            pickPreset(custom)
            Qt.callLater(() => presetChoice.currentIndex = custom)
        }
        keyField.value = ""
        clearKey.checked = false
        formError = ""
        providerEditor.open()
    }
    function saveProvider() {
        formError = ""
        const temperature = Number(temperatureField.text)
        const maxTokens = Number(tokensField.text)
        if (!nameField.text.trim() || !urlField.text.trim() || !modelField.text.trim()) {
            formError = "Name, base URL and model ID are required."
            return
        }
        if (!/^https?:\/\//i.test(urlField.text.trim())) {
            formError = "Base URL must start with https:// or http://."
            return
        }
        if (!temperatureField.text.trim() || !isFinite(temperature) || temperature < -1 || temperature > 2
                || !isFinite(maxTokens) || maxTokens < 256 || maxTokens > 1000000 || Math.floor(maxTokens) !== maxTokens) {
            formError = "Use temperature -1 to 2 (-1 omits it), and a token limit from 256 to 1000000."
            return
        }
        const fields = {
            name: nameField.text.trim(), baseUrl: urlField.text.trim(),
            modelId: modelField.text.trim(), protocol: protocolChoice.currentValue,
            authHeader: authField.text.trim(), temperature: temperature, maxTokens: maxTokens,
            maxTokensParameter: tokenParameter.text.trim(),
            contextWindow: Math.max(0, Math.floor(Number(contextField.text) || 0)),
            // not editable here; kept as they are for a provider that already has them
            customHeadersJson: draft.customHeadersJson || "", extraBodyJson: draft.extraBodyJson || ""
        }
        // Omitting apiKey preserves the saved key; an explicit empty value removes it.
        if (clearKey.checked) fields.apiKey = ""
        else if (keyField.text.length > 0) fields.apiKey = keyField.text
        if (editingProviderId) {
            if (!codingAgent.updateProvider(editingProviderId, fields)) {
                formError = "Could not update provider. " + codingAgent.lastError
                return
            }
        } else {
            const id = codingAgent.addProvider(fields)
            if (!id) {
                formError = "Could not add provider. " + codingAgent.lastError
                return
            }
            codingAgent.activeProviderId = id
        }
        providerEditor.close()
        Qt.callLater(() => composer.forceActiveFocus()) // ready to type right away
    }
    property bool sendAfterPick: false
    property string renamingKey: ""   // "f:<folder path>" or "t:<task id>" while its name is being edited
    property string renameText: ""
    // The sidebar rows live in a ListModel that is patched in place. Handing the list a fresh array each time made every row
    // blink (and lost the name being edited) whenever anything about a task changed.
    ListModel { id: sidebarModel }
    function syncSidebar() {
        const rows = []
        for (const p of codingAgent.projects) {
            rows.push({ kind: "folder", rowKey: "f:" + p.path, path: p.path, name: p.name, title: "", sid: "", count: p.sessions.length, collapsed: p.collapsed, exists: p.exists, active: p.active })
            if (!p.collapsed)
                for (const t of p.sessions) rows.push({ kind: "task", rowKey: "t:" + t.id, path: p.path, name: "", title: t.title, sid: t.id, count: 0, collapsed: false, exists: true, active: t.active })
        }
        for (let i = 0; i < rows.length; ++i) {
            if (i >= sidebarModel.count) { sidebarModel.append(rows[i]); continue }
            const cur = sidebarModel.get(i)
            for (const k in rows[i]) if (cur[k] !== rows[i][k]) sidebarModel.setProperty(i, k, rows[i][k])
        }
        if (sidebarModel.count > rows.length) sidebarModel.remove(rows.length, sidebarModel.count - rows.length)
    }
    // The chat, the to-do list and the provider list are replaced only when their content is different, so an unrelated
    // change (a folded folder, a renamed task) does not rebuild them.
    readonly property bool detailFull: codingAgent.detail === "full"
    ListModel { id: chatModel; dynamicRoles: true } // one row per message, patched in place: only the message that changed is redrawn
    property var todoItems: []
    property string todoJson: ""
    property var providerItems: []
    property string providerJson: ""
    function refreshChat() {
        // the same list again unless something changed; "fp" tells which message did. In headings mode the tool results are left out:
        // the line of the action has them (the model still gets them).
        const items = codingAgent.messages.filter(m => detailFull || m.role !== "tool")
        for (let i = 0; i < items.length; ++i) {
            const m = items[i]
            if (i >= chatModel.count) chatModel.append({ fp: m.fp, msg: m })
            else if (chatModel.get(i).fp !== m.fp || chatModel.get(i).msg.id !== m.id) chatModel.set(i, { fp: m.fp, msg: m })
        }
        if (chatModel.count > items.length) chatModel.remove(items.length, chatModel.count - items.length)
        if (items.length === 0 || chatModel.count !== items.length) transcript.follow = true
        transcript.stick()
    }
    function refreshTodos() { const v = codingAgent.todos, j = JSON.stringify(v); if (j !== todoJson) { todoJson = j; todoItems = v } }
    function refreshProviders() { const v = codingAgent.providers, j = JSON.stringify(v); if (j !== providerJson) { providerJson = j; providerItems = v } }
    Component.onCompleted: { syncSidebar(); refreshChat(); refreshTodos(); refreshProviders() }
    function startRename(key, current) { renameText = current; renamingKey = key }
    function commitRename(text) {
        const key = renamingKey
        renamingKey = ""
        if (!key) return
        if (key.startsWith("f:")) codingAgent.renameFolder(key.slice(2), text)
        else if (text.trim()) codingAgent.renameSession(key.slice(2), text.trim())
    }
    // A new task in the folder you are in: nothing to pick again. Without a folder yet, the picker opens.
    function newTask() {
        if (codingAgent.workspace) codingAgent.openFolder(codingAgent.workspace)
        else workspacePicker.start("")
    }
    property bool planOpen: false        // plan.md is shown open
    property bool revising: false        // the next prompt asks for changes to the plan
    property bool planAutoPending: false // "Accept · Auto" waits for the consent dialog
    property string lastPlanStatus: ""
    Connections {
        target: codingAgent
        function onPlanChanged() { // a decision closes the plan; reading it again is one click away
            const now = codingAgent.planStatus
            if (now === tab.lastPlanStatus) return
            tab.lastPlanStatus = now
            if (now !== "pending") tab.planOpen = false
        }
    }
    function baseName(path) {
        const parts = String(path).replace(/\\/g, "/").split("/").filter(x => x)
        return parts.length ? parts[parts.length - 1] : String(path)
    }
    // ---- the prompt box: @files (chips), !commands (SHELL badge), /commands ----
    property var chips: []          // workspace-relative paths picked with @, sent with the next prompt
    property bool shellMode: false  // the box holds a command to run, not a prompt
    property string suggestKind: "" // "@" | "/" | ""
    property string suggestQuery: ""
    property var suggestRows: []
    property var suggestData: []
    function addChip(path) {
        if (chips.indexOf(path) >= 0) return
        if (chips.length >= 8) { shell.say("At most 8 files per message."); return }
        chips = chips.concat([path])
    }
    function dropChip(path) { chips = chips.filter(c => c !== path) }
    function closeSuggest() { suggestKind = ""; suggestData = []; suggestRows = []; suggest.close() }
    function refreshSuggest() {
        const text = composer.text
        const pos = composer.cursorPosition
        let kind = "", query = "", data = [], rows = []
        if (!shellMode && text.startsWith("/") && text.indexOf(" ") < 0 && text.indexOf("\n") < 0) {
            kind = "/"; query = text.slice(1)
            data = codingAgent.slashCompletions(query)
            rows = data.map(c => ({ title: "/" + c.name, args: c.args, detail: c.description, mono: true }))
        } else if (!shellMode) {
            const m = /(^|\s)@([^\s]*)$/.exec(text.slice(0, pos))
            if (m && codingAgent.workspace) {
                kind = "@"; query = m[2]
                data = codingAgent.findFiles(query, 8)
                rows = data.map(f => ({ glyph: "folder", title: f.name, detail: f.dir }))
            }
        }
        suggestKind = rows.length > 0 ? kind : ""
        suggestQuery = query
        suggestData = data
        suggestRows = rows
        suggest.currentIndex = 0
        if (rows.length > 0) suggest.open(); else suggest.close()
    }
    function acceptSuggest(index) {
        if (index < 0 || index >= suggestData.length) return
        const item = suggestData[index]
        if (suggestKind === "@") {
            const pos = composer.cursorPosition
            const before = composer.text.slice(0, pos).replace(/@[^\s]*$/, "")
            composer.text = before + composer.text.slice(pos)
            composer.cursorPosition = before.length
            addChip(item.path)
            closeSuggest()
        } else if (suggestKind === "/") {
            closeSuggest()
            if (item.args === "") { composer.text = "/" + item.name; send() }
            else { composer.text = "/" + item.name + " "; composer.cursorPosition = composer.text.length }
        }
    }
    function composerKey(event) {
        if (suggest.visible) {
            if (event.key === Qt.Key_Down) { suggest.currentIndex = Math.min(suggestRows.length - 1, suggest.currentIndex + 1); event.accepted = true; return }
            if (event.key === Qt.Key_Up) { suggest.currentIndex = Math.max(0, suggest.currentIndex - 1); event.accepted = true; return }
            if (event.key === Qt.Key_Tab || event.key === Qt.Key_Return || event.key === Qt.Key_Enter) { acceptSuggest(suggest.currentIndex); event.accepted = true; return }
            if (event.key === Qt.Key_Escape) { closeSuggest(); event.accepted = true; return }
        }
        if (event.key === Qt.Key_Escape && codingAgent.busy) { codingAgent.stop(); event.accepted = true; return } // interrupt
        if (event.key === Qt.Key_Escape && shellMode) { shellMode = false; event.accepted = true; return }
        if (event.key === Qt.Key_Backspace && composer.text.length === 0) {
            if (shellMode) { shellMode = false; event.accepted = true; return }
            if (chips.length > 0) { chips = chips.slice(0, chips.length - 1); event.accepted = true; return }
        }
        if (event.text === "!" && composer.text.length === 0 && !shellMode) { shellMode = true; event.accepted = true; return }
    }
    function helpText() {
        return "@ file: type @ and pick a file; its content goes with your message.\n"
            + "! command: start the box with ! to run a command in the workspace; the output shows here and the agent sees it next time.\n"
            + "/ commands: " + codingAgent.slashCompletions("").map(c => "/" + c.name).join("  ")
    }
    function runSlash(name, args) {
        const free = name === "copy" || name === "help" || name === "workspace" || name === "details" || name === "model" || name === "effort" || name === "mode" // these may change while it works
        if (locked && !free) { shell.say("Wait for the agent to finish first."); return }
        if (name === "fork") codingAgent.forkSession()
        else if (name === "new") codingAgent.newSession()
        else if (name === "clear") clearConfirm.open()
        else if (name === "compact") codingAgent.compactNow(args)
        else if (name === "rename") { if (args) codingAgent.renameSession(codingAgent.activeSessionId, args); else shell.say("Usage: /rename <title>") }
        else if (name === "workspace") workspacePicker.start(codingAgent.workspace)
        else if (name === "copy") { const a = codingAgent.lastAnswer(); if (a) { notch.copyText(a); shell.say("Copied the last answer") } else shell.say("Nothing to copy yet") }
        else if (name === "export") exportPicker.start(codingAgent.workspace)
        else if (name === "details") {
            const v = args.trim().toLowerCase()
            codingAgent.detail = v === "full" ? "full" : v === "headings" ? "headings" : (tab.detailFull ? "headings" : "full")
            shell.say(tab.detailFull ? "Showing everything the agent did." : "Showing one line per action; click a line for the details.")
        }
        else if (name === "help") codingAgent.addNote(helpText())
        else if (name === "model") { if (args) codingAgent.activeModelId = args; else modelMenu.open() }
        else if (name === "effort") {
            if (!args) effortMenu.open()
            else { codingAgent.effort = args; if (codingAgent.effort !== "" || args === "default") shell.say("Effort: " + effortName(codingAgent.effort)); else shell.say("Effort levels: low, medium, high, xhigh, max") }
        }
        else if (name === "mode") {
            const v = args.trim().toLowerCase()
            if (v === "plan" || v === "manual" || v === "auto") pickMode(v); else modeMenu.open()
        }
        else if (name === "plan") { codingAgent.mode = "plan"; if (args) codingAgent.sendPrompt(args); else shell.say("Plan mode: describe the task, the agent will discuss and write a plan.") }
    }
    // Never a dead button: what is missing is asked for, then the prompt goes out.
    function send() {
        const text = composer.text.trim()
        if (shellMode) {
            if (!text || locked) return
            if (!codingAgent.workspace) { shell.say("Choose a workspace first."); workspacePicker.start(codingAgent.workspace); return }
            codingAgent.runShell(text)
            composer.text = ""; shellMode = false; closeSuggest()
            return
        }
        const sl = codingAgent.parseSlash(text)
        if (sl.ok) { composer.text = ""; closeSuggest(); runSlash(sl.name, sl.args); return }
        sendPromptText()
    }
    function sendPromptText() {
        const prompt = composer.text.trim()
        if (!prompt || locked || !codingAgent.activeProviderId) return
        if (!hasKey) { editProvider(codingAgent.activeProviderId); return }
        if (!codingAgent.workspace) { sendAfterPick = true; workspacePicker.start(codingAgent.workspace); return }
        if (revising && codingAgent.planStatus === "pending") codingAgent.revisePlan(prompt)
        else codingAgent.sendPrompt(prompt, chips) // a plain comment: the agent tells a question from a change request
        revising = false
        chips = []
        composer.text = ""
    }
    function selectSession(session) {
        if (session.mode === "auto") {
            autoSessionId = session.id
            autoConfirm.open()
        } else {
            codingAgent.selectSession(session.id)
        }
    }

    Connections {
        target: codingAgent
        function onStatusMessage(text) { tab.shell.say(text) }
        function onSessionsChanged() { tab.syncSidebar() }
        function onMessagesChanged() { tab.refreshChat() }
        function onTodosChanged() { tab.refreshTodos() }
        function onProvidersChanged() { tab.refreshProviders() }
        function onDetailChanged() { tab.refreshChat() }
    }
    // Esc interrupts the agent wherever the focus is in this tab (a menu or dialog that is open keeps its own Esc)
    Shortcut {
        sequence: "Escape"
        context: Qt.WindowShortcut
        enabled: tab.visible && codingAgent.busy && !tab.modalOpen
        onActivated: codingAgent.stop()
    }

    component Choice: ComboBox {
        id: choice
        implicitHeight: 28
        leftPadding: 9
        rightPadding: 24
        font.pixelSize: 12
        contentItem: Text {
            text: choice.displayText
            color: choice.enabled ? theme.text : theme.textFaint
            font: choice.font
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        indicator: Text {
            x: choice.width - width - 8
            anchors.verticalCenter: parent.verticalCenter
            text: "⌄"
            color: theme.textDim
        }
        background: Rectangle {
            radius: theme.radiusSm
            color: theme.well
            border.width: 1
            border.color: choice.activeFocus ? theme.accent : theme.hairline
        }
        delegate: ItemDelegate {
            required property var modelData
            required property int index
            width: choice.width
            implicitHeight: 30
            highlighted: choice.highlightedIndex === index
            contentItem: Text {
                text: choice.textRole ? modelData[choice.textRole] : modelData
                color: theme.text
                font.pixelSize: 12
                elide: Text.ElideRight
            }
            background: Rectangle {
                color: parent.highlighted ? Qt.alpha(theme.accent, 0.16) : "transparent"
                radius: theme.radiusSm
            }
        }
        popup: Popup {
            y: choice.height + 4
            width: choice.width
            implicitHeight: Math.min(contentItem.implicitHeight + 8, 200)
            padding: 4
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: choice.popup.visible ? choice.delegateModel : null
                currentIndex: choice.highlightedIndex
                ScrollBar.vertical: ScrollBar {}
            }
            background: Rectangle {
                color: theme.surface
                radius: theme.radiusSm
                border.width: 1
                border.color: theme.outline
            }
        }
    }

    component CopyText: TextArea {
        Layout.fillWidth: true
        implicitHeight: contentHeight + topPadding + bottomPadding
        readOnly: true
        selectByMouse: true
        wrapMode: TextEdit.Wrap
        textFormat: TextEdit.PlainText
        color: theme.text
        font.pixelSize: 14 // the conversation is what you read most: roomy
        font.family: Qt.application.font.family
        padding: 0
        background: null
    }

    component Step: RowLayout {
        id: step
        property bool done: false
        property string title: ""
        property string detail: ""
        property string action: ""
        signal triggered()
        Layout.fillWidth: true
        spacing: 10
        Rectangle {
            Layout.preferredWidth: 20
            Layout.preferredHeight: 20
            radius: 10
            color: step.done ? Qt.alpha(theme.accent, 0.2) : "transparent"
            border.width: 1
            border.color: step.done ? Qt.alpha(theme.accent, 0.6) : theme.outline
            LineIcon { anchors.centerIn: parent; visible: step.done; glyph: "check"; sourceSize: Qt.size(11, 11); color: theme.accentText }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1
            Text { text: step.title; color: step.done ? theme.textDim : theme.text; font.pixelSize: 12; font.weight: Font.Medium }
            Text { Layout.fillWidth: true; visible: !!text; text: step.detail; color: theme.textFaint; font.pixelSize: 11; elide: Text.ElideMiddle }
        }
        PillButton { visible: !step.done && !!step.action; text: step.action; checked: true; onClicked: step.triggered() }
    }

    readonly property var modes: [
        { id: "plan", name: "Plan", icon: "eye", note: "Discusses and writes a plan. Nothing changes until you accept." },
        { id: "manual", name: "Manual", icon: "pen", note: "Asks before every edit or command." },
        { id: "auto", name: "Auto", icon: "play", note: "Edits and runs commands without asking." }
    ]
    // The model menu: one section per category, built flat so a header is just another row.
    property string modelFilter: ""
    property real menuRoom: 320 // the list stops below the header instead of running over it
    readonly property var modelRows: {
        const q = modelFilter.trim().toLowerCase()
        const rows = []
        let last = ""
        for (const m of codingAgent.models) {
            if (q && m.id.toLowerCase().indexOf(q) < 0 && m.group.toLowerCase().indexOf(q) < 0) continue
            if (m.group !== last) { rows.push({ header: true, name: m.group }); last = m.group }
            rows.push({ header: false, id: m.id })
        }
        // not in the list: what was typed can be used as it is (any model id the endpoint knows)
        const typed = modelFilter.trim()
        if (typed && !codingAgent.models.some(m => m.id.toLowerCase() === typed.toLowerCase())) {
            rows.push({ header: true, name: "Not in the list" })
            rows.push({ header: false, id: typed, custom: true })
        }
        return rows
    }
    readonly property var efforts: [
        { id: "", name: "Default", note: "Send nothing: the model's own level." },
        { id: "low", name: "Low", note: "Fastest and cheapest, least thinking." },
        { id: "medium", name: "Medium", note: "Balanced for everyday work." },
        { id: "high", name: "High", note: "Deeper reasoning for harder tasks." },
        { id: "xhigh", name: "Extra", note: "For long, difficult coding tasks." },
        { id: "max", name: "Max", note: "The most thinking the model allows." }
    ]
    function effortName(id) { const e = efforts.find(x => x.id === id); return e ? e.name : "Default" }
    function modeInfo(id) { return modes.find(m => m.id === id) || modes[1] }
    function pickMode(id) {
        modeMenu.close()
        if (id === "auto" && codingAgent.mode !== "auto") {
            autoSessionId = ""
            autoConfirm.open()
        } else if (id !== codingAgent.mode) {
            codingAgent.mode = id
            appliedLater()
        }
    }
    // Mode, model and effort can be changed while the agent works; what is already on its way keeps the old setting.
    function appliedLater() { if (codingAgent.busy) shell.say("Applies from the agent's next step.") }

    // The tasks sit on the left, the conversation on the right, like a chat app.
    RowLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: 10

        Rectangle { // the tasks, grouped by folder
            id: sidebar
            Layout.preferredWidth: 220
            Layout.fillHeight: true
            radius: theme.radiusMd
            color: theme.well
            border.width: 1
            border.color: theme.hairline
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Text {
                        Layout.fillWidth: true
                        Layout.leftMargin: 4
                        text: "TASKS"
                        color: theme.textFaint
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        font.letterSpacing: 0.8
                    }
                    PillButton { // one line per action, or everything
                        iconOnly: true; iconName: "eye"
                        text: tab.detailFull ? "Showing everything: click for one line per action" : "One line per action: click to show everything"
                        checked: tab.detailFull
                        implicitHeight: 26
                        implicitWidth: 30
                        onClicked: codingAgent.detail = tab.detailFull ? "headings" : "full"
                    }
                    PillButton {
                        iconOnly: true; iconName: "folder"; text: "Add a folder"
                        implicitHeight: 26
                        implicitWidth: 30
                        enabled: !tab.locked
                        onClicked: workspacePicker.start(codingAgent.workspace)
                    }
                    PillButton {
                        iconOnly: true; iconName: "plus"; text: "New task in this folder"
                        implicitHeight: 26
                        implicitWidth: 30
                        enabled: !tab.locked
                        onClicked: tab.newTask()
                    }
                }
                ListView {
                    id: sessionList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 1
                    boundsBehavior: Flickable.StopAtBounds
                    model: sidebarModel
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        id: item
                        required property string kind
                        required property string rowKey
                        required property string path
                        required property string name
                        required property string title
                        required property string sid
                        required property int count
                        required property bool collapsed
                        required property bool exists
                        required property bool active
                        readonly property bool isFolder: kind === "folder"
                        readonly property bool renaming: tab.renamingKey === rowKey
                        width: sessionList.width - 8
                        height: isFolder ? 32 : 34
                        radius: theme.radiusSm
                        color: !isFolder && active ? Qt.alpha(theme.accent, 0.16) : rowArea.containsMouse && !renaming ? theme.surfaceHover : "transparent"
                        opacity: !isFolder && tab.locked && !active ? 0.5 : 1
                        MouseArea {
                            id: rowArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            acceptedButtons: Qt.LeftButton
                            enabled: !item.renaming
                            onClicked: {
                                if (item.isFolder) codingAgent.setFolderCollapsed(item.path, !item.collapsed)
                                else if (!tab.locked) codingAgent.selectSession(item.sid)
                            }
                            onDoubleClicked: if (!item.isFolder && !tab.locked) tab.startRename(item.rowKey, item.title)
                        }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: item.isFolder ? 6 : 24
                            anchors.rightMargin: 6
                            spacing: 6
                            Text { visible: item.isFolder; text: item.collapsed ? "›" : "⌄"; color: theme.textFaint; font.pixelSize: 12; Layout.preferredWidth: 10 }
                            LineIcon { visible: item.isFolder; glyph: "folder"; sourceSize: Qt.size(14, 14); color: item.exists === false ? "#ff9f0a" : item.active ? theme.accentText : theme.textDim }
                            Text {
                                visible: !item.renaming
                                Layout.fillWidth: true
                                text: item.isFolder ? item.name : (item.title || "New task")
                                color: item.isFolder ? theme.text : item.active ? theme.accentText : theme.text
                                font.pixelSize: item.isFolder ? 12 : 12
                                font.weight: item.isFolder ? Font.DemiBold : Font.Medium
                                elide: Text.ElideRight
                            }
                            TextField { // renaming, right where the name is
                                id: renameField
                                visible: item.renaming
                                Layout.fillWidth: true
                                Layout.preferredHeight: 24
                                color: theme.text
                                font.pixelSize: 12
                                leftPadding: 6
                                selectByMouse: true
                                background: Rectangle { radius: 5; color: theme.surface; border.width: 1; border.color: Qt.alpha(theme.accent, 0.6) }
                                onVisibleChanged: if (visible) { text = tab.renameText; forceActiveFocus(); selectAll() }
                                onAccepted: tab.commitRename(text)
                                Keys.onEscapePressed: tab.renamingKey = ""
                                onActiveFocusChanged: if (!activeFocus && item.renaming) tab.commitRename(text)
                            }
                            Text { visible: item.isFolder && !item.renaming && !rowArea.containsMouse; text: item.count; color: theme.textFaint; font.pixelSize: 11 }
                            // actions, on hover
                            Row {
                                visible: !item.renaming && rowArea.containsMouse && !tab.locked
                                spacing: 2
                                Repeater {
                                    model: item.isFolder
                                        ? (item.count === 0 ? ["plus", "pen", "trash"] : ["plus", "pen"])
                                        : ["pen", "trash"]
                                    delegate: MouseArea {
                                        id: act
                                        required property string modelData
                                        width: 22; height: 22
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            if (modelData === "plus") codingAgent.openFolder(item.path)
                                            else if (modelData === "pen") tab.startRename(item.rowKey, item.isFolder ? item.name : item.title)
                                            else if (item.isFolder) codingAgent.removeFolder(item.path)
                                            else { tab.deleteSessionId = item.sid; deleteSessionConfirm.open() }
                                        }
                                        LineIcon { anchors.centerIn: parent; glyph: act.modelData; sourceSize: Qt.size(13, 13); color: act.containsMouse ? theme.accentText : theme.textFaint }
                                        ToolTip.visible: containsMouse
                                        ToolTip.delay: 400
                                        ToolTip.text: act.modelData === "plus" ? "New task in this folder" : act.modelData === "pen" ? "Rename"
                                            : item.isFolder ? "Remove this folder from the list (files stay)" : "Delete task"
                                    }
                                }
                            }
                        }
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: sessionList.count === 0
                        text: "No folders yet.\nAdd one with the folder button."
                        horizontalAlignment: Text.AlignHCenter
                        color: theme.textFaint
                        font.pixelSize: 11
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6

            // ---- toolbar: where it works (workspace) and who answers (provider, key) ----
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Rectangle {
                    id: workspaceChip
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    radius: theme.radiusSm
                    color: wsHover.hovered && !tab.locked ? theme.surfaceHover : theme.well
                    border.width: 1
                    border.color: codingAgent.workspace ? theme.hairline : Qt.alpha(theme.accent, 0.5)
                    opacity: tab.locked ? 0.5 : 1
                    HoverHandler { id: wsHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { enabled: !tab.locked; onTapped: workspacePicker.start(codingAgent.workspace) }
                    ToolTip.visible: wsHover.hovered
                    ToolTip.delay: 400
                    ToolTip.text: codingAgent.workspace || "Choose the folder the agent works in"
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 8
                        LineIcon { glyph: "folder"; sourceSize: Qt.size(15, 15); color: codingAgent.workspace ? theme.accentText : theme.textDim }
                        Text {
                            Layout.fillWidth: true
                            text: codingAgent.workspace ? tab.baseName(codingAgent.workspace) : "Choose a workspace folder"
                            color: codingAgent.workspace ? theme.text : theme.textDim
                            font.pixelSize: 12
                            font.weight: Font.Medium
                            elide: Text.ElideMiddle
                        }
                        Text { visible: !!codingAgent.workspace; text: "Change"; color: theme.textFaint; font.pixelSize: 11 }
                    }
                }
                Choice {
                    id: providerChoice
                    Layout.preferredWidth: 170
                    Layout.preferredHeight: 32
                    model: tab.providerItems
                    textRole: "name"
                    currentIndex: tab.indexOfId(tab.providerItems, codingAgent.activeProviderId)
                    displayText: currentIndex < 0 ? "Select provider" : currentText
                    enabled: !tab.locked
                    onActivated: index => codingAgent.activeProviderId = tab.providerItems[index].id
                }
                Rectangle { // a key is saved for this provider (or it needs none)
                    Layout.preferredWidth: 8
                    Layout.preferredHeight: 8
                    radius: 4
                    color: tab.hasKey ? "#30d158" : "#ff9f0a"
                    ToolTip.visible: keyDot.hovered
                    ToolTip.text: tab.hasKey ? "API key saved" : "No API key yet"
                    HoverHandler { id: keyDot }
                }
                PillButton {
                    text: tab.hasKey ? "Configure" : "Add API key"
                    iconName: "gear"
                    implicitHeight: 32
                    checked: !tab.hasKey
                    enabled: !tab.locked && !!codingAgent.activeProviderId
                    onClicked: tab.editProvider(codingAgent.activeProviderId)
                }
                PillButton {
                    iconOnly: true; iconName: "plus"; text: "Add a custom AI or another provider"
                    implicitHeight: 32
                    enabled: !tab.locked
                    onClicked: tab.editProvider("")
                }
            }

            Rectangle { // the conversation (hidden while the plan is open for reading)
                visible: !(planCard.visible && tab.planOpen)
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 100
                color: theme.well
                border.width: 1
                border.color: theme.hairline
                radius: theme.radiusMd

                ListView { // only the messages in view are built: a long task stays light
                    id: transcript
                    anchors.fill: parent
                    anchors.margins: 8
                    clip: true
                    spacing: 8
                    cacheBuffer: 500
                    boundsBehavior: Flickable.StopAtBounds
                    model: chatModel
                    property bool follow: true // stay at the end while it writes; reading further up stops that
                    // not at every change of the height (it changes while rows are being built, and moving makes more rows): a short pause
                    Timer { id: followTimer; interval: 50; onTriggered: if (transcript.follow) transcript.positionViewAtEnd() }
                    function stick() { if (follow) followTimer.restart() }
                    onContentHeightChanged: stick()
                    onCountChanged: stick()
                    onMovementStarted: follow = false
                    onAtYEndChanged: if (atYEnd) follow = true
                    ScrollBar.vertical: ScrollBar {}
                    delegate: ColumnLayout {
                        id: message
                        property bool thought: false
                        required property var msg
                        width: ListView.view ? ListView.view.width - 10 : 0
                        spacing: 4
                        RowLayout { // a command the person ran with !
                            visible: message.msg.role === "shell"
                            Layout.fillWidth: true
                            spacing: 8
                            readonly property var call: (message.msg.toolCalls || [])[0] || ({})
                            readonly property var exit: /\[exit (-?\d+)\]\s*$/.exec(call.output || "")
                            AgentBadge { text: "SHELL"; glyph: "server"; tone: "accent" }
                            Text {
                                Layout.fillWidth: true
                                text: "$ " + (message.msg.content || "")
                                color: theme.text
                                font.pixelSize: 13
                                font.family: "monospace"
                                elide: Text.ElideRight
                            }
                            AgentBadge {
                                text: !parent.exit ? (parent.call.status === "rejected" ? "Stopped" : "Running…") : parent.exit[1] === "0" ? "exit 0" : "exit " + parent.exit[1]
                                tone: !parent.exit ? (parent.call.status === "rejected" ? "warn" : "accent") : parent.exit[1] === "0" ? "ok" : "bad"
                                mono: true
                            }
                        }
                        RowLayout { // a divider the app wrote, such as "Compacted"
                            visible: message.msg.role === "note"
                            Layout.fillWidth: true
                            spacing: 10
                            Rectangle { Layout.fillWidth: true; height: 1; color: theme.hairline }
                            Text { text: message.msg.content; color: theme.textFaint; font.pixelSize: 11; elide: Text.ElideRight; Layout.maximumWidth: 420 }
                            Rectangle { Layout.fillWidth: true; height: 1; color: theme.hairline }
                        }
                        Text {
                            visible: message.msg.role !== "shell" && message.msg.role !== "note"
                            text: message.msg.role === "tool"
                                ? "Tool · " + (message.msg.toolName || "result") : message.msg.role
                            color: theme.accentText
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }
                        MouseArea { // what the model thought: a line, opened on a click (headings mode)
                            visible: !tab.detailFull && !!message.msg.reasoning
                            Layout.fillWidth: true
                            Layout.preferredHeight: visible ? 22 : 0
                            cursorShape: Qt.PointingHandCursor
                            onClicked: message.thought = !message.thought
                            Row {
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 6
                                Text { text: "Thought"; color: theme.textFaint; font.pixelSize: 12; font.italic: true }
                                Text { text: String(message.msg.reasoning || "").split(/\s+/).length + " words"; color: theme.textFaint; font.pixelSize: 11 }
                                Text { text: message.thought ? "⌃" : "⌄"; color: theme.textFaint; font.pixelSize: 11 }
                            }
                        }
                        CopyText {
                            visible: !!text && (tab.detailFull || message.thought)
                            text: message.msg.reasoning || ""
                            color: theme.textDim
                            font.pixelSize: 13
                            font.italic: true
                        }
                        CopyText {
                            visible: !!text && message.msg.role !== "shell" && message.msg.role !== "note"
                            text: message.msg.shown || message.msg.content || ""
                            // a very long answer is laid out as plain text: Markdown of a hundred thousand characters stalls the panel
                            textFormat: message.msg.role === "assistant" && (message.msg.content || "").length < 30000 ? TextEdit.MarkdownText : TextEdit.PlainText
                            font.family: message.msg.role === "tool" ? "monospace" : Qt.application.font.family
                            font.pixelSize: message.msg.role === "tool" ? 12 : 14
                        }
                        PillButton {
                            visible: message.msg.role === "tool" && (message.msg.content || "").startsWith("[…")
                            text: "Copy all output"
                            iconName: "copy"
                            implicitHeight: 24
                            onClicked: { notch.copyText(codingAgent.fullText(message.msg.id)); tab.shell.say("Copied the whole output") }
                        }
                        Flow { // files that went with this message
                            visible: (message.msg.attachments || []).length > 0
                            Layout.fillWidth: true
                            spacing: 6
                            Repeater {
                                model: message.msg.attachments || []
                                delegate: AgentBadge {
                                    required property var modelData
                                    text: tab.baseName(modelData.path)
                                    glyph: "folder"
                                    tone: "accent"
                                    ToolTip.visible: attHover.hovered
                                    ToolTip.text: modelData.path + " · " + Math.max(1, Math.round(modelData.bytes / 1024)) + " KB"
                                    HoverHandler { id: attHover }
                                }
                            }
                        }
                        Repeater { // headings mode: one line per action, the details open on a click
                            model: tab.detailFull || message.msg.role === "shell" ? [] : (message.msg.toolCalls || [])
                            delegate: AgentActivity {
                                required property var modelData
                                call: modelData
                                msgId: message.msg.id
                            }
                        }
                        Repeater { // full mode (and a command you ran with !): the whole card
                            model: tab.detailFull || message.msg.role === "shell" ? (message.msg.toolCalls || []) : []
                            delegate: Rectangle {
                                id: tool
                                required property var modelData
                                readonly property bool isCommand: modelData.name === "run_command" || !!modelData.command
                                Layout.fillWidth: true
                                implicitHeight: toolBody.implicitHeight + 16
                                color: theme.surface
                                radius: theme.radiusSm
                                border.width: 1
                                border.color: modelData.status === "awaiting_approval"
                                    ? Qt.alpha(theme.accent, 0.6) : theme.hairline
                                ColumnLayout {
                                    id: toolBody
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    spacing: 4
                                    Text {
                                        visible: message.msg.role !== "shell"
                                        Layout.fillWidth: true
                                        text: tool.modelData.name + " · " + tool.modelData.status
                                        color: theme.accentText
                                        font.pixelSize: 12
                                        wrapMode: Text.Wrap
                                    }
                                    CopyText {
                                        visible: !!text && message.msg.role !== "shell"
                                        text: tool.modelData.summary || tool.modelData.filePath || ""
                                        font.pixelSize: 13
                                    }
                                    CopyText {
                                        visible: !!text && message.msg.role !== "shell"
                                        text: tool.modelData.command || ""
                                        font.family: "monospace"
                                        font.pixelSize: 12
                                    }
                                    CopyText {
                                        visible: !!text
                                        text: tool.modelData.diffText || ""
                                        font.family: "monospace"
                                        font.pixelSize: 12
                                    }
                                    CopyText {
                                        visible: !!text
                                        text: tool.modelData.output || ""
                                        font.family: "monospace"
                                        font.pixelSize: 12
                                    }
                                    PillButton { // the chat shows only the end of a long output
                                        visible: (tool.modelData.output || "").startsWith("[…")
                                        text: "Copy all output"
                                        iconName: "copy"
                                        implicitHeight: 24
                                        onClicked: { notch.copyText(codingAgent.fullText(message.msg.id, tool.modelData.id)); tab.shell.say("Copied the whole output") }
                                    }
                                    RowLayout {
                                        visible: tool.modelData.status === "awaiting_approval"
                                        Text {
                                            Layout.fillWidth: true
                                            text: tool.isCommand
                                                ? "Unsandboxed command — review before running."
                                                : "Review the diff before applying."
                                            color: theme.textDim
                                            font.pixelSize: 11
                                            wrapMode: Text.Wrap
                                        }
                                        PillButton {
                                            text: tool.isCommand ? "Run" : "Apply"
                                            checked: true
                                            enabled: !codingAgent.busy && codingAgent.mode !== "plan"
                                            onClicked: codingAgent.approveToolCall(message.msg.id, tool.modelData.id)
                                        }
                                        PillButton {
                                            text: "Reject"
                                            enabled: !codingAgent.busy
                                            onClicked: codingAgent.rejectToolCall(message.msg.id, tool.modelData.id)
                                        }
                                    }
                                }
                            }
                        }
                    }
                    footer: Item { // the answer as it arrives, then what the run changed
                        width: transcript.width
                        height: footerCol.implicitHeight + (footerCol.implicitHeight > 0 ? 8 : 0)
                        ColumnLayout {
                            id: footerCol
                            width: parent.width - 10
                            spacing: 8
                            ColumnLayout { // plain text while it arrives; Markdown once the answer is complete
                                id: streamCol
                                Layout.fillWidth: true
                                visible: codingAgent.streaming
                                spacing: 4
                                Text {
                                    text: "Assistant · streaming…"
                                    color: theme.accentText
                                    font.pixelSize: 12
                                }
                                CopyText {
                                    visible: !!text
                                    text: codingAgent.streamingReasoning
                                    color: theme.textDim
                                    font.pixelSize: 13
                                    font.italic: true
                                }
                                CopyText { text: codingAgent.streamingContent || "Waiting for response…" }
                            }
                            Rectangle { // what the run really changed (from what happened, not from what the model says)
                                id: summaryCard
                                readonly property var run: codingAgent.lastRun
                                readonly property int more: Math.max(0, (run.files || []).length - 8)
                                visible: !codingAgent.busy && !codingAgent.streaming && !codingAgent.hasPendingApprovals && run.any === true
                                Layout.fillWidth: true
                                implicitHeight: visible ? sumCol.implicitHeight + 18 : 0
                                radius: theme.radiusMd
                                color: theme.surface
                                border.width: 1
                                border.color: run.failedCommands > 0 ? Qt.alpha("#ff453a", 0.45) : Qt.alpha("#30d158", 0.4)
                                ColumnLayout {
                                    id: sumCol
                                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 9 }
                                    spacing: 5
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8
                                        LineIcon { glyph: summaryCard.run.failedCommands > 0 ? "info" : "check"; sourceSize: Qt.size(14, 14); color: summaryCard.run.failedCommands > 0 ? "#ff9f0a" : "#30d158" }
                                        Text { text: "What changed"; color: theme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
                                        AgentBadge { visible: (summaryCard.run.files || []).length > 0; text: (summaryCard.run.files || []).length + ((summaryCard.run.files || []).length === 1 ? " file" : " files"); tone: "accent" }
                                        AgentBadge { visible: summaryCard.run.added > 0 || summaryCard.run.removed > 0; text: "+" + summaryCard.run.added + " −" + summaryCard.run.removed; tone: "ok"; mono: true }
                                        Item { Layout.fillWidth: true }
                                    }
                                    Repeater {
                                        model: (summaryCard.run.files || []).slice(0, 8)
                                        delegate: RowLayout {
                                            required property var modelData
                                            Layout.fillWidth: true
                                            Layout.leftMargin: 22
                                            spacing: 8
                                            Text { text: modelData.verb; color: theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 52 }
                                            Text { Layout.fillWidth: true; text: modelData.path; color: theme.text; font.pixelSize: 12; font.family: "monospace"; elide: Text.ElideMiddle }
                                            Text { text: "+" + modelData.added + " −" + modelData.removed; color: theme.textFaint; font.pixelSize: 11; font.family: "monospace" }
                                        }
                                    }
                                    Text { visible: summaryCard.more > 0; Layout.leftMargin: 22; text: "…and " + summaryCard.more + " more"; color: theme.textFaint; font.pixelSize: 11 }
                                    Text {
                                        visible: summaryCard.run.commands > 0
                                        Layout.fillWidth: true
                                        Layout.leftMargin: 22
                                        wrapMode: Text.Wrap
                                        text: "Ran " + summaryCard.run.commands + (summaryCard.run.commands === 1 ? " command" : " commands")
                                            + (summaryCard.run.failedCommands > 0 ? " · " + summaryCard.run.failedCommands + " failed (" + (summaryCard.run.failed || []).join(", ") + ")" : " · all fine")
                                        color: summaryCard.run.failedCommands > 0 ? "#ff6b61" : theme.textDim
                                        font.pixelSize: 12
                                    }
                                    Text {
                                        visible: summaryCard.run.declined > 0
                                        Layout.leftMargin: 22
                                        text: summaryCard.run.declined + (summaryCard.run.declined === 1 ? " proposal was declined" : " proposals were declined")
                                        color: "#ff9f0a"
                                        font.pixelSize: 12
                                    }
                                    Text {
                                        visible: tab.todoItems.length > 0
                                        Layout.leftMargin: 22
                                        text: "To-do " + todoCard.done + " of " + todoCard.total + (todoCard.done === todoCard.total ? " done" : " done, the rest is open")
                                        color: theme.textDim
                                        font.pixelSize: 12
                                    }
                                }
                            }
                        }
                    }
                }
                ColumnLayout { // nothing yet: what to do first
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 14 }
                    visible: codingAgent.messageCount === 0 && !codingAgent.streaming
                    spacing: 8
                    Text { text: "What should we work on?"; color: theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
                    Text {
                        Layout.fillWidth: true
                        text: "Describe a task below. Ask for a plan and it switches to Plan by itself: it discusses, writes plan.md, and waits for you. Manual asks before every edit or command, Auto does not."
                        color: theme.textDim
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }
                    Step {
                        done: !!codingAgent.workspace
                        title: done ? "Workspace" : "Choose a workspace"
                        detail: codingAgent.workspace || "The folder the agent can read and change"
                        action: "Choose folder…"
                        onTriggered: workspacePicker.start(codingAgent.workspace)
                    }
                    Step {
                        done: tab.hasKey
                        title: done ? "API key for " + (codingAgent.activeProvider.name || "") : "Add an API key for " + (codingAgent.activeProvider.name || "this provider")
                        detail: done ? "Saved on this computer" : "Pick the model and paste the key; the rest is filled in"
                        action: "Add key…"
                        onTriggered: tab.editProvider(codingAgent.activeProviderId)
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                visible: !!text
                text: codingAgent.hasPendingApprovals ? "Apply / Run or Reject pending tools before sending another prompt."
                    : codingAgent.retryStatus || codingAgent.lastError || (codingAgent.commandRunning ? "Command running…" : "")
                color: codingAgent.retryStatus ? "#ff9f0a" : theme.accentText
                font.pixelSize: 11
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }

            Rectangle { // the agent's own checklist for a bigger task
                id: todoCard
                readonly property int total: tab.todoItems.length
                readonly property int done: tab.todoItems.filter(t => t.status === "completed").length
                readonly property var current: tab.todoItems.find(t => t.status === "in_progress")
                property bool open: true
                visible: total > 0
                Layout.fillWidth: true
                Layout.preferredHeight: todoCol.implicitHeight + 16
                radius: theme.radiusMd
                color: theme.well
                border.width: 1
                border.color: done === total ? Qt.alpha("#30d158", 0.5) : theme.hairline
                ColumnLayout {
                    id: todoCol
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 8 }
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        MouseArea { // the whole header folds the list
                            Layout.fillWidth: true
                            Layout.preferredHeight: 24
                            cursorShape: Qt.PointingHandCursor
                            onClicked: todoCard.open = !todoCard.open
                            RowLayout {
                                anchors.fill: parent
                                spacing: 8
                                LineIcon { glyph: todoCard.done === todoCard.total ? "check" : "grid"; sourceSize: Qt.size(14, 14); color: todoCard.done === todoCard.total ? "#30d158" : theme.accentText }
                                Text { text: "To-do"; color: theme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
                                AgentBadge { text: todoCard.done + " / " + todoCard.total; tone: todoCard.done === todoCard.total ? "ok" : "accent"; mono: true }
                                Text {
                                    Layout.fillWidth: true
                                    visible: !todoCard.open && !!todoCard.current
                                    text: todoCard.current ? todoCard.current.content : ""
                                    color: theme.textDim
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                                Item { Layout.fillWidth: true; visible: todoCard.open || !todoCard.current }
                                Text { text: todoCard.open ? "⌃" : "⌄"; color: theme.textFaint; font.pixelSize: 11 }
                            }
                        }
                    }
                    Rectangle { // progress
                        Layout.fillWidth: true
                        height: 3; radius: 1.5
                        color: theme.hairline
                        Rectangle { width: parent.width * (todoCard.total > 0 ? todoCard.done / todoCard.total : 0); height: parent.height; radius: parent.radius; color: todoCard.done === todoCard.total ? "#30d158" : theme.accent
                            Behavior on width { enabled: activities.animations; NumberAnimation { duration: 200 } } }
                    }
                    Flickable {
                        visible: todoCard.open
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(150, todoList.implicitHeight)
                        contentHeight: todoList.implicitHeight
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}
                        ColumnLayout {
                            id: todoList
                            width: parent.width - 10
                            spacing: 3
                            Repeater {
                                model: tab.todoItems
                                delegate: RowLayout {
                                    id: todoRow
                                    required property var modelData
                                    readonly property bool finished: modelData.status === "completed"
                                    readonly property bool active: modelData.status === "in_progress"
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Rectangle {
                                        Layout.preferredWidth: 14; Layout.preferredHeight: 14; Layout.alignment: Qt.AlignTop; Layout.topMargin: 2
                                        radius: 7
                                        color: todoRow.finished ? Qt.alpha("#30d158", 0.2) : "transparent"
                                        border.width: 1.5
                                        border.color: todoRow.finished ? "#30d158" : todoRow.active ? theme.accent : theme.outline
                                        LineIcon { anchors.centerIn: parent; visible: todoRow.finished; glyph: "check"; sourceSize: Qt.size(9, 9); color: "#30d158" }
                                        Rectangle { anchors.centerIn: parent; visible: todoRow.active; width: 6; height: 6; radius: 3; color: theme.accent }
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: todoRow.modelData.content
                                        color: todoRow.finished ? theme.textFaint : todoRow.active ? theme.text : theme.textDim
                                        font.pixelSize: 13
                                        font.weight: todoRow.active ? Font.Medium : Font.Normal
                                        font.strikeout: todoRow.finished
                                        wrapMode: Text.Wrap
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Rectangle { // the plan the agent wrote: read it, then decide
                id: planCard
                Layout.fillWidth: true
                readonly property bool pending: codingAgent.planStatus === "pending"
                visible: codingAgent.planStatus !== ""
                Layout.preferredHeight: planCol.implicitHeight + 20
                Layout.fillHeight: tab.planOpen // reading the plan: it takes the room the conversation had
                radius: theme.radiusMd
                color: theme.well
                border.width: 1
                border.color: Qt.alpha(theme.accent, 0.5)
                ColumnLayout {
                    id: planCol
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        LineIcon { glyph: "pen"; sourceSize: Qt.size(15, 15); color: theme.accentText }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Text {
                                text: planCard.pending ? "Plan ready" : codingAgent.planStatus === "accepted" ? "Plan accepted" : "Plan rejected"
                                color: theme.text; font.pixelSize: 13; font.weight: Font.DemiBold
                            }
                            Text {
                                Layout.fillWidth: true
                                text: planCard.pending ? "Saved as plan.md. Read it, then choose, or just comment in the chat."
                                    : codingAgent.planStatus === "accepted" ? "The agent is working from it. plan.md stays readable."
                                    : "Kept as plan.md. Reopen it if you change your mind."
                                color: theme.textFaint; font.pixelSize: 11; elide: Text.ElideRight
                            }
                        }
                        PillButton {
                            visible: codingAgent.planStatus === "rejected"
                            text: "Reopen"
                            iconName: "undo"
                            enabled: !tab.locked
                            onClicked: codingAgent.reopenPlan()
                        }
                        PillButton {
                            text: tab.planOpen ? "Hide plan.md" : "Read plan.md"
                            iconName: "eye"
                            onClicked: tab.planOpen = !tab.planOpen
                        }
                    }
                    Rectangle { // the plan as a document: a column of text on its own page, like a Markdown preview
                        visible: tab.planOpen
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 80
                        Layout.preferredHeight: Math.min(320, planDoc.implicitHeight + 40)
                        radius: theme.radiusSm
                        color: theme.dark ? "#0f0f11" : "#fafafc"
                        border.width: 1
                        border.color: theme.hairline
                        clip: true
                        ScrollView {
                            id: planView
                            anchors.fill: parent
                            contentWidth: availableWidth
                            Item {
                                width: planView.availableWidth
                                implicitWidth: planView.availableWidth
                                implicitHeight: planDoc.implicitHeight + 40 // a ScrollView takes the content size from this, not from height
                                CopyText {
                                    id: planDoc
                                    x: Math.max(20, (parent.width - 700) / 2)
                                    y: 20
                                    width: Math.min(700, parent.width - 40)
                                    textFormat: TextEdit.RichText
                                    font.pixelSize: 14
                                    text: codingAgent.renderMarkdown(codingAgent.plan, Qt.application.font.family, 14,
                                                                     theme.dark ? "#ff9b8e" : "#c2362b", theme.dark ? "#1fffffff" : "#14000000",
                                                                     theme.accentText, theme.dark ? "#33ffffff" : "#26000000")
                                    onLinkActivated: link => Qt.openUrlExternally(link)
                                }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: planCard.pending
                        spacing: 6
                        PillButton {
                            text: "Revise"
                            iconName: "pen"
                            implicitHeight: 30
                            enabled: !tab.locked
                            ToolTip.visible: hovered
                            ToolTip.delay: 400
                            ToolTip.text: "Tell the agent what to change; it writes a new plan"
                            onClicked: { tab.revising = true; composer.forceActiveFocus() }
                        }
                        PillButton {
                            text: "Reject"
                            iconName: "close"
                            implicitHeight: 30
                            enabled: !tab.locked
                            ToolTip.visible: hovered
                            ToolTip.delay: 400
                            ToolTip.text: "Drop this plan; nothing is changed"
                            onClicked: codingAgent.rejectPlan()
                        }
                        Item { Layout.fillWidth: true }
                        PillButton {
                            text: "Accept · Manual"
                            iconName: "check"
                            checked: true
                            implicitHeight: 30
                            enabled: !tab.locked
                            ToolTip.visible: hovered
                            ToolTip.delay: 400
                            ToolTip.text: "Start the work; you approve each edit and command"
                            onClicked: codingAgent.acceptPlan("manual")
                        }
                        PillButton {
                            text: "Accept · Auto"
                            iconName: "play"
                            implicitHeight: 30
                            enabled: !tab.locked
                            ToolTip.visible: hovered
                            ToolTip.delay: 400
                            ToolTip.text: "Start the work; edits and commands run without asking"
                            onClicked: { tab.planAutoPending = true; autoConfirm.open() }
                        }
                    }
                }
            }

            Rectangle { // the prompt box, with Send inside it
                Layout.fillWidth: true
                // grows with what is typed (the conversation above keeps its place), then the text scrolls inside
                id: composerBox
                Layout.preferredHeight: Math.min(260, Math.max(92, composer.contentHeight + composer.topPadding + composer.bottomPadding + 54 + (chipsFlow.visible ? chipsFlow.height + 6 : 0)))
                radius: theme.radiusMd
                color: theme.well
                border.width: tab.shellMode ? 2 : 1
                border.color: tab.shellMode ? Qt.alpha(theme.accent, 0.8) : composer.activeFocus ? Qt.alpha(theme.accent, 0.55) : theme.hairline
                AgentSuggest { // @files and /commands
                    id: suggest
                    parent: composerBox
                    items: tab.suggestRows
                    onPicked: index => tab.acceptSuggest(index)
                }
                Flow { // the files picked with @, on their way with the next message
                    id: chipsFlow
                    visible: tab.chips.length > 0
                    anchors { left: parent.left; right: parent.right; top: parent.top; leftMargin: 10; rightMargin: 10; topMargin: 8 }
                    spacing: 6
                    Repeater {
                        model: tab.chips
                        delegate: AgentBadge {
                            required property string modelData
                            text: tab.baseName(modelData)
                            glyph: "folder"
                            tone: "accent"
                            closable: true
                            onClosed: tab.dropChip(modelData)
                            ToolTip.visible: chipHover.hovered
                            ToolTip.text: modelData
                            HoverHandler { id: chipHover }
                        }
                    }
                }
                AgentBadge { // the box holds a command, not a prompt
                    visible: tab.shellMode
                    text: "SHELL"
                    glyph: "server"
                    tone: "accent"
                    x: 10
                    y: (chipsFlow.visible ? chipsFlow.height + 14 : 8) + 8
                    z: 2
                }
                ScrollView {
                    anchors { left: parent.left; right: parent.right; top: chipsFlow.visible ? chipsFlow.bottom : parent.top; bottom: sendRow.top }
                    anchors.margins: 4
                    clip: true
                    TextArea {
                        id: composer
                        leftPadding: tab.shellMode ? 76 : 8
                        placeholderText: tab.shellMode ? "Run a command in " + (codingAgent.workspace ? tab.baseName(codingAgent.workspace) : "the workspace")
                            : tab.revising ? "What should change in the plan?" : !codingAgent.workspace ? "Describe a task. You will pick the workspace when you send." : "Ask about this workspace…"
                        placeholderTextColor: theme.textFaint
                        color: theme.text
                        font.pixelSize: 14
                        wrapMode: TextEdit.Wrap
                        selectByMouse: true
                        padding: 8
                        background: null
                        // A command runs on Enter; a whole /command too; a prompt needs Ctrl+Enter (Enter writes a new line)
                        readonly property bool plainEnterSends: tab.shellMode || codingAgent.parseSlash(text).ok && text.indexOf("\n") < 0
                        Keys.onPressed: event => tab.composerKey(event)
                        onTextChanged: tab.refreshSuggest()
                        onCursorPositionChanged: tab.refreshSuggest()
                        Keys.onReturnPressed: event => {
                            if ((event.modifiers & Qt.ControlModifier) || (plainEnterSends && !(event.modifiers & Qt.ShiftModifier))) { tab.send(); event.accepted = true }
                            else event.accepted = false
                        }
                        Keys.onEnterPressed: event => {
                            if ((event.modifiers & Qt.ControlModifier) || (plainEnterSends && !(event.modifiers & Qt.ShiftModifier))) { tab.send(); event.accepted = true }
                            else event.accepted = false
                        }
                    }
                }
                RowLayout {
                    id: sendRow
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 8; topMargin: 0 }
                    height: 30
                    spacing: 8
                    Rectangle {
                        id: modeChip
                        Layout.preferredHeight: 26
                        Layout.preferredWidth: modeRow.implicitWidth + 18
                        radius: theme.radiusSm
                        color: modeHover.hovered || modeMenu.visible ? theme.surfaceHover : "transparent"
                        opacity: 1
                        HoverHandler { id: modeHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: modeMenu.visible ? modeMenu.close() : modeMenu.open() }
                        Row {
                            id: modeRow
                            anchors.centerIn: parent
                            spacing: 6
                            LineIcon { anchors.verticalCenter: parent.verticalCenter; glyph: tab.modeInfo(codingAgent.mode).icon; sourceSize: Qt.size(13, 13); color: codingAgent.mode === "auto" ? theme.accentText : theme.textDim }
                            Text { anchors.verticalCenter: parent.verticalCenter; text: tab.modeInfo(codingAgent.mode).name; color: codingAgent.mode === "auto" ? theme.accentText : theme.text; font.pixelSize: 12; font.weight: Font.Medium }
                            Text { anchors.verticalCenter: parent.verticalCenter; text: "⌃"; color: theme.textFaint; font.pixelSize: 10 }
                        }
                        Popup { // opens upward, like a menu above the box
                            id: modeMenu
                            parent: modeChip
                            x: 0
                            y: -height - 6
                            width: 290
                            padding: 6
                            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                            background: Rectangle {
                                color: theme.surface
                                radius: theme.radiusMd
                                border.width: 1
                                border.color: theme.outline
                            }
                            contentItem: Column {
                                spacing: 2
                                Repeater {
                                    model: tab.modes
                                    delegate: Rectangle {
                                        id: modeItem
                                        required property var modelData
                                        readonly property bool current: codingAgent.mode === modelData.id
                                        width: 278
                                        height: 48
                                        radius: theme.radiusSm
                                        color: itemHover.hovered ? theme.surfaceHover : "transparent"
                                        HoverHandler { id: itemHover; cursorShape: Qt.PointingHandCursor }
                                        TapHandler { onTapped: tab.pickMode(modeItem.modelData.id) }
                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 10
                                            anchors.rightMargin: 10
                                            spacing: 10
                                            LineIcon { glyph: modeItem.modelData.icon; sourceSize: Qt.size(16, 16); color: modeItem.current ? theme.accentText : theme.textDim }
                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 1
                                                Text { text: modeItem.modelData.name; color: theme.text; font.pixelSize: 13; font.weight: Font.Medium }
                                                Text { Layout.fillWidth: true; text: modeItem.modelData.note; color: theme.textFaint; font.pixelSize: 11; elide: Text.ElideRight }
                                            }
                                            LineIcon { visible: modeItem.current; glyph: "check"; sourceSize: Qt.size(14, 14); color: theme.accentText }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    Rectangle { // which model answers: every model the key can use, by category
                        id: modelChip
                        Layout.preferredHeight: 26
                        Layout.preferredWidth: Math.min(modelRow.implicitWidth + 18, 230)
                        radius: theme.radiusSm
                        color: modelHover.hovered || modelMenu.visible ? theme.surfaceHover : "transparent"
                        opacity: 1
                        HoverHandler { id: modelHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: modelMenu.visible ? modelMenu.close() : modelMenu.open() }
                        Row {
                            id: modelRow
                            anchors.centerIn: parent
                            spacing: 6
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.min(implicitWidth, 190)
                                text: codingAgent.activeModelId || "Model"
                                color: theme.textDim
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                            Text { anchors.verticalCenter: parent.verticalCenter; text: "⌃"; color: theme.textFaint; font.pixelSize: 10 }
                        }
                        Popup {
                            id: modelMenu
                            parent: modelChip
                            x: 0
                            y: -height - 6
                            width: 330
                            padding: 6
                            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                            onAboutToShow: {
                                tab.modelFilter = ""
                                tab.menuRoom = Math.max(120, Math.min(320, modelChip.mapToItem(Overlay.overlay, 0, 0).y - 150))
                                codingAgent.refreshModels()
                            }
                            background: Rectangle {
                                color: theme.surface
                                radius: theme.radiusMd
                                border.width: 1
                                border.color: theme.outline
                            }
                            contentItem: ColumnLayout {
                                spacing: 4
                                TextField {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 30
                                    placeholderText: "Filter, or type any model id"
                                    placeholderTextColor: theme.textFaint
                                    color: theme.text
                                    font.pixelSize: 12
                                    leftPadding: 10
                                    text: tab.modelFilter
                                    onTextEdited: tab.modelFilter = text
                                    onAccepted: { // Enter takes the first match, or the typed id when nothing matches
                                        const first = tab.modelRows.find(r => !r.header)
                                        if (first) { codingAgent.activeModelId = first.id; modelMenu.close(); tab.appliedLater() }
                                    }
                                    background: Rectangle { radius: theme.radiusSm; color: theme.well; border.width: 1; border.color: theme.hairline }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    visible: codingAgent.modelsLoading || !tab.hasKey
                                    text: !tab.hasKey ? "Add the API key to load every model your account can use." : "Loading the models your key can use…"
                                    color: theme.textFaint
                                    font.pixelSize: 11
                                    wrapMode: Text.Wrap
                                }
                                ListView {
                                    id: modelList
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: Math.min(contentHeight, tab.menuRoom)
                                    clip: true
                                    boundsBehavior: Flickable.StopAtBounds
                                    model: tab.modelRows
                                    ScrollBar.vertical: ScrollBar {}
                                    delegate: Rectangle {
                                        id: modelItem
                                        required property var modelData
                                        readonly property bool current: !modelData.header && modelData.id === codingAgent.activeModelId
                                        width: modelList.width - 8
                                        height: modelData.header ? 26 : 30
                                        radius: theme.radiusSm
                                        color: !modelData.header && itemHover2.hovered ? theme.surfaceHover : "transparent"
                                        HoverHandler { id: itemHover2; enabled: !modelItem.modelData.header; cursorShape: Qt.PointingHandCursor }
                                        TapHandler {
                                            enabled: !modelItem.modelData.header
                                            onTapped: { codingAgent.activeModelId = modelItem.modelData.id; modelMenu.close(); tab.appliedLater() }
                                        }
                                        Text {
                                            visible: modelItem.modelData.header
                                            anchors { left: parent.left; leftMargin: 8; bottom: parent.bottom; bottomMargin: 3 }
                                            text: modelItem.modelData.name || ""
                                            color: theme.textFaint
                                            font.pixelSize: 10
                                            font.weight: Font.DemiBold
                                            font.letterSpacing: 0.6
                                        }
                                        RowLayout {
                                            visible: !modelItem.modelData.header
                                            anchors.fill: parent
                                            anchors.leftMargin: 8
                                            anchors.rightMargin: 8
                                            Text {
                                                Layout.fillWidth: true
                                                text: modelItem.modelData.custom ? "Use “" + modelItem.modelData.id + "” as the model" : (modelItem.modelData.id || "")
                                                color: modelItem.current || modelItem.modelData.custom ? theme.accentText : theme.text
                                                font.pixelSize: 12
                                                font.family: modelItem.modelData.custom ? Qt.application.font.family : "monospace"
                                                elide: Text.ElideRight
                                            }
                                            LineIcon { visible: modelItem.current; glyph: "check"; sourceSize: Qt.size(13, 13); color: theme.accentText }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    Rectangle { // how hard it thinks
                        id: effortChip
                        Layout.preferredHeight: 26
                        Layout.preferredWidth: effortRow.implicitWidth + 18
                        radius: theme.radiusSm
                        color: effortHover.hovered || effortMenu.visible ? theme.surfaceHover : "transparent"
                        opacity: 1
                        HoverHandler { id: effortHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: effortMenu.visible ? effortMenu.close() : effortMenu.open() }
                        Row {
                            id: effortRow
                            anchors.centerIn: parent
                            spacing: 6
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: codingAgent.effort ? "Effort · " + tab.effortName(codingAgent.effort) : "Effort"
                                color: codingAgent.effort ? theme.text : theme.textDim
                                font.pixelSize: 12
                            }
                            Text { anchors.verticalCenter: parent.verticalCenter; text: "⌃"; color: theme.textFaint; font.pixelSize: 10 }
                        }
                        Popup {
                            id: effortMenu
                            parent: effortChip
                            x: 0
                            y: -height - 6
                            width: 300
                            padding: 6
                            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                            background: Rectangle {
                                color: theme.surface
                                radius: theme.radiusMd
                                border.width: 1
                                border.color: theme.outline
                            }
                            contentItem: Column {
                                spacing: 2
                                Repeater {
                                    model: tab.efforts
                                    delegate: Rectangle {
                                        id: effortItem
                                        required property var modelData
                                        readonly property bool current: codingAgent.effort === modelData.id
                                        width: 288
                                        height: 44
                                        radius: theme.radiusSm
                                        color: effortItemHover.hovered ? theme.surfaceHover : "transparent"
                                        HoverHandler { id: effortItemHover; cursorShape: Qt.PointingHandCursor }
                                        TapHandler { onTapped: { codingAgent.effort = effortItem.modelData.id; effortMenu.close(); tab.appliedLater() } }
                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 10
                                            anchors.rightMargin: 10
                                            spacing: 10
                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 1
                                                Text { text: effortItem.modelData.name; color: theme.text; font.pixelSize: 13; font.weight: Font.Medium }
                                                Text { Layout.fillWidth: true; text: effortItem.modelData.note; color: theme.textFaint; font.pixelSize: 11; elide: Text.ElideRight }
                                            }
                                            LineIcon { visible: effortItem.current; glyph: "check"; sourceSize: Qt.size(14, 14); color: theme.accentText }
                                        }
                                    }
                                }
                                Text {
                                    width: 288
                                    topPadding: 4
                                    leftPadding: 10
                                    rightPadding: 10
                                    text: "Not every model takes every level; a refusal shows as an error. Default sends nothing."
                                    color: theme.textFaint
                                    font.pixelSize: 10
                                    wrapMode: Text.Wrap
                                }
                            }
                        }
                    }
                    Item { // how much of the model's context this conversation holds
                        id: contextMeter
                        readonly property real ratio: codingAgent.contextWindow > 0 ? Math.min(1, codingAgent.contextUsed / codingAgent.contextWindow) : 0
                        readonly property color tint: ratio >= 0.9 ? "#ff453a" : ratio >= 0.7 ? "#ff9f0a" : theme.accent
                        Layout.preferredHeight: 26
                        Layout.preferredWidth: meterRow.implicitWidth + 10
                        HoverHandler { id: meterHover }
                        ToolTip.visible: meterHover.hovered
                        ToolTip.delay: 300
                        ToolTip.text: codingAgent.compacting ? "Summarizing older messages…"
                            : Number(codingAgent.contextUsed).toLocaleString(Qt.locale(), "f", 0) + " of " + Number(codingAgent.contextWindow).toLocaleString(Qt.locale(), "f", 0)
                              + " tokens" + (codingAgent.contextExact ? "" : " (estimated)") + ". At 85% older messages are summarized by themselves; /compact does it now."
                        Row {
                            id: meterRow
                            anchors.centerIn: parent
                            spacing: 6
                            Rectangle {
                                width: 36; height: 5; radius: 2.5
                                anchors.verticalCenter: parent.verticalCenter
                                color: theme.hairline
                                Rectangle { width: Math.max(2, parent.width * contextMeter.ratio); height: parent.height; radius: parent.radius; color: contextMeter.tint
                                    Behavior on width { enabled: activities.animations; NumberAnimation { duration: 200 } } }
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: codingAgent.compacting ? "Compacting…" : Math.round(contextMeter.ratio * 100) + "%"
                                color: contextMeter.ratio >= 0.7 || codingAgent.compacting ? contextMeter.tint : theme.textFaint
                                font.pixelSize: 11
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "Ctrl+Enter to send · @ files · ! command · / commands"
                        color: theme.textFaint
                        font.pixelSize: 10
                        elide: Text.ElideRight
                    }
                    Rectangle { // Send while idle; the same button turns into Stop while the agent works (Esc does the same)
                        id: sendStop
                        readonly property bool stopping: codingAgent.busy
                        readonly property bool canSend: !tab.locked && !!composer.text.trim() && (tab.shellMode || composer.text.trim().startsWith("/") || !!codingAgent.activeProviderId)
                        Layout.preferredWidth: 84
                        Layout.preferredHeight: 28
                        radius: theme.radiusSm
                        color: stopping ? Qt.alpha("#ff453a", sendArea.pressed ? 0.32 : sendArea.containsMouse ? 0.24 : 0.16) : Qt.alpha(theme.accent, sendArea.pressed ? 0.3 : 0.18)
                        border.width: 1
                        border.color: stopping ? Qt.alpha("#ff453a", 0.55) : Qt.alpha(theme.accent, 0.45)
                        opacity: stopping || canSend ? 1 : 0.4
                        MouseArea {
                            id: sendArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            enabled: sendStop.stopping || sendStop.canSend
                            onClicked: sendStop.stopping ? codingAgent.stop() : tab.send()
                        }
                        Row {
                            anchors.centerIn: parent
                            spacing: 6
                            Rectangle { // the stop square
                                visible: sendStop.stopping
                                anchors.verticalCenter: parent.verticalCenter
                                width: 9; height: 9; radius: 2
                                color: "#ff453a"
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: sendStop.stopping ? "Stop" : "Send"
                                color: sendStop.stopping ? "#ff6b61" : theme.accentText
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                            }
                        }
                        ToolTip.visible: sendArea.containsMouse && sendStop.stopping
                        ToolTip.delay: 300
                        ToolTip.text: "Stop the agent (Esc)"
                    }
                }
            }
        }
    }

    ConfirmDialog {
        id: clearConfirm
        title: "Clear this task?"
        body: "The whole conversation, its plan and its to-do list are removed. The files in your workspace are not touched."
        confirmText: "Clear"
        onConfirmed: codingAgent.clearSession()
    }
    FolderPicker { // the folder the agent works in: inside the notch, because a system dialog opens behind it
        id: workspacePicker
        title: "Choose the agent workspace"
        onChosen: path => {
            if (codingAgent.openFolder(path) && tab.sendAfterPick) { tab.sendAfterPick = false; tab.send() }
        }
        onClosed: if (!picked) tab.sendAfterPick = false
    }
    FolderPicker {
        id: exportPicker
        title: "Save this task as Markdown in…"
        confirmText: "Save here"
        onChosen: path => { const f = codingAgent.exportSessionTo(path); tab.shell.say(f ? "Saved " + f : "Could not save the file") }
    }
    ConfirmDialog {
        id: autoConfirm
        title: "Enable Auto approval?"
        body: "Auto can modify workspace files and run unsandboxed shell commands without asking. Commands have your user permissions and may access files outside the workspace. Only continue with a trusted endpoint and workspace."
        confirmText: "Enable Auto"
        onConfirmed: {
            if (tab.planAutoPending) { tab.planAutoPending = false; codingAgent.acceptPlan("auto") }
            else if (tab.autoSessionId) codingAgent.selectSession(tab.autoSessionId)
            else { codingAgent.mode = "auto"; tab.appliedLater() }
        }
        onClosed: tab.planAutoPending = false
    }
    ConfirmDialog {
        id: deleteSessionConfirm
        title: "Delete session?"
        body: "This permanently removes this conversation. Workspace files are not deleted."
        onConfirmed: codingAgent.deleteSession(tab.deleteSessionId)
    }
    ConfirmDialog {
        id: deleteProviderConfirm
        title: "Delete provider?"
        body: "Remove this provider configuration and its saved API key?"
        onConfirmed: {
            if (codingAgent.removeProvider(tab.deleteProviderId)) providerEditor.close()
        }
    }

    Popup {
        id: providerEditor
        parent: Overlay.overlay
        x: Math.max(12, (Overlay.overlay.width - width) / 2)
        y: Math.max(12, (Overlay.overlay.height - height) / 2)
        width: Math.min(620, Overlay.overlay.width - 24)
        height: Math.min(tab.extras ? 520 : tab.full ? 460 : 340, Overlay.overlay.height - 24)
        padding: 12
        modal: true
        dim: false
        focus: true
        Overlay.modal: Item {}
        closePolicy: Popup.CloseOnEscape
        onClosed: {
            keyField.value = ""
            clearKey.checked = false
        }
        background: Rectangle {
            color: theme.surface
            radius: theme.radiusLg
            border.width: 1
            border.color: theme.outline
        }
        contentItem: ColumnLayout {
            spacing: 8
            Text {
                text: !tab.known ? (tab.editingProviderId ? "Custom AI" : "Add a custom AI") : tab.editingProviderId ? "Configure provider" : "Add provider"
                color: theme.text
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: parent.width
                    spacing: 8
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: !tab.editingProviderId && tab.addChoices.length > 1
                        spacing: 4
                        Text { text: "Provider"; color: theme.textDim; font.pixelSize: 11 }
                        Choice {
                            id: presetChoice
                            Layout.fillWidth: true
                            model: tab.addChoices
                            textRole: "name"
                            onActivated: index => tab.pickPreset(index)
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: tab.known && !tab.advanced
                        text: (tab.draft.name || "") + " · " + (tab.draft.baseUrl || "") + " — the endpoint and sign-in style are set for you. Choose a model and paste your API key."
                        color: theme.textDim
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: tab.known && !tab.advanced
                        spacing: 4
                        Text { text: "Model"; color: theme.textDim; font.pixelSize: 11 }
                        Choice {
                            id: modelChoice
                            Layout.fillWidth: true
                            model: tab.draft.modelPresets || []
                            currentIndex: Math.max(0, (tab.draft.modelPresets || []).indexOf(tab.draft.modelId))
                            onActivated: index => tab.setDraft("modelId", (tab.draft.modelPresets || [])[index])
                        }
                    }
                    Text { // a custom AI: whatever speaks one of the two protocols
                        Layout.fillWidth: true
                        visible: !tab.known
                        text: "Any OpenAI-compatible server (Ollama, LM Studio, vLLM, OpenRouter, a company gateway) or an Anthropic-style endpoint. Example address: http://localhost:11434/v1. Leave the key empty for a local server."
                        color: theme.textDim
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: tab.full
                        Field { id: nameField; label: "Name"; placeholder: "My AI"; value: tab.draft.name || ""; onEdited: text => tab.setDraft("name", text) }
                        Field { id: modelField; label: "Model ID"; placeholder: "llama3.1:8b"; value: tab.draft.modelId || ""; mono: true; onEdited: text => tab.setDraft("modelId", text) }
                    }
                    Field { id: urlField; show: tab.full; label: "Base URL"; placeholder: "https://…/v1"; value: tab.draft.baseUrl || ""; mono: true; onEdited: text => tab.setDraft("baseUrl", text) }
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: tab.full
                        Text { text: "Protocol"; color: theme.textDim; font.pixelSize: 11 }
                        Choice {
                            id: protocolChoice
                            Layout.fillWidth: true
                            model: [{id: "openai", name: "OpenAI-compatible (most servers)"}, {id: "anthropic", name: "Anthropic (Claude-style)"}]
                            textRole: "name"
                            valueRole: "id"
                            currentIndex: tab.draft.protocol === "anthropic" ? 1 : 0
                            onActivated: tab.setDraft("protocol", currentValue)
                        }
                    }
                    Field {
                        id: keyField
                        label: tab.editingProviderId && tab.draft.hasApiKey ? "API key — leave blank to keep the saved key" : tab.known ? "API key" : "API key (empty if the server needs none)"
                        echoMode: TextInput.Password
                        enabled: !clearKey.checked
                        onEdited: text => keyField.value = text
                    }
                    PillButton {
                        id: clearKey
                        visible: !!tab.editingProviderId && !!tab.draft.hasApiKey
                        text: "Clear saved API key on save"
                        checkable: true
                    }
                    PillButton {
                        text: tab.advanced ? "Hide advanced" : "Advanced (sign-in style, tokens, context)"
                        onClicked: tab.advanced = !tab.advanced
                    }
                    Field {
                        id: authField
                        show: tab.extras
                        label: "Sign-in style (Bearer / api-key / x-api-key / none)"
                        value: tab.draft.authHeader || ""
                        mono: true
                        onEdited: text => tab.setDraft("authHeader", text)
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: tab.extras
                        Field { id: temperatureField; label: "Temperature (-1 = omit)"; value: String(tab.draft.temperature ?? 0.2); onEdited: text => tab.setDraft("temperature", text) }
                        Field { id: tokensField; label: "Max tokens"; value: String(tab.draft.maxTokens ?? 8192); onEdited: text => tab.setDraft("maxTokens", text) }
                    }
                    Field {
                        id: contextField
                        show: tab.extras
                        label: "Context window in tokens (0 = guess from the model)"
                        value: String(tab.draft.contextWindow ?? 0)
                        mono: true
                        onEdited: text => tab.setDraft("contextWindow", text)
                    }
                    Field {
                        id: tokenParameter
                        show: tab.extras
                        label: "Token limit parameter (max_tokens / max_completion_tokens)"
                        value: tab.draft.maxTokensParameter || "max_tokens"
                        onEdited: text => tab.setDraft("maxTokensParameter", text)
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "Keys are stored as owner-only plaintext, not encrypted. Code and prompts go to this endpoint."
                        color: theme.textDim
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                visible: !!tab.formError
                text: tab.formError
                color: theme.accentText
                font.pixelSize: 11
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                PillButton {
                    visible: !!tab.editingProviderId
                    text: "Delete provider…"
                    onClicked: {
                        tab.deleteProviderId = tab.editingProviderId
                        deleteProviderConfirm.open()
                    }
                }
                Item { Layout.fillWidth: true }
                PillButton { text: "Cancel"; onClicked: providerEditor.close() }
                PillButton { text: "Save"; checked: true; onClicked: tab.saveProvider() }
            }
        }
    }
}
