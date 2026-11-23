#include "core/ContestComparison.h"
#include "core/LogDatabase.h"
#include <QTest>
#include <QSqlQuery>
#include <QTemporaryDir>
using namespace decolog::core;
class TestContestComparison : public QObject {
    Q_OBJECT
  private slots:
    void overlappingPeriodsAndMalformedExtrasAreExplicit()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        auto add=[&](const QString &call) { return db.insertQso(AdifRecord{{"CALL",call},{"QSO_DATE","20241123"},{"TIME_ON","120000"},
            {"BAND","20m"},{"MODE","CW"},{"CONTEST_ID","CQ-WW-CW"}},"fixture",{},true,0,false).id; };
        QVERIFY(add("K1AAA")>0); QVERIFY(add("K1BBB")>0);
        QSqlQuery q(db.connection()); QVERIFY(q.exec("UPDATE qso SET adif_extra='broken' WHERE call='K1BBB'"));
        const auto r=contestcomparison::compare(db.connection(),"cq-ww-cw",QDate(2024,11,23),QDate(2024,11,23),QDate(2024,11,23),QDate(2024,11,23));
        QVERIFY2(r.error.isEmpty(),qPrintable(r.error)); QCOMPARE(r.totalA,1); QCOMPARE(r.totalB,1);
        QVERIFY(!contestcomparison::compare(db.connection(),"CQ-WW-CW",QDate(2024,1,2),QDate(2024,1,1),QDate(2025,1,1),QDate(2025,1,1)).error.isEmpty());
        QCOMPARE(db.qsoCount(),qint64(2));
    }
    void includesFractionalMidnightAndReportsMalformedUtc()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        auto add=[&](const QString &call) { return db.insertQso(AdifRecord{{"CALL",call},{"QSO_DATE","20241123"},{"TIME_ON","000000"},
            {"BAND","20m"},{"MODE","CW"},{"CONTEST_ID","CQ-WW-CW"}},"fixture",{},true,0,false).id; };
        QVERIFY(add("K1AAA")>0); QVERIFY(add("K1BBB")>0); QSqlQuery q(db.connection());
        QVERIFY(q.exec("UPDATE qso SET qso_datetime_on='2024-11-23T00:00:00.000Z' WHERE call='K1AAA'"));
        QVERIFY(q.exec("UPDATE qso SET qso_datetime_on='2024-11-23T12:00:99Z' WHERE call='K1BBB'"));
        const auto r=contestcomparison::compare(db.connection(),"CQ-WW-CW",QDate(2024,11,23),QDate(2024,11,23),QDate(2025,11,22),QDate(2025,11,22));
        QVERIFY2(r.error.isEmpty(),qPrintable(r.error)); QCOMPARE(r.totalA,1); QCOMPARE(r.invalidTimestamp,1);
    }
    void goalsAreBoundToContestAndPeriodAndRejectStaleWrites()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); LogDatabase a,b; const auto path=dir.filePath("log.sqlite"); QVERIFY(a.open(path)); QVERIFY(b.open(path));
        const auto a0=QDate(2024,11,23), a1=QDate(2024,11,24), b0=QDate(2025,11,22), b1=QDate(2025,11,23);
        auto empty=contestcomparison::loadGoals(a.connection(),"CQ-WW-CW",a0,a1,b0,b1); QVERIFY(empty.error.isEmpty());
        QCOMPARE(empty.a.revision,0); QCOMPARE(empty.b.revision,0);
        auto saved=contestcomparison::saveGoals(a.connection(),a.identity(),"CQ-WW-CW",a0,a1,b0,b1,5,7,0,0,0);
        QVERIFY2(saved.error.isEmpty(),qPrintable(saved.error)); QCOMPARE(saved.a.revision,1); QCOMPARE(saved.b.target,7);
        auto stale=contestcomparison::loadGoals(b.connection(),"CQ-WW-CW",a0,a1,b0,b1); QVERIFY(stale.error.isEmpty());
        auto updated=contestcomparison::saveGoals(a.connection(),a.identity(),"CQ-WW-CW",a0,a1,b0,b1,10,20,1,1,1); QVERIFY(updated.error.isEmpty());
        auto rejected=contestcomparison::saveGoals(b.connection(),b.identity(),"CQ-WW-CW",a0,a1,b0,b1,11,21,stale.a.revision,stale.b.revision,stale.generation);
        QVERIFY(!rejected.error.isEmpty()); auto current=contestcomparison::loadGoals(a.connection(),"CQ-WW-CW",a0,a1,b0,b1);
        QCOMPARE(current.a.target,10); QCOMPARE(current.b.target,20); QCOMPARE(current.a.revision,2);
        auto secondStale=contestcomparison::saveGoals(b.connection(),b.identity(),"CQ-WW-CW",a0,a1,b0,b1,50,60,2,1,2);
        QVERIFY(!secondStale.error.isEmpty()); current=contestcomparison::loadGoals(a.connection(),"CQ-WW-CW",a0,a1,b0,b1);
        QCOMPARE(current.a.target,10); QCOMPARE(current.a.revision,2); QCOMPARE(current.b.target,20);
        auto other=contestcomparison::loadGoals(a.connection(),"CQ-WW-SSB",a0,a1,b0,b1); QVERIFY(other.error.isEmpty()); QCOMPARE(other.a.revision,0);
        auto wrongIdentity=contestcomparison::saveGoals(a.connection(),"wrong","CQ-WW-CW",a0,a1,b0,b1,12,22,2,2,2);
        QVERIFY(!wrongIdentity.error.isEmpty()); QCOMPARE(a.qsoCount(),qint64(0));
    }
    void duplicatePeriodsAndGoalLimitAreSafe()
    {
        LogDatabase db; QVERIFY(db.open(":memory:")); const QDate a(2024,11,23),b(2025,11,22);
        auto conflict=contestcomparison::saveGoals(db.connection(),db.identity(),"CQ-WW-CW",a,a,a,a,5,6,0,0,0);
        QVERIFY(!conflict.error.isEmpty());
        auto one=contestcomparison::saveGoals(db.connection(),db.identity(),"CQ-WW-CW",a,a,a,a,5,5,0,0,0);
        QVERIFY2(one.error.isEmpty(),qPrintable(one.error)); QCOMPARE(one.a.revision,1); QCOMPARE(one.b.revision,1);
        QSqlQuery q(db.connection()); QVERIFY(q.exec("WITH RECURSIVE n(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM n WHERE x<998) INSERT INTO app_setting(key,value) SELECT printf('contest_goal.v1.fixture%04d',x),'{}' FROM n"));
        auto failure=contestcomparison::saveGoals(db.connection(),db.identity(),"CQ-WW-CW",a,a,b,b,10,20,1,0,1);
        QVERIFY(failure.error.isEmpty());
        const QDate c(2026,11,21); auto over=contestcomparison::saveGoals(db.connection(),db.identity(),"CQ-WW-CW",a,a,c,c,11,30,2,0,2);
        QVERIFY(!over.error.isEmpty()); auto current=contestcomparison::loadGoals(db.connection(),"CQ-WW-CW",a,a,b,b);
        QVERIFY(current.error.isEmpty()); QCOMPARE(current.a.target,10); QCOMPARE(current.a.revision,2); QCOMPARE(current.b.target,20);
    }
    void removeGoalsRejectsStaleAndPreventsRecreateAba()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); LogDatabase a,b; const auto path=dir.filePath("log.sqlite"); QVERIFY(a.open(path)); QVERIFY(b.open(path));
        const QDate first(2024,11,23),second(2025,11,22); auto absent=contestcomparison::loadGoals(a.connection(),"CQ-WW-CW",first,first,second,second);
        auto made=contestcomparison::saveGoals(a.connection(),a.identity(),"CQ-WW-CW",first,first,second,second,5,7,0,0,absent.generation); QVERIFY(made.error.isEmpty());
        auto stale=contestcomparison::loadGoals(b.connection(),"CQ-WW-CW",first,first,second,second);
        auto removed=contestcomparison::removeGoals(a.connection(),a.identity(),"CQ-WW-CW",first,first,second,second,made.a.revision,made.b.revision,made.generation);
        QVERIFY2(removed.error.isEmpty(),qPrintable(removed.error)); QCOMPARE(removed.a.revision,0); QCOMPARE(removed.b.revision,0);
        QSqlQuery count(a.connection()); QVERIFY(count.exec("SELECT COUNT(*) FROM app_setting WHERE key LIKE 'contest_goal.v1.%'")); QVERIFY(count.next()); QCOMPARE(count.value(0).toInt(),0);
        auto oldAbsent=contestcomparison::saveGoals(b.connection(),b.identity(),"CQ-WW-CW",first,first,second,second,9,9,0,0,absent.generation); QVERIFY(!oldAbsent.error.isEmpty());
        auto oldPresent=contestcomparison::saveGoals(b.connection(),b.identity(),"CQ-WW-CW",first,first,second,second,9,9,stale.a.revision,stale.b.revision,stale.generation); QVERIFY(!oldPresent.error.isEmpty());
        auto fresh=contestcomparison::loadGoals(a.connection(),"CQ-WW-CW",first,first,second,second); QCOMPARE(fresh.a.revision,0); QCOMPARE(fresh.b.revision,0);
        auto recreated=contestcomparison::saveGoals(a.connection(),a.identity(),"CQ-WW-CW",first,first,second,second,11,12,0,0,fresh.generation); QVERIFY(recreated.error.isEmpty());
        auto staleDelete=contestcomparison::removeGoals(b.connection(),b.identity(),"CQ-WW-CW",first,first,second,second,made.a.revision,made.b.revision,made.generation); QVERIFY(!staleDelete.error.isEmpty());
        auto current=contestcomparison::loadGoals(a.connection(),"CQ-WW-CW",first,first,second,second); QCOMPARE(current.a.target,11); QCOMPARE(current.b.target,12); QCOMPARE(a.qsoCount(),qint64(0));
    }
    void comparesRecordedContestQsosAcrossPeriods()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        auto add=[&](const QString &call,const QString &date,const QString &time,const QString &band,const QString &op,const QString &contest) {
            return db.insertQso(AdifRecord{{"CALL",call},{"QSO_DATE",date},{"TIME_ON",time},{"BAND",band},{"MODE","CW"},
                {"OPERATOR",op},{"MY_RIG","Rig A"},{"RIG","Remote rig"},{"CONTEST_ID",contest}},"fixture",{},true,0,false).id;
        };
        QVERIFY(add("K1AAA","20241123","120100","20m","K1OP","CQ-WW-CW")>0);
        QVERIFY(add("K1BBB","20241123","130100","40m","","CQ-WW-CW")>0);
        QVERIFY(add("K1CCC","20251122","120100","20m","K1OP","CQ-WW-CW")>0);
        QVERIFY(add("K1DDD","20251122","140100","20m","K2OP","OTHER")>0);
        const auto deleted=add("K1EEE","20251122","150100","20m","K1OP","CQ-WW-CW"); QVERIFY(deleted>0); QVERIFY(db.softDeleteQso(deleted));
        auto r=contestcomparison::compare(db.connection(),"CQ-WW-CW",QDate(2024,11,23),QDate(2024,11,23),QDate(2025,11,22),QDate(2025,11,22));
        QVERIFY2(r.error.isEmpty(),qPrintable(r.error)); QCOMPARE(r.totalA,2); QCOMPARE(r.totalB,1);
        auto find=[&](const QString &dimension,const QString &value) { for (const auto &item:r.rows) { auto row=item.toMap(); if (row.value("dimension")==dimension && row.value("value")==value) return row; } return QVariantMap(); };
        QCOMPARE(find("band","20m").value("a").toInt(),1); QCOMPARE(find("band","20m").value("b").toInt(),1);
        QCOMPARE(find("operator","").value("a").toInt(),1); QCOMPARE(find("operator","K1OP").value("b").toInt(),1);
        QCOMPARE(find("hour","13:00").value("a").toInt(),1); QCOMPARE(find("radio","Rig A").value("a").toInt(),2);
        QVERIFY(find("radio","Remote rig").isEmpty()); QCOMPARE(db.qsoCount(),qint64(4));
        QVERIFY(!contestcomparison::compare(db.connection(),"",QDate(2024,1,1),QDate(2024,1,1),QDate(2025,1,1),QDate(2025,1,1)).error.isEmpty());
    }
};
QTEST_GUILESS_MAIN(TestContestComparison)
#include "tst_contestcomparison.moc"
