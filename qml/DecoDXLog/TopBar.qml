// DecoDXLog — barra superiore: marchio, frequenza di Decodium, comandi, profilo
// stazione, stato del cloud, ricerca.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decodium.UI

Rectangle {
    id: root

    signal setupRequested()
    signal logsRequested()
    signal importRequested()
    signal exportRequested()
    signal exportPeriodRequested()
    signal spotRequested()
    signal awardsRequested()
    signal clusterRequested()
    signal chatRequested()
    signal trafficRequested()
    signal activationRequested()
    signal profilesRequested()
    // Il menu Pannelli e' un Popup nell'overlay della finestra principale:
    // passiamo chi l'ha aperto per posizionarlo accanto al comando, non sul
    // bordo destro della finestra.
    signal panelsRequested(var opener)
    signal aboutRequested()
    // Per le prove: apre il menu del marchio senza mouse.
    function openMainMenu() {
        Qt.callLater(function() { mainMenu.popup(brandBlock, 0, brandBlock.height + 4) })
    }
    // Idem per l'elenco dei modi, che di solito si apre cliccando la pillola.
    function openModeMenu() { modeMenu.popup(modePill, 0, modePill.height + 6) }
    signal logFolderRequested()
    signal quitRequested()
    // Quanti pannelli sono chiusi adesso: lo dice il pulsante, cosi' un pannello
    // sparito non e' un pannello perso.
    property int closedPanels: 0

    // Contest Mode: tutto quello che serve in gara sta in un menu solo, qui in
    // alto: la sessione, entrare e uscire, le finestre, le disposizioni, il
    // Cabrillo e l'invio del log. Il comando va alla finestra principale, che
    // e' l'unica che sa dove sono le finestre.
    signal contestCommand(string what, string arg)
    property bool contestModeOn: false
    property var contestOpenPanels: []
    function openSetupMenu() { setupMenu.popup(setupButton, 0, setupButton.height + 6) }
    function openContestMenu() { contestMenu.popup(contestButton, 0, contestButton.height + 6) }

    function focusSearch() {
        if (searchField.visible) {
            searchField.forceActiveFocus()
            searchField.selectAll()
        } else {
            searchPopup.open()
        }
    }

    readonly property var profiles: decolog.stationProfiles

    // Sugli schermi piccoli la barra va a capo: i riquadri scendono sulla riga
    // sotto invece di uscire dalla finestra, e la barra si alza con loro.
    implicitHeight: bar.implicitHeight + 16
    color: Theme.bgMedium

    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 1
        color: Theme.borderSoft
    }

    component Block: Rectangle {
        id: blockRoot
        default property alias content: blockRow.data
        property color outline: Theme.glassBorder
        property int hPadding: 12
        property alias spacing: blockRow.spacing
        // Un riquadro che si puo' premere tutto intero: serve al marchio, che
        // apre il menu. Il MouseArea sta qui e non fra il contenuto, perche'
        // li' dentro c'e' un layout e gli anchors non ci vanno.
        property bool clickable: false
        signal clicked()
        implicitHeight: 48
        implicitWidth: blockRow.implicitWidth + hPadding * 2
        radius: 6
        color: Theme.panelColor
        border.width: 1
        border.color: outline
        MouseArea {
            anchors.fill: parent
            enabled: blockRoot.clickable
            hoverEnabled: blockRoot.clickable
            // The layout row is declared after this handler and therefore is
            // above it in the scene graph. Keep the full-block hit target on
            // top when the block is meant to be clickable.
            z: blockRoot.clickable ? 10 : 0
            cursorShape: Qt.PointingHandCursor
            onClicked: blockRoot.clicked()
        }
        RowLayout {
            id: blockRow
            anchors.fill: parent
            anchors.leftMargin: parent.hPadding
            anchors.rightMargin: parent.hPadding
            spacing: 8
        }
    }

    // Un riquadro a larghezza fissa dentro la barra che va a capo: il Flow
    // guarda la larghezza vera, non quella che il riquadro vorrebbe.
    component FixedBlock: Block {
        width: implicitWidth
        height: implicitHeight
    }

    Flow {
        id: bar
        anchors { left: parent.left; right: parent.right; top: parent.top }
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        anchors.topMargin: 8
        spacing: 10

        // Marchio e versione.
        FixedBlock {
            id: brandBlock
            hPadding: 14

            // Tutto il riquadro del marchio apre il menu: le tre righette da
            // sole sono un bersaglio piccolo, e la gente ci clicca sopra il nome.
            clickable: true
            onClicked: {
                if (mainMenu.opened) {
                    mainMenu.close()
                } else {
                    // Popup.Window must be opened after the originating mouse
                    // event has finished, otherwise macOS can treat the same
                    // click as an outside click and close it immediately.
                    Qt.callLater(function() {
                        mainMenu.popup(brandBlock, 0, brandBlock.height + 4)
                    })
                }
            }

            StyledMenu {
                id: mainMenu
                StyledMenuItem { text: qsTr("About DecoDXLog…"); onTriggered: root.aboutRequested() }
                MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.borderSoft } }
                StyledMenuItem { text: qsTr("Settings…"); onTriggered: root.setupRequested() }
                // Open the theme menu explicitly. Qt treats a Menu child as a
                // hover submenu, but on macOS/Windows/Linux a click can close
                // the parent without showing that child when the menu uses a
                // custom Popup.Window delegate.
                StyledMenuItem {
                    text: qsTr("Appearance")
                    onTriggered: {
                        mainMenu.close()
                        // Menu closes the whole popup chain after onTriggered;
                        // defer the sibling popup until the parent is gone.
                        appearancePopupTimer.restart()
                    }
                }
                StyledMenuItem { text: qsTr("Station profiles…"); onTriggered: root.profilesRequested() }
                StyledMenuItem { text: qsTr("Panels…"); onTriggered: root.panelsRequested(brandBlock) }
                StyledMenuItem { text: qsTr("DX Cluster…"); onTriggered: root.clusterRequested() }
                StyledMenuItem { text: qsTr("Spot a station to the cluster…"); onTriggered: root.spotRequested() }
                StyledMenuItem {
                    text: decolog.chat.unread > 0 ? qsTr("ON4KST chat (%1)…").arg(decolog.chat.unread) : qsTr("ON4KST chat…")
                    onTriggered: root.chatRequested()
                }
                StyledMenuItem { text: qsTr("Decodium traffic monitor…"); onTriggered: root.trafficRequested() }
                MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.borderSoft } }
                StyledMenuItem { text: qsTr("Import a log (ADIF, CSV, N1MM)…"); onTriggered: root.importRequested() }
                StyledMenuItem { text: qsTr("Export ADIF…"); onTriggered: root.exportRequested() }
                StyledMenuItem { text: qsTr("Export a period (from… to…)…"); onTriggered: root.exportPeriodRequested() }
                StyledMenuItem { text: qsTr("Open the log folder"); onTriggered: root.logFolderRequested() }
                MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.borderSoft } }
                StyledMenuItem { text: qsTr("Quit"); onTriggered: root.quitRequested() }
            }

            // Kept outside mainMenu so it can be opened explicitly from the
            // Appearance action after the parent menu has closed.
            StyledMenu {
                id: appearanceMenu
                // Keep this popup in the main QML window. A second native
                // Popup.Window can lose focus while the parent menu closes on
                // macOS, making Appearance appear to do nothing.
                popupType: Popup.Item
                Repeater {
                    model: Theme.availableThemes
                    delegate: StyledMenuItem {
                        required property string modelData
                        text: modelData
                        checkable: true
                        checked: Theme.currentTheme === modelData
                        onTriggered: {
                            Theme.currentTheme = modelData
                            appearanceMenu.close()
                        }
                    }
                }
            }

            Timer {
                id: appearancePopupTimer
                interval: 50
                repeat: false
                onTriggered: appearanceMenu.popup(brandBlock, 0, brandBlock.height + 4)
            }

            Text {
                text: "≡"
                color: Theme.textSecondary
                font.family: Theme.monoFamily
                font.pixelSize: 22
            }
            Column {
                // Il marchio, con il DX acceso come sull'icona.
                Text {
                    textFormat: Text.StyledText
                    text: "DECO<font color=\"" + Theme.secondaryColor + "\">DX</font>LOG"
                    color: Theme.primaryColor
                    font.pixelSize: 15
                    font.weight: Font.ExtraBold
                    font.letterSpacing: 0.6
                }
                Text {
                    text: "v" + decolog.version
                    color: Theme.textSecondary
                    font.family: Theme.monoFamily
                    font.pixelSize: 11
                }
            }
        }

        // Frequenza e modo di Decodium: DecoDXLog non tocca il CAT, li legge dallo Status.
        FixedBlock {
            outline: decolog.clientConnected ? Theme.accentColor : Theme.glassBorder
            hPadding: 14
            Led {
                color: !decolog.listening ? Theme.errorColor
                     : decolog.transmitting ? Theme.warningColor
                     : decolog.clientConnected ? Theme.accentColor : Theme.textSecondary
                size: 10
                glow: decolog.clientConnected
                blinking: decolog.transmitting
            }
            VfoDisplay {
                id: vfo
                // La radio quando c'e', perche' e' lo stato vero; Decodium
                // quando la radio non c'e'. Chi clicca uno spot deve vedere il
                // display muoversi con il VFO.
                mhz: decolog.shownFrequency.length ? parseFloat(decolog.shownFrequency) : 0
                tunable: decolog.rig.connected || decolog.clientConnected
                textColor: decolog.clientConnected || decolog.rig.connected
                           ? Theme.accentColor : Theme.textSecondary
                onTuned: (freq) => decolog.tuneTo(freq, "")
            }
            Text {
                text: "MHz"
                color: Theme.textSecondary
                font.pixelSize: 12
            }
            // Il modo: si clicca e si sceglie, CW e fonia in cima, i digitali
            // sotto. Quello che si sceglie va alla radio e a Decodium.
            Pill {
                id: modePill
                readonly property string shown: decolog.shownMode
                visible: shown.length > 0 || vfo.tunable
                text: shown.length ? shown : "···"
                tone: Theme.primaryColor
                rounded: false
                pillHeight: 26
                fontPixelSize: 12
                interactive: vfo.tunable
                onClicked: modeMenu.opened ? modeMenu.close()
                                           : modeMenu.popup(modePill, 0, modePill.height + 6)

                StyledMenu {
                    id: modeMenu
                    // L'elenco non cambia mentre il programma e' aperto: si
                    // chiede una volta sola, se no l'Instantiator rifa' tutte
                    // le voci ogni volta che qualcuno lo guarda.
                    readonly property var entries: decolog.operatingModes()

                    Instantiator {
                        model: modeMenu.entries
                        delegate: StyledMenuItem {
                            required property var modelData
                            text: modelData.name
                            checkable: true
                            checked: modePill.shown === modelData.name
                            onTriggered: decolog.tuneTo(vfo.mhz, modelData.name)
                        }
                        onObjectAdded: (index, object) => modeMenu.insertItem(index, object)
                        onObjectRemoved: (index, object) => modeMenu.removeItem(object)
                    }
                }
            }
            // Split, VFO, RIT e XIT della radio: solo quello che il
            // collegamento sa fare (features: 1 split, 2 VFO, 4 RIT, 8 XIT).
            Pill {
                id: splitPill
                visible: decolog.rig.connected && (decolog.rig.features & 1)
                text: decolog.rig.split
                      ? "SPLIT " + (decolog.rig.txFrequencyHz > 0 ? (decolog.rig.txFrequencyHz / 1e6).toFixed(3) : "")
                      : "SPLIT"
                tone: decolog.rig.split ? Theme.warningColor : Theme.textSecondary
                rounded: false
                pillHeight: 26
                fontPixelSize: 11
                interactive: true
                onClicked: splitMenu.opened ? splitMenu.close() : splitMenu.popup(splitPill, 0, splitPill.height + 6)
                StyledMenu {
                    id: splitMenu
                    StyledMenuItem { text: qsTr("Split off"); enabled: decolog.rig.split; onTriggered: decolog.rig.setSplit(false, 0) }
                    Repeater {
                        model: [1000, 2000, 5000, 10000, -1000, -5000]
                        delegate: StyledMenuItem {
                            required property int modelData
                            text: modelData > 0 ? qsTr("TX up %1 kHz").arg(modelData / 1000)
                                                : qsTr("TX down %1 kHz").arg(-modelData / 1000)
                            onTriggered: decolog.rig.splitUp(modelData)
                        }
                    }
                }
            }
            Pill {
                visible: decolog.rig.connected && (decolog.rig.features & 2)
                text: decolog.rig.vfo === "VFOB" ? "VFO B" : "VFO A"
                tone: decolog.rig.vfo === "VFOB" ? Theme.warningColor : Theme.textSecondary
                rounded: false
                pillHeight: 26
                fontPixelSize: 11
                interactive: true
                onClicked: decolog.rig.swapVfo()
            }
            Pill {
                id: ritPill
                readonly property bool hasRit: (decolog.rig.features & 4) !== 0
                readonly property bool hasXit: (decolog.rig.features & 8) !== 0
                visible: decolog.rig.connected && (hasRit || hasXit)
                text: decolog.rig.ritHz !== 0 ? "RIT " + (decolog.rig.ritHz > 0 ? "+" : "") + decolog.rig.ritHz
                    : decolog.rig.xitHz !== 0 ? "XIT " + (decolog.rig.xitHz > 0 ? "+" : "") + decolog.rig.xitHz
                    : (hasRit ? "RIT" : "XIT")
                tone: decolog.rig.ritHz !== 0 || decolog.rig.xitHz !== 0 ? Theme.warningColor : Theme.textSecondary
                rounded: false
                pillHeight: 26
                fontPixelSize: 11
                interactive: true
                onClicked: ritMenu.opened ? ritMenu.close() : ritMenu.popup(ritPill, 0, ritPill.height + 6)
                StyledMenu {
                    id: ritMenu
                    StyledMenuItem {
                        text: qsTr("RIT and XIT off")
                        onTriggered: {
                            if (ritPill.hasRit) decolog.rig.setRit(0)
                            if (ritPill.hasXit) decolog.rig.setXit(0)
                        }
                    }
                    Repeater {
                        model: ritPill.hasRit ? [500, 200, 100, -100, -200, -500] : []
                        delegate: StyledMenuItem {
                            required property int modelData
                            text: "RIT " + (modelData > 0 ? "+" : "") + modelData + " Hz"
                            onTriggered: decolog.rig.setRit(decolog.rig.ritHz + modelData)
                        }
                    }
                    Repeater {
                        model: ritPill.hasXit ? [500, 200, 100, -100, -200, -500] : []
                        delegate: StyledMenuItem {
                            required property int modelData
                            text: "XIT " + (modelData > 0 ? "+" : "") + modelData + " Hz"
                            onTriggered: decolog.rig.setXit(decolog.rig.xitHz + modelData)
                        }
                    }
                }
            }
        }

        Rectangle {
            id: commands
            // In fila se c'e' posto, altrimenti i pulsanti vanno a capo dentro
            // il riquadro, che non esce mai dalla finestra.
            readonly property real rowWidth: {
                let w = 0
                for (let i = 0; i < commandFlow.children.length; ++i)
                    w += commandFlow.children[i].implicitWidth
                return w + commandFlow.spacing * Math.max(0, commandFlow.children.length - 1)
            }
            width: Math.min(rowWidth, bar.width - 20) + 20
            height: Math.max(48, commandFlow.implicitHeight + 18)
            radius: 6
            color: Theme.panelColor
            border.width: 1
            border.color: Theme.glassBorder

            Flow {
                id: commandFlow
                anchors.fill: parent
                anchors.margins: 10
                anchors.topMargin: 9
                spacing: 6
            // Impostazioni e stazione insieme: il pulsante dice con quale
            // profilo si scrivono i QSO, e il menu apre le impostazioni o
            // cambia profilo. Prima la stazione aveva un riquadro suo.
            GlassButton {
                id: setupButton
                readonly property string station: root.profiles.activeProfile.name || ""
                text: station.length > 0 ? qsTr("Setup · %1 ▾").arg(station) : qsTr("Setup ▾")
                tone: Theme.primaryColor
                filled: true
                onClicked: setupMenu.opened ? setupMenu.close()
                                            : setupMenu.popup(setupButton, 0, setupButton.height + 6)

                StyledMenu {
                    id: setupMenu
                    StyledMenuItem { text: qsTr("Settings…"); onTriggered: root.setupRequested() }
                    MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.borderSoft } }
                    StyledMenuItem { text: qsTr("Station"); enabled: false }
                    // I profili: spuntato quello con cui si scrive adesso.
                    Instantiator {
                        model: root.profiles
                        delegate: StyledMenuItem {
                            required property int index
                            required property string name
                            required property var profileId
                            required property bool deleted
                            text: name
                            visible: !deleted
                            height: deleted ? 0 : implicitHeight
                            checkable: true
                            checked: root.profiles.activeProfileId === profileId
                            onTriggered: root.profiles.activeProfileId = profileId
                        }
                        onObjectAdded: (index, object) => setupMenu.insertItem(3 + index, object)
                        onObjectRemoved: (index, object) => setupMenu.removeItem(object)
                    }
                    MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.borderSoft } }
                    StyledMenuItem { text: qsTr("Station profiles…"); onTriggered: root.profilesRequested() }
                }
            }
            // I log della stazione: quello di sempre e quelli dei contest.
            GlassButton { text: qsTr("Logs"); onClicked: root.logsRequested() }
            GlassButton { text: qsTr("Import"); onClicked: root.importRequested() }
            GlassButton { text: qsTr("Export"); onClicked: root.exportRequested() }
            GlassButton { text: qsTr("Awards"); onClicked: root.awardsRequested() }
            GlassButton {
                id: contestButton
                readonly property var act: decolog.activation
                // La sessione in corto: il nome della gara senza "Contest"
                // davanti, e i QSO — su quanti ne servono, per le attivazioni.
                readonly property string shortTitle: String(act.state.title || "").replace(/^Contest /, "")
                text: !act.active ? qsTr("Contest Mode ▾")
                      : act.requiredQsos > 0
                        ? qsTr("Contest Mode · %1 · %2/%3 ▾").arg(shortTitle).arg(act.qsoCount).arg(act.requiredQsos)
                        : qsTr("Contest Mode · %1 · %2 QSO ▾").arg(shortTitle).arg(act.qsoCount)
                tone: root.contestModeOn ? Theme.accentColor
                      : !act.active ? "transparent"
                      : act.requiredQsos > 0 && act.qsoCount < act.requiredQsos ? Theme.warningColor : Theme.primaryColor
                filled: root.contestModeOn
                onClicked: contestMenu.opened ? contestMenu.close() : root.openContestMenu()

                StyledMenu {
                    id: contestMenu
                    readonly property var act: decolog.activation
                    property var scoring: ({})
                    // Il conto si chiede quando il menu si apre, non a ogni QSO.
                    onAboutToShow: {
                        contestMenu.scoring = act.active ? act.score() : ({})
                        scoreLine.text = act.active
                            ? (contestMenu.scoring.valid
                               ? qsTr("%1 · %2 QSO · %3 points · %4 mult").arg(act.state.title || "")
                                     .arg(act.qsoCount).arg(contestMenu.scoring.points || 0)
                                     .arg(contestMenu.scoring.multipliers || 0)
                               : qsTr("%1 · %2 QSO").arg(act.state.title || "").arg(act.qsoCount))
                            : qsTr("No session open")
                    }

                    function isOpen(key) { return root.contestOpenPanels.indexOf(key) >= 0 }

                    // Com'e' andata finora, in una riga: non si clicca.
                    StyledMenuItem { id: scoreLine; enabled: false }
                    StyledMenuItem {
                        text: qsTr("Contest and activations…")
                        onTriggered: root.contestCommand("session", "")
                    }
                    MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.borderSoft } }
                    StyledMenuItem {
                        text: root.contestModeOn ? qsTr("Leave contest mode") : qsTr("Enter contest mode")
                        // Si entra con una sessione aperta: senza, le finestre
                        // della gara non avrebbero niente da mostrare.
                        enabled: root.contestModeOn || contestMenu.act.active
                        onTriggered: root.contestCommand(root.contestModeOn ? "exit" : "enter", "")
                    }
                    MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.borderSoft } }
                    // Le finestre della gara: spuntata vuol dire aperta.
                    Instantiator {
                        model: [
                            { key: "contest", label: qsTr("QSO entry") },
                            { key: "cluster", label: qsTr("Cluster") },
                            { key: "logbook", label: qsTr("Logbook") },
                            { key: "callinfo", label: qsTr("Callsign card") },
                            { key: "rate", label: qsTr("Rate") },
                            { key: "score", label: qsTr("Score") },
                            { key: "map", label: qsTr("Map") },
                            { key: "dxcalendar", label: qsTr("DXpedition calendar") },
                            { key: "cw", label: qsTr("CW") }
                        ]
                        delegate: StyledMenuItem {
                            required property var modelData
                            text: modelData.label
                            checkable: true
                            checked: contestMenu.isOpen(modelData.key)
                            enabled: root.contestModeOn
                            onTriggered: root.contestCommand("toggle", modelData.key)
                        }
                        onObjectAdded: (index, object) => contestMenu.insertItem(5 + index, object)
                        onObjectRemoved: (index, object) => contestMenu.removeItem(object)
                    }
                    MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.borderSoft } }
                    StyledMenuItem {
                        // I pannelli e le misure di partenza, per chi ha
                        // trascinato troppo.
                        text: qsTr("Reset the layout")
                        enabled: root.contestModeOn
                        onTriggered: root.contestCommand("reset", "")
                    }
                    MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.borderSoft } }
                    StyledMenuItem {
                        text: qsTr("Cabrillo…")
                        onTriggered: root.contestCommand("export", "")
                    }
                    StyledMenuItem {
                        // Ogni contest ha il suo posto dove si manda il log: il
                        // programma ci porta, con il Cabrillo gia' scritto.
                        text: qsTr("Send the log…")
                        enabled: contestMenu.scoring.valid === true
                        onTriggered: root.contestCommand("submit", "")
                    }
                }
            }
            GlassButton {
                id: panelsButton
                text: root.closedPanels > 0 ? qsTr("Panels (%1 closed)").arg(root.closedPanels) : qsTr("Panels")
                tone: root.closedPanels > 0 ? Theme.warningColor : "transparent"
                onClicked: root.panelsRequested(panelsButton)
            }
            }
        }

        // Niente vuoto in mezzo: cloud e ricerca stanno subito dopo i comandi.
        // Spinti a destra lasciavano una fascia nera fra un riquadro e l'altro.
        // DecoDXLog Cloud: come sta il collegamento e cosa aspetta di partire.
        FixedBlock {
            Led {
                color: decolog.cloud.busy ? Theme.primaryColor
                     : decolog.cloud.linked ? Theme.accentColor
                     : decolog.cloud.server.length ? Theme.warningColor : Theme.textSecondary
            }
            Column {
                Text {
                    text: decolog.cloud.linked ? qsTr("Cloud %1").arg(decolog.cloud.callsign)
                        : decolog.cloud.server.length ? qsTr("Cloud not linked")
                        : qsTr("Cloud not configured")
                    color: Theme.textPrimary
                    font.family: Theme.monoFamily
                    font.pixelSize: 12
                    font.bold: true
                }
                Text {
                    text: decolog.cloud.busy ? qsTr("syncing…")
                          + (decolog.cloud.progress >= 0 ? " " + Math.round(decolog.cloud.progress * 100) + "%" : "")
                        : decolog.cloud.queued > 0 ? qsTr("%1 queued").arg(decolog.cloud.queued)
                        : decolog.cloud.lastSync.length ? qsTr("synced %1").arg(decolog.cloud.lastSync)
                        : qsTr("%1 queued").arg(decolog.dirtyCount)
                    color: decolog.cloud.queued > 0 ? Theme.warningColor : Theme.textSecondary
                    font.family: Theme.monoFamily
                    font.pixelSize: 11
                }
                // La barra sotto lo stato: riempita quanto e' salito del log.
                ProgressBar {
                    visible: decolog.cloud.busy && decolog.cloud.progress >= 0
                    width: parent.width
                    height: 4
                    from: 0
                    to: 1
                    value: Math.max(0, decolog.cloud.progress)
                }
            }
            // Solo l'icona: lo stato e' gia' scritto accanto, e la barra
            // cosi' sta su una riga anche su uno schermo normale.
            GlassButton {
                text: "⟳"
                tone: Theme.primaryColor
                filled: true
                minimumWidth: 30
                implicitWidth: 30
                fontPixelSize: 14
                enabled: decolog.cloud.linked && !decolog.cloud.busy
                onClicked: decolog.cloud.syncNow()
                ToolTip.visible: hovered
                ToolTip.text: decolog.cloud.linked ? qsTr("Sync now") : qsTr("Sign in from Setup → Sync & Cloud")
            }
        }

        // La ricerca non va mai a capo: sta in fondo alla riga degli altri
        // blocchi, lunga al massimo 220, e prende solo lo spazio che resta su
        // quella riga. Se non ce n'e' abbastanza per il campo resta la lente,
        // che apre il campo sotto la barra. Lo spazio si conta come fa il Flow,
        // riga per riga, cosi' torna anche quando la barra ne ha due.
        Block {
            id: searchBlock
            hPadding: 8
            spacing: 6
            readonly property real room: {
                let x = 0
                for (let i = 0; i < bar.children.length; ++i) {
                    const c = bar.children[i]
                    if (c === searchBlock || !c.visible)
                        continue
                    if (x > 0 && x + c.width > bar.width)
                        x = 0
                    x += c.width + bar.spacing
                }
                return Math.floor(bar.width - x) - 1
            }
            readonly property bool compact: room < 150
            width: compact ? 48 : Math.min(220, room)
            height: implicitHeight
            StyledTextField {
                id: searchField
                visible: !searchBlock.compact
                Layout.fillWidth: true
                placeholderText: qsTr("Search…")
                text: decolog.qsoModel.filterText
                onTextEdited: decolog.qsoModel.filterText = text
                Keys.onEscapePressed: { text = ""; decolog.qsoModel.filterText = "" }
            }
            GlassButton {
                text: "⌕"
                // Con un filtro attivo e il campo nascosto la lente si accende.
                tone: searchBlock.compact && decolog.qsoModel.filterText.length > 0 ? Theme.warningColor
                                                                                  : Theme.secondaryColor
                filled: searchBlock.compact && decolog.qsoModel.filterText.length > 0
                minimumWidth: 30
                implicitWidth: 30
                buttonHeight: 30
                fontPixelSize: 14
                ToolTip.visible: hovered && searchBlock.compact
                ToolTip.text: decolog.qsoModel.filterText.length > 0
                              ? qsTr("Search: %1").arg(decolog.qsoModel.filterText) : qsTr("Search… (Ctrl+F)")
                onClicked: root.focusSearch()
            }

            // Il campo quando in riga non c'e' posto: si apre sotto la lente.
            Popup {
                id: searchPopup
                y: searchBlock.height + 4
                x: parent ? parent.width - width : 0
                width: 280
                padding: 8
                focus: true
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                background: Rectangle { color: Theme.panelColor; border.color: Theme.glassBorder; radius: 6 }
                onOpened: { popupSearchField.forceActiveFocus(); popupSearchField.selectAll() }
                contentItem: StyledTextField {
                    id: popupSearchField
                    placeholderText: qsTr("Search…")
                    text: decolog.qsoModel.filterText
                    onTextEdited: decolog.qsoModel.filterText = text
                    Keys.onEscapePressed: { text = ""; decolog.qsoModel.filterText = ""; searchPopup.close() }
                    Keys.onReturnPressed: searchPopup.close()
                    Keys.onEnterPressed: searchPopup.close()
                }
            }
        }
    }
}
