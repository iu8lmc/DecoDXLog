// DecoDXLog — il posto di comando DecoRotor, dentro DecoDXLog.
//
// E' la finestra del programma originale rifatta com'e': testata con le spie,
// le tre schede CONTROLLO / DIAGNOSTICA / IMPOSTAZIONI, e in fondo la striscia
// di stato. Il gateway e' lo stesso: qui cambia solo la finestra che lo mostra.
// Copia di `desktop/qml/Main.qml`.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtCore
import Decodium.UI as DUI

DUI.RoundedWindow {
    id: root

    property bool nightMode: true

    RotorPalette { id: rt; dark: root.nightMode }

    // Lo shack puo' avere un ultrawide scalato o un portatile: la finestra si
    // adatta allo spazio realmente disponibile invece di eccederlo.
    width: Math.min(1400, Screen.desktopAvailableWidth - 60)
    height: Math.min(900, Screen.desktopAvailableHeight - 40)
    minimumWidth: 940
    minimumHeight: 600
    visible: true
    title: qsTr("DecoRotor — PRO.SIS.TEL rotor control")
    surfaceColor: rt.bgDeep
    transientParent: null
    modality: Qt.NonModal

    OnScreen { target: root }

    Settings {
        id: rotorWindowSettings
        category: "rotorWindow"
        property alias width: root.width
        property alias height: root.height
        // Anche la posizione: se la finestra sta sul secondo schermo, e' li'
        // che deve riaprirsi.
        property alias windowX: root.x
        property alias windowY: root.y
        property alias nightMode: root.nightMode
    }

    function persistPlacement() { rotorWindowSettings.sync() }
    Component.onDestruction: root.persistPlacement()

    // Il quadrante chiaro o notturno e' una scelta che resta: alla riapertura
    // lo shack ritrova la luce che aveva.
    // Niente barra di Windows: sopra la barra del rotore c'e' la nostra testata,
    // piu' bassa. Si sposta dalla testata e si ridimensiona dai bordi.
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowMinimizeButtonHint | Qt.WindowMaximizeButtonHint
    header: Column {
        DUI.WindowTitleBar { window: root; width: parent.width; cornerRadius: root.frameRadius }
        RotorTopBar {
            width: parent.width
            nightMode: root.nightMode
            onLightToggled: root.nightMode = !root.nightMode
        }
    }
    DUI.WindowChrome {
        window: root
        parent: root.contentItem.parent
        dragHeight: DUI.Theme.panelHeight
        cornerRadius: root.frameRadius
    }

    footer: RotorStatus {
        nightMode: root.nightMode
        cornerRadius: root.frameRadius
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: rt.spacing
        spacing: rt.spacing

        TabBar {
            id: tabs

            Layout.fillWidth: true
            background: Item {}

            Repeater {
                model: [qsTr("CONTROL"), qsTr("DIAGNOSTICS"), qsTr("SETTINGS")]

                TabButton {
                    id: tab

                    required property string modelData

                    text: modelData
                    implicitHeight: 34
                    width: implicitWidth

                    background: Rectangle {
                        radius: 8
                        color: tab.checked ? Qt.rgba(0.22, 0.74, 0.97, 0.16) : "transparent"
                        border.color: tab.checked ? rt.primary : "transparent"
                        border.width: 1
                    }

                    contentItem: Text {
                        text: tab.text
                        color: tab.checked ? rt.primary : rt.textSecondary
                        font.pixelSize: rt.fontSmall
                        font.bold: true
                        font.letterSpacing: 1.2
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            RotorControlPage { nightMode: root.nightMode }

            RotorDiagnostics {}

            RotorSettings {}
        }
    }

    // Per le schermate di prova: --show rotor:window:1 apre la diagnostica.
    function showTab(index) {
        tabs.currentIndex = Math.max(0, Math.min(2, index))
    }
}
