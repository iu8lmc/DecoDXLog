import QtQuick
import QtTest
import "../../qml/DecoDXLog"

TestCase {
    id: testCase
    name: "NewQsoTuning"
    when: windowShown
    width: 600
    height: 750
    property var decolog: fakeDecolog
    QtObject {
        id: fakeDecolog
        property var incoming: []
        property var newQsoVisibleFields: []
        property var newQsoCustomFields: []
        property var newQsoFieldOptions: []
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
        function tuneTo() {}
        signal tuningChanged()
        signal qsoPrepared(var fields)
        signal lookupChanged()
    }
    Component { id: panelComponent; NewQsoPanel { width: 580; height: 700 } }

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
