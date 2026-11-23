import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decodium.UI

DialogFrame {
    id: root
    title: qsTr("PREDICT satellite tracker")
    dialogKey: "predict-tracker"
    width: 720
    height: 600
    dotColor: Theme.secondaryColor
    info: qsTr("Read-only: the tracker controls neither radio nor rotor")
    readonly property var tracker: decolog.predictTracker
    readonly property var planner: decolog.predictPassPlanner
    readonly property var schedule: decolog.predictPassSchedule
    property bool scheduleVisible: false
    property bool probingSchedule: false
    property bool probingScheduleNext: false
    property var selectedPass: ({})
    readonly property var sample: tracker.snapshot
    property var correction: ({})
    property bool probing: false
    property bool probingList: false
    property bool probingLive: false
    property bool probingPass: false
    property bool probingDraft: false
    property bool probingDraftSave: false
    property string probingDraftMode: "CW"
    property string probingDraftMapping: "independent"
    property int probePassCount: 0
    property int probeLiveSuccesses: 0
    property bool listValid: false
    property string listHost: ""
    property string listPort: ""
    property bool resultValid: false
    property bool discardPending: false
    property bool passValid: false
    readonly property var plannedPass: selectedPass
    readonly property double passEpochMs: plannedPass.tleEpoch ? new Date(plannedPass.tleEpoch).getTime() : NaN
    readonly property double passAgeDays: isFinite(passEpochMs) ? (Date.now() - passEpochMs) / 86400000 : NaN
    readonly property double epochMs: sample.tleEpoch ? new Date(sample.tleEpoch).getTime() : NaN
    readonly property double ageDays: isFinite(epochMs) ? (Date.now() - epochMs) / 86400000 : NaN
    function invalidateSample() {
        root.resultValid = false
        root.correction = ({})
        if (correctionStatus) correctionStatus.text = ""
    }
    function probe(host, port, name) {
        probing = true
        hostField.text = host
        portField.text = String(port)
        satelliteField.text = name
        requestButton.clicked()
    }
    function probeList(host, port) {
        probingList = true
        hostField.text = host
        portField.text = String(port)
        listButton.clicked()
    }
    function probeLive(host, port, name) {
        probingLive = true
        probeLiveSuccesses = 0
        hostField.text = host
        portField.text = String(port)
        satelliteField.text = name
        autoRefresh.checked = true
        autoRefresh.toggled()
    }
    function probePass(host, port, name) {
        probingPass = true
        probePassCount = 0
        hostField.text = host
        portField.text = String(port)
        satelliteField.text = name
        firstPassButton.clicked()
    }
    function probeSchedule(host, port, name) {
        probingSchedule = true
        hostField.text = host
        portField.text = String(port)
        satelliteField.text = name
        scheduleCount.value = 3
        scheduleButton.clicked()
    }
    function probeDraft(host, port, name, save, mode, mapping) {
        probingDraft = true
        probingDraftSave = !!save
        probingDraftMode = mode || "CW"
        probingDraftMapping = mapping || "independent"
        mappingBox.currentIndex = probingDraftMapping === "inverting" ? 1 : probingDraftMapping === "noninverting" ? 2 : 0
        offsetField.text = probingDraftMapping === "independent" ? "" : "10"
        halfWidthField.text = probingDraftMapping === "independent" ? "" : "50"
        hostField.text = host
        portField.text = String(port)
        satelliteField.text = name
        uplinkField.text = "146.000000"
        downlinkField.text = "435.000000"
        requestButton.clicked()
    }
    function calculateCorrection() {
        if (!root.resultValid || root.sample.satellite !== satelliteField.text.trim()) {
            correction = ({ok: false, error: qsTr("Read the selected satellite first.")})
            return false
        }
        const mapping=["independent", "inverting", "noninverting"][mappingBox.currentIndex]
        if (!mapping) { correction=({ok:false, error:qsTr("Select a transponder model.")}); return false }
        const nominal=root.tracker.nominalTransponderPair(Number(uplinkField.text.replace(",", ".")),
            Number(downlinkField.text.replace(",", ".")), mapping === "independent" ? 0 : Number(offsetField.text.replace(",", ".")),
            mapping === "independent" ? 0 : Number(halfWidthField.text.replace(",", ".")), mapping)
        if (!nominal.ok) { correction=nominal; return false }
        correction = root.tracker.correctedFrequencies(nominal.txMHz,nominal.rxMHz)
        return !!correction.ok
    }
    function prepareDraft() {
        if (satelliteQso.visible) {
            correctionStatus.text = qsTr("Finish or close the existing satellite QSO draft first.")
            satelliteQso.raise()
            return
        }
        if (!root.calculateCorrection()) { correctionStatus.text = root.correction.error; return }
        satelliteQso.initialSatellite = root.sample.satellite
        satelliteQso.initialTxMHz = Number(root.correction.txMHz).toFixed(6)
        satelliteQso.initialRxMHz = Number(root.correction.rxMHz).toFixed(6)
        satelliteQso.open()
        correctionStatus.text = qsTr("Review the draft before saving; no radio command was sent.")
    }
    function selectSchedulePass(index) {
        if (!root.scheduleVisible || root.schedule.busy || index < 0 || index >= root.schedule.passes.length)
            return
        root.selectedPass = root.schedule.passes[index]
        root.passValid = true
        status.text = qsTr("Pass selected for review; no radio command sent.")
    }
    function requestPass(start) {
        autoRefresh.checked = false
        refreshTimer.stop()
        root.passValid = false
        status.text = qsTr("Predicting pass…")
        if (!root.planner.request(hostField.text.trim(), Number(portField.text), satelliteField.text.trim(), start)) {
            status.text = qsTr("Enter a valid server and satellite name for pass prediction.")
            if (root.probingPass) { console.warn("PROBE predictPass success=false invalid=true"); root.probingPass = false }
        }
    }
    function refreshSnapshot() {
        root.discardPending = false
        root.resultValid = false
        status.text = qsTr("Reading…")
        if (!root.tracker.request(hostField.text.trim(), Number(portField.text), satelliteField.text.trim())) {
            autoRefresh.checked = false
            status.text = qsTr("Enter a numeric IP, port and satellite name (up to 25 characters).")
            if (root.probing) { console.warn("PROBE predict success=false invalid=true"); root.probing = false }
        }
    }
    onClosed: {
        autoRefresh.checked = false
        refreshTimer.stop()
        root.resultValid = false
        root.discardPending = true
        root.planner.cancel()
        root.schedule.cancel()
        root.scheduleVisible = false
        root.selectedPass = ({})
        root.passValid = false
    }
    Timer {
        id: refreshTimer
        interval: 2500
        repeat: false
        onTriggered: if (root.visible && autoRefresh.checked && !root.tracker.busy) root.refreshSnapshot()
    }
    Timer {
        id: correctionAgeTimer
        interval: 10000
        onTriggered: {
            if (root.correction.ok && !satelliteQso.visible)
                correctionStatus.text = qsTr("Tracker reading expired; read it again.")
            root.correction = ({})
        }
    }
    NewQsoDialog { id: satelliteQso; profileDialogKey: "predict-satellite-qso" }
    Connections {
        target: root.schedule
        function onFinished(success, error) {
            scheduleStatus.text = success ? qsTr("Requested passes received") : qsTr("Incomplete schedule: %1").arg(error)
            if (root.probingSchedule) {
                console.warn("PROBE predictSchedule success=" + success + " count=" + root.schedule.passes.length
                             + " rendered=" + scheduleRepeater.count + " old=" + (root.schedule.passes.length > 0 &&
                             (Date.now() - new Date(root.schedule.passes[0].tleEpoch).getTime()) / 86400000 > 14))
                root.probingSchedule = false
                if (success && root.schedule.passes.length >= 2) {
                    scheduleRepeater.itemAt(1).selectPass()
                    console.warn("PROBE selectedSchedule aos=" + root.plannedPass.aos + " next=" + nextPassButton.enabled)
                    root.probingScheduleNext = true
                    nextPassButton.clicked()
                }
            }
        }
    }
    Connections {
        target: root.planner
        function onFinished(success, error) {
            root.selectedPass = success ? root.planner.pass : ({})
            root.passValid = success && root.visible
            if (root.probingScheduleNext) {
                console.warn("PROBE selectedScheduleNext success=" + success + " aos=" + (root.plannedPass.aos || 0))
                root.probingScheduleNext = false
            }
            status.text = success ? qsTr("Pass received") : qsTr("Tracker error: %1").arg(error)
            if (root.probingPass) {
                ++root.probePassCount
                if (root.probePassCount === 1 && success) {
                    console.warn("PROBE predictPass success=" + success + " rendered=" + passSummary.visible
                                 + " max=" + (root.plannedPass.maxElevation || 0) + " old=" + (root.passAgeDays > 14))
                    Qt.callLater(() => nextPassButton.clicked())
                } else {
                    console.warn("PROBE predictPassNext success=" + success + " rendered=" + passSummary.visible)
                    root.probingPass = false
                }
            }
        }
    }
    Connections {
        target: root.tracker
        function onSatellitesFinished(success, error) {
            root.listValid = success && root.listHost === hostField.text.trim() && root.listPort === portField.text.trim()
            status.text = root.listValid ? qsTr("Satellite list received") : qsTr("Tracker error: %1").arg(error)
            if (root.probingList) {
                if (success && satelliteChoices.count > 0) {
                    satelliteChoices.currentIndex = 0
                    satelliteChoices.activated(0)
                }
                console.warn("PROBE predictList success=" + success + " count=" + satelliteChoices.count
                             + " selected=" + satelliteField.text + " error=" + error)
                root.probingList = false
            }
        }
        function onFinished(success, error) {
            root.resultValid = success && root.visible && !root.discardPending
            root.correction = ({})
            correctionStatus.text = ""
            if (root.resultValid) correctionAgeTimer.restart()
            else correctionAgeTimer.stop()
            status.text = success ? qsTr("Snapshot received") : qsTr("Tracker error: %1").arg(error)
            if (root.resultValid && autoRefresh.checked) refreshTimer.start()
            if (!success) { autoRefresh.checked = false; refreshTimer.stop() }
            if (root.probingLive) {
                if (success) ++root.probeLiveSuccesses
                else {
                    console.warn("PROBE predictLive stopped=" + !autoRefresh.checked + " rendered=" + satelliteStatus.visible
                                 + " successes=" + root.probeLiveSuccesses)
                    root.probingLive = false
                }
            }
            if (root.probing) {
                console.warn("PROBE predict success=" + success + " satellite=" + (root.sample.satellite || "")
                             + " old=" + (root.ageDays > 14) + " rendered=" + satelliteStatus.visible
                             + " warning=" + (staleNotice.visible && root.ageDays > 14) + " error=" + error)
                root.probing = false
            }
            if (root.probingDraft) {
                if (success) {
                    if (root.probingDraftMapping !== "independent") {
                        offsetField.text="51"; const rejected=!root.calculateCorrection()
                        console.warn("PROBE predictLinear outside=" + rejected + " draft=" + satelliteQso.visible)
                        offsetField.text="10"
                    }
                    prepareDraftButton.clicked()
                    console.warn("PROBE predictDraft visible=" + satelliteQso.visible + " tx=" + satelliteQso.initialTxForProbe
                                 + " rx=" + satelliteQso.initialRxForProbe + " sat=" + satelliteQso.initialSatelliteForProbe)
                    prepareDraftButton.clicked()
                    console.warn("PROBE predictDraftPreserved=" + satelliteQso.visible + " notice=" + (correctionStatus.text.length > 0))
                    if (root.probingDraftSave && satelliteQso.visible) {
                        console.warn("PROBE predictDraftBeforeSave count=" + decolog.currentLogInfo().qsos)
                        satelliteQso.probeTrackerDraftSave(root.probingDraftMode)
                    }
                } else console.warn("PROBE predictDraft failed=" + error)
                root.probingDraft = false
            }
        }
    }
    body: ScrollView {
        id: dialogScroll
        anchors.margins: 16
        clip: true
        contentWidth: availableWidth
        ColumnLayout {
        width: dialogScroll.availableWidth
        spacing: 12
        Text { text: qsTr("PREDICT UDP server"); color: Theme.textPrimary; font.bold: true }
        RowLayout {
            Layout.fillWidth: true
            StyledTextField { id: hostField; text: "127.0.0.1"; placeholderText: qsTr("IP address"); Accessible.name: qsTr("Tracker IP address"); Layout.fillWidth: true; enabled: !root.tracker.busy && !root.planner.busy && !root.schedule.busy && !autoRefresh.checked; onTextChanged: { root.listValid = false; root.invalidateSample(); root.passValid = false; root.scheduleVisible = false } }
            StyledTextField { id: portField; text: "1210"; placeholderText: qsTr("Port"); Accessible.name: qsTr("Tracker port"); validator: IntValidator { bottom: 1; top: 65535 } Layout.preferredWidth: 90; enabled: !root.tracker.busy && !root.planner.busy && !root.schedule.busy && !autoRefresh.checked; onTextChanged: { root.listValid = false; root.invalidateSample(); root.passValid = false; root.scheduleVisible = false } }
            StyledTextField { id: satelliteField; placeholderText: qsTr("Exact satellite name"); Accessible.name: qsTr("Exact satellite name"); Layout.fillWidth: true; enabled: !root.tracker.busy && !root.planner.busy && !root.schedule.busy && !autoRefresh.checked; onTextChanged: { root.invalidateSample(); root.passValid = false; root.scheduleVisible = false } }
            GlassButton {
                id: requestButton
                text: qsTr("Read snapshot")
                enabled: !root.tracker.busy && !root.planner.busy && !root.schedule.busy && !autoRefresh.checked && portField.acceptableInput
                onClicked: root.refreshSnapshot()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            GlassButton {
                id: listButton
                text: qsTr("Load satellites")
                enabled: !root.tracker.busy && !root.planner.busy && !root.schedule.busy && !autoRefresh.checked && portField.acceptableInput
                onClicked: {
                    root.listValid = false
                    satelliteField.text = ""
                    root.listHost = hostField.text.trim()
                    root.listPort = portField.text.trim()
                    status.text = qsTr("Reading…")
                    if (!root.tracker.requestSatellites(hostField.text.trim(), Number(portField.text))) {
                        status.text = qsTr("Enter a numeric IP and a valid port.")
                        if (root.probingList) { console.warn("PROBE predictList success=false invalid=true"); root.probingList = false }
                    }
                }
            }
            StyledComboBox {
                id: satelliteChoices
                Layout.fillWidth: true
                Accessible.name: qsTr("Available tracked satellites")
                enabled: !root.tracker.busy && !root.planner.busy && !root.schedule.busy && !autoRefresh.checked
                model: root.listValid ? root.tracker.satellites : []
                onActivated: if (currentIndex >= 0) satelliteField.text = currentText
            }
        }
        CheckBox {
            id: autoRefresh
            text: qsTr("Refresh automatically (2.5 s after each reading)")
            enabled: root.visible && !root.planner.busy && !root.schedule.busy && (!root.tracker.busy || checked) && portField.acceptableInput
            onToggled: {
                if (checked) root.refreshSnapshot()
                else refreshTimer.stop()
            }
        }
        RowLayout {
            Layout.fillWidth:true; spacing:8
            Text { text:qsTr("Transponder model"); color:Theme.textSecondary }
            StyledComboBox { id:mappingBox; Layout.fillWidth:true
                model:[qsTr("Independent frequencies / FM"),qsTr("Linear: inverting"),qsTr("Linear: non-inverting")]
                onActivated:{ root.correction=({}); correctionStatus.text="" } }
        }
        RowLayout {
            Layout.fillWidth:true; spacing:8; visible:mappingBox.currentIndex>0
            StyledTextField { id:offsetField; Layout.fillWidth:true; placeholderText:qsTr("Uplink centre offset (kHz)"); Accessible.name:qsTr("Uplink centre offset in kHz"); onTextChanged:{ root.correction=({}); if (correctionStatus) correctionStatus.text="" } }
            StyledTextField { id:halfWidthField; Layout.fillWidth:true; placeholderText:qsTr("Half bandwidth (kHz)"); Accessible.name:qsTr("Linear half bandwidth in kHz"); onTextChanged:{ root.correction=({}); if (correctionStatus) correctionStatus.text="" } }
        }
        Text { Layout.fillWidth:true; visible:mappingBox.currentIndex>0; wrapMode:Text.WordWrap; color:Theme.warningColor
            text:qsTr("Enter nominal uplink/downlink centre frequencies and half bandwidth for this transponder. This ideal 1:1 mapping is not a satellite catalogue; verify current limits and operating mode.") }
        RowLayout {
            Layout.fillWidth: true
            StyledTextField { id: uplinkField; Layout.fillWidth: true; placeholderText: mappingBox.currentIndex>0 ? qsTr("Uplink centre TX MHz") : qsTr("Nominal uplink TX MHz"); Accessible.name: qsTr("Nominal uplink TX MHz"); onTextChanged: { root.correction = ({}); if (correctionStatus) correctionStatus.text = "" } }
            StyledTextField { id: downlinkField; Layout.fillWidth: true; placeholderText: mappingBox.currentIndex>0 ? qsTr("Downlink centre RX MHz") : qsTr("Nominal downlink RX MHz"); Accessible.name: qsTr("Nominal downlink RX MHz"); onTextChanged: { root.correction = ({}); if (correctionStatus) correctionStatus.text = "" } }
            GlassButton { text: qsTr("Preview Doppler"); enabled: root.resultValid && !root.tracker.busy; onClicked: { root.calculateCorrection(); correctionStatus.text = root.correction.ok ? qsTr("Corrected TX %1 MHz · RX %2 MHz").arg(Number(root.correction.txMHz).toFixed(6)).arg(Number(root.correction.rxMHz).toFixed(6)) : root.correction.error } }
            GlassButton { id: prepareDraftButton; text: qsTr("Prepare QSO…"); enabled: root.resultValid && !root.tracker.busy; onClicked: root.prepareDraft() }
        }
        Text { id: correctionStatus; Layout.fillWidth: true; color: Theme.textSecondary; wrapMode: Text.Wrap }
        RowLayout {
            Layout.fillWidth: true
            GlassButton { id: firstPassButton; text: qsTr("Predict next pass"); enabled: !root.tracker.busy && !root.planner.busy && !root.schedule.busy && portField.acceptableInput; onClicked: root.requestPass(Math.floor(Date.now() / 1000)) }
            GlassButton { id: nextPassButton; text: qsTr("Following pass"); enabled: root.passValid && !root.tracker.busy && !root.planner.busy && !root.schedule.busy; onClicked: root.requestPass(Number(root.plannedPass.los) + 1) }
            GlassButton { text: qsTr("Cancel prediction"); visible: root.planner.busy; onClicked: root.planner.cancel() }
        }
        RowLayout {
            Layout.fillWidth: true
            Text { text: qsTr("Number of passes"); color: Theme.textSecondary }
            SpinBox { id: scheduleCount; from: 1; to: 10; value: 3; editable: true; enabled: !root.schedule.busy; Accessible.name: qsTr("Number of predicted passes") }
            GlassButton {
                id: scheduleButton
                text: qsTr("Plan passes")
                enabled: !root.tracker.busy && !root.planner.busy && !root.schedule.busy && portField.acceptableInput
                onClicked: {
                    autoRefresh.checked = false
                    refreshTimer.stop()
                    root.scheduleVisible = true
                    root.selectedPass = ({})
                    root.passValid = false
                    scheduleStatus.text = qsTr("Predicting passes…")
                    if (!root.schedule.request(hostField.text.trim(), Number(portField.text), satelliteField.text.trim(), Math.floor(Date.now() / 1000), scheduleCount.value)) {
                        root.scheduleVisible = false
                        status.text = qsTr("Enter a valid server and satellite name.")
                    }
                }
            }
            GlassButton { text: qsTr("Cancel plan"); visible: root.schedule.busy; onClicked: root.schedule.cancel() }
        }
        Text { id: scheduleStatus; Layout.fillWidth: true; visible: root.scheduleVisible && !root.schedule.busy; color: Theme.textSecondary; wrapMode: Text.Wrap }
        Text { Layout.fillWidth: true; visible: root.scheduleVisible && root.schedule.busy; color: Theme.textSecondary; wrapMode: Text.Wrap
            text: qsTr("Predicting pass %1 of %2…").arg(root.schedule.passes.length + 1).arg(scheduleCount.value) }
        Text { Layout.fillWidth: true; visible: root.scheduleVisible && root.schedule.passes.length > 0; color: Theme.warningColor; wrapMode: Text.Wrap
            text: qsTr("Predictions use sampled elevations and the same TLE. Check the TLE epoch and current operating conditions before use.") }
        Repeater {
            id: scheduleRepeater
            model: root.scheduleVisible ? root.schedule.passes : []
            RowLayout {
                required property var modelData
                required property int index
                Layout.fillWidth: true
                function selectPass() { selectPassButton.clicked() }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.textPrimary
                    text: qsTr("UTC %1 – %2 · maximum sampled elevation %3° · TLE epoch %4")
                        .arg(new Date(Number(modelData.aos) * 1000).toISOString())
                        .arg(new Date(Number(modelData.los) * 1000).toISOString())
                        .arg(modelData.maxElevation)
                        .arg(new Date(modelData.tleEpoch).toISOString())
                }
                GlassButton {
                    id: selectPassButton
                    text: root.passValid && Number(root.plannedPass.aos) === Number(modelData.aos) ? qsTr("Selected") : qsTr("Select")
                    Accessible.name: qsTr("Select pass %1").arg(index + 1)
                    enabled: !root.schedule.busy && !root.planner.busy && !root.tracker.busy
                    onClicked: root.selectSchedulePass(index)
                }
            }
        }
        Text {
            id: passSummary
            Layout.fillWidth: true
            visible: root.passValid
            text: root.passValid ? qsTr("Selected pass UTC: %1 – %2 · maximum sampled elevation %3°")
                  .arg(new Date(Number(root.plannedPass.aos) * 1000).toISOString())
                  .arg(new Date(Number(root.plannedPass.los) * 1000).toISOString())
                  .arg(root.plannedPass.maxElevation) : ""
            color: Theme.textPrimary
            wrapMode: Text.Wrap
        }
        Text {
            Layout.fillWidth: true
            visible: root.passValid
            text: qsTr("Prediction TLE epoch: %1").arg(isFinite(root.passEpochMs) ? new Date(root.passEpochMs).toISOString() : "—")
                  + (root.passAgeDays < 0 || root.passAgeDays > 14 ? " · " + qsTr("Orbital data may be stale; verify before use.") : "")
            color: root.passAgeDays < 0 || root.passAgeDays > 14 ? Theme.warningColor : Theme.textSecondary
            wrapMode: Text.Wrap
        }
        Text { id: status; Layout.fillWidth: true; text: root.tracker.busy ? qsTr("Reading…") : ""; color: Theme.textSecondary; wrapMode: Text.Wrap }
        Text { id: satelliteStatus; visible: root.resultValid && !!root.sample.satellite; text: qsTr("Satellite: %1").arg(root.sample.satellite || ""); color: Theme.textPrimary; font.bold: true }
        Text { visible: root.resultValid && !!root.sample.satellite; text: qsTr("Azimuth: %1° · Elevation: %2°").arg(root.sample.azimuth).arg(root.sample.elevation); color: Theme.textPrimary }
        Text { visible: root.resultValid && !!root.sample.satellite; text: qsTr("Next AOS/LOS (Unix UTC): %1").arg(root.sample.nextEventUnix); color: Theme.textSecondary }
        Text { visible: root.resultValid && !!root.sample.satellite; text: qsTr("Doppler at 100 MHz: %1 Hz").arg(root.sample.doppler100MHz); color: Theme.textSecondary }
        Text { visible: root.resultValid && !!root.sample.satellite; text: qsTr("Received (UTC): %1").arg(root.sample.receivedAt ? new Date(root.sample.receivedAt).toISOString() : "—"); color: Theme.textSecondary }
        Text { visible: root.resultValid && !!root.sample.satellite; text: qsTr("TLE epoch (UTC): %1").arg(isFinite(root.epochMs) ? new Date(root.epochMs).toISOString() : "—"); color: Theme.textSecondary }
        Text {
            id: staleNotice
            Layout.fillWidth: true
            visible: root.resultValid && !!root.sample.satellite
            text: !isFinite(root.ageDays) ? qsTr("TLE age unavailable") : root.ageDays < 0 ? qsTr("TLE epoch is in the future; check clocks.") : root.ageDays > 14 ? qsTr("TLE is %1 days old; update orbital data before use.").arg(Math.floor(root.ageDays)) : qsTr("TLE age: %1 days (check suitability for this satellite).").arg(Math.floor(root.ageDays))
            color: !isFinite(root.ageDays) || root.ageDays < 0 || root.ageDays > 14 ? Theme.warningColor : Theme.textSecondary
            wrapMode: Text.Wrap
        }
        Text { Layout.fillWidth: true; text: qsTr("A received snapshot does not guarantee current tracking. PREDICT must be running and its orbital data must be maintained."); color: Theme.textSecondary; wrapMode: Text.Wrap }
        Item { Layout.fillHeight: true }
        }
    }
}
