// DecoDXLog — la finestra principale (mockup 1a).
//
// La stessa grammatica di Decodium: barra superiore a blocchi, pannelli su
// SplitView ridimensionabili con le misure che restano da una sessione
// all'altra, barra di stato con le pillole dei servizi.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore
import Decodium.UI

ApplicationWindow {
    id: window

    width: 1440
    height: 900
    // Su uno schermo piccolo la finestra non puo' chiedere piu' di quello che c'e':
    // la barra in alto va a capo, e il resto si stringe.
    minimumWidth: Math.min(1100, Screen.desktopAvailableWidth > 0 ? Screen.desktopAvailableWidth : 1100)
    minimumHeight: 640
    visible: true
    title: "DecoDXLog " + decolog.version
           + (decolog.stationProfiles.activeProfile.stationCallsign
              ? " — " + decolog.stationProfiles.activeProfile.stationCallsign : "")
    color: Theme.bgDeep
    font.pixelSize: Theme.fontSize

    // La X della finestra principale chiude DecoDXLog per davvero. Senza questo,
    // con un pannello in finestra propria il programma restava in piedi: Qt
    // aspetta che si chiuda l'ultima finestra, e quella staccata era ancora li'.
    onClosing: {
        window.quitting = true
        Qt.quit()
    }

    // Le impostazioni arrivate da un altro computer valgono subito: il tema si
    // ridipinge senza aspettare il riavvio.
    Connections {
        target: decolog.cloud
        function onSettingsApplied() { Theme.reload() }
    }

    // Se lo schermo dove stava non c'e' piu', si torna su questo.
    OnScreen { target: window }

    Settings {
        id: layout
        category: "layout"
        property alias windowWidth: window.width
        property alias windowHeight: window.height
        // Anche dove sta, non solo quanto e' grande: chi lavora con due monitor
        // mette DecoDXLog su quello di destra e vuole ritrovarlo li'. Le
        // finestre staccate la posizione se la ricordavano da sempre; questa,
        // che e' la principale, no — e si riapriva dove decideva Windows.
        property alias windowX: window.x
        property alias windowY: window.y
        property real leftWidth: 300
        property real rightWidth: 300
        property real bottomHeight: 280
        // Il cluster tiene un'altezza sua: chi guarda gli spot vuole la fascia
        // alta, chi guarda il registro la vuole bassa, e nessuno dei due deve
        // rifarla ogni volta che cambia scheda.
        property real clusterBottomHeight: 340
        property real mapWidth: 308
        // I pannelli chiusi e quelli in finestra propria, come liste di chiavi
        // separate da virgola. Restano da una sessione all'altra.
        property string hiddenPanels: "cw"
        property string detachedPanels: ""
        // La modalita' contest, e com'era la finestra principale prima di
        // entrarci: all'uscita si rimette tutto uguale.
        property bool contestModeOn: false
        property string preContestHidden: ""
        property string preContestDetached: ""
        property int preContestVisibility: 2
        // Disposizione bloccata: le maniglie non si tirano e i pannelli non si
        // spostano. Si mette e si toglie col tasto destro sulla testata di un
        // pannello qualsiasi.
        property bool layoutLocked: false
        // Chi sta in quale casella: "casella=pannello", separati da virgola.
        // Vuoto vuol dire la disposizione di partenza.
        property string panelSlots: ""
    }

    readonly property real defaultLeftWidth: 300
    readonly property real defaultRightWidth: 300
    readonly property real defaultBottomHeight: 280
    readonly property real defaultClusterBottomHeight: 340
    readonly property real defaultMapWidth: 308

    // ── I pannelli: chi sono, dove stanno ───────────────────────────────────
    //
    // Ogni pannello ha una chiave. Con quella si sa come si chiama, da quale
    // file nasce quando lo si stacca, e se adesso e' agganciato, in finestra o
    // chiuso. Chiuso vuol dire chiuso davvero: lo spazio non resta vuoto.
    readonly property var panelKeys: ["newqso", "logbook", "callinfo", "cw", "rotor", "ft2", "tabs", "map",
                                      "contest", "score", "rate", "cluster"]
    // Gli ultimi quattro vivono solo in finestra: nel contest ognuno se li
    // mette dove vuole, e nella disposizione agganciata non hanno un posto.
    readonly property var windowOnlyPanels: ["contest", "score", "rate", "cluster"]
    function isWindowOnly(key) { return window.windowOnlyPanels.indexOf(key) >= 0 }

    function panelTitle(key) {
        switch (key) {
        case "newqso":   return qsTr("New QSO")
        case "logbook":  return qsTr("Logbook")
        case "callinfo": return qsTr("Callsign card")
        case "cw":       return qsTr("CW")
        case "rotor":    return qsTr("Rotator")
        case "ft2":      return qsTr("FT2 Award")
        case "tabs":     return qsTr("Awards, statistics, QSL, activity")
        case "map":      return qsTr("Map")
        case "contest":  return qsTr("Contest entry")
        case "score":    return qsTr("Score and multipliers")
        case "rate":     return qsTr("How it is going")
        case "cluster":  return qsTr("DX Cluster")
        }
        return key
    }
    function panelSource(key) {
        switch (key) {
        case "newqso":   return "NewQsoPanel.qml"
        case "logbook":  return "LogbookPanel.qml"
        case "callinfo": return "CallInfoPanel.qml"
        case "cw":       return "CwPanel.qml"
        case "rotor":    return "RotorPanel.qml"
        case "ft2":      return "Ft2AwardPanel.qml"
        case "tabs":     return "BottomTabs.qml"
        case "map":      return "MapPanel.qml"
        case "contest":  return "ContestEntryPanel.qml"
        case "score":    return "ContestScorePanel.qml"
        case "rate":     return "ContestRatePanel.qml"
        case "cluster":  return "ClusterPanel.qml"
        }
        return ""
    }

    // ── La lavagna ──────────────────────────────────────────────────────────
    //
    // Fuori dalla gara la finestra e' una lavagna magnetica come quella della
    // gara (mainBoard): ogni pannello si sposta per la testata, si
    // ridimensiona dai bordi, si attacca ai bordi vicini, si stacca e si
    // chiude. Chi e' chiuso o staccato lo tengono hiddenPanels e
    // detachedPanels, come prima; posizioni e misure la lavagna.
    readonly property var boardKeys: ["newqso", "logbook", "callinfo", "cw", "rotor", "ft2", "tabs", "map"]

    // Il pannello sulla lavagna, con la stessa faccia delle caselle di prima
    // (item, currentTab, setTab, showMenu…).
    function panelItem(key) { return mainBoard.panelFor(key) }
    readonly property int tabsTab: {
        const it = window.panelItem("tabs")
        return it ? it.currentTab : -1
    }

    function panelListOf(text) {
        const out = []
        const parts = String(text || "").split(",")
        for (let i = 0; i < parts.length; ++i) {
            const k = parts[i].trim()
            if (k.length > 0 && window.panelKeys.indexOf(k) >= 0 && out.indexOf(k) < 0)
                out.push(k)
        }
        return out
    }
    readonly property var hiddenPanels: window.panelListOf(layout.hiddenPanels)
    readonly property var detachedPanels: window.panelListOf(layout.detachedPanels)

    function isPanelHidden(key) { return window.hiddenPanels.indexOf(key) >= 0 }
    function isPanelDetached(key) { return window.detachedPanels.indexOf(key) >= 0 }
    function isPanelDocked(key) { return !window.isPanelHidden(key) && !window.isPanelDetached(key) }
    function panelShows(key) {
        return window.isPanelDocked(key)
    }
    function panelState(key) {
        return window.isPanelHidden(key) ? qsTr("closed")
             : window.isPanelDetached(key) ? qsTr("window") : qsTr("docked")
    }

    // Queste quattro leggono sempre layout.hiddenPanels / layout.detachedPanels,
    // mai le liste calcolate qui sopra: una property che dipende da un'altra si
    // rifa' quando le pare, e due chiamate di fila — stacca questo, stacca
    // quello — leggevano ancora la lista di prima e si cancellavano a vicenda.
    // Il pannello staccato per primo spariva dall'elenco con la finestra
    // ancora aperta, e da li' venivano le finestre orfane.
    function showPanel(key) {
        // Riaperto, sta davanti agli altri.
        if (window.boardKeys.indexOf(key) >= 0)
            mainBoard.raiseKey(key)
        layout.hiddenPanels = window.panelListOf(layout.hiddenPanels)
                                    .filter(function (k) { return k !== key }).join(",")
    }
    function closePanel(key) {
        // Chiuso e' chiuso: se era in finestra, la finestra sparisce.
        layout.detachedPanels = window.panelListOf(layout.detachedPanels)
                                      .filter(function (k) { return k !== key }).join(",")
        if (key === "rotor")
            rotorWindow.active = false
        const hidden = window.panelListOf(layout.hiddenPanels)
        if (hidden.indexOf(key) < 0)
            layout.hiddenPanels = hidden.concat([key]).join(",")
    }
    function detachPanel(key) {
        window.showPanel(key)
        const detached = window.panelListOf(layout.detachedPanels)
        if (detached.indexOf(key) < 0)
            layout.detachedPanels = detached.concat([key]).join(",")
        if (key === "rotor")
            window.openRotor()
    }
    function attachPanel(key) {
        layout.detachedPanels = window.panelListOf(layout.detachedPanels)
                                      .filter(function (k) { return k !== key }).join(",")
        if (key === "rotor")
            rotorWindow.active = false
        window.showPanel(key)
    }
    function togglePanel(key) {
        if (window.isPanelHidden(key)) window.showPanel(key)
        else window.closePanel(key)
    }
    function resetPanels() {
        layout.hiddenPanels = ""
        layout.detachedPanels = ""
        layout.layoutLocked = false
        mainBoard.resetLayout()
        window.syncDetachedWindows()
    }

    // ── Azioni comuni a barra, scorciatoie e pannelli ───────────────────────
    function openQso(id) {
        if (id > 0)
            qsoDialog.openFor(id)
    }
    function focusSearch() { topBar.focusSearch() }
    function openStats() {
        statsWindow.active = true
        if (statsWindow.item) {
            statsWindow.item.raise()
            statsWindow.item.requestActivate()
        }
    }
    function openActivation() { activationDialog.openDialog() }
    function openContest() {
        contestWindow.active = true
        if (contestWindow.item) {
            contestWindow.item.raise()
            contestWindow.item.requestActivate()
        }
    }
    // La modalita' contest, come nei programmi da gara: la finestra principale
    // smette di essere il log di tutti i giorni e mostra, agganciati nel suo
    // corpo, i pannelli della gara (ContestLayout). Si entra e si esce dal menu
    // Contest Mode in alto, o chiudendo la sessione; all'uscita la finestra
    // torna com'era.
    readonly property bool contestModeOn: layout.contestModeOn

    // Una lista di pannelli senza quelli che vivevano solo nelle finestre del
    // banco di prima (inserimento, punteggio, ritmo, cluster): adesso stanno
    // agganciati in gara, e fuori dalla gara non devono tornare a galla.
    function withoutContestWindows(text) {
        return window.panelListOf(text).filter(k => !window.isWindowOnly(k)).join(",")
    }

    // In gara non ci sono finestre staccate: tutto sta nella disposizione
    // della gara. Serve anche all'avvio, per chi arriva dalla 1.15.6 o prima
    // con il programma chiuso in gara: le finestre del banco di allora
    // restavano aperte sopra i pannelli agganciati, e i pannelli chiusi con la
    // loro X risultavano chiusi anche nella finestra di tutti i giorni.
    function settleContestMode() {
        if (!layout.contestModeOn)
            return
        if (layout.detachedPanels.length > 0)
            layout.detachedPanels = ""
        layout.preContestDetached = window.withoutContestWindows(layout.preContestDetached)
        layout.preContestHidden = window.withoutContestWindows(layout.preContestHidden)
    }

    function openContestDesk() {
        if (!layout.contestModeOn) {
            // Com'era prima: la si rimette uguale quando la gara finisce. I
            // pannelli staccati tornano dentro, perche' in gara ci sono gia'
            // quelli agganciati, e due log aperti confondono.
            layout.preContestDetached = window.withoutContestWindows(layout.detachedPanels)
            layout.preContestHidden = window.withoutContestWindows(layout.hiddenPanels)
            layout.preContestVisibility = window.visibility
            layout.detachedPanels = ""
            layout.contestModeOn = true
            // Solo entrando: chi poi chiude la CW la trova chiusa.
            contestLayout.prepare(decolog.activation.isCwContest())
        }
        if (window.visibility !== Window.Maximized && window.visibility !== Window.FullScreen)
            window.showMaximized()
    }

    function exitContestMode() {
        if (!layout.contestModeOn)
            return
        layout.contestModeOn = false
        layout.detachedPanels = window.withoutContestWindows(layout.preContestDetached)
        layout.hiddenPanels = window.withoutContestWindows(layout.preContestHidden)
        if (layout.preContestVisibility === Window.Windowed)
            window.showNormal()
    }

    Timer { id: exitProbe; interval: 900; onTriggered: window.exitContestMode() }
    Timer {
        id: qsyProbe
        property int step: 0
        interval: 1500
        repeat: true
        onTriggered: {
            const entry = contestLayout.panelFor("contest").item
            ++step
            if (step === 1) {
                entry.band = "40m"
                entry.qsyTo("40m", entry.mode)
            } else if (step === 2) {
                entry.mode = "SSB"
                entry.qsyTo(entry.band, "SSB")
            } else if (step === 3) {
                entry.band = "20m"
                entry.qsyTo("20m", entry.mode)
            } else {
                console.warn("PROBE qsy radio=" + decolog.shownFrequency + " " + decolog.shownMode
                             + " entry=" + entry.band + " " + entry.mode)
                stop()
            }
        }
    }
    Timer {
        id: pointerProbe
        property var args: []
        property int step: 0
        property var start0: null
        // Le prove "mboard…" lavorano sulla lavagna di tutti i giorni.
        property bool useMain: false
        interval: 60
        repeat: true
        function send(win, kind, pt) { decolog.testPointer(win, kind, pt.x, pt.y) }
        // Il comando della testata con questo simbolo, cercato fra i figli.
        function findGlyph(item, glyph) {
            if (!item)
                return null
            if ((item.glyph === glyph || (item.text === glyph && item.glyph === undefined)) && item.visible)
                return item
            for (let i = 0; i < item.children.length; ++i) {
                const found = findGlyph(item.children[i], glyph)
                if (found)
                    return found
            }
            return null
        }
        onTriggered: {
            const kind = args[0]
            const key = args[1]
            // Si aspetta che la lavagna abbia preso le misure.
            if (step++ < 10)
                return
            if (kind === "clustercols") {
                contestLayout.panelFor("cluster").item.openColumns()
                stop()
            } else if (kind === "clusterresize") {
                // Tira il bordo destro di un'intestazione del cluster di dx pixel.
                const cp = contestLayout.panelFor("cluster").item
                const i = parseInt(args[1]), dx = parseInt(args[2])
                const h = cp.headerItem(i)
                if (step === 11) {
                    start0 = h.mapToItem(null, h.width + 4, h.height / 2)
                    pointerProbe.args = args.concat([String(h.width)])
                    send(window, "hover", start0)
                    send(window, "press", start0)
                    return
                }
                const n = step - 11
                if (n <= 8) {
                    send(window, "move", Qt.point(start0.x + dx * n / 8, start0.y))
                    return
                }
                send(window, "release", Qt.point(start0.x + dx, start0.y))
                stop()
                Qt.callLater(function () {
                    console.warn("PROBE clusterresize " + h.modelData + " " + args[args.length - 1]
                                 + " => " + cp.headerItem(i).width + " stored " + JSON.stringify(cp.storedWidths))
                })
            } else if (kind === "clustermove") {
                const cp = contestLayout.panelFor("cluster").item
                const from = parseInt(args[1]), to = parseInt(args[2])
                if (step === 11) {
                    const h = cp.headerItem(from)
                    start0 = h.mapToItem(null, h.width / 2, h.height / 2)
                    pointerProbe.args = args.concat([JSON.stringify(cp.columns)])
                    send(window, "hover", start0)
                    send(window, "press", start0)
                    return
                }
                const target = cp.headerItem(to)
                const end = target.mapToItem(null, target.width / 2, target.height / 2)
                const n = step - 11
                if (n <= 8) {
                    send(window, "move", Qt.point(start0.x + (end.x - start0.x) * n / 8, start0.y))
                    return
                }
                send(window, "release", end)
                stop()
                Qt.callLater(function () {
                    console.warn("PROBE clustermove " + from + "->" + to + ": " + args[args.length - 1]
                                 + " => " + JSON.stringify(cp.columns))
                })
            } else if (kind === "headermove") {
                const lp = args[1] === "board" ? contestLayout.panelFor("logbook").item : window.panelItem("logbook").item
                const from = parseInt(args[2]), to = parseInt(args[3])
                if (step === 11) {
                    const cell = lp.headerCell(from)
                    start0 = cell.mapToItem(null, 20, cell.height / 2)
                    pointerProbe.args = args.concat([JSON.stringify(lp.model.columnLayout)])
                    send(window, "hover", start0)
                    send(window, "press", start0)
                    return
                }
                const target = lp.headerCell(to)
                const end = target.mapToItem(null, target.width / 2, target.height / 2)
                const n = step - 11
                if (n <= 8) {
                    send(window, "move", Qt.point(start0.x + (end.x - start0.x) * n / 8, start0.y))
                    return
                }
                send(window, "release", end)
                stop()
                Qt.callLater(function () {
                    console.warn("PROBE headermove " + args[1] + " " + from + "->" + to + ": "
                                 + args[args.length - 1] + " => " + JSON.stringify(lp.model.columnLayout))
                })
            } else if (kind === "headerresize") {
                const lp = window.panelItem("logbook").item
                const col = 2
                if (step === 11) {
                    const cell = args[1] === "table" ? lp.tableCell(col) : lp.headerCell(col)
                    start0 = cell.mapToItem(null, cell.width - 2, cell.height / 2)
                    pointerProbe.args = args.concat([String(lp.columnWidthOf(col))])
                    // Prima ci si passa sopra, come il mouse: la maniglia del
                    // bordo si accende col passaggio, non con la pressione.
                    send(window, "hover", Qt.point(start0.x - 20, start0.y))
                    send(window, "hover", start0)
                    return
                }
                if (step === 12) {
                    send(window, "press", start0)
                    return
                }
                const n = step - 12
                if (n <= 8) {
                    send(window, "move", Qt.point(start0.x + 10 * n, start0.y))
                    return
                }
                send(window, "release", Qt.point(start0.x + 80, start0.y))
                stop()
                Qt.callLater(function () {
                    console.warn("PROBE header column " + col + " width " + args[args.length - 1] + " -> " + lp.columnWidthOf(col))
                })
            } else if (kind === "logprobe") {
                const lp = key === "board" ? contestLayout.panelFor("logbook").item : window.panelItem("logbook").item
                if (step === 11) {
                    lp.showMenu(args[2] === "band" ? "filters" : "columns")
                    return
                }
                if (step < 18)
                    return
                // Filtri: una banda; colonne: il pulsante che aggiunge "Ora inizio".
                const label = args[2] === "band" ? "20m" : lp.model.titleOf("time_on") + "  ·  TIME_ON"
                const c = findGlyph(lp.menuFor(args[2]).contentItem, label)
                if (!c) { console.warn("PROBE log: no item " + label); stop(); return }
                const at = c.mapToItem(null, c.width / 2, c.height / 2)
                send(c.Window.window, "press", at)
                send(c.Window.window, "release", at)
                stop()
                Qt.callLater(function () {
                    console.warn("PROBE log " + key + " " + args[2] + ": bands=" + JSON.stringify(lp.model.bandFilter)
                                 + " time_on shown=" + (lp.model.columnLayout.indexOf("time_on") >= 0))
                })
            } else if (kind === "panelsclick") {
                const c = findGlyph(panelsPopup.contentItem, window.panelTitle(key))
                if (!c) { console.warn("PROBE no row"); stop(); return }
                const w = c.Window.window
                const at = c.mapToItem(null, c.width / 2, c.height / 2)
                const before = contestLayout.isShown(key)
                send(w, "press", at)
                send(w, "release", at)
                stop()
                Qt.callLater(function () {
                    console.warn("PROBE panels row " + key + ": shown " + before + " -> " + contestLayout.isShown(key))
                })
            } else if (kind === "dialogclose") {
                const w = aboutDialog
                const c = findGlyph(w.header, "✕")
                if (!c) { console.warn("PROBE no close glyph"); stop(); return }
                const at = c.mapToItem(null, c.width / 2, c.height / 2)
                send(w, "press", at)
                send(w, "release", at)
                stop()
                Qt.callLater(function () { console.warn("PROBE dialog visible after ✕: " + aboutDialog.visible) })
            } else if (kind === "mainclick") {
                const c = findGlyph(mainBoard, "✕")
                const at = c.mapToItem(null, c.width / 2, c.height / 2)
                console.warn("PROBE main glyph at " + Math.round(at.x) + "," + Math.round(at.y) + " hidden before: " + layout.hiddenPanels)
                send(window, "press", at)
                send(window, "release", at)
                stop()
                Qt.callLater(function () { console.warn("PROBE main hidden after: " + layout.hiddenPanels) })
            } else if (kind === "boardclick") {
                const board = pointerProbe.useMain ? mainBoard : contestLayout
                const p = board.panelFor(key)
                const c = findGlyph(p, args[2] === "detach" ? "⤢" : "✕")
                const at = c.mapToItem(null, c.width / 2, c.height / 2)
                console.warn("PROBE glyph at " + Math.round(at.x) + "," + Math.round(at.y)
                             + " panel " + Math.round(p.x) + "," + Math.round(p.y) + " " + Math.round(p.width))
                send(window, "press", at)
                send(window, "release", at)
                stop()
                Qt.callLater(function () {
                    console.warn("PROBE " + args[2] + " " + key + ": shown=" + board.isShown(key)
                                 + " floating=" + board.isFloating(key))
                })
            } else if (kind === "boarddrag" || kind === "boardresize") {
                const board = pointerProbe.useMain ? mainBoard : contestLayout
                const p = board.panelFor(key)
                if (step === 11) {
                    start0 = kind === "boardresize" ? p.mapToItem(null, p.width - 1, p.height - 1)
                                                    : p.mapToItem(null, 90, Theme.panelHeight / 2)
                    console.warn("PROBE " + kind + " " + key + " size " + Math.round(p.width) + "x" + Math.round(p.height))
                    console.warn("PROBE drag " + key + " from " + Math.round(p.x) + "," + Math.round(p.y))
                    send(window, "press", start0)
                    return
                }
                const n = step - 11
                const dx = parseInt(args[2]), dy = parseInt(args[3])
                if (n <= 10) {
                    send(window, "move", Qt.point(start0.x + dx * n / 10, start0.y + dy * n / 10))
                    return
                }
                send(window, "release", Qt.point(start0.x + dx, start0.y + dy))
                stop()
                Qt.callLater(function () {
                    console.warn("PROBE drag " + key + " to " + Math.round(p.x) + "," + Math.round(p.y)
                                 + " size " + Math.round(p.width) + "x" + Math.round(p.height))
                })
            } else if (kind === "floatclose") {
                const w = contestLayout.floatingWindowFor(key)
                if (!w)
                    return
                const c = findGlyph(w.contentItem, "✕")
                if (!c)
                    return
                const at = c.mapToItem(null, c.width / 2, c.height / 2)
                send(w, "press", at)
                send(w, "release", at)
                stop()
                Qt.callLater(function () {
                    console.warn("PROBE floatclose " + key + ": shown=" + contestLayout.isShown(key)
                                 + " floating=" + contestLayout.isFloating(key))
                })
            }
        }
    }
    Timer {
        id: boardProbe
        property var args: []
        interval: 600
        onTriggered: contestLayout.testMove(args[1], parseInt(args[2]), parseInt(args[3]))
    }

    // Il programma chiuso in gara si riapre in gara; ma se la sessione nel
    // frattempo non c'e' piu', la finestra principale torna com'era.
    Timer {
        running: true
        interval: 500
        onTriggered: if (layout.contestModeOn && !decolog.activation.active) window.exitContestMode()
    }

    // Chiusa la sessione, finita la gara: si esce anche dalla modalita'.
    Connections {
        target: decolog.activation
        function onChanged() {
            if (layout.contestModeOn && !decolog.activation.active)
                window.exitContestMode()
        }
    }

    // I comandi del menu Contest Mode.
    function deskDo(what, arg) {
        if (what === "enter") {
            window.openContestDesk()
        } else if (what === "session") {
            activationDialog.openDialog()
        } else if (what === "toggle") {
            contestLayout.toggle(arg)
        } else if (what === "reset") {
            contestLayout.resetLayout()
        } else if (what === "exit") {
            window.exitContestMode()
        } else if (what === "export") {
            window.openContest()
        } else if (what === "submit") {
            submitDialog.openDialog()
        }
    }

    function openRotor() {
        rotorWindow.active = true
        if (rotorWindow.item) {
            rotorWindow.item.raise()
            rotorWindow.item.requestActivate()
        }
    }
    function openCards(view) {
        cardsWindow.active = true
        if (cardsWindow.item) {
            if (view)
                cardsWindow.item.view = view
            cardsWindow.item.raise()
            cardsWindow.item.requestActivate()
        }
    }
    function openCluster(tab) {
        clusterWindow.tab = tab
        clusterWindow.active = true
        if (clusterWindow.item) {
            clusterWindow.item.tab = tab
            clusterWindow.item.raise()
            clusterWindow.item.requestActivate()
        }
    }

    // Lo spot finto entra nel modello un attimo dopo: si aspetta, poi si fa il
    // doppio clic sulla prima riga.
    Timer {
        id: clusterTuneTimer
        interval: 600
        onTriggered: {
            const model = decolog.cluster.spots
            if (model && model.count > 0)
                decolog.cluster.tune(model.get(0).spotKey)
            window.panelItem("tabs").setTab(3)
        }
    }

    Component.onCompleted: {
        const what = startupShow.split(":")
        if (what[0] === "contestdesk") window.openContestDesk()
        // Per le prove: si entra e si esce, e la finestra principale deve
        // tornare com'era.
        else if (what[0] === "contestexit") { window.openContestDesk(); exitProbe.start() }
        // Per misurare: apre il banco e registra N QSO di fila, dicendo quanto
        // ci mette ognuno. Un QSO in gara deve essere istantaneo.
        // Per misurare: il banco aperto con il cluster che corre (uno spot ogni
        // 250 ms, come un RBN in gara) e ogni tanto un clic su uno spot.
        else if (what[0] === "benchspots") { if (what[2] !== "nodesk") window.openContestDesk(); else window.exitContestMode(); spotBench.left = parseInt(what[1] || "40"); spotBench.start() }
        // Per misurare: scrivere un nominativo lettera per lettera, come si fa
        // nell'inserimento veloce. Ogni lettera deve essere istantanea.
        else if (what[0] === "benchtype") { window.openContestDesk(); typeBench.left = parseInt(what[1] || "30"); typeBench.start() }
        // Per le prove: il banco aperto e un clic sul primo spot del cluster; il
        // nominativo deve finire nell'inserimento veloce.
        else if (what[0] === "pickspot") {
            window.openContestDesk()
            Qt.callLater(function () {
                const m = decolog.cluster.spots
                if (m.count > 0)
                    decolog.cluster.lookupSpot(m.get(0).spotKey)
            })
        }
        else if (what[0] === "benchqso") { window.openContestDesk(); benchTimer.left = parseInt(what[1] || "5"); benchTimer.start() }
        else if (what[0] === "new") newQsoDialog.open()
        else if (what[0] === "qso") { openQso(parseInt(what[1])); if (what[2]) qsoDialog.currentTab = parseInt(what[2]) }
        else if (what[0] === "profiles") profilesDialog.open()
        else if (what[0] === "setup") { setupDialog.page = parseInt(what[1] || "3"); setupDialog.open(); if (what[2] === "end") Qt.callLater(setupDialog.scrollToBottom) }
        else if (what[0] === "menu") window.panelItem("logbook").showMenu(what[1])
        else if (what[0] === "select") window.panelItem("logbook").showSelection(what[1], what[2])
        // Lavori di manutenzione, utili anche da riga di comando.
        else if (what[0] === "combo") { window.showPanel("cw"); comboTimer.start() }
        else if (what[0] === "combo2") { newQsoDialog.open(); combo2Timer.start() }
        else if (what[0] === "cwsend") { window.showPanel("cw"); cwSendTimer.start() }
        else if (what[0] === "radioprobe") { window.panelItem("tabs").setTab(3); decolog.rig.probeRadio() }
        // Il VFO della barra: "tune:14.074" o "tune:14.074:FT8"; "modes" apre
        // l'elenco dei modi, per guardarlo.
        else if (what[0] === "tune") { window.panelItem("tabs").setTab(3)
                                      tuneTimer.mhz = parseFloat(what[1]); tuneTimer.mode = what[2] || ""
                                      tuneTimer.start() }
        else if (what[0] === "modes") topBar.openModeMenu()
        // "newqsy:40m:CW": nel Nuovo QSO si sceglie banda (e modo), come dalla
        // tendina; la radio deve andarci.
        else if (what[0] === "newqsy") newQsyTimer.start()
        else if (what[0] === "repair") decolog.repairImportedFields()
        else if (what[0] === "fillall") decolog.completeMissingFromCallbook()
        else if (what[0] === "maintenance") { decolog.repairImportedFields(); decolog.completeMissingFromCallbook() }
        else if (what[0] === "tab") window.panelItem("tabs").setTab(parseInt(what[1]))
        else if (what[0] === "pop") popWindow.active = true
        else if (what[0] === "panels") { if (what[1]) { const how = what.slice(2); for (let i = 0; i < how.length; ++i) { if (what[1] === "close") window.closePanel(how[i]); else if (what[1] === "detach") window.detachPanel(how[i]); else if (what[1] === "show") window.showPanel(how[i]); else if (what[1] === "attach") window.attachPanel(how[i]) } } else panelsPopup.open() }
        // "cluster:spot:14025.1:3Y0J:CW" mette una riga come se venisse da un
        // nodo e ci fa sopra il doppio clic: serve a guardare se la radio ci va.
        else if (what[0] === "cluster" && what[1] === "spot") {
            const now = new Date()
            const hhmm = ("0" + now.getUTCHours()).slice(-2) + ("0" + now.getUTCMinutes()).slice(-2)
            decolog.cluster.injectLine("DX de IK0TEST:  " + what[2] + "  " + what[3]
                                       + "  " + (what[4] || "CW") + " 18 dB  " + hhmm + "Z")
            clusterTuneTimer.start()
        }
        else if (what[0] === "cluster") openCluster(parseInt(what[1] || "0"))
        else if (what[0] === "logs") logsDialog.openDialog()
        else if (what[0] === "activation") { activationDialog.openDialog(what[1] || ""); if (what[2] === "choose") Qt.callLater(activationDialog.chooseContest) }
        else if (what[0] === "modes") window.panelItem("newqso").showModes()
        // Per le prove: apre tutte le finestre due volte di fila. Due volte
        // perche' il guaio da cercare e' proprio quello — la finestra che si
        // sdoppia invece di venire in primo piano.
        else if (what[0] === "windows") {
            for (let round = 0; round < 2; ++round) {
                openStats()
                openCluster(0)
                openCards()
                openContest()
                openRotor()
                popWindow.active = true
                window.detachPanel("callinfo")
                window.detachPanel("ft2")
                window.detachPanel("map")
            }
        }
        else if (what[0] === "lock") layout.layoutLocked = what[1] !== "off"
        else if (what[0] === "layoutmenu") layoutMenu.openAt(what[1] || "logbook", 420, 300)
        // "lookup:JA1ZZZ": la scheda del nominativo, con la griglia banda x modo.
        else if (what[0] === "lookup") { if (what[2] === "detach") window.detachPanel("callinfo"); else window.showPanel("callinfo"); decolog.lookupCall = what[1] || "" }
        else if (what[0] === "about") aboutDialog.open()
        else if (what[0] === "update") updateDialog.open()
        else if (what[0] === "updatecheck") { window.panelItem("tabs").setTab(3); decolog.updates.checkNow() }
        else if (what[0] === "updateget") { decolog.updates.checkNow(); updateGetTimer.start() }
        else if (what[0] === "mainmenu") topBar.openMainMenu()
        // Per le prove: in gara un pannello spostato sulla lavagna di dx, dy.
        else if (what[0] === "contestmove") {
            window.openContestDesk()
            boardProbe.args = what
            boardProbe.start()
        }
        // Per le prove, col mouse vero: un clic sulla ✕ o su ⤢ di un pannello
        // della lavagna, un trascinamento per la testata, o la ✕ di un pannello
        // staccato. Dice poi cosa e' successo.
        else if (what[0] === "dialogclose") {
            aboutDialog.open()
            pointerProbe.args = what
            pointerProbe.step = 0
            pointerProbe.start()
        }
        else if (what[0] === "setupmenu") Qt.callLater(topBar.openSetupMenu)
        // Per le prove, col mouse vero: in gara, un clic su una riga di Pannelli.
        else if (what[0] === "panelsclick") {
            window.openContestDesk()
            panelsPopup.open()
            pointerProbe.args = what
            pointerProbe.step = 0
            pointerProbe.start()
        }
        // Per le prove col mouse vero: nel log (normal o in gara) un clic su
        // una banda nei filtri, o su una colonna nel menu Colonne.
        // Per le prove: in gara, l'inserimento veloce cambia banda e poi modo,
        // come dal menu; la radio deve andare dove si e' scelto.
        else if (what[0] === "qsyprobe") {
            window.openContestDesk()
            qsyProbe.start()
        }
        else if (what[0] === "clustermove" || what[0] === "clustercols" || what[0] === "clusterresize") {
            window.openContestDesk()
            pointerProbe.args = what
            pointerProbe.step = 0
            pointerProbe.start()
        }
        else if (what[0] === "headermove") {
            if (what[1] === "board")
                window.openContestDesk()
            pointerProbe.args = what
            pointerProbe.step = 0
            pointerProbe.start()
        }
        else if (what[0] === "headerresize") {
            pointerProbe.args = what
            pointerProbe.step = 0
            pointerProbe.start()
        }
        else if (what[0] === "logprobe") {
            if (what[1] === "board")
                window.openContestDesk()
            pointerProbe.args = what
            pointerProbe.step = 0
            pointerProbe.start()
        }
        else if (what[0] === "mainclick") {
            pointerProbe.args = what
            pointerProbe.step = 0
            pointerProbe.start()
        }
        else if (what[0] === "mboardclick" || what[0] === "mboarddrag" || what[0] === "mboardresize") {
            window.exitContestMode()
            pointerProbe.useMain = true
            pointerProbe.args = [what[0].slice(1)].concat(what.slice(1))
            pointerProbe.step = 0
            pointerProbe.start()
        }
        else if (what[0] === "boardclick" || what[0] === "boarddrag" || what[0] === "boardresize" || what[0] === "floatclose") {
            window.openContestDesk()
            if (what[0] === "floatclose")
                contestLayout.setFloating(what[1], true)
            pointerProbe.args = what
            pointerProbe.step = 0
            pointerProbe.start()
        }
        // Per le prove: in gara un pannello staccato in una finestra sua.
        else if (what[0] === "contestfloat") {
            window.openContestDesk()
            for (let i = 1; i < what.length; ++i)
                contestLayout.setFloating(what[i], true)
        }
        // Per le prove: in gara si chiude e si riapre un pannello, come dal menu.
        else if (what[0] === "contesttoggle") {
            window.openContestDesk()
            for (let i = 1; i < what.length; ++i)
                window.deskDo("toggle", what[i])
        }
        // Per le prove: il menu Contest Mode, dentro o fuori dalla modalita'.
        else if (what[0] === "contestmenu") {
            if (what[1] === "on")
                window.openContestDesk()
            Qt.callLater(topBar.openContestMenu)
        }
        else if (what[0] === "stats") openStats()
        else if (what[0] === "cards") { openCards(what[1])
                                       if (what[2] === "menu" && cardsWindow.item)
                                           cardsWindow.item.showCardMenu()
                                       else if (what[2] === "standard")
                                           decolog.cards.addStandardCardFields() }
        else if (what[0] === "cloud") {
            // cloud:signup:CALL:PASSWORD · cloud:login:CALL:PASSWORD · cloud:sync
            if (what[1] === "signup") decolog.cloud.signup(what[2], what[3])
            else if (what[1] === "login") decolog.cloud.login(what[2], what[3])
            else if (what[1] === "purge") decolog.cloud.purgeCloud("DELETE")
            else if (what[1] === "loginpurge") { decolog.cloud.login(what[2], what[3]); purgeAfterLogin.start() }
            else decolog.cloud.syncNow()
            window.panelItem("tabs").setTab(3)
        }
        else if (what[0] === "rotor") {
            if (what[1] === "window") {
                openRotor()
                if (what[2] !== undefined && rotorWindow.item)
                    rotorWindow.item.showTab(parseInt(what[2]))
            }
            else
                decolog.rotor.pointTo(parseFloat(what[1] || "0"), what[2] || "")
        }
        else if (what[0] === "contest") {
            openContest()
            if (what[1] === "cabrillo" && contestWindow.item)
                contestWindow.item.openCabrillo()
        }
        else if (what[0] === "call") decolog.lookupCall = what[1]
        else if (what[0] === "awards") {
            // awards:<id>[:map|:missing|:unconfirmed]
            if (what[2] === "map") awardsDialog.showMap = true
            else if (what[2]) awardsDialog.view = what[2]
            awardsDialog.openAt(what[1] || "dxcc")
        }
    }

    // Per le prove: svuota il Cloud appena entrato.
    Timer { id: purgeAfterLogin; interval: 4000; onTriggered: decolog.cloud.purgeCloud("DELETE") }

    Timer {
        id: newQsyTimer
        interval: 2500
        onTriggered: {
            const p = window.panelItem("newqso").item
            const what = startupShow.split(":")
            p.qsyTo(what[1], what[2] || "", true)
            Qt.callLater(function () { console.warn("PROBE newqsy freq field ok") })
        }
    }
    Timer { id: comboTimer; interval: 800
           onTriggered: { const it = window.panelItem("cw"); if (it) it.showCombo() } }
    Timer { id: combo2Timer; interval: 900; onTriggered: newQsoDialog.showBandCombo() }
    Timer { id: cwSendTimer; interval: 1200; onTriggered: decolog.rig.sendMacro(0, {}) }
    // Prova dell'aggiornamento: si aspetta la risposta, poi si scarica.
    Timer { id: updateGetTimer; interval: 2500; onTriggered: decolog.updates.downloadAndInstall() }
    // La radio ci mette un attimo a rispondere: la prova aspetta che ci sia.
    Timer { id: tuneTimer; interval: 1500
           property real mhz: 0
           property string mode: ""
           onTriggered: decolog.tuneTo(mhz, mode) }

    NewQsoDialog { id: newQsoDialog }
    QsoDetailDialog { id: qsoDialog }
    StationProfilesDialog { id: profilesDialog }
    AboutDialog { id: aboutDialog }
    UpdateDialog { id: updateDialog }

    // Quando il controllo trova una versione nuova la finestra si apre da se':
    // e' l'unico momento in cui l'aggiornamento si fa vedere.
    Connections {
        target: decolog.updates
        function onUpdateFound(version) { updateDialog.open() }
    }

    SetupDialog { id: setupDialog; onUpdateRequested: updateDialog.open() }
    ActivationDialog { id: activationDialog }
    ContestSubmitDialog { id: submitDialog }
    Timer {
        id: benchTimer
        property int left: 0
        property int n: 0
        interval: 2500
        repeat: true
        onTriggered: {
            if (left <= 0) { stop(); return }
            left--; n++
            const t0 = Date.now()
            const now = decolog.utcNow()
            const err = decolog.logManualQso({ call: "B" + (Date.now() % 100000) + "X" + n, date: now.date, time: now.time,
                                               band: "20m", mode: "CW", rst_sent: "599", rst_rcvd: "599",
                                               srx: String(10 + n % 30) })
            console.warn("BENCH qso " + n + ": " + (Date.now() - t0) + " ms " + err
                         + " | scatto piu' lungo dal QSO prima: " + benchWatch.worst + " ms")
            benchWatch.worst = 0
        }
    }
    Timer {
        id: typeBench
        property int left: 0
        property int n: 0
        readonly property var calls: ["IK0ABC", "DL1XYZ", "JA1ZZZ", "W1AW", "PY2ABC", "VK3ABC", "G4ABC", "UA9XX"]
        interval: 120
        repeat: true
        onTriggered: {
            if (left <= 0) { stop(); return }
            n++
            const call = calls[Math.floor(n / 6) % calls.length]
            const part = call.substring(0, 1 + n % 6)
            const t0 = Date.now()
            decolog.lookupCall = part
            const dt = Date.now() - t0
            if (n % 6 === 5) {
                left--
                console.warn("BENCH type " + part + ": " + dt + " ms | scatto piu' lungo: " + benchWatch.worst + " ms")
                benchWatch.worst = 0
            }
        }
    }
    Timer {
        id: spotBench
        property int left: 0
        property int n: 0
        property int worstInject: 0
        interval: 250
        repeat: true
        onTriggered: {
            if (left <= 0) { stop(); return }
            n++
            const pre = ["K", "DL", "JA", "PY", "UA", "G", "I", "EA", "VK", "W"]
            const call = pre[n % pre.length] + (n % 10) + "B" + String.fromCharCode(65 + n % 26)
            const khz = (14005 + (n * 7) % 60) + ".0"
            const t0 = Date.now()
            decolog.cluster.injectLine("DX de TEST" + (n % 9) + ":   " + khz + "  " + call + "  CW 24 dB 28 WPM CQ  "
                                       + decolog.utcNow().time.replace(":", "").substring(0, 4) + "Z")
            worstInject = Math.max(worstInject, Date.now() - t0)
            if (n % 8 === 0) {
                const t1 = Date.now()
                const m = decolog.cluster.spots
                if (m.count > 0)
                    decolog.cluster.lookupSpot(m.get(n % m.count).spotKey)
                console.warn("BENCH click: " + (Date.now() - t1) + " ms")
            }
            if (n % 4 === 0) {
                left--
                console.warn("BENCH spots " + n + " | inserimento peggiore " + worstInject
                             + " ms | scatto piu' lungo: " + benchWatch.worst + " ms")
                benchWatch.worst = 0
                worstInject = 0
            }
        }
    }
    // Misura la fluidita': un battito ogni 16 ms, e il buco piu' lungo fra due
    // battiti e' quanto il programma e' rimasto fermo.
    Timer {
        id: benchWatch
        property double last: 0
        property int worst: 0
        interval: 16
        repeat: true
        running: benchTimer.running || spotBench.running || typeBench.running
        onTriggered: {
            const now = Date.now()
            if (last > 0)
                worst = Math.max(worst, now - last)
            last = now
        }
    }
    LogsDialog {
        id: logsDialog
        // All'avvio si chiede quale log aprire, per chi tiene un log per ogni
        // contest. Di serie no: chi ne ha uno solo non deve rispondere a niente.
        Component.onCompleted: if (decolog.logs.askAtStart) Qt.callLater(logsDialog.openDialog)
    }
    AwardsDialog {
        id: awardsDialog
        onOpenQso: (id) => window.openQso(id)
    }

    FileDialog {
        id: importDialog
        title: qsTr("Import ADIF")
        nameFilters: [qsTr("ADIF files (*.adi *.adif)"), qsTr("All files (*)")]
        onAccepted: decolog.importAdif(selectedFile)
    }
    FileDialog {
        id: exportDialog
        title: qsTr("Export ADIF")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "adi"
        nameFilters: [qsTr("ADIF files (*.adi)")]
        onAccepted: decolog.exportAdif(selectedFile)
    }

    // Queste finestre nascono quando servono e muoiono quando si chiudono. Il
    // Loader si spegne con Qt.callLater e non dentro l'onClosing: spegnerlo li'
    // vuol dire distruggere la finestra mentre sta ancora chiudendosi, ed e' il
    // genere di cosa che fa cadere il programma invece di chiudere una finestra.
    Loader {
        id: statsWindow
        active: false
        sourceComponent: StatsWindow {
            onClosing: Qt.callLater(function () { statsWindow.active = false })
        }
    }

    Loader {
        id: rotorWindow
        active: false
        sourceComponent: RotorWindow {
            onClosing: Qt.callLater(function () {
                if (!window.quitting && window.isPanelDetached("rotor"))
                    window.attachPanel("rotor")
                rotorWindow.active = false
            })
        }
    }

    Loader {
        id: contestWindow
        active: false
        sourceComponent: ContestWindow {
            onClosing: Qt.callLater(function () { contestWindow.active = false })
        }
    }

    Loader {
        id: cardsWindow
        active: false
        sourceComponent: QslCardsWindow {
            onClosing: Qt.callLater(function () { cardsWindow.active = false })
        }
    }

    Loader {
        id: clusterWindow
        property int tab: 0
        property bool contestMode: false
        active: false
        sourceComponent: ClusterWindow {
            tab: clusterWindow.tab
            contestMode: clusterWindow.contestMode
            onClosing: Qt.callLater(function () { clusterWindow.active = false })
        }
    }

    Loader {
        id: popWindow
        active: false
        sourceComponent: LogbookWindow {
            onClosing: Qt.callLater(function () { popWindow.active = false })
        }
    }

    // Una finestra per ogni pannello staccato: nasce quando si stacca, muore
    // quando si riaggancia o si chiude. Il modello e' un ListModel tenuto in
    // pari riga per riga, non la lista calcolata: con una lista JS ogni
    // cambiamento faceva rinascere *tutte* le finestre — quelle gia' aperte
    // sparivano e tornavano altrove, svuotate di quello che avevano dentro.
    // Quando si chiude il programma le finestre staccate si chiudono anche
    // loro, ma quella non e' una richiesta di riagganciare: senza questo, uscire
    // da DecoDXLog riportava dentro tutti i pannelli e la volta dopo li si
    // ritrovava nella finestra principale.
    property bool quitting: false
    Connections {
        target: Qt.application
        function onAboutToQuit() { window.quitting = true }
    }

    ListModel {
        id: detachedModel
        // All'avvio le finestre staccate sono quelle dell'ultima volta.
        Component.onCompleted: window.syncDetachedWindows()
    }

    function syncDetachedWindows() {
        const wanted = window.panelListOf(layout.detachedPanels)
                             .filter(function (key) { return key !== "rotor" })
        if (window.isPanelDetached("rotor"))
            window.openRotor()
        // Prima via quelle che non servono piu', poi dentro quelle nuove: cosi'
        // le finestre che restano non vengono nemmeno sfiorate.
        for (let i = detachedModel.count - 1; i >= 0; --i) {
            if (wanted.indexOf(detachedModel.get(i).key) < 0)
                detachedModel.remove(i)
        }
        for (let j = 0; j < wanted.length; ++j) {
            let there = false
            for (let k = 0; k < detachedModel.count; ++k) {
                if (detachedModel.get(k).key === wanted[j]) {
                    there = true
                    break
                }
            }
            if (!there)
                detachedModel.append({key: wanted[j]})
        }
    }

    Connections {
        target: layout
        function onDetachedPanelsChanged() { window.syncDetachedWindows() }
    }

    // La finestra di un pannello staccato, per spostarla o portarla davanti.
    function panelWindowFor(key) {
        for (let i = 0; i < detachedModel.count; ++i) {
            if (detachedModel.get(i).key === key)
                return detachedWindows.objectAt(i)
        }
        return null
    }
    function raisePanel(key) {
        const win = window.panelWindowFor(key)
        if (win) {
            win.raise()
            win.requestActivate()
        }
    }

    Instantiator {
        id: detachedWindows
        model: detachedModel
        delegate: PanelWindow {
            required property string key
            panelKey: key
            panelTitle: window.panelTitle(key)
            panelSource: window.panelSource(key)
            // Quelli del contest vivono solo in finestra: riagganciarli
            // vorrebbe dire farli sparire, perche' nella disposizione della
            // finestra principale non hanno un posto.
            dockable: !window.isWindowOnly(key)
            // La X di una finestra staccata riaggancia il pannello. Ma quando a
            // chiudersi e' tutto il programma, la finestra si chiude lo stesso e
            // quello non e' un riaggancio: prima tornavano dentro tutti.
            onClosing: {
                if (window.quitting)
                    return
                if (window.isWindowOnly(panelKey))
                    window.closePanel(panelKey)
                else
                    window.attachPanel(panelKey)
            }
            onAttachRequested: window.attachPanel(panelKey)
            onCloseRequested: window.closePanel(panelKey)
            onOpenQsoRequested: (id) => window.openQso(id)
            onAwardRequested: (id) => awardsDialog.openAt(id)
            onClusterRequested: (tab) => window.openCluster(tab)
            onStatsRequested: window.openStats()
            onRotorRequested: window.openRotor()
            onContestRequested: window.openContest()
        }
    }

    // ── Il menu dei pannelli ────────────────────────────────────────────────
    Popup {
        id: panelsPopup
        // Una finestra sua: in modalita' contest resta sopra le finestre della
        // gara invece di aprirsi sotto.
        popupType: Popup.Window
        parent: Overlay.overlay
        x: window.width - width - 16
        y: 72
        width: 320
        padding: 12
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: Theme.panelColor; border.color: Theme.glassBorder; radius: 6 }

        ColumnLayout {
            anchors.fill: parent
            spacing: 6

            Text {
                text: qsTr("PANELS")
                color: Theme.secondaryColor
                font.family: Theme.monoFamily
                font.pixelSize: 11
                font.bold: true
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Click a panel to close it or bring it back. The arrow detaches it into "
                           + "a window of its own; a closed panel frees its space instead of leaving a hole.")
                color: Theme.textSecondary
                font.pixelSize: 11
                wrapMode: Text.Wrap
            }

            // In gara l'elenco e' quello della lavagna: prima comandava la
            // disposizione di tutti i giorni, che in gara e' spenta, e i clic
            // non facevano niente.
            Repeater {
                model: window.contestModeOn ? contestLayout.allKeys : window.panelKeys
                delegate: Rectangle {
                    id: panelRow
                    required property string modelData
                    readonly property bool closed: window.contestModeOn
                                                   ? !contestLayout.isShown(panelRow.modelData)
                                                   : window.isPanelHidden(panelRow.modelData)
                    readonly property bool floating: window.contestModeOn
                                                     ? contestLayout.isFloating(panelRow.modelData)
                                                     : window.isPanelDetached(panelRow.modelData)
                    Layout.fillWidth: true
                    implicitHeight: 26
                    radius: 4
                    color: rowArea.containsMouse ? Theme.glassOverlay : "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        anchors.rightMargin: 4
                        spacing: 8

                        Rectangle {
                            implicitWidth: 8
                            implicitHeight: 8
                            radius: 4
                            color: panelRow.closed ? Theme.borderColor : Theme.accentColor
                        }
                        Text {
                            Layout.fillWidth: true
                            text: window.panelTitle(panelRow.modelData)
                            color: panelRow.closed ? Theme.textSecondary : Theme.textPrimary
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                        Text {
                            text: !window.contestModeOn ? window.panelState(panelRow.modelData)
                                  : panelRow.closed ? qsTr("closed")
                                  : panelRow.floating ? qsTr("window") : qsTr("docked")
                            color: Theme.textSecondary
                            font.family: Theme.monoFamily
                            font.pixelSize: 10
                        }
                        PanelControl {
                            glyph: panelRow.floating ? "↩" : "⤢"
                            hint: panelRow.floating
                                  ? qsTr("Put it back in the main window")
                                  : qsTr("Detach it into its own window")
                            onClicked: {
                                if (window.contestModeOn) {
                                    if (panelRow.closed)
                                        contestLayout.setShown(panelRow.modelData, true)
                                    contestLayout.setFloating(panelRow.modelData, !panelRow.floating)
                                } else if (panelRow.floating) {
                                    window.attachPanel(panelRow.modelData)
                                } else {
                                    window.detachPanel(panelRow.modelData)
                                }
                            }
                        }
                    }

                    MouseArea {
                        id: rowArea
                        anchors.fill: parent
                        anchors.rightMargin: 24
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: window.contestModeOn ? contestLayout.toggle(panelRow.modelData)
                                                        : window.togglePanel(panelRow.modelData)
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.borderSoft }
            GlassButton {
                Layout.alignment: Qt.AlignRight
                text: qsTr("Restore the default layout")
                buttonHeight: 24
                fontPixelSize: 11
                onClicked: {
                    if (window.contestModeOn)
                        contestLayout.resetLayout()
                    else
                        window.resetPanels()
                    panelsPopup.close()
                }
            }
        }
    }

    // Il menu che esce col tasto destro sulla testata di un pannello: quello
    // che si puo' fare alla disposizione, li' dove la si guarda.
    StyledMenu {
        id: layoutMenu
        property string key: ""
        function openAt(panelKey, screenX, screenY) {
            layoutMenu.key = panelKey
            const p = window.contentItem.mapFromGlobal(screenX, screenY)
            layoutMenu.popup(window.contentItem, p.x, p.y)
        }

        StyledMenuItem {
            text: layout.layoutLocked ? qsTr("Unlock the layout") : qsTr("Lock the layout")
            onTriggered: layout.layoutLocked = !layout.layoutLocked
        }
        MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.borderSoft } }
        StyledMenuItem {
            text: qsTr("Detach this panel into its own window")
            enabled: layoutMenu.key.length > 0 && !window.isPanelDetached(layoutMenu.key)
            onTriggered: window.detachPanel(layoutMenu.key)
        }
        StyledMenuItem {
            text: qsTr("Close this panel")
            enabled: layoutMenu.key.length > 0
            onTriggered: window.closePanel(layoutMenu.key)
        }
        MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.borderSoft } }
        StyledMenuItem {
            text: qsTr("Panels…")
            onTriggered: panelsPopup.open()
        }
        StyledMenuItem {
            text: qsTr("Restore the default layout")
            onTriggered: window.resetPanels()
        }
    }

    Shortcut { sequence: "Ctrl+N"; onActivated: newQsoDialog.open() }
    Shortcut { sequence: "Ctrl+F"; onActivated: window.focusSearch() }
    Shortcut { sequence: "Ctrl+,"; onActivated: setupDialog.open() }
    Shortcut { sequence: "Ctrl+I"; onActivated: importDialog.open() }
    Shortcut { sequence: "Ctrl+E"; onActivated: exportDialog.open() }
    Shortcut { sequence: "Ctrl+K"; onActivated: window.openCluster(0) }
    Shortcut { sequence: "Ctrl+T"; onActivated: activationDialog.openDialog() }
    Shortcut { sequence: "Ctrl+Shift+T"; onActivated: window.openContest() }
    Shortcut { sequence: "Ctrl+R"; onActivated: window.openRotor() }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TopBar {
            id: topBar
            Layout.fillWidth: true
            onSetupRequested: setupDialog.open()
            onLogsRequested: logsDialog.openDialog()
            onImportRequested: importDialog.open()
            onExportRequested: exportDialog.open()
            onAwardsRequested: awardsDialog.openAt("")
            onClusterRequested: window.openCluster(0)
            onActivationRequested: activationDialog.openDialog()
            contestModeOn: window.contestModeOn
            contestOpenPanels: contestLayout.shownList
            onContestCommand: (what, arg) => window.deskDo(what, arg)
            onProfilesRequested: profilesDialog.open()
            closedPanels: window.contestModeOn
                          ? contestLayout.allKeys.filter(k => !contestLayout.isShown(k)).length
                          : window.hiddenPanels.length
            onPanelsRequested: panelsPopup.opened ? panelsPopup.close() : panelsPopup.open()
            onAboutRequested: aboutDialog.open()
            onLogFolderRequested: decolog.openDatabaseFolder()
            onQuitRequested: { window.quitting = true; Qt.quit() }
        }

        // Lo spazio delle lavagne: quella di tutti i giorni e quella della
        // gara ci stanno sopra, una alla volta.
        Item {
            id: boardArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 8
        }

        StatusRail { Layout.fillWidth: true }
    }

    // La lavagna di tutti i giorni.
    ContestLayout {
        id: mainBoard
        contestMode: false
        external: true
        settingsCategory: "layout/mainboard"
        allKeys: window.boardKeys
        defaultKeys: window.boardKeys.filter(k => k !== "cw")
        // Come la disposizione di prima: l'inserimento a sinistra, il log al
        // centro, la colonna di destra, la fascia delle schede in basso.
        defaultGeometry: ({
            newqso:   { x: 0.00, y: 0.00, w: 0.21, h: 0.68 },
            logbook:  { x: 0.21, y: 0.00, w: 0.57, h: 0.68 },
            callinfo: { x: 0.78, y: 0.00, w: 0.22, h: 0.30 },
            rotor:    { x: 0.78, y: 0.30, w: 0.22, h: 0.36 },
            map:      { x: 0.78, y: 0.66, w: 0.22, h: 0.34 },
            cw:       { x: 0.50, y: 0.30, w: 0.28, h: 0.38 },
            tabs:     { x: 0.00, y: 0.68, w: 0.60, h: 0.32 },
            ft2:      { x: 0.60, y: 0.68, w: 0.18, h: 0.32 }
        })
        externalShown: window.boardKeys.filter(k => !window.isPanelHidden(k))
        externalFloating: window.detachedPanels
        locked: layout.layoutLocked
        emptyText: qsTr("All the panels are closed: open them again from Panels, up in the bar.")
        floatingText: qsTr("All the panels are in their own windows: ↩ in a panel brings it back here.")
        titleOf: (key) => window.panelTitle(key)
        // boardArea sta nella colonna, che copre tutta la finestra: le sue
        // coordinate sono quelle della finestra.
        x: boardArea.x
        y: boardArea.y
        width: boardArea.width
        height: boardArea.height
        visible: !window.contestModeOn
        onCloseRequested: (key) => window.closePanel(key)
        onDetachRequested: (key) => window.detachPanel(key)
        onMenuRequested: (key, x, y) => layoutMenu.openAt(key, x, y)
        onExpandRequested: newQsoDialog.open()
        onOpenQso: (id) => window.openQso(id)
        onAwardRequested: (id) => awardsDialog.openAt(id)
        onClusterRequested: (tab) => window.openCluster(tab)
        onStatsRequested: window.openStats()
        onRotorRequested: window.openRotor()
        onContestRequested: window.openContest()
    }

    // La modalita' contest: la lavagna dei pannelli della gara, al posto di
    // quella di tutti i giorni.
    ContestLayout {
        id: contestLayout
        titleOf: (key) => window.panelTitle(key)
        Component.onCompleted: window.settleContestMode()
        // boardArea sta nella colonna, che copre tutta la finestra: le sue
        // coordinate sono quelle della finestra.
        x: boardArea.x
        y: boardArea.y
        width: boardArea.width
        height: boardArea.height
        z: 20
        visible: window.contestModeOn
        onOpenQso: (id) => window.openQso(id)
        onAwardRequested: (id) => awardsDialog.openAt(id)
        onClusterRequested: (tab) => window.openCluster(tab)
        onStatsRequested: window.openStats()
        onRotorRequested: window.openRotor()
        onContestRequested: window.openContest()
    }
}
