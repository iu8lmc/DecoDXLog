// DecoDXLog — il pannello inferiore a schede: award, statistiche, QSL, attività.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Decodium.UI

GlassPanel {
    id: root
    // Per le prove: la scelta del periodo LoTW aperta.
    function openLotwPeriod() { lotwPeriod.open() }
    // Per le prove: la previsione verso un locatore.
    function showForecast(grid) { currentTab = 5; propagationPanel.showForecast(grid) }

    // 0 Awards · 1 Statistics · 2 QSL Upload · 3 Activity log · 4 DX Cluster · 5 Propagation
    property int currentTab: 3
    signal awardRequested(string id)
    signal clusterRequested(int tab)
    signal statsRequested()

    padding: 0
    headerLeading: [
        Row {
            spacing: 2
            Repeater {
                model: [qsTr("Awards"), qsTr("Statistics"), qsTr("QSL Upload"), qsTr("Activity log"),
                        qsTr("DX Cluster"), qsTr("Propagation")]
                TabChip {
                    required property string modelData
                    required property int index
                    text: modelData
                    badge: index === 4 && decolog.cluster.spots.count > 0 ? String(decolog.cluster.spots.count) : ""
                    active: root.currentTab === index
                    onClicked: root.currentTab = index
                }
            }
        }
    ]
    headerTools: [
        GlassButton {
            visible: root.currentTab === 1
            text: qsTr("Open statistics")
            tone: Theme.primaryColor
            buttonHeight: 24
            fontPixelSize: 11
            onClicked: root.statsRequested()
        },
        GlassButton {
            visible: root.currentTab === 4
            text: qsTr("Open cluster window")
            tone: Theme.primaryColor
            buttonHeight: 24
            fontPixelSize: 11
            onClicked: root.clusterRequested(0)
        },
        GlassButton {
            visible: root.currentTab === 3
            text: qsTr("Clear")
            buttonHeight: 24
            fontPixelSize: 11
            onClicked: decolog.clearActivity()
        }
    ]
    showDot: false

    function categoryColor(category) {
        switch (category) {
        case "UDP": return Theme.accentColor
        case "SYNC": return Theme.secondaryColor
        case "LOTW":
        case "EQSL":
        case "QRZ": return Theme.primaryColor
        case "CLUSTER": return Theme.warningColor
        case "LOG": return Theme.primaryColor
        case "IMPORT":
        case "EXPORT": return Theme.secondaryColor
        default: return Theme.textSecondary
        }
    }

    component Cell: Text {
        property bool heading: false
        color: heading ? Theme.textSecondary : Theme.textPrimary
        font.family: Theme.monoFamily
        font.pixelSize: 12
        font.bold: !heading
    }

    component BarRow: RowLayout {
        property string key: ""
        property int count: 0
        property int maximum: 1
        property color barColor: Theme.primaryColor
        spacing: 8
        Text {
            Layout.preferredWidth: 52
            text: parent.key
            color: Theme.textPrimary
            font.family: Theme.monoFamily
            font.pixelSize: 12
            elide: Text.ElideRight
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 8
            radius: 4
            color: Theme.bgMedium
            Rectangle {
                width: parent.width * (parent.parent.count / Math.max(1, parent.parent.maximum))
                height: parent.height
                radius: 4
                color: parent.parent.barColor
            }
        }
        Text {
            Layout.preferredWidth: 52
            horizontalAlignment: Text.AlignRight
            text: parent.count.toLocaleString(Qt.locale("en_US"), "f", 0)
            color: Theme.textSecondary
            font.family: Theme.monoFamily
            font.pixelSize: 12
        }
    }

    StackLayout {
        anchors.fill: parent
        anchors.margins: 10
        anchors.topMargin: 6
        currentIndex: root.currentTab

        // ── Awards ──────────────────────────────────────────────────────────
        // Le mattonelle sono tante e il pannello e' basso: si scorre, non si taglia.
        ScrollView {
            id: awardsScroll
            clip: true
            contentWidth: availableWidth
            ScrollBar.vertical: PanelScrollBar {}

            Flow {
            width: awardsScroll.availableWidth
            spacing: 8
            Repeater {
                // Solo quando la scheda si vede: nascosta non serve a nessuno.
                model: root.currentTab === 0 ? decolog.awardSummary : []
                Rectangle {
                    id: tile
                    required property var modelData
                    width: 150
                    height: 58
                    radius: 5
                    color: tileArea.containsMouse ? Theme.glassOverlay : Theme.bgMedium
                    border.width: 1
                    border.color: modelData.id === "ft2" ? Theme.accentColor : Theme.borderSoft
                    Column {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 2
                        Text {
                            width: parent.width
                            text: tile.modelData.title
                            color: tile.modelData.id === "ft2" ? Theme.accentColor : Theme.secondaryColor
                            font.family: Theme.monoFamily
                            font.pixelSize: 11
                            font.bold: true
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            text: tile.modelData.worked + (tile.modelData.total > 0 ? " / " + tile.modelData.total : "")
                            color: Theme.textPrimary
                            font.family: Theme.monoFamily
                            font.pixelSize: 15
                            font.bold: true
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            text: qsTr("%1 confirmed").arg(tile.modelData.confirmed)
                            color: Theme.textSecondary
                            font.family: Theme.monoFamily
                            font.pixelSize: 10
                            elide: Text.ElideRight
                        }
                    }
                    MouseArea {
                        id: tileArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.awardRequested(tile.modelData.id)
                    }
                }
            }
        }
        }

        // ── Statistiche ─────────────────────────────────────────────────────
        // Bande e modi possono essere parecchi: la scheda scorre.
        ScrollView {
            id: statsScroll
            clip: true
            contentWidth: availableWidth
            ScrollBar.vertical: PanelScrollBar {}

            RowLayout {
            width: statsScroll.availableWidth
            spacing: 24
            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 4
                SectionTitle { text: qsTr("By band") }
                // Tutte le bande lavorate, non le prime otto: chi scende in 12,
                // 10, 6, 2 metri o piu' in su le vuole vedere.
                Repeater {
                    model: root.currentTab === 1 ? decolog.bandStats : []
                    BarRow {
                        required property var modelData
                        Layout.fillWidth: true
                        key: modelData.key
                        count: modelData.count
                        maximum: root.currentTab === 1 ? Math.max.apply(null, decolog.bandStats.map(r => r.count)) : 1
                        barColor: Theme.primaryColor
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 4
                SectionTitle { text: qsTr("By mode") }
                Repeater {
                    model: root.currentTab === 1 ? decolog.modeStats : []
                    BarRow {
                        required property var modelData
                        Layout.fillWidth: true
                        key: modelData.key
                        count: modelData.count
                        maximum: root.currentTab === 1 ? Math.max.apply(null, decolog.modeStats.map(r => r.count)) : 1
                        barColor: modelData.key === "FT2" ? Theme.accentColor : Theme.secondaryColor
                    }
                }
            }
        }
        }

        // ── Invio QSL ───────────────────────────────────────────────────────
        ScrollView {
            id: qslScroll
            clip: true
            contentWidth: availableWidth
            ScrollBar.vertical: PanelScrollBar {}

            ColumnLayout {
            width: qslScroll.availableWidth
            spacing: 6

            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.textSecondary
                font.pixelSize: 11
                text: qsTr("LoTW: each click sends at most 500 QSOs using TQSL. An upload is not yet a LoTW confirmation; check processing in LoTW Your Activity.")
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 0
                Repeater {
                model: [qsTr("Service"), qsTr("Account / profile"), qsTr("To send"), qsTr("Sent"), qsTr("Confirmed"), qsTr("Errors")]
                    Text {
                        required property string modelData
                        required property int index
                        Layout.preferredWidth: index === 0 ? 120 : index === 1 ? 220 : 80
                        text: modelData
                        color: Theme.secondaryColor
                        font.family: Theme.monoFamily
                        font.pixelSize: 12
                        font.bold: true
                    }
                }
                Item { Layout.fillWidth: true }
            }

            Repeater {
                model: decolog.qsl.services
                RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 0
                    Text { Layout.preferredWidth: 120; text: modelData.label; color: Theme.textPrimary; font.family: Theme.monoFamily; font.pixelSize: 12; font.bold: true }
                    Text { Layout.preferredWidth: 220; text: (modelData.account || qsTr("not configured")) + " · " + (modelData.profile || qsTr("active station")); color: Theme.textSecondary; font.family: Theme.monoFamily; font.pixelSize: 11; elide: Text.ElideRight }
                    Text { Layout.preferredWidth: 80; text: modelData.pending || 0; color: modelData.pending ? Theme.warningColor : Theme.textSecondary; font.family: Theme.monoFamily; font.pixelSize: 12 }
                    Text { Layout.preferredWidth: 80; text: modelData.sent || 0; color: Theme.textPrimary; font.family: Theme.monoFamily; font.pixelSize: 12 }
                    Text { Layout.preferredWidth: 80; text: modelData.confirmed || 0; color: modelData.confirmed ? Theme.accentColor : Theme.textSecondary; font.family: Theme.monoFamily; font.pixelSize: 12 }
                    Text { Layout.preferredWidth: 80; text: modelData.errors || 0; color: modelData.errors ? Theme.errorColor : Theme.textSecondary; font.family: Theme.monoFamily; font.pixelSize: 12 }
                    GlassButton {
                        text: modelData.busy ? qsTr("sending…") : qsTr("Send %1").arg(modelData.pending || 0)
                        tone: Theme.accentColor
                        buttonHeight: 22
                        fontPixelSize: 11
                        enabled: !decolog.qsl.busy && modelData.ready && (modelData.pending || 0) > 0
                        onClicked: decolog.qsl.uploadPending(modelData.id, 0)
                    }
                    ToggleSwitch {
                        Layout.leftMargin: 10
                        text: qsTr("automatic")
                        checked: modelData.auto
                        enabled: modelData.ready
                        onToggled: decolog.qsl.setAutoUpload(modelData.id, checked)
                    }
                    Text {
                        Layout.fillWidth: true
                        Layout.leftMargin: 10
                        elide: Text.ElideRight
                        text: modelData.ready ? modelData.lastResult : modelData.hint
                        color: modelData.ready ? Theme.textSecondary : Theme.warningColor
                        font.pixelSize: 11
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                GlassButton {
                    text: qsTr("Integration diagnostics…")
                    buttonHeight: 24
                    fontPixelSize: 11
                    onClicked: diagnosticDialog.open()
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Runs locally; no test QSO is uploaded")
                    color: Theme.textSecondary
                    font.pixelSize: 10
                }
            }

            RowLayout {
                Layout.topMargin: 4
                spacing: 8
                GlassButton {
                    text: decolog.lotwBusy ? qsTr("LoTW…") : qsTr("Download LoTW confirmations")
                    tone: Theme.primaryColor
                    buttonHeight: 24
                    fontPixelSize: 11
                    enabled: !decolog.lotwBusy
                    onClicked: decolog.syncLotw(false)
                }
                // Lo scarico per un periodo: dal … al …, per LoTW, eQSL o QRZ.
                GlassButton {
                    id: lotwPeriodButton
                    text: qsTr("Period from… to…")
                    buttonHeight: 24
                    fontPixelSize: 11
                    enabled: !decolog.lotwBusy || !decolog.confirmBusy
                    onClicked: lotwPeriod.open()
                    Popup {
                        id: lotwPeriod
                        y: -implicitHeight - 6
                        padding: 12
                        modal: true
                        popupType: Popup.Window
                        // In una finestra sua il popup riceve i tasti solo se ha il fuoco:
                        // senza "focus: true" il campo prende il cursore ma la data che si
                        // scrive si perde.
                        focus: true
                        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                        onOpened: periodRow.focusFirst()
                        background: Rectangle { color: Theme.panelColor; border.color: Theme.glassBorder; radius: 6 }
                        contentItem: ColumnLayout {
                            spacing: 8
                            Text {
                                text: qsTr("Confirmations of the QSOs made in this period")
                                color: Theme.textSecondary
                                font.pixelSize: 12
                            }
                            LotwRangeRow { id: periodRow; pickService: true; onStarted: lotwPeriod.close() }
                        }
                    }
                }
                // Le conferme degli altri due servizi: solo le nuove dall'ultimo scarico.
                Repeater {
                    model: [{ id: "eqsl", label: "eQSL" }, { id: "qrz", label: "QRZ" }]
                    GlassButton {
                        required property var modelData
                        text: decolog.confirmBusyService === modelData.id ? modelData.label + "…"
                              : qsTr("%1 confirmations").arg(modelData.label)
                        tone: Theme.primaryColor
                        buttonHeight: 24
                        fontPixelSize: 11
                        enabled: !decolog.confirmBusy
                        onClicked: {
                            confirmRow.last = "confirm"
                            decolog.syncConfirmations(modelData.id, false)
                        }
                    }
                }
                // Il riepilogo dell'ultimo scarico di ogni servizio.
                GlassButton {
                    readonly property int fresh: {
                        let n = 0
                        for (const r of (decolog.qslImport.runs || []))
                            n += r.confirmed
                        return n
                    }
                    text: fresh > 0 ? qsTr("Import summary (%1 new)").arg(fresh) : qsTr("Import summary")
                    buttonHeight: 24
                    fontPixelSize: 11
                    onClicked: window.openQslImport()
                }
                GlassButton {
                    text: qsTr("Paper QSL (%1)").arg(decolog.cards.counts.queue || 0)
                    buttonHeight: 24
                    fontPixelSize: 11
                    onClicked: window.openCards("queue")
                }
                GlassButton {
                    // Dritto alla cartolina: e' li' che si carica la propria QSL
                    // e si mettono i campi al loro posto.
                    text: qsTr("QSL card")
                    buttonHeight: 24
                    fontPixelSize: 11
                    onClicked: window.openCards("card")
                }
                GlassButton {
                    visible: decolog.qsl.busy || decolog.confirmBusy
                    text: qsTr("Stop")
                    buttonHeight: 24
                    fontPixelSize: 11
                    onClicked: {
                        if (decolog.qsl.busy)
                            decolog.qsl.cancel()
                        if (decolog.confirmBusy)
                            decolog.cancelConfirmations()
                    }
                }
                Text {
                    id: confirmRow
                    // Lo stato dell'ultimo scarico partito: LoTW, o eQSL/QRZ.
                    property string last: "lotw"
                    Connections {
                        target: decolog
                        function onLotwChanged() { if (decolog.lotwBusy) confirmRow.last = "lotw" }
                        // Anche quando parte da solo, a orario.
                        function onConfirmChanged() { if (decolog.confirmBusy) confirmRow.last = "confirm" }
                    }
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: last === "confirm" && decolog.confirmStatus.length ? decolog.confirmStatus
                          : decolog.lotwStatus.length ? decolog.lotwStatus
                          : decolog.lotwLastSync.length ? qsTr("LoTW last sync %1").arg(decolog.lotwLastSync)
                          : qsTr("LoTW: TQSL signs and sends, and the confirmations come back here.")
                    color: Theme.textSecondary
                    font.pixelSize: 11
                }
            }
            Item { Layout.fillHeight: true }
        }

        // Una finestra del programma (come le altre), non un Dialog di stile:
        // con i temi scuri come Ocean Blue il Dialog di stile resta chiaro e il
        // testo del tema non si legge.
        DialogFrame {
            id: diagnosticDialog
            title: qsTr("Integration diagnostics")
            dialogKey: "integration-diagnostics"
            dotColor: Theme.accentColor
            width: 640
            height: 540
            property var steps: []
            onOpened: {
                const started = Date.now()
                steps = decolog.integrationDiagnostics()
                Qt.callLater(function() { decolog.recordUiLatency("integration-diagnostics.open", Date.now() - started) })
            }
            body: ColumnLayout {
                anchors.margins: 16
                spacing: 8
                Text {
                    Layout.fillWidth: true
                    text: qsTr("The checks stay on this computer. The support report is redacted and is saved only after confirmation.")
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondary
                }
                Repeater {
                    model: diagnosticDialog.steps
                    RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Text { Layout.preferredWidth: 110; text: modelData.label; color: Theme.textPrimary; font.bold: true }
                        Text { Layout.preferredWidth: 70; text: modelData.state; color: modelData.state === "ok" ? Theme.accentColor : modelData.state === "error" ? Theme.errorColor : Theme.warningColor }
                        Text { Layout.fillWidth: true; text: modelData.detail; color: Theme.textSecondary; wrapMode: Text.WordWrap }
                    }
                }
                TextArea {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 110
                    readOnly: true
                    text: decolog.diagnosticSupportText()
                    wrapMode: TextArea.Wrap
                    color: Theme.textSecondary
                    background: Rectangle { color: Theme.bgDeep; radius: 4; border.color: Theme.glassBorder }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    GlassButton {
                        text: qsTr("Approve and save report…")
                        tone: Theme.accentColor
                        onClicked: saveDiagnostic.open()
                    }
                    GlassButton {
                        text: qsTr("Save JSON evidence…")
                        onClicked: saveDiagnosticJson.open()
                    }
                }
            }
        }

        FileDialog {
            id: saveDiagnostic
            title: qsTr("Save diagnostic support report")
            fileMode: FileDialog.SaveFile
            defaultSuffix: "txt"
            nameFilters: [qsTr("Text files (*.txt)"), qsTr("All files (*)")]
            onAccepted: {
                const error = decolog.saveDiagnosticReport(selectedFile)
                if (error.length)
                    diagnosticDialog.title = error
                else
                    diagnosticDialog.close()
            }
        }
        FileDialog {
            id: saveDiagnosticJson
            title: qsTr("Save structured diagnostic evidence")
            fileMode: FileDialog.SaveFile
            defaultSuffix: "json"
            nameFilters: [qsTr("JSON files (*.json)"), qsTr("All files (*)")]
            onAccepted: {
                const error = decolog.saveDiagnosticJsonReport(selectedFile)
                if (error.length)
                    diagnosticDialog.title = error
                else
                    diagnosticDialog.close()
            }
        }
        }

        // ── Registro attività ───────────────────────────────────────────────
        ListView {
            clip: true
            model: decolog.activity
            ScrollBar.vertical: PanelScrollBar {}
            delegate: Text {
                required property string time
                required property string category
                required property string message
                required property string level
                width: ListView.view.width
                height: 22
                elide: Text.ElideRight
                textFormat: Text.StyledText
                font.family: Theme.monoFamily
                font.pixelSize: 12
                verticalAlignment: Text.AlignVCenter
                color: level === "error" ? Theme.errorColor
                     : level === "warning" ? Theme.warningColor
                     : level === "highlight" && (category === "LOTW" || category === "EQSL" || category === "QRZ") ? Theme.accentColor
                     : level === "highlight" && category === "CLUSTER" ? Theme.warningColor
                     : Theme.textPrimary
                text: "<font color=\"" + Theme.textSecondary + "\">" + time + "</font> "
                      + "<font color=\"" + root.categoryColor(category) + "\">" + category + "</font> "
                      + message.replace(/&/g, "&amp;").replace(/</g, "&lt;")
                          .replace(/( · new DXCC on FT2: .*)$/, "<font color=\"" + Theme.warningColor + "\">$1</font>")
            }
        }

        // ── DX Cluster ──────────────────────────────────────────────────────
        ClusterPanel {
            compact: true
            onWindowRequested: (tab) => root.clusterRequested(tab)
        }

        // ── Propagazione ────────────────────────────────────────────────────
        PropagationPanel { id: propagationPanel }
    }
}
