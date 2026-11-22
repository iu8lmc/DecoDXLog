#include "core/LogDatabase.h"
#include "core/RoverSession.h"
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
using namespace decolog::core;
class TestRoverSession : public QObject {
    Q_OBJECT
  private slots:
    void persistsHistoryAndRejectsAmbiguousMinutes()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("log.sqlite");
        const QDateTime now = QDateTime::currentDateTimeUtc();
        {
            LogDatabase db;
            QVERIFY(db.open(path));
            RoverSession rover(&db);
            QVERIFY(!rover.start("Rover", "ZZ99"));
            QVERIFY(!rover.start("Rover", "JN71", 999999));
            QVERIFY(rover.start("Rover", "JN71DC"));
            QCOMPARE(db.qsoCount(), 0);
            QVERIFY(!rover.start("Second", "JM67"));
            QSqlQuery q(db.connection());
            q.prepare("UPDATE rover_position SET effective_at=?");
            q.addBindValue(now.addSecs(-3600).toString("yyyy-MM-dd'T'HH:mm'Z'"));
            QVERIFY(q.exec());
            QVERIFY(rover.setPosition("JM67AB"));
            QCOMPARE(rover.positions().size(), 2);
            QCOMPARE(rover.gridAt(now.addSecs(-1800)), QString("JN71DC"));
            QCOMPARE(rover.gridAt(now), QString("JM67AB"));
            QString futureError;
            QVERIFY(rover.gridAt(now.addSecs(7200), &futureError).isEmpty());
            QVERIFY(!futureError.isEmpty());
            QVERIFY(rover.setPosition("JM67AB"));
            QCOMPARE(rover.positions().size(), 2);
        }
        {
            LogDatabase db;
            QVERIFY(db.open(path));
            RoverSession rover(&db);
            QCOMPARE(rover.session().value("grid").toString(), QString("JM67AB"));
            QCOMPARE(rover.positions().size(), 2);
            QVERIFY(rover.setPosition("JM68AA"));
            QString error;
            QVERIFY(rover.gridAt(QDateTime::currentDateTimeUtc(), &error).isEmpty());
            QVERIFY(error.contains("Several"));
            QVERIFY(rover.close());
            QVERIFY(rover.session().isEmpty());
            QCOMPARE(rover.sessions().size(), 1);
            QCOMPARE(rover.positions().size(), 3);
            QVERIFY(rover.selectHistory(rover.sessions().first().toMap().value("id").toLongLong()));
            QCOMPARE(rover.positions().size(), 3);
            QCOMPARE(db.qsoCount(), 0);
        }
    }
    void failedPositionWriteRollsBackSession()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        QSqlQuery q(db.connection());
        QVERIFY(q.exec(
            "CREATE TRIGGER reject_rover BEFORE INSERT ON rover_position BEGIN SELECT RAISE(ABORT,'reject'); END"));
        RoverSession rover(&db);
        QVERIFY(!rover.start("Trip", "JN71"));
        QVERIFY(rover.session().isEmpty());
        QVERIFY(q.exec("SELECT COUNT(*) FROM rover_session"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 0);
    }
    void rejectsClockMovingBackwards()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        RoverSession rover(&db);
        QVERIFY(rover.start("Trip", "JN71"));
        QSqlQuery q(db.connection());
        q.prepare("UPDATE rover_position SET effective_at=?");
        q.addBindValue(QDateTime::currentDateTimeUtc().addSecs(7200).toString("yyyy-MM-dd'T'HH:mm'Z'"));
        QVERIFY(q.exec());
        QVERIFY(!rover.setPosition("JM67"));
        QCOMPARE(rover.positions().size(), 1);
        QCOMPARE(rover.session().value("grid").toString(), QString("JN71"));
    }
};
QTEST_GUILESS_MAIN(TestRoverSession)
#include "tst_roversession.moc"
