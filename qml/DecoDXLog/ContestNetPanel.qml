// DecoDXLog — la rete della stazione multi-operatore: accesa o spenta, chi
// c'e' e su che banda, i messaggi fra operatori e gli spot interni. I QSO
// viaggiano da soli: fatto qui, arriva negli altri log, e viceversa.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Decodium.UI

GlassPanel {
    id: root

    readonly property var net: decolog.net
    function probeHandoff() {
        root.net.setLoopbackForProbe(); root.net.enabled=true; handoffDialog.open()
        handoffScope.text="test-event"; handoffDialog.configure()
        handoffRecipient.text="other"; handoffCall.text="K1ABC"; handoffFrequency.text="14025000"; handoffDialog.sendOffer()
        const incoming=root.net.handoffInbox; const before=incoming.length ? incoming[0].state : "missing"
        const stateError=incoming.length ? root.net.setHandoffState(incoming[0].id,"accepted") : "missing"
        return {visible:handoffDialog.visible,scope:root.net.handoffScope,offers:root.net.handoffOutbox.length,
            call:root.net.handoffOutbox.length ? root.net.handoffOutbox[0].call : "",warning:handoffWarning.text,error:handoffDialog.result,
            before:before,after:root.net.handoffInbox.length ? root.net.handoffInbox[0].state : "missing",stateError:stateError}
    }
    function probeNetworkReviewStart(secure) {
        root.net.setLoopbackForProbe(); root.net.enabled=true; qsoWireDialog.open(); wireKey.text=secure ? "a".repeat(64) : ""
        const error=root.net.setExperimentalQsoKey(wireKey.text); qsoWireDialog.keyReady=!error
        return !error && !root.net.armExperimentalQsoReceiver("sender","test-transfer")
    }
    function probeNetworkReviewOpen() { if (!root.net.reviewExperimentalQso()) return false; qsoDetailsDialog.open(); return true }
    function probeNetworkReviewState() { return {visible:qsoDetailsDialog.visible,fromNetwork:root.net.qsoReview.fromNetwork || false,secure:root.net.qsoReview.transportSecure || false,sender:root.net.qsoReview.sender || "",sourceLog:root.net.qsoReview.sourceLog || "",trust:networkReviewTrust.text,staleNotice:networkReviewStale.text,canInsert:root.net.qsoReview.canInsert || false,canApply:root.net.qsoReview.canApply || false,fields:(root.net.qsoReview.fields || []).length} }
    function probeExperimentalWire(secure) {
        qsoWireDialog.open()
        let expiryGuard=true
        if (secure) {
            wireKey.text="a".repeat(64); let error=root.net.setExperimentalQsoKey(wireKey.text); qsoWireDialog.keyReady=!error
            root.net.clearExperimentalQso(); expiryGuard=!qsoWireDialog.keyConsistent
            error=root.net.setExperimentalQsoKey(wireKey.text); qsoWireDialog.keyReady=!error
        }
        return {visible:qsoWireDialog.visible,title:qsoWireDialog.title,warning:qsoWireWarning.text,secure:root.net.experimentalQsoSecure,expiryGuard:expiryGuard}
    }
    function probeComparison(url) { qsoComparisonDialog.open(); return root.net.startQsoComparison(url) }
    function probeReview() { const rows=root.net.qsoComparisonRows; if (!rows.length || !root.net.reviewQsoComparison(rows[0].uuid)) return false; qsoDetailsDialog.open(); return true }
    function probeApply(accept) { if (!root.net.qsoReview.canApply) return false; qsoApplyDialog.open(); if (accept) qsoApplyDialog.confirmApply(); else qsoApplyDialog.close(); return true }
    function probeLongReview() {
        const fields=root.net.qsoReview.fields || []
        for (let i=0;i<fields.length;i++) for (const part of ["name","local","remote"]) {
            if ((fields[i][part] || "").length>2048) {
                if (!qsoLongDialog.begin(i,part)) return false
                while (!qsoLongDialog.pageDone) if (!qsoLongDialog.nextPage()) return false
                qsoLongDialog.close()
            }
        }
        return root.net.qsoReview.canInsert === true
    }
    function probeImport(accept) { if (!root.net.qsoReview.canInsert) return false; qsoImportDialog.open(); if (accept) qsoImportDialog.confirmImport(); else qsoImportDialog.close(); return true }
    function probeReviewState() { return {visible:qsoDetailsDialog.visible,title:qsoDetailsDialog.title,fields:(root.net.qsoReview.fields || []).length,ok:root.net.qsoReview.ok || false} }
    function probeComparisonState() { const rows=root.net.qsoComparisonRows, first=qsoComparisonList.itemAtIndex(0); return {visible:qsoComparisonDialog.visible,title:qsoComparisonDialog.title,summary:root.net.qsoComparisonSummary,rows:rows.length,firstFields:rows.length ? (rows[0].fields || []).length : 0,fieldShown:first ? first.fieldTextAt(0) : ""} }

    title: qsTr("Station network")
    dotColor: root.net.running ? Theme.accentColor : Theme.textSecondary
    padding: 8

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            ToggleSwitch {
                text: qsTr("On")
                checked: root.net.enabled
                onToggled: root.net.enabled = checked
            }
            LabeledField {
                label: qsTr("Network")
                StyledTextField {
                    Layout.preferredWidth: 110
                    text: root.net.group
                    uppercase: true
                    onEditingFinished: root.net.group = text
                }
            }
            LabeledField {
                label: qsTr("This station")
                StyledTextField {
                    Layout.preferredWidth: 110
                    text: root.net.stationName
                    onEditingFinished: root.net.stationName = text
                }
            }
            LabeledField {
                label: qsTr("UDP port")
                StyledTextField {
                    Layout.preferredWidth: 70
                    text: String(root.net.port)
                    onEditingFinished: root.net.port = parseInt(text) || 12060
                }
            }
            Item { Layout.fillWidth: true }
            GlassButton {
                Layout.alignment: Qt.AlignBottom
                text: qsTr("Sync")
                enabled: root.net.running
                onClicked: root.net.requestSync()
            }
        }
        Text {
            Layout.fillWidth: true
            elide: Text.ElideRight
            text: root.net.status + (root.net.running ? qsTr(" · sent %1 · received %2").arg(root.net.sent).arg(root.net.received) : "")
            color: Theme.textSecondary
            font.family: Theme.monoFamily
            font.pixelSize: 11
        }
        RowLayout {
            Layout.fillWidth:true
            GlassButton { text:qsTr("Serial experiment…"); onClicked:serialAuthorityDialog.open() }
            GlassButton { text:qsTr("Multiplier handoff…"); onClicked:handoffDialog.open() }
            GlassButton { text:qsTr("Compare logs…"); onClicked:qsoComparisonDialog.open() }
            GlassButton { text:qsTr("QSO transfer experiment…"); onClicked:qsoWireDialog.open() }
            Item { Layout.fillWidth:true }
        }

        // ── Chi c'e' ────────────────────────────────────────────────────────
        Repeater {
            model: root.net.peers
            RowLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 8
                Led { color: modelData.age < 12 ? Theme.accentColor : Theme.warningColor }
                Text { text: modelData.name; color: Theme.textPrimary; font.family: Theme.monoFamily; font.pixelSize: 12; font.bold: true }
                Text { text: modelData.op; color: Theme.textSecondary; font.family: Theme.monoFamily; font.pixelSize: 11 }
                Text {
                    text: [modelData.band, modelData.mode, modelData.khz > 0 ? modelData.khz.toFixed(1) : ""].filter(s => s).join(" ")
                    color: Theme.primaryColor
                    font.family: Theme.monoFamily
                    font.pixelSize: 11
                }
                Item { Layout.fillWidth: true }
                Text { text: qsTr("%n QSO", "", modelData.qsos); color: Theme.textSecondary; font.family: Theme.monoFamily; font.pixelSize: 11 }
            }
        }
        Text {
            visible: root.net.running && root.net.peers.length === 0
            text: qsTr("Nobody else on the network yet.")
            color: Theme.textSecondary
            font.pixelSize: 11
        }

        // ── Messaggi ────────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 60
            radius: 4
            color: Theme.bgDeep
            border.color: Theme.borderSoft
            ListView {
                id: gabList
                anchors.fill: parent
                anchors.margins: 4
                clip: true
                model: root.net.messages
                onCountChanged: gabScrollTimer.restart()
                ScrollBar.vertical: PanelScrollBar {}
                delegate: Text {
                    required property var modelData
                    width: gabList.width - 10
                    wrapMode: Text.Wrap
                    text: modelData.time + "  " + modelData.from + (modelData.to ? " → " + modelData.to : "") + ": " + modelData.text
                    color: modelData.mine ? Theme.secondaryColor : Theme.textPrimary
                    font.family: Theme.monoFamily
                    font.pixelSize: 11
                }
            }
        }
        // Se il pannello viene chiuso mentre arriva un messaggio, il Timer e'
        // distrutto insieme alla ListView e non rimane una funzione QML in
        // attesa su Qt.callLater.
        Timer {
            id: gabScrollTimer
            interval: 0
            onTriggered: gabList.positionViewAtEnd()
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            StyledTextField {
                id: gabInput
                Layout.fillWidth: true
                enabled: root.net.running
                mono: false
                placeholderText: qsTr("Message to the other operators")
                onAccepted: { root.net.sendGab(text); text = "" }
            }
            GlassButton {
                text: qsTr("Send")
                enabled: root.net.running && gabInput.text.trim().length > 0
                onClicked: { root.net.sendGab(gabInput.text); gabInput.text = "" }
            }
            // Lo spot interno: il nominativo della scheda sulla frequenza della radio.
            GlassButton {
                text: qsTr("Spot %1").arg(decolog.callInfo.call || "")
                tone: Theme.warningColor
                enabled: root.net.running && (decolog.callInfo.call || "").length > 0 && parseFloat(decolog.shownFrequency || "0") > 0
                onClicked: root.net.sendSpot(decolog.callInfo.call, parseFloat(decolog.shownFrequency) * 1000, "")
            }
        }
    }

    DialogFrame {
        id: handoffDialog
        transientParent: root.Window.window
        title: qsTr("Multiplier handoff")
        info: qsTr("Experimental station network")
        width: 740; height: 740
        property string result: ""
        function configure() { result = root.net.configureHandoffScope(handoffScope.text) }
        function sendOffer() {
            const value = handoffFrequency.text.trim()
            if (!/^[0-9]{1,13}$/.test(value)) { result = qsTr("Enter a frequency in whole hertz."); return }
            result = root.net.sendHandoffOffer(handoffRecipient.text, handoffCall.text, Number(value))
        }
        body: ColumnLayout {
            spacing: 7
            Text { id: handoffWarning; Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.warningColor
                text: qsTr("Station identities are not authenticated and delivery is not confirmed. Agree the same contest scope with the other station. This station ID: %1").arg(root.net.nodeId) }
            Text { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.textSecondary
                text: qsTr("Received offer status is local to this log and is not sent to the other station.") }
            RowLayout {
                Layout.fillWidth: true
                LabeledField { label: qsTr("Contest scope"); Layout.fillWidth: true
                    StyledTextField { id: handoffScope; Layout.fillWidth: true; placeholderText: qsTr("Same scope on every station") } }
                GlassButton { Layout.alignment: Qt.AlignBottom; text: qsTr("Apply"); onClicked: handoffDialog.configure() }
            }
            Text { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.textSecondary
                text: qsTr("Active scope: %1").arg(root.net.handoffScope || qsTr("none")) }
            RowLayout {
                Layout.fillWidth: true
                LabeledField { label: qsTr("Recipient station ID"); Layout.fillWidth: true
                    StyledTextField { id: handoffRecipient; Layout.fillWidth: true } }
                LabeledField { label: qsTr("Callsign"); Layout.fillWidth: true
                    StyledTextField { id: handoffCall; Layout.fillWidth: true; uppercase: true } }
                LabeledField { label: qsTr("Frequency (Hz)"); Layout.fillWidth: true
                    StyledTextField { id: handoffFrequency; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly; placeholderText: "14025000" } }
                GlassButton { Layout.alignment: Qt.AlignBottom; text: qsTr("Send offer"); enabled: root.net.running
                    onClicked: handoffDialog.sendOffer() }
            }
            Text { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.warningColor
                text: handoffDialog.result || root.net.handoffError || root.net.handoffStatus }
            Text { text: qsTr("Received offers"); color: Theme.textPrimary; font.bold: true }
            ListView {
                id: handoffInboxList; Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 100
                clip: true; model: root.net.handoffInbox; ScrollBar.vertical: PanelScrollBar {}
                delegate: RowLayout { required property var modelData; width: handoffInboxList.width
                    Text { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.textPrimary
                        text: qsTr("%1 · %2 Hz · from %3 · %4 UTC").arg(modelData.call).arg(modelData.frequencyHz).arg(modelData.sender).arg(modelData.createdUtc) + " · " +
                              (modelData.state === "pending" ? qsTr("Pending") : modelData.state === "accepted" ? qsTr("Accepted") : modelData.state === "declined" ? qsTr("Declined") : qsTr("Completed")) }
                    GlassButton { text: qsTr("Accept"); visible: modelData.state === "pending"; onClicked: handoffDialog.result = root.net.setHandoffState(modelData.id,"accepted") }
                    GlassButton { text: qsTr("Decline"); visible: modelData.state === "pending"; onClicked: handoffDialog.result = root.net.setHandoffState(modelData.id,"declined") }
                    GlassButton { text: qsTr("Complete"); visible: modelData.state === "accepted"; onClicked: handoffDialog.result = root.net.setHandoffState(modelData.id,"completed") }
                }
            }
            Text { text: qsTr("Sent offers — delivery unconfirmed"); color: Theme.textPrimary; font.bold: true }
            ListView {
                id: handoffOutboxList; Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 100
                clip: true; model: root.net.handoffOutbox; ScrollBar.vertical: PanelScrollBar {}
                delegate: RowLayout { required property var modelData; width: handoffOutboxList.width
                    Text { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.textPrimary
                        text: qsTr("%1 · %2 Hz · to %3 · %4 UTC").arg(modelData.call).arg(modelData.frequencyHz).arg(modelData.recipient).arg(modelData.createdUtc) }
                    GlassButton { text: qsTr("Retry"); enabled: root.net.running
                        onClicked: handoffDialog.result = root.net.retryHandoffOffer(modelData.id) }
                }
            }
            GlassButton { text: qsTr("Close"); onClicked: handoffDialog.close() }
        }
    }

    DialogFrame {
        id: serialAuthorityDialog
        transientParent: root.Window.window
        title: qsTr("Experimental serial authority")
        info: qsTr("Test only · configured for this session")
        width: 620; height: 510
        property string result: ""
        body: ColumnLayout {
            spacing: 10
            Text {
                Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.warningColor
                text: qsTr("Experimental UDP serials. Sender IDs are not authenticated; multiple authorities can issue the same number. A grant must be selected explicitly for each manual QSO.")
            }
            Text {
                Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.textSecondary
                text: qsTr("This station ID: %1. Select one authority for the contest. Configuration is cleared on restart; after reconfiguration a saved request resumes when the network is on. Leave both fields empty to disable.").arg(root.net.nodeId)
            }
            LabeledField {
                label: qsTr("Contest scope")
                StyledTextField { id: serialScope; Layout.fillWidth: true; placeholderText: qsTr("Same scope on every station") }
            }
            LabeledField {
                label: qsTr("Authority station ID")
                StyledTextField { id: serialAuthority; Layout.fillWidth: true; placeholderText: qsTr("Explicit authority ID") }
            }
            LabeledField {
                label: qsTr("Minimum serial")
                StyledTextField { id: serialMinimum; Layout.fillWidth: true; text: "1"; validator: IntValidator { bottom: 1 } }
            }
            Text { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.textSecondary; text: root.net.durableSerialStatus }
            Text { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.warningColor; text: serialAuthorityDialog.result }
            Item { Layout.fillHeight: true }
            RowLayout {
                GlassButton { text: qsTr("Apply"); onClicked: {
                    const error = root.net.configureDurableSerialAuthority(serialScope.text, serialAuthority.text)
                    serialAuthorityDialog.result = error || qsTr("Applied for this session.")
                } }
                GlassButton { text: qsTr("Request / retry"); onClicked: {
                    const error = root.net.requestDurableSerial(parseInt(serialMinimum.text) || 0)
                    serialAuthorityDialog.result = error || qsTr("Checking the saved request…")
                } }
                GlassButton { text: qsTr("Close"); onClicked: serialAuthorityDialog.close() }
            }
        }
    }

    DialogFrame {
        id:qsoWireDialog
        transientParent:root.Window.window
        title:qsTr("Experimental QSO transfer")
        width:700; height:730
        property string localError:""
        property bool keyReady:true
        readonly property bool keyConsistent:keyReady && ((wireKey.text.trim().length === 0) === !root.net.experimentalQsoSecure)
        onClosed: { root.net.clearExperimentalQso(); wireKey.text=""; keyReady=true }
        body:ColumnLayout {
            spacing:8
            Text { id:qsoWireWarning; Layout.fillWidth:true; wrapMode:Text.Wrap; color:Theme.warningColor
                text:root.net.experimentalQsoSecure
                    ? qsTr("Shared-key mode encrypts only this experimental transfer. Group and station IDs remain visible; anyone with the key can impersonate a station. Legacy QSO and other network messages remain unencrypted. Use a new random key and transfer ID, and share the key separately.")
                    : qsTr("Experimental UDP broadcast: QSO data is not encrypted and any listener on the network may read it. Station IDs are not authenticated. Only share data with trusted stations and networks.") }
            Text { Layout.fillWidth:true; wrapMode:Text.Wrap; color:Theme.textSecondary
                text:qsTr("Your station ID: %1. Both stations must enter the same transfer ID. The receiver arms first; the sender then sends one QSO. Received data is added only after review and confirmation.").arg(root.net.nodeId) }
            LabeledField { label:qsTr("Shared key (64 hexadecimal characters; optional)"); RowLayout { Layout.fillWidth:true
                StyledTextField { id:wireKey; Layout.fillWidth:true; echoMode:wireKeyShow.checked ? TextInput.Normal : TextInput.Password; onTextChanged:qsoWireDialog.keyReady=false }
                CheckBox { id:wireKeyShow; text:qsTr("Show") }
                GlassButton { text:qsTr("Generate"); onClicked:wireKey.text=root.net.generateExperimentalQsoKey() }
                GlassButton { text:qsTr("Use key"); onClicked: { const error=root.net.setExperimentalQsoKey(wireKey.text); qsoWireDialog.localError=error; qsoWireDialog.keyReady=!error } }
            } }
            Text { Layout.fillWidth:true; wrapMode:Text.Wrap; color:Theme.textSecondary
                text:qsTr("Set the same random key on both stations before arming or sending. Leave it empty and choose Use key for unencrypted mode. Keys are not saved and are removed from this session when this window closes.") }
            LabeledField { label:qsTr("Other station ID"); StyledTextField { id:wirePeer; Layout.fillWidth:true } }
            LabeledField { label:qsTr("Transfer ID"); RowLayout { Layout.fillWidth:true
                StyledTextField { id:wireTransfer; Layout.fillWidth:true }
                GlassButton { text:qsTr("Generate"); onClicked:wireTransfer.text=root.net.nodeId + "-" + Date.now().toString(36) }
            } }
            LabeledField { label:qsTr("QSO UUID to send"); StyledTextField { id:wireUuid; Layout.fillWidth:true } }
            RowLayout {
                GlassButton { text:qsTr("Arm receiver"); enabled:root.net.running && qsoWireDialog.keyConsistent; onClicked:qsoWireDialog.localError=root.net.armExperimentalQsoReceiver(wirePeer.text,wireTransfer.text) }
                GlassButton { text:qsTr("Send QSO"); enabled:root.net.running && qsoWireDialog.keyConsistent && !root.net.experimentalQsoSending; onClicked:qsoWireDialog.localError=root.net.sendExperimentalQso(wirePeer.text,wireUuid.text,wireTransfer.text) }
                GlassButton { text:qsTr("Request missing"); enabled:root.net.running && qsoWireDialog.keyConsistent; onClicked:qsoWireDialog.localError=root.net.requestExperimentalQsoMissing() }
            }
            Text { Layout.fillWidth:true; wrapMode:Text.Wrap; textFormat:Text.PlainText; color:Theme.textSecondary
                text:qsoWireDialog.localError || root.net.experimentalQsoStatus }
            GlassButton { text:qsTr("Review received QSO…"); visible:root.net.experimentalQsoPreview.complete === true
                enabled:!root.net.qsoReviewBusy; onClicked: { if (root.net.reviewExperimentalQso()) qsoDetailsDialog.open(); else qsoWireDialog.localError=qsTr("Cannot review the received QSO; check the current log.") } }
            Text { Layout.fillWidth:true; wrapMode:Text.Wrap; textFormat:Text.PlainText; color:Theme.textPrimary
                visible:root.net.experimentalQsoPreview.complete === true
                text:qsTr("Received %1 · UUID %2 · revision %3 · deleted %4").arg(root.net.experimentalQsoPreview.call || "—").arg(root.net.experimentalQsoPreview.uuid || "").arg(root.net.experimentalQsoPreview.revision).arg(root.net.experimentalQsoPreview.deleted ? qsTr("yes") : qsTr("no")) }
            Text { Layout.fillWidth:true; wrapMode:Text.Wrap; color:Theme.textSecondary
                text:qsTr("The receiver automatically requests the first or missing fragments every two seconds. Transfer state expires after five minutes of inactivity. If integrity verification fails, arm the receiver again. Closing this window clears the transfer.") }
            Item { Layout.fillHeight:true }
            GlassButton { text:qsTr("Close"); onClicked:qsoWireDialog.close() }
        }
    }

    FileDialog {
        id:qsoComparisonFile
        title:qsTr("Choose another DecoDXLog log")
        fileMode:FileDialog.OpenFile
        nameFilters:[qsTr("DecoDXLog logs (*.sqlite *.db)"),qsTr("All files (*)")]
        onAccepted: {
            qsoComparisonDialog.localError = root.net.startQsoComparison(selectedFile) ? "" : qsTr("Cannot start comparison; choose an existing log and reopen the current log if necessary.")
        }
    }

    DialogFrame {
        id:qsoComparisonDialog
        transientParent:root.Window.window
        title:qsTr("Compare station logs")
        info:qsTr("Snapshot comparison · imports require confirmation")
        width:880; height:580
        property string localError:""
        onOpened:localError=""
        function label(kind) {
            switch (kind) {
            case "localOnly": return qsTr("Only current log")
            case "remoteOnly": return qsTr("Only selected log")
            case "revisionDifference": return qsTr("Revision differs")
            case "contentConflict": return qsTr("Content differs")
            case "deletionConflict": return qsTr("Deletion differs")
            }
            return kind
        }
        onClosed:root.net.cancelQsoComparison()
        body:ColumnLayout {
            spacing:8
            Text { Layout.fillWidth:true; wrapMode:Text.Wrap; color:Theme.textSecondary
                text:qsTr("Compare the open log with a local copy from another station. The comparison does not contact the station. Missing QSO can be imported after inspection and confirmation. Matching covers ADIF fields only; attachments and local metadata are excluded.") }
            RowLayout {
                GlassButton { text:qsTr("Choose log…"); enabled:!root.net.qsoComparisonBusy; onClicked:qsoComparisonFile.open() }
                GlassButton { text:qsTr("Cancel comparison"); visible:root.net.qsoComparisonBusy; onClicked:root.net.cancelQsoComparison() }
                Text { Layout.fillWidth:true; elide:Text.ElideMiddle; textFormat:Text.PlainText; color:Theme.textSecondary; text:root.net.qsoComparisonSummary.remotePath || "" }
            }
            Text { Layout.fillWidth:true; wrapMode:Text.Wrap; textFormat:Text.PlainText; color:Theme.warningColor
                text:qsoComparisonDialog.localError || root.net.qsoComparisonSummary.error || (root.net.qsoComparisonBusy ? qsTr("Comparing snapshots…") : root.net.qsoComparisonSummary.stale ? qsTr("A log changed or was replaced; compare again.") : "") }
            Text { Layout.fillWidth:true; wrapMode:Text.Wrap; color:Theme.textPrimary
                visible:root.net.qsoComparisonSummary.differences !== undefined
                text:qsTr("Matching %1 · only current %2 · only selected %3 · revision %4 · content %5 · deletion %6")
                    .arg(root.net.qsoComparisonSummary.matching || 0).arg(root.net.qsoComparisonSummary.localOnly || 0).arg(root.net.qsoComparisonSummary.remoteOnly || 0)
                    .arg(root.net.qsoComparisonSummary.revisionDifference || 0).arg(root.net.qsoComparisonSummary.contentConflict || 0).arg(root.net.qsoComparisonSummary.deletionConflict || 0) }
            Text { Layout.fillWidth:true; color:Theme.warningColor; visible:root.net.qsoComparisonSummary.truncated === true
                text:qsTr("Showing the first 200 differences; counts include all records.") }
            Rectangle {
                Layout.fillWidth:true; Layout.fillHeight:true; Layout.minimumHeight:120; color:Theme.bgDeep; radius:4; border.color:Theme.borderSoft
                ListView {
                    id:qsoComparisonList
                    anchors.fill:parent; anchors.margins:6; clip:true; spacing:8; model:root.net.qsoComparisonRows
                    ScrollBar.vertical:PanelScrollBar {}
                    delegate:Column {
                        id:differenceRow
                        required property var modelData
                        width:ListView.view.width-12; spacing:3
                        function fieldTextAt(index) { const item=fieldRepeater.itemAt(index); return item ? item.text : "" }
                        Text { width:parent.width; wrapMode:Text.Wrap; textFormat:Text.PlainText; color:Theme.textPrimary; font.pixelSize:12
                            text:qsoComparisonDialog.label(parent.modelData.kind) + " · " + parent.modelData.uuid + "\n" +
                                 qsTr("Current: %1 %2 %3 %4 · rev %5").arg(parent.modelData.localCall || "—").arg(parent.modelData.localDate || "").arg(parent.modelData.localBand || "").arg(parent.modelData.localMode || "").arg(parent.modelData.localRevision === null || parent.modelData.localRevision === undefined ? "—" : parent.modelData.localRevision) + "\n" +
                                 qsTr("Selected: %1 %2 %3 %4 · rev %5").arg(parent.modelData.remoteCall || "—").arg(parent.modelData.remoteDate || "").arg(parent.modelData.remoteBand || "").arg(parent.modelData.remoteMode || "").arg(parent.modelData.remoteRevision === null || parent.modelData.remoteRevision === undefined ? "—" : parent.modelData.remoteRevision)
                        }
                        GlassButton {
                            text:qsTr("Inspect all fields…")
                            enabled:!root.net.qsoReviewBusy && !root.net.qsoComparisonSummary.stale
                            onClicked: {
                                if (root.net.reviewQsoComparison(differenceRow.modelData.uuid)) qsoDetailsDialog.open()
                                else qsoComparisonDialog.localError=qsTr("A log changed; compare again.")
                            }
                        }
                        Repeater {
                            id:fieldRepeater
                            model:parent.modelData.fields || []
                            delegate:Text {
                                required property var modelData
                                width:differenceRow.width; wrapMode:Text.WrapAnywhere; textFormat:Text.PlainText; color:Theme.textSecondary; font.pixelSize:11
                                text:qsTr("%1: current %2 · selected %3").arg(modelData.name + (modelData.nameTruncated ? "…" : ""))
                                    .arg(modelData.localPresent ? modelData.local + (modelData.localTruncated ? "…" : "") : qsTr("not present"))
                                    .arg(modelData.remotePresent ? modelData.remote + (modelData.remoteTruncated ? "…" : "") : qsTr("not present"))
                            }
                        }
                        Text { width:parent.width; visible:(parent.modelData.fieldsOmitted || 0)>0; color:Theme.warningColor; font.pixelSize:11
                            text:qsTr("%1 additional fields omitted").arg(parent.modelData.fieldsOmitted || 0) }
                    }
                }
            }
            GlassButton { text:qsTr("Close"); onClicked:qsoComparisonDialog.close() }
        }
    }
    DialogFrame {
        id:qsoDetailsDialog
        transientParent:root.Window.window
        title:qsTr("Inspect QSO fields")
        info:qsTr("Read-only snapshot · no changes applied")
        width:820; height:600
        body:ColumnLayout {
            spacing:8
            Text { Layout.fillWidth:true; color:Theme.textSecondary; wrapMode:Text.Wrap
                text:qsTr("Fields are shown up to 2048 characters. Long values are marked; attachments and local metadata are not shown.") }
            Text { Layout.fillWidth:true; wrapMode:Text.Wrap; textFormat:Text.PlainText; color:Theme.textSecondary; visible:root.net.qsoReview.fromNetwork === true
                text:qsTr("Sender %1 · source log %2").arg(root.net.qsoReview.sender || "—").arg(root.net.qsoReview.sourceLog || "—") }
            Text { id:networkReviewTrust; Layout.fillWidth:true; wrapMode:Text.Wrap; color:Theme.warningColor; visible:root.net.qsoReview.fromNetwork === true
                text:root.net.qsoReview.transportSecure ? qsTr("Received over the network with a shared key. Any key holder can impersonate a station; verify the source before applying.") : qsTr("Received over the network without authentication. The sender can be impersonated; verify the source before applying.") }
            Text { id:networkReviewStale; Layout.fillWidth:true; wrapMode:Text.Wrap; color:Theme.warningColor; visible:root.net.qsoReview.fromNetwork === true
                text:qsTr("The received snapshot may no longer match the sender's current log.") }
            Text { Layout.fillWidth:true; wrapMode:Text.Wrap; color:Theme.warningColor; visible:root.net.qsoReview.hasLongFields === true
                text:qsTr("Open and read every page of each long field before importing or applying. Remaining: %1").arg(root.net.qsoReview.longRemaining || 0) }
            Text { Layout.fillWidth:true; wrapMode:Text.Wrap; textFormat:Text.PlainText; color:Theme.warningColor
                text:root.net.qsoReviewBusy ? qsTr("Reading QSO snapshots…") : root.net.qsoReview.error || ((!root.net.qsoReview.fromNetwork && root.net.qsoComparisonSummary.stale) ? qsTr("A log changed; compare again.") : "") }
            Rectangle {
                Layout.fillWidth:true; Layout.fillHeight:true; color:Theme.bgDeep; radius:4; border.color:Theme.borderSoft
                ListView {
                    anchors.fill:parent; anchors.margins:6; clip:true; spacing:8; model:root.net.qsoReview.fields || []
                    ScrollBar.vertical:PanelScrollBar {}
                    delegate:Column {
                        id:fullFieldRow
                        required property var modelData
                        required property int index
                        width:ListView.view.width-12; spacing:4
                        function preview(value) { const t=value || ""; return t.length>2048 ? t.slice(0,2048)+"…" : t }
                        Text { width:parent.width; color:Theme.textPrimary; wrapMode:Text.WrapAnywhere; textFormat:Text.PlainText
                            text:qsTr("%1: current %2 · selected %3").arg(fullFieldRow.preview(fullFieldRow.modelData.name))
                                .arg(fullFieldRow.modelData.localPresent ? fullFieldRow.preview(fullFieldRow.modelData.local) : qsTr("not present"))
                                .arg(fullFieldRow.modelData.remotePresent ? fullFieldRow.preview(fullFieldRow.modelData.remote) : qsTr("not present")) }
                        Row {
                            spacing:6
                            GlassButton { visible:(fullFieldRow.modelData.name || "").length>2048; text:qsTr("Read field name…"); onClicked:qsoLongDialog.begin(fullFieldRow.index,"name") }
                            GlassButton { visible:(fullFieldRow.modelData.local || "").length>2048; text:qsTr("Read current value…"); onClicked:qsoLongDialog.begin(fullFieldRow.index,"local") }
                            GlassButton { visible:(fullFieldRow.modelData.remote || "").length>2048; text:qsTr("Read selected value…"); onClicked:qsoLongDialog.begin(fullFieldRow.index,"remote") }
                        }
                    }
                }
            }
            RowLayout {
                GlassButton { text:qsTr("Import this missing QSO…"); visible:root.net.qsoReview.canInsert === true
                    enabled:!root.net.qsoReviewBusy && (root.net.qsoReview.fromNetwork || !root.net.qsoComparisonSummary.stale); onClicked:qsoImportDialog.open() }
                GlassButton { text:root.net.qsoReview.remoteDeleted ? qsTr("Review deletion…") : qsTr("Review selected changes…")
                    visible:root.net.qsoReview.canApply === true; enabled:!root.net.qsoReviewBusy && (root.net.qsoReview.fromNetwork || !root.net.qsoComparisonSummary.stale)
                    onClicked:qsoApplyDialog.open() }
                GlassButton { text:qsTr("Close"); onClicked:qsoDetailsDialog.close() }
            }
        }
    }
    DialogFrame {
        id:qsoLongDialog
        transientParent:root.Window.window
        title:qsTr("Read long field")
        width:760; height:520
        property int fieldIndex:-1
        property string fieldPart:""
        property int nextOffset:0
        property int total:0
        property bool pageDone:false
        property string pageText:""
        function begin(index,part) { fieldIndex=index; fieldPart=part; nextOffset=0; pageDone=false; const ok=nextPage(); if (ok) open(); return ok }
        function nextPage() {
            const page=root.net.qsoLongFieldPage(fieldIndex,fieldPart,nextOffset)
            if (page.next === undefined) { pageText=qsTr("A log changed; compare again."); return false }
            pageText=page.text; nextOffset=page.next; total=page.total; pageDone=page.done; return true
        }
        body:ColumnLayout {
            spacing:8
            Text { Layout.fillWidth:true; color:Theme.textSecondary; text:qsTr("Read through position %1 of %2").arg(qsoLongDialog.nextOffset).arg(qsoLongDialog.total) }
            ScrollView {
                Layout.fillWidth:true; Layout.fillHeight:true; clip:true
                TextArea { readOnly:true; selectByMouse:true; wrapMode:TextEdit.WrapAnywhere; textFormat:TextEdit.PlainText; text:qsoLongDialog.pageText }
            }
            RowLayout {
                GlassButton { text:qsTr("Restart"); onClicked:qsoLongDialog.begin(qsoLongDialog.fieldIndex,qsoLongDialog.fieldPart) }
                GlassButton { text:qsTr("Next page"); enabled:!qsoLongDialog.pageDone; onClicked:qsoLongDialog.nextPage() }
                GlassButton { text:qsTr("Close"); onClicked:qsoLongDialog.close() }
            }
        }
    }
    DialogFrame {
        id:qsoImportDialog
        transientParent:root.Window.window
        title:qsTr("Import reviewed QSO?")
        width:620; height:300
        function confirmImport() {
            const network=root.net.qsoReview.fromNetwork === true
            const error=root.net.applyReviewedMissingQso()
            if (network) qsoWireDialog.localError=error || qsTr("Received QSO imported.")
            else qsoComparisonDialog.localError=error || qsTr("QSO imported. Compare the logs again.")
            if (!error) qsoDetailsDialog.close()
            close()
        }
        body:ColumnLayout {
            spacing:12
            Text { Layout.fillWidth:true; wrapMode:Text.Wrap; color:Theme.textPrimary
                text:qsTr("Import this QSO into the current log? Its ADIF fields and QSL states will be copied. Attachments, local service metadata and station profile are not copied. The QSO may be included in the normal Cloud sync.") }
            Text { Layout.fillWidth:true; wrapMode:Text.Wrap; color:Theme.warningColor
                text:root.net.qsoReview.fromNetwork ? qsTr("The current log will be checked again. Existing or duplicate QSO are not overwritten.") : qsTr("The current log and selected log will be checked again. Existing or duplicate QSO are not overwritten.") }
            Item { Layout.fillHeight:true }
            RowLayout {
                GlassButton { text:qsTr("Cancel"); onClicked:qsoImportDialog.close() }
                GlassButton { text:qsTr("Import QSO"); onClicked:qsoImportDialog.confirmImport() }
            }
        }
    }

    DialogFrame {
        id:qsoApplyDialog
        transientParent:root.Window.window
        title:root.net.qsoReview.remoteDeleted ? qsTr("Delete current QSO?") : qsTr("Apply selected QSO fields?")
        width:680; height:340
        function confirmApply() {
            const network=root.net.qsoReview.fromNetwork === true
            const error=root.net.applyReviewedExistingQso()
            if (network) qsoWireDialog.localError=error || qsTr("Received change applied.")
            else qsoComparisonDialog.localError=error || qsTr("Change applied. Compare the logs again.")
            if (!error) qsoDetailsDialog.close()
            close()
        }
        body:ColumnLayout {
            spacing:12
            Text { Layout.fillWidth:true; wrapMode:Text.Wrap; color:Theme.warningColor
                text:root.net.qsoReview.remoteDeleted
                    ? qsTr("Delete the current QSO? It remains in history, and configured services may receive a deletion.")
                    : root.net.qsoReview.fromNetwork ? qsTr("Replace the current QSO's ADIF fields and QSL states with the received values? Existing station profile and attachments remain. Configured services may receive the change.") : qsTr("Replace the current QSO's ADIF fields and QSL states with the selected log's values? Existing station profile and attachments remain. Configured services may receive the change.") }
            Text { Layout.fillWidth:true; wrapMode:Text.Wrap; color:Theme.textSecondary
                text:qsTr("The QSO will be checked again. This is an explicit choice, not an automatic merge.") }
            Item { Layout.fillHeight:true }
            RowLayout {
                GlassButton { text:qsTr("Cancel"); onClicked:qsoApplyDialog.close() }
                GlassButton { text:root.net.qsoReview.remoteDeleted ? qsTr("Delete QSO") : qsTr("Apply changes"); onClicked:qsoApplyDialog.confirmApply() }
            }
        }
    }

}
