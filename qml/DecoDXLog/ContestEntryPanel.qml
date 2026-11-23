// DecoDXLog — l'inserimento veloce del contest, in un pannello suo.
//
// In gara questa e' la finestra dove stanno le mani: nominativo, rapporti,
// scambio, Invio. Sta in una finestra a sé perche' durante un contest ognuno
// la mette dove vuole — di solito in mezzo, davanti a tutto — e le altre
// (cluster, log, mappa) si dispongono attorno.
//
// Enter registra, Esc pulisce, Tab passa al campo dopo. Lo spazio dopo il
// nominativo salta allo scambio: e' il gesto che si fa senza pensarci.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import Decodium.UI

GlassPanel {
    id: root

    property int revision: 0
    readonly property var act: decolog.activation
    readonly property var session: { revision; return act.state }
    readonly property var scoring: { revision; return act.score() }
    readonly property bool running: act.active
    property var experimentalSelection: ({})
    readonly property var experimentalGrant: { const status=decolog.net.durableSerialStatus; return decolog.net.durableSerialGrantForQso() }
    function serialForExchange() { return useExperimentalSerial.checked ? String(experimentalSelection.serial || "") : String(root.session.nextSerial || 1) }
    function selectedGrantCurrent() {
        if (!useExperimentalSerial.checked) return true
        const current=decolog.net.durableSerialGrantForQso(), selected=root.experimentalSelection
        return !!selected.serial && current.serial===selected.serial && current.scope===selected.scope && current.authority===selected.authority && current.requester===selected.requester && current.requestId===selected.requestId
    }
    function probeExperimentalSerial() {
        useExperimentalSerial.checked=true
        const macro=root.macroContext().nr, saved=root.experimentalSelection
        root.experimentalSelection=Object.assign({},saved,{serial:11})
        root.sendKey(2)
        const staleRejected=message.text.indexOf("selected serial grant changed")>=0
        root.experimentalSelection=saved; callField.text="K1CONTEST"; rcvdNr.text="5"; root.logQso()
        return {macro:macro,staleRejected:staleRejected,error:message.text}
    }

    property string band: ""
    property string mode: ""

    // ── ESM e tasti funzione ────────────────────────────────────────────────
    // Enter Sends Message, come nei log da contest: in Run, Invio a campo vuoto
    // chiama CQ, con il nominativo manda nominativo e scambio, con lo scambio
    // manda il grazie e registra. In S&P, Invio col nominativo manda il proprio,
    // con lo scambio manda il proprio scambio e registra.
    Settings {
        id: esmStore
        category: "contest"
        property bool esm: true
        property bool run: true
    }
    readonly property bool phone: root.mode === "SSB"
    function macroContext() {
        return {
            call: callField.text.trim(),
            rst: sentRst.text.trim(),
            nr: root.session.serialEnabled ? root.serialForExchange() : "",
            exch: rcvdNr.text.trim(),
            mode: root.mode
        }
    }
    function sendKey(index) {
        if (!root.selectedGrantCurrent()) { message.text=qsTr("The selected serial grant changed; review it before transmitting."); return }
        decolog.functionKey(index, root.macroContext())
    }
    function enterPressed(field) {
        if (!esmStore.esm) {
            root.logQso()
            return
        }
        const hasCall = callField.text.trim().length > 0
        const hasExch = rcvdNr.text.trim().length > 0
        if (esmStore.run) {
            if (!hasCall) { root.sendKey(0); return }                       // F1 CQ
            if (field === "call" || !hasExch) {
                root.sendKey(2)                                             // F3 nominativo + scambio
                root.goToExchange()
                return
            }
            root.sendKey(3)                                                 // F4 grazie
            root.logQso()
        } else {
            if (!hasCall)
                return
            if (field === "call" || !hasExch) {
                root.sendKey(8)                                             // F9 il proprio nominativo
                root.goToExchange()
                return
            }
            root.sendKey(9)                                                 // F10 il proprio scambio
            root.logQso()
        }
    }
    Repeater {
        model: 12
        Item {
            required property int index
            Shortcut {
                sequence: "F" + (index + 1)
                enabled: root.visible && root.running
                onActivated: root.sendKey(index)
            }
        }
    }
    // Ctrl+W pulisce l'inserimento; Ctrl+R passa fra Run e S&P.
    Shortcut { sequence: "Ctrl+W"; enabled: root.visible; onActivated: root.clearEntry() }
    Shortcut { sequence: "Ctrl+R"; enabled: root.visible && root.running; onActivated: esmStore.run = !esmStore.run }

    Connections {
        target: decolog
        function onLogChanged() { root.revision++ }
    }
    Connections {
        target: decolog.activation
        function onChanged() { root.revision++ }
    }

    // Un clic su uno spot del cluster: il nominativo, la banda e il modo
    // vengono qui, la finestra passa davanti e il cursore va sullo scambio —
    // la stazione si chiama, si scrive quello che manda, Invio.
    function pickSpot(call, band, mode) {
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
        message.text = ""
        const w = root.Window.window
        if (w) {
            w.raise()
            w.requestActivate()
        }
        root.goToExchange()
    }
    Connections {
        target: decolog.cluster
        function onSpotPicked(call, band, mode, freqKhz) { root.pickSpot(call, band, mode) }
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
        if (band.length === 0)
            band = session.band || "20m"
        if (mode.length === 0)
            mode = root.startingMode()
        sentRst.text = mode === "SSB" ? "59" : "599"
        rcvdRst.text = sentRst.text
        callField.forceActiveFocus()
    }

    // Quello che non va nello scambio, mentre lo si scrive: finche' il campo e'
    // vuoto non si dice niente, perche' avvisare prima che si scriva e' rumore.
    readonly property string exchangeProblem: {
        revision
        if (!running || rcvdNr.text.trim().length === 0)
            return ""
        return act.checkExchange(rcvdNr.text)
    }
    readonly property bool duplicate: callField.text.trim().length > 0
                                      && act.wouldDuplicate(callField.text, root.band, root.mode)

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
            srx: rcvdNr.text, experimental_serial: useExperimentalSerial.checked, experimental_serial_grant: root.experimentalSelection
        })
        if (error.length > 0) {
            message.text = error
            return
        }
        const problem = root.exchangeProblem
        useExperimentalSerial.checked=false
        root.clearEntry()
        // Lo scambio storto non ferma il QSO — la stazione e' gia' passata — ma
        // si dice, cosi' si corregge adesso invece che a spoglio fatto.
        if (problem.length > 0)
            message.text = problem
    }

    // Le dodici macro CW, da scrivere come servono.
    CwMacroEditor {
        id: macroPopup
        // Come vanno i tasti con l'ESM, in gara.
        note: qsTr("{CALL} the station you work, {MYCALL} yours, {RST} the report, {NR} your serial, {EXCH} what you received. "
                               + "With ESM in Run: Enter sends F1 on an empty call, F3 with the call, F4 and logs with the exchange. "
                               + "In S&P: F9 with the call, F10 and logs with the exchange. In phone the keys play the voice keyer.")
    }

    // SO2R dalla tastiera: Ctrl+freccia sceglie la radio, Pausa le scambia,
    // l'accento grave accende e spegne l'ascolto stereo.
    Shortcut { sequence: "Ctrl+Left"; enabled: root.visible && decolog.so2r.enabled; onActivated: decolog.so2r.focus = 1 }
    Shortcut { sequence: "Ctrl+Right"; enabled: root.visible && decolog.so2r.enabled; onActivated: decolog.so2r.focus = 2 }
    Shortcut { sequence: "Pause"; enabled: root.visible && decolog.so2r.enabled; onActivated: decolog.so2r.toggleFocus() }
    Shortcut { sequence: "`"; enabled: root.visible && decolog.so2r.enabled; onActivated: decolog.so2r.toggleStereo() }

    // Un nominativo proposto dal Super Check Partial: un clic lo mette nel campo.
    component CallChip: Rectangle {
        id: chip
        property string call: ""
        property string typed: ""
        property color tone: Theme.textPrimary
        implicitWidth: chipText.implicitWidth + 10
        implicitHeight: 20
        radius: 3
        color: chipHover.hovered ? Theme.bgMedium : "transparent"
        border.width: 1
        border.color: Theme.borderSoft
        Text {
            id: chipText
            anchors.centerIn: parent
            textFormat: Text.StyledText
            // Il pezzo scritto si vede in evidenza.
            text: {
                const i = chip.typed.length > 0 && chip.typed.indexOf("?") < 0 ? chip.call.indexOf(chip.typed) : -1
                if (i < 0)
                    return chip.call
                return chip.call.substring(0, i) + "<b><font color='" + Theme.accentColor + "'>"
                     + chip.call.substr(i, chip.typed.length) + "</font></b>" + chip.call.substring(i + chip.typed.length)
            }
            color: chip.tone
            font.family: Theme.monoFamily
            font.pixelSize: 12
        }
        HoverHandler { id: chipHover; cursorShape: Qt.PointingHandCursor }
        TapHandler {
            onTapped: {
                callField.text = chip.call
                callField.forceActiveFocus()
            }
        }
    }
    title: root.running ? qsTr("%1 · next %2").arg(root.session.title || qsTr("Contest"))
                                              .arg(root.serialForExchange() || qsTr("Unavailable"))
                        : qsTr("Contest entry · no session")
    dotColor: root.running ? Theme.accentColor : Theme.textSecondary

    // Quanto serve per vedere tutti i campi e Registra: chi la ospita non la
    // stringe sotto questa misura, anche quando i campi vanno a capo.
    implicitHeight: entryColumn.implicitHeight + 20 + padding * 2 + Theme.panelHeight + 2

    ColumnLayout {
        id: entryColumn
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            LabeledField {
                label: qsTr("Band")
                StyledComboBox {
                    id: bandBox
                    Layout.preferredWidth: 96
                    readonly property var bands: ["160m", "80m", "40m", "30m", "20m", "17m",
                                                  "15m", "12m", "10m", "6m", "2m"]
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
                    Layout.preferredWidth: 96
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
            // Run o S&P, e l'ESM acceso o spento.
            GlassButton {
                Layout.alignment: Qt.AlignBottom
                text: esmStore.run ? "RUN" : "S&P"
                tone: esmStore.run ? Theme.accentColor : Theme.secondaryColor
                filled: true
                onClicked: esmStore.run = !esmStore.run
            }
            GlassButton {
                Layout.alignment: Qt.AlignBottom
                text: "ESM"
                tone: Theme.primaryColor
                filled: esmStore.esm
                onClicked: esmStore.esm = !esmStore.esm
            }
            GlassButton {
                Layout.alignment: Qt.AlignBottom
                text: qsTr("Macros…")
                onClicked: macroPopup.openAll()
            }
            Item { Layout.fillWidth: true }
            // SO2R: le due radio, quella col fuoco evidenziata. Un clic la sceglie.
            Repeater {
                model: decolog.so2r.enabled ? [1, 2] : []
                GlassButton {
                    required property int modelData
                    Layout.alignment: Qt.AlignBottom
                    readonly property bool focused: decolog.so2r.focus === modelData
                    readonly property real hz: modelData === 1 ? decolog.rig.frequencyHz : decolog.so2r.radio2Hz
                    text: "R" + modelData + " " + (hz > 0 ? (hz / 1000).toFixed(1) : "—")
                    tone: focused ? Theme.accentColor : Theme.textSecondary
                    filled: focused
                    onClicked: decolog.so2r.focus = modelData
                }
            }
            Pill {
                visible: decolog.so2r.enabled && decolog.so2r.stereo
                text: qsTr("stereo")
                tone: Theme.secondaryColor
            }
            Pill {
                visible: root.duplicate
                text: qsTr("already worked")
                tone: Theme.errorColor
            }
        }

        // Quando la finestra e' stretta i campi vanno a capo invece di
        // schiacciare il nominativo fino a farlo sparire: in una finestra da
        // mettere dove si vuole, "stretta" succede.
        GridLayout {
            Layout.fillWidth: true
            columns: root.width < 660 ? 4 : 6
            rowSpacing: 6
            columnSpacing: 8
            LabeledField {
                Layout.fillWidth: true
                Layout.minimumWidth: 150
                Layout.columnSpan: parent.columns === 4 ? 4 : 1
                label: qsTr("Callsign")
                StyledTextField {
                    id: callField
                    Layout.fillWidth: true
                    uppercase: true
                    fieldHeight: 40
                    font.pixelSize: 22
                    accentBorder: root.duplicate ? Theme.errorColor : Theme.primaryColor
                    onTextChanged: {
                        decolog.lookupCall = text
                        suggestTimer.restart()
                    }
                    Keys.onSpacePressed: root.goToExchange()
                    Keys.onReturnPressed: root.enterPressed("call")
                    Keys.onEnterPressed: root.enterPressed("call")
                    Keys.onEscapePressed: { decolog.stopSending(); root.clearEntry() }
                }
            }
            LabeledField {
                label: qsTr("RST s")
                StyledTextField { id: sentRst; Layout.preferredWidth: 72; fieldHeight: 40; text: "599" }
            }
            LabeledField {
                label: qsTr("Nr s")
                StyledTextField {
                    Layout.preferredWidth: 72
                    fieldHeight: 40
                    readOnly: true
                    text: root.session.serialEnabled ? (root.serialForExchange() || qsTr("Unavailable")) : "—"
                }
            }
            LabeledField {
                label: qsTr("RST r")
                StyledTextField { id: rcvdRst; Layout.preferredWidth: 72; fieldHeight: 40; text: "599" }
            }
            LabeledField {
                // Il nome del campo lo decide il contest: zona, provincia,
                // sezione o numero.
                label: root.exchangeLabelText()
                StyledTextField {
                    id: rcvdNr
                    Layout.preferredWidth: 130
                    onTextChanged: if (!root.settingAuto) root.autoFrom = ""
                    color: root.autoFrom.length > 0 ? Theme.accentColor : Theme.textPrimary
                    fieldHeight: 40
                    uppercase: true
                    accentBorder: root.exchangeProblem.length > 0 ? Theme.warningColor
                                                                  : Theme.primaryColor
                    Keys.onReturnPressed: root.enterPressed("exch")
                    Keys.onEnterPressed: root.enterPressed("exch")
                    Keys.onEscapePressed: { decolog.stopSending(); root.clearEntry() }
                }
            }
            GlassButton {
                Layout.alignment: Qt.AlignBottom
                Layout.bottomMargin: 2
                Layout.columnSpan: parent.columns === 4 ? 4 : 1
                Layout.fillWidth: parent.columns === 4
                text: qsTr("Log")
                tone: Theme.accentColor
                filled: true
                buttonHeight: 40
                enabled: root.running && callField.text.trim().length > 0
                onClicked: root.logQso()
            }
        }

        CheckBox {
            id:useExperimentalSerial
            Layout.fillWidth:true
            visible:root.session.serialEnabled && (decolog.net.durableSerialStatus || "").length>0
            property var shownGrant: checked ? root.experimentalSelection : root.experimentalGrant
            onCheckedChanged:root.experimentalSelection=checked ? Object.assign({},root.experimentalGrant) : ({})
            text:shownGrant.serial ? qsTr("Use experimental serial %1 · scope %2 · authority %3").arg(shownGrant.serial).arg(shownGrant.scope).arg(shownGrant.authority) : qsTr("Use experimental serial: no grant available")
            contentItem:Text { text:useExperimentalSerial.text; color:Theme.warningColor; wrapMode:Text.WordWrap; leftPadding:useExperimentalSerial.indicator.width+6 }
        }
        Text { Layout.fillWidth:true; visible:useExperimentalSerial.visible && useExperimentalSerial.checked; color:Theme.warningColor; wrapMode:Text.WordWrap; font.pixelSize:11
            text:qsTr("UDP IDs are unauthenticated; multiple authorities can issue the same number. Verify before transmitting.") }

        // ── Super Check Partial e N+1 ──────────────────────────────────────
        // Mentre si scrive: i nominativi veri che contengono quello che c'e'
        // nel campo, e quelli a un carattere di distanza. Un clic lo mette nel
        // campo.
        ColumnLayout {
            id: scpBox
            Layout.fillWidth: true
            spacing: 3
            visible: root.running
            readonly property string typed: callField.text.trim().toUpperCase()
            readonly property var partial: { decolog.scp.count; return typed.length >= 2 ? decolog.scp.partial(typed, 30) : [] }
            readonly property var neighbours: { decolog.scp.count; return typed.length >= 3 ? decolog.scp.nPlusOne(typed, 10) : [] }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Text {
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 2
                    text: "SCP"
                    color: Theme.textSecondary
                    font.family: Theme.monoFamily
                    font.pixelSize: 11
                    font.bold: true
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 4
                    Repeater {
                        model: scpBox.partial
                        CallChip { required property string modelData; call: modelData; typed: scpBox.typed }
                    }
                    Text {
                        visible: scpBox.partial.length === 0
                        text: scpBox.typed.length >= 2 ? (decolog.scp.known(scpBox.typed) ? "" : qsTr("no known call"))
                                                       : decolog.scp.status
                        color: Theme.textSecondary
                        font.pixelSize: 11
                    }
                }
                GlassButton {
                    Layout.alignment: Qt.AlignTop
                    visible: decolog.scp.fileCount === 0 || decolog.scp.busy
                    text: decolog.scp.busy ? qsTr("Downloading…") : qsTr("Download MASTER.SCP")
                    buttonHeight: 20
                    fontPixelSize: 10
                    enabled: !decolog.scp.busy
                    onClicked: decolog.scp.download()
                }
            }
            // I moltiplicatori del nominativo: dove sono gia' presi e dove mancano.
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                readonly property var check: { root.revision; return scpBox.typed.length >= 3 ? decolog.activation.multiplierCheck(scpBox.typed, root.band) : [] }
                visible: check.length > 0
                Text {
                    text: "MULT"
                    color: Theme.accentColor
                    font.family: Theme.monoFamily
                    font.pixelSize: 11
                    font.bold: true
                }
                Repeater {
                    model: parent.check
                    Text {
                        required property var modelData
                        textFormat: Text.StyledText
                        text: {
                            let t = "<b>" + modelData.label + "</b>"
                            if (modelData.perBand) {
                                if (modelData.worked.length) t += " ✓" + modelData.worked.join(" ")
                                if (modelData.needed.length) t += " <font color='" + Theme.warningColor + "'>" + qsTr("needed") + " " + modelData.needed.join(" ") + "</font>"
                            } else {
                                t += modelData.newHere ? " <font color='" + Theme.warningColor + "'>" + qsTr("new") + "</font>" : " ✓"
                            }
                            return t
                        }
                        color: modelData.newHere ? Theme.warningColor : Theme.textSecondary
                        font.family: Theme.monoFamily
                        font.pixelSize: 11
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                visible: scpBox.neighbours.length > 0
                Text {
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 2
                    text: "N+1"
                    color: Theme.warningColor
                    font.family: Theme.monoFamily
                    font.pixelSize: 11
                    font.bold: true
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 4
                    Repeater {
                        model: scpBox.neighbours
                        CallChip { required property string modelData; call: modelData; tone: Theme.warningColor }
                    }
                }
            }
        }

        // I dodici tasti: in CW le macro, in fonia i messaggi registrati.
        GridLayout {
            Layout.fillWidth: true
            visible: root.running
            columns: root.width > 760 ? 12 : 6
            columnSpacing: 4
            rowSpacing: 4
            Repeater {
                // In fonia i dodici messaggi del keyer vocale; in CW le macro che ci sono.
                model: root.phone ? 12 : Math.min(12, decolog.rig.macros.length)
                GlassButton {
                    required property int index
                    Layout.fillWidth: true
                    // Tutti uguali: una scritta lunga non allarga la griglia.
                    Layout.preferredWidth: 1
                    Layout.minimumWidth: 0
                    clip: true
                    buttonHeight: 24
                    fontPixelSize: 10
                    readonly property var macro: decolog.rig.macros[index] || ({})
                    readonly property var voice: decolog.dvk.messages[index] || null
                    // In CW la scritta e' tutta della macro ("F1" compreso).
                    text: root.phone ? "F" + (index + 1) + " " + (voice ? voice.label : "—")
                                     : (macro.label || "F" + (index + 1))
                    enabled: root.phone ? (voice !== null && voice.present) : true
                    tone: (esmStore.run && (index === 0 || index === 2 || index === 3))
                          || (!esmStore.run && (index === 8 || index === 9)) ? Theme.accentColor : Theme.primaryColor
                    onClicked: root.sendKey(index)
                }
            }
        }

        Text {
            id: message
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.warningColor
            font.pixelSize: 12
            text: root.running ? "" : qsTr("Open a session from Contest, then the QSOs come in here.")
        }
        Item { Layout.fillHeight: true }
    }
}
