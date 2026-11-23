// DecoDXLog — scheda del QSO, lettura e modifica (mockup 1c).
//
// Si modifica una copia dei campi ADIF; "Save" la riscrive come nuova revisione
// e la versione di prima va nello storico, da cui si puo' rimettere in vigore.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Decodium.UI

DialogFrame {
    id: root

    property var qsoId: 0
    property var detail: ({})
    property var fields: ({})
    property var touchedFields: ({})
    property int formRev: 0
    property bool edited: false
    property bool recalculationApplied: false
    property bool activationReviewApplied: false
    property string detailLogPath: ""
    // Cresce a ogni tasto scritto in un campo: chi guarda cosa e' cambiato lo rilegge.
    property int editRev: 0
    property int currentTab: 0
    property var profileId: 0
    property var enrichments: []
    property var attachments: []
    property var managerSuggestion: ({})
    QslOcrDialog { id: qslOcrDialog; onAcquired: root.attachments = decolog.qsoDetail(root.qsoId).attachments || [] }
    QslOcrHistoryDialog { id: qslOcrHistory; onHistoryChanged: root.attachments = decolog.qsoDetail(root.qsoId).attachments || [] }
    function probeOcr(url, match, acquire, filtered) { qslOcrDialog.open(); qslOcrDialog.probe(url, match, acquire, filtered) }
    function probeOcrHistory() { qslOcrHistory.openFor(root.qsoId, true) }
    function probeOcrReview() { qslOcrHistory.openFor(root.qsoId, false, true) }
    function probeOcrRevisions() { qslOcrHistory.openFor(root.qsoId, false, false, true) }
    function showOcrHistory() { qslOcrHistory.openFor(root.qsoId, false) }
    function openActivationReview() {
        activationReview.keys = ["MY_POTA_REF", "MY_SOTA_REF", "MY_WWFF_REF", "MY_IOTA", "MY_GRIDSQUARE", "STATION_CALLSIGN", "OPERATOR"]
        activationReview.values = activationReview.keys.map(k => String(root.field(k)))
        activationReview.open()
    }
    function probeActivationReview() {
        console.warn("PROBE activation review shown " + activationWarning.visible)
        root.openActivationReview(); activationReview.close()
        console.warn("PROBE activation review cancel " + root.field("APP_DECOLOG_ACTIVATION_REVIEW"))
        root.openActivationReview(); activationReview.values = ["IT-0002", "", "", "", "JM76", "", ""]
        activationReview.applyDraft(); decolog.editQsoField(root.qsoId,"comment","concurrent review edit"); root.save()
        console.warn("PROBE activation review stale " + (errorText.text.length > 0 && decolog.qsoDetail(root.qsoId).fields.APP_DECOLOG_ACTIVATION_REVIEW === "PERIOD_UNCERTAIN"))
        root.reload(); root.openActivationReview(); activationReview.values = ["IT-0002", "", "", "", "JM76", "", ""]
        activationReview.applyDraft(); root.save()
        console.warn("PROBE activation review saved " + (root.field("APP_DECOLOG_ACTIVATION_REVIEW") === "" && root.field("MY_POTA_REF") === "IT-0002" && !errorText.text))
    }

    title: qsTr("QSO detail")
    dotColor: Theme.primaryColor
    info: detail.uuid ? "#" + qsoId + " · rev " + detail.revision + " · " + detail.uuid.substring(0, 6) + "…"
                        + detail.uuid.slice(-2) : ""
    dialogKey: "qso"
    width: 820
    height: 700

    // Campi ADIF degli stati QSL per servizio: [inviato, data, ricevuto, data].
    readonly property var qslFields: ({
        lotw: ["LOTW_QSL_SENT", "LOTW_QSLSDATE", "LOTW_QSL_RCVD", "LOTW_QSLRDATE"],
        qrz: ["QRZCOM_QSO_UPLOAD_STATUS", "QRZCOM_QSO_UPLOAD_DATE", "QRZCOM_QSO_DOWNLOAD_STATUS", "QRZCOM_QSO_DOWNLOAD_DATE"],
        clublog: ["CLUBLOG_QSO_UPLOAD_STATUS", "CLUBLOG_QSO_UPLOAD_DATE", "", ""],
        eqsl: ["EQSL_QSL_SENT", "EQSL_QSLSDATE", "EQSL_QSL_RCVD", "EQSL_QSLRDATE"],
        card: ["QSL_SENT", "QSLSDATE", "QSL_RCVD", "QSLRDATE"]
    })
    readonly property var derivedInspection: { root.formRev; root.editRev; return decolog.inspectDerivedQso(root.fields) }
    function probeDerivedEdit() {
        console.warn("PROBE derived before " + root.derivedInspection.state)
        root.setField("APP_A","6")
        Qt.callLater(function() {
            console.warn("PROBE derived editing " + root.derivedInspection.state + " " + derivedWarning.visible + " " + (root.derivedInspection.affected || []).join(","))
            root.save()
            console.warn("PROBE derived saved " + root.derivedInspection.state + " " + derivedWarning.visible + " " + errorText.text)
        })
    }
    function probeDerivedRestore() { console.warn("PROBE derived restored " + root.derivedInspection.state + " " + derivedWarning.visible + " " + (root.derivedInspection.affected || []).join(",")) }
    function previewDerived() {
        const result=decolog.previewDerivedQso(root.fields)
        if (result.error) { errorText.text=result.error; return }
        derivedPreview.data=result; derivedPreview.editRevision=root.editRev; derivedPreview.formRevision=root.formRev; derivedPreview.qso=root.qsoId
        errorText.text=""; derivedPreview.open()
    }
    function probeDerivedRecalculate() {
        root.previewDerived()
        console.warn("PROBE derived preview " + derivedPreview.visible + " " + JSON.stringify(derivedPreview.data.changes || []) + " db=" + decolog.qsoDetail(root.qsoId).fields.APP_B)
        derivedPreview.close(); console.warn("PROBE derived cancelled " + root.field("APP_B"))
        root.previewDerived(); root.setField("COMMENT","probe note"); applyDerivedPreview.clicked()
        console.warn("PROBE derived stale preview " + errorText.text + " " + root.field("APP_B"))
        root.previewDerived(); const edit=decolog.editQsoField(root.qsoId,"comment","external edit"); applyDerivedPreview.clicked()
        console.warn("PROBE derived changed QSO " + edit + " " + errorText.text + " " + root.field("APP_B")); root.reload()
        root.previewDerived(); applyDerivedPreview.clicked()
        console.warn("PROBE derived draft " + root.field("APP_B") + " " + root.field("APP_C") + " " + root.derivedInspection.state + " db=" + decolog.qsoDetail(root.qsoId).fields.APP_B)
        const late=decolog.editQsoField(root.qsoId,"comment","late edit"); root.save()
        console.warn("PROBE derived guarded save " + late + " " + errorText.text + " db=" + decolog.qsoDetail(root.qsoId).fields.APP_B)
        root.reload(); root.previewDerived(); applyDerivedPreview.clicked()
        root.save(); console.warn("PROBE derived recalculated " + root.derivedInspection.state + " " + root.field("APP_B") + " " + root.field("APP_C") + " " + errorText.text)
    }
    function probeDerivedPreview(path) {
        root.previewDerived(); console.warn("PROBE derived visual preview " + derivedPreview.visible + " " + (derivedPreview.data.changes || []).length)
        Qt.callLater(function() { derivedPreview.contentItem.grabToImage(function(image) { console.warn("PROBE derived image " + image.saveToFile(path)) }) })
    }

    function openFor(id) {
        qsoId = id
        currentTab = 0
        reload()
        open()
    }

    function reload() {
        let started = Date.now()
        detail = decolog.qsoDetail(qsoId)
        detailLogPath = decolog.currentLogInfo().path || ""
        recalculationApplied = false
        activationReviewApplied = false
        decolog.recordUiLatency("qso.detail.record", Date.now() - started)
        started = Date.now()
        fields = Object.assign({}, detail.fields || {})
        touchedFields = ({})
        profileId = detail.stationProfileId || 0
        enrichments = decolog.qsoEnrichments(qsoId)
        attachments = detail.attachments || []
        decolog.recordUiLatency("qso.detail.fields", Date.now() - started)
        started = Date.now()
        managerSuggestion = decolog.qslManagerSuggestion(qsoId)
        edited = false
        errorText.text = ""
        formRev++
        decolog.recordUiLatency("qso.detail.bindings", Date.now() - started)
    }

    function field(key) { return fields[key] !== undefined ? fields[key] : "" }
    function setField(key, value) {
        if (!key)
            return
        touchedFields[key] = true
        if (value === "" || value === undefined)
            delete fields[key]
        else
            fields[key] = value
        edited = true
        editRev++
    }
    // Il nominativo e' stato corretto, non ancora salvato.
    readonly property bool callChanged: {
        root.editRev; root.formRev
        return !!detail.fields && String(fields.CALL || "").trim().toUpperCase()
                                  !== String(detail.fields.CALL || "").trim().toUpperCase()
    }

    // Le date si leggono e si scrivono nella forma della lingua (in italiano
    // 25/09/2026); nel QSO restano ADIF, 20260925.
    function adifDate(d) { return d && d.length === 8 ? decolog.showDate(d) : d }
    function adifTime(t) { return t && t.length >= 4 ? t.substring(0, 2) + ":" + t.substring(2, 4) + (t.length === 6 ? ":" + t.substring(4, 6) : "") : t }
    function toAdifDate(s) {
        const iso = decolog.readDate(s)
        return iso.length > 0 ? iso.replace(/-/g, "") : s.replace(/[^0-9]/g, "")
    }
    function toAdifTime(s) { return s.replace(/:/g, "").trim() }

    function modeLabel() {
        const sub = field("SUBMODE")
        return sub.length ? sub : field("MODE")
    }

    function save() {
        // Un'ora di fine senza data di fine vale per lo stesso giorno.
        if (fields.TIME_OFF && !fields.QSO_DATE_OFF && fields.QSO_DATE)
            fields.QSO_DATE_OFF = fields.QSO_DATE
        const error = recalculationApplied || activationReviewApplied
            ? decolog.saveQsoGuarded(qsoId, fields, profileId, Object.keys(touchedFields), detail.uuid, detail.revision, detailLogPath)
            : decolog.saveQso(qsoId, fields, profileId, Object.keys(touchedFields))
        errorText.text = error
        if (error.length === 0)
            reload()
    }

    FileDialog {
        id: exportOne
        title: qsTr("Export QSO as ADIF")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "adi"
        nameFilters: [qsTr("ADIF files (*.adi)")]
        onAccepted: decolog.exportQsos([root.qsoId], selectedFile)
    }
    Popup {
        id: activationReview
        anchors.centerIn: Overlay.overlay; modal: true; padding: 16; width: 620; height: 570
        property var keys: []
        property var values: []
        background: Rectangle { color: Theme.panelColor; border.color: Theme.warningColor; radius: 6 }
        function applyDraft() {
            for (let i=0; i<keys.length; ++i) root.setField(keys[i], String(values[i]).trim().toUpperCase())
            root.setField("APP_DECOLOG_ACTIVATION_REVIEW", "")
            root.setField("APP_DECOLOG_ACTIVATION_REVIEWED_AT", new Date().toISOString())
            root.activationReviewApplied=true; root.formRev++; close()
        }
        contentItem: ColumnLayout {
            spacing: 7
            Text { Layout.fillWidth: true; color: Theme.textPrimary; font.bold: true; text: qsTr("Review recovered activation") }
            Text { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.warningColor
                text: qsTr("The QSO overlaps an uncertain activation period. Check the original records and enter only known values. No session is assigned. Applying changes the draft; save the QSO separately.") }
            GridLayout {
                columns: 2; Layout.fillWidth: true
                Repeater {
                    model: [qsTr("My POTA reference"), qsTr("My SOTA reference"), qsTr("My WWFF reference"), qsTr("My IOTA reference"), qsTr("My grid"), qsTr("Station call"), qsTr("Operator")]
                    delegate: RowLayout {
                        required property string modelData; required property int index
                        Layout.fillWidth: true
                        Text { Layout.preferredWidth: 135; text: modelData; color: Theme.textPrimary; wrapMode: Text.Wrap }
                        TextField { Layout.fillWidth: true; text: activationReview.values[index] || ""; onTextEdited: { let a=activationReview.values.slice(); a[index]=text; activationReview.values=a } }
                    }
                }
            }
            Item { Layout.fillHeight: true }
            RowLayout { Layout.alignment: Qt.AlignRight
                GlassButton { text: qsTr("Cancel"); onClicked: activationReview.close() }
                GlassButton { text: qsTr("Apply reviewed values"); onClicked: activationReview.applyDraft() }
            }
        }
    }
    Popup {
        id:derivedPreview; anchors.centerIn:Overlay.overlay; modal:true; padding:16; width:620; height:420
        property var data:({}); property int editRevision:0; property int formRevision:0; property var qso:0
        background:Rectangle { color:Theme.panelColor; border.color:Theme.warningColor; radius:6 }
        contentItem:ColumnLayout {
            spacing:8
            Text { Layout.fillWidth:true; color:Theme.textPrimary; font.bold:true; text:qsTr("Review derived values") }
            Text { Layout.fillWidth:true; wrapMode:Text.Wrap; color:Theme.warningColor
                text:qsTr("Uses the formulas and numeric locale recorded with this QSO (%1). Review the proposed values; applying changes the draft only. Save the QSO separately.").arg(derivedPreview.data.inputLocale || "") }
            ScrollView {
                Layout.fillWidth:true; Layout.fillHeight:true; clip:true
                ColumnLayout {
                    width:parent.width
                    Repeater {
                        model:derivedPreview.data.changes || []
                        Text { required property var modelData; Layout.fillWidth:true; wrapMode:Text.Wrap; textFormat:Text.PlainText; color:Theme.textPrimary
                            text:modelData.field + ": " + (modelData.before || "—") + " → " + modelData.after + "\n" + modelData.formula }
                    }
                }
            }
            RowLayout { Layout.alignment:Qt.AlignRight
                GlassButton { text:qsTr("Cancel"); onClicked:derivedPreview.close() }
                GlassButton { id:applyDerivedPreview; text:qsTr("Apply to draft"); onClicked:{
                    const live=decolog.qsoDetail(root.qsoId)
                    if (derivedPreview.qso!==root.qsoId || derivedPreview.editRevision!==root.editRev || derivedPreview.formRevision!==root.formRev || !live.uuid || live.uuid!==root.detail.uuid || live.revision!==root.detail.revision || decolog.currentLogInfo().path!==root.detailLogPath) {
                        derivedPreview.close(); errorText.text=qsTr("The QSO or draft changed; review the calculation again."); return
                    }
                    const values=derivedPreview.data.values || ({})
                    for (const key of Object.keys(values)) root.setField(key,String(values[key]))
                    root.setField("APP_DECOLOG_DERIVED_V1",String(derivedPreview.data.trace)); root.recalculationApplied=true; root.formRev++; derivedPreview.close()
                } }
            }
        }
    }

    FileDialog {
        id: attachmentPicker
        title: qsTr("Import received QSL scan or image")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Images and documents (*.png *.jpg *.jpeg *.webp *.pdf)"), qsTr("All files (*)")]
        onAccepted: {
            const error = decolog.importQslAttachment([root.qsoId], selectedFile)
            errorText.text = error
            if (!error.length)
                root.reload()
        }
    }

    Popup {
        id: confirmDelete
        anchors.centerIn: Overlay.overlay
        modal: true
        padding: 14
        background: Rectangle { color: Theme.panelColor; border.color: Theme.errorColor; radius: 6 }
        ColumnLayout {
            spacing: 10
            Text {
                text: qsTr("Delete %1? The QSO stays in the history and can be recovered.").arg(root.field("CALL"))
                color: Theme.textPrimary
                font.pixelSize: 13
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                GlassButton { text: qsTr("Cancel"); onClicked: confirmDelete.close() }
                GlassButton {
                    text: qsTr("Delete")
                    tone: Theme.errorColor
                    filled: true
                    onClicked: {
                        confirmDelete.close()
                        if (decolog.deleteQso(root.qsoId))
                            root.close()
                    }
                }
            }
        }
    }

    // Un campo ADIF con etichetta. `transform` e `untransform` convertono fra il
    // formato ADIF e quello mostrato (date e ore).
    component AdifInput: LabeledField {
        id: input
        property string key: ""
        property bool mono: true
        property bool upper: false
        property var display: (v) => v
        property var store: (v) => v
        property color accent: Theme.primaryColor
        property bool highlight: false
        Layout.fillWidth: true
        StyledTextField {
            Layout.fillWidth: true
            mono: input.mono
            uppercase: input.upper
            accentBorder: input.accent
            font.bold: input.highlight
            color: input.highlight ? input.accent : Theme.textPrimary
            text: { root.formRev; return input.display(root.field(input.key)) }
            onTextEdited: root.setField(input.key, input.store(input.upper ? text.toUpperCase() : text))
            background: Rectangle {
                radius: 4
                color: Theme.bgMedium
                border.width: 1
                border.color: parent.activeFocus || input.highlight ? input.accent : Theme.glassBorder
            }
        }
    }

    body: ColumnLayout {
        spacing: 0
        RowLayout {
            Layout.fillWidth: true; Layout.leftMargin: 14; Layout.rightMargin: 14; Layout.topMargin: 6
            visible: { root.formRev; root.editRev; return root.field("APP_DECOLOG_ACTIVATION_REVIEW") === "PERIOD_UNCERTAIN" }
            Text { id: activationWarning; Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.warningColor
                text: qsTr("Recovered activation period is uncertain. Review the site, references and operator against your records.") }
            GlassButton { text: qsTr("Review activation…"); onClicked: root.openActivationReview() }
        }
        RowLayout {
            visible:root.derivedInspection.state !== "none"
            Layout.fillWidth:true; Layout.leftMargin:14; Layout.rightMargin:14; Layout.topMargin:6
            Text {
                id:derivedWarning; objectName:"derivedProvenanceWarning"
                Layout.fillWidth:true; visible:root.derivedInspection.state === "stale" || root.derivedInspection.state === "unknown"
                wrapMode:Text.Wrap; color:Theme.warningColor
                text:root.derivedInspection.state === "stale" ? qsTr("Derived values may be stale: %1. Recorded inputs or results changed; values are not recalculated automatically.").arg((root.derivedInspection.affected || []).join(", ")) : qsTr("Derived-field provenance cannot be checked for this QSO. Review the stored values and formula manually.")
            }
            GlassButton { text:qsTr("Review calculation…"); visible:root.derivedInspection.state === "stale" || root.derivedInspection.state === "matches"; onClicked:root.previewDerived() }
        }

        // ── Intestazione: nominativo, dove, come ────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            Layout.topMargin: 12
            Layout.bottomMargin: 12
            spacing: 14
            Text {
                text: { root.formRev; return root.field("CALL") }
                color: Theme.textPrimary
                font.family: Theme.monoFamily
                font.pixelSize: 26
                font.bold: true
                font.letterSpacing: 1.5
            }
            Column {
                Layout.fillWidth: true
                Text {
                    text: { root.formRev; return [root.field("NAME"), root.field("QTH"), root.field("GRIDSQUARE")].filter(s => s).join(" · ") }
                    color: Theme.textPrimary
                    font.pixelSize: 12
                }
                Text {
                    text: {
                        root.formRev
                        const parts = []
                        if (root.field("COUNTRY")) parts.push(root.field("COUNTRY"))
                        if (root.field("DXCC")) parts.push("DXCC " + root.field("DXCC"))
                        if (root.field("CQZ")) parts.push("CQ " + root.field("CQZ"))
                        if (root.field("ITUZ")) parts.push("ITU " + root.field("ITUZ"))
                        if (root.detail.distanceKm !== undefined) parts.push(root.detail.distanceKm.toLocaleString(Qt.locale("en_US"), "f", 0) + " km")
                        if (root.detail.azimuth !== undefined) parts.push(root.detail.azimuth + "°")
                        return parts.join(" · ")
                    }
                    color: Theme.textSecondary
                    font.family: Theme.monoFamily
                    font.pixelSize: 11
                }
            }
            Pill {
                text: { root.formRev; return root.modeLabel() }
                detail: { root.formRev; return root.field("SUBMODE").length ? root.field("MODE") + "/" + root.field("SUBMODE") : "" }
                tone: root.modeLabel() === "FT2" ? Theme.accentColor : Theme.primaryColor
                pillHeight: 24
            }
            Pill { text: root.detail.source || ""; tone: Theme.secondaryColor; pillHeight: 24 }
            Pill { visible: root.detail.dirty === true || root.edited; text: root.edited ? qsTr("edited") : "dirty"; tone: Theme.warningColor; pillHeight: 24 }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.borderSoft }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.topMargin: 8
            spacing: 2
            TabChip { text: qsTr("General"); active: root.currentTab === 0; onClicked: root.currentTab = 0 }
            TabChip { text: qsTr("Location"); active: root.currentTab === 1; onClicked: root.currentTab = 1 }
            TabChip { text: qsTr("QSL"); active: root.currentTab === 2; onClicked: root.currentTab = 2 }
            TabChip {
                text: qsTr("ADIF extra")
                badge: String((root.detail.extra || []).length)
                active: root.currentTab === 3
                onClicked: root.currentTab = 3
            }
            TabChip {
                text: qsTr("History")
                badge: String((root.detail.history || []).length)
                active: root.currentTab === 4
                onClicked: root.currentTab = 4
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            Layout.topMargin: 12
            spacing: 14

            StackLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                Layout.preferredHeight: 360
                currentIndex: root.currentTab

                // ── General ─────────────────────────────────────────────────
                ColumnLayout {
                    Layout.alignment: Qt.AlignTop
                    spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        // Il nominativo sbagliato si corregge qui: nazione, DXCC e zone si rifanno.
                        AdifInput {
                            Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 5; key: "CALL"; label: qsTr("Call"); upper: true; highlight: true }
                        AdifInput {
                            Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 4; key: "QSO_DATE"; label: qsTr("Date on UTC"); display: root.adifDate; store: root.toAdifDate }
                        AdifInput {
                            Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 4; key: "TIME_ON"; label: qsTr("Time on"); display: root.adifTime; store: root.toAdifTime }
                        AdifInput {
                            Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 4; key: "TIME_OFF"; label: qsTr("Time off"); display: root.adifTime; store: root.toAdifTime }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: root.callChanged
                        wrapMode: Text.Wrap
                        color: Theme.warningColor
                        font.pixelSize: 11
                        text: qsTr("The call changes: country, DXCC and zones are worked out again from the new call, and "
                                   + "the services that already have this QSO unconfirmed will get it again.")
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        AdifInput {
                            Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 2; key: "BAND"; label: qsTr("Band") }
                        AdifInput {
                            Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 4; key: "FREQ"; label: qsTr("Freq MHz") }
                        AdifInput {
                            Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 3; key: "MODE"; label: qsTr("Mode"); upper: true }
                        AdifInput {
                            Layout.preferredWidth: 1
                            Layout.horizontalStretchFactor: 3; key: "SUBMODE"; label: qsTr("Submode"); upper: true
                            highlight: { root.formRev; return root.field("SUBMODE") === "FT2" }
                            accent: Theme.accentColor
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        AdifInput {
                            Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 3; key: "RST_SENT"; label: qsTr("RST sent") }
                        AdifInput {
                            Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 3; key: "RST_RCVD"; label: qsTr("RST rcvd") }
                        AdifInput {
                            Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 3; key: "GRIDSQUARE"; label: qsTr("Grid"); upper: true }
                        AdifInput {
                            Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 3; key: "TX_PWR"; label: qsTr("TX pwr W") }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        AdifInput {
                            Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 6; key: "NAME"; label: qsTr("Name"); mono: false }
                        AdifInput {
                            Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 6; key: "QTH"; label: qsTr("QTH"); mono: false }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        AdifInput {
                            Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 7; key: "COMMENT"; label: qsTr("Comment"); mono: false }
                        LabeledField {
                            Layout.preferredWidth: 1
                            Layout.horizontalStretchFactor: 4
                            Layout.fillWidth: true
                            label: qsTr("Station profile")
                            StyledComboBox {
                                Layout.fillWidth: true
                                model: decolog.stationProfiles
                                textRole: "name"
                                currentIndex: decolog.stationProfiles.rowForId(root.profileId)
                                displayText: currentIndex >= 0 ? currentText : qsTr("none")
                                onActivated: (index) => {
                                    root.profileId = decolog.stationProfiles.get(index).id
                                    root.edited = true
                                }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Rectangle {
                            Layout.preferredWidth: 1
                            Layout.horizontalStretchFactor: 12
                            Layout.fillWidth: true
                            implicitHeight: originText.implicitHeight + 16
                            radius: 4
                            color: Theme.bgMedium
                            border.width: 1
                            border.color: Theme.borderSoft
                            Text {
                                id: originText
                                anchors.fill: parent
                                anchors.margins: 8
                                wrapMode: Text.Wrap
                                textFormat: Text.StyledText
                                text: "<b><font color=\"" + Theme.secondaryColor + "\">ORIGIN</font></b>  "
                                + "source_app = " + (root.detail.sourceApp || "—")
                                + " · created_at " + decolog.showDate(root.detail.createdAt || "")
                                + " · updated_at " + decolog.showDate(root.detail.updatedAt || "")
                                color: Theme.textSecondary
                                font.family: Theme.monoFamily
                                font.pixelSize: 11
                            }
                        }
                    }
                }

                // ── Location ────────────────────────────────────────────────
                ColumnLayout {
                    Layout.alignment: Qt.AlignTop
                    spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        AdifInput {
                            Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 6; key: "COUNTRY"; label: qsTr("Country"); mono: false }
                        AdifInput {
                            Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 2; key: "DXCC"; label: "DXCC" }
                        AdifInput {
                            Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 2; key: "CQZ"; label: qsTr("CQ zone") }
                        AdifInput {
                            Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 2; key: "ITUZ"; label: qsTr("ITU zone") }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        AdifInput {
                            Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 2; key: "CONT"; label: qsTr("Cont"); upper: true }
                        AdifInput {
                            Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 4; key: "STATE"; label: qsTr("State"); upper: true }
                        AdifInput {
                            Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 6; key: "CNTY"; label: qsTr("County"); mono: false }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        AdifInput {
                            Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 8; key: "ADDRESS"; label: qsTr("Address"); mono: false }
                        AdifInput {
                            Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 5; key: "EMAIL"; label: qsTr("E-mail"); mono: false }
                        AdifInput {
                            Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 4; key: "QSL_VIA"; label: qsTr("QSL via"); upper: true }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        AdifInput {
                            Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 3; key: "POTA_REF"; label: "POTA"; upper: true }
                        AdifInput {
                            Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 3; key: "SOTA_REF"; label: "SOTA"; upper: true }
                        AdifInput {
                            Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 3; key: "IOTA"; label: "IOTA"; upper: true }
                        AdifInput {
                            Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 3; key: "WWFF_REF"; label: "WWFF"; upper: true }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        // SIG e SIG_INFO: il programma di un QSO e il suo
                        // riferimento. "DCI" e "NA015" per i castelli d'Italia.
                        AdifInput {
                            Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 3
                            key: "SIG"; label: qsTr("Program"); upper: true }
                        AdifInput {
                            Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 9
                            key: "SIG_INFO"; label: qsTr("Reference (DCI, GMA…)"); upper: true }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        AdifInput {
                            Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 3; key: "PROP_MODE"; label: qsTr("Prop mode"); upper: true }
                        AdifInput {
                            Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 3; key: "SAT_NAME"; label: qsTr("Satellite"); upper: true }
                        AdifInput {
                            Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 3; key: "SAT_MODE"; label: qsTr("Sat mode"); upper: true }
                        AdifInput {
                            Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 3; key: "MY_GRIDSQUARE"; label: qsTr("My grid"); upper: true }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        AdifInput {
                            Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 4; key: "STATION_CALLSIGN"; label: qsTr("Station call"); upper: true }
                        AdifInput {
                            Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 4; key: "OPERATOR"; label: qsTr("Operator"); upper: true }
                        AdifInput {
                            Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 4; key: "BAND_RX"; label: qsTr("Band RX") }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        AdifInput {
                            Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 8; key: "NOTES"; label: qsTr("Notes"); mono: false }
                        AdifInput {
                            Layout.preferredWidth: 1; Layout.horizontalStretchFactor: 4; key: "APP_DECOLOG_TAGS"; label: qsTr("Tags (comma separated)"); mono: false }
                    }
                }

                // ── QSL ─────────────────────────────────────────────────────
                ColumnLayout {
                    spacing: 6
                    Layout.alignment: Qt.AlignTop
                    RowLayout {
                        spacing: 10
                        Repeater {
                            model: [[qsTr("Service"), 80], [qsTr("Sent"), 90], [qsTr("Sent date"), 120], [qsTr("Rcvd"), 90], [qsTr("Rcvd date"), 120]]
                            FieldLabel {
                                required property var modelData
                                Layout.preferredWidth: modelData[1]
                                text: modelData[0]
                            }
                        }
                    }
                    Repeater {
                        model: root.detail.qsl || []
                        RowLayout {
                            id: qslRow
                            required property var modelData
                            readonly property var keys: root.qslFields[modelData.service]
                            spacing: 10
                            Text {
                                Layout.preferredWidth: 80
                                text: qslRow.modelData.label
                                color: Theme.textPrimary
                                font.family: Theme.monoFamily
                                font.pixelSize: 12
                                font.bold: true
                            }
                            StyledComboBox {
                                Layout.preferredWidth: 90
                                model: qslRow.modelData.service === "clublog" || qslRow.modelData.service === "qrz"
                                       ? ["N", "Y", "M"] : ["N", "Y", "R", "Q", "I"]
                                currentIndex: { root.formRev; return Math.max(0, model.indexOf(root.field(qslRow.keys[0]) || "N")) }
                                onActivated: root.setField(qslRow.keys[0], currentText === "N" ? "" : currentText)
                            }
                            StyledTextField {
                                Layout.preferredWidth: 120
                                placeholderText: decolog.dateHint
                                text: { root.formRev; return root.adifDate(root.field(qslRow.keys[1])) }
                                onTextEdited: root.setField(qslRow.keys[1], root.toAdifDate(text))
                            }
                            StyledComboBox {
                                Layout.preferredWidth: 90
                                enabled: qslRow.keys[2].length > 0
                                model: ["N", "Y", "R", "I", "V"]
                                currentIndex: { root.formRev; return Math.max(0, model.indexOf(root.field(qslRow.keys[2]) || "N")) }
                                onActivated: root.setField(qslRow.keys[2], currentText === "N" ? "" : currentText)
                            }
                            StyledTextField {
                                Layout.preferredWidth: 120
                                enabled: qslRow.keys[3].length > 0
                                placeholderText: enabled ? decolog.dateHint : ""
                                text: { root.formRev; return root.adifDate(root.field(qslRow.keys[3])) }
                                onTextEdited: root.setField(qslRow.keys[3], root.toAdifDate(text))
                            }
                        }
                    }
                    Text {
                        Layout.topMargin: 6
                        text: qsTr("Y yes · N no · R requested · Q queued · I ignore · M modified · V verified")
                        color: Theme.textSecondary
                        font.family: Theme.monoFamily
                        font.pixelSize: 11
                    }
                }

                // ── ADIF extra ──────────────────────────────────────────────
                ColumnLayout {
                    spacing: 6
                    Layout.alignment: Qt.AlignTop
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: qsTr("Fields without a column of their own, kept exactly as they arrived (adif_extra). They go back out on export.")
                        color: Theme.textSecondary
                        font.pixelSize: 11
                    }
                    ListView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 260
                        clip: true
                        spacing: 6
                        model: root.detail.extra || []
                        ScrollBar.vertical: ScrollBar {}
                        delegate: RowLayout {
                            id: extraRow
                            required property var modelData
                            width: ListView.view.width - 12
                            spacing: 10
                            Text {
                                Layout.preferredWidth: 200
                                text: extraRow.modelData.name
                                color: Theme.secondaryColor
                                font.family: Theme.monoFamily
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                            StyledTextField {
                                Layout.fillWidth: true
                                text: { root.formRev; return root.field(extraRow.modelData.name) }
                                onTextEdited: root.setField(extraRow.modelData.name, text)
                            }
                        }
                    }
                    RowLayout {
                        spacing: 8
                        StyledTextField { id: newKey; Layout.preferredWidth: 200; uppercase: true; placeholderText: qsTr("FIELD_NAME") }
                        StyledTextField { id: newValue; Layout.fillWidth: true; placeholderText: qsTr("value") }
                        GlassButton {
                            text: qsTr("+ Add")
                            tone: Theme.primaryColor
                            enabled: /^[A-Za-z][A-Za-z0-9_]*$/.test(newKey.text) && newValue.text.length > 0
                            onClicked: {
                                root.setField(newKey.text.toUpperCase(), newValue.text)
                                const extra = (root.detail.extra || []).slice()
                                extra.push({ name: newKey.text.toUpperCase(), value: newValue.text })
                                root.detail = Object.assign({}, root.detail, { extra: extra })
                                newKey.text = ""
                                newValue.text = ""
                                root.formRev++
                            }
                        }
                    }
                }

                // ── History ─────────────────────────────────────────────────
                ColumnLayout {
                    spacing: 6
                    Layout.alignment: Qt.AlignTop
                    Text {
                        visible: (root.detail.history || []).length === 0
                        text: qsTr("No earlier revisions: this QSO has never been edited.")
                        color: Theme.textSecondary
                        font.pixelSize: 12
                    }
                    ListView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 320
                        clip: true
                        spacing: 4
                        model: root.detail.history || []
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            id: histRow
                            required property var modelData
                            width: ListView.view.width - 12
                            implicitHeight: 40
                            radius: 4
                            color: Theme.bgMedium
                            border.width: 1
                            border.color: Theme.borderSoft
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 8
                                spacing: 10
                                Pill { text: "rev " + histRow.modelData.revision; tone: Theme.primaryColor; pillHeight: 20; fontPixelSize: 10 }
                                Pill {
                                    text: histRow.modelData.reason
                                    tone: histRow.modelData.reason === "delete" ? Theme.errorColor
                                        : histRow.modelData.reason === "conflict_lost" ? Theme.warningColor : Theme.textSecondary
                                    pillHeight: 20
                                    fontPixelSize: 10
                                }
                                Text {
                                    text: histRow.modelData.recordedAt + "Z"
                                    color: Theme.textSecondary
                                    font.family: Theme.monoFamily
                                    font.pixelSize: 11
                                }
                                Text {
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    text: histRow.modelData.summary
                                    color: Theme.textPrimary
                                    font.family: Theme.monoFamily
                                    font.pixelSize: 11
                                }
                                GlassButton {
                                    text: qsTr("Restore")
                                    buttonHeight: 24
                                    fontPixelSize: 11
                                    onClicked: {
                                        const error = decolog.restoreRevision(root.qsoId, histRow.modelData.id)
                                        errorText.text = error
                                        if (error.length === 0)
                                            root.reload()
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ── Colonna QSL e award ─────────────────────────────────────────
            ColumnLayout {
                Layout.preferredWidth: 250
                Layout.maximumWidth: 250
                Layout.fillHeight: true
                spacing: 6

                SectionTitle { text: qsTr("QSL status · per service") }
                RowLayout {
                    Layout.fillWidth: true
                    FieldLabel { Layout.preferredWidth: 70; text: "" }
                    FieldLabel { Layout.fillWidth: true; text: qsTr("Sent") }
                    FieldLabel { Layout.fillWidth: true; text: qsTr("Rcvd") }
                }
                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.glassBorder }
                Repeater {
                    model: root.detail.qsl || []
                    ColumnLayout {
                        id: statusRow
                        required property var modelData
                        function stateText(flag, date) {
                            if (flag === "N" || !flag) return "N"
                            if (flag === "R" || flag === "Q") return flag + " queued"
                            return flag + (date ? " · " + decolog.showDate(date) : "")
                        }
                        function stateColor(flag) {
                            return flag === "Y" ? Theme.accentColor : (flag === "R" || flag === "Q") ? Theme.warningColor : Theme.textSecondary
                        }
                        Layout.fillWidth: true
                        spacing: 0
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 28
                            Text { Layout.preferredWidth: 70; text: statusRow.modelData.label; color: Theme.textPrimary; font.family: Theme.monoFamily; font.pixelSize: 12; font.bold: true }
                            Text {
                                Layout.fillWidth: true
                                text: statusRow.stateText(statusRow.modelData.sent, statusRow.modelData.sentDate)
                                color: statusRow.stateColor(statusRow.modelData.sent)
                                font.family: Theme.monoFamily
                                font.pixelSize: 12
                            }
                            Text {
                                Layout.fillWidth: true
                                text: statusRow.modelData.hasRcvd ? statusRow.stateText(statusRow.modelData.rcvd, statusRow.modelData.rcvdDate) : "—"
                                color: statusRow.stateColor(statusRow.modelData.rcvd)
                                font.family: Theme.monoFamily
                                font.pixelSize: 12
                            }
                        }
                        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.borderSoft }
                    }
                }
                SectionTitle { text: qsTr("Received QSL archive") }
                Rectangle {
                    visible: !!root.managerSuggestion.available
                    Layout.fillWidth: true
                    implicitHeight: managerText.implicitHeight + 14
                    radius: 4
                    color: "transparent"
                    border.width: 1
                    border.color: root.managerSuggestion.stale ? Theme.warningColor : Theme.glassBorder
                    Text {
                        id: managerText
                        anchors.fill: parent
                        anchors.margins: 7
                        wrapMode: Text.Wrap
                        text: qsTr("QSL manager: %1 · %2 · %3")
                              .arg(root.managerSuggestion.manager)
                              .arg(root.managerSuggestion.source || qsTr("unknown source"))
                              .arg(root.managerSuggestion.message || "")
                        color: root.managerSuggestion.stale ? Theme.warningColor : Theme.textSecondary
                        font.pixelSize: 10
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    GlassButton {
                        text: qsTr("Import scan or image…")
                        buttonHeight: 24
                        fontPixelSize: 11
                        onClicked: attachmentPicker.open()
                    }
                    GlassButton {
                        text: qsTr("Read image with local OCR…")
                        buttonHeight: 24
                        fontPixelSize: 11
                        onClicked: qslOcrDialog.open()
                    }
                    GlassButton {
                        text: qsTr("OCR acquisition history…")
                        buttonHeight: 24
                        fontPixelSize: 11
                        enabled: !decolog.qslOcrAcquisition.busy
                        onClicked: qslOcrHistory.openFor(root.qsoId, false)
                    }
                    Text { Layout.fillWidth: true; text: qsTr("Not a confirmation by itself"); color: Theme.textSecondary; font.pixelSize: 10 }
                }
                Repeater {
                    model: root.attachments
                    RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Text { Layout.fillWidth: true; text: modelData.filename + " · " + modelData.mime; color: Theme.textPrimary; elide: Text.ElideRight; font.pixelSize: 11 }
                        Text { text: qsTr("%1 KB").arg(Math.max(1, Math.round((modelData.size || 0) / 1024))); color: Theme.textSecondary; font.pixelSize: 10 }
                    }
                }
                Repeater {
                    model: (root.detail.qsl || []).filter(q => q.lastError)
                    Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: errText.implicitHeight + 16
                        radius: 4
                        color: "transparent"
                        border.width: 1
                        border.color: Theme.errorColor
                        Text {
                            id: errText
                            anchors.fill: parent
                            anchors.margins: 8
                            wrapMode: Text.Wrap
                            text: modelData.label + " last_error: " + modelData.lastError
                            textFormat: Text.PlainText
                            color: Theme.errorColor
                            font.family: Theme.monoFamily
                            font.pixelSize: 11
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                SectionTitle { text: qsTr("Data provenance") }
                Rectangle {
                    Layout.fillWidth: true
                    visible: root.enrichments.length > 0
                    implicitHeight: provenanceColumn.implicitHeight + 16
                    radius: 4
                    color: Theme.glassOverlay
                    border.width: 1
                    border.color: Theme.borderSoft
                    ColumnLayout {
                        id: provenanceColumn
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 4
                        Repeater {
                            model: root.enrichments
                            RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.field + ": " + (modelData.previousValue || qsTr("empty"))
                                          + " → " + modelData.value + " · " + modelData.source
                                          + " · " + modelData.acquiredAt
                                    color: Theme.textSecondary
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                }
                                GlassButton {
                                    text: qsTr("Undo")
                                    buttonHeight: 24
                                    onClicked: {
                                        if (decolog.undoQsoEnrichment(root.qsoId, modelData.field))
                                            root.reload()
                                    }
                                }
                            }
                        }
                        Text {
                            visible: root.enrichments.length === 0
                            text: qsTr("No enriched fields recorded.")
                            color: Theme.textSecondary
                            font.pixelSize: 11
                        }
                    }
                }

                SectionTitle { text: qsTr("Award impact") }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: impactColumn.implicitHeight + 16
                    radius: 4
                    readonly property bool newDxcc: root.detail.firstFt2Dxcc === true
                    color: newDxcc ? Theme.rowMatchBg : "transparent"
                    border.width: 1
                    border.color: newDxcc ? Theme.accentColor : Theme.borderSoft
                    Column {
                        id: impactColumn
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 2
                        Text {
                            width: parent.width
                            wrapMode: Text.Wrap
                            text: parent.parent.newDxcc
                                  ? qsTr("FT2 Award: new DXCC %1").arg(root.field("COUNTRY") || root.field("DXCC"))
                                  : root.modeLabel() === "FT2" ? qsTr("FT2 Award: DXCC already worked")
                                  : qsTr("Not an FT2 QSO")
                            color: parent.parent.newDxcc ? Theme.accentColor : Theme.textSecondary
                            font.family: Theme.monoFamily
                            font.pixelSize: 11
                            font.bold: parent.parent.newDxcc
                        }
                        Text {
                            visible: root.modeLabel() === "FT2" && root.field("DXCC").length === 0
                            width: parent.width
                            wrapMode: Text.Wrap
                            text: qsTr("No DXCC entity on this QSO: it does not count yet")
                            color: Theme.warningColor
                            font.family: Theme.monoFamily
                            font.pixelSize: 11
                        }
                        Text {
                            visible: parent.parent.newDxcc
                            text: qsTr("%1 DXCC worked on FT2 · %2").arg(root.visible ? decolog.ft2Award.dxccWorked : 0)
                                  .arg(root.field("LOTW_QSL_RCVD") === "Y" ? qsTr("confirmed") : qsTr("unconfirmed"))
                            color: Theme.textSecondary
                            font.family: Theme.monoFamily
                            font.pixelSize: 11
                        }
                    }
                }
            }
        }

        Text {
            id: errorText
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.bottomMargin: 6
            visible: text.length > 0
            color: Theme.errorColor
            font.pixelSize: 12
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 52
            color: Theme.bgMedium
            radius: 6
            Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top } height: 6; color: parent.color }
            Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top } height: 1; color: Theme.borderSoft }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 10
                GlassButton { text: qsTr("Delete"); tone: Theme.errorColor; buttonHeight: 30; onClicked: confirmDelete.open() }
                Text {
                    text: qsTr("soft delete · kept in history")
                    color: Theme.textSecondary
                    font.family: Theme.monoFamily
                    font.pixelSize: 11
                }
                Item { Layout.fillWidth: true }
                GlassButton { text: qsTr("Revert"); buttonHeight: 30; enabled: root.edited; onClicked: root.reload() }
                GlassButton { text: qsTr("Export ADIF"); buttonHeight: 30; onClicked: exportOne.open() }
                GlassButton {
                    text: qsTr("Save · rev %1").arg((root.detail.revision || 0) + 1)
                    tone: Theme.accentColor
                    filled: true
                    buttonHeight: 30
                    enabled: root.edited
                    onClicked: root.save()
                }
            }
        }
    }
}
