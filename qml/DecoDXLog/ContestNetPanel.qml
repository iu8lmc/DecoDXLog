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
    function probeComparison(url) { qsoComparisonDialog.open(); return root.net.startQsoComparison(url) }
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
            GlassButton { text:qsTr("Compare logs…"); onClicked:qsoComparisonDialog.open() }
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
        info:qsTr("Read-only snapshot · no changes applied")
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
                text:qsTr("Compare the open log with a local copy from another station. This does not contact the station, merge records or prove the logs remain unchanged. Matching covers ADIF fields only; attachments and local metadata are excluded.") }
            RowLayout {
                GlassButton { text:qsTr("Choose log…"); enabled:!root.net.qsoComparisonBusy; onClicked:qsoComparisonFile.open() }
                GlassButton { text:qsTr("Cancel comparison"); visible:root.net.qsoComparisonBusy; onClicked:root.net.cancelQsoComparison() }
                Text { Layout.fillWidth:true; elide:Text.ElideMiddle; textFormat:Text.PlainText; color:Theme.textSecondary; text:root.net.qsoComparisonSummary.remotePath || "" }
            }
            Text { Layout.fillWidth:true; wrapMode:Text.Wrap; textFormat:Text.PlainText; color:Theme.warningColor
                text:qsoComparisonDialog.localError || root.net.qsoComparisonSummary.error || (root.net.qsoComparisonBusy ? qsTr("Comparing snapshots…") : root.net.qsoComparisonSummary.stale ? qsTr("The open log changed or a file was replaced; compare again.") : "") }
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
}
