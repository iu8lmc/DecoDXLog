// DecoDXLog — la finestra del DX cluster: spot, fonti, avvisi, voce e console.
// Pensata per stare aperta di fianco a Decodium, anche su un altro monitor.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import Decodium.UI

RoundedWindow {
    id: root

    property int tab: 0
    // In contest il cluster e' una cosa sola: i filtri e gli spot, con i
    // moltiplicatori che mancano in evidenza. Le altre schede — fonti, avvisi,
    // console — durante una gara sono in mezzo ai piedi.
    property bool contestMode: false
    readonly property var cluster: decolog.cluster

    width: 1280
    height: 760
    minimumWidth: 820
    minimumHeight: 480
    visible: true
    title: qsTr("DecoDXLog — DX Cluster")
    surfaceColor: Theme.bgDeep
    transientParent: null
    modality: Qt.NonModal

    OnScreen { target: root }

    // Niente barra di Windows: la testata e' la nostra, piu' bassa, con gli
    // stessi comandi. Si sposta dalla testata e si ridimensiona dai bordi.
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowMinimizeButtonHint | Qt.WindowMaximizeButtonHint
    header: WindowTitleBar { window: root; cornerRadius: root.frameRadius }
    WindowChrome {
        window: root
        parent: root.contentItem.parent
        dragHeight: Theme.panelHeight
        cornerRadius: root.frameRadius
    }

    Settings {
        id: clusterWindowSettings
        category: "clusterWindow"
        property alias width: root.width
        property alias height: root.height
        // Anche la posizione: se la finestra sta sul secondo schermo, e' li'
        // che deve riaprirsi.
        property alias windowX: root.x
        property alias windowY: root.y
    }

    function persistPlacement() { clusterWindowSettings.sync() }
    Component.onDestruction: root.persistPlacement()

    component Section: SectionTitle { Layout.fillWidth: true }
    component Note: Text {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        color: Theme.textSecondary
        font.pixelSize: 12
    }
    component Card: Rectangle {
        default property alias content: cardBody.data
        Layout.fillWidth: true
        implicitHeight: cardBody.implicitHeight + 20
        radius: 6
        color: Theme.panelColor
        border.width: 1
        border.color: Theme.borderSoft
        ColumnLayout {
            id: cardBody
            anchors.fill: parent
            anchors.margins: 10
            spacing: 8
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            Repeater {
                model: root.contestMode ? [qsTr("Spots")]
                                        : [qsTr("Spots"), qsTr("Sources"), qsTr("Alerts"),
                                           qsTr("Voice & LoTW"), qsTr("Console")]
                TabChip {
                    required property string modelData
                    required property int index
                    text: modelData
                    active: root.tab === index
                    badge: index === 1 ? root.cluster.onlineCount + "/" + root.cluster.sources.length
                         : index === 2 ? String(root.cluster.alertRules.filter(r => r.enabled).length) : ""
                    onClicked: root.tab = index
                }
            }
            Item { Layout.fillWidth: true }
            // Segnalare una stazione al cluster: dalla scheda di adesso, o scrivendola.
            GlassButton {
                visible: !root.contestMode
                text: qsTr("Send a spot…")
                tone: Theme.accentColor
                buttonHeight: 28
                onClicked: spotDialog.openCurrent()
            }
            Text {
                text: decolog.clientConnected ? qsTr("Decodium on %1 · %2").arg(decolog.dialBand || "—").arg(decolog.currentMode || "—")
                                              : qsTr("Decodium not connected")
                color: decolog.clientConnected ? Theme.accentColor : Theme.textSecondary
                font.family: Theme.monoFamily
                font.pixelSize: 12
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.tab

            // ── Spot ────────────────────────────────────────────────────────
            ClusterPanel {
                contestMode: root.contestMode
                id: spotsPanel
                onWindowRequested: (t) => root.tab = t
            }

            // ── Fonti ───────────────────────────────────────────────────────
            ScrollView {
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        Section { text: qsTr("Spot sources") }
                        GlassButton {
                            text: qsTr("Add ▾")
                            tone: Theme.accentColor
                            filled: true
                            onClicked: presetMenu.popup()
                            StyledMenu {
                                id: presetMenu
                                Repeater {
                                    model: root.cluster.presets
                                    StyledMenuItem {
                                        required property var modelData
                                        required property int index
                                        text: "%1  ·  %2%3".arg(modelData.name).arg(modelData.host)
                                                           .arg(modelData.type === "pota" ? "" : ":" + modelData.port)
                                        onTriggered: root.cluster.addPreset(index)
                                    }
                                }
                                MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.borderSoft } }
                                StyledMenuItem {
                                    text: qsTr("Custom node…")
                                    onTriggered: sourceEditor.openFor({ id: "", name: "", type: "cluster", host: "", port: 7300, login: "", commands: "", enabled: true })
                                }
                            }
                        }
                    }
                    Note {
                        text: qsTr("All sources flow into one list. Telnet nodes log in with the callsign of the active station profile unless a login is set. "
                                   + "RBN gives skimmer spots (CW/RTTY on 7000, FT8/FT4 on 7001). HamAlert sends the spots of your triggers "
                                   + "(set them up on hamalert.org; the password goes in the system keystore below). POTA reads the public activation list every minute.")
                    }
                    Repeater {
                        model: root.cluster.sources
                        Card {
                            id: sourceCard
                            required property var modelData
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                Led {
                                    color: sourceCard.modelData.online ? Theme.accentColor
                                         : sourceCard.modelData.state === 0 ? Theme.borderSoft
                                         : sourceCard.modelData.state === 4 ? Theme.errorColor : Theme.warningColor
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text {
                                        text: sourceCard.modelData.name
                                        color: Theme.textPrimary
                                        font.family: Theme.monoFamily
                                        font.pixelSize: 13
                                        font.bold: true
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        text: "%1 · %2%3 · %4 · %5".arg(sourceCard.modelData.type.toUpperCase())
                                              .arg(sourceCard.modelData.host)
                                              .arg(sourceCard.modelData.type === "pota" ? "" : ":" + sourceCard.modelData.port)
                                              .arg(sourceCard.modelData.stateText)
                                              .arg(qsTr("%1 spots").arg(sourceCard.modelData.spotCount)
                                                   + (sourceCard.modelData.lastSpot ? qsTr(", last %1Z").arg(sourceCard.modelData.lastSpot) : ""))
                                        color: sourceCard.modelData.state === 4 ? Theme.warningColor : Theme.textSecondary
                                        font.pixelSize: 11
                                    }
                                }
                                ToggleSwitch {
                                    text: qsTr("On")
                                    checked: sourceCard.modelData.enabled
                                    onToggled: root.cluster.setSourceEnabled(sourceCard.modelData.id, checked)
                                }
                                GlassButton {
                                    visible: sourceCard.modelData.type !== "pota"
                                    enabled: !sourceCard.modelData.checking
                                    text: qsTr("Check")
                                    onClicked: root.cluster.checkSource(sourceCard.modelData.id)
                                }
                                GlassButton { text: qsTr("Edit"); onClicked: sourceEditor.openFor(sourceCard.modelData) }
                                GlassButton { text: qsTr("Remove"); tone: Theme.errorColor; onClicked: root.cluster.removeSource(sourceCard.modelData.id) }
                            }
                            // Il risultato di «Check»: dove si ferma il collegamento e cosa fare.
                            Text {
                                Layout.fillWidth: true
                                visible: text.length > 0
                                text: sourceCard.modelData.checkText
                                wrapMode: Text.Wrap
                                color: Theme.textSecondary
                                font.pixelSize: 11
                            }
                        }
                    }
                    Section { text: qsTr("HamAlert account") }
                    CredentialsList { Layout.fillWidth: true; serviceIds: ["hamalert"] }
                    Item { Layout.preferredHeight: 10 }
                }
            }

            // ── Avvisi ──────────────────────────────────────────────────────
            ScrollView {
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        Section { text: qsTr("Alert rules") }
                        GlassButton {
                            text: qsTr("New rule")
                            tone: Theme.accentColor
                            filled: true
                            onClicked: ruleEditor.openFor({ id: "", name: "", enabled: true, voice: true, decodium: true, filter: { maxAgeMinutes: 10 } })
                        }
                    }
                    Note {
                        text: qsTr("A spot that matches a rule is written in the activity log and highlighted; if the rule says so it is sent to Decodium, "
                                   + "announced by voice, shown in a popup (click it to tune the radio) and played as a sound. "
                                   + "The same DX on the same band and mode alerts at most once every %1 minutes.")
                              .arg(root.cluster.voiceCooldownMinutes)
                    }
                    Repeater {
                        model: root.cluster.alertRules
                        Card {
                            id: ruleCard
                            required property var modelData
                            readonly property var f: modelData.filter || {}
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                ToggleSwitch {
                                    checked: ruleCard.modelData.enabled
                                    onToggled: {
                                        const r = Object.assign({}, ruleCard.modelData)
                                        r.enabled = checked
                                        root.cluster.saveAlertRule(r)
                                    }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text { text: ruleCard.modelData.name; color: Theme.textPrimary; font.pixelSize: 13; font.bold: true }
                                    Text {
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        color: Theme.textSecondary
                                        font.family: Theme.monoFamily
                                        font.pixelSize: 11
                                        text: {
                                            const parts = []
                                            const st = root.cluster.statusNames.filter(s => ((ruleCard.f.anyStatus || 0) & s.bit) !== 0).map(s => s.label)
                                            if (st.length) parts.push(st.join("|"))
                                            if ((ruleCard.f.calls || "").length) parts.push(ruleCard.f.calls)
                                            if ((ruleCard.f.bands || []).length) parts.push(ruleCard.f.bands.join(" "))
                                            if ((ruleCard.f.modes || []).length) parts.push(ruleCard.f.modes.join(" "))
                                            if ((ruleCard.f.dxContinents || []).length) parts.push(qsTr("DX in %1").arg(ruleCard.f.dxContinents.join(" ")))
                                            if ((ruleCard.f.spotterContinents || []).length) parts.push(qsTr("spotted from %1").arg(ruleCard.f.spotterContinents.join(" ")))
                                            if (ruleCard.f.onlyActivations) parts.push("POTA/SOTA/WWFF/IOTA")
                                            if (ruleCard.f.hideWorkedBand) parts.push(qsTr("not worked on band"))
                                            return parts.length ? parts.join(" · ") : qsTr("every spot")
                                        }
                                    }
                                }
                                Pill { visible: ruleCard.modelData.voice; text: qsTr("voice"); tone: Theme.accentColor }
                                Pill { visible: ruleCard.modelData.decodium; text: "Decodium"; tone: Theme.primaryColor }
                                Pill { visible: ruleCard.modelData.popup === true; text: qsTr("popup"); tone: Theme.warningColor }
                                Pill { visible: ruleCard.modelData.sound === true; text: qsTr("sound"); tone: Theme.secondaryColor }
                                GlassButton { text: qsTr("Edit"); onClicked: ruleEditor.openFor(ruleCard.modelData) }
                                GlassButton { text: qsTr("Delete"); tone: Theme.errorColor; onClicked: root.cluster.removeAlertRule(ruleCard.modelData.id) }
                            }
                        }
                    }
                    Item { Layout.preferredHeight: 10 }
                }
            }

            // ── Voce e LoTW ─────────────────────────────────────────────────
            ScrollView {
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: 12
                    Section { text: qsTr("Voice announcements") }
                    Note {
                        text: root.cluster.voiceAvailable
                              ? qsTr("Speech: %1. Voices come from Windows Settings → Time & language → Speech.").arg(root.cluster.voiceBackend)
                              : qsTr("No speech synthesizer found (install speech-dispatcher or espeak-ng on Linux).")
                    }
                    RowLayout {
                        spacing: 16
                        ToggleSwitch { text: qsTr("Announce alerts"); checked: root.cluster.voiceEnabled; onToggled: root.cluster.voiceEnabled = checked }
                        ToggleSwitch { text: qsTr("Spell calls with the phonetic alphabet"); checked: root.cluster.voicePhonetic; onToggled: root.cluster.voicePhonetic = checked }
                    }
                    RowLayout {
                        spacing: 12
                        LabeledField {
                            label: qsTr("Voice")
                            StyledComboBox {
                                Layout.preferredWidth: 320
                                model: [qsTr("System default")].concat(root.cluster.voices)
                                currentIndex: Math.max(0, root.cluster.voices.indexOf(root.cluster.voiceName) + 1)
                                onActivated: root.cluster.voiceName = currentIndex === 0 ? "" : currentText
                            }
                        }
                        LabeledField {
                            label: qsTr("Language of the sentences")
                            StyledComboBox {
                                Layout.preferredWidth: 140
                                readonly property var values: ["it", "en"]
                                model: ["Italiano", "English"]
                                currentIndex: Math.max(0, values.indexOf(root.cluster.voiceLanguage))
                                onActivated: root.cluster.voiceLanguage = values[currentIndex]
                            }
                        }
                        LabeledField {
                            label: qsTr("Repeat the same DX after")
                            StyledComboBox {
                                Layout.preferredWidth: 120
                                readonly property var values: [5, 10, 20, 30, 60]
                                model: ["5 min", "10 min", "20 min", "30 min", "60 min"]
                                currentIndex: Math.max(0, values.indexOf(root.cluster.voiceCooldownMinutes))
                                onActivated: root.cluster.voiceCooldownMinutes = values[currentIndex]
                            }
                        }
                    }
                    RowLayout {
                        spacing: 12
                        Text { text: qsTr("Speed"); color: Theme.textSecondary; font.pixelSize: 12 }
                        Slider {
                            Layout.preferredWidth: 200
                            from: -5; to: 5; stepSize: 1
                            value: root.cluster.voiceRate
                            onMoved: root.cluster.voiceRate = value
                        }
                        Text { text: qsTr("Volume"); color: Theme.textSecondary; font.pixelSize: 12 }
                        Slider {
                            Layout.preferredWidth: 200
                            from: 0; to: 100; stepSize: 5
                            value: root.cluster.voiceVolume
                            onMoved: root.cluster.voiceVolume = value
                        }
                        GlassButton { text: qsTr("Test"); tone: Theme.accentColor; enabled: root.cluster.voiceAvailable; onClicked: root.cluster.testVoice() }
                        GlassButton { text: qsTr("Stop"); onClicked: root.cluster.stopVoice() }
                    }
                    Note {
                        text: qsTr("Example: “New DXCC. Bouvet. 3 Y 0 J. 20 metri. C W.” Keep the announcements on the PC speakers, "
                                   + "not on the audio device that goes to the radio.")
                    }

                    Section { text: qsTr("LoTW users") }
                    RowLayout {
                        spacing: 12
                        Note { Layout.fillWidth: false; text: root.cluster.lotwUsersInfo || qsTr("list not loaded yet") }
                        GlassButton { text: qsTr("Update now"); onClicked: root.cluster.refreshLotwUsers() }
                    }
                    Note { text: qsTr("The ARRL list of LoTW users marks spots of stations that upload to LoTW (filter “LoTW users”). It is refreshed once a week.") }

                    Section { text: qsTr("Decodium") }
                    ToggleSwitch {
                        text: qsTr("Send the spots shown and the alerts to Decodium (DecoLink)")
                        checked: root.cluster.sendToDecodium
                        onToggled: root.cluster.sendToDecodium = checked
                    }
                    Note { text: qsTr("Double-click a spot to tune Decodium: dial frequency and mode, and the DX call ready in the QSO panel. It never starts transmitting.") }
                    Item { Layout.preferredHeight: 10 }
                }
            }

            // ── Console ─────────────────────────────────────────────────────
            GlassPanel {
                title: qsTr("Console")
                padding: 8
                headerTools: [
                    GlassButton { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Clear"); buttonHeight: 24; fontPixelSize: 11; onClicked: root.cluster.clearConsole() }
                ]
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8
                    ListView {
                        id: consoleView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: root.cluster.console
                        ScrollBar.vertical: ScrollBar {}
                        onCountChanged: positionViewAtEnd()
                        delegate: Text {
                            required property string modelData
                            width: ListView.view.width
                            text: modelData
                            elide: Text.ElideRight
                            color: modelData.indexOf("│ >") >= 0 ? Theme.accentColor : modelData.indexOf("│ —") >= 0 ? Theme.warningColor : Theme.textPrimary
                            font.family: Theme.monoFamily
                            font.pixelSize: 12
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        StyledComboBox {
                            id: consoleTarget
                            Layout.preferredWidth: 220
                            readonly property var ids: [""].concat(root.cluster.sources.filter(s => s.type !== "pota").map(s => s.id))
                            model: [qsTr("First node online")].concat(root.cluster.sources.filter(s => s.type !== "pota").map(s => s.name))
                        }
                        StyledTextField {
                            id: commandField
                            Layout.fillWidth: true
                            placeholderText: "sh/dx 20 · sh/wwv · set/skimmer · set/ft8 …"
                            Keys.onReturnPressed: sendButton.clicked()
                        }
                        GlassButton {
                            id: sendButton
                            text: qsTr("Send")
                            tone: Theme.primaryColor
                            filled: true
                            onClicked: {
                                consoleError.text = root.cluster.sendCommand(consoleTarget.ids[consoleTarget.currentIndex] || "", commandField.text)
                                if (!consoleError.text.length) commandField.text = ""
                            }
                        }
                    }
                    Text { id: consoleError; visible: text.length > 0; color: Theme.errorColor; font.pixelSize: 12 }
                }
            }
        }
    }

    SpotDialog { id: spotDialog }

    // ── Modifica di una fonte ───────────────────────────────────────────────
    Popup {
        id: sourceEditor
        property var source: ({})
        property bool passwordRemoved: false
        function openFor(s) {
            source = Object.assign({}, s)
            passwordRemoved = false
            srcName.text = s.name || ""
            srcHost.text = s.host || ""
            srcPort.text = s.port || ""
            srcLogin.text = s.login || ""
            srcPassword.text = ""
            srcCommands.text = s.commands || ""
            srcType.currentIndex = Math.max(0, srcType.values.indexOf(s.type || "cluster"))
            open()
        }
        anchors.centerIn: Overlay.overlay
        modal: true
        padding: 16
        width: 560
        background: Rectangle { color: Theme.panelColor; border.color: Theme.glassBorder; radius: 6 }
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Text { text: sourceEditor.source.id ? qsTr("Edit source") : qsTr("New source"); color: Theme.textPrimary; font.pixelSize: 15; font.bold: true }
            RowLayout {
                spacing: 10
                LabeledField { Layout.fillWidth: true; label: qsTr("Name"); StyledTextField { id: srcName; Layout.fillWidth: true; mono: false } }
                LabeledField {
                    label: qsTr("Type")
                    StyledComboBox {
                        id: srcType
                        Layout.preferredWidth: 150
                        readonly property var values: ["cluster", "rbn", "hamalert", "pota"]
                        model: [qsTr("DX cluster"), "RBN", "HamAlert", "POTA"]
                    }
                }
            }
            RowLayout {
                spacing: 10
                LabeledField { Layout.fillWidth: true; label: qsTr("Host"); StyledTextField { id: srcHost; Layout.fillWidth: true } }
                LabeledField { label: qsTr("Port"); StyledTextField { id: srcPort; Layout.preferredWidth: 90; validator: IntValidator { bottom: 1; top: 65535 } } }
            }
            LabeledField {
                Layout.fillWidth: true
                label: srcType.currentIndex === 2 ? qsTr("HamAlert username") : qsTr("Login (empty = station profile callsign)")
                StyledTextField { id: srcLogin; Layout.fillWidth: true }
            }
            LabeledField {
                Layout.fillWidth: true
                visible: srcType.values[srcType.currentIndex] === "cluster" || srcType.values[srcType.currentIndex] === "rbn"
                label: qsTr("Node password (only if requested)")
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    StyledTextField {
                        id: srcPassword
                        Layout.fillWidth: true
                        echoMode: TextInput.Password
                        placeholderText: sourceEditor.source.passwordStored && !sourceEditor.passwordRemoved
                                         ? qsTr("stored in the system keystore — leave empty to keep it") : ""
                        Keys.onReturnPressed: saveSourceButton.clicked()
                    }
                    GlassButton {
                        text: qsTr("Remove saved password")
                        tone: Theme.errorColor
                        visible: sourceEditor.source.id && sourceEditor.source.passwordStored && !sourceEditor.passwordRemoved
                        onClicked: {
                            root.cluster.removeSourcePassword(sourceEditor.source.id)
                            sourceEditor.passwordRemoved = true
                            srcPassword.text = ""
                        }
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                visible: (sourceEditor.source.passwordStatus || "").length > 0
                text: sourceEditor.source.passwordStatus || ""
                wrapMode: Text.Wrap
                color: Theme.textSecondary
                font.pixelSize: 11
            }
            LabeledField {
                Layout.fillWidth: true
                label: qsTr("Commands after login (one per line)")
                ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 80
                    TextArea {
                        id: srcCommands
                        color: Theme.textPrimary
                        font.family: Theme.monoFamily
                        font.pixelSize: 12
                        placeholderText: "set/skimmer\nset/ft8\nsh/dx 30"
                        background: Rectangle { color: Theme.bgMedium; border.color: Theme.glassBorder; radius: 4 }
                    }
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8
                GlassButton { text: qsTr("Cancel"); onClicked: sourceEditor.close() }
                GlassButton {
                    id: saveSourceButton
                    text: qsTr("Save")
                    tone: Theme.accentColor
                    filled: true
                    enabled: srcHost.text.trim().length > 0
                    onClicked: {
                        const s = {
                            name: srcName.text.trim() || srcHost.text.trim(), type: srcType.values[srcType.currentIndex],
                            host: srcHost.text.trim(), port: parseInt(srcPort.text) || 23, login: srcLogin.text.trim(),
                            commands: srcCommands.text, enabled: sourceEditor.source.enabled !== false
                        }
                        let id = sourceEditor.source.id || ""
                        if (id) root.cluster.updateSource(id, s)
                        else id = root.cluster.addSource(s)
                        if (srcPassword.text.length > 0)
                            root.cluster.saveSourcePassword(id, srcPassword.text)
                        srcPassword.text = ""
                        sourceEditor.close()
                    }
                }
            }
        }
    }

    // ── Modifica di una regola ──────────────────────────────────────────────
    Popup {
        id: ruleEditor
        property var rule: ({})
        property var editedFilter: ({})
        function openFor(r) {
            rule = Object.assign({}, r)
            editedFilter = Object.assign({}, r.filter || {})
            ruleName.text = r.name || ""
            ruleVoice.checked = r.voice !== false
            ruleDecodium.checked = r.decodium !== false
            rulePopup.checked = r.popup === true
            ruleSound.checked = r.sound === true
            open()
        }
        anchors.centerIn: Overlay.overlay
        modal: true
        padding: 16
        width: Math.min(860, root.width - 40)
        background: Rectangle { color: Theme.panelColor; border.color: Theme.glassBorder; radius: 6 }
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Text { text: ruleEditor.rule.id ? qsTr("Edit alert rule") : qsTr("New alert rule"); color: Theme.textPrimary; font.pixelSize: 15; font.bold: true }
            RowLayout {
                spacing: 16
                LabeledField { Layout.preferredWidth: 280; label: qsTr("Name"); StyledTextField { id: ruleName; Layout.fillWidth: true; mono: false; placeholderText: qsTr("e.g. 3Y0J on any band") } }
                ToggleSwitch { id: ruleVoice; Layout.alignment: Qt.AlignBottom; text: qsTr("Announce by voice") }
                ToggleSwitch { id: ruleDecodium; Layout.alignment: Qt.AlignBottom; text: qsTr("Send to Decodium") }
                ToggleSwitch { id: rulePopup; Layout.alignment: Qt.AlignBottom; text: qsTr("Show a popup") }
                ToggleSwitch { id: ruleSound; Layout.alignment: Qt.AlignBottom; text: qsTr("Play a sound") }
            }
            SpotFilterEditor {
                Layout.fillWidth: true
                filter: ruleEditor.editedFilter
                onEdited: (f) => ruleEditor.editedFilter = f
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8
                GlassButton { text: qsTr("Cancel"); onClicked: ruleEditor.close() }
                GlassButton {
                    text: qsTr("Save rule")
                    tone: Theme.accentColor
                    filled: true
                    onClicked: {
                        root.cluster.saveAlertRule({ id: ruleEditor.rule.id || "", name: ruleName.text, enabled: ruleEditor.rule.enabled !== false,
                                                     voice: ruleVoice.checked, decodium: ruleDecodium.checked,
                                                     popup: rulePopup.checked, sound: ruleSound.checked, filter: ruleEditor.editedFilter })
                        ruleEditor.close()
                    }
                }
            }
        }
    }
}
