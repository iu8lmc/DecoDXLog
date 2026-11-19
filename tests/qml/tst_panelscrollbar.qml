import QtQuick
import QtQuick.Controls
import QtTest
import Decodium.UI

TestCase {
    id: testCase
    name: "PanelScrollBar"
    visible: true
    width: 500; height: 280
    when: windowShown
    property bool rtl: false
    LayoutMirroring.enabled: rtl
    LayoutMirroring.childrenInherit: true

    ScrollView {
        id: scroll
        width: 240; height: 200
        contentWidth: availableWidth
        contentHeight: testCase.contentHeight
        ScrollBar.vertical: PanelScrollBar { id: scrollBar }
        Rectangle { width: scroll.availableWidth; height: testCase.contentHeight; color: "#173151" }
    }
    property int contentHeight: 800
    ListView {
        id: list
        x: 250; width: 200; height: 200
        model: 50
        delegate: Rectangle { required property int index; width: list.width; height: 20 }
        ScrollBar.vertical: PanelScrollBar { id: listBar }
    }

    function init() {
        contentHeight = 800
        rtl = false
        scrollBar.policy = ScrollBar.AsNeeded
        scroll.contentItem.contentY = 0
        list.positionViewAtBeginning()
        wait(30)
    }
    function test_scrollview_geometry_and_drag() {
        tryCompare(scrollBar, "height", scroll.height)
        compare(scrollBar.x, scroll.width - scrollBar.width)
        tryVerify(function() { return scrollBar.size < 1 })
        verify(scrollBar.background.opacity > 0)
        mouseDrag(scrollBar, scrollBar.width / 2, scrollBar.contentItem.y + scrollBar.contentItem.height / 2,
                  0, 60, Qt.LeftButton)
        tryVerify(function() { return scroll.contentItem.contentY > 0 })
    }
    function test_listview_geometry_and_drag() {
        tryCompare(listBar, "height", list.height)
        compare(listBar.x, list.width - listBar.width)
        verify(listBar.size < 1)
        mouseDrag(listBar, listBar.width / 2, listBar.contentItem.y + listBar.contentItem.height / 2,
                  0, 60, Qt.LeftButton)
        tryVerify(function() { return list.contentY > 0 })
    }
    function test_no_scroll_indicator_when_content_fits() {
        contentHeight = 100
        tryCompare(scrollBar, "size", 1)
        compare(scrollBar.background.opacity, 0)
        compare(scrollBar.contentItem.opacity, 0)
        scrollBar.policy = ScrollBar.AlwaysOn
        compare(scrollBar.background.opacity, 1)
        compare(scrollBar.contentItem.opacity, 1)
    }
    function test_mirrored_position() {
        rtl = true
        tryCompare(scrollBar, "x", 0)
        tryCompare(listBar, "x", 0)
    }
}
