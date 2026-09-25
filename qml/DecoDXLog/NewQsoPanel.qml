// DecoDXLog — a sinistra: il QSO appena arrivato da Decodium e la scheda compatta
// per quelli a voce (mockup 1a). La scheda completa e' NewQsoDialog.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decodium.UI

GlassPanel {
    id: root

    signal expandRequested()

    readonly property var last: decolog.incoming.length > 0 ? decolog.incoming[0] : null

    title: qsTr("New QSO")
    dotColor: Theme.accentColor
    padding: 0
    headerTools: [
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("manual")
            color: Theme.textSecondary
            font.family: Theme.monoFamily
            font.pixelSize: 11
        },
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "⤢"
            color: expandArea.containsMouse ? Theme.primaryColor : Theme.textSecondary
            font.pixelSize: Theme.fontSize + 2
            MouseArea {
                id: expandArea
                anchors.fill: parent
                anchors.margins: -4
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.expandRequested()
            }
            ToolTip.visible: expandArea.containsMouse
            ToolTip.text: qsTr("Full form (Ctrl+N)")
        }
    ]

    // Per le schermate di prova: apre la tendina dei modi.
    function showModes() { modeBox.popup.open() }

    // La radio va dove si sceglie, come nell'inserimento della gara: una
    // banda nuova porta la radio li' (dove la si era lasciata in quel modo, o
    // all'inizio del segmento del modo), un modo nuovo cambia il modo. Senza
    // radio e senza Decodium la frequenza si scrive lo stesso nel campo.
    property var bandMemory: ({})
    property double lastQsy: 0
    function memoryKey(band, mode) {
        const m = ["LSB", "USB", "AM", "FM", "SSB"].indexOf(mode) >= 0 ? "SSB"
                : mode === "CW-R" || mode === "CWR" ? "CW" : mode
        return band + "|" + m
    }
    function qsyTo(band, mode, bandChanged) {
        if (!band || band === "—")
            return
        const m = String(mode || "").toUpperCase()
        const here = parseFloat(decolog.shownFrequency || "0")
        const hereBand = here > 0 ? decolog.bandForFrequency(String(here)) : ""
        const hereMode = String(decolog.shownMode || "").toUpperCase()
        if (hereBand.length > 0 && hereMode.length > 0)
            root.bandMemory[root.memoryKey(hereBand, hereMode)] = here
        let mhz = 0
        if (bandChanged || band !== hereBand)
            mhz = root.bandMemory[root.memoryKey(band, m)] || decolog.bandFrequency(band, m)
        // I digitali hanno la loro frequenza di chiamata anche restando in banda.
        else if (["FT8", "FT4", "FT2"].indexOf(m) >= 0)
            mhz = decolog.bandFrequency(band, m)
        if (mhz > 0)
            freqField.text = Number(mhz).toFixed(6)
        if (decolog.rig.connected || decolog.decoLinkClients.length > 0) {
            root.lastQsy = Date.now()
            decolog.tuneTo(mhz, m)
        }
    }

    // E al contrario: girando la manopola, o cambiando modo sulla radio,
    // frequenza, banda e modo qui seguono — tranne mentre si scrive la
    // frequenza a mano, e subito dopo aver mandato la radio da qualche parte.
    Connections {
        target: decolog
        function onTuningChanged() {
            if (Date.now() - root.lastQsy < 2500 || freqField.activeFocus)
                return
            const f = decolog.shownFrequency || ""
            if (f.length > 0 && f !== freqField.text)
                freqField.text = f
            const raw = String(decolog.shownMode || "").toUpperCase()
            const m = raw === "LSB" || raw === "USB" ? "SSB" : raw === "CW-R" || raw === "CWR" ? "CW" : raw
            const i = modeBox.find(m)
            if (i >= 0 && modeBox.currentIndex !== i && !modeBox.activeFocus)
                modeBox.currentIndex = i
        }
    }

    function resetTime() {
        const now = decolog.utcNow()
        dateField.text = decolog.showDate(now.date)
        timeField.text = now.time
    }

    function clearForm() {
        callField.text = ""
        nameField.text = ""
        qthField.text = ""
        gridField.text = ""
        commentField.text = ""
        propModeBox.currentIndex = 0
        satNameField.text = ""
        satModeField.text = ""
        srxField.text = ""
        rcvdField.text = "59"
        formError.text = ""
        resetTime()
        callField.forceActiveFocus()
    }

    function submit() {
        const error = decolog.logManualQso({
            call: callField.text, date: dateField.text, time: timeField.text,
            freq: freqField.text, band: bandBox.currentIndex > 0 ? bandBox.currentText : "",
            mode: modeBox.editText, rst_sent: sentField.text, rst_rcvd: rcvdField.text,
            gridsquare: gridField.text, name: nameField.text, qth: qthField.text, comment: commentField.text,
            prop_mode: propModeBox.editText, sat_name: satNameField.text, sat_mode: satModeField.text,
            srx: srxField.text
        })
        formError.text = error
        if (error.length === 0)
            clearForm()
    }

    Component.onCompleted: resetTime()

    // Uno spot scelto nel cluster: il DX finisce qui, pronto da registrare.
    // Il nominativo si sostituisce sempre — cliccare uno spot vuol dire "adesso
    // lavoro questo" — ma quello che l'operatore ha scritto di suo (nome, QTH,
    // rapporti, commento) non si tocca: lo aggiorna semmai il callbook, e solo
    // dove e' vuoto.
    Connections {
        target: decolog
        function onQsoPrepared(fields) {
            if (!fields || !fields.call)
                return
            if (callField.text.trim().toUpperCase() !== String(fields.call).toUpperCase()) {
                root.clearForm()
                callField.text = fields.call
            }
            if (fields.mhz > 0)
                freqField.text = Number(fields.mhz).toFixed(6)
            if (fields.mode) {
                const i = modeBox.find(fields.mode)
                if (i >= 0) modeBox.currentIndex = i
                else modeBox.editText = fields.mode
            }
            if (fields.grid && gridField.text.length === 0)
                gridField.text = fields.grid
        }
    }

    // Come nella scheda completa: il callbook riempie solo i campi vuoti.
    Connections {
        target: decolog
        function onLookupChanged() {
            const cb = decolog.callInfo.callbook
            if (!decolog.callbookAutofill || !cb || callField.text.trim().toUpperCase() !== decolog.callInfo.call)
                return
            if (nameField.text.length === 0) nameField.text = cb.name
            if (qthField.text.length === 0) qthField.text = cb.qth
            if (gridField.text.length === 0) gridField.text = cb.grid
        }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true
        ScrollBar.vertical: PanelScrollBar {}

        ColumnLayout {
            width: parent.width
            spacing: 0

            // L'ultimo QSO ricevuto via UDP: si vede subito se e' stato salvato.
            Rectangle {
                id: incomingCard
                visible: root.last !== null
                Layout.fillWidth: true
                Layout.margins: 8
                Layout.bottomMargin: 0
                implicitHeight: incomingColumn.implicitHeight + 16
                radius: 5
                readonly property color tone: !root.last ? Theme.accentColor
                                             : root.last.status === "logged" ? Theme.accentColor
                                             : root.last.status === "duplicate" ? Theme.warningColor : Theme.errorColor
                color: root.last && root.last.status === "logged" ? Theme.rowMatchBg : Theme.glassOverlay
                border.width: 1
                border.color: tone

                ColumnLayout {
                    id: incomingColumn
                    anchors.fill: parent
                    anchors.margins: 8
                    anchors.leftMargin: 10
                    spacing: 4
                    RowLayout {
                        spacing: 8
                        Led { color: incomingCard.tone; glow: true }
                        Text {
                            text: root.last ? qsTr("INCOMING · %1").arg(root.last.message) : ""
                            color: incomingCard.tone
                            font.family: Theme.monoFamily
                            font.pixelSize: 11
                            font.bold: true
                        }
                    }
                    Text {
                        text: root.last ? root.last.call + "  <font color=\"" + Theme.textSecondary + "\" size=\"2\">"
                                          + root.last.grid + "</font>" : ""
                        textFormat: Text.StyledText
                        color: Theme.textPrimary
                        font.family: Theme.monoFamily
                        font.pixelSize: 16
                        font.bold: true
                    }
                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: {
                            if (!root.last)
                                return ""
                            const state = root.last.status === "logged" ? qsTr("saved ✓")
                                        : root.last.status === "duplicate" ? qsTr("duplicate") : qsTr("not saved")
                            return [root.last.freq, root.last.mode].filter(s => s).join(" ")
                                   + " · " + root.last.rstSent + " / " + root.last.rstRcvd
                                   + " · " + root.last.time + "Z · " + state
                        }
                        color: Theme.textSecondary
                        font.family: Theme.monoFamily
                        font.pixelSize: 11
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: decolog.lookupCall = root.last.call
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: 10
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    LabeledField {
                        Layout.preferredWidth: 1
                        Layout.horizontalStretchFactor: 6
                        Layout.fillWidth: true
                        label: qsTr("Callsign")
                        StyledTextField {
                            id: callField
                            Layout.fillWidth: true
                            fieldHeight: 36
                            uppercase: true
                            font.pixelSize: 18
                            font.bold: true
                            font.letterSpacing: 1
                            onTextChanged: decolog.lookupCall = text
                            // L'ora si prende quando si comincia a scrivere il nominativo,
                            // non quando la finestra e' stata aperta.
                            onActiveFocusChanged: if (activeFocus && text.length === 0) root.resetTime()
                            Keys.onReturnPressed: root.submit()
                            Keys.onEnterPressed: root.submit()
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    LabeledField {
                        Layout.preferredWidth: 1
                        Layout.horizontalStretchFactor: 3
                        Layout.fillWidth: true
                        label: qsTr("Date UTC")
                        StyledTextField { id: dateField; Layout.fillWidth: true; placeholderText: decolog.dateHint }
                    }
                    LabeledField {
                        Layout.preferredWidth: 1
                        Layout.horizontalStretchFactor: 3
                        Layout.fillWidth: true
                        label: qsTr("Time on")
                        StyledTextField {
                            id: timeField
                            Layout.fillWidth: true
                            rightPadding: nowButton.width + 10
                            Text {
                                id: nowButton
                                anchors.right: parent.right
                                anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("NOW")
                                color: Theme.secondaryColor
                                font.family: Theme.monoFamily
                                font.pixelSize: 10
                                font.bold: true
                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -4
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.resetTime()
                                }
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    LabeledField {
                        Layout.preferredWidth: 1
                        Layout.horizontalStretchFactor: 2
                        Layout.fillWidth: true
                        label: qsTr("Band")
                        StyledComboBox {
                            id: bandBox
                            Layout.fillWidth: true
                            model: ["—"].concat(decolog.bands)
                            onActivated: root.qsyTo(currentText, modeBox.editText, true)
                        }
                    }
                    LabeledField {
                        Layout.preferredWidth: 1
                        Layout.horizontalStretchFactor: 2
                        Layout.fillWidth: true
                        label: qsTr("Mode")
                        StyledComboBox {
                            id: modeBox
                            Layout.fillWidth: true
                            editable: true
                            model: ["SSB", "CW", "FM", "AM", "RTTY", "JTTY", "FT8", "FT4", "FT2", "PSK31"]
                            onActivated: root.qsyTo(bandBox.currentText, currentText, false)
                        }
                    }
                    LabeledField {
                        Layout.preferredWidth: 1
                        Layout.horizontalStretchFactor: 2
                        Layout.fillWidth: true
                        label: qsTr("Freq")
                        StyledTextField {
                            id: freqField
                            Layout.fillWidth: true
                            text: decolog.shownFrequency
                            onTextChanged: {
                                const i = bandBox.find(decolog.bandForFrequency(text))
                                if (i >= 0) bandBox.currentIndex = i
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    LabeledField {
                        Layout.preferredWidth: 1
                        Layout.horizontalStretchFactor: 2
                        Layout.fillWidth: true
                        label: qsTr("RST S")
                        StyledTextField { id: sentField; Layout.fillWidth: true; text: "59" }
                    }
                    LabeledField {
                        Layout.preferredWidth: 1
                        Layout.horizontalStretchFactor: 2
                        Layout.fillWidth: true
                        label: qsTr("RST R")
                        StyledTextField { id: rcvdField; Layout.fillWidth: true; text: "59" }
                    }
                    LabeledField {
                        Layout.preferredWidth: 1
                        Layout.horizontalStretchFactor: 2
                        Layout.fillWidth: true
                        label: qsTr("Grid")
                        StyledTextField { id: gridField; Layout.fillWidth: true; uppercase: true }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    LabeledField {
                        Layout.preferredWidth: 1
                        Layout.horizontalStretchFactor: 3
                        Layout.fillWidth: true
                        label: qsTr("Name")
                        StyledTextField { id: nameField; Layout.fillWidth: true; mono: false }
                    }
                    LabeledField {
                        Layout.preferredWidth: 1
                        Layout.horizontalStretchFactor: 3
                        Layout.fillWidth: true
                        label: qsTr("QTH")
                        StyledTextField { id: qthField; Layout.fillWidth: true; mono: false }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    LabeledField {
                        Layout.preferredWidth: 1
                        Layout.horizontalStretchFactor: 2
                        Layout.fillWidth: true
                        label: qsTr("Prop")
                        StyledComboBox {
                            id: propModeBox
                            Layout.fillWidth: true
                            editable: true
                            model: ["", "SAT", "AUR", "AUE", "BS", "ECH", "EME", "ES", "F2", "FAI", "GWAVE", "ION", "IRL", "MS", "RPT", "RS", "TEP", "TR"]
                        }
                    }
                    LabeledField {
                        Layout.preferredWidth: 1
                        Layout.horizontalStretchFactor: 2
                        Layout.fillWidth: true
                        label: qsTr("Sat")
                        StyledTextField { id: satNameField; Layout.fillWidth: true; uppercase: true; placeholderText: "—" }
                    }
                    LabeledField {
                        Layout.preferredWidth: 1
                        Layout.horizontalStretchFactor: 2
                        Layout.fillWidth: true
                        label: qsTr("Sat mode")
                        StyledTextField { id: satModeField; Layout.fillWidth: true; uppercase: true; placeholderText: "—" }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    LabeledField {
                        // Durante un contest il numero che arriva e' il campo piu'
                        // battuto: sta prima del commento.
                        visible: decolog.activation.state.serialEnabled === true
                        label: qsTr("Nr rcvd")
                        StyledTextField {
                            id: srxField
                            Layout.preferredWidth: 70
                            validator: IntValidator { bottom: 0; top: 99999 }
                            Keys.onReturnPressed: root.submit()
                            Keys.onEnterPressed: root.submit()
                        }
                    }
                    LabeledField {
                        visible: decolog.activation.state.serialEnabled === true
                        label: qsTr("Nr sent")
                        StyledTextField {
                            Layout.preferredWidth: 70
                            readOnly: true
                            text: decolog.activation.nextSerial
                        }
                    }
                    LabeledField {
                        Layout.preferredWidth: 1
                        Layout.horizontalStretchFactor: 6
                        Layout.fillWidth: true
                        label: qsTr("Comment")
                        StyledTextField {
                            id: commentField
                            Layout.fillWidth: true
                            mono: false
                            Keys.onReturnPressed: root.submit()
                            Keys.onEnterPressed: root.submit()
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        Layout.preferredWidth: 1
                        id: formError
                        Layout.horizontalStretchFactor: 6
                        Layout.fillWidth: true
                        visible: text.length > 0
                        color: Theme.errorColor
                        wrapMode: Text.Wrap
                        font.pixelSize: 11
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    RowLayout {
                        Layout.preferredWidth: 1
                        Layout.horizontalStretchFactor: 6
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        spacing: 8
                        GlassButton {
                            Layout.fillWidth: true
                            text: "✎ " + qsTr("LOG QSO")
                            tone: Theme.accentColor
                            filled: true
                            buttonHeight: 34
                            fontPixelSize: 13
                            enabled: callField.text.length > 2
                            onClicked: root.submit()
                        }
                        GlassButton {
                            text: qsTr("CLEAR")
                            tone: Theme.warningColor
                            buttonHeight: 34
                            onClicked: root.clearForm()
                        }
                    }
                }
            }
        }
    }
}
