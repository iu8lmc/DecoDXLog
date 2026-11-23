// DecoDXLog — il logbook: filtri a pillole, colonne scelte dall'operatore, una riga
// per QSO con gli stati QSL (L Q C E = LoTW, QRZ, Club Log, eQSL).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore
import Decodium.UI

GlassPanel {
    id: root

    QsoJourneyLauncher { id: journey }

    function showJourney(id) {
        journey.openJourney(decolog.qsoDetail(id))
    }

    // Colonne, larghezze e filtri salvati li tiene il log stesso, nelle
    // impostazioni: prima li teneva chi lo ospitava, e dove nessuno lo faceva
    // (la lavagna del contest, un log staccato, il log spostato in un'altra
    // casella) scegliere una colonna o salvare un filtro non faceva niente.
    Settings {
        id: logStore
        category: "layout"
        // Colonne nascoste, come chiavi separate da virgola ("dxcc,source"):
        // e' com'era prima, e serve solo a partire dalle stesse colonne.
        property string hiddenColumns: ""
        // Le colonne mostrate, nell'ordine in cui si vedono.
        property string columnLayout: ""
        // Add the new map column to existing layouts only once; a later
        // deliberate choice to hide it must survive application restarts.
        property bool journeyColumnMigrated: false
        // Le larghezze scelte a mano, come {"call": 120, "name": 260}.
        property string columnWidths: ""
        property var savedFilters: ({})
        // Le righe colorate secondo la conferma QSL (verde, giallo, arancio).
        property bool qslTint: false
        // L'ordine delle righe: la colonna e il verso, come lo si e' lasciato.
        property string sortKey: "utc"
        property bool sortAscending: false
    }
    Connections {
        target: root.model
        function onSortChanged() {
            logStore.sortKey = root.model.sortKey
            logStore.sortAscending = root.model.sortAscending
        }
    }
    readonly property string hiddenColumns: logStore.hiddenColumns
    readonly property string columnWidths: logStore.columnWidths
    readonly property var savedFilters: logStore.savedFilters
    onHiddenColumnsEdited: (value) => logStore.hiddenColumns = value
    onColumnWidthsEdited: (value) => logStore.columnWidths = value
    onSavedFiltersEdited: (value) => logStore.savedFilters = value
    property bool showPopButton: true
    signal hiddenColumnsEdited(string value)
    signal columnWidthsEdited(string value)
    signal savedFiltersEdited(var value)
    signal openQso(var id)
    signal popRequested()

    property int selectedRow: -1
    // Selezione multipla: il clic sinistro aggiunge o toglie una riga, lo shift
    // prende tutto quello che sta in mezzo, Esc lascia andare tutto.
    property var selectedIds: []
    readonly property var model: decolog.qsoModel

    // ── Quali colonne, e in che ordine ──────────────────────────────────────
    // Tutte quelle del log e i campi ADIF (anche quelli che altri programmi
    // scrivono, come Logger32): si scelgono, si tolgono e si spostano, e la
    // disposizione e' la stessa in gara e fuori.
    function storedLayout() {
        if (logStore.columnLayout.length > 0)
            return logStore.columnLayout.split(",")
        // Chi arriva da prima: le colonne di sempre, meno quelle che aveva nascosto.
        const hidden = logStore.hiddenColumns.length ? logStore.hiddenColumns.split(",") : []
        return root.defaultLayout.filter(k => hidden.indexOf(k) < 0)
    }
    readonly property var defaultLayout: ["utc", "call", "band", "freq", "mode", "rst_sent", "rst_rcvd",
                                          "grid", "name", "comment", "qth", "country", "state", "county",
                                          "cqz", "ituz", "iota", "dxcc", "qsl", "source", "tags", "journey"]
    function setLayout(list) {
        logStore.columnLayout = list.join(",")
        root.model.columnLayout = list
    }
    Component.onCompleted: {
        let layout = root.storedLayout()
        if (!logStore.journeyColumnMigrated) {
            if (layout.indexOf("journey") < 0)
                layout.push("journey")
            logStore.columnLayout = layout.join(",")
            logStore.journeyColumnMigrated = true
        }
        root.model.columnLayout = layout
        root.model.setSort(logStore.sortKey, logStore.sortAscending)
    }
    function isHidden(key) { return root.model.columnLayout.indexOf(key) < 0 }
    function toggleColumn(key) {
        const list = root.model.columnLayout.slice()
        const i = list.indexOf(key)
        if (i >= 0) {
            if (key === "call")
                return
            list.splice(i, 1)
        } else {
            list.push(key)
        }
        root.setLayout(list)
    }
    function moveColumn(from, to) {
        const list = root.model.columnLayout.slice()
        if (from < 0 || from >= list.length || to < 0 || to >= list.length || from === to)
            return
        const key = list.splice(from, 1)[0]
        list.splice(to, 0, key)
        root.setLayout(list)
    }

    // La cella che si sta correggendo sul posto: riga e colonna. F2 sul nominativo della riga
    // corrente, o il menu della riga; Invio salva, Esc lascia com'era.
    property int editRow: -1
    property string editKey: ""
    // L'ultima colonna cliccata: F2 corregge quella, se si puo', altrimenti il nominativo.
    property string cellKey: "call"
    function startInlineEdit(row, key) {
        if (row < 0 || row >= root.model.count || !decolog.canEditInline(key))
            return
        root.editRow = row
        root.editKey = key
    }
    function stopInlineEdit() {
        root.editRow = -1
        root.editKey = ""
    }

    function isSelected(id) { return root.selectedIds.indexOf(id) >= 0 }
    function clearSelection() { root.selectedIds = [] }
    function selectOnly(row) {
        const id = root.model.idAt(row)
        root.selectedIds = id > 0 ? [id] : []
    }
    function toggleRow(row) {
        const id = root.model.idAt(row)
        if (id <= 0)
            return
        const list = root.selectedIds.slice()
        const i = list.indexOf(id)
        if (i >= 0) list.splice(i, 1)
        else list.push(id)
        root.selectedIds = list
    }
    function selectRange(row) {
        // Dall'ultima riga toccata fino a questa, estremi compresi.
        const anchor = root.selectedRow < 0 ? row : root.selectedRow
        const list = root.selectedIds.slice()
        for (let r = Math.min(anchor, row); r <= Math.max(anchor, row); ++r) {
            const id = root.model.idAt(r)
            if (id > 0 && list.indexOf(id) < 0)
                list.push(id)
        }
        root.selectedIds = list
    }

    // ── La selezione col mouse, trascinando ─────────────────────────────────
    // Si preme su una riga e si scende (o si sale) tenendo premuto: tutte le righe fino a quella
    // sotto il mouse sono scelte. Con Ctrl si aggiungono a quelle che gia' c'erano. Per le righe
    // fuori dallo schermo resta Maiusc+clic.
    property int dragAnchor: -1
    property bool dragMoved: false
    property var dragBase: []
    function rowAtY(y) {
        const r = Math.floor((y + table.contentY) / Theme.rowHeight)
        return Math.max(0, Math.min(root.model.count - 1, r))
    }
    function dragBegin(row, ctrl) {
        root.dragAnchor = row
        root.dragMoved = false
        root.dragBase = ctrl ? root.selectedIds.slice() : []
    }
    // `y`: dove sta il mouse, in coordinate della tabella.
    function dragUpdate(y) {
        if (root.dragAnchor < 0)
            return
        const row = root.rowAtY(y)
        // Un clic che si sposta di un pelo dentro la stessa riga resta un clic.
        if (!root.dragMoved && row === root.dragAnchor)
            return
        root.dragMoved = true
        const list = root.dragBase.slice()
        for (let r = Math.min(root.dragAnchor, row); r <= Math.max(root.dragAnchor, row); ++r) {
            const id = root.model.idAt(r)
            if (id > 0 && list.indexOf(id) < 0)
                list.push(id)
        }
        root.selectedIds = list
        root.selectedRow = row
    }
    function dragEnd() {
        if (root.dragAnchor >= 0 && root.dragMoved)
            decolog.lookupCall = root.model.callAt(root.selectedRow)
        root.dragAnchor = -1
    }

    // ── Larghezza delle colonne ─────────────────────────────────────────
    //
    // Il bordo fra due intestazioni si trascina: Qt se lo ricorda in
    // explicitColumnWidth, e noi lo scriviamo nelle impostazioni con la chiave
    // della colonna — cosi' resta anche se un domani le colonne cambiano posto.
    function widthMap() {
        try {
            return JSON.parse(root.columnWidths || "{}") || {}
        } catch (e) {
            return {}
        }
    }
    function applyStoredWidths() {
        const map = root.widthMap()
        for (let c = 0; c < root.model.columns; ++c) {
            const w = map[root.model.columnKey(c)]
            if (w > 0)
                table.setColumnWidth(c, w)
        }
        table.forceLayout()
    }
    function storeWidths() {
        const map = {}
        for (let c = 0; c < root.model.columns; ++c) {
            const w = table.explicitColumnWidth(c)
            if (w >= 0)
                map[root.model.columnKey(c)] = Math.round(w)
        }
        const text = JSON.stringify(map)
        if (text !== root.columnWidths)
            root.columnWidthsEdited(text)
    }
    function resetWidths() {
        table.clearColumnWidths()
        root.columnWidthsEdited("")
        table.forceLayout()
    }

    // La colonna che si chiama cosi', qualunque posto abbia: gli indici
    // cambiano quando se ne aggiungono, i nomi no.
    function columnOf(key) {
        for (let c = 0; c < root.model.columns; ++c) {
            if (root.model.columnKey(c) === key)
                return c
        }
        return -1
    }

    function modeColor(mode) {
        if (mode === "FT2") return Theme.accentColor
        if (mode === "FT8" || mode === "FT4") return Theme.primaryColor
        return Theme.textPrimary
    }
    function qslColor(code) {
        return code === "c" ? Theme.accentColor : code === "s" ? Theme.warningColor : Theme.textSecondary
    }
    // Le quattro lettere della colonna QSL, dette a parole: passando il mouse
    // sopra la L si legge com'e' andata con LoTW, e cosi' per le altre.
    function qslService(index) {
        return ["LoTW", "QRZ Logbook", "Club Log", "eQSL"][index] || ""
    }
    function qslTip(index, code) {
        const name = root.qslService(index)
        return code === "c" ? qsTr("%1: confirmed — received").arg(name)
             : code === "s" ? qsTr("%1: sent, waiting for the confirmation").arg(name)
             : qsTr("%1: not sent").arg(name)
    }
    function sourceColor(src) {
        return src === "udp" || src === "rec" ? Theme.secondaryColor : src === "cld" ? Theme.warningColor : Theme.textSecondary
    }
    // Per le schermate di prova (--show menu:<nome>).
    function showMenu(name) {
        if (name === "columns") columnsDialog.open()
        else if (name === "filters") { addFilterMenu.popup(60, Theme.panelHeight + 30); bandMenu.open() }
        else if (name === "saved") savedMenu.popup(root.width - 200, Theme.panelHeight + 30)
        else if (name === "row") rowMenu.popupFor(root.model.idAt(0))
        // La correzione sul posto, aperta per guardarla: il nominativo della prima riga.
        else if (name === "editcall") { root.selectedRow = 0; root.startInlineEdit(0, "call") }
        else if (name === "actions") actionsMenu.popup(root.width - 320, Theme.panelHeight)
        else if (name === "tag") tagPopup.openFor(root.model.shownIds(), true)
        else if (name === "bulk") bulkDialog.openFor(root.model.shownIds())
        else if (name === "duplicates") { duplicatesDialog.open(); decolog.findDuplicates(5) }
        else if (name === "dates") datePopup.open()
        else if (name === "wide") { table.setColumnWidth(1, 260); table.forceLayout(); root.storeWidths() }
        else if (name === "sub") { addFilterMenu.popup(60, Theme.panelHeight + 30); subTimer.start() }
        else if (name === "fieldmenu") { addFilterMenu.popup(60, Theme.panelHeight + 30); fieldMenu.open() }
        // "field-cqz": la scelta del valore; "field-cqz-14": il filtro gia' messo.
        else if (name.startsWith("field-")) {
            const parts = name.split("-")
            if (parts.length > 2) root.model.setFieldFilter(parts[1], parts.slice(2).join("-"))
            else fieldPicker.openFor(parts[1])
        }
        else if (name === "tint") logStore.qslTint = true
        // "alllogs-cont-EU": prima il filtro, poi la ricerca in tutti i log.
        else if (name.startsWith("alllogs")) {
            const parts = name.split("-")
            if (parts.length > 2) root.model.setFieldFilter(parts[1], parts.slice(2).join("-"))
            logsSearch.openSearch()
        }
    }
    // Per le prove col mouse vero: il menu aperto da showMenu.
    function menuFor(name) { return name === "band" ? bandMenu : columnsDialog }
    // La colonna sotto quel punto dell'intestazione (in coordinate del suo
    // contenuto): serve a spostarle trascinando.
    property int headerDropTarget: -1
    function columnAtHeaderX(x) {
        let edge = 0
        for (let c = 0; c < root.model.columns; ++c) {
            // Qt may not have laid out a column yet (notably with an empty
            // log); use the same width provider as the table in that case.
            let width = table.columnWidth(c)
            if (!(width > 0))
                width = table.columnWidthProvider(c)
            edge += width
            if (x < edge)
                return c
        }
        return root.model.columns - 1
    }
    function headerCell(col) { return header.itemAtCell(Qt.point(col, 0)) }
    function tableCell(col) { return table.itemAtCell(Qt.point(col, 0)) }
    function journeyProbeCell(row) { return table.itemAtCell(Qt.point(root.columnOf("journey"), row)) }
    function journeyProbeId() { return journey.detail ? journey.detail.id : 0 }
    function bulkProbeIds(allRows) {
        const row = Math.min(10000, root.model.count - 1)
        if (row >= 0)
            table.positionViewAtRow(row, TableView.AlignVCenter)
        return allRows ? root.model.shownIds() : [root.model.idAt(row)]
    }
    function prepareBulkProbe(allRows, field, value, onlyEmpty) {
        const ids = bulkProbeIds(allRows)
        bulkDialog.prepareProbe(ids, field, value, onlyEmpty)
        return ids.length
    }
    function prepareSelectedBulkProbe(field, value, onlyEmpty) {
        root.selectedIds = root.model.shownIds()
        rowMenu.qsoId = root.selectedIds.length ? root.selectedIds[0] : 0
        changeFieldMenu.triggered()
        bulkDialog.configureProbe(field, value, onlyEmpty)
        return root.selectedIds.length
    }
    function bulkProbeButton() { return bulkDialog.probeApplyButton() }
    function bulkProbeResult() { return bulkDialog.probeResult() }
    function columnWidthOf(col) { return table.columnWidth(col) }
    // Per le schermate di prova (--show select:<righe separate da virgola>:<cosa>).
    function showSelection(rows, what) {
        const list = []
        const wanted = String(rows).split(",")
        for (let i = 0; i < wanted.length; ++i) {
            const id = root.model.idAt(parseInt(wanted[i]))
            if (id > 0)
                list.push(id)
        }
        root.selectedIds = list
        root.selectedRow = parseInt(wanted[0])
        if (what === "menu") rowMenu.popupFor(list[0])
        else if (what === "confirm") confirmRowDelete.openFor(list)
        else if (what === "confirm2") { confirmRowDelete.openFor(list); confirmRowDelete.step = 2 }
        else if (what === "delete") { if (decolog.deleteQsosAsync(list)) root.clearSelection() }
        else if (what === "callbook") { for (let k = 0; k < list.length; ++k) decolog.completeQsoFromCallbook(list[k]) }
        else if (String(what).indexOf("upload") === 0)
            decolog.qsl.uploadQsos(list, String(what).split("-")[1] || "lotw")
    }
    function qslFilterLabel(key) {
        return { confirmed: qsTr("confirmed"), lotw: qsTr("LoTW confirmed"), card: qsTr("card confirmed"),
                 eqsl: qsTr("eQSL confirmed"), unconfirmed: qsTr("not confirmed") }[key] || key
    }
    function thisMonth() {
        const now = decolog.utcNow()
        return now.date.substring(0, 7)
    }

    title: qsTr("Logbook")
    dotColor: Theme.primaryColor
    padding: 0

    headerLeading: [
        Pill {
            visible: root.selectedIds.length > 0
            text: qsTr("%1 selected").arg(root.selectedIds.length)
            tone: Theme.primaryColor
            pillHeight: 20
            fontPixelSize: 10
        },
        Pill {
            visible: decolog.clientConnected
            text: "LIVE"
            tone: Theme.accentColor
            pillHeight: 20
            fontPixelSize: 10
        }
    ]
    headerTools: [
        // Un'importazione grande gira in secondo piano: qui si vede a che punto e'.
        Row {
            anchors.verticalCenter: parent.verticalCenter
            visible: decolog.importProgress >= 0
            spacing: 6
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: decolog.importUndoRunning
                      ? (decolog.importUndoPreviewing ? qsTr("Checking import %1%") : qsTr("Undoing import %1%"))
                          .arg(Math.round(decolog.importProgress * 100))
                      : qsTr("Importing %1%").arg(Math.round(decolog.importProgress * 100))
                color: Theme.accentColor
                font.family: Theme.monoFamily
                font.pixelSize: 11
            }
            ProgressBar {
                anchors.verticalCenter: parent.verticalCenter
                width: 80
                from: 0
                to: 1
                value: Math.max(0, decolog.importProgress)
            }
            GlassButton {
                visible: decolog.importUndoRunning
                text: qsTr("Cancel")
                onClicked: decolog.cancelImportUndo()
            }
        },
        // Un ordine o un filtro su un log grande si preparano da parte.
        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: root.model.busy
            text: qsTr("Sorting…")
            color: Theme.accentColor
            font.family: Theme.monoFamily
            font.pixelSize: 11
        },
        Row {
            anchors.verticalCenter: parent.verticalCenter
            visible: decolog.bulkProgress >= 0
            spacing: 6
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Changing %1%").arg(Math.round(decolog.bulkProgress * 100))
                color: Theme.accentColor
                font.family: Theme.monoFamily
                font.pixelSize: 11
            }
            ProgressBar {
                anchors.verticalCenter: parent.verticalCenter
                width: 80
                from: 0
                to: 1
                value: Math.max(0, decolog.bulkProgress)
            }
        },
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.model.filtered
                  ? qsTr("%1 QSO · %2 shown").arg(root.model.totalCount.toLocaleString(Qt.locale("en_US"), "f", 0))
                                             .arg(root.model.count.toLocaleString(Qt.locale("en_US"), "f", 0))
                  : qsTr("%1 QSO").arg(root.model.totalCount.toLocaleString(Qt.locale("en_US"), "f", 0))
            color: Theme.textSecondary
            font.family: Theme.monoFamily
            font.pixelSize: 11
        },
        GlassButton {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Actions ▾")
            buttonHeight: 24
            fontPixelSize: 11
            onClicked: actionsMenu.popup()
        },
        GlassButton {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Columns")
            buttonHeight: 24
            fontPixelSize: 11
            onClicked: columnsDialog.open()
        },
        GlassButton {
            anchors.verticalCenter: parent.verticalCenter
            visible: root.showPopButton
            text: qsTr("Pop")
            tone: Theme.primaryColor
            buttonHeight: 24
            fontPixelSize: 11
            onClicked: root.popRequested()
        }
    ]

    ColumnsDialog {
        id: columnsDialog
        panel: root
        shown: root.model.columnLayout
        all: { root.model.columnLayout; return root.model.availableColumns() }
    }
    BulkEditDialog { id: bulkDialog }
    ExportPeriodDialog { id: exportPeriodDialog }
    DuplicatesDialog {
        id: duplicatesDialog
        onOpenQso: (id) => root.openQso(id)
    }

    // Per le prove: apre il sottomenu quando il menu padre e' gia' in piedi.
    Timer {
        id: subTimer
        interval: 400
        onTriggered: bandMenu.open()
    }

    StyledMenu {
        id: addFilterMenu
        StyledMenu {
            id: bandMenu
            title: qsTr("Band")
            Repeater {
                model: bandMenu.opened || addFilterMenu.opened ? root.model.bandsInLog() : []
                StyledMenuItem {
                    required property string modelData
                    text: modelData
                    checkable: true
                    checked: root.model.bandFilter.indexOf(modelData) >= 0
                    onTriggered: {
                        const list = root.model.bandFilter.slice()
                        const i = list.indexOf(modelData)
                        if (i >= 0) list.splice(i, 1); else list.push(modelData)
                        root.model.bandFilter = list
                    }
                }
            }
        }
        StyledMenu {
            id: modeMenu
            title: qsTr("Mode")
            Repeater {
                model: modeMenu.opened || addFilterMenu.opened ? root.model.modesInLog() : []
                StyledMenuItem {
                    required property string modelData
                    text: modelData
                    checkable: true
                    checked: root.model.modeFilter.indexOf(modelData) >= 0
                    onTriggered: {
                        const list = root.model.modeFilter.slice()
                        const i = list.indexOf(modelData)
                        if (i >= 0) list.splice(i, 1); else list.push(modelData)
                        root.model.modeFilter = list
                    }
                }
            }
        }
        StyledMenu {
            id: dxccMenu
            title: qsTr("DXCC entity")
            Repeater {
                model: dxccMenu.opened ? decolog.dxccInLog() : []
                StyledMenuItem {
                    required property var modelData
                    text: "%1  %2 (%3)".arg(modelData.dxcc).arg(modelData.name || "?").arg(modelData.count)
                    checkable: true
                    checked: root.model.dxccFilter === modelData.dxcc
                    onTriggered: root.model.dxccFilter = checked ? modelData.dxcc : 0
                }
            }
        }
        StyledMenu {
            title: qsTr("QSL")
            Repeater {
                model: ["confirmed", "lotw", "card", "eqsl", "unconfirmed"]
                StyledMenuItem {
                    required property string modelData
                    text: root.qslFilterLabel(modelData)
                    checkable: true
                    checked: root.model.qslFilter === modelData
                    onTriggered: root.model.qslFilter = checked ? modelData : ""
                }
            }
        }
        StyledMenu {
            id: profileMenu
            title: qsTr("Station profile")
            Repeater {
                model: decolog.stationProfiles
                StyledMenuItem {
                    required property var profileId
                    required property string name
                    required property bool deleted
                    visible: !deleted
                    height: visible ? implicitHeight : 0
                    text: name
                    checkable: true
                    checked: root.model.profileFilter === profileId
                    onTriggered: root.model.profileFilter = checked ? profileId : 0
                }
            }
        }
        StyledMenu {
            id: tagMenu
            title: qsTr("Tag")
            Repeater {
                model: tagMenu.opened ? root.model.tagsInLog() : []
                StyledMenuItem {
                    required property var modelData
                    text: "%1 (%2)".arg(modelData.key).arg(modelData.count)
                    checkable: true
                    checked: root.model.tagFilter.toLowerCase() === modelData.key.toLowerCase()
                    onTriggered: root.model.tagFilter = checked ? modelData.key : ""
                }
            }
            StyledMenuItem {
                visible: tagMenu.opened && root.model.tagsInLog().length === 0
                height: visible ? implicitHeight : 0
                enabled: false
                text: qsTr("No tags in the log yet")
            }
        }
        // Qualunque altro campo, per valore: zona CQ e ITU, continente,
        // prefisso WPX, QTH, riferimenti, manager, mese della cartolina…
        StyledMenu {
            id: fieldMenu
            title: qsTr("Other field")
            Repeater {
                model: root.model.fieldFilterChoices()
                StyledMenuItem {
                    required property var modelData
                    text: modelData.label + "…"
                    checkable: true
                    checked: root.model.fieldFilters[modelData.key] !== undefined
                    onTriggered: fieldPicker.openFor(modelData.key)
                }
            }
        }
        StyledMenuItem {
            text: qsTr("This month")
            onTriggered: root.model.monthFilter = root.thisMonth()
        }
        StyledMenuItem {
            text: qsTr("Date range…")
            onTriggered: datePopup.open()
        }
    }

    // ── Azioni sulle righe mostrate ─────────────────────────────────────────
    StyledMenu {
        id: actionsMenu
        onAboutToShow: decolog.refreshDamagedFieldCount()
        StyledMenuItem {
            text: qsTr("Tag the %1 QSO shown…").arg(root.model.count)
            enabled: root.model.count > 0
            onTriggered: tagPopup.openFor(root.model.shownIds(), true)
        }
        StyledMenuItem {
            text: qsTr("Remove a tag from the QSO shown…")
            enabled: root.model.count > 0
            onTriggered: tagPopup.openFor(root.model.shownIds(), false)
        }
        StyledMenuItem {
            text: qsTr("Export the %1 QSO shown to ADIF…").arg(root.model.count)
            enabled: root.model.count > 0
            onTriggered: exportShown.open()
        }
        StyledMenuItem {
            text: qsTr("Export a period (from… to…) to ADIF…")
            onTriggered: exportPeriodDialog.openDialog()
        }
        StyledMenuItem {
            text: qsTr("Change a field on the %1 QSO shown…").arg(root.model.count)
            enabled: root.model.count > 0 && decolog.bulkProgress < 0
            onTriggered: bulkDialog.openFor(root.model.shownIds())
        }
        StyledMenuItem {
            text: qsTr("Find duplicate QSO…")
            onTriggered: duplicatesDialog.open()
        }
        StyledMenuItem {
            text: qsTr("Complete the QSO shown from the callbook…")
            enabled: root.model.count > 0 && decolog.callbookProvider !== "off"
            onTriggered: decolog.completeShownFromCallbook()
        }
        // Tutto il log, non solo quello che si vede: una ricerca per volta,
        // e si puo' fermare quando si vuole.
        StyledMenuItem {
            text: qsTr("Complete every QSO without a grid…")
            enabled: decolog.callbookProvider !== "off" && decolog.callbookQueued === 0
            onTriggered: decolog.completeMissingFromCallbook()
        }
        StyledMenuItem {
            visible: decolog.callbookQueued > 0
            height: visible ? implicitHeight : 0
            text: qsTr("Stop: %1 QSO still to go").arg(decolog.callbookQueued)
            onTriggered: decolog.stopCallbookQueue()
        }
        StyledMenuItem {
            readonly property int damaged: decolog.damagedFieldCountCached
            visible: damaged !== 0
            height: visible ? implicitHeight : 0
            enabled: damaged > 0 && decolog.bulkProgress < 0
            text: damaged === -1 ? qsTr("Checking for damaged QSOs…")
                : damaged < 0 ? qsTr("Could not check damaged QSOs")
                : qsTr("Clean up %1 QSO damaged by an old import…").arg(damaged)
            onTriggered: confirmRepair.open()
        }
        MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.borderSoft } }
        StyledMenuItem {
            text: qsTr("Search these filters in every log…")
            enabled: root.model.filtered
            onTriggered: logsSearch.openSearch()
        }
        StyledMenuItem {
            text: qsTr("Colour the rows by QSL confirmation")
            checkable: true
            checked: logStore.qslTint
            onTriggered: logStore.qslTint = checked
        }
        StyledMenuItem {
            text: qsTr("Clear all filters")
            enabled: root.model.filtered
            onTriggered: root.model.clearFilters()
        }
    }

    // I colori della conferma: gli stessi nella riga e nella legenda.
    // 4 LoTW (con o senza cartolina), 3 solo cartolina, 2 solo eQSL/QRZ,
    // 1 cartolina partita e non tornata.
    function qslTintColor(state) {
        return state === 4 ? Qt.rgba(0.20, 0.78, 0.25, 0.26)
             : state === 3 ? Qt.rgba(0.31, 0.62, 0.97, 0.26)
             : state === 2 ? Qt.rgba(0.95, 0.80, 0.10, 0.24)
             : state === 1 ? Qt.rgba(1.00, 0.55, 0.00, 0.22)
             : "transparent"
    }

    // ── Gli stessi filtri in tutti i log ────────────────────────────────────
    // Quello che fa la finestra "Ricerche" di altri log: i QSO che passano i
    // filtri di adesso, in ogni log dell'elenco (quello di tutti i giorni, i
    // contest…), letti in sola lettura su un altro filo.
    Popup {
        id: logsSearch
        property var rows: []
        property var perLog: []
        property bool searching: false
        function openSearch() {
            rows = []
            perLog = []
            searching = true
            open()
            const logs = []
            for (const l of decolog.logs.logs) {
                if (l.exists !== false)
                    logs.push({ name: l.name, path: l.path })
            }
            root.model.searchLogs(logs)
        }
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(980, (parent ? parent.width : 1000) - 60)
        height: Math.min(640, (parent ? parent.height : 700) - 60)
        modal: true
        focus: true
        padding: 14
        background: Rectangle { color: Theme.panelColor; border.color: Theme.glassBorder; radius: 6 }

        Connections {
            target: root.model
            function onLogsSearched(rows, perLog) {
                logsSearch.rows = rows
                logsSearch.perLog = perLog
                logsSearch.searching = false
            }
        }

        // Le colonne della lista: titolo, chiave, larghezza.
        readonly property var cols: [
            { key: "log", title: qsTr("Log"), w: 170 },
            { key: "utc", title: qsTr("UTC"), w: 130 },
            { key: "call", title: qsTr("Call"), w: 120 },
            { key: "band", title: qsTr("Band"), w: 60 },
            { key: "mode", title: qsTr("Mode"), w: 70 },
            { key: "country", title: qsTr("Country"), w: 220 }
        ]

        contentItem: ColumnLayout {
            spacing: 8
            Text {
                text: qsTr("The filters of the log, in every log")
                color: Theme.textPrimary
                font.pixelSize: 13
                font.bold: true
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.textSecondary
                font.pixelSize: 11
                text: qsTr("The same filters you have on the log now, applied to every log in the list (Log → logs). "
                           + "The station profile is left out: each log numbers its profiles its own way.")
            }
            Flow {
                Layout.fillWidth: true
                spacing: 8
                Repeater {
                    model: logsSearch.perLog
                    Pill {
                        required property var modelData
                        text: modelData.error.length > 0 ? qsTr("%1: cannot be read").arg(modelData.name)
                              : modelData.truncated ? qsTr("%1: first %2").arg(modelData.name).arg(modelData.count)
                              : "%1: %2".arg(modelData.name).arg(modelData.count)
                        tone: modelData.error.length > 0 ? Theme.warningColor
                              : modelData.count > 0 ? Theme.accentColor : Theme.textSecondary
                        rounded: false
                        ToolTip.visible: modelData.error.length > 0 && hoverArea.containsMouse
                        ToolTip.text: qsTr("%1\nOpen it once in DecoDXLog to bring it up to date.").arg(modelData.error)
                        MouseArea { id: hoverArea; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
                    }
                }
            }
            Text {
                visible: logsSearch.searching || (logsSearch.rows.length === 0 && logsSearch.perLog.length > 0)
                color: Theme.textSecondary
                font.pixelSize: 12
                text: logsSearch.searching ? qsTr("Searching the logs…") : qsTr("No QSO passes these filters in any log.")
            }
            // L'intestazione della lista.
            Row {
                visible: logsSearch.rows.length > 0
                spacing: 0
                Repeater {
                    model: logsSearch.cols
                    Text {
                        required property var modelData
                        width: modelData.w
                        leftPadding: 8
                        text: modelData.title
                        color: Theme.secondaryColor
                        font.family: Theme.monoFamily
                        font.pixelSize: 11
                        font.bold: true
                    }
                }
            }
            ListView {
                id: logsList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: logsSearch.rows
                ScrollBar.vertical: PanelScrollBar {}
                delegate: Rectangle {
                    id: foundRow
                    required property var modelData
                    width: logsList.width
                    height: Theme.rowHeight
                    color: root.qslTintColor(modelData.qsl)
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        Repeater {
                            model: logsSearch.cols
                            Text {
                                required property var modelData
                                width: modelData.w
                                leftPadding: 8
                                elide: Text.ElideRight
                                text: foundRow.modelData[modelData.key] || ""
                                color: Theme.textPrimary
                                font.family: modelData.key === "log" || modelData.key === "country" ? Theme.uiFamily
                                                                                                   : Theme.monoFamily
                                font.pixelSize: Theme.fontSize
                                font.bold: modelData.key === "call"
                            }
                        }
                    }
                    Rectangle {
                        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                        height: 1
                        color: Theme.borderSoft
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    visible: logsSearch.rows.length > 0
                    text: qsTr("%1 QSO found").arg(logsSearch.rows.length)
                    color: Theme.textSecondary
                    font.pixelSize: 11
                }
                GlassButton { text: qsTr("Close"); onClicked: logsSearch.close() }
            }
        }
    }

    // ── Un campo e il suo valore ────────────────────────────────────────────
    // I valori che il campo ha nel log, ciascuno coi suoi QSO, contati su un
    // altro filo; si restringono scrivendo.
    Popup {
        id: fieldPicker
        property string key: ""
        property var values: []
        property bool loading: false
        readonly property var shown: {
            const f = fieldSearch.text.trim().toUpperCase()
            return f.length === 0 ? values
                 : values.filter(v => String(v.label).toUpperCase().indexOf(f) >= 0)
        }
        function openFor(k) {
            key = k
            values = []
            loading = true
            open()
            root.model.requestFieldValues(k)
        }
        anchors.centerIn: parent
        width: Math.min(420, root.width - 40)
        height: Math.min(520, root.height - 40)
        modal: true
        focus: true
        padding: 14
        background: Rectangle { color: Theme.panelColor; border.color: Theme.glassBorder; radius: 6 }
        onOpened: { fieldSearch.text = ""; fieldSearch.forceActiveFocus() }

        Connections {
            target: root.model
            function onFieldValuesReady(k, list) {
                if (k !== fieldPicker.key)
                    return
                fieldPicker.values = list
                fieldPicker.loading = false
            }
        }

        contentItem: ColumnLayout {
            spacing: 8
            Text {
                text: root.model.fieldFilterLabel(fieldPicker.key)
                color: Theme.textPrimary
                font.pixelSize: 13
                font.bold: true
            }
            StyledTextField {
                id: fieldSearch
                Layout.fillWidth: true
                mono: false
                placeholderText: qsTr("Narrow the list…")
                Keys.onReturnPressed: if (fieldPicker.shown.length === 1) fieldPicker.pick(fieldPicker.shown[0].value)
                Keys.onEnterPressed: if (fieldPicker.shown.length === 1) fieldPicker.pick(fieldPicker.shown[0].value)
            }
            Text {
                Layout.fillWidth: true
                visible: fieldPicker.loading || fieldPicker.values.length === 0
                wrapMode: Text.Wrap
                color: Theme.textSecondary
                font.pixelSize: 12
                text: fieldPicker.loading ? qsTr("Counting the log…")
                                          : qsTr("No QSO in the log has this field filled in.")
            }
            ListView {
                id: fieldList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: fieldPicker.shown
                ScrollBar.vertical: PanelScrollBar {}
                delegate: Rectangle {
                    required property var modelData
                    width: fieldList.width
                    height: 26
                    radius: 3
                    readonly property bool chosen: root.model.fieldFilters[fieldPicker.key] === modelData.value
                    color: pickArea.containsMouse || chosen
                           ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, chosen ? 0.24 : 0.12)
                           : "transparent"
                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.right: countText.left
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.label
                        elide: Text.ElideRight
                        color: Theme.textPrimary
                        font.family: Theme.monoFamily
                        font.pixelSize: 12
                    }
                    Text {
                        id: countText
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: Number(modelData.count).toLocaleString(Qt.locale(), "f", 0)
                        color: Theme.textSecondary
                        font.family: Theme.monoFamily
                        font.pixelSize: 11
                    }
                    MouseArea {
                        id: pickArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: fieldPicker.pick(modelData.value)
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    visible: !fieldPicker.loading && fieldPicker.values.length > 0
                    text: qsTr("%1 values").arg(fieldPicker.shown.length)
                    color: Theme.textSecondary
                    font.pixelSize: 11
                }
                GlassButton {
                    visible: root.model.fieldFilters[fieldPicker.key] !== undefined
                    text: qsTr("Remove this filter")
                    onClicked: { root.model.setFieldFilter(fieldPicker.key, ""); fieldPicker.close() }
                }
                GlassButton { text: qsTr("Close"); onClicked: fieldPicker.close() }
            }
        }
        function pick(value) {
            root.model.setFieldFilter(key, value)
            close()
        }
    }

    // Cancellare e' morbido, ma si chiede lo stesso, e due volte: un clic
    // sbagliato capita, e qui le righe possono essere tante.
    Popup {
        id: confirmRowDelete
        property var ids: []
        property string call: ""
        property int step: 1
        function openFor(list) {
            ids = list
            call = list.length === 1 ? root.model.valueFor(root.model.rowForId(list[0]), "call") : ""
            step = 1
            open()
        }
        anchors.centerIn: Overlay.overlay
        modal: true
        padding: 16
        onClosed: step = 1
        background: Rectangle { color: Theme.panelColor; border.color: Theme.borderColor; radius: 6 }
        contentItem: ColumnLayout {
            spacing: 12
            Text {
                text: confirmRowDelete.step === 1
                      ? (confirmRowDelete.ids.length === 1
                         ? qsTr("Delete %1? The QSO stays in the history and can be recovered.").arg(confirmRowDelete.call)
                         : qsTr("Delete the %1 QSO selected? They stay in the history and can be recovered.").arg(confirmRowDelete.ids.length))
                      : (confirmRowDelete.ids.length === 1
                         ? qsTr("Once more, to be sure: delete %1?").arg(confirmRowDelete.call)
                         : qsTr("Once more, to be sure: delete %1 QSO?").arg(confirmRowDelete.ids.length))
                color: confirmRowDelete.step === 1 ? Theme.textPrimary : Theme.errorColor
                wrapMode: Text.Wrap
                Layout.maximumWidth: 360
            }
            RowLayout {
                spacing: 8
                Item { Layout.fillWidth: true }
                GlassButton { text: qsTr("Cancel"); onClicked: confirmRowDelete.close() }
                GlassButton {
                    id: confirmDeleteButton
                    text: confirmRowDelete.step === 1 ? qsTr("Delete") : qsTr("Delete for good")
                    tone: Theme.errorColor
                    filled: true
                    onClicked: {
                        // Il primo clic chiede di nuovo, il secondo cancella.
                        if (confirmRowDelete.step === 1) {
                            confirmRowDelete.step = 2
                            return
                        }
                        const list = confirmRowDelete.ids
                        confirmRowDelete.close()
                        if (decolog.deleteQsosAsync(list))
                            root.clearSelection()
                    }
                }
            }
        }
    }

    // Ripulire tocca dei QSO gia' scritti: si dice prima cosa succede.
    Popup {
        id: confirmRepair
        anchors.centerIn: Overlay.overlay
        modal: true
        padding: 16
        background: Rectangle { color: Theme.panelColor; border.color: Theme.borderColor; radius: 6 }
        contentItem: ColumnLayout {
            spacing: 12
            Text {
                text: qsTr("Some QSO imported long ago have a name or a QTH cut in half, with a piece of the "
                           + "next ADIF field stuck to it. Clean them up? What cannot be read is emptied, so "
                           + "the callbook can write it properly; the old text stays in the history.")
                color: Theme.textPrimary
                wrapMode: Text.Wrap
                Layout.maximumWidth: 420
            }
            RowLayout {
                spacing: 8
                Item { Layout.fillWidth: true }
                GlassButton { text: qsTr("Cancel"); onClicked: confirmRepair.close() }
                GlassButton {
                    text: qsTr("Clean up")
                    tone: Theme.accentColor
                    filled: true
                    onClicked: {
                        if (decolog.repairImportedFieldsAsync())
                            confirmRepair.close()
                    }
                }
            }
        }
    }

    // Il salvataggio dei marcati: il nome di partenza dice quanti sono e di
    // che giorno, cosi' non si salvano tre "export.adi" uno sopra l'altro.
    FileDialog {
        id: exportChosen
        property var ids: []
        function openFor(list) {
            exportChosen.ids = list
            const today = new Date().toISOString().slice(0, 10)
            const one = list.length === 1 ? root.model.valueFor(root.model.rowForId(list[0]), "call") : ""
            currentFile = "file:///" + (one ? one.replace("/", "-") : "decolog-" + list.length + "-qso")
                          + "-" + today + ".adi"
            open()
        }
        title: qsTr("Save the QSO chosen")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "adi"
        nameFilters: [qsTr("ADIF files (*.adi)")]
        onAccepted: decolog.exportQsos(exportChosen.ids, selectedFile)
    }

    FileDialog {
        id: exportShown
        title: qsTr("Export the QSO shown")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "adi"
        nameFilters: [qsTr("ADIF files (*.adi)")]
        onAccepted: decolog.exportQsos(root.model.shownIds(), selectedFile)
    }

    // Etichetta da aggiungere o togliere a un gruppo di QSO.
    Popup {
        id: tagPopup
        property var ids: []
        property bool adding: true
        function openFor(list, add) { ids = list; adding = add; open() }
        anchors.centerIn: parent
        modal: true
        padding: 14
        background: Rectangle { color: Theme.panelColor; border.color: Theme.glassBorder; radius: 6 }
        onOpened: { tagField.text = ""; tagField.forceActiveFocus() }
        ColumnLayout {
            spacing: 8
            Text {
                text: tagPopup.adding ? qsTr("Add a tag to %1 QSO").arg(tagPopup.ids.length)
                                      : qsTr("Remove a tag from %1 QSO").arg(tagPopup.ids.length)
                color: Theme.textPrimary
                font.pixelSize: 13
                font.bold: true
            }
            StyledTextField {
                id: tagField
                Layout.preferredWidth: 300
                mono: false
                placeholderText: qsTr("e.g. pota, field day, portable")
                Keys.onReturnPressed: tagApply.clicked()
            }
            Flow {
                Layout.preferredWidth: 300
                spacing: 4
                Repeater {
                    model: tagPopup.opened ? root.model.tagsInLog().slice(0, 16) : []
                    Pill {
                        required property var modelData
                        text: modelData.key
                        tone: Theme.secondaryColor
                        interactive: true
                        onClicked: tagField.text = modelData.key
                    }
                }
            }
            Text {
                id: tagResult
                Layout.preferredWidth: 300
                wrapMode: Text.Wrap
                visible: text.length > 0
                color: Theme.textSecondary
                font.pixelSize: 11
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8
                GlassButton { text: qsTr("Close"); onClicked: tagPopup.close() }
                GlassButton {
                    id: tagApply
                    text: tagPopup.adding ? qsTr("Add tag") : qsTr("Remove tag")
                    tone: tagPopup.adding ? Theme.accentColor : Theme.warningColor
                    filled: true
                    enabled: tagField.text.trim().length > 0 && tagField.text.indexOf(",") < 0
                    onClicked: {
                        if (decolog.tagQsosAsync(tagPopup.ids, tagField.text.trim(), tagPopup.adding)) {
                            tagResult.text = ""
                            tagPopup.close()
                        }
                    }
                }
            }
        }
    }

    // Intervallo di date, estremi compresi.
    Popup {
        id: datePopup
        anchors.centerIn: parent
        modal: true
        padding: 14
        background: Rectangle { color: Theme.panelColor; border.color: Theme.glassBorder; radius: 6 }
        onOpened: { fromField.text = decolog.showDate(root.model.dateFrom); toField.text = decolog.showDate(root.model.dateTo); fromField.forceActiveFocus() }
        ColumnLayout {
            spacing: 8
            RowLayout {
                spacing: 10
                LabeledField {
                    label: qsTr("From (UTC)")
                    StyledTextField { id: fromField; Layout.preferredWidth: 130; placeholderText: decolog.showDate("2026-01-01") }
                }
                LabeledField {
                    label: qsTr("To (included)")
                    StyledTextField { id: toField; Layout.preferredWidth: 130; placeholderText: decolog.showDate("2026-12-31"); Keys.onReturnPressed: dateApply.clicked() }
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8
                GlassButton { text: qsTr("Cancel"); onClicked: datePopup.close() }
                GlassButton {
                    id: dateApply
                    text: qsTr("Apply")
                    tone: Theme.accentColor
                    filled: true
                    onClicked: {
                        root.model.monthFilter = ""
                        // Scritte come vuole la lingua, tenute ISO.
                        root.model.dateFrom = decolog.readDate(fromField.text)
                        root.model.dateTo = decolog.readDate(toField.text)
                        datePopup.close()
                    }
                }
            }
        }
    }

    StyledMenu {
        id: savedMenu
        Repeater {
            model: Object.keys(root.savedFilters)
            StyledMenuItem {
                required property string modelData
                text: modelData
                onTriggered: root.model.applyFilterState(root.savedFilters[modelData])
            }
        }
        MenuSeparator {
            visible: Object.keys(root.savedFilters).length > 0
            contentItem: Rectangle { implicitHeight: 1; color: Theme.borderSoft }
        }
        StyledMenuItem {
            text: qsTr("Save current filters…")
            enabled: root.model.filtered
            onTriggered: saveFilterPopup.open()
        }
        StyledMenu {
            title: qsTr("Delete")
            enabled: Object.keys(root.savedFilters).length > 0
            Repeater {
                model: Object.keys(root.savedFilters)
                StyledMenuItem {
                    required property string modelData
                    text: modelData
                    onTriggered: {
                        const copy = Object.assign({}, root.savedFilters)
                        delete copy[modelData]
                        root.savedFiltersEdited(copy)
                    }
                }
            }
        }
    }

    Popup {
        id: saveFilterPopup
        anchors.centerIn: parent
        modal: true
        padding: 12
        background: Rectangle { color: Theme.panelColor; border.color: Theme.glassBorder; radius: 6 }
        onOpened: { filterName.text = ""; filterName.forceActiveFocus() }
        ColumnLayout {
            spacing: 8
            LabeledField {
                label: qsTr("Filter name")
                StyledTextField {
                    id: filterName
                    Layout.preferredWidth: 240
                    mono: false
                    Keys.onReturnPressed: saveButton.clicked()
                }
            }
            GlassButton {
                id: saveButton
                Layout.alignment: Qt.AlignRight
                text: qsTr("Save")
                tone: Theme.accentColor
                filled: true
                enabled: filterName.text.trim().length > 0
                onClicked: {
                    const copy = Object.assign({}, root.savedFilters)
                    copy[filterName.text.trim()] = root.model.filterState()
                    root.savedFiltersEdited(copy)
                    saveFilterPopup.close()
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── Filtri ──────────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: filterRow.implicitHeight + 12
            color: "transparent"
            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: 1
                color: Theme.borderSoft
            }

            RowLayout {
                id: filterRow
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 6

                Text {
                    text: qsTr("Filters")
                    color: Theme.textSecondary
                    font.family: Theme.monoFamily
                    font.pixelSize: 11
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Pill {
                        visible: root.model.filterText.length > 0
                        text: qsTr("Search: %1 ✕").arg(root.model.filterText)
                        tone: Theme.secondaryColor
                        rounded: false
                        interactive: true
                        onClicked: root.model.filterText = ""
                    }
                    Pill {
                        visible: root.model.bandFilter.length > 0
                        text: qsTr("Band: %1 ✕").arg(root.model.bandFilter.join(" "))
                        tone: Theme.secondaryColor
                        rounded: false
                        interactive: true
                        onClicked: root.model.bandFilter = []
                    }
                    Pill {
                        visible: root.model.modeFilter.length > 0
                        text: qsTr("Mode: %1 ✕").arg(root.model.modeFilter.join(" "))
                        tone: Theme.secondaryColor
                        rounded: false
                        interactive: true
                        onClicked: root.model.modeFilter = []
                    }
                    Pill {
                        visible: root.model.monthFilter.length > 0
                        text: {
                            const d = new Date(root.model.monthFilter + "-01T00:00:00Z")
                            return d.toLocaleDateString(Qt.locale("en_US"), "MMM yyyy") + " ✕"
                        }
                        tone: Theme.secondaryColor
                        rounded: false
                        interactive: true
                        onClicked: root.model.monthFilter = ""
                    }
                    Pill {
                        visible: root.model.dxccFilter > 0
                        text: qsTr("DXCC: %1 ✕").arg(root.model.dxccFilter + " " + decolog.dxccName(root.model.dxccFilter))
                        tone: Theme.secondaryColor
                        rounded: false
                        interactive: true
                        onClicked: root.model.dxccFilter = 0
                    }
                    Pill {
                        visible: root.model.qslFilter.length > 0
                        text: qsTr("QSL: %1 ✕").arg(root.qslFilterLabel(root.model.qslFilter))
                        tone: Theme.secondaryColor
                        rounded: false
                        interactive: true
                        onClicked: root.model.qslFilter = ""
                    }
                    Pill {
                        visible: root.model.profileFilter > 0
                        text: qsTr("Station: %1 ✕").arg(decolog.stationProfiles.byId(root.model.profileFilter).name || root.model.profileFilter)
                        tone: Theme.secondaryColor
                        rounded: false
                        interactive: true
                        onClicked: root.model.profileFilter = 0
                    }
                    Pill {
                        visible: root.model.tagFilter.length > 0
                        text: qsTr("Tag: %1 ✕").arg(root.model.tagFilter)
                        tone: Theme.accentColor
                        rounded: false
                        interactive: true
                        onClicked: root.model.tagFilter = ""
                    }
                    Repeater {
                        model: Object.keys(root.model.fieldFilters)
                        Pill {
                            required property string modelData
                            text: "%1: %2 ✕".arg(root.model.fieldFilterLabel(modelData))
                                            .arg(root.model.fieldValueLabel(modelData, root.model.fieldFilters[modelData]))
                            tone: Theme.secondaryColor
                            rounded: false
                            interactive: true
                            onClicked: root.model.setFieldFilter(modelData, "")
                        }
                    }
                    Pill {
                        visible: root.model.dateFrom.length > 0 || root.model.dateTo.length > 0
                        text: "%1 → %2 ✕".arg(decolog.showDate(root.model.dateFrom) || "…").arg(decolog.showDate(root.model.dateTo) || "…")
                        tone: Theme.secondaryColor
                        rounded: false
                        interactive: true
                        onClicked: { root.model.dateFrom = ""; root.model.dateTo = "" }
                    }
                    // La legenda dei colori della conferma, quando sono accesi.
                    Row {
                        visible: logStore.qslTint
                        spacing: 10
                        height: 22
                        Repeater {
                            model: [{ state: 4, text: qsTr("LoTW") },
                                    { state: 3, text: qsTr("card only") },
                                    { state: 2, text: qsTr("eQSL / QRZ only") },
                                    { state: 1, text: qsTr("card sent, not back") }]
                            Row {
                                required property var modelData
                                spacing: 4
                                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                                Rectangle {
                                    width: 12
                                    height: 12
                                    radius: 2
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: root.qslTintColor(modelData.state)
                                    border.width: 1
                                    border.color: Theme.glassBorder
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.text
                                    color: Theme.textSecondary
                                    font.pixelSize: 11
                                }
                            }
                        }
                    }
                    Rectangle {
                        implicitHeight: 22
                        implicitWidth: addText.implicitWidth + 16
                        radius: 4
                        color: "transparent"
                        border.width: 1
                        border.color: Theme.glassBorder
                        Text {
                            id: addText
                            anchors.centerIn: parent
                            text: qsTr("+ add")
                            color: Theme.textSecondary
                            font.family: Theme.monoFamily
                            font.pixelSize: 11
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: addFilterMenu.popup()
                        }
                    }
                }
                Text {
                    text: qsTr("Saved:")
                    color: Theme.textSecondary
                    font.family: Theme.monoFamily
                    font.pixelSize: 11
                }
                Pill {
                    text: Object.keys(root.savedFilters).length ? qsTr("%1 filters ▾").arg(Object.keys(root.savedFilters).length)
                                                               : qsTr("none ▾")
                    tone: Theme.glassBorder
                    rounded: false
                    interactive: true
                    textColor: Theme.textPrimary
                    onClicked: savedMenu.popup()
                }
            }
        }

        // ── Tabella ─────────────────────────────────────────────────────────
        HorizontalHeaderView {
            id: header
            Layout.fillWidth: true
            syncView: table
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            // Le colonne si allargano e si stringono tirando la riga verticale
            // fra due intestazioni: prima si poteva solo dentro la tabella, dove
            // il bordo non si vede.
            resizableColumns: true
            delegate: Rectangle {
                required property var display
                required property int index
                readonly property bool isQsl: root.model.columnKey(index) === "qsl"
                implicitHeight: Theme.rowHeight
                color: "transparent"

                // Cosa vogliono dire le quattro lettere, per chi non le sa a memoria.
                HoverHandler { id: headHover; enabled: parent.isQsl }
                ToolTip.visible: headHover.hovered
                ToolTip.delay: 400
                ToolTip.text: qsTr("L LoTW · Q QRZ Logbook · C Club Log · E eQSL")

                // Presa e portata sopra un'altra intestazione, la colonna si
                // sposta li'. Il bordo a destra resta libero: quello allarga.
                MouseArea {
                    id: headDrag
                    anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                    width: Math.max(0, parent.width - 8)
                    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.PointingHandCursor
                    property real pressX: 0
                    onPressed: (mouse) => pressX = mouse.x
                    onPositionChanged: (mouse) => {
                        if (pressed)
                            root.headerDropTarget = Math.abs(mouse.x - pressX) > 8
                                ? root.columnAtHeaderX(mapToItem(header.contentItem, mouse.x, 0).x) : -1
                    }
                    onReleased: (mouse) => {
                        const target = root.columnAtHeaderX(mapToItem(header.contentItem, mouse.x, 0).x)
                        root.headerDropTarget = -1
                        if (Math.abs(mouse.x - pressX) > 8)
                            root.moveColumn(index, target)
                        else
                            // Un clic, non un trascinamento: si ordina per questa
                            // colonna, e di nuovo al contrario.
                            root.model.sortBy(root.model.columnKey(index))
                    }
                    onCanceled: root.headerDropTarget = -1
                }
                // Dove cade la colonna trascinata.
                Rectangle {
                    anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                    width: 3
                    color: Theme.accentColor
                    visible: root.headerDropTarget === index
                }

                // Il bordo da prendere: una riga verticale fra un'intestazione e
                // la successiva.
                Rectangle {
                    anchors { right: parent.right; top: parent.top; bottom: parent.bottom; topMargin: 4; bottomMargin: 4 }
                    width: 1
                    color: Theme.glassBorder
                }

                // La freccia dice per quale colonna e in che verso si ordina:
                // sta a destra, fuori dal titolo, cosi' si vede anche se la
                // colonna e' stretta.
                readonly property bool sorted: root.model.sortKey === root.model.columnKey(index)
                Text {
                    anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
                    visible: parent.sorted
                    text: root.model.sortAscending ? "▲" : "▼"
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSize - 2
                }
                Text {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: parent.sorted ? 22 : 8
                    verticalAlignment: Text.AlignVCenter
                    text: parent.display
                    color: parent.sorted ? Theme.textPrimary : Theme.secondaryColor
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontSize
                    font.bold: true
                    elide: Text.ElideRight
                }
            }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.glassBorder }

        TableView {
            id: table
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.model
            boundsBehavior: Flickable.StopAtBounds
            resizableColumns: true
            columnWidthProvider: function (column) {
                // Se il bordo l'ha tirato l'operatore, comanda lui.
                const chosen = table.explicitColumnWidth(column)
                if (chosen >= 0)
                    return chosen
                // Il nome prende lo spazio che avanza.
                // Il nome prende lo spazio che avanza, se avanza.
                if (column === root.columnOf("name")) {
                    let used = 0
                    for (let c = 0; c < root.model.columns; ++c)
                        if (c !== column)
                            used += root.model.columnWidthHint(c)
                    return Math.max(root.model.columnWidthHint(column), table.width - used)
                }
                return root.model.columnWidthHint(column)
            }
            rowHeightProvider: function () { return Theme.rowHeight }
            ScrollBar.vertical: ScrollBar {}
            ScrollBar.horizontal: ScrollBar {}
            onWidthChanged: forceLayout()

            // Dopo una tirata, la misura si scrive nelle impostazioni — ma non a
            // ogni pixel: si aspetta che l'operatore abbia finito.
            onLayoutChanged: storeWidthsTimer.restart()
            Timer {
                id: storeWidthsTimer
                interval: 600
                onTriggered: root.storeWidths()
            }
            Component.onCompleted: root.applyStoredWidths()
            Connections {
                target: root
                function onColumnWidthsChanged() { root.applyStoredWidths() }
            }
            Connections {
                target: root.model
                function onRowsInserted() {
                    Qt.callLater(function() {
                        table.forceLayout()
                        if (root.model)
                            root.model.refreshAfterInsert()
                    })
                }
                function onLayoutChanged() {
                    table.clearColumnWidths()
                    root.applyStoredWidths()
                }
            }

            Connections {
                target: Theme
                function onDensityChanged() { table.forceLayout() }
            }
            Connections {
                target: root
                function onHiddenColumnsChanged() { table.forceLayout() }
            }

            delegate: Rectangle {
                id: cell
                required property var display
                required property int column
                required property string columnKey
                required property string modeName
                required property bool isNew
                required property int row
                required property var qsoId
                required property string rowCategory
                required property int qslState
                // I colori della categoria, come in Decodium 4: vuoti se spenti.
                // Con le righe colorate per conferma vince la conferma.
                readonly property var paint: decolog.logColors[rowCategory] || ({})
                readonly property string paintFg: paint.fg || ""
                readonly property string paintBg: paint.bg || ""

                readonly property bool selected: root.isSelected(qsoId)
                readonly property bool current: row === root.selectedRow
                readonly property bool editing: root.editRow === row && root.editKey === columnKey

                implicitHeight: Theme.rowHeight
                clip: true
                color: selected ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.24)
                     : current ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.10)
                     : isNew ? Theme.rowMatchBg
                     : logStore.qslTint && qslState > 0 ? root.qslTintColor(qslState)
                     : paintBg.length > 0 ? paintBg
                     : "transparent"

                Rectangle {
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                    height: 1
                    color: Theme.borderSoft
                }

                Text {
                    visible: cell.columnKey !== "qsl" && cell.columnKey !== "journey"
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 4
                    verticalAlignment: Text.AlignVCenter
                    text: cell.display
                    elide: Text.ElideRight
                    font.pixelSize: cell.columnKey === "source" ? 10 : Theme.fontSize
                    font.family: cell.columnKey === "name" || cell.columnKey === "tags" ? Theme.uiFamily : Theme.monoFamily
                    font.bold: cell.columnKey === "call"
                    color: cell.paintFg.length > 0 ? cell.paintFg
                         : cell.columnKey === "utc" || cell.columnKey === "dxcc" ? Theme.textSecondary
                         : cell.columnKey === "mode" ? root.modeColor(cell.display)
                         : cell.columnKey === "source" ? root.sourceColor(cell.display)
                         : cell.columnKey === "tags" ? Theme.accentColor
                         : Theme.textPrimary
                }

                Row {
                    visible: cell.columnKey === "qsl"
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4
                    Repeater {
                        model: cell.columnKey === "qsl" ? ["L", "Q", "C", "E"] : []
                        Text {
                            required property string modelData
                            required property int index
                            text: modelData
                            color: root.qslColor(String(cell.display).charAt(index))
                            font.family: Theme.monoFamily
                            font.pixelSize: Theme.fontSize
                            font.bold: true

                            HoverHandler { id: qslHover; cursorShape: Qt.WhatsThisCursor }
                            ToolTip.visible: qslHover.hovered
                            ToolTip.delay: 400
                            ToolTip.text: root.qslTip(index, String(cell.display).charAt(index))
                        }
                    }
                }

                MouseArea {
                    id: cellMouse
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    // Senza, la lista (un Flickable) si prende il trascinamento e scorre invece di
                    // scegliere le righe.
                    preventStealing: true
                    onPressed: (mouse) => {
                        if (mouse.button === Qt.LeftButton)
                            root.dragBegin(cell.row, mouse.modifiers & Qt.ControlModifier)
                    }
                    onPositionChanged: (mouse) => {
                        if (pressed && root.dragAnchor >= 0)
                            root.dragUpdate(cellMouse.mapToItem(table, mouse.x, mouse.y).y)
                    }
                    onReleased: root.dragEnd()
                    onCanceled: root.dragEnd()
                    onClicked: (mouse) => {
                        root.forceActiveFocus()
                        // La selezione l'ha gia' fatta il trascinamento.
                        if (mouse.button === Qt.LeftButton && root.dragMoved) {
                            root.dragMoved = false
                            return
                        }
                        if (mouse.button === Qt.RightButton) {
                            // Il menu vale per quello che e' scelto: se si clicca
                            // fuori dalla selezione, la selezione diventa questa riga.
                            if (!root.isSelected(cell.qsoId))
                                root.selectOnly(cell.row)
                        } else if (mouse.modifiers & Qt.ShiftModifier) {
                            root.selectRange(cell.row)
                        } else {
                            root.toggleRow(cell.row)
                        }
                        root.selectedRow = cell.row
                        root.cellKey = cell.columnKey
                        decolog.lookupCall = root.model.callAt(cell.row)
                        if (mouse.button === Qt.RightButton)
                            rowMenu.popupFor(cell.qsoId, cell.row, cell.columnKey)
                    }
                    onDoubleClicked: root.openQso(cell.qsoId)
                }

                ToolButton {
                    visible: cell.columnKey === "journey" && !cell.editing
                    anchors.centerIn: parent
                    height: parent.height
                    width: height
                    text: "◎"
                    Accessible.name: qsTr("QSO map")
                    ToolTip.visible: hovered
                    ToolTip.text: Accessible.name
                    onClicked: root.showJourney(cell.qsoId)
                }

                // La correzione sul posto: il testo della cella diventa un campo.
                StyledTextField {
                    id: editor
                    visible: cell.editing
                    anchors.fill: parent
                    anchors.margins: 2
                    uppercase: cell.columnKey === "call" || cell.columnKey === "grid"
                    mono: cell.columnKey !== "name" && cell.columnKey !== "qth" && cell.columnKey !== "comment"
                    property string problem: ""
                    // Parte col testo della cella, tutto scelto: scrivere sostituisce.
                    function begin() {
                        problem = ""
                        text = String(cell.display)
                        selectAll()
                        forceActiveFocus()
                    }
                    onVisibleChanged: if (visible) begin()
                    // Una cella nata gia' in modifica (la riga e' stata ricreata) non vede il cambio.
                    Component.onCompleted: if (visible) begin()
                    function commit() {
                        const error = decolog.editQsoField(cell.qsoId, cell.columnKey, text)
                        if (error.length > 0)
                            problem = error
                        else
                            root.stopInlineEdit()
                    }
                    onTextEdited: problem = ""
                    Keys.onReturnPressed: commit()
                    Keys.onEnterPressed: commit()
                    Keys.onEscapePressed: root.stopInlineEdit()
                    // Le frecce restano al campo: non spostano la riga mentre si scrive.
                    Keys.onUpPressed: {}
                    Keys.onDownPressed: {}
                    // Chi clicca altrove lascia com'era: niente salvataggi per sbaglio.
                    onActiveFocusChanged: if (!activeFocus && cell.editing) root.stopInlineEdit()
                    ToolTip.visible: problem.length > 0
                    ToolTip.text: problem
                }
            }

            Text {
                // Sulla tabella, non sul suo contenuto: senza righe il
                // contenuto e' alto zero e la frase finiva sotto le intestazioni.
                // TableView riparenta i figli nel proprio contentItem: ancorarlo
                // al TableView esterno produceva il warning QML perche' non era
                // piu' ne' genitore ne' fratello. Posizioniamolo nel viewport.
                parent: table.contentItem
                x: (table.width - width) / 2
                y: table.contentY + (table.height - height) / 2
                width: table.width - 40
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                visible: root.model.count === 0
                text: root.model.filtered ? qsTr("No QSO matches the filters.")
                                          : qsTr("The log is empty. Log a QSO in Decodium, or import an ADIF file.")
                color: Theme.textSecondary
            }
        }
    }

    StyledMenu {
        id: rowMenu
        property var qsoId: 0
        property int row: -1
        property string columnKey: "call"
        function popupFor(id, rowIndex, key) {
            qsoId = id
            row = rowIndex === undefined ? root.model.rowForId(id) : rowIndex
            columnKey = key || "call"
            popup()
        }
        StyledMenuItem { text: qsTr("Open / edit…"); onTriggered: root.openQso(rowMenu.qsoId) }
        // Un nominativo sbagliato si corregge sulla riga, senza aprire la scheda.
        StyledMenuItem {
            text: qsTr("Edit the call here (F2)")
            onTriggered: root.startInlineEdit(rowMenu.row, "call")
        }
        StyledMenuItem {
            visible: rowMenu.columnKey !== "call" && decolog.canEditInline(rowMenu.columnKey)
            height: visible ? implicitHeight : 0
            text: qsTr("Edit this cell here")
            onTriggered: root.startInlineEdit(rowMenu.row, rowMenu.columnKey)
        }
        // Cancellare un QSO si fa da dove lo si guarda, non solo dalla scheda:
        // e' morbida, la riga resta nello storico e si recupera.
        StyledMenuItem {
            readonly property int chosen: root.selectedIds.length
            text: chosen > 1 ? qsTr("Delete the %1 QSO selected…").arg(chosen) : qsTr("Delete QSO…")
            onTriggered: confirmRowDelete.openFor(chosen > 1 ? root.selectedIds : [rowMenu.qsoId])
        }
        // Lo stesso campo su tutti quelli scelti: il locatore di casa, un
        // riferimento POTA, le QSL spedite.
        StyledMenuItem {
            id: changeFieldMenu
            readonly property int chosen: root.selectedIds.length
            text: chosen > 1 ? qsTr("Change a field on the %1 QSO selected…").arg(chosen) : qsTr("Change a field…")
            enabled: decolog.bulkProgress < 0
            onTriggered: bulkDialog.openFor(chosen > 1 ? root.selectedIds : [rowMenu.qsoId])
        }
        // Il callbook sa nome, locatore e indirizzo: se al QSO mancano, glieli
        // mette adesso.
        StyledMenuItem {
            text: qsTr("Complete from the callbook")
            enabled: decolog.callbookProvider !== "off"
            onTriggered: decolog.completeQsoFromCallbook(rowMenu.qsoId)
        }
        // Un QSO gia' registrato puo' servire a una prova di HamAward o a
        // riallineare un altro programma. Si manda esattamente il formato
        // N1MM che usiamo quando il QSO nasce, senza salvare o modificare il
        // log; con una selezione multipla valgono tutte le righe selezionate.
        StyledMenuItem {
            readonly property var ids: root.selectedIds.length > 1 ? root.selectedIds : [rowMenu.qsoId]
            text: ids.length > 1 ? qsTr("Resend the %1 selected QSO via UDP").arg(ids.length)
                                 : qsTr("Resend this QSO via UDP")
            enabled: decolog.udpForward.length > 0 && decolog.udpForwardError.length === 0 && !decolog.udpResendBusy
            onTriggered: decolog.resendQsosViaUdpAsync(ids)
        }
        StyledMenuItem { text: qsTr("Filter by this call"); onTriggered: root.model.filterText = decolog.lookupCall }
        StyledMenuItem {
            readonly property int dxcc: parseInt(root.model.valueFor(root.model.rowForId(rowMenu.qsoId), "dxcc")) || 0
            enabled: dxcc > 0
            text: dxcc > 0 ? qsTr("Filter by entity: %1").arg(decolog.dxccName(dxcc) || dxcc) : qsTr("Filter by entity")
            onTriggered: root.model.dxccFilter = dxcc
        }
        MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.borderSoft } }
        // Le conferme elettroniche: LoTW, eQSL e gli altri, sui QSO scelti.
        StyledMenu {
            id: uploadMenu
            readonly property var ids: root.selectedIds.length > 1 ? root.selectedIds : [rowMenu.qsoId]
            title: root.selectedIds.length > 1
                   ? qsTr("Send the %1 QSO chosen to…").arg(root.selectedIds.length)
                   : qsTr("Send this QSO to…")
            enabled: !decolog.qsl.busy
            Repeater {
                model: decolog.qsl.services
                StyledMenuItem {
                    required property var modelData
                    text: modelData.ready ? modelData.label
                                          : "%1 — %2".arg(modelData.label).arg(modelData.hint)
                    enabled: modelData.ready && !decolog.qsl.busy
                    onTriggered: decolog.qsl.uploadQsos(uploadMenu.ids, modelData.id)
                }
            }
        }
        // Un file ADIF con dentro solo quelli marcati. Serve piu' spesso di
        // quanto sembri: si marcano dieci QSO e si mandano a chi li chiede,
        // invece di esportare tutto il log e poi togliere ventiduemila righe.
        StyledMenuItem {
            readonly property var ids: root.selectedIds.length > 1 ? root.selectedIds : [rowMenu.qsoId]
            text: root.selectedIds.length > 1
                  ? qsTr("Save the %1 QSO chosen as ADIF…").arg(root.selectedIds.length)
                  : qsTr("Save this QSO as ADIF…")
            onTriggered: exportChosen.openFor(ids)
        }
        MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.borderSoft } }
        StyledMenuItem {
            text: root.selectedIds.length > 1
                  ? qsTr("Paper QSL: queue the %1 chosen for the bureau").arg(root.selectedIds.length)
                  : qsTr("Paper QSL: queue for the bureau")
            enabled: !decolog.cards.operationBusy
            onTriggered: decolog.cards.enqueue(root.selectedIds.length > 1 ? root.selectedIds
                                                                           : [rowMenu.qsoId], "B")
        }
        StyledMenuItem {
            text: root.selectedIds.length > 1
                  ? qsTr("Paper QSL: queue the %1 chosen as direct").arg(root.selectedIds.length)
                  : qsTr("Paper QSL: queue as direct")
            enabled: !decolog.cards.operationBusy
            onTriggered: decolog.cards.enqueue(root.selectedIds.length > 1 ? root.selectedIds
                                                                           : [rowMenu.qsoId], "D")
        }
        MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.borderSoft } }
        StyledMenuItem { text: qsTr("Add tag…"); onTriggered: tagPopup.openFor([rowMenu.qsoId], true) }
        StyledMenu {
            id: removeTagMenu
            readonly property var tags: {
                const v = root.model.valueFor(root.model.rowForId(rowMenu.qsoId), "tags")
                return v.length ? v.split(", ") : []
            }
            title: qsTr("Remove tag")
            enabled: tags.length > 0
            Repeater {
                model: removeTagMenu.tags
                StyledMenuItem {
                    required property string modelData
                    text: modelData
                    onTriggered: decolog.tagQsosAsync([rowMenu.qsoId], modelData, false)
                }
            }
        }
    }

    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Down || event.key === Qt.Key_Up) {
            const next = Math.max(0, Math.min(root.model.count - 1, root.selectedRow + (event.key === Qt.Key_Down ? 1 : -1)))
            if (event.modifiers & Qt.ShiftModifier)
                root.selectRange(next)
            else
                root.selectOnly(next)
            root.selectedRow = next
            decolog.lookupCall = root.model.callAt(next)
            table.positionViewAtRow(next, TableView.Contain)
            event.accepted = true
        } else if (event.key === Qt.Key_A && (event.modifiers & Qt.ControlModifier)) {
            // Ctrl+A: tutte le righe che si vedono con i filtri di adesso.
            root.selectedIds = root.model.shownIds()
            event.accepted = true
        } else if (event.key === Qt.Key_Escape) {
            // Esc: la selezione se ne va, la riga corrente resta dov'e'.
            root.clearSelection()
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            root.openQso(root.model.idAt(root.selectedRow))
            event.accepted = true
        } else if (event.key === Qt.Key_F2) {
            // Corregge la cella cliccata per ultima, se si scrive a mano; altrimenti il nominativo.
            root.startInlineEdit(root.selectedRow, decolog.canEditInline(root.cellKey) ? root.cellKey : "call")
            event.accepted = true
        }
    }
}
