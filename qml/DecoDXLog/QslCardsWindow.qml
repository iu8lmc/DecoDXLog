// DecoDXLog — le QSL di carta: coda, stato, etichette.
//
// A sinistra la coda, a destra quello che si fa con le righe scelte. Le etichette
// escono in PDF: una per corrispondente, con dentro fino a quattro QSO, perche'
// una cartolina sola risponde a tutti i collegamenti fatti con quella stazione.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtCore
import Decodium.UI

RoundedWindow {
    id: root

    property string state: "queue"
    // Quale dei due mestieri si sta facendo: "queue" o "card".
    property string view: "queue"
    property int revision: 0
    property var chosen: ({})       // id -> true

    readonly property var rows: { revision; return decolog.cards.rows(root.state, 0) }
    readonly property var counts: { revision; return decolog.cards.counts }

    width: 1020
    height: 680
    minimumWidth: 720
    minimumHeight: 480
    visible: true
    title: qsTr("DecoDXLog — Paper QSL")
    surfaceColor: Theme.bgDeep
    transientParent: null
    modality: Qt.NonModal

    OnScreen { target: root }

    // Niente barra di Windows: la testata e' la nostra, piu' bassa, con gli
    // stessi comandi. Si sposta dalla testata e si ridimensiona dai bordi.
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowMinimizeButtonHint | Qt.WindowMaximizeButtonHint
    header: WindowTitleBar { window: root; cornerRadius: root.frameRadius }
    WindowChrome {
        window: root
        parent: root.contentItem.parent
        dragHeight: Theme.panelHeight
        cornerRadius: root.frameRadius
    }

    Settings {
        category: "cardsWindow"
        property alias width: root.width
        property alias height: root.height
        // Anche la posizione: se la finestra sta sul secondo schermo, e' li'
        // che deve riaprirsi.
        property alias windowX: root.x
        property alias windowY: root.y
    }

    Connections {
        target: decolog.cards
        function onChanged() { root.revision++ }
    }
    Connections {
        target: decolog
        function onLogChanged() { root.revision++ }
    }

    function selectedIds() {
        const out = []
        for (const row of root.rows) {
            if (root.chosen[row.id])
                out.push(row.id)
        }
        return out
    }
    function allIds() {
        return root.rows.map(row => row.id)
    }
    function chooseAll(on) {
        const next = {}
        if (on) {
            for (const row of root.rows)
                next[row.id] = true
        }
        root.chosen = next
    }
    // Per le schermate di prova.
    function showCardMenu() { cardDesign.showKeyMenu() }

    function viaLabel(code) {
        return code === "B" ? qsTr("bureau")
             : code === "D" ? qsTr("direct")
             : code === "E" ? qsTr("electronic")
             : code === "M" ? qsTr("manager") : "—"
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        // Due mestieri diversi nella stessa finestra: tenere la coda, e
        // preparare la cartolina da stampare.
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Repeater {
                model: [{ id: "queue", text: qsTr("Queue & labels") },
                        { id: "card", text: qsTr("QSL card") }]
                GlassButton {
                    required property var modelData
                    text: modelData.text
                    tone: root.view === modelData.id ? Theme.secondaryColor : Theme.glassBorder
                    filled: root.view === modelData.id
                    buttonHeight: 28
                    fontPixelSize: 12
                    onClicked: root.view = modelData.id
                }
            }
            Item { Layout.fillWidth: true }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.borderSoft }

        RowLayout {
            visible: root.view === "queue"
            Layout.fillWidth: true
            spacing: 8

            Repeater {
                model: [{ id: "queue", text: qsTr("To send") },
                        { id: "sent", text: qsTr("Sent") },
                        { id: "received", text: qsTr("Received") },
                        { id: "all", text: qsTr("All") }]
                GlassButton {
                    required property var modelData
                    text: modelData.text + " " + (root.counts[modelData.id] !== undefined
                                                  ? root.counts[modelData.id] : "")
                    tone: root.state === modelData.id ? Theme.primaryColor : Theme.glassBorder
                    filled: root.state === modelData.id
                    buttonHeight: 26
                    fontPixelSize: 12
                    onClicked: { root.state = modelData.id; root.chosen = ({}) }
                }
            }
            Item { Layout.fillWidth: true }
            Text {
                text: qsTr("%1 waiting for an answer").arg(root.counts.unanswered || 0)
                color: (root.counts.unanswered || 0) > 0 ? Theme.warningColor : Theme.textSecondary
                font.pixelSize: 12
            }
            GlassButton {
                text: qsTr("Queue every QSL to answer")
                buttonHeight: 26
                fontPixelSize: 12
                enabled: (root.counts.unanswered || 0) > 0
                onClicked: decolog.cards.enqueueUnanswered("B")
            }
        }

        GlassPanel {
            visible: root.view === "queue"
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: qsTr("Paper QSL · %1 rows · %2 chosen").arg(root.rows.length).arg(root.selectedIds().length)

            ColumnLayout {
                anchors.fill: parent
                spacing: 4

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    CheckBox {
                        Layout.preferredWidth: 30
                        padding: 0
                        checked: root.rows.length > 0 && root.selectedIds().length === root.rows.length
                        onToggled: root.chooseAll(checked)
                    }
                    Repeater {
                        model: [{ t: qsTr("Call"), w: 110 }, { t: qsTr("Date"), w: 100 },
                                { t: qsTr("UTC"), w: 60 }, { t: qsTr("Band"), w: 70 },
                                { t: qsTr("Mode"), w: 70 }, { t: qsTr("RST"), w: 55 },
                                { t: qsTr("Via"), w: 90 }, { t: qsTr("Sent"), w: 90 },
                                { t: qsTr("Received"), w: 90 }]
                        Text {
                            required property var modelData
                            Layout.preferredWidth: modelData.w
                            text: modelData.t
                            color: Theme.secondaryColor
                            font.family: Theme.monoFamily
                            font.pixelSize: 12
                            font.bold: true
                        }
                    }
                    Item { Layout.fillWidth: true }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }

                ListView {
                    id: list
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: root.rows
                    ScrollBar.vertical: ScrollBar {}

                    delegate: Rectangle {
                        required property var modelData
                        width: list.width
                        height: 26
                        color: root.chosen[modelData.id] ? Qt.alpha(Theme.primaryColor, 0.16) : "transparent"

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                const next = Object.assign({}, root.chosen)
                                if (next[modelData.id])
                                    delete next[modelData.id]
                                else
                                    next[modelData.id] = true
                                root.chosen = next
                            }
                            onDoubleClicked: window.openQso(modelData.id)
                        }

                        RowLayout {
                            anchors.fill: parent
                            spacing: 0
                            CheckBox {
                                Layout.preferredWidth: 30
                                padding: 0
                                checked: root.chosen[modelData.id] === true
                                onToggled: {
                                    const next = Object.assign({}, root.chosen)
                                    if (checked)
                                        next[modelData.id] = true
                                    else
                                        delete next[modelData.id]
                                    root.chosen = next
                                }
                            }
                            Repeater {
                                model: [{ v: modelData.call, w: 110, bold: true },
                                        { v: decolog.showDate(modelData.date), w: 100 },
                                        { v: modelData.time, w: 60 },
                                        { v: modelData.band, w: 70 },
                                        { v: modelData.mode, w: 70 },
                                        { v: modelData.rst, w: 55 },
                                        { v: root.viaLabel(modelData.via), w: 90 },
                                        { v: modelData.sent === "Y" ? modelData.sentDate || "✓" : "—", w: 90 },
                                        { v: modelData.rcvd === "Y" ? modelData.rcvdDate || "✓" : "—", w: 90 }]
                                Text {
                                    required property var modelData
                                    Layout.preferredWidth: modelData.w
                                    text: modelData.v || "—"
                                    color: modelData.bold ? Theme.textPrimary : Theme.textSecondary
                                    font.family: Theme.monoFamily
                                    font.pixelSize: 12
                                    font.bold: modelData.bold === true
                                    elide: Text.ElideRight
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: [modelData.name, modelData.country].filter(s => s).join(" · ")
                                color: Theme.textSecondary
                                font.pixelSize: 11
                                elide: Text.ElideRight
                            }
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: root.rows.length === 0
                        width: parent.width * 0.7
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        text: root.state === "queue"
                              ? qsTr("Nothing in the queue. Put a QSO here from its card, from the log row menu, "
                                     + "or with “Queue every QSL to answer”.")
                              : qsTr("Nothing here yet.")
                        color: Theme.textSecondary
                        font.pixelSize: 12
                    }
                }
            }
        }

        RowLayout {
            visible: root.view === "queue"
            Layout.fillWidth: true
            spacing: 8

            GlassButton {
                text: qsTr("Queue (bureau)")
                buttonHeight: 26
                fontPixelSize: 12
                enabled: root.selectedIds().length > 0
                onClicked: decolog.cards.enqueue(root.selectedIds(), "B")
            }
            GlassButton {
                text: qsTr("Queue (direct)")
                buttonHeight: 26
                fontPixelSize: 12
                enabled: root.selectedIds().length > 0
                onClicked: decolog.cards.enqueue(root.selectedIds(), "D")
            }
            GlassButton {
                text: qsTr("Mark as sent")
                tone: Theme.accentColor
                buttonHeight: 26
                fontPixelSize: 12
                enabled: root.selectedIds().length > 0
                onClicked: { decolog.cards.markSent(root.selectedIds(), ""); root.chosen = ({}) }
            }
            GlassButton {
                text: qsTr("Out of the queue")
                buttonHeight: 26
                fontPixelSize: 12
                enabled: root.selectedIds().length > 0
                onClicked: { decolog.cards.drop(root.selectedIds()); root.chosen = ({}) }
            }
            Item { Layout.fillWidth: true }
            Text {
                text: decolog.cards.status
                color: Theme.textSecondary
                font.pixelSize: 11
                elide: Text.ElideRight
                Layout.maximumWidth: 320
            }
        }

        GlassPanel {
            visible: root.view === "queue"
            Layout.fillWidth: true
            Layout.preferredHeight: 108
            title: qsTr("Labels · PDF, one label per correspondent")

            RowLayout {
                anchors.fill: parent
                spacing: 12

                LabeledField {
                    label: qsTr("Sheet")
                    StyledComboBox {
                        id: sheetBox
                        Layout.preferredWidth: 340
                        readonly property var ids: decolog.cards.sheets.map(s => s.id)
                        model: decolog.cards.sheets.map(s => s.label)
                        currentIndex: 0
                    }
                }
                LabeledField {
                    label: qsTr("QSO per label")
                    StyledComboBox {
                        id: perBox
                        Layout.preferredWidth: 140
                        model: ["1", "2", "3", "4", "5", "6"]
                        currentIndex: 3
                    }
                }
                ToggleSwitch {
                    id: guides
                    Layout.alignment: Qt.AlignBottom
                    Layout.bottomMargin: 4
                    text: qsTr("Cutting guides")
                    checked: false
                }
                GlassButton {
                    Layout.alignment: Qt.AlignBottom
                    Layout.bottomMargin: 2
                    text: qsTr("Write the PDF…")
                    tone: Theme.primaryColor
                    filled: true
                    buttonHeight: 28
                    enabled: (root.counts.queue || 0) > 0
                    onClicked: saveDialog.open()
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: decolog.cards.lastFile.length
                          ? qsTr("Last file: %1").arg(decolog.cards.lastFile)
                          : qsTr("The queue becomes labels: the QSOs of one station end up on the same label.")
                    color: Theme.textSecondary
                    font.pixelSize: 11
                }
            }
        }

        QslCardDesign {
            id: cardDesign
            visible: root.view === "card"
            Layout.fillWidth: true
            Layout.fillHeight: true
            selectedIds: root.selectedIds()
        }
    }

    FileDialog {
        id: saveDialog
        title: qsTr("QSL labels")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "pdf"
        currentFile: "file:decolog-qsl.pdf"
        nameFilters: [qsTr("PDF files (*.pdf)")]
        onAccepted: decolog.cards.writeLabels(selectedFile, sheetBox.ids[sheetBox.currentIndex],
                                              perBox.currentIndex + 1, guides.checked)
    }
}
