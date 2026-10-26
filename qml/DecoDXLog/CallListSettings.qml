import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Decodium.UI

ColumnLayout {
    id: root
    property var controller
    property url fileUrl
    property bool previewCurrent: false
    property bool awaitingPreview: false
    property var mapping: ({})
    readonly property var preview: controller ? controller.preview : ({})
    readonly property var columnChoices: [qsTr("Not mapped")].concat(preview.columns || [])
    spacing: 10

    function baseOptions() {
        return {delimiter: ["auto", ",", ";", "\t"][separator.currentIndex],
                encoding: ["auto", "utf8", "latin1"][encoding.currentIndex], header: header.checked}
    }
    function inspectFile() {
        previewCurrent = false
        awaitingPreview = true
        mapping = ({})
        controller.inspect(fileUrl, baseOptions())
    }
    function importFile() {
        const options = baseOptions()
        if (!preview.n1mm) {
            options.callColumn = callColumn.currentIndex - 1
            for (const key in mapping) options[key] = mapping[key]
        }
        controller.add(fileUrl, listName.text, options)
    }
    Connections {
        target: root.controller
        function onChanged() {
            if (root.awaitingPreview && !root.controller.busy) {
                root.awaitingPreview = false
                root.previewCurrent = !!root.preview.columns
            }
        }
    }
    component Note: Text {
        Layout.fillWidth: true
        textFormat: Text.PlainText
        wrapMode: Text.WrapAnywhere
        color: Theme.textSecondary
        font.pixelSize: 12
    }
    SectionTitle { text: qsTr("Callsign & club lists") }
    Note {
        text: qsTr("Import local CSV/TXT or N1MM Call History files. Several lists can be active together, outside Contest Mode too. Results appear in Call info; they never replace QSO, callbook or contest fields.")
    }
    Note { text: qsTr("Changes here are saved immediately. An offline copy is kept; Reload reads the original file again.") }
    Repeater {
        model: root.controller ? root.controller.sources : []
        ColumnLayout {
            required property var modelData
            Layout.fillWidth: true
            spacing: 4
            RowLayout {
                Layout.fillWidth: true
                ToggleSwitch {
                    checked: modelData.enabled
                    enabled: !root.controller.busy
                    Accessible.name: modelData.label
                    onToggled: root.controller.setEnabled(modelData.id, checked)
                }
                Note { text: modelData.label; color: Theme.textPrimary }
            }
            Note { text: modelData.path + "\n" + qsTr("%1 entries · imported %2").arg(modelData.records).arg(modelData.importedAt) }
            Note { visible: modelData.error.length > 0; text: modelData.error; color: Theme.warningColor }
            RowLayout {
                GlassButton { text: qsTr("Reload"); enabled: !root.controller.busy; onClicked: root.controller.reload(modelData.id) }
                GlassButton {
                    text: qsTr("Remove")
                    enabled: !root.controller.busy
                    onClicked: { removeDialog.sourceId = modelData.id; removeDialog.open() }
                }
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.borderSoft }
        }
    }
    GlassButton {
        objectName: "callListChooseFile"
        text: qsTr("Add a list…")
        enabled: root.controller && !root.controller.busy
        onClicked: fileDialog.open()
    }
    ColumnLayout {
        visible: root.fileUrl.toString().length > 0
        Layout.fillWidth: true
        enabled: root.controller && !root.controller.busy
        spacing: 8
        Note { text: root.fileUrl.toString() }
        LabeledField {
            Layout.fillWidth: true
            label: qsTr("List name")
            StyledTextField { id: listName; Layout.fillWidth: true; maximumLength: 120; placeholderText: qsTr("For example: my CW clubs") }
        }
        GridLayout {
            Layout.fillWidth: true
            columns: root.width > 620 ? 2 : 1
            LabeledField {
                Layout.fillWidth: true
                label: qsTr("Separator")
                StyledComboBox {
                    id: separator
                    Layout.fillWidth: true
                    model: [qsTr("Auto"), qsTr("Comma"), qsTr("Semicolon"), qsTr("Tab")]
                    onActivated: root.previewCurrent = false
                }
            }
            LabeledField {
                Layout.fillWidth: true
                label: qsTr("Encoding")
                StyledComboBox {
                    id: encoding
                    Layout.fillWidth: true
                    model: [qsTr("Auto (UTF-8 / Latin-1)"), "UTF-8", "Latin-1"]
                    onActivated: root.previewCurrent = false
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            ToggleSwitch { id: header; checked: true; onToggled: root.previewCurrent = false; Accessible.name: headerLabel.text }
            Note { id: headerLabel; text: qsTr("First row contains column names (CSV)") }
        }
        GlassButton { text: qsTr("Preview / refresh columns"); onClicked: root.inspectFile() }
        ColumnLayout {
            visible: root.previewCurrent
            Layout.fillWidth: true
            Note { text: (root.preview.format || "") + " · " + (root.preview.encoding || "") }
            Note { visible: !!root.preview.n1mm; text: qsTr("N1MM columns are read from each !!Order!! directive. All memberships are kept separately.") }
            GridLayout {
                visible: !root.preview.n1mm
                Layout.fillWidth: true
                columns: root.width > 620 ? 2 : 1
                LabeledField {
                    Layout.fillWidth: true
                    label: qsTr("Callsign column (required)")
                    StyledComboBox {
                        id: callColumn
                        objectName: "callListCallColumn"
                        Layout.fillWidth: true
                        model: root.columnChoices
                        currentIndex: (root.preview.callColumn === undefined ? -1 : root.preview.callColumn) + 1
                    }
                }
                Repeater {
                    model: [{key: "nameColumn", label: qsTr("Name column")}, {key: "clubColumn", label: qsTr("Club column")},
                            {key: "memberColumn", label: qsTr("Membership number column")}, {key: "qthColumn", label: qsTr("QTH column")}]
                    LabeledField {
                        required property var modelData
                        Layout.fillWidth: true
                        label: modelData.label
                        StyledComboBox {
                            objectName: "callListMap_" + modelData.key
                            Layout.fillWidth: true
                            model: root.columnChoices
                            currentIndex: (root.mapping[modelData.key] === undefined ? -1 : root.mapping[modelData.key]) + 1
                            onActivated: {
                                const updated = Object.assign({}, root.mapping)
                                updated[modelData.key] = currentIndex - 1
                                root.mapping = updated
                            }
                        }
                    }
                }
            }
            Note { text: qsTr("Unmapped non-empty fields are also displayed, using their column names. Matching is exact, ignoring letter case; portable suffixes are not removed.") }
            Note { text: (root.preview.columns || []).join(" | "); color: Theme.secondaryColor }
            Repeater {
                model: root.preview.samples || []
                Note { required property var modelData; text: modelData.join(" | ") }
            }
            GlassButton {
                objectName: "callListImport"
                text: qsTr("Import list")
                tone: Theme.accentColor
                enabled: root.previewCurrent && (!!root.preview.n1mm || callColumn.currentIndex > 0)
                onClicked: root.importFile()
            }
        }
    }
    Note { objectName: "callListStatus"; text: root.controller ? root.controller.status : ""; color: Theme.secondaryColor }
    Item { Layout.fillHeight: true }
    FileDialog {
        id: fileDialog
        title: qsTr("Import a callsign list")
        nameFilters: [qsTr("Call lists (*.csv *.txt *.tsv)"), qsTr("All files (*)")]
        onAccepted: { root.fileUrl = selectedFile; listName.text = ""; root.inspectFile() }
    }
    MessageDialog {
        id: removeDialog
        property string sourceId
        title: qsTr("Remove list")
        text: qsTr("Remove this list and its offline copy? The original file will not be deleted.")
        buttons: MessageDialog.Yes | MessageDialog.No
        onButtonClicked: function(button, role) { if (button === MessageDialog.Yes) root.controller.remove(sourceId) }
    }
}
