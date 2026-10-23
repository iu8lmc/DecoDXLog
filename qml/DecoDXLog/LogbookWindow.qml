// DecoDXLog — il logbook in una finestra a parte ("Pop"), per un secondo monitor.
// Stesso modello della finestra principale: filtri e colonne restano in comune.
import QtQuick
import QtQuick.Controls
import QtCore
import Decodium.UI

RoundedWindow {
    id: root

    width: 1100
    height: 700
    minimumWidth: 760
    minimumHeight: 420
    visible: true
    title: qsTr("DecoDXLog — Logbook")
    surfaceColor: Theme.bgDeep
    transientParent: null
    modality: Qt.NonModal

    OnScreen { target: root }

    // Niente barra di Windows: la testata e' la nostra, piu' bassa, con gli
    // stessi comandi. Si sposta dalla testata e si ridimensiona dai bordi.
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowMinimizeButtonHint | Qt.WindowMaximizeButtonHint
    header: WindowTitleBar { window: root; cornerRadius: root.frameRadius }
    WindowChrome {
        window: root
        parent: root.contentItem.parent
        dragHeight: Theme.panelHeight
        cornerRadius: root.frameRadius
    }

    Settings {
        id: shared
        category: "layout"
        property alias popWidth: root.width
        property alias popHeight: root.height
        // Anche la posizione: se la finestra sta sul secondo schermo, e' li'
        // che deve riaprirsi.
        property alias popX: root.x
        property alias popY: root.y
    }

    QsoDetailDialog { id: qsoDialog }

    LogbookPanel {
        anchors.fill: parent
        anchors.margins: 8
        showPopButton: false
        onOpenQso: (id) => { if (id > 0) qsoDialog.openFor(id) }
    }
}
