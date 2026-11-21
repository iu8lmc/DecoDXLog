import QtQuick
import QtQuick.Controls
import QtTest
import "../../qml/DecoDXLog"

TestCase {
    id: testCase
    name: "CallInfoQrz"
    visible: true
    when: windowShown
    width: 640; height: 800
    property var decolog: fakeDecolog
    property var opened: []

    QtObject {
        id: fakeDecolog
        property var callInfo: ({call: ""})
        property bool callbookBusy: false
        property string dialBand: ""
        property string qrzProfileState: "unknown"
        property string qrzProfileCall: ""
        property string hamqthProfileState: "unknown"
        property string hamqthProfileCall: ""
        property var myPosition: ({lat: 35.9, lon: 14.5})
        property string deCall: "K1HOME"
        property QtObject stationProfiles: QtObject {
            property var activeProfile: ({stationCallsign: "9H1TEST"})
            function byId(id) { return {} }
        }
        property QtObject callLists: QtObject {
            property int revision: 0
            property var sources: []
            property bool busy: false
            function lookup(call) { return [] }
        }
        function subdivisionName(state, dxcc) { return state }
        function requestBandModeQsos(token, call, dxcc, band, group) {}
        signal bandModeQsosReady(string token, var rows, bool ok)
    }
    Component {
        id: panelComponent
        CallInfoPanel {
            width: 600; height: 740
            function openQrzUrl(url) { testCase.opened.push(url) }
            function openHamqthUrl(url) { testCase.opened.push(url) }
        }
    }
    SignalSpy { id: clicks; signalName: "clicked" }

    function test_journeyFromUnworkedCall() {
        const panel = createTemporaryObject(panelComponent, testCase)
        verify(panel)
        const button = findChild(panel, "callJourneyButton")
        const launcher = findChild(panel, "callJourneyLauncher")
        verify(button && launcher, "Expected a map button and a lazy flight window in the callsign card")
        fakeDecolog.callInfo = {call: "K1DX", count: 0}
        compare(button.enabled, false)
        compare(launcher.active, false)
        fakeDecolog.callInfo = {call: "K1DX", count: 0, position: {lat: 0, lon: -42}, positionApprox: true}
        compare(button.enabled, true)
        fakeDecolog.myPosition = {}
        compare(button.enabled, false)
        fakeDecolog.myPosition = {lat: 35.9, lon: 0}
        compare(button.enabled, true)
        mouseClick(button, button.width / 2, button.height / 2)
        tryVerify(function() { return launcher.item && launcher.item.visible }, 8000)
        const flight = launcher.item
        compare(flight.fields.STATION_CALLSIGN, "9H1TEST")
        compare(flight.fields.CALL, "K1DX")
        compare(flight.fields.QSO_DATE, undefined)
        compare(flight.origin.longitude, 0)
        compare(flight.destination.latitude, 0)
        compare(flight.destination.longitude, -42)
        compare(flight.approximateDestination, true)
        tryVerify(function() { return flight.progress > 0 }, 8000)
        fakeDecolog.callInfo = {call: "K2DX", count: 0, position: {lat: 1, lon: -40}}
        compare(flight.fields.CALL, "K1DX") // An open flight keeps its endpoints.
        flight.close()
        tryCompare(launcher, "active", false)
        fakeDecolog.myPosition = {lat: 35.9, lon: 14.5}
    }

    function test_profileButtonFollowsVerifiedCall() {
        const panel = createTemporaryObject(panelComponent, testCase)
        verify(panel !== null)
        const button = findChild(panel, "qrzProfileButton")
        const area = findChild(panel, "qrzProfileArea")
        verify(button !== null)
        verify(area !== null)
        clicks.target = button
        opened = []
        fakeDecolog.callInfo = {call: "K1ABC", count: 1}
        fakeDecolog.qrzProfileCall = "K1ABC"
        fakeDecolog.qrzProfileState = "checking"
        compare(button.enabled, false)
        compare(button.Accessible.description, "Checking the QRZ.com profile…")
        fakeDecolog.qrzProfileState = "unknown"
        compare(button.enabled, false)
        verify(button.Accessible.description.indexOf("not verified") >= 0)
        wait(100)
        mouseMove(area, button.width / 2, button.height / 2)
        tryCompare(area.ToolTip, "visible", true)
        mouseClick(button, button.width / 2, button.height / 2)
        compare(clicks.count, 0)
        compare(opened.length, 0)
        fakeDecolog.qrzProfileState = "available"
        compare(button.enabled, true)
        compare(button.profileUrl, "https://www.qrz.com/db/K1ABC")
        mouseClick(button, button.width / 2, button.height / 2)
        compare(clicks.count, 1)
        compare(opened.length, 1)
        compare(opened[0], "https://www.qrz.com/db/K1ABC")
        // A late response for the previous callsign cannot enable the new one.
        fakeDecolog.callInfo = {call: "EA8/K1ABC", count: 0}
        compare(button.enabled, false)
        fakeDecolog.qrzProfileCall = "EA8/K1ABC"
        compare(button.enabled, true)
        compare(button.profileUrl, "https://www.qrz.com/db/EA8%2FK1ABC")
        mouseClick(button, button.width / 2, button.height / 2)
        compare(opened.length, 2)
        compare(opened[1], "https://www.qrz.com/db/EA8%2FK1ABC")
        fakeDecolog.callInfo = {call: ""}
        compare(button.enabled, false)
    }

    function test_hamqthIndependentAndNarrowLayout() {
        const panel = createTemporaryObject(panelComponent, testCase)
        verify(panel !== null)
        const qrz = findChild(panel, "qrzProfileButton")
        const ham = findChild(panel, "hamqthProfileButton")
        const area = findChild(panel, "hamqthProfileArea")
        const qrzArea = findChild(panel, "qrzProfileArea")
        verify(qrz !== null && ham !== null && area !== null && qrzArea !== null)
        opened = []
        fakeDecolog.callInfo = {call: "OK2CQR", count: 1}
        fakeDecolog.qrzProfileCall = "OK2CQR"
        fakeDecolog.qrzProfileState = "unknown"
        fakeDecolog.hamqthProfileCall = "OK2CQR"
        fakeDecolog.hamqthProfileState = "checking"
        compare(ham.enabled, false)
        compare(ham.Accessible.description, "Checking the HamQTH profile…")
        fakeDecolog.hamqthProfileState = "unknown"
        compare(ham.enabled, false)
        verify(ham.Accessible.description.indexOf("not verified") >= 0)
        wait(100)
        mouseMove(area, ham.width / 2, ham.height / 2)
        tryCompare(area.ToolTip, "visible", true)
        mouseClick(ham, ham.width / 2, ham.height / 2)
        compare(opened.length, 0)
        fakeDecolog.hamqthProfileState = "available"
        compare(ham.enabled, true)
        compare(qrz.enabled, false)
        mouseClick(ham, ham.width / 2, ham.height / 2)
        compare(opened.length, 1)
        compare(opened[0], "https://www.hamqth.com/OK2CQR")
        fakeDecolog.callInfo = {call: "DL/OK2CQR", count: 0}
        compare(ham.enabled, false)
        fakeDecolog.hamqthProfileCall = "DL/OK2CQR"
        compare(ham.enabled, true)
        compare(ham.profileUrl, "https://www.hamqth.com/DL%2FOK2CQR")
        panel.width = 220
        tryVerify(function() { return area.y > qrzArea.y })
    }
}
