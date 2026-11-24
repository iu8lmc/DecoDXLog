// DecoDXLog — tutti i filtri di uno spot, per il filtro della lista e per le regole
// d'avviso. Lavora su una copia: `changed` porta il filtro nuovo.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decodium.UI

ColumnLayout {
    id: root

    property var filter: ({})
    property bool showAge: true
    signal edited(var filter)

    readonly property var bandList: ["160m", "80m", "60m", "40m", "30m", "20m", "17m", "15m", "12m", "10m", "6m", "4m", "2m", "70cm"]
    readonly property var modeList: ["FT2", "FT8", "FT4", "CW", "SSB", "RTTY", "DIGI"]
    readonly property var continentList: ["EU", "NA", "SA", "AS", "AF", "OC", "AN"]
    readonly property var sourceList: [["cluster", qsTr("Cluster")], ["rbn", "RBN"], ["hamalert", "HamAlert"], ["pota", "POTA"]]

    function has(key, value) { return (root.filter[key] || []).indexOf(value) >= 0 }
    function toggle(key, value) {
        const f = Object.assign({}, root.filter)
        const list = (f[key] || []).slice()
        const i = list.indexOf(value)
        if (i >= 0) list.splice(i, 1); else list.push(value)
        f[key] = list
        root.edited(f)
    }
    function set(key, value) {
        const f = Object.assign({}, root.filter)
        f[key] = value
        root.edited(f)
    }
    function toggleStatus(bit) { root.set("anyStatus", (root.filter.anyStatus || 0) ^ bit) }
    function probeChipsFit() {
        for (const repeater of [statusChips,bandChips,modeChips]) {
            for (let i=0;i<repeater.count;++i) {
                const chip=repeater.itemAt(i); if (!chip) return false
                const pos=chip.mapToItem(root,0,0)
                if (pos.x < -0.5 || pos.x+chip.width > root.width+0.5) return false
            }
        }
        return true
    }
    function probeToggleBand(band) {
        for (let i=0;i<bandChips.count;++i) {
            const chip=bandChips.itemAt(i)
            if (chip && chip.label===band) { chip.toggled(); return true }
        }
        return false
    }

    spacing: 8

    component Chip: Rectangle {
        id: chip
        property string label
        property bool on: false
        property color tone: Theme.primaryColor
        signal toggled()
        implicitHeight: 24
        implicitWidth: chipText.implicitWidth + 16
        radius: 4
        color: on ? Qt.rgba(tone.r, tone.g, tone.b, 0.22) : chipArea.containsMouse ? Theme.glassOverlay : "transparent"
        border.width: 1
        border.color: on ? tone : Theme.glassBorder
        Text {
            id: chipText
            anchors.centerIn: parent
            text: chip.label
            color: chip.on ? chip.tone : Theme.textSecondary
            font.family: Theme.monoFamily
            font.pixelSize: 11
            font.bold: chip.on
        }
        MouseArea {
            id: chipArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: chip.toggled()
        }
    }
    component Row2: RowLayout {
        property alias title: caption.text
        default property alias chips: flow.data
        Layout.fillWidth: true
        spacing: 10
        Text {
            id: caption
            Layout.preferredWidth: 110
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 4
            color: Theme.textSecondary
            font.pixelSize: 11
        }
        Flow {
            id: flow
            Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredWidth: 0
            spacing: 4
        }
    }

    Row2 {
        title: qsTr("Status (any of)")
        Repeater {
            id:statusChips
            model: decolog.cluster.statusNames
            Chip {
                required property var modelData
                label: modelData.label
                tone: modelData.bit === 1 ? Theme.errorColor : modelData.bit === 2 ? Theme.warningColor : Theme.accentColor
                on: ((root.filter.anyStatus || 0) & modelData.bit) !== 0
                onToggled: root.toggleStatus(modelData.bit)
            }
        }
    }
    Row2 {
        title: qsTr("Bands")
        Repeater {
            id:bandChips
            model: root.bandList
            Chip { required property string modelData; label: modelData; on: root.has("bands", modelData); onToggled: root.toggle("bands", modelData) }
        }
    }
    Row2 {
        title: qsTr("Modes")
        Repeater {
            id:modeChips
            model: root.modeList
            Chip {
                required property string modelData
                label: modelData
                tone: modelData === "FT2" ? Theme.accentColor : Theme.primaryColor
                on: root.has("modes", modelData)
                onToggled: root.toggle("modes", modelData)
            }
        }
    }
    Row2 {
        title: qsTr("DX continent")
        Repeater {
            model: root.continentList
            Chip { required property string modelData; label: modelData; tone: Theme.secondaryColor; on: root.has("dxContinents", modelData); onToggled: root.toggle("dxContinents", modelData) }
        }
    }
    Row2 {
        title: qsTr("Spotter continent")
        Repeater {
            model: root.continentList
            Chip { required property string modelData; label: modelData; tone: Theme.secondaryColor; on: root.has("spotterContinents", modelData); onToggled: root.toggle("spotterContinents", modelData) }
        }
    }
    Row2 {
        title: qsTr("Sources")
        Repeater {
            model: root.sourceList
            Chip { required property var modelData; label: modelData[1]; on: root.has("sources", modelData[0]); onToggled: root.toggle("sources", modelData[0]) }
        }
    }
    Row2 {
        title: qsTr("Only")
        Chip { label: qsTr("not worked on band"); on: !!root.filter.hideWorkedBand; onToggled: root.set("hideWorkedBand", !root.filter.hideWorkedBand) }
        Chip { label: qsTr("POTA/SOTA/WWFF/IOTA"); on: !!root.filter.onlyActivations; onToggled: root.set("onlyActivations", !root.filter.onlyActivations) }
        Chip { label: qsTr("LoTW users"); on: !!root.filter.onlyLotw; onToggled: root.set("onlyLotw", !root.filter.onlyLotw) }
        Chip { label: qsTr("no skimmers"); on: root.filter.skimmers === false; onToggled: root.set("skimmers", root.filter.skimmers === false) }
    }
    RowLayout {
        Layout.fillWidth: true; Layout.minimumWidth: 0
        spacing: 10
        LabeledField {
            Layout.minimumWidth: 0
            Layout.preferredWidth: 1
            Layout.horizontalStretchFactor: 5
            Layout.fillWidth: true
            label: qsTr("Calls (wildcards * ?)")
            StyledTextField {
                Layout.fillWidth: true
                uppercase: true
                placeholderText: "3Y0J, VP8*, *…"
                text: root.filter.calls || ""
                onEditingFinished: if (text !== (root.filter.calls || "")) root.set("calls", text)
            }
        }
        LabeledField {
            Layout.minimumWidth: 0
            Layout.preferredWidth: 1
            Layout.horizontalStretchFactor: 4
            Layout.fillWidth: true
            label: qsTr("Text (call, entity, comment)")
            StyledTextField {
                Layout.fillWidth: true
                mono: false
                text: root.filter.text || ""
                onEditingFinished: if (text !== (root.filter.text || "")) root.set("text", text)
            }
        }
        LabeledField {
            Layout.minimumWidth: 0
            Layout.preferredWidth: 1
            Layout.horizontalStretchFactor: 2
            Layout.fillWidth: true
            label: qsTr("Min SNR (skimmer)")
            StyledComboBox {
                Layout.fillWidth: true
                readonly property var values: [-99, -20, -15, -10, 0, 10, 20]
                model: [qsTr("any"), "-20 dB", "-15 dB", "-10 dB", "0 dB", "10 dB", "20 dB"]
                currentIndex: Math.max(0, values.indexOf(root.filter.minSnr === undefined ? -99 : root.filter.minSnr))
                onActivated: root.set("minSnr", values[currentIndex])
            }
        }
        LabeledField {
            Layout.minimumWidth: 0
            visible: root.showAge
            Layout.preferredWidth: 1
            Layout.horizontalStretchFactor: 2
            Layout.fillWidth: true
            label: qsTr("Max age")
            StyledComboBox {
                Layout.fillWidth: true
                readonly property var values: [5, 10, 15, 30, 60]
                model: ["5 min", "10 min", "15 min", "30 min", "60 min"]
                currentIndex: Math.max(0, values.indexOf(root.filter.maxAgeMinutes === undefined ? 30 : root.filter.maxAgeMinutes))
                onActivated: root.set("maxAgeMinutes", values[currentIndex])
            }
        }
    }
}
