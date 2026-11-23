// DecoDXLog — la sessione di attivazione o di contest: si apre, si conta, si esporta.
// Finche' e' aperta, ogni QSO prende la referenza, il locatore del posto,
// l'etichetta e il numero progressivo, e i duplicati si contano dentro la sessione.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Decodium.UI

DialogFrame {
    id: root

    readonly property var act: decolog.activation
    readonly property var state: act.state
    property var draft: ({})
    property var partitionPreview: ({})
    property bool previewShown: false
    property bool probeAwaitPreview: false
    property bool probeAwaitExport: false
    property int previewPage: 0
    property var exportGroup: ({})
    property string partitionMessage: ""

    // `kind` apre gia' sul tipo giusto: "contest" quando si arriva dal contest.
    function openDialog(kind) {
        draft = act.active ? Object.assign({}, root.state)
                           : { kind: kind || "pota", reference: "", name: "", contestId: "",
                               myGrid: decolog.myGrid, tag: "", band: "", mode: "",
                               serialEnabled: kind === "contest", nextSerial: 1 }
        errorText.text = ""
        partitionMessage = ""
        previewShown = false
        open()
    }
    function chooseContest() { contestPicker.openPicker() }
    function set(key, value) {
        const d = Object.assign({}, root.draft)
        d[key] = value
        root.draft = d
    }
    function setProgram(key, value) {
        const programs = Object.assign({}, root.draft.programs || {})
        if (String(value).trim().length) programs[key] = value
        else delete programs[key]
        root.set("programs", programs)
    }
    function probeMultiProgram() {
        root.openDialog("pota")
        root.set("reference", "IT-0001,IT-0002")
        root.setProgram("sota", "invalid")
        startSessionButton.clicked()
        console.warn("PROBE multiInvalid blocked=" + (!root.act.active && errorText.text.length > 0))
        root.setProgram("sota", "I/LM-001")
        root.setProgram("wwff", "IFF-0123")
        startSessionButton.clicked()
        console.warn("PROBE multiStarted active=" + root.act.active + " sota=" + root.act.state.programs.sota)
        const now = decolog.utcNow()
        const one = decolog.logManualQso({call: "K1AAA", date: decolog.showDate(now.date), time: now.time,
                                         band: "20m", mode: "CW", pota_ref: "US-1234", sota_ref: "W2/WE-003", wwff_ref: "KFF-4655"})
        root.draft = Object.assign({}, root.act.state)
        root.setProgram("sota", "invalid")
        applySessionButton.clicked()
        console.warn("PROBE multiInvalidUpdate blocked=" + (errorText.text.length > 0) + " sota=" + root.act.state.programs.sota)
        root.setProgram("sota", "I/LM-002")
        applySessionButton.clicked()
        const two = decolog.logManualQso({call: "K1BBB", date: decolog.showDate(now.date), time: now.time, band: "20m", mode: "CW"})
        console.warn("PROBE multiSaved count=" + decolog.currentLogInfo().qsos + " firstError=" + one + " secondError=" + two)
        root.probeAwaitPreview = true
        previewButton.clicked()
    }
    Connections {
        target: root.act
        function onChanged() { root.previewShown = false }
        function onPreviewChanged() {
            if (!root.act.previewBusy) {
                root.partitionPreview = root.act.previewResult
                root.previewShown = Object.keys(root.partitionPreview).length > 0
                root.previewPage = 0
            }
        }
        function onPartitionExportFinished(success, message) {
            errorText.text = success ? "" : message
            root.partitionMessage = success ? qsTr("Partition file saved. Review it before submission.") : ""
            if (root.probeAwaitExport) {
                root.probeAwaitExport = false
                console.warn("PROBE multiExport success=" + success + " error=" + message)
                root.act.stop()
            }
        }
        function onPreviewFinished(success) {
            if (root.probeAwaitPreview) {
                root.probeAwaitPreview = false
                console.warn("PROBE multiPreview reported=" + root.partitionPreview.reportedCount + " groups=" + (root.partitionPreview.groups || []).length + " error=" + (root.partitionPreview.error || ""))
                const group = (root.partitionPreview.groups || []).find(g => g.program === "POTA" && g.reference === "IT-0001")
                root.probeAwaitExport = true
                const started = group && root.act.startPartitionExport(group, "file://" + decolog.currentLogInfo().path + ".probe.adi")
                console.warn("PROBE multiExport started=" + !!started)
                if (!started) { root.probeAwaitExport = false; root.act.stop() }
            }
        }
    }
    readonly property var kindLabels: ({ pota: "POTA", sota: "SOTA", wwff: "WWFF", iota: "IOTA",
                                         contest: qsTr("Contest"), free: qsTr("Free session") })
    readonly property bool needsReference: ["pota", "sota", "wwff", "iota"].indexOf(draft.kind) >= 0

    title: act.active ? qsTr("Session · %1").arg(root.state.title) : qsTr("Contest and activations")
    dotColor: act.active ? Theme.accentColor : Theme.secondaryColor
    dialogKey: "activation"
    width: 760
    height: 700
    info: act.active ? qsTr("open since %1 UTC · %2").arg(String(root.state.startedAt).substring(11, 16)).arg(root.state.elapsed)
                     : ""

    FileDialog {
        id: historyDialog
        title: qsTr("Call history file (N1MM format)")
        nameFilters: [qsTr("Call history (*.txt *.csv)"), qsTr("All files (*)")]
        onAccepted: errorText.text = root.act.loadCallHistory(selectedFile)
    }

    // Le regole di questa edizione nel draft: solo quello che si cambia.
    function setRule(key, value) {
        const rules = Object.assign({}, root.draft.rules || {})
        if (value === undefined || value === null || value === "")
            delete rules[key]
        else
            rules[key] = value
        root.set("rules", rules)
    }
    readonly property var edition: { root.draft; root.state; return root.act.editionRules() }

    FileDialog {
        id: exportDialog
        title: qsTr("Export the session")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "adi"
        currentFile: "file:///" + root.state.fileName
        nameFilters: [qsTr("ADIF files (*.adi)")]
        onAccepted: errorText.text = root.act.exportAdif(selectedFile)
    }

    FileDialog {
        id: partitionExportDialog
        title: qsTr("Save programme partition")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "adi"
        nameFilters: [qsTr("ADIF files (*.adi)")]
        onAccepted: {
            if (!root.act.startPartitionExport(root.exportGroup, selectedFile))
                errorText.text = qsTr("Cannot start the export. Recalculate the preview and choose a different file.")
        }
    }

    component Note: Text {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        color: Theme.textSecondary
        font.pixelSize: 12
    }

    body: ScrollView {
        id: activationScroll
        anchors.margins: 14
        clip: true
        contentWidth: availableWidth
        ColumnLayout {
        width: activationScroll.availableWidth
        spacing: 12

        // ── Quello che sta succedendo ───────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            visible: root.act.active
            spacing: 8
            StatTile { Layout.fillWidth: true; label: qsTr("QSO"); value: root.act.qsoCount }
            StatTile { Layout.fillWidth: true; label: qsTr("Different calls"); value: root.act.uniqueCalls }
            StatTile { Layout.fillWidth: true; label: qsTr("Duration"); value: root.state.elapsed || "0:00" }
            StatTile { Layout.fillWidth: true; label: qsTr("Last QSO"); value: root.state.lastQso || "—" }
            StatTile {
                Layout.fillWidth: true
                visible: root.state.serialEnabled
                label: qsTr("Next number")
                value: root.act.nextSerial
            }
        }
        MeterBar {
            Layout.fillWidth: true
            visible: root.act.active && root.act.requiredQsos > 0
            label: qsTr("Local QSO total; programme eligibility not verified")
            valueText: qsTr("%1 / %2").arg(root.act.qsoCount).arg(root.act.requiredQsos)
            fraction: root.act.requiredQsos > 0 ? Math.min(1, root.act.qsoCount / root.act.requiredQsos) : 0
            barColor: Theme.secondaryColor
        }
        Flow {
            Layout.fillWidth: true
            visible: root.act.active
            spacing: 6
            Repeater {
                model: root.act.perBand
                Pill {
                    required property var modelData
                    text: "%1 %2 · %3".arg(modelData.band).arg(modelData.mode).arg(modelData.count)
                    tone: modelData.mode === "FT2" ? Theme.accentColor : Theme.secondaryColor
                }
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.borderSoft; visible: root.act.active }

        // ── Come e' fatta la sessione ───────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            LabeledField {
                label: qsTr("Kind")
                StyledComboBox {
                    Layout.preferredWidth: 150
                    model: root.act.kinds.map(k => root.kindLabels[k])
                    currentIndex: Math.max(0, root.act.kinds.indexOf(root.draft.kind))
                    onActivated: root.set("kind", root.act.kinds[currentIndex])
                }
            }
            LabeledField {
                Layout.fillWidth: true
                visible: root.needsReference
                label: qsTr("Reference")
                StyledTextField {
                    Layout.fillWidth: true
                    uppercase: true
                    text: root.draft.reference || ""
                    placeholderText: root.draft.kind === "sota" ? "I/LM-001" : root.draft.kind === "wwff" ? "IFF-0123"
                                     : root.draft.kind === "iota" ? "EU-025" : "IT-1234"
                    onTextEdited: root.set("reference", text)
                }
            }
            LabeledField {
                Layout.fillWidth: true
                visible: root.draft.kind === "contest"
                label: qsTr("Contest")
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    StyledTextField {
                        id: contestIdField
                        Layout.preferredWidth: 150
                        uppercase: true
                        text: root.draft.contestId || ""
                        placeholderText: "CQ-WW-SSB"
                        onTextEdited: root.set("contestId", text)
                    }
                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        color: Theme.textSecondary
                        font.pixelSize: 12
                        // Il nome dice se l'id e' quello giusto: "CQ-WW-SSB" da
                        // solo non si controlla guardandolo.
                        text: !(root.draft.contestId || "").length ? qsTr("pick it from the list")
                              : decolog.activation.contestName(root.draft.contestId)
                                || qsTr("identifier not in the list")
                    }
                    GlassButton {
                        text: qsTr("Choose…")
                        onClicked: contestPicker.openPicker()
                    }
                }
            }
            LabeledField {
                Layout.fillWidth: true
                label: qsTr("Name")
                StyledTextField {
                    Layout.fillWidth: true
                    mono: false
                    text: root.draft.name || ""
                    onTextEdited: root.set("name", text)
                }
            }
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Additional operator references (format checked; catalogue and eligibility not verified). Changes apply to future QSO only.")
            color: Theme.textSecondary
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Repeater {
                model: [
                    {key: "pota", label: "POTA", hint: "IT-0001,IT-0002"},
                    {key: "sota", label: "SOTA", hint: "I/LM-001"},
                    {key: "wwff", label: "WWFF", hint: "IFF-0123"},
                    {key: "iota", label: "IOTA", hint: "EU-025"}
                ]
                LabeledField {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    label: modelData.label
                    StyledTextField {
                        Layout.fillWidth: true
                        uppercase: true
                        text: (root.draft.programs || {})[modelData.key] || ""
                        placeholderText: modelData.hint
                        onTextEdited: root.setProgram(modelData.key, text)
                    }
                }
            }
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("If an additional program is also the primary kind, its references must match the primary reference.")
            color: Theme.textSecondary
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            LabeledField {
                label: qsTr("Grid of the place")
                StyledTextField {
                    Layout.preferredWidth: 120
                    uppercase: true
                    text: root.draft.myGrid || ""
                    placeholderText: decolog.myGrid
                    onTextEdited: root.set("myGrid", text)
                }
            }
            LabeledField {
                label: qsTr("Tag on every QSO")
                StyledTextField {
                    Layout.preferredWidth: 140
                    mono: false
                    text: root.draft.tag || ""
                    placeholderText: root.draft.kind === "contest" ? (root.draft.contestId || "contest").toLowerCase() : (root.draft.kind || "")
                    onTextEdited: root.set("tag", text)
                }
            }
            LabeledField {
                label: qsTr("Station profile")
                StyledComboBox {
                    Layout.preferredWidth: 180
                    readonly property var ids: {
                        const list = [0]
                        for (let i = 0; i < decolog.stationProfiles.count; ++i) {
                            const p = decolog.stationProfiles.get(i)
                            if (!p.deleted) list.push(p.id)
                        }
                        return list
                    }
                    model: ids.map(id => id === 0 ? qsTr("The active one") : decolog.stationProfiles.byId(id).name)
                    currentIndex: Math.max(0, ids.indexOf(root.draft.stationProfileId || 0))
                    onActivated: root.set("stationProfileId", ids[currentIndex])
                }
            }
            ToggleSwitch {
                Layout.alignment: Qt.AlignBottom
                Layout.bottomMargin: 4
                text: qsTr("Serial number")
                checked: !!root.draft.serialEnabled
                onToggled: root.set("serialEnabled", checked)
            }
            LabeledField {
                visible: !!root.draft.serialEnabled
                label: qsTr("Next")
                StyledTextField {
                    Layout.preferredWidth: 70
                    text: root.draft.nextSerial || 1
                    validator: IntValidator { bottom: 1; top: 99999 }
                    onEditingFinished: root.set("nextSerial", parseInt(text) || 1)
                }
            }
            // Multi-operatore: una sequenza sola per tutti i PC della rete.
            ToggleSwitch {
                Layout.alignment: Qt.AlignBottom
                Layout.bottomMargin: 4
                visible: !!root.draft.serialEnabled
                text: qsTr("Shared on the network")
                checked: !!root.draft.sharedSerial
                onToggled: root.set("sharedSerial", checked)
            }
        }

        // ── Le regole di questa edizione e il call history (contest) ────────
        RowLayout {
            Layout.fillWidth: true
            visible: root.draft.kind === "contest" && root.act.active && root.edition.valid
            spacing: 10
            LabeledField {
                Layout.fillWidth: true
                label: qsTr("Bands of this edition")
                StyledTextField {
                    Layout.fillWidth: true
                    text: (root.draft.rules && root.draft.rules.bands ? root.draft.rules.bands : root.edition.bands || []).join(", ")
                    placeholderText: (root.edition.baseBands || []).join(", ")
                    onEditingFinished: root.setRule("bands", text.trim().length
                                                    ? text.split(/[ ,;]+/).filter(b => b.length) : undefined)
                }
            }
            LabeledField {
                label: qsTr("Operating hours")
                StyledTextField {
                    Layout.preferredWidth: 70
                    text: root.edition.maxOperatingHours || ""
                    placeholderText: qsTr("no limit")
                    validator: IntValidator { bottom: 0; top: 96 }
                    onEditingFinished: root.setRule("maxOperatingHours", text.length ? parseInt(text) : undefined)
                }
            }
            LabeledField {
                label: qsTr("Off-time from (min)")
                StyledTextField {
                    Layout.preferredWidth: 70
                    text: root.edition.minOffMinutes || ""
                    placeholderText: "30"
                    validator: IntValidator { bottom: 0; top: 600 }
                    onEditingFinished: root.setRule("minOffMinutes", text.length ? parseInt(text) : undefined)
                }
            }
            LabeledField {
                label: qsTr("Log within (h)")
                StyledTextField {
                    Layout.preferredWidth: 70
                    text: root.edition.submitHours || ""
                    validator: IntValidator { bottom: 0; top: 720 }
                    onEditingFinished: root.setRule("submitHours", text.length ? parseInt(text) : undefined)
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.draft.kind === "contest" && root.act.active
            spacing: 10
            Text {
                text: qsTr("Call history")
                color: Theme.textSecondary
                font.pixelSize: 12
            }
            Text {
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                text: root.state.callHistoryCount > 0
                      ? qsTr("%1 · %2 calls").arg(root.state.callHistoryName).arg(root.state.callHistoryCount)
                      : qsTr("none: the exchange is suggested from the log and the country")
                color: root.state.callHistoryCount > 0 ? Theme.textPrimary : Theme.textSecondary
                font.pixelSize: 12
            }
            GlassButton { text: qsTr("Load…"); onClicked: historyDialog.open() }
            GlassButton {
                visible: root.state.callHistoryCount > 0
                text: qsTr("Remove")
                onClicked: root.act.clearCallHistory()
            }
        }

        Note {
            text: root.draft.kind === "contest"
                  ? qsTr("The QSOs get CONTEST_ID and the serial number sent (STX). The number received goes in the New QSO panel.")
                  : qsTr("The QSOs get the activator fields (MY_SIG, MY_SIG_INFO, MY_SOTA_REF…), the grid of the place and the tag. "
                         + "A call already worked in this session on the same band and mode counts as a duplicate, whenever it was.")
        }
        Note {
            visible: root.act.active && (Object.keys(root.state.programs || {}).length > 0 || String(root.state.reference || "").indexOf(",") >= 0)
            text: qsTr("The existing ADIF export is not split by programme, reference or UTC day. Review and prepare separate submission files as required.")
            color: Theme.warningColor
        }
        GlassButton {
            id: previewButton
            visible: root.act.active
            text: root.act.previewBusy ? qsTr("Cancel programme preview") : qsTr("Preview reported programme groups")
            onClicked: {
                if (root.act.previewBusy) root.act.cancelPartitionPreview()
                else root.act.startPartitionPreview()
            }
        }
        GlassButton {
            visible: root.act.partitionExportBusy
            text: qsTr("Cancel partition export")
            onClicked: root.act.cancelPartitionExport()
        }
        Note {
            visible: root.partitionMessage.length > 0
            text: root.partitionMessage
        }
        Note {
            visible: root.previewShown
            color: Theme.warningColor
            text: root.partitionPreview.error || qsTr("%1 QSO report this session. Groups are not eligibility checks or submission files; imported or edited identifiers can be inaccurate. Network QSO may be missing. Missing operator or grid is shown as —. Recalculate after changes.").arg(root.partitionPreview.reportedCount || 0)
        }
        Note {
            visible: root.previewShown && !root.partitionPreview.error
            text: qsTr("Excluded from grouping: %1 missing/invalid UTC date; %2 without operator programme references. %3 groups.").arg(root.partitionPreview.missingDate || 0).arg(root.partitionPreview.missingReference || 0).arg((root.partitionPreview.groups || []).length)
        }
        Repeater {
            model: root.previewShown && !root.partitionPreview.error ? (root.partitionPreview.groups || []).slice(root.previewPage * 100, (root.previewPage + 1) * 100) : []
            RowLayout {
                required property var modelData
                Layout.fillWidth: true
                Note {
                    text: qsTr("%1 · %2 · %3 UTC · operator %4 · grid %5: %6 QSO")
                        .arg(modelData.program).arg(modelData.reference).arg(modelData.day)
                        .arg(modelData.operator || "—").arg(modelData.grid || "—").arg(modelData.count)
                }
                GlassButton {
                    visible: modelData.program === "POTA" || modelData.program === "WWFF"
                    enabled: !root.act.partitionExportBusy && !root.act.previewBusy
                    text: qsTr("Export…")
                    onClicked: { root.exportGroup = modelData; partitionExportDialog.open() }
                }
            }
        }
        RowLayout {
            visible: root.previewShown && (root.partitionPreview.groups || []).length > 100
            GlassButton { text: qsTr("Previous groups"); enabled: root.previewPage > 0; onClicked: root.previewPage-- }
            Text { color: Theme.textSecondary; text: qsTr("Page %1").arg(root.previewPage + 1) }
            GlassButton { text: qsTr("Next groups"); enabled: (root.previewPage + 1) * 100 < root.partitionPreview.groups.length; onClicked: root.previewPage++ }
        }
        Text {
            id: errorText
            Layout.fillWidth: true
            visible: text.length > 0
            wrapMode: Text.Wrap
            color: Theme.errorColor
            font.pixelSize: 12
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            GlassButton {
                visible: root.act.active
                text: qsTr("Export ADIF (%1)").arg(root.state.fileName || "")
                onClicked: exportDialog.open()
            }
            GlassButton {
                // Il banco: ogni cosa in una finestra sua, da mettere dove si
                // vuole. E' quello che serve in gara.
                text: qsTr("Contest Mode")
                tone: Theme.accentColor
                onClicked: { window.openContestDesk(); root.close() }
            }
            GlassButton {
                text: qsTr("Single window (Ctrl+Shift+T)")
                tone: Theme.primaryColor
                onClicked: { window.openContest(); root.close() }
            }
            Item { Layout.fillWidth: true }
            GlassButton { text: qsTr("Close"); onClicked: root.close() }
            GlassButton {
                id: applySessionButton
                visible: root.act.active
                text: qsTr("Apply changes")
                onClicked: errorText.text = root.act.update(root.draft)
            }
            GlassButton {
                visible: root.act.active
                text: qsTr("End session")
                tone: Theme.errorColor
                onClicked: { root.act.stop(); root.close() }
            }
            GlassButton {
                id: startSessionButton
                visible: !root.act.active
                text: qsTr("Start session")
                tone: Theme.accentColor
                filled: true
                onClicked: {
                    const wasContest = root.draft.kind === "contest"
                    errorText.text = root.act.start(root.draft)
                    if (errorText.text.length === 0) {
                        root.close()
                        // In un contest il banco si apre da solo: la finestra
                        // dell'inserimento, il cluster dei moltiplicatori e la
                        // CW se e' una gara in telegrafia.
                        if (wasContest)
                            Qt.callLater(window.openContestDesk)
                    }
                }
            }
        }
        }
    }

    // I contest che il programma conosce: l'elenco ADIF piu' quelli della ARI.
    Popup {
        id: contestPicker
        anchors.centerIn: Overlay.overlay
        modal: true
        padding: 14
        width: Math.min(620, root.width - 60)
        height: Math.min(520, root.height - 60)
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: Theme.panelColor; border.color: Theme.glassBorder; radius: 6 }

        property string search: ""
        readonly property var found: decolog.activation.contests(search)

        function openPicker() {
            search = ""
            searchField.text = ""
            open()
            searchField.forceActiveFocus()
        }

        contentItem: ColumnLayout {
            spacing: 10
            SectionTitle { text: qsTr("Choose the contest") }
            StyledTextField {
                id: searchField
                Layout.fillWidth: true
                mono: false
                placeholderText: qsTr("Search by name or identifier…")
                onTextChanged: contestPicker.search = text
            }
            Text {
                text: qsTr("%n contest(s)", "", contestPicker.found.length)
                color: Theme.textSecondary
                font.pixelSize: 11
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: contestPicker.found
                ScrollBar.vertical: PanelScrollBar {}
                delegate: ItemDelegate {
                    required property var modelData
                    width: ListView.view.width
                    height: 30
                    contentItem: RowLayout {
                        spacing: 8
                        Text {
                            Layout.preferredWidth: 170
                            text: modelData.id
                            color: Theme.primaryColor
                            font.family: Theme.monoFamily
                            font.pixelSize: 12
                        }
                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: modelData.name
                            color: Theme.textPrimary
                            font.pixelSize: 12
                        }
                    }
                    background: Rectangle {
                        color: parent.hovered ? Theme.bgLight : "transparent"
                        radius: 4
                    }
                    onClicked: {
                        root.set("contestId", modelData.id)
                        if (!(root.draft.name || "").length)
                            root.set("name", modelData.name)
                        contestPicker.close()
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                GlassButton { text: qsTr("Cancel"); onClicked: contestPicker.close() }
            }
        }
    }
}
