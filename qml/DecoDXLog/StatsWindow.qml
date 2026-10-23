// DecoDXLog — le statistiche del log: quanto, quando e dove.
//
// Grafici a barre disegnati con i rettangoli del tema (niente librerie): anni,
// mesi, ore UTC, bande, modi, continenti, e la mappa di calore banda per ora, che
// e' il modo piu' rapido di vedere quando una banda e' aperta.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import Decodium.UI

RoundedWindow {
    id: root

    property string mode: ""
    property int year: 0
    property int revision: 0
    // 0 = l'andamento del log, 1 = diplomi e classifiche.
    property int page: 0

    // Le statistiche si contano su un altro filo e arrivano tutte insieme: su
    // un log grande sono secondi, e la finestra intanto resta viva.
    property var stats: ({})
    property bool counting: false
    readonly property var summary: stats.summary || ({})
    readonly property var years: stats.years || []
    readonly property var byYear: stats.byYear || []
    readonly property var byMonth: stats.byMonth || []
    readonly property var byHour: stats.byHour || []
    readonly property var byBand: stats.byBand || []
    readonly property var byMode: stats.byMode || []
    readonly property var byContinent: stats.byContinent || []
    readonly property var bandHour: stats.bandHour || []

    function refresh() {
        counting = true
        decolog.requestStats(mode, year)
    }
    onModeChanged: refresh()
    onYearChanged: refresh()
    onRevisionChanged: refresh()
    Component.onCompleted: refresh()

    width: 1180
    height: 780
    minimumWidth: 760
    minimumHeight: 480
    visible: true
    title: qsTr("DecoDXLog — Statistics")
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
        category: "statsWindow"
        property alias width: root.width
        property alias height: root.height
        // Anche la posizione: se la finestra sta sul secondo schermo, e' li'
        // che deve riaprirsi.
        property alias windowX: root.x
        property alias windowY: root.y
    }

    Connections {
        target: decolog
        function onLogChanged() { root.revision++ }
        function onStatsReady(result) {
            if (result.mode !== root.mode || result.year !== root.year)
                return
            root.stats = result
            root.counting = false
        }
    }

    function maxOf(list) {
        let m = 0
        for (const row of list)
            m = Math.max(m, row.count)
        return m
    }
    function monthLabel(key) {
        const d = new Date(key + "-01T00:00:00Z")
        return d.toLocaleDateString(Qt.locale(), "MMM yy")
    }

    // Un grafico a barre verticali: chiave sotto, valore sopra.
    component Bars: GlassPanel {
        id: chart
        property var rows: []
        property color barColor: Theme.primaryColor
        property int labelEvery: 1
        property bool labelRotated: false
        readonly property int maximum: {
            let m = 0
            for (const row of chart.rows)
                m = Math.max(m, row.count)
            return m
        }
        padding: 8
        Item {
            anchors.fill: parent
            Text {
                anchors.centerIn: parent
                visible: chart.rows.length === 0
                text: qsTr("no QSO")
                color: Theme.textSecondary
                font.pixelSize: 12
            }
            Row {
                anchors.fill: parent
                spacing: 2
                Repeater {
                    model: chart.rows
                    Item {
                        required property var modelData
                        required property int index
                        width: (chart.width - 16 - (chart.rows.length - 1) * 2) / Math.max(1, chart.rows.length)
                        height: parent.height
                        Text {
                            anchors { bottom: bar.top; bottomMargin: 2; horizontalCenter: parent.horizontalCenter }
                            visible: modelData.count > 0 && parent.width > 22
                            text: modelData.count
                            color: Theme.textSecondary
                            font.family: Theme.monoFamily
                            font.pixelSize: 9
                        }
                        Rectangle {
                            id: bar
                            anchors { bottom: label.top; bottomMargin: 3; horizontalCenter: parent.horizontalCenter }
                            // Una barra sola non deve diventare un muro: larghezza al massimo
                            // quella di una colonna leggibile.
                            width: Math.max(3, Math.min(72, parent.width - 4))
                            height: chart.maximum > 0 ? Math.max(modelData.count > 0 ? 2 : 0,
                                                                 (parent.height - 22) * modelData.count / chart.maximum) : 0
                            radius: 2
                            color: chart.barColor
                            opacity: 0.85
                            Behavior on height { NumberAnimation { duration: 120 } }
                        }
                        Text {
                            id: label
                            anchors { bottom: parent.bottom; horizontalCenter: parent.horizontalCenter }
                            visible: index % chart.labelEvery === 0
                            text: modelData.label !== undefined ? modelData.label : modelData.key
                            color: Theme.textSecondary
                            font.family: Theme.monoFamily
                            font.pixelSize: 9
                            elide: Text.ElideRight
                            width: parent.width + 6
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        // ── Filtri e totali ─────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text { text: qsTr("Mode"); color: Theme.textSecondary; font.pixelSize: 12 }
            StyledComboBox {
                Layout.preferredWidth: 160
                readonly property var values: ["", "FT2", "FT8", "FT4", "CW", "SSB", "RTTY", "JTTY"]
                model: [qsTr("All modes"), "FT2", "FT8", "FT4", "CW", "SSB", "RTTY", "JTTY"]
                currentIndex: Math.max(0, values.indexOf(root.mode))
                onActivated: root.mode = values[currentIndex]
            }
            Text { text: qsTr("Year"); color: Theme.textSecondary; font.pixelSize: 12 }
            StyledComboBox {
                Layout.preferredWidth: 140
                readonly property var values: [""].concat(root.years)
                model: [qsTr("All years")].concat(root.years)
                currentIndex: Math.max(0, values.indexOf(root.year === 0 ? "" : String(root.year)))
                onActivated: root.year = currentIndex === 0 ? 0 : parseInt(currentText)
            }
            Text {
                visible: root.counting
                text: qsTr("Counting…")
                color: Theme.accentColor
                font.pixelSize: 12
            }
            Item { Layout.fillWidth: true }
            GlassButton {
                text: qsTr("Activity")
                tone: Theme.primaryColor
                filled: root.page === 0
                onClicked: root.page = 0
            }
            GlassButton {
                text: qsTr("Awards and rankings")
                tone: Theme.primaryColor
                filled: root.page === 1
                onClicked: root.page = 1
            }
            GlassButton { text: qsTr("Refresh"); onClicked: root.revision++ }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            StatTile { Layout.fillWidth: true; label: qsTr("QSO"); value: root.summary.qsos || 0 }
            StatTile { Layout.fillWidth: true; label: qsTr("Different calls"); value: root.summary.calls || 0 }
            StatTile { Layout.fillWidth: true; label: qsTr("DXCC entities"); value: root.summary.dxcc || 0 }
            StatTile { Layout.fillWidth: true; label: qsTr("Grids"); value: root.summary.grids || 0 }
            StatTile { Layout.fillWidth: true; label: qsTr("First QSO"); value: decolog.showDate(root.summary.first || "") || "—" }
            StatTile { Layout.fillWidth: true; label: qsTr("Last QSO"); value: decolog.showDate(root.summary.last || "") || "—" }
            StatTile {
                Layout.fillWidth: true
                label: qsTr("Best day")
                value: root.summary.bestDay ? "%1 · %2".arg(root.summary.bestDay).arg(root.summary.bestDayCount) : "—"
            }
            StatTile {
                Layout.fillWidth: true
                label: qsTr("Best hour")
                value: root.summary.bestHour ? "%1Z · %2".arg(root.summary.bestHour).arg(root.summary.bestHourCount) : "—"
            }
        }

        // ── Grafici ─────────────────────────────────────────────────────────
        // Altezze esplicite: un pannello di grafico non ha una sua altezza
        // naturale, e lasciata al caso si schiaccia sull'intestazione.
        GridLayout {
            visible: root.page === 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: 2
            columnSpacing: 8
            rowSpacing: 8

            Bars {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 150
                title: qsTr("QSO per year")
                dotColor: Theme.primaryColor
                rows: root.byYear
                barColor: Theme.primaryColor
            }
            Bars {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 150
                title: qsTr("QSO per month (last 24)")
                dotColor: Theme.secondaryColor
                rows: root.byMonth.map(r => ({ key: r.key, count: r.count, label: root.monthLabel(r.key) }))
                barColor: Theme.secondaryColor
                labelEvery: 3
            }
            Bars {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 150
                title: qsTr("QSO per UTC hour")
                dotColor: Theme.accentColor
                rows: root.byHour
                barColor: Theme.accentColor
                labelEvery: 2
            }
            Bars {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 150
                title: qsTr("QSO per band")
                dotColor: Theme.warningColor
                rows: root.byBand
                barColor: Theme.warningColor
            }

            // ── Mappa di calore: banda per ora ──────────────────────────────
            GlassPanel {
                id: heat
                Layout.fillWidth: true
                Layout.columnSpan: 2
                Layout.preferredHeight: 52 + Math.max(1, heat.bands.length) * 20
                title: qsTr("When a band is open · QSO per band and UTC hour")
                dotColor: Theme.accentColor
                padding: 8

                readonly property var bands: {
                    const seen = []
                    for (const row of root.byBand)
                        seen.push(row.key)
                    return seen
                }
                readonly property var cells: {
                    const map = ({})
                    let peak = 0
                    for (const row of root.bandHour) {
                        map[row.band + "|" + row.hour] = row.count
                        peak = Math.max(peak, row.count)
                    }
                    map.__peak = peak
                    return map
                }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 2
                    Repeater {
                        model: heat.bands
                        RowLayout {
                            required property string modelData
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                Layout.preferredWidth: 46
                                text: modelData
                                color: Theme.textSecondary
                                font.family: Theme.monoFamily
                                font.pixelSize: 11
                            }
                            Repeater {
                                model: 24
                                Rectangle {
                                    required property int index
                                    readonly property string band: parent.modelData
                                    readonly property int count: heat.cells[band + "|" + index] || 0
                                    readonly property real share: heat.cells.__peak > 0 ? count / heat.cells.__peak : 0
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 18
                                    radius: 2
                                    color: count > 0 ? Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b,
                                                               0.12 + 0.8 * Math.sqrt(share))
                                                     : Theme.bgMedium
                                    border.width: 1
                                    border.color: Theme.borderSoft
                                    ToolTip.visible: cellArea.containsMouse && count > 0
                                    ToolTip.text: qsTr("%1 · %2Z · %3 QSO").arg(band)
                                                  .arg(String(index).padStart(2, "0")).arg(count)
                                    MouseArea { id: cellArea; anchors.fill: parent; hoverEnabled: true }
                                }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Item { Layout.preferredWidth: 46; Layout.preferredHeight: 12 }
                        Repeater {
                            model: 24
                            Text {
                                required property int index
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                                text: index % 2 === 0 ? String(index).padStart(2, "0") : ""
                                color: Theme.textSecondary
                                font.family: Theme.monoFamily
                                font.pixelSize: 9
                            }
                        }
                    }
                    Item { Layout.fillHeight: true }
                }
            }
        }

        // ── Modi e continenti ───────────────────────────────────────────────
        RowLayout {
            visible: root.page === 0
            Layout.fillWidth: true
            Layout.preferredHeight: 96
            Layout.maximumHeight: 96
            spacing: 8
            GlassPanel {
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: qsTr("Modes")
                dotColor: Theme.primaryColor
                padding: 8
                Flow {
                    anchors.fill: parent
                    spacing: 6
                    Repeater {
                        model: root.byMode
                        Pill {
                            required property var modelData
                            text: "%1 · %2".arg(modelData.key).arg(modelData.count)
                            tone: modelData.key === "FT2" ? Theme.accentColor : Theme.primaryColor
                        }
                    }
                }
            }
            GlassPanel {
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: qsTr("Continents")
                dotColor: Theme.secondaryColor
                padding: 8
                Text {
                    anchors.centerIn: parent
                    visible: root.byContinent.length === 0
                    text: qsTr("no continent on these QSOs (fill in the DXCC)")
                    color: Theme.textSecondary
                    font.pixelSize: 12
                }
                Flow {
                    anchors.fill: parent
                    spacing: 6
                    Repeater {
                        model: root.byContinent
                        Pill {
                            required property var modelData
                            text: "%1 · %2".arg(modelData.key).arg(modelData.count)
                            tone: Theme.secondaryColor
                        }
                    }
                }
            }
        }

        StatsAwardsPage {
            visible: root.page === 1
            Layout.fillWidth: true
            Layout.fillHeight: true
            stats: root.stats
        }
    }
}
