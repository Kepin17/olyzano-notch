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
    property string deleteSessionId: ""
    property string deleteProviderId: ""
    property string autoSessionId: ""

    function editProvider(id) { providerEditor.edit(id) } // DevPage opens it from outside
    function indexOfId(items, id) {
        for (let i = 0; i < items.length; ++i)
            if (items[i].id === id) return i
        return -1
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
    // While an answer arrives: how much of it is whole blocks (up to the last blank line, and never inside an open code fence)
    function stableLength(t) {
        let end = t.lastIndexOf("\n\n")
        if (end < 0) return 0
        const fences = t.slice(0, end).match(/^```/gm)
        if (fences && fences.length % 2 === 1) end = t.lastIndexOf("\n\n", t.lastIndexOf("```", end))
        return end < 0 ? 0 : end + 2
    }
    // The paragraph still being written, without its markers (## and ** are noise until it can be drawn properly)
    function tidy(t) { return t.replace(/^#{1,6}\s+/gm, "").replace(/\*\*/g, "").replace(/^\s*[-*]\s+/gm, "• ") }
    // Markdown as a document (room between paragraphs, bullets with air, chips for code): the plan reader and the agent's answers
    function renderDoc(text) {
        return codingAgent.renderMarkdown(text, Qt.application.font.family, 14, theme.dark ? "#ff9b8e" : "#c2362b",
                                          theme.dark ? "#1fffffff" : "#14000000", theme.accentText, theme.dark ? "#33ffffff" : "#26000000")
    }
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
        if (!hasKey) { providerEditor.edit(codingAgent.activeProviderId); return }
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

        AgentSidebar {
            tab: tab
            model: sidebarModel
            onAddFolder: workspacePicker.start(codingAgent.workspace)
            onDeleteTask: sid => { tab.deleteSessionId = sid; deleteSessionConfirm.open() }
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
                AgentChoice {
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
                    onClicked: providerEditor.edit(codingAgent.activeProviderId)
                }
                PillButton {
                    iconOnly: true; iconName: "plus"; text: "Add a custom AI or another provider"
                    implicitHeight: 32
                    enabled: !tab.locked
                    onClicked: providerEditor.edit("")
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
                        AgentCopyText {
                            visible: !!text && (tab.detailFull || message.thought)
                            readonly property bool doc: (message.msg.reasoning || "").length < 12000
                            text: doc ? tab.renderDoc(message.msg.reasoning || "") : (message.msg.reasoning || "")
                            textFormat: doc ? TextEdit.RichText : TextEdit.PlainText
                            color: theme.textDim
                            font.pixelSize: 13
                            onLinkActivated: link => codingAgent.openLink(link)
                        }
                        AgentCopyText {
                            visible: !!text && message.msg.role !== "shell" && message.msg.role !== "note"
                            // an answer is laid out as a document (spacing, bullets, code chips); a very long one as plain text, because
                            // laying out a hundred thousand characters of Markdown stalls the panel
                            readonly property bool doc: message.msg.role === "assistant" && (message.msg.content || "").length < 12000
                            text: doc ? tab.renderDoc(message.msg.content) : (message.msg.shown || message.msg.content || "")
                            textFormat: doc ? TextEdit.RichText : TextEdit.PlainText // a longer answer is plain text: Markdown of it would also load its pictures
                            onLinkActivated: link => codingAgent.openLink(link)
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
                                    AgentCopyText {
                                        visible: !!text && message.msg.role !== "shell"
                                        text: tool.modelData.summary || tool.modelData.filePath || ""
                                        font.pixelSize: 13
                                    }
                                    AgentCopyText {
                                        visible: !!text && message.msg.role !== "shell"
                                        text: tool.modelData.command || ""
                                        font.family: "monospace"
                                        font.pixelSize: 12
                                    }
                                    AgentCopyText {
                                        visible: !!text
                                        text: tool.modelData.diffText || ""
                                        font.family: "monospace"
                                        font.pixelSize: 12
                                    }
                                    AgentCopyText {
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
                            ColumnLayout { // the finished paragraphs are drawn as a document as they arrive; the one being written, plain
                                id: streamCol
                                readonly property string text: codingAgent.streamingContent
                                readonly property int cutAt: tab.stableLength(text)
                                readonly property string stable: text.slice(Math.max(0, cutAt - 8000), cutAt) // the end of it: laying out all of a long answer at each paragraph stalls the panel
                                Layout.fillWidth: true
                                visible: codingAgent.streaming
                                spacing: 4
                                Text {
                                    text: "Assistant · streaming…"
                                    color: theme.accentText
                                    font.pixelSize: 12
                                }
                                AgentCopyText {
                                    visible: !!text
                                    text: codingAgent.streamingReasoning
                                    color: theme.textDim
                                    font.pixelSize: 13
                                    font.italic: true
                                }
                                AgentCopyText {
                                    visible: streamCol.stable !== ""
                                    textFormat: TextEdit.RichText
                                    text: tab.renderDoc(streamCol.stable)
                                    onLinkActivated: link => codingAgent.openLink(link)
                                }
                                Text {
                                    Layout.fillWidth: true
                                    visible: text !== ""
                                    wrapMode: Text.Wrap
                                    color: theme.text
                                    font.pixelSize: 14
                                    text: tab.tidy(streamCol.text.slice(streamCol.cutAt)) || (streamCol.cutAt === 0 && streamCol.text === "" ? "Waiting for response…" : "")
                                }
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
                    AgentStep {
                        done: !!codingAgent.workspace
                        title: done ? "Workspace" : "Choose a workspace"
                        detail: codingAgent.workspace || "The folder the agent can read and change"
                        action: "Choose folder…"
                        onTriggered: workspacePicker.start(codingAgent.workspace)
                    }
                    AgentStep {
                        done: tab.hasKey
                        title: done ? "API key for " + (codingAgent.activeProvider.name || "") : "Add an API key for " + (codingAgent.activeProvider.name || "this provider")
                        detail: done ? "Saved on this computer" : "Pick the model and paste the key; the rest is filled in"
                        action: "Add key…"
                        onTriggered: providerEditor.edit(codingAgent.activeProviderId)
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

            AgentTodoCard { id: todoCard; tab: tab }

            AgentPlanCard {
                id: planCard
                tab: tab
                onRevise: { tab.revising = true; composer.forceActiveFocus() }
                onAcceptAuto: { tab.planAutoPending = true; autoConfirm.open() }
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
    AgentProviderEditor {
        id: providerEditor
        onSaved: Qt.callLater(() => composer.forceActiveFocus()) // ready to type right away
        onDeleteRequested: id => { tab.deleteProviderId = id; deleteProviderConfirm.open() }
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

}
