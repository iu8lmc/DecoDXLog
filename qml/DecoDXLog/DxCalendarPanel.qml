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
    readonly property var campaignBackend: typeof decolog === "undefined" ? null : decolog
    property string calendarObserved: ""
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
            const stamp = /^(\d{4}-\d\d-\d\d) (\d\d:\d\d) UTC$/.exec(message.updated || "")
            root.calendarObserved = stamp ? stamp[1] + "T" + stamp[2] + ":00Z" : ""
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
    function beginCampaignFromEvent(event) {
        campaignDialog.edit({title:event.entity || event.title || "", calls:event.call ? [event.call] : [],
                             start:event.start || "", end:event.end || "", source:root.feedUrl,
                             observedAt:root.calendarObserved, bands:["20m","40m","15m","10m"], modes:["CW","SSB","FT8"], profileId:0, archived:false, revision:0})
    }
    function probeCampaign() {
        calendarObserved="2026-01-01T10:00:00Z"
        visibleEntries=[{entity:"Fixture expedition",title:"Fixture expedition (ZZ1AAA)",call:"ZZ1AAA",start:"2026-01-01",end:"2026-01-07",description:"Synthetic announcement"}]
        Qt.callLater(function() {
            const row=list.itemAtIndex(0)
            if (!row) { console.warn("PROBE dxcampaign missing calendar row"); return }
            row.probePlan(); callsField.text="ZZ1AAA, ZZ1BBB"; bandsField.text="20m, 40m"; modesField.text="CW, FT8"
            saveCampaign.clicked(); campaignProbeResult.start()
        })
    }
    function probeCampaignRestore() {
        campaignDialog.edit({bands:["20m"],modes:["CW"]})
        const campaigns=root.campaignBackend.dxCampaigns
        console.warn("PROBE dxcampaign restored " + JSON.stringify(campaigns))
        if (campaigns.length) { root.campaignBackend.selectDxCampaign(campaigns[0].id); campaignDialog.edit(root.campaignBackend.dxCampaign); campaignProbeRestored.start() }
    }
    Timer {
        id:campaignProbeRestored; interval:200; repeat:true; property int tries:0
        onTriggered: {
            if ((root.campaignBackend.dxCampaignBusy || root.campaignBackend.dxCampaignMatrix.length<4) && ++tries<25) return
            stop(); const row=campaignMatrix.itemAtIndex(0)
            console.warn("PROBE dxcampaign restored matrix " + JSON.stringify(root.campaignBackend.dxCampaignMatrix))
            console.warn("PROBE dxcampaign rendered " + (row ? row.text : "missing"))
        }
    }
    Timer {
        id:campaignProbeResult; interval:200; repeat:true; property int tries:0
        onTriggered: {
            if ((root.campaignBackend.dxCampaignBusy || root.campaignBackend.dxCampaignMatrix.length<4) && ++tries<25) return
            stop(); console.warn("PROBE dxcampaign saved " + JSON.stringify(root.campaignBackend.dxCampaign))
            console.warn("PROBE dxcampaign matrix " + JSON.stringify(root.campaignBackend.dxCampaignMatrix))
            archivedField.checked=true; saveCampaign.clicked(); console.warn("PROBE dxcampaign archived " + root.campaignBackend.dxCampaign.archived)
            archivedField.checked=false; saveCampaign.clicked(); console.warn("PROBE dxcampaign reopened " + !root.campaignBackend.dxCampaign.archived)
            root.campaignBackend.cards.markReceived(1,true); campaignProbeConfirmation.start()
        }
    }
    Timer {
        id:campaignProbeConfirmation; interval:200; repeat:true; property int tries:0
        onTriggered: {
            const rows=root.campaignBackend.dxCampaignMatrix
            if ((root.campaignBackend.dxCampaignBusy || !rows.length || rows[0].confirmed!==1) && ++tries<20) return
            stop(); console.warn("PROBE dxcampaign new confirmation " + JSON.stringify(rows))
        }
    }

    DialogFrame {
        id: campaignDialog
        objectName: "dxCampaignDialog"
        transientParent: root.Window.window
        title: qsTr("DX campaigns")
        info: qsTr("Local log · source data requires review")
        width: 1000; height: 760
        property var draft: ({})
        property string logPath: ""
        property string editorError: ""
        function edit(c) {
            draft=c || ({}); logPath=root.campaignBackend.currentLogInfo().path || ""
            titleField.text=draft.title || ""; callsField.text=(draft.calls || []).join(", ")
            startField.text=draft.start || ""; endField.text=draft.end || ""; sourceField.text=draft.source || ""
            observedField.text=draft.observedAt || ""; bandsField.text=(draft.bands || []).join(", "); modesField.text=(draft.modes || []).join(", ")
            profileField.currentIndex=Math.max(0,profileField.ids.indexOf(draft.profileId || 0)); archivedField.checked=!!draft.archived
            editorError=""; open()
        }
        function splitValues(text) { return text.split(",").map(s => s.trim()).filter(s => s.length) }
        function save() {
            if (!logPath || logPath !== root.campaignBackend.currentLogInfo().path) { editorError=qsTr("The open log changed; reopen the campaign."); return }
            const data={id:draft.id || "", revision:draft.revision || 0, title:titleField.text, calls:splitValues(callsField.text),
                start:startField.text, end:endField.text, source:sourceField.text, observedAt:observedField.text,
                bands:splitValues(bandsField.text), modes:splitValues(modesField.text), profileId:profileField.ids[profileField.currentIndex] || 0,
                archived:archivedField.checked}
            if (root.campaignBackend.saveDxCampaign(data)) { draft=root.campaignBackend.dxCampaign; editorError="" }
            else editorError=root.campaignBackend.dxCampaignError
        }
        body: RowLayout {
            spacing: 12
            ColumnLayout {
                Layout.preferredWidth: 205; Layout.fillHeight: true
                Text { text: qsTr("Saved campaigns"); color: root.textColor; font.bold: true }
                ListView {
                    id: campaignList; objectName: "dxCampaignList"; Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                    model: root.campaignBackend ? root.campaignBackend.dxCampaigns : []
                    delegate: Rectangle {
                        required property var modelData
                        width: ListView.view.width; height: 46; color: Qt.rgba(1,1,1,0.05)
                        Text { anchors.fill: parent; anchors.margins: 4; color: root.textColor; elide: Text.ElideRight
                            text: modelData.title + (modelData.archived ? " · " + qsTr("archived") : "") + "\n" + modelData.start }
                        MouseArea { anchors.fill: parent; onClicked: { if (root.campaignBackend.selectDxCampaign(modelData.id)) campaignDialog.edit(root.campaignBackend.dxCampaign) } }
                    }
                }
                CalendarButton { text: qsTr("New campaign"); onClicked: campaignDialog.edit({bands:["20m","40m","15m","10m"],modes:["CW","SSB","FT8"]}) }
            }
            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 5
                Text { text: qsTr("Review the announcement and operating period before saving."); color: root.mutedColor; wrapMode: Text.Wrap; Layout.fillWidth: true }
                StyledTextField { id: titleField; objectName:"dxCampaignTitle"; Layout.fillWidth:true; placeholderText:qsTr("Campaign name") }
                StyledTextField { id: callsField; objectName:"dxCampaignCalls"; Layout.fillWidth:true; placeholderText:qsTr("Callsigns, comma separated") }
                RowLayout {
                    StyledTextField { id:startField; objectName:"dxCampaignStart"; Layout.fillWidth:true; placeholderText:qsTr("Start YYYY-MM-DD UTC") }
                    StyledTextField { id:endField; objectName:"dxCampaignEnd"; Layout.fillWidth:true; placeholderText:qsTr("End YYYY-MM-DD UTC, inclusive") }
                }
                StyledTextField { id:sourceField; Layout.fillWidth:true; placeholderText:qsTr("Source") }
                StyledTextField { id:observedField; Layout.fillWidth:true; placeholderText:qsTr("Source observed at YYYY-MM-DDTHH:MM:SSZ") }
                RowLayout {
                    StyledTextField { id:bandsField; Layout.fillWidth:true; placeholderText:qsTr("Bands, comma separated") }
                    StyledTextField { id:modesField; Layout.fillWidth:true; placeholderText:qsTr("Modes, comma separated") }
                }
                RowLayout {
                    StyledComboBox {
                        id:profileField; Layout.preferredWidth:220
                        readonly property var ids: { const out=[0]; if (root.campaignBackend) for (let i=0;i<root.campaignBackend.stationProfiles.count;++i) { const p=root.campaignBackend.stationProfiles.get(i); if (!p.deleted) out.push(p.id) }; return out }
                        model: ids.map(id => id===0 ? qsTr("All station profiles") : root.campaignBackend.stationProfiles.byId(id).name)
                    }
                    CheckBox { id:archivedField; text:qsTr("Archived") }
                    CalendarButton { id:saveCampaign; objectName:"saveDxCampaign"; text:qsTr("Save"); onClicked:campaignDialog.save() }
                    CalendarButton { text:qsTr("Refresh matrix"); enabled:!!campaignDialog.draft.id && !root.campaignBackend.dxCampaignBusy; onClicked:root.campaignBackend.refreshDxCampaign() }
                }
                Text { id:errorText; Layout.fillWidth:true; wrapMode:Text.Wrap; color:Theme.warningColor; text:campaignDialog.editorError || (root.campaignBackend ? root.campaignBackend.dxCampaignError : "") }
                Text { Layout.fillWidth:true; wrapMode:Text.Wrap; color:root.mutedColor; text:qsTr("Matrix: exact calls and inclusive UTC dates; local LoTW/card states only. This is not an award eligibility decision.") }
                ListView {
                    id:campaignMatrix; objectName:"dxCampaignMatrix"; Layout.fillWidth:true; Layout.fillHeight:true; clip:true
                    model: root.campaignBackend && root.campaignBackend.dxCampaign.id === campaignDialog.draft.id ? root.campaignBackend.dxCampaignMatrix : []
                    delegate: Text {
                        required property var modelData; width:ListView.view.width; color:root.textColor
                        text: modelData.band + " · " + modelData.mode + " · " + (modelData.confirmed > 0 ? qsTr("confirmed") : modelData.worked > 0 ? qsTr("worked") : qsTr("missing")) + " (" + modelData.worked + "/" + modelData.confirmed + ")"
                    }
                }
            }
        }
    }
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
            CalendarButton { objectName:"dxCampaignsButton"; visible:!!root.campaignBackend; text:qsTr("Campaigns"); onClicked:campaignDialog.edit({bands:["20m","40m","15m","10m"],modes:["CW","SSB","FT8"]}) }
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
                function probePlan() { planButton.clicked() }
                width: list.width
                height: details.implicitHeight + 76
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
                    CalendarButton { id:planButton; visible:!!root.campaignBackend; text:qsTr("Plan campaign"); onClicked:root.beginCampaignFromEvent(modelData) }
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
