// DecoDXLog — la cartolina QSL: si carica la propria, si posano i campi sopra.
//
// Una QSL di carta si stampa sulla cartolina che ci si e' fatti fare, dove
// restano i riquadri vuoti da riempire: il corrispondente, la data, l'ora, la
// banda, il modo, il rapporto. Qui si dice dove vanno quei riquadri — una volta
// sola, trascinandoli col mouse — e poi il programma li riempie da solo per
// tutti i QSO che si vogliono.
//
// Quello che si vede qui e' quello che si stampa: le posizioni sono frazioni
// del modello e i corpi millesimi della sua altezza, e a disegnare e' sempre lo
// stesso codice, sullo schermo come nel PDF.
//
// Il vestito e' quello di tutte le altre finestre: pannelli di vetro con la
// testata, etichette sopra i controlli. Dentro c'e' una cartolina vera, che
// porta i suoi colori: sta in mezzo, con un po' d'aria attorno, come una foto
// appoggiata sul tavolo.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Decodium.UI

ColumnLayout {
    id: root

    // Gli id scelti nella coda: vuoto vuol dire "tutta la coda".
    property var selectedIds: []

    readonly property var cards: decolog.cards
    readonly property var fields: cards.cardFields
    readonly property var sample: { cards.cardChanged; decolog.logChanged; return cards.sampleQso() }
    readonly property var station: cards.stationInfo()
    readonly property bool hasTemplate: cards.cardTemplate.length > 0
    property int selected: -1

    readonly property var chosenField: root.selected >= 0 && root.selected < root.fields.length
                                       ? root.fields[root.selected] : null

    spacing: 10

    function fieldValue(field) {
        const key = field.key
        if (key === "text") return field.text
        if (key.startsWith("my")) {
            const what = key.substring(2, 3).toLowerCase() + key.substring(3)
            return root.station[what] || ""
        }
        const date = root.sample.date || ""
        if (key === "day")   return date.length === 10 ? date.substring(8, 10) : ""
        if (key === "month") return date.length === 10 ? date.substring(5, 7) : ""
        if (key === "year")  return date.length === 10 ? date.substring(0, 4) : ""
        if (key === "monthName") {
            const names = ["JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                           "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"]
            return date.length === 10 ? names[parseInt(date.substring(5, 7), 10) - 1] : ""
        }
        if (key === "time") {
            const t = root.sample.time || ""
            return t.length === 4 ? t.substring(0, 2) + ":" + t.substring(2) : t
        }
        if (key === "freq") {
            const mhz = parseFloat(root.sample.freq)
            return isNaN(mhz) || mhz <= 0 ? "" : mhz.toFixed(3)
        }
        return root.sample[key] || ""
    }

    // Per le schermate di prova: apre l'elenco dei campi che si possono posare.
    function showKeyMenu() { keyMenu.popup() }

    function keyLabel(key) {
        const all = root.cards.cardKeys()
        for (let i = 0; i < all.length; ++i) {
            if (all[i].key === key)
                return all[i].label
        }
        return key
    }

    // ── La cartolina ────────────────────────────────────────────────────────
    GlassPanel {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 240
        detachable: false
        closable: false
        title: root.hasTemplate ? qsTr("The card · %1 fields").arg(root.fields.length)
                                : qsTr("The card")
        dotColor: root.hasTemplate ? Theme.accentColor : Theme.textSecondary
        headerTools: [
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.cards.cardSize.width > 0
                text: root.cards.cardSize.width + " × " + root.cards.cardSize.height
                color: Theme.textSecondary
                font.family: Theme.monoFamily
                font.pixelSize: 11
            },
            Text {
                anchors.verticalCenter: parent.verticalCenter
                // Il percorso per intero non ci sta e non serve: basta il nome
                // del file, il resto lo dice il fumetto.
                text: root.hasTemplate
                      ? root.cards.cardTemplate.split(/[\\/]/).pop()
                      : qsTr("no image yet")
                color: Theme.textSecondary
                font.pixelSize: 11
                elide: Text.ElideMiddle
                ToolTip.visible: pathHover.hovered && root.hasTemplate
                ToolTip.text: root.cards.cardTemplate
                HoverHandler { id: pathHover }
            },
            GlassButton {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Load an image…")
                buttonHeight: 22
                fontPixelSize: 11
                onClicked: templateDialog.open()
            }
        ]

        // La cartolina tiene le sue proporzioni e sta in mezzo: quello che si
        // vede e' la cartolina, non il riquadro che la contiene.
        Item {
            id: view
            readonly property real ratio: root.cards.cardSize.height > 0
                                          ? root.cards.cardSize.width / root.cards.cardSize.height
                                          : 148 / 105
            readonly property real maxW: parent.width - 16
            readonly property real maxH: parent.height - 16
            width: Math.max(80, Math.min(maxW, maxH * ratio))
            height: width / ratio
            anchors.centerIn: parent

            // Un filo d'ombra sotto, che la stacca dal fondo scuro: sembra
            // appoggiata, non incollata.
            Rectangle {
                anchors.fill: parent
                anchors.margins: -1
                radius: 3
                color: "transparent"
                border.width: 1
                border.color: Theme.borderColor
            }

            Image {
                id: background
                anchors.fill: parent
                source: root.hasTemplate
                        ? "file:///" + root.cards.cardTemplate.replace(/\\/g, "/") : ""
                fillMode: Image.Stretch
                smooth: true
                cache: false
            }

            // Senza modello non si finge una cartolina: si dice cosa manca.
            Rectangle {
                anchors.fill: parent
                visible: background.status !== Image.Ready
                color: Theme.bgMedium
                radius: 3
                ColumnLayout {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 40, 420)
                    spacing: 10
                    SectionTitle {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Card model")
                    }
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        text: qsTr("Load the image of your QSL card: a scan or the file the printer gave you. "
                                   + "Then drop the fields on it and drag them where the boxes are.")
                        color: Theme.textSecondary
                        font.pixelSize: 12
                    }
                    GlassButton {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Load an image…")
                        tone: Theme.primaryColor
                        filled: true
                        buttonHeight: 26
                        fontPixelSize: 12
                        onClicked: templateDialog.open()
                    }
                }
            }

            Repeater {
                model: root.fields
                Item {
                    id: chip
                    required property var modelData
                    required property int index
                    readonly property bool isSelected: root.selected === chip.index
                    // Dove si aggancia il testo: a sinistra, in mezzo o a destra
                    // del punto che si e' scelto.
                    readonly property real anchorOffset: modelData.align === "center" ? width / 2
                                                       : modelData.align === "right" ? width : 0
                    width: label.implicitWidth
                    height: label.implicitHeight
                    x: view.width * modelData.x - anchorOffset
                    y: view.height * modelData.y

                    Text {
                        id: label
                        // Il corpo e' in millesimi dell'altezza della cartolina,
                        // come nel disegno vero: cosi' l'anteprima non mente.
                        text: root.fieldValue(chip.modelData) || "—"
                        color: chip.modelData.color
                        font.pixelSize: Math.max(4, view.height * chip.modelData.size / 1000)
                        font.bold: chip.modelData.bold
                    }
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -2
                        color: "transparent"
                        border.width: 1
                        border.color: chip.isSelected ? Theme.primaryColor
                                    : dragger.containsMouse ? Theme.accentColor : "transparent"
                        radius: 2
                    }
                    MouseArea {
                        id: dragger
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.SizeAllCursor
                        drag.target: chip
                        drag.threshold: 0
                        onPressed: root.selected = chip.index
                        onReleased: {
                            root.cards.moveCardField(chip.index,
                                                     (chip.x + chip.anchorOffset) / view.width,
                                                     chip.y / view.height)
                        }
                    }
                }
            }
        }
    }

    // ── Il campo ────────────────────────────────────────────────────────────
    GlassPanel {
        Layout.fillWidth: true
        Layout.preferredHeight: 92
        detachable: false
        closable: false
        dotColor: root.chosenField ? Theme.primaryColor : Theme.textSecondary
        title: root.chosenField
               ? qsTr("Field · %1").arg(root.keyLabel(root.chosenField.key))
               : qsTr("Fields · drag them where the boxes are")

        RowLayout {
            anchors.fill: parent
            spacing: 12

            GlassButton {
                Layout.alignment: Qt.AlignBottom
                Layout.bottomMargin: 2
                text: qsTr("Add a field ▾")
                buttonHeight: 26
                fontPixelSize: 12
                onClicked: keyMenu.popup()
                StyledMenu {
                    id: keyMenu
                    Instantiator {
                        model: root.cards.cardKeys()
                        // StyledMenuItem e non MenuItem: quello di Qt resta coi
                        // colori del sistema, e su un tema scuro il testo si
                        // legge a stento.
                        delegate: StyledMenuItem {
                            required property var modelData
                            text: modelData.label
                            onTriggered: {
                                root.cards.addCardField(modelData.key)
                                root.selected = root.fields.length
                            }
                        }
                        onObjectAdded: (index, object) => keyMenu.insertItem(index, object)
                        onObjectRemoved: (index, object) => keyMenu.removeItem(object)
                    }
                }
            }

            // Una QSL ha quasi sempre gli stessi otto riquadri: si mettono tutti
            // in una volta, e si trascina solo quello che non cade giusto.
            GlassButton {
                Layout.alignment: Qt.AlignBottom
                Layout.bottomMargin: 2
                text: qsTr("The usual eight")
                tone: Theme.secondaryColor
                buttonHeight: 26
                fontPixelSize: 12
                enabled: root.hasTemplate
                onClicked: root.cards.addStandardCardFields()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Callsign, day, month, year, UTC, MHz, mode and RST, in the boxes "
                                   + "where a QSL usually has them. Those already on the card move "
                                   + "there. On a card made differently, drag them.")
            }

            Text {
                visible: root.chosenField === null
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignBottom
                Layout.bottomMargin: 6
                wrapMode: Text.Wrap
                text: root.fields.length > 0
                      ? qsTr("Click a field on the card to change it.")
                      : qsTr("Add the fields you want on the card, then drag them onto the boxes.")
                color: Theme.textSecondary
                font.pixelSize: 11
            }

            LabeledField {
                visible: root.chosenField !== null && root.chosenField.key === "text"
                Layout.preferredWidth: 150
                label: qsTr("Free text")
                StyledTextField {
                    Layout.fillWidth: true
                    text: root.chosenField ? root.chosenField.text : ""
                    onEditingFinished: root.cards.updateCardField(root.selected, { "text": text })
                }
            }
            LabeledField {
                visible: root.chosenField !== null
                Layout.preferredWidth: 160
                label: qsTr("Size")
                Slider {
                    Layout.fillWidth: true
                    from: 10
                    to: 200
                    stepSize: 1
                    value: root.chosenField ? root.chosenField.size : 40
                    onMoved: root.cards.updateCardField(root.selected, { "size": Math.round(value) })
                }
            }
            LabeledField {
                visible: root.chosenField !== null
                Layout.preferredWidth: 120
                label: qsTr("Hangs")
                StyledComboBox {
                    Layout.fillWidth: true
                    readonly property var codes: ["left", "center", "right"]
                    model: [qsTr("left"), qsTr("centre"), qsTr("right")]
                    currentIndex: root.chosenField ? codes.indexOf(root.chosenField.align) : 0
                    onActivated: root.cards.updateCardField(root.selected, { "align": codes[currentIndex] })
                }
            }
            LabeledField {
                visible: root.chosenField !== null
                Layout.preferredWidth: 110
                label: qsTr("Colour")
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Repeater {
                        model: ["#000000", "#ffffff", "#c00000", "#0050c0"]
                        Rectangle {
                            required property string modelData
                            readonly property bool chosen: root.chosenField
                                                           && root.chosenField.color === modelData
                            implicitWidth: 22
                            implicitHeight: 22
                            radius: 3
                            color: modelData
                            border.width: chosen ? 2 : 1
                            border.color: chosen ? Theme.primaryColor : Theme.borderColor
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.cards.updateCardField(root.selected, { "color": modelData })
                            }
                        }
                    }
                }
            }
            ToggleSwitch {
                visible: root.chosenField !== null
                Layout.alignment: Qt.AlignBottom
                Layout.bottomMargin: 4
                text: qsTr("Bold")
                checked: root.chosenField ? root.chosenField.bold : true
                onToggled: root.cards.updateCardField(root.selected, { "bold": checked })
            }
            Item { Layout.fillWidth: true }
            GlassButton {
                visible: root.chosenField !== null
                Layout.alignment: Qt.AlignBottom
                Layout.bottomMargin: 2
                text: qsTr("Remove")
                tone: Theme.errorColor
                buttonHeight: 26
                fontPixelSize: 12
                onClicked: { root.cards.removeCardField(root.selected); root.selected = -1 }
            }
        }
    }

    // ── Stampare ────────────────────────────────────────────────────────────
    GlassPanel {
        Layout.fillWidth: true
        Layout.preferredHeight: 92
        detachable: false
        closable: false
        dotColor: Theme.secondaryColor
        title: qsTr("Print · a PDF sheet, or one PNG per QSO")

        RowLayout {
            anchors.fill: parent
            spacing: 12

            LabeledField {
                Layout.preferredWidth: 110
                label: qsTr("Cards per sheet")
                StyledComboBox {
                    id: perPageBox
                    Layout.fillWidth: true
                    readonly property var values: [1, 2, 4]
                    model: ["1", "2", "4"]
                    currentIndex: 1
                }
            }
            GlassButton {
                Layout.alignment: Qt.AlignBottom
                Layout.bottomMargin: 2
                text: qsTr("Write the PDF…")
                tone: Theme.primaryColor
                filled: true
                buttonHeight: 28
                enabled: root.fields.length > 0 && !root.cards.exportBusy && !root.cards.operationBusy
                onClicked: pdfDialog.open()
            }
            GlassButton {
                Layout.alignment: Qt.AlignBottom
                Layout.bottomMargin: 2
                text: qsTr("One PNG each…")
                buttonHeight: 28
                enabled: root.fields.length > 0 && !root.cards.exportBusy && !root.cards.operationBusy
                onClicked: pngDialog.open()
            }
            // La cartolina per email: l'indirizzo lo sa il callbook.
            GlassButton {
                Layout.alignment: Qt.AlignBottom
                Layout.bottomMargin: 2
                text: root.cards.mailBusy ? qsTr("Sending…") : qsTr("Send by email…")
                tone: Theme.secondaryColor
                buttonHeight: 28
                enabled: root.fields.length > 0 && !root.cards.mailBusy && !root.cards.operationBusy
                onClicked: mailDialog.open()
                ToolTip.visible: hovered
                ToolTip.text: !root.cards.mail.ready
                              ? (root.cards.mail.route === "cloud"
                                 ? qsTr("Link the Cloud first, or choose your own mailbox: Setup → QSL services.")
                                 : qsTr("First set up the outgoing mailbox: Setup → QSL services."))
                              : root.cards.mail.route === "cloud"
                                ? qsTr("The address comes from the callbook (QRZ.com or HamQTH). "
                                       + "The Cloud posts the card in your name, and answers come back to you.")
                                : qsTr("The address comes from the callbook (QRZ.com or HamQTH). "
                                       + "What goes out is sent by your own mailbox.")
            }
            GlassButton {
                Layout.alignment: Qt.AlignBottom
                Layout.bottomMargin: 2
                visible: root.cards.mailBusy
                text: qsTr("Stop")
                tone: Theme.errorColor
                buttonHeight: 28
                onClicked: root.cards.cancelMail()
            }
            Text {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignBottom
                Layout.bottomMargin: 6
                wrapMode: Text.Wrap
                text: root.cards.mailStatus.length > 0 ? root.cards.mailStatus
                    : root.selectedIds.length > 0
                      ? qsTr("%n chosen QSO", "", root.selectedIds.length)
                      : qsTr("Nothing chosen in the queue: the whole queue becomes cards.")
                color: Theme.textSecondary
                font.pixelSize: 11
            }
        }
    }

    // Prima di mandare si dice a chi e con cosa: una email parte e non torna.
    Popup {
        id: mailDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        padding: 16
        width: Math.min(520, root.width - 40)
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: Theme.panelColor; border.color: Theme.glassBorder; radius: 6 }
        contentItem: ColumnLayout {
            spacing: 10
            SectionTitle { text: qsTr("Send the card by email") }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: !root.cards.mail.ready
                      ? (root.cards.mail.route === "cloud"
                         ? qsTr("The Cloud is not linked yet: it goes in Setup → Cloud. Or send from your own "
                                + "mailbox instead, in Setup → QSL services.")
                         : qsTr("There is no outgoing mailbox yet. It goes in Setup → QSL services: "
                                + "the address and the password of the mailbox the cards go out from."))
                      : root.cards.mail.route === "cloud"
                        ? qsTr("Through the Cloud, in your name. For every chosen QSO the address is looked up "
                               + "in the callbook, the card is drawn and sent as a PNG attachment. Stations the "
                               + "callbook has no email for are skipped and said so, and there is a ceiling of "
                               + "cards per day.")
                        : qsTr("From %1. For every chosen QSO the address is looked up in the callbook, "
                               + "the card is drawn and sent as a PNG attachment. Stations the callbook "
                               + "has no email for are skipped and said so.")
                              .arg(root.cards.mail.address)
                color: Theme.textSecondary
                font.pixelSize: 12
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: root.selectedIds.length > 0
                      ? qsTr("%n chosen QSO", "", root.selectedIds.length)
                      : qsTr("Nothing chosen in the queue: the whole queue becomes cards.")
                color: Theme.warningColor
                font.pixelSize: 12
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Item { Layout.fillWidth: true }
                GlassButton { text: qsTr("Cancel"); onClicked: mailDialog.close() }
                GlassButton {
                    text: qsTr("Send")
                    tone: Theme.accentColor
                    filled: true
                    enabled: root.cards.mail.ready
                    onClicked: { mailDialog.close(); root.cards.sendCardsByEmail(root.selectedIds) }
                }
            }
        }
    }

    FileDialog {
        id: templateDialog
        title: qsTr("The image of your QSL card")
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.bmp *.webp)"), qsTr("All files (*)")]
        onAccepted: root.cards.setCardTemplate(selectedFile)
    }
    FileDialog {
        id: pdfDialog
        title: qsTr("QSL cards")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "pdf"
        currentFile: "file:decodxlog-qsl-cards.pdf"
        nameFilters: [qsTr("PDF files (*.pdf)")]
        onAccepted: root.cards.writeCardsPdf(selectedFile, root.selectedIds,
                                             perPageBox.values[perPageBox.currentIndex])
    }
    FolderDialog {
        id: pngDialog
        title: qsTr("Where to put the cards")
        onAccepted: root.cards.writeCardsPng(selectedFolder, root.selectedIds)
    }
}
