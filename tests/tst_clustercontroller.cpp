// Cluster nell'applicazione: spot arricchiti dal log e dal cty.csv, filtri, regole
// d'avviso e messaggi a Decodium (spot e tune) su un DecoLink vero.
#include "app/ClusterController.h"
#include "core/DecoLinkServer.h"
#include "core/LogDatabase.h"
#include "core/Modes.h"

#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

using namespace decolog::core;
using decolog::app::ClusterController;
using decolog::app::SpotModel;

namespace {

class Client {
public:
    QTcpSocket socket;
    QList<QJsonObject> received;

    bool connectTo(quint16 port)
    {
        socket.connectToHost(QHostAddress::LocalHost, port);
        return socket.waitForConnected(3000);
    }
    void send(const QJsonObject& o) { socket.write(QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n'); }
    std::optional<QJsonObject> waitFor(const QString& type, int timeoutMs = 3000)
    {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < timeoutMs) {
            for (qsizetype i = m_consumed; i < received.size(); ++i) {
                if (received.at(i).value("type").toString() == type) {
                    m_consumed = i + 1;
                    return received.at(i);
                }
            }
            if (!socket.waitForReadyRead(50))
                QCoreApplication::processEvents();
            m_buffer += socket.readAll();
            qsizetype nl;
            while ((nl = m_buffer.indexOf('\n')) >= 0) {
                received << QJsonDocument::fromJson(m_buffer.left(nl)).object();
                m_buffer.remove(0, nl + 1);
            }
        }
        return std::nullopt;
    }

private:
    QByteArray m_buffer;
    qsizetype m_consumed{0};
};

} // namespace

class TestClusterController : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Impostazioni e cache in una cartella di prova, non in quelle di DecoDXLog.
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("DecoDXLogTest");
        QCoreApplication::setApplicationName("tst_clustercontroller");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings().clear();
    }

    void spotsAlertsAndDecodium()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        db.insertQso({{"CALL", "JA1XX"}, {"QSO_DATE", "20260101"}, {"TIME_ON", "1200"}, {"BAND", "20m"},
                      {"MODE", "FT8"}, {"DXCC", "339"}}, "import");

        Countries countries;
        QFile cty(":/decolog/cty.csv");
        QVERIFY(cty.open(QIODevice::ReadOnly));
        QVERIFY(countries.load(cty.readAll()));

        DecoLinkServer link;
        link.workedRows = [] { return QList<QJsonArray>{}; };
        link.awardState = [] { return QJsonObject{}; };
        link.resolveQuery = [](const QJsonObject&) { return QJsonArray{}; };
        QVERIFY(link.start(0));

        QStringList activity;
        QString looked;
        // La radio finta: si segna dove le e' stato detto di andare.
        double radioMhz = 0;
        QString radioMode;
        QString radioCatMode;
        bool radioThere = true;
        ClusterController::Context ctx;
        ctx.db = &db;
        ctx.countries = &countries;
        ctx.decoLink = &link;
        ctx.stationCall = [] { return QStringLiteral("IU8LMC"); };
        ctx.stationGrid = [] { return QStringLiteral("JN71DC"); };
        ctx.propagationForecast = [](const QString&, const QString& band, const QString&) {
            if (band == QStringLiteral("20m"))
                return QVariantMap{{QStringLiteral("available"), true},
                                   {QStringLiteral("quality"), 3},
                                   {QStringLiteral("engine"), QStringLiteral("voacap")}};
            return QVariantMap{{QStringLiteral("available"), false}};
        };
        ctx.activity = [&activity](const QString& cat, const QString& text, const QString&) { activity << cat + ": " + text; };
        ctx.lookup = [&looked](const QString& call) { looked = call; };
        ctx.tuneRadio = [&](double mhz, const QString& mode) {
            if (!radioThere)
                return false;
            radioMhz = mhz;
            radioMode = mode;
            radioCatMode = modes::catForClusterSpot(mode, mhz);
            return true;
        };
        ClusterController cluster(std::move(ctx));
        cluster.setMuted(true);

        Client decodium;
        QVERIFY(decodium.connectTo(link.port()));
        decodium.send({{"type", "hello"}, {"app", "Decodium"}, {"version", "test"}, {"protocol", 1}});
        QVERIFY(decodium.waitFor("hello"));

        QSignalSpy alerts(&cluster, &ClusterController::alertRaised);
        auto* model = qobject_cast<SpotModel*>(cluster.spots());
        const QString hhmm = QDateTime::currentDateTimeUtc().toString("HHmm");

        // Nuovo DXCC: regola predefinita, avviso e spot a Decodium con alert.
        cluster.injectLine(QString("DX de EA5WU-#:  14025.1  3Y0J  CW 18 dB 24 WPM CQ  %1Z").arg(hhmm));
        QCOMPARE(model->count(), 1);
        QCOMPARE(alerts.size(), 1);
        const auto spot = decodium.waitFor("spot");
        QVERIFY(spot);
        QCOMPARE(spot->value("call").toString(), QString("3Y0J"));
        QVERIFY(spot->value("alert").toBool());
        QVERIFY(spot->value("statusBits").toInt() & StatusNewDxcc);
        QVERIFY(activity.last().startsWith("CLUSTER:"));

        const QVariantList recommendations = cluster.assistantRecommendations();
        QCOMPARE(recommendations.size(), 1);
        QCOMPARE(recommendations.first().toMap().value("call").toString(), QString("3Y0J"));
        QVERIFY(recommendations.first().toMap().value("reasons").toStringList().join(" ").contains("new DXCC"));
        QVERIFY(recommendations.first().toMap().value("reasons").toStringList().join(" ").contains("propagation quality 3/3"));
        QVERIFY(!recommendations.first().toMap().value("limitations").toStringList().join(" ").contains("propagation unavailable"));
        QCOMPARE(recommendations.first().toMap().value("propagationEngine").toString(), QStringLiteral("voacap"));

        QVariantMap preferences = cluster.assistantPreferences();
        preferences["newEntityWeight"] = 90;
        preferences["freshnessMinutes"] = 45;
        preferences["rejectStale"] = true;
        preferences["stalePenalty"] = 0.25;
        cluster.setAssistantPreferences(preferences);
        QCOMPARE(cluster.assistantPreferences().value("newEntityWeight").toInt(), 90);
        QCOMPARE(cluster.assistantPreferences().value("freshnessMinutes").toInt(), 45);
        QCOMPARE(cluster.assistantPreferences().value("rejectStale").toBool(), true);
        QCOMPARE(cluster.assistantPreferences().value("stalePenalty").toDouble(), 0.25);
        preferences["newEntityWeight"] = 999;
        preferences["freshnessMinutes"] = 0;
        cluster.setAssistantPreferences(preferences);
        QCOMPARE(cluster.assistantPreferences().value("newEntityWeight").toInt(), 100);
        QCOMPARE(cluster.assistantPreferences().value("freshnessMinutes").toInt(), 1);
        preferences["stalePenalty"] = 2.0;
        cluster.setAssistantPreferences(preferences);
        QCOMPARE(cluster.assistantPreferences().value("stalePenalty").toDouble(), 1.0);

        // Lo stesso DX da un altro skimmer: una riga con due spotter, nessun nuovo avviso.
        cluster.injectLine(QString("DX de DL1ABC-#:  14025.1  3Y0J  CW 22 dB 25 WPM CQ  %1Z").arg(hhmm));
        QCOMPARE(model->count(), 1);
        QCOMPARE(model->get(0).value("spotCount").toInt(), 2);
        QCOMPARE(alerts.size(), 1);

        // Gia' lavorato sulla banda: nessun avviso; il filtro "hide worked" lo nasconde.
        cluster.injectLine(QString("DX de IK8XXX:  14075.0  JA1XX  FT8 -12 dB  %1Z").arg(hhmm));
        QCOMPARE(model->count(), 2);
        QCOMPARE(model->get(0).value("statusLabel").toString(), QString("WORKED"));
        QCOMPARE(alerts.size(), 1);
        QVariantMap f = cluster.filter();
        f["hideWorkedBand"] = true;
        cluster.setFilter(f);
        QCOMPARE(model->count(), 1);

        // Doppio clic: tune con dial e audio per FT8.
        cluster.injectLine(QString("DX de SP9ABC:  18101.5  VK2DEF  FT8 -15 dB  %1Z").arg(hhmm));
        const QString key = model->get(0).value("spotKey").toString();
        QCOMPARE(model->get(0).value("call").toString(), QString("VK2DEF"));
        QVERIFY(model->get(0).value("distance").toInt() > 12000);
        // Lo spot scelto va anche all'inserimento veloce del contest: nominativo,
        // banda e modo. Con un clic e con il doppio clic.
        QSignalSpy picked(&cluster, &ClusterController::spotPicked);
        cluster.lookupSpot(key);
        QCOMPARE(picked.size(), 1);
        QCOMPARE(picked.at(0).at(0).toString(), QString("VK2DEF"));
        QCOMPARE(picked.at(0).at(1).toString(), QString("17m"));
        QCOMPARE(picked.at(0).at(2).toString(), QString("FT8"));
        cluster.tune(key);
        QCOMPARE(picked.size(), 2);
        QCOMPARE(looked, QString("VK2DEF"));
        const auto tune = decodium.waitFor("tune");
        QVERIFY(tune);
        QCOMPARE(tune->value("dialKhz").toDouble(), 18100.0);
        QCOMPARE(tune->value("audioHz").toInt(), 1500);
        QCOMPARE(tune->value("mode").toString(), QString("FT8"));

        // E la radio ci va davvero. Per un modo digitale il VFO sta sulla
        // sotto-banda, non sulla frequenza dello spot: il DX e' un tono
        // nell'audio, e una radio portata su 18101.5 non lo sentirebbe.
        QCOMPARE(radioMhz, 18.100);
        QCOMPARE(radioMode, QString("FT8"));
        QCOMPARE(radioCatMode, QString("USB"));

        // In CW il quadrante e' la frequenza dello spot, senza scarti.
        cluster.injectLine(QString("DX de F5ABC:  14025.1  3Y0J  CW 18 dB  %1Z").arg(hhmm));
        radioMhz = 0;
        radioMode.clear();
        for (int i = 0; i < model->count(); ++i) {
            if (model->get(i).value("call").toString() == QLatin1String("3Y0J")) {
                cluster.tune(model->get(i).value("spotKey").toString());
                break;
            }
        }
        QCOMPARE(radioMhz, 14.0251);
        QCOMPARE(radioMode, QString("CW"));
        QCOMPARE(radioCatMode, QString("CW"));

        // Senza Decodium la radio si muove lo stesso: chi lavora in CW non ha
        // Decodium aperto, e fino a ieri un doppio clic non muoveva un VFO.
        decodium.socket.disconnectFromHost();
        QTRY_COMPARE_WITH_TIMEOUT(link.clientCount(), 0, 4000);
        radioMhz = 0;
        activity.clear();
        for (int i = 0; i < model->count(); ++i) {
            if (model->get(i).value("call").toString() == QLatin1String("3Y0J")) {
                cluster.tune(model->get(i).value("spotKey").toString());
                break;
            }
        }
        QCOMPARE(radioMhz, 14.0251);
        QVERIFY2(activity.last().contains(QStringLiteral("radio")), qPrintable(activity.last()));

        // E senza ne' radio ne' Decodium si dice, invece di non fare niente in
        // silenzio.
        radioThere = false;
        activity.clear();
        for (int i = 0; i < model->count(); ++i) {
            if (model->get(i).value("call").toString() == QLatin1String("3Y0J")) {
                cluster.tune(model->get(i).value("spotKey").toString());
                break;
            }
        }
        QVERIFY2(activity.last().contains(QStringLiteral("Nowhere")), qPrintable(activity.last()));
        radioThere = true;

        // Un QSO nuovo nel log cambia lo stato degli spot gia' in lista.
        cluster.setFilter({});
        const QString dxcc = model->get(model->count() - 1).value("dxcc").toString();
        db.insertQso({{"CALL", "3Y0J"}, {"QSO_DATE", QDateTime::currentDateTimeUtc().toString("yyyyMMdd")}, {"TIME_ON", hhmm},
                      {"BAND", "20m"}, {"MODE", "CW"}, {"DXCC", dxcc}}, "manual");
        cluster.logChanged();
        QTRY_COMPARE_WITH_TIMEOUT(model->get(model->count() - 1).value("statusLabel").toString(), QString("WORKED"), 4000);
    }

    // Mandare uno spot: si controlla prima (nominativo, frequenza dentro le bande, commento
    // corto, gia' segnalata da poco), e parte al nodo che si e' scelto con il comando di
    // DX Spider, "DX <kHz> <nominativo> <commento>".
    void postingASpotChecksFirstAndSendsTheCommand()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        ClusterController::Context ctx;
        ctx.db = &db;
        ctx.stationCall = [] { return QStringLiteral("IU8LMC"); };
        QStringList activity;
        ctx.activity = [&activity](const QString& cat, const QString& text, const QString&) { activity << cat + ": " + text; };
        ClusterController cluster(std::move(ctx));
        cluster.setMuted(true);

        // I controlli, senza nodo.
        QVERIFY(!cluster.checkSpot("", "14074", "").value("ok").toBool());
        QVERIFY(!cluster.checkSpot("14074", "14074", "").value("ok").toBool());          // non e' un nominativo
        QVERIFY(!cluster.checkSpot("K1ABC", "", "").value("ok").toBool());               // niente frequenza
        QVERIFY(!cluster.checkSpot("K1ABC", "abc", "").value("ok").toBool());
        const QVariantMap outside = cluster.checkSpot("K1ABC", "15000", "");             // fra le bande
        QVERIFY(!outside.value("ok").toBool());
        QVERIFY(outside.value("error").toString().contains("outside"));
        const QVariantMap good = cluster.checkSpot(" k1abc ", "14074,06", "FT8   -10 dB   FN42");
        QVERIFY2(good.value("ok").toBool(), qPrintable(good.value("error").toString()));
        QCOMPARE(good.value("call").toString(), QString("K1ABC"));
        QCOMPARE(good.value("freqKhz").toString(), QString("14074.1"));                   // un decimale
        QCOMPARE(good.value("band").toString(), QString("20m"));
        QCOMPARE(good.value("comment").toString(), QString("FT8 -10 dB FN42"));           // spazi in meno
        QVERIFY(good.value("duplicate").toString().isEmpty());
        // In MHz, come si scrive spesso: 14.074 sono 14074 kHz.
        QCOMPARE(cluster.checkSpot("K1ABC", "14.074", "").value("freqKhz").toString(), QString("14074.0"));
        // Il commento dei nodi finisce a 30 caratteri.
        QCOMPARE(cluster.checkSpot("K1ABC", "14074", QString(50, 'x')).value("comment").toString().size(), 30);

        // Senza nodo collegato non parte, e lo dice.
        QVERIFY(cluster.postSpot("K1ABC", "14074.0", "FT8").contains("No cluster node"));

        // Un nodo che saluta, e il login fino a «online».
        QTcpServer node;
        QTcpSocket* client = nullptr;
        QByteArray received;
        QObject::connect(&node, &QTcpServer::newConnection, &node, [&] {
            client = node.nextPendingConnection();
            QObject::connect(client, &QTcpSocket::readyRead, client, [&] { received += client->readAll(); });
            client->write("Welcome to FAKE-1\r\nlogin: ");
        });
        QVERIFY(node.listen(QHostAddress::LocalHost));
        const QString id = cluster.addSource({{"name", "Fake"}, {"type", "cluster"}, {"host", "127.0.0.1"},
                                              {"port", node.serverPort()}, {"enabled", true}});
        QTRY_VERIFY_WITH_TIMEOUT(received.contains("IU8LMC"), 5000);
        client->write("IU8LMC-2 de FAKE-1 >\r\n");
        QTRY_VERIFY_WITH_TIMEOUT(cluster.onlineCount() == 1, 5000);

        QCOMPARE(cluster.postSpot("k1abc", "14074.0", "FT8 -10 dB", id), QString());
        QTRY_VERIFY_WITH_TIMEOUT(received.contains("DX 14074.0 K1ABC FT8 -10 dB\r\n"), 3000);
        QVERIFY2(activity.last().contains("Spot sent"), qPrintable(activity.last()));
        // Un nodo che non c'e' non riceve niente, e non si finisce su un altro per caso.
        const qsizetype before = received.size();
        QVERIFY(!cluster.postSpot("K1ABC", "14074.0", "", "no-such-node").isEmpty());
        QTest::qWait(150);
        QCOMPARE(received.size(), before);

        // Gia' segnalata da poco sulla stessa frequenza: lo dice, ma non lo vieta.
        const QString hhmm = QDateTime::currentDateTimeUtc().toString("HHmm");
        cluster.injectLine(QString("DX de DL1XYZ:  14074.2  K1ABC  FT8 -12 dB  %1Z").arg(hhmm));
        const QVariantMap dup = cluster.checkSpot("K1ABC", "14074.0", "");
        QVERIFY(dup.value("ok").toBool());
        QVERIFY2(dup.value("duplicate").toString().contains("DL1XYZ"), qPrintable(dup.value("duplicate").toString()));
        // Altra frequenza o altra stazione: nessun avviso.
        QVERIFY(cluster.checkSpot("K1ABC", "14080.0", "").value("duplicate").toString().isEmpty());
        QVERIFY(cluster.checkSpot("K2XYZ", "14074.0", "").value("duplicate").toString().isEmpty());
    }

    // «Check» su una fonte: dove si ferma il collegamento, scritto nella fonte
    // stessa. Un nodo che parla, uno che accetta e chiude (il segno di un
    // antivirus in mezzo), e una fonte web che non ha niente da controllare.
    void checkSourceSaysWhereTheLinkStops()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        ClusterController::Context ctx;
        ctx.db = &db;
        ctx.stationCall = [] { return QStringLiteral("IU8LMC"); };
        ClusterController cluster(std::move(ctx));
        cluster.setMuted(true);

        bool closeAtOnce = false;
        QTcpServer node;
        QObject::connect(&node, &QTcpServer::newConnection, &node, [&] {
            QTcpSocket* c = node.nextPendingConnection();
            if (closeAtOnce)
                c->disconnectFromHost();
            else
                c->write("Welcome to FAKE-1\r\nlogin: ");
        });
        QVERIFY(node.listen(QHostAddress::LocalHost));

        const auto sourceOf = [&](const QString& id) {
            for (const QVariant& v : cluster.sources()) {
                if (v.toMap().value("id").toString() == id)
                    return v.toMap();
            }
            return QVariantMap{};
        };
        const auto added = [&](const char* type, const QString& host, int port) {
            return cluster.addSource({{"name", "Prova"}, {"type", type}, {"host", host}, {"port", port}, {"enabled", false}});
        };

        const QString talking = added("cluster", "127.0.0.1", node.serverPort());
        cluster.checkSource(talking);
        QVERIFY(sourceOf(talking).value("checking").toBool());
        QTRY_VERIFY2(!sourceOf(talking).value("checking").toBool(), "il controllo non finisce");
        QVERIFY2(sourceOf(talking).value("checkText").toString().contains(QStringLiteral("Welcome to FAKE-1")),
                 qPrintable(sourceOf(talking).value("checkText").toString()));

        closeAtOnce = true;
        cluster.checkSource(talking);
        QTRY_VERIFY(!sourceOf(talking).value("checking").toBool());
        QVERIFY2(sourceOf(talking).value("checkText").toString().contains(QStringLiteral("Avast")),
                 qPrintable(sourceOf(talking).value("checkText").toString()));

        const QString pota = added("pota", "api.pota.app", 443);
        cluster.checkSource(pota);
        QVERIFY(!sourceOf(pota).value("checking").toBool());
        QVERIFY(!sourceOf(pota).value("checkText").toString().isEmpty());

        // Tolta la fonte, un controllo in corso non la rimette in lista.
        cluster.checkSource(talking);
        cluster.removeSource(talking);
        QTest::qWait(300);
        QVERIFY(sourceOf(talking).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestClusterController)
#include "tst_clustercontroller.moc"
