// DecoDXLog — i QSO doppi, e come unirli.
//
// Un QSO arriva due volte quando lo scrivono due programmi (Decodium e un
// import dal suo ADIF), o quando lo stesso log si importa con l'ora un po'
// diversa. Qui si cercano: stesso nominativo, banda, genere di modo (CW,
// fonia, digitali) e profilo di stazione, entro i minuti scelti. Di ogni
// gruppo se ne tiene uno — quello proposto e' il piu' confermato, poi il piu'
// completo, e un clic ne sceglie un altro — che prende dagli altri quello che
// gli manca; gli altri si cancellano e restano nello storico.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decodium.UI

Popup {
    id: root

    signal openQso(var id)

    readonly property var result: decolog.duplicates
    readonly property var groups: result.groups || []
    // Per gruppo (l'id del primo QSO): quello da tenere e se si salta.
    property var keepFor: ({})
    property var skipped: ({})

    function groupKey(g) { return g.rows[0].id }
    function keepOf(g) {
        const k = root.keepFor[root.groupKey(g)]
        return k !== undefined ? k : g.keep
    }
    function ticked() {
        return root.groups.filter(g => root.skipped[root.groupKey(g)] !== true)
    }
    function mergeTicked() {
        const list = root.ticked().map(g => ({ keep: root.keepOf(g), ids: g.rows.map(r => r.id) }))
        if (list.length > 0)
            decolog.mergeDuplicatesAsync(list)
    }

    popupType: Popup.Window
    parent: Overlay.overlay
    anchors.centerIn: parent
    implicitWidth: 820
    implicitHeight: 620
    width: 820
    height: 620
    modal: true
    padding: 14
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { color: Theme.panelColor; border.color: Theme.glassBorder; radius: 6 }
    onOpened: {
        root.keepFor = ({})
        root.skipped = ({})
    }

    contentItem: ColumnLayout {
        spacing: 10

        Text {
            text: qsTr("DUPLICATE QSO")
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
            text: qsTr("Same call, band, kind of mode (CW, phone, digital) and station profile, within the minutes "
                       + "chosen. Of each group one QSO is kept — a click on a row chooses which — and it takes from "
                       + "the others what it is missing: fields, confirmations, tags. The others are deleted; they "
                       + "stay in the history and can be recovered.")
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text { text: qsTr("Within"); color: Theme.textSecondary; font.pixelSize: 12 }
            StyledComboBox {
                id: windowBox
                Layout.preferredWidth: 90
                model: [1, 2, 5, 10, 30, 60]
                // L'ultima ricerca, o i due minuti dei doppioni digitali.
                currentIndex: Math.max(0, model.indexOf(root.result.windowMinutes || 2))
            }
            Text { text: qsTr("minutes"); color: Theme.textSecondary; font.pixelSize: 12 }
            GlassButton {
                text: root.result.busy ? qsTr("Searching…") : qsTr("Search")
                enabled: !root.result.busy && decolog.bulkProgress < 0
                onClicked: {
                    root.keepFor = ({})
                    root.skipped = ({})
                    decolog.findDuplicates(windowBox.model[windowBox.currentIndex])
                }
            }
            Item { Layout.fillWidth: true }
            Text {
                visible: root.result.searched === true
                text: root.groups.length === 0
                      ? qsTr("No duplicates within %1 minutes").arg(root.result.windowMinutes)
                      : root.result.limited
                        ? qsTr("The first %1 groups: merge them and search again for the rest").arg(root.groups.length)
                        : qsTr("%1 groups found, %2 ticked").arg(root.groups.length).arg(root.ticked().length)
                color: root.groups.length === 0 ? Theme.accentColor : Theme.textPrimary
                font.pixelSize: 12
            }
        }

        Text { Layout.fillWidth: true; visible: !!root.result.error; wrapMode: Text.Wrap; color: Theme.warningColor
            text: qsTr("Duplicate search failed: %1").arg(root.result.error || "") }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.bgMedium
            border.color: Theme.borderSoft
            radius: 4
            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: 4
                clip: true
                enabled: decolog.bulkProgress < 0
                spacing: 6
                model: root.groups
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    id: group
                    required property var modelData
                    readonly property bool on: root.skipped[root.groupKey(modelData)] !== true
                    width: ListView.view.width - 10
                    height: groupColumn.implicitHeight + 10
                    color: Theme.glassOverlay
                    border.color: on ? Theme.borderColor : Theme.borderSoft
                    radius: 4
                    opacity: on ? 1 : 0.55
                    ColumnLayout {
                        id: groupColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 5
                        spacing: 2
                        RowLayout {
                            spacing: 6
                            CheckBox {
                                padding: 0
                                checked: group.on
                                onToggled: {
                                    const next = Object.assign({}, root.skipped)
                                    if (checked)
                                        delete next[root.groupKey(group.modelData)]
                                    else
                                        next[root.groupKey(group.modelData)] = true
                                    root.skipped = next
                                }
                            }
                            Text {
                                text: group.modelData.rows[0].call
                                color: Theme.primaryColor
                                font.family: Theme.monoFamily
                                font.pixelSize: 13
                                font.bold: true
                            }
                            Text {
                                text: qsTr("%1 QSO").arg(group.modelData.rows.length)
                                color: Theme.textSecondary
                                font.pixelSize: 11
                            }
                        }
                        Repeater {
                            model: group.modelData.rows
                            delegate: Rectangle {
                                id: row
                                required property var modelData
                                readonly property bool keep: root.keepOf(group.modelData) === modelData.id
                                Layout.fillWidth: true
                                implicitHeight: 24
                                radius: 3
                                color: rowArea.containsMouse ? Theme.glassOverlay : "transparent"
                                MouseArea {
                                    id: rowArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        const next = Object.assign({}, root.keepFor)
                                        next[root.groupKey(group.modelData)] = row.modelData.id
                                        root.keepFor = next
                                    }
                                    onDoubleClicked: root.openQso(row.modelData.id)
                                }
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 26
                                    anchors.rightMargin: 6
                                    spacing: 10
                                    Pill {
                                        Layout.preferredWidth: 70
                                        text: row.keep ? qsTr("KEEP") : qsTr("merge")
                                        tone: row.keep ? Theme.accentColor : Theme.textSecondary
                                        pillHeight: 18
                                        fontPixelSize: 9
                                    }
                                    Text {
                                        Layout.preferredWidth: 140
                                        text: row.modelData.when
                                        color: Theme.textPrimary
                                        font.family: Theme.monoFamily
                                        font.pixelSize: 11
                                    }
                                    Text {
                                        Layout.preferredWidth: 50
                                        text: row.modelData.band
                                        color: Theme.textPrimary
                                        font.family: Theme.monoFamily
                                        font.pixelSize: 11
                                    }
                                    Text {
                                        Layout.preferredWidth: 60
                                        text: row.modelData.mode
                                        color: Theme.textPrimary
                                        font.family: Theme.monoFamily
                                        font.pixelSize: 11
                                    }
                                    Text {
                                        Layout.preferredWidth: 110
                                        elide: Text.ElideRight
                                        text: row.modelData.source
                                        color: Theme.textSecondary
                                        font.pixelSize: 11
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        text: row.modelData.confirmed.length
                                              ? qsTr("confirmed: %1").arg(row.modelData.confirmed) : ""
                                        color: Theme.accentColor
                                        font.pixelSize: 11
                                    }
                                    Text {
                                        text: qsTr("%1 fields").arg(row.modelData.fields)
                                        color: Theme.textSecondary
                                        font.pixelSize: 10
                                    }
                                }
                            }
                        }
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                visible: root.groups.length === 0
                text: root.result.busy ? qsTr("Reading the whole log…")
                                       : root.result.searched ? qsTr("Nothing to merge.")
                                                              : qsTr("Choose the minutes and press Search.")
                color: Theme.textSecondary
                font.pixelSize: 12
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: root.result.merging ? qsTr("Merging… %1%").arg(Math.round(decolog.bulkProgress * 100))
                                          : qsTr("A double click opens the QSO.")
                color: Theme.textSecondary
                font.pixelSize: 11
            }
            GlassButton { text: qsTr("Close"); onClicked: root.close() }
            GlassButton {
                text: qsTr("Merge %1 groups").arg(root.ticked().length)
                tone: Theme.accentColor
                filled: true
                enabled: !root.result.busy && decolog.bulkProgress < 0 && root.ticked().length > 0
                onClicked: root.mergeTicked()
            }
        }
    }
}
