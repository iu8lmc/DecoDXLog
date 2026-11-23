import QtQuick
import QtTest
import "../../qml/DecoDXLog"

TestCase {
    id: testCase
    name: "NewQsoTuning"
    when: windowShown
    width: 600
    height: 750
    visible: true
    property var decolog: fakeDecolog
    QtObject {
        id: fakeDecolog
        property var incoming: []
        property var newQsoVisibleFields: []
        property var newQsoCustomFields: []
        property var submitted: null
        property int submissions: 0
        property int tuneCalls: 0
        property bool enterToLogQso: false
        function newQsoFieldOptions() { return [{key: "IOTA", label: "IOTA"}] }
        function logManualQso(fields) { submitted = fields; submissions++; return "" }
        property var bands: ["6m", "20m"]
        property var decoLinkClients: []
        property bool clientConnected: false
        property string shownFrequency: "50.318000"
        property string shownMode: "FT4"
        property string dateHint: ""
        property string lookupCall: ""
        property bool callbookAutofill: false
        property var callInfo: ({call: ""})
        property QtObject rig: QtObject { property bool connected: false }
        property QtObject stationProfiles: QtObject { property var activeProfile: ({defaultTxPwr: 0}) }
        property QtObject activation: QtObject { property bool active: false; property var session: ({}); property var state: ({serialEnabled: false}); property string nextSerial: "" }
        property QtObject cluster: QtObject {
            property var sources: []
            function checkSpot() { return {ok: false} }
        }
        function utcNow() { return {date: "2026-11-02", time: "12:00"} }
        function bandForFrequency(freq) { return Number(freq) >= 50 ? "6m" : "20m" }
        function bandFrequency(band, mode) { return mode === "FT8" ? 50.313 : 50.318 }
        function showDate(value) { return value }
        function readDate(value) { return value }
        function tuneTo() { tuneCalls++ }
        signal tuningChanged()
        signal qsoPrepared(var fields)
        signal lookupChanged()
    }
    Component { id: panelComponent; NewQsoPanel { width: 580; height: 700 } }
    Component { id: dialogComponent; NewQsoDialog {} }

    function test_historicalModeDoesNotTuneOrInheritCurrentTime() {
        decolog.tuneCalls = 0
        decolog.rig.connected = true
        const form = createTemporaryObject(dialogComponent, testCase, {historicalMode: true})
        verify(form !== null)
        form.open()
        compare(findChild(form, "manualDateUtc").text, "")
        compare(findChild(form, "manualTimeUtc").text, "")
        compare(findChild(form, "manualTxFrequency").text, "")
        form.qsyTo("20m", "CW", true)
        compare(decolog.tuneCalls, 0)
        decolog.rig.connected = false
    }

    function test_historicalRolloverRequiresExplicitChoice() {
        decolog.submissions = 0
        const form = createTemporaryObject(dialogComponent, testCase, {historicalMode: true})
        verify(form !== null)
        form.open()
        const call = findChild(form, "manualCallsignField")
        const date = findChild(form, "manualDateUtc")
        const time = findChild(form, "manualTimeUtc")
        const freq = findChild(form, "manualTxFrequency")
        const mode = findChild(form, "manualMode")
        const earlier = findChild(form, "historicalEarlier")
        const profile = findChild(form, "historicalProfile")
        call.text = "K1AAA"; date.text = "2026-12-31"; time.text = "23:58"
        freq.text = "14.050000"; mode.currentIndex = mode.find("CW")
        form.submit(true)
        compare(decolog.submissions, 1)
        compare(decolog.submitted.historical_entry, true)
        compare(decolog.submitted.historical_use_profile, false)
        compare(date.text, "2026-12-31")
        compare(time.text, "")
        compare(freq.text, "14.050000")
        call.text = "K1BBB"; time.text = "00:05"
        form.submit(true)
        compare(decolog.submissions, 1, "Earlier UTC time must require a decision")
        verify(earlier.visible)
        earlier.checked = true
        profile.checked = true
        form.submit(true)
        compare(decolog.submissions, 2)
        compare(decolog.submitted.date, "2026-12-31")
        compare(decolog.submitted.historical_use_profile, true)
        verify(!earlier.checked)
        form.advanceHistoricalDay()
        compare(date.text, "2027-01-01")
        compare(time.text, "")
        call.text = "K1CCC"; time.text = "00:06"
        form.submit(true)
        compare(decolog.submissions, 3)
        compare(decolog.submitted.date, "2027-01-01")
        date.text = "2024-02-28"
        form.advanceHistoricalDay()
        compare(date.text, "2024-02-29")
    }

    function test_satelliteSplitFrequencies() {
        decolog.tuneCalls = 0
        decolog.rig.connected = true
        const form = createTemporaryObject(dialogComponent, testCase)
        verify(form !== null)
        form.open()
        const tx = findChild(form, "manualTxFrequency")
        const rx = findChild(form, "manualRxFrequency")
        const sat = findChild(form, "manualSatellite")
        verify(tx !== null && rx !== null && sat !== null)
        tx.text = "145.900000"
        rx.text = "435.250000"
        sat.text = "FO-29"
        form.submit(true)
        compare(decolog.submitted.freq, "145.900000")
        compare(decolog.submitted.freq_rx, "435.250000")
        compare(decolog.submitted.sat_name, "FO-29")
        compare(decolog.tuneCalls, 0)
        form.close()
        form.open()
        compare(rx.text, "", "A new draft must not inherit a stale receive frequency")
        decolog.rig.connected = false
    }

    function test_trackerDraftDoesNotTuneConnectedRig() {
        decolog.tuneCalls = 0
        decolog.rig.connected = true
        const form = createTemporaryObject(dialogComponent, testCase)
        verify(form !== null)
        form.initialSatellite = "FO-29"
        form.initialTxMHz = "145.998596"
        form.initialRxMHz = "435.004184"
        form.open()
        compare(findChild(form, "manualTxFrequency").text, "145.998596")
        compare(findChild(form, "manualRxFrequency").text, "435.004184")
        compare(findChild(form, "manualSatellite").text, "FO-29")
        compare(decolog.tuneCalls, 0)
        form.submit(true)
        compare(decolog.submitted.prop_mode, "SAT")
        compare(decolog.submitted.freq, "145.998596")
        compare(decolog.submitted.freq_rx, "435.004184")
        decolog.rig.connected = false
    }

    function test_manualLoggingKeys_data() {
        const rows = []
        for (const full of [false, true]) {
            const form = full ? "full" : "compact"
            for (const enabled of [false, true]) {
                const pref = enabled ? "on" : "off"
                for (const key of [Qt.Key_Return, Qt.Key_Enter]) {
                    const keyName = key === Qt.Key_Return ? "return" : "keypad-enter"
                    const mods = key === Qt.Key_Enter ? Qt.KeypadModifier : Qt.NoModifier
                    rows.push({tag: form + "-" + pref + "-" + keyName, full, enabled, key, mods})
                    if (full)
                        rows.push({tag: form + "-" + pref + "-ctrl-" + keyName, full, enabled, key, mods: Qt.ControlModifier | mods})
                }
            }
        }
        return rows
    }

    function test_manualLoggingKeys(data) {
        decolog.submissions = 0
        decolog.enterToLogQso = data.enabled
        const form = createTemporaryObject(data.full ? dialogComponent : panelComponent, testCase)
        verify(form !== null)
        if (data.full)
            form.open()
        else
            testCase.Window.window.requestActivate()
        tryCompare(data.full ? form : testCase.Window.window, "active", true)
        const call = findChild(form, "manualCallsignField")
        verify(call !== null)
        call.text = "HD4W"
        call.forceActiveFocus()
        tryCompare(call, "activeFocus", true)
        keyClick(data.key, data.mods)
        compare(decolog.submissions, data.enabled ? 1 : 0)
        if (!data.enabled) {
            compare(call.text, "HD4W", "Enter should preserve the draft")
            const button = findChild(form, "manualLogButton")
            verify(button !== null && button.enabled)
            mouseClick(button)
            compare(decolog.submissions, 1)
        }
        compare(decolog.submitted.call, "HD4W")
        decolog.enterToLogQso = false
    }

    function test_iotaManualEntry() {
        decolog.newQsoVisibleFields = []
        const panel = createTemporaryObject(panelComponent, testCase)
        verify(panel !== null)
        const iota = findChild(panel, "newQsoIota")
        verify(iota !== null, "The compact panel must offer a dedicated IOTA input")
        compare(iota.visible, false)
        decolog.newQsoVisibleFields = ["IOTA"]
        tryCompare(iota, "visible", true)

        decolog.qsoPrepared({call: "HD4W", mhz: 14.025, mode: "CW"})
        iota.text = "SA-056"
        panel.submit()
        compare(decolog.submitted.call, "HD4W")
        compare(decolog.submitted.iota, "SA-056")
        compare(iota.text, "", "Saving clears the island before the next contact")

        decolog.qsoPrepared({call: "W1AW"})
        iota.text = "NA-031"
        decolog.newQsoVisibleFields = []
        tryCompare(iota, "visible", false)
        panel.submit()
        compare(decolog.submitted.call, "W1AW")
        verify(!decolog.submitted.iota, "A hidden island field must not add a reference")
    }

    function test_disconnectedAndConnectedSources() {
        const panel = createTemporaryObject(panelComponent, testCase)
        verify(panel !== null)
        const mode = findChild(panel, "newQsoMode")
        const freq = findChild(panel, "newQsoFrequency")
        verify(mode !== null && freq !== null)
        mode.currentIndex = mode.find("FT8")
        freq.text = "50.313000"
        // CAT reconnect fails: stale FT4/50.318 data must not overwrite input.
        decolog.tuningChanged()
        compare(mode.currentText, "FT8")
        compare(freq.text, "50.313000")
        // A genuine connected CAT source must still update both fields.
        decolog.rig.connected = true
        decolog.tuningChanged()
        compare(mode.currentText, "FT4")
        compare(freq.text, "50.318000")
        // Decodium without CAT is also a valid live source.
        decolog.rig.connected = false
        decolog.clientConnected = true
        decolog.shownMode = "CW"
        decolog.shownFrequency = "14.025000"
        decolog.tuningChanged()
        compare(mode.currentText, "CW")
        compare(freq.text, "14.025000")
    }
}
