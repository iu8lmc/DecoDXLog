// Un popup in una finestra sua (popupType: Popup.Window) riceve i tasti solo se ha
// il fuoco: senza «focus: true» il campo di testo prende il cursore ma quello che
// si scrive si perde. E' successo con la data dello scarico da LoTW, che non si
// poteva scrivere. Qui si guardano i sorgenti QML: ogni popup di questo tipo che
// contiene un campo da scrivere deve dichiarare il fuoco.
#include <QDir>
#include <QFile>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QTest>

namespace {

QString readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll());
}

// Cio' che fa scrivere: i campi di testo e i pezzi che li contengono.
const QRegularExpression kTextInputs(
    QStringLiteral(R"(\b(StyledTextField|TextField|TextArea|LotwRangeRow|DatePeriodFields|NumberField)\b)"));

struct Block {
    QString head;   // la riga che apre il blocco, per dire dov'e'
    QString body;   // tutto cio' che sta fra le graffe
};

// I blocchi `{ … }` che contengono `popupType: Popup.Window` come proprieta'
// diretta (non in un blocco annidato): sono i popup. Le graffe nelle stringhe
// non ci sono, nei QML del programma.
QList<Block> windowPopups(QString text)
{
    // Via i commenti: parlano di «focus: true» e di graffe senza esserlo. Il «//» di
    // un indirizzo (https://…) non e' un commento.
    static const QRegularExpression comment(QStringLiteral(R"((?<!:)//[^\n]*)"));
    text.remove(comment);
    QList<Block> found;
    struct Open { qsizetype at; bool direct; };
    QList<Open> stack;
    const QString marker = QStringLiteral("popupType: Popup.Window");
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('{')) {
            stack.append({i, false});
        } else if (c == QLatin1Char('}') && !stack.isEmpty()) {
            const Open open = stack.takeLast();
            if (open.direct) {
                const qsizetype lineStart = text.lastIndexOf(QLatin1Char('\n'), open.at) + 1;
                found.append({text.mid(lineStart, open.at - lineStart + 1).trimmed(),
                              text.mid(open.at + 1, i - open.at - 1)});
            }
        } else if (c == QLatin1Char('p') && !stack.isEmpty() && text.mid(i, marker.size()) == marker) {
            stack.last().direct = true;
        }
    }
    return found;
}

// «focus: true» dichiarato dal popup stesso, non da un campo dentro.
bool declaresFocus(const QString& body)
{
    int depth = 0;
    const QString wanted = QStringLiteral("focus: true");
    for (qsizetype i = 0; i < body.size(); ++i) {
        const QChar c = body.at(i);
        if (c == QLatin1Char('{'))
            ++depth;
        else if (c == QLatin1Char('}'))
            --depth;
        else if (depth == 0 && c == QLatin1Char('f') && body.mid(i, wanted.size()) == wanted
                 && (i == 0 || !body.at(i - 1).isLetterOrNumber()))
            return true;
    }
    return false;
}

QStringList qmlStringListProperty(const QString& text, const QString& name)
{
    const QRegularExpression property(
        QStringLiteral(R"((?:readonly\s+)?property\s+var\s+%1\s*:\s*\[([^\]]*)\])")
            .arg(QRegularExpression::escape(name)),
        QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch match = property.match(text);
    if (!match.hasMatch())
        return {};

    QStringList values;
    const QRegularExpression quoted(QStringLiteral("\\\"([^\\\"]+)\\\""));
    auto it = quoted.globalMatch(match.captured(1));
    while (it.hasNext())
        values.append(it.next().captured(1));
    return values;
}

} // namespace

class TestQmlPopups : public QObject {
    Q_OBJECT

private slots:
    void windowPopupsWithTextFieldsHaveFocus()
    {
        const QDir dir(QStringLiteral(QML_DIR));
        QVERIFY2(dir.exists(), "la cartella dei QML non c'e'");
        const QStringList files = dir.entryList({QStringLiteral("*.qml")}, QDir::Files);
        QVERIFY(files.size() > 50);
        int popups = 0;
        QStringList missing;
        for (const QString& name : files) {
            QFile file(dir.filePath(name));
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QString text = QString::fromUtf8(file.readAll());
            for (const Block& popup : windowPopups(text)) {
                ++popups;
                if (kTextInputs.match(popup.body).hasMatch() && !declaresFocus(popup.body))
                    missing << name + QStringLiteral(": ") + popup.head;
            }
        }
        QVERIFY2(popups >= 5, "non ha trovato i popup: il controllo non guarda piu' niente");
        QVERIFY2(missing.isEmpty(),
                 qPrintable(QStringLiteral("popup in una finestra con un campo di testo e senza «focus: true»:\n")
                            + missing.join(QLatin1Char('\n'))));
    }

    // Il controllo stesso: un popup senza fuoco viene visto, uno col fuoco no.
    void theCheckSeesTheDifference()
    {
        const QString without = QStringLiteral("Item { Popup { popupType: Popup.Window\n modal: true\n"
                                               " contentItem: Item { StyledTextField { focus: true } } } }");
        const QString with = QStringLiteral("Item { Popup { popupType: Popup.Window\n focus: true\n"
                                            " contentItem: Item { StyledTextField { } } } }");
        const QList<Block> a = windowPopups(without);
        QCOMPARE(a.size(), 1);
        QVERIFY(kTextInputs.match(a.first().body).hasMatch());
        QVERIFY(!declaresFocus(a.first().body));   // il focus del campo non e' quello del popup
        const QList<Block> b = windowPopups(with);
        QCOMPARE(b.size(), 1);
        QVERIFY(declaresFocus(b.first().body));
    }

    void mainKeyboardEntryPointsRemainAvailable()
    {
        const QDir dir(QStringLiteral(QML_DIR));
        QFile main(dir.filePath(QStringLiteral("Main.qml")));
        QVERIFY(main.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(main.readAll());
        for (const QString& shortcut : {QStringLiteral("Ctrl+N"), QStringLiteral("Ctrl+F"),
                                        QStringLiteral("Ctrl+I"), QStringLiteral("Ctrl+E"),
                                        QStringLiteral("Ctrl+K"), QStringLiteral("Ctrl+T"),
                                        QStringLiteral("Ctrl+Shift+T"), QStringLiteral("Ctrl+R")})
            QVERIFY2(text.contains(QStringLiteral("sequence: \"") + shortcut + QStringLiteral("\"")),
                     qPrintable(QStringLiteral("shortcut mancante: ") + shortcut));

        QFile qso(dir.filePath(QStringLiteral("NewQsoDialog.qml")));
        QVERIFY(qso.open(QIODevice::ReadOnly));
        const QString qsoText = QString::fromUtf8(qso.readAll());
        QVERIFY(qsoText.contains(QStringLiteral("Ctrl+Return")));
        QVERIFY(qsoText.contains(QStringLiteral("Ctrl+Enter")));
    }

    void operationalPresetsRemainExposed()
    {
        const QDir dir(QStringLiteral(QML_DIR));
        QFile main(dir.filePath(QStringLiteral("Main.qml")));
        QVERIFY(main.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(main.readAll());
        for (const QString& preset : {QStringLiteral("Beginner"), QStringLiteral("DX"),
                                      QStringLiteral("Digital"), QStringLiteral("Contest"),
                                      QStringLiteral("Portable")})
            QVERIFY2(text.contains(QStringLiteral("qsTr(\"") + preset + QStringLiteral("\")")),
                     qPrintable(QStringLiteral("preset mancante: ") + preset));
        QVERIFY(text.contains(QStringLiteral("function applyOperationalPreset")));
        QVERIFY(text.contains(QStringLiteral("layout.detachedPanels = \"\"")));
        QVERIFY(text.contains(QStringLiteral("mainBoard.resetLayout()")));
    }

    void detachedPanelsAreIndependentAndPersistent()
    {
        const QDir qmlDir(QStringLiteral(QML_DIR));
        const QString panel = readFile(qmlDir.filePath(QStringLiteral("PanelWindow.qml")));
        QVERIFY(panel.contains(QStringLiteral("transientParent: null")));
        QVERIFY(panel.contains(QStringLiteral("modality: Qt.NonModal")));
        QVERIFY(panel.contains(QStringLiteral("category: \"layout/panel/\" + root.panelKey")));
        for (const QString& alias : {QStringLiteral("windowWidth: root.width"),
                                     QStringLiteral("windowHeight: root.height"),
                                     QStringLiteral("windowX: root.x"),
                                     QStringLiteral("windowY: root.y")})
            QVERIFY2(panel.contains(QStringLiteral("property alias ") + alias), qPrintable(alias));
        QVERIFY(panel.contains(QStringLiteral("function persistPlacement()")));
        QVERIFY(panel.contains(QStringLiteral("panelSettings.sync()")));

        const QString contest = readFile(qmlDir.filePath(QStringLiteral("ContestLayout.qml")));
        QVERIFY(contest.contains(QStringLiteral("function persistFloatingWindows()")));
        QVERIFY(contest.contains(QStringLiteral("transientParent: null")));
        QVERIFY(!contest.contains(QStringLiteral("transientParent: root.Window.window")));
        QVERIFY(contest.contains(QStringLiteral("property bool quitting: false")));
        QVERIFY(contest.contains(QStringLiteral("if (!root.quitting)")));

        const QString main = readFile(qmlDir.filePath(QStringLiteral("Main.qml")));
        QVERIFY(main.contains(QStringLiteral("function persistDetachedWindowState()")));
        QVERIFY(main.contains(QStringLiteral("window.persistDetachedWindowState()")));
        QVERIFY(main.contains(QStringLiteral("layout.sync()")));
        QVERIFY(main.contains(QStringLiteral("property string detachedPanels: \"\"")));
        QVERIFY(main.contains(QStringLiteral("property alias clusterWindowOpen: clusterWindow.active")));
        QVERIFY(main.contains(QStringLiteral("quitting: window.quitting")));
        QVERIFY(main.contains(QStringLiteral("clusterWindow.item.persistPlacement()")));
        QVERIFY(main.contains(QStringLiteral("if (window.quitting || window.panelStateTransition || window.syncingDetachedWindows)")));

        // Anche le finestre operative aperte dalla dashboard devono essere
        // top-level vere, altrimenti su alcuni desktop restano sul suo monitor.
        const QStringList independent = {
            QStringLiteral("ChatWindow.qml"), QStringLiteral("ClusterWindow.qml"),
            QStringLiteral("ContestWindow.qml"), QStringLiteral("LogbookWindow.qml"),
            QStringLiteral("QslCardsWindow.qml"), QStringLiteral("RotorWindow.qml"),
            QStringLiteral("StatsWindow.qml")};
        for (const QString& name : independent) {
            const QString source = readFile(qmlDir.filePath(name));
            QVERIFY2(source.contains(QStringLiteral("transientParent: null")), qPrintable(name));
            QVERIFY2(source.contains(QStringLiteral("OnScreen { target: root }")), qPrintable(name));
        }
    }

    void everyPanelHasARealDetachableWindowRoute()
    {
        const QDir qmlDir(QStringLiteral(QML_DIR));
        const QString main = readFile(qmlDir.filePath(QStringLiteral("Main.qml")));
        const QString contest = readFile(qmlDir.filePath(QStringLiteral("ContestLayout.qml")));

        const QMap<QString, QString> panels = {
            {QStringLiteral("newqso"), QStringLiteral("NewQsoPanel.qml")},
            {QStringLiteral("logbook"), QStringLiteral("LogbookPanel.qml")},
            {QStringLiteral("callinfo"), QStringLiteral("CallInfoPanel.qml")},
            {QStringLiteral("cw"), QStringLiteral("CwPanel.qml")},
            {QStringLiteral("rotor"), QStringLiteral("RotorPanel.qml")},
            {QStringLiteral("ft2"), QStringLiteral("Ft2AwardPanel.qml")},
            {QStringLiteral("tabs"), QStringLiteral("BottomTabs.qml")},
            {QStringLiteral("map"), QStringLiteral("MapPanel.qml")},
            {QStringLiteral("bandmap"), QStringLiteral("BandMapPanel.qml")},
            {QStringLiteral("decodium"), QStringLiteral("DecodiumPanel.qml")},
            {QStringLiteral("decfull"), QStringLiteral("DecodiumWindowPanel.qml")},
            {QStringLiteral("decsig"), QStringLiteral("DecodiumWindowPanel.qml")},
            {QStringLiteral("contest"), QStringLiteral("ContestEntryPanel.qml")},
            {QStringLiteral("score"), QStringLiteral("ContestScorePanel.qml")},
            {QStringLiteral("rate"), QStringLiteral("ContestRatePanel.qml")},
            {QStringLiteral("cluster"), QStringLiteral("ClusterPanel.qml")},
            {QStringLiteral("net"), QStringLiteral("ContestNetPanel.qml")},
            {QStringLiteral("dvk"), QStringLiteral("DvkPanel.qml")},
            {QStringLiteral("mults"), QStringLiteral("ContestMultipliersPanel.qml")}
        };
        QCOMPARE(panels.size(), 19);

        QStringList registered = qmlStringListProperty(main, QStringLiteral("panelKeys"));
        QStringList board = qmlStringListProperty(main, QStringLiteral("boardKeys"));
        QStringList contestKeys = qmlStringListProperty(contest, QStringLiteral("allKeys"));
        QStringList expected = panels.keys();
        registered.sort();
        board.sort();
        contestKeys.sort();
        expected.sort();
        QCOMPARE(registered, expected);

        QSet<QString> routed(board.cbegin(), board.cend());
        routed.unite(QSet<QString>(contestKeys.cbegin(), contestKeys.cend()));
        QStringList routedList(routed.cbegin(), routed.cend());
        routedList.sort();
        QCOMPARE(routedList, expected);

        for (auto it = panels.cbegin(); it != panels.cend(); ++it) {
            const QString key = it.key();
            const QString source = it.value();
            const QRegularExpression mainRoute(
                QStringLiteral(R"(case\s+"%1"\s*:\s*return\s+"%2")")
                    .arg(QRegularExpression::escape(key), QRegularExpression::escape(source)));
            QVERIFY2(mainRoute.match(main).hasMatch(), qPrintable(QStringLiteral("factory mancante: ") + key));
            QVERIFY2(qmlDir.exists(source), qPrintable(QStringLiteral("sorgente mancante: ") + source));

            if (contestKeys.contains(key)) {
                const QRegularExpression contestRoute(
                    QStringLiteral(R"(case\s+"%1"\s*:\s*return\s+"%2")")
                        .arg(QRegularExpression::escape(key), QRegularExpression::escape(source)));
                QVERIFY2(contestRoute.match(contest).hasMatch(),
                         qPrintable(QStringLiteral("factory contest mancante: ") + key));
            }
        }

        // Entrambi i percorsi generici usano la stessa finestra top-level;
        // Rotore e Cluster sono le due eccezioni con una finestra dedicata.
        QVERIFY(main.contains(QStringLiteral("delegate: PanelWindow")));
        QVERIFY(contest.contains(QStringLiteral("delegate: PanelWindow")));
        QVERIFY(main.contains(QStringLiteral("if (key === \"rotor\")")));
        QVERIFY(main.contains(QStringLiteral("if (key === \"cluster\")")));

        const QString clusterWindow = readFile(qmlDir.filePath(QStringLiteral("ClusterWindow.qml")));
        QVERIFY(clusterWindow.contains(QStringLiteral("transientParent: null")));
        QVERIFY(clusterWindow.contains(QStringLiteral("modality: Qt.NonModal")));
        QVERIFY(clusterWindow.contains(QStringLiteral("function persistPlacement()")));
        QVERIFY(clusterWindow.contains(QStringLiteral("clusterWindowSettings.sync()")));

        const QString rotorWindow = readFile(qmlDir.filePath(QStringLiteral("RotorWindow.qml")));
        QVERIFY(rotorWindow.contains(QStringLiteral("transientParent: null")));
        QVERIFY(rotorWindow.contains(QStringLiteral("modality: Qt.NonModal")));
        QVERIFY(rotorWindow.contains(QStringLiteral("function persistPlacement()")));
    }

    void firstRunGuideExplainsRadioAndFirstQso()
    {
        const QDir dir(QStringLiteral(QML_DIR));
        QFile guide(dir.filePath(QStringLiteral("FirstRunGuide.qml")));
        QVERIFY(guide.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(guide.readAll());
        QVERIFY(text.contains(QStringLiteral("Station profile")));
        QVERIFY(text.contains(QStringLiteral("Radio (CAT)")));
        QVERIFY(text.contains(QStringLiteral("First QSO")));
        QVERIFY(text.contains(QStringLiteral("Ctrl+Enter")));
        QVERIFY(text.contains(QStringLiteral("signal setupRequested")));
        QVERIFY(text.contains(QStringLiteral("signal newQsoRequested")));
        QVERIFY(text.contains(QStringLiteral("activeFocusOnTab: true")));
        QVERIFY(text.contains(QStringLiteral("KeyNavigation.tab: newQsoButton")));
        QVERIFY(text.contains(QStringLiteral("KeyNavigation.tab: closeButton")));
        QVERIFY(text.contains(QStringLiteral("Accessible.description")));

        const QString main = readFile(dir.filePath(QStringLiteral("Main.qml")));
        QVERIFY(main.contains(QStringLiteral("firstRunGuideSeen")));
        QVERIFY(main.contains(QStringLiteral("decolog.qsoCount === 0")));
        QVERIFY(main.contains(QStringLiteral("FirstRunGuide")));
    }

    void unverifiedAwardResultsAreLabelledAsStatistics()
    {
        const QDir dir(QStringLiteral(QML_DIR));
        QFile awards(dir.filePath(QStringLiteral("AwardsDialog.qml")));
        QVERIFY(awards.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(awards.readAll());
        QVERIFY(text.contains(QStringLiteral("root.current.officialRules === false")));
        QVERIFY(text.contains(QStringLiteral("statistics · rules not verified")));
    }

    void importPreviewRequiresBeforeAfterCounts()
    {
        const QDir qmlDir(QStringLiteral(QML_DIR));
        const QString source = readFile(qmlDir.filePath(QStringLiteral("Main.qml")));
        QVERIFY(source.contains(QStringLiteral("estimatedInserted")));
        QVERIFY(source.contains(QStringLiteral("estimatedDuplicates")));
        QVERIFY(source.contains(QStringLiteral("estimatedInvalid")));
        QVERIFY(source.contains(QStringLiteral("Estimated result")));
    }

    void qslManagerSuggestionShowsApprovalAndPotentialCredits()
    {
        const QDir qmlDir(QStringLiteral(QML_DIR));
        const QString source = readFile(qmlDir.filePath(QStringLiteral("../../src/app/DecoLogController.cpp")));
        QVERIFY(source.contains(QStringLiteral("potentialCredits")));
        QVERIFY(source.contains(QStringLiteral("requiresApproval")));
        QVERIFY(source.contains(QStringLiteral("automaticSend")));
        QVERIFY(source.contains(QStringLiteral("verify the route before sending")));
    }

    void integrationDiagnosticsRequiresExplicitReportApproval()
    {
        const QDir qmlDir(QStringLiteral(QML_DIR));
        const QString source = readFile(qmlDir.filePath(QStringLiteral("BottomTabs.qml")));
        QVERIFY(source.contains(QStringLiteral("integrationDiagnostics")));
        QVERIFY(source.contains(QStringLiteral("diagnosticSupportText")));
        QVERIFY(source.contains(QStringLiteral("Approve and save report")));
        QVERIFY(source.contains(QStringLiteral("onAccepted")));
        QVERIFY(source.contains(QStringLiteral("saveDiagnosticReport")));
        QVERIFY(source.contains(QStringLiteral("saveDiagnosticJsonReport")));
        QVERIFY(source.contains(QStringLiteral("Save JSON evidence")));
        QVERIFY(source.contains(QStringLiteral("The checks stay on this computer")));
        QVERIFY(source.contains(QStringLiteral("recordUiLatency")));
        const QString profiles = readFile(qmlDir.filePath(QStringLiteral("StationProfilesDialog.qml")));
        QVERIFY(profiles.contains(QStringLiteral("awardAccountConflicts")));
        QVERIFY(profiles.contains(QStringLiteral("awardAccountGroups")));
        QVERIFY(profiles.contains(QStringLiteral("Award account groups")));
        QVERIFY(profiles.contains(QStringLiteral("Award account requires review")));
        const QString awards = readFile(qmlDir.filePath(QStringLiteral("AwardsDialog.qml")));
        QVERIFY(awards.contains(QStringLiteral("reconcileAwardResult")));
        QVERIFY(awards.contains(QStringLiteral("Paste verified external result JSON")));
        const QString cluster = readFile(qmlDir.filePath(QStringLiteral("ClusterPanel.qml")));
        QVERIFY(cluster.contains(QStringLiteral("availableBands")));
        QVERIFY(cluster.contains(QStringLiteral("availableModes")));
        QVERIFY(cluster.contains(QStringLiteral("Available bands")));
        QVERIFY(cluster.contains(QStringLiteral("DX Assistant available bands")));
        QVERIFY(cluster.contains(QStringLiteral("DX Assistant available modes")));
        QVERIFY(cluster.contains(QStringLiteral("DX Assistant available radios")));
        QVERIFY(cluster.contains(QStringLiteral("DX Assistant available antennas")));
    }

    void clusterNodePasswordStaysInTheSystemKeystore()
    {
        const QDir qmlDir(QStringLiteral(QML_DIR));
        const QString source = readFile(qmlDir.filePath(QStringLiteral("ClusterWindow.qml")));
        QVERIFY(source.contains(QStringLiteral("Node password (only if requested)")));
        QVERIFY(source.contains(QStringLiteral("echoMode: TextInput.Password")));
        QVERIFY(source.contains(QStringLiteral("saveSourcePassword")));
        QVERIFY(source.contains(QStringLiteral("removeSourcePassword")));
        QVERIFY(source.contains(QStringLiteral("stored in the system keystore")));
    }

    void newQsoPanelSupportsClearShortcutAndConfigurableAdifFields()
    {
        const QDir qmlDir(QStringLiteral(QML_DIR));
        const QString panel = readFile(qmlDir.filePath(QStringLiteral("NewQsoPanel.qml")));
        const QString dialog = readFile(qmlDir.filePath(QStringLiteral("NewQsoDialog.qml")));
        const QString settings = readFile(qmlDir.filePath(QStringLiteral("NewQsoFieldsDialog.qml")));

        QVERIFY(panel.contains(QStringLiteral("sequence: \"Alt+W\"")));
        QVERIFY(dialog.contains(QStringLiteral("sequence: \"Alt+W\"")));
        QVERIFY(panel.contains(QStringLiteral("onActivated: root.clearForm()")));
        QVERIFY(dialog.contains(QStringLiteral("onActivated: root.clearAll()")));
        QVERIFY(panel.contains(QStringLiteral("function showFieldSettings()")));
        QVERIFY(panel.contains(QStringLiteral("custom_fields: root.customAdifValues()")));
        QVERIFY(panel.contains(QStringLiteral("root.fieldVisible(\"SAT_MODE\")")));
        QVERIFY(panel.contains(QStringLiteral("root.fieldVisible(\"STATE\")")));
        QVERIFY(panel.contains(QStringLiteral("root.fieldVisible(\"TX_PWR\")")));
        QVERIFY(settings.contains(QStringLiteral("saveNewQsoPanelSettings")));
        QVERIFY(settings.contains(QStringLiteral("resetNewQsoPanelSettings")));
        QVERIFY(settings.contains(QStringLiteral("CUSTOM ADIF FIELDS")));
        QVERIFY(settings.contains(QStringLiteral("STATE and TX_PWR")));
    }

    void awardExtensionControlsHaveAccessibleNames()
    {
        const QDir qmlDir(QStringLiteral(QML_DIR));
        const QString source = readFile(qmlDir.filePath(QStringLiteral("AwardsDialog.qml")));
        QVERIFY(source.contains(QStringLiteral("Accessible.name: qsTr(\"CQ DX Marathon edition year\")")));
        QVERIFY(source.contains(QStringLiteral("Accessible.name: qsTr(\"CQ DX Marathon category\")")));
        QVERIFY(source.contains(QStringLiteral("Accessible.name: qsTr(\"Export CQ DX Marathon JSON\")")));
        QVERIFY(source.contains(QStringLiteral("Accessible.name: qsTr(\"VUCC award account\")")));
        QVERIFY(source.contains(QStringLiteral("Accessible.name: qsTr(\"Preview VUCC selection\")")));
    }

    // Nella 1.17.20 «importa» non partiva (la finestra dell'anteprima chiamava
    // `root`, che in Main.qml non c'e': l'id e' `window`), la scheda inferiore non
    // compariva (FileDialog senza `import QtQuick.Dialogs`) e la finestra restava
    // chiara con Ocean Blue (un Dialog di stile, non una finestra del programma).
    void mainQmlDoesNotUseAnUndefinedRootId()
    {
        const QDir qmlDir(QStringLiteral(QML_DIR));
        const QString source = readFile(qmlDir.filePath(QStringLiteral("Main.qml")));
        QVERIFY(!source.contains(QStringLiteral("id: root")));
        QVERIFY(!source.contains(QRegularExpression(QStringLiteral(R"(\broot\.)"))));
    }

    void everyFileDialogHasItsImport()
    {
        const QDir qmlDir(QStringLiteral(QML_DIR));
        const QStringList names = qmlDir.entryList({QStringLiteral("*.qml")}, QDir::Files);
        QVERIFY(!names.isEmpty());
        for (const QString& name : names) {
            const QString source = readFile(qmlDir.filePath(name));
            if (!source.contains(QRegularExpression(QStringLiteral(R"(\bFileDialog\s*\{)"))))
                continue;
            QVERIFY2(source.contains(QStringLiteral("import QtQuick.Dialogs")),
                     qPrintable(name + QStringLiteral(" uses FileDialog without import QtQuick.Dialogs")));
        }
    }

    void dialogsAreProgramWindowsNotStyleDialogs()
    {
        const QDir qmlDir(QStringLiteral(QML_DIR));
        const QStringList names = qmlDir.entryList({QStringLiteral("*.qml")}, QDir::Files);
        const QRegularExpression styleDialog(QStringLiteral(R"(^\s*Dialog\s*\{)"),
                                             QRegularExpression::MultilineOption);
        for (const QString& name : names) {
            const QString source = readFile(qmlDir.filePath(name));
            QVERIFY2(!source.contains(styleDialog),
                     qPrintable(name + QStringLiteral(" uses a style Dialog: unreadable with dark themes, use DialogFrame")));
        }
    }
};

QTEST_GUILESS_MAIN(TestQmlPopups)
#include "tst_qmlpopups.moc"
