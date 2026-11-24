// DecoDXLog — punteggio e moltiplicatori del contest, in un pannello suo.
//
// Punti, moltiplicatori e totale secondo il regolamento della gara, e sotto la
// riga delle bande: nei contest i moltiplicatori si contano per banda, e sapere
// dove mancano e' quello che dice dove andare.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decodium.UI

GlassPanel {
    id: root

    property int revision: 0
    readonly property var act: decolog.activation
    readonly property var scoring: { revision; return act.score() }
    property string band: ""
    function probeUnsupportedContest() {
        console.warn("PROBE contestUnsupported valid=" + root.scoring.valid + " warning=" + unsupportedRules.visible + " text=" + unsupportedRules.text)
    }

    Connections {
        target: decolog
        function onLogChanged() { root.revision++ }
    }
    Connections {
        target: decolog.activation
        function onChanged() { root.revision++ }
    }

    // Cabrillo e ADIF stanno nella finestra unica del contest: di qui ci si
    // arriva, invece di tenersi aperta anche quella per due pulsanti.
    signal contestRequested()

    title: qsTr("Score")
    dotColor: root.scoring.valid ? Theme.accentColor : Theme.textSecondary

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        // Il contest che non ha una scheda non ha punteggio, e si dice:
        // un numero inventato sarebbe peggio di nessun numero.
        Text {
            id: unsupportedRules
            Layout.fillWidth: true
            visible: !root.scoring.valid
            wrapMode: Text.Wrap
            text: root.scoring.contestId
                  ? qsTr("The rules of %1 are not in the program: QSO remain in the log, but scoring and Cabrillo export require a verified rule sheet.")
                        .arg(root.scoring.contestId)
                  : qsTr("No contest session open.")
            color: Theme.textSecondary
            font.pixelSize: 12
        }

        RowLayout {
            Layout.fillWidth: true
            visible: root.scoring.valid
            spacing: 8
            Repeater {
                model: [
                    { label: qsTr("Points"), value: String(root.scoring.points || 0), tone: Theme.primaryColor },
                    { label: qsTr("Mult"), value: String(root.scoring.multipliers || 0), tone: Theme.warningColor },
                    { label: qsTr("Score"), value: String(root.scoring.score || 0), tone: Theme.accentColor },
                ]
                Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: 62
                    radius: 5
                    color: Theme.bgDeep
                    border.color: Theme.borderSoft
                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 2
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: modelData.label
                            color: Theme.textSecondary
                            font.family: Theme.monoFamily
                            font.pixelSize: 10
                        }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: modelData.value
                            color: modelData.tone
                            font.family: Theme.monoFamily
                            font.pixelSize: 24
                            font.bold: true
                        }
                    }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: root.scoring.valid
            text: qsTr("Band by band · QSO, points, multipliers")
            color: Theme.textSecondary
            font.family: Theme.monoFamily
            font.pixelSize: 11
        }

        // Le bande in colonna: il pannello e' stretto, e una riga sola le
        // taglierebbe.
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.scoring.valid
            clip: true
            model: root.scoring.bands || []
            spacing: 2
            ScrollBar.vertical: PanelScrollBar {}
            delegate: Rectangle {
                required property var modelData
                width: ListView.view.width
                height: 30
                radius: 4
                color: modelData.band === root.band ? Theme.rowMatchBg : "transparent"
                border.color: modelData.band === root.band ? Theme.accentColor : Theme.borderSoft

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 8
                    Text {
                        Layout.preferredWidth: 52
                        text: modelData.band
                        color: Theme.textPrimary
                        font.family: Theme.monoFamily
                        font.pixelSize: 12
                        font.bold: true
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("%1 QSO").arg(modelData.qsos || 0)
                        color: Theme.textSecondary
                        font.pixelSize: 11
                    }
                    Text {
                        text: String(modelData.points || 0)
                        color: Theme.primaryColor
                        font.family: Theme.monoFamily
                        font.pixelSize: 12
                    }
                    Text {
                        Layout.preferredWidth: 40
                        horizontalAlignment: Text.AlignRight
                        text: String(modelData.multipliers || 0)
                        color: Theme.warningColor
                        font.family: Theme.monoFamily
                        font.pixelSize: 12
                        font.bold: true
                    }
                }
            }
        }

        // Il tempo in aria: il WPX da singolo ne concede 36 ore su 48, e una
        // pausa conta solo se dura almeno un'ora.
        Text {
            Layout.fillWidth: true
            visible: root.scoring.valid && (root.scoring.onMinutes || 0) > 0
            readonly property string on: Math.floor((root.scoring.onMinutes || 0) / 60) + ":"
                                         + String((root.scoring.onMinutes || 0) % 60).padStart(2, "0")
            readonly property string max: Math.floor((root.scoring.maxMinutes || 0) / 60) + ":00"
            text: (root.scoring.maxMinutes || 0) > 0
                  ? qsTr("On the air %1 of %2 · %n break(s)", "", root.scoring.breaks || 0).arg(on).arg(max)
                  : qsTr("On the air %1 · %n break(s)", "", root.scoring.breaks || 0).arg(on)
            color: root.scoring.overTime ? Theme.errorColor : Theme.textSecondary
            font.family: Theme.monoFamily
            font.pixelSize: 11
            wrapMode: Text.Wrap
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            GlassButton {
                text: qsTr("Export…")
                buttonHeight: 24
                onClicked: root.contestRequested()
            }
            Item { Layout.fillWidth: true }
        }

        Text {
            Layout.fillWidth: true
            visible: root.scoring.valid && (root.scoring.source || "").length > 0
            text: qsTr("Rules: %1").arg(root.scoring.source)
            color: Theme.textDim !== undefined ? Theme.textDim : Theme.textSecondary
            wrapMode: Text.Wrap
            font.pixelSize: 10
        }
    }
}
