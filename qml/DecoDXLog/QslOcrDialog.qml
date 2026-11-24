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
    property bool probeFiltered: false
    property bool probeSimilar: false
    property bool probeSelection: false
    property bool probeSecondSearch: false
    property string searchedCall: ""
    property string searchedCriteria: ""
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
    function probe(url, match, acquire, filtered, similar, selection) {
        probing = true
        probeMatches = !!match
        probeAcquire = !!acquire
        probeFiltered = !!filtered
        probeSimilar = !!similar
        probeSelection = !!selection
        probeSecondSearch = false
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
                if (index >= 0) {
                    callChoice.currentIndex = index
                    if (root.probeFiltered) {
                        manualCall.text="K1ABC"
                        const date=criteria.itemAt(0), time=criteria.itemAt(1)
                        date.currentIndex=date.values.findIndex(x => x.value === "2026-09-23")+1
                        time.currentIndex=time.values.findIndex(x => x.value === "14:30")+1
                        console.warn("PROBE qslFilters " + date.chosenValue() + " " + time.chosenValue())
                    }
                    similarCall.checked = root.probeSimilar
                    searchButton.clicked()
                }
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
            if (root.probing && root.probeMatches && success && root.search.matches.length)
                Qt.callLater(function() { const row=ocrCandidates.itemAtIndex(0); console.warn("PROBE qslCandidate " + (row ? row.probeText : "missing")) })
            if (root.probing && root.probeSelection && success) {
                for (const row of root.search.matches) root.toggleQso(row, true)
                selectionProbeTimer.start()
            }
            if (root.probing && root.probeFiltered && success) {
                console.warn("PROBE qslFilterResult " + root.searchedCriteria + " " + (root.search.matches.length ? root.search.matches[0].at : "none"))
                if (!root.probeSecondSearch) { root.probeSecondSearch=true; manualDate.text="2026-09-24"; searchButton.clicked() }
            }
            if (root.probing && root.probeAcquire && success) {
                for (const row of root.search.matches) root.toggleQso(row, true)
                approveButton.clicked()
            }
        }
    }
    Timer { id:selectionProbeTimer; interval:400; onTriggered: {
        const row=selectedReview.itemAtIndex(0); const before=root.selectedQsos.length
        const label=row ? row.probeText : "missing"; if (row) row.probeRemove()
        console.warn("PROBE qslSelectionReview before="+before+" after="+root.selectedQsos.length+" row="+label)
        if (row && root.selectedQsos.length===1) approveButton.clicked()
    } }
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
            GlassButton { id: scan; text: qsTr("Read image"); enabled: !root.ocr.busy && !root.search.busy && !root.acquisition.busy && String(root.selectedImage).length > 0; onClicked: { root.selectedQsos = []; root.saved = false; root.searchedCall = ""; root.searchDone = false; manualCall.text=""; manualDate.text=""; manualTime.text=""; for (let i=0;i<criteria.count;++i) criteria.itemAt(i).currentIndex=0; root.processedImage = root.selectedImage; root.ocr.start(root.selectedImage) } }
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
        Text { Layout.fillWidth: true; text: qsTr("Unverified suggestions — choose a callsign or enter one manually. Optional date and time filters use UTC and apply only when selected."); color: Theme.warningColor; wrapMode: Text.Wrap }
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
            StyledTextField { id: manualCall; Layout.preferredWidth: 150; placeholderText: qsTr("Manual callsign"); enabled: !root.search.busy && !root.acquisition.busy; Accessible.name: qsTr("Manual callsign") }
            GlassButton {
                id: searchButton
                text: qsTr("Find exact callsign")
                enabled: root.showingResult && !root.ocr.busy && !root.search.busy && !root.acquisition.busy && (manualCall.text.trim().length > 0 || (callChoice.currentIndex >= 0 && callChoice.rows.length > 0))
                onClicked: {
                    root.searchDone = false
                    root.searchedCall = (manualCall.text.trim() || callChoice.rows[callChoice.currentIndex].value).toUpperCase()
                    const date=manualDate.text.trim() || criteria.itemAt(0).chosenValue(), time=manualTime.text.trim() || criteria.itemAt(1).chosenValue()
                    root.searchedCriteria = [root.searchedCall,date,time].filter(x => x.length).join(" · ") + (similarCall.checked ? " · " + qsTr("similar calls") : "")
                    if (similarCall.checked) root.search.startSimilar(root.searchedCall,date,time)
                    else root.search.startFiltered(root.searchedCall,date,time)
                }
            }
            GlassButton { text: qsTr("Cancel search"); enabled: root.search.busy; onClicked: root.search.cancel() }
        }
        CheckBox {
            id: similarCall
            text: qsTr("Include similar calls (one character added, removed, substituted or adjacent swap)")
            enabled: !root.search.busy && !root.acquisition.busy
        }
        Text {
            Layout.fillWidth: true; visible: similarCall.checked; color: Theme.warningColor; wrapMode: Text.Wrap
            text: qsTr("Similar matches are suggestions only; verify each callsign on the image, including any slash.")
        }
        RowLayout {
            Layout.fillWidth: true
            Repeater {
                id: criteria
                model: [qsTr("Date"), qsTr("Time"), qsTr("Band"), qsTr("Mode")]
                StyledComboBox {
                    required property int index
                    required property string modelData
                    Layout.fillWidth: true
                    Accessible.name: modelData
                    readonly property var values: root.showingResult ? (root.ocr.suggestions[["dates", "times", "bands", "modes"][index]] || []) : []
                    function chosenValue() { return currentIndex > 0 && currentIndex <= values.length ? values[currentIndex-1].value : "" }
                    model: [modelData + " (" + values.length + "): —"].concat(values.map(x => x.value + (x.ambiguous ? " ? (" + x.raw + ")" : x.uncertain ? " ?" : "")))
                }
            }
        }
        RowLayout {
            Layout.fillWidth:true
            StyledTextField { id:manualDate; Layout.fillWidth:true; placeholderText:qsTr("Manual UTC date YYYY-MM-DD"); Accessible.name:qsTr("Manual UTC date"); enabled:!root.search.busy && !root.acquisition.busy }
            StyledTextField { id:manualTime; Layout.fillWidth:true; placeholderText:qsTr("Manual UTC time HH:MM"); Accessible.name:qsTr("Manual UTC time"); enabled:!root.search.busy && !root.acquisition.busy }
        }
        Text { Layout.fillWidth:true; color:Theme.textSecondary; wrapMode:Text.Wrap; text:qsTr("Manual date or time overrides its selected suggestion. Date and time filter the search; band and mode remain suggestions for review. Ambiguous dates require an explicit choice.") }
        Text { Layout.fillWidth:true; color:Theme.textSecondary; visible:root.searchDone; text:qsTr("Results for: %1").arg(root.searchedCriteria) }
        Text { Layout.fillWidth: true; visible: root.search.error.length > 0; text: root.search.error; color: Theme.warningColor; wrapMode: Text.Wrap }
        Text { Layout.fillWidth: true; visible: root.search.truncated; text: qsTr("Showing the first 100 matches; refine the search separately."); color: Theme.warningColor; wrapMode: Text.Wrap }
        Text {
            id: noMatchNotice
            Layout.fillWidth: true
            visible: root.searchDone && !root.search.busy && root.search.matches.length === 0
            text: qsTr("No matches for the selected search criteria in the current log.")
            color: Theme.textSecondary
            wrapMode: Text.Wrap
        }
        ListView {
            id: ocrCandidates
            Layout.fillWidth: true
            Layout.preferredHeight: 85
            clip: true
            model: root.showingResult && root.searchDone ? root.search.matches : []
            ScrollBar.vertical: ScrollBar { }
            delegate: RowLayout {
                required property var modelData
                readonly property string probeText: candidateLabel.text
                width: ListView.view.width
                CheckBox {
                    Accessible.name: qsTr("Select QSO %1 for attachment").arg(modelData.call)
                    checked: root.selectedQsos.some(x => x.uuid === modelData.uuid)
                    enabled: !root.acquisition.busy
                    onClicked: root.toggleQso(modelData, checked)
                }
                Text {
                    id: candidateLabel
                    Layout.fillWidth: true
                    text: "#" + modelData.id + " · " + modelData.call + (modelData.similar ? " · " + qsTr("similar — verify") : "") + " · "
                          + (modelData.timeValid ? modelData.at : qsTr("invalid recorded timestamp: %1").arg(modelData.rawAt || "—")) + " · " + modelData.band + " · " + modelData.mode
                    textFormat: Text.PlainText
                    color: Theme.textPrimary
                    elide: Text.ElideRight
                }
            }
        }
        Text { Layout.fillWidth:true; text:qsTr("Selected for attachment: %1").arg(root.selectedQsos.length); color:Theme.textSecondary }
        ListView {
            id:selectedReview; Layout.fillWidth:true; Layout.preferredHeight:Math.min(140,root.selectedQsos.length*38)
            visible:root.selectedQsos.length>0; clip:true; model:root.selectedQsos; ScrollBar.vertical:ScrollBar {}
            delegate:RowLayout {
                required property var modelData; width:ListView.view.width
                readonly property string probeText:selectedLabel.text
                function probeRemove() { removeSelected.clicked() }
                Text { id:selectedLabel; Layout.fillWidth:true; textFormat:Text.PlainText; elide:Text.ElideRight; color:Theme.textPrimary
                    text:"#"+modelData.id+" · "+modelData.call+" · "+(modelData.timeValid ? modelData.at : qsTr("invalid recorded timestamp: %1").arg(modelData.rawAt || "—"))+" · "+modelData.band+" · "+modelData.mode }
                GlassButton { id:removeSelected; text:qsTr("Remove"); Accessible.name:qsTr("Remove QSO %1 from selection").arg(modelData.id); enabled:!root.acquisition.busy; onClicked:root.toggleQso(modelData,false) }
            }
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
