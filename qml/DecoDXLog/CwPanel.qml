// DecoDXLog — il CW: le macro che vanno al manipolatore della radio e il decoder
// che legge quello che arriva.
//
// Sta in piedi da solo: non serve aprire il contest. Il testo esce dal
// manipolatore della radio (Hamlib), e quello che entra lo legge DecoDXLog
// dall'audio, senza chiedere niente alla radio.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Dialogs
import QtCore
import Decodium.UI

GlassPanel {
    id: root

    // Con chi si sta parlando, per riempire {CALL} nelle macro. Chi ospita il
    // pannello lo dice; da solo, guarda il nominativo che il log sta cercando.
    property string callsign: decolog.lookupCall
    property string rstSent: "599"
    property string serial: ""
    property string exchange: ""
    property bool compact: false

    readonly property var rig: decolog.rig

    // Le stazioni in banda: di serie una striscia di etichette (una riga sola, anche con cinque
    // segnali); con "Dettagli" una riga per stazione, in una finestra a scorrimento di tre.
    Settings {
        id: view
        category: "layout/cwpanel"
        property bool skimmerDetail: false
    }

    // Il colore del nominativo, dallo stato che ne da' il log (nuovo, gia' lavorato, mancante…).
    function statusColor(status) {
        if (status & 1) return Theme.errorColor
        if (status & 2) return Theme.warningColor
        if (status & 4) return Theme.secondaryColor
        if (status & 8) return Theme.primaryColor
        if (status & (256 | 512 | 2048)) return Theme.warningColor
        if (status & 32) return Theme.textSecondary
        if (status & 16) return Theme.accentColor
        return Theme.textPrimary
    }

    // Per le schermate di prova: apre la tendina dell'ingresso audio.
    function showCombo() { audioBox.popup.open() }
    // Per le schermate di prova: le macro da scrivere, una (0-11) o tutte (-1).
    function showMacros(index) { if (index >= 0) macroEditor.openFor(index); else macroEditor.openAll() }
    // Per le prove: quale macro manda ogni tasto, nell'ordine della griglia.
    function keyTargets() {
        const out = []
        for (let i = 0; i < macroKeys.count; ++i)
            out.push(macroKeys.itemAt(i).macroIndex)
        return out
    }

    function cwContext() {
        return {
            call: root.callsign.trim(),
            rst: root.rstSent.trim(),
            nr: root.serial.trim(),
            exch: root.exchange.trim()
        }
    }

    title: qsTr("CW")
    dotColor: root.rig.connected ? Theme.accentColor : Theme.textSecondary
    headerTools: [
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.rig.connected
                  ? "%1 %2".arg(root.rig.frequencyLabel).arg(root.rig.mode)
                  : root.rig.status
            color: root.rig.connected ? Theme.textSecondary : Theme.warningColor
            font.family: Theme.monoFamily
            font.pixelSize: 11
            elide: Text.ElideRight
        },
        GlassButton {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Stop")
            tone: Theme.errorColor
            buttonHeight: 22
            fontPixelSize: 11
            // Ferma must remain available even after CAT reports disconnected:
            // the radio can still be transmitting, and RigControl has a
            // separate emergency path that can reconnect to rigctld.
            enabled: true
            onClicked: root.rig.stop()
        },
        // Le macro si scrivono anche da qui, non solo dal contest.
        GlassButton {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Macros…")
            buttonHeight: 22
            fontPixelSize: 11
            onClicked: macroEditor.openAll()
        },
        // La puntina: staccato, il CW sta davanti alle altre finestre. Si vede
        // solo quando c'e' una finestra da tenere davanti.
        GlassButton {
            id: onTopButton
            readonly property var hostWindow: root.Window.window
            anchors.verticalCenter: parent.verticalCenter
            // In gara no: le finestre staccate dalla lavagna stanno gia' sopra
            // la principale, come tutte le altre, e la CW non fa eccezione.
            visible: root.detached && hostWindow !== null
                     && hostWindow.alwaysOnTop !== undefined && !hostWindow.contestMode
            text: qsTr("On top")
            tone: Theme.primaryColor
            filled: visible && hostWindow.alwaysOnTop
            buttonHeight: 22
            fontPixelSize: 11
            onClicked: hostWindow.alwaysOnTop = !hostWindow.alwaysOnTop
        }
    ]

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        // Il CAT resta utilizzabile, ma la memoria personale non e' protetta.
        Text {
            Layout.fillWidth: true
            visible: root.rig.connected && root.rig.canKeyCw && root.rig.cwUsesKeyerMemory
            wrapMode: Text.Wrap
            color: Theme.warningColor
            font.pixelSize: 12
            text: qsTr("CW via Yaesu CAT is enabled. Hamlib may overwrite keyer memory 1 with the transmitted text; "
                       + "its previous contents are not restored. Back up that memory before sending. "
                       + "To keep radio memories unchanged, use a separate serial keyer or WinKeyer.")
        }

        Text {
            Layout.fillWidth: true
            visible: root.rig.connected && !root.rig.canKeyCw
            wrapMode: Text.Wrap
            color: Theme.errorColor
            font.pixelSize: 12
            text: qsTr("This CAT link does not key CW: it reads the radio but it cannot send. "
                       + "Either connect rigctld to the radio itself, or — with Decodium holding "
                       + "the CAT — set up the keyer on a serial port of its own: "
                       + "Setup → Radio (CAT) → Keying on a serial port.")
        }

        // La radio spenta non si nasconde: si dice dov'e' l'interruttore.
        Text {
            Layout.fillWidth: true
            visible: !root.rig.enabled
            wrapMode: Text.Wrap
            color: Theme.warningColor
            font.pixelSize: 12
            text: qsTr("The radio is off: Setup → Radio (CAT) to turn it on. The decoder works anyway, "
                       + "it only needs the audio coming out of the radio.")
        }

        // ── Le macro ────────────────────────────────────────────────────────
        // Tasti tutti uguali: la griglia non si allarga per una scritta lunga
        // (si accorcia coi puntini). Tasto destro su un tasto: lo si modifica.
        GridLayout {
            Layout.fillWidth: true
            columns: root.compact ? 8 : 4
            columnSpacing: 6
            rowSpacing: 6
            Repeater {
                id: macroKeys
                model: root.rig.macros
                CwMacroKey {
                    required property var modelData
                    required property int index
                    macroIndex: index
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.minimumWidth: 0
                    macro: modelData
                    keyHeight: 28
                    fontPixelSize: 12
                    sendEnabled: (root.rig.connected || root.rig.keyerOn) && root.rig.canKeyCw
                    tone: root.rig.activeMacroIndex === index ? Theme.accentColor : "transparent"
                    onSendRequested: (i) => root.rig.sendMacro(i, root.cwContext())
                    onEditRequested: (i) => macroEditor.openFor(i)
                }
            }
        }

        // Su macOS la fila superiore nasce per luminosita', Mission Control e
        // volume: fn/Globe la trasforma nel vero F1-F12 che Qt riceve. Ctrl+F
        // e' un'alternativa utile con tastiere senza tasto fn; sugli altri
        // sistemi resta solo la scorciatoia canonica F1-F12.
        Text {
            Layout.fillWidth: true
            visible: Qt.platform.os === "osx"
            text: qsTr("Mac: hold fn (or Globe) and press F1–F12. With a keyboard without fn, Ctrl+F1–F12 also works.")
            color: Theme.textSecondary
            font.pixelSize: 10
            wrapMode: Text.Wrap
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text {
                text: qsTr("Speed")
                color: Theme.textSecondary
                font.pixelSize: 11
            }
            Slider {
                Layout.fillWidth: true
                from: 10
                to: 45
                stepSize: 1
                value: root.rig.wpm
                enabled: root.rig.connected
                onMoved: root.rig.wpm = Math.round(value)
            }
            Text {
                text: qsTr("%1 wpm").arg(root.rig.wpm)
                color: Theme.textPrimary
                font.family: Theme.monoFamily
                font.pixelSize: 12
                font.bold: true
            }
            // Si risponde alla velocita' con cui si riceve: il keyer prende i WPM che il decoder legge.
            GlassButton {
                readonly property real received: Number(root.rig.decoderScope.wpm || 0)
                visible: root.rig.decoderOn
                enabled: root.rig.connected && root.rig.decoderScope.reading === true && received >= 5
                text: qsTr("= RX")
                buttonHeight: 22
                fontPixelSize: 11
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: qsTr("Set the keyer to the speed you are receiving")
                onClicked: root.rig.wpm = Math.min(45, Math.max(10, Math.round(received)))
            }
        }

        // ── Scrivere a mano quello che non sta in una macro ─────────────────
        // Con «Invia mentre scrivi» ogni lettera battuta esce subito in CW, come su una tastiera
        // da CW; senza, il testo parte intero con Invio.
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            ToggleSwitch {
                id: typeSwitch
                text: qsTr("Send as you type")
                checked: root.rig.typeToSend
                onToggled: {
                    root.rig.typeToSend = checked
                    // Quello che c'e' gia' nel campo non parte: conta solo cio' che si scrive dopo.
                    freeText.sentText = freeText.text
                }
            }
            StyledTextField {
                id: freeText
                Layout.fillWidth: true
                uppercase: true
                // Il testo com'era all'ultimo carattere visto: la differenza e' quello che e' stato
                // battuto. Cancellare non richiama quello che e' gia' uscito.
                property string sentText: ""
                placeholderText: root.rig.typeToSend ? qsTr("every letter you type goes out in CW")
                                                     : qsTr("write here and press Enter: it goes out in CW")
                enabled: (root.rig.connected || root.rig.keyerOn) && root.rig.canKeyCw
                onTextEdited: {
                    if (!root.rig.typeToSend) {
                        sentText = text
                        return
                    }
                    const grown = text.length - sentText.length
                    if (grown > 0) {
                        // Dove il testo nuovo comincia a differire da quello vecchio: li' sono
                        // finite le lettere appena battute (in coda, o in mezzo se si e' incollato).
                        let at = 0
                        while (at < sentText.length && sentText.charAt(at) === text.charAt(at))
                            ++at
                        root.rig.sendTyped(text.substring(at, at + grown))
                    }
                    sentText = text
                }
                onAccepted: {
                    if (root.rig.typeToSend) {
                        // Tutto e' gia' uscito: Invio chiude la riga e ricomincia.
                        text = ""
                        sentText = ""
                        return
                    }
                    root.rig.sendText(text, root.cwContext())
                    text = ""
                }
            }
            GlassButton {
                visible: !root.rig.typeToSend
                text: qsTr("Send")
                tone: Theme.accentColor
                buttonHeight: 28
                fontPixelSize: 11
                enabled: (root.rig.connected || root.rig.keyerOn) && root.rig.canKeyCw
                         && freeText.text.trim().length > 0
                onClicked: {
                    root.rig.sendText(freeText.text, root.cwContext())
                    freeText.text = ""
                }
            }
        }

        // ── Il decoder ──────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            ToggleSwitch {
                text: qsTr("Decoder")
                checked: root.rig.decoderOn
                onToggled: root.rig.decoderOn = checked
            }
            // Le parole corrette (Q-code, abbreviazioni): una parola che e' a un errore da una voce
            // del lessico viene sistemata. Cifre e nominativi non si toccano mai.
            ToggleSwitch {
                text: qsTr("Words")
                checked: root.rig.decoderLexicon
                onToggled: root.rig.decoderLexicon = checked
            }
            // Il decodificatore neurale (CTC) e' un'opzione: senza il modello c'e' ggmorse.
            GlassButton {
                visible: root.rig.neuralDecoderAvailable
                buttonHeight: 26
                fontPixelSize: 11
                text: root.rig.decoderNeural ? qsTr("AI: %1").arg(root.rig.decoderModel.split(/[\\/]/).pop())
                                             : qsTr("AI model…")
                tone: root.rig.decoderNeural ? Theme.accentColor : Theme.textSecondary
                onClicked: root.rig.decoderNeural ? root.rig.clearDecoderModel() : neuralModelDialog.open()
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: root.rig.decoderNeural ? qsTr("Click to go back to ggmorse")
                                                     : qsTr("Choose the neural CW model (.onnx) for the decoder")
            }
            FileDialog {
                id: neuralModelDialog
                title: qsTr("Neural CW model")
                nameFilters: [qsTr("ONNX models (*.onnx)")]
                onAccepted: root.rig.chooseDecoderModel(selectedFile)
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                StyledComboBox {
                    id: audioBox
                    Layout.fillWidth: true
                    mono: false
                    fieldHeight: 26
                    // Quello che si vede e' quello che ascolta: «Predefinito» e' una
                    // scelta scritta, e se la scheda scelta non c'e' lo dice invece di
                    // mostrare un'altra riga e usarne una a caso.
                    model: [qsTr("System default")].concat(root.rig.audioInputDevices.map(d => d.label))
                    currentIndex: root.rig.audioInputIndex
                    displayText: root.rig.audioInputIndex < 0 ? qsTr("Not available: %1").arg(root.rig.audioInput)
                                                              : currentText
                    onActivated: (row) => root.rig.chooseAudioInput(row)
                }
                Text {
                    Layout.fillWidth: true
                    visible: root.rig.decoderOn && root.rig.audioInputFormat.length > 0
                    text: qsTr("Input: %1 → mono 16-bit").arg(root.rig.audioInputFormat)
                    color: Theme.textSecondary
                    font.family: Theme.monoFamily
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }
            }
            ColumnLayout {
                Layout.preferredWidth: 122
                spacing: 1
                Text {
                    text: qsTr("Decoder tone")
                    color: Theme.textSecondary
                    font.pixelSize: 10
                }
                StyledComboBox {
                    id: toneBox
                    Layout.fillWidth: true
                    mono: false
                    editable: true
                    fieldHeight: 26
                    popupMinimumWidth: 150
                    readonly property var choices: [0, 400, 500, 550, 570, 575, 600, 610, 615, 620, 625, 630, 650, 700, 800, 1000]
                    model: choices.map(hz => hz === 0 ? qsTr("Auto") : hz + " Hz")
                    currentIndex: choices.indexOf(root.rig.decoderToneLock)
                    Component.onCompleted: editText = root.rig.decoderToneLock === 0 ? qsTr("Auto")
                                                                                      : root.rig.decoderToneLock + " Hz"
                    onActivated: (row) => {
                        root.rig.decoderToneLock = choices[row]
                        editText = root.rig.decoderToneLock === 0 ? qsTr("Auto")
                                                                   : root.rig.decoderToneLock + " Hz"
                    }
                    onAccepted: {
                        const text = editText.trim().toLowerCase()
                        if (text === "auto" || text === "0") {
                            root.rig.decoderToneLock = 0
                            editText = qsTr("Auto")
                            return
                        }
                        const hz = parseInt(text)
                        if (!isNaN(hz)) {
                            root.rig.decoderToneLock = hz
                            editText = root.rig.decoderToneLock + " Hz"
                        }
                    }
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Auto follows the strongest tone. A manual value keeps the decoder on that CW frequency; type an exact value if needed.")
                }
            }
            ColumnLayout {
                Layout.preferredWidth: 116
                spacing: 1
                Text {
                    text: qsTr("Decoder speed")
                    color: Theme.textSecondary
                    font.pixelSize: 10
                }
                StyledComboBox {
                    id: speedBox
                    Layout.fillWidth: true
                    mono: false
                    editable: true
                    fieldHeight: 26
                    popupMinimumWidth: 150
                    // Ricezione e trasmissione non sono la stessa cosa: questa
                    // lista non modifica il cursore Speed sopra, che regola il
                    // keyer. Qui si puo' fissare il ritmo del corrispondente
                    // quando Auto scambia un segnale debole per 40 WPM.
                    readonly property var choices: [0, 10, 12, 15, 16, 17, 18, 19, 20, 22, 25, 27, 30, 35, 40, 45, 50]
                    model: choices.map(wpm => wpm === 0 ? qsTr("Auto") : wpm + " WPM")
                    currentIndex: choices.indexOf(root.rig.decoderSpeedLock)
                    Component.onCompleted: editText = root.rig.decoderSpeedLock === 0 ? qsTr("Auto")
                                                                                       : root.rig.decoderSpeedLock + " WPM"
                    onActivated: (row) => {
                        root.rig.decoderSpeedLock = choices[row]
                        editText = root.rig.decoderSpeedLock === 0 ? qsTr("Auto")
                                                                    : root.rig.decoderSpeedLock + " WPM"
                    }
                    onAccepted: {
                        const text = editText.trim().toLowerCase()
                        if (text === "auto" || text === "0") {
                            root.rig.decoderSpeedLock = 0
                            editText = qsTr("Auto")
                            return
                        }
                        const wpm = parseInt(text)
                        if (!isNaN(wpm)) {
                            root.rig.decoderSpeedLock = wpm
                            editText = root.rig.decoderSpeedLock + " WPM"
                        }
                    }
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Auto learns the other station's speed. Lock a known speed only when Auto is unstable; this does not change transmit speed.")
                }
            }
            Text {
                readonly property var scopeInfo: root.rig.decoderScope
                readonly property bool readingNow: scopeInfo && scopeInfo.reading === true
                visible: root.rig.decoderOn
                // decoderWpm e decoderTone sono l'ultimo aggancio valido. Non
                // usarli mentre il grafico segnala rumore: altrimenti poteva
                // restare scritto, per esempio, "16 wpm · 600 Hz" mentre in
                // realta' il decoder stava vedendo rumore a 52 wpm.
                text: {
                    const speed = root.rig.decoderSpeedLock > 0
                                  ? qsTr("fixed %1 WPM").arg(root.rig.decoderSpeedLock)
                                  : qsTr("%1 WPM").arg(Math.round(scopeInfo.wpm || 0))
                    if (root.rig.decoderToneLock > 0) {
                        return readingNow
                               ? qsTr("fixed %1 Hz · %2").arg(root.rig.decoderToneLock).arg(speed)
                               : qsTr("fixed %1 Hz · noise").arg(root.rig.decoderToneLock)
                    }
                    return readingNow
                           ? qsTr("auto · %1 · %2 Hz").arg(speed)
                                                           .arg(Math.round(scopeInfo.pitch || 0))
                           : qsTr("listening…")
                }
                color: readingNow ? Theme.secondaryColor : Theme.warningColor
                font.family: Theme.monoFamily
                font.pixelSize: 11
            }
            GlassButton {
                text: qsTr("Clear")
                buttonHeight: 24
                fontPixelSize: 11
                onClicked: root.rig.clearDecoder()
            }
        }

        // ── Il grafico, come quello di ggmorse ────────────────────────────
        // Sopra, in verde, i segni come li sta leggendo: una barra per ogni
        // tono sopra la soglia. Sotto, in arancio, il segnale filtrato sul
        // tono negli ultimi tre secondi, con la soglia tratteggiata. Scorre da
        // destra a sinistra: a destra c'e' quello che si sente adesso.
        Rectangle {
            id: scope
            Layout.fillWidth: true
            Layout.preferredHeight: 92
            visible: root.rig.decoderOn
            color: Theme.bgDeep
            border.color: Theme.borderSoft
            radius: 4
            clip: true

            readonly property var info: root.rig.decoderScope
            readonly property var trace: info && info.signal ? info.signal : []
            readonly property real level: info && info.level !== undefined ? info.level : 0
            readonly property bool reading: info ? info.reading === true : false
            readonly property color keyColor: Theme.successColor
            readonly property color traceColor: "#f28c28"
            onInfoChanged: plot.requestPaint()

            Canvas {
                id: plot
                anchors.fill: parent
                anchors.margins: 4
                anchors.bottomMargin: 18
                renderStrategy: Canvas.Cooperative
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    const sig = scope.trace
                    const n = sig.length
                    if (n < 2 || width <= 0)
                        return
                    const keyH = 12
                    const top = keyH + 6
                    const h = height - top
                    const dx = width / (n - 1)

                    // I segni: acceso dove il segnale passa la soglia. Pieno
                    // quando sta leggendo, sbiadito quando e' solo rumore.
                    ctx.fillStyle = scope.keyColor
                    ctx.globalAlpha = scope.reading ? 0.9 : 0.25
                    let start = -1
                    for (let i = 0; i <= n; ++i) {
                        const on = i < n && sig[i] > scope.level
                        if (on && start < 0)
                            start = i
                        else if (!on && start >= 0) {
                            ctx.fillRect(start * dx, 1, Math.max(1.5, (i - start) * dx), keyH - 2)
                            start = -1
                        }
                    }
                    ctx.globalAlpha = 1

                    // I segni con la loro durata vera: i punti in verde, le linee in ambra. La
                    // lunghezza e' quella misurata, non un simbolo: si vede com'e' la mano di chi
                    // trasmette (linee corte, punti lunghi, ritmo irregolare).
                    const els = scope.info.elements || []
                    for (let k = 0; k < els.length; ++k) {
                        const e = els[k]
                        ctx.fillStyle = e.dah ? Theme.warningColor : Theme.accentColor
                        ctx.globalAlpha = 1.0
                        ctx.fillRect(e.at * width, 1, Math.max(2, e.length * width), keyH - 2)
                    }
                    ctx.globalAlpha = 1

                    // Il segnale, pieno sotto la linea.
                    ctx.beginPath()
                    ctx.moveTo(0, top + h)
                    for (let i = 0; i < n; ++i)
                        ctx.lineTo(i * dx, top + h - sig[i] * h)
                    ctx.lineTo(width, top + h)
                    ctx.closePath()
                    ctx.fillStyle = Qt.alpha(scope.traceColor, 0.25)
                    ctx.fill()
                    ctx.beginPath()
                    for (let i = 0; i < n; ++i) {
                        const y = top + h - sig[i] * h
                        if (i === 0)
                            ctx.moveTo(0, y)
                        else
                            ctx.lineTo(i * dx, y)
                    }
                    ctx.strokeStyle = scope.traceColor
                    ctx.lineWidth = 1.2
                    ctx.stroke()

                    // La soglia.
                    const ly = Math.round(top + h - scope.level * h) + 0.5
                    ctx.setLineDash([4, 3])
                    ctx.strokeStyle = Theme.textSecondary
                    ctx.lineWidth = 1
                    ctx.beginPath()
                    ctx.moveTo(0, ly)
                    ctx.lineTo(width, ly)
                    ctx.stroke()
                }
            }

            Text {
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.leftMargin: 6
                anchors.bottomMargin: 3
                text: scope.trace.length === 0
                      ? qsTr("waiting for audio…")
                      : qsTr("F: %1 Hz · S: %2 WPM · C: %3")
                        .arg(Number(scope.info.pitch || 0).toFixed(1))
                        .arg(Math.round(scope.info.wpm || 0))
                        .arg(Number(scope.info.cost || 0).toFixed(3))
                        + (Number(scope.info.fist || 0) > 0
                           ? " · " + qsTr("Fist: %1:1").arg(Number(scope.info.fist).toFixed(1)) : "")
                color: scope.reading ? Theme.textPrimary : Theme.textSecondary
                font.family: Theme.monoFamily
                font.pixelSize: 10
            }
            Text {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.rightMargin: 6
                anchors.bottomMargin: 3
                visible: scope.trace.length > 0
                // La fiducia: verde sopra l'80%, ambra sopra il 60%, rosso sotto. Dice a colpo
                // d'occhio se il testo che scorre merita fiducia in questo momento.
                readonly property real fid: Number(scope.info.confidence || 0)
                text: !scope.reading ? qsTr("noise")
                      : qsTr("reading · %1%").arg(Math.round(fid * 100))
                color: !scope.reading ? Theme.textSecondary
                       : fid > 0.8 ? Theme.accentColor : fid > 0.6 ? Theme.warningColor : Theme.errorColor
                font.pixelSize: 10
                font.bold: scope.reading
            }
        }

        // ── Le stazioni in banda (lo skimmer) ───────────────────────────────
        // Il decoder principale ascolta un tono solo; qui si vedono tutte le portanti che ci sono
        // nell'audio, ognuna letta per conto suo. Un clic su una stazione ferma il decoder sul suo
        // tono; un doppio clic ne porta il nominativo nel QSO.
        RowLayout {
            Layout.fillWidth: true
            visible: root.rig.decoderOn && !root.compact
            spacing: 8
            ToggleSwitch {
                text: qsTr("Stations")
                checked: root.rig.skimmerOn
                onToggled: root.rig.skimmerOn = checked
            }
            ToggleSwitch {
                visible: root.rig.skimmerOn
                text: qsTr("Details")
                checked: view.skimmerDetail
                onToggled: view.skimmerDetail = checked
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("One line per station (call, speed, what it sends) instead of one label each")
            }
            Text {
                visible: root.rig.skimmerOn
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                elide: Text.ElideRight
                color: Theme.textSecondary
                font.pixelSize: 11
                text: root.rig.skimmerStations.length === 0
                      ? qsTr("no signals in the band")
                      : qsTr("signals in the band: %1").arg(root.rig.skimmerStations.length)
            }
            Item { Layout.fillWidth: true; visible: !root.rig.skimmerOn }
        }

        Rectangle {
            id: band
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            visible: root.rig.decoderOn && root.rig.skimmerOn && !root.compact
            color: Theme.bgDeep
            border.color: Theme.borderSoft
            radius: 4
            clip: true

            readonly property var bins: root.rig.skimmerSpectrum
            readonly property real low: root.rig.skimmerLowHz
            readonly property real high: root.rig.skimmerHighHz
            readonly property int mainTone: root.rig.decoderToneLock > 0 ? root.rig.decoderToneLock
                                                                         : Math.round(Number(root.rig.decoderScope.pitch || 0))
            function xAt(hz) { return (hz - low) / Math.max(1, high - low) * canvas.width }
            function hzAt(x) { return low + (high - low) * x / Math.max(1, canvas.width) }
            onBinsChanged: canvas.requestPaint()

            Canvas {
                id: canvas
                anchors.fill: parent
                anchors.margins: 3
                renderStrategy: Canvas.Cooperative
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    const bins = band.bins
                    const n = bins.length
                    if (n < 2 || width <= 0)
                        return
                    const labelH = 10
                    const h = height - labelH
                    const bw = width / n
                    // Lo spettro: barre dal fondo, alte quanto sta sopra il rumore.
                    ctx.fillStyle = Qt.alpha(Theme.secondaryColor, 0.55)
                    for (let i = 0; i < n; ++i) {
                        const v = bins[i]
                        if (v <= 0.02)
                            continue
                        ctx.fillRect(i * bw, h - v * h, Math.max(1, bw - 0.5), v * h)
                    }
                    // I riferimenti ogni 100 Hz.
                    ctx.fillStyle = Theme.textSecondary
                    ctx.font = "8px sans-serif"
                    ctx.textAlign = "center"
                    for (let hz = Math.ceil(band.low / 100) * 100; hz <= band.high; hz += 100) {
                        const x = Math.round(band.xAt(hz)) + 0.5
                        ctx.fillRect(x - 0.5, h, 1, hz % 500 === 0 ? 4 : 2)
                        if (hz % 500 === 0)
                            ctx.fillText(String(hz), x, height - 1)
                    }
                    // Le stazioni: una riga e il nominativo, se c'e'.
                    const stations = root.rig.skimmerStations
                    ctx.textAlign = "left"
                    // Il tono su cui sta il decoder, anche se in quel punto non c'e' nessuno.
                    if (band.mainTone >= band.low && band.mainTone <= band.high) {
                        ctx.fillStyle = Qt.alpha(Theme.accentColor, 0.5)
                        ctx.fillRect(Math.round(band.xAt(band.mainTone)) - 0.5, 0, 1, h)
                    }
                    for (let k = 0; k < stations.length; ++k) {
                        const s = stations[k]
                        const x = Math.round(band.xAt(s.freq)) + 0.5
                        ctx.fillStyle = s.listening ? Theme.accentColor : (s.present ? Theme.warningColor : Theme.textSecondary)
                        ctx.fillRect(x - 0.5, 0, 1.5, h)
                        if (s.call && s.call.length > 0) {
                            ctx.font = "bold 9px sans-serif"
                            const w = ctx.measureText(s.call).width
                            ctx.fillText(s.call, Math.min(x + 3, width - w - 1), 9)
                        }
                    }
                }
            }
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: (mouse) => {
                    let hz = band.hzAt(mouse.x - 3)
                    // Vicino a una stazione: si prende il suo tono preciso.
                    let best = 25
                    const stations = root.rig.skimmerStations
                    for (let k = 0; k < stations.length; ++k) {
                        const d = Math.abs(stations[k].freq - hz)
                        if (d < best) { best = d; hz = stations[k].freq }
                    }
                    root.rig.listenTo(hz)
                }
                ToolTip.visible: containsMouse
                ToolTip.delay: 600
                ToolTip.text: qsTr("Click to listen to the signal at that tone")
            }
        }

        // Le etichette: una per stazione, tutte in una riga (vanno a capo solo se non ci stanno).
        // Il nominativo ha il colore dello stato (nuovo, gia' lavorato…); un clic ascolta, il doppio
        // clic porta il nominativo nel QSO, il tasto destro apre lo spot. Il resto sta nel suggerimento.
        Flow {
            id: chips
            Layout.fillWidth: true
            spacing: 4
            visible: root.rig.decoderOn && root.rig.skimmerOn && !root.compact && !view.skimmerDetail
                     && root.rig.skimmerStations.length > 0
            Repeater {
                model: chips.visible ? root.rig.skimmerStations : []
                delegate: Rectangle {
                    id: chip
                    required property var modelData
                    readonly property var st: modelData
                    readonly property bool known: st.call && st.call.length > 0
                    height: 22
                    width: chipRow.implicitWidth + 12
                    radius: 3
                    color: st.listening ? Qt.alpha(Theme.accentColor, 0.18) : Qt.alpha(Theme.bgMedium, 0.6)
                    border.color: st.listening ? Theme.accentColor : Theme.borderSoft
                    opacity: st.present ? 1.0 : 0.6

                    Row {
                        id: chipRow
                        anchors.centerIn: parent
                        spacing: 5
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: chip.known ? chip.st.call : qsTr("%1 Hz").arg(chip.st.freq)
                            color: chip.known ? root.statusColor(chip.st.status || 0) : Theme.textSecondary
                            font.family: Theme.monoFamily
                            font.pixelSize: 12
                            font.bold: chip.known
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: chip.st.cq === true
                            text: "CQ"
                            color: Theme.accentColor
                            font.pixelSize: 10
                            font.bold: true
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: chip.st.wpm > 0
                            text: String(chip.st.wpm)
                            color: Theme.textSecondary
                            font.family: Theme.monoFamily
                            font.pixelSize: 10
                        }
                    }
                    MouseArea {
                        id: chipMouse
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: (mouse) => {
                            if (mouse.button === Qt.RightButton) {
                                if (chip.known)
                                    spotDialog.openFor(chip.st.call, chip.st.khz || "",
                                                       chip.st.wpm > 0 ? qsTr("CW %1 WPM").arg(chip.st.wpm) : qsTr("CW"))
                            } else {
                                root.rig.listenTo(chip.st.freq)
                            }
                        }
                        onDoubleClicked: (mouse) => {
                            if (mouse.button === Qt.LeftButton && chip.known)
                                decolog.lookupCall = chip.st.call
                        }
                        ToolTip.visible: containsMouse
                        ToolTip.delay: 500
                        ToolTip.text: (chip.known ? chip.st.call + " · " : "")
                                      + qsTr("%1 Hz").arg(chip.st.freq)
                                      + (chip.st.wpm > 0 ? " · " + qsTr("%1 wpm").arg(chip.st.wpm) : "")
                                      + ((chip.st.label || "").length > 0 ? " · " + chip.st.label : "")
                                      + (chip.st.text ? "\n" + chip.st.text : "")
                                      + "\n" + (chip.known
                                                ? qsTr("Click to listen · double-click to put %1 in the QSO · right-click to spot it").arg(chip.st.call)
                                                : qsTr("Click to listen to this signal"))
                    }
                }
            }
        }

        // Una riga per stazione, in una finestra a scorrimento di tre righe al massimo: cinque
        // segnali non allungano piu' il pannello.
        ScrollView {
            id: detailScroll
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(3, Math.max(1, root.rig.skimmerStations.length)) * 26
            visible: root.rig.decoderOn && root.rig.skimmerOn && !root.compact && view.skimmerDetail
                     && root.rig.skimmerStations.length > 0
            clip: true
            contentWidth: availableWidth
            ScrollBar.vertical: PanelScrollBar {}
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            Column {
                width: detailScroll.availableWidth
                spacing: 2
                Repeater {
                    model: detailScroll.visible ? root.rig.skimmerStations : []
                    delegate: Rectangle {
                        id: row
                        required property var modelData
                        readonly property var st: modelData
                        readonly property bool known: st.call && st.call.length > 0
                        width: parent ? parent.width : 0
                        height: 24
                        radius: 3
                        color: st.listening ? Qt.alpha(Theme.accentColor, 0.18) : Qt.alpha(Theme.bgMedium, 0.6)
                        border.color: st.listening ? Theme.accentColor : Theme.borderSoft
                        opacity: st.present ? 1.0 : 0.6

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 6
                            anchors.rightMargin: 4
                            spacing: 6
                            Text {
                                text: qsTr("%1 Hz").arg(row.st.freq)
                                color: Theme.textSecondary
                                font.family: Theme.monoFamily
                                font.pixelSize: 11
                                Layout.preferredWidth: 58
                            }
                            Text {
                                text: row.st.wpm > 0 ? qsTr("%1 wpm").arg(row.st.wpm) : "—"
                                color: Theme.textSecondary
                                font.family: Theme.monoFamily
                                font.pixelSize: 11
                                Layout.preferredWidth: 52
                            }
                            Text {
                                text: row.known ? row.st.call : "…"
                                color: row.known ? root.statusColor(row.st.status || 0) : Theme.textSecondary
                                font.family: Theme.monoFamily
                                font.pixelSize: 12
                                font.bold: row.known
                                Layout.preferredWidth: 78
                                elide: Text.ElideRight
                            }
                            Text {
                                visible: row.st.cq === true
                                text: "CQ"
                                color: Theme.accentColor
                                font.pixelSize: 10
                                font.bold: true
                            }
                            Text {
                                visible: row.known && (row.st.label || "").length > 0
                                text: row.st.label || ""
                                color: root.statusColor(row.st.status || 0)
                                font.pixelSize: 10
                                elide: Text.ElideRight
                                Layout.maximumWidth: 90
                            }
                            Text {
                                Layout.fillWidth: true
                                text: row.st.text
                                color: Theme.textSecondary
                                font.family: Theme.monoFamily
                                font.pixelSize: 10
                                elide: Text.ElideLeft
                                horizontalAlignment: Text.AlignRight
                            }
                            GlassButton {
                                visible: row.known
                                text: qsTr("Spot")
                                buttonHeight: 18
                                fontPixelSize: 10
                                onClicked: spotDialog.openFor(row.st.call, row.st.khz || "",
                                                              row.st.wpm > 0 ? qsTr("CW %1 WPM").arg(row.st.wpm) : qsTr("CW"))
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            anchors.rightMargin: 44
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.rig.listenTo(row.st.freq)
                            onDoubleClicked: { if (row.known) decolog.lookupCall = row.st.call }
                            ToolTip.visible: containsMouse && row.known
                            ToolTip.delay: 600
                            ToolTip.text: qsTr("Click to listen · double-click to put %1 in the QSO").arg(row.st.call)
                            hoverEnabled: true
                        }
                    }
                }
            }
        }

        // Il testo decodificato scorre dentro una misura sua: non allunga il
        // pannello man mano che arriva (prima si allungava fino a non finire).
        ScrollView {
            id: decodedScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 60
            Layout.preferredHeight: 140
            implicitHeight: 140
            ScrollBar.vertical: PanelScrollBar {}
            clip: true
            contentWidth: availableWidth

            TextArea {
                id: decoded
                width: decodedScroll.availableWidth
                readOnly: true
                wrapMode: TextArea.WrapAnywhere
                // I nominativi sono collegamenti: un clic li porta nel QSO. Il testo si prende col
                // mouse (un doppio clic sceglie una parola, trascinando se ne sceglie quanto si
                // vuole) e si copia con Ctrl+C.
                textFormat: TextEdit.RichText
                selectByMouse: true
                persistentSelection: true
                onLinkActivated: (link) => {
                    if (link.indexOf("call:") === 0)
                        decolog.lookupCall = link.substring(5)
                }
                color: Theme.textPrimary
                font.family: Theme.monoFamily
                font.pixelSize: 13
                background: Rectangle { color: Theme.bgMedium; border.color: Theme.borderSoft; radius: 4 }

                // Con una selezione in corso il testo aspetta: se scorresse, la selezione si
                // perderebbe a ogni carattere che arriva. Quando la selezione se ne va, il testo
                // raggiunge quello che c'e' di nuovo.
                readonly property string html: { const unused = root.rig.decoderText; return root.rig.decoderHtml(String(Theme.accentColor)) }
                readonly property bool holding: selectionStart !== selectionEnd
                function refresh() {
                    if (holding)
                        return
                    text = html
                    // Si guarda sempre l'ultima riga, come in una telescrivente.
                    cursorPosition = length
                }
                onHtmlChanged: refresh()
                onHoldingChanged: refresh()
                Component.onCompleted: refresh()
            }
        }
    }

    CwMacroEditor { id: macroEditor }
    SpotDialog { id: spotDialog }

    // I tasti funzione, quando il pannello ha il fuoco.
    Repeater {
        model: Math.min(12, root.rig.macros.length)
        Item {
            required property int index
            Shortcut {
                // fn/Globe+F su macOS arriva qui come F. L'alternativa Ctrl+F
                // serve alle tastiere Mac esterne prive di fn, senza cambiare
                // nulla a Windows e Linux.
                sequences: Qt.platform.os === "osx"
                           ? ["F" + (index + 1), "Ctrl+F" + (index + 1)]
                           : ["F" + (index + 1)]
                // In gara i tasti funzione li tiene l'inserimento del contest (ESM).
                enabled: root.visible && (root.rig.connected || root.rig.keyerOn)
                         && root.rig.canKeyCw && !decolog.activation.active
                onActivated: decolog.functionKey(index, root.cwContext())
            }
        }
    }
}
