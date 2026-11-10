import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Add or configure an AI provider (Dev → Agent). `edit("")` starts a new one, `edit(id)` changes a saved one.
Popup {
    property var draft: ({})
    property string editingProviderId: ""
    property string formError: ""
    // A known provider (MiMo, DeepSeek, Claude, OpenAI) only needs a model and a key: its endpoint, protocol and
    // auth are filled in. Everything else shows for a custom endpoint, or under "Advanced".
    property bool known: false
    property bool advanced: false
    readonly property bool full: !known || advanced   // name, address, model
    readonly property bool extras: advanced           // sign-in style, temperature, tokens, context window
    property var addChoices: [] // presets not in the list yet, then "Custom endpoint"
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
    function edit(id) {
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
        open()
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
        close()
        saved()
    }
    signal saved()
    signal deleteRequested(string id)
    id: providerEditor
        parent: Overlay.overlay
        x: Math.max(12, (Overlay.overlay.width - width) / 2)
        y: Math.max(12, (Overlay.overlay.height - height) / 2)
        width: Math.min(620, Overlay.overlay.width - 24)
        height: Math.min(providerEditor.extras ? 520 : providerEditor.full ? 460 : 340, Overlay.overlay.height - 24)
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
                text: !providerEditor.known ? (providerEditor.editingProviderId ? "Custom AI" : "Add a custom AI") : providerEditor.editingProviderId ? "Configure provider" : "Add provider"
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
                        visible: !providerEditor.editingProviderId && providerEditor.addChoices.length > 1
                        spacing: 4
                        Text { text: "Provider"; color: theme.textDim; font.pixelSize: 11 }
                        AgentChoice {
                            id: presetChoice
                            Layout.fillWidth: true
                            model: providerEditor.addChoices
                            textRole: "name"
                            onActivated: index => providerEditor.pickPreset(index)
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: providerEditor.known && !providerEditor.advanced
                        text: (providerEditor.draft.name || "") + " · " + (providerEditor.draft.baseUrl || "") + " — the endpoint and sign-in style are set for you. Choose a model and paste your API key."
                        color: theme.textDim
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: providerEditor.known && !providerEditor.advanced
                        spacing: 4
                        Text { text: "Model"; color: theme.textDim; font.pixelSize: 11 }
                        AgentChoice {
                            id: modelChoice
                            Layout.fillWidth: true
                            model: providerEditor.draft.modelPresets || []
                            currentIndex: Math.max(0, (providerEditor.draft.modelPresets || []).indexOf(providerEditor.draft.modelId))
                            onActivated: index => providerEditor.setDraft("modelId", (providerEditor.draft.modelPresets || [])[index])
                        }
                    }
                    Text { // a custom AI: whatever speaks one of the two protocols
                        Layout.fillWidth: true
                        visible: !providerEditor.known
                        text: "Any OpenAI-compatible server (Ollama, LM Studio, vLLM, OpenRouter, a company gateway) or an Anthropic-style endpoint. Example address: http://localhost:11434/v1. Leave the key empty for a local server."
                        color: theme.textDim
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: providerEditor.full
                        Field { id: nameField; label: "Name"; placeholder: "My AI"; value: providerEditor.draft.name || ""; onEdited: text => providerEditor.setDraft("name", text) }
                        Field { id: modelField; label: "Model ID"; placeholder: "llama3.1:8b"; value: providerEditor.draft.modelId || ""; mono: true; onEdited: text => providerEditor.setDraft("modelId", text) }
                    }
                    Field { id: urlField; show: providerEditor.full; label: "Base URL"; placeholder: "https://…/v1"; value: providerEditor.draft.baseUrl || ""; mono: true; onEdited: text => providerEditor.setDraft("baseUrl", text) }
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: providerEditor.full
                        Text { text: "Protocol"; color: theme.textDim; font.pixelSize: 11 }
                        AgentChoice {
                            id: protocolChoice
                            Layout.fillWidth: true
                            model: [{id: "openai", name: "OpenAI-compatible (most servers)"}, {id: "anthropic", name: "Anthropic (Claude-style)"}]
                            textRole: "name"
                            valueRole: "id"
                            currentIndex: providerEditor.draft.protocol === "anthropic" ? 1 : 0
                            onActivated: providerEditor.setDraft("protocol", currentValue)
                        }
                    }
                    Field {
                        id: keyField
                        label: providerEditor.editingProviderId && providerEditor.draft.hasApiKey ? "API key — leave blank to keep the saved key" : providerEditor.known ? "API key" : "API key (empty if the server needs none)"
                        echoMode: TextInput.Password
                        enabled: !clearKey.checked
                        onEdited: text => keyField.value = text
                    }
                    PillButton {
                        id: clearKey
                        visible: !!providerEditor.editingProviderId && !!providerEditor.draft.hasApiKey
                        text: "Clear saved API key on save"
                        checkable: true
                    }
                    PillButton {
                        text: providerEditor.advanced ? "Hide advanced" : "Advanced (sign-in style, tokens, context)"
                        onClicked: providerEditor.advanced = !providerEditor.advanced
                    }
                    Field {
                        id: authField
                        show: providerEditor.extras
                        label: "Sign-in style (Bearer / api-key / x-api-key / none)"
                        value: providerEditor.draft.authHeader || ""
                        mono: true
                        onEdited: text => providerEditor.setDraft("authHeader", text)
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: providerEditor.extras
                        Field { id: temperatureField; label: "Temperature (-1 = omit)"; value: String(providerEditor.draft.temperature ?? 0.2); onEdited: text => providerEditor.setDraft("temperature", text) }
                        Field { id: tokensField; label: "Max tokens"; value: String(providerEditor.draft.maxTokens ?? 8192); onEdited: text => providerEditor.setDraft("maxTokens", text) }
                    }
                    Field {
                        id: contextField
                        show: providerEditor.extras
                        label: "Context window in tokens (0 = guess from the model)"
                        value: String(providerEditor.draft.contextWindow ?? 0)
                        mono: true
                        onEdited: text => providerEditor.setDraft("contextWindow", text)
                    }
                    Field {
                        id: tokenParameter
                        show: providerEditor.extras
                        label: "Token limit parameter (max_tokens / max_completion_tokens)"
                        value: providerEditor.draft.maxTokensParameter || "max_tokens"
                        onEdited: text => providerEditor.setDraft("maxTokensParameter", text)
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
                visible: !!providerEditor.formError
                text: providerEditor.formError
                color: theme.accentText
                font.pixelSize: 11
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                PillButton {
                    visible: !!providerEditor.editingProviderId
                    text: "Delete provider…"
                    onClicked: {
                        providerEditor.deleteRequested(providerEditor.editingProviderId)
                    }
                }
                Item { Layout.fillWidth: true }
                PillButton { text: "Cancel"; onClicked: providerEditor.close() }
                PillButton { text: "Save"; primary: true; onClicked: providerEditor.saveProvider() }
            }
        }
}
