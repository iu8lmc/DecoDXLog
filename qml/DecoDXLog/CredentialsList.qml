// DecoDXLog — elenco delle credenziali dei servizi, con la scheda per modificarle.
//
// Il segreto non torna mai indietro verso l'interfaccia: il campo si lascia
// vuoto per tenere quello salvato, e "stored" dice solo che c'e'.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decodium.UI

ColumnLayout {
    id: root

    // Gli id dei servizi da mostrare, in quest'ordine; vuoto = tutti.
    property var serviceIds: []
    // Righe gia' fatte al posto dell'elenco dei servizi: gli account di un
    // profilo di stazione (store.profileServices).
    property var customRows: null
    readonly property var store: decolog.credentials
    readonly property var rows: customRows !== null ? customRows
                              : store.services.filter(s => serviceIds.length === 0 || serviceIds.indexOf(s.id) >= 0)
                                              .sort((a, b) => serviceIds.indexOf(a.id) - serviceIds.indexOf(b.id))
    property string message: ""
    property bool messageOk: true

    spacing: 0

    Connections {
        target: root.store
        function onFinished(service, ok, text) {
            root.message = text
            root.messageOk = ok
            if (ok && editor.opened && editor.service === service)
                editor.close()
        }
    }

    Repeater {
        model: root.rows
        RowLayout {
            id: row
            required property var modelData
            required property int index
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            spacing: 10

            Text {
                // Un'etichetta lunga («Casella da cui mandare») non entrava nei
                // 120 e finiva sopra la descrizione accanto.
                Layout.preferredWidth: 120
                Layout.maximumWidth: 200
                Layout.minimumWidth: Math.min(200, implicitWidth)
                elide: Text.ElideRight
                text: row.modelData.label
                color: Theme.textPrimary
                font.family: Theme.monoFamily
                font.pixelSize: 12
                font.bold: true
            }
            Text {
                Layout.fillWidth: true
                elide: Text.ElideRight
                text: row.modelData.account.length
                      ? row.modelData.account + (row.modelData.stored ? " · " + row.modelData.secretLabel.toLowerCase() + " ••••" : "")
                      : row.modelData.hint
                color: Theme.textSecondary
                font.family: row.modelData.account.length ? Theme.monoFamily : Theme.uiFamily
                font.pixelSize: row.modelData.account.length ? 12 : 11
            }
            Text {
                Layout.preferredWidth: 90
                text: row.modelData.busy ? qsTr("…")
                    : row.modelData.error.length ? qsTr("error")
                    : row.modelData.stored ? qsTr("stored")
                    : row.modelData.account.length ? qsTr("no secret") : qsTr("not set")
                color: row.modelData.error.length ? Theme.errorColor
                     : row.modelData.stored ? Theme.accentColor
                     : row.modelData.account.length ? Theme.warningColor : Theme.textSecondary
                font.family: Theme.monoFamily
                font.pixelSize: 12
                ToolTip.visible: errorHover.hovered && row.modelData.error.length > 0
                ToolTip.text: row.modelData.error
                HoverHandler { id: errorHover }
            }
            Text {
                Layout.preferredWidth: 50
                horizontalAlignment: Text.AlignRight
                text: row.modelData.stored || row.modelData.account.length ? qsTr("Edit") : qsTr("Add")
                color: root.store.available ? (editArea.containsMouse ? Theme.textPrimary : Theme.primaryColor) : Theme.textSecondary
                font.family: Theme.monoFamily
                font.pixelSize: 12
                font.bold: true
                MouseArea {
                    id: editArea
                    anchors.fill: parent
                    anchors.margins: -6
                    hoverEnabled: true
                    enabled: root.store.available
                    cursorShape: Qt.PointingHandCursor
                    onClicked: editor.edit(row.modelData)
                }
            }
        }
    }

    Text {
        Layout.fillWidth: true
        Layout.topMargin: 6
        wrapMode: Text.Wrap
        text: root.store.available
              ? qsTr("Secrets live in the system keystore (%1), never in the settings file.").arg(root.store.backend)
              : qsTr("This build has no system keystore (qtkeychain): credentials cannot be stored.")
        color: root.store.available ? Theme.textSecondary : Theme.warningColor
        font.pixelSize: 11
    }
    Text {
        Layout.fillWidth: true
        visible: root.message.length > 0
        text: root.message
        color: root.messageOk ? Theme.accentColor : Theme.errorColor
        font.family: Theme.monoFamily
        font.pixelSize: 11
    }

    Popup {
        id: editor
        objectName: "credentialsEditor"

        property string service: ""
        property var info: ({})

        function edit(info) {
            editor.info = info
            editor.service = info.id
            accountField.text = info.account
            secretField.text = ""
            root.message = ""
            open()
            if (info.id === "clublog") secretField.forceActiveFocus()
            else accountField.forceActiveFocus()
        }

        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 420
        padding: 0
        background: Rectangle { color: Theme.panelColor; border.color: Theme.glassBorder; radius: 6 }

        contentItem: ColumnLayout {
            spacing: 0
            PanelHeader {
                Layout.fillWidth: true
                text: editor.info.label || ""
                dotColor: Theme.secondaryColor
                showHandle: false
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: 14
                spacing: 10
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: editor.info.hint || ""
                    color: Theme.textSecondary
                    font.pixelSize: 12
                }
                Text {
                    Layout.fillWidth: true
                    visible: editor.service === "clublog"
                    objectName: "clubLogStationHelp"
                    wrapMode: Text.Wrap
                    text: qsTr("REST token: email is not required. The log callsign comes from the active station profile: %1.")
                              .arg(decolog.stationProfiles.activeProfile.stationCallsign || qsTr("not set"))
                    color: Theme.textSecondary
                    font.pixelSize: 12
                }
                Text {
                    Layout.fillWidth: true
                    visible: editor.service === "clublog"
                    wrapMode: Text.Wrap
                    text: qsTr("Check confirms the secret is in the system keystore; it does not test the Club Log connection.")
                    color: Theme.textSecondary
                    font.pixelSize: 11
                }
                LabeledField {
                    Layout.fillWidth: true
                    label: editor.info.accountLabel || ""
                    StyledTextField { id: accountField; objectName: "credentialAccount"; Layout.fillWidth: true }
                }
                LabeledField {
                    Layout.fillWidth: true
                    label: editor.info.secretLabel || ""
                    StyledTextField {
                        id: secretField
                        objectName: "credentialSecret"
                        Layout.fillWidth: true
                        echoMode: TextInput.Password
                        placeholderText: editor.info.stored ? qsTr("stored — leave empty to keep it") : ""
                        Keys.onReturnPressed: saveButton.clicked()
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    spacing: 8
                    GlassButton {
                        text: qsTr("Remove")
                        tone: Theme.errorColor
                        visible: editor.info.stored || (editor.info.account || "").length > 0
                        onClicked: root.store.remove(editor.service)
                    }
                    GlassButton {
                        text: qsTr("Check")
                        visible: editor.info.stored === true
                        onClicked: root.store.verify(editor.service)
                    }
                    Item { Layout.fillWidth: true }
                    GlassButton { text: qsTr("Cancel"); onClicked: editor.close() }
                    GlassButton {
                        id: saveButton
                        objectName: "credentialSave"
                        text: qsTr("Save")
                        tone: Theme.accentColor
                        filled: true
                        enabled: accountField.text.trim().length > 0
                                 || (editor.service === "clublog"
                                     && (secretField.text.trim().startsWith("cl_") || editor.info.stored))
                        onClicked: root.store.save(editor.service, accountField.text, secretField.text)
                    }
                }
                Text {
                    Layout.fillWidth: true
                    visible: root.message.length > 0 && editor.opened
                    text: root.message
                    wrapMode: Text.Wrap
                    color: root.messageOk ? Theme.accentColor : Theme.errorColor
                    font.family: Theme.monoFamily
                    font.pixelSize: 11
                }
            }
        }
    }
}
