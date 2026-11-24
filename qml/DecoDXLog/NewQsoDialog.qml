// DecoDXLog — Nuovo QSO, scheda completa (mockup 1b). Per SSB e CW: i digitali
// arrivano da Decodium. Sotto il nominativo, quello che il log sa gia' di lui.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decodium.UI

DialogFrame {
    id: root

    // 0 = profilo attivo, >0 = finestra dedicata a quel profilo.
    property int profileId: 0
    property string profileDialogKey: ""
    property double radioNetSessionId: 0
    property double radioNetMemberId: 0
    property string radioNetSessionUuid: ""
    property var radioNetLogContext: ({})
    property bool historicalMode: false
    property string historicalLastDate: ""
    property int historicalLastSeconds: -1
    // Optional explicit handoff; applied only when opening a fresh dialog.
    property string initialCall: ""
    property string initialName: ""
    property string initialSatellite: ""
    property string initialTxMHz: ""
    property string initialRxMHz: ""
    property string initialTrackerReceivedAt: ""
    property string trackerReceivedAt: ""
    property string trackerSeedSatellite: ""
    property string trackerSeedTx: ""
    property string trackerSeedRx: ""
    property bool trackerPrepared: false
    readonly property string enteredCallForProbe: callField.text
    readonly property string initialTxForProbe: freqField.text
    readonly property string initialRxForProbe: rxFreqField.text
    readonly property string initialSatelliteForProbe: satNameField.text
    readonly property var stationProfile: profileId > 0
                                           ? decolog.stationProfiles.byId(profileId)
                                           : decolog.stationProfiles.activeProfile

    readonly property var lookup: decolog.callInfo
    readonly property bool matchesCall: (lookup.call || "") === callField.text.trim().toUpperCase()
                                        && callField.text.trim().length > 0

    title: root.historicalMode ? qsTr("Historical QSO entry") : qsTr("New QSO")
    dotColor: Theme.accentColor
    info: root.stationProfile.name
          ? qsTr("Station: %1").arg(root.stationProfile.name) : qsTr("No station profile")
    dialogKey: root.profileDialogKey.length > 0 ? root.profileDialogKey : "newqso"
    width: 760
    height: root.historicalMode ? 820 : 660

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
        if (root.historicalMode)
            return
        if (root.trackerPrepared) {
            if (bandChanged) {
                const currentBand = decolog.bandForFrequency(freqField.text)
                const index = bandBox.find(currentBand)
                bandBox.currentIndex = index >= 0 ? index : 0
            }
            return
        }
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
        if (!root.trackerPrepared && (decolog.rig.connected || decolog.decoLinkClients.length > 0)) {
            root.lastQsy = Date.now()
            decolog.tuneTo(mhz, m)
        }
    }

    // Il modo per la radio: il sottomodo se c'e' (FT8, FT4…), se no il modo.
    function tuneMode() {
        const sub = submodeBox.editText.trim()
        return sub.length > 0 && modeBox.editText.toUpperCase() !== "SSB" ? sub : modeBox.editText
    }

    // Il rapporto di partenza secondo il modo: 59 in fonia, 599 in CW, RTTY e
    // PSK, i dB nei modi di WSJT (FT8, FT4, FT2…), dove 59 non vuol dire niente.
    // Si cambia da solo solo se nel campo c'e' ancora un valore di partenza:
    // quello scritto dall'operatore non si tocca.
    readonly property var defaultReports: ["", "59", "599", "-10"]
    function defaultReport(mode) {
        const m = String(mode || "").toUpperCase()
        if (["FT8", "FT4", "FT2", "JT65", "JT9", "JT4", "Q65", "MSK144", "FST4", "FST4W", "JS8", "WSPR"].indexOf(m) >= 0)
            return "-10"
        if (["SSB", "USB", "LSB", "AM", "FM", "DIGITALVOICE", "C4FM", "DSTAR", "DMR"].indexOf(m) >= 0)
            return "59"
        return "599"
    }
    function applyDefaultReports() {
        if (root.historicalMode)
            return
        const want = root.defaultReport(root.tuneMode())
        if (root.defaultReports.indexOf(sentField.text.trim()) >= 0)
            sentField.text = want
        if (root.defaultReports.indexOf(rcvdField.text.trim()) >= 0)
            rcvdField.text = want
    }

    function resetTime() {
        const now = decolog.utcNow()
        dateField.text = decolog.showDate(now.date)
        timeField.text = now.time
    }

    function clearAll() {
        for (const f of [callField, gridField, myGridField, nameField, qthField, commentField, potaField, sotaField,
                         iotaField, wwffField, dciField, tagsField, countryField, addressField, stateField,
                         cntyField, contField, cqzField, ituzField, dxccField, qslViaField,
                         satNameField, satModeField, rxFreqField])
            f.text = ""
        propModeBox.currentIndex = 0
        sentField.text = root.historicalMode ? "" : root.defaultReport(root.tuneMode())
        rcvdField.text = root.historicalMode ? "" : root.defaultReport(root.tuneMode())
        errorText.text = ""
        if (root.historicalMode) { dateField.text = ""; timeField.text = ""; historicalEarlier.checked = false }
        else resetTime()
        callField.forceActiveFocus()
    }

    function submitOnEnter(event) {
        if (decolog.enterToLogQso && !event.isAutoRepeat && (event.modifiers & ~Qt.KeypadModifier) === 0)
            root.submit(false)
    }

    // `keep`: resta aperta con banda, modo, potenza e referenze, per il QSO dopo.
    // Per le prove: apre la tendina delle bande.
    function showBandCombo() { bandBox.popup.open() }

    // Isolated --show probe only: exercise the actual manual submission path.
    function probeRadioNetSave() {
        const original = callField.text
        callField.text = ""
        root.submit(false)
        console.warn("PROBE radioNetInvalid kept=" + (root.visible && errorText.text.length > 0))
        callField.text = "K1BBB"
        root.submit(false)
        console.warn("PROBE radioNetMismatch kept=" + (root.visible && errorText.text === qsTranslate("decolog::app::DecoLogController", "The radio net session or participant no longer matches this QSO.")))
        callField.text = original
        freqField.text = "14.050000"
        modeBox.currentIndex = modeBox.find("CW")
        root.submit(false)
        console.warn("PROBE radioNetSaved closed=" + !root.visible + " error=" + errorText.text)
    }

    function probeRadioNetRepeat() {
        freqField.text = "14.050000"
        modeBox.currentIndex = modeBox.find("CW")
        root.submit(false)
        console.warn("PROBE radioNetRepeat closed=" + !root.visible + " error=" + errorText.text)
    }
    function probeRadioNetStaleDraft() {
        freqField.text="14.050000"; modeBox.currentIndex=modeBox.find("CW"); root.submit(false)
        console.warn("PROBE radioNetStaleDraft result " + JSON.stringify({visible:root.visible,error:errorText.text}))
    }

    function probeRadioNetClosed() {
        freqField.text = "14.050000"
        modeBox.currentIndex = modeBox.find("CW")
        root.submit(false)
        console.warn("PROBE radioNetClosed kept=" + (root.visible && errorText.text === qsTranslate("decolog::app::DecoLogController", "The radio net session or participant no longer matches this QSO.")))
    }

    function probeSatelliteSplit() {
        callField.text = "K1AAA"
        freqField.text = "145.900000"
        rxFreqField.text = "invalid"
        modeBox.currentIndex = modeBox.find("CW")
        propModeBox.currentIndex = propModeBox.find("SAT")
        satNameField.text = "FO-29"
        satModeField.text = "U/V"
        root.submit(false)
        console.warn("PROBE satelliteInvalid kept=" + (root.visible && errorText.text.length > 0))
        rxFreqField.text = "435.250000"
        root.submit(false)
        console.warn("PROBE satelliteSaved closed=" + !root.visible + " error=" + errorText.text)
    }

    function probeRigSplit() {
        callField.text = "K1AAA"
        freqField.text = "435.250000"
        rxFreqField.text = ""
        modeBox.currentIndex = modeBox.find("CW")
        propModeBox.currentIndex = propModeBox.find("SAT")
        satNameField.text = "FO-29"
        root.submit(false)
        console.warn("PROBE rigSplitSaved=" + !root.visible + " error=" + errorText.text)
    }
    function probeTrackerSelections() {
        modeBox.currentIndex = modeBox.find("CW")
        modeBox.activated(modeBox.currentIndex)
        root.qsyTo("20m", "CW", true)
        console.warn("PROBE trackerRig selections=true tracker=" + root.trackerPrepared + " connected=" + decolog.rig.connected
                     + " tx=" + freqField.text + " rx=" + rxFreqField.text)
    }
    function probeTrackerDraftSave(mode) {
        callField.text = ""
        root.submit(false)
        console.warn("PROBE predictDraftInvalid kept=" + (root.visible && errorText.text.length > 0))
        callField.text = "K1AAA"
        modeBox.currentIndex = modeBox.find(mode || "CW")
        satModeField.text = "V/U"
        root.submit(false)
        console.warn("PROBE predictDraftSaved=" + !root.visible + " error=" + errorText.text)
    }
    function probeRoverLog(call, ownGrid) {
        callField.text = call
        freqField.text = "14.050000"
        modeBox.currentIndex = modeBox.find("CW")
        myGridField.text = ownGrid
        root.submit(false)
    }
    function probeHistoricalEntry() {
        const roverOk = decolog.rover.start("Historical guard fixture", "JN71DC", 0)
        const activationError = decolog.activation.start({kind: "free", name: "Historical guard fixture", myGrid: "JN50"})
        const missingTimeError = decolog.logManualQso({call: "K1AAA", date: decolog.showDate("2026-12-31"), time: "", band: "20m", mode: "CW", historical_entry: true})
        console.warn("PROBE historicalBackendMissing blocked=" + (missingTimeError.length > 0) + " count=" + decolog.currentLogInfo().qsos)
        dateField.text = decolog.showDate("2026-12-31")
        callField.text = "K1AAA"
        timeField.text = ""
        freqField.text = "14.050000"
        modeBox.currentIndex = modeBox.find("CW")
        root.submit(true)
        console.warn("PROBE historicalMissingTime count=" + decolog.currentLogInfo().qsos + " kept=" + root.visible)
        timeField.text = "23:58"
        root.submit(true)
        console.warn("PROBE historicalFirst count=" + decolog.currentLogInfo().qsos + " date=" + decolog.readDate(dateField.text) + " blankTime=" + (timeField.text === ""))
        callField.text = "K1BBB"; timeField.text = "00:05"
        root.submit(true)
        console.warn("PROBE historicalEarlier count=" + decolog.currentLogInfo().qsos + " blocked=" + (errorText.text.length > 0))
        root.advanceHistoricalDay()
        timeField.text = "00:05"
        root.submit(true)
        console.warn("PROBE historicalNext count=" + decolog.currentLogInfo().qsos + " date=" + decolog.readDate(dateField.text))
        callField.text = "K1CCC"; timeField.text = "00:03"; historicalEarlier.checked = true
        root.submit(true)
        console.warn("PROBE historicalConfirmed count=" + decolog.currentLogInfo().qsos + " rover=" + roverOk + " activationError=" + activationError)
        historicalProfile.checked = true
        callField.text = "K1DDD"; timeField.text = "00:04"
        root.submit(true)
        console.warn("PROBE historicalProfile count=" + decolog.currentLogInfo().qsos)
        decolog.activation.stop()
        decolog.rover.close()
    }

    function historicalSeconds(value) {
        const match = /^(\d{2}):(\d{2})(?::(\d{2}))?$|^(\d{2})(\d{2})$/.exec(String(value).trim())
        if (!match) return -1
        const hour = Number(match[1] || match[4]), minute = Number(match[2] || match[5]), second = Number(match[3] || 0)
        return hour < 24 && minute < 60 && second < 60 ? hour * 3600 + minute * 60 + second : -1
    }
    function advanceHistoricalDay() {
        const iso = decolog.readDate(dateField.text)
        if (!/^\d{4}-\d{2}-\d{2}$/.test(iso)) { errorText.text = qsTr("Enter a valid UTC date first."); return }
        const date = new Date(iso + "T12:00:00Z")
        if (isNaN(date.getTime())) { errorText.text = qsTr("Enter a valid UTC date first."); return }
        date.setUTCDate(date.getUTCDate() + 1)
        const next = date.toISOString().slice(0, 10)
        if (!/^\d{4}-\d{2}-\d{2}$/.test(next)) { errorText.text = qsTr("Cannot advance this UTC date."); return }
        dateField.text = decolog.showDate(next)
        timeField.text = ""
        historicalEarlier.checked = false
        errorText.text = ""
    }

    function submit(keep) {
        const iso = root.historicalMode ? decolog.readDate(dateField.text) : ""
        const seconds = root.historicalMode ? root.historicalSeconds(timeField.text) : -1
        if (root.historicalMode && (!iso.length || seconds < 0)) {
            errorText.text = qsTr("Enter an explicit valid UTC date and time.")
            return
        }
        if (root.historicalMode && iso === root.historicalLastDate && root.historicalLastSeconds >= 0 &&
                seconds < root.historicalLastSeconds && !historicalEarlier.checked) {
            errorText.text = qsTr("The time is earlier on the same date. Confirm it or advance the UTC date.")
            return
        }
        const error = decolog.logManualQso({
            call: callField.text, date: dateField.text, time: timeField.text,
            band: bandBox.currentIndex > 0 ? bandBox.currentText : "", freq: freqField.text, freq_rx: rxFreqField.text,
            mode: modeBox.editText, submode: submodeBox.editText,
            rst_sent: sentField.text, rst_rcvd: rcvdField.text,
            gridsquare: gridField.text, my_gridsquare: myGridField.text, name: nameField.text, qth: qthField.text, tx_pwr: pwrField.text,
            pota_ref: potaField.text, sota_ref: sotaField.text, iota: iotaField.text, wwff_ref: wwffField.text,
            dci: dciField.text,
            prop_mode: propModeBox.editText, sat_name: satNameField.text, sat_mode: satModeField.text,
            tracker_snapshot_utc: root.trackerPrepared && root.trackerReceivedAt.length > 0
                && root.trackerSeedTx.length > 0 && root.trackerSeedRx.length > 0
                && satNameField.text.trim().toUpperCase() === root.trackerSeedSatellite.trim().toUpperCase()
                && propModeBox.editText.trim().toUpperCase() === "SAT"
                && Number(freqField.text.replace(",", ".")) === Number(root.trackerSeedTx)
                && Number(rxFreqField.text.replace(",", ".")) === Number(root.trackerSeedRx) ? root.trackerReceivedAt : "",
            comment: commentField.text, tags: tagsField.text,
            country: countryField.text, address: addressField.text, state: stateField.text,
            cnty: cntyField.text, cont: contField.text, cqz: cqzField.text, ituz: ituzField.text,
            dxcc: dxccField.text, qsl_via: qslViaField.text,
            station_profile_id: root.profileId,
            radio_net_session_id: root.radioNetSessionId, radio_net_member_id: root.radioNetMemberId,
            radio_net_session_uuid: root.radioNetSessionUuid, radio_net_log_context: root.radioNetLogContext,
            historical_entry: root.historicalMode, historical_use_profile: root.historicalMode && historicalProfile.checked
        })
        errorText.text = error
        if (error.length > 0)
            return
        if (root.historicalMode) { root.historicalLastDate = iso; root.historicalLastSeconds = seconds; historicalEarlier.checked = false }
        if (keep) {
            for (const f of [callField, gridField, myGridField, nameField, qthField, commentField, countryField,
                             addressField, stateField, cntyField, contField, cqzField, ituzField,
                             dxccField, qslViaField])
                f.text = ""
            if (root.historicalMode) { timeField.text = ""; sentField.text = ""; rcvdField.text = "" }
            else resetTime()
            callField.forceActiveFocus()
        } else {
            root.accept()
        }
    }

    onOpened: {
        root.historicalLastDate = ""
        root.historicalLastSeconds = -1
        clearAll()
        root.trackerPrepared = root.initialSatellite.length > 0
        root.trackerReceivedAt = root.initialTrackerReceivedAt
        root.trackerSeedSatellite = root.initialSatellite
        root.trackerSeedTx = root.initialTxMHz
        root.trackerSeedRx = root.initialRxMHz
        if (root.initialCall.length > 0) callField.text = root.initialCall
        if (root.initialName.length > 0) nameField.text = root.initialName
        root.initialCall = ""
        root.initialName = ""
        if (!root.historicalMode && decolog.shownFrequency.length)
            freqField.text = decolog.shownFrequency
        if (root.historicalMode) {
            historicalProfile.checked = false
            bandBox.currentIndex = 0
            freqField.text = ""
            modeBox.currentIndex = -1
            modeBox.editText = ""
            submodeBox.currentIndex = -1
            submodeBox.editText = ""
        }
        if (root.initialSatellite.length > 0) {
            satNameField.text = root.initialSatellite
            propModeBox.currentIndex = propModeBox.find("SAT")
        }
        if (root.initialTxMHz.length > 0) freqField.text = root.initialTxMHz
        if (root.initialRxMHz.length > 0) rxFreqField.text = root.initialRxMHz
        root.initialSatellite = ""
        root.initialTxMHz = ""
        root.initialRxMHz = ""
        root.initialTrackerReceivedAt = ""
        const pwr = root.stationProfile.defaultTxPwr
        pwrField.text = !root.historicalMode && pwr > 0 ? String(pwr) : ""
    }

    // Nome, QTH e locatore dal callbook, solo nei campi ancora vuoti: quello che
    // l'operatore ha scritto non si tocca.
    Connections {
        target: decolog
        function onLookupChanged() {
            const cb = decolog.callInfo.callbook
            if (root.historicalMode || !root.visible || !decolog.callbookAutofill || !cb || !root.matchesCall)
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
        enabled: root.visible && decolog.enterToLogQso
        autoRepeat: false
        onActivated: root.submit(false)
    }
    Shortcut {
        sequence: "Alt+W"
        enabled: root.visible
        autoRepeat: false
        onActivated: root.clearAll()
    }

    body: ColumnLayout {
        Text {
            Layout.fillWidth: true
            visible: root.trackerPrepared
            text: qsTr("Tracker frequencies are a fixed snapshot; review them before saving. The band follows TX frequency; changing mode or band does not tune the radio or replace the frequencies.")
            color: Theme.warningColor
            wrapMode: Text.Wrap
        }
        Text {
            Layout.fillWidth: true
            visible: root.trackerPrepared && root.trackerReceivedAt.length > 0
            text: qsTr("Tracker snapshot received at %1. This is not confirmation of the radio frequency.").arg(root.trackerReceivedAt)
            color: Theme.textSecondary
            wrapMode: Text.Wrap
        }
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
                    objectName: "manualCallsignField"
                    Layout.fillWidth: true
                    fieldHeight: 44
                    uppercase: true
                    font.pixelSize: 24
                    font.bold: true
                    font.letterSpacing: 2
                    onTextChanged: if (!root.historicalMode) decolog.lookupCall = text
                    Keys.onReturnPressed: (event) => root.submitOnEnter(event)
                    Keys.onEnterPressed: (event) => root.submitOnEnter(event)
                }
            }
            LabeledField {
                Layout.preferredWidth: 150
                Layout.fillWidth: false
                label: qsTr("Date UTC")
                StyledTextField {
                    id: dateField
                    objectName: "manualDateUtc"
                    Layout.fillWidth: true
                    fieldHeight: 44
                    font.pixelSize: 15
                    placeholderText: decolog.dateHint
                    invalid: text.length > 0 && decolog.readDate(text).length === 0
                    onEditingFinished: {
                        const iso = decolog.readDate(text)
                        if (iso.length > 0)
                            text = decolog.showDate(iso)
                    }
                    onTextChanged: if (root.historicalMode) historicalEarlier.checked = false
                }
            }
            LabeledField {
                Layout.preferredWidth: 120
                Layout.fillWidth: false
                label: qsTr("Time on")
                StyledTextField {
                    id: timeField
                    objectName: "manualTimeUtc"
                    Layout.fillWidth: true
                    fieldHeight: 44
                    font.pixelSize: 15
                    onTextChanged: if (root.historicalMode) historicalEarlier.checked = false
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
                        interactive: !root.historicalMode
                        visible: !root.historicalMode
                        onClicked: root.resetTime()
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            visible: root.historicalMode
            CheckBox { id: historicalProfile; objectName: "historicalProfile"; enabled: !!root.stationProfile.id; Accessible.name: qsTr("Use the selected station profile for historical QSO") }
            Text { Layout.fillWidth: true; text: qsTr("Use selected station profile details"); color: Theme.textSecondary; wrapMode: Text.Wrap }
            GlassButton { objectName: "historicalNextDay"; text: qsTr("Next UTC day"); onClicked: root.advanceHistoricalDay() }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            visible: root.historicalMode && root.historicalLastSeconds >= 0 && decolog.readDate(dateField.text) === root.historicalLastDate && root.historicalSeconds(timeField.text) >= 0 && root.historicalSeconds(timeField.text) < root.historicalLastSeconds
            CheckBox { id: historicalEarlier; objectName: "historicalEarlier"; Accessible.name: qsTr("Confirm earlier time on the same UTC date") }
            Text { Layout.fillWidth: true; text: qsTr("Confirm the earlier time on this same UTC date, or use Next UTC day."); color: Theme.warningColor; wrapMode: Text.Wrap }
        }

        // Quello che il log sa del nominativo.
        Rectangle {
            Layout.fillWidth: true
            visible: !root.historicalMode
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
                    readonly property bool hasEntity: root.lookup.entityDxcc !== undefined && !root.lookup.entityCounting
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
                        objectName: "manualBand"
                        Layout.fillWidth: true
                        model: ["—"].concat(decolog.bands)
                        onActivated: root.qsyTo(currentText, root.tuneMode(), true)
                    }
                }
                LabeledField {
                    Layout.preferredWidth: 1
                    Layout.horizontalStretchFactor: 2; Layout.fillWidth: true
                    label: qsTr("Freq / TX MHz")
                    StyledTextField {
                        id: freqField
                        objectName: "manualTxFrequency"
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
                        objectName: "manualMode"
                        onEditTextChanged: root.applyDefaultReports()
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
                        onEditTextChanged: root.applyDefaultReports()
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
                    StyledTextField { id: sentField; Layout.fillWidth: true; text: root.defaultReport(modeBox.currentText) }
                }
                LabeledField {
                    Layout.preferredWidth: 1
                    Layout.horizontalStretchFactor: 2; Layout.fillWidth: true
                    label: qsTr("RST rcvd")
                    StyledTextField { id: rcvdField; Layout.fillWidth: true; text: root.defaultReport(modeBox.currentText) }
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
                    Layout.horizontalStretchFactor: 2; Layout.fillWidth: true
                    label: qsTr("My grid (optional)")
                    StyledTextField { id: myGridField; objectName: "manualMyGrid"; Layout.fillWidth: true; uppercase: true }
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
                    StyledTextField { id: satNameField; objectName: "manualSatellite"; Layout.fillWidth: true; uppercase: true; placeholderText: "OSCAR-100" }
                }
                LabeledField {
                    Layout.preferredWidth: 1
                    Layout.horizontalStretchFactor: 4; Layout.fillWidth: true
                    label: qsTr("Sat mode")
                    StyledTextField { id: satModeField; Layout.fillWidth: true; uppercase: true; placeholderText: "U/V" }
                }
                LabeledField {
                    Layout.preferredWidth: 1
                    Layout.horizontalStretchFactor: 4; Layout.fillWidth: true
                    label: qsTr("RX frequency MHz")
                    StyledTextField { id: rxFreqField; objectName: "manualRxFrequency"; Layout.fillWidth: true; placeholderText: "—" }
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
                text: root.historicalMode ? qsTr("Historical data: review before saving") : "source=manual · dirty=1 · uuid client-side"
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
                objectName: "manualLogButton"
                text: "✎ " + qsTr("LOG QSO")
                hint: decolog.enterToLogQso ? "Ctrl+Enter" : ""
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
