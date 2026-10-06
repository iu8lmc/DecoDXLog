// DecoDXLog — la rete della stazione multi-operatore: accesa o spenta, chi
// c'e' e su che banda, i messaggi fra operatori e gli spot interni. I QSO
// viaggiano da soli: fatto qui, arriva negli altri log, e viceversa.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decodium.UI

GlassPanel {
    id: root

    readonly property var net: decolog.net

    title: qsTr("Station network")
    dotColor: root.net.running ? Theme.accentColor : Theme.textSecondary
    padding: 8

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            ToggleSwitch {
                text: qsTr("On")
                checked: root.net.enabled
                onToggled: root.net.enabled = checked
            }
            LabeledField {
                label: qsTr("Network")
                StyledTextField {
                    Layout.preferredWidth: 110
                    text: root.net.group
                    uppercase: true
                    onEditingFinished: root.net.group = text
                }
            }
            LabeledField {
                label: qsTr("This station")
                StyledTextField {
                    Layout.preferredWidth: 110
                    text: root.net.stationName
                    onEditingFinished: root.net.stationName = text
                }
            }
            LabeledField {
                label: qsTr("UDP port")
                StyledTextField {
                    Layout.preferredWidth: 70
                    text: String(root.net.port)
                    onEditingFinished: root.net.port = parseInt(text) || 12060
                }
            }
            Item { Layout.fillWidth: true }
            GlassButton {
                Layout.alignment: Qt.AlignBottom
                text: qsTr("Sync")
                enabled: root.net.running
                onClicked: root.net.requestSync()
            }
        }
        Text {
            Layout.fillWidth: true
            elide: Text.ElideRight
            text: root.net.status + (root.net.running ? qsTr(" · sent %1 · received %2").arg(root.net.sent).arg(root.net.received) : "")
            color: Theme.textSecondary
            font.family: Theme.monoFamily
            font.pixelSize: 11
        }

        // ── Chi c'e' ────────────────────────────────────────────────────────
        Repeater {
            model: root.net.peers
            RowLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 8
                Led { color: modelData.age < 12 ? Theme.accentColor : Theme.warningColor }
                Text { text: modelData.name; color: Theme.textPrimary; font.family: Theme.monoFamily; font.pixelSize: 12; font.bold: true }
                Text { text: modelData.op; color: Theme.textSecondary; font.family: Theme.monoFamily; font.pixelSize: 11 }
                Text {
                    text: [modelData.band, modelData.mode, modelData.khz > 0 ? modelData.khz.toFixed(1) : ""].filter(s => s).join(" ")
                    color: Theme.primaryColor
                    font.family: Theme.monoFamily
                    font.pixelSize: 11
                }
                Item { Layout.fillWidth: true }
                Text { text: qsTr("%n QSO", "", modelData.qsos); color: Theme.textSecondary; font.family: Theme.monoFamily; font.pixelSize: 11 }
            }
        }
        Text {
            visible: root.net.running && root.net.peers.length === 0
            text: qsTr("Nobody else on the network yet.")
            color: Theme.textSecondary
            font.pixelSize: 11
        }

        // ── Messaggi ────────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 60
            radius: 4
            color: Theme.bgDeep
            border.color: Theme.borderSoft
            ListView {
                id: gabList
                anchors.fill: parent
                anchors.margins: 4
                clip: true
                model: root.net.messages
                onCountChanged: gabScrollTimer.restart()
                ScrollBar.vertical: PanelScrollBar {}
                delegate: Text {
                    required property var modelData
                    width: gabList.width - 10
                    wrapMode: Text.Wrap
                    text: modelData.time + "  " + modelData.from + (modelData.to ? " → " + modelData.to : "") + ": " + modelData.text
                    color: modelData.mine ? Theme.secondaryColor : Theme.textPrimary
                    font.family: Theme.monoFamily
                    font.pixelSize: 11
                }
            }
        }
        // Se il pannello viene chiuso mentre arriva un messaggio, il Timer e'
        // distrutto insieme alla ListView e non rimane una funzione QML in
        // attesa su Qt.callLater.
        Timer {
            id: gabScrollTimer
            interval: 0
            onTriggered: gabList.positionViewAtEnd()
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            StyledTextField {
                id: gabInput
                Layout.fillWidth: true
                enabled: root.net.running
                mono: false
                placeholderText: qsTr("Message to the other operators")
                onAccepted: { root.net.sendGab(text); text = "" }
            }
            GlassButton {
                text: qsTr("Send")
                enabled: root.net.running && gabInput.text.trim().length > 0
                onClicked: { root.net.sendGab(gabInput.text); gabInput.text = "" }
            }
            // Lo spot interno: il nominativo della scheda sulla frequenza della radio.
            GlassButton {
                text: qsTr("Spot %1").arg(decolog.callInfo.call || "")
                tone: Theme.warningColor
                enabled: root.net.running && (decolog.callInfo.call || "").length > 0 && parseFloat(decolog.shownFrequency || "0") > 0
                onClicked: root.net.sendSpot(decolog.callInfo.call, parseFloat(decolog.shownFrequency) * 1000, "")
            }
        }
    }
}
