// DecoDXLog — un pannello staccato: la stessa cosa che sta nella finestra
// principale, ma in una finestra sua, che si sposta su un altro monitor e si
// ridimensiona come si vuole. La misura e la posizione restano.
import QtQuick
import QtQuick.Controls
import QtCore
import Decodium.UI

RoundedWindow {
    id: root

    property string panelKey: ""
    property string panelTitle: ""
    property string panelSource: ""
    // Falso per i pannelli che vivono solo in finestra (quelli del contest):
    // il pulsante per riagganciare non si vede, perche' non hanno un posto
    // nella finestra principale.
    property bool dockable: true

    // Riagganciare alla finestra principale, oppure chiudere del tutto.
    signal attachRequested()
    signal closeRequested()
    // Quello che il pannello chiede e che solo la finestra principale sa fare.
    signal openQsoRequested(var id)
    signal awardRequested(string id)
    signal clusterRequested(int tab)
    signal statsRequested()
    signal rotorRequested()
    signal contestRequested()

    // Una misura di partenza sensata per quel pannello: il log e le schede
    // vogliono spazio, la scheda nominativo no. Poi vale quella che si sceglie.
    width: root.panelKey === "logbook" ? 1100 : root.panelKey === "tabs" ? 1000 : 760
    height: root.panelKey === "logbook" ? 700 : 520
    minimumWidth: 320
    minimumHeight: 200
    visible: true
    surfaceColor: Theme.bgDeep
    title: qsTr("DecoDXLog — %1").arg(root.panelTitle)

    // Una Window dichiarata dentro un'altra Window riceve automaticamente da
    // QML quella esterna come transientParent. Su macOS (e con alcuni window
    // manager Windows/Linux) questo la tiene legata allo schermo/desktop della
    // dashboard: sembra staccata, ma non lo e' per il sistema operativo.
    // I pannelli sono invece finestre di primo livello vere, spostabili su
    // qualunque monitor e indipendenti da minimizzazione e cambio desktop
    // della finestra principale.
    transientParent: null
    modality: Qt.NonModal

    // Davanti a tutte le altre. Il CW nasce cosi': mentre si manipola si guarda
    // quello che esce dal decoder e si scrive nel log, e la finestra del CW non
    // deve finire dietro a quella grande. Dalla puntina nella sua testata si
    // toglie, e resta tolta.
    property bool alwaysOnTop: false
    // In contest le finestre stanno davanti e non si riducono a icona: una
    // finestra sparita sotto quella grande, mentre passa la stazione, e' un QSO
    // perso. Il pulsante per ridurre non c'e' proprio, cosi' non ci si casca.
    property bool contestMode: false
    property bool pinned: true
    // Niente barra di Windows: il titolo e' gia' nella testata del pannello, e
    // la barra sopra era spazio perso. Si sposta dalla testata, si ridimensiona
    // dai bordi (WindowChrome qui sotto).
    flags: root.contestMode
           ? (Qt.Window | Qt.FramelessWindowHint
              | (root.pinned ? Qt.WindowStaysOnTopHint : 0))
           : (Qt.Window | Qt.FramelessWindowHint | Qt.WindowMinimizeButtonHint
              | (root.alwaysOnTop ? Qt.WindowStaysOnTopHint : 0))

    OnScreen { target: root }

    WindowChrome {
        window: root
        parent: root.contentItem.parent
        dragHeight: Theme.panelHeight + holder.anchors.margins + 2
        cornerRadius: root.frameRadius
    }

    Settings {
        id: panelSettings
        category: "layout/panel/" + root.panelKey
        property alias windowWidth: root.width
        property alias windowHeight: root.height
        property alias windowX: root.x
        property alias windowY: root.y
    }

    // Settings aggiorna gli alias mentre la finestra si muove; sync() rende il
    // punto finale durevole anche quando subito dopo si chiude tutto il
    // programma. Il timer evita una scrittura su disco per ogni pixel.
    property bool placementReady: false
    function schedulePlacementSave() {
        if (root.placementReady)
            placementSave.restart()
    }
    function persistPlacement() {
        placementSave.stop()
        panelSettings.sync()
    }
    onXChanged: root.schedulePlacementSave()
    onYChanged: root.schedulePlacementSave()
    onWidthChanged: root.schedulePlacementSave()
    onHeightChanged: root.schedulePlacementSave()

    Timer {
        id: placementSave
        interval: 300
        onTriggered: panelSettings.sync()
    }

    Component.onCompleted: {
        // Il .ini restituisce stringhe: "true" e true sono la stessa cosa.
        const saved = panelSettings.value("alwaysOnTop", root.panelKey === "cw")
        root.alwaysOnTop = saved === true || saved === "true"
        root.placementReady = true
    }
    Component.onDestruction: root.persistPlacement()
    onAlwaysOnTopChanged: panelSettings.setValue("alwaysOnTop", root.alwaysOnTop)

    QsoDetailDialog { id: qsoDialog }


    Loader {
        id: holder
        anchors.fill: parent
        // Quanto basta per prendere il bordo e ridimensionare: il resto e'
        // spazio per il pannello.
        anchors.margins: 3
        source: root.panelSource
        onLoaded: {
            if (item.panelKey !== undefined) {
                item.panelKey = root.panelKey
                item.detached = true
            }
            if (item.dockable !== undefined)
                item.dockable = root.dockable
            if (item.contestMode !== undefined)
                item.contestMode = root.contestMode
            // Il logbook ha il suo pulsante "Stacca": qui non serve piu'.
            if (item.showPopButton !== undefined)
                item.showPopButton = false
        }
    }

    // I segnali dei pannelli sono tanti e diversi: si ascolta quello che c'e'.
    Connections {
        target: holder.item
        ignoreUnknownSignals: true
        function onAttachRequested() { root.attachRequested() }
        function onCloseRequested() { root.closeRequested() }
        function onOpenQso(id) { if (id > 0) qsoDialog.openFor(id) }
        function onAwardRequested(id) { root.awardRequested(id) }
        function onDetailsRequested(id) { root.awardRequested(id) }
        function onStatsRequested() { root.statsRequested() }
        function onClusterRequested(tab) { root.clusterRequested(tab) }
        function onWindowRequested() { root.rotorRequested() }
        function onContestRequested() { root.contestRequested() }
        function onPopRequested() { }
    }
}
