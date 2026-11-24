#include "core/LogDatabase.h"
#include "core/RadioNet.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QLibrary>
#include <QSqlDriver>
#include <QElapsedTimer>
#include <atomic>
#include <thread>
#include <cstring>
#include <filesystem>

using namespace decolog::core;
class TestRadioNet : public QObject {
    Q_OBJECT
  private slots:
    void editParticipantPreservesHistoryAndRejectsStaleChanges()
    {
        QTemporaryDir dir; const QString path=dir.filePath("net.sqlite");
        LogDatabase db,other; QVERIFY(db.open(path)); QVERIFY(other.open(path));
        RadioNet net(&db), external(&other); QVERIFY(net.start("Net")); QVERIFY(net.checkIn("K1AAA","Alice","first note"));
        const auto session=net.session(); const auto member=net.members().first().toMap();
        const qint64 id=member.value("id").toLongLong(); const QString uuid=session.value("uuid").toString();
        QVERIFY(net.metaObject()->indexOfMethod("editParticipant(qlonglong,QString,QString,QString,QString,QString)")>=0);
        auto edit=[&](const QString& oldName,const QString& oldNotes,const QString& newName,const QString& newNotes) {
            bool ok=false; const bool invoked=QMetaObject::invokeMethod(&net,"editParticipant",Q_RETURN_ARG(bool,ok),
                Q_ARG(qlonglong,id),Q_ARG(QString,uuid),Q_ARG(QString,oldName),Q_ARG(QString,oldNotes),Q_ARG(QString,newName),Q_ARG(QString,newNotes)); return invoked && ok;
        };
        QSqlQuery fault(db.connection()); QVERIFY(fault.exec("CREATE TRIGGER reject_details BEFORE INSERT ON radio_net_event WHEN NEW.event='participant_details' BEGIN SELECT RAISE(FAIL,'injected'); END"));
        QVERIFY(!edit("Alice","first note","Alicia",""));
        QCOMPARE(net.members().first().toMap().value("name").toString(),QString("Alice"));
        QVERIFY(fault.exec("DROP TRIGGER reject_details"));
        QVERIFY(edit("Alice","first note","Alicia",""));
        QCOMPARE(net.members().first().toMap().value("name").toString(),QString("Alicia"));
        QCOMPARE(net.members().first().toMap().value("notes").toString(),QString());
        QVERIFY(!edit("Alice","first note","Lost update","no"));
        QVERIFY(external.selectSession(session.value("id").toLongLong()));
        QVERIFY(external.checkIn("K1AAA","Other operator",""));
        QVERIFY(!edit("Alicia","","Lost update","no"));
        QCOMPARE(net.members().first().toMap().value("name").toString(),QString("Other operator"));
        QVERIFY(external.closeSession());
        QVERIFY(!edit("Other operator","","Late edit","no"));
        QCOMPARE(db.qsoCount(),qint64(0));
        bool found=false, checkInFound=false; for (const auto& value : net.events()) {
            if (value.toMap().value("event").toString()==QLatin1String("participant_details")) found=true;
            if (value.toMap().value("event").toString()==QLatin1String("check_in")) checkInFound=true;
        }
        QVERIFY(found); QVERIFY(checkInFound);
        db.close(); LogDatabase reopened; QVERIFY(reopened.open(path)); RadioNet restored(&reopened); restored.load();
        QCOMPARE(restored.members().first().toMap().value("name").toString(),QString("Other operator"));
    }
    void checkInSurvivesConcurrentWriterAfterSessionRead()
    {
#ifndef Q_OS_MACOS
        QSKIP("Native authorizer fixture requires the verified system SQLite driver on macOS");
#endif
        if (!qEnvironmentVariableIsSet("DECOLOG_TEST_SYSTEM_SQLITE_AUTHORIZER")) QSKIP("Explicit opt-in required for verified system SQLite driver");
        QLibrary sqlite(QStringLiteral("sqlite3")); using Authorizer=int (*)(void*,int,const char*,const char*,const char*,const char*); using SetAuthorizer=int (*)(void*,Authorizer,void*);
        const auto setAuthorizer=reinterpret_cast<SetAuthorizer>(sqlite.resolve("sqlite3_set_authorizer")); if (!setAuthorizer) QSKIP("SQLite native authorizer unavailable");
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path=dir.filePath("race.sqlite"); LogDatabase db; QVERIFY(db.open(path));
        QSqlQuery wal(db.connection()); QVERIFY(wal.exec("PRAGMA journal_mode=WAL")); QVERIFY(wal.next()); QCOMPARE(wal.value(0).toString().toLower(),QString("wal")); wal.finish();
        RadioNet net(&db); QVERIFY(net.start("Net")); QVariant handle=db.connection().driver()->handle();
        if (QByteArray(handle.typeName())!="sqlite3*" || !handle.data()) QSKIP("SQLite native handle unavailable");
        void* native=*static_cast<void**>(handle.data()); QVERIFY(native);
        struct Gate { std::atomic_bool ready{false},entered{false},done{false},wrote{false},timedOut{false}; } gate;
        const auto hook=+[](void* opaque,int action,const char* table,const char*,const char*,const char*) {
            auto* g=static_cast<Gate*>(opaque); if (action==18 && table && std::strcmp(table,"radio_net_member")==0 && !g->entered.exchange(true)) {
                QElapsedTimer timer; timer.start(); while (!g->done.load() && timer.elapsed()<5000) QThread::msleep(1); if (!g->done.load()) g->timedOut=true;
            } return 0;
        };
        std::thread writer([&] { LogDatabase other; if (other.openExistingReadWrite(path)) { QSqlQuery q(other.connection()); q.exec("PRAGMA busy_timeout=0"); gate.ready=true;
            QElapsedTimer timer; timer.start(); while (!gate.entered.load() && timer.elapsed()<5000) QThread::msleep(1);
            if (gate.entered.load()) gate.wrote=q.exec("INSERT INTO app_setting(key,value) VALUES('race.writer','1')"); } gate.done=true; });
        QElapsedTimer ready; ready.start(); while (!gate.ready.load() && !gate.done.load() && ready.elapsed()<5000) QThread::msleep(1);
        if (!gate.ready.load()) { gate.done=true; writer.join(); QFAIL("Competing connection did not open"); }
        const int installed=setAuthorizer(native,hook,&gate); if (installed!=0) { gate.done=true; writer.join(); QFAIL("Cannot install SQLite authorizer"); }
        const bool checkedIn=net.checkIn("K1ABC","Alice",{}); const QString error=net.error();
        const int removed=setAuthorizer(native,nullptr,nullptr); writer.join(); QCOMPARE(removed,0);
        qInfo("RADIO_NET_WRITE_RACE competitor_committed=%d checkin=%d",gate.wrote.load()?1:0,checkedIn?1:0);
        QVERIFY(!gate.timedOut.load()); QVERIFY(gate.entered.load()); QVERIFY2(checkedIn,qPrintable(error));
        QVERIFY(!gate.wrote.load()); QCOMPARE(net.members().size(),1); QCOMPARE(net.members().first().toMap().value("call").toString(),QString("K1ABC"));
    }
    void replacedLogFileRejectsRadioNetActions()
    {
#ifndef Q_OS_UNIX
        QSKIP("Open file replacement fixture is currently tested on Unix only");
#else
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path=dir.filePath("log.sqlite"),copy=dir.filePath("copy.sqlite");
        LogDatabase db; QVERIFY(db.open(path)); RadioNet net(&db); QVERIFY(net.start("Net"));
        db.close(); QVERIFY(QFile::copy(path,copy)); QVERIFY(db.open(path)); net.load(); QVERIFY(!net.session().isEmpty());
        QVERIFY(QFile::rename(path,dir.filePath("original.sqlite"))); QVERIFY(QFile::copy(copy,path));
        QVERIFY(net.session().isEmpty()); QVERIFY(net.sessions().isEmpty()); QVERIFY(!net.checkIn("K1ABC","Alice",{})); QVERIFY(!net.start("Other"));
        QVERIFY(!net.reminderOverview().value("error").toString().isEmpty()); QCOMPARE(db.qsoCount(),qint64(0));
#endif
    }
    void staleSelectionCannotMutateDifferentLog()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto pathA=dir.filePath("a.sqlite"),pathB=dir.filePath("b.sqlite");
        { LogDatabase b; QVERIFY(b.open(pathB)); RadioNet netB(&b); QVERIFY(netB.start("Net B")); QCOMPARE(netB.session().value("id").toLongLong(),qint64(1)); }
        LogDatabase db; QVERIFY(db.open(pathA)); RadioNet net(&db); QVERIFY(net.start("Net A")); QCOMPARE(net.session().value("id").toLongLong(),qint64(1));
        db.close(); QVERIFY(db.open(pathB));
        QVERIFY2(!net.checkIn("K1ABC","Alice",{}),"A stale session ID must not add a member to another log");
        QVERIFY(!net.clearReminder()); QVERIFY(!net.closeSession()); QVERIFY(!net.reopenSession());
        QVERIFY(net.session().isEmpty()); QVERIFY(net.members().isEmpty());
        QSqlQuery q(db.connection()); QVERIFY(q.exec("SELECT COUNT(*) FROM radio_net_member")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),0);
        QVERIFY(net.selectSession(1)); QCOMPARE(net.session().value("title").toString(),QString("Net B")); QVERIFY(net.checkIn("K1ABC","Alice",{}));
        db.close(); QVERIFY(db.open(pathB)); QVERIFY(!net.checkIn("K2ABC","Bob",{})); QVERIFY(net.session().isEmpty());
        QVERIFY(net.selectSession(1)); QVERIFY(net.checkIn("K2ABC","Bob",{})); QCOMPARE(net.members().size(),2);
        QSqlQuery mutate(db.connection()); QVERIFY(mutate.exec("UPDATE radio_net_session SET uuid='00000000-0000-4000-8000-000000000001' WHERE id=1"));
        QVERIFY(net.session().isEmpty()); QVERIFY(!net.checkIn("K3ABC","Carol",{}));
        QVERIFY(net.selectSession(1)); QVERIFY(net.checkIn("K3ABC","Carol",{})); QCOMPARE(net.members().size(),3);
    }
    void guardedManualInsertRejectsSessionClosedByAnotherConnection()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const QString path=dir.filePath("log.sqlite"); LogDatabase first,other; QVERIFY(first.open(path)); QVERIFY(other.open(path));
        RadioNet net(&first); QVERIFY(net.start("Net")); QVERIFY(net.checkIn("K1ABC","Alice",""));
        const auto session=net.session(); const qint64 sessionId=session.value("id").toLongLong();
        QSqlQuery q(first.connection()); q.prepare("SELECT uuid FROM radio_net_session WHERE id=?"); q.addBindValue(sessionId); QVERIFY(q.exec()); QVERIFY(q.next()); const QString uuid=q.value(0).toString(); q.finish();
        q.prepare("SELECT id FROM radio_net_member WHERE session_id=? AND call=?"); q.addBindValue(sessionId); q.addBindValue("K1ABC"); QVERIFY(q.exec()); QVERIFY(q.next()); const qint64 memberId=q.value(0).toLongLong(); q.finish();
        AdifRecord record{{"CALL","K1ABC"},{"QSO_DATE","20261101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"APP_DECOLOG_RADIO_NET_SESSION",uuid},{"APP_DECOLOG_RADIO_NET_CALL","K1ABC"}};
        QCOMPARE(first.insertQso(record,"manual",{},true,0,false,sessionId,memberId).status,InsertResult::Status::Inserted);
        RadioNet otherNet(&other); QVERIFY(otherNet.selectSession(sessionId)); QVERIFY(otherNet.closeSession());
        const auto rejected=first.insertQso(record,"manual",{},true,0,false,sessionId,memberId); QVERIFY(rejected.status!=InsertResult::Status::Inserted); QCOMPARE(first.qsoCount(),qint64(1));
        // Comparison with the previous unguarded insertion path after the same closure.
        QCOMPARE(first.insertQso(record,"manual",{},true,0,false).status,InsertResult::Status::Inserted); QCOMPARE(first.qsoCount(),qint64(2));
        QCOMPARE(first.insertQso(record,"import",{},false,0,false).status,InsertResult::Status::Inserted);
    }
    void guardedInsertRejectsCloseBetweenCheckAndWrite()
    {
#ifndef Q_OS_MACOS
        QSKIP("Native authorizer fixture requires the verified system SQLite driver on macOS");
#endif
        if (!qEnvironmentVariableIsSet("DECOLOG_TEST_SYSTEM_SQLITE_AUTHORIZER")) QSKIP("Explicit opt-in required for verified system SQLite driver");
        QLibrary sqlite(QStringLiteral("sqlite3")); using Authorizer=int (*)(void*,int,const char*,const char*,const char*,const char*); using SetAuthorizer=int (*)(void*,Authorizer,void*);
        const auto setAuthorizer=reinterpret_cast<SetAuthorizer>(sqlite.resolve("sqlite3_set_authorizer")); if (!setAuthorizer) QSKIP("SQLite native authorizer unavailable");
        struct Gate { std::atomic_bool ready{false}, entered{false}, done{false}, closed{false}, timedOut{false}; };
        const auto hook=+[](void* opaque,int action,const char* table,const char*,const char*,const char*) {
            auto* gate=static_cast<Gate*>(opaque); if (action==18 && table && std::strcmp(table,"qso")==0 && !gate->entered.exchange(true)) {
                QElapsedTimer timer; timer.start(); while (!gate->done.load() && timer.elapsed()<5000) QThread::msleep(1); if (!gate->done.load()) gate->timedOut=true;
            } return 0;
        };
        for (const bool guarded : {false,true}) {
            QTemporaryDir dir; QVERIFY(dir.isValid()); const QString path=dir.filePath("race.sqlite"); LogDatabase db; QVERIFY(db.open(path));
            QSqlQuery q(db.connection()); QVERIFY(q.exec("PRAGMA journal_mode=WAL")); QVERIFY(q.next()); QCOMPARE(q.value(0).toString().toLower(),QString("wal")); q.finish();
            RadioNet net(&db); QVERIFY(net.start("Net")); QVERIFY(net.checkIn("K1ABC","Alice","")); const qint64 sid=net.session().value("id").toLongLong();
            q.prepare("SELECT s.uuid,m.id FROM radio_net_session s JOIN radio_net_member m ON m.session_id=s.id WHERE s.id=?"); q.addBindValue(sid); QVERIFY(q.exec()); QVERIFY(q.next()); const QString uuid=q.value(0).toString(); const qint64 mid=q.value(1).toLongLong(); q.finish();
            QVariant handle=db.connection().driver()->handle(); if (QByteArray(handle.typeName())!="sqlite3*" || !handle.data()) QSKIP("SQLite native handle unavailable"); void* native=*static_cast<void**>(handle.data()); QVERIFY(native);
            Gate gate; std::thread writer([&] { LogDatabase other; if (other.openExistingReadWrite(path)) { RadioNet otherNet(&other); if (otherNet.selectSession(sid)) { gate.ready=true; QElapsedTimer timer; timer.start(); while (!gate.entered.load() && timer.elapsed()<5000) QThread::msleep(1); if (gate.entered.load()) gate.closed=otherNet.closeSession(); } } gate.done=true; });
            QElapsedTimer readyTimer; readyTimer.start(); while (!gate.ready.load() && !gate.done.load() && readyTimer.elapsed()<5000) QThread::msleep(1);
            const bool ready=gate.ready.load(); const int installed=ready ? setAuthorizer(native,hook,&gate) : -1;
            InsertResult result; if (installed==0) { AdifRecord r{{"CALL","K1ABC"},{"QSO_DATE","20261101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"APP_DECOLOG_RADIO_NET_SESSION",uuid},{"APP_DECOLOG_RADIO_NET_CALL","K1ABC"}};
                result=db.insertQso(r,"manual",{},true,0,false,guarded?sid:0,guarded?mid:0); }
            setAuthorizer(native,nullptr,nullptr); writer.join();
            QVERIFY(ready); QCOMPARE(installed,0); QVERIFY(gate.entered.load()); QVERIFY(gate.done.load()); QVERIFY(gate.closed.load()); QVERIFY(!gate.timedOut.load());
            QCOMPARE(result.status==InsertResult::Status::Inserted,!guarded); QCOMPARE(db.qsoCount(),guarded?qint64(0):qint64(1));
        }
    }
    void guardedInsertRejectsUuidChangeBetweenCheckAndWrite()
    {
#ifndef Q_OS_MACOS
        QSKIP("Native authorizer fixture requires the verified system SQLite driver on macOS");
#endif
        if (!qEnvironmentVariableIsSet("DECOLOG_TEST_SYSTEM_SQLITE_AUTHORIZER")) QSKIP("Explicit opt-in required for verified system SQLite driver");
        QLibrary sqlite(QStringLiteral("sqlite3")); using Authorizer=int (*)(void*,int,const char*,const char*,const char*,const char*); using SetAuthorizer=int (*)(void*,Authorizer,void*);
        const auto setAuthorizer=reinterpret_cast<SetAuthorizer>(sqlite.resolve("sqlite3_set_authorizer")); if (!setAuthorizer) QSKIP("SQLite native authorizer unavailable");
        struct Gate { std::atomic_bool ready{false},entered{false},done{false},changed{false},timedOut{false}; };
        const auto hook=+[](void* opaque,int action,const char* table,const char*,const char*,const char*) {
            auto* gate=static_cast<Gate*>(opaque); if (action==18 && table && std::strcmp(table,"qso")==0 && !gate->entered.exchange(true)) {
                QElapsedTimer timer; timer.start(); while (!gate->done.load() && timer.elapsed()<5000) QThread::msleep(1); if (!gate->done.load()) gate->timedOut=true;
            } return 0;
        };
        for (const bool guarded:{false,true}) {
            QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path=dir.filePath("race.sqlite"); LogDatabase db; QVERIFY(db.open(path));
            QSqlQuery q(db.connection()); QVERIFY(q.exec("PRAGMA journal_mode=WAL")); QVERIFY(q.next()); QCOMPARE(q.value(0).toString().toLower(),QString("wal")); q.finish();
            RadioNet net(&db); QVERIFY(net.start("Net")); QVERIFY(net.checkIn("K1ABC","Alice",{})); const qint64 sid=net.session().value("id").toLongLong();
            q.prepare("SELECT s.uuid,m.id FROM radio_net_session s JOIN radio_net_member m ON m.session_id=s.id WHERE s.id=?"); q.addBindValue(sid); QVERIFY(q.exec()); QVERIFY(q.next());
            const auto uuid=q.value(0).toString(); const auto mid=q.value(1).toLongLong(); q.finish();
            QVariant handle=db.connection().driver()->handle(); if (QByteArray(handle.typeName())!="sqlite3*" || !handle.data()) QSKIP("SQLite native handle unavailable"); void* native=*static_cast<void**>(handle.data()); QVERIFY(native);
            Gate gate; std::thread writer([&] { LogDatabase other; if (other.openExistingReadWrite(path)) { gate.ready=true; QElapsedTimer timer; timer.start();
                while (!gate.entered.load() && timer.elapsed()<5000) QThread::msleep(1);
                if (gate.entered.load()) { QSqlQuery change(other.connection()); gate.changed=change.exec("UPDATE radio_net_session SET uuid='00000000-0000-4000-8000-000000000001' WHERE id=1"); }
            } gate.done=true; });
            QElapsedTimer readyTimer; readyTimer.start(); while (!gate.ready.load() && !gate.done.load() && readyTimer.elapsed()<5000) QThread::msleep(1);
            const bool ready=gate.ready.load(); const int installed=ready ? setAuthorizer(native,hook,&gate) : -1;
            InsertResult result; if (installed==0) { AdifRecord record{{"CALL","K1ABC"},{"QSO_DATE","20261101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"APP_DECOLOG_RADIO_NET_SESSION",uuid},{"APP_DECOLOG_RADIO_NET_CALL","K1ABC"}};
                result=db.insertQso(record,"manual",{},true,0,false,guarded?sid:0,guarded?mid:0); }
            const int removed=installed==0?setAuthorizer(native,nullptr,nullptr):0; writer.join();
            QVERIFY(ready); QCOMPARE(installed,0); QCOMPARE(removed,0); QVERIFY(gate.entered.load()); QVERIFY(gate.done.load()); QVERIFY(gate.changed.load()); QVERIFY(!gate.timedOut.load());
            QCOMPARE(result.status==InsertResult::Status::Inserted,!guarded); QCOMPARE(db.qsoCount(),guarded?qint64(0):qint64(1));
        }
    }
    void guardedSerialDraftSerializesConcurrentUuidChange()
    {
#ifndef Q_OS_MACOS
        QSKIP("Native authorizer fixture requires the verified system SQLite driver on macOS");
#endif
        if (!qEnvironmentVariableIsSet("DECOLOG_TEST_SYSTEM_SQLITE_AUTHORIZER")) QSKIP("Explicit opt-in required for verified system SQLite driver");
        QLibrary sqlite(QStringLiteral("sqlite3")); using Authorizer=int (*)(void*,int,const char*,const char*,const char*,const char*); using SetAuthorizer=int (*)(void*,Authorizer,void*);
        const auto setAuthorizer=reinterpret_cast<SetAuthorizer>(sqlite.resolve("sqlite3_set_authorizer")); if (!setAuthorizer) QSKIP("SQLite native authorizer unavailable");
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path=dir.filePath("serial-race.sqlite"); LogDatabase db; QVERIFY(db.open(path));
        QSqlQuery q(db.connection()); QVERIFY(q.exec("PRAGMA journal_mode=WAL")); QVERIFY(q.next()); QCOMPARE(q.value(0).toString().toLower(),QString("wal")); q.finish();
        RadioNet net(&db); QVERIFY(net.start("Net")); QVERIFY(net.checkIn("K1ABC","Alice",{})); const qint64 sid=net.session().value("id").toLongLong();
        q.prepare("SELECT s.uuid,m.id FROM radio_net_session s JOIN radio_net_member m ON m.session_id=s.id WHERE s.id=?"); q.addBindValue(sid); QVERIFY(q.exec()); QVERIFY(q.next());
        const auto uuid=q.value(0).toString(); const auto mid=q.value(1).toLongLong(); q.finish();
        QVERIFY(db.beginContestSerialRequest("event","node","req","authority",10).ok); QVERIFY(db.acceptContestSerialGrant("event","node","req","authority",10).ok);
        QVariant handle=db.connection().driver()->handle(); if (QByteArray(handle.typeName())!="sqlite3*" || !handle.data()) QSKIP("SQLite native handle unavailable"); void* native=*static_cast<void**>(handle.data()); QVERIFY(native);
        struct Gate { std::atomic_bool ready{false},entered{false},done{false},changed{false},timedOut{false}; } gate;
        const auto hook=+[](void* opaque,int action,const char* table,const char*,const char*,const char*) {
            auto* g=static_cast<Gate*>(opaque); if (action==18 && table && std::strcmp(table,"qso")==0 && !g->entered.exchange(true)) {
                QElapsedTimer timer; timer.start(); while (!g->done.load() && timer.elapsed()<5000) QThread::msleep(1); if (!g->done.load()) g->timedOut=true;
            } return 0;
        };
        std::thread writer([&] { LogDatabase other; if (other.openExistingReadWrite(path)) { gate.ready=true; QElapsedTimer timer; timer.start();
            while (!gate.entered.load() && timer.elapsed()<5000) QThread::msleep(1);
            if (gate.entered.load()) { QSqlQuery change(other.connection()); change.exec("PRAGMA busy_timeout=0"); gate.changed=change.exec("UPDATE radio_net_session SET uuid='00000000-0000-4000-8000-000000000001' WHERE id=1"); }
        } gate.done=true; });
        QElapsedTimer readyTimer; readyTimer.start(); while (!gate.ready.load() && !gate.done.load() && readyTimer.elapsed()<5000) QThread::msleep(1);
        const bool ready=gate.ready.load(); const int installed=ready?setAuthorizer(native,hook,&gate):-1;
        AdifRecord record{{"CALL","K1ABC"},{"QSO_DATE","20261101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"APP_DECOLOG_RADIO_NET_SESSION",uuid},{"APP_DECOLOG_RADIO_NET_CALL","K1ABC"}};
        InsertResult result; if (installed==0) result=db.insertQsoWithContestSerial(record,"manual","event","node","req","authority",{},true,0,false,sid,mid);
        const int removed=installed==0?setAuthorizer(native,nullptr,nullptr):0; writer.join();
        QVERIFY(ready); QCOMPARE(installed,0); QCOMPARE(removed,0); QVERIFY(gate.entered.load()); QVERIFY(gate.done.load()); QVERIFY(!gate.changed.load()); QVERIFY(!gate.timedOut.load());
        QCOMPARE(result.status,InsertResult::Status::Inserted); QCOMPARE(db.qsoCount(),qint64(1)); QCOMPARE(db.contestSerialRequest("event","node","req").state,QString("consumed"));
        QVERIFY(q.exec("UPDATE radio_net_session SET uuid='00000000-0000-4000-8000-000000000001' WHERE id=1"));
        QVERIFY(db.beginContestSerialRequest("event","node","req2","authority",11).ok); QVERIFY(db.acceptContestSerialGrant("event","node","req2","authority",11).ok);
        QVERIFY(db.insertQsoWithContestSerial(record,"manual","event","node","req2","authority",{},true,0,false,sid,mid).status!=InsertResult::Status::Inserted);
        QCOMPARE(db.contestSerialRequest("event","node","req2").state,QString("granted")); QCOMPARE(db.qsoCount(),qint64(1));
        record.set("APP_DECOLOG_RADIO_NET_SESSION","00000000-0000-4000-8000-000000000001");
        QCOMPARE(db.insertQsoWithContestSerial(record,"manual","event","node","req2","authority",{},true,0,false,sid,mid).status,InsertResult::Status::Inserted);
        QCOMPARE(db.contestSerialRequest("event","node","req2").state,QString("consumed")); QCOMPARE(db.qsoCount(),qint64(2));
    }
    void radioNetGuardAndContestSerialCommitTogether()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const QString path=dir.filePath("log.sqlite"); LogDatabase db,other; QVERIFY(db.open(path)); QVERIFY(other.open(path));
        RadioNet net(&db); QVERIFY(net.start("Net")); QVERIFY(net.checkIn("K1ABC","Alice","")); const qint64 sessionId=net.session().value("id").toLongLong();
        QSqlQuery q(db.connection()); q.prepare("SELECT s.uuid,m.id FROM radio_net_session s JOIN radio_net_member m ON m.session_id=s.id WHERE s.id=? AND m.call='K1ABC'"); q.addBindValue(sessionId); QVERIFY(q.exec()); QVERIFY(q.next()); const QString uuid=q.value(0).toString(); const qint64 memberId=q.value(1).toLongLong(); q.finish();
        AdifRecord record{{"CALL","K1ABC"},{"QSO_DATE","20261101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"APP_DECOLOG_RADIO_NET_SESSION",uuid},{"APP_DECOLOG_RADIO_NET_CALL","K1ABC"}};
        auto grant=[&](const QString& request,int serial) { const QString node=request==QLatin1String("legacy") ? QStringLiteral("node2") : QStringLiteral("node"); return db.beginContestSerialRequest("event",node,request,"authority",serial).ok && db.acceptContestSerialGrant("event",node,request,"authority",serial).ok; };
        QVERIFY(grant("first",10)); auto wrong=record; wrong.set("APP_DECOLOG_RADIO_NET_CALL","K1OTHER");
        QVERIFY(db.insertQsoWithContestSerial(wrong,"manual","event","node","first","authority",{},true,0,false,sessionId,memberId).status!=InsertResult::Status::Inserted);
        QCOMPARE(db.contestSerialRequest("event","node","first").state,QString("granted")); QCOMPARE(db.qsoCount(),qint64(0));
        const auto inserted=db.insertQsoWithContestSerial(record,"manual","event","node","first","authority",{},true,0,false,sessionId,memberId);
        QCOMPARE(inserted.status,InsertResult::Status::Inserted); const auto meta=db.meta(inserted.id); QVERIFY(meta); const auto stored=db.record(inserted.id); QVERIFY(stored);
        QCOMPARE(stored->value("STX"),QString("10")); QCOMPARE(stored->value("APP_DECOLOG_RADIO_NET_SESSION"),uuid);
        QCOMPARE(db.contestSerialRequest("event","node","first").state,QString("consumed")); QCOMPARE(db.contestSerialRequest("event","node","first").useId,meta->uuid);
        QVERIFY(grant("second",11)); QVERIFY(grant("legacy",12)); RadioNet otherNet(&other); QVERIFY(otherNet.selectSession(sessionId)); QVERIFY(otherNet.closeSession());
        QVERIFY(db.insertQsoWithContestSerial(record,"manual","event","node","second","authority",{},true,0,false,sessionId,memberId).status!=InsertResult::Status::Inserted);
        QCOMPARE(db.contestSerialRequest("event","node","second").state,QString("granted")); QCOMPARE(db.qsoCount(),qint64(1));
        QCOMPARE(db.insertQsoWithContestSerial(record,"manual","event","node2","legacy","authority",{},true,0,false).status,InsertResult::Status::Inserted);
        QCOMPARE(db.contestSerialRequest("event","node2","legacy").state,QString("consumed")); QCOMPARE(db.qsoCount(),qint64(2));
        db.close(); QVERIFY(db.open(path)); QCOMPARE(db.contestSerialRequest("event","node","second").state,QString("granted")); QCOMPARE(db.contestSerialRequest("event","node","first").useId,meta->uuid); QCOMPARE(db.qsoCount(),qint64(2));
    }
    void reportedQsoHistoryIsPagedAndSessionScoped()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        RadioNet net(&db);
        QVERIFY(net.metaObject()->indexOfMethod("reportedQsoPage(qlonglong)") >= 0);
        QVERIFY(net.start("First"));
        const qint64 first = net.session().value("id").toLongLong();
        QSqlQuery session(db.connection());
        QVERIFY(session.exec("SELECT uuid FROM radio_net_session WHERE id=" + QString::number(first)));
        QVERIFY(session.next());
        const QString uuid = session.value(0).toString();
        QSqlQuery plan(db.connection());
        QVERIFY(plan.exec("EXPLAIN QUERY PLAN SELECT id FROM qso WHERE deleted=0 AND "
                          "(CASE WHEN json_valid(adif_extra) THEN json_extract(adif_extra,'$.APP_DECOLOG_RADIO_NET_SESSION') END) IS NOT NULL AND "
                          "(CASE WHEN json_valid(adif_extra) THEN json_extract(adif_extra,'$.APP_DECOLOG_RADIO_NET_SESSION') END)='x' "
                          "AND id<999 ORDER BY id DESC LIMIT 201"));
        bool indexed = false;
        while (plan.next()) indexed |= plan.value(3).toString().contains("idx_qso_reported_radio_net");
        QVERIFY(indexed);
        for (int i = 0; i < 205; ++i) {
            AdifRecord r{{"CALL", "K1AAA"}, {"QSO_DATE", "20260101"}, {"TIME_ON", "120000"},
                         {"BAND", "20m"}, {"MODE", "CW"}, {"APP_DECOLOG_RADIO_NET_SESSION", uuid},
                         {"APP_DECOLOG_RADIO_NET_CALL", "K1AAA"}};
            QCOMPARE(db.insertQso(r, "manual", {}, true, 0, false).status, InsertResult::Status::Inserted);
        }
        QVariantList firstPage;
        QVERIFY(QMetaObject::invokeMethod(&net, "reportedQsoPage", Q_RETURN_ARG(QVariantList, firstPage), Q_ARG(qlonglong, 0)));
        QCOMPARE(firstPage.size(), 201);
        QVariantList older;
        QVERIFY(QMetaObject::invokeMethod(&net, "reportedQsoPage", Q_RETURN_ARG(QVariantList, older),
                                          Q_ARG(qlonglong, firstPage.at(199).toMap().value("id").toLongLong())));
        QCOMPARE(older.size(), 5);
        QCOMPARE(firstPage.first().toMap().value("reportedCall").toString(), QString("K1AAA"));
        QSqlQuery corrupt(db.connection());
        QVERIFY(corrupt.exec("UPDATE qso SET adif_extra='not json' WHERE id=1"));
        QVERIFY(corrupt.exec("UPDATE qso SET deleted=1 WHERE id=2"));
        QVariantList remaining;
        QVERIFY(QMetaObject::invokeMethod(&net, "reportedQsoPage", Q_RETURN_ARG(QVariantList, remaining),
                                          Q_ARG(qlonglong, firstPage.at(199).toMap().value("id").toLongLong())));
        QCOMPARE(remaining.size(), 3);
        QVERIFY(net.start("Second"));
        QVariantList empty;
        QVERIFY(QMetaObject::invokeMethod(&net, "reportedQsoPage", Q_RETURN_ARG(QVariantList, empty), Q_ARG(qlonglong, 0)));
        QVERIFY(empty.isEmpty());
        QCOMPARE(db.qsoCount(), 204);
    }
    void reminderSurvivesRestartAndCanBeCleared()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("log.sqlite");
        {
            LogDatabase db;
            QVERIFY(db.open(path));
            RadioNet net(&db);
            QVERIFY(net.metaObject()->indexOfMethod("setReminder(int)") >= 0);
            QVERIFY(net.start("Reminder"));
            bool accepted = true;
            QVERIFY(QMetaObject::invokeMethod(&net, "setReminder", Q_RETURN_ARG(bool, accepted), Q_ARG(int, 0)));
            QVERIFY(!accepted);
            QVERIFY(QMetaObject::invokeMethod(&net, "setReminder", Q_RETURN_ARG(bool, accepted), Q_ARG(int, 1)));
            QVERIFY(accepted);
            QVERIFY(net.property("reminder").toMap().value("scheduled").toBool());
            const QString deadline = net.property("reminder").toMap().value("deadline").toString();
            QSqlQuery q(db.connection());
            QVERIFY(q.exec("CREATE TRIGGER reject_reminder BEFORE INSERT ON radio_net_event WHEN "
                           "NEW.event='reminder_set' BEGIN SELECT RAISE(ABORT, 'rejected'); END"));
            QVERIFY(QMetaObject::invokeMethod(&net, "setReminder", Q_RETURN_ARG(bool, accepted), Q_ARG(int, 30)));
            QVERIFY(!accepted);
            QCOMPARE(net.property("reminder").toMap().value("deadline").toString(), deadline);
            QVERIFY(q.exec("DROP TRIGGER reject_reminder"));
            QCOMPARE(db.qsoCount(), 0);
        }
        {
            LogDatabase db;
            QVERIFY(db.open(path));
            RadioNet net(&db);
            net.load();
            QVERIFY(net.property("reminder").toMap().value("scheduled").toBool());
            QTRY_VERIFY_WITH_TIMEOUT(net.property("reminder").toMap().value("due").toBool(), 3000);
            bool cleared = false;
            QVERIFY(QMetaObject::invokeMethod(&net, "clearReminder", Q_RETURN_ARG(bool, cleared)));
            QVERIFY(cleared);
            QVERIFY(!net.property("reminder").toMap().value("scheduled").toBool());
            QCOMPARE(db.qsoCount(), 0);
        }
        {
            LogDatabase db;
            QVERIFY(db.open(path));
            RadioNet net(&db);
            net.load();
            QVERIFY(!net.property("reminder").toMap().value("scheduled").toBool());
        }
    }

    void reminderReadFailureIsDistinctFromNoReminders()
    {
        LogDatabase db; QVERIFY(db.open(":memory:")); RadioNet net(&db);
        QVERIFY(net.start("First"));
        QVERIFY(net.reminderOverview().value("error").toString().isEmpty());
        QVERIFY(net.reminderOverview().value("reminders").toList().isEmpty());
        QVERIFY(net.setReminder(60)); QCOMPARE(net.reminderOverview().value("reminders").toList().size(), 1);
        QSqlQuery q(db.connection()); QVERIFY(q.exec("UPDATE radio_net_event SET detail='invalid' WHERE event='reminder_set'"));
        QVERIFY(!net.reminderOverview().value("error").toString().isEmpty());
        QVERIFY(q.exec("DROP TABLE radio_net_event"));
        const auto failed=net.reminderOverview(); QVERIFY(!failed.value("error").toString().isEmpty());
        QVERIFY(failed.value("reminders").toList().isEmpty());
    }

    void remindersAreScopedToSessions()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        RadioNet net(&db);
        QVERIFY(net.start("First"));
        const qint64 first = net.session().value("id").toLongLong();
        QVERIFY(net.setReminder(60));
        const QString deadline = net.reminder().value("deadline").toString();
        QVERIFY(net.start("Second"));
        QVERIFY(!net.reminder().value("scheduled").toBool());
        QVERIFY(net.setReminder(120));
        QVERIFY(net.closeSession());
        QVERIFY(!net.setReminder(5));
        QVERIFY(net.clearReminder());
        QVERIFY(!net.reminder().value("scheduled").toBool());
        QVERIFY(net.selectSession(first));
        QCOMPARE(net.reminder().value("deadline").toString(), deadline);
        QVERIFY(net.setReminder(180));
        QVERIFY(net.reminder().value("deadline").toString() != deadline);
        QCOMPARE(db.qsoCount(), 0);
    }

    void multipleActiveRemindersSurviveReopen()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const QString path=dir.filePath("log.sqlite");
        qint64 first=0, second=0;
        {
            LogDatabase db; QVERIFY(db.open(path)); RadioNet net(&db);
            QVERIFY(net.start("First")); first=net.session().value("id").toLongLong(); QVERIFY(net.setReminder(60));
            QVERIFY(net.start("Second")); second=net.session().value("id").toLongLong(); QVERIFY(net.setReminder(120));
        }
        {
            LogDatabase db; QVERIFY(db.open(path)); RadioNet net(&db); net.load();
            const auto reminders=net.activeReminders(); QCOMPARE(reminders.size(), 2);
            QSet<qint64> ids; for (const auto &r : reminders) ids.insert(r.toMap().value("sessionId").toLongLong());
            QVERIFY(ids.contains(first)); QVERIFY(ids.contains(second));
            QVERIFY(net.selectSession(first)); QVERIFY(net.clearReminder());
            QCOMPARE(net.activeReminders().size(), 1);
            QCOMPARE(net.activeReminders().first().toMap().value("sessionId").toLongLong(), second);
            QCOMPARE(db.qsoCount(), qint64(0));
        }
    }

    void activeRemindersIncludeUnselectedSessions()
    {
        LogDatabase db; QVERIFY(db.open(":memory:")); RadioNet net(&db);
        QVERIFY(net.start("First")); const auto first=net.session().value("id").toLongLong();
        QVERIFY(net.setReminder(60)); QVERIFY(net.start("Second"));
        QCOMPARE(net.activeReminders().size(), 1);
        QCOMPARE(net.activeReminders().first().toMap().value("sessionId").toLongLong(), first);
        QVERIFY(net.setReminder(120)); QCOMPARE(net.activeReminders().size(), 2);
        QVERIFY(net.closeSession()); QCOMPARE(net.activeReminders().size(), 1);
        QVERIFY(net.selectSession(first)); QVERIFY(net.clearReminder()); QVERIFY(net.activeReminders().isEmpty());
        QCOMPARE(db.qsoCount(), qint64(0));
    }

    void pagesCompleteHistoryWithoutChangingRoster()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        RadioNet net(&db);
        QVERIFY(net.metaObject()->indexOfMethod("historyPage(qlonglong)") >= 0);
        QVERIFY(net.start("Paging"));
        QVERIFY(net.checkIn("K1AAA", "Alice", ""));
        const qint64 memberId = net.members().first().toMap().value("id").toLongLong();
        QSqlQuery plan(db.connection());
        QVERIFY(plan.exec("EXPLAIN QUERY PLAN SELECT e.id,e.event,e.recorded_at,e.detail,IFNULL(m.call,'') "
                          "FROM radio_net_event e LEFT JOIN radio_net_member m ON m.id=e.member_id "
                          "WHERE e.session_id=1 AND e.id<100000 ORDER BY e.id DESC LIMIT 201"));
        bool indexed = false;
        while (plan.next())
            indexed |= plan.value(3).toString().contains("idx_radio_net_event_session");
        QVERIFY(indexed);
        for (int i = 0; i < 405; ++i)
            QVERIFY(net.setStatus(memberId, (i % 2) ? "present" : "waiting"));
        QSet<qint64> ids;
        qint64 before = 0;
        qint64 originalNewest = 0;
        int pages = 0;
        for (;;) {
            QVariantList page;
            QVERIFY(QMetaObject::invokeMethod(&net, "historyPage", Q_RETURN_ARG(QVariantList, page),
                                              Q_ARG(qlonglong, before)));
            QVERIFY(page.size() <= 201);
            const int visible = qMin(200, int(page.size()));
            if (pages == 0) {
                originalNewest = page.first().toMap().value("id").toLongLong();
                QVERIFY(net.setStatus(memberId, "present"));
            }
            for (int i = 0; i < visible; ++i) {
                const qint64 id = page.at(i).toMap().value("id").toLongLong();
                QVERIFY(id > 0);
                QVERIFY(!ids.contains(id));
                if (before > 0)
                    QVERIFY(id < before);
                ids.insert(id);
            }
            ++pages;
            if (page.size() <= 200)
                break;
            before = page.at(199).toMap().value("id").toLongLong();
        }
        QCOMPARE(pages, 3);
        QCOMPARE(ids.size(), 407);
        QVERIFY(net.events().first().toMap().value("id").toLongLong() > originalNewest);
        QCOMPARE(net.members().size(), 1);
        QCOMPARE(db.qsoCount(), 0);
    }

    void summaryCountsCurrentStatusesAndReturns()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        RadioNet net(&db);
        QVERIFY(net.metaObject()->indexOfProperty("summary") >= 0);
        QVERIFY(net.start("Round table"));
        QVERIFY(net.checkIn("K1AAA", "Alice", ""));
        QVERIFY(net.checkIn("K1BBB", "Bob", ""));
        const qint64 bob = net.members().at(1).toMap().value("id").toLongLong();
        QVERIFY(net.setStatus(bob, "departed"));
        QVERIFY(net.checkIn("K1BBB", "", ""));
        const auto summary = net.property("summary").toMap();
        QCOMPARE(summary.value("participants").toInt(), 2);
        QCOMPARE(summary.value("present").toInt(), 2);
        QCOMPARE(summary.value("departed").toInt(), 0);
        QCOMPARE(summary.value("checkIns").toInt(), 3);
        QVERIFY(net.setStatus(bob, "waiting"));
        QCOMPARE(net.property("summary").toMap().value("waiting").toInt(), 1);
        QVERIFY(net.setStatus(bob, "absent"));
        QCOMPARE(net.property("summary").toMap().value("absent").toInt(), 1);
        QVERIFY(net.setStatus(bob, "departed"));
        QCOMPARE(net.property("summary").toMap().value("departed").toInt(), 1);
        QVERIFY(net.start("Another net"));
        QCOMPARE(net.property("summary").toMap().value("participants").toInt(), 0);
        QCOMPARE(net.property("summary").toMap().value("checkIns").toInt(), 0);
        QCOMPARE(db.qsoCount(), 0);
    }

    void sessionHasAnExportAction()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        RadioNet net(&db);
        QVERIFY(net.metaObject()->indexOfMethod("exportSession(QUrl)") >= 0);
    }

    void exportIncludesReportedQsoProvenance()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        LogDatabase db;
        QVERIFY(db.open(dir.filePath("log.sqlite")));
        RadioNet net(&db);
        QVERIFY(net.start("Test"));
        QSqlQuery q(db.connection());
        QVERIFY(q.exec("SELECT uuid FROM radio_net_session"));
        QVERIFY(q.next());
        const QString uuid = q.value(0).toString();
        AdifRecord record{{"CALL", "K1AAA"}, {"QSO_DATE", "20260101"}, {"TIME_ON", "120000"},
                          {"BAND", "20m"}, {"MODE", "CW"}, {"APP_DECOLOG_RADIO_NET_SESSION", uuid},
                          {"APP_DECOLOG_RADIO_NET_CALL", "K1BBB"}};
        QCOMPARE(db.insertQso(record, "import", {}, true, 0, false).status, InsertResult::Status::Inserted);
        const QString path = dir.filePath("net.json");
        QSignalSpy finished(&net, &RadioNet::exportFinished);
        QVERIFY(net.exportSession(QUrl::fromLocalFile(path)));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
        QVERIFY2(finished.first().at(0).toBool(), qPrintable(finished.first().at(1).toString()));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto array = QJsonDocument::fromJson(file.readAll()).object().value("reported_qsos").toArray();
        QCOMPARE(array.size(), 1);
        QCOMPARE(array.first().toObject().value("call").toString(), QString("K1AAA"));
        QCOMPARE(array.first().toObject().value("reported_member_call").toString(), QString("K1BBB"));
        QCOMPARE(array.first().toObject().value("source").toString(), QString("import"));
    }

    void exportsCompleteHistoryAndProtectsOpenLog()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString logPath = dir.filePath(QStringLiteral("log.sqlite"));
        LogDatabase db;
        QVERIFY(db.open(logPath));
        RadioNet net(&db);
        QVERIFY(net.start(QStringLiteral("Test net")));
        QVERIFY(net.checkIn(QStringLiteral("K1AAA"), QStringLiteral("Alè"), QStringLiteral("note, \"quoted\"")));
        const qint64 id = net.members().first().toMap().value(QStringLiteral("id")).toLongLong();
        for (int i = 0; i < 205; ++i)
            QVERIFY(net.setStatus(id, (i % 2) ? QStringLiteral("waiting") : QStringLiteral("present")));
        QVERIFY(!net.exportSession(QUrl::fromLocalFile(logPath)));
        QVERIFY(!net.exportSession(QUrl::fromLocalFile(logPath + QStringLiteral("-wal"))));
        QCOMPARE(db.qsoCount(), 0);
        const QString outputPath = dir.filePath(QStringLiteral("session.json"));
        QSignalSpy finished(&net, &RadioNet::exportFinished);
        QVERIFY(net.exportSession(QUrl::fromLocalFile(outputPath)));
        QVERIFY(net.exportBusy());
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
        QVERIFY2(finished.first().at(0).toBool(), qPrintable(finished.first().at(1).toString()));
        QVERIFY(!net.exportBusy());
        QFile output(outputPath);
        QVERIFY(output.open(QIODevice::ReadOnly));
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(output.readAll(), &parseError);
        QCOMPARE(parseError.error, QJsonParseError::NoError);
        const auto json = document.object();
        QCOMPARE(json.value(QStringLiteral("format")).toString(), QStringLiteral("decodxlog-radio-net-1"));
        QCOMPARE(json.value(QStringLiteral("participants")).toArray().size(), 1);
        const auto member = json.value(QStringLiteral("participants")).toArray().first().toObject();
        QCOMPARE(member.value(QStringLiteral("name")).toString(), QStringLiteral("Alè"));
        QCOMPARE(member.value(QStringLiteral("notes")).toString(), QStringLiteral("note, \"quoted\""));
        QCOMPARE(json.value(QStringLiteral("history")).toArray().size(), 207);
        QCOMPARE(net.events().size(), 200);
        QCOMPARE(db.qsoCount(), 0);
    }

    void exportReportsLogSwitchAfterWriting()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); LogDatabase db; QVERIFY(db.open(dir.filePath("first.sqlite")));
        RadioNet net(&db); QVERIFY(net.start("Original net"));
        const QString output=dir.filePath("net.json"); QSignalSpy finished(&net,&RadioNet::exportFinished);
        QVERIFY(net.exportSession(QUrl::fromLocalFile(output)));
        QElapsedTimer timer; timer.start();
        while (!QFile::exists(output) && timer.elapsed()<5000) QThread::msleep(1);
        QVERIFY(QFile::exists(output)); QVERIFY(db.open(dir.filePath("second.sqlite")));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,5000);
        QVERIFY(finished.first().at(0).toBool());
        QVERIFY(finished.first().at(1).toString().contains("original log",Qt::CaseInsensitive));
        QFile file(output); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(file.readAll()).object().value("session").toObject().value("title").toString(),QString("Original net"));
    }

    void exportRejectsReplacedLogFile()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const QString path=dir.filePath("log.sqlite");
        const QString replacement=dir.filePath("replacement.sqlite");
        { LogDatabase other; QVERIFY(other.open(replacement)); RadioNet net(&other); QVERIFY(net.start("Other net")); }
        LogDatabase db; QVERIFY(db.open(path)); RadioNet net(&db); QVERIFY(net.start("Original net"));
        QVERIFY(QFile::rename(path,dir.filePath("original.sqlite")));
        QVERIFY(QFile::rename(replacement,path));
        const QString output=dir.filePath("export.json");
        QVERIFY(!net.exportSession(QUrl::fromLocalFile(output)));
        QVERIFY(!QFile::exists(output));
    }

    void exportRejectsDatabaseHardlink()
    {
#ifdef Q_OS_UNIX
        QTemporaryDir dir; QVERIFY(dir.isValid()); const QString path=dir.filePath("log.sqlite");
        LogDatabase db; QVERIFY(db.open(path)); RadioNet net(&db); QVERIFY(net.start("Test"));
        const QString alias=dir.filePath("hardlink.sqlite"); std::error_code error;
        std::filesystem::create_hard_link(path.toStdString(),alias.toStdString(),error);
        QVERIFY2(!error,error.message().c_str()); QVERIFY(!net.exportSession(QUrl::fromLocalFile(alias)));
#else
        QSKIP("Hard-link fixture requires Unix");
#endif
    }

    void exportReportsUnwritableDestination()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        LogDatabase db;
        QVERIFY(db.open(dir.filePath(QStringLiteral("log.sqlite"))));
        RadioNet net(&db);
        QVERIFY(net.start(QStringLiteral("Test")));
        QSignalSpy finished(&net, &RadioNet::exportFinished);
        QVERIFY(net.exportSession(QUrl::fromLocalFile(dir.filePath(QStringLiteral("missing/out.json")))));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
        QVERIFY(!finished.first().at(0).toBool());
        QVERIFY(!finished.first().at(1).toString().isEmpty());
        QVERIFY(!net.exportBusy());
        QCOMPARE(db.qsoCount(), 0);
    }

    void cancelledExportPreservesExistingDestination()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        LogDatabase db;
        QVERIFY(db.open(dir.filePath(QStringLiteral("log.sqlite"))));
        RadioNet net(&db);
        QVERIFY(net.start(QStringLiteral("Test")));
        const QVariant sessionId = net.session().value(QStringLiteral("id"));
        QSqlDatabase sql = db.connection();
        QVERIFY(sql.transaction());
        QSqlQuery q(sql);
        QVERIFY(q.prepare("INSERT INTO radio_net_event(session_id,event,recorded_at,detail) VALUES(?, 'turn', "
                          "'2026-01-01T00:00:00Z', '')"));
        for (int i = 0; i < 10000; ++i) {
            q.bindValue(0, sessionId);
            QVERIFY(q.exec());
        }
        QVERIFY(sql.commit());
        const QString path = dir.filePath(QStringLiteral("existing.json"));
        QFile original(path);
        QVERIFY(original.open(QIODevice::WriteOnly));
        QCOMPARE(original.write("original"), qint64(8));
        original.close();
        QSignalSpy finished(&net, &RadioNet::exportFinished);
        QVERIFY(net.exportSession(QUrl::fromLocalFile(path)));
        net.cancelExport();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
        QVERIFY(!finished.first().at(0).toBool());
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), QByteArray("original"));
        QCOMPARE(db.qsoCount(), 0);
    }

    void twoConnectionsShareRosterWithoutDuplicateOrQso()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("shared.sqlite"));
        LogDatabase firstDb, secondDb;
        QVERIFY(firstDb.open(path));
        QVERIFY(secondDb.open(path));
        RadioNet first(&firstDb), second(&secondDb);
        QVERIFY(first.start(QStringLiteral("Local net")));
        second.load();
        QVERIFY(first.checkIn(QStringLiteral("K1AAA"), QStringLiteral("Alice"), QString()));
        QVERIFY(second.checkIn(QStringLiteral("k1aaa"), QString(), QStringLiteral("returned")));
        QCOMPARE(first.members().size(), 1);
        QCOMPARE(second.members().size(), 1);
        QCOMPARE(first.members().first().toMap().value(QStringLiteral("notes")).toString(), QStringLiteral("returned"));
        QVERIFY(second.closeSession());
        QVERIFY(!first.checkIn(QStringLiteral("K1BBB"), QString(), QString()));
        QCOMPARE(firstDb.qsoCount(), 0);
        QCOMPARE(secondDb.qsoCount(), 0);
    }

    void rosterPersistsWithoutCreatingQsos()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("net.sqlite"));
        qint64 sessionId = 0, firstId = 0, secondId = 0;
        {
            LogDatabase db;
            QVERIFY(db.open(path));
            RadioNet net(&db);
            net.load();
            QVERIFY(net.start(QStringLiteral("Sunday net")));
            sessionId = net.session().value(QStringLiteral("id")).toLongLong();
            QVERIFY(net.checkIn(QStringLiteral("k1aaa"), QStringLiteral("Alice"), QStringLiteral("note")));
            QVERIFY(net.checkIn(QStringLiteral("K1AAB"), QStringLiteral("Bob"), QString()));
            QCOMPARE(net.members().size(), 2);
            firstId = net.members().at(0).toMap().value(QStringLiteral("id")).toLongLong();
            secondId = net.members().at(1).toMap().value(QStringLiteral("id")).toLongLong();
            QVERIFY(net.setStatus(firstId, QStringLiteral("departed")));
            QVERIFY(net.checkIn(QStringLiteral("K1AAA"), QString(), QString()));
            QCOMPARE(net.members().size(), 2);
            QCOMPARE(net.members().at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Alice"));
            QVERIFY(net.checkIn(QStringLiteral("K1AAA"), QStringLiteral("Alicia"), QStringLiteral("new note")));
            QVERIFY(net.events()
                        .first()
                        .toMap()
                        .value(QStringLiteral("detail"))
                        .toString()
                        .contains(QStringLiteral("new note")));
            QVERIFY(net.moveMember(secondId, -1));
            QCOMPARE(net.members().at(0).toMap().value(QStringLiteral("id")).toLongLong(), secondId);
            QVERIFY(net.setStatus(firstId, QStringLiteral("absent")));
            QVERIFY(net.nextTurn());
            QCOMPARE(net.session().value(QStringLiteral("currentMemberId")).toLongLong(), secondId);
            QVERIFY(net.setStatus(firstId, QStringLiteral("waiting")));
            QVERIFY(net.nextTurn());
            QCOMPARE(net.session().value(QStringLiteral("currentMemberId")).toLongLong(), firstId);
            QVERIFY(net.setStatus(firstId, QStringLiteral("departed")));
            QVERIFY(net.setStatus(secondId, QStringLiteral("departed")));
            QVERIFY(!net.nextTurn());
            QVERIFY(!net.checkIn(QStringLiteral("K1?"), QString(), QString()));
            QCOMPARE(net.members().size(), 2);
            QCOMPARE(db.qsoCount(), 0);
        }
        {
            LogDatabase db;
            QVERIFY(db.open(path));
            RadioNet net(&db);
            net.load();
            QCOMPARE(net.session().value(QStringLiteral("id")).toLongLong(), sessionId);
            QCOMPARE(net.members().size(), 2);
            QCOMPARE(net.members().at(0).toMap().value(QStringLiteral("id")).toLongLong(), secondId);
            QVERIFY(net.events().size() >= 7);
            QVERIFY(net.closeSession());
            QVERIFY(!net.checkIn(QStringLiteral("K1XYZ"), QString(), QString()));
            QVERIFY(net.reopenSession());
            QVERIFY(net.checkIn(QStringLiteral("K1XYZ"), QString(), QString()));
            QCOMPARE(db.qsoCount(), 0);
        }
    }
    void failedHistoryWriteRollsBackCheckIn()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        RadioNet net(&db);
        net.load();
        QVERIFY(net.start(QStringLiteral("Test")));
        QSqlQuery q(db.connection());
        QVERIFY(q.exec("CREATE TRIGGER reject_radio_net_event BEFORE INSERT ON radio_net_event BEGIN SELECT "
                       "RAISE(ABORT,'injected'); END"));
        QVERIFY(!net.checkIn(QStringLiteral("K1AAA"), QString(), QString()));
        QVERIFY(!net.error().isEmpty());
        QVERIFY(net.members().isEmpty());
        QCOMPARE(db.qsoCount(), 0);
    }
};
QTEST_MAIN(TestRadioNet)
#include "tst_radionet.moc"
