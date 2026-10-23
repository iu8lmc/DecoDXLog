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

RoundedWindow {
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
    // La cornice principale resta nelle mani del sistema operativo: su macOS
    // questo significa angoli e ombra nativi, senza alterare fullscreen,
    // spostamento o snapping su Windows e Linux.
    // The classic theme keeps the native macOS surface.  Liquid Glass opts
    // into the existing rounded/masked frame on every platform; Linux still
    // uses the compositor-independent opaque fallback in RoundedWindow.
    transparentFrame: Theme.isGlassTheme
    surfaceColor: Theme.bgDeep
    font.pixelSize: Theme.fontSize

    // La X della finestra principale chiude DecoDXLog per davvero. Senza questo,
    // con un pannello in finestra propria il programma restava in piedi: Qt
    // aspetta che si chiuda l'ultima finestra, e quella staccata era ancora li'.
    onClosing: {
        window.quitting = true
        window.persistDetachedWindowState()
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
        property string hiddenPanels: "cw,bandmap,decodium,decfull,decsig"
        property string detachedPanels: ""
        // Il Cluster fuori dal Contest vive sempre in una finestra propria.
        // Fa quindi parte dello stato dei pannelli esattamente come quelli
        // elencati in detachedPanels.
        property alias clusterWindowOpen: clusterWindow.active
        // La band map e' arrivata dopo: a chi aveva gia' la sua lavagna si
        // presenta chiusa, la apre da Pannelli quando vuole.
        property bool bandMapIntroduced: false
        // Lo stesso per il pannello Decodium (Full Spectrum e Signal RX).
        property bool decodiumIntroduced: false
        // E per le due finestre vere di Decodium (specchio).
        property bool decodiumWindowsIntroduced: false
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
        property bool firstRunGuideSeen: false
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
    readonly property var panelKeys: ["newqso", "logbook", "callinfo", "cw", "rotor", "ft2", "tabs", "map", "bandmap", "decodium", "decfull", "decsig",
                                      "contest", "score", "rate", "cluster", "net", "dvk", "mults"]
    // Gli ultimi quattro vivono solo in finestra: nel contest ognuno se li
    // mette dove vuole, e nella disposizione agganciata non hanno un posto.
    readonly property var windowOnlyPanels: ["contest", "score", "rate", "cluster", "net", "dvk", "mults"]
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
        case "bandmap":  return qsTr("Band map")
        case "decodium": return qsTr("Decodium")
        case "decfull":  return qsTr("Full Spectrum (Decodium window)")
        case "decsig":   return qsTr("Signal RX (Decodium window)")
        case "net":      return qsTr("Station network")
        case "dvk":      return qsTr("Voice keyer")
        case "mults":    return qsTr("Multipliers")
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
        case "bandmap":  return "BandMapPanel.qml"
        case "decodium": return "DecodiumPanel.qml"
        case "decfull":  return "DecodiumWindowPanel.qml"
        case "decsig":   return "DecodiumWindowPanel.qml"
        case "net":      return "ContestNetPanel.qml"
        case "dvk":      return "DvkPanel.qml"
        case "mults":    return "ContestMultipliersPanel.qml"
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
    readonly property var boardKeys: ["newqso", "logbook", "callinfo", "cw", "rotor", "ft2", "tabs", "map", "bandmap", "decodium", "decfull", "decsig"]

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
        if (key === "cluster")
            return clusterWindow.active ? qsTr("window") : qsTr("closed")
        // I pannelli senza una casella sulla lavagna normale (per ora il
        // Voice keyer) esistono soltanto nella loro finestra. Non devono
        // apparire come "docked" quando in realta' non sono aperti.
        if (!window.contestModeOn && window.isWindowOnly(key))
            return window.isPanelDetached(key) ? qsTr("window") : qsTr("closed")
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
        if (key === "cluster") {
            window.openCluster(0)
            return
        }
        // Il Voice keyer non ha una casella nella lavagna dell'uso normale:
        // aprirlo da Pannelli crea quindi direttamente la sua finestra. In
        // Contest Mode resta invece parte della lavagna del contest.
        if (!window.contestModeOn && window.isWindowOnly(key)) {
            const detached = window.panelListOf(layout.detachedPanels)
            layout.hiddenPanels = window.panelListOf(layout.hiddenPanels)
                                        .filter(function (k) { return k !== key }).join(",")
            if (detached.indexOf(key) < 0)
                layout.detachedPanels = detached.concat([key]).join(",")
            else
                Qt.callLater(function () { window.raisePanel(key) })
            return
        }
        // Riaperto, sta davanti agli altri.
        if (window.boardKeys.indexOf(key) >= 0)
            mainBoard.raiseKey(key)
        layout.hiddenPanels = window.panelListOf(layout.hiddenPanels)
                                    .filter(function (k) { return k !== key }).join(",")
    }
    function closePanel(key) {
        if (window.panelStateTransition)
            return
        window.panelStateTransition = true
        // Chiuso e' chiuso: se era in finestra, la finestra sparisce.
        layout.detachedPanels = window.panelListOf(layout.detachedPanels)
                                      .filter(function (k) { return k !== key }).join(",")
        if (key === "rotor")
            rotorWindow.active = false
        if (key === "cluster")
            clusterWindow.active = false
        const hidden = window.panelListOf(layout.hiddenPanels)
        if (hidden.indexOf(key) < 0)
            layout.hiddenPanels = hidden.concat([key]).join(",")
        window.finishPanelStateTransition()
    }
    function detachPanel(key) {
        if (!window.contestModeOn && window.isWindowOnly(key)) {
            window.showPanel(key)
            return
        }
        if (window.panelStateTransition)
            return
        window.panelStateTransition = true
        window.showPanel(key)
        const detached = window.panelListOf(layout.detachedPanels)
        if (detached.indexOf(key) < 0)
            layout.detachedPanels = detached.concat([key]).join(",")
        if (key === "rotor")
            window.openRotor()
        window.finishPanelStateTransition()
    }
    function attachPanel(key) {
        if (!window.contestModeOn && window.isWindowOnly(key)) {
            window.closePanel(key)
            return
        }
        if (window.panelStateTransition)
            return
        window.panelStateTransition = true
        layout.detachedPanels = window.panelListOf(layout.detachedPanels)
                                      .filter(function (k) { return k !== key }).join(",")
        if (key === "rotor")
            rotorWindow.active = false
        window.showPanel(key)
        window.finishPanelStateTransition()
    }
    function togglePanel(key) {
        if (key === "cluster") {
            if (clusterWindow.active) window.closePanel(key)
            else window.showPanel(key)
            return
        }
        if (!window.contestModeOn && window.isWindowOnly(key)) {
            if (window.isPanelDetached(key)) window.closePanel(key)
            else window.showPanel(key)
            return
        }
        if (window.isPanelHidden(key)) window.showPanel(key)
        else window.closePanel(key)
    }
    function resetPanels() {
        if (window.panelStateTransition)
            return
        window.panelStateTransition = true
        layout.hiddenPanels = ""
        layout.detachedPanels = ""
        layout.layoutLocked = false
        mainBoard.resetLayout()
        window.syncDetachedWindows()
        window.finishPanelStateTransition()
    }

    // Preset operativi: non vengono applicati automaticamente. Sono profili
    // di visibilita' verificabili e reversibili, utili alla prima
    // configurazione senza cancellare la disposizione personale dell'utente.
    readonly property var operationalPresets: [
        { key: "beginner", title: qsTr("Beginner"), panels: ["newqso", "logbook", "callinfo", "tabs", "map"] },
        { key: "dx", title: qsTr("DX"), panels: ["newqso", "logbook", "callinfo", "rotor", "map", "tabs"] },
        { key: "digital", title: qsTr("Digital"), panels: ["newqso", "logbook", "callinfo", "ft2", "tabs", "decfull", "decsig"] },
        { key: "contest", title: qsTr("Contest"), panels: ["newqso", "logbook", "callinfo", "cw", "tabs", "map"] },
        { key: "portable", title: qsTr("Portable"), panels: ["newqso", "logbook", "tabs"] }
    ]
    function applyOperationalPreset(preset) {
        if (!preset || !preset.panels)
            return
        if (window.contestModeOn) {
            contestLayout.resetLayout()
            for (const key of contestLayout.allKeys)
                contestLayout.setShown(key, preset.panels.indexOf(key) >= 0)
            return
        }
        window.panelStateTransition = true
        layout.detachedPanels = ""
        layout.hiddenPanels = window.boardKeys.filter(function (key) {
            return preset.panels.indexOf(key) < 0
        }).join(",")
        mainBoard.resetLayout()
        window.syncDetachedWindows()
        window.finishPanelStateTransition()
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
    // Il tempo fra due fotogrammi disegnati, mentre si trascina.
    QtObject {
        id: frameGap
        property double last: 0
        property int worst: 0
        property int frames: 0
    }
    Connections {
        target: dragBench.running && dragBench.n > 0 ? window : null
        function onFrameSwapped() {
            const now = Date.now()
            if (frameGap.last > 0)
                frameGap.worst = Math.max(frameGap.worst, now - frameGap.last)
            frameGap.last = now
            frameGap.frames++
        }
    }
    Timer {
        id: dragBench
        property string key: ""
        property var board: null
        property string mode: "move"
        property int n: 0
        property var p0: null
        property int worst: 0
        property int total: 0
        interval: 16
        repeat: true
        onTriggered: {
            // Un secondo di attesa perche' tutto sia disegnato.
            if (n < 0) { n++; return }
            const p = board.panelFor(key)
            if (!p) { stop(); return }
            const t0 = Date.now()
            if (n === 45)
                console.warn("DRAG a meta': " + Math.round(p.x) + "," + Math.round(p.y) + " " + Math.round(p.width) + "x" + Math.round(p.height) + " live=" + p.live)
            if (n === 0) {
                console.warn("DRAG prima: " + Math.round(p.x) + "," + Math.round(p.y) + " " + Math.round(p.width) + "x" + Math.round(p.height))
                p0 = mode === "resize" ? p.mapToItem(null, p.width - 1, p.height - 1)
                                       : p.mapToItem(null, 90, Theme.panelHeight / 2)
                benchWatch.worst = 0
                decolog.testPointer(window, "press", p0.x, p0.y)
            } else if (n <= 90) {
                const k = n <= 45 ? n : 90 - n
                decolog.testPointer(window, "move", p0.x + k * 4, p0.y + k * 2)
            } else {
                decolog.testPointer(window, "release", p0.x, p0.y)
                console.warn("DRAG " + mode + " " + key + ": movimento peggiore " + worst + " ms, medio "
                             + (total / 90).toFixed(1) + " ms, scatto piu' lungo " + benchWatch.worst + " ms"
                             + " | pannello " + Math.round(p.x) + "," + Math.round(p.y) + " " + Math.round(p.width) + "x" + Math.round(p.height)
                             + " live=" + p.live + " | fotogrammi " + frameGap.frames + ", buco piu' lungo fra due " + frameGap.worst + " ms")
                stop()
                return
            }
            const dt = Date.now() - t0
            worst = Math.max(worst, dt)
            total += dt
            n++
        }
    }
    Timer {
        id: idleWatch
        property int left: 0
        interval: 1000
        repeat: true
        onTriggered: {
            console.warn("IDLE scatto piu' lungo: " + benchWatch.worst + " ms")
            benchWatch.worst = 0
            if (--left <= 0) stop()
        }
    }
    Timer {
        id: frameCounter
        property int frames: 0
        property int round: 0
        interval: 5000
        repeat: true
        onTriggered: {
            if (round > 0)
                console.warn("FRAMES in 5 s: " + frames)
            frames = 0
            if (++round > 3) stop()
        }
    }
    Connections {
        target: frameCounter.running ? window : null
        function onFrameSwapped() { frameCounter.frames++ }
    }
    // Per misurare i passaggi fra finestre e pannelli: ogni passo si cronometra
    // (quanto blocca subito) e si guarda lo scatto piu' lungo che segue.
    Timer {
        id: switchBench
        property int step: 0
        property string pending: ""
        readonly property var steps: [
            ["tab awards", () => window.panelItem("tabs").setTab(0)],
            ["tab stats", () => window.panelItem("tabs").setTab(1)],
            ["tab qsl", () => window.panelItem("tabs").setTab(2)],
            ["tab activity", () => window.panelItem("tabs").setTab(3)],
            ["tab cluster", () => window.panelItem("tabs").setTab(4)],
            ["tab propagation", () => window.panelItem("tabs").setTab(5)],
            ["raise logbook", () => mainBoard.raiseKey("logbook")],
            ["raise map", () => mainBoard.raiseKey("map")],
            ["raise newqso", () => mainBoard.raiseKey("newqso")],
            ["open cluster window", () => window.openCluster(0)],
            ["activate main", () => { window.raise(); window.requestActivate() }],
            ["activate cluster", () => { clusterWindow.item.raise(); clusterWindow.item.requestActivate() }],
            ["activate main 2", () => { window.raise(); window.requestActivate() }],
            ["activate cluster 2", () => { clusterWindow.item.raise(); clusterWindow.item.requestActivate() }],
            ["activate main 3", () => { window.raise(); window.requestActivate() }],
            ["close cluster window", () => { clusterWindow.active = false }],
            ["open stats window", () => window.openStats()],
            ["close stats window", () => { statsWindow.active = false }],
            ["open awards", () => awardsDialog.openAt("dxcc")],
            ["close awards", () => awardsDialog.close()],
            ["open setup", () => setupDialog.open()],
            ["close setup", () => setupDialog.close()],
            ["open world clock", () => worldClockWindow.open()],
            ["close world clock", () => worldClockWindow.close()],
            ["detach callinfo", () => window.detachPanel("callinfo")],
            ["attach callinfo", () => window.attachPanel("callinfo")],
            ["enter contest", () => window.openContestDesk()],
            ["exit contest", () => window.exitContestMode()],
            ["enter contest 2", () => window.openContestDesk()],
            ["exit contest 2", () => window.exitContestMode()]
        ]
        interval: 1800
        repeat: true
        onTriggered: {
            if (pending.length)
                console.warn("SWITCH " + pending + " | scatto piu' lungo dopo: " + benchWatch.worst + " ms")
            if (step >= steps.length) { stop(); console.warn("SWITCH fine"); return }
            const s = steps[step++]
            benchWatch.worst = 0
            const t0 = Date.now()
            s[1]()
            pending = s[0] + ": " + (Date.now() - t0) + " ms subito"
        }
    }
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
            } else if (kind === "headerclick") {
                // Uno o piu' clic veri sull'intestazione della colonna: il log si ordina.
                const lp = window.panelItem("logbook").item
                const col = parseInt(args[1])
                const times = parseInt(args[2] || "1")
                const cell = lp.headerCell(col)
                const at = cell.mapToItem(null, 20, cell.height / 2)
                if (step - 11 < times) {
                    send(window, "hover", at)
                    send(window, "press", at)
                    send(window, "release", at)
                    return
                }
                stop()
                const calls = []
                for (let r = 0; r < Math.min(7, lp.model.count); ++r)
                    calls.push(lp.model.callAt(r))
                console.warn("PROBE headerclick " + lp.model.sortKey + " asc=" + lp.model.sortAscending + " " + calls.join(","))
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
        // Il cluster e' una finestra, ma deve comunque avere uno stato unico
        // nel menu Pannelli: dopo una chiusura la voce deve poterla riaprire.
        layout.hiddenPanels = window.panelListOf(layout.hiddenPanels)
                                    .filter(function (k) { return k !== "cluster" }).join(",")
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
        if (!layout.bandMapIntroduced) {
            const hidden = window.panelListOf(layout.hiddenPanels)
            if (hidden.indexOf("bandmap") < 0)
                layout.hiddenPanels = hidden.concat(["bandmap"]).join(",")
            layout.bandMapIntroduced = true
        }
        // Il pannello Decodium e' arrivato dopo: a chi aveva gia' la sua
        // lavagna si presenta chiuso, lo apre da Pannelli quando vuole.
        if (!layout.decodiumIntroduced) {
            const hidden = window.panelListOf(layout.hiddenPanels)
            if (hidden.indexOf("decodium") < 0)
                layout.hiddenPanels = hidden.concat(["decodium"]).join(",")
            layout.decodiumIntroduced = true
        }
        if (!layout.decodiumWindowsIntroduced) {
            let hidden = window.panelListOf(layout.hiddenPanels)
            for (const k of ["decfull", "decsig"])
                if (hidden.indexOf(k) < 0)
                    hidden = hidden.concat([k])
            layout.hiddenPanels = hidden.join(",")
            layout.decodiumWindowsIntroduced = true
        }
        if (!layout.firstRunGuideSeen && decolog.qsoCount === 0)
            Qt.callLater(firstRunGuide.open)
        const what = startupShow.split(":")
        if (what[0] === "contestdesk") window.openContestDesk()
        // Il banco della gara con la finestra dei moltiplicatori.
        else if (what[0] === "mults") {
            window.openContestDesk()
            contestLayout.setShown("mults", true)
            scpProbe.call = what[1] || "JA1ABC"
            scpProbe.start()
        }
        // Il banco della gara con la rete accesa.
        else if (what[0] === "net") {
            window.openContestDesk()
            decolog.net.enabled = true
            if (what[1]) decolog.net.stationName = what[1]
            contestLayout.setShown("net", true)
            if (what[2]) { benchTimer.left = parseInt(what[2]); benchTimer.start() }
            if (what[3]) decolog.net.sendGab(what[3])
        }
        // Il banco della gara con un pezzo di nominativo scritto: SCP e N+1.
        else if (what[0] === "scp") {
            window.openContestDesk()
            decolog.scp.loadTestCalls(["DL1ABC", "DL1ABD", "DL1AB", "K1ABC", "OH2ABC", "JA1ABC", "VK2ABC", "DL1XYZ"])
            scpProbe.call = what[1] || "DL1ABC"
            scpProbe.start()
        }
        // Per le prove: si entra e si esce, e la finestra principale deve
        // tornare com'era.
        else if (what[0] === "contestexit") { window.openContestDesk(); exitProbe.start() }
        else if (what[0] === "switchbench") switchBench.start()
        // Il trascinamento mentre il cluster manda spot a raffica.
        else if (what[0] === "dragflood") {
            spotFlood.rate = 60; spotFlood.left = 300; spotFlood.oneBand = true; spotFlood.start()
            dragBench.key = what[1] || "logbook"
            dragBench.board = mainBoard
            dragBench.mode = "move"
            dragBench.n = -600
            dragBench.start()
        }
        // Trascina un pannello come farebbe la mano: 90 movimenti a 60 al secondo,
        // e dice quanto costa ogni movimento e lo scatto piu' lungo.
        else if (what[0] === "dragbench") {
            if (what[2] === "contest") window.openContestDesk()
            dragBench.key = what[1] || "logbook"
            dragBench.board = what[2] === "contest" ? contestLayout : mainBoard
            dragBench.mode = what[3] || "move"
            dragBench.n = -60
            dragBench.start()
        }
        // Fermo, a guardare: ogni secondo lo scatto piu' lungo (per misurare con
        // un Decodium finto che manda Status e decodifiche).
        else if (what[0] === "idlewatch") { if (what[1] === "contest") window.openContestDesk(); idleWatch.left = parseInt(what[2] || "20"); idleWatch.start() }
        // Quanti fotogrammi disegna la finestra da ferma: dovrebbero essere pochi.
        else if (what[0] === "framecount") { if (what[1] === "contest") window.openContestDesk(); frameCounter.start() }
        // Per misurare: apre il banco e registra N QSO di fila, dicendo quanto
        // ci mette ognuno. Un QSO in gara deve essere istantaneo.
        // Per misurare: il banco aperto con il cluster che corre (uno spot ogni
        // 250 ms, come un RBN in gara) e ogni tanto un clic su uno spot.
        // Una valanga di spot come quella dell'RBN: `rate` al secondo per `secs` secondi.
        else if (what[0] === "spotflood") { spotFlood.rate = parseInt(what[1] || "40"); spotFlood.left = parseInt(what[2] || "20") * 10; spotFlood.oneBand = what[3] === "one"; spotFlood.start() }
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
        else if (what[0] === "setup") { setupDialog.page = parseInt(what[1] || "3"); setupDialog.open(); if (what[2] === "end") Qt.callLater(setupDialog.scrollToBottom); else if (what[2]) { const y = parseInt(what[2]); Qt.callLater(() => setupDialog.scrollTo(y)) } }
        else if (what[0] === "menu") window.panelItem("logbook").showMenu(what[1])
        // «editcall»: il log in primo piano e il nominativo della prima riga aperto alla correzione.
        else if (what[0] === "editcall") {
            mainBoard.raiseKey("logbook")
            Qt.callLater(() => window.panelItem("logbook").showMenu("editcall"))
        }
        else if (what[0] === "select") window.panelItem("logbook").showSelection(what[1], what[2])
        // Lavori di manutenzione, utili anche da riga di comando.
        else if (what[0] === "combo") { window.showPanel("cw"); comboTimer.start() }
        // Le macro CW da scrivere: "cwmacros" tutte, "cwmacros:0" solo F1.
        else if (what[0] === "cwmacros") { window.showPanel("cw"); cwMacrosTimer.start() }
        else if (what[0] === "cwkeys") { window.showPanel("cw"); cwKeysTimer.start() }
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
        // «editfield:id:colonna:valore»: corregge una cella del log come farebbe F2, e lo scrive nei registri.
        else if (what[0] === "editfield")
            console.log("editfield ->", JSON.stringify(decolog.editQsoField(parseInt(what[1]), what[2], what.slice(3).join(":"))))
        // «spotdialog[:nominativo:kHz:commento]»: il riquadro per segnalare una stazione al cluster.
        else if (what[0] === "spotdialog") spotDialog.openFor(what[1] || "", what[2] || "", what[3] || "")
        // «exportperiod[:dal:al]»: il riquadro per esportare un periodo, con date ISO se date.
        else if (what[0] === "exportperiod")
            exportPeriodDialog.openDialog(what[1] ? decolog.showDate(what[1]) : "", what[2] ? decolog.showDate(what[2]) : "")
        // La scelta del periodo per lo scarico LoTW, aperta per guardarla.
        else if (what[0] === "lotwperiod") { window.panelItem("tabs").setTab(2); lotwPeriodTimer.start() }
        // La previsione verso un locatore, con VOACAP se c'e'.
        else if (what[0] === "forecast") forecastTimer.start()
        // Per le prove: il recupero dal log di Decodium, degli ultimi N giorni (0 = tutto).
        else if (what[0] === "recover") decolog.recoverFromDecodium(parseInt(what[1] || "0"))
        else if (what[0] === "pop") popWindow.active = true
        // Un riquadro d'avviso DX, per vederlo.
        else if (what[0] === "toast") { dxToast.show("NEW DXCC", "3Y0J 14025.0 CW Bouvet · New DXCC", "3Y0J|20m|CW"); dxToast.show("NEW IOTA", "IH9R 7012.0 CW Italy · IOTA AF-018", "IH9R|40m|CW") }
        // L'orologio mondiale aperto, con la citta' scelta se c'e'.
        else if (what[0] === "worldclock") { if (what[1]) decolog.worldClock.selected = what[1]; worldClockWindow.open() }
        else if (what[0] === "panels") { if (what[1]) { const how = what.slice(2); for (let i = 0; i < how.length; ++i) { if (what[1] === "close") window.closePanel(how[i]); else if (what[1] === "detach") window.detachPanel(how[i]); else if (what[1] === "show") window.showPanel(how[i]); else if (what[1] === "attach") window.attachPanel(how[i]) } } else panelsPopup.openFrom(null) }
        // "cluster:spot:14025.1:3Y0J:CW" mette una riga come se venisse da un
        // nodo e ci fa sopra il doppio clic: serve a guardare se la radio ci va.
        // La previsione verso un locatore, nella scheda Propagazione.
        else if (what[0] === "pathforecast") { if (what[2] === "detach") window.detachPanel("tabs"); window.panelItem("tabs").setTab(5); decolog.lookupCall = what[1] || "JA1ABC" }
        // La chat, con qualche messaggio finto.
        else if (what[0] === "chat") {
            decolog.chat.injectLine("Welcome to the ON4KST chat")
            decolog.chat.injectLine("1230Z IK1ABC Mario> QRV 144.300 JN45 any sked?")
            decolog.chat.injectLine("1231Z DL1XX Hans> (IU8LMC) pse 144.310 in 5 min")
            decolog.chat.injectLine("1232Z OK1YY Jan> EME tonight 432.050")
            window.openChat()
        }
        // La band map aperta, con una manciata di spot su 20 m.
        else if (what[0] === "bandmap") {
            window.showPanel("bandmap")
            const now = new Date()
            const hhmm = ("0" + now.getUTCHours()).slice(-2) + ("0" + now.getUTCMinutes()).slice(-2)
            const lines = [["14005.0", "3Y0J", "CW"], ["14012.5", "JA1ABC", "CW"], ["14013.1", "VK9XX", "CW"],
                           ["14013.4", "K1ABC", "CW"], ["14025.0", "ZL1AA", "CW"], ["14074.0", "FT8WW", "FT8"],
                           ["14195.0", "VP8ABC", "SSB"], ["14205.0", "IH9R", "SSB IOTA AF-018"], ["14244.0", "W1AW", "SSB"],
                           ["14290.0", "PY2XX", "SSB"]]
            for (let i = 0; i < lines.length; ++i)
                decolog.cluster.injectLine("DX de IK0TEST:  " + lines[i][0] + "  " + lines[i][1] + "  " + lines[i][2] + "  " + hhmm + "Z")
        }
        else if (what[0] === "cluster" && what[1] === "spot") {
            const now = new Date()
            const hhmm = ("0" + now.getUTCHours()).slice(-2) + ("0" + now.getUTCMinutes()).slice(-2)
            decolog.cluster.injectLine("DX de IK0TEST:  " + what[2] + "  " + what[3]
                                       + "  " + (what[4] || "CW") + " 18 dB  " + hhmm + "Z")
            clusterTuneTimer.start()
        }
        // «cluster check»: la scheda Fonti con tre fonti spente — un nodo vero, una
        // porta chiusa, un nome che non esiste — e il controllo lanciato su ognuna.
        else if (what[0] === "cluster" && what[1] === "check") {
            const c = decolog.cluster
            c.addSource({ name: "IQ8DO Caserta", type: "cluster", host: "iq8do.aricaserta.it", port: 7300, enabled: false })
            c.addSource({ name: "Nodo spento", type: "cluster", host: "127.0.0.1", port: 9, enabled: false })
            c.addSource({ name: "Nome sbagliato", type: "cluster", host: "nonexistent.invalid", port: 7300, enabled: false })
            openCluster(1)
            c.sources.forEach(s => c.checkSource(s.id))
        }
        else if (what[0] === "cluster") openCluster(parseInt(what[1] || "0"))
        else if (what[0] === "logs") logsDialog.openDialog()
        else if (what[0] === "qslimport") qslImportDialog.open()
        // Il pannello Decodium; "decodium demo" ci mette qualche decodifica finta.
        // Le finestre vere di Decodium: "decwin" apre Full Spectrum e Signal RX.
        else if (what[0] === "decwin") {
            window.showPanel("decfull")
            window.showPanel("decsig")
        }
        else if (what[0] === "decodium") {
            window.showPanel("decodium")
            if (what[1] === "demo") decolog.decodium.demo()
            if (what[1] === "menu") { decolog.decodium.demo(); Qt.callLater(() => window.panelItem("decodium").showMenu("row")) }
            // Largo: i due elenchi uno accanto all'altro.
            if (what[1] === "wide") {
                decolog.decodium.demo()
                mainBoard.saveGeometry("decodium", { x: mainBoard.width * 0.21, y: 0, w: mainBoard.width * 0.57, h: mainBoard.height * 0.45 })
            }
        }
        // Il monitor del traffico; "traffic demo" ci fa passare qualche riga finta.
        else if (what[0] === "traffic") {
            trafficWindow.open()
            if (what[1] === "demo") decolog.traffic.demo()
        }
        // La ricerca in alto (Ctrl+F): con poco posto si apre sotto la lente.
        else if (what[0] === "search") {
            if (what[1]) decolog.qsoModel.filterText = what[1]
            Qt.callLater(window.focusSearch)
        }
        else if (what[0] === "activation") { activationDialog.openDialog(what[1] || ""); if (what[2] === "choose") Qt.callLater(activationDialog.chooseContest) }
        else if (what[0] === "modes") window.panelItem("newqso").showModes()
        // Per le prove: le schede audio come le vede la scelta del decoder e del DVK.
        else if (what[0] === "audioprobe")
            console.warn("PROBE audio in=" + JSON.stringify(decolog.rig.audioInputDevices.map(d => d.label))
                         + " index=" + decolog.rig.audioInputIndex + " saved=" + decolog.rig.audioInput
                         + " | dvk out=" + JSON.stringify(decolog.dvk.outputDevices.map(d => d.label))
                         + " index=" + decolog.dvk.outputIndex)
        else if (what[0] === "rstprobe")
            console.warn("PROBE rst " + window.panelItem("newqso").probeReports(["SSB", "CW", "FT8", "RTTY", "FT4", "SSB"]))
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
        // Il ripristino: "restore" apre la finestra, "restore:<file>" lo fa
        // davvero (il programma riparte), per le prove da riga di comando.
        else if (what[0] === "restore") {
            if (what.length > 1) decolog.restoreBackup(what.slice(1).join(":"))
            else restoreDialog.openDialog()
        }
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
            panelsPopup.openFrom(null)
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
        else if (what[0] === "headerclick") {
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
        else if (what[0] === "stats") { openStats(); if (what[1] === "awards" && statsWindow.item) statsWindow.item.page = 1 }
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
        else if (what[0] === "newqsofields") newQsoFieldsTimer.start()
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
    Timer { id: lotwPeriodTimer; interval: 1200; onTriggered: window.panelItem("tabs").item.openLotwPeriod() }
    Timer { id: forecastTimer; interval: 1200
           onTriggered: window.panelItem("tabs").item.showForecast(startupShow.split(":")[1] || "PM95") }
    Timer { id: comboTimer; interval: 800
           onTriggered: { const it = window.panelItem("cw"); if (it) it.showCombo() } }
    // Per le prove: ogni tasto delle macro deve mandare la sua, non la prima.
    Timer { id: cwKeysTimer; interval: 800
           onTriggered: { const it = window.panelItem("cw"); if (it) console.warn("PROBE cw keys " + JSON.stringify(it.keyTargets())) } }
    Timer { id: cwMacrosTimer; interval: 800
           onTriggered: { const it = window.panelItem("cw"); if (it) it.showMacros(parseInt(startupShow.split(":")[1] || "-1")) } }
    Timer { id: combo2Timer; interval: 900; onTriggered: newQsoDialog.showBandCombo() }
    Timer {
        id: newQsoFieldsTimer
        interval: 700
        onTriggered: {
            const panel = window.panelItem("newqso")
            if (panel && panel.item)
                panel.item.showFieldSettings()
        }
    }
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
    // Il riepilogo delle conferme scaricate: si apre da solo dopo uno scarico
    // chiesto a mano che ha portato qualcosa, e dalla scheda QSL.
    QslImportDialog {
        id: qslImportDialog
        onOpenQsoRequested: (id) => window.openQso(id)
    }
    Connections {
        target: decolog
        function onQslImportReady() { qslImportDialog.open() }
    }
    function openQslImport() { qslImportDialog.open() }
    // Il monitor del traffico con Decodium: UDP, DecoLink, DecoPort, a due vie.
    TrafficMonitorWindow { id: trafficWindow }
    UpdateDialog { id: updateDialog }

    // Quando il controllo trova una versione nuova la finestra si apre da se':
    // e' l'unico momento in cui l'aggiornamento si fa vedere.
    Connections {
        target: decolog.updates
        function onUpdateFound(version) { updateDialog.open() }
    }

    SetupDialog {
        id: setupDialog
        onUpdateRequested: updateDialog.open()
        onRestoreRequested: restoreDialog.openDialog()
    }
    FirstRunGuide {
        id: firstRunGuide
        onSetupRequested: {
            layout.firstRunGuideSeen = true
            close()
            setupDialog.page = 6
            setupDialog.open()
        }
        onNewQsoRequested: {
            layout.firstRunGuideSeen = true
            close()
            mainBoard.raiseKey("newqso")
        }
        onClosed: layout.firstRunGuideSeen = true
    }
    RestoreBackupDialog { id: restoreDialog }

    // Gli avvisi DX che chiedono il riquadro: sopra a tutto, in alto a destra.
    DxAlertToast {
        id: dxToast
        anchors.fill: parent
        anchors.topMargin: 48
        z: 1000
    }
    Connections {
        target: decolog.cluster
        function onAlertNotify(title, text, spotKey, popup, sound) {
            if (popup)
                dxToast.show(title, text, spotKey)
        }
    }
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
        id: spotFlood
        property int rate: 40
        property int left: 0
        property int n: 0
        property int spent: 0
        property bool oneBand: false
        interval: 100
        repeat: true
        onTriggered: {
            if (left <= 0) { stop(); console.warn("FLOOD fine"); return }
            left--
            const per = Math.max(1, Math.round(rate / 10))
            const t0 = Date.now()
            const hhmm = decolog.utcNow().time.replace(":", "").substring(0, 4)
            const bands = [7074, 10136, 14074, 18100, 21074, 24915, 28074, 14025, 7025, 3573]
            const pre = ["K", "DL", "JA", "PY", "UA", "G", "I", "EA", "VK", "W", "LU", "ZS", "OH", "SM", "VE"]
            for (let i = 0; i < per; ++i) {
                n++
                const call = pre[n % pre.length] + (n % 10) + String.fromCharCode(65 + (n * 7) % 26) + String.fromCharCode(65 + (n * 3) % 26) + String.fromCharCode(65 + n % 26)
                const khz = oneBand ? (14074 + (n * 37) % 3000 / 1000).toFixed(1) : (bands[n % bands.length] + (n % 3) * 0.1).toFixed(1)
                decolog.cluster.injectLine("DX de " + pre[(n * 5) % pre.length] + (n % 9) + "ABC-#:   " + khz + "  " + call
                                           + "  FT8 -" + (n % 20) + " dB " + hhmm + "Z")
            }
            spent += Date.now() - t0
            if (left % 10 === 0) {
                console.warn("FLOOD " + rate + "/s | ms spesi nell'ultimo secondo: " + spent + " | scatto piu' lungo: " + benchWatch.worst + " ms")
                spent = 0
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
        running: benchTimer.running || spotBench.running || typeBench.running || switchBench.running || spotFlood.running
                 || idleWatch.running || dragBench.running
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
        // Col biglietto d'avvio davanti la domanda aspetta che si chiuda.
        Component.onCompleted: if (decolog.logs.askAtStart && !startupBadge) Qt.callLater(logsDialog.openDialog)
    }
    AwardsDialog {
        id: awardsDialog
        onOpenQso: (id) => window.openQso(id)
    }

    FileDialog {
        id: importDialog
        title: qsTr("Import a log")
        // L'ADIF di qualunque programma, un foglio di calcolo, il database di N1MM.
        nameFilters: [qsTr("Logs (*.adi *.adif *.csv *.tsv *.s3db)"), qsTr("ADIF files (*.adi *.adif)"),
                      qsTr("Spreadsheets (*.csv *.tsv)"), qsTr("N1MM Logger+ databases (*.s3db)"),
                      qsTr("All files (*)")]
        onAccepted: {
            pendingImport = selectedFile
            importPreview = decolog.previewImport(selectedFile)
            importPreviewDialog.open()
        }
    }
    property url pendingImport
    property var importPreview: ({})
    // Una finestra del programma, come le altre: un Dialog di stile resta
    // chiaro anche con i temi scuri (Ocean Blue) e il testo non si legge.
    DialogFrame {
        id: importPreviewDialog
        title: qsTr("Import preview")
        dialogKey: "import-preview"
        dotColor: Theme.primaryColor
        width: 600
        height: 300
        onAccepted: decolog.importAdif(window.pendingImport)
        body: ColumnLayout {
            anchors.margins: 16
            spacing: 10
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: window.importPreview.ok
                      ? qsTr("%1 records read from %2.\nEstimated result: %3 new · %4 duplicates · %5 rejected.\nQSL status: %6 · incomplete UTC: %7.\nExtra fields are preserved: %8")
                          .arg(window.importPreview.read).arg(window.importPreview.kind || "file")
                          .arg(window.importPreview.estimatedInserted || 0)
                          .arg(window.importPreview.estimatedDuplicates || 0)
                          .arg(window.importPreview.estimatedInvalid || 0)
                          .arg(window.importPreview.qslRecords).arg(window.importPreview.missingUtc)
                          .arg((window.importPreview.extraFields || []).join(", ") || qsTr("none"))
                      : qsTr("Cannot preview this import: %1").arg(window.importPreview.error || qsTr("unknown error"))
                color: Theme.textPrimary
            }
            Text {
                Layout.fillWidth: true
                visible: !!window.importPreview.ok && (window.importPreview.missingUtc || 0) > 0
                text: qsTr("Records with incomplete UTC will be rejected during import.")
                color: Theme.warningColor
                wrapMode: Text.Wrap
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Item { Layout.fillWidth: true }
                GlassButton {
                    text: qsTr("Cancel")
                    onClicked: importPreviewDialog.reject()
                }
                GlassButton {
                    text: qsTr("Import")
                    tone: Theme.accentColor
                    enabled: !!window.importPreview.ok
                    onClicked: importPreviewDialog.accept()
                }
            }
        }
    }
    FileDialog {
        id: exportDialog
        title: qsTr("Export ADIF")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "adi"
        nameFilters: [qsTr("ADIF files (*.adi)")]
        onAccepted: decolog.exportAdif(selectedFile)
    }
    ExportPeriodDialog { id: exportPeriodDialog }
    SpotDialog { id: spotDialog }

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
            onClosing: Qt.callLater(function () {
                if (!window.quitting)
                    clusterWindow.active = false
            })
        }
    }

    Timer {
        id: scpProbe
        property string call: ""
        interval: 1500
        onTriggered: {
            const p = contestLayout.panelFor("contest")
            if (p && p.item)
                p.item.pickSpot(call, "", "")
        }
    }

    // La chat ON4KST, in una finestra sua.
    Loader {
        id: chatWindow
        active: false
        sourceComponent: ChatWindow {
            onClosing: Qt.callLater(function () { chatWindow.active = false })
        }
    }
    function openChat() {
        chatWindow.active = true
        if (chatWindow.item) {
            chatWindow.item.raise()
            chatWindow.item.requestActivate()
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
    // Cambiare detachedPanels distrugge/crea delegate di Instantiator. Durante
    // quella transizione onClosing della finestra nativa puo' arrivare di
    // nuovo e riagganciare subito il pannello appena staccato (o viceversa).
    // Il guard evita che una singola azione venga applicata due volte.
    property bool panelStateTransition: false
    property bool syncingDetachedWindows: false

    function finishPanelStateTransition() {
        Qt.callLater(function () { window.panelStateTransition = false })
    }

    Connections {
        target: Qt.application
        function onAboutToQuit() {
            window.quitting = true
            window.persistDetachedWindowState()
        }
    }

    ListModel {
        id: detachedModel
        // All'avvio le finestre staccate sono quelle dell'ultima volta.
        Component.onCompleted: window.syncDetachedWindows()
    }

    function syncDetachedWindows() {
        window.syncingDetachedWindows = true
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
        Qt.callLater(function () { window.syncingDetachedWindows = false })
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

    function persistDetachedWindowState() {
        for (let i = 0; i < detachedWindows.count; ++i) {
            const win = detachedWindows.objectAt(i)
            if (win && win.persistPlacement)
                win.persistPlacement()
        }
        if (contestLayout && contestLayout.persistFloatingWindows)
            contestLayout.persistFloatingWindows()
        if (rotorWindow.item && rotorWindow.item.persistPlacement)
            rotorWindow.item.persistPlacement()
        if (clusterWindow.item && clusterWindow.item.persistPlacement)
            clusterWindow.item.persistPlacement()
        // hiddenPanels/detachedPanels descrivono lo stato che deve tornare al
        // prossimo avvio. Lo si scrive prima di Qt.quit(), non affidandosi
        // all'ordine con cui QML distrugge finestre e Settings.
        layout.sync()
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
                if (window.quitting || window.panelStateTransition || window.syncingDetachedWindows)
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
        // Deve restare nell'overlay della finestra principale. Popup.Window
        // crea una QWindow separata che su alcune combinazioni Qt/KDE finisce
        // dietro la finestra principale: il menu sembra aperto, ma i suoi
        // comandi non sono raggiungibili. Item mantiene coordinate e input
        // nello stesso scene graph dei pannelli.
        popupType: Popup.Item
        parent: Overlay.overlay
        // Per il normale pulsante della barra in alto il menu deve comparire
        // subito sotto al pulsante. Il fallback resta utile ai comandi da menu,
        // alle scorciatoie e alle prove senza mouse.
        property Item opener: null
        function placeNearOpener() {
            const margin = 8
            const menuHeight = height > 0 ? height : implicitHeight
            let targetX = window.width - width - 16
            let targetY = 72

            if (opener) {
                const below = opener.mapToItem(Overlay.overlay, 0, opener.height + 6)
                targetX = below.x
                targetY = below.y
                // Se non entra sotto, lo mettiamo sopra al comando. Su uno
                // schermo molto basso il margine superiore e' piu' utile che
                // lasciarne una parte fuori dalla finestra.
                if (targetY + menuHeight > window.height - margin)
                    targetY = opener.mapToItem(Overlay.overlay, 0, -menuHeight - 6).y
            }

            const maxX = Math.max(margin, window.width - width - margin)
            const maxY = Math.max(margin, window.height - menuHeight - margin)
            x = Math.round(Math.max(margin, Math.min(targetX, maxX)))
            y = Math.round(Math.max(margin, Math.min(targetY, maxY)))
        }
        function openFrom(openerItem) {
            opener = openerItem || null
            placeNearOpener()
            open()
            // Il contenuto puo' calcolare l'altezza definitiva soltanto al
            // primo frame: rifacciamo il clamp senza far dipendere il risultato
            // dal compositor o dal sistema operativo.
            Qt.callLater(placeNearOpener)
        }
        onOpened: placeNearOpener()
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
                // Fuori dalla gara score/rate/contest sono pannelli contest-only
                // e non hanno una casella nella lavagna. Il cluster e' invece
                // una finestra propria, ma resta qui con lo stesso stato del
                // comando Cluster in alto, cosi' si puo' riaprire dopo una X.
                model: window.contestModeOn ? contestLayout.allKeys
                                             : window.boardKeys.concat(["cluster", "dvk"])
                delegate: Rectangle {
                    id: panelRow
                    required property string modelData
                    readonly property bool normalWindowOnly: !window.contestModeOn
                                                              && window.isWindowOnly(panelRow.modelData)
                    readonly property bool closed: window.contestModeOn
                                                   ? !contestLayout.isShown(panelRow.modelData)
                                                   : panelRow.modelData === "cluster"
                                                     ? !clusterWindow.active
                                                     : panelRow.normalWindowOnly
                                                       ? !window.isPanelDetached(panelRow.modelData)
                                                     : window.isPanelHidden(panelRow.modelData)
                    readonly property bool floating: window.contestModeOn
                                                     ? contestLayout.isFloating(panelRow.modelData)
                                                     : panelRow.modelData === "cluster"
                                                       ? false : window.isPanelDetached(panelRow.modelData)
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
                            visible: !panelRow.normalWindowOnly
                            glyph: panelRow.floating ? "↩" : "⤢"
                            hint: panelRow.floating
                                  ? qsTr("Put it back in the main window")
                                  : qsTr("Detach it into its own window")
                            onClicked: {
                                if (window.contestModeOn) {
                                    if (panelRow.closed)
                                        contestLayout.setShown(panelRow.modelData, true)
                                    contestLayout.setFloating(panelRow.modelData, !panelRow.floating)
                                } else if (panelRow.modelData === "cluster") {
                                    window.togglePanel(panelRow.modelData)
                                } else if (panelRow.floating) {
                                    window.attachPanel(panelRow.modelData)
                                } else {
                                    window.detachPanel(panelRow.modelData)
                                }
                                panelsPopup.close()
                            }
                        }
                    }

                    MouseArea {
                        id: rowArea
                        anchors.fill: parent
                        anchors.rightMargin: panelRow.normalWindowOnly ? 0 : 24
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (window.contestModeOn)
                                contestLayout.toggle(panelRow.modelData)
                            else
                                window.togglePanel(panelRow.modelData)
                            panelsPopup.close()
                        }
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.borderSoft }
            Text {
                Layout.fillWidth: true
                text: qsTr("Operational presets")
                color: Theme.secondaryColor
                font.family: Theme.monoFamily
                font.pixelSize: 11
                font.bold: true
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Choose a starting layout; your current layout is changed only after selection.")
                color: Theme.textSecondary
                font.pixelSize: 10
                wrapMode: Text.Wrap
            }
            Repeater {
                model: window.operationalPresets
                delegate: GlassButton {
                    required property var modelData
                    Layout.fillWidth: true
                    text: modelData.title
                    buttonHeight: 23
                    fontPixelSize: 10
                    onClicked: {
                        window.applyOperationalPreset(modelData)
                        panelsPopup.close()
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
            onTriggered: panelsPopup.openFrom(null)
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
            onExportPeriodRequested: exportPeriodDialog.openDialog()
            onSpotRequested: spotDialog.openCurrent()
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
            onPanelsRequested: function(opener) {
                if (panelsPopup.opened)
                    panelsPopup.close()
                else
                    panelsPopup.openFrom(opener)
            }
            onAboutRequested: aboutDialog.open()
            onChatRequested: window.openChat()
            onTrafficRequested: trafficWindow.open()
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

        StatusRail {
            id: statusRail
            Layout.fillWidth: true
            onWorldClockRequested: worldClockWindow.opened ? worldClockWindow.close() : worldClockWindow.open()
        }
    }

    // L'orologio mondiale in grande, sopra a tutto.
    WorldClockWindow {
        id: worldClockWindow
        opener: statusRail.clockButton
    }
    // A finestra ridotta a icona l'orologio si ferma: nessuno lo guarda.
    Binding {
        target: decolog.worldClock
        property: "active"
        value: window.visibility !== Window.Minimized && window.visibility !== Window.Hidden
    }

    // La lavagna di tutti i giorni.
    ContestLayout {
        id: mainBoard
        contestMode: false
        external: true
        settingsCategory: "layout/mainboard"
        allKeys: window.boardKeys
        defaultKeys: window.boardKeys.filter(k => k !== "cw" && k !== "bandmap" && k !== "decodium" && k !== "decfull" && k !== "decsig")
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
            ft2:      { x: 0.60, y: 0.68, w: 0.18, h: 0.32 },
            bandmap:  { x: 0.60, y: 0.00, w: 0.18, h: 0.68 },
            decodium: { x: 0.50, y: 0.30, w: 0.28, h: 0.38 },
            decfull:  { x: 0.00, y: 0.68, w: 0.40, h: 0.32 },
            decsig:   { x: 0.40, y: 0.68, w: 0.30, h: 0.32 }
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
        quitting: window.quitting
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

    // Il biglietto d'avvio, sopra tutto: chi lo fa, con che cosa, e il caffe'.
    Loader {
        id: startupBadgeLoader
        anchors.fill: parent
        z: 10000
        active: startupBadge
        sourceComponent: StartupBadge {
            onFinished: {
                startupBadgeLoader.active = false
                if (decolog.logs.askAtStart)
                    logsDialog.openDialog()
            }
        }
        onLoaded: item.forceActiveFocus()
    }
}
