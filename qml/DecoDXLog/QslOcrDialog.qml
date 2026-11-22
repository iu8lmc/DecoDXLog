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
    property url selectedImage: ""
    property url processedImage: ""
    property bool probing: false
    readonly property bool showingResult: String(processedImage).length > 0 && String(selectedImage) === String(processedImage)
    onClosed: if (root.ocr.busy) root.ocr.cancel()
    function probe(url) {
        probing = true
        selectedImage = url
        scan.clicked()
    }
    Connections {
        target: root.ocr
        function onFinished(success) {
            if (root.probing)
                console.warn("PROBE qslOcr success=" + success + " hasCall=" + root.ocr.text.includes("K1ABC")
                             + " count=" + decolog.currentLogInfo().qsos + " words=" + root.ocr.words.length)
        }
    }
    FileDialog {
        id: picker
        title: qsTr("Select a local QSL image")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.tif *.tiff *.bmp *.webp)"), qsTr("All files (*)")]
        onAccepted: { root.selectedImage = selectedFile; root.processedImage = "" }
    }
    body: ColumnLayout {
        anchors.margins: 14
        spacing: 8
        Text {
            Layout.fillWidth: true
            text: qsTr("Local OCR is a suggestion only. Verify calls, dates, times and every other field against the image. It does not change the QSO or confirmation status.")
            color: Theme.textSecondary
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            GlassButton { text: qsTr("Choose image…"); enabled: !root.ocr.busy; onClicked: picker.open() }
            Text { Layout.fillWidth: true; text: String(root.selectedImage) || qsTr("No image selected"); textFormat: Text.PlainText; color: Theme.textPrimary; elide: Text.ElideMiddle }
            GlassButton { id: scan; text: qsTr("Read image"); enabled: !root.ocr.busy && String(root.selectedImage).length > 0; onClicked: { root.processedImage = root.selectedImage; root.ocr.start(root.selectedImage) } }
            GlassButton { text: qsTr("Cancel"); enabled: root.ocr.busy; onClicked: root.ocr.cancel() }
        }
        Text {
            Layout.fillWidth: true
            text: root.ocr.executable.length ? qsTr("Engine: %1 · English model").arg(root.ocr.executable) : qsTr("Tesseract OCR is not installed or not on the search path.")
            color: Theme.textSecondary
            wrapMode: Text.Wrap
        }
        Text { Layout.fillWidth: true; visible: root.ocr.busy; text: qsTr("Reading locally…"); color: Theme.secondaryColor }
        Text { Layout.fillWidth: true; visible: root.showingResult && root.ocr.error.length > 0; text: root.ocr.error; color: Theme.warningColor; wrapMode: Text.Wrap }
        Text { text: qsTr("Recognized text"); color: Theme.textPrimary; font.bold: true }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
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
            text: qsTr("Use Import scan or image in the QSO detail to attach the original separately.")
            color: Theme.textSecondary
            wrapMode: Text.Wrap
        }
    }
}
