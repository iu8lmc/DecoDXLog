import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decodium.UI

DialogFrame {
    id: root
    title: qsTr("PREDICT satellite tracker")
    dialogKey: "predict-tracker"
    width: 720
    height: 480
    dotColor: Theme.secondaryColor
    info: qsTr("Read-only: the tracker controls neither radio nor rotor")
    readonly property var tracker: decolog.predictTracker
    readonly property var sample: tracker.snapshot
    property bool probing: false
    property bool probingList: false
    property bool listValid: false
    property string listHost: ""
    property string listPort: ""
    property bool resultValid: false
    readonly property double epochMs: sample.tleEpoch ? new Date(sample.tleEpoch).getTime() : NaN
    readonly property double ageDays: isFinite(epochMs) ? (Date.now() - epochMs) / 86400000 : NaN
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
            root.resultValid = success
            status.text = success ? qsTr("Snapshot received") : qsTr("Tracker error: %1").arg(error)
            if (root.probing) {
                console.warn("PROBE predict success=" + success + " satellite=" + (root.sample.satellite || "")
                             + " old=" + (root.ageDays > 14) + " rendered=" + satelliteStatus.visible
                             + " warning=" + (staleNotice.visible && root.ageDays > 14) + " error=" + error)
                root.probing = false
            }
        }
    }
    body: ColumnLayout {
        anchors.margins: 16
        spacing: 12
        Text { text: qsTr("PREDICT UDP server"); color: Theme.textPrimary; font.bold: true }
        RowLayout {
            Layout.fillWidth: true
            StyledTextField { id: hostField; text: "127.0.0.1"; placeholderText: qsTr("IP address"); Accessible.name: qsTr("Tracker IP address"); Layout.fillWidth: true; enabled: !root.tracker.busy; onTextChanged: { root.listValid = false; root.resultValid = false } }
            StyledTextField { id: portField; text: "1210"; placeholderText: qsTr("Port"); Accessible.name: qsTr("Tracker port"); validator: IntValidator { bottom: 1; top: 65535 } Layout.preferredWidth: 90; enabled: !root.tracker.busy; onTextChanged: { root.listValid = false; root.resultValid = false } }
            StyledTextField { id: satelliteField; placeholderText: qsTr("Exact satellite name"); Accessible.name: qsTr("Exact satellite name"); Layout.fillWidth: true; enabled: !root.tracker.busy; onTextChanged: root.resultValid = false }
            GlassButton {
                id: requestButton
                text: qsTr("Read snapshot")
                enabled: !root.tracker.busy && portField.acceptableInput
                onClicked: {
                    root.resultValid = false
                    status.text = qsTr("Reading…")
                    if (!root.tracker.request(hostField.text.trim(), Number(portField.text), satelliteField.text.trim())) {
                        status.text = qsTr("Enter a numeric IP, port and satellite name (up to 25 characters).")
                        if (root.probing) { console.warn("PROBE predict success=false invalid=true"); root.probing = false }
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            GlassButton {
                id: listButton
                text: qsTr("Load satellites")
                enabled: !root.tracker.busy && portField.acceptableInput
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
                enabled: !root.tracker.busy
                model: root.listValid ? root.tracker.satellites : []
                onActivated: if (currentIndex >= 0) satelliteField.text = currentText
            }
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
