// Campi del pannello Nuovo QSO: quelli standard si mostrano o nascondono;
// quelli liberi sono veri campi ADIF e vengono conservati nel log e negli
// export. La validazione finale e il salvataggio stanno nel controller.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decodium.UI

DialogFrame {
    id: root

    title: qsTr("New QSO fields")
    dialogKey: "new-qso-fields"
    dotColor: Theme.accentColor
    width: 680
    height: 650
    minimumWidth: 520
    minimumHeight: 480

    property var options: []

    ListModel { id: customModel }

    function reload(useDefaults = false) {
        root.options = decolog.newQsoFieldOptions()
        enterToLogToggle.checked = !useDefaults && decolog.enterToLogQso
        customModel.clear()
        if (!useDefaults) {
            for (const field of decolog.newQsoCustomFields)
                customModel.append({ fieldName: field.name, fieldLabel: field.label, fieldType:field.type || "text", fieldUnit:field.unit || "", fieldChoices:(field.choices || []).join(", "), fieldDepends:(field.requiredIf || {}).field || "", fieldExpected:(field.requiredIf || {}).equals || "",fieldFormula:field.formula || "" })
        }
        settingsError.text = ""
        Qt.callLater(function() {
            const visible = decolog.newQsoVisibleFields
            for (let i = 0; i < standardRepeater.count; ++i) {
                const item = standardRepeater.itemAt(i)
                if (item)
                    item.checked = useDefaults ? item.modelData.defaultVisible : visible.indexOf(item.fieldKey) >= 0
            }
        })
    }

    function save() {
        const visible = []
        for (let i = 0; i < standardRepeater.count; ++i) {
            const item = standardRepeater.itemAt(i)
            if (item && item.checked)
                visible.push(item.fieldKey)
        }

        const custom = []
        for (let i = 0; i < customRepeater.count; ++i) {
            const item = customRepeater.itemAt(i)
            if (item)
                custom.push({ name: item.fieldNameText, label: item.fieldLabelText, type:item.selectedType, unit:item.unitText, choices:item.choicesText.split(",").map(x => x.trim()).filter(x => x.length), requiredIf:{field:item.dependsText, equals:item.expectedText},formula:item.formulaText })
        }
        const error = decolog.saveNewQsoPanelSettings(visible, custom, enterToLogToggle.checked)
        settingsError.text = error
        if (error.length === 0)
            root.accept()
    }

    onOpened: reload()
    function probeTyped() {
        customModel.clear(); customModel.append({fieldName:"APP_TYPED_POWER",fieldLabel:"Power",fieldType:"number",fieldUnit:"W",fieldChoices:"",fieldDepends:"",fieldExpected:"",fieldFormula:""})
        customModel.append({fieldName:"APP_TYPED_NOTE",fieldLabel:"Note",fieldType:"text",fieldUnit:"",fieldChoices:"",fieldDepends:"APP_TYPED_POWER",fieldExpected:"12",fieldFormula:""})
        customModel.append({fieldName:"APP_TYPED_DOUBLE",fieldLabel:"Double",fieldType:"number",fieldUnit:"W",fieldChoices:"",fieldDepends:"",fieldExpected:"",fieldFormula:"{APP_TYPED_POWER} * 2"})
        Qt.callLater(function() { const row=customRepeater.itemAt(0); if (!row) { console.warn("PROBE typed missing field"); return }; console.warn("PROBE typed editor " + row.selectedType + " " + row.unitText); root.save() })
    }

    body: ColumnLayout {
        anchors.margins: 16
        spacing: 10

        Text {
            Layout.fillWidth: true
            text: qsTr("Choose the optional fields shown in the compact New QSO panel. Callsign, UTC, band, frequency, mode and reports always remain available.")
            color: Theme.textSecondary
            wrapMode: Text.WordWrap
            font.pixelSize: 12
        }

        ToggleSwitch {
            id: enterToLogToggle
            objectName: "enterToLogToggle"
            Layout.fillWidth: true
            text: qsTr("Enter logs the QSO in manual entry fields")
        }

        Text {
            text: qsTr("STANDARD FIELDS")
            color: Theme.secondaryColor
            font.family: Theme.monoFamily
            font.pixelSize: 11
            font.bold: true
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 16
            rowSpacing: 8

            Repeater {
                id: standardRepeater
                model: root.options
                delegate: ToggleSwitch {
                    required property var modelData
                    property string fieldKey: String(modelData.key)
                    Layout.fillWidth: true
                    text: String(modelData.label) + "  ·  " + fieldKey
                    checked: decolog.newQsoVisibleFields.indexOf(fieldKey) >= 0
                    Accessible.name: qsTr("Show %1 in New QSO").arg(String(modelData.label))
                }
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.borderSoft }

        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: qsTr("CUSTOM ADIF FIELDS")
                color: Theme.secondaryColor
                font.family: Theme.monoFamily
                font.pixelSize: 11
                font.bold: true
            }
            GlassButton {
                text: qsTr("Add field")
                enabled: customModel.count < 12
                onClicked: customModel.append({ fieldName: "", fieldLabel: "", fieldType:"text", fieldUnit:"", fieldChoices:"",fieldDepends:"",fieldExpected:"",fieldFormula:"" })
            }
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("Add fields such as RDA or an APP_ field. Use the exact ADIF name; the optional label is what appears in the panel. STATE and TX_PWR are already available above.")
            color: Theme.textSecondary
            wrapMode: Text.WordWrap
            font.pixelSize: 11
        }
        Text { Layout.fillWidth:true; color:Theme.textSecondary; wrapMode:Text.Wrap; font.pixelSize:11
            text:qsTr("Numeric formulas use {FIELD}, decimal constants with a dot, +, -, *, / and parentheses. Refer only to configured numeric custom fields. Missing inputs leave the result empty; the formula and input locale are recorded with the QSO.") }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 150
            clip: true
            contentWidth: availableWidth
            ScrollBar.vertical: PanelScrollBar {}

            ColumnLayout {
                width: parent.width
                spacing: 7

                Text {
                    Layout.fillWidth: true
                    visible: customModel.count === 0
                    text: qsTr("No custom fields configured.")
                    color: Theme.textSecondary
                    font.italic: true
                }

                Repeater {
                    id: customRepeater
                    model: customModel
                    delegate: ColumnLayout {
                        id: customRow
                        required property int index
                        required property string fieldName
                        required property string fieldLabel
                        required property string fieldType
                        required property string fieldUnit
                        required property string fieldChoices
                        required property string fieldDepends
                        required property string fieldExpected
                        required property string fieldFormula
                        property alias fieldNameText: nameField.text
                        property alias fieldLabelText: labelField.text
                        property alias unitText: unitField.text
                        property alias choicesText: choicesField.text
                        property alias dependsText: dependsField.text
                        property alias expectedText: expectedField.text
                        property alias formulaText: formulaField.text
                        readonly property string selectedType: typeChoice.keys[typeChoice.currentIndex] || "text"
                        Layout.fillWidth: true
                        spacing: 8
                        RowLayout {
                        Layout.fillWidth:true
                        LabeledField {
                            Layout.preferredWidth: 2
                            Layout.fillWidth: true
                            label: qsTr("ADIF name")
                            StyledTextField {
                                id: nameField
                                Layout.fillWidth: true
                                text: customRow.fieldName
                                uppercase: true
                                placeholderText: qsTr("ADIF name")
                                Accessible.name: qsTr("Custom ADIF field name")
                            }
                        }
                        LabeledField {
                            Layout.preferredWidth: 3
                            Layout.fillWidth: true
                            label: qsTr("Label (optional)")
                            StyledTextField {
                                id: labelField
                                Layout.fillWidth: true
                                text: customRow.fieldLabel
                                placeholderText: qsTr("Label (optional)")
                                Accessible.name: qsTr("Custom ADIF field label")
                            }
                        }
                        GlassButton {
                            Layout.alignment: Qt.AlignBottom
                            text: "×"
                            tone: Theme.errorColor
                            hint: qsTr("Remove custom field")
                            Accessible.name: qsTr("Remove custom field %1").arg(nameField.text)
                            onClicked: customModel.remove(customRow.index)
                        }
                        }
                        RowLayout {
                            Layout.fillWidth:true
                            StyledComboBox { id:typeChoice; Layout.preferredWidth:155
                                readonly property var keys:["text","number","date","boolean","list"]
                                model:[qsTr("Text"),qsTr("Number"),qsTr("Date"),qsTr("Boolean"),qsTr("List")]
                                currentIndex:Math.max(0,keys.indexOf(customRow.fieldType)); Accessible.name:qsTr("Custom field type") }
                            StyledTextField { id:unitField; Layout.preferredWidth:100; text:customRow.fieldUnit; placeholderText:qsTr("Unit"); Accessible.name:qsTr("Custom field unit") }
                            StyledTextField { id:choicesField; Layout.fillWidth:true; text:customRow.fieldChoices; visible:customRow.selectedType==="list"; placeholderText:qsTr("Allowed values, comma separated"); Accessible.name:qsTr("Custom field allowed values") }
                        }
                        RowLayout {
                            Layout.fillWidth:true
                            StyledTextField { id:dependsField; Layout.fillWidth:true; text:customRow.fieldDepends; uppercase:true; placeholderText:qsTr("Required if custom field…"); Accessible.name:qsTr("Required if custom field") }
                            StyledTextField { id:expectedField; Layout.fillWidth:true; text:customRow.fieldExpected; placeholderText:qsTr("…equals this value"); Accessible.name:qsTr("Required if field equals") }
                        }
                        StyledTextField { id:formulaField; Layout.fillWidth:true; text:customRow.fieldFormula; visible:customRow.selectedType==="number" || text.length>0
                            placeholderText:qsTr("Optional formula, e.g. {APP_POWER} * 2"); Accessible.name:qsTr("Numeric derived field formula") }
                    }
                }
            }
        }

        Text {
            id: settingsError
            Layout.fillWidth: true
            visible: text.length > 0
            color: Theme.errorColor
            wrapMode: Text.WordWrap
            font.pixelSize: 11
        }

        RowLayout {
            Layout.fillWidth: true
            GlassButton {
                text: qsTr("Restore defaults")
                onClicked: root.reload(true)
            }
            Item { Layout.fillWidth: true }
            GlassButton { text: qsTr("Cancel"); onClicked: root.reject() }
            GlassButton {
                text: qsTr("Save")
                tone: Theme.accentColor
                filled: true
                onClicked: root.save()
            }
        }
    }
}
