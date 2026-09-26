// LoTW: lettura di lotwreport.adi, abbinamento delle conferme ai QSO del log e
// download contro un server HTTP finto (credenziali mai negli errori).
#include "core/LogDatabase.h"
#include "core/Lotw.h"

#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUrlQuery>

using namespace decolog::core;

namespace {

const QByteArray kReport = R"(ARRL Logbook of the World Status Report
Generated at 2026-09-17 12:00:00
for iu8lmc
Query:
    QSL ONLY: YES
QSL RX SINCE: 2026-01-01 00:00:00

<PROGRAMID:4>LoTW
<APP_LoTW_LASTQSL:19>2026-09-16 21:04:11

<APP_LoTW_NUMREC:1>3

<eoh>

<APP_LoTW_OWNCALL:6>IU8LMC
<STATION_CALLSIGN:6>IU8LMC
<CALL:5>K1ABC
<BAND:3>20M
<FREQ:8>14.08000
<MODE:4>MFSK
<SUBMODE:3>FT2
<APP_LoTW_MODEGROUP:4>DATA
<QSO_DATE:8>20260910
<TIME_ON:6>120300
<QSL_RCVD:1>Y
<QSLRDATE:8>20260916
<DXCC:3>291
<COUNTRY:24>UNITED STATES OF AMERICA
<GRIDSQUARE:6>FN42AB
<STATE:2>MA
<CQZ:2>05
<ITUZ:2>08
<eor>

<APP_LoTW_OWNCALL:6>IU8LMC
<CALL:5>JA1XX
<BAND:3>40M
<MODE:3>FT8
<QSO_DATE:8>20260911
<TIME_ON:4>0715
<QSL_RCVD:1>Y
<QSLRDATE:8>20260915
<DXCC:3>339
<eor>

<APP_LoTW_OWNCALL:6>IU8LMC
<CALL:5>ZZ9ZZ
<BAND:3>15M
<MODE:2>CW
<QSO_DATE:8>20260912
<TIME_ON:6>101000
<QSL_RCVD:1>Y
<QSLRDATE:8>20260914
<eor>
)";

AdifRecord qso(const char* call, const char* band, const char* mode, const char* submode, const char* date,
               const char* time, const char* grid = "")
{
    return AdifRecord{{"CALL", call}, {"BAND", band}, {"MODE", mode}, {"SUBMODE", submode},
                      {"QSO_DATE", date}, {"TIME_ON", time}, {"GRIDSQUARE", grid}};
}

class FakeLotw : public QTcpServer {
public:
    QByteArray body;
    int status{200};
    QList<QUrlQuery> requests;

    FakeLotw()
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* s = nextPendingConnection()) {
                connect(s, &QTcpSocket::readyRead, s, [this, s] {
                    const QByteArray head = s->readAll();
                    const QByteArray line = head.left(head.indexOf("\r\n"));
                    requests << QUrlQuery(QUrl(QString::fromLatin1(line.split(' ').value(1))));
                    s->write("HTTP/1.1 " + QByteArray::number(status) + (status == 200 ? " OK" : " Error")
                             + "\r\nContent-Type: text/plain\r\nConnection: close\r\nContent-Length: "
                             + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                    s->disconnectFromHost();
                });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
        listen(QHostAddress::LocalHost);
    }
    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/lotwuser/lotwreport.adi").arg(serverPort())); }
};

} // namespace

class TestLotw : public QObject {
    Q_OBJECT

private slots:
    void modeGroups()
    {
        QCOMPARE(adif::modeGroup("MFSK", "FT2"), QString("DATA"));
        QCOMPARE(adif::modeGroup("FT8"), QString("DATA"));
        QCOMPARE(adif::modeGroup("CW"), QString("CW"));
        QCOMPARE(adif::modeGroup("SSB", "USB"), QString("PHONE"));
        QCOMPARE(adif::modeGroup("FM"), QString("PHONE"));
        QCOMPARE(adif::modeGroup("SSTV"), QString("IMAGE"));
    }

    void parseReport()
    {
        const auto r = lotw::parseReport(kReport);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.lastQsl, QString("2026-09-16 21:04:11"));
        QCOMPARE(r.declaredRecords, 3);
        QCOMPARE(r.confirmations.size(), 3);
        QCOMPARE(r.confirmations.at(0).value("CALL"), QString("K1ABC"));
        QCOMPARE(r.confirmations.at(0).value("QSLRDATE"), QString("20260916"));
    }

    void parseLoginPage()
    {
        const auto r = lotw::parseReport(
            "<html><body><h1>Logbook of the World</h1><p>Username/password incorrect</p></body></html>");
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains("password"));
    }

    void applyConfirmations()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        // Stessa banda e ora vicina, ma un altro QSO in CW nella stessa finestra:
        // deve vincere quello del gruppo DATA.
        const qint64 k1 = db.insertQso(qso("K1ABC", "20m", "MFSK", "FT2", "20260910", "121500", "FN42"), "udp_decodium").id;
        const qint64 k1cw = db.insertQso(qso("K1ABC", "20m", "CW", "", "20260910", "120300"), "manual").id;
        // FT4 nel log, FT8 in LoTW: stesso gruppo, conferma valida.
        const qint64 ja = db.insertQso(qso("JA1XX", "40m", "MFSK", "FT4", "20260911", "071000"), "import").id;
        QVERIFY(k1 > 0 && k1cw > 0 && ja > 0);

        const auto report = lotw::parseReport(kReport);

        auto res = db.applyConfirmation("lotw", report.confirmations.at(0));
        QCOMPARE(res.status, ConfirmationResult::Status::Confirmed);
        QCOMPARE(res.id, k1);
        const auto rec = db.record(k1);
        QCOMPARE(rec->value("LOTW_QSL_RCVD"), QString("Y"));
        QCOMPARE(rec->value("LOTW_QSLRDATE"), QString("20260916"));
        QCOMPARE(rec->value("LOTW_QSL_SENT"), QString("Y"));
        // Il locatore si allunga (stesso quadrato), i dettagli vuoti si riempiono.
        QCOMPARE(rec->value("GRIDSQUARE"), QString("FN42AB"));
        QCOMPARE(rec->value("DXCC"), QString("291"));
        QCOMPARE(rec->value("STATE"), QString("MA"));
        // Nessun campo della cartolina: QSL_RCVD in LoTW e' la conferma LoTW.
        QVERIFY(rec->value("QSL_RCVD").isEmpty());
        QCOMPARE(db.meta(k1)->revision, 2);
        QCOMPARE(db.history(k1).first().reason, QString("lotw"));
        QVERIFY(db.record(k1cw)->value("LOTW_QSL_RCVD").isEmpty());

        // Di nuovo la stessa conferma: nessuna nuova revisione.
        res = db.applyConfirmation("lotw", report.confirmations.at(0));
        QCOMPARE(res.status, ConfirmationResult::Status::AlreadyConfirmed);
        QCOMPARE(db.meta(k1)->revision, 2);

        res = db.applyConfirmation("lotw", report.confirmations.at(1));
        QCOMPARE(res.status, ConfirmationResult::Status::Confirmed);
        QCOMPARE(res.id, ja);

        res = db.applyConfirmation("lotw", report.confirmations.at(2));
        QCOMPARE(res.status, ConfirmationResult::Status::NotFound);

        // Fuori dalla mezz'ora: non e' lo stesso QSO.
        AdifRecord late = report.confirmations.at(0);
        late.set("TIME_ON", "140000");
        QCOMPARE(db.applyConfirmation("lotw", late).status, ConfirmationResult::Status::NotFound);

        QCOMPARE(db.ft2Award().dxccConfirmed, 1);
    }

    void download()
    {
        FakeLotw server;
        server.body = kReport;
        LotwClient client;
        client.setEndpoint(server.url());
        QSignalSpy done(&client, &LotwClient::finished);
        client.download("IU8LMC", "s3cret&pw", "2026-09-01 00:00:00");
        QVERIFY(done.wait(5000));
        const auto report = done.at(0).at(0).value<lotw::Report>();
        QVERIFY2(report.ok, qPrintable(report.error));
        QCOMPARE(report.confirmations.size(), 3);
        QCOMPARE(server.requests.size(), 1);
        const QUrlQuery q = server.requests.first();
        QCOMPARE(q.queryItemValue("login"), QString("IU8LMC"));
        QCOMPARE(q.queryItemValue("password", QUrl::FullyDecoded), QString("s3cret&pw"));
        QCOMPARE(q.queryItemValue("qso_qsl"), QString("yes"));
        QCOMPARE(q.queryItemValue("qso_qslsince", QUrl::FullyDecoded), QString("2026-09-01 00:00:00"));
    }

    void downloadForAPeriod()
    {
        // Il "dal … al …": sono le date dei QSO, e senza segno dell'ultimo scarico.
        FakeLotw server;
        server.body = kReport;
        LotwClient client;
        client.setEndpoint(server.url());
        QSignalSpy done(&client, &LotwClient::finished);
        client.download("IU8LMC", "pw", {}, QDate(2026, 1, 1), QDate(2026, 3, 31));
        QVERIFY(done.wait(5000));
        const QUrlQuery q = server.requests.first();
        QCOMPARE(q.queryItemValue("qso_startdate"), QString("2026-01-01"));
        QCOMPARE(q.queryItemValue("qso_enddate"), QString("2026-03-31"));
        QVERIFY(!q.hasQueryItem("qso_qslsince"));

        // Solo l'inizio: fino a oggi.
        client.download("IU8LMC", "pw", {}, QDate(2025, 6, 1), {});
        QVERIFY(done.wait(5000));
        QCOMPARE(server.requests.last().queryItemValue("qso_startdate"), QString("2025-06-01"));
        QVERIFY(!server.requests.last().hasQueryItem("qso_enddate"));
    }

    void errorsHideThePassword()
    {
        FakeLotw server;
        server.status = 503;
        server.body = "busy";
        LotwClient client;
        client.setEndpoint(server.url());
        QSignalSpy done(&client, &LotwClient::finished);
        client.download("IU8LMC", "s3cretpw", {});
        QVERIFY(done.wait(5000));
        const auto report = done.at(0).at(0).value<lotw::Report>();
        QVERIFY(!report.ok);
        QVERIFY(!report.error.isEmpty());
        QVERIFY2(!report.error.contains("s3cretpw"), qPrintable(report.error));
        QVERIFY2(!report.error.contains("password"), qPrintable(report.error));
    }
};

QTEST_GUILESS_MAIN(TestLotw)
#include "tst_lotw.moc"
