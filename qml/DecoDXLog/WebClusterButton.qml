// DecoDXLog — il web cluster nella barra in basso: un globo, e un clic apre nel
// browser decowebcluster.ft2.it. Il simbolo e' disegnato, non emoji: si vede uguale
// dappertutto, come le altre icone del programma.
import QtQuick
import QtQuick.Controls
import QtQuick.Shapes
import Decodium.UI

AbstractButton {
    id: root

    readonly property string address: "https://decowebcluster.ft2.it"
    readonly property color tint: root.hovered || root.visualFocus ? Theme.accentColor : Theme.secondaryColor

    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Button
    Accessible.name: qsTr("Open the web cluster")
    implicitWidth: 42
    implicitHeight: 42
    onClicked: Qt.openUrlExternally(root.address)

    ToolTip.visible: root.hovered
    ToolTip.delay: 400
    ToolTip.text: qsTr("Web cluster — decowebcluster.ft2.it")

    background: Rectangle {
        radius: 8
        color: root.hovered || root.visualFocus ? Theme.panelHeader : Theme.bgDeep
        border.color: root.visualFocus ? Theme.secondaryColor : Theme.borderColor
        border.width: root.visualFocus ? 2 : 1
    }

    // Il globo: il cerchio, un meridiano, l'equatore e due paralleli.
    contentItem: Shape {
        id: globe
        readonly property real s: 24
        readonly property real c: s / 2
        readonly property real r: s * 0.44
        implicitWidth: s
        implicitHeight: s
        preferredRendererType: Shape.CurveRenderer

        component Stroke: ShapePath {
            strokeColor: root.tint
            strokeWidth: 1.6
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
        }

        // Il cerchio.
        Stroke {
            startX: globe.c + globe.r; startY: globe.c
            PathArc { x: globe.c - globe.r; y: globe.c; radiusX: globe.r; radiusY: globe.r }
            PathArc { x: globe.c + globe.r; y: globe.c; radiusX: globe.r; radiusY: globe.r }
        }
        // Il meridiano: un'ellisse stretta, dal polo al polo.
        Stroke {
            startX: globe.c; startY: globe.c - globe.r
            PathArc { x: globe.c; y: globe.c + globe.r; radiusX: globe.s * 0.2; radiusY: globe.r }
            PathArc { x: globe.c; y: globe.c - globe.r; radiusX: globe.s * 0.2; radiusY: globe.r }
        }
        // L'equatore e i due paralleli, lunghi quanto il cerchio a quell'altezza.
        Stroke {
            startX: globe.c - globe.r; startY: globe.c
            PathLine { x: globe.c + globe.r; y: globe.c }
            PathMove { x: globe.c - globe.r * 0.86; y: globe.c - globe.r * 0.5 }
            PathLine { x: globe.c + globe.r * 0.86; y: globe.c - globe.r * 0.5 }
            PathMove { x: globe.c - globe.r * 0.86; y: globe.c + globe.r * 0.5 }
            PathLine { x: globe.c + globe.r * 0.86; y: globe.c + globe.r * 0.5 }
        }
    }
}
