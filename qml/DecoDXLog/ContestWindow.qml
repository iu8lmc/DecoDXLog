// DecoDXLog — il contest, con le mani sulla tastiera.
//
// In un contest non si usa il mouse: si scrive il nominativo, si va avanti con
// Tab o con la barra, si registra con Invio, si pulisce con Esc. Tutto quello che
// serve sapere sta a schermo mentre si scrive: se e' un duplicato, che ritmo si
// tiene, quanti moltiplicatori sono entrati, e gli ultimi QSO fatti.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtCore
import Decodium.UI

RoundedWindow {
    id: root

    property int revision: 0
    property bool reportProbe: false
    property bool reportProbeSave: false
    property bool reportProbeFind: false
    property bool reportProbeOpen: false
    property bool reportProbeWindow: false
    property bool reportHistoryProbe: false
    property string reportExportProbePath: ""
    function probeReportHistory() { reportHistory.openHistory() }
    function probeContestAudio(folder) {
        contestAudioDialog.open()
        contestAudioDialog.folder=folder
        const index=decolog.contestAudio.inputs.indexOf("BlackHole 2ch")
        if (index < 0) { console.warn("PROBE contestAudio inputMissing"); return }
        decolog.contestAudio.chooseInput(index)
        contestAudioStart.clicked()
        console.warn("PROBE contestAudio started=" + decolog.contestAudio.recording + " dialog=" + contestAudioDialog.visible)
        if (decolog.contestAudio.recording) audioProbeStop.start()
    }
    function openAudioForQso(row) {
        contestAudioDialog.qsoUtc=row.utc || ""
        contestAudioDialog.qsoCall=row.call || ""
        contestAudioDialog.open()
    }
    function probeContestAudioRecovery(path) {
        contestAudioDialog.open()
        contestAudioDialog.recoverPartial(path)
        console.warn("PROBE contestRecover success=" + (decolog.contestAudio.lastPath.length > 0) + " path=" + decolog.contestAudio.lastPath)
    }
    function probeContestAudioFind(folder) {
        openAudioForQso({utc:"2026-11-23T12:00:01Z", call:"K1ABC"})
        contestAudioDialog.folder=folder
        contestAudioDialog.probingFind=true
        contestAudioFind.clicked()
    }
    function probeContestAudioPlayback(path) {
        openAudioForQso({utc:"2026-11-23T12:00:01Z", call:"K1ABC"})
        contestAudioDialog.playbackFile=path
        const index=decolog.contestAudio.outputs.indexOf("BlackHole 2ch")
        if (index < 0) { console.warn("PROBE contestPlayback outputMissing"); return }
        decolog.contestAudio.chooseOutput(index)
        contestAudioPlay.clicked()
        console.warn("PROBE contestPlayback playing=" + decolog.contestAudio.playing + " qso=" + contestAudioDialog.qsoCall)
        if (decolog.contestAudio.playing) {
            contestAudioForward.clicked()
            const advanced=decolog.contestAudio.playbackPositionMs>0
            contestAudioBack.clicked()
            console.warn("PROBE contestSeek advanced=" + advanced + " rewound=" + (decolog.contestAudio.playbackPositionMs<100))
            audioPlaybackProbeStop.start()
        }
    }
    Timer { id:audioPlaybackProbeStop; interval:700; onTriggered: {
        contestAudioStopPlayback.clicked()
        console.warn("PROBE contestPlayback stopped=" + !decolog.contestAudio.playing)
    } }
    Timer { id:audioProbeStop; interval:1100; onTriggered: {
        contestAudioStop.clicked()
        console.warn("PROBE contestAudio stopped=" + !decolog.contestAudio.recording + " path=" + decolog.contestAudio.lastPath)
    } }
    function inspectContestReport(url, probe) {
        reportProbe = !!probe
        reportReview.result = ({})
        reportReview.entries = []
        reportReview.errorText = ""
        reportReview.saveMessage = ""
        reportReview.open()
        if (!decolog.activation.startContestReportPreview(url)) reportReview.errorText = qsTr("Cannot start report review.")
    }
    Connections {
        target: decolog.activation
        function onContestReportFinished(result) {
            reportReview.result = result
            reportReview.errorText = result.error || ""
            const entries = (result.entries || []).map(e => ({line:e.line, text:e.original, kind:e.kind, row:e}))
            const unknown = (result.unknown || []).map(e => ({line:e.line, text:e.text, kind:"unrecognised"}))
            reportReview.entries = entries.concat(unknown)
            if (root.reportHistoryProbe) console.warn("PROBE lcr reopened " + result.total + " " + result.unknownTotal + " " + reportReview.errorText)
            if (root.reportProbe) console.warn("PROBE lcr ready " + result.total + " " + result.unknownTotal + " " + reportReview.entries.length + " " + reportReview.errorText)
            if (root.reportProbeSave && result.archiveReady) reportReview.saveSnapshot()
            if (root.reportProbeFind && (result.entries || []).length) reportReview.findCandidates(result.entries[0])
        }
        function onContestReportCandidatesFinished(result) {
            reportCandidates.result=result
            reportCandidates.errorText=result.error || ""
            if (root.reportProbeFind) console.warn("PROBE lcr candidates " + (result.candidates || []).length + " " + reportCandidates.errorText)
            if (root.reportProbeFind && (result.candidates || []).length) console.warn("PROBE lcr basis " + result.candidates[0].matchBasis + " call " + result.candidates[0].call)
            if (root.reportProbeWindow && result.toleranceMinutes === 0) {
                console.warn("PROBE lcr exact " + (result.candidates || []).length)
                candidateTolerance.value=1
                Qt.callLater(() => retryCandidateSearch.clicked())
                return
            }
            if (root.reportProbeWindow) {
                console.warn("PROBE lcr window " + result.toleranceMinutes + " count " + (result.candidates || []).length)
                if ((result.candidates || []).length) console.warn("PROBE lcr closest " + result.candidates[0].utc + " truncated " + result.truncated)
            }
            if ((root.reportProbeOpen || root.reportProbeWindow) && (result.candidates || []).length) {
                const id=result.candidates[0].id
                reportCandidates.openCandidate(result.candidates[0])
                Qt.callLater(() => console.warn("PROBE lcr opened " + window.probeQsoDialogOpen(id)))
            }
        }
        function onContestReportHistoryFinished(result) {
            reportHistory.result = result
            reportHistory.errorText = result.error || ""
            if (root.reportHistoryProbe) {
                console.warn("PROBE lcr history " + (result.reports || []).length + " " + reportHistory.errorText)
                if ((result.reports || []).length) {
                    if (root.reportExportProbePath) reportHistory.exportSelected(result.reports[0], "file://" + root.reportExportProbePath)
                    else reportHistory.openSelected(result.reports[0])
                }
            }
        }
        function onContestReportExportFinished(success, message) {
            reportHistory.statusText = message
            reportHistory.statusSuccess = success
            if (root.reportExportProbePath) console.warn("PROBE lcr exported " + success + " " + message)
        }
        function onContestReportArchiveFinished(success, message) {
            reportReview.saveMessage = message
            reportReview.saveSuccess = success
            if (root.reportProbeSave) console.warn("PROBE lcr archive " + success + " " + message)
        }
    }
    readonly property var session: { revision; return decolog.activation.state }
    readonly property var recent: { revision; return decolog.activation.recentQsos(14) }
    readonly property var rate: { revision; return decolog.activation.rate() }
    // Punti, moltiplicatori e punteggio secondo il regolamento del contest.
    readonly property var scoring: { revision; return decolog.activation.score() }
    readonly property bool running: decolog.activation.active

    property string band: ""
    property string mode: ""

    width: 1080
    height: 660
    minimumWidth: 820
    minimumHeight: 520
    visible: true
    title: qsTr("DecoDXLog — Contest")
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
        category: "contestWindow"
        property alias width: root.width
        property alias height: root.height
        // Anche la posizione: se la finestra sta sul secondo schermo, e' li'
        // che deve riaprirsi.
        property alias windowX: root.x
        property alias windowY: root.y
        property alias band: root.band
        property alias mode: root.mode
    }

    Connections {
        target: decolog
        function onLogChanged() { root.revision++ }
    }
    Connections {
        target: decolog.activation
        function onChanged() { root.revision++ }
    }
    Timer {
        running: root.running
        interval: 15000
        repeat: true
        onTriggered: root.revision++
    }

    // Il modo di partenza: quello della sessione, o quello che dice il nome
    // della gara (CQ-WW-SSB e' in fonia), o quello della radio. Prima partiva
    // sempre in CW, anche in una gara in SSB, con il 599 al posto del 59.
    function startingMode() {
        if (root.session.mode)
            return root.session.mode
        const id = String(root.session.contestId || "").toUpperCase()
        if (id.endsWith("-SSB") || id.endsWith("-PH") || id.endsWith("-PHONE"))
            return "SSB"
        if (id.endsWith("-RTTY"))
            return "RTTY"
        if (id.endsWith("-CW"))
            return "CW"
        const rig = String(decolog.shownMode || "").toUpperCase()
        if (rig === "LSB" || rig === "USB" || rig === "SSB" || rig === "AM" || rig === "FM")
            return "SSB"
        if (rig === "CW" || rig === "CW-R" || rig === "RTTY" || rig === "JTTY" || rig === "FT8" || rig === "FT4" || rig === "FT2")
            return rig === "CW-R" ? "CW" : rig
        return "CW"
    }

    Component.onCompleted: {
        if (root.band.length === 0)
            root.band = root.session.band || "20m"
        if (root.mode.length === 0)
            root.mode = root.startingMode()
        sentRst.text = root.mode === "SSB" ? "59" : "599"
        rcvdRst.text = sentRst.text
        callField.forceActiveFocus()
    }

    // Quello che non va nello scambio ricevuto, mentre lo si scrive: vuoto
    // finche' il campo e' vuoto, perche' avvisare prima che si scriva e' rumore.
    readonly property string exchangeProblem: {
        revision
        if (!running || rcvdNr.text.trim().length === 0)
            return ""
        return decolog.activation.checkExchange(rcvdNr.text)
    }
    readonly property bool duplicate: callField.text.trim().length > 0
                                      && decolog.activation.wouldDuplicate(callField.text, root.band, root.mode)

    function openCabrillo() { cabrilloDialog.openDialog() }

    // Un clic su uno spot del cluster riempie l'inserimento, come nel banco.
    Connections {
        target: decolog.cluster
        function onSpotPicked(call, band, mode, freqKhz) {
            if (!call || call.length === 0)
                return
            callField.text = call.toUpperCase()
            const b = bandBox.bands.indexOf(band)
            if (b >= 0) {
                root.band = band
                bandBox.currentIndex = b
            }
            const m = mode === "LSB" || mode === "USB" || mode === "AM" || mode === "FM" ? "SSB" : mode
            const mi = modeBox.modes.indexOf(m)
            if (mi >= 0 && m !== root.mode) {
                root.mode = m
                modeBox.currentIndex = mi
                sentRst.text = m === "SSB" ? "59" : "599"
                rcvdRst.text = sentRst.text
            }
            root.raise()
            root.requestActivate()
            root.goToExchange()
        }
    }

    // Lo scambio che la stazione mandera', scritto da solo mentre si batte il
    // nominativo: la zona dal paese, o quello che ha mandato l'ultima volta
    // (la provincia, la sezione, la zona di una HQ). Si sostituisce solo un
    // campo vuoto o gia' riempito da qui: quello scritto a mano non si tocca.
    // Il progressivo non si puo' sapere prima, e resta da scrivere.
    property string autoFrom: ""
    property bool settingAuto: false
    function suggestExchange() {
        if (!root.running)
            return
        if (rcvdNr.text.length > 0 && root.autoFrom === "")
            return
        const s = decolog.activation.suggestExchange(callField.text.trim())
        root.settingAuto = true
        rcvdNr.text = s.value || ""
        root.settingAuto = false
        root.autoFrom = rcvdNr.text.length > 0 ? s.from : ""
    }
    Timer { id: suggestTimer; interval: 200; onTriggered: root.suggestExchange() }
    // Il nome del campo dice da dove viene quello che c'e' dentro.
    function exchangeLabelText() {
        const base = root.scoring.exchangeLabel || qsTr("Nr r")
        return root.autoFrom === "log" ? qsTr("%1 · log").arg(base)
             : root.autoFrom === "history" ? qsTr("%1 · history").arg(base)
             : root.autoFrom === "cty" ? qsTr("%1 · country").arg(base)
             : base
    }
    // Si passa allo scambio con tutto selezionato: se il suggerimento va bene
    // si preme Invio, se no si scrive sopra.
    function goToExchange() {
        suggestTimer.stop()
        root.suggestExchange()
        rcvdNr.forceActiveFocus()
        rcvdNr.selectAll()
    }

    // La radio va dove si sceglie: cambiando banda, sulla frequenza di quella
    // banda (l'ultima usata li', o l'inizio del segmento del modo); cambiando
    // modo, il modo — e per i digitali anche la frequenza di chiamata. E al
    // contrario, se si gira la manopola della radio l'inserimento la segue.
    property var bandMemory: ({})
    // Subito dopo un cambio chiesto da qui la radio ci mette un attimo: nel
    // frattempo non si segue quello che dice, se no il menu tornerebbe indietro.
    property double lastQsy: 0
    // La memoria e' per banda e modo, come il tasto band stack delle radio:
    // tornando sui 20 metri in SSB si ritrova la frequenza lasciata li' in
    // SSB, non quella del CW.
    function memoryKey(band, mode) {
        const m = ["LSB", "USB", "AM", "FM"].indexOf(mode) >= 0 ? "SSB" : mode
        return band + "|" + m
    }
    function qsyTo(band, mode) {
        root.lastQsy = Date.now()
        const here = parseFloat(decolog.shownFrequency || "0")
        const hereBand = here > 0 ? decolog.bandForFrequency(String(here)) : ""
        const hereMode = String(decolog.shownMode || "").toUpperCase()
        if (hereBand.length > 0 && hereMode.length > 0)
            root.bandMemory[root.memoryKey(hereBand, hereMode)] = here
        let mhz = root.bandMemory[root.memoryKey(band, mode)] || decolog.bandFrequency(band, mode)
        if (mhz <= 0)
            mhz = here
        decolog.tuneTo(mhz, mode)
    }
    Connections {
        target: decolog
        function onTuningChanged() {
            if (Date.now() - root.lastQsy < 2500)
                return
            const f = parseFloat(decolog.shownFrequency || "0")
            const b = f > 0 ? decolog.bandForFrequency(String(f)) : ""
            if (b.length > 0 && b !== root.band && bandBox.bands.indexOf(b) >= 0) {
                root.band = b
                bandBox.currentIndex = bandBox.bands.indexOf(b)
            }
            const raw = String(decolog.shownMode || "").toUpperCase()
            const m = raw === "LSB" || raw === "USB" || raw === "AM" || raw === "FM" ? "SSB"
                    : raw === "CW-R" ? "CW" : raw
            const i = modeBox.modes.indexOf(m)
            if (i >= 0 && m !== root.mode) {
                root.mode = m
                modeBox.currentIndex = i
                sentRst.text = m === "SSB" ? "59" : "599"
                rcvdRst.text = sentRst.text
            }
        }
    }

    function clearEntry() {
        callField.text = ""
        rcvdRst.text = root.mode === "SSB" ? "59" : "599"
        rcvdNr.text = ""
        message.text = ""
        callField.forceActiveFocus()
    }

    function logQso() {
        const call = callField.text.trim()
        if (call.length < 3) {
            message.text = qsTr("The callsign is too short")
            return
        }
        const now = decolog.utcNow()
        const error = decolog.logManualQso({
            call: call, date: now.date, time: now.time,
            band: root.band, mode: root.mode,
            rst_sent: sentRst.text, rst_rcvd: rcvdRst.text,
            srx: rcvdNr.text
        })
        // Lo scambio storto non ferma il QSO — la stazione e' gia' passata — ma
        // si dice, cosi' si corregge adesso invece che a spoglio fatto.
        if (root.exchangeProblem.length > 0)
            message.text = root.exchangeProblem
        if (error.length > 0) {
            message.text = error
            return
        }
        root.clearEntry()
    }

    component Tile: Rectangle {
        property string label: ""
        property string value: ""
        property color tone: Theme.textPrimary
        implicitWidth: 120
        implicitHeight: 52
        radius: 5
        color: Theme.bgMedium
        border.width: 1
        border.color: Theme.borderSoft
        Column {
            anchors.centerIn: parent
            spacing: 2
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: parent.parent.label
                color: Theme.textSecondary
                font.family: Theme.monoFamily
                font.pixelSize: 10
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: parent.parent.value
                color: parent.parent.tone
                font.family: Theme.monoFamily
                font.pixelSize: 18
                font.bold: true
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        // ── Sessione ────────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Text {
                text: root.running ? root.session.title || qsTr("Session") : qsTr("No session open")
                color: root.running ? Theme.textPrimary : Theme.warningColor
                font.family: Theme.monoFamily
                font.pixelSize: 15
                font.bold: true
            }
            Text {
                visible: root.running
                text: "· " + (root.session.elapsed || "") + " · " + qsTr("next number %1").arg(root.session.nextSerial || 1)
                color: Theme.textSecondary
                font.pixelSize: 12
            }
            Item { Layout.fillWidth: true }
            GlassButton {
                visible: !root.running
                text: qsTr("Open a session…")
                tone: Theme.primaryColor
                filled: true
                buttonHeight: 26
                fontPixelSize: 12
                onClicked: window.openActivation()
            }
        }

        // ── Contatori ───────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Tile { label: qsTr("QSO"); value: String(root.session.qsoCount || 0); tone: Theme.primaryColor }
            Tile { label: qsTr("Calls"); value: String(root.session.uniqueCalls || 0) }
            Tile { label: qsTr("DXCC"); value: String(root.rate.dxcc || 0); tone: Theme.accentColor }
            Tile { label: qsTr("Grids"); value: String(root.rate.grids || 0) }
            Tile { label: qsTr("Last 10 min"); value: String(root.rate.last10 || 0) }
            Tile {
                label: qsTr("QSO/h")
                value: String(root.rate.perHour10 || 0)
                tone: (root.rate.perHour10 || 0) >= 60 ? Theme.accentColor : Theme.textPrimary
            }
            Tile { label: qsTr("Last hour"); value: String(root.rate.last60 || 0) }
            Tile {
                visible: root.scoring.valid
                label: qsTr("Points")
                value: String(root.scoring.points || 0)
            }
            Tile {
                visible: root.scoring.valid
                label: qsTr("Mult")
                value: String(root.scoring.multipliers || 0)
                tone: Theme.warningColor
            }
            Tile {
                visible: root.scoring.valid
                label: qsTr("Score")
                value: String(root.scoring.score || 0)
                tone: Theme.accentColor
            }
            Item { Layout.fillWidth: true }
        }

        // ── Riga di battitura ───────────────────────────────────────────────
        GlassPanel {
            Layout.fillWidth: true
            Layout.preferredHeight: 130
            title: qsTr("New QSO · Enter logs · Esc clears · Tab moves")
            dotColor: root.duplicate ? Theme.errorColor : Theme.accentColor

            RowLayout {
                anchors.fill: parent
                spacing: 12

                LabeledField {
                    label: qsTr("Band")
                    StyledComboBox {
                        id: bandBox
                        Layout.preferredWidth: 110
                        readonly property var bands: ["160m", "80m", "60m", "40m", "30m", "20m", "17m",
                                                      "15m", "12m", "10m", "6m", "2m", "70cm"]
                        model: bands
                        currentIndex: Math.max(0, bands.indexOf(root.band))
                        onActivated: {
                            root.band = bands[currentIndex]
                            root.qsyTo(root.band, root.mode)
                        }
                    }
                }
                LabeledField {
                    label: qsTr("Mode")
                    StyledComboBox {
                        id: modeBox
                        Layout.preferredWidth: 110
                        readonly property var modes: ["CW", "SSB", "RTTY", "JTTY", "FT2", "FT8", "FT4", "PSK31"]
                        model: modes
                        currentIndex: Math.max(0, modes.indexOf(root.mode))
                        onActivated: {
                            root.mode = modes[currentIndex]
                            sentRst.text = root.mode === "SSB" ? "59" : "599"
                            rcvdRst.text = sentRst.text
                            root.qsyTo(root.band, root.mode)
                        }
                    }
                }
                LabeledField {
                    label: qsTr("Callsign")
                    StyledTextField {
                        id: callField
                        Layout.preferredWidth: 190
                        uppercase: true
                        fieldHeight: 38
                        font.pixelSize: 20
                        accentBorder: root.duplicate ? Theme.errorColor : Theme.primaryColor
                        onTextChanged: {
                            decolog.lookupCall = text
                            suggestTimer.restart()
                        }
                        Keys.onSpacePressed: rcvdRst.forceActiveFocus()
                    }
                }
                LabeledField {
                    label: qsTr("RST s")
                    StyledTextField {
                        id: sentRst
                        Layout.preferredWidth: 80
                        fieldHeight: 38
                        text: "599"
                    }
                }
                LabeledField {
                    label: qsTr("Nr s")
                    StyledTextField {
                        Layout.preferredWidth: 90
                        fieldHeight: 38
                        readOnly: true
                        text: root.session.serialEnabled ? String(root.session.nextSerial || 1) : "—"
                    }
                }
                LabeledField {
                    label: qsTr("RST r")
                    StyledTextField {
                        id: rcvdRst
                        Layout.preferredWidth: 80
                        fieldHeight: 38
                        text: "599"
                    }
                }
                LabeledField {
                    // Il nome del campo lo decide il contest: zona, provincia,
                    // sezione o numero. Scrivere "59 14" dove ci vuole una
                    // provincia e' l'errore che si scopre a spoglio fatto.
                    label: root.exchangeLabelText()
                    StyledTextField {
                        id: rcvdNr
                        Layout.preferredWidth: 130
                        onTextChanged: if (!root.settingAuto) root.autoFrom = ""
                        color: root.autoFrom.length > 0 ? Theme.accentColor : Theme.textPrimary
                        fieldHeight: 38
                        uppercase: true
                        accentBorder: root.exchangeProblem.length > 0 ? Theme.warningColor
                                                                      : Theme.primaryColor
                    }
                }
                GlassButton {
                    Layout.alignment: Qt.AlignBottom
                    Layout.bottomMargin: 2
                    text: qsTr("Log")
                    tone: Theme.accentColor
                    filled: true
                    buttonHeight: 38
                    // Un nominativo scritto e' un nominativo buono: la lunghezza
                    // non la decide il log.
                    enabled: root.running && callField.text.trim().length > 0
                    onClicked: root.logQso()
                }
                Item { Layout.fillWidth: true }
            }
        }

        //  Macro CW: il manipolatore lo fa la radio, via Hamlib.
        GlassPanel {
            id: cwPanel
            Layout.fillWidth: true
            Layout.preferredHeight: 112
            visible: decolog.rig.enabled
            title: qsTr("CW macros")
            dotColor: decolog.rig.connected ? Theme.accentColor : Theme.textSecondary

            // Quello che serve alle macro per riempire i buchi.
            function cwContext() {
                return {
                    call: callField.text.trim(),
                    rst: sentRst.text.trim(),
                    nr: root.session.serialEnabled ? String(root.session.nextSerial || 1) : "",
                    exch: rcvdNr.text.trim()
                }
            }

            headerTools: [
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: decolog.rig.connected
                          ? "%1 %2".arg(decolog.rig.frequencyLabel).arg(decolog.rig.mode)
                          : decolog.rig.status
                    color: decolog.rig.connected ? Theme.textSecondary : Theme.warningColor
                    font.family: Theme.monoFamily
                    font.pixelSize: 11
                },
                GlassButton {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Stop")
                    tone: Theme.errorColor
                    buttonHeight: 22
                    fontPixelSize: 11
                    onClicked: decolog.rig.stop()
                },
                GlassButton {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Edit macros")
                    buttonHeight: 22
                    fontPixelSize: 11
                    onClicked: oneMacroEditor.openAll()
                }
            ]

            ColumnLayout {
                anchors.fill: parent
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: decolog.rig.macros
                        CwMacroKey {
                            required property var modelData
                            required property int index
                            macroIndex: index
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            Layout.minimumWidth: 0
                            macro: modelData
                            keyHeight: 30
                            fontPixelSize: 12
                            sendEnabled: (decolog.rig.connected || decolog.rig.keyerOn) && decolog.rig.canKeyCw
                            tone: decolog.rig.activeMacroIndex === index ? Theme.accentColor : "transparent"
                            onSendRequested: (i) => decolog.rig.sendMacro(i, cwPanel.cwContext())
                            onEditRequested: (i) => oneMacroEditor.openFor(i)
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        text: qsTr("Speed")
                        color: Theme.textSecondary
                        font.pixelSize: 11
                    }
                    // La manopola della velocita': il manipolatore e' quello della
                    // radio, qui si dice solo quanto deve andare svelto.
                    Slider {
                        Layout.fillWidth: true
                        from: 10
                        to: 45
                        stepSize: 1
                        value: decolog.rig.wpm
                        enabled: decolog.rig.connected
                        onMoved: decolog.rig.wpm = Math.round(value)
                    }
                    Text {
                        text: qsTr("%1 wpm").arg(decolog.rig.wpm)
                        color: Theme.textPrimary
                        font.family: Theme.monoFamily
                        font.pixelSize: 12
                        font.bold: true
                    }
                }
            }
        }

        // I tasti funzione mandano le macro, come in ogni log da contest.
        Repeater {
            model: 8
            Item {
                required property int index
                Shortcut {
                    sequence: "F" + (index + 1)
                    enabled: decolog.rig.enabled && decolog.rig.connected && decolog.rig.canKeyCw
                    onActivated: decolog.functionKey(index, cwPanel.cwContext())
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Rectangle {
                visible: root.duplicate
                implicitWidth: 90
                implicitHeight: 24
                radius: 4
                color: Qt.alpha(Theme.errorColor, 0.2)
                border.width: 1
                border.color: Theme.errorColor
                Text {
                    anchors.centerIn: parent
                    text: qsTr("DUPE")
                    color: Theme.errorColor
                    font.family: Theme.monoFamily
                    font.pixelSize: 13
                    font.bold: true
                }
            }
            Text {
                readonly property var info: decolog.callInfo
                text: info.call === callField.text.trim().toUpperCase()
                      ? [info.country,
                         (info.count || 0) > 0 ? qsTr("worked %1×").arg(info.count) : qsTr("new station"),
                         info.distanceKm ? qsTr("%1 km · %2°").arg(Math.round(info.distanceKm)).arg(Math.round(info.bearing || 0)) : ""
                        ].filter(s => s).join(" · ")
                      : ""
                color: (decolog.callInfo.count || 0) > 0 ? Theme.textSecondary : Theme.accentColor
                font.pixelSize: 12
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Text {
                id: message
                color: Theme.errorColor
                font.pixelSize: 12
                elide: Text.ElideRight
                Layout.maximumWidth: 380
            }
        }

        // ── Come va, banda per banda ────────────────────────────────────────
        //
        // Nei contest i moltiplicatori si contano per banda: sapere che sui 20
        // ce ne sono trenta e sugli 80 due e' quello che dice dove andare.
        GlassPanel {
            Layout.fillWidth: true
            Layout.preferredHeight: 92
            visible: root.scoring.valid && (root.scoring.bands || []).length > 0
            title: qsTr("Band by band · QSO, points, multipliers")

            RowLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8
                Repeater {
                    model: root.scoring.bands || []
                    Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 4
                        color: modelData.band === root.band ? Theme.rowMatchBg : Theme.bgDeep
                        border.color: modelData.band === root.band ? Theme.accentColor : Theme.borderSoft

                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: 1
                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                text: modelData.band
                                color: Theme.textPrimary
                                font.family: Theme.monoFamily
                                font.pixelSize: 13
                                font.bold: true
                            }
                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                text: qsTr("%1 QSO").arg(modelData.qsos || 0)
                                color: Theme.textSecondary
                                font.pixelSize: 11
                            }
                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                text: qsTr("%1 pt · %2 mult").arg(modelData.points || 0)
                                                             .arg(modelData.multipliers || 0)
                                color: Theme.primaryColor
                                font.family: Theme.monoFamily
                                font.pixelSize: 11
                            }
                        }
                    }
                }
                Item { Layout.fillWidth: true; visible: (root.scoring.bands || []).length < 3 }
            }
        }

        // ── Ultimi QSO ──────────────────────────────────────────────────────
        GlassPanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: qsTr("Last QSOs of the session")

            ListView {
                anchors.fill: parent
                clip: true
                model: root.recent
                ScrollBar.vertical: ScrollBar {}
                delegate: RowLayout {
                    required property var modelData
                    width: ListView.view.width
                    spacing: 0
                    Repeater {
                        model: [{ v: modelData.time, w: 70 }, { v: modelData.call, w: 130, bold: true },
                                { v: modelData.band, w: 70 }, { v: modelData.mode, w: 80 },
                                { v: modelData.rstSent, w: 60 }, { v: modelData.rstRcvd, w: 60 }]
                        Text {
                            required property var modelData
                            Layout.preferredWidth: modelData.w
                            text: modelData.v || "—"
                            color: modelData.bold ? Theme.textPrimary : Theme.textSecondary
                            font.family: Theme.monoFamily
                            font.pixelSize: 13
                            font.bold: modelData.bold === true
                        }
                    }
                    GlassButton { text:qsTr("Audio…"); buttonHeight:24; fontPixelSize:11
                        onClicked:root.openAudioForQso(modelData) }
                    Text {
                        Layout.fillWidth: true
                        text: modelData.country || ""
                        color: Theme.textSecondary
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: root.recent.length === 0
                    text: qsTr("No QSO in this session yet.")
                    color: Theme.textSecondary
                    font.pixelSize: 12
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            GlassButton {
                text: qsTr("Cabrillo…")
                tone: Theme.primaryColor
                filled: true
                buttonHeight: 26
                fontPixelSize: 12
                enabled: root.running && (root.session.qsoCount || 0) > 0
                onClicked: cabrilloDialog.openDialog()
            }
            GlassButton {
                text: qsTr("ADIF of the session…")
                buttonHeight: 26
                fontPixelSize: 12
                enabled: root.running && (root.session.qsoCount || 0) > 0
                onClicked: adifDialog.open()
            }
            GlassButton {
                text: qsTr("Review CQ WW report…"); buttonHeight: 26; fontPixelSize: 12
                enabled: !decolog.activation.contestReportBusy
                onClicked: contestReportFile.open()
            }
            GlassButton { text:qsTr("Report history…"); buttonHeight:26; fontPixelSize:12
                enabled:!decolog.activation.contestReportBusy
                onClicked:reportHistory.openHistory() }
            GlassButton { text: decolog.contestAudio.recording ? qsTr("Audio recording…") : qsTr("Session audio…"); buttonHeight:26; fontPixelSize:12
                onClicked: contestAudioDialog.open() }
            Item { Layout.fillWidth: true }
            Text {
                text: qsTr("Enter logs · Esc clears · space moves to the report")
                color: Theme.textSecondary
                font.pixelSize: 11
            }
        }
    }

    // Invio e Esc valgono ovunque nella finestra.
    Shortcut { sequence: "Return"; enabled: !contestAudioDialog.visible && !contestAudioFolder.visible && !contestAudioPlaybackFile.visible && !contestAudioRecoveryFile.visible && !reportReview.visible && !reportHistory.visible && !contestReportFile.visible && !contestReportExportFile.visible; onActivated: root.logQso() }
    Shortcut { sequence: "Enter"; enabled: !contestAudioDialog.visible && !contestAudioFolder.visible && !contestAudioPlaybackFile.visible && !contestAudioRecoveryFile.visible && !reportReview.visible && !reportHistory.visible && !contestReportFile.visible && !contestReportExportFile.visible; onActivated: root.logQso() }
    Shortcut { sequence: "Esc"; enabled: !contestAudioDialog.visible && !contestAudioFolder.visible && !contestAudioPlaybackFile.visible && !contestAudioRecoveryFile.visible && !reportReview.visible && !reportHistory.visible && !contestReportFile.visible && !contestReportExportFile.visible; onActivated: root.clearEntry() }

    FileDialog {
        id: contestReportExportFile
        property var selectedReport: ({})
        title:qsTr("Export original report")
        fileMode:FileDialog.SaveFile
        defaultSuffix:"rpt"
        nameFilters:[qsTr("Text reports (*.rpt *.txt)"),qsTr("All files (*)")]
        onAccepted:reportHistory.exportSelected(selectedReport, selectedFile)
    }
    FileDialog {
        id: contestReportFile
        title: qsTr("Choose a CQ WW log checking report")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Text reports (*.rpt *.txt)"), qsTr("All files (*)")]
        onAccepted: root.inspectContestReport(selectedFile, false)
    }
    DialogFrame {
        id: reportHistory
        property var result: ({})
        property var previousPages: []
        property real beforeId: 0
        property string errorText: ""
        property string statusText: ""
        property bool statusSuccess: false
        function exportSelected(row, destination) {
            statusText=""
            if (!decolog.activation.startContestReportExport(row.id,row.sha256,destination)) {
                statusSuccess=false; statusText=qsTr("Cannot export: reload history and choose a destination other than the log database.")
            }
        }
        dialogKey:"contestReportHistory"; width:760; height:520
        title:qsTr("Archived CQ WW reports"); dotColor:Theme.primaryColor
        function loadPage(before) {
            result=({}); errorText=""; beforeId=before
            if (!decolog.activation.startContestReportHistory(before)) errorText=qsTr("Cannot load report history.")
        }
        function openHistory() { previousPages=[]; statusText=""; open(); loadPage(0) }
        function openSelected(row) {
            reportReview.result=({}); reportReview.entries=[]; reportReview.errorText=""; reportReview.saveMessage=""
            if (!decolog.activation.startArchivedContestReportPreview(row.id,row.sha256)) {
                errorText=qsTr("The log or report changed; reload the history."); return
            }
            close(); reportReview.open()
        }
        ColumnLayout {
            anchors.fill:parent; spacing:8
            Text { Layout.fillWidth:true; wrapMode:Text.WordWrap; color:Theme.textSecondary
                text:qsTr("Original report copies in the current log. Opening a copy does not change QSO.") }
            Text { Layout.fillWidth:true; wrapMode:Text.WordWrap; color:Theme.errorColor; visible:reportHistory.errorText.length>0; text:reportHistory.errorText }
            Text { visible:decolog.activation.contestReportBusy; color:Theme.textSecondary; text:qsTr("Loading…") }
            ListView {
                id:historyRows; Layout.fillWidth:true; Layout.fillHeight:true; clip:true; model:reportHistory.result.reports || []
                ScrollBar.vertical:PanelScrollBar {}
                delegate:RowLayout {
                    required property var modelData; width:historyRows.width; spacing:8
                    Text { Layout.fillWidth:true; color:Theme.textPrimary; elide:Text.ElideRight
                        text:modelData.importedAt + " · " + modelData.ownCall + " · " + modelData.contest + " · " + modelData.filename }
                    GlassButton { text:qsTr("Export"); enabled:!decolog.activation.contestReportBusy
                        onClicked:{ contestReportExportFile.selectedReport=modelData; contestReportExportFile.open() } }
                    GlassButton { text:qsTr("Review"); enabled:!decolog.activation.contestReportBusy; onClicked:reportHistory.openSelected(modelData) }
                }
            }
            Text { Layout.fillWidth:true; wrapMode:Text.WordWrap; visible:reportHistory.statusText.length>0
                color:reportHistory.statusSuccess ? Theme.accentColor : Theme.errorColor; text:reportHistory.statusText }
            RowLayout {
                Layout.fillWidth:true
                GlassButton { text:qsTr("Previous"); enabled:reportHistory.previousPages.length>0 && !decolog.activation.contestReportBusy
                    onClicked:{ const pages=reportHistory.previousPages.slice(); const before=pages.pop(); reportHistory.previousPages=pages; reportHistory.loadPage(before) } }
                GlassButton { text:qsTr("Next"); enabled:!!reportHistory.result.hasMore && !decolog.activation.contestReportBusy && (reportHistory.result.reports || []).length>0
                    onClicked:{ const rows=reportHistory.result.reports; const pages=reportHistory.previousPages.slice(); pages.push(reportHistory.beforeId); reportHistory.previousPages=pages; reportHistory.loadPage(rows[rows.length-1].id) } }
                Item { Layout.fillWidth:true }
                GlassButton { text:qsTr("Close"); onClicked:reportHistory.close() }
            }
        }
    }
    DialogFrame {
        id: reportReview
        property var result: ({})
        property var entries: []
        property string errorText: ""
        property string saveMessage: ""
        property bool saveSuccess: false
        function findCandidates(row) {
            reportCandidates.row=row
            candidateTolerance.value=0
            reportCandidates.open()
            reportCandidates.search()
        }
        function saveSnapshot() {
            saveMessage = ""
            if (!decolog.activation.archiveContestReportPreview()) {
                saveSuccess = false
                saveMessage = qsTr("The log changed or the reviewed copy is unavailable; review the report again.")
            }
        }
        dialogKey: "contestReportReview"; width: 800; height: 600
        title: qsTr("CQ WW report review"); dotColor: Theme.primaryColor
        ColumnLayout {
            anchors.fill: parent; spacing: 8
            Text { Layout.fillWidth:true; wrapMode:Text.WordWrap; color:Theme.textSecondary
                text:qsTr("Read-only review. Only three own-log error sections are interpreted; line previews are limited to 1024 characters. The original file and QSO are unchanged.") }
            Text { Layout.fillWidth:true; wrapMode:Text.WordWrap; color:Theme.errorColor; visible:reportReview.errorText.length>0; text:reportReview.errorText }
            Text { Layout.fillWidth:true; color:Theme.textPrimary; wrapMode:Text.WordWrap
                text:decolog.activation.contestReportBusy ? qsTr("Reading report…") : reportReview.result.contest ?
                    qsTr("%1 · %2 · NIL %3 · calls %4 · exchanges %5 · unrecognised %6").arg(reportReview.result.contest).arg(reportReview.result.ownCall).arg(reportReview.result.nil).arg(reportReview.result.incorrectCall).arg(reportReview.result.incorrectExchange).arg(reportReview.result.unknownTotal) : "" }
            Text { Layout.fillWidth:true; color:Theme.warningColor; wrapMode:Text.WordWrap
                visible:(reportReview.result.total || 0)>500 || (reportReview.result.unknownTotal || 0)>200
                text:qsTr("Showing up to 500 findings and 200 unrecognised lines; review the original file for the complete text.") }
            ListView {
                id: reportRows; Layout.fillWidth:true; Layout.fillHeight:true; clip:true; model:reportReview.entries
                ScrollBar.vertical: PanelScrollBar {}
                delegate: RowLayout { required property var modelData; width:reportRows.width; spacing:6
                    Text { Layout.fillWidth:true; wrapMode:Text.WrapAnywhere
                        color:modelData.kind === "unrecognised" ? Theme.warningColor : Theme.textPrimary
                        font.family:Theme.monoFamily; font.pixelSize:11
                        text:qsTr("Line %1 · %2 · %3").arg(modelData.line).arg(modelData.kind).arg(modelData.text) }
                    GlassButton { text:qsTr("Find QSO"); visible:!!modelData.row; enabled:!decolog.activation.contestReportBusy
                        onClicked:reportReview.findCandidates(modelData.row) }
                }
            }
            Text { Layout.fillWidth:true; wrapMode:Text.WordWrap; visible:reportReview.saveMessage.length>0
                color:reportReview.saveSuccess ? Theme.accentColor : Theme.errorColor; text:reportReview.saveMessage }
            RowLayout { Layout.alignment:Qt.AlignRight
                GlassButton { text:qsTr("Save reviewed copy to log"); enabled:!!reportReview.result.archiveReady && !decolog.activation.contestReportBusy
                    onClicked:reportReview.saveSnapshot() }
                GlassButton { text:qsTr("Close"); onClicked:reportReview.close() }
            }
        }
    }

    DialogFrame {
        id:contestAudioDialog
        dialogKey:"contestAudio"; width:700; height:620; title:qsTr("Contest session audio"); dotColor:Theme.primaryColor
        property url folder: ""
        property url playbackFile: ""
        property string qsoUtc: ""
        property string qsoCall: ""
        property var foundRecordings: []
        property string findMessage: ""
        property bool probingFind: false
        onFolderChanged: { foundRecordings=[]; findMessage="" }
        onQsoUtcChanged: { foundRecordings=[]; findMessage="" }
        Connections {
            target:decolog.contestAudio
            function onRecordingsFound(result) {
                if (String(result.folder)!==String(contestAudioDialog.folder) || result.qsoUtc!==contestAudioDialog.qsoUtc) return
                contestAudioDialog.foundRecordings=result.matches || []
                contestAudioDialog.findMessage=result.error ? result.error : qsTr("%1 possible recordings · %2 files checked%3").arg(contestAudioDialog.foundRecordings.length).arg(result.inspected).arg(result.truncated ? qsTr(" · search limited") : "")
                if (contestAudioDialog.probingFind) {
                    if (contestAudioDialog.foundRecordings.length && audioMatches.itemAt(0)) audioMatches.itemAt(0).selectRecording()
                    console.warn("PROBE contestFind count=" + contestAudioDialog.foundRecordings.length + " selected=" + (String(contestAudioDialog.playbackFile).length>0))
                    contestAudioDialog.probingFind=false
                }
            }
        }
        function recoverPartial(path) { decolog.contestAudio.recoverFile(path) }
        ColumnLayout {
            anchors.fill:parent; spacing:10
            Text { Layout.fillWidth:true; wrapMode:Text.WordWrap; color:Theme.warningColor
                text:qsTr("Optional local audio recording. Select the receiving audio input and a folder; this does not control the radio or PTT. Recording continues if this window is closed.") }
            RowLayout {
                Layout.fillWidth:true
                Text { text:qsTr("Input"); color:Theme.textSecondary }
                StyledComboBox { Layout.fillWidth:true; model:decolog.contestAudio.inputs; currentIndex:decolog.contestAudio.inputIndex
                    enabled:!decolog.contestAudio.recording; onActivated:decolog.contestAudio.chooseInput(currentIndex) }
            }
            RowLayout {
                Layout.fillWidth:true
                GlassButton { text:qsTr("Choose folder…"); enabled:!decolog.contestAudio.recording; onClicked:contestAudioFolder.open() }
                Text { Layout.fillWidth:true; text:contestAudioDialog.folder ? String(contestAudioDialog.folder) : qsTr("No folder selected"); color:Theme.textSecondary; elide:Text.ElideMiddle }
            }
            RowLayout {
                Text { text:qsTr("Maximum recording size (MiB)"); color:Theme.textSecondary }
                SpinBox { id:contestAudioLimit; from:1; to:1024; value:128; editable:true; enabled:!decolog.contestAudio.recording }
                GlassButton { id:contestAudioStart; text:qsTr("Start recording"); enabled:!decolog.contestAudio.recording && String(contestAudioDialog.folder).length>0
                    onClicked:decolog.contestAudio.start(contestAudioDialog.folder,contestAudioLimit.value) }
                GlassButton { id:contestAudioStop; text:qsTr("Stop"); enabled:decolog.contestAudio.recording; onClicked:decolog.contestAudio.stop() }
            }
            Text { Layout.fillWidth:true; wrapMode:Text.Wrap; color:Theme.textPrimary
                text:decolog.contestAudio.message + (decolog.contestAudio.recording ? qsTr(" · %1 bytes").arg(decolog.contestAudio.bytesWritten) : "") }
            RowLayout {
                Layout.fillWidth:true
                GlassButton { text:qsTr("Recover partial recording…"); enabled:!decolog.contestAudio.recording && !decolog.contestAudio.playing; onClicked:contestAudioRecoveryFile.open() }
                Text { Layout.fillWidth:true; wrapMode:Text.Wrap; color:Theme.textSecondary; text:decolog.contestAudio.lastPath }
            }
            Text { Layout.fillWidth:true; wrapMode:Text.Wrap; color:Theme.warningColor
                text:qsTr("Review QSO %1 at %2. Choose the recording covering its UTC time; playback starts up to 10 seconds earlier. Timing is approximate and does not prove an association.").arg(contestAudioDialog.qsoCall || "—").arg(contestAudioDialog.qsoUtc || "—") }
            RowLayout {
                Layout.fillWidth:true
                GlassButton { id:contestAudioFind; text:qsTr("Find recordings in folder"); enabled:!decolog.contestAudio.searchBusy && contestAudioDialog.qsoUtc.length>0 && String(contestAudioDialog.folder).length>0
                    onClicked: { contestAudioDialog.foundRecordings=[]; contestAudioDialog.findMessage=qsTr("Searching…"); if (!decolog.contestAudio.findRecordings(contestAudioDialog.folder,contestAudioDialog.qsoUtc)) contestAudioDialog.findMessage=qsTr("Choose a folder and a valid UTC QSO time.") } }
                Text { Layout.fillWidth:true; color:Theme.textSecondary; text:contestAudioDialog.findMessage; wrapMode:Text.Wrap }
            }
            ScrollView {
                Layout.fillWidth:true; Layout.preferredHeight:90; visible:contestAudioDialog.foundRecordings.length>0; clip:true
                ColumnLayout {
                    width:parent.availableWidth
                    Repeater {
                        id:audioMatches
                        model:contestAudioDialog.foundRecordings
                        RowLayout { required property var modelData; Layout.fillWidth:true
                            function selectRecording() { audioSelect.clicked() }
                            Text { Layout.fillWidth:true; wrapMode:Text.Wrap; color:Theme.textSecondary
                                text:qsTr("Start %1 · duration %2 s · QSO offset %3 s").arg(modelData.startUtc).arg((modelData.durationMs/1000).toFixed(1)).arg((modelData.offsetMs/1000).toFixed(1)) }
                            GlassButton { id:audioSelect; text:qsTr("Select"); enabled:!decolog.contestAudio.recording && !decolog.contestAudio.playing; onClicked:contestAudioDialog.playbackFile=modelData.url }
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth:true
                GlassButton { text:qsTr("Choose recording…"); enabled:!decolog.contestAudio.recording && !decolog.contestAudio.playing; onClicked:contestAudioPlaybackFile.open() }
                Text { Layout.fillWidth:true; text:contestAudioDialog.playbackFile ? String(contestAudioDialog.playbackFile) : qsTr("No recording selected"); color:Theme.textSecondary; elide:Text.ElideMiddle }
            }
            RowLayout {
                Layout.fillWidth:true
                Text { text:qsTr("Playback output"); color:Theme.textSecondary }
                StyledComboBox { Layout.fillWidth:true; model:decolog.contestAudio.outputs; currentIndex:decolog.contestAudio.outputIndex
                    enabled:!decolog.contestAudio.playing; onActivated:decolog.contestAudio.chooseOutput(currentIndex) }
                GlassButton { id:contestAudioPlay; text:qsTr("Play for QSO"); enabled:!decolog.contestAudio.recording && !decolog.contestAudio.playing && String(contestAudioDialog.playbackFile).length>0 && contestAudioDialog.qsoUtc.length>0
                    onClicked:decolog.contestAudio.playForQso(contestAudioDialog.playbackFile,contestAudioDialog.qsoUtc) }
                GlassButton { id:contestAudioStopPlayback; text:qsTr("Stop playback"); enabled:decolog.contestAudio.playing; onClicked:decolog.contestAudio.stopPlayback() }
            }
            RowLayout {
                Layout.fillWidth:true
                GlassButton { id:contestAudioBack; text:qsTr("−10 s"); enabled:decolog.contestAudio.playbackDurationMs>0 && !decolog.contestAudio.recording; onClicked:decolog.contestAudio.seekPlayback(-10) }
                GlassButton { id:contestAudioForward; text:qsTr("+10 s"); enabled:decolog.contestAudio.playbackDurationMs>0 && !decolog.contestAudio.recording; onClicked:decolog.contestAudio.seekPlayback(10) }
                Text { Layout.fillWidth:true; color:Theme.textSecondary
                    text:qsTr("Approx. %1 s / %2 s").arg((decolog.contestAudio.playbackPositionMs/1000).toFixed(1)).arg((decolog.contestAudio.playbackDurationMs/1000).toFixed(1)) }
            }
        }
    }
    FileDialog { id:contestAudioRecoveryFile; title:qsTr("Choose interrupted contest recording"); fileMode:FileDialog.OpenFile; nameFilters:[qsTr("Partial contest audio (*.wav.part)")]; onAccepted:contestAudioDialog.recoverPartial(selectedFile) }
    FileDialog { id:contestAudioPlaybackFile; title:qsTr("Choose contest recording"); fileMode:FileDialog.OpenFile; nameFilters:[qsTr("WAV audio (*.wav)")]; onAccepted:contestAudioDialog.playbackFile=selectedFile }
    FolderDialog { id:contestAudioFolder; title:qsTr("Choose audio recording folder"); onAccepted:contestAudioDialog.folder=selectedFolder }

    DialogFrame {
        id:reportCandidates
        property var result:({})
        property string errorText:""
        property var row:({})
        function search() {
            result=({}); errorText=""
            if (!decolog.activation.startContestReportCandidates(row, candidateTolerance.value)) errorText=qsTr("Cannot search this log now.")
        }
        function openCandidate(candidate) {
            if (!decolog.activation.isContestReportCandidateCurrent(candidate.id,candidate.uuid,candidate.revision)) {
                errorText=qsTr("This QSO or log changed; search again before opening."); return
            }
            close(); reportReview.close(); window.openQso(candidate.id); window.raise(); window.requestActivate()
        }
        dialogKey:"contestReportCandidates"; width:650; height:400
        title:qsTr("Possible local QSO"); dotColor:Theme.primaryColor
        ColumnLayout {
            anchors.fill:parent; spacing:8
            Text { Layout.fillWidth:true; wrapMode:Text.WordWrap; color:Theme.warningColor
                text:qsTr("Suggestions share the reported or suggested corrected call, band and mode family within the selected UTC minute window. Ordered by time proximity, then call basis. Check time, station, frequency, exchange and context; current profile data may have changed. No QSO is changed.") }
            RowLayout {
                Text { text:qsTr("Additional minutes before and after"); color:Theme.textSecondary }
                SpinBox { id:candidateTolerance; from:0; to:5; value:0; enabled:!decolog.activation.contestReportBusy; Accessible.name:qsTr("Additional minutes before and after") }
                GlassButton { id:retryCandidateSearch; text:qsTr("Search again"); enabled:!decolog.activation.contestReportBusy; onClicked:reportCandidates.search() }
            }
            Text { Layout.fillWidth:true; wrapMode:Text.WordWrap; color:Theme.errorColor; visible:reportCandidates.errorText.length>0; text:reportCandidates.errorText }
            Text { Layout.fillWidth:true; color:Theme.textSecondary
                text:decolog.activation.contestReportBusy ? qsTr("Searching…") : qsTr("Line %1 · %2 · ±%3 min · %4 shown%5").arg(reportCandidates.result.line || "—").arg(reportCandidates.result.call || "").arg(reportCandidates.result.toleranceMinutes || 0).arg((reportCandidates.result.candidates || []).length).arg(reportCandidates.result.truncated ? qsTr(" · more matches exist") : "") }
            ListView {
                id:candidateRows; Layout.fillWidth:true; Layout.fillHeight:true; clip:true; model:reportCandidates.result.candidates || []
                ScrollBar.vertical:PanelScrollBar {}
                delegate:RowLayout { required property var modelData; width:candidateRows.width; spacing:6
                    Text { Layout.fillWidth:true; wrapMode:Text.WordWrap; color:Theme.textPrimary; font.family:Theme.monoFamily
                        text:qsTr("QSO #%1 · %2 · station %3 (%4)\nFrequency: QSO %5 kHz / report %6 kHz · received: QSO %7 / report %8 · contest %9")
                            .arg(modelData.id).arg(modelData.utc).arg(modelData.stationCall || qsTr("unknown")).arg(modelData.stationSource)
                            .arg(modelData.frequency || qsTr("unknown")).arg(reportCandidates.result.reportFrequency || qsTr("unknown"))
                            .arg(modelData.receivedExchange || qsTr("unknown")).arg(reportCandidates.result.reportExchange || qsTr("unknown"))
                            .arg(modelData.contestId || qsTr("unknown")) + "\n" + qsTr("Call: %1 (%2)").arg(modelData.call || qsTr("unknown")).arg(modelData.matchBasis === "corrected" ? qsTr("report correction") : qsTr("as reported")) }
                    GlassButton { text:qsTr("Open QSO"); onClicked:reportCandidates.openCandidate(modelData) }
                }
            }
            GlassButton { Layout.alignment:Qt.AlignRight; text:qsTr("Close"); onClicked:reportCandidates.close() }
        }
    }

    FileDialog {
        id: adifDialog
        title: qsTr("ADIF of the session")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "adi"
        nameFilters: [qsTr("ADIF files (*.adi)")]
        onAccepted: decolog.activation.exportAdif(selectedFile)
    }

    FileDialog {
        id: cabrilloFile
        title: qsTr("Cabrillo log")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "log"
        nameFilters: [qsTr("Cabrillo files (*.log *.cbr)"), qsTr("All files (*)")]
        onAccepted: {
            const error = decolog.activation.exportCabrillo(selectedFile, cabrilloDialog.values())
            cabrilloDialog.error = error
            if (error.length === 0)
                cabrilloDialog.close()
        }
    }

    CwMacroEditor { id: oneMacroEditor }


    // ── Testata Cabrillo ────────────────────────────────────────────────────
    DialogFrame {
        id: cabrilloDialog
        property string error: ""
        dialogKey: "cabrillo"
        width: 680
        height: 640
        title: qsTr("Cabrillo header")
        dotColor: Theme.primaryColor

        function openDialog() {
            const d = decolog.activation.cabrilloDefaults()
            contestField.text = d.contest || ""
            callField2.text = d.callsign || ""
            gridField.text = d.gridLocator || ""
            operatorsField.text = d.operators || ""
            locationField.text = d.location || ""
            error = ""
            open()
        }
        function values() {
            return {
                contest: contestField.text, callsign: callField2.text,
                gridLocator: gridField.text, location: locationField.text,
                operators: operatorsField.text, club: clubField.text,
                categoryOperator: opBox.currentText, categoryPower: powerBox.currentText,
                categoryMode: modeBox2.currentText, categoryAssisted: assistedBox.currentText,
                claimedScore: scoreField.text, soapbox: soapboxField.text
            }
        }

        body: ColumnLayout {
            anchors.margins: 14
            spacing: 10

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 12
                rowSpacing: 8

                LabeledField {
                    label: qsTr("Contest (CONTEST)")
                    StyledTextField {
                        id: contestField
                        Layout.preferredWidth: 260
                        uppercase: true
                        placeholderText: "CQ-WW-CW"
                    }
                }
                LabeledField {
                    label: qsTr("Callsign")
                    StyledTextField { id: callField2; Layout.preferredWidth: 260; uppercase: true }
                }
                LabeledField {
                    label: qsTr("Grid")
                    StyledTextField { id: gridField; Layout.preferredWidth: 260; uppercase: true }
                }
                LabeledField {
                    label: qsTr("Location (section, zone)")
                    StyledTextField { id: locationField; Layout.preferredWidth: 260; uppercase: true }
                }
                LabeledField {
                    label: qsTr("Operators")
                    StyledTextField { id: operatorsField; Layout.preferredWidth: 260; uppercase: true }
                }
                LabeledField {
                    label: qsTr("Club")
                    StyledTextField { id: clubField; Layout.preferredWidth: 260; mono: false }
                }
                LabeledField {
                    label: qsTr("Category")
                    StyledComboBox {
                        id: opBox
                        Layout.preferredWidth: 260
                        model: ["SINGLE-OP", "MULTI-OP", "CHECKLOG"]
                    }
                }
                LabeledField {
                    label: qsTr("Power")
                    StyledComboBox {
                        id: powerBox
                        Layout.preferredWidth: 260
                        model: ["LOW", "HIGH", "QRP"]
                    }
                }
                LabeledField {
                    label: qsTr("Mode")
                    StyledComboBox {
                        id: modeBox2
                        Layout.preferredWidth: 260
                        model: ["MIXED", "CW", "SSB", "RTTY", "DIGI", "FM"]
                    }
                }
                LabeledField {
                    label: qsTr("Assisted")
                    StyledComboBox {
                        id: assistedBox
                        Layout.preferredWidth: 260
                        model: ["NON-ASSISTED", "ASSISTED"]
                    }
                }
                LabeledField {
                    label: qsTr("Claimed score")
                    StyledTextField { id: scoreField; Layout.preferredWidth: 260 }
                }
                LabeledField {
                    label: qsTr("Soapbox")
                    StyledTextField { id: soapboxField; Layout.preferredWidth: 260; mono: false }
                }
            }

            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: cabrilloDialog.error.length > 0
                      ? cabrilloDialog.error
                      : qsTr("The exchange sent is the serial number of the session; the received one is what "
                             + "was typed in “Nr r”. Frequencies go out in kHz, VHF and up as the band number.")
                color: cabrilloDialog.error.length > 0 ? Theme.errorColor : Theme.textSecondary
                font.pixelSize: 11
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                GlassButton {
                    text: qsTr("Close")
                    buttonHeight: 28
                    onClicked: cabrilloDialog.close()
                }
                GlassButton {
                    text: qsTr("Write the file…")
                    tone: Theme.primaryColor
                    filled: true
                    buttonHeight: 28
                    enabled: contestField.text.trim().length > 0 && callField2.text.trim().length > 0
                    onClicked: cabrilloFile.open()
                }
            }
        }
    }
}
