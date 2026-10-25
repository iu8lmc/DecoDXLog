import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decodium.UI

ColumnLayout {
    id: root
    property var controller
    property string callsign: ""
    readonly property var groups: {
        const revision = controller ? controller.revision : 0
        return controller ? controller.lookup(callsign) : []
    }
    visible: callsign.length > 0 && controller && controller.sources.length > 0
    spacing: 6
    Text {
        Layout.fillWidth: true
        text: qsTr("Callsign & club lists")
        color: Theme.secondaryColor
        font.bold: true
        wrapMode: Text.Wrap
    }
    Text {
        Layout.fillWidth: true
        visible: root.groups.length === 0
        text: root.controller && root.controller.busy ? qsTr("Loading local lists…")
            : qsTr("No exact match in the available enabled lists.")
        color: Theme.textSecondary
        wrapMode: Text.Wrap
        font.pixelSize: 11
    }
    Repeater {
        model: root.groups
        ColumnLayout {
            required property var modelData
            Layout.fillWidth: true
            spacing: 3
            Text {
                objectName: "callListSource"
                Layout.fillWidth: true
                text: modelData.source + " · " + modelData.importedAt
                textFormat: Text.PlainText
                color: Theme.textSecondary
                wrapMode: Text.WrapAnywhere
                font.pixelSize: 11
            }
            Repeater {
                model: modelData.entries
                Text {
                    objectName: "callListEntry"
                    required property var modelData
                    Layout.fillWidth: true
                    text: modelData.fields.length > 0
                        ? modelData.fields.map(f => f.label + ": " + f.value).join(" · ")
                        : modelData.call
                    textFormat: Text.PlainText
                    color: Theme.textPrimary
                    wrapMode: Text.WrapAnywhere
                    font.pixelSize: 12
                }
            }
        }
    }
    Text {
        Layout.fillWidth: true
        text: qsTr("Information from imported lists, not live membership verification. Log and callbook data are unchanged.")
        color: Theme.textSecondary
        wrapMode: Text.Wrap
        font.pixelSize: 10
    }
}
