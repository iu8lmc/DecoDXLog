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
    property int campaignSpotRevision: 0
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
    Connections {
        target: root.campaignBackend ? root.campaignBackend.cluster : null
        ignoreUnknownSignals: true
        function onSpotsUpdated() { ++root.campaignSpotRevision }
        function onFilterChanged() { ++root.campaignSpotRevision }
        function onSettingsChanged() { ++root.campaignSpotRevision }
    }
    Timer { interval:5000; repeat:true; running:campaignDialog.visible; onTriggered:++root.campaignSpotRevision }
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
    function reviewCampaignDates(event) { dateReview.review(event) }
    function probeDateReview() {
        calendarObserved="2026-01-09T10:00:00Z"
        visibleEntries=[{entity:"Fixture expedition",title:"Fixture expedition (ZZ1AAA)",call:"ZZ1AAA",start:"2026-01-01",end:"2026-01-10",description:"Updated dates"}]
        Qt.callLater(function() {
            const row=list.itemAtIndex(0)
            if (!row) { console.warn("PROBE dxdates missing calendar row"); return }
            row.probeReviewDates()
            const before=root.campaignBackend.dxCampaigns
            console.warn("PROBE dxdates before " + JSON.stringify(before))
            const index=before.findIndex(c => c.title === "Spring")
            dateReview.choose(index)
            console.warn("PROBE dxdates comparison " + dateComparison.text)
            cancelDates.clicked(); console.warn("PROBE dxdates cancel " + !dateReview.visible)
            row.probeReviewDates(); dateReview.choose(index)
            const changed=Object.assign({},dateReview.selected)
            delete changed.watched
            root.campaignBackend.saveDxCampaign(changed)
            applyDates.clicked(); console.warn("PROBE dxdates stale " + dateReview.visible + " " + dateReview.reviewError)
            dateReview.choose(root.campaignBackend.dxCampaigns.findIndex(c => c.title === "Spring"))
            applyDates.clicked(); dateProbeResult.start()
        })
    }
    Timer {
        id:dateProbeResult; interval:200; repeat:true; property int tries:0
        onTriggered: {
            if (root.campaignBackend.dxCampaignBusy && ++tries<25) return
            stop(); console.warn("PROBE dxdates after " + JSON.stringify(root.campaignBackend.dxCampaigns))
            console.warn("PROBE dxdates matrix " + JSON.stringify(root.campaignBackend.dxCampaignMatrix))
        }
    }
    function probeCampaign() {
        calendarObserved="2026-01-01T10:00:00Z"
        visibleEntries=[{entity:"Fixture expedition",title:"Fixture expedition (ZZ1AAA)",call:"ZZ1AAA",start:"2026-01-01",end:"2026-01-07",description:"Synthetic announcement"}]
        Qt.callLater(function() {
            const row=list.itemAtIndex(0)
            if (!row) { console.warn("PROBE dxcampaign missing calendar row"); return }
            row.probePlan(); callsField.text="ZZ1AAA, ZZ1BBB"; bandsField.text="20m, 40m"; modesField.text="CW, FT8"; watchedField.checked=true
            saveCampaign.clicked(); campaignProbeResult.start()
        })
    }
    function probeCampaignRestore() {
        campaignDialog.edit({bands:["20m"],modes:["CW"]})
        const campaigns=root.campaignBackend.dxCampaigns
        console.warn("PROBE dxcampaign restored " + JSON.stringify(campaigns))
        if (campaigns.length) { root.campaignBackend.selectDxCampaign(campaigns[0].id); campaignDialog.edit(root.campaignBackend.dxCampaign); campaignProbeRestored.start() }
    }
    function probeCampaignSpots() {
        campaignDialog.edit({bands:["20m"],modes:["CW"]})
        const campaigns=root.campaignBackend.dxCampaigns
        if (!campaigns.length) { console.warn("PROBE dxspots missing campaign"); return }
        root.campaignBackend.selectDxCampaign(campaigns[0].id); campaignDialog.edit(root.campaignBackend.dxCampaign)
        root.campaignBackend.cluster.filter={maxAgeMinutes:30}
        root.campaignBackend.cluster.injectLine("DX de IK1AAA: 7025.0 3Y0K CW 1200Z")
        root.campaignBackend.cluster.injectLine("DX de IK1AAA: 14025.0 3Y0J CW 1200Z")
        root.campaignBackend.cluster.injectLine("DX de IK1AAA: 14025.0 K1XYZ CW 1200Z")
        campaignSpotProbe.start()
    }
    Timer {
        id:campaignSpotProbe; interval:800; repeat:true; property int tries:0
        onTriggered: {
            if (root.campaignBackend.dxCampaignBusy && ++tries<10) return
            stop()
            const spots=root.campaignBackend.dxCampaignSpots(30)
            console.warn("PROBE dxspots matching " + JSON.stringify(spots))
            console.warn("PROBE dxspots goalorder " + spots.map(s => s.call+":"+s.goalNeeded).join(","))
            const row=campaignSpots.itemAtIndex(0); console.warn("PROBE dxspots rendered " + (row ? row.text : "missing"))
            root.campaignBackend.cluster.filter={bands:["15m"],maxAgeMinutes:30}
            console.warn("PROBE dxspots filtered " + JSON.stringify(root.campaignBackend.dxCampaignSpots(30)))
        }
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
            const row=campaignMatrix.itemAtIndex(0)
            if (!row) { console.warn("PROBE dxcampaign missing matrix row"); return }
            row.probeGoal(2); console.warn("PROBE dxcampaign goal draft " + JSON.stringify(campaignDialog.goalDraft))
            saveCampaign.clicked(); campaignProbeGoal.start()
        }
    }
    Timer {
        id:campaignProbeGoal; interval:200; repeat:true; property int tries:0
        onTriggered: {
            const rows=root.campaignBackend.dxCampaignMatrix
            if ((root.campaignBackend.dxCampaignBusy || !rows.length || rows[0].target!=="confirmed") && ++tries<25) return
            stop(); console.warn("PROBE dxcampaign goal open " + JSON.stringify(rows))
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
        id:dateReview
        transientParent:root.Window.window
        title:qsTr("Review announced dates")
        info:qsTr("Choose the campaign explicitly")
        width:760; height:480
        property var announcement: ({})
        property var selected: ({})
        property string observedAt: ""
        property string logPath: ""
        property string reviewError: ""
        function review(event) {
            announcement={title:event.title || "",call:event.call || "",start:event.start || "",end:event.end || ""}
            observedAt=root.calendarObserved; logPath=root.campaignBackend.currentLogInfo().path || ""
            selected=({}); campaignChoice.currentIndex=-1; reviewError=""; open()
        }
        function choose(index) {
            const campaigns=root.campaignBackend.dxCampaigns
            selected=index>=0 && index<campaigns.length ? campaigns[index] : ({})
            campaignChoice.currentIndex=index
        }
        function apply() {
            if (!selected.id) { reviewError=qsTr("Choose a campaign first."); return }
            if (!logPath || logPath!==root.campaignBackend.currentLogInfo().path) { reviewError=qsTr("The open log changed; review again."); return }
            if (!observedAt) { reviewError=qsTr("The calendar observation time is unavailable. Update the calendar and review again."); return }
            const updated=Object.assign({},selected,{start:announcement.start,end:announcement.end,source:root.feedUrl,observedAt:observedAt})
            if (!root.campaignBackend.saveDxCampaign(updated)) { reviewError=root.campaignBackend.dxCampaignError; return }
            close()
        }
        body:ColumnLayout {
            spacing:10
            Text { Layout.fillWidth:true; wrapMode:Text.Wrap; color:root.mutedColor
                text:qsTr("The feed has no stable event identity. Select the intended campaign and compare the announcement yourself; calls alone do not establish a match.") }
            Text { Layout.fillWidth:true; wrapMode:Text.Wrap; color:root.textColor
                text:qsTr("Announcement: %1 · %2 · %3 → %4\nSource: %5 · observed: %6")
                    .arg(dateReview.announcement.title || "").arg(dateReview.announcement.call || "")
                    .arg(dateReview.announcement.start || "").arg(dateReview.announcement.end || "")
                    .arg(root.feedUrl).arg(dateReview.observedAt || qsTr("unknown")) }
            StyledComboBox {
                id:campaignChoice; Layout.fillWidth:true
                model:root.campaignBackend ? root.campaignBackend.dxCampaigns.map(c => c.title + " · " + c.start + " → " + c.end + (c.archived ? " · " + qsTr("archived") : "")) : []
                currentIndex:-1
                onActivated:dateReview.choose(currentIndex)
            }
            Text { id:dateComparison; Layout.fillWidth:true; wrapMode:Text.Wrap; color:root.textColor
                text:!dateReview.selected.id ? qsTr("No campaign selected")
                    : qsTr("Selected: %1 · %2\nCurrent: %3 → %4 · source: %5\nProposed: %6 → %7 · source: %8")
                        .arg(dateReview.selected.title).arg((dateReview.selected.calls || []).join(", "))
                        .arg(dateReview.selected.start).arg(dateReview.selected.end).arg(dateReview.selected.source)
                        .arg(dateReview.announcement.start).arg(dateReview.announcement.end).arg(root.feedUrl) }
            Text { Layout.fillWidth:true; wrapMode:Text.Wrap; color:Theme.warningColor; text:dateReview.reviewError }
            Item { Layout.fillHeight:true }
            RowLayout {
                CalendarButton { id:applyDates; text:qsTr("Apply dates to selected campaign"); enabled:!!dateReview.selected.id; onClicked:dateReview.apply() }
                CalendarButton { id:cancelDates; text:qsTr("Cancel"); onClicked:dateReview.close() }
            }
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
        property var goalDraft: []
        property bool goalsDirty: false
        property string logPath: ""
        property string editorError: ""
        function edit(c) {
            draft=c || ({}); logPath=root.campaignBackend.currentLogInfo().path || ""
            goalDraft=(draft.goals || []).map(g => ({band:g.band,mode:g.mode,target:g.target})); goalsDirty=false
            titleField.text=draft.title || ""; callsField.text=(draft.calls || []).join(", ")
            startField.text=draft.start || ""; endField.text=draft.end || ""; sourceField.text=draft.source || ""
            observedField.text=draft.observedAt || ""; bandsField.text=(draft.bands || []).join(", "); modesField.text=(draft.modes || []).join(", ")
            profileField.currentIndex=Math.max(0,profileField.ids.indexOf(draft.profileId || 0)); archivedField.checked=!!draft.archived
            watchedField.checked=!!draft.watched
            editorError=""; open()
        }
        function splitValues(text) { return text.split(",").map(s => s.trim()).filter(s => s.length) }
        function goalTarget(band,mode) {
            const goal=goalDraft.find(g => g.band === band && g.mode === mode)
            return goal ? goal.target : ""
        }
        function setGoal(band,mode,target) {
            const next=goalDraft.filter(g => !(g.band === band && g.mode === mode))
            if (target) next.push({band:band,mode:mode,target:target})
            goalDraft=next; goalsDirty=true
        }
        function save() {
            if (!logPath || logPath !== root.campaignBackend.currentLogInfo().path) { editorError=qsTr("The open log changed; reopen the campaign."); return }
            const data={id:draft.id || "", revision:draft.revision || 0, title:titleField.text, calls:splitValues(callsField.text),
                start:startField.text, end:endField.text, source:sourceField.text, observedAt:observedField.text,
                bands:splitValues(bandsField.text), modes:splitValues(modesField.text), goals:goalDraft, profileId:profileField.ids[profileField.currentIndex] || 0,
                archived:archivedField.checked, watched:watchedField.checked}
            if (root.campaignBackend.saveDxCampaign(data)) { draft=root.campaignBackend.dxCampaign; goalsDirty=false; editorError="" }
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
                            text: modelData.title + (modelData.watched ? " ★" : "") + (modelData.archived ? " · " + qsTr("archived") : "") + "\n" + modelData.start }
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
                    CheckBox { id:watchedField; objectName:"dxCampaignWatched"; text:qsTr("Watch spots") }
                    CalendarButton { id:saveCampaign; objectName:"saveDxCampaign"; text:qsTr("Save"); onClicked:campaignDialog.save() }
                    CalendarButton { text:qsTr("Refresh matrix"); enabled:!!campaignDialog.draft.id && !root.campaignBackend.dxCampaignBusy; onClicked:root.campaignBackend.refreshDxCampaign() }
                }
                Text { Layout.fillWidth:true; color:root.mutedColor; wrapMode:Text.Wrap
                    text:qsTr("Watched active campaigns add matching fresh spots to the activity log, respecting the cluster filter and active station profile. Repeated alerts are limited.") }
                Text { id:errorText; Layout.fillWidth:true; wrapMode:Text.Wrap; color:Theme.warningColor; text:campaignDialog.editorError || (root.campaignBackend ? root.campaignBackend.dxCampaignError : "") }
                Text { Layout.fillWidth:true; wrapMode:Text.Wrap; color:root.mutedColor; text:qsTr("Matrix uses saved bands, modes and dates; goals are saved with the campaign. LoTW/card confirmations are local observations, not award credit. Watch alerts currently include all matching cells.") }
                RowLayout {
                    Text { color:root.mutedColor; text:qsTr("Goals: %1%2").arg(campaignDialog.goalDraft.length).arg(campaignDialog.goalsDirty ? qsTr(" · unsaved") : "") }
                    CalendarButton { text:qsTr("Clear goals"); enabled:campaignDialog.goalDraft.length>0; onClicked:{ campaignDialog.goalDraft=[]; campaignDialog.goalsDirty=true } }
                }
                Text {
                    readonly property var bands:campaignDialog.splitValues(bandsField.text).map(v => v.toLowerCase())
                    readonly property var modes:campaignDialog.splitValues(modesField.text).map(v => v.toUpperCase())
                    readonly property int outside:campaignDialog.goalDraft.filter(g => bands.indexOf(g.band)<0 || modes.indexOf(g.mode)<0).length
                    Layout.fillWidth:true; wrapMode:Text.Wrap; color:Theme.warningColor; visible:outside>0
                    text:qsTr("%1 goal(s) are outside the entered matrix and remain saved until cleared.").arg(outside)
                }
                ListView {
                    id:campaignMatrix; objectName:"dxCampaignMatrix"; Layout.fillWidth:true; Layout.fillHeight:true; clip:true
                    model: root.campaignBackend && root.campaignBackend.dxCampaign.id === campaignDialog.draft.id ? root.campaignBackend.dxCampaignMatrix : []
                    delegate: RowLayout {
                        required property var modelData; width:ListView.view.width
                        readonly property string text:matrixLabel.text
                        function probeGoal(index) { goalChoice.currentIndex=index; goalChoice.activated(index) }
                        Text { id:matrixLabel; Layout.fillWidth:true; color:root.textColor
                            text: modelData.band + " · " + modelData.mode + " · " + (modelData.confirmed > 0 ? qsTr("confirmed") : modelData.worked > 0 ? qsTr("worked") : qsTr("missing")) + " (" + modelData.worked + "/" + modelData.confirmed + ")" + (modelData.needed ? " · " + qsTr("goal open") : "") }
                        StyledComboBox {
                            id:goalChoice; Layout.preferredWidth:155
                            readonly property var targets:["","worked","confirmed"]
                            model:[qsTr("No goal"),qsTr("Work"),qsTr("Confirm")]
                            currentIndex:Math.max(0,targets.indexOf(campaignDialog.goalTarget(modelData.band,modelData.mode)))
                            onActivated:campaignDialog.setGoal(modelData.band,modelData.mode,targets[currentIndex])
                        }
                    }
                }
                Text { Layout.fillWidth:true; wrapMode:Text.Wrap; color:root.mutedColor
                    text:qsTr("Fresh spots for the saved campaign (30 min, current cluster filters). Open campaign goals rank first when the matrix is ready; DX Assistant ranks within each group using the cluster's global log context. This is not verified award credit.") }
                ListView {
                    id:campaignSpots; objectName:"dxCampaignSpots"; Layout.fillWidth:true; Layout.preferredHeight:110; clip:true
                    model: {
                        root.campaignSpotRevision
                        if (!campaignDialog.visible || !root.campaignBackend || root.campaignBackend.dxCampaign.id !== campaignDialog.draft.id) return []
                        return root.campaignBackend.dxCampaignSpots(30)
                    }
                    delegate: Text {
                        required property var modelData; width:ListView.view.width; wrapMode:Text.Wrap; color:root.textColor
                        text:modelData.call + " · " + modelData.band + " · " + modelData.mode + " · " + modelData.frequencyKhz + " kHz · "
                            + qsTr("score %1").arg(modelData.score) + " · " + modelData.spottedAt + " · " + (modelData.source || qsTr("source unknown"))
                            + (modelData.goalTarget ? " · " + (modelData.goalNeeded ? qsTr("goal open") : qsTr("goal met")) : "")
                            + " · " + (modelData.reasons || []).join(", ") + ((modelData.limitations || []).length ? " · " + (modelData.limitations || []).join(", ") : "")
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
                function probeReviewDates() { reviewDatesButton.clicked() }
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
                    Row {
                        spacing:6
                        CalendarButton { id:planButton; visible:!!root.campaignBackend; text:qsTr("Plan campaign"); onClicked:root.beginCampaignFromEvent(modelData) }
                        CalendarButton { id:reviewDatesButton; visible:!!root.campaignBackend; text:qsTr("Review date update"); onClicked:root.reviewCampaignDates(modelData) }
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
