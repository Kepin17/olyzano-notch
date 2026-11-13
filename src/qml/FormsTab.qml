import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Forms: ask people questions on a page you share with a QR code (on this network), and collect their answers and files. Build a form, share
// it, watch the answers come in, take them out as a table or as files. The answers stay on this computer.
ColumnLayout {
    id: tab
    required property var shell
    property string view: "list"     // list | edit | share | answers
    property var draft: ({ fields: [], limits: {} })
    property string currentId: ""
    property string problem: ""
    property string answersPart: "answers" // for pictures
    property string editPart: "questions" // for pictures: which part of the editor opens
    property bool dates: false      // the calendar is open even before a range is picked
    property string address: ""
    property string pin: ""
    property string confirmId: ""    // the form whose delete is waiting for a second press
    property var table: ({ columns: [], rows: [] })
    readonly property var current: { for (const r of forms.list) if (r.id === currentId) return r; return null }
    // [type, name, glyph on its tile, what it is for]
    readonly property var kinds: [["short", "Short answer", "Aa", "A name, a word, a line"], ["long", "Paragraph", "¶", "A longer message"], ["choice", "Pick one", "◉", "Choose from a list"], ["multi", "Pick many", "☑", "Tick all that apply"],
                                  ["number", "Number", "#", "How many, how much"], ["email", "Email", "@", "An email address"], ["date", "Date", "31", "A day on the calendar"], ["file", "Upload files", "↑", "Photos, documents"], ["rating", "Rating", "★", "Stars, from 1 up"], ["scale", "Scale", "1–5", "A number on a scale"], ["signature", "Signature", "✍", "Drawn on the screen"], ["section", "Section", "§", "A heading that groups questions"]]
    function kindName(t) { for (const k of kinds) if (k[0] === t) return k[1]; return t }
    function kindGlyph(t) { for (const k of kinds) if (k[0] === t) return k[2]; return "?" }
    function scheduled(r) { return r.running && r.opensAt > Date.now() / 1000 }
    function statusName(r) { return scheduled(r) ? "Scheduled" : r.running ? "Live" : r.status === "closed" ? "Closed" : "Draft" }
    function statusColor(r) { return scheduled(r) ? theme.warn : r.running ? theme.success : theme.textFaint }
    function summary(r) {
        const n = r.questions + (r.questions === 1 ? " question" : " questions")
        const a = r.answers + (r.answers === 1 ? " answer" : " answers")
        if (scheduled(r)) return n + " · " + a + " · opens " + new Date(r.opensAt * 1000).toLocaleString(Qt.locale(), "d MMM, HH:mm")
        if (r.running) return n + " · " + r.answers + " of " + r.max + " answers · " + r.people + (r.people === 1 ? " person" : " people")
        return n + " · " + a
    }
    function openShare(r) {
        currentId = r.id; shareError = ""
        if (r.running) { view = "share"; return }
        draft = forms.loadForm(r.id)
        const why = forms.validate(draft)
        if (why !== "") { problem = why; view = "edit"; return }
        address = ""; pin = ""; view = "share"
    }
    function openAnswers(id) { currentId = id; table = forms.responses(id); view = "answers" }
    // rules: a question that waits for an earlier choice, and a choice that ends the form
    function earlier(i) { return draft.fields.slice(0, i).filter(f => f.type === "choice" || f.type === "multi") }
    function showOn(i) { const r = draft.fields[i].showIf; return !!(r && r.field) }
    function setShow(i, fieldId, values) { const f = clone(draft.fields); f[i].showIf = { field: fieldId, values: values }; restructure(f) }
    function toggleShowValue(i, v) {
        const r = draft.fields[i].showIf, vals = (r.values || []).slice(), at = vals.indexOf(v)
        if (at >= 0) vals.splice(at, 1); else vals.push(v)
        setShow(i, r.field, vals)
    }
    function toggleEnd(i, v) { const f = clone(draft.fields); const e = f[i].endOn || []; const at = e.indexOf(v); if (at >= 0) e.splice(at, 1); else e.push(v); f[i].endOn = e; restructure(f) }
    function removeOption(i, k) { const f = clone(draft.fields); const gone = f[i].options.splice(k, 1)[0]; if (f[i].optionLimits) delete f[i].optionLimits[gone]; restructure(f) }
    function renameOption(i, k, text) { // typing: the page is not rebuilt; a limit goes with the name
        const fld = draft.fields[i], old = fld.options[k]
        fld.options[k] = text
        if (fld.optionLimits && fld.optionLimits[old] !== undefined) { fld.optionLimits[text] = fld.optionLimits[old]; delete fld.optionLimits[old] }
    }
    function optionLimit(i, option) { const l = (draft.fields[i].optionLimits || {})[option]; return l ? String(l) : "" }
    function setOptionLimit(i, option, text) { // "" = as many as come
        const fld = draft.fields[i]
        fld.optionLimits = fld.optionLimits || {}
        const n = parseInt(text, 10)
        if (n >= 1) fld.optionLimits[option] = Math.min(1000, n); else delete fld.optionLimits[option]
    }
    function addOption(i) { const f = clone(draft.fields); f[i].options.push(""); restructure(f) }
    function startSharing() {
        if (shareMode === "relay") { publishing = true; forms.publishRelay(currentId, serverId !== "" ? serverId : goodServers[0].id, pin) }
        else shareError = forms.publish(currentId, address, pin)
        pin = ""
    }
    function qrSource(text) { return "image://qr/" + Qt.btoa(text).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "") }
    function clone(o) { return JSON.parse(JSON.stringify(o)) }
    function startNew(which) { draft = forms.newForm(which); dates = false; problem = ""; view = "edit" }
    function edit(id) { draft = forms.loadForm(id); dates = false; problem = ""; view = "edit" }
    function field(i, key, value) { draft.fields[i][key] = value } // typing: the page is not rebuilt, so the cursor stays
    function restructure(fields) { const d = clone(draft); d.fields = fields; draft = d }
    function addField(type) { const f = clone(draft.fields); f.push(forms.newField(type)); restructure(f) }
    function moveField(i, by) { const f = clone(draft.fields); const j = i + by; if (j < 0 || j >= f.length) return; const t = f[i]; f[i] = f[j]; f[j] = t; restructure(f) }
    function removeField(i) { const f = clone(draft.fields); f.splice(i, 1); restructure(f) }
    function setEnding(key, value) { draft.ending = draft.ending || {}; draft.ending[key] = value }
    function setFilesToShelf(v) { const d = clone(draft); d.filesToShelf = v; draft = d }
    function setOneAnswer(v) { const d = clone(draft); d.oneAnswer = v; draft = d }
    function setAllowEdit(v) { const d = clone(draft); d.allowEdit = v; draft = d }
    function setLayout(v) { const d = clone(draft); d.layout = v; draft = d }
    function setSchedule(a, b) { const d = clone(draft); d.limits.opensAt = a; d.limits.closesAt = b; draft = d }
    function setLimit(key, value) { const d = clone(draft); d.limits[key] = value; if (key === "maxFileMb") d.limits.maxTotalMb = Math.min(4096, Math.max(100, value * 4)); draft = d }
    function save(next) {
        problem = ""
        if (!forms.saveForm(draft)) { problem = "It could not be saved (a form that is open cannot be changed: close it first)."; return false }
        currentId = draft.id
        view = next
        return true
    }
    function share() {
        const why = forms.validate(draft)
        if (why !== "") { problem = why; return }
        if (save("share")) { address = ""; pin = ""; shareError = "" }
    }
    property string shareError: ""
    property string shareMode: "local"   // local | relay
    property string serverId: ""
    property bool publishing: false
    Timer { id: lateScroll; property var go; interval: 600; onTriggered: go() } // pictures only
    readonly property var servers: { forms.list; share.servers; return forms.relayServers() }
    readonly property var goodServers: servers.filter(s => s.ok)
    // for pictures: `notch --shot-page x.png docs-forms` with NOTCH_SHOT_ACTION=edit | share | answers
    function debug(action) {
        if (action === "edit") startNew("send")
        else if (action === "edit-look" || action === "edit-settings") { startNew("send"); editPart = action.substring(5); const d = clone(draft); d.layout = "sections"; d.ending = { title: "You are in!", message: "Join the group to hear what comes next.", buttonLabel: "Join the group", buttonUrl: "https://chat.example.com/join" }; draft = d }
        else if (action === "edit-rules") { startNew("rsvp"); const d = clone(draft); d.fields[1].endOn = ["No"]; d.fields[2].showIf = { field: d.fields[1].id, values: ["Yes", "Maybe"] }; draft = d; Qt.callLater(() => lateScroll.start()); lateScroll.go = () => { let p = tab.parent; while (p && !("contentY" in p)) p = p.parent; if (p) p.contentY = 330 } }
        else if (action === "summary") { currentId = forms.debugSample(); draft = forms.loadForm(currentId); table = forms.responses(currentId); view = "answers"; answersPart = "summary" }
        else if (action === "edit-new") { startNew("blank"); const d = clone(draft); const mk = t => forms.newField(t); const day = mk("choice"); day.label = "Which day?"; day.options = ["Saturday", "Sunday"]; day.optionLimits = { "Saturday": 10 }
            const stars = mk("rating"); stars.label = "How was it?"; const sc = mk("scale"); sc.label = "Would you come again?"; sc.scaleMax = 7; sc.lowLabel = "Never"; sc.highLabel = "Always"; const sg = mk("signature"); sg.label = "Sign here"
            d.fields = [stars, sc, day, sg]; draft = d }
        else if (action === "list") view = "list"
        else if (action === "edit-late") { // the lower half of the builder: no questions, so the schedule and the ending are in the picture
            startNew("send"); const d = clone(draft); d.fields = []; d.layout = "sections"
            const t = new Date(); t.setDate(t.getDate() + 2); t.setHours(9, 0, 0, 0); const u = new Date(t); u.setDate(u.getDate() + 3); u.setHours(17, 0, 0, 0)
            d.limits.opensAt = Math.floor(t.getTime() / 1000); d.limits.closesAt = Math.floor(u.getTime() / 1000)
            d.ending = { title: "You are in!", message: "Join the group to hear what comes next.", buttonLabel: "Join the group", buttonUrl: "https://chat.example.com/join" }
            draft = d
            Qt.callLater(() => lateScroll.start())
            lateScroll.go = () => { let p = tab.parent; while (p && !("contentY" in p)) p = p.parent; if (p) p.contentY = Math.max(0, p.contentHeight - p.height + 18) }
        }
        else if (action === "share" || action === "answers") {
            currentId = forms.debugSample()
            draft = forms.loadForm(currentId)
            table = forms.responses(currentId)
            view = action
        }
    }
    Connections { target: forms; function onPublishFinished(id, error) { tab.publishing = false; tab.shareError = error } }
    Connections { target: forms; function onChanged() { if (tab.view === "answers") tab.table = forms.responses(tab.currentId) } }
    spacing: 10
    Layout.fillWidth: true

    FormsList { visible: tab.view === "list"; tab: tab }
    FormEditor { visible: tab.view === "edit"; tab: tab }
    FormShare { visible: tab.view === "share"; tab: tab }
    FormAnswers { visible: tab.view === "answers"; tab: tab }
}
