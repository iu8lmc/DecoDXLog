// DecoDXLog — la lavagna magnetica.
//
// La finestra principale e' una lavagna, in gara e fuori: i pannelli ci
// stanno sopra liberi, e ognuno si mette dove serve a chi opera. Ce ne sono
// due, con disposizioni separate: quella della gara (i pannelli della gara,
// che la lavagna apre, chiude e stacca da se') e quella di tutti i giorni
// (`external`: chi e' chiuso o in finestra lo decide la finestra principale,
// che ha le sue finestre staccate e il suo menu Pannelli). Si prendono
// per la testata e si spostano in qualsiasi punto; si ridimensionano dai bordi
// e dagli angoli; vicino al bordo della lavagna o a un altro pannello si
// attaccano da soli, come calamite, cosi' si allineano senza fatica. Un clic
// porta un pannello davanti agli altri.
//
// Ogni pannello si stacca anche dalla lavagna (⤢) e diventa una finestra sua,
// da mettere su un altro monitor; con ↩ torna dove era. La ✕ lo chiude, e dal
// menu Contest Mode in alto si riapre.
//
// Posizioni e misure si ricordano in proporzione alla lavagna: allargando o
// stringendo la finestra, la disposizione la segue.
import QtQuick
import QtQuick.Controls
import QtCore
import Decodium.UI

Item {
    id: root

    // Quello che i pannelli chiedono e che sa fare solo la finestra principale.
    signal openQso(var id)
    signal awardRequested(string id)
    signal clusterRequested(int tab)
    signal statsRequested()
    signal rotorRequested()
    signal contestRequested()
    // Solo con `external`: la finestra principale chiude e stacca.
    signal closeRequested(string key)
    signal detachRequested(string key)
    // Il tasto destro sulla testata (il menu della disposizione) e il ⤢
    // dell'inserimento, che apre la finestra grande.
    signal menuRequested(string key, real screenX, real screenY)
    signal expandRequested()
    // Il titolo di un pannello, per la finestra quando lo si stacca.
    property var titleOf: function (key) { return key }

    // La lavagna della gara (i pannelli lo sanno) o quella di tutti i giorni.
    property bool contestMode: true
    // Dove si ricorda la disposizione.
    property string settingsCategory: "layout/board"
    // Chiusi e staccati li tiene la finestra principale: qui arrivano le liste.
    property bool external: false
    property var externalShown: []
    property var externalFloating: []
    // Durante la chiusura generale le finestre native ricevono onClosing. Non
    // e' un comando dell'operatore per riagganciare il pannello: lo stato
    // flottante deve restare salvato per il prossimo avvio.
    property bool quitting: false
    // Disposizione bloccata: niente spostamenti ne' misure.
    property bool locked: false
    property string emptyText: qsTr("All the contest panels are closed: open them again from Contest Mode, up in the bar.")
    property string floatingText: qsTr("All the contest panels are in their own windows: ↩ in a panel brings it back here.")

    // I pannelli, nell'ordine del menu.
    property var allKeys: ["contest", "cluster", "logbook", "callinfo", "rate", "score", "map", "cw", "bandmap", "decodium", "decfull", "decsig", "net", "dvk", "mults"]
    property var defaultKeys: ["contest", "cluster", "logbook", "callinfo", "rate", "score", "map"]
    // Dove stanno all'inizio, in proporzione alla lavagna: in gara tre
    // colonne, come nei programmi da gara.
    property var defaultGeometry: ({
        cluster:  { x: 0.00, y: 0.00, w: 0.24, h: 1.00 },
        contest:  { x: 0.24, y: 0.00, w: 0.52, h: 0.30 },
        logbook:  { x: 0.24, y: 0.30, w: 0.52, h: 0.40 },
        callinfo: { x: 0.24, y: 0.70, w: 0.29, h: 0.30 },
        cw:       { x: 0.53, y: 0.70, w: 0.23, h: 0.30 },
        score:    { x: 0.76, y: 0.00, w: 0.24, h: 0.30 },
        rate:     { x: 0.76, y: 0.30, w: 0.24, h: 0.34 },
        map:      { x: 0.76, y: 0.64, w: 0.24, h: 0.36 },
        bandmap:  { x: 0.00, y: 0.00, w: 0.16, h: 1.00 },
        decodium: { x: 0.53, y: 0.30, w: 0.30, h: 0.40 },
        decfull:  { x: 0.24, y: 0.70, w: 0.30, h: 0.30 },
        decsig:   { x: 0.54, y: 0.70, w: 0.22, h: 0.30 },
        net:      { x: 0.53, y: 0.40, w: 0.30, h: 0.40 },
        dvk:      { x: 0.53, y: 0.60, w: 0.30, h: 0.40 },
        mults:    { x: 0.76, y: 0.00, w: 0.24, h: 0.64 }
    })
    // Quanto vicino deve arrivare un bordo per attaccarsi, e lo spazio che
    // resta fra due pannelli attaccati.
    readonly property real magnet: 12
    readonly property real gap: 4

    Settings {
        id: saved
        category: root.settingsCategory
        // Vuoto: mai scelto, si parte con quelli di serie. "-": tutti chiusi.
        property string shown: ""
        // Quelli staccati in una finestra loro.
        property string floating: ""
        // {chiave: {x, y, w, h}} in proporzione alla lavagna.
        property string geometry: ""
        // Chi sta davanti: l'ultimo della lista.
        property string order: ""
    }

    function listOf(text, fallback) {
        const out = []
        const parts = text === "-" ? [] : text.length > 0 ? text.split(",") : fallback
        for (let i = 0; i < parts.length; ++i) {
            const k = String(parts[i]).trim()
            if (root.allKeys.indexOf(k) >= 0 && out.indexOf(k) < 0)
                out.push(k)
        }
        return out
    }
    readonly property var shownList: root.external ? root.externalShown.filter(k => root.allKeys.indexOf(k) >= 0)
                                                   : root.listOf(saved.shown, root.defaultKeys)
    readonly property var floatingList: (root.external ? root.externalFloating : root.listOf(saved.floating, []))
                                        .filter(k => root.shownList.indexOf(k) >= 0)
    readonly property var orderList: {
        const out = root.listOf(saved.order, [])
        for (const k of root.allKeys) {
            if (out.indexOf(k) < 0)
                out.unshift(k)
        }
        return out
    }
    readonly property var geometry: {
        let stored = {}
        try { stored = saved.geometry.length > 0 ? JSON.parse(saved.geometry) : {} } catch (e) { stored = {} }
        const out = {}
        for (const k of root.allKeys) {
            const g = stored[k]
            out[k] = g && g.w > 0 && g.h > 0 ? g : root.defaultGeometry[k]
        }
        return out
    }

    function isShown(key) { return root.shownList.indexOf(key) >= 0 }
    function isFloating(key) { return root.floatingList.indexOf(key) >= 0 }
    function setShown(key, on) {
        if (root.external) {
            if (!on)
                root.closeRequested(key)
            else
                root.raiseKey(key)
            return
        }
        const list = root.shownList.slice()
        const i = list.indexOf(key)
        if (on && i < 0)
            list.push(key)
        else if (!on && i >= 0)
            list.splice(i, 1)
        saved.shown = list.length > 0 ? list.join(",") : "-"
        if (!on)
            root.setFloating(key, false)
        else
            root.raiseKey(key)
    }
    function toggle(key) { root.setShown(key, !root.isShown(key)) }
    function setFloating(key, on) {
        if (root.external) {
            if (on)
                root.detachRequested(key)
            return
        }
        const list = root.listOf(saved.floating, [])
        const i = list.indexOf(key)
        if (on && i < 0)
            list.push(key)
        else if (!on && i >= 0)
            list.splice(i, 1)
        saved.floating = list.join(",")
    }
    function raiseKey(key) {
        const list = root.orderList.filter(k => k !== key)
        list.push(key)
        saved.order = list.join(",")
    }
    function saveGeometry(key, px) {
        const all = Object.assign({}, root.geometry)
        all[key] = {
            x: Math.max(0, px.x / root.width),
            y: Math.max(0, px.y / root.height),
            w: Math.max(0.05, px.w / root.width),
            h: Math.max(0.05, px.h / root.height)
        }
        saved.geometry = JSON.stringify(all)
    }
    // All'ingresso in una gara in telegrafia la CW si apre da sola.
    function prepare(cw) {
        if (cw && !root.isShown("cw"))
            root.setShown("cw", true)
    }
    // Pannelli, posizioni e misure di partenza; quelli staccati rientrano.
    function resetLayout() {
        if (!root.external) {
            saved.shown = root.defaultKeys.concat(decolog.activation.isCwContest() ? ["cw"] : []).join(",")
            saved.floating = ""
        }
        saved.geometry = ""
        saved.order = ""
    }

    // Prima dell'uscita rende durevoli sia l'elenco dei pannelli in finestra
    // sia la geometria di ciascuna finestra. PanelWindow fa gia' il proprio
    // sync; qui lo si richiama prima che l'albero QML venga distrutto.
    function persistFloatingWindows() {
        for (let i = 0; i < floatingWindows.count; ++i) {
            const w = floatingWindows.objectAt(i)
            if (w && w.persistPlacement)
                w.persistPlacement()
        }
        saved.sync()
    }
    function panelFor(key) {
        for (let i = 0; i < boardPanels.count; ++i) {
            const p = boardPanels.itemAt(i)
            if (p && p.key === key)
                return p
        }
        return null
    }

    // ── Le calamite ─────────────────────────────────────────────────────────
    // I bordi della lavagna e degli altri pannelli, in pixel: quelli a filo e
    // quelli a un passo di distanza, per affiancare senza sovrapporre.
    function edgesExcept(key) {
        const xs = [0, root.width]
        const ys = [0, root.height]
        for (let i = 0; i < boardPanels.count; ++i) {
            const p = boardPanels.itemAt(i)
            if (!p || p.key === key || !p.visible)
                continue
            xs.push(p.x - root.gap, p.x + p.width + root.gap, p.x, p.x + p.width)
            ys.push(p.y - root.gap, p.y + p.height + root.gap, p.y, p.y + p.height)
        }
        return { xs: xs, ys: ys }
    }
    // Il valore piu' vicino fra i candidati, se abbastanza vicino.
    function nearest(value, candidates) {
        let best = value
        let dist = root.magnet
        for (const c of candidates) {
            const d = Math.abs(c - value)
            if (d < dist) {
                dist = d
                best = c
            }
        }
        return best
    }
    // Spostando: il bordo sinistro o il destro si attacca, e cosi' sopra o sotto.
    function snapMove(key, r) {
        const e = root.edgesExcept(key)
        const left = root.nearest(r.x, e.xs)
        const right = root.nearest(r.x + r.w, e.xs)
        let x = r.x
        if (left !== r.x) x = left
        else if (right !== r.x + r.w) x = right - r.w
        const top = root.nearest(r.y, e.ys)
        const bottom = root.nearest(r.y + r.h, e.ys)
        let y = r.y
        if (top !== r.y) y = top
        else if (bottom !== r.y + r.h) y = bottom - r.h
        // Sulla lavagna: un pannello non si perde fuori dal bordo.
        x = Math.max(0, Math.min(x, root.width - Math.min(r.w, root.width)))
        y = Math.max(0, Math.min(y, root.height - Math.min(r.h, root.height)))
        return { x: x, y: y, w: r.w, h: r.h }
    }

    // Per le prove: un pannello spostato come col mouse, di dx e dy pixel.
    function testMove(key, dx, dy) {
        if (root.locked)
            return
        const p = root.panelFor(key)
        if (!p)
            return
        p.begin()
        const r = root.snapMove(key, { x: p.liveX + dx, y: p.liveY + dy, w: p.liveW, h: p.liveH })
        p.liveX = r.x
        p.liveY = r.y
        p.finish()
    }

    // Una griglia leggera: e' una lavagna, non una finestra vuota.
    Canvas {
        anchors.fill: parent
        opacity: 0.25
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.strokeStyle = Theme.borderSoft
            ctx.lineWidth = 1
            for (let x = 0; x < width; x += 48) {
                ctx.beginPath(); ctx.moveTo(x + 0.5, 0); ctx.lineTo(x + 0.5, height); ctx.stroke()
            }
            for (let y = 0; y < height; y += 48) {
                ctx.beginPath(); ctx.moveTo(0, y + 0.5); ctx.lineTo(width, y + 0.5); ctx.stroke()
            }
        }
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }

    // Una scritta quando sulla lavagna non c'e' niente.
    Text {
        anchors.centerIn: parent
        visible: root.shownList.filter(k => !root.isFloating(k)).length === 0
        text: root.shownList.length === 0 ? root.emptyText : root.floatingText
        color: Theme.textSecondary
        font.pixelSize: 13
    }

    // I bordi e gli angoli: si tirano per ridimensionare, e anche loro si
    // attaccano ai bordi vicini.
    component Edge: MouseArea {
        // Il pannello di cui e' il bordo.
        required property Item panel
        property bool l: false
        property bool r: false
        property bool t: false
        property bool b: false
        property point press
        property rect start
        hoverEnabled: true
        preventStealing: true
        enabled: !root.locked
        z: 1000
        onPressed: (mouse) => {
            panel.begin()
            press = mapToItem(root, mouse.x, mouse.y)
            start = Qt.rect(panel.liveX, panel.liveY, panel.liveW, panel.liveH)
        }
        onPositionChanged: (mouse) => {
            if (!pressed)
                return
            const p = mapToItem(root, mouse.x, mouse.y)
            const dx = p.x - press.x
            const dy = p.y - press.y
            const e = root.edgesExcept(panel.key)
            let x1 = start.x, y1 = start.y, x2 = start.x + start.width, y2 = start.y + start.height
            if (l) x1 = Math.min(root.nearest(start.x + dx, e.xs), x2 - panel.minW)
            if (r) x2 = Math.max(root.nearest(x2 + dx, e.xs), x1 + panel.minW)
            if (t) y1 = Math.min(root.nearest(start.y + dy, e.ys), y2 - panel.minH)
            if (b) y2 = Math.max(root.nearest(y2 + dy, e.ys), y1 + panel.minH)
            panel.liveX = Math.max(0, x1)
            panel.liveY = Math.max(0, y1)
            panel.liveW = Math.min(root.width, x2) - panel.liveX
            panel.liveH = Math.min(root.height, y2) - panel.liveY
        }
        onReleased: panel.finish()
    }

    // ── Un pannello sulla lavagna ───────────────────────────────────────────
    component BoardPanel: Item {
        id: panel
        required property string key
        readonly property Item item: loader.item
        readonly property var g: root.geometry[key]
        // Mentre lo si sposta o lo si ridimensiona comanda il mouse; a mano
        // lasciata si salva in proporzione e torna a seguire la lavagna.
        property bool live: false
        property real liveX: 0
        property real liveY: 0
        property real liveW: 0
        property real liveH: 0
        // L'inserimento non si stringe mai sotto quanto serve a vedere lo
        // scambio e Registra; il rotore sotto la sua bussola.
        readonly property real minH: key === "contest" && item ? item.implicitHeight
                                   : key === "rotor" ? 220 : 110
        readonly property real minW: 220
        readonly property int grip: 6

        visible: root.isShown(key) && !root.isFloating(key)
        // Vero dalla prima volta che il pannello e' comparso davvero.
        property bool seen: false
        onVisibleChanged: if (visible) seen = true
        Component.onCompleted: if (visible) seen = true
        x: live ? liveX : g.x * root.width
        y: live ? liveY : g.y * root.height
        width: live ? liveW : Math.max(minW, g.w * root.width - root.gap)
        height: live ? liveH : Math.max(minH, g.h * root.height - root.gap)
        z: root.orderList.indexOf(key)

        function begin() {
            liveX = x; liveY = y; liveW = width; liveH = height
            live = true
            root.raiseKey(key)
        }
        function finish() {
            root.saveGeometry(key, { x: liveX, y: liveY, w: liveW + root.gap, h: liveH + root.gap })
            live = false
        }

        // Come una casella della disposizione di prima: la finestra principale
        // e le prove ci parlano cosi'.
        readonly property string panelKey: key
        property int currentTab: -1
        function setTab(n) { if (item && item.currentTab !== undefined) item.currentTab = n }
        function showMenu(name) { if (item && item.showMenu !== undefined) item.showMenu(name) }
        function showSelection(rows, what) { if (item && item.showSelection !== undefined) item.showSelection(rows, what) }
        function showModes() { if (item && item.showModes !== undefined) item.showModes() }
        function probeReports(m) { return item && item.probeReports !== undefined ? item.probeReports(m) : "" }
        function showCombo() { if (item && item.showCombo !== undefined) item.showCombo() }
        function showMacros(i) { if (item && item.showMacros !== undefined) item.showMacros(i) }
        function keyTargets() { return item && item.keyTargets !== undefined ? item.keyTargets() : [] }

        function sourceOf(k) {
            switch (k) {
            case "newqso":   return "NewQsoPanel.qml"
            case "rotor":    return "RotorPanel.qml"
            case "ft2":      return "Ft2AwardPanel.qml"
            case "tabs":     return "BottomTabs.qml"
            case "contest":  return "ContestEntryPanel.qml"
            case "cluster":  return "ClusterPanel.qml"
            case "logbook":  return "LogbookPanel.qml"
            case "callinfo": return "CallInfoPanel.qml"
            case "rate":     return "ContestRatePanel.qml"
            case "score":    return "ContestScorePanel.qml"
            case "map":      return "MapPanel.qml"
            case "cw":       return "CwPanel.qml"
            case "bandmap":  return "BandMapPanel.qml"
            case "decodium": return "DecodiumPanel.qml"
            case "decfull":  return "DecodiumWindowPanel.qml"
            case "decsig":   return "DecodiumWindowPanel.qml"
            case "net":      return "ContestNetPanel.qml"
            case "dvk":      return "DvkPanel.qml"
            case "mults":    return "ContestMultipliersPanel.qml"
            }
            return ""
        }

        // La testata: si prende e si sposta. Sta *sotto* il pannello: il clic
        // su un pulsante della testata (✕, ⤢, i menu) lo prende il pulsante, e
        // qui arriva solo quello che cade sul vuoto. Sopra, rubava anche i clic
        // dei pulsanti, e la ✕ non chiudeva.
        Item {
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: Theme.panelHeight
            // Un MouseArea e non un DragHandler: il DragHandler, anche sotto,
            // annullava il clic dei pulsanti della testata appena premuti.
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                enabled: !root.locked
                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                property point press
                property real sx0: 0
                property real sy0: 0
                onPressed: (mouse) => {
                    panel.begin()
                    press = mapToItem(root, mouse.x, mouse.y)
                    sx0 = panel.liveX
                    sy0 = panel.liveY
                }
                onPositionChanged: (mouse) => {
                    if (!pressed)
                        return
                    const p = mapToItem(root, mouse.x, mouse.y)
                    const r = root.snapMove(panel.key, { x: sx0 + p.x - press.x, y: sy0 + p.y - press.y,
                                                         w: panel.liveW, h: panel.liveH })
                    panel.liveX = r.x
                    panel.liveY = r.y
                }
                onReleased: panel.finish()
                onCanceled: panel.finish()
            }
        }

        Loader {
            id: loader
            anchors.fill: parent
            // Quello che non ci sta resta dentro il pannello, non sopra i vicini.
            clip: true
            // Il pannello resta caricato anche quando la lavagna intera e'
            // nascosta (si passa dalla gara alla finestra di tutti i giorni):
            // prima lo si distruggeva e rifaceva a ogni passaggio, e con un
            // log grande il cambio costava quasi un secondo di finestra ferma.
            // Si scarica solo se il pannello si chiude o va in una finestra sua.
            // Si carica la prima volta che si vede, non prima: la lavagna della
            // gara non pesa su chi il contest non lo apre.
            active: root.isShown(panel.key) && !root.isFloating(panel.key) && (panel.visible || panel.seen)
            source: active ? panel.sourceOf(panel.key) : ""
            onLoaded: {
                if (item.panelKey !== undefined)
                    item.panelKey = panel.key
                if (item.detached !== undefined)
                    item.detached = false
                if (item.detachable !== undefined)
                    item.detachable = true
                if (item.contestMode !== undefined)
                    item.contestMode = root.contestMode
                if (item.showPopButton !== undefined && root.contestMode)
                    item.showPopButton = false
                if (item.currentTab !== undefined)
                    panel.currentTab = item.currentTab
            }
        }
        Connections {
            target: loader.item
            ignoreUnknownSignals: true
            function onCloseRequested() { root.setShown(panel.key, false) }
            function onDetachRequested() { root.setFloating(panel.key, true) }
            function onOpenQso(id) { if (id > 0) root.openQso(id) }
            function onAwardRequested(id) { root.awardRequested(id) }
            function onDetailsRequested(id) { root.awardRequested(id) }
            function onStatsRequested() { root.statsRequested() }
            function onClusterRequested(tab) { root.clusterRequested(tab) }
            function onWindowRequested() { root.rotorRequested() }
            function onContestRequested() { root.contestRequested() }
            function onPopRequested() { root.setFloating(panel.key, true) }
            function onExpandRequested() { root.expandRequested() }
            function onMenuRequested(sx, sy) { root.menuRequested(panel.key, sx, sy) }
            function onCurrentTabChanged() { panel.currentTab = loader.item.currentTab }
            // La maniglia ⠿ sposta anche lei.
            function onMoveStarted() {
                handleDrag.grabbing = false
                if (!root.locked)
                    panel.begin()
            }
            function onMoveMoved(sx, sy) {
                if (root.locked || !panel.live)
                    return
                const p = root.mapFromGlobal(sx, sy)
                if (!handleDrag.grabbing) {
                    handleDrag.grabAt = Qt.point(p.x - panel.liveX, p.y - panel.liveY)
                    handleDrag.grabbing = true
                    return
                }
                const r = root.snapMove(panel.key, { x: p.x - handleDrag.grabAt.x, y: p.y - handleDrag.grabAt.y,
                                                     w: panel.liveW, h: panel.liveH })
                panel.liveX = r.x
                panel.liveY = r.y
            }
            function onMoveEnded(sx, sy) {
                handleDrag.grabbing = false
                if (panel.live)
                    panel.finish()
            }
        }
        QtObject {
            id: handleDrag
            property point grabAt
            property bool grabbing: false
        }

        Edge { panel: panel; l: true; cursorShape: Qt.SizeHorCursor; x: -3; y: panel.grip; width: panel.grip; height: panel.height - 2 * panel.grip }
        Edge { panel: panel; r: true; cursorShape: Qt.SizeHorCursor; x: panel.width - 3; y: panel.grip; width: panel.grip; height: panel.height - 2 * panel.grip }
        Edge { panel: panel; t: true; cursorShape: Qt.SizeVerCursor; x: panel.grip; y: -3; width: panel.width - 2 * panel.grip; height: panel.grip }
        Edge { panel: panel; b: true; cursorShape: Qt.SizeVerCursor; x: panel.grip; y: panel.height - 3; width: panel.width - 2 * panel.grip; height: panel.grip }
        Edge { panel: panel; l: true; t: true; cursorShape: Qt.SizeFDiagCursor; x: -3; y: -3; width: panel.grip * 2; height: panel.grip * 2 }
        Edge { panel: panel; r: true; b: true; cursorShape: Qt.SizeFDiagCursor; x: panel.width - panel.grip - 3; y: panel.height - panel.grip - 3; width: panel.grip * 2; height: panel.grip * 2 }
        Edge { panel: panel; r: true; t: true; cursorShape: Qt.SizeBDiagCursor; x: panel.width - panel.grip - 3; y: -3; width: panel.grip * 2; height: panel.grip * 2 }
        Edge { panel: panel; l: true; b: true; cursorShape: Qt.SizeBDiagCursor; x: -3; y: panel.height - panel.grip - 3; width: panel.grip * 2; height: panel.grip * 2 }
    }

    Repeater {
        id: boardPanels
        model: root.allKeys
        delegate: BoardPanel {
            required property string modelData
            key: modelData
        }
    }

    // ── I pannelli staccati: finestre loro, sopra la principale ──────────────
    function floatingWindowFor(key) {
        for (let i = 0; i < floatingWindows.count; ++i) {
            const w = floatingWindows.objectAt(i)
            if (w && w.panelKey === key)
                return w
        }
        return null
    }
    Instantiator {
        id: floatingWindows
        // Fuori dalla gara non ci sono: la lavagna e' spenta. Quella di tutti
        // i giorni ha le finestre della finestra principale.
        model: root.visible && !root.external ? root.floatingList : []
        delegate: PanelWindow {
            required property string modelData
            panelKey: modelData
            panelTitle: root.titleOf(modelData)
            panelSource: {
                switch (modelData) {
                case "contest":  return "ContestEntryPanel.qml"
                case "cluster":  return "ClusterPanel.qml"
                case "logbook":  return "LogbookPanel.qml"
                case "callinfo": return "CallInfoPanel.qml"
                case "rate":     return "ContestRatePanel.qml"
                case "score":    return "ContestScorePanel.qml"
                case "map":      return "MapPanel.qml"
                case "cw":       return "CwPanel.qml"
                case "bandmap":  return "BandMapPanel.qml"
                case "decodium": return "DecodiumPanel.qml"
                case "decfull":  return "DecodiumWindowPanel.qml"
                case "decsig":   return "DecodiumWindowPanel.qml"
                case "net":      return "ContestNetPanel.qml"
                case "dvk":      return "DvkPanel.qml"
                case "mults":    return "ContestMultipliersPanel.qml"
                }
                return ""
            }
            dockable: true
            contestMode: true
            pinned: false
            // Finestre di primo livello reali: possono stare su un altro
            // monitor o desktop e non vengono trascinate/minimizzate assieme
            // alla dashboard.
            transientParent: null
            // Anche la chiusura nativa deve compiere la stessa transizione del
            // pulsante ↩: altrimenti la finestra sparisce ma floating resta
            // salvato, e il pannello non torna piu' selezionabile dal menu.
            onClosing: {
                if (!root.quitting)
                    root.setFloating(modelData, false)
            }
            onAttachRequested: root.setFloating(modelData, false)
            onCloseRequested: root.setShown(modelData, false)
            onOpenQsoRequested: (id) => root.openQso(id)
            onAwardRequested: (id) => root.awardRequested(id)
            onClusterRequested: (tab) => root.clusterRequested(tab)
            onStatsRequested: root.statsRequested()
            onRotorRequested: root.rotorRequested()
            onContestRequested: root.contestRequested()
        }
    }
}
