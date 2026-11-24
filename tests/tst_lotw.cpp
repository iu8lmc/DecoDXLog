// LoTW: lettura di lotwreport.adi, abbinamento delle conferme ai QSO del log e
// download contro un server HTTP finto (credenziali mai negli errori).
#include "core/LogDatabase.h"
#include "core/Lotw.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QSqlQuery>
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
    void missingCredentialsNeverOpenConnection()
    {
        FakeLotw server;
        LotwClient client;
        client.setEndpoint(server.url());
        QSignalSpy done(&client, &LotwClient::finished);
        client.download(QStringLiteral("K1ABC"), QStringLiteral(" "), {});
        client.download(QStringLiteral(" "), QStringLiteral("secret"), {});
        QCOMPARE(done.size(), 2);
        QTest::qWait(100);
        QVERIFY(server.requests.isEmpty());
    }

    void modeGroups()
    {
        QCOMPARE(adif::modeGroup("MFSK", "FT2"), QString("DATA"));
        QCOMPARE(adif::modeGroup("FT8"), QString("DATA"));
        QCOMPARE(adif::modeGroup("CW"), QString("CW"));
        QCOMPARE(adif::modeGroup("CWR"), QString("CW"));
        QCOMPARE(adif::modeGroup("CW-R"), QString("CW"));
        QCOMPARE(adif::modeGroup("SSB", "USB"), QString("PHONE"));
        QCOMPARE(adif::modeGroup("FM"), QString("PHONE"));
        QCOMPARE(adif::modeGroup("DMR"), QString("PHONE"));
        QCOMPARE(adif::modeGroup("C4FM"), QString("PHONE"));
        QCOMPARE(adif::modeGroup("SSTV"), QString("IMAGE"));
        QCOMPARE(adif::modeGroup("UNKNOWN-MODE"), QString("DATA"));
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

    void rejectsIncompleteReportCount()
    {
        QByteArray truncated=kReport.left(kReport.indexOf("<APP_LoTW_OWNCALL:6>IU8LMC", kReport.indexOf("<eoh>")+5));
        const auto incomplete=lotw::parseReport(truncated);
        QVERIFY(!incomplete.ok);
        QVERIFY(incomplete.confirmations.isEmpty());
        QVERIFY(!incomplete.error.isEmpty());
        const auto complete=lotw::parseReport(kReport);
        QVERIFY(complete.ok);
        QCOMPARE(complete.confirmations.size(),3);
        const auto eof=lotw::parseReport(kReport+"\n<APP_LoTW_EOF:1>Y");
        QVERIFY(eof.ok);
        QCOMPARE(eof.confirmations.size(),3);
        QByteArray wrongCount=kReport; wrongCount.replace("<APP_LoTW_NUMREC:1>3", "<APP_LoTW_NUMREC:1>4");
        QVERIFY(!lotw::parseReport(wrongCount).ok);
    }

    void rejectsIncompleteFinalRecord()
    {
        QByteArray partial=kReport;
        const auto lastEor=partial.lastIndexOf("<eor>"); QVERIFY(lastEor>0);
        partial.truncate(lastEor);
        const auto permissive=adif::parse(partial);
        QCOMPARE(permissive.records.size(),3);
        QVERIFY(permissive.unterminatedRecord);
        QVERIFY(!lotw::parseReport(partial).ok);
        QByteArray brokenTag=kReport+"\n<CALL:5";
        QVERIFY(!lotw::parseReport(brokenTag).ok);
        QVERIFY(lotw::parseReport(kReport).ok);
        QVERIFY(lotw::parseReport(kReport+"\n<APP_LoTW_EOF:1>Y").ok);
    }

    void rejectsMalformedFieldLength()
    {
        QByteArray malformed=kReport;
        malformed.replace("<QSL_RCVD:1>Y", "<QSL_RCVD:x>Y");
        const auto general=adif::parse(malformed);
        QCOMPARE(general.records.size(),3);
        QVERIFY(general.invalidFieldLength);
        QVERIFY(!lotw::parseReport(malformed).ok);
        malformed=kReport; malformed.replace("<QSL_RCVD:1>Y", "<QSL_RCVD:-1>Y");
        QVERIFY(!lotw::parseReport(malformed).ok);
        QVERIFY(lotw::parseReport(kReport).ok);
    }

    void parseMixedCaseHeaderAndPreserveConfirmationGrid()
    {
        QByteArray mixed=kReport; mixed.replace("<eoh>","<eOh>");
        const auto report=lotw::parseReport(mixed);
        QVERIFY2(report.ok,qPrintable(report.error)); QCOMPARE(report.confirmations.size(),3);
        QCOMPARE(report.confirmations.first().value("GRIDSQUARE"),QString("FN42AB"));
        QCOMPARE(report.lastQsl,QString("2026-09-16 21:04:11"));
    }

    void matchedConfirmationPersistsGridSeparatelyAndAtomically()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        const qint64 id=db.insertQso(qso("K1ABC","20m","CW","","20260910","120000","FN41"),"fixture").id; QVERIFY(id>0);
        auto incoming=qso("K1ABC","20m","CW","","20260910","120000","FN42AB"); incoming.set("VUCC_GRIDS","FN42,FN43");
        QSqlQuery query(db.connection()); QVERIFY(query.exec("CREATE TRIGGER reject_observation BEFORE INSERT ON lotw_grid_observation BEGIN SELECT RAISE(ABORT,'injected'); END"));
        QCOMPARE(db.applyConfirmation("lotw",incoming,1800,{}, {},true).status,ConfirmationResult::Status::Error);
        QCOMPARE(db.record(id)->value("LOTW_QSL_RCVD"),QString()); QCOMPARE(db.record(id)->value("GRIDSQUARE"),QString("FN41"));
        QVERIFY(query.exec("DROP TRIGGER reject_observation"));
        QCOMPARE(db.applyConfirmation("lotw",incoming,1800,{}, {},true).status,ConfirmationResult::Status::Confirmed);
        QCOMPARE(db.record(id)->value("GRIDSQUARE"),QString("FN41"));
        QVERIFY(query.exec("SELECT grid,vucc_grids FROM lotw_grid_observation")); QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(),QString("FN42AB")); QCOMPARE(query.value(1).toString(),QString("FN42,FN43")); QVERIFY(!query.next());
        QCOMPARE(db.applyConfirmation("lotw",incoming,1800,{}, {},true).status,ConfirmationResult::Status::AlreadyConfirmed);
        QVERIFY(query.exec("SELECT COUNT(*) FROM lotw_grid_observation")); QVERIFY(query.next()); QCOMPARE(query.value(0).toInt(),1);
        incoming.set("GRIDSQUARE","FN44"); incoming.remove("VUCC_GRIDS");
        QCOMPARE(db.applyConfirmation("lotw",incoming,1800,{}, {},true).status,ConfirmationResult::Status::AlreadyConfirmed);
        QVERIFY(query.exec("SELECT COUNT(*) FROM lotw_grid_observation")); QVERIFY(query.next()); QCOMPARE(query.value(0).toInt(),2);
        QVERIFY(db.connection().transaction()); incoming.set("GRIDSQUARE","FN45");
        QCOMPARE(db.applyConfirmation("lotw",incoming,1800,{}, {},true).status,ConfirmationResult::Status::AlreadyConfirmed);
        QVERIFY(db.connection().rollback());
        QVERIFY(query.exec("SELECT COUNT(*) FROM lotw_grid_observation")); QVERIFY(query.next()); QCOMPARE(query.value(0).toInt(),2);
    }

    void retainsDistinctMatchContextsForSameGrid()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        const auto id=db.insertQso(qso("K1ABC","20m","CW","","20260910","120000","FN41"),"fixture").id;
        QVERIFY(id>0);
        auto report=qso("K1ABC","20m","CW","","20260910","120100","FN42");
        report.set("STATION_CALLSIGN","IU8LMC"); report.set("APP_LOTW_RXQSL","2026-09-16 21:04:11");
        QCOMPARE(db.applyConfirmation("lotw",report,1800,{}, {},true).status,ConfirmationResult::Status::Confirmed);
        QCOMPARE(db.applyConfirmation("lotw",report,1800,{}, {},true).status,ConfirmationResult::Status::AlreadyConfirmed);
        QSqlQuery q(db.connection());
        QVERIFY(q.exec("SELECT distance_seconds,station_call,rxqsl FROM lotw_grid_match_context")); QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(),60); QCOMPARE(q.value(1).toString(),QString("IU8LMC"));
        QCOMPARE(q.value(2).toString(),QString("2026-09-16 21:04:11")); QVERIFY(!q.next());
        report.set("APP_LOTW_RXQSL","2026-09-17 21:04:11");
        QCOMPARE(db.applyConfirmation("lotw",report,1800,{}, {},true).status,ConfirmationResult::Status::AlreadyConfirmed);
        QVERIFY(q.exec("SELECT COUNT(*) FROM lotw_grid_match_context")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),2);
        QVERIFY(q.exec("SELECT COUNT(*) FROM lotw_grid_observation")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),1);
        const auto meta=db.meta(id); QVERIFY(meta);
        const auto rows=db.lotwGridObservationPage(id,meta->uuid,meta->revision).value("rows").toList();
        QCOMPARE(rows.size(),1); const auto row=rows.first().toMap();
        QCOMPARE(row.value("contextCount").toInt(),2); QCOMPARE(row.value("distanceSeconds").toInt(),60);
        QCOMPARE(row.value("rxqsl").toString(),QString("2026-09-17 21:04:11"));
        QVERIFY(q.exec("CREATE TRIGGER reject_context BEFORE INSERT ON lotw_grid_match_context BEGIN SELECT RAISE(ABORT,'injected'); END"));
        report.set("GRIDSQUARE","FN44");
        QCOMPARE(db.applyConfirmation("lotw",report,1800,{}, {},true).status,ConfirmationResult::Status::Error);
        QVERIFY(q.exec("SELECT COUNT(*) FROM lotw_grid_observation")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),1);
    }

    void delayedDownloadCanBeBoundToOriginalLog()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); FakeLotw server; server.body=kReport;
        LogDatabase db; QVERIFY(db.open(dir.filePath("one.sqlite")));
        QVERIFY(db.insertQso(qso("K1ABC","20m","MFSK","FT2","20260910","120300","FN41"),"fixture").id>0);
        const auto origin=db.sessionToken(); QVERIFY(origin);
        LotwClient client; client.setEndpoint(server.url()); QSignalSpy done(&client,&LotwClient::finished);
        client.download("fixture","fixture",{}); QVERIFY(done.wait(5000));
        const auto report=done.first().first().value<lotw::Report>(); QVERIFY(report.ok); QVERIFY(db.matchesSessionToken(*origin));
        QCOMPARE(server.requests.first().queryItemValue("qso_qslsince"),QString("1900-01-01"));
        const auto applyIfSame=[&] { if (!db.matchesSessionToken(*origin)) return false;
            return db.applyConfirmation("lotw",report.confirmations.first(),1800,{}, {},true).status==ConfirmationResult::Status::Confirmed; };
        QVERIFY(applyIfSame());
        QVERIFY(db.open(dir.filePath("two.sqlite")));
        QVERIFY(db.insertQso(qso("K1ABC","20m","MFSK","FT2","20260910","120300","FN40"),"fixture").id>0);
        QVERIFY(!db.matchesSessionToken(*origin));
        QVERIFY(!applyIfSame());
        QSqlQuery q(db.connection()); QVERIFY(q.exec("SELECT COUNT(*) FROM lotw_grid_observation")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),0);
        QVERIFY(db.record(1)->value("LOTW_QSL_RCVD").isEmpty());
    }

    void downloadedGridIsRetainedOnlyForUniqueMatch()
    {
        FakeLotw server; server.body=kReport; LotwClient client; client.setEndpoint(server.url());
        QSignalSpy done(&client,&LotwClient::finished); client.download("fixture","fixture",{}); QVERIFY(done.wait(5000));
        const auto report=done.first().first().value<lotw::Report>(); QVERIFY(report.ok);
        {
            LogDatabase baseline; QVERIFY(baseline.open(":memory:"));
            QVERIFY(baseline.insertQso(qso("K1ABC","20m","MFSK","FT2","20260910","120200","FN41"),"fixture").id>0);
            QCOMPARE(baseline.applyConfirmation("lotw",report.confirmations.first()).status,ConfirmationResult::Status::Confirmed);
            QSqlQuery count(baseline.connection()); QVERIFY(count.exec("SELECT COUNT(*) FROM lotw_grid_observation")); QVERIFY(count.next()); QCOMPARE(count.value(0).toInt(),0);
        }
        LogDatabase db; QVERIFY(db.open(":memory:"));
        const auto one=db.insertQso(qso("K1ABC","20m","MFSK","FT2","20260910","120200","FN41"),"fixture",{},true,0,false);
        const auto two=db.insertQso(qso("K1ABC","20m","MFSK","FT2","20260910","120400","FN40"),"fixture",{},true,0,false);
        QVERIFY(one.id>0 && two.id>0);
        QCOMPARE(db.applyConfirmation("lotw",report.confirmations.first(),1800,{}, {},true).status,ConfirmationResult::Status::Ambiguous);
        QSqlQuery q(db.connection()); QVERIFY(q.exec("SELECT COUNT(*) FROM lotw_grid_observation")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),0); q.finish();
        QVERIFY(db.softDeleteQso(two.id));
        QCOMPARE(db.applyConfirmation("lotw",report.confirmations.first(),1800,{}, {},true).status,ConfirmationResult::Status::Confirmed);
        QVERIFY(q.exec("SELECT qso_id,grid FROM lotw_grid_observation")); QVERIFY(q.next()); QCOMPARE(q.value(0).toLongLong(),one.id); QCOMPARE(q.value(1).toString(),QString("FN42AB")); QVERIFY(!q.next());
    }

    void lotwOwnCallRejectsExplicitlyDifferentStation()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        auto wrong=qso("K1ABC","20m","CW","","20260910","120000","FN41");
        wrong.set("STATION_CALLSIGN","N0OTHER");
        auto right=qso("K1ABC","20m","CW","","20260910","120100","FN41");
        right.set("STATION_CALLSIGN","IU8LMC");
        const auto a=db.insertQso(wrong,"fixture",{},true,0,false);
        const auto b=db.insertQso(right,"fixture",{},true,0,false);
        QVERIFY(a.id>0 && b.id>0);
        auto incoming=qso("K1ABC","20m","CW","","20260910","120000","FN42");
        incoming.set("STATION_CALLSIGN","iu8lmc");
        const auto result=db.applyConfirmation("lotw",incoming,1800,{}, {},true);
        QCOMPARE(result.status,ConfirmationResult::Status::Confirmed);
        QCOMPARE(result.id,b.id);
        QVERIFY(db.record(a.id)->value("LOTW_QSL_RCVD").isEmpty());
        QCOMPARE(db.record(b.id)->value("LOTW_QSL_RCVD"),QString("Y"));
        LogDatabase other; QVERIFY(other.open(":memory:"));
        const auto only=other.insertQso(wrong,"fixture"); QVERIFY(only.id>0);
        QCOMPARE(other.applyConfirmation("lotw",incoming).status,ConfirmationResult::Status::NotFound);
        QVERIFY(other.record(only.id)->value("LOTW_QSL_RCVD").isEmpty());
        incoming.remove("STATION_CALLSIGN"); incoming.set("APP_LOTW_OWNCALL","iu8lmc");
        QCOMPARE(other.applyConfirmation("lotw",incoming).status,ConfirmationResult::Status::NotFound);
        incoming.remove("APP_LOTW_OWNCALL");
        QCOMPARE(other.applyConfirmation("lotw",incoming).status,ConfirmationResult::Status::Confirmed);
        LogDatabase unknown; QVERIFY(unknown.open(":memory:"));
        wrong.remove("STATION_CALLSIGN"); QVERIFY(unknown.insertQso(wrong,"fixture").id>0);
        incoming.set("STATION_CALLSIGN","IU8LMC");
        QCOMPARE(unknown.applyConfirmation("lotw",incoming).status,ConfirmationResult::Status::Confirmed);
    }

    void gridObservationHistoryPagesAndRejectsChangedQso()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); LogDatabase db; QVERIFY(db.open(dir.filePath("history.sqlite")));
        const qint64 id=db.insertQso(qso("K1ABC","20m","CW","","20260910","120000","FN41"),"fixture").id; QVERIFY(id>0);
        QString error; for (int i=0;i<55;++i) QVERIFY(db.recordLotwGridObservation(id,QString("FN%1").arg(i,2,10,QLatin1Char('0')),QStringLiteral(""),&error));
        const auto meta=db.meta(id); QVERIFY(meta);
        const auto first=db.lotwGridObservationPage(id,meta->uuid,meta->revision,0);
        QVERIFY2(first.value("error").toString().isEmpty(),qPrintable(first.value("error").toString()));
        QCOMPARE(first.value("rows").toList().size(),50); QVERIFY(first.value("hasMore").toBool());
        QCOMPARE(first.value("rows").toList().first().toMap().value("grid").toString(),QString("FN54"));
        const auto second=db.lotwGridObservationPage(id,meta->uuid,meta->revision,first.value("nextBeforeId").toLongLong());
        QCOMPARE(second.value("rows").toList().size(),5); QVERIFY(!second.value("hasMore").toBool());
        QCOMPARE(second.value("rows").toList().first().toMap().value("grid").toString(),QString("FN04"));
        QCOMPARE(second.value("rows").toList().last().toMap().value("grid").toString(),QString("FN00"));
        auto edited=*db.record(id); edited.set("COMMENT","changed"); QCOMPARE(db.updateQso(id,edited).status,InsertResult::Status::Inserted);
        QVERIFY(!db.lotwGridObservationPage(id,meta->uuid,meta->revision,0).value("error").toString().isEmpty());
        QVERIFY(!db.lotwGridObservationPage(id,"wrong-uuid",meta->revision,0).value("error").toString().isEmpty());
    }

    void storesSeparateGridObservationsIdempotently()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const QString path=dir.filePath("log.sqlite"); qint64 id=0;
        {
            LogDatabase db; QVERIFY(db.open(path));
            id=db.insertQso(qso("K1ABC","20m","CW","","20260910","120000","FN41"),"fixture").id; QVERIFY(id>0);
            QString error; QVERIFY2(db.recordLotwGridObservation(id,"FN42AB","FN42,FN43",&error),qPrintable(error));
            QVERIFY2(db.recordLotwGridObservation(id,"FN42AB","FN42,FN43",&error),qPrintable(error));
            QVERIFY(!db.recordLotwGridObservation(id,"","",&error)); QVERIFY(!error.isEmpty());
            QVERIFY(!db.recordLotwGridObservation(id+9999,"FN44","",&error));
            QVERIFY(db.connection().transaction()); QVERIFY(db.recordLotwGridObservation(id,"FN44","",&error)); QVERIFY(db.connection().rollback());
            QCOMPARE(db.record(id)->value("GRIDSQUARE"),QString("FN41"));
        }
        LogDatabase reopened; QVERIFY(reopened.open(path)); QSqlQuery q(reopened.connection());
        q.prepare("SELECT grid,vucc_grids FROM lotw_grid_observation WHERE qso_id=?"); q.addBindValue(id); QVERIFY(q.exec()); QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(),QString("FN42AB")); QCOMPARE(q.value(1).toString(),QString("FN42,FN43")); QVERIFY(!q.next());
        QCOMPARE(reopened.record(id)->value("GRIDSQUARE"),QString("FN41"));
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
        QCOMPARE(q.queryItemValue("qso_qslsince"),QString("1900-01-01"));

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
