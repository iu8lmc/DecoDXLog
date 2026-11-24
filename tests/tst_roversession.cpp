#include "core/LogDatabase.h"
#include "core/RoverSession.h"
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QLibrary>
#include <QSqlDriver>
#include <QElapsedTimer>
#include <QThread>
#include <atomic>
#include <thread>
#include <cstring>
using namespace decolog::core;
class TestRoverSession : public QObject {
    Q_OBJECT
  private slots:
    void roverGuardRejectsChangeBetweenCheckAndInsert()
    {
#ifndef Q_OS_MACOS
        QSKIP("Native authorizer fixture requires verified system SQLite on macOS");
#endif
        if (!qEnvironmentVariableIsSet("DECOLOG_TEST_SYSTEM_SQLITE_AUTHORIZER")) QSKIP("Explicit opt-in required for verified SQLite driver");
        QLibrary sqlite(QStringLiteral("sqlite3")); using Authorizer=int (*)(void*,int,const char*,const char*,const char*,const char*); using SetAuthorizer=int (*)(void*,Authorizer,void*);
        const auto setAuthorizer=reinterpret_cast<SetAuthorizer>(sqlite.resolve("sqlite3_set_authorizer")); if (!setAuthorizer) QSKIP("SQLite authorizer unavailable");
        struct Gate { std::atomic_bool ready{false}, entered{false}, done{false}, changed{false}, timeout{false}; };
        const auto hook=+[](void* opaque,int action,const char* table,const char*,const char*,const char*) {
            auto* gate=static_cast<Gate*>(opaque); if (action==18 && table && std::strcmp(table,"qso")==0 && !gate->entered.exchange(true)) {
                QElapsedTimer timer; timer.start(); while (!gate->done.load() && timer.elapsed()<5000) QThread::msleep(1); if (!gate->done.load()) gate->timeout=true;
            } return 0;
        };
        for (const bool guarded : {false,true}) {
            QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path=dir.filePath("race.sqlite"); LogDatabase db; QVERIFY(db.open(path));
            QSqlQuery journal(db.connection()); QVERIFY(journal.exec("PRAGMA journal_mode=WAL")); QVERIFY(journal.next()); QCOMPARE(journal.value(0).toString().toLower(),QString("wal")); journal.finish();
            RoverSession rover(&db); QVERIFY(rover.start("Trip","JN71")); const auto session=rover.session();
            RoverInsertGuard guard{session.value("id").toLongLong(),session.value("uuid").toString(),"JN71"};
            QVariant handle=db.connection().driver()->handle(); if (QByteArray(handle.typeName())!="sqlite3*" || !handle.data()) QSKIP("SQLite native handle unavailable");
            void* native=*static_cast<void**>(handle.data()); QVERIFY(native);
            Gate gate; std::thread writer([&] { LogDatabase other; if (other.openExistingReadWrite(path)) { gate.ready=true; QElapsedTimer timer; timer.start();
                while (!gate.entered.load() && timer.elapsed()<5000) QThread::msleep(1);
                if (gate.entered.load()) { QSqlQuery q(other.connection()); gate.changed=q.exec("UPDATE rover_position SET gridsquare='JM67'"); }
            } gate.done=true; });
            QElapsedTimer ready; ready.start(); while (!gate.ready.load() && !gate.done.load() && ready.elapsed()<5000) QThread::msleep(1);
            const bool isReady=gate.ready.load(); const int installed=isReady ? setAuthorizer(native,hook,&gate) : -1;
            InsertResult result; if (installed==0) { const auto now=QDateTime::currentDateTimeUtc();
                AdifRecord record{{"CALL","K1ABC"},{"QSO_DATE",now.toString("yyyyMMdd")},{"TIME_ON",now.toString("hhmm")},{"BAND","20m"},{"MODE","CW"},{"MY_GRIDSQUARE","JN71"}};
                result=db.insertQso(record,"manual",{},true,0,false,0,0,guarded ? std::optional<RoverInsertGuard>(guard) : std::nullopt); }
            setAuthorizer(native,nullptr,nullptr); writer.join();
            QVERIFY(isReady); QCOMPARE(installed,0); QVERIFY(gate.entered.load()); QVERIFY(gate.done.load()); QVERIFY(gate.changed.load()); QVERIFY(!gate.timeout.load());
            QCOMPARE(result.status==InsertResult::Status::Inserted,!guarded); QCOMPARE(db.qsoCount(),guarded?qint64(0):qint64(1));
        }
    }
    void roverGuardAndContestSerialCommitTogether()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path=dir.filePath("serial.sqlite");
        LogDatabase db,writer; QVERIFY(db.open(path)); QVERIFY(writer.open(path)); RoverSession rover(&db); QVERIFY(rover.start("Trip","JN71"));
        const auto active=rover.session(); RoverInsertGuard guard{active.value("id").toLongLong(),active.value("uuid").toString(),"JN71"};
        const auto now=QDateTime::currentDateTimeUtc(); AdifRecord record{{"CALL","K1ABC"},{"QSO_DATE",now.toString("yyyyMMdd")},{"TIME_ON",now.toString("hhmm")},{"BAND","20m"},{"MODE","CW"},{"MY_GRIDSQUARE","JN71"}};
        auto grant=[&](const char* node,const char* request,int serial) { return db.beginContestSerialRequest("event",node,request,"authority",serial).ok && db.acceptContestSerialGrant("event",node,request,"authority",serial).ok; };
        QVERIFY(grant("node","baseline",10)); QVERIFY(grant("node2","guarded",11));
        QSqlQuery q(writer.connection()); QVERIFY(q.exec("UPDATE rover_position SET gridsquare='JM67'"));
        const auto baseline=db.insertQsoWithContestSerial(record,"manual","event","node","baseline","authority",{},true,0,false);
        QCOMPARE(baseline.status,InsertResult::Status::Inserted); QCOMPARE(db.contestSerialRequest("event","node","baseline").state,QString("consumed"));
        const auto stale=db.insertQsoWithContestSerial(record,"manual","event","node2","guarded","authority",{},true,0,false,0,0,guard);
        QCOMPARE(stale.status,InsertResult::Status::Error); QCOMPARE(db.contestSerialRequest("event","node2","guarded").state,QString("granted")); QCOMPARE(db.qsoCount(),qint64(1));
        guard.grid="JM67"; record.set("MY_GRIDSQUARE","JM67");
        const auto saved=db.insertQsoWithContestSerial(record,"manual","event","node2","guarded","authority",{},true,0,false,0,0,guard);
        QCOMPARE(saved.status,InsertResult::Status::Inserted); QCOMPARE(db.record(saved.id)->value("MY_GRIDSQUARE"),QString("JM67"));
        QCOMPARE(db.record(saved.id)->value("STX"),QString("11")); QCOMPARE(db.contestSerialRequest("event","node2","guarded").state,QString("consumed"));
    }
    void guardedInsertRejectsChangedPosition()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path=dir.filePath("rover.sqlite");
        LogDatabase db,writer; QVERIFY(db.open(path)); QVERIFY(writer.open(path)); RoverSession rover(&db);
        QVERIFY(rover.start("Trip","JN71")); const auto active=rover.session();
        const QDateTime now=QDateTime::currentDateTimeUtc();
        AdifRecord record{{"CALL","K1ABC"},{"QSO_DATE",now.toString("yyyyMMdd")},{"TIME_ON",now.toString("hhmm")},{"BAND","20m"},{"MODE","CW"},{"MY_GRIDSQUARE","JN71"}};
        RoverInsertGuard guard{active.value("id").toLongLong(),active.value("uuid").toString(),"JN71"};
        const auto newlyStarted=db.insertQso(record,"manual",{},true,0,false,0,0,RoverInsertGuard{});
        QCOMPARE(newlyStarted.status,InsertResult::Status::Error); QCOMPARE(db.qsoCount(),qint64(0));
        QSqlQuery q(writer.connection()); QVERIFY(q.exec("UPDATE rover_position SET gridsquare='JM67'"));
        const auto baseline=db.insertQso(record,"manual",{},true,0,false);
        QCOMPARE(baseline.status,InsertResult::Status::Inserted); QCOMPARE(db.record(baseline.id)->value("MY_GRIDSQUARE"),QString("JN71"));
        QVERIFY(db.softDeleteQso(baseline.id));
        const auto stale=db.insertQso(record,"manual",{},true,0,false,0,0,guard);
        QCOMPARE(stale.status,InsertResult::Status::Error); QCOMPARE(db.qsoCount(),qint64(0));
        guard.grid="JM67"; record.set("MY_GRIDSQUARE","JM67");
        const auto accepted=db.insertQso(record,"manual",{},true,0,false,0,0,guard);
        QCOMPARE(accepted.status,InsertResult::Status::Inserted); QCOMPARE(db.record(accepted.id)->value("MY_GRIDSQUARE"),QString("JM67"));
        QVERIFY(rover.close()); const auto closed=db.insertQso(record,"manual",{},true,0,false,0,0,guard);
        QCOMPARE(closed.status,InsertResult::Status::Error); QCOMPARE(db.qsoCount(),qint64(1));
    }
    void failedSessionReadIsNotAnInactiveSession()
    {
        LogDatabase db; QVERIFY(db.open(":memory:")); RoverSession rover(&db);
        QVariantMap active; QString error;
        QVERIFY(rover.readSession(&active,&error)); QVERIFY(active.isEmpty()); QVERIFY(error.isEmpty());
        QVERIFY(rover.start("Trip","JN71")); QVERIFY(rover.readSession(&active,&error)); QVERIFY(!active.isEmpty());
        QSqlQuery q(db.connection()); QVERIFY(q.exec("DROP TABLE rover_position"));
        QVERIFY(!rover.readSession(&active,&error)); QVERIFY(active.isEmpty()); QVERIFY(!error.isEmpty());
    }
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
