// DecoDXLog — il pannello inferiore a schede: award, statistiche, QSL, attività.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decodium.UI

GlassPanel {
    id: root
    // Per le prove: la scelta del periodo LoTW aperta.
    function openLotwPeriod() { lotwPeriod.open() }

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
        case "LOTW": return Theme.primaryColor
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
                            text: tile.modelData.title
                            color: tile.modelData.id === "ft2" ? Theme.accentColor : Theme.secondaryColor
                            font.family: Theme.monoFamily
                            font.pixelSize: 11
                            font.bold: true
                        }
                        Text {
                            text: tile.modelData.worked + (tile.modelData.total > 0 ? " / " + tile.modelData.total : "")
                            color: Theme.textPrimary
                            font.family: Theme.monoFamily
                            font.pixelSize: 15
                            font.bold: true
                        }
                        Text {
                            text: qsTr("%1 confirmed").arg(tile.modelData.confirmed)
                            color: Theme.textSecondary
                            font.family: Theme.monoFamily
                            font.pixelSize: 10
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

            RowLayout {
                Layout.fillWidth: true
                spacing: 0
                Repeater {
                    model: [qsTr("Service"), qsTr("To send"), qsTr("Sent"), qsTr("Confirmed"), qsTr("Errors")]
                    Text {
                        required property string modelData
                        required property int index
                        Layout.preferredWidth: index === 0 ? 120 : 80
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
                // Lo scarico per un periodo: dal … al …
                GlassButton {
                    id: lotwPeriodButton
                    text: qsTr("LoTW from… to…")
                    buttonHeight: 24
                    fontPixelSize: 11
                    enabled: !decolog.lotwBusy
                    onClicked: lotwPeriod.open()
                    Popup {
                        id: lotwPeriod
                        y: -implicitHeight - 6
                        padding: 12
                        modal: true
                        popupType: Popup.Window
                        background: Rectangle { color: Theme.panelColor; border.color: Theme.glassBorder; radius: 6 }
                        contentItem: ColumnLayout {
                            spacing: 8
                            Text {
                                text: qsTr("Confirmations of the QSOs made in this period")
                                color: Theme.textSecondary
                                font.pixelSize: 12
                            }
                            LotwRangeRow { onStarted: lotwPeriod.close() }
                        }
                    }
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
                    visible: decolog.qsl.busy
                    text: qsTr("Stop")
                    buttonHeight: 24
                    fontPixelSize: 11
                    onClicked: decolog.qsl.cancel()
                }
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: decolog.lotwStatus.length ? decolog.lotwStatus
                          : decolog.lotwLastSync.length ? qsTr("LoTW last sync %1").arg(decolog.lotwLastSync)
                          : qsTr("LoTW: TQSL signs and sends, and the confirmations come back here.")
                    color: Theme.textSecondary
                    font.pixelSize: 11
                }
            }
            Item { Layout.fillHeight: true }
        }
        }

        // ── Registro attività ───────────────────────────────────────────────
        ListView {
            clip: true
            model: decolog.activity
            ScrollBar.vertical: PanelScrollBar {}
            delegate: Text {
                required property var modelData
                width: ListView.view.width
                height: 22
                elide: Text.ElideRight
                textFormat: Text.StyledText
                font.family: Theme.monoFamily
                font.pixelSize: 12
                verticalAlignment: Text.AlignVCenter
                color: modelData.level === "error" ? Theme.errorColor
                     : modelData.level === "warning" ? Theme.warningColor
                     : modelData.level === "highlight" && modelData.category === "LOTW" ? Theme.accentColor
                     : modelData.level === "highlight" && modelData.category === "CLUSTER" ? Theme.warningColor
                     : Theme.textPrimary
                text: "<font color=\"" + Theme.textSecondary + "\">" + modelData.time + "</font> "
                      + "<font color=\"" + root.categoryColor(modelData.category) + "\">" + modelData.category + "</font> "
                      + modelData.text.replace(/&/g, "&amp;").replace(/</g, "&lt;")
                          .replace(/( · new DXCC on FT2: .*)$/, "<font color=\"" + Theme.warningColor + "\">$1</font>")
            }
        }

        // ── DX Cluster ──────────────────────────────────────────────────────
        ClusterPanel {
            compact: true
            onWindowRequested: (tab) => root.clusterRequested(tab)
        }

        // ── Propagazione ────────────────────────────────────────────────────
        PropagationPanel {}
    }
}
