// DecoDXLog — il rotore nella colonna di destra: il quadrante grande al
// centro, i gradi e "Punta il DX". Nient'altro: passi, STOP, park e il resto
// stanno nella finestra del rotore (Apri ▾, Ctrl+R).
//
// Il DX e' quello scelto adesso: uno spot cliccato nel cluster o il
// nominativo nella scheda. La sua rotta arriva da sola, e il pulsante la dice.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decodium.UI

GlassPanel {
    id: root

    signal windowRequested()

    readonly property var rotor: decolog.rotor
    readonly property var state: rotor.state
    readonly property var home: decolog.myPosition
    readonly property var dx: decolog.callInfo.position
    readonly property var dxTarget: rotor.dxTarget
    readonly property bool hasDx: dxTarget && dxTarget.azimuth !== undefined

    title: qsTr("Rotor")
    dotColor: state.connected ? (state.moving ? Theme.warningColor : Theme.accentColor) : Theme.errorColor
    padding: 8
    headerTools: [
        GlassButton {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Open ▾")
            buttonHeight: 20
            fontPixelSize: 10
            onClicked: root.windowRequested()
        }
    ]

    implicitHeight: 300

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        // Il quadrante: grande quanto il pannello lascia, sempre al centro.
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 120

            RotorDial {
                readonly property real side: Math.max(110, Math.min(parent.width, parent.height))
                width: side
                height: side
                anchors.centerIn: parent

                azimuth: root.state.az || 0
                target: root.state.azTarget !== undefined && root.state.azTarget >= 0 ? root.state.azTarget : -1
                beamwidth: root.state.beamwidth || 45
                hasPosition: root.state.connected === true
                moving: root.state.moving === true
                latitude: root.home && root.home.lat !== undefined ? root.home.lat : 41.5
                longitude: root.home && root.home.lon !== undefined ? root.home.lon : 12.5
                pinValid: root.dx !== undefined && root.dx !== null && root.dx.lat !== undefined
                pinLatitude: pinValid ? root.dx.lat : 0
                pinLongitude: pinValid ? root.dx.lon : 0

                onBearingRequested: (degrees) => root.rotor.pointTo(degrees, "")
            }
        }

        // I gradi, grandi, e dove sta andando.
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 8
            Text {
                text: root.state.connected ? Math.round(root.state.az || 0) + "°" : "—"
                color: root.state.connected ? Theme.textPrimary : Theme.warningColor
                font.family: Theme.monoFamily
                font.pixelSize: 26
                font.bold: true
            }
            Text {
                visible: (root.state.azTarget || -1) >= 0
                text: "→ " + Math.round(root.state.azTarget || 0) + "°"
                color: Theme.warningColor
                font.family: Theme.monoFamily
                font.pixelSize: 15
            }
        }

        GlassButton {
            Layout.fillWidth: true
            text: root.hasDx ? qsTr("Point to the DX · %1 %2°").arg(root.dxTarget.call).arg(root.dxTarget.azimuth)
                             : qsTr("Point to the DX")
            tone: Theme.primaryColor
            filled: root.hasDx
            buttonHeight: 26
            fontPixelSize: 12
            enabled: root.state.connected && root.hasDx
            onClicked: root.rotor.pointToDx()
            ToolTip.visible: hovered && !root.hasDx
            ToolTip.text: qsTr("Pick a spot in the cluster, or a call: its bearing comes here by itself")
        }
    }
}
