// DecoDXLog — il riepilogo delle conferme scaricate (LoTW, eQSL, QRZ).
//
// Dopo uno scarico si vuole sapere cosa e' arrivato: quante nuove, quante gia'
// segnate, quante non trovano il loro QSO e perche'. Qui c'e' l'ultimo scarico
// di ogni servizio, con i filtri, i nominativi, e la cartolina eQSL della riga
// scelta (chiesta a eQSL solo quando la si guarda: lo vuole eQSL).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decodium.UI

DialogFrame {
    id: root

    signal openQsoRequested(var id)

    readonly property var summary: decolog.qslImport
    readonly property var runs: summary.runs || []
    readonly property var rows: summary.rows || []

    // I filtri: il servizio (etichetta dello scarico), l'esito, il nominativo.
    property string labelFilter: ""
    property string kindFilter: "new"
    property string search: ""
    property bool onlyNewDxcc: false
    readonly property var shown: {
        const f = root.search.trim().toUpperCase()
        return root.rows.filter(r => (root.labelFilter.length === 0 || r.label === root.labelFilter)
                                     && (root.kindFilter.length === 0 || r.kind === root.kindFilter || (root.kindFilter === "errors" && (r.kind === "invalid" || r.kind === "database_error")))
                                     && (!root.onlyNewDxcc || r.newDxcc === true)
                                     && (f.length === 0 || String(r.call).indexOf(f) >= 0
                                         || String(r.country || "").toUpperCase().indexOf(f) >= 0))
    }
    property var selected: null
    function probeAmbiguous() {
        root.kindFilter = "ambiguous"
        console.warn("PROBE qsl import ambiguous count=" + root.shown.length + " label=" + root.kindLabel("ambiguous") + " text=" + root.asText())
        root.kindFilter = "errors"
        console.warn("PROBE qsl import errors count=" + root.shown.length + " label=" + root.kindLabel("database_error") + " text=" + root.asText())
    }

    // La cartolina della riga scelta.
    property string cardKey: ""
    property string cardUrl: ""
    property string cardError: ""
    property bool cardLoading: false
    function showCard(row) {
        root.cardUrl = ""
        root.cardError = ""
        root.cardKey = ""
        root.cardLoading = false
        if (!row || row.service !== "eqsl" || row.kind === "invalid" || row.kind === "database_error")
            return
        root.cardLoading = true
        root.cardKey = decolog.requestEqslCard(row)
    }
    Connections {
        target: decolog
        function onEqslCardReady(key, fileUrl, error) {
            if (key !== root.cardKey)
                return
            root.cardLoading = false
            root.cardUrl = fileUrl
            root.cardError = error
        }
    }

    function kindLabel(kind) {
        return kind === "new" ? qsTr("new") : kind === "notfound" ? qsTr("not matched") : kind === "ambiguous" ? qsTr("ambiguous") : kind === "database_error" ? qsTr("database error") : qsTr("invalid")
    }
    function kindTone(kind) {
        return kind === "new" ? Theme.accentColor : (kind === "notfound" || kind === "ambiguous") ? Theme.warningColor : Theme.errorColor
    }
    function candidateText(row) {
        if (!row || row.kind !== "ambiguous" || !(row.candidateIds || []).length) return ""
        const ids = row.candidateIds.join(", ")
        return qsTr("QSO IDs: %1").arg(ids) + ((row.candidateCount || row.candidateIds.length) > row.candidateIds.length ? " …" : "")
    }
    // Il riepilogo come testo, per copiarlo dove serve.
    function asText() {
        let out = []
        for (const run of root.runs)
            out.push((run.error || "").length > 0
                     ? "%1 (%2): %3".arg(run.label).arg(run.whenText || run.when).arg(run.error)
                     : qsTr("%1 (%2): %3 new, %4 already marked, %5 not matched, %6 invalid, %7 ambiguous, %8 database errors")
                       .arg(run.label).arg(run.whenText || run.when).arg(run.confirmed).arg(run.already)
                       .arg(run.notFound).arg(run.invalid || 0).arg(run.ambiguous || 0).arg(run.databaseErrors || 0))
        out.push("")
        for (const r of root.shown)
            out.push("%1; %2; %3; %4; %5; %6%7".arg(r.utc).arg(r.call).arg(r.band).arg(r.mode).arg(r.label)
                     .arg(root.kindLabel(r.kind))
                     .arg(r.kind === "new" ? (r.country ? "; " + r.country + (r.newDxcc ? " — " + qsTr("new DXCC") : "") : "")
                                           : (r.reason ? "; " + r.reason : "")) + (root.candidateText(r) ? "; " + root.candidateText(r) : ""))
        return out.join("\n")
    }

    title: qsTr("QSL import summary")
    dotColor: Theme.accentColor
    info: qsTr("%1 rows shown").arg(root.shown.length)
    dialogKey: "qslimport"
    width: 1180
    height: 700

    onClosed: root.selected = null

    readonly property var cols: [
        { key: "utc", title: qsTr("UTC"), w: 130 },
        { key: "call", title: qsTr("Call"), w: 110 },
        { key: "band", title: qsTr("Band"), w: 56 },
        { key: "mode", title: qsTr("Mode"), w: 60 },
        { key: "station", title: qsTr("My call"), w: 96 },
        { key: "label", title: qsTr("Service"), w: 110 },
        { key: "kind", title: qsTr("Result"), w: 100 },
        { key: "detail", title: qsTr("DXCC / reason"), w: 0 }
    ]

    body: ColumnLayout {
        spacing: 10
        anchors.margins: 14

        // Gli scarichi, uno per servizio: i conti.
        Flow {
            Layout.fillWidth: true
            spacing: 8
            Repeater {
                model: root.runs
                Rectangle {
                    required property var modelData
                    width: runText.implicitWidth + 24
                    height: runText.implicitHeight + 14
                    radius: 6
                    color: root.labelFilter === modelData.label ? Theme.panelHeader : Theme.bgMedium
                    border.color: root.labelFilter === modelData.label ? Theme.primaryColor
                                : (modelData.error || "").length > 0 ? Theme.errorColor : Theme.glassBorder
                    border.width: 1
                    Text {
                        id: runText
                        anchors.centerIn: parent
                        textFormat: Text.StyledText
                        text: (modelData.error || "").length > 0
                              ? "<b>%1</b> · %2<br><font color=\"%3\">%4</font>".arg(modelData.label)
                                    .arg(modelData.whenText || "").arg(Theme.errorColor).arg(modelData.error)
                              : "<b>%1</b> · %2<br>%3 <font color=\"%4\">%5</font> · %6 %7 · %8 <font color=\"%9\">%10</font> · %11 %12 · %13 %14 · %15 %16"
                              .arg(modelData.label).arg(modelData.whenText || "")
                              .arg(qsTr("new ones")).arg(Theme.accentColor).arg(modelData.confirmed)
                              .arg(qsTr("already marked")).arg(modelData.already)
                              .arg(qsTr("not matched")).arg(Theme.warningColor).arg(modelData.notFound)
                              .arg(qsTr("invalid")).arg(modelData.invalid || 0).arg(qsTr("ambiguous")).arg(modelData.ambiguous || 0)
                              .arg(qsTr("database errors")).arg(modelData.databaseErrors || 0)
                        color: Theme.textPrimary
                        font.pixelSize: 12
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.labelFilter = root.labelFilter === parent.modelData.label ? "" : parent.modelData.label
                    }
                }
            }
            Text {
                visible: root.runs.length === 0
                text: qsTr("No confirmations downloaded yet: use LoTW, eQSL or QRZ in the QSL tab.")
                color: Theme.textSecondary
                font.pixelSize: 12
            }
        }

        // I filtri.
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            StyledComboBox {
                Layout.preferredWidth: 170
                readonly property var labels: [""].concat(root.runs.map(r => r.label))
                model: labels.map(l => l.length ? l : qsTr("All services"))
                currentIndex: Math.max(0, labels.indexOf(root.labelFilter))
                onActivated: root.labelFilter = labels[currentIndex]
            }
            StyledComboBox {
                Layout.preferredWidth: 170
                readonly property var kinds: ["new", "notfound", "ambiguous", "errors", ""]
                model: [qsTr("New confirmations"), qsTr("Not matched"), qsTr("Ambiguous"), qsTr("Errors"), qsTr("Everything")]
                currentIndex: Math.max(0, kinds.indexOf(root.kindFilter))
                onActivated: root.kindFilter = kinds[currentIndex]
            }
            ToggleSwitch {
                text: qsTr("only new DXCC")
                checked: root.onlyNewDxcc
                onToggled: root.onlyNewDxcc = checked
            }
            StyledTextField {
                Layout.preferredWidth: 220
                mono: false
                placeholderText: qsTr("Call or country…")
                onTextChanged: root.search = text
            }
            Item { Layout.fillWidth: true }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            // L'elenco.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: Theme.rowHeight
                    color: Theme.panelHeader
                    radius: 4
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        Repeater {
                            model: root.cols
                            Text {
                                required property var modelData
                                width: modelData.w > 0 ? modelData.w : 260
                                leftPadding: 8
                                text: modelData.title
                                color: Theme.secondaryColor
                                font.family: Theme.monoFamily
                                font.pixelSize: 11
                                font.bold: true
                            }
                        }
                    }
                }
                ListView {
                    id: list
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: root.shown
                    ScrollBar.vertical: PanelScrollBar {}
                    delegate: Rectangle {
                        id: line
                        required property var modelData
                        required property int index
                        readonly property bool chosen: root.selected === modelData
                        width: list.width
                        height: Theme.rowHeight
                        color: chosen ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.22)
                             : pick.containsMouse ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.08)
                             : modelData.newDxcc ? Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.12)
                             : "transparent"
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            Repeater {
                                model: root.cols
                                Text {
                                    required property var modelData
                                    width: modelData.w > 0 ? modelData.w : Math.max(120, list.width - 662)
                                    leftPadding: 8
                                    elide: Text.ElideRight
                                    readonly property var r: line.modelData
                                    text: modelData.key === "kind" ? root.kindLabel(r.kind)
                                        : modelData.key === "detail"
                                          ? (r.kind === "new" ? (r.country || "") + (r.newDxcc ? "  ★ " + qsTr("new DXCC") : "")
                                                              : (r.reason || ""))
                                        : (r[modelData.key] || "")
                                    color: modelData.key === "kind" ? root.kindTone(r.kind)
                                         : modelData.key === "detail" && r.newDxcc ? Theme.accentColor
                                         : Theme.textPrimary
                                    font.family: modelData.key === "detail" || modelData.key === "label" ? Theme.uiFamily
                                                                                                         : Theme.monoFamily
                                    font.pixelSize: Theme.fontSize
                                    font.bold: modelData.key === "call"
                                }
                            }
                        }
                        Rectangle {
                            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                            height: 1
                            color: Theme.borderSoft
                        }
                        MouseArea {
                            id: pick
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                root.selected = line.modelData
                                root.showCard(line.modelData)
                            }
                            onDoubleClicked: if (line.modelData.qsoId > 0) root.openQsoRequested(line.modelData.qsoId)
                        }
                    }
                }
            }

            // La riga scelta: la cartolina eQSL, o quello che c'e' da dire.
            Rectangle {
                Layout.preferredWidth: 340
                Layout.fillHeight: true
                radius: 6
                color: Theme.bgMedium
                border.color: Theme.glassBorder
                border.width: 1
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8
                    Text {
                        Layout.fillWidth: true
                        text: root.selected ? "%1 · %2 %3 · %4".arg(root.selected.call).arg(root.selected.band)
                                                                .arg(root.selected.mode).arg(root.selected.utc)
                                            : qsTr("Pick a row")
                        color: Theme.textPrimary
                        font.family: Theme.monoFamily
                        font.pixelSize: 13
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.warningColor
                        visible: !!root.candidateText(root.selected)
                        text: root.candidateText(root.selected) + "\n" + qsTr("Candidates are from the saved download summary; recheck the current log before acting.")
                    }
                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Image {
                            id: card
                            anchors.fill: parent
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                            cache: false
                            source: root.cardUrl
                            visible: root.cardUrl.length > 0
                        }
                        Text {
                            anchors.fill: parent
                            visible: root.cardUrl.length === 0
                            wrapMode: Text.Wrap
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            color: root.cardError.length > 0 ? Theme.warningColor : Theme.textSecondary
                            font.pixelSize: 12
                            text: !root.selected ? qsTr("The eQSL card of a row shows up here.")
                                : root.selected.service !== "eqsl"
                                  ? qsTr("%1 confirmations have no card picture. Double click opens the QSO.").arg(root.selected.label)
                                : root.cardLoading ? qsTr("Asking eQSL for the card… (eQSL wants at most six a minute: it may take a few seconds)")
                                : root.cardError.length > 0 ? root.cardError
                                : ""
                        }
                    }
                    GlassButton {
                        Layout.fillWidth: true
                        visible: root.selected && root.selected.qsoId > 0
                        text: qsTr("Open the QSO")
                        onClicked: root.openQsoRequested(root.selected.qsoId)
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            GlassButton {
                text: qsTr("Copy as text")
                enabled: root.runs.length > 0
                onClicked: {
                    copyBuffer.text = root.asText()
                    copyBuffer.selectAll()
                    copyBuffer.copy()
                    copied.visible = true
                }
            }
            Pill {
                id: copied
                visible: false
                text: qsTr("copied")
                tone: Theme.accentColor
            }
            Item { Layout.fillWidth: true }
            GlassButton {
                text: qsTr("Clear the summary")
                enabled: root.runs.length > 0
                onClicked: { root.selected = null; decolog.clearQslImport() }
            }
            GlassButton { text: qsTr("Close"); onClicked: root.close() }
        }
    }

    TextEdit {
        id: copyBuffer
        visible: false
    }
}
