import QtQuick
import QtQuick.Controls
import QtTest
import "../../qml/DecoDXLog"

TestCase {
    id: testCase
    name: "ClubLogCredentials"
    when: windowShown
    width: 800
    height: 600
    property var decolog: fakeDecolog
    QtObject {
        id: fakeDecolog
        property QtObject stationProfiles: QtObject { property var activeProfile: ({stationCallsign: "K1TEST"}) }
        property QtObject credentials: QtObject {
            property bool available: true
            property string backend: "fixture"
            property var services: []
            property string savedAccount: "unused"
            property string savedSecret: ""
            signal finished(string service, bool ok, string text)
            function save(service, account, secret) { savedAccount = account; savedSecret = secret }
            function remove(service) {}
            function verify(service) {}
        }
    }
    CredentialsList { id: list; anchors.fill: parent }

    function test_tokenNeedsNoEmailAndShowsStation() {
        const editor = findChild(list, "credentialsEditor")
        verify(editor !== null)
        editor.edit({id: "clublog", label: "Club Log", account: "", stored: false,
                     accountLabel: "Email (legacy)", secretLabel: "Token", hint: ""})
        const account = findChild(editor, "credentialAccount")
        const secret = findChild(editor, "credentialSecret")
        const save = findChild(editor, "credentialSave")
        const help = findChild(editor, "clubLogStationHelp")
        verify(account !== null && secret !== null && save !== null && help !== null)
        compare(account.text, "")
        verify(!save.enabled)
        secret.text = "cl_synthetic_fixture"
        tryVerify(() => save.enabled)
        verify(help.text.includes("K1TEST"))
        save.clicked()
        compare(fakeDecolog.credentials.savedAccount, "")
        compare(fakeDecolog.credentials.savedSecret, "cl_synthetic_fixture")
        editor.close()
    }
}
