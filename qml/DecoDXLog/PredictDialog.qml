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
    readonly property var sample: tracker.snapshot
    property var correction: ({})
    property bool probing: false
    property bool probingList: false
    property bool probingLive: false
    property bool probingPass: false
    property bool probingDraft: false
    property int probePassCount: 0
    property int probeLiveSuccesses: 0
    property bool listValid: false
    property string listHost: ""
    property string listPort: ""
    property bool resultValid: false
    property bool discardPending: false
    property bool passValid: false
    readonly property var plannedPass: planner.pass
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
    function probeDraft(host, port, name) {
        probingDraft = true
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
        correction = root.tracker.correctedFrequencies(Number(uplinkField.text.replace(",", ".")), Number(downlinkField.text.replace(",", ".")))
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
        target: root.planner
        function onFinished(success, error) {
            root.passValid = success && root.visible
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
                    prepareDraftButton.clicked()
                    console.warn("PROBE predictDraft visible=" + satelliteQso.visible + " tx=" + satelliteQso.initialTxForProbe
                                 + " rx=" + satelliteQso.initialRxForProbe + " sat=" + satelliteQso.initialSatelliteForProbe)
                    prepareDraftButton.clicked()
                    console.warn("PROBE predictDraftPreserved=" + satelliteQso.visible + " notice=" + (correctionStatus.text.length > 0))
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
            StyledTextField { id: hostField; text: "127.0.0.1"; placeholderText: qsTr("IP address"); Accessible.name: qsTr("Tracker IP address"); Layout.fillWidth: true; enabled: !root.tracker.busy && !root.planner.busy && !autoRefresh.checked; onTextChanged: { root.listValid = false; root.invalidateSample(); root.passValid = false } }
            StyledTextField { id: portField; text: "1210"; placeholderText: qsTr("Port"); Accessible.name: qsTr("Tracker port"); validator: IntValidator { bottom: 1; top: 65535 } Layout.preferredWidth: 90; enabled: !root.tracker.busy && !root.planner.busy && !autoRefresh.checked; onTextChanged: { root.listValid = false; root.invalidateSample(); root.passValid = false } }
            StyledTextField { id: satelliteField; placeholderText: qsTr("Exact satellite name"); Accessible.name: qsTr("Exact satellite name"); Layout.fillWidth: true; enabled: !root.tracker.busy && !root.planner.busy && !autoRefresh.checked; onTextChanged: { root.invalidateSample(); root.passValid = false } }
            GlassButton {
                id: requestButton
                text: qsTr("Read snapshot")
                enabled: !root.tracker.busy && !root.planner.busy && !autoRefresh.checked && portField.acceptableInput
                onClicked: root.refreshSnapshot()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            GlassButton {
                id: listButton
                text: qsTr("Load satellites")
                enabled: !root.tracker.busy && !root.planner.busy && !autoRefresh.checked && portField.acceptableInput
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
                enabled: !root.tracker.busy && !root.planner.busy && !autoRefresh.checked
                model: root.listValid ? root.tracker.satellites : []
                onActivated: if (currentIndex >= 0) satelliteField.text = currentText
            }
        }
        CheckBox {
            id: autoRefresh
            text: qsTr("Refresh automatically (2.5 s after each reading)")
            enabled: root.visible && !root.planner.busy && (!root.tracker.busy || checked) && portField.acceptableInput
            onToggled: {
                if (checked) root.refreshSnapshot()
                else refreshTimer.stop()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            StyledTextField { id: uplinkField; Layout.fillWidth: true; placeholderText: qsTr("Nominal uplink TX MHz"); Accessible.name: qsTr("Nominal uplink TX MHz"); onTextChanged: { root.correction = ({}); if (correctionStatus) correctionStatus.text = "" } }
            StyledTextField { id: downlinkField; Layout.fillWidth: true; placeholderText: qsTr("Nominal downlink RX MHz"); Accessible.name: qsTr("Nominal downlink RX MHz"); onTextChanged: { root.correction = ({}); if (correctionStatus) correctionStatus.text = "" } }
            GlassButton { text: qsTr("Preview Doppler"); enabled: root.resultValid && !root.tracker.busy; onClicked: { root.calculateCorrection(); correctionStatus.text = root.correction.ok ? qsTr("Corrected TX %1 MHz · RX %2 MHz").arg(Number(root.correction.txMHz).toFixed(6)).arg(Number(root.correction.rxMHz).toFixed(6)) : root.correction.error } }
            GlassButton { id: prepareDraftButton; text: qsTr("Prepare QSO…"); enabled: root.resultValid && !root.tracker.busy; onClicked: root.prepareDraft() }
        }
        Text { id: correctionStatus; Layout.fillWidth: true; color: Theme.textSecondary; wrapMode: Text.Wrap }
        RowLayout {
            Layout.fillWidth: true
            GlassButton { id: firstPassButton; text: qsTr("Predict next pass"); enabled: !root.tracker.busy && !root.planner.busy && portField.acceptableInput; onClicked: root.requestPass(Math.floor(Date.now() / 1000)) }
            GlassButton { id: nextPassButton; text: qsTr("Following pass"); enabled: root.passValid && !root.tracker.busy && !root.planner.busy; onClicked: root.requestPass(Number(root.plannedPass.los) + 60) }
            GlassButton { text: qsTr("Cancel prediction"); visible: root.planner.busy; onClicked: root.planner.cancel() }
        }
        Text {
            id: passSummary
            Layout.fillWidth: true
            visible: root.passValid
            text: root.passValid ? qsTr("Pass UTC: %1 – %2 · maximum sampled elevation %3°")
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
