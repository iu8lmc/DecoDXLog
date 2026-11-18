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
