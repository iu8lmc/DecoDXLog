// DecoDXLog — la barra di stato: collegamento, pillole dei servizi, totali, tema.
import QtQuick
import QtQuick.Layouts
import Decodium.UI

Rectangle {
    id: rail
    implicitHeight: 46
    // Il pulsante dell'orologio mondiale, per ridargli il focus.
    readonly property alias clockButton: clockButton
    signal worldClockRequested()
    color: Theme.bgMedium

    Rectangle {
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 1
        color: Theme.borderSoft
    }

    component Separator: Text {
        text: "|"
        color: Theme.textSecondary
        font.family: Theme.monoFamily
        font.pixelSize: 12
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 14

        Row {
            spacing: 6
            Led {
                anchors.verticalCenter: parent.verticalCenter
                color: decolog.clientConnected ? Theme.accentColor : decolog.listening ? Theme.textSecondary : Theme.errorColor
            }
            Text {
                text: decolog.clientConnected ? qsTr("%1: connected").arg(decolog.clientName)
                     : decolog.listening ? qsTr("Decodium: waiting") : qsTr("UDP closed")
                color: Theme.textPrimary
                font.family: Theme.monoFamily
                font.pixelSize: 12
            }
        }
        Separator {}
        Pill {
            text: "UDP " + decolog.udpPort
            tone: decolog.listening ? Theme.accentColor : Theme.errorColor
        }
        Pill {
            text: "LINK"
            tone: decolog.decoLinkClients.length ? Theme.accentColor
                : decolog.decoLinkEnabled && decolog.decoLinkListening ? Theme.textSecondary
                : decolog.decoLinkEnabled ? Theme.errorColor : Theme.textSecondary
            opacity: decolog.decoLinkEnabled ? 1.0 : 0.5
        }
        Pill {
            readonly property int queued: decolog.cloud.queued
            text: decolog.cloud.linked && queued > 0 ? "SYNC " + queued : "SYNC"
            tone: decolog.cloud.busy ? Theme.primaryColor
                : queued > 0 ? Theme.warningColor
                : decolog.cloud.linked ? Theme.accentColor
                : decolog.cloud.server.length ? Theme.secondaryColor : Theme.textSecondary
        }
        Pill {
            readonly property int queued: decolog.qslSummary.length ? decolog.qslSummary[0].queued || 0 : 0
            text: queued > 0 ? "LOTW " + queued : "LOTW"
            tone: queued > 0 ? Theme.warningColor : Theme.textSecondary
        }
        Separator {}
        StatusItem { label: qsTr("QSO:"); value: decolog.qsoCount.toLocaleString(Qt.locale("en_US"), "f", 0) }
        StatusItem {
            label: qsTr("Queue:")
            value: decolog.dirtyCount.toLocaleString(Qt.locale("en_US"), "f", 0)
            valueColor: decolog.dirtyCount > 0 ? Theme.warningColor : Theme.textPrimary
        }
        StatusItem {
            label: qsTr("Last backup:")
            value: decolog.lastBackup.length ? decolog.lastBackup : qsTr("never")
            valueColor: decolog.lastBackup.length ? Theme.textPrimary : Theme.warningColor
            boldValue: false
        }
        Item { Layout.fillWidth: true }
        // Il cluster sul web: decowebcluster.ft2.it nel browser.
        WebClusterButton {}
        WorldClockButton {
            id: clockButton
            onClicked: rail.worldClockRequested()
        }
    }
}
