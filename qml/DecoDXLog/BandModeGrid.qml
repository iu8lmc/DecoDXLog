// DecoDXLog — la griglia banda x modo, come la tiene appesa al muro chi caccia
// il DX: una riga per CW, digitale e fonia, una colonna per banda.
//
// Casella vuota: mai lavorato. Casella accesa: lavorato. Casella piena e verde:
// confermato, e le lettere dicono da dove — L LoTW, e eQSL, C Club Log, Q QRZ,
// K la cartolina.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decodium.UI

ColumnLayout {
    id: root

    // La mappa che arriva dal controller: { bands: [...], rows: [{label, cells}] }
    property var grid: ({})
    signal slotClicked(string band, string group)
    readonly property var bandList: (grid && grid.bands) || []
    readonly property var rowList: (grid && grid.rows) || []

    // La colonna delle etichette e' larga quanto la parola piu' lunga — in
    // tedesco "Sprechfunk", in italiano "Digitale" — ma non oltre, che lo spazio
    // che prende lo toglie alle caselle.
    readonly property int labelWidth: {
        let w = 24
        // FontMetrics misura senza cambiare niente: con TextMetrics si scriveva
        // il testo dentro il calcolo stesso, e il calcolo ripartiva da capo a
        // ogni nominativo scelto.
        for (let i = 0; i < rowList.length; ++i)
            w = Math.max(w, metrics.advanceWidth(rowList[i].label || ""))
        return Math.min(56, Math.ceil(w) + 3)
    }

    FontMetrics {
        id: metrics
        font.family: Theme.monoFamily
        font.pixelSize: 9
    }

    spacing: 2

    // Intestazione: il nome della banda senza la "m", che li' lo spazio non c'e'.
    RowLayout {
        Layout.fillWidth: true
        spacing: 2
        Item { Layout.preferredWidth: root.labelWidth }
        Repeater {
            model: root.bandList
            Text {
                required property string modelData
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                horizontalAlignment: Text.AlignHCenter
                // Senza la "m" finale ci stanno tutte: 160, 80, 40 e, per i
                // centimetri, 70c e 23c — che con le bande in metri non si
                // confondono.
                text: modelData.endsWith("m") ? modelData.slice(0, -1) : modelData
                color: Theme.textSecondary
                font.family: Theme.monoFamily
                font.pixelSize: 9
                elide: Text.ElideNone
            }
        }
    }

    Repeater {
        model: root.rowList
        RowLayout {
            required property var modelData
            readonly property string modeGroup: modelData.group
            Layout.fillWidth: true
            spacing: 2
            Text {
                Layout.preferredWidth: root.labelWidth
                text: modelData.label
                color: Theme.textSecondary
                font.family: Theme.monoFamily
                font.pixelSize: 9
                elide: Text.ElideRight
            }
            Repeater {
                model: modelData.cells
                Rectangle {
                    required property var modelData
                    readonly property bool worked: (modelData.count || 0) > 0
                    readonly property bool confirmed: modelData.confirmed === true
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    implicitHeight: 15
                    radius: 2
                    color: confirmed ? Theme.successColor
                         : worked ? Qt.rgba(Theme.warningColor.r, Theme.warningColor.g, Theme.warningColor.b, 0.35)
                         : "transparent"
                    border.width: 1
                    border.color: worked ? (confirmed ? Theme.successColor : Theme.warningColor) : Theme.borderSoft
                    Text {
                        anchors.centerIn: parent
                        width: parent.width - 2
                        horizontalAlignment: Text.AlignHCenter
                        // Sulla casella confermata ci stanno poche lettere: si
                        // mostrano quelle che ci stanno, il resto lo dice il fumetto.
                        text: confirmed ? (modelData.marks || "") : (worked ? "·" : "")
                        color: confirmed ? Theme.bgDeep : Theme.warningColor
                        font.family: Theme.monoFamily
                        font.pixelSize: 9
                        font.bold: confirmed
                        elide: Text.ElideRight
                    }
                    HoverHandler { id: hover }
                    TapHandler { enabled: worked; onTapped: root.slotClicked(modelData.band, parent.parent.modeGroup) }
                    ToolTip.visible: hover.hovered && worked
                    ToolTip.text: qsTr("%1 · %n QSO", "", modelData.count || 0).arg(modelData.band)
                                  + (confirmed ? " · " + qsTr("confirmed: %1").arg(modelData.marks) : "")
                }
            }
        }
    }
}
