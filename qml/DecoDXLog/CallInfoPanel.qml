// DecoDXLog — a destra: cosa sa il log del nominativo che si sta lavorando.
//
// Nel mockup la testata dice "QRZ.com": il callbook arriva con le release 1.x.
// Fino ad allora nome, QTH e locatore vengono dai QSO gia' fatti, e la testata
// lo dichiara.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decodium.UI

GlassPanel {
    id: root

    signal openQso(var id)

    readonly property var info: decolog.callInfo
    readonly property bool hasCall: (info.call || "").length > 0
    readonly property bool worked: (info.count || 0) > 0

    title: qsTr("Call info")
    dotColor: Theme.secondaryColor
    headerTools: [
        Text {
            anchors.verticalCenter: parent.verticalCenter
            // Da dove vengono nome e QTH: il callbook scelto, o solo il log.
            text: decolog.callbookBusy ? (root.info.callbookSource || "") + " …"
                : root.info.callbook ? root.info.callbook.source
                : root.info.callbookSource ? root.info.callbookSource + " · " + qsTr("log") : qsTr("from log")
            color: Theme.textSecondary
            font.family: Theme.monoFamily
            font.pixelSize: 11
        }
    ]

    // L'ora locale dall'altra parte scorre: si ricalcola ogni minuto.
    property string localTime: ""
    function updateLocalTime() {
        if (root.info.utcOffsetHours === undefined) {
            localTime = "—"
            return
        }
        const d = new Date(Date.now() + root.info.utcOffsetHours * 3600000)
        localTime = ("0" + d.getUTCHours()).slice(-2) + ":" + ("0" + d.getUTCMinutes()).slice(-2)
    }
    onInfoChanged: updateLocalTime()
    Timer { interval: 30000; running: true; repeat: true; onTriggered: root.updateLocalTime() }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true
        ScrollBar.vertical: PanelScrollBar {}

        ColumnLayout {
            width: parent.width
            spacing: 10

            Text {
                visible: !root.hasCall
                Layout.fillWidth: true
                Layout.topMargin: 20
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: qsTr("Select a QSO or type a callsign.\nThe call Decodium is working shows up here by itself.")
                color: Theme.textSecondary
                font.pixelSize: 12
            }

            RowLayout {
                visible: root.hasCall
                Layout.fillWidth: true
                spacing: 10

            // La foto del callbook, quando c'e'.
            Rectangle {
                visible: root.info.callbook !== undefined && (root.info.callbook.imageUrl || "").length > 0
                Layout.preferredWidth: 56
                Layout.preferredHeight: 56
                Layout.alignment: Qt.AlignTop
                radius: 4
                color: Theme.bgMedium
                border.width: 1
                border.color: Theme.glassBorder
                clip: true
                Image {
                    id: photo
                    anchors.fill: parent
                    anchors.margins: 1
                    source: parent.visible ? root.info.callbook.imageUrl : ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    sourceSize.width: 112
                    sourceSize.height: 112
                }
                Text {
                    anchors.centerIn: parent
                    visible: photo.status !== Image.Ready
                    text: photo.status === Image.Loading ? "…" : qsTr("photo")
                    color: Theme.textSecondary
                    font.family: Theme.monoFamily
                    font.pixelSize: 10
                }

                // Col mouse sopra, la foto grande: 640x480, in una finestrella
                // sua che puo' uscire dal pannello. Sta DI FIANCO alla
                // miniatura, mai sotto il mouse: se il mouse ci finisce sopra,
                // la miniatura perde l'hover, la foto sparisce, l'hover torna e
                // la foto ricompare — era il lampeggio. E si chiude con un
                // attimo di ritardo, cosi' passare dalla miniatura alla foto
                // grande non la fa sparire.
                HoverHandler {
                    id: photoHover
                    cursorShape: Qt.PointingHandCursor
                    onHoveredChanged: {
                        if (hovered) {
                            bigPhotoHide.stop()
                            if (photo.status === Image.Ready && !bigPhoto.opened)
                                bigPhotoShow.start()
                        } else {
                            bigPhotoShow.stop()
                            bigPhotoHide.start()
                        }
                    }
                }
                Timer { id: bigPhotoShow; interval: 250; onTriggered: bigPhoto.open() }
                Timer {
                    id: bigPhotoHide
                    interval: 180
                    onTriggered: if (!photoHover.hovered && !bigPhotoHover.hovered) bigPhoto.close()
                }
                Popup {
                    id: bigPhoto
                    // A sinistra se la miniatura sta nella meta' destra dello
                    // schermo, a destra altrimenti: dove c'e' posto.
                    readonly property bool onLeft: {
                        const at = parent.mapToGlobal(0, 0)
                        const screen = parent.Window.window ? parent.Window.window.screen : null
                        const middle = screen ? screen.virtualX + screen.width / 2 : 960
                        return at.x > middle
                    }
                    x: onLeft ? -width - 10 : parent.width + 10
                    y: -40
                    padding: 4
                    popupType: Popup.Window
                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                    background: Rectangle { color: Theme.panelColor; border.color: Theme.glassBorder; radius: 6 }
                    contentItem: Item {
                        implicitWidth: 640
                        implicitHeight: 480
                        Image {
                            anchors.fill: parent
                            source: photo.source
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                            sourceSize.width: 640
                            sourceSize.height: 480
                        }
                        HoverHandler {
                            id: bigPhotoHover
                            onHoveredChanged: if (!hovered) bigPhotoHide.start()
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                RowLayout {
                    spacing: 8
                    Text {
                        text: root.info.call || ""
                        color: Theme.textPrimary
                        font.family: Theme.monoFamily
                        font.pixelSize: 20
                        font.bold: true
                        font.letterSpacing: 0.8
                    }
                    Pill {
                        text: root.worked ? qsTr("worked %1×").arg(root.info.count) : qsTr("new station")
                        tone: root.worked ? Theme.textSecondary : Theme.accentColor
                        pillHeight: 20
                        fontPixelSize: 10
                    }
                    // L'entita' conta piu' del nominativo: e' quella che fa l'award.
                    Pill {
                        readonly property bool hasEntity: root.info.entityDxcc !== undefined && !root.info.entityCounting
                        readonly property bool newDxcc: hasEntity && root.info.entityWorked === 0
                        readonly property bool newOnBand: hasEntity && !newDxcc && decolog.dialBand.length > 0
                                                          && (root.info.entityBands || []).indexOf(decolog.dialBand) < 0
                        visible: newDxcc || newOnBand
                        text: newDxcc ? qsTr("NEW DXCC") : qsTr("NEW DXCC on %1").arg(decolog.dialBand)
                        tone: Theme.warningColor
                        pillHeight: 20
                        fontPixelSize: 10
                    }
                }
                Text {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    elide: Text.ElideRight
                    text: [root.info.name, root.info.qth].filter(s => s).join(" · ")
                    color: Theme.textPrimary
                    font.pixelSize: 12
                }
                Text {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    elide: Text.ElideRight
                    text: {
                        const parts = []
                        if (root.info.gridsquare) parts.push(root.info.gridsquare)
                        // Lo stato: la provincia, lo stato USA, la prefettura.
                        if (root.info.state) parts.push(decolog.subdivisionName(root.info.state, root.info.entityDxcc || root.info.dxcc || 0))
                        if (root.info.country) parts.push(root.info.country)
                        if (root.info.entityDxcc) parts.push("DXCC " + root.info.entityDxcc)
                        if (root.info.cqz) parts.push("CQ " + root.info.cqz)
                        if (root.info.ituz) parts.push("ITU " + root.info.ituz)
                        return parts.join(" · ")
                    }
                    color: Theme.textSecondary
                    font.family: Theme.monoFamily
                    font.pixelSize: 11
                }
                Row {
                    visible: root.info.callbook !== undefined
                    spacing: 6
                    Pill { visible: !!(root.info.callbook && root.info.callbook.lotw); text: "LoTW user"; tone: Theme.accentColor; pillHeight: 18; fontPixelSize: 9 }
                    Pill { visible: !!(root.info.callbook && root.info.callbook.eqsl); text: "eQSL"; tone: Theme.secondaryColor; pillHeight: 18; fontPixelSize: 9 }
                    Pill {
                        visible: !!root.info.callbook && (root.info.callbook.qslVia || "").length > 0
                        text: "QSL " + (root.info.callbook ? root.info.callbook.qslVia : "")
                        tone: Theme.textSecondary
                        pillHeight: 18
                        fontPixelSize: 9
                    }
                }
                Text {
                    Layout.fillWidth: true
                    visible: (root.info.callbookError || "").length > 0
                    wrapMode: Text.Wrap
                    text: root.info.callbookError || ""
                    color: (root.info.callbookError || "").toLowerCase().indexOf("not found") >= 0
                           ? Theme.textSecondary : Theme.warningColor
                    font.family: Theme.monoFamily
                    font.pixelSize: 10
                }
            }
            }

            RowLayout {
                visible: root.hasCall
                Layout.fillWidth: true
                spacing: 6
                StatTile {
                    Layout.fillWidth: true
                    label: qsTr("Distance")
                    // Senza locatore la distanza e' dal centro dell'entita': si dice.
                    value: root.info.distanceKm !== undefined
                           ? (root.info.positionApprox ? "≈ " : "")
                             + root.info.distanceKm.toLocaleString(Qt.locale("en_US"), "f", 0) + " km" : "—"
                }
                StatTile {
                    Layout.fillWidth: true
                    label: qsTr("Azimuth")
                    value: root.info.azimuth !== undefined ? root.info.azimuth + "°" : "—"
                }
                StatTile {
                    Layout.fillWidth: true
                    label: qsTr("Local ≈")
                    value: root.localTime
                }
            }

            CallListMatches {
                Layout.fillWidth: true
                controller: decolog.callLists
                callsign: root.info.call || ""
            }

            // ── Worked before ───────────────────────────────────────────────
            ColumnLayout {
                visible: root.hasCall
                Layout.fillWidth: true
                spacing: 0
                RowLayout {
                    Layout.fillWidth: true
                    Layout.bottomMargin: 4
                    spacing: 8
                    SectionTitle { text: qsTr("Worked before") }
                    Pill {
                        text: String(root.info.count || 0)
                        tone: root.worked ? Theme.accentColor : Theme.textSecondary
                        pillHeight: 18
                        fontPixelSize: 10
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        readonly property bool entityNewOnFt2: (root.info.entityWorked || 0) > 0
                                                               && (root.info.entityModes || []).indexOf("FT2") < 0
                        visible: entityNewOnFt2 || (root.worked && !root.info.workedFt2)
                        text: entityNewOnFt2 ? qsTr("new DXCC on FT2") : qsTr("new on FT2")
                        color: Theme.warningColor
                        font.family: Theme.monoFamily
                        font.pixelSize: 11
                        font.bold: true
                    }
                    Text {
                        readonly property bool newBand: root.worked && decolog.dialBand.length > 0
                                                        && (root.info.bands || []).indexOf(decolog.dialBand) < 0
                        visible: newBand && root.info.workedFt2
                        text: qsTr("new on %1").arg(decolog.dialBand)
                        color: Theme.warningColor
                        font.family: Theme.monoFamily
                        font.pixelSize: 11
                        font.bold: true
                    }
                }
                Repeater {
                    model: root.info.recent || []
                    RowLayout {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        Layout.preferredHeight: 22
                        spacing: 8
                        Text { text: modelData.date; color: Theme.textSecondary; font.family: Theme.monoFamily; font.pixelSize: 11; Layout.preferredWidth: 78 }
                        Text { text: modelData.band; color: Theme.textSecondary; font.family: Theme.monoFamily; font.pixelSize: 11; Layout.preferredWidth: 40 }
                        Text { text: modelData.mode; color: Theme.textSecondary; font.family: Theme.monoFamily; font.pixelSize: 11; Layout.preferredWidth: 44 }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.lotw ? qsTr("LoTW ✓") : "—"
                            color: modelData.lotw ? Theme.accentColor : Theme.textSecondary
                            font.family: Theme.monoFamily
                            font.pixelSize: 11
                        }
                    }
                }
                Text {
                    visible: !root.worked
                    text: qsTr("Not in the log yet.")
                    color: Theme.textSecondary
                    font.pixelSize: 11
                }
            }

            // ── Griglia banda x modo ────────────────────────────────────────
            // Prima la stazione, poi la sua entita': la seconda e' quella che
            // conta per il DXCC, ma chi chiama vuole vedere subito la prima.
            ColumnLayout {
                visible: root.worked
                Layout.fillWidth: true
                spacing: 4
                SectionTitle { text: qsTr("Bands · modes") }
                BandModeGrid {
                    Layout.fillWidth: true
                    grid: root.info.slots || ({})
                }
            }

            ColumnLayout {
                visible: (root.info.entityWorked || 0) > 0
                Layout.fillWidth: true
                spacing: 4
                SectionTitle {
                    text: root.info.entity !== undefined
                          ? qsTr("%1 · bands · modes").arg(root.info.entity)
                          : qsTr("Entity · bands · modes")
                }
                BandModeGrid {
                    Layout.fillWidth: true
                    grid: root.info.entitySlots || ({})
                }
            }

            // ── QSL dell'ultimo QSO ─────────────────────────────────────────
            ColumnLayout {
                visible: root.worked
                Layout.fillWidth: true
                spacing: 4
                SectionTitle { text: qsTr("QSL status · last QSO") }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 5
                    columnSpacing: 4
                    uniformCellWidths: true
                    Repeater {
                        model: root.info.qsl || []
                        Rectangle {
                            required property var modelData
                            readonly property color tone: modelData.rcvd === "Y" ? Theme.accentColor
                                                        : modelData.sent !== "N" ? Theme.warningColor
                                                        : Theme.textSecondary
                            Layout.fillWidth: true
                            implicitHeight: 38
                            radius: 4
                            color: "transparent"
                            border.width: 1
                            border.color: modelData.rcvd === "Y" || modelData.sent !== "N" ? tone : Theme.borderSoft
                            Column {
                                anchors.centerIn: parent
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: modelData.label
                                    color: parent.parent.tone
                                    font.family: Theme.monoFamily
                                    font.pixelSize: 10
                                }
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    // ✓ inviato, ✓ confermato; ↑ in coda; · niente.
                                    text: (modelData.sent === "Y" ? "✓" : modelData.sent === "N" ? "·" : "↑")
                                          + (modelData.rcvd === "Y" ? "✓" : " ·")
                                    color: parent.parent.tone
                                    font.family: Theme.monoFamily
                                    font.pixelSize: 10
                                }
                            }
                        }
                    }
                }
                GlassButton {
                    Layout.alignment: Qt.AlignRight
                    text: qsTr("Open last QSO")
                    buttonHeight: 24
                    fontPixelSize: 11
                    onClicked: root.openQso(root.info.lastId)
                }
            }
        }
    }
}
