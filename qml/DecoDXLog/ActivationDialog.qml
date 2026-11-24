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
    function probeLocalFileUrl(path) { const p=String(path).replace(/\\/g,"/"); return "file://" + (p.startsWith("/") ? "" : "/") + encodeURI(p).replace(/#/g,"%23").replace(/\?/g,"%3F") }
    signal openQsoRequested(var id)
    property string previewLogPath: ""
    property bool probeOpenExcluded: false
    property bool probeStaleExcluded: false

    readonly property var act: decolog.activation
    readonly property var state: act.state
    property var draft: ({})
    property var partitionPreview: ({})
    property bool previewShown: false
    property bool probeAwaitPreview: false
    property bool probeAwaitExport: false
    property int probeExportStep: 0
    property bool probeClosedMode: false
    property bool probeExclusionMode: false
    property int previewPage: 0
    property int excludedPage: 0
    property int potaReviewPage: 0
    property var historyRows: []
    property var historyCursors: [0]
    property int historyPage: 0
    property var exportGroup: ({})
    property string partitionMessage: ""
    property string sotaMessage: ""
    property string potaMessage: ""
    property var potaLookup: ({})
    property bool probePotaMode: false
    property bool probePotaGroup: false
    property var sotaLookup: ({})
    property bool probeSotaMode: false
    property bool probeSotaGroup: false
    property bool probeSotaOnline: false

    // `kind` apre gia' sul tipo giusto: "contest" quando si arriva dal contest.
    function openDialog(kind) {
        draft = act.active ? Object.assign({}, root.state)
                           : { kind: kind || "pota", reference: "", name: "", contestId: "",
                               myGrid: decolog.myGrid, tag: "", band: "", mode: "",
                               serialEnabled: kind === "contest", nextSerial: 1 }
        errorText.text = ""
        partitionMessage = ""
        previewShown = false
        historyCursors = [0]
        historyPage = 0
        historyRows = act.closedRuns(0)
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
    function probeSiteProfiles() {
        root.openDialog("pota")
        let first = 0, second = 0
        for (let i = 0; i < decolog.stationProfiles.count; ++i) {
            const p = decolog.stationProfiles.get(i)
            if (p.name === "Fixture A") first = p.id
            if (p.name === "Fixture B") second = p.id
        }
        const startError = root.act.start({kind: "pota", reference: "IT-0001", myGrid: "JM75", stationProfileId: first})
        const now = decolog.utcNow()
        const a = decolog.logManualQso({call: "K1SAA", date: decolog.showDate(now.date), time: now.time, band: "20m", mode: "CW"})
        const b = decolog.logManualQso({call: "K1SBB", date: decolog.showDate(now.date), time: now.time, band: "20m", mode: "CW", my_gridsquare: "JN70"})
        const change = Object.assign({}, root.act.state, {myGrid: "JM76", stationProfileId: second})
        const updateError = root.act.update(change)
        const c = decolog.logManualQso({call: "K1SCC", date: decolog.showDate(now.date), time: now.time, band: "20m", mode: "CW"})
        console.warn("PROBE siteProfiles first=" + first + " second=" + second + " start=" + startError + " update=" + updateError + " errors=" + a + "," + b + "," + c)
        root.act.stop()
    }
    function probePotaCatalogue() {
        root.openDialog("pota")
        root.probePotaMode = true
        potaImportDialog.importPath(root.probeLocalFileUrl(decolog.currentLogInfo().path + ".pota.csv"))
    }
    function probeSotaUpdate() {
        root.openDialog("pota")
        root.probeSotaOnline = true
        sotaUpdateButton.clicked()
    }
    function probeSotaCatalogue() {
        root.openDialog("pota")
        root.probeSotaMode = true
        sotaImportDialog.importPath(root.probeLocalFileUrl(decolog.currentLogInfo().path + ".sota.csv"))
    }
    function probeExclusions() {
        root.openDialog("pota")
        root.probeExclusionMode = true
        Qt.callLater(() => { const row = historyRepeater.itemAt(0); if (row) row.probeClick() })
    }
    function probeMultiProgram() {
        root.openDialog("pota")
        root.set("reference", "IT-0001,IT-0002")
        root.setProgram("sota", "invalid")
        startSessionButton.clicked()
        console.warn("PROBE multiInvalid blocked=" + (!root.act.active && errorText.text.length > 0))
        root.setProgram("sota", "I/LM-001")
        root.setProgram("wwff", "IFF-0123")
        root.setProgram("iota", "EU-025")
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
    function probeUncertainCount() {
        root.openDialog("pota")
        const started = root.act.start({kind:"pota", reference:"IT-0001"})
        const now = decolog.utcNow()
        const logged = decolog.logManualQso({call:"K1TST", date:decolog.showDate(now.date), time:now.time, band:"20m", mode:"CW"})
        const ids = root.act.qsoIds()
        if (started || logged || ids.length !== 1) { console.warn("PROBE activation uncertainty setup failed " + started + " " + logged); return }
        const id=ids[0], key="APP_DECOLOG_ACTIVATION_REVIEW"
        let fields=decolog.qsoDetail(id).fields
        fields[key]="PERIOD_UNCERTAIN"
        const mark=decolog.saveQso(id,fields,-1,[key])
        const marked={count:root.act.uncertainCount,total:root.act.qsoCount,visible:uncertaintyWarning.visible,text:uncertaintyWarning.text}
        fields=decolog.qsoDetail(id).fields; fields[key]=""
        const clear=decolog.saveQso(id,fields,-1,[key])
        console.warn("PROBE activation uncertainty " + JSON.stringify({mark:mark,clear:clear,marked:marked,after:{count:root.act.uncertainCount,total:root.act.qsoCount,visible:uncertaintyWarning.visible}}))
    }
    function probeExcludedOpen() {
        root.openDialog("pota")
        const started = root.act.start({kind:"pota", reference:"IT-0001"})
        const now = decolog.utcNow()
        const logged = decolog.logManualQso({call:"K1TST", date:decolog.showDate(now.date), time:now.time, band:"20m", mode:"CW"})
        const ids = root.act.qsoIds()
        if (started || logged || ids.length !== 1) { console.warn("PROBE excluded open setup failed"); return }
        const fields=decolog.qsoDetail(ids[0]).fields
        fields.APP_DECOLOG_ACTIVATION_REVIEW="PERIOD_UNCERTAIN"
        const error=decolog.saveQso(ids[0],fields,-1,["APP_DECOLOG_ACTIVATION_REVIEW"])
        if (error) { console.warn("PROBE excluded open save failed " + error); return }
        root.probeOpenExcluded=true
        if (!root.act.startPartitionPreview()) console.warn("PROBE excluded open preview failed")
    }
    function openExcludedQso(row) {
        const current = decolog.qsoDetail(row.id)
        if (!root.previewLogPath || root.previewLogPath !== String(decolog.currentLogInfo().path || "")
                || !row.uuid || current.uuid !== row.uuid || current.revision !== row.revision) {
            root.partitionMessage = qsTr("The log or QSO changed; recalculate the preview before opening it.")
            return false
        }
        root.close()
        root.openQsoRequested(row.id)
        return true
    }
    Connections {
        target: root.act
        function onChanged() { root.previewShown = false; root.historyCursors = [0]; root.historyPage = 0; root.historyRows = root.act.closedRuns(0) }
        function onPreviewChanged() {
            if (!root.act.previewBusy) {
                root.partitionPreview = root.act.previewResult
                root.previewShown = Object.keys(root.partitionPreview).length > 0
                root.previewPage = 0
                root.excludedPage = 0
                root.potaReviewPage = 0
            }
        }
        function onPartitionExportFinished(success, message) {
            errorText.text = success ? "" : message
            root.partitionMessage = success ? qsTr("Partition file saved. Review it before submission.") : ""
            if (root.probeClosedMode) {
                console.warn("PROBE closedExport success=" + success + " error=" + message)
                root.probeClosedMode = false
                return
            }
            if (root.probeAwaitExport) {
                console.warn("PROBE multiExport step=" + root.probeExportStep + " success=" + success + " error=" + message)
                if (!success || root.probeExportStep >= 2) {
                    root.probeAwaitExport = false; root.act.stop()
                    root.probeClosedMode = true
                    Qt.callLater(() => { const row = historyRepeater.itemAt(0); console.warn("PROBE closedRow exists=" + !!row); if (row) row.probeClick() })
                    return
                }
                root.probeExportStep++
                const program = root.probeExportStep === 1 ? "SOTA" : "IOTA"
                const group = (root.partitionPreview.groups || []).find(g => g.program === program)
                const started = group && root.act.startPartitionExport(group, root.probeLocalFileUrl(decolog.currentLogInfo().path + "." + program + ".adi"))
                console.warn("PROBE multiExport " + program + " started=" + !!started)
                if (!started) { root.probeAwaitExport = false; root.act.stop() }
            }
        }
        function onPreviewFinished(success) {
            if (success) root.previewLogPath = String(decolog.currentLogInfo().path || "")
            if (root.probeOpenExcluded) {
                root.probeOpenExcluded=false
                Qt.callLater(() => {
                    const row=exclusionRepeater.itemAt(0)
                    console.warn("PROBE excluded row " + !!row)
                    if (!row) return
                    if (root.probeStaleExcluded) {
                        const saved=Object.assign({},row.modelData)
                        const error=decolog.editQsoField(saved.id,"comment","Changed after preview")
                        const opened=root.openExcludedQso(saved)
                        console.warn("PROBE excluded stale edit=" + error + " opened=" + opened + " warning=" + root.partitionMessage)
                    } else row.probeOpen()
                })
                return
            }
            if (root.probePotaGroup) {
                root.probePotaGroup = false
                Qt.callLater(() => {
                    const group = groupRepeater.itemAt(0)
                    if (group) console.warn("PROBE potaGroup status=" + group.modelData.potaSnapshotStatus + " location=" + group.modelData.potaLocationStatus + " text=" + group.probeText())
                    const row = potaReviewRepeater.itemAt(0)
                    if (row) {
                        console.warn("PROBE potaReview call=" + row.modelData.call + " total=" + root.partitionPreview.potaCorrespondentRows.length + " reason=" + row.modelData.reason + " text=" + row.probeText())
                        if (root.partitionPreview.potaCorrespondentRows.length > 200) {
                            potaReviewNext.clicked(); potaReviewNext.clicked()
                            Qt.callLater(() => {
                                const third = potaReviewRepeater.itemAt(0)
                                if (third) console.warn("PROBE potaReview third=" + third.modelData.call + " page=" + root.potaReviewPage)
                            })
                            return
                        }
                        const stale = Object.assign({}, row.modelData)
                        stale.revision = Number(stale.revision) + 1
                        console.warn("PROBE potaReview stale=" + root.openExcludedQso(stale))
                        row.probeOpen()
                    }
                })
                return
            }
            if (root.probeSotaGroup) {
                root.probeSotaGroup = false
                Qt.callLater(() => {
                    const group = groupRepeater.itemAt(0)
                    if (group) console.warn("PROBE sotaGroup status=" + group.modelData.catalogueStatus + " text=" + group.probeText() + " version=" + root.partitionPreview.sotaCatalogue.version)
                })
                return
            }
            if (root.probeExclusionMode) {
                root.probeExclusionMode = false
                Qt.callLater(() => {
                    const group = groupRepeater.itemAt(0)
                    if (group) console.warn("PROBE referenceWarning text=" + group.probeText())
                    const first = exclusionRepeater.itemAt(0)
                    if (!first) return
                    console.warn("PROBE exclusions total=" + root.partitionPreview.excluded.length + " first=" + first.modelData.call + " reason=" + first.modelData.reason + " text=" + first.text)
                    exclusionNext.clicked()
                    Qt.callLater(() => {
                        const next = exclusionRepeater.itemAt(0)
                        if (!next) return
                        console.warn("PROBE exclusions next=" + next.modelData.call + " reason=" + next.modelData.reason + " page=" + root.excludedPage + " text=" + next.text)
                        exclusionPrevious.clicked()
                        Qt.callLater(() => {
                            const back = exclusionRepeater.itemAt(0)
                            if (back) console.warn("PROBE exclusions back=" + back.modelData.call + " page=" + root.excludedPage)
                        })
                    })
                })
                return
            }
            if (root.probeClosedMode) {
                console.warn("PROBE closedPreview success=" + success + " reported=" + root.partitionPreview.reportedCount)
                const group = (root.partitionPreview.groups || []).find(g => g.program === "POTA" && g.reference === "IT-0001")
                const started = group && root.act.startPartitionExport(group, root.probeLocalFileUrl(decolog.currentLogInfo().path + ".closed.adi"))
                console.warn("PROBE closedExport started=" + !!started)
                if (!started) root.probeClosedMode = false
                return
            }
            if (root.probeAwaitPreview) {
                root.probeAwaitPreview = false
                console.warn("PROBE multiPreview reported=" + root.partitionPreview.reportedCount + " groups=" + (root.partitionPreview.groups || []).length + " error=" + (root.partitionPreview.error || ""))
                const group = (root.partitionPreview.groups || []).find(g => g.program === "POTA" && g.reference === "IT-0001")
                root.probeAwaitExport = true
                const started = group && root.act.startPartitionExport(group, root.probeLocalFileUrl(decolog.currentLogInfo().path + ".probe.adi"))
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

    FileDialog {
        id: sotaImportDialog
        title: qsTr("Import SOTA summit list CSV")
        nameFilters: [qsTr("CSV files (*.csv)"), qsTr("All files (*)")]
        function importPath(path) {
            root.sotaMessage = ""
            if (!root.act.sotaCatalogue.importFile(path)) root.sotaMessage = qsTr("Cannot start the catalogue import.")
        }
        onAccepted: importPath(selectedFile)
    }
    Connections {
        target: root.act.sotaCatalogue
        function onFinished(success, message) {
            const official = root.act.sotaCatalogue.info.source === "official-https"
            root.sotaMessage = success ? (official ? qsTr("SOTA catalogue downloaded over HTTPS and installed. The list is not signed.") : qsTr("SOTA catalogue imported. File origin was not independently verified.")) : message
            root.sotaLookup = ({})
            if (root.probeSotaOnline) {
                root.probeSotaOnline = false
                console.warn("PROBE sotaOnline success=" + success + " source=" + root.act.sotaCatalogue.info.source + " count=" + root.act.sotaCatalogue.info.count + " error=" + message)
            }
            if (root.probeSotaMode) {
                root.probeSotaMode = false
                sotaSearchRef.text = "I/LM-001"; sotaSearchDate.text = "2026-11-23"
                sotaCheckButton.clicked()
                console.warn("PROBE sotaImport success=" + success + " status=" + root.sotaLookup.status + " name=" + root.sotaLookup.name + " version=" + root.sotaLookup.version + " error=" + message)
                root.probeSotaGroup = true
                Qt.callLater(() => { const row = historyRepeater.itemAt(0); if (row) row.probeClick() })
            }
        }
    }

    FileDialog {
        id: potaImportDialog
        title: qsTr("Import POTA park list CSV")
        nameFilters: [qsTr("CSV files (*.csv)"), qsTr("All files (*)")]
        function importPath(path) {
            root.potaMessage = ""
            if (!root.act.potaCatalogue.importFile(path)) root.potaMessage = qsTr("Cannot start the catalogue import.")
        }
        onAccepted: importPath(selectedFile)
    }
    Connections {
        target: root.act.potaCatalogue
        function onFinished(success, message) {
            root.potaMessage = success ? qsTr("POTA snapshot imported. File origin was not independently verified.") : message
            root.potaLookup = ({})
            if (root.probePotaMode) {
                root.probePotaMode = false
                potaSearchRef.text = "US-0015@US-MD"; potaCheckButton.clicked()
                console.warn("PROBE potaImport success=" + success + " status=" + root.potaLookup.status + " location=" + root.potaLookup.locationStatus + " name=" + root.potaLookup.name + " error=" + message)
                root.probePotaGroup = true
                Qt.callLater(() => { const row = historyRepeater.itemAt(0); if (row) row.probeClick() })
            }
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
        Text {
            id: uncertaintyWarning
            Layout.fillWidth: true
            visible: root.act.active && root.act.uncertainCount !== 0
            color: Theme.warningColor
            wrapMode: Text.Wrap
            text: root.act.uncertainCount < 0
                  ? qsTr("Could not count QSO awaiting activation-period review.")
                  : qsTr("%1 QSO awaiting activation-period review are excluded from session totals and exports. Identify them in the programme preview, then open each QSO for review.").arg(root.act.uncertainCount)
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

        Text {
            Layout.fillWidth: true
            visible: !!root.draft.serialEnabled && !!root.draft.sharedSerial
            text: qsTr("Shared serials are assigned on save. A network split, restart or lost reply can duplicate numbers; uniqueness is not guaranteed.")
            color: Theme.warningColor
            wrapMode: Text.WordWrap
            font.pixelSize: 11
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
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.borderSoft }
        Note {
            text: root.act.sotaCatalogue.info.status === "available"
                ? qsTr("Offline SOTA catalogue · list date %1 · %2 summits · installed %3 · file %4 · SHA-256 %5. %6")
                    .arg(root.act.sotaCatalogue.info.version).arg(root.act.sotaCatalogue.info.count)
                    .arg(root.act.sotaCatalogue.info.importedAt).arg(root.act.sotaCatalogue.info.fileName).arg(root.act.sotaCatalogue.info.sha256)
                    .arg(root.act.sotaCatalogue.info.source === "official-https" ? qsTr("HTTPS source: %1; list is not signed.").arg(root.act.sotaCatalogue.info.sourceUrl) : qsTr("File origin is unverified."))
                : qsTr("No offline SOTA catalogue installed. Download the official list, then import the CSV.")
        }
        RowLayout {
            GlassButton { id: sotaUpdateButton; text: qsTr("Update SOTA list online"); enabled: !root.act.sotaCatalogue.busy;
                onClicked: { root.sotaMessage = ""; if (!root.act.sotaCatalogue.updateFromOfficial()) root.sotaMessage = qsTr("Cannot start SOTA update.") } }
            GlassButton { text: root.act.sotaCatalogue.busy ? qsTr("Cancel SOTA import") : qsTr("Import SOTA CSV…");
                onClicked: { if (root.act.sotaCatalogue.busy) root.act.sotaCatalogue.cancel(); else sotaImportDialog.open() } }
        }
        Note { visible: root.sotaMessage.length > 0; text: root.sotaMessage }
        RowLayout {
            Layout.fillWidth: true
            StyledTextField { id: sotaSearchRef; Layout.fillWidth: true; placeholderText: qsTr("Summit reference, e.g. I/LM-001"); uppercase: true }
            StyledTextField { id: sotaSearchDate; Layout.preferredWidth: 130; placeholderText: "YYYY-MM-DD"; text: new Date().toISOString().slice(0, 10) }
            GlassButton { id: sotaCheckButton; text: qsTr("Check offline"); onClicked: root.sotaLookup = root.act.sotaCatalogue.lookupIso(sotaSearchRef.text, sotaSearchDate.text) }
        }
        Note {
            visible: !!root.sotaLookup.status
            text: qsTr("SOTA lookup: %1 · %2 · %3–%4. Catalogue presence and dates do not prove activation eligibility.")
                .arg(({active: qsTr("in list for this date"), outsideValidity: qsTr("outside listed dates"), notFound: qsTr("not found in imported list"), unavailable: qsTr("catalogue unavailable"), invalidDate: qsTr("invalid date")})[root.sotaLookup.status] || root.sotaLookup.status || "").arg(root.sotaLookup.name || "—")
                .arg(root.sotaLookup.validFrom || "—").arg(root.sotaLookup.validTo || "—")
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.borderSoft }
        Note {
            text: root.act.potaCatalogue.info.status === "available"
                ? qsTr("Offline POTA snapshot · %1 parks · imported %2 · file %3 · SHA-256 %4. Local file origin is unverified.")
                    .arg(root.act.potaCatalogue.info.count).arg(root.act.potaCatalogue.info.importedAt)
                    .arg(root.act.potaCatalogue.info.fileName).arg(root.act.potaCatalogue.info.sha256)
                : qsTr("No offline POTA snapshot installed. Download the park list, then import the CSV.")
        }
        RowLayout {
            GlassButton { text: qsTr("Open POTA list source"); onClicked: Qt.openUrlExternally("https://pota.app/all_parks_ext.csv") }
            GlassButton { text: root.act.potaCatalogue.busy ? qsTr("Cancel POTA import") : qsTr("Import POTA CSV…");
                onClicked: { if (root.act.potaCatalogue.busy) root.act.potaCatalogue.cancel(); else potaImportDialog.open() } }
        }
        Note { visible: root.potaMessage.length > 0; text: root.potaMessage }
        RowLayout {
            Layout.fillWidth: true
            StyledTextField { id: potaSearchRef; Layout.fillWidth: true; placeholderText: qsTr("Park reference, e.g. US-0015@US-MD"); uppercase: true }
            GlassButton { id: potaCheckButton; text: qsTr("Check snapshot"); onClicked: root.potaLookup = root.act.potaCatalogue.lookup(potaSearchRef.text) }
        }
        Note {
            visible: !!root.potaLookup.status
            text: qsTr("POTA snapshot: %1 · %2 · listed locations %3 · requested location %4. This snapshot does not establish historical status or eligibility.")
                .arg(({listedActive: qsTr("active in snapshot"), listedInactive: qsTr("inactive in snapshot"), notFound: qsTr("not found in imported list"), unavailable: qsTr("catalogue unavailable"), invalidReference: qsTr("invalid reference")})[root.potaLookup.status] || root.potaLookup.status || "")
                .arg(root.potaLookup.name || "—").arg(root.potaLookup.locations || "—")
                .arg(({listed: qsTr("listed in snapshot"), notListed: qsTr("not listed in snapshot"), unknown: qsTr("unknown")})[root.potaLookup.locationStatus] || "—")
        }
        Note {
            text: qsTr("Closed tracked sessions: select one to review its reported QSO. Older untracked sessions are not inferred.")
        }
        Repeater {
            id: historyRepeater
            model: root.historyRows.slice(0, 100)
            RowLayout {
                required property var modelData
                function probeClick() { historyPreviewButton.clicked() }
                Layout.fillWidth: true
                Note { text: qsTr("%1 · %2 – %3 UTC").arg(modelData.title || qsTr("Session")).arg(modelData.startedAt).arg(modelData.endedAt) }
                GlassButton {
                    id: historyPreviewButton
                    text: qsTr("Preview")
                    enabled: !root.act.previewBusy && !root.act.partitionExportBusy
                    onClicked: root.act.startRunPartitionPreview(modelData.uuid)
                }
            }
        }
        RowLayout {
            visible: root.historyPage > 0 || root.historyRows.length > 100
            GlassButton {
                text: qsTr("Newer sessions"); enabled: root.historyPage > 0
                onClicked: { root.historyPage--; root.historyRows = root.act.closedRuns(root.historyCursors[root.historyPage]) }
            }
            GlassButton {
                text: qsTr("Older sessions"); enabled: root.historyRows.length > 100
                onClicked: {
                    const cursors = root.historyCursors.slice(0, root.historyPage + 1)
                    cursors.push(root.historyRows[99].rowid)
                    root.historyCursors = cursors; root.historyPage++
                    root.historyRows = root.act.closedRuns(cursors[root.historyPage])
                }
            }
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
            text: qsTr("Excluded from grouping: %1 missing/invalid UTC date; %2 without operator programme references; %3 awaiting activation-period review. %4 groups.").arg(root.partitionPreview.missingDate || 0).arg(root.partitionPreview.missingReference || 0).arg(root.partitionPreview.uncertainPeriod || 0).arg((root.partitionPreview.groups || []).length)
        }
        Repeater {
            id: exclusionRepeater
            model: root.previewShown && !root.partitionPreview.error ? (root.partitionPreview.excluded || []).slice(root.excludedPage * 100, (root.excludedPage + 1) * 100) : []
            RowLayout {
                required property var modelData
                readonly property string text: qsTr("Excluded QSO #%1 · %2 · %3: %4")
                    .arg(modelData.id).arg(modelData.call || "—").arg(modelData.at || "—")
                    .arg(modelData.reason === "missingDate" ? qsTr("missing or invalid UTC date") : modelData.reason === "uncertainPeriod" ? qsTr("activation period needs review") : qsTr("no operator programme reference"))
                Layout.fillWidth: true
                function probeOpen() { openExcluded.clicked() }
                Note { Layout.fillWidth: true; text: parent.text }
                GlassButton {
                    id: openExcluded
                    text: qsTr("Open QSO…")
                    onClicked: root.openExcludedQso(modelData)
                }
            }
        }
        RowLayout {
            visible: root.previewShown && (root.partitionPreview.excluded || []).length > 100
            GlassButton { id: exclusionPrevious; text: qsTr("Previous exclusions"); enabled: root.excludedPage > 0; onClicked: root.excludedPage-- }
            Text { color: Theme.textSecondary; text: qsTr("Page %1").arg(root.excludedPage + 1) }
            GlassButton { id: exclusionNext; text: qsTr("Next exclusions"); enabled: (root.excludedPage + 1) * 100 < (root.partitionPreview.excluded || []).length; onClicked: root.excludedPage++ }
        }
        Note {
            visible: root.previewShown && (root.partitionPreview.potaCorrespondentRows || []).length > 0
            color: Theme.warningColor
            text: qsTr("POTA correspondent references to review: %1 QSO. Open a QSO to inspect its fields.").arg(root.partitionPreview.potaCorrespondentRows.length)
        }
        Repeater {
            id: potaReviewRepeater
            model: root.previewShown && !root.partitionPreview.error ? (root.partitionPreview.potaCorrespondentRows || []).slice(root.potaReviewPage * 100, (root.potaReviewPage + 1) * 100) : []
            RowLayout {
                required property var modelData
                Layout.fillWidth: true
                function probeOpen() { openPotaReview.clicked() }
                function probeText() { return potaReviewNote.text }
                Note { id: potaReviewNote; Layout.fillWidth: true; text: qsTr("POTA review QSO #%1 · %2 · %3: %4").arg(modelData.id).arg(modelData.call || "—").arg(modelData.at || "—")
                        .arg(modelData.reason === "multipleReferences" ? qsTr("multiple correspondent parks") : modelData.reason === "invalidReference" ? qsTr("unrecognized correspondent reference") : qsTr("SIG fields differ or are incomplete")) }
                GlassButton { id: openPotaReview; text: qsTr("Open QSO…"); onClicked: root.openExcludedQso(modelData) }
            }
        }
        RowLayout {
            visible: root.previewShown && (root.partitionPreview.potaCorrespondentRows || []).length > 100
            GlassButton { text: qsTr("Previous review QSO"); enabled: root.potaReviewPage > 0; onClicked: root.potaReviewPage-- }
            Text { color: Theme.textSecondary; text: qsTr("Page %1").arg(root.potaReviewPage + 1) }
            GlassButton { id: potaReviewNext; text: qsTr("Next review QSO"); enabled: (root.potaReviewPage + 1) * 100 < (root.partitionPreview.potaCorrespondentRows || []).length; onClicked: root.potaReviewPage++ }
        }
        Note {
            visible: root.previewShown && !!root.partitionPreview.potaCatalogue
            color: Theme.warningColor
            text: root.partitionPreview.potaCatalogue && root.partitionPreview.potaCatalogue.status === "available"
                ? qsTr("POTA group checks use the imported snapshot from %1 (SHA-256 %2). File origin is unverified; status is not historical and does not establish eligibility.")
                    .arg(root.partitionPreview.potaCatalogue.importedAt).arg(root.partitionPreview.potaCatalogue.sha256)
                : qsTr("POTA snapshot unavailable; references could not be checked against a list.")
        }
        Note {
            visible: root.previewShown && !!root.partitionPreview.sotaCatalogue
            color: Theme.warningColor
            text: root.partitionPreview.sotaCatalogue && root.partitionPreview.sotaCatalogue.status === "available"
                ? qsTr("SOTA group checks use list %1 (SHA-256 %2). %3 A listed date is not activation eligibility.")
                    .arg(root.partitionPreview.sotaCatalogue.version).arg(root.partitionPreview.sotaCatalogue.sha256)
                    .arg(root.partitionPreview.sotaCatalogue.source === "official-https" ? qsTr("Downloaded over HTTPS; the list is not signed.") : qsTr("File origin is unverified."))
                : qsTr("SOTA catalogue unavailable; references could not be checked against a list.")
        }
        Note {
            visible: root.previewShown && !root.partitionPreview.error
            text: qsTr("Review missing station and operator calls. POTA requires at least one; WWFF identifies the station and the individual operator separately. Counts do not validate callsigns or eligibility.")
            color: Theme.warningColor
        }
        Repeater {
            id: groupRepeater
            model: root.previewShown && !root.partitionPreview.error ? (root.partitionPreview.groups || []).slice(root.previewPage * 100, (root.previewPage + 1) * 100) : []
            RowLayout {
                required property var modelData
                function probeText() { return groupNote.text }
                Layout.fillWidth: true
                Note {
                    id: groupNote
                    text: qsTr("%1 · %2 · %3 UTC · operator %4 · grid %5: %6 QSO")
                        .arg(modelData.program).arg(modelData.reference).arg(modelData.day)
                        .arg(modelData.operator || "—").arg(modelData.grid || "—").arg(modelData.count)
                        + qsTr(" · missing station call: %1; missing operator: %2; missing both: %3")
                            .arg(modelData.missingStationCall || 0).arg(modelData.missingOperator || 0).arg(modelData.missingCallIdentity || 0)
                        + (modelData.referenceSyntaxWarning ? qsTr(" · reference does not match local syntax; verify it") : "")
                        + (modelData.program === "POTA" && modelData.potaCorrespondentReview > 0
                            ? qsTr(" · correspondent park needs review: %1 QSO").arg(modelData.potaCorrespondentReview) : "")
                        + (modelData.program === "POTA" ? qsTr(" · POTA snapshot: %1%2%3")
                            .arg(({listedActive: qsTr("active in snapshot"), listedInactive: qsTr("inactive in snapshot"), notFound: qsTr("not in imported snapshot"), unavailable: qsTr("unavailable"), invalidReference: qsTr("invalid reference")})[modelData.potaSnapshotStatus] || qsTr("unavailable"))
                            .arg(modelData.potaName ? " · " + modelData.potaName : "")
                            .arg(modelData.potaLocationStatus ? " · " + (({listed: qsTr("location listed in snapshot"), notListed: qsTr("location not listed in snapshot"), unknown: qsTr("location unknown")})[modelData.potaLocationStatus] || "") : "") : "")
                        + (modelData.program === "SOTA" ? qsTr(" · SOTA list: %1%2")
                            .arg(({active: qsTr("listed for this day"), outsideValidity: qsTr("outside listed dates"), notFound: qsTr("not in imported list"), unavailable: qsTr("unavailable")})[modelData.catalogueStatus] || qsTr("unavailable"))
                            .arg(modelData.catalogueName ? " · " + modelData.catalogueName : "") : "")
                    color: modelData.referenceSyntaxWarning || modelData.potaCorrespondentReview > 0 ? Theme.warningColor : Theme.textSecondary
                }
                GlassButton {
                    visible: ["POTA", "WWFF", "SOTA", "IOTA"].indexOf(modelData.program) >= 0
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
            GlassButton { text: qsTr("Next groups"); enabled: (root.previewPage + 1) * 100 < (root.partitionPreview.groups || []).length; onClicked: root.previewPage++ }
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
