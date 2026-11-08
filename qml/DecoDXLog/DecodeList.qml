// DecoDXLog — una lista di decodifiche di Decodium, compatta: Full Spectrum o
// Signal RX. Ogni riga e' un periodo FT8/FT4/FT2 con ora, dB, DT, frequenza e
// messaggio; il colore dice cosa vale quel nominativo per il log (stessi colori
// del cluster), e le righe che ci chiamano o che riguardano il corrispondente
// si vedono subito. Le colonne che non ci stanno spariscono.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decodium.UI

Item {
    id: root

    required property var model
    property string title: ""
    property color tone: Theme.primaryColor
    // 0 Full Spectrum, 1 Signal RX: serve a chi risponde, per ritrovare la riga.
    property int which: 0
    property bool newestOnTop: true
    property string emptyText: ""
    property string myCall: ""
    property var feed: decolog.decodium
    property var selectedSerial: -1

    // Quello che ci sta: dalla piu' stretta alla piu' larga.
    readonly property bool showDf: width >= 300
    readonly property bool showDt: width >= 380
    readonly property bool showEntity: width >= 500
    readonly property int rowH: 20
    readonly property int pad: 6

    function statusColor(status) {
        if (status & 1) return Theme.errorColor
        if (status & 2) return Theme.warningColor
        if (status & 4) return Theme.secondaryColor
        if (status & 8) return Theme.primaryColor
        if (status & (256 | 512 | 2048)) return Theme.warningColor
        if (status & 32) return Theme.textSecondary
        if (status & 16) return Theme.accentColor
        return "transparent"
    }
    function tint(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }

    // Per le schermate di prova.
    function showMenu() {
        if (list.count > 0) {
            const first = list.itemAtIndex(0)
            if (first) first.openMenu()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // La testatina della lista.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 22
            color: Theme.panelHeader
            Row {
                anchors { left: parent.left; leftMargin: root.pad; verticalCenter: parent.verticalCenter }
                spacing: 6
                Rectangle {
                    width: 7; height: 7; radius: 3.5
                    anchors.verticalCenter: parent.verticalCenter
                    color: root.tone
                }
                Text {
                    text: root.title
                    color: Theme.textPrimary
                    font.pixelSize: 11
                    font.bold: true
                    font.family: Theme.uiFamily
                }
                Text {
                    text: qsTr("%1 rows").arg(root.model.count)
                    color: Theme.textSecondary
                    font.pixelSize: 11
                    font.family: Theme.monoFamily
                }
            }
            PanelControl {
                anchors { right: parent.right; rightMargin: root.pad; verticalCenter: parent.verticalCenter }
                glyph: "⌫"
                hint: qsTr("Empty this list")
                onClicked: root.which === 0 ? root.feed.clearFullSpectrum() : root.feed.clearSignalRx()
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.model
            boundsBehavior: Flickable.StopAtBounds
            // La riga piu' recente e' la prima del modello: in cima, oppure in
            // fondo come in Decodium (e allora la lista resta ancorata al fondo).
            verticalLayoutDirection: root.newestOnTop ? ListView.TopToBottom : ListView.BottomToTop
            ScrollBar.vertical: PanelScrollBar {}

            // Nelle raffiche di decodifiche la vista lasciava una riga vecchia
            // sotto quella nuova, finche' non si ridimensionava la finestra: dopo
            // ogni raffica si fa ricalcolare il layout delle righe.
            Connections {
                target: root.model
                function onCountChanged() { relayoutTimer.restart() }
            }
            Timer {
                id: relayoutTimer
                interval: 150
                onTriggered: list.forceLayout()
            }

            delegate: Rectangle {
                id: line
                required property int index
                required property var serial
                required property string time
                required property int slot
                required property int snr
                required property real dt
                required property int df
                required property string message
                required property string from
                required property bool cq
                required property bool forMe
                required property bool withDx
                required property bool ownTx
                required property bool lowConfidence
                required property int status
                required property string statusLabel
                required property string entity

                readonly property bool chosen: root.selectedSerial === serial
                readonly property color stripe: root.statusColor(status)
                // Chi ci chiama ha il fondo rosso e il testo pieno: il colore del
                // testo e' riservato a quello che vale per il log (che puo' essere
                // rosso anche lui, per un'entita' nuova).
                readonly property color textTone: ownTx ? Theme.warningColor
                                                : forMe ? Theme.textPrimary
                                                : withDx ? Theme.warningColor
                                                : status !== 0 && stripe.a > 0 ? stripe
                                                : cq ? Theme.accentColor
                                                : Theme.textPrimary
                function openMenu() { menu.popupFor(line.serial) }

                width: list.width
                height: root.rowH
                color: chosen ? root.tint(Theme.primaryColor, 0.28)
                     : ownTx ? root.tint(Theme.warningColor, 0.14)
                     : forMe ? root.tint(Theme.errorColor, 0.16)
                     : withDx ? root.tint(Theme.warningColor, 0.12)
                     : hover.containsMouse ? root.tint(Theme.primaryColor, 0.10)
                     : slot % 2 === 0 ? "transparent" : root.tint(Theme.textPrimary, 0.035)

                // Cosa vale per il log: una barretta a sinistra.
                Rectangle {
                    anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                    width: 3
                    color: line.ownTx ? "transparent" : line.stripe
                }

                RowLayout {
                    anchors { left: parent.left; leftMargin: root.pad + 3; right: parent.right; rightMargin: root.pad
                              verticalCenter: parent.verticalCenter }
                    spacing: 6
                    Text {
                        Layout.preferredWidth: 42
                        text: line.time
                        color: Theme.textSecondary
                        font.family: Theme.monoFamily; font.pixelSize: 11
                    }
                    Text {
                        Layout.preferredWidth: 26
                        horizontalAlignment: Text.AlignRight
                        text: line.ownTx ? "TX" : line.snr
                        color: line.ownTx ? Theme.warningColor : Theme.textSecondary
                        font.family: Theme.monoFamily; font.pixelSize: 11
                    }
                    Text {
                        visible: root.showDt
                        Layout.preferredWidth: 26
                        horizontalAlignment: Text.AlignRight
                        text: line.ownTx ? "" : line.dt.toFixed(1)
                        color: Theme.textSecondary
                        font.family: Theme.monoFamily; font.pixelSize: 11
                    }
                    Text {
                        visible: root.showDf
                        Layout.preferredWidth: 34
                        horizontalAlignment: Text.AlignRight
                        text: line.df
                        color: Theme.textSecondary
                        font.family: Theme.monoFamily; font.pixelSize: 11
                    }
                    Text {
                        Layout.fillWidth: true
                        text: line.message + (line.lowConfidence ? " ?" : "")
                        color: line.textTone
                        elide: Text.ElideRight
                        font.family: Theme.monoFamily; font.pixelSize: 11
                        font.bold: line.forMe || line.ownTx
                    }
                    // Cosa e' per il log, e dove sta.
                    Text {
                        visible: root.showEntity
                        Layout.preferredWidth: 64
                        text: line.statusLabel
                        color: line.stripe.a > 0 ? line.stripe : Theme.textSecondary
                        elide: Text.ElideRight
                        font.family: Theme.uiFamily; font.pixelSize: 10; font.bold: true
                    }
                    Text {
                        visible: root.showEntity
                        Layout.preferredWidth: 120
                        elide: Text.ElideRight
                        text: line.entity
                        color: Theme.textSecondary
                        font.family: Theme.uiFamily; font.pixelSize: 10
                    }
                }

                MouseArea {
                    id: hover
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: (mouse) => {
                        root.selectedSerial = line.serial
                        if (mouse.button === Qt.RightButton)
                            menu.popupFor(line.serial)
                    }
                    // Il doppio clic porta il nominativo nella scheda e nel
                    // riquadro del QSO; non fa partire niente in Decodium: per
                    // quello c'e' «Rispondi in Decodium», nel tasto destro.
                    onDoubleClicked: root.feed.pick(root.which, line.serial)
                }
                ToolTip.visible: hover.containsMouse && (line.entity.length > 0 || line.lowConfidence)
                ToolTip.delay: 700
                ToolTip.text: (line.from.length > 0 ? line.from : "")
                              + (line.entity.length > 0 ? " — " + line.entity : "")
                              + (line.statusLabel.length > 0 ? " (" + line.statusLabel + ")" : "")
                              + (line.lowConfidence ? "\n" + qsTr("Low confidence decode") : "")
            }

            Text {
                anchors.centerIn: parent
                width: parent.width - 24
                visible: list.count === 0
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: Theme.textSecondary
                font.pixelSize: 11
                text: root.emptyText
            }
        }
    }

    StyledMenu {
        id: menu
        property var serial: -1
        function popupFor(s) { serial = s; root.selectedSerial = s; popup() }
        StyledMenuItem {
            text: qsTr("Answer in Decodium")
            enabled: root.feed.online
            onTriggered: root.feed.reply(root.which, menu.serial)
        }
        StyledMenuItem { text: qsTr("Show in Call info and prepare the QSO"); onTriggered: root.feed.pick(root.which, menu.serial) }
        StyledMenuItem {
            text: qsTr("Spot this station to the cluster…")
            onTriggered: {
                const d = root.feed.spotDraft(root.which, menu.serial)
                spotDialog.openFor(d.call || "", d.freqKhz || "", d.comment || "")
            }
        }
        MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.borderSoft } }
        StyledMenuItem {
            text: qsTr("Copy the line")
            onTriggered: {
                copyBuffer.text = root.feed.lineText(root.which, menu.serial)
                copyBuffer.selectAll()
                copyBuffer.copy()
            }
        }
    }

    SpotDialog { id: spotDialog }

    TextEdit { id: copyBuffer; visible: false }
}
