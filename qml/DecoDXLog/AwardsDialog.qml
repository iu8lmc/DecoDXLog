// DecoDXLog — award: elenco a sinistra con lavorati/confermati, tabella a destra
// con un elemento per riga e una colonna per banda (● confermato, ○ lavorato),
// i totali per banda in fondo e, per i locatori, una mappa.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore
import Decodium.UI

DialogFrame {
    id: root

    signal openQso(var id)
    signal openPaperQueue()

    property string awardId: "dxcc"
    property string search: ""
    // "all", "unconfirmed", "missing"
    property string view: "all"
    property bool showMap: false
    property var selectedQsoIds: []
    property string claimNote: ""
    property string claimEvidence: ""
    property string externalResultJson: ""
    property string externalResultMessage: ""
    // Si ricalcola quando cambiano log, filtri o selezione.
    property int revision: 0
    // Solo a finestra aperta: chiusa, ricalcolava tutti i diplomi del log a ogni QSO.
    readonly property var summary: visible ? decolog.awardSummary : []
    readonly property var current: summary.find(a => a.id === awardId) || ({})
    readonly property bool hasMissing: decolog.awardHasMissing(awardId)
    readonly property string effectiveView: view === "missing" && !hasMissing ? "all" : view
    readonly property bool mapView: showMap && awardId === "grids"
    readonly property var items: { revision; return decolog.awardItems(awardId, search, effectiveView) }
    readonly property var totals: { revision; return decolog.awardBandTotals(awardId) }
    readonly property var grids: { revision; return awardId === "grids" ? decolog.awardGrids() : [] }
    readonly property var bands: decolog.awardBands
    readonly property var claims: { revision; return decolog.awardClaims() }
    property int marathonYear: new Date().getUTCFullYear()
    property string marathonCategory: "mixed"
    property int marathonMaxPerEntity: 1
    property var marathonPreview: ({})
    property var marathonWorkingEntities: []
    property var marathonWorkingZones: []
    property string marathonExportMessage: ""
    onMarathonYearChanged: marathonPreview=({})
    onMarathonCategoryChanged: marathonPreview=({})
    onMarathonMaxPerEntityChanged: marathonPreview=({})
    property string marathonAdifMessage: ""
    property int marathonAdifYear: 0
    property double marathonAdifProfile: 0
    property bool marathonAdifProbe: false
    property string vuccBand: "6m"
    property string vuccAccount: ""
    property string vuccSite: ""
    property string vuccScope: "terrestrial"
    property int vuccTarget: 100
    property var vuccResult: ({})
    readonly property int vuccInitial: decolog.vuccInitialThreshold(vuccScope,vuccBand)
    onVuccBandChanged: vuccResult=({})
    onVuccAccountChanged: vuccResult=({})
    onVuccSiteChanged: vuccResult=({})
    onVuccTargetChanged: vuccResult=({})
    onVuccScopeChanged: vuccResult=({})
    property var plannerGoals: ["dxcc"]
    property bool plannerSelectionDirty: false
    Settings { id: plannerSettings; category: "qslPlanner"; property string goalsJson: '["dxcc"]' }

    Component.onCompleted: {
        try {
            const saved = JSON.parse(plannerSettings.goalsJson)
            if (Array.isArray(saved) && saved.every(v => typeof v === "string")) plannerGoals = saved
        } catch (error) { plannerGoals = ["dxcc"] }
    }

    function schedulePlannerRefresh() {
        if (qslPlanner.visible && plannerGoals.length > 0) plannerRefresh.restart()
    }
    Timer {
        id: plannerRefresh; interval: 500
        onTriggered: {
            if (!qslPlanner.visible || root.plannerGoals.length === 0) return
            root.plannerSelectionDirty = false
            decolog.buildQslPlan(root.plannerGoals)
        }
    }
    Connections {
        target: decolog
        function onQslPlanChanged() {
            if (decolog.qslPlanBusy || decolog.qslPlan.stale) { alternativesDialog.close(); paperConfirm.close() }
            if (decolog.qslPlan.stale) root.schedulePlannerRefresh()
        }
    }

    function togglePlannerGoal(id, enabled) {
        let selected = plannerGoals.slice()
        const at = selected.indexOf(id)
        if (enabled && at < 0) selected.push(id)
        if (!enabled && at >= 0) selected.splice(at, 1)
        plannerGoals = selected
        alternativesDialog.close()
        paperConfirm.close()
        plannerSettings.goalsJson = JSON.stringify(selected)
        plannerSelectionDirty = true
        schedulePlannerRefresh()
    }

    function probeQslPlan() { openAt("dxcc"); plannerProbeStart.start() }
    function pendingDetailText(p) { return p.service + ": " + (p.dateStatus === "valid" ? qsTr("recorded sent %1 · %2 days ago (UTC)").arg(p.date).arg(p.days) : p.dateStatus === "future" ? qsTr("recorded sent date %1 is in the future; verify").arg(p.date) : p.dateStatus === "missing" ? qsTr("sent date not recorded") : qsTr("invalid sent date: %1").arg(p.rawDate)) }
    function probeQslPlanRestore() { openAt("dxcc"); plannerProbeRestore.start() }
    function probeAlternativePages() { openAt("dxcc"); plannerPagesStart.start() }
    Timer {
        id: plannerPagesStart; interval: 1000
        onTriggered: { openPlannerButton.clicked(); plannerRefresh.stop(); root.plannerGoals=["dxcc"]; root.plannerSelectionDirty=false; buildPlannerButton.clicked(); plannerPagesReady.start() }
    }
    Timer {
        id: plannerPagesReady; interval: 200; repeat: true; property int tries: 0
        onTriggered: {
            if ((decolog.qslPlanBusy || !decolog.qslPlan.candidates || !decolog.qslPlan.candidates.length) && ++tries<30) return
            stop(); alternativesDialog.showCandidate(decolog.qslPlan.candidates[0])
            console.warn("PROBE qslpages first " + JSON.stringify(alternativesDialog.page))
            nextAlternatives.clicked(); console.warn("PROBE qslpages next " + JSON.stringify(alternativesDialog.page))
            nextAlternatives.clicked(); console.warn("PROBE qslpages last " + JSON.stringify(alternativesDialog.page))
            decolog.awardBand="40m"
            console.warn("PROBE qslpages invalidated " + !alternativesDialog.visible + " " + JSON.stringify(decolog.qslPlanAlternativePage(1,"dxcc","291",0)))
        }
    }
    Timer {
        id: plannerProbeRestore; interval: 1000
        onTriggered: { console.warn("PROBE qslplan restored " + JSON.stringify(root.plannerGoals)); openPlannerButton.clicked(); plannerProbeRestoreResult.start() }
    }
    Timer {
        id: plannerProbeRestoreResult; interval: 250; repeat: true; property int tries: 0
        onTriggered: {
            if ((!decolog.qslPlan.candidates || decolog.qslPlanBusy || decolog.qslPlan.stale) && ++tries < 20) return
            stop(); console.warn("PROBE qslplan restored result " + JSON.stringify(decolog.qslPlan))
        }
    }
    Timer {
        id: plannerProbeStart; interval: 1000
        onTriggered: { openPlannerButton.clicked(); root.plannerGoals = ["dxcc", "waz"]; root.plannerSelectionDirty = false; buildPlannerButton.clicked(); plannerProbeCheck.start() }
    }
    Timer {
        id: plannerProbeCheck; interval: 100; repeat: true; property int tries: 0
        onTriggered: {
            if (decolog.qslPlanBusy && ++tries < 80) return
            stop()
            const plan = decolog.qslPlan
            console.warn("PROBE qslplan " + JSON.stringify(plan))
            for (const c of plan.candidates) { if ((c.pendingDetails || []).length) { console.warn("PROBE qslplan pending " + root.pendingDetailText(c.pendingDetails[0])); break } }
            console.warn("PROBE qslplan status " + qslPlanStatus.text)
            const first = qslPlanCandidates.itemAtIndex(0)
            console.warn("PROBE qslplan row " + (first ? first.probeText : "missing"))
            console.warn("PROBE qslplan route " + (first ? first.probeRoute : "missing"))
            const grouped = paperRoutes.itemAtIndex(0)
            console.warn("PROBE qslplan group " + (grouped ? grouped.probeText : "missing"))
            costEditor.edit(plan.paperRoutes[0]); amountField.text = "12.50"; currencyField.text = "EUR"; saveCostButton.clicked()
            plannerProbeCost.start()
        }
    }
    Timer {
        id: plannerProbeCost; interval: 200; repeat: true; property int tries: 0
        onTriggered: {
            const p=decolog.qslPlan
            if ((decolog.qslPlanBusy || p.stale || !p.paperRoutes || !p.paperRoutes.length || !p.paperRoutes[0].cost) && ++tries < 20) return
            stop(); console.warn("PROBE qslplan saved cost " + JSON.stringify(p.paperRoutes || []))
            const row=paperRoutes.itemAtIndex(0); console.warn("PROBE qslplan cost row " + (row ? row.probeText : "missing"))
            const first=qslPlanCandidates.itemAtIndex(0)
            if (!first) { console.warn("PROBE qslplan queue candidate missing"); return }
            first.probeOpenQueue(); cancelPaperQueue.clicked()
            console.warn("PROBE qslplan queue cancelled " + (!paperConfirm.visible && !decolog.cards.operationBusy))
            first.probeOpenQueue(); confirmPaperQueue.clicked(); plannerProbeQueue.start()
        }
    }
    Timer {
        id: plannerProbeQueue; interval: 200; repeat: true; property int tries: 0
        onTriggered: {
            if (decolog.cards.operationBusy && ++tries < 20) return
            stop(); console.warn("PROBE qslplan enqueue " + decolog.cards.status)
            paperQueueButton.clicked(); decolog.cards.markReceived(2, true); plannerProbeConfirm.start()
        }
    }
    Timer {
        id: plannerProbeConfirm; interval: 200; repeat: true; property int tries: 0
        onTriggered: {
            const p = decolog.qslPlan
            if ((decolog.qslPlanBusy || p.stale || p.uniqueBenefits !== 1) && ++tries < 25) return
            stop(); console.warn("PROBE qslplan confirmation " + JSON.stringify(p))
            decolog.awardBand = "40m"
            console.warn("PROBE qslplan stale " + decolog.qslPlan.stale)
            plannerProbeBand.start()
        }
    }
    Timer {
        id: plannerProbeBand; interval: 200; repeat: true; property int tries: 0
        onTriggered: {
            const p = decolog.qslPlan
            if ((decolog.qslPlanBusy || p.stale || p.uniqueBenefits !== 0) && ++tries < 20) return
            stop(); console.warn("PROBE qslplan band result " + JSON.stringify(p))
        }
    }

    DialogFrame {
        id: qslPlanner
        objectName: "qslPlannerWindow"
        transientParent: root
        title: qsTr("Potential QSL plan")
        info: qsTr("Review before requesting; no messages are sent")
        dialogKey: "qslPlanner"
        onVisibleChanged: if (visible) root.schedulePlannerRefresh()
        width: 960; height: 650
        body: ColumnLayout {
            spacing: 8
            Text {
                Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.textSecondary
                text: qsTr("Potential benefits use current award filters and rules. Candidates are shown by QSO ID; priority greedily covers new benefits, with ties favouring sent/unconfirmed accepted services, then QSO ID. Costs and success probabilities are not ranked. Review before action.")
            }
            RowLayout {
                Layout.fillWidth: true
                ScrollView {
                    Layout.fillWidth: true; Layout.preferredHeight: 95; clip: true
                    Flow {
                        width: qslPlanner.width - 50; spacing: 6
                        Repeater {
                            model: root.summary
                            CheckBox {
                                required property var modelData
                                text: modelData.title; checked: root.plannerGoals.indexOf(modelData.id) >= 0
                                onToggled: root.togglePlannerGoal(modelData.id, checked)
                            }
                        }
                    }
                }
                GlassButton {
                    id: buildPlannerButton
                    objectName: "buildQslPlanButton"
                    text: decolog.qslPlanBusy ? qsTr("Calculating…") : qsTr("Build plan")
                    enabled: !decolog.qslPlanBusy && root.plannerGoals.length > 0
                    onClicked: { plannerSettings.goalsJson = JSON.stringify(root.plannerGoals); root.plannerSelectionDirty = false; decolog.buildQslPlan(root.plannerGoals) }
                }
                GlassButton {
                    text: qsTr("Clear")
                    enabled: root.plannerGoals.length > 0
                    onClicked: { root.plannerGoals = []; alternativesDialog.close(); plannerSettings.goalsJson = "[]"; root.plannerSelectionDirty = true; plannerRefresh.stop() }
                }
            }
            Text {
                id: qslPlanStatus
                objectName: "qslPlanStatus"
                Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.textSecondary
                text: root.plannerGoals.length === 0 ? qsTr("Select at least one award.")
                      : decolog.qslPlanBusy ? qsTr("Calculating from a log snapshot…")
                      : (decolog.qslPlan.stale || root.plannerSelectionDirty) ? qsTr("The log or award settings changed. Updating the plan…")
                      : decolog.qslPlan.error ? decolog.qslPlan.error
                      : decolog.qslPlan.candidates ? qsTr("%1 potential benefits · %2 candidate QSO")
                          .arg(decolog.qslPlan.uniqueBenefits).arg(decolog.qslPlan.candidates.length)
                      : qsTr("Select awards and build a plan.")
            }
            Text {
                Layout.fillWidth: true; color: Theme.textSecondary; wrapMode: Text.Wrap
                visible: !decolog.qslPlan.stale && !root.plannerSelectionDirty && !!decolog.qslPlan.paperRoutes
                text: qsTr("Paper routes by recorded QSL via text and channel; group benefits can overlap · %1 candidate(s) missing one or both")
                    .arg((decolog.qslPlan.ungroupedPaperQsoIds || []).length)
            }
            ListView {
                id: paperRoutes; Layout.fillWidth: true; Layout.preferredHeight: Math.min(90, contentHeight)
                clip: true; spacing: 3
                model: (decolog.qslPlan.stale || root.plannerSelectionDirty) ? [] : (decolog.qslPlan.paperRoutes || [])
                delegate: RowLayout {
                    required property var modelData
                    readonly property string probeText: routeLabel.text
                    width: ListView.view.width
                    Text {
                        id: routeLabel; Layout.fillWidth: true; color: Theme.textPrimary; wrapMode: Text.Wrap
                        text: modelData.manager + " · " + (modelData.channel === "B" ? qsTr("bureau") : qsTr("direct"))
                            + " · " + qsTr("QSO: %1 · potential benefits: %2").arg(modelData.qsoIds.join(", ")).arg(modelData.uniqueBenefits)
                            + " · " + (!modelData.cost ? qsTr("cost unknown")
                                : qsTr("%1 %2 · %3").arg(modelData.cost.amount).arg(modelData.cost.currency)
                                    .arg(modelData.cost.basis === "estimate" ? qsTr("operator estimate") : qsTr("operator-supplied source: %1 (%2)").arg(modelData.cost.source).arg(modelData.cost.observedDate)))
                    }
                    GlassButton { text: qsTr("Edit cost"); onClicked: costEditor.edit(modelData) }
                }
            }
            ListView {
                id: qslPlanCandidates
                objectName: "qslPlanCandidates"
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 4
                model: (decolog.qslPlan.stale || root.plannerSelectionDirty) ? [] : (decolog.qslPlan.candidates || [])
                delegate: Rectangle {
                    required property var modelData
                    readonly property string probeText: candidateHeading.text + " " + candidatePriority.text + " " + candidateBenefits.text
                    readonly property string probeRoute: candidateRoute.text
                    function probeOpenQueue() { reviewQueueButton.clicked() }
                    width: ListView.view.width; height: row.implicitHeight + 14; radius: 4
                    color: Theme.glassOverlay; border.color: Theme.borderSoft
                    ColumnLayout {
                        id: row; anchors.fill: parent; anchors.margins: 7
                        Text { id: candidateHeading; Layout.fillWidth: true; color: Theme.textPrimary; font.bold: true
                            text: modelData.call + "  #" + modelData.id + "  ·  " + (modelData.manager || qsTr("manager unknown")) }
                        Text { id: candidatePriority; Layout.fillWidth: true; color: Theme.textSecondary; wrapMode: Text.Wrap
                            text: modelData.priorityRank > 0
                                  ? qsTr("Priority %1 · adds %2 · %3 already sent/unconfirmed").arg(modelData.priorityRank)
                                      .arg(modelData.marginalBenefits.join(", ")).arg(modelData.pendingMarginalCount)
                                  : qsTr("Alternative: its benefits are covered by higher-priority candidates") }
                        Text { id: candidateBenefits; Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.textSecondary
                            text: modelData.benefits.map(b => b.award.toUpperCase() + " " + b.key + " [" + b.services.join(", ") + "] · "
                                + qsTr("alternatives: %1").arg(b.alternatives.join(", ") || qsTr("none"))
                                + (b.alternativeCount > b.alternatives.length ? qsTr(" (first %1 of %2)").arg(b.alternatives.length).arg(b.alternativeCount) : "")
                                + (b.pendingAlternatives.length ? " · " + qsTr("sent/unconfirmed alternatives: %1").arg(b.pendingAlternatives.join(", ")) : "")
                                + (b.pendingAlternativeCount > b.pendingAlternatives.length ? qsTr(" (first %1 of %2)").arg(b.pendingAlternatives.length).arg(b.pendingAlternativeCount) : "")).join("\n") }
                        Text { id: candidateRoute; Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.textSecondary
                            text: qsTr("Sent/unconfirmed services: %1 · route source: %2 · observed: %3 · %4")
                                .arg(modelData.pending.join(", ") || qsTr("none recorded"))
                                .arg(modelData.managerSource || qsTr("unknown"))
                                .arg(modelData.managerObservedAt || qsTr("unknown"))
                                .arg(!modelData.managerObservedAt ? qsTr("route age unknown")
                                    : Date.now() - Date.parse(modelData.managerObservedAt) > 180 * 86400000
                                      ? qsTr("route evidence older than 180 days; verify") : qsTr("verify route")) }
                        Text { Layout.fillWidth:true; wrapMode:Text.Wrap; color:Theme.textSecondary; visible:(modelData.pendingDetails || []).length>0
                            text:(modelData.pendingDetails || []).map(p => root.pendingDetailText(p)).join("\n") }
                        RowLayout {
                            GlassButton { text: qsTr("Open QSO"); onClicked: root.openQso(modelData.id) }
                            GlassButton { text: qsTr("View all alternatives"); onClicked: alternativesDialog.showCandidate(modelData) }
                            GlassButton {
                                id: reviewQueueButton
                                text: qsTr("Review paper queue")
                                enabled: !decolog.cards.operationBusy && modelData.benefits.some(b => b.services.indexOf("card") >= 0)
                                onClicked: paperConfirm.review(modelData,decolog.qslPlan.logPath)
                            }
                        }
                    }
                }
            }
            GlassButton { id: paperQueueButton; text: qsTr("Open paper QSL queue"); onClicked: root.openPaperQueue() }
            Text { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.textSecondary; text: decolog.cards.status || "" }
        }
    }

    DialogFrame {
        id: paperConfirm
        transientParent: qslPlanner
        title: qsTr("Review paper QSL queue")
        info: qsTr("No QSL will be sent")
        width: 600; height: 290
        property var candidate: ({})
        property string logPath: ""
        function review(c,path) { candidate=c; logPath=path || ""; paperChannel.currentIndex=c.paperChannel === "D" ? 1 : 0; paperError.text=""; open() }
        body: ColumnLayout {
            spacing: 10
            Text { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.textPrimary
                text: qsTr("Add %1 · QSO #%2 to the paper queue?").arg(paperConfirm.candidate.call || "").arg(paperConfirm.candidate.id || "") }
            Text { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.textSecondary
                text: qsTr("Review the QSO and route first. Already queued, sent, confirmed, edited or changed-route records are left unchanged. This does not send a request.") }
            StyledComboBox { id: paperChannel; Layout.preferredWidth: 180; model: [qsTr("Bureau"),qsTr("Direct")] }
            Text { id: paperError; Layout.fillWidth: true; color: Theme.warningColor; wrapMode: Text.Wrap }
            Item { Layout.fillHeight: true }
            RowLayout {
                GlassButton {
                    id: confirmPaperQueue
                    text: qsTr("Confirm queue")
                    enabled: !decolog.cards.operationBusy
                    onClicked: {
                        const c=paperConfirm.candidate
                        if (root.plannerSelectionDirty || decolog.qslPlan.stale || !paperConfirm.logPath || paperConfirm.logPath !== decolog.qslPlan.logPath) { paperError.text=qsTr("The plan changed. Rebuild and review the QSO."); return }
                        if (!decolog.cards.enqueueIfUnsent(c.id,c.uuid,c.revision,paperChannel.currentIndex === 0 ? "B" : "D",paperConfirm.logPath)) {
                            paperError.text=qsTr("The operation could not be started. Check the log and queue status."); return
                        }
                        paperConfirm.close()
                    }
                }
                GlassButton { id: cancelPaperQueue; text: qsTr("Cancel"); onClicked: paperConfirm.close() }
            }
        }
    }

    DialogFrame {
        id: alternativesDialog
        transientParent: qslPlanner
        title: qsTr("QSO alternatives")
        info: qsTr("Potential alternatives for one benefit")
        width: 650; height: 430
        property var candidate: ({})
        property var page: ({})
        property int offset: 0
        function showCandidate(c) { candidate=c; offset=0; benefitChoice.currentIndex=0; open(); refresh() }
        function refresh() {
            const benefits=candidate.benefits || []
            const choice=benefitChoice.currentIndex
            if (choice < 0 || choice >= benefits.length) { page=({}); return }
            const benefit=benefits[choice]
            page=decolog.qslPlanAlternativePage(candidate.id,benefit.award,benefit.key,offset)
        }
        body: ColumnLayout {
            spacing: 8
            Text { Layout.fillWidth: true; color: Theme.textSecondary; text: qsTr("Alternatives for %1").arg(alternativesDialog.candidate.call || "") }
            StyledComboBox {
                id: benefitChoice; Layout.fillWidth: true
                model: (alternativesDialog.candidate.benefits || []).map(b => b.award.toUpperCase() + " " + b.key)
                onActivated: { alternativesDialog.offset=0; alternativesDialog.refresh() }
            }
            Text { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.textSecondary
                text: alternativesDialog.page.error || (!alternativesDialog.page.total ? qsTr("No alternatives")
                    : qsTr("%1 alternatives · page starts at %2").arg(alternativesDialog.page.total).arg((alternativesDialog.page.offset || 0) + 1)) }
            ListView {
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                model: alternativesDialog.page.ids || []
                delegate: RowLayout {
                    required property var modelData
                    width: ListView.view.width
                    Text { Layout.fillWidth: true; color: Theme.textPrimary; text: qsTr("QSO #%1").arg(modelData.id) + (modelData.pending ? " · " + qsTr("sent/unconfirmed on an accepted service") : "") }
                    GlassButton { text: qsTr("Open QSO"); onClicked: root.openQso(modelData.id) }
                }
            }
            RowLayout {
                GlassButton { text: qsTr("Previous"); enabled: alternativesDialog.offset > 0; onClicked: { alternativesDialog.offset=Math.max(0,alternativesDialog.offset-50); alternativesDialog.refresh() } }
                GlassButton { id: nextAlternatives; text: qsTr("Next"); enabled: alternativesDialog.offset+50 < (alternativesDialog.page.total || 0); onClicked: { alternativesDialog.offset+=50; alternativesDialog.refresh() } }
            }
        }
    }

    DialogFrame {
        id: costEditor
        transientParent: qslPlanner
        title: qsTr("Paper route cost")
        info: qsTr("User supplied; not independently verified")
        width: 600; height: 370
        property var route: ({})
        property string logPath: ""
        function edit(selected) {
            route = selected
            logPath = decolog.currentLogInfo().path || ""
            const c = selected.cost || ({})
            amountField.text = c.amount || ""; currencyField.text = c.currency || "EUR"
            basisField.currentIndex = c.basis === "sourced" ? 1 : 0
            sourceField.text = c.source || ""; observedField.text = c.observedDate || ""
            costError.text = ""; open()
        }
        body: ColumnLayout {
            spacing: 8
            Text { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.textSecondary
                text: qsTr("%1 · %2. Amount is per route as entered; no currency conversion or source verification is performed.")
                    .arg(costEditor.route.manager || "").arg(costEditor.route.channel === "B" ? qsTr("bureau") : qsTr("direct")) }
            RowLayout {
                StyledTextField { id: amountField; Layout.fillWidth: true; placeholderText: qsTr("Amount, e.g. 12.50"); Accessible.name: qsTr("Route cost amount") }
                StyledTextField { id: currencyField; Layout.preferredWidth: 100; placeholderText: qsTr("Currency"); Accessible.name: qsTr("Route cost currency") }
                StyledComboBox { id: basisField; Layout.preferredWidth: 180; model: [qsTr("Estimate"), qsTr("Source supplied")] }
            }
            StyledTextField { id: sourceField; Layout.fillWidth: true; placeholderText: qsTr("Source or note (required for source supplied)"); Accessible.name: qsTr("Cost source") }
            StyledTextField { id: observedField; Layout.fillWidth: true; placeholderText: qsTr("Observation date YYYY-MM-DD (required for source supplied)"); Accessible.name: qsTr("Cost observation date") }
            Text { id: costError; Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.warningColor }
            Item { Layout.fillHeight: true }
            RowLayout {
                GlassButton {
                    id: saveCostButton
                    text: qsTr("Save cost")
                    onClicked: {
                        if (!costEditor.logPath || costEditor.logPath !== decolog.currentLogInfo().path) { costError.text=qsTr("The open log changed. Reopen this editor."); return }
                        const error=decolog.saveQslRouteCost(costEditor.route.manager,costEditor.route.channel,
                            {amount: amountField.text, currency: currencyField.text, basis: basisField.currentIndex === 0 ? "estimate" : "sourced", source: sourceField.text, observedDate: observedField.text})
                        if (error.length) costError.text=error; else costEditor.close()
                    }
                }
                GlassButton {
                    text: qsTr("Clear cost")
                    onClicked: { if (!costEditor.logPath || costEditor.logPath !== decolog.currentLogInfo().path) { costError.text=qsTr("The open log changed. Reopen this editor."); return }; const error=decolog.clearQslRouteCost(costEditor.route.manager,costEditor.route.channel); if (error.length) costError.text=error; else costEditor.close() }
                }
                GlassButton { text: qsTr("Cancel"); onClicked: costEditor.close() }
            }
        }
    }

    function toggleQso(id) {
        if (!id || id <= 0) return
        let next = selectedQsoIds.slice()
        const at = next.indexOf(id)
        if (at >= 0) next.splice(at, 1)
        else next.push(id)
        selectedQsoIds = next
    }

    function createClaim() {
        if (!selectedQsoIds.length) return
        if (decolog.createAwardClaim(root.awardId, root.current.ruleVersion || "computed", selectedQsoIds)) {
            selectedQsoIds = []
            revision++
        }
    }

    function reconcileExternalResult() {
        const result = decolog.reconcileAwardResult(root.externalResultJson)
        root.externalResultMessage = result.length ? result : qsTr("External result reconciled")
        if (!result.length) { root.externalResultJson = ""; revision++ }
    }

    function openAt(id) {
        if (id)
            awardId = id
        revision++
        open()
    }

    function refreshMarathon() {
        marathonExportMessage = ""
        marathonPreview = decolog.cqMarathonPreview(marathonYear, marathonCategory,
                                                     marathonMaxPerEntity, decolog.awardProfile)
        marathonWorkingEntities = (marathonPreview.selected || []).slice()
        marathonWorkingZones = (marathonPreview.zoneCandidates || []).slice()
    }
    function cycleMarathonCandidate(kind, index) {
        const entity = kind === "entity"
        const selected = (entity ? marathonWorkingEntities : marathonWorkingZones).slice()
        const current = selected[index]
        if (!current) return false
        const alternatives = (entity ? marathonPreview.entityAlternatives : marathonPreview.zoneAlternatives) || []
        const key = entity ? "entityCode" : "cqZone"
        const options = alternatives.filter(x => x[key] === current[key])
        const start = options.findIndex(x => x.qsoId === current.qsoId)
        for (let offset = 1; offset < options.length; ++offset) {
            const candidate = options[(start + offset) % options.length]
            if (selected.some((x, i) => i !== index && x.qsoId === candidate.qsoId)) continue
            selected[index] = candidate
            if (entity) marathonWorkingEntities = selected; else marathonWorkingZones = selected
            return true
        }
        return false
    }
    function probeMarathonReview(profile) {
        openAt("dxcc"); decolog.awardProfile=Number(profile); marathonYear=2026; marathonCategory="mixed"; refreshMarathon(); marathonReview.open()
        Qt.callLater(function() { const z=marathonZoneRows.itemAtIndex(0); const e=marathonExcludedRows.itemAtIndex(0); const c=marathonEntityRows.itemAtIndex(0); console.warn("PROBE marathonReview open="+marathonReview.visible+" zone="+(z ? z.text : "missing")+" excluded="+(e ? e.text : "missing")+" entity="+(c ? c.text : "missing")) })
    }
    function probeMarathonAlternatives(profile) {
        openAt("dxcc"); decolog.awardProfile=Number(profile); marathonYear=2026; marathonCategory="mixed"; refreshMarathon(); marathonReview.open()
        const beforeEntity=marathonWorkingEntities[0].qsoId, beforeZone=marathonWorkingZones[0].qsoId
        const entityChanged=cycleMarathonCandidate("entity",0), zoneAfterEntity=marathonWorkingZones[0].qsoId
        const zoneChanged=cycleMarathonCandidate("zone",0)
        Qt.callLater(function() { const e=marathonEntityRows.itemAtIndex(0), z=marathonZoneRows.itemAtIndex(0)
            console.warn("PROBE marathonAlternatives before="+beforeEntity+","+beforeZone+" entityChanged="+entityChanged+" zoneAfterEntity="+zoneAfterEntity+" zoneChanged="+zoneChanged+" entity="+(e?e.text:"missing")+" zone="+(z?z.text:"missing")) })
    }
    function saveMarathonReviewed(file, snapshot) {
        if (marathonPreview.valid !== true || marathonPreview.stationProfileId !== decolog.awardProfile ||
            marathonPreview.edition !== marathonYear || marathonPreview.category !== marathonCategory ||
            marathonPreview.maxPerEntity !== marathonMaxPerEntity || !marathonPreview.snapshot)
            return qsTr("The preview changed; refresh it before exporting")
        return decolog.saveReviewedCqMarathonExport(file, marathonPreview.edition, marathonPreview.category,
                 marathonPreview.maxPerEntity, marathonPreview.stationProfileId, snapshot,
                 marathonWorkingEntities.map(x => x.qsoId), marathonWorkingZones.map(x => x.qsoId))
    }
    function probeMarathonReviewedExport(path,profile,dbPath) {
        openAt("dxcc"); decolog.awardProfile=Number(profile); marathonYear=2026; marathonCategory="mixed"; refreshMarathon()
        cycleMarathonCandidate("entity",0); cycleMarathonCandidate("zone",0)
        const success=saveMarathonReviewed("file://"+path,marathonPreview.snapshot)
        const stale=saveMarathonReviewed("file://"+path,"stale")
        const protectedPath=saveMarathonReviewed("file://"+dbPath,marathonPreview.snapshot)
        const oldProtected=decolog.saveCqMarathonExport("file://"+dbPath,marathonPreview.edition,marathonPreview.category,marathonPreview.maxPerEntity,marathonPreview.stationProfileId)
        marathonYear=2025
        const changed=saveMarathonReviewed("file://"+path,"")
        console.warn("PROBE marathonReviewedExport success="+success+" stale="+stale+" protected="+protectedPath+" oldProtected="+oldProtected+" changed="+changed)
    }
    function beginMarathonAdif(file,year,profile) {
        marathonAdifMessage=decolog.exportCqMarathonReviewAdif(file,year,profile)
        if (marathonAdifProbe && marathonAdifMessage.length) console.warn("PROBE marathonAdif start="+marathonAdifMessage)
    }
    function probeMarathonAdif(path,profile) { openAt("dxcc"); marathonAdifProbe=true; beginMarathonAdif("file://"+path,2026,Number(profile)) }
    Connections { target:decolog
        function onCqMarathonAdifFinished(success,count,invalidTimestamp,error) {
            root.marathonAdifMessage=success ? qsTr("Exported %1 QSO; %2 invalid UTC timestamps skipped. Review before uploading.").arg(count).arg(invalidTimestamp) : qsTr("Export failed: %1").arg(error)
            if (root.marathonAdifProbe) console.warn("PROBE marathonAdif success="+success+" count="+count+" invalid="+invalidTimestamp+" error="+error)
        }
    }

    function refreshVucc() {
        vuccResult = decolog.vuccPreview(vuccBand, vuccAccount, vuccSite, vuccTarget, vuccScope)
    }
    Connections { target:decolog; function onLogChanged() { root.vuccResult=({}); vuccReview.close(); root.marathonPreview=({}); marathonReview.close() } }
    function probeVuccReview(satellite) {
        openAt("dxcc"); vuccBand=satellite ? "2m" : "6m"; vuccScopePicker.currentIndex=satellite ? 1 : 0; refreshVucc(); vuccReview.open()
        Qt.callLater(function() { const row=vuccCandidateRows.itemAtIndex(0); const excluded=vuccExcludedRows.itemAtIndex(0); console.warn("PROBE vuccReview open="+vuccReview.visible+" row="+(row ? row.text : "missing")+" excluded="+(excluded ? excluded.text : "missing")) })
    }
    function probeVuccThreshold() {
        openAt("dxcc"); vuccScopePicker.currentIndex=0; vuccBand="70cm"; vuccTarget=125
        console.warn("PROBE vuccThreshold before="+vuccTarget+" minimum="+vuccInitial+" label="+vuccThresholdLabel.text)
        vuccUseMinimum.clicked(); vuccBand="13cm"; console.warn("PROBE vuccThreshold applied="+vuccTarget+" next="+vuccInitial+" field="+vuccTargetField.text)
        vuccBand="4m"; console.warn("PROBE vuccThreshold unsupported="+vuccInitial+" enabled="+vuccUseMinimum.enabled)
        vuccScopePicker.currentIndex=1; console.warn("PROBE vuccThreshold satellite="+vuccInitial+" target="+vuccTarget)
    }

    DialogFrame {
        id:vuccReview; title:qsTr("VUCC candidate review"); dialogKey:"vuccReview"; width:720; height:600; dotColor:Theme.secondaryColor
        body:ColumnLayout { anchors.margins:12; spacing:6
            Text { Layout.fillWidth:true; wrapMode:Text.Wrap; color:Theme.warningColor
                text:qsTr("Local LoTW or paper confirmation status only. Non-satellite contacts without propagation data are assumed non-satellite and require review. Verify confirmation, propagation, grids, location, band and account with ARRL; these candidates are not award credit.") }
            Text { color:Theme.textPrimary; font.bold:true; text:qsTr("Candidate grids and recorded evidence") }
            ListView { id:vuccCandidateRows; Layout.fillWidth:true; Layout.fillHeight:true; clip:true; model:root.vuccResult.eligible || []
                ScrollBar.vertical:PanelScrollBar {}
                delegate:Text { required property var modelData; width:vuccCandidateRows.width; wrapMode:Text.Wrap; color:Theme.textSecondary
                    text:qsTr("%1 · QSO %2 · local confirmation: %3%4 · %5 · %6").arg(modelData.grid4).arg(modelData.qsoId).arg(modelData.evidence).arg(modelData.otherEvidence ? qsTr(" · other recorded: %1").arg(modelData.otherEvidence) : "").arg(modelData.classification === "terrestrial-unreported" ? qsTr("propagation unreported; verify") : modelData.classification === "satellite-name-missing" ? qsTr("satellite name missing; verify") : modelData.classification === "satellite" ? qsTr("satellite %1 · TX %2 / RX %3").arg(modelData.satName).arg(modelData.band).arg(modelData.rxBand || qsTr("unknown")) : qsTr("propagation %1").arg(modelData.propMode)).arg(modelData.qsoUtc) }
            }
            Text { color:Theme.textPrimary; font.bold:true; text:qsTr("Excluded or duplicate candidates") }
            ListView { id:vuccExcludedRows; Layout.fillWidth:true; Layout.fillHeight:true; clip:true; model:root.vuccResult.exclusions || []
                ScrollBar.vertical:PanelScrollBar {}
                delegate:Text { required property var modelData; width:vuccExcludedRows.width; wrapMode:Text.Wrap; color:Theme.textSecondary; text:modelData }
            }
            GlassButton { text:qsTr("Close"); onClicked:vuccReview.close() }
        }
    }

    FileDialog {
        id: marathonFileDialog
        title: qsTr("Export CQ DX Marathon JSON")
        fileMode: FileDialog.SaveFile
        currentFile: "decodxlog-cq-marathon-" + root.marathonPreview.edition + ".json"
        nameFilters: [qsTr("JSON files (*.json)"), qsTr("All files (*)")]
        onAccepted: { const error=root.saveMarathonReviewed(selectedFile,root.marathonPreview.snapshot)
            root.marathonExportMessage=error.length ? qsTr("Export failed: %1").arg(error) : qsTr("Reviewed JSON exported") }
    }
    DialogFrame { id:marathonReview; title:qsTr("CQ DX Marathon candidate review"); dialogKey:"marathonReview"; width:740; height:650; dotColor:Theme.secondaryColor
        body:ColumnLayout { anchors.margins:12; spacing:6
            Text { Layout.fillWidth:true; wrapMode:Text.Wrap; color:Theme.warningColor
                text:qsTr("Preliminary data, not official credit. The 2026 list v1.1 is described as of 2023. JSON includes reviewed choices; review ADIF includes all alternatives. Review the entry in the official tool.") }
            Text { color:Theme.textPrimary; font.bold:true; text:qsTr("Entity candidates") }
            ListView { id:marathonEntityRows; Layout.fillWidth:true; Layout.fillHeight:true; clip:true; model:root.marathonWorkingEntities; ScrollBar.vertical:PanelScrollBar {}
                delegate:RowLayout { required property var modelData; required property int index; width:marathonEntityRows.width
                    readonly property string text: qsTr("QSO %1 · %2 · Marathon entity %3 · DXCC %4 · REGION %5 · %6 · CQ zone %7 · %8 %9 · %10").arg(modelData.qsoId).arg(modelData.call).arg(modelData.entityCode).arg(modelData.dxcc || qsTr("unknown")).arg(modelData.region || qsTr("not recorded")).arg(modelData.entityBasis).arg(modelData.cqZone || qsTr("unknown")).arg(modelData.band).arg(modelData.mode).arg(modelData.timestamp)
                    Text { Layout.fillWidth:true; wrapMode:Text.Wrap; color:Theme.textSecondary; text:parent.text }
                    GlassButton { text:qsTr("Next alternative"); onClicked:root.cycleMarathonCandidate("entity",index) } } }
            Text { color:Theme.textPrimary; font.bold:true; text:qsTr("Independent zone candidates") }
            ListView { id:marathonZoneRows; Layout.fillWidth:true; Layout.fillHeight:true; clip:true; model:root.marathonWorkingZones; ScrollBar.vertical:PanelScrollBar {}
                delegate:RowLayout { required property var modelData; required property int index; width:marathonZoneRows.width
                    readonly property string text: qsTr("QSO %1 · %2 · CQ zone %3 · DXCC %4 · %5 %6 · %7").arg(modelData.qsoId).arg(modelData.call).arg(modelData.cqZone).arg(modelData.dxcc || qsTr("unknown")).arg(modelData.band).arg(modelData.mode).arg(modelData.timestamp)
                    Text { Layout.fillWidth:true; wrapMode:Text.Wrap; color:Theme.textSecondary; text:parent.text }
                    GlassButton { text:qsTr("Next alternative"); onClicked:root.cycleMarathonCandidate("zone",index) } } }
            Text { color:Theme.textPrimary; font.bold:true; text:qsTr("Automatic selection exclusions and review warnings") }
            ListView { id:marathonExcludedRows; Layout.fillWidth:true; Layout.fillHeight:true; clip:true; model:(root.marathonPreview.exclusions || []).concat(root.marathonPreview.warnings || []); ScrollBar.vertical:PanelScrollBar {}
                delegate:Text { required property var modelData; width:marathonExcludedRows.width; wrapMode:Text.Wrap; color:Theme.textSecondary; text:modelData } }
            GlassButton { text:qsTr("Close"); onClicked:marathonReview.close() }
        }
    }
    FileDialog { id:marathonAdifDialog; title:qsTr("Export CQ DX Marathon review ADIF"); fileMode:FileDialog.SaveFile
        defaultSuffix:"adi"; nameFilters:[qsTr("ADIF files (*.adi *.adif)")]
        onAccepted: { if (root.marathonAdifYear!==root.marathonYear || root.marathonAdifProfile!==decolog.awardProfile) root.marathonAdifMessage=qsTr("Year or profile changed; select the file again."); else root.beginMarathonAdif(selectedFile,root.marathonAdifYear,root.marathonAdifProfile) }
    }

    title: qsTr("Awards")
    dotColor: Theme.accentColor
    info: qsTr("computed from the log · confirmations: %1")
          .arg([decolog.awardConfirmLotw ? "LoTW" : "", decolog.awardConfirmCard ? qsTr("card") : "",
                decolog.awardConfirmEqsl ? "eQSL" : ""].filter(s => s).join(" + ") || qsTr("none"))
    dialogKey: "awards"
    width: 1240
    height: 720

    Connections {
        target: decolog
        function onAwardsChanged() { root.revision++; root.marathonPreview=({}); marathonReview.close() }
    }

    body: RowLayout {
        spacing: 0

        // ── Elenco degli award ──────────────────────────────────────────────
        ListView {
            Layout.preferredWidth: 250
            Layout.fillHeight: true
            Layout.margins: 8
            clip: true
            spacing: 4
            model: root.summary
            delegate: Rectangle {
                id: card
                required property var modelData
                readonly property bool active: modelData.id === root.awardId
                width: ListView.view.width
                implicitHeight: cardColumn.implicitHeight + 16
                radius: 5
                color: active ? Theme.glassOverlay : cardArea.containsMouse ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.06) : "transparent"
                border.width: 1
                border.color: active ? Theme.primaryColor : Theme.borderSoft

                ColumnLayout {
                    id: cardColumn
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 4
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: card.modelData.title
                            color: card.active ? Theme.primaryColor : Theme.textPrimary
                            font.family: Theme.monoFamily
                            font.pixelSize: 13
                            font.bold: true
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: card.modelData.worked + (card.modelData.total > 0 ? " / " + card.modelData.total : "")
                            color: Theme.textPrimary
                            font.family: Theme.monoFamily
                            font.pixelSize: 12
                            font.bold: true
                        }
                    }
                    MeterBar {
                        Layout.fillWidth: true
                        readonly property int goal: card.modelData.total > 0 ? card.modelData.total
                                                    : card.modelData.target > 0 ? card.modelData.target : 0
                        visible: goal > 0
                        label: qsTr("confirmed %1").arg(card.modelData.confirmed)
                        valueText: card.modelData.target > 0 && card.modelData.confirmed >= card.modelData.target
                                   ? qsTr("base award ✓") : card.modelData.target > 0 ? qsTr("goal %1").arg(card.modelData.target) : ""
                        fraction: goal > 0 ? card.modelData.confirmed / goal : 0
                        barColor: card.modelData.id === "ft2" ? Theme.accentColor : Theme.primaryColor
                    }
                    Text {
                        visible: !parent.children[1].visible
                        text: qsTr("confirmed %1").arg(card.modelData.confirmed)
                        color: Theme.textSecondary
                        font.family: Theme.monoFamily
                        font.pixelSize: 11
                    }
                }
                MouseArea {
                    id: cardArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.awardId = card.modelData.id
                }
            }
        }

        Rectangle { Layout.fillHeight: true; implicitWidth: 1; color: Theme.borderSoft }

        // ── Dettaglio ───────────────────────────────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 12
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text {
                    text: root.current.title || ""
                    color: Theme.textPrimary
                    font.family: Theme.monoFamily
                    font.pixelSize: 20
                    font.bold: true
                }
                Pill {
                    visible: root.current.officialRules === false
                    text: qsTr("statistics · rules not verified")
                    tone: Theme.warningColor
                }
                GlassButton {
                    visible: !root.mapView && root.selectedQsoIds.length > 0
                    text: qsTr("Create claim (%1)").arg(root.selectedQsoIds.length)
                    tone: Theme.accentColor
                    onClicked: root.createClaim()
                }
                Pill { text: qsTr("worked %1").arg(root.current.worked || 0); tone: Theme.secondaryColor }
                Pill { text: qsTr("confirmed %1").arg(root.current.confirmed || 0); tone: Theme.accentColor }
                Pill {
                    visible: !!(root.current.official60m || root.current.personal60m)
                    text: root.current.official60m ? qsTr("official rules") : qsTr("personal · 60 m")
                    tone: root.current.official60m ? Theme.primaryColor : Theme.warningColor
                }
                Pill {
                    visible: root.bands.length > 1
                    text: qsTr("band slots %1 / %2").arg(root.current.slotsWorked || 0).arg(root.current.slotsConfirmed || 0)
                    tone: Theme.primaryColor
                }
                Item { Layout.fillWidth: true }
                GlassButton { id: openPlannerButton; objectName: "openQslPlannerButton"; text: qsTr("QSL plan"); onClicked: qslPlanner.open() }
                StyledComboBox {
                    visible: !root.mapView
                    Layout.preferredWidth: 150
                    readonly property var views: ["all", "unconfirmed", "missing"]
                    model: root.hasMissing ? [qsTr("All worked"), qsTr("Not confirmed"), qsTr("Never worked")]
                                           : [qsTr("All worked"), qsTr("Not confirmed")]
                    currentIndex: Math.max(0, views.indexOf(root.effectiveView))
                    onActivated: root.view = views[currentIndex]
                }
                GlassButton {
                    visible: root.awardId === "grids"
                    text: root.showMap ? qsTr("Table") : qsTr("Map")
                    tone: Theme.primaryColor
                    onClicked: root.showMap = !root.showMap
                }
                StyledTextField {
                    Layout.preferredWidth: 180
                    placeholderText: qsTr("Search…")
                    onTextChanged: root.search = text
                }
            }

            // Quello che il regolamento chiede oltre al numero: il DCI vuole
            // anche cinque regioni e un castello di Cuneo, e il conto va fatto
            // da qualche parte -- meglio qui che a mano.
            Text {
                Layout.fillWidth: true
                visible: (root.current.requirement || "").length > 0
                text: root.current.requirement || ""
                wrapMode: Text.Wrap
                color: Theme.textSecondary
                font.pixelSize: 12
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                StyledComboBox {
                    Layout.preferredWidth: 120
                    model: [qsTr("All bands")].concat(root.bands)
                    currentIndex: decolog.awardBand.length ? Math.max(0, root.bands.indexOf(decolog.awardBand) + 1) : 0
                    onActivated: decolog.awardBand = currentIndex === 0 ? "" : currentText
                }
                StyledComboBox {
                    Layout.preferredWidth: 140
                    readonly property var groups: ["", "FT2", "FT8", "DIGITAL", "CW", "PHONE"]
                    model: [qsTr("All modes"), "FT2", "FT8", qsTr("Digital"), "CW", qsTr("Phone")]
                    currentIndex: Math.max(0, groups.indexOf(decolog.awardModeGroup))
                    onActivated: decolog.awardModeGroup = groups[currentIndex]
                }
                Rectangle { implicitWidth: 1; implicitHeight: 20; color: Theme.borderSoft }
                Text { text: qsTr("Confirmed by"); color: Theme.textSecondary; font.pixelSize: 11 }
                ToggleSwitch { text: "LoTW"; checked: decolog.awardConfirmLotw; onToggled: decolog.awardConfirmLotw = checked }
                ToggleSwitch { text: qsTr("Card"); checked: decolog.awardConfirmCard; onToggled: decolog.awardConfirmCard = checked }
                ToggleSwitch { text: "eQSL"; checked: decolog.awardConfirmEqsl; onToggled: decolog.awardConfirmEqsl = checked }
                Item { Layout.fillWidth: true }
                StyledComboBox {
                    id: profileBox
                    Layout.preferredWidth: 150
                    readonly property var ids: {
                        const list = [0]
                        for (let i = 0; i < decolog.stationProfiles.count; ++i) {
                            const p = decolog.stationProfiles.get(i)
                            if (!p.deleted) list.push(p.id)
                        }
                        return list
                    }
                    model: ids.map(id => id === 0 ? qsTr("All stations") : decolog.stationProfiles.byId(id).name)
                    currentIndex: Math.max(0, ids.indexOf(decolog.awardProfile))
                    onActivated: decolog.awardProfile = ids[currentIndex]
                }
                StyledComboBox {
                    Layout.preferredWidth: 130
                    readonly property var tags: { root.revision; return [""].concat(decolog.qsoModel.tagsInLog().map(t => t.key)) }
                    model: tags.map(t => t.length ? "#" + t : qsTr("All tags"))
                    currentIndex: Math.max(0, tags.findIndex(t => t.toLowerCase() === decolog.awardTag.toLowerCase()))
                    onActivated: decolog.awardTag = tags[currentIndex]
                }
            }

            Rectangle {
                Layout.fillWidth: true
                visible: root.claims.length > 0 || root.selectedQsoIds.length > 0
                implicitHeight: claimsColumn.implicitHeight + 12
                color: Theme.glassOverlay
                radius: 4
                ColumnLayout {
                    id: claimsColumn
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 4
                    Text {
                        text: qsTr("Claims")
                        color: Theme.secondaryColor
                        font.family: Theme.monoFamily
                        font.bold: true
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        StyledTextField {
                            Layout.fillWidth: true
                            placeholderText: qsTr("Outcome / note")
                            text: root.claimNote
                            onTextChanged: root.claimNote = text
                        }
                        StyledTextField {
                            Layout.fillWidth: true
                            placeholderText: qsTr("Evidence or external reference")
                            text: root.claimEvidence
                            onTextChanged: root.claimEvidence = text
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        TextArea {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 48
                            placeholderText: qsTr("Paste verified external result JSON")
                            text: root.externalResultJson
                            wrapMode: TextEdit.Wrap
                            onTextChanged: root.externalResultJson = text
                        }
                        GlassButton {
                            text: qsTr("Reconcile result")
                            enabled: root.externalResultJson.trim().length > 0
                            onClicked: root.reconcileExternalResult()
                        }
                    }
                    Text {
                        visible: root.externalResultMessage.length > 0
                        text: root.externalResultMessage
                        color: root.externalResultMessage === qsTr("External result reconciled")
                               ? Theme.accentColor : Theme.warningColor
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Repeater {
                        model: root.claims
                        RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: modelData.awardId + " · " + modelData.state + " · " + modelData.qsoIds.length + " QSO"
                                color: Theme.textPrimary
                                font.pixelSize: 11
                            }
                            Text {
                                text: modelData.outcome || ""
                                color: Theme.textSecondary
                                font.pixelSize: 11
                                elide: Text.ElideRight
                            }
                            GlassButton {
                                visible: modelData.state === "Draft"
                                text: qsTr("Submit")
                                onClicked: decolog.transitionAwardClaim(modelData.id, "Submitted", root.claimNote, root.claimEvidence)
                            }
                            GlassButton {
                                visible: modelData.state === "Submitted"
                                text: qsTr("Verify")
                                onClicked: decolog.transitionAwardClaim(modelData.id, "Verified", root.claimNote, root.claimEvidence)
                            }
                            GlassButton {
                                visible: modelData.state === "Submitted"
                                text: qsTr("Reject")
                                tone: Theme.warningColor
                                onClicked: decolog.transitionAwardClaim(modelData.id, "Rejected", root.claimNote, root.claimEvidence)
                            }
                            GlassButton {
                                visible: modelData.state === "Verified"
                                text: qsTr("Credit")
                                onClicked: decolog.transitionAwardClaim(modelData.id, "Credited", root.claimNote, root.claimEvidence)
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: marathonColumn.implicitHeight + 12
                color: Theme.glassOverlay
                radius: 4
                ColumnLayout {
                    id: marathonColumn
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 5
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: qsTr("CQ DX Marathon preview")
                            color: Theme.secondaryColor
                            font.family: Theme.monoFamily
                            font.bold: true
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("local selection/export · not an official result")
                            color: Theme.textSecondary
                            font.pixelSize: 11
                        }
                        StyledTextField {
                            Layout.preferredWidth: 80
                            Accessible.name: qsTr("CQ DX Marathon edition year")
                            text: String(root.marathonYear)
                            inputMethodHints: Qt.ImhDigitsOnly
                            onEditingFinished: { root.marathonYear = parseInt(text) || 0 }
                        }
                        StyledComboBox {
                            Layout.preferredWidth: 120
                            Accessible.name: qsTr("CQ DX Marathon category")
                            model: [qsTr("Mixed"), "CW", qsTr("Phone"), qsTr("Digital")]
                            readonly property var values: ["mixed", "cw", "phone", "digital"]
                            currentIndex: Math.max(0, values.indexOf(root.marathonCategory))
                            onActivated: root.marathonCategory = values[currentIndex]
                        }
                        StyledTextField {
                            Layout.preferredWidth: 55
                            Accessible.name: qsTr("CQ DX Marathon maximum contacts per entity")
                            text: String(root.marathonMaxPerEntity)
                            inputMethodHints: Qt.ImhDigitsOnly
                            onEditingFinished: { root.marathonMaxPerEntity = Math.max(1, parseInt(text) || 1) }
                        }
                        GlassButton {
                            Accessible.name: qsTr("Preview CQ DX Marathon selection")
                            text: qsTr("Preview")
                            enabled: decolog.awardProfile > 0
                            onClicked: root.refreshMarathon()
                        }
                        GlassButton {
                            Accessible.name: qsTr("Export CQ DX Marathon JSON")
                            enabled: root.marathonPreview.valid === true
                                     && root.marathonPreview.stationProfileId === decolog.awardProfile
                                     && root.marathonPreview.edition === root.marathonYear
                                     && root.marathonPreview.category === root.marathonCategory
                                     && root.marathonPreview.maxPerEntity === root.marathonMaxPerEntity
                            text: qsTr("Export JSON")
                            onClicked: marathonFileDialog.open()
                        }
                        GlassButton { text:qsTr("Export review ADIF"); enabled:decolog.awardProfile>0 && !decolog.exportBusy
                            onClicked: { root.marathonAdifYear=root.marathonYear; root.marathonAdifProfile=decolog.awardProfile; marathonAdifDialog.open() } }
                        GlassButton { text:qsTr("Review candidates"); enabled:root.marathonPreview.valid === true && root.marathonPreview.stationProfileId === decolog.awardProfile && root.marathonPreview.edition === root.marathonYear && root.marathonPreview.category === root.marathonCategory && root.marathonPreview.maxPerEntity === root.marathonMaxPerEntity; onClicked:marathonReview.open() }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Select a station profile above. Confirm that its QSOs are from one operating location; the profile alone does not verify this.")
                        color: Theme.textSecondary
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                    Text { Layout.fillWidth:true; wrapMode:Text.Wrap; color:Theme.textSecondary; font.pixelSize:11
                        text:qsTr("Review ADIF contains all QSO for the selected profile and year, including alternatives and stored fields. Inspect it and review eligibility in the official submission tool; exporting does not submit an entry.") }
                    Text { Layout.fillWidth:true; wrapMode:Text.Wrap; color:Theme.textSecondary; visible:root.marathonAdifMessage.length>0; text:root.marathonAdifMessage }
                    Text { Layout.fillWidth:true; wrapMode:Text.Wrap; color:Theme.textSecondary; visible:root.marathonExportMessage.length>0; text:root.marathonExportMessage }
                    Text {
                        Layout.fillWidth: true
                        text: root.marathonPreview.valid === undefined
                              ? qsTr("Configure edition, category and entity limit, then preview.")
                              : root.marathonPreview.valid === false ? qsTr("Cannot preview: %1").arg(root.marathonPreview.error || qsTr("invalid request"))
                              : qsTr("Entity candidates: %1 · zone candidates: %2 · excluded: %3")
                                .arg(root.marathonPreview.selected.length || 0)
                                .arg(root.marathonPreview.zoneCandidateCount || 0)
                                .arg(root.marathonPreview.exclusions.length || 0)
                        color: root.marathonPreview.valid === false ? Theme.warningColor : Theme.textSecondary
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: vuccColumn.implicitHeight + 12
                color: Theme.glassOverlay
                radius: 4
                ColumnLayout {
                    id: vuccColumn
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 5
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: qsTr("VUCC preview")
                            color: Theme.secondaryColor
                            font.family: Theme.monoFamily
                            font.bold: true
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("separate from statistical locator totals")
                            color: Theme.textSecondary
                            font.pixelSize: 11
                        }
                        StyledComboBox { id:vuccScopePicker; Layout.preferredWidth:145; Accessible.name:qsTr("VUCC category"); model:[qsTr("Non-satellite"),qsTr("Satellite")]; onCurrentIndexChanged:root.vuccScope=currentIndex===1 ? "satellite" : "terrestrial" }
                        StyledTextField { Layout.preferredWidth: 65; enabled:root.vuccScope === "terrestrial"; Accessible.name: qsTr("VUCC band"); text: root.vuccBand; onEditingFinished: root.vuccBand = text }
                        StyledTextField { Layout.preferredWidth: 120; Accessible.name: qsTr("VUCC award account"); placeholderText: qsTr("Award account"); text: root.vuccAccount; onEditingFinished: root.vuccAccount = text }
                        StyledTextField { Layout.preferredWidth: 120; Accessible.name: qsTr("VUCC operating site"); placeholderText: qsTr("Operating site"); text: root.vuccSite; onEditingFinished: root.vuccSite = text }
                        StyledTextField { id:vuccTargetField; Layout.preferredWidth: 55; Accessible.name: qsTr("VUCC target"); text: String(root.vuccTarget); inputMethodHints: Qt.ImhDigitsOnly; onEditingFinished: root.vuccTarget = Math.max(1, parseInt(text) || 1) }
                        GlassButton { Accessible.name: qsTr("Preview VUCC selection"); text: qsTr("Preview"); onClicked: root.refreshVucc() }
                        GlassButton { Accessible.name:qsTr("Review VUCC candidates"); text:qsTr("Review"); enabled:root.vuccResult.valid === true; onClicked:vuccReview.open() }
                    }
                    RowLayout {
                        Text { id:vuccThresholdLabel; Layout.fillWidth:true; color:Theme.textSecondary; wrapMode:Text.Wrap; font.pixelSize:11
                            text:root.vuccInitial ? qsTr("ARRL initial threshold: %1 grids (January 2026); your target remains editable.").arg(root.vuccInitial) : qsTr("No supported initial threshold for this selection.") }
                        GlassButton { id:vuccUseMinimum; text:qsTr("Use initial threshold"); enabled:root.vuccInitial>0; onClicked:root.vuccTarget=root.vuccInitial }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.vuccResult.valid === undefined
                              ? qsTr("Confirmed evidence is required; configure the band/account/site and preview.")
                              : root.vuccResult.valid === false ? qsTr("Cannot preview: %1").arg(root.vuccResult.error || qsTr("invalid request"))
                              : qsTr("Candidate GRID4: %1 / %2 · excluded: %3 · external rules verified: %4")
                                .arg(root.vuccResult.eligible.length || 0).arg(root.vuccTarget)
                                .arg(root.vuccResult.exclusions.length || 0)
                                .arg(root.vuccResult.officialRulesVerified ? qsTr("yes") : qsTr("no"))
                        color: root.vuccResult.valid === false ? Theme.warningColor : Theme.textSecondary
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }
            }

            // Le conferme valide per questo diploma: quelle del regolamento
            // (il DXCC e il WAS solo LoTW e cartolina), quelle generali, o
            // quelle scelte a mano.
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                readonly property var credits: { root.revision; return decolog.awardCredits(root.awardId) }
                Text {
                    text: qsTr("Valid for %1").arg(root.current.title || root.awardId)
                    color: Theme.textSecondary
                    font.pixelSize: 11
                }
                Repeater {
                    model: [{ id: "lotw", label: "LoTW" }, { id: "card", label: qsTr("Card") },
                            { id: "eqsl", label: "eQSL" }, { id: "qrz", label: "QRZ" }]
                    ToggleSwitch {
                        required property var modelData
                        text: modelData.label
                        checked: parent.credits.services.indexOf(modelData.id) >= 0
                        onToggled: decolog.setAwardCredit(root.awardId, modelData.id, checked)
                    }
                }
                // I 60 metri nel DXCC, nel Challenge e nel WAS: l'ARRL non li
                // accetta. Si vede solo su quei diplomi; vale per tutti e tre.
                Rectangle {
                    visible: count60.visible
                    implicitWidth: 1
                    implicitHeight: 20
                    color: Theme.borderSoft
                }
                ToggleSwitch {
                    id: count60
                    visible: ["dxcc", "challenge", "was"].indexOf(root.awardId) >= 0
                    text: decolog.awardCount60m ? qsTr("60 m · personal") : qsTr("60 m · official")
                    checked: decolog.awardCount60m
                    onToggled: decolog.awardCount60m = checked
                    HoverHandler { id: count60Hover }
                    ToolTip.visible: count60Hover.hovered
                    ToolTip.text: qsTr("The official ARRL count excludes 60 m. Enable it only for a personal count.")
                }
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: parent.credits.custom ? qsTr("chosen by you")
                        : parent.credits.official.length ? qsTr("the rules of the award")
                        : qsTr("the general choice above")
                    color: Theme.textSecondary
                    font.pixelSize: 11
                    font.italic: true
                }
                GlassButton {
                    visible: parent.credits.custom
                    text: qsTr("Back to the rules")
                    onClicked: decolog.resetAwardCredits(root.awardId)
                }
            }

            GridSquareMap {
                visible: root.mapView
                Layout.fillWidth: true
                Layout.fillHeight: true
                grids: root.grids
            }

            // Intestazione della tabella.
            Rectangle {
                visible: !root.mapView
                Layout.fillWidth: true
                implicitHeight: Theme.rowHeight
                color: Theme.panelHeader
                radius: 4
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 8
                    component Head: Text {
                        color: Theme.secondaryColor
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fontSize
                        font.bold: true
                    }
                    Head {
                        Layout.preferredWidth: 80
                        text: root.awardId === "was" ? qsTr("State")
                            : root.awardId === "waz" ? qsTr("Zone")
                            : root.awardId === "jcc" || root.awardId === "jcg" ? qsTr("JARL number")
                            : qsTr("Key")
                    }
                    Head { Layout.fillWidth: true; text: qsTr("Name / first QSO") }
                    Repeater {
                        model: root.bands
                        Head {
                            required property string modelData
                            Layout.preferredWidth: 44
                            horizontalAlignment: Text.AlignHCenter
                            text: modelData
                        }
                    }
                    Head { Layout.preferredWidth: 40; horizontalAlignment: Text.AlignRight; text: "QSO" }
                    Head { Layout.preferredWidth: 90; text: qsTr("Last") }
                }
            }

            ListView {
                id: table
                visible: !root.mapView
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.items
                ScrollBar.vertical: ScrollBar {}
                boundsBehavior: Flickable.StopAtBounds

                delegate: Rectangle {
                    id: line
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    height: Theme.rowHeight
                    color: lineArea.containsMouse ? Theme.glassOverlay : "transparent"
                    Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: 1; color: Theme.borderSoft }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 8
                        Text {
                            Layout.preferredWidth: 80
                            text: line.modelData.key
                            color: line.modelData.confirmed ? Theme.accentColor : Theme.textPrimary
                            font.family: Theme.monoFamily
                            font.pixelSize: Theme.fontSize
                            font.bold: true
                        }
                        CheckBox {
                            Layout.preferredWidth: 28
                            checked: root.selectedQsoIds.indexOf(line.modelData.firstQsoId) >= 0
                            enabled: line.modelData.firstQsoId > 0
                            onToggled: root.toggleQso(line.modelData.firstQsoId)
                        }
                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            textFormat: Text.StyledText
                            text: (line.modelData.name.length ? line.modelData.name + "  " : "")
                                  + "<font color=\"" + Theme.textSecondary + "\">"
                                  + (line.modelData.missing ? "· " + qsTr("never worked") : line.modelData.firstCall + " · " + line.modelData.first)
                                  + "</font>"
                            color: line.modelData.missing ? Theme.textSecondary : Theme.textPrimary
                            font.pixelSize: Theme.fontSize
                        }
                        Repeater {
                            model: root.bands
                            Text {
                                required property string modelData
                                readonly property bool conf: line.modelData.bandsConfirmed.indexOf(modelData) >= 0
                                readonly property bool work: line.modelData.bandsWorked.indexOf(modelData) >= 0
                                Layout.preferredWidth: 44
                                horizontalAlignment: Text.AlignHCenter
                                text: conf ? "●" : work ? "○" : "·"
                                color: conf ? Theme.accentColor : work ? Theme.warningColor : Theme.borderSoft
                                font.pixelSize: Theme.fontSize + 1
                            }
                        }
                        Text {
                            Layout.preferredWidth: 40
                            horizontalAlignment: Text.AlignRight
                            text: line.modelData.missing ? "" : line.modelData.qsoCount
                            color: Theme.textSecondary
                            font.family: Theme.monoFamily
                            font.pixelSize: Theme.fontSize
                        }
                        Text {
                            Layout.preferredWidth: 90
                            text: line.modelData.last
                            color: Theme.textSecondary
                            font.family: Theme.monoFamily
                            font.pixelSize: Theme.fontSize
                        }
                    }
                    MouseArea {
                        id: lineArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onDoubleClicked: root.openQso(line.modelData.firstQsoId)
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: table.count === 0
                    width: parent.width - 40
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: root.effectiveView === "missing" && root.search.length === 0 ? qsTr("Everything worked. Well done.")
                        : root.search.length || root.effectiveView !== "all" ? qsTr("Nothing matches.")
                        : root.awardId === "was" ? qsTr("No QSO with a US state (STATE field) in the log.")
                        : root.awardId === "jcc" || root.awardId === "jcg"
                          ? qsTr("No QSO with a JARL number (CNTY field) in the log. The callbook fills it in when it knows it, otherwise it goes in by hand in the QSO card.")
                        : qsTr("No QSO counts for this award with the current filters.")
                    color: Theme.textSecondary
                }
            }

            Rectangle {
                visible: !root.mapView && root.bands.length > 0
                Layout.fillWidth: true
                implicitHeight: Theme.rowHeight + 4
                color: Theme.panelHeader
                radius: 4
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 8
                    Text {
                        Layout.preferredWidth: 80
                        text: qsTr("Per band")
                        color: Theme.secondaryColor
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fontSize
                        font.bold: true
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("worked / confirmed")
                        color: Theme.textSecondary
                        font.pixelSize: 11
                    }
                    Repeater {
                        model: root.totals
                        Column {
                            required property var modelData
                            Layout.preferredWidth: 44
                            Text {
                                width: 44
                                horizontalAlignment: Text.AlignHCenter
                                text: modelData.worked
                                color: modelData.worked ? Theme.warningColor : Theme.textSecondary
                                font.family: Theme.monoFamily
                                font.pixelSize: 10
                            }
                            Text {
                                width: 44
                                horizontalAlignment: Text.AlignHCenter
                                text: modelData.confirmed
                                color: modelData.confirmed ? Theme.accentColor : Theme.textSecondary
                                font.family: Theme.monoFamily
                                font.pixelSize: 10
                                font.bold: true
                            }
                        }
                    }
                    Item { Layout.preferredWidth: 40 }
                    Item { Layout.preferredWidth: 90 }
                }
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("● confirmed  ○ worked  ·  double-click opens the first QSO. DXCC counts entities with a DXCC number; "
                           + "use Setup → General → Fill missing DXCC for older QSOs.")
                wrapMode: Text.Wrap
                color: Theme.textSecondary
                font.pixelSize: 11
            }
        }
    }
}
