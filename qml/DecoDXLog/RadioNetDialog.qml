// Radio round tables: a roster entry does not create a QSO.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Decodium.UI

DialogFrame {
    id: root
    readonly property var net: decolog.radioNet
    readonly property var current: net.session
    readonly property var summary: net.summary
    readonly property var reminder: net.reminder
    readonly property var reminderOverview: net.reminderOverview
    readonly property var activeReminders: reminderOverview.reminders || []
    function probeReminderError() {
        console.warn("PROBE net reminder error dialog=" + reminderReadError.visible + " text=" + reminderReadError.text)
    }
    property double reminderClock: Date.now()
    readonly property var otherDueReminder: {
        let first = null, earliest = Infinity
        for (const entry of activeReminders) {
            const when = Date.parse(entry.deadline || "")
            if (entry.sessionId !== current.id && isFinite(when) && when <= reminderClock && when < earliest) {
                first = entry; earliest = when
            }
        }
        return first
    }
    function probeOtherReminder(expectedId) {
        console.warn("PROBE net other due=" + otherReminderButton.visible)
        if (otherReminderButton.visible) otherReminderButton.clicked()
        console.warn("PROBE net other selected=" + (current.id === expectedId))
    }
    title: qsTr("Radio net manager")
    dialogKey: "radio-net"
    width: 1000
    height: 720
    dotColor: Theme.secondaryColor
    info: qsTr("Roster entries do not create QSO")
    property bool probeOpenQso: false
    property bool probeSaveQso: false
    property bool probeHistory: false
    property bool probeReminder: false
    property bool probeQsoHistory: false
    property var historyAnchors: [0]
    property var historyRows: []
    property bool historyHasOlder: false
    property var historySessionId: 0
    property bool showReportedQsos: false
    property var qsoAnchors: [0]
    property var qsoRows: []
    property bool qsoHasOlder: false
    property var qsoSessionId: 0
    function refreshReportedQsos() {
        const id = root.current.id || 0
        if (id !== root.qsoSessionId) {
            root.qsoSessionId = id
            root.qsoAnchors = [0]
        }
        const rows = root.net.reportedQsoPage(root.qsoAnchors[root.qsoAnchors.length - 1])
        root.qsoHasOlder = rows.length > 200
        root.qsoRows = rows.slice(0, 200)
    }
    function refreshHistory() {
        const id = root.current.id || 0
        if (id !== root.historySessionId) {
            root.historySessionId = id
            root.historyAnchors = [0]
        }
        const rows = root.net.historyPage(root.historyAnchors[root.historyAnchors.length - 1])
        root.historyHasOlder = rows.length > 200
        root.historyRows = rows.slice(0, 200)
    }
    function olderHistory() {
        if (!root.historyHasOlder || root.historyRows.length === 0) return
        root.historyAnchors = root.historyAnchors.concat([root.historyRows[root.historyRows.length - 1].id])
        root.refreshHistory()
    }
    function newerHistory() {
        if (root.historyAnchors.length <= 1) return
        root.historyAnchors = root.historyAnchors.slice(0, -1)
        root.refreshHistory()
    }
    onOpened: { refreshHistory(); refreshReportedQsos() }
    Timer {
        interval: 250
        repeat: true
        running: root.visible && root.activeReminders.length > 0
        onTriggered: root.reminderClock = Date.now()
    }
    NewQsoDialog {
        id: participantQso
        profileDialogKey: "radio-net-qso"
    }
    DialogFrame {
        id: participantEditor
        transientParent: root
        title: qsTr("Edit participant")
        width: 620; height: 310
        property var member: ({})
        property string sessionUuid: ""
        property string logContext: ""
        function edit(row) {
            member=Object.assign({},row)
            sessionUuid=String(root.current.uuid || "")
            logContext=JSON.stringify(decolog.radioNetDraftContext())
            participantName.text=member.name || ""; participantNotes.text=member.notes || ""
            participantError.text=""; open()
        }
        body: ColumnLayout {
            spacing: 8
            Text { text: participantEditor.member.call || ""; color: Theme.textPrimary; font.bold: true }
            StyledTextField { id: participantName; Layout.fillWidth: true; placeholderText: qsTr("Name"); Accessible.name: qsTr("Participant name") }
            StyledTextField { id: participantNotes; Layout.fillWidth: true; placeholderText: qsTr("Notes"); Accessible.name: qsTr("Participant notes") }
            Text { id: participantError; Layout.fillWidth: true; color: Theme.warningColor; wrapMode: Text.Wrap }
            Item { Layout.fillHeight: true }
            RowLayout {
                GlassButton { id:saveParticipant; text: qsTr("Save participant"); onClicked: {
                    if (!participantEditor.logContext || participantEditor.logContext === "{}" || participantEditor.logContext !== JSON.stringify(decolog.radioNetDraftContext())) {
                        participantError.text=qsTr("The log changed; reopen the editor."); return
                    }
                    if (root.net.editParticipant(participantEditor.member.id,participantEditor.sessionUuid,
                            participantEditor.member.call || "",participantEditor.member.name || "",participantEditor.member.notes || "",participantName.text,participantNotes.text)) participantEditor.close()
                    else participantError.text=root.net.error
                } }
                GlassButton { text: qsTr("Cancel"); onClicked: participantEditor.close() }
            }
        }
    }
    FileDialog {
        id: exportDialog
        title: qsTr("Export radio net session")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        currentFile: "radio-net.json"
        nameFilters: [qsTr("JSON files (*.json)")]
        onAccepted: {
            if (!root.net.exportSession(selectedFile))
                exportMessage.text = qsTr("Export failed: %1").arg(root.net.error)
        }
    }
    Connections {
        target: root.net
        function onChanged() { root.refreshHistory(); root.refreshReportedQsos() }
        function onExportFinished(success, message) {
            exportMessage.text = success ? (message || qsTr("Session exported.")) : qsTr("Export failed: %1").arg(message)
        }
    }
    Connections {
        target: decolog
        function onLogChanged() { if (root.visible) root.refreshReportedQsos() }
    }
    function historyText(row) {
        switch (row.event) {
        case "start": return qsTr("Session started")
        case "close": return qsTr("Session closed")
        case "reopen": return qsTr("Session reopened")
        case "check_in": {
            try {
                const details = JSON.parse(row.detail || "{}")
                return qsTr("Checked in · %1 · %2").arg(details.name || "—").arg(details.notes || "—")
            } catch (e) { return qsTr("Checked in") }
        }
        case "participant_details": {
            try { const details=JSON.parse(row.detail || "{}"); return qsTr("Details updated · %1 · %2").arg(details.name || "—").arg(details.notes || "—") }
            catch (e) { return qsTr("Details updated") }
        }
        case "turn": return qsTr("Turn")
        case "reorder": return qsTr("Order changed")
        case "reminder_set": return qsTr("Reminder set for %1 UTC").arg(row.detail)
        case "reminder_clear": return qsTr("Reminder cleared")
        case "status": {
            const labels = {present: qsTr("Present"), waiting: qsTr("Waiting"), absent: qsTr("Absent"), departed: qsTr("Departed")}
            return qsTr("Status: %1").arg(labels[row.detail] || row.detail)
        }
        default: return row.event
        }
    }
    // Exercised only by the explicitly requested isolated --show probe.
    function probe(seed, openQso, saveQso, history, reminder, qsoHistory) {
        root.probeOpenQso = openQso
        root.probeSaveQso = saveQso
        root.probeHistory = history
        root.probeReminder = reminder
        root.probeQsoHistory = qsoHistory
        if (seed) {
            sessionTitle.text = "Test net"
            startButton.clicked()
            callInput.text = "K1AAA"
            nameInput.text = "Alice"
            checkInButton.clicked()
            if (openQso) {
                callInput.text = "K1BBB"
                nameInput.text = "Bob"
                checkInButton.clicked()
            }
        }
        probeTimer.start()
    }
    function probeStaleDraft() {
        if (!root.net.start("Draft fixture",0) || !root.net.checkIn("K1AAA","Alice","")) { console.warn("PROBE radioNetStaleDraft setup failed " + root.net.error); return }
        staleDraftPrepare.start()
    }
    function probeParticipantEdit() {
        if (!root.net.start("Edit fixture",0) || !root.net.checkIn("K1AAA","Alice","first")) { console.warn("PROBE participantEdit setup failed"); return }
        participantEditProbe.start()
    }
    function probeParticipantCallChange() {
        if (!root.net.start("Call change fixture",0) || !root.net.checkIn("K1AAA","Alice","first")) { console.warn("PROBE participantCall setup failed"); return }
        participantCallPrepare.start()
    }
    Timer { id:participantCallPrepare; interval:300; onTriggered: {
        const row=memberList.itemAtIndex(0)
        if (!row) { console.warn("PROBE participantCall no row"); return }
        row.editParticipant(); participantName.text="Wrong member"; console.warn("PROBE participantCall ready"); participantCallSubmit.start()
    } }
    Timer { id:participantCallSubmit; interval:1500; onTriggered: {
        saveParticipant.clicked()
        console.warn("PROBE participantCall rejected=" + (participantError.text.length>0) + " visible=" + participantEditor.visible)
    } }
    Timer { id:participantEditProbe; interval:300; onTriggered: {
        const row=memberList.itemAtIndex(0)
        if (!row) { console.warn("PROBE participantEdit no row"); return }
        row.editParticipant(); participantName.text="New name"; participantNotes.text=""
        root.net.checkIn("K1AAA","Changed elsewhere","")
        saveParticipant.clicked()
        console.warn("PROBE participantEdit stale=" + (participantError.text.length>0) + " visible=" + participantEditor.visible)
        participantEditor.close(); participantEditor.edit(root.net.members[0])
        participantName.text="Alicia"; participantNotes.text=""; saveParticipant.clicked()
        console.warn("PROBE participantEdit saved=" + !participantEditor.visible + " name=" + root.net.members[0].name + " notes=" + root.net.members[0].notes)
    } }
    Timer { id:staleDraftPrepare; interval:300; onTriggered: {
        const row=memberList.itemAtIndex(0)
        if (!row) { console.warn("PROBE radioNetStaleDraft no row"); return }
        row.openQso(); console.warn("PROBE radioNetStaleDraft ready " + participantQso.visible); staleDraftSubmit.start()
    } }
    Timer { id:staleDraftSubmit; interval:2500; onTriggered:participantQso.probeRadioNetStaleDraft() }
    Timer {
        id: probeTimer
        interval: 300
        onTriggered: {
            const row = memberList.itemAtIndex(0)
            console.warn("PROBE radioNet visible=" + root.visible + " members=" + root.net.members.length
                         + " first=" + (row ? row.modelData.call : "missing"))
            if (root.current.id)
                console.warn("PROBE radioNetSummary participants=" + root.summary.participants + " present=" + root.summary.present + " checkIns=" + root.summary.checkIns)
            if (root.probeHistory) {
                console.warn("PROBE historyFirst count=" + root.historyRows.length + " older=" + olderButton.enabled)
                olderButton.clicked()
                console.warn("PROBE historyOlder count=" + root.historyRows.length + " newer=" + newerButton.enabled + " older=" + olderButton.enabled)
                newerButton.clicked()
                console.warn("PROBE historyReturn count=" + root.historyRows.length + " newer=" + newerButton.enabled)
            }
            if (root.probeQsoHistory) {
                showQsoButton.clicked()
                console.warn("PROBE qsoHistoryFirst count=" + root.qsoRows.length + " older=" + olderButton.enabled)
                olderButton.clicked()
                console.warn("PROBE qsoHistoryOlder count=" + root.qsoRows.length + " newer=" + newerButton.enabled + " older=" + olderButton.enabled)
                newerButton.clicked()
                console.warn("PROBE qsoHistoryReturn count=" + root.qsoRows.length + " newer=" + newerButton.enabled)
            }
            if (root.probeReminder) {
                reminderSeconds.text = "1"
                setReminderButton.clicked()
                console.warn("PROBE reminderScheduled=" + !!root.reminder.scheduled)
                reminderProbeTimer.start()
            }
            if (root.probeOpenQso && row) {
                row.openQso()
                console.warn("PROBE radioNetQso visible=" + participantQso.visible + " call=" + participantQso.enteredCallForProbe)
                const other = memberList.itemAtIndex(1)
                if (other) other.openQso()
                console.warn("PROBE radioNetDraft preserved=" + (participantQso.enteredCallForProbe === "K1AAA")
                             + " notice=" + (qsoNotice.text.length > 0))
                if (root.probeSaveQso) {
                    participantQso.probeRadioNetSave()
                    row.openQso()
                    participantQso.probeRadioNetRepeat()
                    console.warn("PROBE radioNetReported count=" + root.qsoRows.length)
                    row.openQso()
                    root.net.closeSession()
                    participantQso.probeRadioNetClosed()
                }
            }
        }
    }
    Timer {
        id: reminderProbeTimer
        interval: 1600
        onTriggered: console.warn("PROBE reminderDue=" + (reminderStatus.text === qsTr("Reminder due")))
    }

    body: ColumnLayout {
        anchors.margins: 14
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            StyledComboBox {
                Layout.fillWidth: true
                Accessible.name: qsTr("Radio net session")
                readonly property var rows: root.net.sessions
                model: rows.map(s => s.title + (s.active ? " ●" : ""))
                currentIndex: Math.max(0, rows.findIndex(s => s.id === root.current.id))
                onActivated: if (currentIndex >= 0) root.net.selectSession(rows[currentIndex].id)
            }
            StyledTextField { id: sessionTitle; Layout.preferredWidth: 200; placeholderText: qsTr("New session name"); Accessible.name: qsTr("New radio net session name") }
            GlassButton {
                id: startButton
                text: qsTr("Start session")
                onClicked: if (root.net.start(sessionTitle.text, decolog.stationProfiles.activeProfileId)) sessionTitle.text = ""
            }
            GlassButton { text: qsTr("Close session"); enabled: !!root.current.active; onClicked: root.net.closeSession() }
            GlassButton { text: qsTr("Reopen"); enabled: !!root.current.id && !root.current.active; onClicked: root.net.reopenSession() }
            GlassButton { text: qsTr("Export JSON…"); enabled: !!root.current.id && !root.net.exportBusy; onClicked: exportDialog.open() }
            GlassButton { text: qsTr("Cancel export"); visible: root.net.exportBusy; onClicked: root.net.cancelExport() }
        }
        Text {
            id: reminderReadError
            Layout.fillWidth: true; visible: !!root.reminderOverview.error
            text: qsTr("Radio net reminders unavailable: %1").arg(root.reminderOverview.error || "")
            color: Theme.warningColor; wrapMode: Text.Wrap
        }
        GlassButton {
            id: otherReminderButton
            Layout.fillWidth: true
            visible: !!root.otherDueReminder
            text: qsTr("Reminder due for %1 — select session").arg(root.otherDueReminder ? root.otherDueReminder.title : "")
            onClicked: if (root.otherDueReminder) root.net.selectSession(root.otherDueReminder.sessionId)
        }
        Text {
            Layout.fillWidth: true
            text: root.current.id ? qsTr("Started: %1 UTC").arg(String(root.current.startedAt || "")) : qsTr("Start or select a session.")
            color: Theme.textSecondary
            wrapMode: Text.Wrap
        }
        Text {
            Layout.fillWidth: true
            visible: !!root.current.id
            text: qsTr("Participants: %1 · Present: %2 · Waiting: %3 · Absent: %4 · Departed: %5 · Check-ins: %6")
                  .arg(root.summary.participants || 0).arg(root.summary.present || 0).arg(root.summary.waiting || 0)
                  .arg(root.summary.absent || 0).arg(root.summary.departed || 0).arg(root.summary.checkIns || 0)
            color: Theme.textSecondary
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            visible: !!root.current.id
            Text { text: qsTr("Reminder (seconds)"); color: Theme.textSecondary }
            StyledTextField {
                id: reminderSeconds
                Layout.preferredWidth: 90
                text: "300"
                validator: IntValidator { bottom: 1; top: 86400 }
                Accessible.name: qsTr("Reminder duration in seconds")
                enabled: !!root.current.active
            }
            GlassButton { id: setReminderButton; text: qsTr("Set reminder"); enabled: !!root.current.active && reminderSeconds.acceptableInput; onClicked: root.net.setReminder(Number(reminderSeconds.text)) }
            GlassButton { text: qsTr("Clear reminder"); enabled: !!root.reminder.scheduled; onClicked: root.net.clearReminder() }
            Text {
                id: reminderStatus
                Layout.fillWidth: true
                visible: !!root.reminder.scheduled
                readonly property double remaining: Math.ceil((Date.parse(root.reminder.deadline || "") - root.reminderClock) / 1000)
                text: remaining > 0 ? qsTr("Reminder in %1 s").arg(remaining) : qsTr("Reminder due")
                color: remaining > 0 ? Theme.textSecondary : Theme.warningColor
            }
        }
        RowLayout {
            Layout.fillWidth: true
            StyledTextField { id: callInput; Layout.preferredWidth: 125; placeholderText: qsTr("Callsign"); Accessible.name: qsTr("Participant callsign"); enabled: !!root.current.active }
            StyledTextField { id: nameInput; Layout.preferredWidth: 150; placeholderText: qsTr("Name"); Accessible.name: qsTr("Participant name"); enabled: !!root.current.active }
            StyledTextField { id: noteInput; Layout.fillWidth: true; placeholderText: qsTr("Notes"); Accessible.name: qsTr("Participant notes"); enabled: !!root.current.active }
            GlassButton {
                id: checkInButton
                text: qsTr("Check in")
                enabled: !!root.current.active
                onClicked: if (root.net.checkIn(callInput.text, nameInput.text, noteInput.text)) { callInput.text = ""; nameInput.text = ""; noteInput.text = "" }
            }
            GlassButton { text: qsTr("Next turn"); enabled: !!root.current.active; onClicked: root.net.nextTurn() }
        }
        Text { Layout.fillWidth: true; visible: root.net.error.length > 0; text: root.net.error; color: Theme.warningColor; wrapMode: Text.Wrap }
        Text { id: exportMessage; Layout.fillWidth: true; visible: text.length > 0; color: Theme.textSecondary; wrapMode: Text.Wrap }
        Text { id: qsoNotice; Layout.fillWidth: true; visible: text.length > 0; color: Theme.warningColor; wrapMode: Text.Wrap }
        Text { text: qsTr("Participants"); color: Theme.secondaryColor; font.bold: true }
        ListView {
            id: memberList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: root.net.members
            ScrollBar.vertical: ScrollBar { }
            delegate: Rectangle {
                id: memberDelegate
                required property var modelData
                function openQso() {
                    if (participantQso.visible) {
                        qsoNotice.text = qsTr("Finish or close the existing radio-net QSO draft first.")
                        participantQso.raise()
                        return
                    }
                    qsoNotice.text = ""
                    participantQso.profileId = Number(root.current.stationProfileId || 0)
                    participantQso.radioNetSessionId = Number(root.current.id || 0)
                    participantQso.radioNetMemberId = Number(modelData.id || 0)
                    participantQso.radioNetSessionUuid = root.current.uuid || ""
                    participantQso.radioNetLogContext = decolog.radioNetDraftContext()
                    participantQso.initialCall = modelData.call
                    participantQso.initialName = modelData.name || ""
                    participantQso.open()
                }
                function editParticipant() { participantEditor.edit(modelData) }
                width: ListView.view.width
                height: memberRow.implicitHeight + 8
                radius: 4
                color: modelData.id == root.current.currentMemberId ? Theme.glassOverlay : "transparent"
                RowLayout {
                    id: memberRow
                    anchors.fill: parent
                    anchors.margins: 4
                    Text { Layout.preferredWidth: 110; text: modelData.call; color: Theme.textPrimary; font.bold: true; elide: Text.ElideRight }
                    Text { Layout.preferredWidth: 120; text: modelData.name; color: Theme.textPrimary; elide: Text.ElideRight }
                    Text { Layout.fillWidth: true; text: modelData.notes; color: Theme.textSecondary; elide: Text.ElideRight }
                    StyledComboBox {
                        Layout.preferredWidth: 135
                        Accessible.name: qsTr("Participant status for %1").arg(modelData.call)
                        readonly property var values: ["present", "waiting", "absent", "departed"]
                        model: [qsTr("Present"), qsTr("Waiting"), qsTr("Absent"), qsTr("Departed")]
                        currentIndex: Math.max(0, values.indexOf(modelData.status))
                        enabled: !!root.current.active
                        onActivated: root.net.setStatus(modelData.id, values[currentIndex])
                    }
                    GlassButton { text: "↑"; Accessible.name: qsTr("Move %1 up").arg(modelData.call); enabled: !!root.current.active; onClicked: root.net.moveMember(modelData.id, -1) }
                    GlassButton { text: "↓"; Accessible.name: qsTr("Move %1 down").arg(modelData.call); enabled: !!root.current.active; onClicked: root.net.moveMember(modelData.id, 1) }
                    GlassButton { text: qsTr("Edit…"); Accessible.name: qsTr("Edit participant %1").arg(modelData.call); enabled: !!root.current.active; onClicked: memberDelegate.editParticipant() }
                    GlassButton { text: qsTr("New QSO…"); Accessible.name: qsTr("Prepare QSO for %1").arg(modelData.call); onClicked: memberDelegate.openQso() }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Text { text: root.showReportedQsos ? qsTr("Reported session QSO") : qsTr("Session history"); color: Theme.secondaryColor; font.bold: true; Layout.fillWidth: true }
            GlassButton { id: showQsoButton; text: root.showReportedQsos ? qsTr("Show history") : qsTr("Show reported QSO"); onClicked: root.showReportedQsos = !root.showReportedQsos }
            GlassButton { id: newerButton; text: qsTr("Newer"); enabled: root.showReportedQsos ? root.qsoAnchors.length > 1 : root.historyAnchors.length > 1; onClicked: { if (root.showReportedQsos) { root.qsoAnchors = root.qsoAnchors.slice(0, -1); root.refreshReportedQsos() } else root.newerHistory() } }
            GlassButton { id: olderButton; text: qsTr("Older"); enabled: root.showReportedQsos ? root.qsoHasOlder : root.historyHasOlder; onClicked: { if (root.showReportedQsos) { root.qsoAnchors = root.qsoAnchors.concat([root.qsoRows[root.qsoRows.length - 1].id]); root.refreshReportedQsos() } else root.olderHistory() } }
        }
        Text {
            Layout.fillWidth: true
            visible: root.showReportedQsos
            text: qsTr("Reported metadata may come from an imported log and does not prove local participation.")
            color: Theme.textSecondary
            wrapMode: Text.Wrap
        }
        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: 110
            clip: true
            model: root.showReportedQsos ? root.qsoRows : root.historyRows
            ScrollBar.vertical: ScrollBar { }
            delegate: Text {
                required property var modelData
                width: ListView.view.width
                text: root.showReportedQsos
                      ? String(modelData.at) + " · " + (modelData.call || "—") + " · " + (modelData.band || "—") + " · " + (modelData.mode || "—") + " · " + (modelData.source || "—") + " · " + qsTr("Reported participant: %1").arg(modelData.reportedCall || "—")
                      : String(modelData.at) + " · " + (modelData.call || "—") + " · " + root.historyText(modelData)
                color: Theme.textSecondary
                elide: Text.ElideRight
            }
        }
    }
}
