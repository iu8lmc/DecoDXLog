// decodium-ui — la barra di scorrimento che si vede.
//
// Quella di serie di Qt sbiadisce appena si smette di toccarla: un pannello
// basso sembra allora che abbia mangiato la parte di sotto, e chi guarda non ha
// modo di sapere che sotto c'e' dell'altro. Questa resta li' finche' c'e'
// qualcosa da scorrere, e sparisce quando tutto ci sta.
//
//     ScrollView { ScrollBar.vertical: PanelScrollBar {} }
import QtQuick
import QtQuick.Controls
import Decodium.UI

ScrollBar {
    id: root

    // AsNeeded la fa comparire da sola quando c'e' da scorrere. La politica non
    // si lega a `size`: legarcela fa un anello — la barra compare, il contenuto
    // si stringe, size cambia, la barra sparisce — e il programma si pianta.
    policy: ScrollBar.AsNeeded
    minimumSize: 0.08
    // Piu' larga sotto il dito, ma il segno che si vede resta sottile.
    implicitWidth: 10
    implicitHeight: 10
    padding: 2

    // ScrollView's default style supplies this geometry on its own scrollbar.
    // Replacing it with this component otherwise leaves a 10x10 item at (0,0).
    // Flickable/ListView use the same viewport-relative geometry.
    x: parent && orientation === Qt.Vertical ? (mirrored ? 0 : parent.width - width) : 0
    y: parent && orientation === Qt.Horizontal ? parent.height - height : 0
    width: parent && orientation === Qt.Horizontal ? parent.width : implicitWidth
    height: parent && orientation === Qt.Vertical ? parent.height : implicitHeight
    enabled: size < 1 || policy === ScrollBar.AlwaysOn

    // Il pezzo che si muove: niente dissolvenza. Quella di serie sbiadisce
    // appena si smette di toccarla, ed e' proprio quello che fa sembrare un
    // pannello basso "tagliato" invece che "da scorrere".
    contentItem: Rectangle {
        implicitWidth: 6
        implicitHeight: 6
        radius: 3
        opacity: root.size < 1 || root.policy === ScrollBar.AlwaysOn ? 1 : 0
        color: root.pressed ? Theme.primaryColor
             : root.hovered ? Theme.textSecondary
             : Qt.rgba(Theme.textSecondary.r, Theme.textSecondary.g, Theme.textSecondary.b, 0.5)
    }

    background: Rectangle {
        radius: 3
        opacity: root.size < 1 || root.policy === ScrollBar.AlwaysOn ? 1 : 0
        color: Qt.rgba(Theme.borderSoft.r, Theme.borderSoft.g, Theme.borderSoft.b, 0.3)
    }
}
