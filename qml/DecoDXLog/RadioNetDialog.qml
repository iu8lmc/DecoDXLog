// Radio round tables: a roster entry does not create a QSO.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decodium.UI

DialogFrame {
    id: root
    readonly property var net: decolog.radioNet
    readonly property var current: net.session
    title: qsTr("Radio net manager")
    dialogKey: "radio-net"
    width: 1000
    height: 720
    dotColor: Theme.secondaryColor
    info: qsTr("Roster entries do not create QSO")
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
        case "turn": return qsTr("Turn")
        case "reorder": return qsTr("Order changed")
        case "status": {
            const labels = {present: qsTr("Present"), waiting: qsTr("Waiting"), absent: qsTr("Absent"), departed: qsTr("Departed")}
            return qsTr("Status: %1").arg(labels[row.detail] || row.detail)
        }
        default: return row.event
        }
    }
    // Exercised only by the explicitly requested isolated --show probe.
    function probe(seed) {
        if (seed) {
            sessionTitle.text = "Test net"
            startButton.clicked()
            callInput.text = "K1AAA"
            nameInput.text = "Alice"
            checkInButton.clicked()
        }
        probeTimer.start()
    }
    Timer {
        id: probeTimer
        interval: 300
        onTriggered: {
            const row = memberList.itemAtIndex(0)
            console.warn("PROBE radioNet visible=" + root.visible + " members=" + root.net.members.length
                         + " first=" + (row ? row.modelData.call : "missing"))
        }
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
        }
        Text {
            Layout.fillWidth: true
            text: root.current.id ? qsTr("Started: %1 UTC").arg(String(root.current.startedAt || "")) : qsTr("Start or select a session.")
            color: Theme.textSecondary
            wrapMode: Text.Wrap
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
                required property var modelData
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
                }
            }
        }
        Text { text: qsTr("Recent session history (latest 200)"); color: Theme.secondaryColor; font.bold: true }
        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: 110
            clip: true
            model: root.net.events
            ScrollBar.vertical: ScrollBar { }
            delegate: Text {
                required property var modelData
                width: ListView.view.width
                text: String(modelData.at) + " · " + (modelData.call || "—") + " · " + root.historyText(modelData)
                color: Theme.textSecondary
                elide: Text.ElideRight
            }
        }
    }
}
