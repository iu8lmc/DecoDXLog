// Il gateway del rotore dentro DecoDXLog: il protocollo PRO.SIS.TEL, e il
// gateway intero con il control box finto, parlato come l'app (WebSocket),
// come N1MM+ (rotctld) e come un browser (HTTP). Sono le stesse prove di
// DecoRotor (tests/test_decorotor.py), piu' quelle della rete.
#include "core/Prosistel.h"
#include "core/RotorGateway.h"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QWebSocket>

using namespace decolog::core;

namespace {

int freePort()
{
    QTcpServer probe;
    probe.listen(QHostAddress::LocalHost, 0);
    return probe.serverPort();
}

GatewaySettings simulated(const QString& model = QStringLiteral("d_azel"))
{
    GatewaySettings s;
    s.simulate = true;
    s.simulateSpeed = 120.0;   // un rotore svelto: la prova non aspetta un minuto
    s.model = model;
    s.timeoutMs = 300;
    s.pollMs = 50;
    s.bind = QStringLiteral("127.0.0.1");
    s.wsPort = freePort();
    s.httpPort = freePort();
    s.rotctldPort = freePort();
    return s;
}

bool waitFor(const std::function<bool()>& ok, int ms = 5000)
{
    for (int i = 0; i < ms / 20 && !ok(); ++i)
        QTest::qWait(20);
    return ok();
}

QByteArray frame(const char* text)
{
    return QByteArray(1, prosistel::kStx) + text + QByteArray(1, prosistel::kCr);
}

QJsonObject httpJson(const QString& method, const QString& url, const QByteArray& body = {})
{
    QNetworkAccessManager net;
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply* reply = method == QLatin1String("POST") ? net.post(req, body) : net.get(req);
    QSignalSpy done(reply, &QNetworkReply::finished);
    done.wait(5000);
    const QJsonObject out = QJsonDocument::fromJson(reply->readAll()).object();
    reply->deleteLater();
    return out;
}

} // namespace

class TestRotorGateway : public QObject {
    Q_OBJECT

private slots:
    // ── il protocollo ───────────────────────────────────────────────────────
    void protocolFrames()
    {
        QCOMPARE(prosistel::queryPosition(QLatin1Char('A')), frame("A?"));
        QCOMPARE(prosistel::gotoAngle(QLatin1Char('A'), 290), frame("AG290"));
        QCOMPARE(prosistel::gotoAngle(QLatin1Char('B'), 45.5, 10), frame("BG455"));
        QCOMPARE(prosistel::stop(QLatin1Char('A')), frame("AG997"));
        QCOMPARE(prosistel::stop(QLatin1Char('A'), true), frame("AG999"));
        QCOMPARE(prosistel::stop(QLatin1Char('A'), false, 10), frame("AG9777"));
        // Un goto proprio su un codice di stop fermerebbe il rotore.
        QCOMPARE(prosistel::gotoAngle(QLatin1Char('A'), 99.9, 10), frame("AG998"));
        QCOMPARE(prosistel::disableCpm(QLatin1Char('A')), frame("AS"));

        const auto r = prosistel::decode(frame("A,?,290,R"));
        QVERIFY(r);
        QCOMPARE(r->axis, QChar(u'A'));
        QCOMPARE(r->value, 290.0);
        QVERIFY(!r->moving());
        QCOMPARE(prosistel::decode(frame("B,?,2905,M"), 10)->value, 290.5);
        QVERIFY(prosistel::decode(frame("B,?,2905,M"), 10)->moving());
        QVERIFY(!prosistel::decode(frame("A?290")));
        QVERIFY(!prosistel::decode("garbage"));
        QCOMPARE(prosistel::printable(frame("A?")), QString("<STX>A?<CR>"));
        QVERIFY(prosistel::modelFor("combi")->hasEl());
        QVERIFY(!prosistel::modelFor("d_az")->hasEl());
        QVERIFY(!prosistel::modelFor("auto"));
    }

    void stationsFromDecodes()
    {
        QCOMPARE(RotorGateway::stationFromMessage("CQ DX EA8ABC IL18"), qMakePair(QString("EA8ABC"), QString("IL18")));
        QCOMPARE(RotorGateway::stationFromMessage("IU8LMC EA8ABC RR73"), qMakePair(QString("EA8ABC"), QString()));
        QCOMPARE(RotorGateway::stationFromMessage("IU8LMC EA8ABC R-09"), qMakePair(QString("EA8ABC"), QString()));
        QCOMPARE(RotorGateway::stationFromMessage("CQ <K1ABC> FN42"), qMakePair(QString("K1ABC"), QString("FN42")));
        QCOMPARE(RotorGateway::stationFromMessage("CQ TEST"), qMakePair(QString(), QString()));
    }

    // ── il gateway, col control box finto ───────────────────────────────────
    void detectsTheModelAndMoves()
    {
        RotorGateway gw;
        GatewaySettings s = simulated(QStringLiteral("auto"));
        gw.start(s, GatewayLive());
        QVERIFY(waitFor([&] { return gw.snapshot().value("connected").toBool(); }));
        QCOMPARE(gw.snapshot().value("model").toString(), QString("d_azel"));
        QVERIFY(waitFor([&] { return !gw.snapshot().value("az").isNull(); }));

        QString error;
        const QJsonObject applied = gw.gotoPosition(90.0, 20.0, &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(applied.value("az").toDouble(), 90.0);
        QVERIFY(waitFor([&] { return std::abs(gw.snapshot().value("az").toDouble() - 90.0) <= 1.0
                                   && gw.snapshot().value("az_target").isNull(); }));
        QCOMPARE(gw.snapshot().value("el").toDouble(), 20.0);

        // Finecorsa software e gradi oltre il giro.
        GatewayLive live;
        live.azMax = 270.0;
        gw.start(s, live);
        QVERIFY(waitFor([&] { return gw.snapshot().value("connected").toBool(); }));
        QCOMPARE(gw.gotoPosition(300.0, std::nullopt, &error).value("az").toDouble(), 270.0);
        QCOMPARE(gw.gotoPosition(370.0, std::nullopt, &error).value("az").toDouble(), 10.0);
        gw.gotoPosition(std::nullopt, std::nullopt, &error);
        QCOMPARE(error, QString("nessun asse indicato"));
        gw.halt();
        QVERIFY(gw.snapshot().value("az_target").isNull());
    }

    void azimuthOnlyBox()
    {
        RotorGateway gw;
        gw.start(simulated(QStringLiteral("d_az")), GatewayLive());
        QVERIFY(waitFor([&] { return gw.snapshot().value("connected").toBool(); }));
        QVERIFY(!gw.snapshot().value("has_el").toBool());
        QString error;
        gw.gotoPosition(std::nullopt, 10.0, &error);
        QCOMPARE(error, QString("il control box configurato non ha elevazione"));
    }

    void speaksLikeRotctld()
    {
        RotorGateway gw;
        gw.start(simulated(), GatewayLive());
        QVERIFY(waitFor([&] { return !gw.snapshot().value("az").isNull(); }));
        QCOMPARE(gw.rotctldLine("p"), QByteArray("0.00\n0.00\n"));
        QCOMPARE(gw.rotctldLine("+p"), QByteArray("Azimuth: 0.00\nElevation: 0.00\n"));
        QCOMPARE(gw.rotctldLine("P 45 10"), QByteArray("RPRT 0\n"));
        QCOMPARE(gw.rotctldLine("P abc"), QByteArray("RPRT -1\n"));
        QCOMPARE(gw.rotctldLine("S"), QByteArray("RPRT 0\n"));
        QCOMPARE(gw.rotctldLine("K"), QByteArray("RPRT 0\n"));
        QCOMPARE(gw.rotctldLine("zz"), QByteArray("RPRT -8\n"));
        QVERIFY(gw.rotctldLine("\\dump_state").endsWith("rot_type=AzEl\ndone\n"));
        bool quit = false;
        gw.rotctldLine("q", &quit);
        QVERIFY(quit);

        // E sul filo, come lo usa un programma di contest.
        QTcpSocket client;
        client.connectToHost(QHostAddress::LocalHost, static_cast<quint16>(gw.settings().rotctldPort));
        // Client e gateway stanno nello stesso thread: niente attese bloccanti.
        QVERIFY(waitFor([&] { return client.state() == QAbstractSocket::ConnectedState; }));
        client.write("P 120 0\n");
        QVERIFY(waitFor([&] { return client.bytesAvailable() > 0; }));
        QCOMPARE(client.readAll(), QByteArray("RPRT 0\n"));
        QVERIFY(waitFor([&] { return std::abs(gw.snapshot().value("az").toDouble() - 120.0) <= 1.0; }));
    }

    void speaksLikeDecoRotorToTheApp()
    {
        RotorGateway gw;
        GatewayLive live;
        live.locator = QStringLiteral("JN70");
        gw.start(simulated(), live);
        QVERIFY(waitFor([&] { return gw.snapshot().value("connected").toBool(); }));

        QWebSocket app;
        QStringList received;
        connect(&app, &QWebSocket::textMessageReceived, this, [&](const QString& t) { received << t; });
        app.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(gw.settings().wsPort)));
        QVERIFY(waitFor([&] { return received.size() >= 2; }));
        QCOMPARE(QJsonDocument::fromJson(received.at(0).toUtf8()).object().value("type").toString(), QString("hello"));
        QCOMPARE(QJsonDocument::fromJson(received.at(1).toUtf8()).object().value("type").toString(), QString("state"));

        auto ask = [&](const QJsonObject& cmd) {
            received.clear();
            app.sendTextMessage(QString::fromUtf8(QJsonDocument(cmd).toJson(QJsonDocument::Compact)));
            QJsonObject answer;
            waitFor([&] {
                for (const QString& t : received) {
                    const QJsonObject o = QJsonDocument::fromJson(t.toUtf8()).object();
                    const QString type = o.value("type").toString();
                    if (type == "ack" || type == "error" || type == "pong") {
                        answer = o;
                        return true;
                    }
                }
                return false;
            });
            return answer;
        };

        QJsonObject a = ask({{"cmd", "goto"}, {"az", 200}});
        QCOMPARE(a.value("type").toString(), QString("ack"));
        QCOMPARE(a.value("applied").toObject().value("az").toDouble(), 200.0);
        a = ask({{"cmd", "bearing"}, {"locator", "FN42"}});
        QVERIFY(a.value("bearing").toObject().value("short_path").toDouble() > 290.0);
        a = ask({{"cmd", "goto_locator"}, {"locator", "FN42"}, {"long_path", true}});
        QCOMPARE(a.value("applied").toObject().value("locator").toString(), QString("FN42"));
        a = ask({{"cmd", "preset_save"}, {"name", "Beam W"}, {"az", 270}});
        QCOMPARE(a.value("presets").toArray().size(), 1);
        a = ask({{"cmd", "preset_recall"}, {"name", "Beam W"}});
        QCOMPARE(a.value("applied").toObject().value("az").toDouble(), 270.0);
        a = ask({{"cmd", "preset_recall"}, {"name", "Nope"}});
        QCOMPARE(a.value("type").toString(), QString("error"));
        a = ask({{"cmd", "config_set"}, {"values", QJsonObject{{"park_az", 180}}}});
        QCOMPARE(a.value("config").toObject().value("park_az").toDouble(), 180.0);
        QCOMPARE(gw.live().parkAz, 180.0);
        a = ask({{"cmd", "traffic"}, {"limit", 5}});
        QVERIFY(!a.value("traffic").toArray().isEmpty());
        a = ask({{"cmd", "ping"}});
        QCOMPARE(a.value("type").toString(), QString("pong"));
        a = ask({{"cmd", "boh"}});
        QCOMPARE(a.value("message").toString(), QString("comando sconosciuto: 'boh'"));

        // L'ultimo che comandava se ne va a meta' movimento: si ferma tutto.
        ask({{"cmd", "goto"}, {"az", 10}});
        QVERIFY(!gw.snapshot().value("az_target").isNull());
        app.close();
        QVERIFY(waitFor([&] { return gw.snapshot().value("az_target").isNull(); }));
    }

    void tokenIsRequiredWhenSet()
    {
        RotorGateway gw;
        GatewaySettings s = simulated();
        s.token = QStringLiteral("secret");
        gw.start(s, GatewayLive());
        QWebSocket app;
        QStringList received;
        connect(&app, &QWebSocket::textMessageReceived, this, [&](const QString& t) { received << t; });
        app.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(s.wsPort)));
        QVERIFY(waitFor([&] { return received.size() >= 2; }));
        received.clear();
        app.sendTextMessage(R"({"cmd":"goto","az":10})");
        // In mezzo passano anche gli stati: si cerca la risposta.
        auto seen = [&](const char* text) {
            return waitFor([&] { return received.filter(QString::fromLatin1(text)).size() > 0; });
        };
        QVERIFY(seen("token mancante o errato"));
        received.clear();
        app.sendTextMessage(R"({"cmd":"auth","token":"secret"})");
        QVERIFY(seen("\"ok\":true"));
    }

    void speaksHttpAndKeepsTheStations()
    {
        RotorGateway gw;
        GatewayLive live;
        live.locator = QStringLiteral("JN70");
        live.callsign = QStringLiteral("IU8LMC");
        gw.start(simulated(), live);
        QVERIFY(waitFor([&] { return gw.snapshot().value("connected").toBool(); }));
        const QString base = QStringLiteral("http://127.0.0.1:%1").arg(gw.settings().httpPort);

        QJsonObject info = httpJson("GET", base + "/api/info");
        QCOMPARE(info.value("product").toString(), QString("DecoRotor"));
        QCOMPARE(info.value("ws_port").toInt(), gw.settings().wsPort);
        QCOMPARE(httpJson("GET", base + "/api/state").value("type").toString(), QString("state"));
        QCOMPARE(httpJson("POST", base + "/api/goto", R"({"az": 33})").value("applied").toObject().value("az").toDouble(), 33.0);
        QVERIFY(httpJson("POST", base + "/api/park").value("ok").toBool());

        gw.noteDecode("CQ EA8ABC IL18", -12, "FT8", 14074000);
        gw.noteDecode("IU8LMC K1ABC FN42", -5, "FT8", 14074000);
        gw.noteDecode("CQ IU8LMC JN70", 0, "FT8", 14074000);   // la propria stazione no
        QTest::qWait(20);   // il piu' recente sta in cima: che lo sia davvero
        gw.noteCluster("VK9XX", -10.5, 105.6, "Christmas Island", "CW", 14025000, "up 1");
        const QJsonObject spots = httpJson("GET", base + "/api/spots");
        const QJsonArray list = spots.value("spots").toArray();
        QCOMPARE(list.size(), 3);
        QCOMPARE(list.first().toObject().value("call").toString(), QString("VK9XX"));
        QCOMPARE(list.first().toObject().value("source").toString(), QString("cluster"));
        bool sawEa8 = false;
        for (const QJsonValue& v : list) {
            if (v.toObject().value("call").toString() == "EA8ABC") {
                sawEa8 = true;
                QCOMPARE(v.toObject().value("snr").toInt(), -12);
                QVERIFY(v.toObject().value("az").toDouble() > 200.0);   // le Canarie, a sud-ovest
            }
        }
        QVERIFY(sawEa8);

        // La pagina web di DecoRotor.
        QNetworkAccessManager net;
        QNetworkReply* page = net.get(QNetworkRequest(QUrl(base + "/")));
        QSignalSpy done(page, &QNetworkReply::finished);
        QVERIFY(done.wait(5000));
        QVERIFY(page->readAll().contains("DecoRotor"));
        page->deleteLater();
    }

    void aPortInUseIsExplained()
    {
        QTcpServer taken;
        QVERIFY(taken.listen(QHostAddress::LocalHost, 0));
        GatewaySettings s = simulated();
        s.rotctldPort = taken.serverPort();
        RotorGateway gw;
        gw.start(s, GatewayLive());
        QCOMPARE(gw.problems().size(), 1);
        QVERIFY(gw.problems().first().startsWith("rotctld"));
    }
};

QTEST_GUILESS_MAIN(TestRotorGateway)
#include "tst_rotorgateway.moc"
