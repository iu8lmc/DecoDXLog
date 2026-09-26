// DecoDXLog — Setup (mockup 1e): navigazione a sinistra, pagina a destra.
//
// Le pagine di cose che non esistono ancora (cloud, servizi QSL, callbook) lo
// dicono chiaramente e salvano solo le preferenze che avranno senso quando
// arriveranno: nessuno stato finto.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Decodium.UI

DialogFrame {
    id: root

    property int page: 3

    // La pillola «c'e' una versione nuova» apre la finestra dell'aggiornamento,
    // che sta in Main: da qui si chiede e basta.
    signal updateRequested()

    title: qsTr("Setup")
    dotColor: Theme.secondaryColor
    // Larga quanto serve alla pagina piu' fitta: in italiano le etichette sono
    // piu' lunghe, e una pagina stretta finiva sopra la colonna di sinistra.
    dialogKey: "setup"
    width: 960
    height: 720

    readonly property var pages: [qsTr("General"), qsTr("Theme & density"), qsTr("Decodium link"),
                                  qsTr("Sync & Cloud"), qsTr("QSL services"), qsTr("Callbook"),
                                  qsTr("Radio (CAT)"), qsTr("Rotor"), qsTr("Backup")]

    // Per le prove: la meta' di sotto di una pagina lunga si vede solo
    // scorrendo, e una prova non ha un dito da passare sulla rotella.
    function scrollToBottom() {
        const flick = pageScroll.contentItem
        flick.contentY = Math.max(0, flick.contentHeight - flick.height)
    }

    onOpened: {
        portField.text = decolog.udpPort
        groupField.text = decolog.multicastGroup
        serverField.text = decolog.cloud.server
        backupDirField.text = decolog.backupDir
        backupTimeField.text = decolog.backupTime
        keepField.text = decolog.backupKeep
    }

    function apply() {
        const port = parseInt(portField.text)
        if (!isNaN(port))
            decolog.udpPort = port
        decolog.multicastGroup = groupField.text.trim()
        decolog.cloud.server = serverField.text.trim()
        decolog.backupDir = backupDirField.text.trim()
        decolog.backupTime = backupTimeField.text.trim()
        const keep = parseInt(keepField.text)
        if (!isNaN(keep))
            decolog.backupKeep = keep
    }

    // Cancellare tutto non si fa con un clic: si scrive DELETE, come su GitHub.
    Popup {
        id: purgeCloudDialog
        function openDialog() { purgeWord.text = ""; open(); purgeWord.forceActiveFocus() }
        anchors.centerIn: Overlay.overlay
        modal: true
        padding: 16
        closePolicy: Popup.CloseOnEscape
        background: Rectangle { color: Theme.panelColor; border.color: Theme.errorColor; radius: 6 }
        contentItem: ColumnLayout {
            spacing: 10
            Text {
                text: qsTr("Empty the Cloud of %1").arg(decolog.cloud.callsign || "—")
                color: Theme.errorColor
                font.pixelSize: 14
                font.bold: true
            }
            Text {
                Layout.maximumWidth: 420
                wrapMode: Text.Wrap
                color: Theme.textPrimary
                text: qsTr("Everything this callsign has on the server goes away: QSO, history, station "
                           + "profiles, settings, sealed credentials. It cannot be undone from here. The "
                           + "log on this computer stays where it is.")
            }
            LabeledField {
                label: qsTr("Write DELETE to confirm")
                StyledTextField {
                    id: purgeWord
                    Layout.preferredWidth: 220
                    uppercase: true
                    Keys.onReturnPressed: if (purgeGo.enabled) purgeGo.clicked()
                }
            }
            RowLayout {
                spacing: 8
                Item { Layout.fillWidth: true }
                GlassButton { text: qsTr("Cancel"); onClicked: purgeCloudDialog.close() }
                GlassButton {
                    id: purgeGo
                    text: qsTr("Empty the Cloud")
                    tone: Theme.errorColor
                    filled: true
                    enabled: purgeWord.text.trim() === "DELETE" && !decolog.cloud.busy
                    onClicked: {
                        purgeCloudDialog.close()
                        decolog.cloud.purgeCloud(purgeWord.text.trim())
                    }
                }
            }
        }
    }

    FileDialog {
        id: ctyDialog
        title: qsTr("cty.csv from country-files.com")
        nameFilters: [qsTr("cty.csv (*.csv)")]
        onAccepted: {
            const error = decolog.installCountries(selectedFile)
            ctyNote.text = error.length ? error : qsTr("cty.csv %1 in use.").arg(decolog.countriesVersion)
        }
    }

    FileDialog {
        id: tqslDialog
        title: qsTr("TQSL program")
        nameFilters: [qsTr("Programs (*.exe)"), qsTr("All files (*)")]
        onAccepted: decolog.qsl.tqslPath = decolog.localPath(selectedFile)
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Backup folder")
        onAccepted: backupDirField.text = decolog.localPath(selectedFolder)
    }

    component Tile: StatTile { Layout.fillWidth: true }
    component Note: Text {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        color: Theme.textSecondary
        font.pixelSize: 12
    }
    body: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ── Navigazione ─────────────────────────────────────────────────
            ColumnLayout {
                Layout.preferredWidth: 190
                Layout.minimumWidth: 190
                Layout.fillHeight: true
                Layout.margins: 8
                spacing: 2
                Repeater {
                    model: root.pages
                    AbstractButton {
                        id: navItem
                        required property string modelData
                        required property int index
                        readonly property bool active: root.page === index
                        Layout.fillWidth: true
                        implicitHeight: 30
                        contentItem: Text {
                            leftPadding: 10
                            verticalAlignment: Text.AlignVCenter
                            text: navItem.modelData
                            color: navItem.active ? Theme.primaryColor : Theme.textSecondary
                            font.family: Theme.monoFamily
                            font.pixelSize: 12
                            font.bold: true
                        }
                        background: Rectangle {
                            radius: 4
                            color: navItem.active ? Theme.glassOverlay : navItem.hovered ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.08) : "transparent"
                            border.width: navItem.active ? 1 : 0
                            border.color: Theme.primaryColor
                        }
                        onClicked: root.page = index
                    }
                }
                Item { Layout.fillHeight: true }
            }
            Rectangle { Layout.fillHeight: true; implicitWidth: 1; color: Theme.borderSoft }

            // La pagina scorre. Senza, una pagina lunga spingeva i pulsanti
            // «Annulla» e «Applica» fuori dalla finestra, e la meta' di sotto
            // non si raggiungeva in nessun modo.
            ScrollView {
                id: pageScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 0
                Layout.minimumHeight: 0
                padding: 14
                clip: true
                contentWidth: availableWidth
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical: PanelScrollBar {}

            StackLayout {
                id: pageStack
                // La riga non si allunga all'infinito: su uno schermo largo un
                // campo lungo un metro non si legge meglio, si legge peggio.
                readonly property real pageWidth: Math.min(920, pageScroll.availableWidth)
                readonly property Item current: pageStack.children[root.page] || null
                width: pageWidth
                x: Math.max(0, (pageScroll.availableWidth - pageWidth) / 2)
                height: Math.max(pageScroll.availableHeight,
                                 current ? current.implicitHeight : 0)
                clip: true
                currentIndex: root.page

                // ── General ─────────────────────────────────────────────────
                ColumnLayout {
                    spacing: 12
                    SectionTitle { text: qsTr("This copy of DecoDXLog") }
                    Note {
                        text: qsTr("DecoDXLog %1 · Qt %2 · %3").arg(decolog.version).arg(decolog.qtVersion).arg(decolog.buildInfo)
                    }

                    SectionTitle { text: qsTr("Log file") }
                    RowLayout {
                        Layout.fillWidth: true
                        StyledTextField { Layout.fillWidth: true; readOnly: true; text: decolog.databasePath }
                        GlassButton { text: qsTr("Open folder"); onClicked: decolog.openDatabaseFolder() }
                    }
                    Note { text: qsTr("SQLite in WAL mode. To use another file start DecoDXLog with --db <path>.") }
                    SectionTitle { text: qsTr("DXCC entities") }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Tile { label: qsTr("cty.csv"); value: decolog.countriesVersion || "—" }
                        Tile { label: qsTr("Entities"); value: decolog.countriesEntities }
                        Tile {
                            label: qsTr("QSO without DXCC")
                            value: decolog.missingDxccCount
                            valueColor: decolog.missingDxccCount > 0 ? Theme.warningColor : Theme.textPrimary
                        }
                    }
                    RowLayout {
                        spacing: 8
                        GlassButton {
                            text: qsTr("Fill missing DXCC")
                            tone: Theme.primaryColor
                            filled: true
                            enabled: decolog.missingDxccCount > 0
                            onClicked: ctyNote.text = qsTr("%1 QSO completed, each kept as a new revision.").arg(decolog.fillMissingDxcc())
                        }
                        GlassButton { text: qsTr("Load newer cty.csv…"); onClicked: ctyDialog.open() }
                    }
                    Note {
                        id: ctyNote
                        text: qsTr("Source: %1. Updated files: country-files.com (AD1C). New QSOs from Decodium and manual entries get DXCC, country, zones and continent automatically; imported ADIF is kept as it is.").arg(decolog.countriesSource)
                    }
                    SectionTitle { text: qsTr("Language") }
                    RowLayout {
                        spacing: 12
                        LabeledField {
                            label: qsTr("Interface")
                            StyledComboBox {
                                Layout.preferredWidth: 240
                                // Le quindici lingue di casa Decodium, ognuna
                                // scritta come la scrive chi la parla: chi cerca
                                // la propria la riconosce senza tradurre niente.
                                readonly property var codes: ["auto", "it", "en", "de", "fr", "es", "ca",
                                                              "nl", "da", "hu", "ro", "lv", "ru", "ja",
                                                              "zh", "zh_TW"]
                                model: [qsTr("Like the system"), "Italiano", "English", "Deutsch",
                                        "Français", "Español", "Català", "Nederlands", "Dansk",
                                        "Magyar", "Română", "Latviešu", "Русский", "日本語",
                                        "简体中文", "繁體中文"]
                                currentIndex: Math.max(0, codes.indexOf(decolog.uiLanguage))
                                onActivated: decolog.uiLanguage = codes[currentIndex]
                            }
                        }
                    }
                    Note { text: qsTr("The new language shows up the next time DecoDXLog starts.") }

                    // ── Aggiornamenti ──────────────────────────────────────
                    SectionTitle { text: qsTr("Updates") }
                    ToggleSwitch {
                        text: qsTr("Look for a new version by itself")
                        checked: decolog.updates.automatic
                        onToggled: decolog.updates.automatic = checked
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        GlassButton {
                            text: decolog.updates.checking ? qsTr("Looking…") : qsTr("Look now")
                            enabled: !decolog.updates.checking
                            onClicked: decolog.updates.checkNow()
                        }
                        Pill {
                            visible: decolog.updates.available
                            text: qsTr("DecoDXLog %1 is out").arg(decolog.updates.latestVersion)
                            tone: Theme.accentColor
                            interactive: true
                            onClicked: root.updateRequested()
                        }
                        Text {
                            Layout.fillWidth: true
                            text: decolog.updates.status
                            color: Theme.textSecondary
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }
                    Note {
                        text: qsTr("Once a day DecoDXLog asks GitHub if there is a newer version, and says so "
                                   + "only when there is. Downloading and installing is up to you: nothing "
                                   + "changes under your feet while you are working. Last look: %1")
                               .arg(decolog.updates.lastCheck.length ? decolog.updates.lastCheck : qsTr("never"))
                    }

                    SectionTitle { text: qsTr("Call info") }
                    ToggleSwitch {
                        text: qsTr("Follow the DX call Decodium is working")
                        checked: decolog.followDxCall
                        onToggled: decolog.followDxCall = checked
                    }
                    SectionTitle { text: qsTr("Duplicates") }
                    RowLayout {
                        spacing: 10
                        LabeledField {
                            label: qsTr("Digital (min)")
                            StyledComboBox {
                                Layout.preferredWidth: 110
                                model: [1, 2, 5, 10]
                                currentIndex: Math.max(0, model.indexOf(decolog.dedupDigitalMinutes))
                                onActivated: decolog.dedupDigitalMinutes = model[currentIndex]
                            }
                        }
                        LabeledField {
                            label: qsTr("Manual (min)")
                            StyledComboBox {
                                Layout.preferredWidth: 110
                                model: [5, 10, 15, 30]
                                currentIndex: Math.max(0, model.indexOf(decolog.dedupManualMinutes))
                                onActivated: decolog.dedupManualMinutes = model[currentIndex]
                            }
                        }
                    }
                    Note { text: qsTr("Same call, band and mode/submode within this window counts as the same QSO.") }
                    Item { Layout.fillHeight: true }
                }

                // ── Theme & density ─────────────────────────────────────────
                ColumnLayout {
                    spacing: 12
                    GridLayout {
                        columns: 2
                        columnSpacing: 14
                        rowSpacing: 10
                        FieldLabel { text: qsTr("Theme") }
                        StyledComboBox {
                            Layout.preferredWidth: 220
                            model: Theme.availableThemes
                            currentIndex: Theme.availableThemes.indexOf(Theme.currentTheme)
                            onActivated: Theme.currentTheme = currentText
                        }
                        FieldLabel { text: qsTr("Accent (Darkcodium)") }
                        StyledComboBox {
                            Layout.preferredWidth: 220
                            enabled: Theme.currentTheme === "Darkcodium"
                            model: Theme.availableVariants
                            currentIndex: Theme.availableVariants.indexOf(Theme.accentVariant)
                            onActivated: Theme.accentVariant = currentText
                        }
                        FieldLabel { text: qsTr("Density") }
                        StyledComboBox {
                            Layout.preferredWidth: 220
                            model: Theme.availableDensities
                            currentIndex: Theme.availableDensities.indexOf(Theme.density)
                            onActivated: Theme.density = currentText
                        }
                        FieldLabel { text: qsTr("Custom colors") }
                        ToggleSwitch {
                            text: qsTr("Background and text over the theme")
                            checked: Theme.customColorsEnabled
                            onToggled: Theme.customColorsEnabled = checked
                        }
                        FieldLabel { text: qsTr("Background") }
                        StyledTextField {
                            Layout.preferredWidth: 220
                            enabled: Theme.customColorsEnabled
                            placeholderText: "#RRGGBB"
                            text: Theme.customBgColor
                            onEditingFinished: Theme.customBgColor = text
                        }
                        FieldLabel { text: qsTr("Text") }
                        StyledTextField {
                            Layout.preferredWidth: 220
                            enabled: Theme.customColorsEnabled
                            placeholderText: "#RRGGBB"
                            text: Theme.customTextColor
                            onEditingFinished: Theme.customTextColor = text
                        }
                    }
                    Note { text: qsTr("Same themes, accents and densities as Decodium: row %1 px · font %2 px · header %3 px.").arg(Theme.rowHeight).arg(Theme.fontSize).arg(Theme.panelHeight) }
                    Item { Layout.fillHeight: true }
                }

                // ── Decodium link ───────────────────────────────────────────
                ColumnLayout {
                    spacing: 12
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: Math.max(56, children[0].implicitHeight + 24)
                        radius: 5
                        color: decolog.clientConnected ? Theme.rowMatchBg : "transparent"
                        border.width: 1
                        border.color: decolog.clientConnected ? Theme.accentColor : Theme.glassBorder
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 12
                            Led { size: 10; glow: decolog.clientConnected; color: decolog.clientConnected ? Theme.accentColor : Theme.textSecondary }
                            Column {
                                Layout.fillWidth: true
                                Text {
                                    text: decolog.clientConnected ? qsTr("%1 · connected").arg(decolog.clientName)
                                                                  : qsTr("No client heard yet")
                                    color: Theme.textPrimary
                                    font.family: Theme.monoFamily
                                    font.pixelSize: 13
                                    font.bold: true
                                }
                                Text {
                                    text: decolog.udpError.length ? decolog.udpError
                                         : decolog.listening ? qsTr("listening on UDP %1").arg(decolog.udpPort) : qsTr("not listening")
                                    color: decolog.udpError.length ? Theme.errorColor : Theme.textSecondary
                                    font.family: Theme.monoFamily
                                    font.pixelSize: 11
                                }
                            }
                        }
                    }
                    SectionTitle { text: qsTr("Decodium / WSJT-X UDP") }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 3
                        columnSpacing: 10
                        LabeledField {
                            Layout.preferredWidth: 100
                            label: qsTr("Port")
                            StyledTextField { id: portField; Layout.fillWidth: true; validator: IntValidator { bottom: 0; top: 65535 } }
                        }
                        LabeledField {
                            Layout.fillWidth: true
                            // Il nome del messaggio non si abbrevia bene: e' quello che
                            // l'operatore cerca nelle impostazioni di Decodium.
                            Layout.horizontalStretchFactor: 3
                            label: qsTr("Primary source")
                            StyledComboBox {
                                Layout.fillWidth: true
                                model: [qsTr("LoggedADIF (lossless)"), qsTr("QSOLogged (structured)")]
                                currentIndex: decolog.preferLoggedAdif ? 0 : 1
                                onActivated: decolog.preferLoggedAdif = currentIndex === 0
                            }
                        }
                        LabeledField {
                            Layout.fillWidth: true
                            Layout.horizontalStretchFactor: 2
                            label: qsTr("Multicast group")
                            StyledTextField { id: groupField; Layout.fillWidth: true; placeholderText: qsTr("empty = unicast") }
                        }
                    }
                    Note { text: qsTr("In Decodium set the UDP server to this address and port. A multicast group (e.g. 239.255.0.1) shares the stream with GridTracker or JTAlert. Duplicate windows are in General.") }

                    SectionTitle { text: qsTr("DecoLink · log towards Decodium") }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        ToggleSwitch {
                            text: qsTr("Share the log with Decodium")
                            checked: decolog.decoLinkEnabled
                            onToggled: decolog.decoLinkEnabled = checked
                        }
                        LabeledField {
                            label: qsTr("Port (127.0.0.1)")
                            StyledTextField {
                                Layout.preferredWidth: 100
                                text: decolog.decoLinkPort
                                validator: IntValidator { bottom: 1; top: 65535 }
                                onEditingFinished: decolog.decoLinkPort = parseInt(text)
                            }
                        }
                        Item { Layout.fillWidth: true }
                        Pill {
                            text: !decolog.decoLinkEnabled ? qsTr("off")
                                : !decolog.decoLinkListening ? qsTr("error")
                                : decolog.decoLinkClients.length ? qsTr("%n client(s)", "", decolog.decoLinkClients.length)
                                : qsTr("waiting")
                            tone: !decolog.decoLinkEnabled ? Theme.textSecondary
                                : !decolog.decoLinkListening ? Theme.errorColor
                                : decolog.decoLinkClients.length ? Theme.accentColor : Theme.textSecondary
                        }
                    }
                    Repeater {
                        model: decolog.decoLinkClients
                        Text {
                            required property var modelData
                            text: "● " + [modelData.app, modelData.version, modelData.station].filter(s => s).join(" · ")
                            color: Theme.accentColor
                            font.family: Theme.monoFamily
                            font.pixelSize: 12
                        }
                    }
                    Note {
                        text: decolog.decoLinkError.length && decolog.decoLinkEnabled
                              ? decolog.decoLinkError
                              : qsTr("Decodium receives the worked calls, confirmations and FT2 Award status from this log, and a confirmation for every QSO written. Only local connections are accepted. Protocol: docs/DECOLINK.md.")
                        color: decolog.decoLinkError.length && decolog.decoLinkEnabled ? Theme.errorColor : Theme.textSecondary
                    }
                    Item { Layout.fillHeight: true }
                }

                // ── Sync & Cloud ────────────────────────────────────────────
                ColumnLayout {
                    spacing: 14
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: Math.max(56, children[0].implicitHeight + 24)
                        radius: 5
                        color: "transparent"
                        border.width: 1
                        border.color: Theme.glassBorder
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 12
                            Led {
                                size: 10
                                color: decolog.cloud.linked ? Theme.accentColor
                                     : decolog.cloud.server.length ? Theme.warningColor : Theme.textSecondary
                            }
                            Column {
                                Layout.fillWidth: true
                                Text {
                                    text: decolog.cloud.linked
                                          ? qsTr("DecoDXLog Cloud · %1").arg(decolog.cloud.callsign)
                                          : qsTr("DecoDXLog Cloud · not linked")
                                    color: Theme.textPrimary
                                    font.family: Theme.monoFamily
                                    font.pixelSize: 13
                                    font.bold: true
                                }
                                Text {
                                    width: parent.width
                                    wrapMode: Text.Wrap
                                    text: decolog.cloud.status.length ? decolog.cloud.status
                                        : decolog.cloud.linked
                                          ? qsTr("%1 QSO on the server · queue %2")
                                                .arg(decolog.cloud.remote.qsos !== undefined ? decolog.cloud.remote.qsos : "—")
                                                .arg(decolog.cloud.queued)
                                          : qsTr("The log stays yours and works offline: the Cloud is where your devices pass each other the changes.")
                                    color: Theme.textSecondary
                                    font.family: Theme.monoFamily
                                    font.pixelSize: 11
                                }
                            }
                            GlassButton {
                                text: decolog.cloud.busy ? qsTr("syncing…") : qsTr("Sync now")
                                tone: Theme.primaryColor
                                filled: true
                                enabled: decolog.cloud.linked && !decolog.cloud.busy
                                onClicked: decolog.cloud.syncNow()
                            }
                            GlassButton {
                                text: qsTr("Unlink")
                                visible: decolog.cloud.linked
                                onClicked: decolog.cloud.logout()
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Tile { label: qsTr("Last sync"); value: decolog.cloud.lastSync || qsTr("never") }
                        Tile {
                            label: qsTr("On the server")
                            value: decolog.cloud.remote.qsos !== undefined ? decolog.cloud.remote.qsos : "—"
                        }
                        Tile { label: qsTr("Queue (dirty)"); value: decolog.dirtyCount; valueColor: decolog.dirtyCount > 0 ? Theme.warningColor : Theme.textPrimary }
                        Tile { label: qsTr("Conflicts kept"); value: qsTr("%1 in history").arg(decolog.conflictCount) }
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 10
                        LabeledField {
                            Layout.fillWidth: true
                            label: qsTr("Server")
                            StyledTextField {
                                id: serverField
                                Layout.fillWidth: true
                                mono: false
                                placeholderText: "http://127.0.0.1:8787"
                            }
                        }
                        LabeledField {
                            Layout.fillWidth: true
                            label: qsTr("Auto sync")
                            StyledComboBox {
                                Layout.fillWidth: true
                                readonly property var values: ["qso", "timer", "manual"]
                                model: [qsTr("After every QSO + every 5 min"), qsTr("Every 5 min"), qsTr("Manual only")]
                                currentIndex: Math.max(0, values.indexOf(decolog.cloud.autoMode))
                                onActivated: decolog.cloud.autoMode = values[currentIndex]
                            }
                        }
                    }

                    // ── Accesso ─────────────────────────────────────────────
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        visible: !decolog.cloud.linked
                        SectionTitle { text: qsTr("Sign in") }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            LabeledField {
                                label: qsTr("Callsign")
                                StyledTextField {
                                    id: cloudCall
                                    Layout.preferredWidth: 150
                                    uppercase: true
                                    text: decolog.cloud.callsign
                                }
                            }
                            LabeledField {
                                label: qsTr("Password")
                                StyledTextField {
                                    id: cloudPassword
                                    Layout.preferredWidth: 220
                                    mono: false
                                    echoMode: TextInput.Password
                                    // Invio fa quello che fa il tasto: stesso
                                    // indirizzo, stessa pulizia dopo.
                                    Keys.onReturnPressed: {
                                        decolog.cloud.server = serverField.text
                                        decolog.cloud.login(cloudCall.text, cloudPassword.text)
                                        cloudPassword.text = ""
                                    }
                                }
                            }
                            GlassButton {
                                Layout.alignment: Qt.AlignBottom
                                Layout.bottomMargin: 2
                                text: qsTr("Sign in")
                                tone: Theme.primaryColor
                                filled: true
                                // Il nominativo e' quello che e': non c'e' una
                                // lunghezza che valga per tutto il mondo.
                                enabled: !decolog.cloud.busy && cloudCall.text.trim().length > 0
                                         && cloudPassword.text.length >= 8
                                onClicked: {
                                    decolog.cloud.server = serverField.text
                                    decolog.cloud.login(cloudCall.text, cloudPassword.text)
                                    cloudPassword.text = ""
                                }
                            }
                            GlassButton {
                                Layout.alignment: Qt.AlignBottom
                                Layout.bottomMargin: 2
                                text: qsTr("Create account")
                                // Il nominativo e' quello che e': non c'e' una
                                // lunghezza che valga per tutto il mondo.
                                enabled: !decolog.cloud.busy && cloudCall.text.trim().length > 0
                                         && cloudPassword.text.length >= 8
                                onClicked: {
                                    decolog.cloud.server = serverField.text
                                    decolog.cloud.signup(cloudCall.text, cloudPassword.text)
                                    cloudPassword.text = ""
                                }
                            }
                        }
                        Note {
                            text: qsTr("The password travels once and is not kept: DecoDXLog stores only the token the "
                                       + "server gives back, in the system keystore. On the network use HTTPS; at home, "
                                       + "on your own LAN, plain HTTP is fine.")
                        }
                    }
                    ColumnLayout {
                        spacing: 6
                        SectionTitle { text: qsTr("Conflicts") }
                        RowLayout {
                            spacing: 16
                            RadioChoice {
                                text: qsTr("Last edit wins, loser kept in history")
                                checked: decolog.conflictPolicy === "lastEdit"
                                onClicked: decolog.conflictPolicy = "lastEdit"
                            }
                            RadioChoice {
                                text: qsTr("Always ask")
                                checked: decolog.conflictPolicy === "ask"
                                onClicked: decolog.conflictPolicy = "ask"
                            }
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        SectionTitle { text: qsTr("Credentials · system keystore") }
                        ToggleSwitch {
                            Layout.topMargin: 4
                            Layout.bottomMargin: 2
                            enabled: decolog.cloud.vaultAvailable
                            text: qsTr("Carry the service passwords to the other devices too")
                            checked: decolog.cloud.syncSecrets && decolog.cloud.vaultAvailable
                            onToggled: decolog.cloud.syncSecrets = checked
                        }
                        // Chi si e' collegato prima che la cassaforte esistesse
                        // ha la chiave mancante: si fa qui, senza scollegarsi.
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            visible: decolog.cloud.linked && decolog.cloud.vaultAvailable
                                     && decolog.cloud.syncSecrets && !decolog.cloud.vaultReady
                            LabeledField {
                                label: qsTr("Cloud password, to open the vault on this device")
                                StyledTextField {
                                    id: vaultPassword
                                    Layout.preferredWidth: 220
                                    mono: false
                                    echoMode: TextInput.Password
                                    Keys.onReturnPressed: {
                                        decolog.cloud.unlockVault(vaultPassword.text)
                                        vaultPassword.text = ""
                                    }
                                }
                            }
                            GlassButton {
                                Layout.alignment: Qt.AlignBottom
                                Layout.bottomMargin: 2
                                text: qsTr("Open the vault")
                                tone: Theme.primaryColor
                                enabled: vaultPassword.text.length >= 8
                                onClicked: {
                                    decolog.cloud.unlockVault(vaultPassword.text)
                                    vaultPassword.text = ""
                                }
                            }
                        }
                        Note {
                            text: decolog.cloud.vaultAvailable
                                  ? qsTr("They travel sealed: DecoDXLog closes them on this computer with AES-256-GCM "
                                         + "and a key made from your Cloud password, which the server only knows as "
                                         + "an Argon2 fingerprint. What reaches the server is a block of bytes that "
                                         + "does not open without that password. Sign in on the other device with "
                                         + "the same password and the services are ready there too.")
                                  : qsTr("This build has no OpenSSL: the service passwords cannot be sealed, so they "
                                         + "stay on this computer.")
                        }
                        CredentialsList {
                            Layout.fillWidth: true
                            serviceIds: ["cloud", "qrz", "qrzlogbook", "lotw", "clublog", "eqsl", "crx", "hamqth"]
                        }
                    }

                    // ── Zona pericolosa ──────────────────────────────────
                    // Si vede sempre, anche scollegati: una funzione che sparisce
                    // e' una funzione che non c'e', e chi la cerca non la trova.
                    // Scollegati il tasto e' spento e la riga sotto dice perche'.
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 10
                        spacing: 6
                        SectionTitle { text: qsTr("Danger zone") }
                        Note {
                            text: qsTr("Empty the Cloud of this callsign: QSO, history, station profiles, "
                                       + "settings and sealed credentials go away from the server for good. "
                                       + "The account stays, and the log on this computer is not touched — at "
                                       + "the next sync it all goes back up from scratch. The other devices, "
                                       + "though, will find nothing up there.")
                        }
                        GlassButton {
                            Layout.alignment: Qt.AlignLeft
                            text: qsTr("Empty the Cloud…")
                            tone: Theme.errorColor
                            enabled: decolog.cloud.linked && !decolog.cloud.busy
                            onClicked: purgeCloudDialog.openDialog()
                        }
                        Note {
                            visible: !decolog.cloud.linked
                            text: qsTr("Sign in above first: emptying the Cloud is something only the owner "
                                       + "of this callsign can ask for.")
                        }
                    }
                    Item { Layout.fillHeight: true }
                }

                // ── QSL services ────────────────────────────────────────────
                ColumnLayout {
                    spacing: 12
                    SectionTitle { text: qsTr("LoTW confirmations") }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Tile { label: qsTr("Last sync"); value: decolog.lotwLastSync || qsTr("never") }
                        Tile { label: qsTr("Confirmations since"); value: decolog.lotwCursor || qsTr("all") }
                        Tile {
                            label: qsTr("Confirmed in log")
                            value: (decolog.qslSummary.find(r => r.service === "lotw") || {}).confirmed || 0
                        }
                    }
                    RowLayout {
                        spacing: 8
                        GlassButton {
                            text: decolog.lotwBusy ? qsTr("Downloading…") : qsTr("Sync now")
                            tone: Theme.accentColor
                            filled: true
                            enabled: !decolog.lotwBusy
                            onClicked: decolog.syncLotw(false)
                        }
                        GlassButton {
                            text: qsTr("Download everything again")
                            enabled: !decolog.lotwBusy
                            onClicked: decolog.syncLotw(true)
                        }
                        GlassButton {
                            visible: decolog.lotwBusy
                            text: qsTr("Cancel")
                            onClicked: decolog.cancelLotw()
                        }
                    }
                    RowLayout {
                        spacing: 8
                        Text { text: qsTr("Automatic sync"); color: Theme.textSecondary; font.pixelSize: 12 }
                        StyledComboBox {
                            Layout.preferredWidth: 150
                            readonly property var hours: [0, 6, 12, 24]
                            model: [qsTr("Off"), qsTr("Every 6 hours"), qsTr("Every 12 hours"), qsTr("Once a day")]
                            currentIndex: Math.max(0, hours.indexOf(decolog.lotwAutoHours))
                            onActivated: decolog.lotwAutoHours = hours[currentIndex]
                        }
                    }
                    Note {
                        visible: decolog.lotwStatus.length > 0
                        text: decolog.lotwStatus
                        color: decolog.lotwStatus.indexOf("LoTW: ") === 0 && /incorrect|error|not available|cancel|HTTP|unexpected|web page/i.test(decolog.lotwStatus)
                               ? Theme.errorColor : Theme.textPrimary
                    }
                    Note {
                        text: qsTr("Confirmations are matched by call, band, mode group (data, CW, phone) and time within 30 minutes, as LoTW does. "
                                   + "A confirmed QSO becomes a new revision; grid, zones, state and county from LoTW fill only empty fields. "
                                   + "Uploading to LoTW still goes through TQSL. QRZ Logbook, Club Log and eQSL arrive later.")
                    }
                    SectionTitle { text: qsTr("Sending to LoTW (TQSL)") }
                    Note { text: decolog.qsl.tqslStatus }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        LabeledField {
                            Layout.fillWidth: true
                            label: qsTr("TQSL program")
                            StyledTextField {
                                id: tqslField
                                Layout.fillWidth: true
                                mono: false
                                text: decolog.qsl.tqslPath
                                placeholderText: "C:/Program Files (x86)/TrustedQSL/tqsl.exe"
                                onEditingFinished: decolog.qsl.tqslPath = text
                            }
                        }
                        GlassButton { text: qsTr("Browse…"); Layout.alignment: Qt.AlignBottom; onClicked: tqslDialog.open() }
                        LabeledField {
                            label: qsTr("Station location")
                            StyledComboBox {
                                Layout.preferredWidth: 200
                                readonly property var names: [""].concat(decolog.qsl.tqslLocations)
                                model: [qsTr("From the station profile")].concat(decolog.qsl.tqslLocations)
                                currentIndex: Math.max(0, names.indexOf(decolog.qsl.tqslLocation))
                                onActivated: decolog.qsl.tqslLocation = names[currentIndex]
                            }
                        }
                    }
                    Note {
                        text: qsTr("The certificate stays in TQSL: DecoDXLog writes a temporary ADIF, TQSL signs it and sends it. "
                                   + "Duplicates are not an error, LoTW simply keeps the one it already has. Sending, automatic sending "
                                   + "and the counters are in the QSL tab at the bottom.")
                    }
                    SectionTitle { text: "Club Log" }
                    RowLayout {
                        spacing: 12
                        LabeledField {
                            label: qsTr("API key")
                            StyledTextField {
                                Layout.preferredWidth: 280
                                mono: false
                                text: decolog.qsl.clubLogApiKey
                                placeholderText: qsTr("the key Club Log gave you")
                                onEditingFinished: decolog.qsl.clubLogApiKey = text
                            }
                        }
                    }
                    Note {
                        text: qsTr("Club Log wants three things: the email and password of the account (below), the callsign of the "
                                   + "station profile, and an API key. The key is free and personal, and is asked for at "
                                   + "clublog.org/need_api.php — it identifies the program, not you. A single QSO leaves as soon as "
                                   + "it is logged, a backlog leaves as one ADIF file.")
                    }
                    // ── CRX Logbook (crx.cloud) ─────────────────────────────
                    SectionTitle { text: "CRX Logbook" }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        LabeledField {
                            label: qsTr("Logbook on CRX")
                            StyledComboBox {
                                id: crxLogBox
                                Layout.preferredWidth: 280
                                readonly property var logs: decolog.qsl.crxLogs
                                model: logs.length > 0 ? logs.map(l => l.name + (l.call ? " · " + l.call : ""))
                                     : [decolog.qsl.crxLogName.length > 0 ? decolog.qsl.crxLogName : qsTr("— load the list —")]
                                currentIndex: {
                                    for (let i = 0; i < logs.length; ++i)
                                        if (logs[i].id === decolog.qsl.crxLogId)
                                            return i
                                    return logs.length > 0 ? -1 : 0
                                }
                                onActivated: if (logs.length > 0) decolog.qsl.crxLogId = logs[currentIndex].id
                            }
                        }
                        GlassButton {
                            Layout.alignment: Qt.AlignBottom
                            text: qsTr("Load my logbooks")
                            onClicked: decolog.qsl.fetchCrxLogs()
                        }
                        LabeledField {
                            label: qsTr("Send QSOs from")
                            StyledTextField {
                                Layout.preferredWidth: 130
                                text: decolog.showDate(decolog.qsl.crxSince)
                                placeholderText: decolog.dateHint
                                onEditingFinished: decolog.qsl.crxSince = decolog.readDate(text)
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: decolog.qsl.crxStatus.length > 0
                        text: decolog.qsl.crxStatus
                        color: Theme.textSecondary
                        font.family: Theme.monoFamily
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }
                    Note {
                        text: qsTr("CRX Logbook is the cloud log of crx.cloud. It wants the API key of your account (it starts "
                                   + "with HAM-, below with the other credentials) and the logbook to write in. It starts from "
                                   + "the day you choose the logbook: older QSOs leave only if you move the date back. Each QSO "
                                   + "goes with its date and time and with its DecoDXLog number (custom field 38). CRX "
                                   + "keeps it in step: a QSO corrected here is corrected there, one deleted here is "
                                   + "deleted there, and with the network down they wait in the queue and leave at the "
                                   + "next sending. Sending, automatic sending and the counters are in the QSL tab at the bottom.")
                    }
                    CredentialsList {
                        Layout.fillWidth: true
                        serviceIds: ["lotw", "qrzlogbook", "clublog", "eqsl", "crx"]
                    }

                    // ── La casella da cui partono le cartoline ─────────────
                    SectionTitle { text: qsTr("QSL by email") }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        LabeledField {
                            label: qsTr("Who puts the card in the post")
                            StyledComboBox {
                                Layout.preferredWidth: 320
                                readonly property var ids: ["cloud", "mailbox"]
                                model: [qsTr("The Cloud, in my name"), qsTr("My own mailbox")]
                                currentIndex: Math.max(0, ids.indexOf(decolog.cards.mail.route))
                                onActivated: decolog.cards.setMailRoute(ids[currentIndex])
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.family: Theme.monoFamily
                            font.pixelSize: 12
                            color: decolog.cards.mail.ready ? Theme.successColor : Theme.textSecondary
                            text: decolog.cards.mail.ready ? qsTr("Ready to send.")
                                 : decolog.cards.mail.route === "cloud"
                                   ? qsTr("Link the Cloud in the Cloud page: without it there is nowhere to send from.")
                                   : qsTr("Fill in the mail server and the password below.")
                        }
                    }
                    Note {
                        text: decolog.cards.mail.route === "cloud"
                              ? qsTr("The card goes to the DecoDXLog Cloud, and the Cloud posts it — so no mailbox "
                                     + "password stays on this computer. It leaves as «%1 via DecoDXLog», and whoever "
                                     + "answers writes to you, not to the service. There is a ceiling of cards per day: "
                                     + "a shared mailbox that sends too much ends up in the spam lists, and it would "
                                     + "end up there for everybody at once.").arg(decolog.cards.stationInfo().call || qsTr("your callsign"))
                              : qsTr("The card goes out from your own mailbox, and the address of whoever gets it comes "
                                     + "from the callbook — QRZ.com or HamQTH. With Gmail it wants an app password, "
                                     + "not the one you sign in with: you make it at myaccount.google.com/apppasswords. "
                                     + "Nothing leaves without you asking: the button is in QSL card → Send by email.")
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        visible: decolog.cards.mail.route !== "cloud"
                        LabeledField {
                            Layout.fillWidth: true
                            label: qsTr("Mail server")
                            StyledTextField {
                                id: mailHost
                                Layout.fillWidth: true
                                text: decolog.cards.mail.host
                                placeholderText: "smtp.gmail.com"
                                onEditingFinished: decolog.cards.setMail({ "host": text })
                            }
                        }
                        LabeledField {
                            Layout.preferredWidth: 90
                            label: qsTr("Port")
                            StyledTextField {
                                id: mailPort
                                Layout.fillWidth: true
                                text: decolog.cards.mail.port
                                onEditingFinished: decolog.cards.setMail({ "port": parseInt(text) })
                            }
                        }
                        LabeledField {
                            Layout.fillWidth: true
                            label: qsTr("Your name in the message")
                            StyledTextField {
                                Layout.fillWidth: true
                                mono: false
                                text: decolog.cards.mail.fromName
                                placeholderText: decolog.cards.stationInfo().call || ""
                                onEditingFinished: decolog.cards.setMail({ "fromName": text })
                            }
                        }
                    }
                    Note {
                        visible: decolog.cards.mail.route !== "cloud"
                        text: qsTr("587 asks for encryption with STARTTLS, 465 is encrypted from the first byte. "
                                   + "A server that offers neither is refused: the password travels through there.")
                    }
                    LabeledField {
                        Layout.fillWidth: true
                        label: qsTr("Subject")
                        StyledTextField {
                            Layout.fillWidth: true
                            mono: false
                            text: decolog.cards.mail.subject
                            onEditingFinished: decolog.cards.setMail({ "subject": text })
                        }
                    }
                    LabeledField {
                        Layout.fillWidth: true
                        label: qsTr("Message")
                        StyledTextArea {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 120
                            text: decolog.cards.mail.body
                            onEditingFinished: decolog.cards.setMail({ "body": text })
                        }
                    }
                    Note {
                        text: qsTr("In the subject and the message: {CALL} {NAME} {DATE} {TIME} {BAND} {MODE} {RST} "
                                   + "for the station you worked, {MYCALL} and {MYNAME} for yourself.")
                    }
                    CredentialsList {
                        Layout.fillWidth: true
                        visible: decolog.cards.mail.route !== "cloud"
                        serviceIds: ["mail"]
                    }
                    Repeater {
                        model: decolog.qslSummary
                        RowLayout {
                            required property var modelData
                            spacing: 10
                            Text { Layout.preferredWidth: 90; text: modelData.label; color: Theme.textPrimary; font.family: Theme.monoFamily; font.pixelSize: 12; font.bold: true }
                            Text {
                                text: qsTr("%1 sent · %2 queued · %3 confirmed").arg(modelData.sent || 0).arg(modelData.queued || 0).arg(modelData.confirmed || 0)
                                color: Theme.textSecondary
                                font.family: Theme.monoFamily
                                font.pixelSize: 12
                            }
                        }
                    }
                    Item { Layout.fillHeight: true }
                }

                // ── Callbook ────────────────────────────────────────────────
                ColumnLayout {
                    spacing: 12
                    SectionTitle { text: qsTr("Callbook") }
                    RowLayout {
                        spacing: 12
                        LabeledField {
                            label: qsTr("Lookup service")
                            StyledComboBox {
                                Layout.preferredWidth: 260
                                readonly property var ids: ["off", "qrz", "hamqth"]
                                model: [qsTr("Off (log and cty.csv only)"), "QRZ.com (XML)", "HamQTH"]
                                currentIndex: Math.max(0, ids.indexOf(decolog.callbookProvider))
                                onActivated: decolog.callbookProvider = ids[currentIndex]
                            }
                        }
                        ToggleSwitch {
                            Layout.alignment: Qt.AlignBottom
                            Layout.bottomMargin: 4
                            text: qsTr("Fill empty name, QTH and grid in New QSO")
                            checked: decolog.callbookAutofill
                            onToggled: decolog.callbookAutofill = checked
                        }
                    }
                    ToggleSwitch {
                        text: qsTr("Complete the QSO just logged (name, QTH, grid, address)")
                        checked: decolog.callbookComplete
                        onToggled: decolog.callbookComplete = checked
                    }
                    ToggleSwitch {
                        text: qsTr("If this callbook doesn't know the callsign, ask the other one")
                        enabled: decolog.callbookProvider !== "off"
                        checked: decolog.callbookFallback
                        onToggled: decolog.callbookFallback = checked
                    }
                    Note {
                        text: qsTr("The two callbooks don't know the same stations: HamQTH has the ones who "
                                   + "signed up there, QRZ has almost everybody. With the fallback on, a "
                                   + "callsign the first one doesn't know is asked to the other — as long as "
                                   + "that one has its user and password here below.")
                    }
                    Note {
                        text: qsTr("Decodium sends callsign, report, band and mode: the rest the callbook knows. "
                                   + "Right after the QSO is written DecoDXLog asks, and what comes back fills only "
                                   + "the empty fields — what you wrote stays. One lookup per callsign, and the "
                                   + "answer is kept for a day. From the log, \"Complete from the callbook\" does "
                                   + "the same on QSOs already written.")
                    }
                    CredentialsList {
                        Layout.fillWidth: true
                        serviceIds: ["qrz", "hamqth"]
                    }
                    SectionTitle { text: qsTr("Try a lookup") }
                    RowLayout {
                        spacing: 8
                        StyledTextField {
                            id: callbookTest
                            Layout.preferredWidth: 160
                            uppercase: true
                            placeholderText: "IU8LMC"
                            Keys.onReturnPressed: testButton.clicked()
                        }
                        GlassButton {
                            id: testButton
                            text: qsTr("Look up")
                            tone: Theme.primaryColor
                            filled: true
                            enabled: decolog.callbookProvider !== "off" && callbookTest.text.trim().length >= 3
                            onClicked: decolog.lookupCall = callbookTest.text
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        readonly property var info: decolog.callInfo
                        readonly property bool mine: callbookTest.text.trim().toUpperCase() === (info.call || "")
                        text: decolog.callbookBusy ? qsTr("Looking up…")
                            : mine && info.callbook ? [info.callbook.call, info.callbook.name, info.callbook.qth,
                                                        info.callbook.grid, info.callbook.country].filter(s => s).join(" · ")
                            : mine && info.callbookError ? info.callbookError
                            : decolog.callbookStatus
                        color: mine && info.callbookError ? Theme.warningColor : Theme.textPrimary
                        font.family: Theme.monoFamily
                        font.pixelSize: 12
                    }
                    Note { text: qsTr("QRZ.com needs an XML data subscription; HamQTH is free. Results are kept in memory for a day, so moving through the log does not use up lookups.") }
                    Item { Layout.fillHeight: true }
                }

                //  Radio (CAT)
                ColumnLayout {
                    spacing: 10

                    SectionTitle { text: decolog.rig.link === "tci" ? qsTr("Radio via TCI") : qsTr("Radio via Hamlib (rigctld)") }
                    Note {
                        visible: decolog.rig.link !== "tci"
                        text: qsTr("Every radio is spoken to by Hamlib, not by DecoDXLog. With the serial cable "
                                   + "pick the model and the port and DecoDXLog starts rigctld by itself; if you "
                                   + "already run rigctld (for a contest program, or on another computer) just "
                                   + "give host and port. From there DecoDXLog reads frequency and mode, can tune "
                                   + "the radio, and hands the CW macros to the rig's own keyer.")
                    }
                    RowLayout {
                        spacing: 12
                        ToggleSwitch {
                            text: qsTr("Talk to the radio")
                            checked: decolog.rig.enabled
                            onToggled: decolog.rig.enabled = checked
                        }
                        LabeledField {
                            label: qsTr("How")
                            StyledComboBox {
                                Layout.preferredWidth: 270
                                readonly property var ids: ["network", "serial", "tci"]
                                model: [qsTr("rigctld already running"), qsTr("Serial cable to the radio"),
                                        qsTr("TCI (SDR, as in Decodium)")]
                                currentIndex: Math.max(0, ids.indexOf(decolog.rig.link))
                                onActivated: decolog.rig.link = ids[currentIndex]
                            }
                        }
                    }

                    //  La radio sul cavo: si scelgono modello, porta e velocita',
                    //  e rigctld lo avvia DecoDXLog.
                    RowLayout {
                        spacing: 12
                        visible: decolog.rig.link === "serial"
                        LabeledField {
                            label: qsTr("Radio (Hamlib)")
                            StyledComboBox {
                                id: modelBox
                                Layout.preferredWidth: 320
                                mono: false
                                editable: true
                                readonly property var models: decolog.rig.rigModels()
                                model: models.map(m => m.name)
                                onActivated: decolog.rig.rigModel = models[currentIndex].id
                                Component.onCompleted: {
                                    for (let i = 0; i < models.length; ++i) {
                                        if (models[i].id === decolog.rig.rigModel)
                                            currentIndex = i
                                    }
                                }
                            }
                        }
                        LabeledField {
                            label: qsTr("Serial port")
                            StyledComboBox {
                                id: serialPortBox
                                Layout.preferredWidth: 140
                                editable: true
                                model: decolog.rig.serialPorts()
                                // La porta scelta si scrive solo quando la
                                // sceglie l'operatore: legandola al testo si
                                // riscriveva da sola sulla prima della lista
                                // appena si apriva la pagina, e la radio
                                // finiva sulla porta sbagliata.
                                Component.onCompleted: editText = decolog.rig.serialPort
                                onActivated: decolog.rig.serialPort = currentText
                                onEditTextChanged: if (activeFocus) decolog.rig.serialPort = editText
                                Connections {
                                    target: decolog.rig
                                    function onChanged() {
                                        if (!serialPortBox.activeFocus)
                                            serialPortBox.editText = decolog.rig.serialPort
                                    }
                                }
                            }
                        }
                        LabeledField {
                            label: qsTr("Baud")
                            StyledComboBox {
                                Layout.preferredWidth: 110
                                readonly property var speeds: [4800, 9600, 19200, 38400, 57600, 115200]
                                model: speeds.map(String)
                                currentIndex: Math.max(0, speeds.indexOf(decolog.rig.baud))
                                onActivated: decolog.rig.baud = speeds[currentIndex]
                            }
                        }
                    }

                    //  Il PTT: chi ha due porte tiene il CAT su una e il PTT
                    //  sull'altra, ed e' la maniera che funziona sempre.
                    RowLayout {
                        spacing: 12
                        visible: decolog.rig.link === "serial"
                        LabeledField {
                            label: qsTr("PTT")
                            StyledComboBox {
                                Layout.preferredWidth: 220
                                readonly property var kinds: ["RIG", "RTS", "DTR", "NONE"]
                                model: [qsTr("The CAT itself (RIG)"), qsTr("RTS on another port"),
                                        qsTr("DTR on another port"), qsTr("None")]
                                currentIndex: Math.max(0, kinds.indexOf(decolog.rig.pttType))
                                onActivated: decolog.rig.pttType = kinds[currentIndex]
                            }
                        }
                        LabeledField {
                            label: qsTr("PTT port")
                            StyledComboBox {
                                id: pttPortBox
                                Layout.preferredWidth: 140
                                editable: true
                                enabled: decolog.rig.pttType === "RTS" || decolog.rig.pttType === "DTR"
                                model: decolog.rig.serialPorts()
                                Component.onCompleted: editText = decolog.rig.pttPort
                                onActivated: decolog.rig.pttPort = currentText
                                onEditTextChanged: if (activeFocus) decolog.rig.pttPort = editText
                                Connections {
                                    target: decolog.rig
                                    function onChanged() {
                                        if (!pttPortBox.activeFocus)
                                            pttPortBox.editText = decolog.rig.pttPort
                                    }
                                }
                            }
                        }
                        GlassButton {
                            Layout.alignment: Qt.AlignBottom
                            Layout.bottomMargin: 2
                            text: qsTr("Test the PTT")
                            tone: Theme.warningColor
                            enabled: decolog.rig.connected
                            onClicked: decolog.rig.testPtt(900)
                        }
                    }
                    Note {
                        visible: decolog.rig.link === "serial"
                        text: qsTr("Two ports is the usual setup: the CAT reads the frequency on one, the "
                                   + "PTT raises RTS or DTR on the other — so the radio transmits while "
                                   + "DecoDXLog keeps reading. With a single cable leave \"the CAT itself\": "
                                   + "the radio goes into transmit on the CAT command, if it can.")
                    }

                    RowLayout {
                        spacing: 12
                        visible: decolog.rig.link === "network"
                        LabeledField {
                            label: qsTr("Host")
                            StyledTextField {
                                Layout.preferredWidth: 180
                                text: decolog.rig.host
                                onEditingFinished: decolog.rig.host = text
                            }
                        }
                        LabeledField {
                            label: qsTr("Port")
                            StyledTextField {
                                Layout.preferredWidth: 90
                                text: String(decolog.rig.port)
                                onEditingFinished: decolog.rig.port = parseInt(text) || 4532
                            }
                        }
                        GlassButton {
                            Layout.alignment: Qt.AlignBottom
                            Layout.bottomMargin: 2
                            text: qsTr("Connect now")
                            tone: Theme.primaryColor
                            onClicked: decolog.rig.connectNow()
                        }
                        // Se non si sa quale porta e' quella giusta, la cerca lui.
                        GlassButton {
                            Layout.alignment: Qt.AlignBottom
                            Layout.bottomMargin: 2
                            text: decolog.rig.probing ? qsTr("Looking…") : qsTr("Find the radio")
                            tone: Theme.accentColor
                            enabled: !decolog.rig.probing
                            onClicked: decolog.rig.probeRadio()
                        }
                    }
                    // TCI: il WebSocket del programma della radio (ExpertSDR,
                    // Thetis, SDR Console…), come in Decodium.
                    RowLayout {
                        spacing: 12
                        visible: decolog.rig.link === "tci"
                        LabeledField {
                            label: qsTr("TCI server")
                            StyledTextField {
                                Layout.preferredWidth: 200
                                text: decolog.rig.tciAddress
                                placeholderText: "127.0.0.1:40001"
                                onEditingFinished: decolog.rig.tciAddress = text
                            }
                        }
                        LabeledField {
                            label: qsTr("Receiver")
                            StyledComboBox {
                                Layout.preferredWidth: 110
                                model: ["RX1", "RX2"]
                                currentIndex: Math.min(1, decolog.rig.tciTrx)
                                onActivated: decolog.rig.tciTrx = currentIndex
                            }
                        }
                        GlassButton {
                            Layout.alignment: Qt.AlignBottom
                            Layout.bottomMargin: 2
                            text: qsTr("Connect now")
                            tone: Theme.primaryColor
                            onClicked: decolog.rig.connectNow()
                        }
                    }
                    Note {
                        visible: decolog.rig.link === "tci"
                        text: qsTr("TCI is the protocol of Expert Electronics SDRs (SunSDR, ColibriNANO with "
                                   + "ExpertSDR) and of the programs that speak it: turn TCI on in the SDR "
                                   + "program (usually port 40001). DecoDXLog reads frequency and mode as soon as "
                                   + "they change, tunes the radio, uses its PTT and sends the CW macros with the "
                                   + "SDR's own keyer. Decodium can be connected at the same time: TCI accepts "
                                   + "more than one program.")
                    }

                    RowLayout {
                        spacing: 10
                        Pill {
                            text: decolog.rig.connected ? qsTr("radio connected") : qsTr("radio not connected")
                            tone: decolog.rig.connected ? Theme.accentColor : Theme.warningColor
                        }
                        Text {
                            Layout.fillWidth: true
                            text: decolog.rig.connected
                                  ? "%1 %2".arg(decolog.rig.frequencyLabel).arg(decolog.rig.mode)
                                  : decolog.rig.status
                            color: Theme.textSecondary
                            font.family: Theme.monoFamily
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }

                    SectionTitle { text: qsTr("CW keyer") }
                    RowLayout {
                        spacing: 10
                        Text {
                            text: qsTr("Speed")
                            color: Theme.textSecondary
                            font.pixelSize: 12
                        }
                        Slider {
                            Layout.preferredWidth: 260
                            from: 10
                            to: 45
                            stepSize: 1
                            value: decolog.rig.wpm
                            onMoved: decolog.rig.wpm = Math.round(value)
                        }
                        Text {
                            text: qsTr("%1 wpm").arg(decolog.rig.wpm)
                            color: Theme.textPrimary
                            font.family: Theme.monoFamily
                            font.pixelSize: 13
                            font.bold: true
                        }
                    }
                    Note {
                        text: qsTr("The eight macros are in the contest window (Ctrl+Shift+T) and in the CW "
                                   + "panel, on the F1-F8 keys, with Esc to stop.")
                    }

                    //  Il manipolatore su una porta tutta sua: e' la via per
                    //  chi tiene Decodium aperto, che il CAT ce l'ha gia' lui.
                    SectionTitle { text: qsTr("Keying on a serial port") }
                    RowLayout {
                        spacing: 12
                        LabeledField {
                            label: qsTr("Keyer port")
                            StyledComboBox {
                                id: keyerPortBox
                                Layout.preferredWidth: 160
                                editable: true
                                model: [qsTr("none")].concat(decolog.rig.serialPorts())
                                Component.onCompleted: editText = decolog.rig.keyerPort
                                onActivated: decolog.rig.keyerPort = currentIndex === 0 ? "" : currentText
                                onEditTextChanged: if (activeFocus) decolog.rig.keyerPort = editText
                                Connections {
                                    target: decolog.rig
                                    function onChanged() {
                                        if (!keyerPortBox.activeFocus)
                                            keyerPortBox.editText = decolog.rig.keyerPort
                                    }
                                }
                            }
                        }
                        LabeledField {
                            label: qsTr("Pin")
                            StyledComboBox {
                                Layout.preferredWidth: 110
                                readonly property var lines: ["DTR", "RTS"]
                                model: lines
                                currentIndex: Math.max(0, lines.indexOf(decolog.rig.keyerLine))
                                onActivated: decolog.rig.keyerLine = lines[currentIndex]
                            }
                        }
                        GlassButton {
                            Layout.alignment: Qt.AlignBottom
                            Layout.bottomMargin: 2
                            text: qsTr("Send VVV")
                            enabled: decolog.rig.keyerOn
                            onClicked: decolog.rig.testKeyer()
                        }
                        Pill {
                            Layout.alignment: Qt.AlignBottom
                            Layout.bottomMargin: 6
                            visible: decolog.rig.keyerPort.length > 0
                            text: decolog.rig.keyerOn ? qsTr("keyer ready") : qsTr("port not open")
                            tone: decolog.rig.keyerOn ? Theme.accentColor : Theme.errorColor
                        }
                    }
                    Note {
                        text: qsTr("With Decodium open the radio's CAT port is already taken, and a CAT "
                                   + "bridge cannot key. Here DecoDXLog keys by itself: it raises DTR or "
                                   + "RTS on a port of its own — the one wired to the keying circuit — so "
                                   + "Decodium keeps the CAT and the macros go on air anyway. Leave the "
                                   + "port on \"none\" to key through the CAT as before.")
                    }
                    Item { Layout.fillHeight: true }
                }

                // ── Rotore ──────────────────────────────────────────────────
                ColumnLayout {
                    spacing: 12
                    ToggleSwitch {
                        text: qsTr("Antenna rotor")
                        checked: decolog.rotor.enabled
                        onToggled: decolog.rotor.enabled = checked
                    }
                    RowLayout {
                        spacing: 12
                        LabeledField {
                            label: qsTr("Talks to")
                            StyledComboBox {
                                Layout.preferredWidth: 380
                                readonly property var ids: ["builtin", "decorotor", "rotctld"]
                                model: [qsTr("The control box, directly (built-in gateway)"),
                                        qsTr("DecoRotor (WebSocket)"), qsTr("rotctld (Hamlib) — any program")]
                                currentIndex: Math.max(0, ids.indexOf(decolog.rotor.backend))
                                onActivated: decolog.rotor.backend = ids[currentIndex]
                            }
                        }
                        LabeledField {
                            visible: decolog.rotor.backend !== "builtin"
                            label: qsTr("Host")
                            StyledTextField {
                                Layout.preferredWidth: 180
                                text: decolog.rotor.host
                                placeholderText: "127.0.0.1"
                                onEditingFinished: decolog.rotor.host = text
                            }
                        }
                        LabeledField {
                            visible: decolog.rotor.backend !== "builtin"
                            label: qsTr("Port")
                            StyledTextField {
                                Layout.preferredWidth: 100
                                text: decolog.rotor.port
                                onEditingFinished: decolog.rotor.port = parseInt(text) || 0
                            }
                        }
                        LabeledField {
                            label: qsTr("Beamwidth")
                            StyledComboBox {
                                Layout.preferredWidth: 120
                                readonly property var values: [20, 30, 45, 60, 90, 120]
                                model: values.map(v => v + "°")
                                currentIndex: Math.max(0, values.indexOf(decolog.rotor.beamwidth))
                                onActivated: decolog.rotor.beamwidth = values[currentIndex]
                            }
                        }
                    }
                    // Il gateway integrato: DecoDXLog apre la seriale del control
                    // box e fa da DecoRotor per l'app e per gli altri programmi.
                    RowLayout {
                        spacing: 12
                        visible: decolog.rotor.backend === "builtin"
                        LabeledField {
                            label: qsTr("Control box port")
                            StyledComboBox {
                                id: rotorPortBox
                                Layout.preferredWidth: 130
                                property var ports: []
                                model: ports
                                // "—" in cima: finche' non si sceglie, nessuna porta.
                                Component.onCompleted: {
                                    const list = decolog.rotor.serialPorts()
                                    const now = decolog.rotor.gatewaySerialPort
                                    if (now.length > 0 && list.indexOf(now) < 0)
                                        list.unshift(now)
                                    list.unshift("—")
                                    ports = list
                                    currentIndex = Math.max(0, list.indexOf(now))
                                }
                                onActivated: decolog.rotor.gatewaySerialPort = currentIndex > 0 ? ports[currentIndex] : ""
                            }
                        }
                        LabeledField {
                            label: qsTr("Control box")
                            StyledComboBox {
                                Layout.preferredWidth: 260
                                readonly property var models: decolog.rotor.gatewayModels()
                                model: models.map(m => m.label)
                                currentIndex: Math.max(0, models.findIndex(m => m.key === decolog.rotor.gatewayModel))
                                onActivated: decolog.rotor.gatewayModel = models[currentIndex].key
                            }
                        }
                        ToggleSwitch {
                            Layout.alignment: Qt.AlignBottom
                            text: qsTr("Simulated")
                            checked: decolog.rotor.gatewaySimulate
                            onToggled: decolog.rotor.gatewaySimulate = checked
                        }
                    }
                    RowLayout {
                        spacing: 12
                        visible: decolog.rotor.backend === "builtin"
                        LabeledField {
                            label: qsTr("App port (WebSocket)")
                            StyledTextField {
                                Layout.preferredWidth: 90
                                text: decolog.rotor.gatewayWsPort
                                onEditingFinished: decolog.rotor.gatewayWsPort = parseInt(text) || 8765
                            }
                        }
                        LabeledField {
                            label: qsTr("Web page port")
                            StyledTextField {
                                Layout.preferredWidth: 90
                                text: decolog.rotor.httpPort
                                onEditingFinished: { decolog.rotor.httpPort = parseInt(text) || 8080; decolog.rotor.reconnect() }
                            }
                        }
                        LabeledField {
                            label: qsTr("rotctld port")
                            StyledTextField {
                                Layout.preferredWidth: 90
                                text: decolog.rotor.gatewayRotctldPort
                                onEditingFinished: decolog.rotor.gatewayRotctldPort = parseInt(text) || 0
                            }
                        }
                        GlassButton {
                            Layout.alignment: Qt.AlignBottom
                            Layout.bottomMargin: 2
                            text: qsTr("Take them from DecoRotor")
                            tone: Theme.primaryColor
                            onClicked: {
                                decolog.rotor.importDecoRotor("")
                                const list = decolog.rotor.serialPorts()
                                const now = decolog.rotor.gatewaySerialPort
                                if (now.length > 0 && list.indexOf(now) < 0)
                                    list.unshift(now)
                                list.unshift("—")
                                rotorPortBox.ports = list
                                rotorPortBox.currentIndex = Math.max(0, list.indexOf(now))
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: decolog.rotor.backend === "builtin" && decolog.rotor.gatewayProblems.length > 0
                        wrapMode: Text.Wrap
                        text: qsTr("Ports in use by another program (is DecoRotor still running?): %1")
                              .arg(decolog.rotor.gatewayProblems.join(" · "))
                        color: Theme.warningColor
                        font.family: Theme.monoFamily
                        font.pixelSize: 11
                    }
                    Note {
                        visible: decolog.rotor.backend === "builtin"
                        text: qsTr("DecoDXLog itself opens the serial port of the PRO.SIS.TEL control box and does "
                                   + "what DecoRotor did: the phone app, the web page and the station programs "
                                   + "(rotctld: N1MM+, Log4OM, PstRotator…) connect to this computer on the same "
                                   + "ports as before. Close DecoRotor first: the serial port and the ports can "
                                   + "have only one owner. The stations on the app map come from Decodium and "
                                   + "the cluster, through DecoDXLog.")
                    }
                    ToggleSwitch {
                        text: qsTr("Follow the call Decodium is working")
                        checked: decolog.rotor.followDx
                        onToggled: decolog.rotor.followDx = checked
                    }
                    RowLayout {
                        spacing: 8
                        GlassButton {
                            text: qsTr("Connect again")
                            enabled: decolog.rotor.enabled
                            onClicked: decolog.rotor.reconnect()
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: decolog.rotor.status
                            color: decolog.rotor.connected ? Theme.accentColor : Theme.warningColor
                            font.family: Theme.monoFamily
                            font.pixelSize: 12
                        }
                    }
                    Note {
                        visible: decolog.rotor.backend !== "builtin"
                        text: qsTr("DecoRotor is the gateway of the family: it reads the Prosistel control box on the "
                                   + "serial port and publishes it on the network (WebSocket 8765). With rotctld any "
                                   + "other rotor program works too — DecoRotor itself answers on 4532. DecoDXLog never "
                                   + "touches the serial port: it only says where to point, and the control box keeps "
                                   + "its own limits.")
                    }
                    Note {
                        text: qsTr("Where a bearing is known — a cluster spot, the call being worked, a QSO with a "
                                   + "grid — the rotor menu points there. The panel is in the right column, with the "
                                   + "compass and the STOP.")
                    }
                    Item { Layout.fillHeight: true }
                }

                // ── Backup ──────────────────────────────────────────────────
                ColumnLayout {
                    spacing: 12
                    ToggleSwitch {
                        text: qsTr("Nightly backup")
                        checked: decolog.backupEnabled
                        onToggled: decolog.backupEnabled = checked
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 3
                        columnSpacing: 10
                        LabeledField {
                            Layout.columnSpan: 3
                            Layout.fillWidth: true
                            label: qsTr("Folder")
                            RowLayout {
                                Layout.fillWidth: true
                                StyledTextField { id: backupDirField; Layout.fillWidth: true }
                                GlassButton { text: qsTr("Browse…"); onClicked: folderDialog.open() }
                            }
                        }
                        LabeledField {
                            label: qsTr("Time (local)")
                            StyledTextField { id: backupTimeField; Layout.preferredWidth: 100; placeholderText: "02:00" }
                        }
                        LabeledField {
                            label: qsTr("Keep copies")
                            StyledTextField { id: keepField; Layout.preferredWidth: 100; validator: IntValidator { bottom: 1; top: 365 } }
                        }
                    }
                    RowLayout {
                        spacing: 8
                        Tile { label: qsTr("Last backup"); value: decolog.lastBackup.length ? decolog.lastBackup : qsTr("never") }
                        Tile { label: qsTr("File"); value: decolog.lastBackupInfo.length ? decolog.lastBackupInfo : "—" }
                    }
                    GlassButton {
                        text: qsTr("Back up now")
                        tone: Theme.primaryColor
                        filled: true
                        onClicked: { root.apply(); decolog.backupNow() }
                    }
                    Note { text: qsTr("A consistent copy made with SQLite VACUUM INTO, even while DecoDXLog is logging. If the PC is off at the chosen time, the copy is made as soon as DecoDXLog is open.") }
                    Item { Layout.fillHeight: true }
                }
            }
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.borderSoft }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            spacing: 10
            Text {
                text: decolog.lastBackup.length
                      ? qsTr("Backup nightly %1 → %2 · last %3").arg(decolog.backupTime).arg(decolog.backupDir).arg(decolog.lastBackup)
                      : qsTr("Backup nightly %1 → %2").arg(decolog.backupTime).arg(decolog.backupDir)
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                color: Theme.textSecondary
                font.family: Theme.monoFamily
                font.pixelSize: 11
            }
            GlassButton { text: qsTr("Cancel"); onClicked: root.reject() }
            GlassButton {
                text: qsTr("Apply")
                tone: Theme.accentColor
                filled: true
                onClicked: { root.apply(); root.accept() }
            }
        }
    }
}
