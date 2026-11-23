#include "core/ActivityReport.h"
#include "core/LogDatabase.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QTemporaryDir>
#include <QTest>
using namespace decolog::core;
class TestActivityReport : public QObject {
    Q_OBJECT
private slots:
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
        QVERIFY(reader.connection().commit()); const auto after=activityreport::run(reader.connection(),2026,"20m");
        QVERIFY(after.error.isEmpty()); QCOMPARE(after.rows.first().toMap().value("count").toInt(),2);
    }
};
QTEST_GUILESS_MAIN(TestActivityReport)
#include "tst_activityreport.moc"
