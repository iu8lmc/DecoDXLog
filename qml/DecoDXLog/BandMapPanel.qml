// DecoDXLog — la band map: la banda in verticale, con la scala delle
// frequenze, gli spot del cluster posati sulla loro frequenza e il segno
// rosso dove sta la radio. Un clic su uno spot porta la radio li' e prepara il
// QSO; un clic sulla scala sposta la radio su quella frequenza. Ctrl+rotella
// cambia lo zoom.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import Decodium.UI

GlassPanel {
    id: root

    readonly property var cluster: decolog.cluster
    // Dove sta la radio (o Decodium), in kHz; 0 se non si sa.
    readonly property real radioKhz: {
        const mhz = parseFloat(decolog.shownFrequency)
        return isNaN(mhz) ? 0 : mhz * 1000
    }
    readonly property string radioBand: radioKhz > 0 ? cluster.bandOf(radioKhz) : ""
    // "" = segue la radio.
    property string chosenBand: store.band
    readonly property string band: chosenBand.length ? chosenBand : (radioBand.length ? radioBand : "20m")
    readonly property var zooms: [0.05, 0.1, 0.2, 0.5, 1, 2, 5]
    property int zoom: store.zoom
    readonly property real kpp: zooms[Math.max(0, Math.min(zooms.length - 1, zoom))]   // kHz per pixel
    property var map: ({})
    // `bandMap()` puo' essere momentaneamente vuota mentre cambia la banda o
    // il controller aggiorna gli spot. Non passiamo mai un intervallo
    // incompleto/invertito ai Repeater: Qt lo interpreta come un modello
    // numerico negativo e stampa "Model size ... is less than 0".
    readonly property real rawLowKhz: Number(map.lowKhz)
    readonly property real rawHighKhz: Number(map.highKhz)
    readonly property bool hasValidRange: isFinite(rawLowKhz) && isFinite(rawHighKhz)
                                          && rawHighKhz > rawLowKhz
    readonly property real lowKhz: hasValidRange ? rawLowKhz : 14000
    readonly property real highKhz: hasValidRange ? rawHighKhz : 14350
    readonly property real spanKhz: Math.max(0, highKhz - lowKhz)
    readonly property var bandList: ["160m", "80m", "60m", "40m", "30m", "20m", "17m", "15m", "12m", "10m", "6m", "4m", "2m"]

    function yOf(khz) { return (khz - lowKhz) / kpp }
    function khzAt(py) { return lowKhz + py * kpp }

    function statusColor(status) {
        if (status & 1) return Theme.errorColor
        if (status & 2) return Theme.warningColor
        if (status & 4) return Theme.secondaryColor
        if (status & 8) return Theme.primaryColor
        if (status & (256 | 512 | 2048)) return Theme.warningColor
        if (status & 32) return Theme.textSecondary
        if (status & 16) return Theme.accentColor
        return Theme.textPrimary
    }

    // Le etichette non si accavallano: ognuna sta sotto la precedente se la
    // sua frequenza e' troppo vicina, e una linea la lega al suo punto.
    readonly property int rowH: 18
    property var placed: []
    // Si creano le etichette solo attorno a quello che si vede: con l'RBN su
    // una banda piena erano centinaia, rifatte tutte a ogni aggiornamento, e
    // la finestra si fermava un attimo ogni secondo. La finestra di lavoro
    // si sposta a passi di 300 pixel, non a ogni pixel di scorrimento.
    readonly property int viewStep: Math.floor(scroller.contentY / 300)
    readonly property var shown: {
        const from = (viewStep - 1) * 300
        const to = (viewStep + 1) * 300 + scroller.height + 300
        return placed.filter(s => s.top + rowH >= from && s.top <= to)
    }
    function relayout() {
        const m = cluster.bandMap(root.band, store.useFilter)
        root.map = m
        const list = (m.spots || []).slice().sort((a, b) => a.freqKhz - b.freqKhz)
        let last = -1e9
        const out = []
        for (let i = 0; i < list.length; ++i) {
            const s = list[i]
            const ideal = (s.freqKhz - (m.lowKhz || 0)) / root.kpp
            const top = Math.max(ideal - rowH / 2, last + rowH)
            last = top
            out.push(Object.assign({ ideal: ideal, top: top }, s))
        }
        root.placed = out
    }
    function centerOnRadio() {
        if (radioKhz <= 0 || cluster.bandOf(radioKhz) !== root.band)
            return
        scroller.contentY = Math.max(0, Math.min(scroller.contentHeight - scroller.height, yOf(radioKhz) - scroller.height / 2))
    }

    Settings {
        id: store
        category: "bandmap"
        property string band: ""
        property int zoom: 2
        property bool useFilter: false
        property bool follow: true
    }

    // Gli spot arrivano a raffiche: la mappa si rifa' al massimo una volta al secondo.
    Timer {
        id: relayoutTimer
        interval: 1500
        onTriggered: root.relayout()
    }
    // Come per gli altri pannelli staccabili, non lasciamo una funzione nella
    // coda globale di Qt: potrebbe essere richiamata dopo che il Loader ha
    // distrutto il pannello e produrre "invalid context" nel motore QML.
    Timer {
        id: centerTimer
        interval: 0
        onTriggered: root.centerOnRadio()
    }
    Connections {
        target: root.cluster
        // Nascosta (pannello chiuso, o l'altra lavagna davanti) non si rifa'.
        function onSpotsUpdated() { if (root.visible && !relayoutTimer.running) relayoutTimer.start() }
    }
    // Anche l'eta' degli spot cambia: ogni mezzo minuto si rifa' comunque.
    Timer { interval: 30000; repeat: true; running: root.visible; onTriggered: root.relayout() }
    onBandChanged: { relayout(); centerTimer.restart() }
    onVisibleChanged: if (visible) relayout()
    onKppChanged: { relayout(); centerTimer.restart() }
    onRadioKhzChanged: {
        if (!store.follow || radioKhz <= 0)
            return
        const py = yOf(radioKhz)
        if (py < scroller.contentY + 30 || py > scroller.contentY + scroller.height - 30)
            centerOnRadio()
    }
    Component.onCompleted: { relayout(); centerTimer.start() }

    title: qsTr("Band map")
    showDot: false
    padding: 6
    headerTools: [
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("%n spot(s)", "", (root.map.spots || []).length)
            color: Theme.textSecondary
            font.family: Theme.monoFamily
            font.pixelSize: 11
        },
        GlassButton {
            anchors.verticalCenter: parent.verticalCenter
            text: (root.chosenBand.length ? root.band : qsTr("Auto · %1").arg(root.band)) + " ▾"
            buttonHeight: 22
            fontPixelSize: 10
            onClicked: bandMenu.popup()
        },
        GlassButton {
            anchors.verticalCenter: parent.verticalCenter
            text: "−"
            buttonHeight: 22
            fontPixelSize: 12
            enabled: root.zoom < root.zooms.length - 1
            onClicked: store.zoom = root.zoom + 1
        },
        GlassButton {
            anchors.verticalCenter: parent.verticalCenter
            text: "+"
            buttonHeight: 22
            fontPixelSize: 12
            enabled: root.zoom > 0
            onClicked: store.zoom = root.zoom - 1
        },
        GlassButton {
            anchors.verticalCenter: parent.verticalCenter
            text: "⋯"
            buttonHeight: 22
            fontPixelSize: 12
            onClicked: optionsMenu.popup()
        }
    ]

    StyledMenu {
        id: bandMenu
        StyledMenuItem {
            text: qsTr("Follow the radio")
            checkable: true
            checked: root.chosenBand.length === 0
            onTriggered: store.band = ""
        }
        Repeater {
            model: root.bandList
            StyledMenuItem {
                required property string modelData
                text: modelData
                checkable: true
                checked: root.chosenBand === modelData
                onTriggered: store.band = modelData
            }
        }
    }
    StyledMenu {
        id: optionsMenu
        StyledMenuItem {
            text: qsTr("Only spots that pass the cluster filter")
            checkable: true
            checked: store.useFilter
            onTriggered: { store.useFilter = checked; root.relayout() }
        }
        StyledMenuItem {
            text: qsTr("Keep the radio in view")
            checkable: true
            checked: store.follow
            onTriggered: store.follow = checked
        }
        StyledMenuItem {
            text: qsTr("Go to the radio")
            onTriggered: root.centerOnRadio()
        }
    }

    Flickable {
        id: scroller
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: Math.max(height, root.spanKhz / root.kpp + 20)
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: PanelScrollBar {}

        // Ctrl+rotella: zoom attorno al punto sotto il mouse.
        WheelHandler {
            acceptedModifiers: Qt.ControlModifier
            onWheel: (event) => {
                const before = root.khzAt(scroller.contentY + point.position.y)
                const step = event.angleDelta.y > 0 ? -1 : 1
                const next = Math.max(0, Math.min(root.zooms.length - 1, root.zoom + step))
                if (next === root.zoom)
                    return
                store.zoom = next
                pendingScroll.frequency = before
                pendingScroll.pointerY = point.position.y
                pendingScroll.restart()
            }
        }

        Timer {
            id: pendingScroll
            property real frequency: 0
            property real pointerY: 0
            interval: 0
            onTriggered: {
                scroller.contentY = Math.max(0, Math.min(scroller.contentHeight - scroller.height,
                                                         root.yOf(frequency) - pointerY))
            }
        }

        Item {
            id: canvas
            width: scroller.width - 10
            height: scroller.contentHeight

            readonly property real scaleW: 74
            // Passo della scala: le tacche piccole ad almeno 8 pixel.
            readonly property real minor: {
                const steps = [0.1, 0.25, 0.5, 1, 2.5, 5, 10, 25, 50, 100]
                for (let i = 0; i < steps.length; ++i)
                    if (steps[i] / root.kpp >= 8) return steps[i]
                return 250
            }
            readonly property real major: minor * (minor === 0.25 || minor === 2.5 || minor === 25 ? 4 : 5)

            // La scala: un clic ci porta la radio.
            Rectangle {
                id: scale
                x: 0
                width: canvas.scaleW
                height: parent.height
                color: Theme.bgDeep
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: (mouse) => {
                        const khz = Math.round(root.khzAt(mouse.y) * 10) / 10
                        decolog.rig.tuneTo(Math.round(khz * 1000), "")
                    }
                }
                Repeater {
                    model: Math.max(0, Math.floor(root.spanKhz / canvas.minor) + 1)
                    Item {
                        required property int index
                        readonly property real khz: Math.ceil(root.lowKhz / canvas.minor) * canvas.minor + index * canvas.minor
                        readonly property bool isMajor: Math.abs(khz / canvas.major - Math.round(khz / canvas.major)) < 1e-6
                        visible: khz <= root.highKhz
                        y: root.yOf(khz)
                        Rectangle {
                            x: canvas.scaleW - width
                            width: parent.isMajor ? 12 : 6
                            height: 1
                            color: parent.isMajor ? Theme.textSecondary : Theme.borderSoft
                        }
                        Text {
                            visible: parent.isMajor
                            x: 4
                            y: -height / 2
                            text: parent.khz.toFixed(canvas.major < 1 ? 2 : canvas.major < 10 ? 1 : 0)
                            color: Theme.textSecondary
                            font.family: Theme.monoFamily
                            font.pixelSize: 10
                        }
                    }
                }
            }

            // Le etichette degli spot, con la linea fino alla frequenza.
            Repeater {
                model: root.shown
                Item {
                    id: spotItem
                    required property var modelData
                    readonly property color tone: root.statusColor(modelData.status)
                    readonly property real labelX: canvas.scaleW + 22
                    // Il punto sulla scala.
                    Rectangle {
                        x: canvas.scaleW - 3
                        y: spotItem.modelData.ideal - 3
                        width: 6; height: 6; radius: 3
                        color: spotItem.tone
                    }
                    // La linea a gomito dal punto all'etichetta.
                    Rectangle {
                        x: canvas.scaleW + 3
                        y: spotItem.modelData.ideal
                        width: 9
                        height: 1
                        color: spotItem.tone
                        opacity: 0.6
                    }
                    Rectangle {
                        x: canvas.scaleW + 12
                        y: Math.min(spotItem.modelData.ideal, spotItem.modelData.top + root.rowH / 2)
                        width: 1
                        height: Math.abs(spotItem.modelData.top + root.rowH / 2 - spotItem.modelData.ideal) + 1
                        color: spotItem.tone
                        opacity: 0.6
                    }
                    Rectangle {
                        x: canvas.scaleW + 12
                        y: spotItem.modelData.top + root.rowH / 2
                        width: 10
                        height: 1
                        color: spotItem.tone
                        opacity: 0.6
                    }
                    Rectangle {
                        id: label
                        x: spotItem.labelX
                        y: spotItem.modelData.top + 1
                        width: canvas.width - x
                        height: root.rowH - 2
                        radius: 3
                        color: hover.hovered ? Qt.rgba(spotItem.tone.r, spotItem.tone.g, spotItem.tone.b, 0.18) : "transparent"
                        opacity: Math.max(0.45, 1 - spotItem.modelData.ageMinutes / 60)
                        HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: root.cluster.tune(spotItem.modelData.key) }
                        ToolTip.visible: hover.hovered
                        ToolTip.delay: 500
                        ToolTip.text: [spotItem.modelData.call, spotItem.modelData.freqKhz.toFixed(1) + " kHz",
                                       spotItem.modelData.mode, spotItem.modelData.entity,
                                       spotItem.modelData.statusLabel, spotItem.modelData.comment,
                                       qsTr("%1 min ago").arg(spotItem.modelData.ageMinutes)].filter(s => s).join(" · ")
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            x: 4
                            spacing: 6
                            Text {
                                text: spotItem.modelData.call
                                color: spotItem.tone
                                font.family: Theme.monoFamily
                                font.pixelSize: 12
                                font.bold: (spotItem.modelData.status & 3) !== 0
                            }
                            Text {
                                text: spotItem.modelData.freqKhz.toFixed(1)
                                color: Theme.textSecondary
                                font.family: Theme.monoFamily
                                font.pixelSize: 10
                                anchors.baseline: parent.children[0].baseline
                            }
                            Text {
                                text: spotItem.modelData.mode
                                color: Theme.textSecondary
                                font.family: Theme.monoFamily
                                font.pixelSize: 10
                                anchors.baseline: parent.children[0].baseline
                            }
                            Text {
                                visible: spotItem.modelData.statusLabel.length > 0
                                text: spotItem.modelData.statusLabel
                                color: spotItem.tone
                                font.family: Theme.monoFamily
                                font.pixelSize: 9
                                anchors.baseline: parent.children[0].baseline
                            }
                        }
                    }
                }
            }

            // La radio: una riga rossa con la frequenza.
            Item {
                visible: root.radioKhz > 0 && root.cluster.bandOf(root.radioKhz) === root.band
                y: root.yOf(root.radioKhz)
                width: canvas.width
                z: 5
                Rectangle { width: parent.width; height: 2; y: -1; color: Theme.errorColor; opacity: 0.85 }
                Rectangle {
                    x: 2
                    y: -9
                    width: radioText.implicitWidth + 8
                    height: 18
                    radius: 3
                    color: Theme.errorColor
                    Text {
                        id: radioText
                        anchors.centerIn: parent
                        text: root.radioKhz.toFixed(1)
                        color: "white"
                        font.family: Theme.monoFamily
                        font.pixelSize: 11
                        font.bold: true
                    }
                }
            }
        }
    }

    // Niente spot: si dice perche'.
    Text {
        anchors.centerIn: parent
        visible: (root.map.spots || []).length === 0
        width: parent.width - 40
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        text: root.cluster.onlineCount > 0 ? qsTr("No spots on %1 right now.").arg(root.band)
                                           : qsTr("No cluster connected: open the DX Cluster and connect a source.")
        color: Theme.textSecondary
        font.pixelSize: 12
    }
}
