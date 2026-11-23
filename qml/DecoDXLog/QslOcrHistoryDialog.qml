import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decodium.UI

DialogFrame {
    id: root
    title: qsTr("OCR acquisition history")
    dialogKey: "qsl-ocr-history"
    width: 780
    height: 650
    dotColor: Theme.secondaryColor
    info: qsTr("Review every affected QSO")
    readonly property var acquisition: decolog.qslOcrAcquisition
    property var qsoId: 0
    property var historyAnchors: [0]
    property var rows: []
    property bool hasOlder: false
    property int selectedIndex: -1
    property bool ownsWork: false
    property bool probing: false
    property bool probingReview: false
    readonly property var selected: selectedIndex >= 0 && selectedIndex < rows.length ? rows[selectedIndex] : ({})
    signal historyChanged()
    function refresh() { ownsWork = acquisition.review(qsoId, historyAnchors[historyAnchors.length - 1]) }
    onSelectedChanged: reviewText.text = selected.reviewId ? selected.reviewedText : (!selected.textTruncated ? (selected.text || "") : "")
    function openFor(id, probe, reviewProbe) {
        qsoId = id; historyAnchors = [0]; rows = []; selectedIndex = -1; probing = !!probe; probingReview=!!reviewProbe
        open(); refresh()
    }
    onClosed: if (root.ownsWork) root.acquisition.cancel()
    Connections {
        target: root.acquisition
        function onFinished(success) {
            if (!root.ownsWork) return
            root.ownsWork = false
            const operation = root.acquisition.result.operation
            if (operation === "review" && success) {
                const found = root.acquisition.history
                root.hasOlder = found.length > 50
                root.rows = found.slice(0, 50)
                root.selectedIndex = root.rows.length ? 0 : -1
                if (root.probing && root.rows.length) confirmUndo.open()
                if (root.probingReview && root.rows.length && !root.selected.reviewId) { reviewText.text="Operator corrected K1ABC"; saveReviewButton.clicked() }
                else if (root.probingReview && root.rows.length && root.selected.reviewId) { console.warn("PROBE qslReview restored " + root.selected.reviewedText); root.probingReview=false }
            } else if (operation === "undo") {
                if (root.probing) console.warn("PROBE qslUndo success=" + success + " removed=" + root.acquisition.result.removedLinks + " error=" + root.acquisition.error)
                if (success) { root.historyChanged(); root.probing = false; root.refresh() }
            } else if (operation === "saveReview") {
                if (root.probingReview) console.warn("PROBE qslReview saved=" + success + " error=" + root.acquisition.error)
                if (success) root.refresh()
            }
        }
    }
    Popup {
        id: confirmUndo
        anchors.centerIn: Overlay.overlay
        modal: true
        padding: 16
        background: Rectangle { color: Theme.panelColor; border.color: Theme.warningColor; radius: 6 }
        onOpened: if (root.probing) Qt.callLater(() => confirmButton.clicked())
        ColumnLayout {
            width: 460
            Text {
                Layout.fillWidth: true
                text: qsTr("Undo links created by this acquisition for up to %1 QSO? Existing or later-used links are protected. The image and audit record remain.").arg((root.selected.targets || []).length)
                wrapMode: Text.Wrap
                color: Theme.textPrimary
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                GlassButton { text: qsTr("Cancel"); onClicked: confirmUndo.close() }
                GlassButton {
                    id: confirmButton
                    text: qsTr("Undo acquisition links")
                    onClicked: {
                        confirmUndo.close()
                        root.ownsWork = root.acquisition.undo(Number(root.selected.id), String(root.selected.uuid))
                    }
                }
            }
        }
    }
    body: ColumnLayout {
        anchors.margins: 14
        spacing: 8
        Text { Layout.fillWidth: true; text: qsTr("An acquisition can involve several QSO. Review every target before undoing it."); color: Theme.warningColor; wrapMode: Text.Wrap }
        RowLayout {
            Layout.fillWidth: true
            GlassButton { text: qsTr("Refresh"); enabled: !root.acquisition.busy; onClicked: root.refresh() }
            GlassButton { text: qsTr("Newer"); enabled: !root.acquisition.busy && root.historyAnchors.length > 1; onClicked: { root.historyAnchors = root.historyAnchors.slice(0, -1); root.refresh() } }
            GlassButton { text: qsTr("Older"); enabled: !root.acquisition.busy && root.hasOlder && root.rows.length > 0; onClicked: { root.historyAnchors = root.historyAnchors.concat([root.rows[root.rows.length - 1].id]); root.refresh() } }
            Text { Layout.fillWidth: true; visible: root.acquisition.busy; text: qsTr("Working…"); color: Theme.textSecondary }
        }
        Text { Layout.fillWidth: true; visible: root.acquisition.error.length > 0; text: root.acquisition.error; color: Theme.warningColor; wrapMode: Text.Wrap }
        ListView {
            id: historyList
            Layout.fillWidth: true
            Layout.preferredHeight: 145
            clip: true
            model: root.rows
            ScrollBar.vertical: ScrollBar { }
            delegate: ItemDelegate {
                required property int index
                required property var modelData
                width: ListView.view.width
                highlighted: root.selectedIndex === index
                onClicked: root.selectedIndex = index
                background: Rectangle {
                    radius: 4
                    color: root.selectedIndex === index ? Theme.glassOverlay : "transparent"
                    border.width: root.selectedIndex === index ? 1 : 0
                    border.color: Theme.secondaryColor
                }
                contentItem: Text {
                    text: String(modelData.createdAt) + " · " + modelData.sourceName + (modelData.undoneAt ? " · " + qsTr("Undone") : "")
                    textFormat: Text.PlainText
                    color: Theme.textPrimary
                    elide: Text.ElideRight
                }
            }
        }
        Text { Layout.fillWidth: true; visible: !root.acquisition.busy && root.rows.length === 0; text: qsTr("No OCR acquisitions for this QSO."); color: Theme.textSecondary }
        Text { Layout.fillWidth: true; visible: !!root.selected.id; text: qsTr("Image SHA-256: %1").arg(root.selected.sha256 || ""); color: Theme.textSecondary; textFormat: Text.PlainText; wrapMode: Text.Wrap }
        Text { Layout.fillWidth: true; visible: !!root.selected.id; text: qsTr("Affected QSO (new links are marked +):"); color: Theme.textPrimary; font.bold: true }
        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: 100
            clip: true
            model: root.selected.targets || []
            ScrollBar.vertical: ScrollBar { }
            delegate: Text {
                required property var modelData
                width: ListView.view.width
                text: (modelData.created ? "+ " : "= ") + (modelData.call || "?") + " #" + modelData.id + (modelData.created && !modelData.unchanged ? " · " + qsTr("changed or unavailable") : "")
                textFormat: Text.PlainText
                color: modelData.created && !modelData.unchanged ? Theme.warningColor : Theme.textSecondary
                elide: Text.ElideRight
            }
        }
        Text { text: qsTr("Recognized text (first 4096 characters)"); color: Theme.textPrimary; font.bold: true }
        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: 70
            clip: true
            TextArea { readOnly: true; text: root.selected.text || ""; textFormat: TextEdit.PlainText; color: Theme.textPrimary; background: null; wrapMode: TextArea.Wrap; selectByMouse: true }
        }
        Text { Layout.fillWidth:true; text:qsTr("Operator-reviewed transcription (optional; previous revisions are retained)"); color:Theme.textPrimary; font.bold:true; wrapMode:Text.Wrap }
        Text { Layout.fillWidth:true; visible:!!root.selected.textTruncated && !root.selected.reviewId; text:qsTr("The OCR preview is truncated; enter the reviewed text explicitly."); color:Theme.warningColor; wrapMode:Text.Wrap }
        ScrollView {
            Layout.fillWidth:true; Layout.fillHeight:true; clip:true
            TextArea { id:reviewText; textFormat:TextEdit.PlainText; color:Theme.textPrimary; background:null; wrapMode:TextArea.Wrap; selectByMouse:true; enabled:!!root.selected.id && !root.selected.undoneAt && !root.acquisition.busy }
        }
        RowLayout {
            Layout.fillWidth:true
            GlassButton { id:saveReviewButton; text:qsTr("Save reviewed text"); enabled:!!root.selected.id && !root.selected.undoneAt && !root.acquisition.busy && reviewText.text.trim().length>0 && reviewText.text.length<=65536
                onClicked:root.ownsWork=root.acquisition.saveReview(Number(root.selected.id),String(root.selected.uuid),Number(root.selected.reviewId || 0),reviewText.text) }
            Text { Layout.fillWidth:true; color:Theme.textSecondary; text:root.selected.reviewedAt ? qsTr("Latest review: %1").arg(root.selected.reviewedAt) : qsTr("No reviewed transcription saved"); wrapMode:Text.Wrap }
        }
        Text { Layout.fillWidth: true; visible: !!root.selected.id && !root.selected.undoneAt && !root.selected.guarded; text: qsTr("Undo is unavailable because a link is legacy, changed or missing. Review manually."); color: Theme.warningColor; wrapMode: Text.Wrap }
        RowLayout {
            Layout.fillWidth: true
            GlassButton { text: qsTr("Undo acquisition…"); enabled: !!root.selected.id && !root.selected.undoneAt && !!root.selected.guarded && !root.acquisition.busy; onClicked: confirmUndo.open() }
            Text { Layout.fillWidth: true; visible: !!root.selected.undoneAt; text: qsTr("Undone at %1").arg(root.selected.undoneAt || ""); color: Theme.textSecondary; wrapMode: Text.Wrap }
        }
    }
}
