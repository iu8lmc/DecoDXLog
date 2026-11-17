import QtQuick
import QtTest
import "../../qml/DecoDXLog"
import "../../qml/DecoDXLog/DxCalendarParser.js" as Calendar

TestCase {
    id: testCase
    name: "DxCalendar"
    visible: true
    when: windowShown
    width: 800; height: 700
    readonly property string feed: "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nSUMMARY:Example Island (ZZ0ABC)\r\nDTSTART;VALUE=DATE:20261110\r\nDTEND;VALUE=DATE:20261112\r\nDESCRIPTION:FT8\\, CW\r\n continuation\r\nEND:VEVENT\r\nBEGIN:VEVENT\r\nSUMMARY:Future (ZZ1XYZ)\r\nDTSTART;VALUE=DATE:20261120\r\nDTEND;VALUE=DATE:20261125\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n"
    Component { id: panel; DxCalendarPanel { persistenceEnabled: false } }
    Timer { id: heartbeat; interval: 1; repeat: true; property int ticks: 0; onTriggered: ticks++ }
    function response(status, body) {
        return { readyState: 0, status: status, responseText: body,
                 open: function(method, url) {},
                 send: function() { this.readyState = 4; this.onreadystatechange() },
                 abort: function() {} }
    }
    function test_parser() {
        let e = Calendar.parse(feed)
        compare(e.length, 2)
        compare(e[0].call, "ZZ0ABC")
        compare(e[0].description, "FT8, CWcontinuation")
        compare(Calendar.filter(e, "zz0abc", "active", "2026-11-12").length, 1)
        compare(Calendar.filter(e, "", "upcoming", "2026-11-12").length, 1)
        compare(Calendar.parse("<html>error</html>").length, 0)
        compare(Calendar.isoDate("20260230"), "")
    }
    function test_async_fetch_filter_and_failure() {
        let view = createTemporaryObject(panel, testCase, {width: 700, height: 600, todayUtc: "2026-11-12"})
        verify(view !== null)
        view.requestFactory = function() { return response(200, feed) }
        view.refresh()
        tryCompare(view, "loading", false, 5000)
        compare(view.entries.length, 2)
        tryCompare(findChild(view, "dxCalendarList"), "count", 2, 5000)
        findChild(view, "dxCalendarSearch").text = "ZZ0ABC"
        tryCompare(findChild(view, "dxCalendarList"), "count", 1, 5000)
        view.requestFactory = function() { return response(500, "") }
        view.refresh()
        tryCompare(view, "loading", false, 5000)
        compare(view.entries.length, 2)
        view.requestFactory = function() { return response(200, "<html>bad</html>") }
        view.refresh()
        tryCompare(view, "loading", false, 5000)
        compare(view.entries.length, 2)
    }
    function test_baseline_synchronous_parse_blocks_timer() {
        let event = "BEGIN:VEVENT\nSUMMARY:Example (ZZ0ABC)\nDTSTART;VALUE=DATE:20261110\nDTEND;VALUE=DATE:20261112\nEND:VEVENT\n"
        let text = "BEGIN:VCALENDAR\n" + Array(12001).join(event) + "END:VCALENDAR\n"
        heartbeat.ticks = 0; heartbeat.start()
        compare(Calendar.parse(text).length, 12000)
        compare(heartbeat.ticks, 0)
        heartbeat.stop()
    }
    function test_worker_keeps_ui_responsive() {
        let view = createTemporaryObject(panel, testCase, {width: 700, height: 600})
        let event = "BEGIN:VEVENT\nSUMMARY:Example (ZZ0ABC)\nDTSTART;VALUE=DATE:20261110\nDTEND;VALUE=DATE:20261112\nEND:VEVENT\n"
        let text = "BEGIN:VCALENDAR\n" + Array(12001).join(event) + "END:VCALENDAR\n"
        view.requestFactory = function() { return response(200, text) }
        heartbeat.ticks = 0; heartbeat.start(); view.refresh()
        tryCompare(view, "loading", false, 15000)
        heartbeat.stop()
        compare(view.entries.length, 12000)
        verify(heartbeat.ticks > 0, "UI timer did not run during parse")
    }
}
