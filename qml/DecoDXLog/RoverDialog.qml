import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decodium.UI

DialogFrame {
    id: root
    title: qsTr("Rover location history")
    dialogKey: "rover-history"
    width: 760
    height: 560
    dotColor: Theme.secondaryColor
    readonly property var rover: decolog.rover
    readonly property var active: rover.session
    info: qsTr("Manual QSO locations; no radio control")
    property bool probing: false
    NewQsoDialog { id: probeQso; profileDialogKey: "rover-probe-qso" }
    function probe() {
        probing = true
        titleField.text = "Rover fixture"
        gridField.text = "JN71DC"
        startButton.clicked()
        console.warn("PROBE roverStarted=" + !!root.active.id + " grid=" + root.active.grid)
        probeQso.open()
        probeQso.probeRoverLog("K1AAA", "")
        console.warn("PROBE roverFirst count=" + decolog.currentLogInfo().qsos + " closed=" + !probeQso.visible)
        gridField.text = "JM67AB"
        positionButton.clicked()
        probeQso.open()
        probeQso.probeRoverLog("K1BBB", "")
        console.warn("PROBE roverAmbiguous kept=" + probeQso.visible + " count=" + decolog.currentLogInfo().qsos)
        probeQso.probeRoverLog("K1BBB", "JM67AB")
        console.warn("PROBE roverOverride closed=" + !probeQso.visible + " count=" + decolog.currentLogInfo().qsos)
        const activationError = decolog.activation.start({kind: "free", name: "Fixture", myGrid: "JN50"})
        probeQso.open()
        probeQso.probeRoverLog("K1CCC", "")
        console.warn("PROBE roverActivation error=" + activationError + " kept=" + probeQso.visible + " count=" + decolog.currentLogInfo().qsos)
        probeQso.probeRoverLog("K1CCC", "JM67AB")
        console.warn("PROBE roverActivationOverride closed=" + !probeQso.visible + " count=" + decolog.currentLogInfo().qsos)
        decolog.activation.stop()
    }
    body: ColumnLayout {
        anchors.margins: 16
        spacing: 10
        Text { Layout.fillWidth: true; text: qsTr("A session records locator changes. New manual QSO use the position for their UTC minute. Other incoming QSO are not changed."); color: Theme.textSecondary; wrapMode: Text.Wrap }
        Text { Layout.fillWidth: true; text: qsTr("If positions change in the same minute, or an activation is active, enter My grid explicitly in the full QSO dialog."); color: Theme.warningColor; wrapMode: Text.Wrap }
        RowLayout {
            Layout.fillWidth: true
            StyledTextField { id: titleField; Layout.fillWidth: true; placeholderText: qsTr("New rover session name"); Accessible.name: qsTr("New rover session name"); enabled: !root.active.id }
            StyledTextField { id: gridField; Layout.preferredWidth: 160; placeholderText: qsTr("Station locator"); Accessible.name: qsTr("Rover station locator"); uppercase: true }
            GlassButton { id: startButton; text: qsTr("Start rover"); enabled: !root.active.id; onClicked: root.rover.start(titleField.text, gridField.text, decolog.stationProfiles.activeProfileId) }
            GlassButton { id: positionButton; text: qsTr("Record position"); enabled: !!root.active.id; onClicked: root.rover.setPosition(gridField.text) }
            GlassButton { text: qsTr("Close rover"); enabled: !!root.active.id; onClicked: root.rover.close() }
        }
        Text { Layout.fillWidth: true; text: root.active.id ? qsTr("Active: %1 · current locator: %2").arg(root.active.title).arg(root.active.grid) : qsTr("No active rover session"); color: Theme.textPrimary; wrapMode: Text.Wrap }
        Text { Layout.fillWidth: true; visible: root.rover.error.length > 0; text: root.rover.error; color: Theme.warningColor; wrapMode: Text.Wrap }
        StyledComboBox {
            id: sessionChoices
            Layout.fillWidth: true
            Accessible.name: qsTr("Rover session history")
            readonly property var rows: root.rover.sessions
            model: rows.map(s => s.title + (s.active ? " ●" : ""))
            currentIndex: Math.max(0, rows.findIndex(s => s.id === root.rover.selectedId))
            onActivated: if (currentIndex >= 0) root.rover.selectHistory(rows[currentIndex].id)
        }
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.rover.positions
            ScrollBar.vertical: ScrollBar { }
            delegate: Text {
                required property var modelData
                width: ListView.view.width
                text: modelData.effectiveAt + " UTC · " + modelData.grid
                color: Theme.textSecondary
                elide: Text.ElideRight
            }
        }
    }
}
