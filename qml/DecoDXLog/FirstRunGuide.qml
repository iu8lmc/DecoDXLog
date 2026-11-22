// DecoDXLog — percorso breve per la prima radio e il primo QSO.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decodium.UI

DialogFrame {
    id: root
    signal setupRequested()
    signal newQsoRequested()
    title: qsTr("Start with DecoDXLog")
    dotColor: Theme.accentColor
    dialogKey: "first-run-guide"
    width: 650
    height: 470

    body: ColumnLayout {
        anchors.margins: 16
        spacing: 12
        Text { Layout.fillWidth: true; text: qsTr("A short path to your first contact"); color: Theme.textPrimary; font.pixelSize: 20; font.bold: true }
        Text { Layout.fillWidth: true; text: qsTr("You can use DecoDXLog without a radio. Configure CAT only if you want the log to read the frequency and mode automatically."); color: Theme.textSecondary; wrapMode: Text.Wrap; font.pixelSize: 12 }
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true; color: Theme.bgDeep; border.color: Theme.borderSoft; radius: 6
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 14; spacing: 10
                Text { Layout.fillWidth: true; text: qsTr("1  Station profile"); color: Theme.textPrimary; font.bold: true }
                Text { Layout.fillWidth: true; text: qsTr("Set your callsign, grid and station details in the profile dialog."); color: Theme.textSecondary; wrapMode: Text.Wrap; font.pixelSize: 12 }
                Text { Layout.fillWidth: true; text: qsTr("2  Radio (optional)"); color: Theme.textPrimary; font.bold: true }
                Text { Layout.fillWidth: true; text: qsTr("In Setup → Radio (CAT), choose the model and connection, then use Find the radio. For a shared CAT connection, enter its host and port instead of opening a second serial connection."); color: Theme.textSecondary; wrapMode: Text.Wrap; font.pixelSize: 12 }
                Text { Layout.fillWidth: true; text: qsTr("3  First QSO"); color: Theme.textPrimary; font.bold: true }
                Text { Layout.fillWidth: true; text: qsTr("Open New QSO, enter a callsign and the required fields, then click Log QSO. The local log remains usable if CAT or an online service is unavailable."); color: Theme.textSecondary; wrapMode: Text.Wrap; font.pixelSize: 12 }
                Item { Layout.fillHeight: true }
            }
        }
        Text { Layout.fillWidth: true; text: qsTr("This guide appears once when the log is empty; the normal setup and New QSO shortcuts remain available afterwards."); color: Theme.textSecondary; font.pixelSize: 11; wrapMode: Text.Wrap }
        Flow {
            Layout.fillWidth: true; spacing: 8
            GlassButton {
                id: configureRadioButton
                text: qsTr("Configure radio")
                tone: Theme.accentColor
                focus: true
                activeFocusOnTab: true
                KeyNavigation.tab: newQsoButton
                Accessible.name: qsTr("Configure radio")
                Accessible.description: qsTr("Open radio and CAT setup")
                onClicked: root.setupRequested()
            }
            GlassButton {
                id: newQsoButton
                text: qsTr("Open New QSO")
                tone: Theme.accentColor
                activeFocusOnTab: true
                KeyNavigation.tab: closeButton
                Accessible.name: qsTr("Open New QSO")
                Accessible.description: qsTr("Open the new QSO form")
                onClicked: root.newQsoRequested()
            }
            Item { width: 1; height: 1 }
            GlassButton {
                id: closeButton
                text: qsTr("Close")
                activeFocusOnTab: true
                KeyNavigation.tab: configureRadioButton
                Accessible.name: qsTr("Close")
                Accessible.description: qsTr("Close the first-run guide")
                onClicked: root.close()
            }
        }
    }
}
