import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decodium.UI

GlassPanel {
    id: root
    objectName: "dxCalendar"
    property var entries: []
    property string statusText: ""
    property bool loading: false
    property var pendingRequest: null
    property var requestFactory: null
    property bool persistenceEnabled: true
    property var cacheBackend: typeof dxCalendarCache === "undefined" ? null : dxCalendarCache
    property var visibleEntries: []
    property int parseSerial: 0
    property int filterSerial: 0
    property var workerJobs: []
    readonly property string feedUrl: "https://www.danplanet.com/dxcal.ics"
    readonly property string sourceUrl: "https://www.ng3k.com/Misc/adxo.html"
    property string todayUtc: new Date().toISOString().slice(0, 10)
    property color panelColor: Theme.bgDeep
    property color textColor: Theme.textPrimary
    property color mutedColor: Theme.textSecondary
    property color accentColor: Theme.primaryColor
    title: qsTr("DXpedition calendar")
    showDot: false
    padding: 8
    component CalendarButton: GlassButton {
        buttonHeight: 28
        fontPixelSize: 11
    }

    function queueWorker(message) {
        workerJobs.push(message)
        workerDispatch.restart()
    }
    Timer {
        id: workerDispatch
        interval: 1
        onTriggered: {
            var jobs = root.workerJobs
            root.workerJobs = []
            for (var i = 0; i < jobs.length; ++i) calendarWorker.sendMessage(jobs[i])
        }
    }
    WorkerScript {
        id: calendarWorker
        source: "DxCalendarWorker.js"
        onMessage: function(message) {
            if (message.kind === "filter") {
                if (message.serial === root.filterSerial) root.visibleEntries = message.rows
                return
            }
            if (message.kind !== "parse" || message.serial !== root.parseSerial) return
            root.loading = false
            if (!message.entries.length) {
                root.statusText = message.fromCache
                    ? qsTr("No saved calendar. Select Update to download it.")
                    : qsTr("The source returned no valid entries. The saved calendar was kept.")
                return
            }
            root.entries = message.entries
            if (message.fromCache) root.statusText = qsTr("Saved calendar: %1").arg(message.updated)
            else {
                root.statusText = qsTr("Updated: %1").arg(message.updated)
                if (root.persistenceEnabled && root.cacheBackend)
                    root.cacheBackend.writeCache(message.text, message.updated)
            }
        }
    }
    Connections {
        target: root.cacheBackend
        ignoreUnknownSignals: true
        function onCacheRead(data, updated, error) {
            if (root.loading || root.entries.length) return
            if (error || !data) { root.statusText = qsTr("No saved calendar. Select Update to download it."); return }
            root.queueWorker({kind:"parse", serial: ++root.parseSerial, text:data,
                                        updated:updated, fromCache:true})
        }
        function onCacheWritten(error) {
            if (error) root.statusText = qsTr("Calendar updated, but the offline copy could not be saved.")
        }
    }
    function requestFilter() {
        if (!query || !filter) return
        ++filterSerial
        filterDebounce.restart()
    }
    Timer {
        id: filterDebounce
        interval: 120
        onTriggered: root.queueWorker({kind:"filter", serial:root.filterSerial, entries:root.entries,
                                       query:query.text, selection:filter.currentValue || "current", today:root.todayUtc})
    }
    onEntriesChanged: requestFilter()
    onTodayUtcChanged: requestFilter()
    Timer {
        id: timeout
        interval: 15000
        onTriggered: {
            if (root.pendingRequest)
                root.pendingRequest.abort();
            root.pendingRequest = null;
            root.loading = false;
            root.statusText = qsTr("Update timed out. Showing the last saved calendar.");
        }
    }
    Timer {
        interval: 60000
        repeat: true
        running: root.visible
        onTriggered: root.todayUtc = new Date().toISOString().slice(0, 10)
    }
    function restore() {
        statusText = qsTr("No saved calendar. Select Update to download it.")
        if (persistenceEnabled && cacheBackend) cacheBackend.readCache()
    }
    function refresh() {
        if (loading)
            return;
        loading = true;
        ++parseSerial;
        statusText = qsTr("Updating calendar…");
        var request = requestFactory ? requestFactory() : new XMLHttpRequest();
        pendingRequest = request;
        request.onreadystatechange = function () {
            if (request.readyState !== XMLHttpRequest.DONE || root.pendingRequest !== request)
                return;
            timeout.stop();
            root.pendingRequest = null;
            if (request.status !== 200 || request.responseText.length > 2000000) {
                root.loading = false
                root.statusText = qsTr("Update failed. Showing the last saved calendar.")
                return
            }
            var updated = new Date().toISOString().replace("T", " ").slice(0, 16) + " UTC"
            root.queueWorker({kind:"parse", serial: ++root.parseSerial,
                                        text:request.responseText, updated:updated, fromCache:false})
        };
        try {
            request.open("GET", feedUrl);
            timeout.restart();
            request.send();
        } catch (error) {
            timeout.stop();
            pendingRequest = null;
            loading = false;
            statusText = qsTr("Update failed. Showing the last saved calendar.");
        }
    }
    Component.onCompleted: restore()
    Component.onDestruction: {
        if (pendingRequest) {
            var request = pendingRequest;
            pendingRequest = null;
            timeout.stop();
            loading = false;
            request.abort();
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            CalendarButton {
                objectName: "dxCalendarRefresh"
                text: root.loading ? qsTr("Updating…") : qsTr("Update")
                enabled: !root.loading
                onClicked: root.refresh()
            }
        }
        Label {
            text: root.statusText
            color: root.mutedColor
            Layout.fillWidth: true
            elide: Text.ElideRight
        }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: query
                onTextChanged: root.requestFilter()
                objectName: "dxCalendarSearch"
                Layout.fillWidth: true
                placeholderText: qsTr("Callsign, entity or details")
                color: root.textColor
                placeholderTextColor: root.mutedColor
                selectionColor: root.accentColor
                selectedTextColor: root.panelColor
                implicitHeight: 34
                background: Rectangle {
                    radius: 4
                    color: root.panelColor
                    border.color: root.accentColor
                }
                selectByMouse: true
            }
            StyledComboBox {
                id: filter
                Layout.preferredWidth: 190
                onCurrentValueChanged: root.requestFilter()
                objectName: "dxCalendarFilter"
                textRole: "label"
                valueRole: "key"
                model: [
                    {
                        label: qsTr("Active and upcoming"),
                        key: "current"
                    },
                    {
                        label: qsTr("Active"),
                        key: "active"
                    },
                    {
                        label: qsTr("Upcoming"),
                        key: "upcoming"
                    },
                    {
                        label: qsTr("All"),
                        key: "all"
                    }
                ]
            }
        }
        ListView {
            id: list
            objectName: "dxCalendarList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: root.visibleEntries
            delegate: Rectangle {
                required property var modelData
                width: list.width
                height: details.implicitHeight + 44
                radius: 4
                color: Qt.rgba(1, 1, 1, 0.04)
                border.color: Qt.rgba(1, 1, 1, 0.12)
                Column {
                    anchors {
                        left: parent.left
                        right: parent.right
                        top: parent.top
                        margins: 8
                    }
                    spacing: 4
                    Text {
                        width: parent.width
                        text: (modelData.call ? modelData.call + " · " : "") + modelData.entity + "   " + modelData.start + " → " + modelData.end
                        textFormat: Text.PlainText
                        color: root.accentColor
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    Text {
                        id: details
                        width: parent.width
                        text: modelData.description || qsTr("No details provided")
                        textFormat: Text.PlainText
                        color: root.textColor
                        wrapMode: Text.Wrap
                        maximumLineCount: 4
                        elide: Text.ElideRight
                    }
                }
            }
            ScrollBar.vertical: ScrollBar {}
            Label {
                anchors.centerIn: parent
                visible: list.count === 0
                text: qsTr("No matching operations")
                color: root.mutedColor
            }
        }
        Text {
            Layout.fillWidth: true
            color: root.mutedColor
            font.pixelSize: 11
            wrapMode: Text.Wrap
            text: qsTr("Announcements: NG3K ADXO · calendar feed: DXCAL / KK7DS. Dates may change; verify with the operation.")
        }
    }
}
