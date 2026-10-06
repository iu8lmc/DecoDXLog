// DecoDXLog — carta del mondo, spot del cluster e direzione dell'antenna.
// La vista completa usa Canvas; su Linux la sostituiamo con MapSafeView prima
// ancora che Canvas venga istanziato, per evitare blocchi Mesa/KWin.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decodium.UI

GlassPanel {
    id: root

    property bool showCoast: true
    property bool showNight: true
    property bool showGrids: true
    property bool showSpots: true
    property bool showRotor: true

    readonly property var target: decolog.callInfo.position
    readonly property var home: decolog.myPosition
    readonly property var grids: decolog.gridPoints
    property var spots: []
    property var coastline: []
    property var land: []

    // Impostato dal C++ prima del caricamento QML. Su Linux e con il renderer
    // software e' vero per impostazione predefinita; --map-renderer canvas lo
    // puo' cambiare esplicitamente per chi ha un driver gia' verificato.
    readonly property bool useSafeMap: mapUsesSafeRenderer === true

    function repaintAll() {
        if (mapRenderer.item && typeof mapRenderer.item.repaintAll === "function")
            mapRenderer.item.repaintAll()
    }
    function repaintBackground() {
        if (mapRenderer.item && typeof mapRenderer.item.repaintAll === "function")
            mapRenderer.item.repaintBackground()
    }
    function repaintOverlay() {
        if (mapRenderer.item && typeof mapRenderer.item.repaintAll === "function")
            mapRenderer.item.repaintOverlay()
    }
    function reloadSpots() {
        spots = root.showSpots ? decolog.cluster.mapSpots() : []
        root.repaintOverlay()
    }

    // In gara gli spot arrivano a raffica: si aggiorna al massimo una volta
    // ogni due secondi, sia sulla mappa completa sia su quella compatibile.
    function scheduleSpots() {
        if (!spotThrottle.running)
            spotThrottle.start()
    }
    Timer {
        id: spotThrottle
        interval: 2000
        onTriggered: root.reloadSpots()
    }
    // Una callback di Qt.callLater puo' arrivare dopo che questo pannello e'
    // stato scaricato da un Loader (per esempio chiudendo una finestra
    // staccata). Il Timer appartiene invece al pannello: se il pannello muore,
    // non resta nessuna funzione QML da valutare fuori contesto.
    Timer {
        id: initialPaint
        interval: 0
        onTriggered: root.repaintAll()
    }

    title: qsTr("Map")
    showDot: false
    padding: 8
    headerTools: [
        Text {
            anchors.verticalCenter: parent.verticalCenter
            readonly property var solar: decolog.solar.data
            visible: solar.valid === true
            text: qsTr("SFI %1 · K %2").arg(solar.solarFlux || 0).arg(solar.kIndex || 0)
            color: (solar.kIndex || 0) >= 4 ? Theme.errorColor
                 : (solar.kIndex || 0) >= 3 ? Theme.warningColor : Theme.accentColor
            font.family: Theme.monoFamily
            font.pixelSize: 11
        },
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: [decolog.callInfo.call, decolog.callInfo.gridsquare].filter(s => s).join(" · ")
            color: Theme.textSecondary
            font.family: Theme.monoFamily
            font.pixelSize: 11
        },
        GlassButton {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Layers ▾")
            buttonHeight: 22
            fontPixelSize: 10
            onClicked: layerMenu.popup()
        }
    ]

    StyledMenu {
        id: layerMenu
        // La carta compatibile rende le terre emerse come geometria vettoriale
        // statica: non richiede Canvas, ma puo' comunque mostrare o nascondere
        // i contorni della costa.
        StyledMenuItem {
            text: qsTr("Coastlines"); checkable: true; checked: root.showCoast
            onTriggered: { root.showCoast = checked; root.repaintBackground() }
        }
        StyledMenuItem {
            visible: !root.useSafeMap
            text: qsTr("Night"); checkable: true; checked: root.showNight
            onTriggered: { root.showNight = checked; root.repaintBackground() }
        }
        StyledMenuItem {
            text: qsTr("Worked grids"); checkable: true; checked: root.showGrids
            onTriggered: { root.showGrids = checked; root.repaintAll() }
        }
        StyledMenuItem {
            text: qsTr("Cluster spots"); checkable: true; checked: root.showSpots
            onTriggered: { root.showSpots = checked; root.reloadSpots() }
        }
        StyledMenuItem {
            text: qsTr("Antenna heading"); checkable: true; checked: root.showRotor
            enabled: decolog.rotor.enabled
            onTriggered: { root.showRotor = checked; root.repaintOverlay() }
        }
    }

    Connections {
        target: decolog.rotor
        function onStateChanged() {
            if (root.showRotor)
                root.repaintOverlay()
        }
    }
    Connections {
        target: decolog
        function onLogChanged() { root.repaintAll() }
        function onLookupChanged() { root.repaintOverlay() }
        function onStationChanged() { root.repaintAll() }
    }
    Connections {
        target: decolog.cluster.spots
        function onCountChanged() { root.scheduleSpots() }
    }
    Timer {
        interval: 120000
        running: root.showNight && !root.useSafeMap
        repeat: true
        onTriggered: root.repaintBackground()
    }

    Component.onCompleted: {
        // Sul renderer sicuro usiamo le terre emerse vettoriali. Niente Canvas
        // o FBO: la mappa resta compatibile con Mesa/KWin ma non sembra vuota.
        if (root.useSafeMap)
            root.land = decolog.landmasses()
        else
            root.coastline = decolog.coastline()
        root.reloadSpots()
        initialPaint.start()
    }

    Loader {
        id: mapRenderer
        anchors.fill: parent
        sourceComponent: root.useSafeMap ? safeMapComponent : canvasMapComponent
        onLoaded: root.repaintAll()
    }

    Component {
        id: safeMapComponent
        MapSafeView {
            showCoast: root.showCoast
            showGrids: root.showGrids
            showSpots: root.showSpots
            showRotor: root.showRotor
            target: root.target
            home: root.home
            grids: root.grids
            spots: root.spots
            land: root.land
        }
    }
    Component {
        id: canvasMapComponent
        MapCanvas {
            showCoast: root.showCoast
            showNight: root.showNight
            showGrids: root.showGrids
            showSpots: root.showSpots
            showRotor: root.showRotor
            target: root.target
            home: root.home
            grids: root.grids
            spots: root.spots
            coastline: root.coastline
        }
    }

    Text {
        anchors.centerIn: mapRenderer
        visible: root.grids.length === 0 && root.spots.length === 0
                 && (!root.home || root.home.lat === undefined)
        width: mapRenderer.width - 30
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        text: qsTr("QSOs with a grid square appear here")
        color: Theme.textSecondary
        font.pixelSize: 12
        z: 10
    }

    Row {
        anchors { left: mapRenderer.left; bottom: mapRenderer.bottom; margins: 6 }
        spacing: 10
        visible: root.spots.length > 0 || root.grids.length > 0
        z: 10
        Row {
            spacing: 4
            visible: root.grids.length > 0
            Rectangle { width: 6; height: 6; y: 4; color: Theme.secondaryColor }
            Text { text: qsTr("%1 grids").arg(root.grids.length); color: Theme.textSecondary; font.pixelSize: 10 }
        }
        Row {
            spacing: 4
            visible: root.spots.length > 0
            Rectangle { width: 6; height: 6; radius: 3; y: 4; color: Theme.accentColor }
            Text { text: qsTr("%1 spots").arg(root.spots.length); color: Theme.textSecondary; font.pixelSize: 10 }
        }
    }
}
