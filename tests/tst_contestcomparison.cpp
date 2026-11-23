#include "core/ContestComparison.h"
#include "core/LogDatabase.h"
#include <QTest>
#include <QSqlQuery>
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
