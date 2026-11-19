// Callbook: lettura delle risposte XML di QRZ.com e HamQTH e il giro completo
// login → ricerca → sessione scaduta → nuovo login, contro un server HTTP finto.
#include "core/Adif.h"
#include "core/Callbook.h"

#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUrlQuery>

using namespace decolog::core;

namespace {

const QByteArray kQrzLogin =
    R"(<?xml version="1.0" ?>
<QRZDatabase version="1.34" xmlns="http://xmldata.qrz.com">
  <Session><Key>2331uf894c4bd29f3923f3bacf02c532d7bd9</Key><Count>123</Count>
  <SubExp>Wed Jan 1 12:34:03 2027</SubExp><GMTime>Sun Aug 16 03:51:47 2026</GMTime></Session>
</QRZDatabase>)";

const QByteArray kQrzCallsign =
    R"(<?xml version="1.0" ?>
<QRZDatabase version="1.34" xmlns="http://xmldata.qrz.com">
  <Callsign>
    <call>EA8OH</call><fname>Pekka</fname><name>Virtanen</name><addr1>Calle Mayor 1</addr1>
    <addr2>Las Palmas</addr2><country>Canary Islands</country><lat>28.12</lat><lon>-15.43</lon>
    <grid>IL18QI</grid><dxcc>29</dxcc><cqzone>33</cqzone><ituzone>36</ituzone>
    <email>ea8oh@example.org</email><qslmgr>LoTW</qslmgr><lotw>1</lotw><eqsl>0</eqsl>
    <image>https://cdn-xml.qrz.com/x/ea8oh/photo.jpg</image>
  </Callsign>
  <Session><Key>2331uf894c4bd29f3923f3bacf02c532d7bd9</Key></Session>
</QRZDatabase>)";

const QByteArray kQrzNotFound =
    R"(<QRZDatabase version="1.34"><Session><Error>Not found: XX9XX</Error><Key>abc</Key></Session></QRZDatabase>)";
const QByteArray kQrzTimeout =
    R"(<QRZDatabase version="1.34"><Session><Error>Session Timeout</Error></Session></QRZDatabase>)";
const QByteArray kQrzBadPassword =
    R"(<QRZDatabase version="1.34"><Session><Error>Username/password incorrect</Error></Session></QRZDatabase>)";

const QByteArray kHamQthLogin =
    R"(<?xml version="1.0"?><HamQTH version="2.8" xmlns="https://www.hamqth.com"><session>
<session_id>09b0ae90050be03c452ad235a1f2915ad684393c</session_id></session></HamQTH>)";

const QByteArray kHamQthNoGrid =
    R"(<?xml version="1.0"?><HamQTH version="2.8" xmlns="https://www.hamqth.com"><search>
<callsign>ea8oh</callsign><nick>Pekka</nick><qth>Las Palmas</qth><country>Canary Islands</country>
<adif>29</adif><lotw>Y</lotw></search></HamQTH>)";

const QByteArray kHamQthNotFound =
    R"(<?xml version="1.0"?><HamQTH version="2.8" xmlns="https://www.hamqth.com"><session>
<error>Callsign not found</error></session></HamQTH>)";

const QByteArray kHamQthSearch =
    R"(<?xml version="1.0"?><HamQTH version="2.8" xmlns="https://www.hamqth.com"><search>
<callsign>ok7an</callsign><nick>Petr</nick><qth>Neratovice</qth><country>Czech Republic</country>
<adif>503</adif><itu>28</itu><cq>15</cq><grid>jo70gg</grid><adr_name>Petr Hlozek</adr_name>
<adr_city>Neratovice</adr_city><lotw>Y</lotw><eqsl>N</eqsl><latitude>50.2</latitude><longitude>14.5</longitude>
<picture>https://www.hamqth.com/userfiles/o/ok/ok7an/_profile/ok7an.jpg</picture></search></HamQTH>)";

// Un server HTTP minimo: una risposta decisa da una funzione della query.
class FakeHttp : public QTcpServer {
public:
    std::function<QByteArray(const QUrlQuery&)> respond;
    QList<QUrlQuery> requests;

    FakeHttp()
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* s = nextPendingConnection()) {
                connect(s, &QTcpSocket::readyRead, s, [this, s] {
                    const QByteArray head = s->readAll();
                    const QByteArray line = head.left(head.indexOf("\r\n"));
                    const QUrl url(QString::fromLatin1(line.split(' ').value(1)));
                    const QUrlQuery q(url);
                    requests << q;
                    const QByteArray body = respond(q);
                    s->write("HTTP/1.1 200 OK\r\nContent-Type: text/xml\r\nConnection: close\r\nContent-Length: "
                             + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                    s->disconnectFromHost();
                });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
        listen(QHostAddress::LocalHost);
    }
    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/xml").arg(serverPort())); }
};

void giveCredentials(CallbookClient& c, const QString& password)
{
    c.setCredentialReaders(
        [](const QString&) { return QStringLiteral("IU8LMC"); },
        [password](const QString&, std::function<void(const QString&, const QString&)> done) { done(password, {}); });
}

} // namespace

class TestCallbook : public QObject {
    Q_OBJECT

private slots:
    void parseQrz()
    {
        const auto session = callbook::parseQrzSession(kQrzLogin);
        QCOMPARE(session.key, QString("2331uf894c4bd29f3923f3bacf02c532d7bd9"));
        QVERIFY(session.error.isEmpty());

        const auto rec = callbook::parseQrzCallsign(kQrzCallsign);
        QVERIFY(rec);
        QCOMPARE(rec->call, QString("EA8OH"));
        QCOMPARE(rec->name, QString("Pekka Virtanen"));
        QCOMPARE(rec->qth, QString("Las Palmas"));
        QCOMPARE(rec->grid, QString("IL18QI"));
        QCOMPARE(rec->dxcc, 29);
        QCOMPARE(rec->cqZone, 33);
        QVERIFY(rec->lotw);
        QVERIFY(!rec->eqsl);
        QVERIFY(rec->hasPosition);
        QCOMPARE(rec->source, QString("QRZ.com"));

        QVERIFY(!callbook::parseQrzCallsign(kQrzNotFound));
        QVERIFY(callbook::parseQrzSession(kQrzTimeout).expired);
        QVERIFY(!callbook::parseQrzSession(kQrzBadPassword).expired);
        QVERIFY(callbook::parseQrzSession(kQrzBadPassword).key.isEmpty());
    }

    void parseHamQth()
    {
        QCOMPARE(callbook::parseHamQthSession(kHamQthLogin).key, QString("09b0ae90050be03c452ad235a1f2915ad684393c"));
        const auto rec = callbook::parseHamQthSearch(kHamQthSearch);
        QVERIFY(rec);
        QCOMPARE(rec->call, QString("OK7AN"));
        QCOMPARE(rec->name, QString("Petr"));
        QCOMPARE(rec->qth, QString("Neratovice"));
        QCOMPARE(rec->dxcc, 503);
        QVERIFY(rec->lotw);
        QVERIFY(rec->imageUrl.startsWith("https://"));
    }

    void qrzLoginLookupCacheAndExpiredSession()
    {
        FakeHttp server;
        int lookups = 0;
        server.respond = [&](const QUrlQuery& q) -> QByteArray {
            if (q.hasQueryItem("username"))
                return q.queryItemValue("password") == "right" ? kQrzLogin : kQrzBadPassword;
            ++lookups;
            if (lookups == 2)
                return kQrzTimeout;     // la seconda ricerca trova la sessione scaduta
            return q.queryItemValue("callsign") == "XX9XX" ? kQrzNotFound : kQrzCallsign;
        };

        CallbookClient client;
        client.setEndpoints(server.url(), server.url());
        client.setProvider(CallbookClient::Provider::Qrz);
        client.setFallbackEnabled(false);   // qui si guarda un callbook solo
        giveCredentials(client, "right");
        QSignalSpy found(&client, &CallbookClient::found);
        QSignalSpy failed(&client, &CallbookClient::failed);

        client.lookup("ea8oh");
        QVERIFY(found.wait(5000));
        QCOMPARE(found.first().at(0).toString(), QString("EA8OH"));
        QCOMPARE(server.requests.size(), 2);                         // login + ricerca
        QCOMPARE(server.requests.at(0).queryItemValue("username"), QString("IU8LMC"));
        QVERIFY(server.requests.at(0).queryItemValue("agent").startsWith("DecoDXLog"));

        // Dalla cache: nessuna richiesta in piu', risposta immediata.
        client.lookup("EA8OH");
        QCOMPARE(found.size(), 2);
        QCOMPARE(server.requests.size(), 2);

        // Sessione scaduta: nuovo login e secondo tentativo, trasparenti.
        client.lookup("K1ABC");
        QTRY_COMPARE_WITH_TIMEOUT(found.size(), 3, 5000);
        QCOMPARE(server.requests.size(), 5);                         // ricerca, login, ricerca
        QVERIFY(server.requests.at(3).hasQueryItem("username"));

        client.lookup("XX9XX");
        QVERIFY(failed.wait(5000));
        QVERIFY(failed.first().at(1).toString().contains("Not found"));
        // Il "non trovato" non si richiede subito di nuovo.
        const qsizetype before = server.requests.size();
        client.lookup("XX9XX");
        QCOMPARE(failed.size(), 2);
        QCOMPARE(server.requests.size(), before);
    }

    void wrongPasswordAndMissingCredentials()
    {
        FakeHttp server;
        server.respond = [](const QUrlQuery&) { return kQrzBadPassword; };
        CallbookClient client;
        client.setEndpoints(server.url(), server.url());
        client.setProvider(CallbookClient::Provider::Qrz);
        client.setFallbackEnabled(false);
        giveCredentials(client, "wrong");
        QSignalSpy failed(&client, &CallbookClient::failed);
        client.lookup("EA8OH");
        QVERIFY(failed.wait(5000));
        QVERIFY(failed.first().at(1).toString().contains("incorrect"));

        CallbookClient noCredentials;
        noCredentials.setProvider(CallbookClient::Provider::HamQth);
        QSignalSpy failed2(&noCredentials, &CallbookClient::failed);
        noCredentials.lookup("OK7AN");
        QCOMPARE(failed2.size(), 1);
        QVERIFY(failed2.first().at(1).toString().contains("no credentials"));
    }

    void hamQthFlow()
    {
        FakeHttp server;
        server.respond = [](const QUrlQuery& q) { return q.hasQueryItem("u") ? kHamQthLogin : kHamQthSearch; };
        CallbookClient client;
        client.setEndpoints(server.url(), server.url());
        client.setProvider(CallbookClient::Provider::HamQth);
        giveCredentials(client, "pw");
        QSignalSpy found(&client, &CallbookClient::found);
        client.lookup("OK7AN");
        QVERIFY(found.wait(5000));
        QCOMPARE(server.requests.at(1).queryItemValue("id"), QString("09b0ae90050be03c452ad235a1f2915ad684393c"));
        QCOMPARE(server.requests.at(1).queryItemValue("prg"), QString("DecoDXLog"));
    }

    // ── Il QSO che si completa ───────────────────────────────────────────────

    void theCallbookFillsWhatTheQsoDoesNotKnow()
    {
        // Da Decodium arriva l'essenziale: nominativo, rapporto, banda, modo.
        AdifRecord qso;
        qso.set(QStringLiteral("CALL"), QStringLiteral("DL9ZZT"));
        qso.set(QStringLiteral("BAND"), QStringLiteral("20m"));
        qso.set(QStringLiteral("MODE"), QStringLiteral("MFSK"));
        qso.set(QStringLiteral("RST_SENT"), QStringLiteral("599"));

        CallbookRecord found;
        found.name = QStringLiteral("Klaus Müller");
        found.qth = QStringLiteral("Dresden");
        found.grid = QStringLiteral("JO61VB");
        found.address = QStringLiteral("Hauptstrasse 1");
        found.state = QStringLiteral("SN");
        found.country = QStringLiteral("Germany");
        found.cqZone = 14;
        found.ituZone = 28;
        found.dxcc = 230;

        const QStringList filled = callbook::fillMissing(qso, found);
        QVERIFY(filled.contains(QStringLiteral("NAME")));
        QVERIFY(filled.contains(QStringLiteral("GRIDSQUARE")));
        QVERIFY(filled.contains(QStringLiteral("ADDRESS")));
        QCOMPARE(qso.value(QStringLiteral("NAME")), QStringLiteral("Klaus Müller"));
        QCOMPARE(qso.value(QStringLiteral("QTH")), QStringLiteral("Dresden"));
        QCOMPARE(qso.value(QStringLiteral("GRIDSQUARE")), QStringLiteral("JO61VB"));
        QCOMPARE(qso.value(QStringLiteral("STATE")), QStringLiteral("SN"));
        QCOMPARE(qso.value(QStringLiteral("CQZ")), QStringLiteral("14"));
        QCOMPARE(qso.value(QStringLiteral("DXCC")), QStringLiteral("230"));
        // Quello che c'era resta com'era.
        QCOMPARE(qso.value(QStringLiteral("RST_SENT")), QStringLiteral("599"));
    }

    void whatTheOperatorWroteIsNeverTouched()
    {
        // Il locatore l'ha sentito in aria, il nome glielo ha detto lui: valgono
        // piu' di quello che dice una scheda su Internet.
        AdifRecord qso;
        qso.set(QStringLiteral("CALL"), QStringLiteral("DL9ZZT"));
        qso.set(QStringLiteral("NAME"), QStringLiteral("Klaus"));
        qso.set(QStringLiteral("GRIDSQUARE"), QStringLiteral("JO62"));
        qso.set(QStringLiteral("DXCC"), QStringLiteral("230"));
        qso.set(QStringLiteral("CQZ"), QStringLiteral("14"));

        CallbookRecord found;
        found.name = QStringLiteral("Klaus-Dieter");
        found.grid = QStringLiteral("JO61VB");
        found.dxcc = 1;                 // una scheda personale puo' sbagliare
        found.cqZone = 40;
        found.qth = QStringLiteral("Dresden");

        const QStringList filled = callbook::fillMissing(qso, found);
        QCOMPARE(qso.value(QStringLiteral("NAME")), QStringLiteral("Klaus"));
        QCOMPARE(qso.value(QStringLiteral("GRIDSQUARE")), QStringLiteral("JO62"));
        QCOMPARE(qso.value(QStringLiteral("DXCC")), QStringLiteral("230"));
        QCOMPARE(qso.value(QStringLiteral("CQZ")), QStringLiteral("14"));
        // Solo il QTH mancava.
        QCOMPARE(filled, QStringList{QStringLiteral("QTH")});
    }

    void nothingToSayNothingWritten()
    {
        AdifRecord qso;
        qso.set(QStringLiteral("CALL"), QStringLiteral("DL9ZZT"));

        CallbookRecord empty;
        QVERIFY(callbook::fillMissing(qso, empty).isEmpty());

        // Spazi e basta non sono un nome.
        CallbookRecord blank;
        blank.name = QStringLiteral("   ");
        QVERIFY(callbook::fillMissing(qso, blank).isEmpty());
        QVERIFY(qso.value(QStringLiteral("NAME")).isEmpty());
    }

    void theLocatorComesFromThePositionWhenTheCallbookDoesNotWriteIt()
    {
        // HamQTH e QRZ non sempre scrivono il locatore, ma quasi sempre dicono
        // dove sta la stazione: il quadrato si ricava da li'.
        AdifRecord qso;
        qso.set(QStringLiteral("CALL"), QStringLiteral("DL9ZZT"));

        CallbookRecord found;
        found.name = QStringLiteral("Klaus");
        found.lat = 51.05;
        found.lon = 13.74;              // Dresda
        found.hasPosition = true;

        const QStringList filled = callbook::fillMissing(qso, found);
        QVERIFY(filled.contains(QStringLiteral("GRIDSQUARE")));
        QCOMPARE(qso.value(QStringLiteral("GRIDSQUARE")), QStringLiteral("JO61UB"));

        // Senza posizione non ci si inventa niente.
        AdifRecord other;
        other.set(QStringLiteral("CALL"), QStringLiteral("DL9ZZT"));
        CallbookRecord blind;
        blind.name = QStringLiteral("Klaus");
        QVERIFY(!callbook::fillMissing(other, blind).contains(QStringLiteral("GRIDSQUARE")));
    }

    void aCoarseLocatorBecomesThePreciseOne()
    {
        // Dalla FT8 arrivano quattro caratteri; il callbook ne sa sei. E' lo
        // stesso quadrato detto meglio, quindi si tiene il piu' preciso.
        AdifRecord qso;
        qso.set(QStringLiteral("CALL"), QStringLiteral("DL9ZZT"));
        qso.set(QStringLiteral("GRIDSQUARE"), QStringLiteral("JO61"));

        CallbookRecord found;
        found.grid = QStringLiteral("JO61VB");

        const QStringList filled = callbook::fillMissing(qso, found);
        QVERIFY(filled.contains(QStringLiteral("GRIDSQUARE")));
        QCOMPARE(qso.value(QStringLiteral("GRIDSQUARE")), QStringLiteral("JO61VB"));

        // Un quadrato diverso pero' non si tocca: quello l'ha sentito la radio.
        AdifRecord heard;
        heard.set(QStringLiteral("CALL"), QStringLiteral("DL9ZZT"));
        heard.set(QStringLiteral("GRIDSQUARE"), QStringLiteral("JO62"));
        QVERIFY(!callbook::fillMissing(heard, found).contains(QStringLiteral("GRIDSQUARE")));
        QCOMPARE(heard.value(QStringLiteral("GRIDSQUARE")), QStringLiteral("JO62"));
    }

    void whenTheFirstCallbookDoesNotKnowTheOtherIsAsked()
    {
        // HamQTH e' quello scelto e non conosce la stazione; QRZ si'. La risposta
        // arriva lo stesso, e dice da dove viene.
        FakeHttp server;
        server.respond = [](const QUrlQuery& q) -> QByteArray {
            if (q.hasQueryItem("u"))        return kHamQthLogin;
            if (q.hasQueryItem("username")) return kQrzLogin;
            if (q.hasQueryItem("prg"))      return kHamQthNotFound;
            return kQrzCallsign;
        };

        CallbookClient client;
        client.setEndpoints(server.url(), server.url());
        client.setProvider(CallbookClient::Provider::HamQth);
        giveCredentials(client, "right");
        QSignalSpy found(&client, &CallbookClient::found);
        QSignalSpy failed(&client, &CallbookClient::failed);
        QSignalSpy providerResults(&client, &CallbookClient::providerResult);

        client.lookup("EA8OH");
        QVERIFY(found.wait(5000));
        QCOMPARE(failed.size(), 0);
        const auto record = found.first().at(1).value<CallbookRecord>();
        QCOMPARE(record.call, QStringLiteral("EA8OH"));
        QCOMPARE(record.source, QStringLiteral("QRZ.com"));
        QCOMPARE(record.grid, QStringLiteral("IL18QI"));
        QCOMPARE(providerResults.size(), 2);
        QCOMPARE(providerResults.at(0).at(0).toString(), QStringLiteral("EA8OH"));
        QCOMPARE(providerResults.at(0).at(1).toString(), QStringLiteral("hamqth"));
        QVERIFY(providerResults.at(0).at(2).toString().isEmpty());
        QCOMPARE(providerResults.at(1).at(1).toString(), QStringLiteral("qrz"));
        QCOMPARE(providerResults.at(1).at(2).toString(), QStringLiteral("EA8OH"));

        // Spento il ripiego, resta il "non lo so" del primo.
        CallbookClient alone;
        alone.setEndpoints(server.url(), server.url());
        alone.setProvider(CallbookClient::Provider::HamQth);
        alone.setFallbackEnabled(false);
        giveCredentials(alone, "right");
        QSignalSpy failedAlone(&alone, &CallbookClient::failed);
        alone.lookup("EA8OH");
        QVERIFY(failedAlone.wait(5000));
        QVERIFY(failedAlone.first().at(1).toString().contains(QStringLiteral("HamQTH")));
    }

    void withoutTheOtherCredentialsThereIsNoFallback()
    {
        FakeHttp server;
        server.respond = [](const QUrlQuery& q) -> QByteArray {
            if (q.hasQueryItem("u")) return kHamQthLogin;
            if (q.hasQueryItem("prg")) return kHamQthNotFound;
            return kQrzLogin;
        };
        CallbookClient client;
        client.setEndpoints(server.url(), server.url());
        client.setProvider(CallbookClient::Provider::HamQth);
        // Solo HamQTH ha un utente: a QRZ non si bussa nemmeno.
        client.setCredentialReaders(
            [](const QString& service) { return service == QLatin1String("hamqth") ? QStringLiteral("IU8LMC") : QString(); },
            [](const QString&, std::function<void(const QString&, const QString&)> done) { done(QStringLiteral("right"), {}); });
        QSignalSpy failed(&client, &CallbookClient::failed);
        client.lookup("EA8OH");
        QVERIFY(failed.wait(5000));
        for (const QUrlQuery& q : server.requests)
            QVERIFY(!q.hasQueryItem("username"));
    }

    void aCallbookWithoutTheGridAsksTheOtherOne()
    {
        // HamQTH sa nome e citta' ma non il quadrato e nemmeno dove sta: QRZ
        // sa il quadrato. Le due risposte si mettono insieme.
        FakeHttp server;
        server.respond = [](const QUrlQuery& q) -> QByteArray {
            if (q.hasQueryItem("u"))        return kHamQthLogin;
            if (q.hasQueryItem("username")) return kQrzLogin;
            if (q.hasQueryItem("prg"))      return kHamQthNoGrid;
            return kQrzCallsign;
        };

        CallbookClient client;
        client.setEndpoints(server.url(), server.url());
        client.setProvider(CallbookClient::Provider::HamQth);
        giveCredentials(client, "right");
        QSignalSpy found(&client, &CallbookClient::found);

        client.lookup("EA8OH");
        QVERIFY(found.wait(5000));
        const auto record = found.first().at(1).value<CallbookRecord>();
        // Il nome resta quello del primo, il quadrato arriva dal secondo.
        QCOMPARE(record.name, QStringLiteral("Pekka"));
        QCOMPARE(record.qth, QStringLiteral("Las Palmas"));
        QCOMPARE(record.grid, QStringLiteral("IL18QI"));
        QVERIFY(record.source.contains(QStringLiteral("HamQTH")));
        QVERIFY(record.source.contains(QStringLiteral("QRZ")));

        // E se il secondo non risponde, vale lo stesso quello che sa il primo.
        FakeHttp mute;
        mute.respond = [](const QUrlQuery& q) -> QByteArray {
            if (q.hasQueryItem("u"))   return kHamQthLogin;
            if (q.hasQueryItem("prg")) return kHamQthNoGrid;
            return kQrzBadPassword;         // QRZ non ci fa entrare
        };
        CallbookClient half;
        half.setEndpoints(mute.url(), mute.url());
        half.setProvider(CallbookClient::Provider::HamQth);
        giveCredentials(half, "right");
        QSignalSpy foundHalf(&half, &CallbookClient::found);
        QSignalSpy failedHalf(&half, &CallbookClient::failed);
        half.lookup("EA8OH");
        QVERIFY(foundHalf.wait(5000));
        QCOMPARE(failedHalf.size(), 0);
        QCOMPARE(foundHalf.first().at(1).value<CallbookRecord>().name, QStringLiteral("Pekka"));
    }
};

QTEST_GUILESS_MAIN(TestCallbook)
#include "tst_callbook.moc"
