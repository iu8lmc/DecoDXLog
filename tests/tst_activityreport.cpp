#include "core/ActivityReport.h"
#include "core/LogDatabase.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QTest>
using namespace decolog::core;
class TestActivityReport : public QObject {
    Q_OBJECT
private slots:
    void reportSnapshotRejectsAnotherLogAndPinsData()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); LogDatabase first, other;
        QVERIFY(first.open(dir.filePath("first.sqlite"))); QVERIFY(other.open(dir.filePath("other.sqlite")));
        QVERIFY(first.identity()!=other.identity());
        QVERIFY(first.insertQso(AdifRecord{{"CALL","K1AAA"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}},"fixture",{},true,0,false).id>0);
        QString error; QSqlDatabase wrong=other.connection();
        QVERIFY(!activityreport::beginVerifiedSnapshot(wrong,first.identity(),&error)); QVERIFY(!error.isEmpty());
        QSqlDatabase snapshot=first.connection(); error.clear(); QVERIFY2(activityreport::beginVerifiedSnapshot(snapshot,first.identity(),&error),qPrintable(error));
        LogDatabase writer; QVERIFY(writer.open(dir.filePath("first.sqlite")));
        QVERIFY(writer.insertQso(AdifRecord{{"CALL","K1BBB"},{"QSO_DATE","20260101"},{"TIME_ON","1201"},{"BAND","20m"},{"MODE","CW"}},"fixture",{},true,0,false).id>0);
        auto result=activityreport::run(snapshot,2026,"20m"); QVERIFY(result.error.isEmpty()); QCOMPARE(result.rows.first().toMap().value("count").toInt(),1);
        QVERIFY(snapshot.rollback());
        auto refreshed=activityreport::run(first.connection(),2026,"20m"); QVERIFY(refreshed.error.isEmpty()); QCOMPARE(refreshed.rows.first().toMap().value("count").toInt(),2);
        QVERIFY(!activityreport::beginVerifiedSnapshot(snapshot,QString(),&error));
        QCOMPARE(first.qsoCount(),qint64(2)); QCOMPARE(other.qsoCount(),qint64(0));
    }
    void savedDefinitionsAreIndependentAndRejectStaleChanges()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const QString path=dir.filePath("reports.sqlite");
        LogDatabase first; QVERIFY(first.open(path)); first.setSetting("activity_report_v1",R"({"year":2024,"band":"20m"})");
        activityreport::Definition a; a.name="Band by mode"; a.kind="custom"; a.year=2026; a.band="20m"; a.dimensions={"band","mode"}; a.aggregate="calls";
        activityreport::Definition b; b.name="Club"; b.kind="club"; b.station="9H1SR"; QString error;
        QVERIFY2(activityreport::saveDefinition(first,a,&error),qPrintable(error)); QVERIFY2(activityreport::saveDefinition(first,b,&error),qPrintable(error)); QVERIFY(a.id!=b.id);
        LogDatabase second; QVERIFY(second.open(path)); auto stale=activityreport::loadDefinition(second,a.id,&error); QVERIFY(stale);
        auto changed=a; changed.name="Band by year"; changed.dimensions={"band","year"}; QVERIFY(activityreport::saveDefinition(first,changed,&error));
        stale->name="stale"; QVERIFY(!activityreport::saveDefinition(second,*stale,&error)); QVERIFY(!activityreport::removeDefinition(second,*stale,&error));
        const auto items=activityreport::listDefinitions(first,&error); QVERIFY2(error.isEmpty(),qPrintable(error)); QCOMPARE(items.size(),2);
        QCOMPARE(activityreport::loadDefinition(first,a.id)->name,QString("Band by year"));
        QVERIFY(activityreport::removeDefinition(first,b,&error)); QCOMPARE(activityreport::listDefinitions(first).size(),1);
        QVERIFY(first.setting("activity_report_v1").contains("2024"));
        changed.dimensions={"band","band"}; QVERIFY(!activityreport::saveDefinition(first,changed,&error)); QCOMPARE(activityreport::listDefinitions(first).size(),1);
        first.setSetting("saved_report.invalid","not json"); QVERIFY(activityreport::listDefinitions(first,&error).isEmpty()); QVERIFY(!error.isEmpty());
        QSqlQuery q(first.connection()); QVERIFY(q.exec("DELETE FROM app_setting WHERE key='saved_report.invalid'")); QCOMPARE(activityreport::listDefinitions(first).size(),1);
    }
    void savedDefinitionLimitDoesNotCreateAnUnreadableLibrary()
    {
        LogDatabase db; QVERIFY(db.open(":memory:")); QSqlQuery q(db.connection());
        QVERIFY(q.exec("WITH RECURSIVE n(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM n WHERE x<1000) INSERT INTO app_setting(key,value) SELECT printf('saved_report.fixture%04d',x),'{}' FROM n"));
        activityreport::Definition d; d.name="Beyond limit"; d.kind="activity"; QString error;
        QVERIFY(!activityreport::saveDefinition(db,d,&error)); QVERIFY(d.id.isEmpty());
        QVERIFY(q.exec("SELECT COUNT(*) FROM app_setting WHERE key LIKE 'saved_report.%'")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),1000);
    }
    void configurableDimensionsAndAggregatesAreBoundedAndSafe()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        auto add=[&](const char* call,const char* band,const char* district) { return db.insertQso(AdifRecord{{"CALL",call},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND",band},{"MODE","CW"},{"APP_DISTRICT",district}},"fixture",{},true,0,false).id; };
        QVERIFY(add("K1AAA","20m","North")>0); QVERIFY(add("K1AAA","20m","North")>0); QVERIFY(add("K1BBB","20m","North")>0); QVERIFY(add("K1CCC","40m","South")>0);
        const QStringList dims{"band","custom:APP_DISTRICT"};
        const auto count=activityreport::runCustom(db.connection(),2026,{},dims,"qsos"); QVERIFY2(count.error.isEmpty(),qPrintable(count.error));
        QCOMPARE(count.rows.size(),2); QCOMPARE(count.rows[0].toMap().value("d0").toString(),QString("20m")); QCOMPARE(count.rows[0].toMap().value("d1").toString(),QString("North")); QCOMPARE(count.rows[0].toMap().value("value").toInt(),3);
        const auto distinct=activityreport::runCustom(db.connection(),2026,"20m",dims,"calls"); QVERIFY(distinct.error.isEmpty()); QCOMPARE(distinct.rows.size(),1); QCOMPARE(distinct.rows[0].toMap().value("value").toInt(),2);
        const auto three=activityreport::runCustom(db.connection(),2026,"20m",{"year","custom:APP_DISTRICT","custom:APP_MISSING"},"qsos");
        QVERIFY2(three.error.isEmpty(),qPrintable(three.error)); QCOMPARE(three.rows.size(),1); QCOMPARE(three.rows.first().toMap().value("d0").toString(),QString("2026"));
        QCOMPARE(three.rows.first().toMap().value("d2").toString(),QString()); QCOMPARE(three.rows.first().toMap().value("value").toInt(),3);
        QVERIFY(activityreport::customCsv(distinct,dims,"calls").contains("\"20m\",\"North\",2"));
        QVERIFY(!activityreport::runCustom(db.connection(),2026,{},QStringList{"custom:APP_X');DROP TABLE qso;--"},"qsos").error.isEmpty());
        QVERIFY(!activityreport::runCustom(db.connection(),2026,{},QStringList{"band","band"},"qsos").error.isEmpty());
        QVERIFY(!activityreport::runCustom(db.connection(),2026,{},dims,"sql").error.isEmpty()); QCOMPARE(db.qsoCount(),qint64(4));
        QSqlQuery malformed(db.connection()); QVERIFY(malformed.exec("UPDATE qso SET adif_extra='broken' WHERE call='K1CCC'"));
        const auto missing=activityreport::runCustom(db.connection(),2026,"40m",dims,"qsos"); QVERIFY(missing.error.isEmpty()); QCOMPARE(missing.rows.first().toMap().value("d1").toString(),QString());
    }
    void annualClubActivityUsesRecordedStationAndOperator()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        auto add=[&](const char* call,const char* date,const char* station,const char* op) {
            return db.insertQso(AdifRecord{{"CALL",call},{"QSO_DATE",date},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"STATION_CALLSIGN",station},{"OPERATOR",op}},"fixture",{},true).id;
        };
        QVERIFY(add("K1AAA","20260101","9H1SR","9H1AA")>0); QVERIFY(add("K1BBB","20260102","9H1SR","9H1AA")>0);
        QVERIFY(add("K1CCC","20260102","9H1SR","")>0); QVERIFY(add("K1DDD","20250102","9H1SR","9H1BB")>0);
        QVERIFY(add("K1EEE","20260102","9H9ZZ","9H1AA")>0);
        const auto report=activityreport::runClub(db.connection(),2026,"20m","9h1sr"); QVERIFY2(report.error.isEmpty(),qPrintable(report.error));
        QCOMPARE(report.rows.size(),2); QCOMPARE(report.rows[0].toMap().value("operator").toString(),QString());
        QCOMPARE(report.rows[1].toMap().value("operator").toString(),QString("9H1AA")); QCOMPARE(report.rows[1].toMap().value("count").toInt(),2);
        QVERIFY(activityreport::clubCsv(report).contains("\"2026\",\"9H1SR\",\"9H1AA\",2"));
        const auto all=activityreport::runClub(db.connection(),0,{},{}); QVERIFY(all.error.isEmpty()); QCOMPARE(all.rows.size(),4);
        QCOMPARE(db.qsoCount(),qint64(5));
    }
    void confirmationTimesKeepServicesAndMissingDatesSeparate()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        for (const QString& call : {QString("K1AAA"),QString("K1BBB"),QString("K1CCC"),QString("K1DDD"),QString("K1EEE")})
            QVERIFY(db.insertQso(AdifRecord{{"CALL",call},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}},"fixture",{},true).id>0);
        QVERIFY(db.insertQso(AdifRecord{{"CALL","K1FFF"},{"QSO_DATE","20250101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}},"fixture",{},true).id>0);
        QVERIFY(db.insertQso(AdifRecord{{"CALL","K1GGG"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","40m"},{"MODE","CW"}},"fixture",{},true).id>0);
        QSqlQuery q(db.connection());
        QVERIFY(q.exec("INSERT INTO qsl_status(qso_id,service,rcvd,rcvd_date) VALUES (1,'card','Y','20260101'),(2,'card','Y','20260103'),(3,'card','Y',NULL),(4,'card','Y','20260230'),(5,'card','Y','20251231'),(1,'lotw','Y','20260111'),(2,'eqsl','N','20260104'),(3,'qrz','Y',NULL)"));
        QVERIFY(q.exec("INSERT INTO qsl_status(qso_id,service,rcvd,rcvd_date) VALUES (6,'card','Y','20250102'),(7,'card','Y','20260102')"));
        const auto result=activityreport::runConfirmations(db.connection(),2026,"20m"); QVERIFY2(result.error.isEmpty(),qPrintable(result.error));
        QCOMPARE(result.rows.size(),3); const auto card=result.rows[0].toMap(); const auto lotw=result.rows[1].toMap();
        QCOMPARE(card.value("service").toString(),QString("card")); QCOMPARE(card.value("confirmed").toInt(),5);
        QCOMPARE(card.value("dated").toInt(),2); QCOMPARE(card.value("missing").toInt(),1); QCOMPARE(card.value("invalid").toInt(),1); QCOMPARE(card.value("earlier").toInt(),1);
        QCOMPARE(card.value("averageDays").toDouble(),1.0); QCOMPARE(lotw.value("averageDays").toDouble(),10.0);
        QVERIFY(!result.rows[2].toMap().contains("averageDays"));
        QVERIFY(activityreport::confirmationsCsv(result).contains("\"card\",5,2,1,1,1,1.00"));
        QCOMPARE(db.qsoCount(),qint64(7));
    }
    void groupsRecordedValuesAndExportsCsv()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        auto add=[&](const char* call,const char* date,const char* band,const char* antenna,const char* grid) {
            return db.insertQso(AdifRecord{{"CALL",call},{"QSO_DATE",date},{"TIME_ON","1200"},{"BAND",band},{"MODE","CW"},{"MY_ANTENNA",antenna},{"MY_GRIDSQUARE",grid}},"fixture",{},true).id;
        };
        QVERIFY(add("K1AAA","20260101","20m","Dipole, \"A\"","JM75")>0);
        QVERIFY(add("K1BBB","20260102","20m","Dipole, \"A\"","JM75")>0);
        QVERIFY(add("K1CCC","20260102","40m","","JM75")>0);
        QVERIFY(add("K1DDD","20250102","20m","Yagi","JN35")>0);
        const auto report=activityreport::run(db.connection(),2026,"20m"); QVERIFY2(report.error.isEmpty(),qPrintable(report.error));
        QCOMPARE(report.rows.size(),1); const auto row=report.rows.first().toMap();
        QCOMPARE(row.value("antenna").toString(),QString("Dipole, \"A\"")); QCOMPARE(row.value("grid").toString(),QString("JM75")); QCOMPARE(row.value("count").toInt(),2);
        QCOMPARE(activityreport::csv(report),QByteArray("antenna,band,own_grid,qso_count\r\n\"Dipole, \"\"A\"\"\",\"20m\",\"JM75\",2\r\n"));
        const auto all=activityreport::run(db.connection(),0,{}); QVERIFY(all.error.isEmpty()); QCOMPARE(all.rows.size(),3);
        const auto missing=activityreport::run(db.connection(),2026,"40m"); QCOMPARE(missing.rows.first().toMap().value("antenna").toString(),QString());
        QVERIFY(!activityreport::run(db.connection(),-1,{}).error.isEmpty());
        QSqlQuery q(db.connection()); QVERIFY(q.exec("UPDATE qso SET adif_extra='broken' WHERE call='K1CCC'"));
        const auto malformed=activityreport::run(db.connection(),2026,"40m"); QVERIFY2(malformed.error.isEmpty(),qPrintable(malformed.error));
        QCOMPARE(malformed.rows.first().toMap().value("grid").toString(),QString()); QCOMPARE(db.qsoCount(),qint64(4));
        QVERIFY(q.exec("UPDATE qso SET deleted=1 WHERE call='K1DDD'"));
        const auto live=activityreport::run(db.connection(),0,{}); QVERIFY(live.error.isEmpty()); QCOMPARE(live.rows.size(),2);
    }
    void capsLargeGroupCountAndRefusesPartialCsv()
    {
        LogDatabase db; QVERIFY(db.open(":memory:")); QSqlQuery q(db.connection());
        QVERIFY2(q.exec("WITH RECURSIVE n(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM n WHERE x<10001) "
                       "INSERT INTO qso(uuid,call,qso_datetime_on,band,mode,source,adif_extra,created_at,updated_at) "
                       "SELECT printf('u%05d',x),'K1AAA','2026-01-01T00:00:00Z','20m','CW','fixture',json_object('MY_ANTENNA',printf('antenna%05d',x)),'2026-01-01','2026-01-01' FROM n"),qPrintable(q.lastError().text()));
        const auto report=activityreport::run(db.connection(),2026,"20m"); QVERIFY2(report.error.isEmpty(),qPrintable(report.error));
        QCOMPARE(report.rows.size(),10000); QVERIFY(report.truncated); QVERIFY(activityreport::csv(report).isEmpty());
        const auto custom=activityreport::runCustom(db.connection(),2026,"20m",{"antenna"},"qsos"); QVERIFY(custom.error.isEmpty()); QVERIFY(custom.truncated); QCOMPARE(custom.rows.size(),10000); QVERIFY(activityreport::customCsv(custom,{"antenna"},"qsos").isEmpty());
    }
    void reportObservesTheReadSnapshotDuringAnotherWrite()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const QString path=dir.filePath("snapshot.sqlite");
        LogDatabase reader; QVERIFY(reader.open(path)); QSqlQuery q(reader.connection()); QVERIFY(q.exec("PRAGMA journal_mode=WAL")); q.finish();
        auto add=[](LogDatabase& db,const char* call) { return db.insertQso(AdifRecord{{"CALL",call},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}},"fixture",{},true).id; };
        QVERIFY(add(reader,"K1AAA")>0); LogDatabase writer; QVERIFY(writer.open(path));
        QVERIFY(reader.connection().transaction()); QVERIFY(q.exec("SELECT COUNT(*) FROM qso")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),1); q.finish();
        QVERIFY(add(writer,"K1BBB")>0);
        const auto before=activityreport::run(reader.connection(),2026,"20m"); QVERIFY2(before.error.isEmpty(),qPrintable(before.error));
        QCOMPARE(before.rows.size(),1); QCOMPARE(before.rows.first().toMap().value("count").toInt(),1);
        const auto customBefore=activityreport::runCustom(reader.connection(),2026,"20m",{"band"},"qsos"); QVERIFY(customBefore.error.isEmpty()); QCOMPARE(customBefore.rows.first().toMap().value("value").toInt(),1);
        QVERIFY(reader.connection().commit()); const auto after=activityreport::run(reader.connection(),2026,"20m");
        QVERIFY(after.error.isEmpty()); QCOMPARE(after.rows.first().toMap().value("count").toInt(),2);
        const auto customAfter=activityreport::runCustom(reader.connection(),2026,"20m",{"band"},"qsos"); QVERIFY(customAfter.error.isEmpty()); QCOMPARE(customAfter.rows.first().toMap().value("value").toInt(),2);
    }
};
QTEST_GUILESS_MAIN(TestActivityReport)
#include "tst_activityreport.moc"
