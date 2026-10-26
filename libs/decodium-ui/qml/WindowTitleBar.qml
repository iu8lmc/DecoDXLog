// decodium-ui — la testata delle finestre grandi, al posto della barra di
// Windows: il titolo, e i tre comandi di sempre (riduci, ingrandisci, chiudi).
// Si sposta e si ingrandisce con WindowChrome, che le sta sopra.
import QtQuick
import QtQuick.Layouts
import Decodium.UI

Rectangle {
    id: root

    property var window: null
    // Il titolo della finestra senza il nome del programma davanti: si sa gia'
    // di stare in DecoDXLog, e cosi' c'e' posto per quello che conta.
    property string text: root.window ? String(root.window.title).replace(/^DecoDXLog\s*[—-]\s*/, "") : ""
    property color dotColor: Theme.primaryColor
    property bool minimizable: true
    property bool maximizable: true
    property real cornerRadius: 0
    property string info: ""
    property var closeAction: null
    // Le finestre staccate hanno una testata disegnata da noi. I tre controlli
    // restano identici su tutte le piattaforme: cosi' una finestra che passa
    // da macOS a Windows o Linux non cambia lato ne' simboli.
    readonly property bool trafficLightControls: true

    function closeWindow() {
        if (root.closeAction)
            root.closeAction()
        else if (root.window)
            root.window.close()
    }

    // Movimento esplicito in coordinate globali. Su alcune piattaforme
    // `startSystemMove()` non avvia il drag di una finestra frameless; usare
    // direttamente x/y mantiene lo stesso comportamento anche attraversando
    // il confine fra due monitor.
    property bool manualDragPending: false
    property bool manualDragActive: false
    property point manualDragAnchorGlobal: Qt.point(0, 0)
    property point manualDragAnchorWindow: Qt.point(0, 0)

    function beginManualDrag() {
        if (!root.window || root.window.visibility === Window.Maximized
                || root.window.visibility === Window.FullScreen)
            return
        root.manualDragPending = true
        root.manualDragActive = false
    }

    function moveManualDrag(globalX, globalY) {
        if (!root.window || (!root.manualDragPending && !root.manualDragActive))
            return
        if (root.manualDragPending) {
            root.manualDragAnchorGlobal = Qt.point(globalX, globalY)
            root.manualDragAnchorWindow = Qt.point(root.window.x, root.window.y)
            root.manualDragPending = false
            root.manualDragActive = true
            return
        }
        root.window.x = Math.round(root.manualDragAnchorWindow.x
                                   + globalX - root.manualDragAnchorGlobal.x)
        root.window.y = Math.round(root.manualDragAnchorWindow.y
                                   + globalY - root.manualDragAnchorGlobal.y)
    }

    function endManualDrag(globalX, globalY) {
        root.moveManualDrag(globalX, globalY)
        root.manualDragPending = false
        root.manualDragActive = false
    }

    implicitHeight: Theme.panelHeight
    color: "transparent"

    // Il primo rettangolo arrotonda solo gli angoli superiori. Quello sotto
    // riempie la fascia bassa, che deve restare piatta contro il contenuto.
    Rectangle {
        anchors.fill: parent
        radius: root.cornerRadius
        color: Theme.panelHeader
        antialiasing: root.cornerRadius > 0
    }
    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: root.cornerRadius
        color: Theme.panelHeader
    }

    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 1
        color: Theme.borderSoft
    }

    // Sta dietro ai controlli, quindi non ruba i clic sui semafori o sui
    // comandi; prende soltanto la parte libera della barra del titolo.
    MouseArea {
        anchors.fill: parent
        z: -1
        acceptedButtons: Qt.LeftButton
        onPressed: {
            const p = mapToGlobal(mouse.x, mouse.y)
            root.beginManualDrag()
            root.manualDragAnchorGlobal = p
            root.manualDragAnchorWindow = root.window
                    ? Qt.point(root.window.x, root.window.y) : Qt.point(0, 0)
            root.manualDragPending = false
            root.manualDragActive = !!root.window
        }
        onPositionChanged: {
            if (!pressed)
                return
            const p = mapToGlobal(mouse.x, mouse.y)
            root.moveManualDrag(p.x, p.y)
        }
        onReleased: {
            const p = mapToGlobal(mouse.x, mouse.y)
            root.endManualDrag(p.x, p.y)
        }
        onCanceled: {
            root.manualDragPending = false
            root.manualDragActive = false
        }
        onDoubleClicked: {
            if (root.window) {
                if (root.window.visibility === Window.Maximized)
                    root.window.showNormal()
                else
                    root.window.showMaximized()
            }
        }
    }

    component TrafficLight: Rectangle {
        id: trafficLight

        required property color lightColor
        required property string glyph
        required property string accessibleName
        signal clicked()

        implicitWidth: 14
        implicitHeight: 14
        radius: width / 2
        color: pointer.containsMouse ? Qt.lighter(lightColor, 1.08) : lightColor
        border.width: 1
        border.color: Qt.darker(lightColor, 1.25)
        antialiasing: true

        Text {
            anchors.centerIn: parent
            text: trafficLight.glyph
            color: "#4a2922"
            opacity: pointer.containsMouse ? 0.9 : 0.0
            font.pixelSize: 11
            font.bold: true
            verticalAlignment: Text.AlignVCenter
        }
        MouseArea {
            id: pointer
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            Accessible.name: trafficLight.accessibleName
            onClicked: trafficLight.clicked()
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: root.trafficLightControls ? 12 : 10
        anchors.rightMargin: 8
        spacing: 8

        Row {
            visible: root.trafficLightControls
            spacing: 7

            TrafficLight {
                lightColor: "#ff5f57"
                glyph: "×"
                accessibleName: qsTr("Close")
                onClicked: root.closeWindow()
            }
            TrafficLight {
                visible: root.minimizable
                lightColor: "#ffbd2e"
                glyph: "−"
                accessibleName: qsTr("Minimise")
                onClicked: if (root.window) root.window.showMinimized()
            }
            TrafficLight {
                visible: root.maximizable
                lightColor: "#28c840"
                glyph: "+"
                accessibleName: qsTr("Maximise or restore")
                onClicked: {
                    if (!root.window)
                        return
                    if (root.window.visibility === Window.Maximized)
                        root.window.showNormal()
                    else
                        root.window.showMaximized()
                }
            }
        }
        Rectangle {
            visible: !root.trafficLightControls
            implicitWidth: 8
            implicitHeight: 8
            radius: 4
            color: root.dotColor
        }
        Text {
            id: label
            text: root.text
            Layout.fillWidth: true
            elide: Text.ElideRight
            color: Theme.textPrimary
            font.family: Theme.monoFamily
            font.pixelSize: Theme.fontSize
            font.bold: true
        }
        Text {
            visible: root.info.length > 0
            text: root.info
            elide: Text.ElideRight
            Layout.maximumWidth: 260
            color: Theme.textSecondary
            font.family: Theme.monoFamily
            font.pixelSize: 11
        }
        PanelControl {
            visible: !root.trafficLightControls && root.minimizable
            glyph: "–"
            hint: qsTr("Minimise")
            onClicked: if (root.window) root.window.showMinimized()
        }
        PanelControl {
            visible: !root.trafficLightControls && root.maximizable
            glyph: root.window && root.window.visibility === Window.Maximized ? "❐" : "▢"
            hint: qsTr("Maximise or restore")
            onClicked: {
                if (!root.window)
                    return
                if (root.window.visibility === Window.Maximized)
                    root.window.showNormal()
                else
                    root.window.showMaximized()
            }
        }
        PanelControl {
            visible: !root.trafficLightControls
            glyph: "✕"
            hint: qsTr("Close")
            onClicked: root.closeWindow()
        }
    }
}
