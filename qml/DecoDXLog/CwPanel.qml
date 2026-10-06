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
            // Se il CAT cade durante una macro affidata al manipolatore
            // seriale/WinKeyer, Ferma deve restare disponibile: il keyer puo'
            // ancora avere il buffer e la linea di manipolazione attivi.
            enabled: root.rig.connected || root.rig.keyerOn || root.rig.activeMacroIndex >= 0
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

        // Il collegamento c'e' ma il CW non passa: lo si dice, invece di
        // lasciare i tasti che non fanno niente.
        Text {
            Layout.fillWidth: true
            visible: root.rig.connected && root.rig.cwMemoryProtected
            wrapMode: Text.Wrap
            color: Theme.errorColor
            font.pixelSize: 12
            text: qsTr("CW via this Yaesu CAT link is disabled to protect keyer memory 1: Hamlib would overwrite "
                       + "it with every message sent from DecoDXLog. The CAT remains available for frequency and mode. "
                       + "For CW, configure a separate serial keyer or WinKeyer in Setup → Radio (CAT) → Keying on a serial port.")
        }

        Text {
            Layout.fillWidth: true
            visible: root.rig.connected && !root.rig.canKeyCw && !root.rig.cwMemoryProtected
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
        }

        // ── Scrivere a mano quello che non sta in una macro ─────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            StyledTextField {
                id: freeText
                Layout.fillWidth: true
                uppercase: true
                placeholderText: qsTr("write here and press Enter: it goes out in CW")
                enabled: (root.rig.connected || root.rig.keyerOn) && root.rig.canKeyCw
                onAccepted: {
                    root.rig.sendText(text, root.cwContext())
                    text = ""
                }
            }
            GlassButton {
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
                text: scope.reading ? qsTr("reading") : qsTr("noise")
                color: scope.reading ? scope.keyColor : Theme.textSecondary
                font.pixelSize: 10
                font.bold: scope.reading
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
                text: root.rig.decoderText
                color: Theme.textPrimary
                font.family: Theme.monoFamily
                font.pixelSize: 13
                background: Rectangle { color: Theme.bgMedium; border.color: Theme.borderSoft; radius: 4 }
                // Si guarda sempre l'ultima riga, come in una telescrivente.
                onTextChanged: cursorPosition = length
            }
        }
    }

    CwMacroEditor { id: macroEditor }

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
