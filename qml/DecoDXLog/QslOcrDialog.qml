import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Decodium.UI

DialogFrame {
    id: root
    title: qsTr("Read QSL image locally")
    dialogKey: "qsl-ocr"
    width: 760
    height: 650
    dotColor: Theme.secondaryColor
    info: qsTr("Review every result")
    readonly property var ocr: decolog.qslOcr
    readonly property var search: decolog.qslOcrSearch
    readonly property var acquisition: decolog.qslOcrAcquisition
    signal acquired()
    property url selectedImage: ""
    property url processedImage: ""
    property bool probing: false
    property bool probeMatches: false
    property bool probeAcquire: false
    property string searchedCall: ""
    property bool searchDone: false
    property var selectedQsos: []
    property bool saved: false
    readonly property bool showingResult: String(processedImage).length > 0 && String(selectedImage) === String(processedImage)
    onClosed: { if (root.ocr.busy) root.ocr.cancel(); if (root.search.busy) root.search.cancel(); if (root.acquisition.busy) root.acquisition.cancel() }
    function toggleQso(row, enabled) {
        const remaining = root.selectedQsos.filter(x => x.uuid !== row.uuid)
        root.selectedQsos = enabled ? remaining.concat([row]) : remaining
        root.saved = false
    }
    function probe(url, match, acquire) {
        probing = true
        probeMatches = !!match
        probeAcquire = !!acquire
        selectedImage = url
        scan.clicked()
    }
    Connections {
        target: root.ocr
        function onFinished(success) {
            if (root.probing)
                console.warn("PROBE qslOcr success=" + success + " hasCall=" + root.ocr.text.includes("K1ABC")
                             + " count=" + decolog.currentLogInfo().qsos + " words=" + root.ocr.words.length)
            if (root.probing && root.probeMatches && success) {
                const rows = root.ocr.suggestions.calls || []
                const index = rows.findIndex(x => x.value === "K1ABC")
                if (index >= 0) { callChoice.currentIndex = index; searchButton.clicked() }
            }
        }
    }
    Connections {
        target: root.search
        function onFinished(success) {
            root.searchDone = success
            if (root.probing && root.probeMatches)
                console.warn("PROBE qslMatches success=" + success + " count=" + root.search.matches.length
                             + " first=" + (root.search.matches.length ? root.search.matches[0].call : "")
                             + " noMatchNotice=" + noMatchNotice.visible)
            if (root.probing && root.probeAcquire && success) {
                for (const row of root.search.matches) root.toggleQso(row, true)
                approveButton.clicked()
            }
        }
    }
    Connections {
        target: root.acquisition
        function onFinished(success) {
            if (root.acquisition.result.operation !== "attach") return
            root.saved = success
            if (success) root.acquired()
            if (root.probing && root.probeAcquire)
                console.warn("PROBE qslAcquire success=" + success + " selected=" + root.acquisition.result.selected
                             + " newLinks=" + root.acquisition.result.newLinks + " error=" + root.acquisition.error)
        }
    }
    FileDialog {
        id: picker
        title: qsTr("Select a local QSL image")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.tif *.tiff *.bmp *.webp)"), qsTr("All files (*)")]
        onAccepted: { root.selectedImage = selectedFile; root.processedImage = ""; root.searchedCall = ""; root.searchDone = false; root.selectedQsos = []; root.saved = false }
    }
    body: ScrollView {
        id: dialogScroll
        anchors.margins: 14
        clip: true
        contentWidth: availableWidth
        ColumnLayout {
        width: dialogScroll.availableWidth
        spacing: 8
        Text {
            Layout.fillWidth: true
            text: qsTr("Local OCR is a suggestion only. Verify calls, dates, times and every other field against the image. It does not change the QSO or confirmation status.")
            color: Theme.textSecondary
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            GlassButton { text: qsTr("Choose image…"); enabled: !root.ocr.busy && !root.acquisition.busy; onClicked: picker.open() }
            Text { Layout.fillWidth: true; text: String(root.selectedImage) || qsTr("No image selected"); textFormat: Text.PlainText; color: Theme.textPrimary; elide: Text.ElideMiddle }
            GlassButton { id: scan; text: qsTr("Read image"); enabled: !root.ocr.busy && !root.search.busy && !root.acquisition.busy && String(root.selectedImage).length > 0; onClicked: { root.selectedQsos = []; root.saved = false; root.searchedCall = ""; root.searchDone = false; root.processedImage = root.selectedImage; root.ocr.start(root.selectedImage) } }
            GlassButton { text: qsTr("Cancel"); enabled: root.ocr.busy; onClicked: root.ocr.cancel() }
        }
        Text {
            Layout.fillWidth: true
            text: root.ocr.executable.length ? qsTr("Engine: %1 · English model").arg(root.ocr.executable) : qsTr("Tesseract OCR is not installed or not on the search path.")
            color: Theme.textSecondary
            wrapMode: Text.Wrap
        }
        Text { Layout.fillWidth: true; visible: root.ocr.busy; text: qsTr("Reading locally…"); color: Theme.secondaryColor }
        Text {
            Layout.fillWidth: true
            visible: root.showingResult && root.ocr.snapshotSha256.length > 0
            text: qsTr("Temporary image copy · SHA-256 %1…").arg(root.ocr.snapshotSha256.substring(0, 12))
            color: Theme.textSecondary
            wrapMode: Text.Wrap
        }
        Text { Layout.fillWidth: true; visible: root.showingResult && root.ocr.error.length > 0; text: root.ocr.error; color: Theme.warningColor; wrapMode: Text.Wrap }
        Text { text: qsTr("Recognized text"); color: Theme.textPrimary; font.bold: true }
        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: 120
            clip: true
            TextArea {
                readOnly: true
                wrapMode: TextArea.Wrap
                text: root.showingResult ? root.ocr.text : ""
                textFormat: TextEdit.PlainText
                color: Theme.textPrimary
                background: null
                selectByMouse: true
            }
        }
        Text { Layout.fillWidth: true; text: qsTr("Unverified suggestions — select a callsign to search the current log."); color: Theme.warningColor; wrapMode: Text.Wrap }
        Text { Layout.fillWidth: true; visible: root.showingResult && !!root.ocr.suggestions.truncated; text: qsTr("Some suggestions are omitted; review the complete OCR text."); color: Theme.warningColor; wrapMode: Text.Wrap }
        RowLayout {
            Layout.fillWidth: true
            StyledComboBox {
                id: callChoice
                Layout.fillWidth: true
                Accessible.name: qsTr("Suggested callsigns")
                enabled: !root.search.busy && !root.acquisition.busy
                readonly property var rows: root.showingResult ? (root.ocr.suggestions.calls || []) : []
                model: rows.map(x => x.value + (x.uncertain ? " ?" : ""))
            }
            GlassButton {
                id: searchButton
                text: qsTr("Find exact callsign")
                enabled: !root.ocr.busy && !root.search.busy && !root.acquisition.busy && callChoice.currentIndex >= 0 && callChoice.rows.length > 0
                onClicked: { root.searchDone = false; root.searchedCall = callChoice.rows[callChoice.currentIndex].value; root.search.start(root.searchedCall) }
            }
            GlassButton { text: qsTr("Cancel search"); enabled: root.search.busy; onClicked: root.search.cancel() }
        }
        RowLayout {
            Layout.fillWidth: true
            Repeater {
                model: [qsTr("Date"), qsTr("Time"), qsTr("Band"), qsTr("Mode")]
                StyledComboBox {
                    required property int index
                    required property string modelData
                    Layout.fillWidth: true
                    Accessible.name: modelData
                    readonly property var values: root.showingResult ? (root.ocr.suggestions[["dates", "times", "bands", "modes"][index]] || []) : []
                    model: [modelData + " (" + values.length + "): —"].concat(values.map(x => x.value + (x.ambiguous ? " ? (" + x.raw + ")" : x.uncertain ? " ?" : "")))
                }
            }
        }
        Text { Layout.fillWidth: true; visible: root.search.error.length > 0; text: root.search.error; color: Theme.warningColor; wrapMode: Text.Wrap }
        Text { Layout.fillWidth: true; visible: root.search.truncated; text: qsTr("Showing the first 100 matches; refine the search separately."); color: Theme.warningColor; wrapMode: Text.Wrap }
        Text {
            id: noMatchNotice
            Layout.fillWidth: true
            visible: root.searchDone && !root.search.busy && root.search.matches.length === 0 && callChoice.currentIndex >= 0 && callChoice.rows[callChoice.currentIndex].value === root.searchedCall
            text: qsTr("No exact callsign matches in the current log.")
            color: Theme.textSecondary
            wrapMode: Text.Wrap
        }
        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: 85
            clip: true
            model: root.showingResult && callChoice.currentIndex >= 0 && callChoice.rows[callChoice.currentIndex].value === root.searchedCall ? root.search.matches : []
            ScrollBar.vertical: ScrollBar { }
            delegate: RowLayout {
                required property var modelData
                width: ListView.view.width
                CheckBox {
                    Accessible.name: qsTr("Select QSO %1 for attachment").arg(modelData.call)
                    checked: root.selectedQsos.some(x => x.uuid === modelData.uuid)
                    enabled: !root.acquisition.busy
                    onClicked: root.toggleQso(modelData, checked)
                }
                Text {
                    Layout.fillWidth: true
                    text: "#" + modelData.id + " · " + modelData.call + " · " + modelData.at + " · " + modelData.band + " · " + modelData.mode
                    textFormat: Text.PlainText
                    color: Theme.textPrimary
                    elide: Text.ElideRight
                }
            }
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Selected for attachment: %1").arg(root.selectedQsos.map(x => x.call + " #" + x.id).join(", ") || "—")
            textFormat: Text.PlainText
            color: Theme.textSecondary
            elide: Text.ElideRight
        }
        Text { Layout.fillWidth: true; text: qsTr("Approval stores the OCR image copy and recognized text with the selected QSO; it does not mark them confirmed."); color: Theme.warningColor; wrapMode: Text.Wrap }
        RowLayout {
            Layout.fillWidth: true
            GlassButton {
                id: approveButton
                text: qsTr("Approve and attach copy")
                enabled: root.showingResult && root.ocr.snapshotSha256.length > 0 && !root.ocr.busy && !root.search.busy && !root.acquisition.busy && root.selectedQsos.length > 0 && root.selectedQsos.length <= 100 && !root.saved
                onClicked: root.acquisition.start(root.selectedQsos, root.selectedImage)
            }
            GlassButton { text: qsTr("Cancel save"); enabled: root.acquisition.busy; onClicked: root.acquisition.cancel() }
            Text { Layout.fillWidth: true; visible: root.saved; text: qsTr("Saved: %1 QSO, %2 new links").arg(root.acquisition.result.selected).arg(root.acquisition.result.newLinks); color: Theme.textSecondary; wrapMode: Text.Wrap }
        }
        Text { Layout.fillWidth: true; visible: root.acquisition.error.length > 0; text: root.acquisition.error; color: Theme.warningColor; wrapMode: Text.Wrap }
        Text {
            Layout.fillWidth: true
            text: qsTr("Low-confidence words (below 75/100; not a probability)")
            color: Theme.textSecondary
            wrapMode: Text.Wrap
        }
        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: 95
            clip: true
            model: root.showingResult ? root.ocr.words.filter(w => w.uncertain) : []
            ScrollBar.vertical: ScrollBar { }
            delegate: Text {
                required property var modelData
                width: ListView.view.width
                text: modelData.text + " · " + (modelData.confidence < 0 ? "?" : Number(modelData.confidence).toFixed(0))
                textFormat: Text.PlainText
                color: Theme.warningColor
                elide: Text.ElideRight
            }
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Use Import scan or image in the QSO detail to attach a different file separately.")
            color: Theme.textSecondary
            wrapMode: Text.Wrap
        }
    }
    }
}
