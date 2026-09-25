// DecoDXLog — Nuovo QSO, scheda completa (mockup 1b). Per SSB e CW: i digitali
// arrivano da Decodium. Sotto il nominativo, quello che il log sa gia' di lui.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decodium.UI

DialogFrame {
    id: root

    readonly property var lookup: decolog.callInfo
    readonly property bool matchesCall: (lookup.call || "") === callField.text.trim().toUpperCase()
                                        && callField.text.trim().length > 0

    title: qsTr("New QSO")
    dotColor: Theme.accentColor
    info: decolog.stationProfiles.activeProfile.name
          ? qsTr("Station: %1").arg(decolog.stationProfiles.activeProfile.name) : qsTr("No station profile")
    dialogKey: "newqso"
    width: 760
    height: 660

    function submodesFor(mode) {
        switch (mode) {
        case "SSB": return ["", "USB", "LSB"]
        case "MFSK": return ["", "FT2", "FT4", "JTTY", "FST4", "Q65", "JS8"]
        case "PSK": return ["", "PSK31", "PSK63", "PSK125"]
        case "RTTY": return ["", "ASCI"]
        default: return [""]
        }
    }

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

    // Il modo per la radio: il sottomodo se c'e' (FT8, FT4…), se no il modo.
    function tuneMode() {
        const sub = submodeBox.editText.trim()
        return sub.length > 0 && modeBox.editText.toUpperCase() !== "SSB" ? sub : modeBox.editText
    }

    function resetTime() {
        const now = decolog.utcNow()
        dateField.text = decolog.showDate(now.date)
        timeField.text = now.time
    }

    function clearAll() {
        for (const f of [callField, gridField, nameField, qthField, commentField, potaField, sotaField,
                         iotaField, wwffField, dciField, tagsField, countryField, addressField, stateField,
                         cntyField, contField, cqzField, ituzField, dxccField, qslViaField,
                         satNameField, satModeField])
            f.text = ""
        propModeBox.currentIndex = 0
        sentField.text = "59"
        rcvdField.text = "59"
        errorText.text = ""
        resetTime()
        callField.forceActiveFocus()
    }

    // `keep`: resta aperta con banda, modo, potenza e referenze, per il QSO dopo.
    // Per le prove: apre la tendina delle bande.
    function showBandCombo() { bandBox.popup.open() }

    function submit(keep) {
        const error = decolog.logManualQso({
            call: callField.text, date: dateField.text, time: timeField.text,
            band: bandBox.currentIndex > 0 ? bandBox.currentText : "", freq: freqField.text,
            mode: modeBox.editText, submode: submodeBox.editText,
            rst_sent: sentField.text, rst_rcvd: rcvdField.text,
            gridsquare: gridField.text, name: nameField.text, qth: qthField.text, tx_pwr: pwrField.text,
            pota_ref: potaField.text, sota_ref: sotaField.text, iota: iotaField.text, wwff_ref: wwffField.text,
            dci: dciField.text,
            prop_mode: propModeBox.editText, sat_name: satNameField.text, sat_mode: satModeField.text,
            comment: commentField.text, tags: tagsField.text,
            country: countryField.text, address: addressField.text, state: stateField.text,
            cnty: cntyField.text, cont: contField.text, cqz: cqzField.text, ituz: ituzField.text,
            dxcc: dxccField.text, qsl_via: qslViaField.text
        })
        errorText.text = error
        if (error.length > 0)
            return
        if (keep) {
            for (const f of [callField, gridField, nameField, qthField, commentField, countryField,
                             addressField, stateField, cntyField, contField, cqzField, ituzField,
                             dxccField, qslViaField])
                f.text = ""
            resetTime()
            callField.forceActiveFocus()
        } else {
            root.accept()
        }
    }

    onOpened: {
        clearAll()
        if (decolog.shownFrequency.length)
            freqField.text = decolog.shownFrequency
        const pwr = decolog.stationProfiles.activeProfile.defaultTxPwr
        pwrField.text = pwr > 0 ? String(pwr) : ""
    }

    // Nome, QTH e locatore dal callbook, solo nei campi ancora vuoti: quello che
    // l'operatore ha scritto non si tocca.
    Connections {
        target: decolog
        function onLookupChanged() {
            const cb = decolog.callInfo.callbook
            if (!root.visible || !decolog.callbookAutofill || !cb || !root.matchesCall)
                return
            if (nameField.text.length === 0) nameField.text = cb.name
            if (qthField.text.length === 0) qthField.text = cb.qth
            if (gridField.text.length === 0) gridField.text = cb.grid
            if (iotaField.text.length === 0 && cb.iota) iotaField.text = cb.iota
            if (countryField.text.length === 0 && cb.country) countryField.text = cb.country
            if (addressField.text.length === 0 && cb.address) addressField.text = cb.address
            if (stateField.text.length === 0 && cb.state) stateField.text = cb.state
            if (cntyField.text.length === 0 && cb.county) cntyField.text = cb.county
            if (cqzField.text.length === 0 && cb.cqZone > 0) cqzField.text = String(cb.cqZone)
            if (ituzField.text.length === 0 && cb.ituZone > 0) ituzField.text = String(cb.ituZone)
            if (dxccField.text.length === 0 && cb.dxcc > 0) dxccField.text = String(cb.dxcc)
            if (qslViaField.text.length === 0 && cb.qslVia) qslViaField.text = cb.qslVia
        }
    }

    Shortcut {
        sequences: ["Ctrl+Return", "Ctrl+Enter"]
        enabled: root.visible
        onActivated: root.submit(false)
    }

    body: ColumnLayout {
        spacing: 12

        GridLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            Layout.bottomMargin: 0
            columns: 3
            columnSpacing: 10
            LabeledField {
                Layout.fillWidth: true
                label: qsTr("Callsign")
                StyledTextField {
                    id: callField
                    Layout.fillWidth: true
                    fieldHeight: 44
                    uppercase: true
                    font.pixelSize: 24
                    font.bold: true
                    font.letterSpacing: 2
                    onTextChanged: decolog.lookupCall = text
                    Keys.onReturnPressed: root.submit(false)
                    Keys.onEnterPressed: root.submit(false)
                }
            }
            LabeledField {
                Layout.preferredWidth: 150
                Layout.fillWidth: false
                label: qsTr("Date UTC")
                StyledTextField { id: dateField; Layout.fillWidth: true; fieldHeight: 44; font.pixelSize: 15; placeholderText: decolog.dateHint }
            }
            LabeledField {
                Layout.preferredWidth: 120
                Layout.fillWidth: false
                label: qsTr("Time on")
                StyledTextField {
                    id: timeField
                    Layout.fillWidth: true
                    fieldHeight: 44
                    font.pixelSize: 15
                    rightPadding: nowPill.width + 14
                    Pill {
                        id: nowPill
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("NOW")
                        tone: Theme.secondaryColor
                        rounded: false
                        pillHeight: 20
                        fontPixelSize: 10
                        interactive: true
                        onClicked: root.resetTime()
                    }
                }
            }
        }

        // Quello che il log sa del nominativo.
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            implicitHeight: 34
            radius: 4
            color: Theme.bgMedium
            border.width: 1
            border.color: Theme.borderSoft
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 10
                Text {
                    text: root.lookup.callbook && root.matchesCall
                          ? (root.lookup.callbook.source === "QRZ.com" ? "QRZ" : "HamQTH") : qsTr("LOG")
                    color: Theme.secondaryColor
                    font.family: Theme.monoFamily
                    font.pixelSize: 12
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: {
                        if (!root.matchesCall)
                            return qsTr("Type a callsign to see what the log knows")
                        const parts = [root.lookup.name, root.lookup.qth, root.lookup.gridsquare, root.lookup.country]
                                      .filter(s => s)
                        if (root.lookup.entityDxcc) parts.push("DXCC " + root.lookup.entityDxcc)
                        if (root.lookup.cqz) parts.push("CQ " + root.lookup.cqz)
                        return parts.length ? parts.join(" · ") : qsTr("unknown prefix")
                    }
                    color: root.matchesCall ? Theme.textPrimary : Theme.textSecondary
                    font.family: Theme.monoFamily
                    font.pixelSize: 12
                }
                // Come nel mockup: prima l'entita' (nuovo DXCC, nuovo DXCC sulla
                // banda), poi il nominativo.
                Pill {
                    readonly property string band: bandBox.currentIndex > 0 ? bandBox.currentText : ""
                    readonly property bool hasEntity: root.lookup.entityDxcc !== undefined
                    readonly property bool newDxcc: hasEntity && root.lookup.entityWorked === 0
                    readonly property bool newDxccOnBand: hasEntity && !newDxcc && band.length > 0
                                                          && (root.lookup.entityBands || []).indexOf(band) < 0
                    readonly property bool newCallOnBand: !hasEntity && band.length > 0 && (root.lookup.count || 0) > 0
                                                          && (root.lookup.bands || []).indexOf(band) < 0
                    visible: root.matchesCall && (newDxcc || newDxccOnBand || newCallOnBand)
                    text: newDxcc ? qsTr("NEW DXCC") : newDxccOnBand ? qsTr("NEW DXCC on %1").arg(band) : qsTr("NEW on %1").arg(band)
                    tone: Theme.warningColor
                    pillHeight: 20
                    fontPixelSize: 10
                }
                Pill {
                    visible: root.matchesCall
                    text: (root.lookup.count || 0) > 0 ? qsTr("worked %1×").arg(root.lookup.count) : qsTr("new station")
                    tone: (root.lookup.count || 0) > 0 ? Theme.textSecondary : Theme.accentColor
                    pillHeight: 20
                    fontPixelSize: 10
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            spacing: 12
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                LabeledField {
                    Layout.preferredWidth: 1
                    Layout.horizontalStretchFactor: 2; Layout.fillWidth: true
                    label: qsTr("Band")
                    StyledComboBox {
                        id: bandBox
                        Layout.fillWidth: true
                        model: ["—"].concat(decolog.bands)
                        onActivated: root.qsyTo(currentText, root.tuneMode(), true)
                    }
                }
                LabeledField {
                    Layout.preferredWidth: 1
                    Layout.horizontalStretchFactor: 2; Layout.fillWidth: true
                    label: qsTr("Freq MHz")
                    StyledTextField {
                        id: freqField
                        Layout.fillWidth: true
                        onTextChanged: {
                            const i = bandBox.find(decolog.bandForFrequency(text))
                            if (i >= 0) bandBox.currentIndex = i
                        }
                    }
                }
                LabeledField {
                    Layout.preferredWidth: 1
                    Layout.horizontalStretchFactor: 2; Layout.fillWidth: true
                    label: qsTr("Mode")
                    StyledComboBox {
                        id: modeBox
                        Layout.fillWidth: true
                        editable: true
                        model: ["SSB", "CW", "FM", "AM", "RTTY", "MFSK", "FT8", "PSK"]
                        onActivated: root.qsyTo(bandBox.currentText, root.tuneMode(), false)
                    }
                }
                LabeledField {
                    Layout.preferredWidth: 1
                    Layout.horizontalStretchFactor: 2; Layout.fillWidth: true
                    label: qsTr("Submode")
                    StyledComboBox {
                        id: submodeBox
                        Layout.fillWidth: true
                        editable: true
                        model: root.submodesFor(modeBox.editText.toUpperCase())
                        onActivated: root.qsyTo(bandBox.currentText, root.tuneMode(), false)
                    }
                }
                LabeledField {
                    Layout.preferredWidth: 1
                    Layout.horizontalStretchFactor: 2; Layout.fillWidth: true
                    label: qsTr("RST sent")
                    StyledTextField { id: sentField; Layout.fillWidth: true; text: "59" }
                }
                LabeledField {
                    Layout.preferredWidth: 1
                    Layout.horizontalStretchFactor: 2; Layout.fillWidth: true
                    label: qsTr("RST rcvd")
                    StyledTextField { id: rcvdField; Layout.fillWidth: true; text: "59" }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                LabeledField {
                    Layout.preferredWidth: 1
                    Layout.horizontalStretchFactor: 2; Layout.fillWidth: true
                    label: qsTr("Grid")
                    StyledTextField { id: gridField; Layout.fillWidth: true; uppercase: true }
                }
                LabeledField {
                    Layout.preferredWidth: 1
                    Layout.horizontalStretchFactor: 4; Layout.fillWidth: true
                    label: qsTr("Name")
                    StyledTextField { id: nameField; Layout.fillWidth: true; mono: false }
                }
                LabeledField {
                    Layout.preferredWidth: 1
                    Layout.horizontalStretchFactor: 4; Layout.fillWidth: true
                    label: qsTr("QTH")
                    StyledTextField { id: qthField; Layout.fillWidth: true; mono: false }
                }
                LabeledField {
                    Layout.preferredWidth: 1
                    Layout.horizontalStretchFactor: 2; Layout.fillWidth: true
                    label: qsTr("TX pwr W")
                    StyledTextField { id: pwrField; Layout.fillWidth: true }
                }
            }
            //  Dove sta la stazione: quello che il callbook sa e che prima
            //  si poteva scrivere solo dopo, riaprendo il QSO.
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                LabeledField {
                    Layout.preferredWidth: 1
                    Layout.horizontalStretchFactor: 5; Layout.fillWidth: true
                    label: qsTr("Country")
                    StyledTextField { id: countryField; Layout.fillWidth: true; mono: false }
                }
                LabeledField {
                    Layout.preferredWidth: 1
                    Layout.horizontalStretchFactor: 6; Layout.fillWidth: true
                    label: qsTr("Address / city")
                    StyledTextField { id: addressField; Layout.fillWidth: true; mono: false }
                }
                LabeledField {
                    Layout.preferredWidth: 1
                    Layout.horizontalStretchFactor: 2; Layout.fillWidth: true
                    label: qsTr("State")
                    StyledTextField { id: stateField; Layout.fillWidth: true; uppercase: true; placeholderText: "—" }
                }
                LabeledField {
                    Layout.preferredWidth: 1
                    Layout.horizontalStretchFactor: 3; Layout.fillWidth: true
                    label: qsTr("County / JCC")
                    StyledTextField { id: cntyField; Layout.fillWidth: true; mono: false; placeholderText: "—" }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                LabeledField {
                    Layout.preferredWidth: 1
                    Layout.horizontalStretchFactor: 2; Layout.fillWidth: true
                    label: qsTr("DXCC")
                    StyledTextField { id: dxccField; Layout.fillWidth: true; placeholderText: "—" }
                }
                LabeledField {
                    Layout.preferredWidth: 1
                    Layout.horizontalStretchFactor: 2; Layout.fillWidth: true
                    label: qsTr("CQ zone")
                    StyledTextField { id: cqzField; Layout.fillWidth: true; placeholderText: "—" }
                }
                LabeledField {
                    Layout.preferredWidth: 1
                    Layout.horizontalStretchFactor: 2; Layout.fillWidth: true
                    label: qsTr("ITU zone")
                    StyledTextField { id: ituzField; Layout.fillWidth: true; placeholderText: "—" }
                }
                LabeledField {
                    Layout.preferredWidth: 1
                    Layout.horizontalStretchFactor: 2; Layout.fillWidth: true
                    label: qsTr("Cont")
                    StyledTextField { id: contField; Layout.fillWidth: true; uppercase: true; placeholderText: "—" }
                }
                LabeledField {
                    Layout.preferredWidth: 1
                    Layout.horizontalStretchFactor: 5; Layout.fillWidth: true
                    label: qsTr("QSL via")
                    StyledTextField { id: qslViaField; Layout.fillWidth: true; uppercase: true; placeholderText: "—" }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                LabeledField {
                    Layout.preferredWidth: 1
                    Layout.horizontalStretchFactor: 3; Layout.fillWidth: true
                    label: "POTA"
                    StyledTextField { id: potaField; Layout.fillWidth: true; uppercase: true; placeholderText: "—" }
                }
                LabeledField {
                    Layout.preferredWidth: 1
                    Layout.horizontalStretchFactor: 3; Layout.fillWidth: true
                    label: "SOTA"
                    StyledTextField { id: sotaField; Layout.fillWidth: true; uppercase: true; placeholderText: "—" }
                }
                LabeledField {
                    Layout.preferredWidth: 1
                    Layout.horizontalStretchFactor: 3; Layout.fillWidth: true
                    label: "IOTA"
                    StyledTextField { id: iotaField; Layout.fillWidth: true; uppercase: true; placeholderText: "—" }
                }
                LabeledField {
                    Layout.preferredWidth: 1
                    Layout.horizontalStretchFactor: 3; Layout.fillWidth: true
                    label: "WWFF"
                    StyledTextField { id: wwffField; Layout.fillWidth: true; uppercase: true; placeholderText: "—" }
                }
                LabeledField {
                    Layout.preferredWidth: 1
                    Layout.horizontalStretchFactor: 3; Layout.fillWidth: true
                    label: "DCI"
                    // Il castello: due lettere di provincia e tre cifre, "NA015".
                    StyledTextField { id: dciField; Layout.fillWidth: true; uppercase: true; placeholderText: "NA015" }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                LabeledField {
                    Layout.preferredWidth: 1
                    Layout.horizontalStretchFactor: 4; Layout.fillWidth: true
                    label: qsTr("Prop mode")
                    StyledComboBox {
                        id: propModeBox
                        Layout.fillWidth: true
                        editable: true
                        model: ["", "SAT", "AUR", "AUE", "BS", "ECH", "EME", "ES", "F2", "FAI", "GWAVE", "ION", "IRL", "MS", "RPT", "RS", "TEP", "TR"]
                    }
                }
                LabeledField {
                    Layout.preferredWidth: 1
                    Layout.horizontalStretchFactor: 4; Layout.fillWidth: true
                    label: qsTr("Satellite")
                    StyledTextField { id: satNameField; Layout.fillWidth: true; uppercase: true; placeholderText: "OSCAR-100" }
                }
                LabeledField {
                    Layout.preferredWidth: 1
                    Layout.horizontalStretchFactor: 4; Layout.fillWidth: true
                    label: qsTr("Sat mode")
                    StyledTextField { id: satModeField; Layout.fillWidth: true; uppercase: true; placeholderText: "U/V" }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                LabeledField {
                    Layout.preferredWidth: 1
                    Layout.horizontalStretchFactor: 8; Layout.fillWidth: true
                    label: qsTr("Comment")
                    StyledTextField { id: commentField; Layout.fillWidth: true; mono: false }
                }
                LabeledField {
                    Layout.preferredWidth: 1
                    Layout.horizontalStretchFactor: 4; Layout.fillWidth: true
                    label: qsTr("Tags")
                    // Restano per il QSO dopo, come le referenze: un'attivazione e' fatta di tanti QSO.
                    StyledTextField { id: tagsField; Layout.fillWidth: true; mono: false; placeholderText: qsTr("pota, portable") }
                }
            }
        }

        Text {
            id: errorText
            Layout.fillWidth: true
            Layout.leftMargin: 14
            visible: text.length > 0
            color: Theme.errorColor
            font.pixelSize: 12
        }

        Rectangle { Layout.fillWidth: true; Layout.leftMargin: 14; Layout.rightMargin: 14; implicitHeight: 1; color: Theme.borderSoft }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            Layout.topMargin: 0
            spacing: 10
            Text {
                text: "source=manual · dirty=1 · uuid client-side"
                color: Theme.textSecondary
                font.family: Theme.monoFamily
                font.pixelSize: 11
            }
            Item { Layout.fillWidth: true }
            GlassButton { text: qsTr("CLEAR"); tone: Theme.warningColor; buttonHeight: 34; onClicked: root.clearAll() }
            GlassButton {
                text: qsTr("Log & keep")
                buttonHeight: 34
                enabled: callField.text.trim().length > 2
                onClicked: root.submit(true)
            }
            GlassButton {
                text: "✎ " + qsTr("LOG QSO")
                hint: "Ctrl+Enter"
                tone: Theme.accentColor
                filled: true
                buttonHeight: 34
                fontPixelSize: 13
                enabled: callField.text.trim().length > 2
                onClicked: root.submit(false)
            }
        }
    }
}
