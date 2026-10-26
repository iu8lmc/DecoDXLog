// DecoDXLog — profili stazione (mockup 1d): elenco a sinistra, scheda a destra.
// I campi corrispondono uno a uno alla tabella station_profile.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decodium.UI

DialogFrame {
    id: root

    readonly property var profiles: decolog.stationProfiles
    property int selectedRow: -1
    property var draft: ({})
    property int formRev: 0
    property bool edited: false
    signal profileQsoRequested(var profileId)
    readonly property var awardConflicts: { root.formRev; return profiles.awardAccountConflicts() }
    readonly property var awardGroups: { root.formRev; return profiles.awardAccountGroups() }

    title: qsTr("Station profiles")
    dotColor: Theme.primaryColor
    info: qsTr("active: %1").arg(profiles.activeProfile.name || "—")
    dialogKey: "profiles"
    width: 800
    height: 700

    function select(row) {
        selectedRow = row
        draft = row >= 0 ? profiles.get(row) : { id: 0, name: "", stationCallsign: decolog.deCall, isDefault: profiles.count === 0 }
        edited = false
        errorText.text = ""
        formRev++
    }
    function value(key) { return draft[key] !== undefined && draft[key] !== 0 ? String(draft[key]) : "" }
    function setValue(key, v) { draft[key] = v; edited = true }

    function save() {
        const id = profiles.save(draft)
        if (id <= 0) {
            errorText.text = qsTr("A profile needs a name and a station callsign.")
            return
        }
        select(profiles.rowForId(id))
    }

    onOpened: select(profiles.count > 0 ? Math.max(0, profiles.rowForId(profiles.activeProfileId)) : -1)

    component ProfileInput: LabeledField {
        id: input
        property string key: ""
        property bool mono: true
        property bool upper: false
        property bool bold: false
        Layout.fillWidth: true
        StyledTextField {
            Layout.fillWidth: true
            mono: input.mono
            uppercase: input.upper
            font.bold: input.bold
            text: { root.formRev; return root.value(input.key) }
            onTextEdited: root.setValue(input.key, input.upper ? text.toUpperCase() : text)
        }
    }

    body: RowLayout {
        spacing: 0

        // ── Elenco ──────────────────────────────────────────────────────────
        ColumnLayout {
            Layout.preferredWidth: 250
            Layout.fillHeight: true
            spacing: 0

            ListView {
                id: list
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 380
                clip: true
                model: root.profiles
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    id: item
                    required property int index
                    required property string name
                    required property string subtitle
                    required property bool isDefault
                    required property bool deleted
                    required property bool dirty
                    required property int qsoCount
                    required property var profileId

                    readonly property bool selected: index === root.selectedRow
                    width: ListView.view.width
                    height: Theme.rowHeight + 22
                    color: selected ? Theme.rowMatchBg : mouse.containsMouse ? Theme.glassOverlay : "transparent"

                    Rectangle { visible: item.selected; width: 3; height: parent.height; color: Theme.accentColor }
                    Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: 1; color: Theme.borderSoft }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 10
                        spacing: 8
                        Column {
                            Layout.fillWidth: true
                            Text {
                                width: parent.width
                                elide: Text.ElideRight
                                text: item.name
                                color: item.deleted ? Theme.textSecondary : Theme.textPrimary
                                font.family: Theme.monoFamily
                                font.pixelSize: 13
                                font.bold: true
                                font.strikeout: item.deleted
                            }
                            Text {
                                width: parent.width
                                elide: Text.ElideRight
                                text: item.deleted ? qsTr("deleted · %1 QSO keep it").arg(item.qsoCount) : item.subtitle
                                color: Theme.textSecondary
                                font.family: Theme.monoFamily
                                font.pixelSize: 11
                            }
                        }
                        Text {
                            visible: item.isDefault
                            text: qsTr("DEFAULT")
                            color: Theme.accentColor
                            font.family: Theme.monoFamily
                            font.pixelSize: 10
                            font.bold: true
                        }
                        Text {
                            visible: !item.isDefault && item.profileId === root.profiles.activeProfileId
                            text: qsTr("ACTIVE")
                            color: Theme.primaryColor
                            font.family: Theme.monoFamily
                            font.pixelSize: 10
                            font.bold: true
                        }
                    }
                    MouseArea {
                        id: mouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: root.select(item.index)
                        onDoubleClicked: if (!item.deleted) root.profiles.activeProfileId = item.profileId
                    }
                }

                Text {
                    anchors.centerIn: parent
                    width: parent.width - 30
                    visible: root.profiles.count === 0
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("No profiles yet. Create one, or start Decodium: DecoDXLog creates the first from its callsign and grid.")
                    color: Theme.textSecondary
                    font.pixelSize: 11
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 10
                spacing: 6
                GlassButton {
                    Layout.fillWidth: true
                    text: qsTr("+ New")
                    tone: Theme.primaryColor
                    filled: true
                    onClicked: root.select(-1)
                }
                GlassButton {
                    text: qsTr("Duplicate")
                    enabled: root.selectedRow >= 0
                    onClicked: {
                        const copy = Object.assign({}, root.draft)
                        copy.id = 0
                        copy.name = copy.name + " " + qsTr("(copy)")
                        copy.isDefault = false
                        root.selectedRow = -1
                        root.draft = copy
                        root.edited = true
                        root.formRev++
                    }
                }
            }
        }

        Rectangle { Layout.fillHeight: true; implicitWidth: 1; color: Theme.borderSoft }

        // ── Scheda ──────────────────────────────────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 14
            spacing: 10

            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Each profile is a separate logical log. Incoming UDP, N1MM, API and network QSOs are routed by STATION_CALLSIGN, then by a unique OPERATOR. Open a dedicated QSO window for each family member to enter manually without changing the global active profile.")
                color: Theme.textSecondary
                font.pixelSize: 11
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                ProfileInput { key: "name"; label: qsTr("Profile name"); mono: false }
                ToggleSwitch {
                    Layout.alignment: Qt.AlignBottom
                    Layout.bottomMargin: 4
                    text: qsTr("Default profile for new QSO")
                    checked: { root.formRev; return root.draft.isDefault === true }
                    onToggled: root.setValue("isDefault", checked)
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 10
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    ProfileInput {
                        Layout.preferredWidth: 1
                        Layout.horizontalStretchFactor: 1; key: "stationCallsign"; label: qsTr("Station callsign"); upper: true; bold: true }
                    ProfileInput {
                        Layout.preferredWidth: 1
                        Layout.horizontalStretchFactor: 1; key: "operatorCall"; label: qsTr("Operator"); upper: true }
                    ProfileInput {
                        Layout.preferredWidth: 1
                        Layout.horizontalStretchFactor: 1; key: "myGridsquare"; label: qsTr("My gridsquare") }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    ProfileInput {
                        Layout.preferredWidth: 1
                        Layout.horizontalStretchFactor: 1; key: "myCqZone"; label: qsTr("CQ zone") }
                    ProfileInput {
                        Layout.preferredWidth: 1
                        Layout.horizontalStretchFactor: 1; key: "myItuZone"; label: qsTr("ITU zone") }
                    ProfileInput {
                        Layout.preferredWidth: 1
                        Layout.horizontalStretchFactor: 1; key: "myDxcc"; label: qsTr("My DXCC") }
                }
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 3
                columnSpacing: 10
                ProfileInput { key: "myRig"; label: qsTr("Rig"); mono: false }
                ProfileInput { key: "myAntenna"; label: qsTr("Antenna"); mono: false }
                ProfileInput { key: "defaultTxPwr"; label: qsTr("Def. pwr W"); Layout.preferredWidth: 90; Layout.fillWidth: false }
            }
            ProfileInput { key: "lotwStationLocation"; label: qsTr("LoTW station location (TQSL)") }
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 10
                ProfileInput { key: "awardAccount"; label: qsTr("Award account") }
                ProfileInput { key: "operatingLocation"; label: qsTr("Historical operating location") }
                ProfileInput { key: "validFrom"; label: qsTr("Valid from (YYYY-MM-DD)") }
                ProfileInput { key: "validTo"; label: qsTr("Valid to (YYYY-MM-DD)") }
            }

            Rectangle {
                Layout.fillWidth: true
                visible: root.awardConflicts.length > 0
                implicitHeight: conflictText.implicitHeight + 16
                radius: 5
                color: Qt.rgba(Theme.warningColor.r, Theme.warningColor.g, Theme.warningColor.b, 0.12)
                border.width: 1
                border.color: Theme.warningColor
                Text {
                    id: conflictText
                    anchors.fill: parent
                    anchors.margins: 8
                    wrapMode: Text.Wrap
                    text: qsTr("Award account requires review: %1")
                          .arg(root.awardConflicts.map(function(c) { return c.account + " — " + c.reason }).join(" · "))
                    color: Theme.warningColor
                    font.pixelSize: 11
                }
            }
            Text {
                Layout.fillWidth: true
                visible: root.awardGroups.length > 0
                text: qsTr("Award account groups: %1").arg(root.awardGroups.map(function(g) {
                    return g.account + " (" + g.profiles.length + ")" + (g.compatible ? "" : " · review")
                }).join(" · "))
                color: Theme.textSecondary
                wrapMode: Text.Wrap
                font.pixelSize: 11
            }

            // Un nominativo speciale ha il suo logbook QRZ e la sua casella eQSL:
            // l'invio e le conferme dei suoi QSO passano da li'.
            Text {
                Layout.topMargin: 4
                text: qsTr("QSL accounts of this profile")
                color: Theme.textSecondary
                font.pixelSize: 11
                font.bold: true
            }
            CredentialsList {
                Layout.fillWidth: true
                visible: root.draft.id > 0
                customRows: { root.formRev; decolog.credentials.services; return decolog.credentials.profileServices(root.draft.id || 0) }
            }
            Text {
                Layout.fillWidth: true
                visible: !(root.draft.id > 0)
                wrapMode: Text.Wrap
                text: qsTr("Save the profile first: then it can have its own QRZ Logbook and eQSL accounts.")
                color: Theme.textSecondary
                font.pixelSize: 11
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: usage.implicitHeight + 16
                radius: 4
                color: Theme.bgMedium
                border.width: 1
                border.color: Theme.borderSoft
                Text {
                    id: usage
                    anchors.fill: parent
                    anchors.margins: 8
                    textFormat: Text.StyledText
                    text: root.draft.id > 0
                          ? qsTr("Used by <b><font color=\"%1\">%2</font></b> QSO · uuid %3 · rev %4 · %5")
                                .arg(Theme.textPrimary).arg(root.draft.qsoCount)
                                .arg(String(root.draft.uuid).substring(0, 4) + "…" + String(root.draft.uuid).slice(-2))
                                .arg(root.draft.revision).arg(root.draft.dirty ? qsTr("not synced") : qsTr("synced"))
                          : qsTr("New profile · not saved yet")
                    color: Theme.textSecondary
                    font.family: Theme.monoFamily
                    font.pixelSize: 11
                }
            }

            Text {
                id: errorText
                visible: text.length > 0
                color: Theme.errorColor
                font.pixelSize: 12
            }

            Item { Layout.fillHeight: true }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.borderSoft }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                GlassButton {
                    text: qsTr("Delete")
                    tone: Theme.errorColor
                    enabled: root.draft.id > 0 && !root.draft.deleted
                    onClicked: {
                        root.profiles.remove(root.draft.id)
                        root.select(Math.min(root.selectedRow, root.profiles.count - 1))
                    }
                }
                GlassButton {
                    text: qsTr("Use now")
                    enabled: root.draft.id > 0 && !root.draft.deleted && root.draft.id !== root.profiles.activeProfileId
                    onClicked: root.profiles.activeProfileId = root.draft.id
                }
                GlassButton {
                    text: qsTr("Open QSO for this profile")
                    enabled: root.draft.id > 0 && !root.draft.deleted
                    onClicked: root.profileQsoRequested(root.draft.id)
                }
                Item { Layout.fillWidth: true }
                GlassButton { text: qsTr("Cancel"); onClicked: root.edited ? root.select(root.selectedRow) : root.reject() }
                GlassButton {
                    text: qsTr("Save profile")
                    tone: Theme.accentColor
                    filled: true
                    enabled: root.edited
                    onClicked: root.save()
                }
            }
        }
    }
}
