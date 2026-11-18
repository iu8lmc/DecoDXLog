// DecoDXLog — lo stesso valore in un campo di molti QSO.
//
// Dopo un'importazione da un altro programma capita spesso: il locatore di
// casa mancante su mille QSO, il riferimento POTA di un'attivazione scritto
// dopo, le QSL cartacee spedite tutte insieme. Ogni QSO tiene il valore di
// prima nel suo storico: la modifica si disfa QSO per QSO, dalla scheda.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decodium.UI

Popup {
    id: root

    property var ids: []
    property var fields: []
    readonly property var chosen: fieldBox.currentIndex >= 0 && fieldBox.currentIndex < fields.length
                                  ? fields[fieldBox.currentIndex] : null
    readonly property bool hasChoices: chosen !== null && chosen.choices.length > 0

    function openFor(list) {
        root.ids = list
        root.fields = decolog.bulkFields()
        valueField.text = ""
        onlyEmpty.checked = true
        open()
    }

    // Isolated UI regression probe: prepare the real dialog and let the
    // application send a mouse click to the same button used by operators.
    function prepareProbe(list, field, value, onlyEmptyValue) {
        openFor(list)
        fieldBox.currentIndex = root.fields.findIndex(function(f) { return f.field === field })
        valueField.text = value
        onlyEmpty.checked = onlyEmptyValue
    }
    function probeApplyButton() { return applyButton }

    // In una finestra sua il popup riceve i tasti solo se ha il fuoco: senza
    // "focus: true" il campo prende il cursore ma quello che si scrive si perde.
    popupType: Popup.Window
    focus: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 520
    modal: true
    padding: 16
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { color: Theme.panelColor; border.color: Theme.glassBorder; radius: 6 }

    contentItem: ColumnLayout {
        spacing: 10

        Text {
            text: qsTr("CHANGE A FIELD ON %1 QSO").arg(root.ids.length)
            color: Theme.secondaryColor
            font.family: Theme.monoFamily
            font.pixelSize: 11
            font.bold: true
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.textSecondary
            font.pixelSize: 11
            text: qsTr("The same value on every QSO chosen. Each QSO keeps the old value in its history, so the "
                       + "change can be undone QSO by QSO from its card. Leave the value empty to clear the field.")
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 10
            rowSpacing: 8

            Text { text: qsTr("Field"); color: Theme.textSecondary; font.pixelSize: 12 }
            StyledComboBox {
                id: fieldBox
                Layout.fillWidth: true
                model: root.fields
                textRole: "label"
                onActivated: {
                    valueField.text = ""
                    valueBox.currentIndex = 0
                }
            }

            Text { text: qsTr("Value"); color: Theme.textSecondary; font.pixelSize: 12 }
            Item {
                Layout.fillWidth: true
                implicitHeight: Math.max(valueField.implicitHeight, valueBox.implicitHeight)
                StyledTextField {
                    id: valueField
                    anchors.left: parent.left
                    anchors.right: parent.right
                    visible: !root.hasChoices
                    mono: root.chosen !== null && root.chosen.field.indexOf("REF") >= 0
                    placeholderText: root.chosen ? root.chosen.field : ""
                    Keys.onReturnPressed: applyButton.clicked()
                }
                StyledComboBox {
                    id: valueBox
                    anchors.left: parent.left
                    anchors.right: parent.right
                    visible: root.hasChoices
                    model: root.hasChoices ? root.chosen.choices : []
                    textRole: "label"
                }
            }
        }

        ToggleSwitch {
            id: onlyEmpty
            text: qsTr("Only where the field is empty")
            checked: true
        }
        Text {
            Layout.fillWidth: true
            visible: root.ids.length > 1000
            wrapMode: Text.Wrap
            color: Theme.textSecondary
            font.pixelSize: 11
            text: qsTr("With this many QSO the change runs in the background: the log stays usable and the "
                       + "progress shows in its header.")
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Item { Layout.fillWidth: true }
            GlassButton { text: qsTr("Cancel"); onClicked: root.close() }
            GlassButton {
                id: applyButton
                text: qsTr("Change %1 QSO").arg(root.ids.length)
                tone: Theme.accentColor
                filled: true
                enabled: root.chosen !== null && root.ids.length > 0 && decolog.bulkProgress < 0
                onClicked: {
                    const value = root.hasChoices ? root.chosen.choices[Math.max(0, valueBox.currentIndex)].value
                                                  : valueField.text
                    decolog.bulkEdit(root.ids, root.chosen.field, value, onlyEmpty.checked)
                    root.close()
                }
            }
        }
    }
}
