// decodium-ui — la cornice delle finestre di dialogo.
//
// E' una finestra vera, non un riquadro incollato in mezzo al programma: si
// trascina dove si vuole, anche su un secondo schermo, si ridimensiona, e la
// prossima volta si riapre dove l'avevi lasciata. La testata resta quella dei
// pannelli, con l'informazione a destra e la ✕ per chiudere.
import QtQuick
import QtQuick.Controls
import QtCore
import Decodium.UI

RoundedWindow {
    id: root

    property color dotColor: Theme.primaryColor
    property string info: ""
    // Il contenuto della finestra, assegnato come `body: ColumnLayout { … }`.
    property Item body: null
    // Con che nome si ricordano misura e posizione. Vuoto: non si ricordano.
    property string dialogKey: ""

    // Gli stessi segnali di prima, cosi' chi li ascoltava continua a funzionare.
    signal opened()
    signal closed()
    signal accepted()
    signal rejected()

    function open() {
        if (!root.visible) {
            root.placeOnce()
            root.show()
        }
        root.raise()
        root.requestActivate()
        root.opened()
    }
    function accept() { root.accepted(); root.close() }
    function reject() { root.rejected(); root.close() }

    // La prima volta la finestra si mette in mezzo a quella che l'ha aperta —
    // sullo schermo giusto, quindi, anche quando sono due.
    property bool placed: false
    function placeOnce() {
        if (root.placed)
            return
        root.placed = true
        if (root.x !== 0 || root.y !== 0)
            return
        const owner = root.transientParent
        if (!owner)
            return
        root.x = owner.x + Math.max(0, (owner.width - root.width) / 2)
        root.y = owner.y + Math.max(0, (owner.height - root.height) / 2)
    }

    width: 960
    height: 640
    minimumWidth: 380
    minimumHeight: 260
    visible: false
    surfaceColor: Theme.bgDeep
    readableSurface: true
    // Una finestra modale deve privilegiare la lettura: il vetro resta nella
    // cornice e nella testata, ma il contenuto non lascia filtrare la
    // dashboard sottostante. Il 97% conserva appena la profondita' del tema.
    surfaceOpacity: Theme.isGlassTheme ? 0.97 : 1.0
    // Una finestra di dialogo: sta sopra a quella che l'ha aperta, ma si sposta
    // dove si vuole — anche su un altro schermo — si ingrandisce, si riduce a
    // icona e si chiude come tutte le altre.
    // La barra di Windows no: il titolo sta gia' nella testata qui sotto. Dalla
    // testata si sposta, doppio clic la ingrandisce, dai bordi si ridimensiona.
    flags: Qt.Dialog | Qt.FramelessWindowHint | Qt.WindowMinimizeButtonHint

    WindowChrome {
        window: root
        parent: root.contentItem.parent
        dragHeight: Theme.panelHeight
        cornerRadius: root.frameRadius
    }

    onVisibleChanged: if (!visible) root.closed()

    Settings {
        category: "layout/dialog/" + (root.dialogKey.length > 0 ? root.dialogKey : "generic")
        property alias dialogWidth: root.width
        property alias dialogHeight: root.height
        property alias dialogX: root.x
        property alias dialogY: root.y
    }

    header: WindowTitleBar {
        window: root
        dotColor: root.dotColor
        info: root.info
        minimizable: false
        maximizable: false
        cornerRadius: root.frameRadius
        closeAction: function() { root.reject() }
    }

    // Esc chiude, come faceva quando era un riquadro.
    Shortcut {
        sequences: [StandardKey.Cancel]
        onActivated: root.reject()
    }

    Item {
        id: holder
        anchors.fill: parent
    }

    onBodyChanged: {
        if (!root.body)
            return
        root.body.parent = holder
        root.body.anchors.fill = holder
    }
}
