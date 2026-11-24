// DecoDXLog — le statistiche del log: quanto, quando e dove.
//
// Grafici a barre disegnati con i rettangoli del tema (niente librerie): anni,
// mesi, ore UTC, bande, modi, continenti, e la mappa di calore banda per ora, che
// e' il modo piu' rapido di vedere quando una banda e' aperta.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore
import Decodium.UI

RoundedWindow {
    id: root
    function probeLocalFileUrl(path) { const p=String(path).replace(/\\/g,"/"); return "file://" + (p.startsWith("/") ? "" : "/") + encodeURI(p).replace(/#/g,"%23").replace(/\?/g,"%3F") }

    property string mode: ""
    property int year: 0
    property int revision: 0
    // 0 = l'andamento del log, 1 = diplomi e classifiche.
    property int page: 0
    property var activityReport: ({})
    property bool activityBusy:false
    property string reportMessage:""
    property url reportProbePath:""
    property bool reportProbePdf:false
    property bool reportPdfBusy:false
    property bool customRestoreProbe:false
    property var savedReports:[]
    property string savedReportsPath:""
    property var loadedReport:({})
    property bool namedRestoreProbe:false
    property bool daysProbe:false
    property bool loadProbe:false
    property double loadStart:0
    property int loadTicks:0
    property double loadLast:0
    property double loadMaxGap:0
    Timer { id:loadProbeTimer; interval:10; repeat:true; onTriggered:{ const now=Date.now(); root.loadTicks++; root.loadMaxGap=Math.max(root.loadMaxGap,now-root.loadLast); root.loadLast=now } }
    function aggregateKey() { return ["qsos", "calls", "days", "mean_power", "entities"][reportAggregate.currentIndex] || "qsos" }
    function aggregateIndex(key) { const index=["qsos", "calls", "days", "mean_power", "entities"].indexOf(key); return index < 0 ? 0 : index }
    function refreshNamedReports() {
        const result=decolog.savedReportDefinitions(); savedReportsPath=result.path || ""; savedReports=result.rows || []
        if (result.error) reportMessage=result.error
    }
    function applyNamedReport(row) {
        if (!row || !row.id) return
        loadedReport=row; reportName.text=row.name; reportYear.text=row.year ? String(row.year) : ""; reportBand.text=row.band || ""
        reportKind.currentIndex=row.kind==="custom" ? 3 : row.kind==="club" ? 2 : row.kind==="confirmation" ? 1 : 0
        reportStation.text=row.station || ""; reportMode.text=row.kind==="custom" ? (row.mode || "") : ""; if (row.kind==="custom") { root.loadDimensions(row.dimensions); reportAggregate.currentIndex=root.aggregateIndex(row.aggregate) }
        reportMessage=qsTr("Loaded report: %1").arg(row.name)
    }
    function saveNamedReport(update) {
        const value=reportYear.text.trim()
        if (value.length && (!/^[0-9]{4}$/.test(value) || Number(value)===0)) { reportMessage=qsTr("Enter a four-digit year or leave it empty."); return }
        if (update && !loadedReport.id) { reportMessage=qsTr("Load a saved report before updating it."); return }
        const definition={id:update ? loadedReport.id : "", revision:update ? loadedReport.revision : 0, name:reportName.text,
            kind:root.chosenReportKind(),year:value.length ? Number(value) : 0,band:reportBand.text,station:reportStation.text,
            dimensions:root.chosenDimensions(),aggregate:root.aggregateKey(),mode:reportKind.currentIndex===3 ? reportMode.text : ""}
        const result=decolog.saveReportDefinition(definition,savedReportsPath)
        if (result.error) { reportMessage=result.error; return }
        loadedReport=result.definition; root.refreshNamedReports(); savedPicker.currentIndex=savedReports.findIndex(x => x.id===loadedReport.id)
        reportMessage=qsTr("Saved report: %1").arg(loadedReport.name)
    }
    property var restoredDimensions:[]
    readonly property var dimensionKeys: {
        let keys=["","year","band","mode","station","operator","grid","antenna","dxcc","day","hour"]
        for (const field of decolog.newQsoCustomFields) { const key="custom:"+field.name; if (keys.indexOf(key)<0) keys.push(key) }
        for (const key of restoredDimensions) if (keys.indexOf(key)<0) keys.push(key)
        return keys
    }
    function dimensionLabel(key) { return key === "day" ? qsTr("UTC day") : key === "hour" ? qsTr("UTC hour") : (key || qsTr("No grouping")) }
    function chosenDimensions() { return [dimension0,dimension1,dimension2].map(x => root.dimensionKeys[x.currentIndex] || "").filter(x => x.length) }
    function loadDimensions(dims) { restoredDimensions=dims || []; Qt.callLater(function() { dimension0.currentIndex=Math.max(0,root.dimensionKeys.indexOf(restoredDimensions[0] || "")); dimension1.currentIndex=Math.max(0,root.dimensionKeys.indexOf(restoredDimensions[1] || "")); dimension2.currentIndex=Math.max(0,root.dimensionKeys.indexOf(restoredDimensions[2] || "")) }) }
    function beginReportPdf(path) {
        const error=decolog.exportActivityReportPdf(path)
        reportMessage=error
        if (!error.length) reportPdfBusy=true
        else if (reportProbePdf) console.warn("PROBE activityreport pdf start error=" + error)
    }
    function chosenReportKind() { return reportKind.currentIndex===3 ? "custom" : reportKind.currentIndex===2 ? "club" : reportKind.currentIndex===1 ? "confirmation" : "activity" }
    function runActivityReport() {
        const value=reportYear.text.trim()
        if (value.length && (!/^[0-9]{4}$/.test(value) || Number(value)===0)) { reportMessage=qsTr("Enter a four-digit year or leave it empty."); return }
        activityBusy=true; reportMessage=""; activityReport=({})
        if (reportKind.currentIndex===3) decolog.requestCustomReportMode(value.length ? Number(value) : 0,reportBand.text,root.chosenDimensions(),root.aggregateKey(),reportMode.text)
        else if (reportKind.currentIndex===2) decolog.requestClubReport(value.length ? Number(value) : 0,reportBand.text,reportStation.text)
        else if (reportKind.currentIndex===1) decolog.requestConfirmationReport(value.length ? Number(value) : 0,reportBand.text)
        else decolog.requestActivityReport(value.length ? Number(value) : 0,reportBand.text)
    }
    function probeReport(path,kind,pdf) {
        page=2; reportYear.text="2026"; reportBand.text="20m"; reportKind.currentIndex=kind==="custom" ? 3 : kind==="club" ? 2 : kind==="confirmation" ? 1 : 0; reportStation.text=kind==="club" ? "9H1SR" : ""
        if (kind==="custom") {
            decolog.saveNewQsoPanelSettings(decolog.newQsoVisibleFields,[{name:"APP_DISTRICT",label:"District"}],false)
            dimension0.currentIndex=root.dimensionKeys.indexOf("band"); dimension1.currentIndex=root.dimensionKeys.indexOf("custom:APP_DISTRICT"); dimension2.currentIndex=0; reportAggregate.currentIndex=1
        }
        console.warn("PROBE activityreport prior settings " + JSON.stringify(decolog.activityReportSettings()))
        console.warn("PROBE activityreport saved " + (kind==="custom" ? decolog.saveCustomReportSettings(2026,"20m",root.chosenDimensions(),"calls") : kind==="club" ? decolog.saveClubReportSettings(2026,"20m",reportStation.text) : decolog.saveSelectedReportSettings(kind || "activity",2026,"20m")))
        reportProbePath=path; reportProbePdf=!!pdf; runActivityReport()
    }
    function probeCustomRestore() {
        const s=decolog.activityReportSettings(); decolog.saveNewQsoPanelSettings(decolog.newQsoVisibleFields,[],false)
        page=2; reportKind.currentIndex=3; reportYear.text=String(s.year || ""); reportBand.text=s.band || ""; reportAggregate.currentIndex=root.aggregateIndex(s.aggregate)
        root.loadDimensions(s.dimensions)
        Qt.callLater(function() { console.warn("PROBE custom restored " + JSON.stringify(root.chosenDimensions())); root.customRestoreProbe=true; root.runActivityReport() })
    }
    function probeNamedReports() {
        page=2; reportKind.currentIndex=3; reportYear.text="2026"; reportBand.text="20m"; reportName.text="A band mode"
        dimension0.currentIndex=root.dimensionKeys.indexOf("band"); dimension1.currentIndex=root.dimensionKeys.indexOf("mode"); dimension2.currentIndex=0; reportAggregate.currentIndex=0
        saveNamedNew.clicked()
        reportKind.currentIndex=2; reportStation.text="9H1SR"; reportName.text="B club"; saveNamedNew.clicked()
        console.warn("PROBE named saved " + JSON.stringify(root.savedReports.map(x => x.name)))
    }
    function probeUtcDays() {
        page=2; root.refreshNamedReports(); reportKind.currentIndex=3; reportYear.text="2026"; reportBand.text="20m"; reportName.text="UTC days"
        dimension0.currentIndex=root.dimensionKeys.indexOf("band"); dimension1.currentIndex=0; dimension2.currentIndex=0; reportAggregate.currentIndex=2
        root.saveNamedReport(false)
        const row=root.savedReports.find(x => x.name==="UTC days")
        if (!row) { console.warn("PROBE days save failed " + root.reportMessage); return }
        reportAggregate.currentIndex=0; root.applyNamedReport(row)
        Qt.callLater(() => { console.warn("PROBE days restored " + root.aggregateKey()); root.daysProbe=true; root.runActivityReport() })
    }
    function probeMeanPower(path) {
        page=2; root.refreshNamedReports(); reportKind.currentIndex=3; reportYear.text="2026"; reportBand.text=""; reportName.text="Mean power"
        dimension0.currentIndex=root.dimensionKeys.indexOf("band"); dimension1.currentIndex=0; dimension2.currentIndex=0; reportAggregate.currentIndex=3
        root.saveNamedReport(false)
        const row=root.savedReports.find(x => x.name==="Mean power")
        if (!row) { console.warn("PROBE power save failed " + root.reportMessage); return }
        reportAggregate.currentIndex=0; root.applyNamedReport(row)
        Qt.callLater(() => { console.warn("PROBE power restored " + root.aggregateKey()); root.daysProbe=true; if (path) { root.reportProbePath=path; root.reportProbePdf=true }; root.runActivityReport() })
    }
    function probeReportLoad() {
        page=2; reportKind.currentIndex=3; reportYear.text="2026"; reportBand.text="20m"
        dimension0.currentIndex=root.dimensionKeys.indexOf("band"); dimension1.currentIndex=0; dimension2.currentIndex=0; reportAggregate.currentIndex=3
        loadProbe=true; loadTicks=0; loadMaxGap=0; loadStart=Date.now(); loadLast=loadStart; loadProbeTimer.start(); runActivityReport()
    }
    function probeUtcGrouping(path) {
        page=2; root.refreshNamedReports(); reportKind.currentIndex=3; reportYear.text=""; reportBand.text="20m"; reportName.text="UTC periods"
        dimension0.currentIndex=root.dimensionKeys.indexOf("day"); dimension1.currentIndex=root.dimensionKeys.indexOf("hour"); dimension2.currentIndex=0; reportAggregate.currentIndex=0
        root.saveNamedReport(false); const row=root.savedReports.find(x => x.name==="UTC periods")
        if (!row) { console.warn("PROBE UTC grouping save failed " + root.reportMessage); return }
        dimension1.currentIndex=0; root.applyNamedReport(row)
        Qt.callLater(() => { console.warn("PROBE UTC grouping restored " + JSON.stringify(root.chosenDimensions())); root.daysProbe=true; if (path) { root.reportProbePath=path; root.reportProbePdf=true }; root.runActivityReport() })
    }
    function probeDxcc() {
        page=2; root.refreshNamedReports(); reportKind.currentIndex=3; reportYear.text="2026"; reportBand.text="20m"; reportName.text="DXCC groups"
        dimension0.currentIndex=root.dimensionKeys.indexOf("band"); dimension1.currentIndex=root.dimensionKeys.indexOf("dxcc"); dimension2.currentIndex=0; reportAggregate.currentIndex=0
        root.saveNamedReport(false); const row=root.savedReports.find(x => x.name==="DXCC groups")
        if (!row) { console.warn("PROBE dxcc save failed " + root.reportMessage); return }
        dimension1.currentIndex=0; root.applyNamedReport(row)
        Qt.callLater(() => { console.warn("PROBE dxcc restored " + JSON.stringify(root.chosenDimensions())); root.daysProbe=true; root.runActivityReport() })
    }
    function probeDxccCount() {
        page=2; root.refreshNamedReports(); reportKind.currentIndex=3; reportYear.text="2026"; reportBand.text="20m"; reportName.text="DXCC counts"
        dimension0.currentIndex=root.dimensionKeys.indexOf("band"); dimension1.currentIndex=0; dimension2.currentIndex=0; reportAggregate.currentIndex=4
        root.saveNamedReport(false); const row=root.savedReports.find(x => x.name==="DXCC counts")
        if (!row) { console.warn("PROBE dxcc count save failed " + root.reportMessage); return }
        reportAggregate.currentIndex=0; root.applyNamedReport(row)
        Qt.callLater(() => { console.warn("PROBE dxcc count restored " + root.aggregateKey()); root.daysProbe=true; root.runActivityReport() })
    }
    function probeModeFilter(path) {
        page=2; root.refreshNamedReports(); reportKind.currentIndex=3; reportYear.text="2026"; reportBand.text="20m"; reportName.text="FT8 only"; reportMode.text="FT8"
        dimension0.currentIndex=root.dimensionKeys.indexOf("band"); dimension1.currentIndex=0; dimension2.currentIndex=0; reportAggregate.currentIndex=0
        root.saveNamedReport(false); const row=root.savedReports.find(x => x.name==="FT8 only")
        if (!row) { console.warn("PROBE mode save failed " + root.reportMessage); return }
        console.warn("PROBE mode settings saved " + decolog.saveCustomReportModeSettings(2026,"20m",["band"],"qsos","FT8")); console.warn("PROBE mode settings loaded " + JSON.stringify(decolog.activityReportSettings()))
        reportMode.text=""; root.applyNamedReport(row)
        Qt.callLater(() => { console.warn("PROBE mode restored " + reportMode.text); root.daysProbe=true; if (path) { root.reportProbePath=path; root.reportProbePdf=false }; root.runActivityReport() })
    }
    function probeNamedRestore() {
        page=2; root.refreshNamedReports(); console.warn("PROBE named restored " + JSON.stringify(root.savedReports.map(x => x.name)))
        const row=root.savedReports.find(x => x.name==="A band mode"); if (!row) return
        root.applyNamedReport(row)
        Qt.callLater(function() { console.warn("PROBE named dimensions " + JSON.stringify(root.chosenDimensions())); root.namedRestoreProbe=true; root.runActivityReport() })
    }

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
    Component.onCompleted: { refresh(); refreshNamedReports() }

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
        function onLogChanged() { root.revision++; if ((decolog.currentLogInfo().path || "")!==root.savedReportsPath) { root.loadedReport=({}); root.refreshNamedReports() } }
        function onStatsReady(result) {
            if (result.mode !== root.mode || result.year !== root.year)
                return
            root.stats = result
            root.counting = false
        }
        function onActivityReportReady(result) {
            root.activityBusy=false; root.activityReport=result
            root.reportMessage=result.error || ""
            if (root.loadProbe) { const now=Date.now(); root.loadMaxGap=Math.max(root.loadMaxGap,now-root.loadLast); loadProbeTimer.stop(); root.loadProbe=false; console.warn("PROBE reportload " + JSON.stringify({elapsed:now-root.loadStart,ticks:root.loadTicks,maxgap:root.loadMaxGap,rows:result.rows,error:result.error || ""})) }
            if (root.daysProbe) { root.daysProbe=false; console.warn("PROBE days result " + JSON.stringify({aggregate:result.aggregate,rows:result.rows,error:result.error || ""})) }
            if (root.customRestoreProbe) { console.warn("PROBE custom restored result " + JSON.stringify(result.rows || [])); root.customRestoreProbe=false }
            if (root.namedRestoreProbe) {
                console.warn("PROBE named result " + JSON.stringify(result.rows || [])); root.namedRestoreProbe=false
                const other=root.savedReports.find(x => x.name==="B club")
                if (other) { removeNamedReport.target=other; removeNamedReport.logPath=root.savedReportsPath; removeNamedReport.open(); Qt.callLater(function() { confirmRemoveNamed.clicked(); console.warn("PROBE named after remove " + JSON.stringify(root.savedReports.map(x => x.name))) }) }
            }
            if (String(root.reportProbePath).length) {
                console.warn("PROBE activityreport result " + JSON.stringify(result))
                if (root.reportProbePdf) { console.warn("PROBE activityreport pdf protected " + decolog.exportActivityReportPdf(root.probeLocalFileUrl(decolog.currentLogInfo().path))); root.beginReportPdf(root.reportProbePath) }
                else {
                    console.warn("PROBE activityreport export " + decolog.exportActivityReport(root.reportProbePath))
                    console.warn("PROBE activityreport context " + decolog.exportActivityReportContext(String(root.reportProbePath) + ".context.json"))
                    console.warn("PROBE activityreport context protected " + decolog.exportActivityReportContext(root.probeLocalFileUrl(decolog.currentLogInfo().path)))
                }
                console.warn("PROBE activityreport protected " + decolog.exportActivityReport(root.probeLocalFileUrl(decolog.currentLogInfo().path)))
                console.warn("PROBE activityreport settings " + JSON.stringify(decolog.activityReportSettings()))
                Qt.callLater(function() {
                    function inspect(index) {
                        if (index >= Math.min(3, activityRows.count)) return
                        activityRows.positionViewAtIndex(index,ListView.Beginning)
                        Qt.callLater(function() { const row=activityRows.itemAtIndex(index); console.warn("PROBE activityreport rendered " + (index===0 ? "" : index===1 ? "second " : "third ") + (row ? row.text : "missing")); inspect(index+1) })
                    }
                    inspect(0)
                })
                root.reportProbePath=""
            }
        }
        function onActivityReportPdfFinished(success,error) {
            root.reportPdfBusy=false; root.reportMessage=success ? qsTr("PDF exported.") : error
            if (root.reportProbePdf) { console.warn("PROBE activityreport pdf success=" + success + " error=" + error); root.reportProbePdf=false }
        }
    }
    FileDialog {
        id:reportFile; title:qsTr("Export activity report"); fileMode:FileDialog.SaveFile
        nameFilters:[qsTr("CSV files (*.csv)")]
        onAccepted: { const error=decolog.exportActivityReport(selectedFile); root.reportMessage=error || qsTr("CSV exported.") }
    }
    FileDialog { id:reportContextFile; title:qsTr("Export report context"); fileMode:FileDialog.SaveFile; nameFilters:[qsTr("JSON files (*.json)")]; onAccepted:{ const error=decolog.exportActivityReportContext(selectedFile); root.reportMessage=error || qsTr("Report context exported.") } }
    FileDialog { id:reportPdfFile; title:qsTr("Export report PDF"); fileMode:FileDialog.SaveFile; nameFilters:[qsTr("PDF files (*.pdf)")]; onAccepted:root.beginReportPdf(selectedFile) }
    Popup {
        id:removeNamedReport; anchors.centerIn:Overlay.overlay; modal:true; padding:16
        property var target:({}); property string logPath:""
        background:Rectangle { color:Theme.panelColor; border.color:Theme.warningColor; radius:6 }
        contentItem:ColumnLayout {
            width:440
            Text { Layout.fillWidth:true; wrapMode:Text.Wrap; color:Theme.textPrimary; text:qsTr("Remove saved report '%1' from this log?").arg(removeNamedReport.target.name || "") }
            RowLayout { Layout.alignment:Qt.AlignRight
                GlassButton { text:qsTr("Cancel"); onClicked:removeNamedReport.close() }
                GlassButton { id:confirmRemoveNamed; text:qsTr("Remove"); onClicked:{ const id=removeNamedReport.target.id; const error=decolog.removeReportDefinition(id,Number(removeNamedReport.target.revision),removeNamedReport.logPath); removeNamedReport.close(); root.reportMessage=error || qsTr("Saved report removed."); if (!error) { if (root.loadedReport.id===id) root.loadedReport=({}); root.refreshNamedReports() } } }
            }
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
            Text { visible:root.page!==2; text: qsTr("Mode"); color: Theme.textSecondary; font.pixelSize: 12 }
            StyledComboBox {
                visible:root.page!==2
                Layout.preferredWidth: 160
                readonly property var values: ["", "FT2", "FT8", "FT4", "CW", "SSB", "RTTY", "JTTY"]
                model: [qsTr("All modes"), "FT2", "FT8", "FT4", "CW", "SSB", "RTTY", "JTTY"]
                currentIndex: Math.max(0, values.indexOf(root.mode))
                onActivated: root.mode = values[currentIndex]
            }
            Text { visible:root.page!==2; text: qsTr("Year"); color: Theme.textSecondary; font.pixelSize: 12 }
            StyledComboBox {
                visible:root.page!==2
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
            GlassButton { text:qsTr("Activity report"); tone:Theme.primaryColor; filled:root.page===2; onClicked:root.page=2 }
            GlassButton { text: qsTr("Refresh"); onClicked: root.revision++ }
        }

        RowLayout {
            visible:root.page!==2
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
        ColumnLayout {
            visible:root.page===2; Layout.fillWidth:true; Layout.fillHeight:true; spacing:8
            Text { Layout.fillWidth:true; color:Theme.textSecondary; wrapMode:Text.Wrap
                text:(root.activityReport.kind || root.chosenReportKind())==="custom" ? qsTr("Group recorded values by one to three dimensions and choose an aggregation. Custom fields use their stored ADIF text; missing values remain blank. Results do not establish award credit and are a read snapshot.") : (root.activityReport.kind || root.chosenReportKind())==="club" ? qsTr("Annual activity by station callsign and operator recorded in each QSO. Enter the club station callsign to restrict the report; blank station or operator values are not inferred. The log does not establish club membership. Results are a read snapshot.") : (root.activityReport.kind || root.chosenReportKind())==="confirmation" ? qsTr("Confirmed records by service and elapsed UTC calendar days from QSO to recorded receipt date. Missing, invalid or earlier dates are excluded from the average. A QSO can appear under multiple services; this is not official award credit. Results are a read snapshot and may not include later changes.") : qsTr("Activity by reported antenna, band and own grid. Blank values were not recorded in the QSO; current station profiles are not used to fill gaps. Results are a read snapshot and may not include later changes.") }
            RowLayout {
                Layout.fillWidth:true
                StyledComboBox { id:reportKind; Layout.preferredWidth:200; model:[qsTr("Station activity"),qsTr("Confirmation times"),qsTr("Annual club activity"),qsTr("Custom report")]; Accessible.name:qsTr("Report type") }
                StyledTextField { id:reportYear; Layout.preferredWidth:150; placeholderText:qsTr("UTC year (all)"); Accessible.name:qsTr("Report UTC year") }
                StyledTextField { id:reportBand; Layout.preferredWidth:150; placeholderText:qsTr("Band (all)"); Accessible.name:qsTr("Report band") }
                GlassButton { text:qsTr("Run report"); enabled:!root.activityBusy; onClicked:root.runActivityReport() }
            }
            Text { Layout.fillWidth:true; color:Theme.textSecondary; wrapMode:Text.Wrap; text:qsTr("Export context separately to preserve filters even for an empty CSV. Its SHA-256 identifies the CSV from this report snapshot.") }
            RowLayout {
                Layout.fillWidth:true
                GlassButton { text:qsTr("Save filters"); enabled:!root.activityBusy; onClicked:{ const value=reportYear.text.trim(); if (value.length && (!/^[0-9]{4}$/.test(value) || Number(value)===0)) { root.reportMessage=qsTr("Enter a four-digit year or leave it empty."); return }; const year=value.length ? Number(value) : 0; root.reportMessage=(reportKind.currentIndex===3 ? decolog.saveCustomReportModeSettings(year,reportBand.text,root.chosenDimensions(),root.aggregateKey(),reportMode.text) : reportKind.currentIndex===2 ? decolog.saveClubReportSettings(year,reportBand.text,reportStation.text) : decolog.saveSelectedReportSettings(root.chosenReportKind(),year,reportBand.text)) || qsTr("Filters saved in this log.") } }
                GlassButton { text:qsTr("Load filters"); enabled:!root.activityBusy; onClicked:{ const s=decolog.activityReportSettings(); reportYear.text=s.year ? String(s.year) : ""; reportBand.text=s.band || ""; reportKind.currentIndex=s.kind==="custom" ? 3 : s.kind==="club" ? 2 : s.kind==="confirmation" ? 1 : 0; reportStation.text=s.station || ""; reportMode.text=s.kind==="custom" ? (s.mode || "") : ""; if (s.kind==="custom") { root.loadDimensions(s.dimensions); reportAggregate.currentIndex=root.aggregateIndex(s.aggregate) }; root.reportMessage=qsTr("Filters loaded from this log.") } }
                GlassButton { text:qsTr("Export CSV…"); enabled:!root.activityBusy && !!root.activityReport.generatedAt && !root.activityReport.error && !root.activityReport.truncated; onClicked:reportFile.open() }
                GlassButton { text:qsTr("Export PDF…"); enabled:!root.activityBusy && !root.reportPdfBusy && !!root.activityReport.generatedAt && !root.activityReport.error && !root.activityReport.truncated; onClicked:reportPdfFile.open() }
                GlassButton { text:qsTr("Export context…"); enabled:!root.activityBusy && !!root.activityReport.generatedAt && !root.activityReport.error && !root.activityReport.truncated; onClicked:reportContextFile.open() }
            }
            RowLayout {
                Layout.fillWidth:true
                StyledComboBox { id:savedPicker; Layout.fillWidth:true; model:root.savedReports.map(x => x.name + " · " + x.kind + " · " + x.id.substring(0,8)); Accessible.name:qsTr("Saved reports") }
                GlassButton { text:qsTr("Reload list"); onClicked:root.refreshNamedReports() }
                GlassButton { text:qsTr("Load report"); enabled:savedPicker.currentIndex>=0 && savedPicker.currentIndex<root.savedReports.length; onClicked:root.applyNamedReport(root.savedReports[savedPicker.currentIndex]) }
                GlassButton { text:qsTr("Remove…"); enabled:savedPicker.currentIndex>=0 && savedPicker.currentIndex<root.savedReports.length; onClicked:{ removeNamedReport.target=root.savedReports[savedPicker.currentIndex]; removeNamedReport.logPath=root.savedReportsPath; removeNamedReport.open() } }
            }
            RowLayout {
                Layout.fillWidth:true
                StyledTextField { id:reportName; Layout.fillWidth:true; placeholderText:qsTr("Saved report name"); Accessible.name:qsTr("Saved report name") }
                GlassButton { id:saveNamedNew; text:qsTr("Save as new"); enabled:!root.activityBusy; onClicked:root.saveNamedReport(false) }
                GlassButton { text:qsTr("Update loaded report"); enabled:!root.activityBusy && !!root.loadedReport.id; onClicked:root.saveNamedReport(true) }
            }
            StyledTextField { id:reportStation; Layout.fillWidth:true; visible:reportKind.currentIndex===2; placeholderText:qsTr("Recorded club station callsign (all if blank)"); Accessible.name:qsTr("Recorded club station callsign") }
            StyledTextField { id:reportMode; Layout.fillWidth:true; visible:reportKind.currentIndex===3; placeholderText:qsTr("Recorded mode or submode (all if blank)"); Accessible.name:qsTr("Report recorded mode or submode") }
            Text { Layout.fillWidth:true; visible:reportKind.currentIndex===3; wrapMode:Text.Wrap; color:Theme.textSecondary; text:qsTr("Mode filter matches the recorded submode when present, otherwise the recorded mode; it does not expand mode families.") }
            RowLayout {
                visible:reportKind.currentIndex===3; Layout.fillWidth:true
                StyledComboBox { id:dimension0; Layout.fillWidth:true; model:root.dimensionKeys.map(x => root.dimensionLabel(x)); currentIndex:2; Accessible.name:qsTr("First grouping dimension") }
                StyledComboBox { id:dimension1; Layout.fillWidth:true; model:root.dimensionKeys.map(x => root.dimensionLabel(x)); currentIndex:3; Accessible.name:qsTr("Second grouping dimension") }
                StyledComboBox { id:dimension2; Layout.fillWidth:true; model:root.dimensionKeys.map(x => root.dimensionLabel(x)); currentIndex:0; Accessible.name:qsTr("Third grouping dimension") }
                StyledComboBox { id:reportAggregate; Layout.preferredWidth:180; model:[qsTr("Count QSO"),qsTr("Distinct calls"),qsTr("Distinct UTC days"),qsTr("Mean TX power (W)"),qsTr("Distinct recorded DXCC")]; Accessible.name:qsTr("Aggregation") }
            }
            Text { Layout.fillWidth:true; visible:root.activityBusy; text:qsTr("Preparing report…"); color:Theme.secondaryColor }
            Text { Layout.fillWidth:true; visible:root.reportMessage.length>0; text:root.reportMessage; color:Theme.warningColor; wrapMode:Text.Wrap }
            Text { Layout.fillWidth:true; visible:!!root.activityReport.generatedAt; color:Theme.textSecondary; wrapMode:Text.Wrap
                text:qsTr("Snapshot %1 · year %2 · band %3 · %4 groups%5").arg(root.activityReport.generatedAt || "").arg(root.activityReport.year || qsTr("all")).arg(root.activityReport.band || qsTr("all")).arg((root.activityReport.rows || []).length).arg(root.activityReport.truncated ? qsTr(" · truncated; refine filters before export") : "") }
            Text { Layout.fillWidth:true; visible:root.activityReport.kind==="custom"; color:Theme.textSecondary; text:qsTr("Recorded mode or submode: %1").arg(root.activityReport.mode || qsTr("all")) }
            Text { Layout.fillWidth:true; visible:root.activityReport.kind==="club"; color:Theme.textSecondary; text:qsTr("Recorded station filter: %1").arg(root.activityReport.station || qsTr("all")) }
            Text { Layout.fillWidth:true; color:Theme.textPrimary; font.bold:true; text:root.activityReport.kind==="custom" ? (root.activityReport.dimensions || []).join(" · ")+" · "+(root.activityReport.aggregate==="entities" ? qsTr("Distinct recorded DXCC") : root.activityReport.aggregate==="mean_power" ? qsTr("Mean TX power (W) · samples · excluded") : root.activityReport.aggregate==="calls" ? qsTr("Distinct calls") : root.activityReport.aggregate==="days" ? qsTr("Distinct UTC days") : qsTr("QSO")) : root.activityReport.kind==="club" ? qsTr("UTC year · recorded station · recorded operator · QSO") : root.activityReport.kind==="confirmation" ? qsTr("Service · confirmed · dated · missing · invalid · earlier · average days") : qsTr("Antenna · band · own grid · QSO") }
            Text { Layout.fillWidth:true; visible:root.activityReport.kind==="custom" && root.activityReport.aggregate==="days"; wrapMode:Text.Wrap; color:Theme.textSecondary; text:qsTr("Counts distinct UTC calendar dates with at least one recorded QSO; unparseable dates are excluded. This does not measure operating duration or award eligibility.") }
            Text { Layout.fillWidth:true; visible:root.activityReport.kind==="custom" && root.activityReport.aggregate==="mean_power"; wrapMode:Text.Wrap; color:Theme.textSecondary; text:qsTr("Uses recorded TX power, not measured output. Missing, invalid and negative values are excluded; zero is included.") }
            Text { Layout.fillWidth:true; visible:root.activityReport.kind==="custom" && ((root.activityReport.dimensions || []).indexOf("day")>=0 || (root.activityReport.dimensions || []).indexOf("hour")>=0); wrapMode:Text.Wrap; color:Theme.textSecondary; text:qsTr("Day and hour use recorded timestamps in UTC; invalid timestamps are grouped as not recorded.") }
            Text { Layout.fillWidth:true; visible:root.activityReport.kind==="custom" && (root.activityReport.dimensions || []).indexOf("dxcc")>=0; wrapMode:Text.Wrap; color:Theme.textSecondary; text:qsTr("DXCC groups use the numeric entity recorded in each QSO. Missing or zero values are grouped as not recorded; entity and award eligibility are not verified.") }
            Text { Layout.fillWidth:true; visible:root.activityReport.kind==="custom" && root.activityReport.aggregate==="entities"; wrapMode:Text.Wrap; color:Theme.textSecondary; text:qsTr("Counts distinct positive DXCC numbers recorded in matching QSO; missing values are excluded. This does not validate entities, identify new ones, or establish award credit.") }
            ListView {
                id:activityRows
                Layout.fillWidth:true; Layout.fillHeight:true; clip:true; model:root.activityReport.rows || []
                ScrollBar.vertical:ScrollBar {}
                delegate:Text { required property var modelData; width:ListView.view.width; color:Theme.textPrimary; textFormat:Text.PlainText; elide:Text.ElideRight
                    text:root.activityReport.kind==="custom" ? (root.activityReport.dimensions || []).map((_,i) => modelData["d"+i] || qsTr("(not recorded)")).join(" · ")+" · "+(root.activityReport.aggregate==="mean_power" ? ((Number(modelData.samples)===0 || modelData.value===null || modelData.value===undefined || modelData.value==="") ? "—" : Number(modelData.value).toFixed(2))+" · "+modelData.samples+" · "+modelData.excluded : modelData.value) : root.activityReport.kind==="club" ? (modelData.year || qsTr("(not recorded)"))+" · "+(modelData.station || qsTr("(not recorded)"))+" · "+(modelData.operator || qsTr("(not recorded)"))+" · "+modelData.count : root.activityReport.kind==="confirmation" ? (modelData.service || qsTr("(not recorded)"))+" · "+modelData.confirmed+" · "+modelData.dated+" · "+modelData.missing+" · "+modelData.invalid+" · "+modelData.earlier+" · "+(modelData.averageDays === undefined ? "—" : Number(modelData.averageDays).toFixed(2)) : (modelData.antenna || qsTr("(not recorded)"))+" · "+(modelData.band || qsTr("(not recorded)"))+" · "+(modelData.grid || qsTr("(not recorded)"))+" · "+modelData.count }
            }
        }
    }
}
