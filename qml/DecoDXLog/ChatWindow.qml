// DecoDXLog — la chat ON4KST: i messaggi al centro, chi c'e' a destra, la
// riga per scrivere in basso. Un clic su un nominativo lo mette nella scheda
// nominativo; il doppio clic prepara un messaggio privato.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import Decodium.UI

RoundedWindow {
    id: root

    readonly property var chat: decolog.chat
    // A chi va il prossimo messaggio: vuoto = a tutti.
    property string privateTo: ""

    width: 980
    height: 640
    minimumWidth: 640
    minimumHeight: 400
    visible: true
    title: qsTr("DecoDXLog — ON4KST chat")
    surfaceColor: Theme.bgDeep

    OnScreen { target: root }

    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowMinimizeButtonHint | Qt.WindowMaximizeButtonHint
    header: WindowTitleBar { window: root; cornerRadius: root.frameRadius }
    WindowChrome {
        window: root
        parent: root.contentItem.parent
        dragHeight: Theme.panelHeight
        cornerRadius: root.frameRadius
    }

    Settings {
        category: "chatWindow"
        property alias width: root.width
        property alias height: root.height
        property alias windowX: root.x
        property alias windowY: root.y
    }

    onActiveChanged: if (active) root.chat.markRead()
    Connections {
        target: root.chat
        function onMessagesChanged() {
            if (root.active)
                root.chat.markRead()
            scrollToLatest.restart()
        }
    }
    // La chat e' caricata da un Loader. Un Timer viene eliminato assieme alla
    // finestra, mentre una lambda passata a Qt.callLater potrebbe essere
    // eseguita quando il suo contesto QML non esiste piu'.
    Timer {
        id: scrollToLatest
        interval: 0
        onTriggered: messageList.positionViewAtEnd()
    }

    function sendLine() {
        const t = input.text.trim()
        if (!t.length)
            return
        const ok = root.privateTo.length ? root.chat.sendTo(root.privateTo, t) : root.chat.send(t)
        if (ok)
            input.text = ""
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        // ── Stanza e collegamento ───────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Led {
                color: root.chat.online ? Theme.accentColor
                     : root.chat.state === "off" ? Theme.textSecondary : Theme.warningColor
            }
            Text {
                text: root.chat.status
                color: Theme.textPrimary
                font.family: Theme.monoFamily
                font.pixelSize: 12
            }
            Item { Layout.fillWidth: true }
            Text { text: qsTr("Room"); color: Theme.textSecondary; font.pixelSize: 12 }
            StyledComboBox {
                Layout.preferredWidth: 230
                model: root.chat.rooms.map(r => r.number + " · " + r.name)
                currentIndex: root.chat.room - 1
                onActivated: root.chat.room = currentIndex + 1
            }
            GlassButton {
                text: root.chat.state === "off" ? qsTr("Connect") : qsTr("Disconnect")
                tone: root.chat.state === "off" ? Theme.accentColor : Theme.errorColor
                filled: root.chat.state === "off"
                enabled: root.chat.hasCredentials || root.chat.state !== "off"
                onClicked: root.chat.state === "off" ? root.chat.connectChat() : root.chat.disconnectChat()
            }
        }
        Text {
            Layout.fillWidth: true
            visible: !root.chat.hasCredentials
            wrapMode: Text.Wrap
            text: qsTr("Put your ON4KST callsign and password in Settings → Sync & Cloud, in the list of services (ON4KST Chat). They are kept in the system keychain.")
            color: Theme.warningColor
            font.pixelSize: 12
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            // ── I messaggi ──────────────────────────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 6
                color: Theme.panelColor
                border.color: Theme.borderSoft
                ListView {
                    id: messageList
                    anchors.fill: parent
                    anchors.margins: 6
                    clip: true
                    model: root.chat.messages
                    spacing: 2
                    ScrollBar.vertical: PanelScrollBar {}
                    delegate: Rectangle {
                        required property var modelData
                        width: messageList.width - 10
                        implicitHeight: row.implicitHeight + 4
                        radius: 3
                        color: modelData.toMe ? Qt.rgba(Theme.warningColor.r, Theme.warningColor.g, Theme.warningColor.b, 0.16)
                             : modelData.to.length ? Qt.rgba(Theme.secondaryColor.r, Theme.secondaryColor.g, Theme.secondaryColor.b, 0.10)
                             : "transparent"
                        RowLayout {
                            id: row
                            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; leftMargin: 4; rightMargin: 4 }
                            spacing: 8
                            Text {
                                Layout.alignment: Qt.AlignTop
                                text: modelData.system ? "" : modelData.time
                                color: Theme.textSecondary
                                font.family: Theme.monoFamily
                                font.pixelSize: 11
                            }
                            Text {
                                Layout.alignment: Qt.AlignTop
                                Layout.preferredWidth: 150
                                visible: !modelData.system
                                elide: Text.ElideRight
                                text: modelData.from + (modelData.name ? " " + modelData.name : "")
                                      + (modelData.to.length ? " → " + modelData.to : "")
                                color: modelData.mine ? Theme.secondaryColor : Theme.primaryColor
                                font.family: Theme.monoFamily
                                font.pixelSize: 12
                                font.bold: true
                                TapHandler { onTapped: decolog.lookupCall = modelData.from }
                                TapHandler {
                                    acceptedButtons: Qt.LeftButton
                                    onDoubleTapped: { root.privateTo = modelData.from; input.forceActiveFocus() }
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.text
                                wrapMode: Text.Wrap
                                color: modelData.system ? Theme.textSecondary : Theme.textPrimary
                                font.family: Theme.monoFamily
                                font.pixelSize: 12
                            }
                        }
                    }
                }
            }

            // ── Chi c'e' ────────────────────────────────────────────────────
            Rectangle {
                Layout.preferredWidth: 220
                Layout.fillHeight: true
                radius: 6
                color: Theme.panelColor
                border.color: Theme.borderSoft
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 4
                    Text {
                        text: qsTr("Heard (%1)").arg(root.chat.people.length)
                        color: Theme.textSecondary
                        font.pixelSize: 12
                    }
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: root.chat.people
                        ScrollBar.vertical: PanelScrollBar {}
                        delegate: Item {
                            required property var modelData
                            width: ListView.view.width - 8
                            height: 22
                            HoverHandler { id: personHover; cursorShape: Qt.PointingHandCursor }
                            Rectangle { anchors.fill: parent; radius: 3; color: personHover.hovered ? Theme.bgMedium : "transparent" }
                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 4
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.call
                                color: Theme.textPrimary
                                font.family: Theme.monoFamily
                                font.pixelSize: 12
                            }
                            Text {
                                anchors.right: parent.right
                                anchors.rightMargin: 4
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.last
                                color: Theme.textSecondary
                                font.family: Theme.monoFamily
                                font.pixelSize: 10
                            }
                            TapHandler { onTapped: decolog.lookupCall = modelData.call }
                            TapHandler { onDoubleTapped: { root.privateTo = modelData.call; input.forceActiveFocus() } }
                        }
                    }
                }
            }
        }

        // ── La riga per scrivere ────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            GlassButton {
                visible: root.privateTo.length > 0
                text: qsTr("to %1 ✕").arg(root.privateTo)
                tone: Theme.secondaryColor
                onClicked: root.privateTo = ""
            }
            StyledTextField {
                id: input
                Layout.fillWidth: true
                enabled: root.chat.online
                mono: false
                placeholderText: root.privateTo.length ? qsTr("Private message to %1").arg(root.privateTo)
                                                       : qsTr("Message to the whole room")
                onAccepted: root.sendLine()
            }
            GlassButton {
                text: qsTr("Send")
                tone: Theme.accentColor
                filled: true
                enabled: root.chat.online && input.text.trim().length > 0
                onClicked: root.sendLine()
            }
        }
    }
}
