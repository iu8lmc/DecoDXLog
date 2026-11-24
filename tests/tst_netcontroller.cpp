#include "app/NetController.h"
#include "core/ContestSerialProtocol.h"
#include "core/ContestNetSecure.h"
#include "core/ContestHandoff.h"
#include "core/LogDatabase.h"

#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QSettings>
#include <QSqlQuery>
#include <QJsonDocument>
#include <QSqlError>
#include <QFile>
#include <QElapsedTimer>
#include <QUdpSocket>
#include <memory>

class TestNetController : public QObject {
    Q_OBJECT
private slots:
    void skedBacklogDoesNotSpinWhenAllClaimsFail()
    {
        using namespace decolog::core; using decolog::app::NetController;
        QTemporaryDir dir; QVERIFY(dir.isValid()); QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());
        QCoreApplication::setOrganizationName("SkedFailureTest"); QCoreApplication::setApplicationName("Reminders"); QSettings().setValue("net/enabled",false);
        LogDatabase db; QVERIFY(db.open(dir.filePath("skeds.sqlite"))); int messages=0;
        NetController::Context ctx; ctx.db=&db; ctx.activity=[&](const QString& cat,const QString&,const QString&) { if(cat=="SKED") ++messages; };
        NetController net(ctx); net.setIdForTests("receiver"); QVERIFY(net.configureHandoffScope("event").isEmpty()); QCoreApplication::processEvents();
        const auto now=QDateTime::currentDateTimeUtc(); QVERIFY(db.connection().transaction()); QSqlQuery q(db.connection()); QVERIFY(q.prepare("INSERT INTO app_setting(key,value) VALUES(?,?)"));
        for (int i=1;i<=65;++i) {
            const QString id=QStringLiteral("00000000-0000-0000-0000-%1").arg(i,12,16,QLatin1Char('0'));
            contesthandoff::Offer o{id,"event","sender","receiver",QStringLiteral("K%1AAA").arg(i),14025000,now.toString(Qt::ISODate)}; o.scheduledUtc=now.addSecs(120).toString(Qt::ISODate);
            const auto frame=contesthandoff::offerFrame(o); QVERIFY(!frame.isEmpty());
            q.bindValue(0,"contest_handoff.in."+id); q.bindValue(1,QString::fromUtf8(QJsonDocument(frame).toJson(QJsonDocument::Compact))); QVERIFY(q.exec());
            q.bindValue(0,"contest_handoff.state."+id); q.bindValue(1,"accepted"); QVERIFY(q.exec());
        }
        QVERIFY(db.connection().commit()); QVERIFY(q.exec("CREATE TABLE sked_attempt_counter(n INTEGER NOT NULL)")); QVERIFY(q.exec("INSERT INTO sked_attempt_counter VALUES(0)"));
        QVERIFY(q.exec("CREATE TRIGGER reject_all_sked BEFORE INSERT ON app_setting WHEN NEW.key LIKE 'contest_handoff.reminder.%' BEGIN UPDATE sked_attempt_counter SET n=n+1; SELECT RAISE(FAIL,'injected'); END"));
        net.checkSkedReminders(now); QTest::qWait(250); QVERIFY(q.exec("SELECT n FROM sked_attempt_counter")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),64);
        QCOMPARE(messages,0); QCOMPARE(db.qsoCount(),qint64(0));
    }
    void skedBacklogContinuesWithoutWaitingThirtySeconds()
    {
        using namespace decolog::core; using decolog::app::NetController;
        QTemporaryDir dir; QVERIFY(dir.isValid()); QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());
        QCoreApplication::setOrganizationName("SkedBacklogTest"); QCoreApplication::setApplicationName("Reminders"); QSettings().setValue("net/enabled",false);
        LogDatabase db; QVERIFY(db.open(dir.filePath("skeds.sqlite"))); QStringList messages;
        NetController::Context ctx; ctx.db=&db; ctx.activity=[&](const QString& cat,const QString& message,const QString&) { if(cat=="SKED") messages << message; };
        NetController net(ctx); net.setIdForTests("receiver"); QVERIFY(net.configureHandoffScope("event").isEmpty()); QCoreApplication::processEvents();
        const auto now=QDateTime::currentDateTimeUtc(); const auto due=now.addSecs(120).toString(Qt::ISODate);
        QVERIFY(db.connection().transaction()); QSqlQuery q(db.connection()); QVERIFY(q.prepare("INSERT INTO app_setting(key,value) VALUES(?,?)"));
        for (int i=1;i<=130;++i) {
            const QString id=QStringLiteral("00000000-0000-0000-0000-%1").arg(i,12,16,QLatin1Char('0'));
            contesthandoff::Offer o{id,"event","sender","receiver",QStringLiteral("K%1AAA").arg(i),14025000,now.toString(Qt::ISODate)}; o.scheduledUtc=due;
            const auto frame=contesthandoff::offerFrame(o); QVERIFY(!frame.isEmpty());
            q.bindValue(0,"contest_handoff.in."+id); q.bindValue(1,QString::fromUtf8(QJsonDocument(frame).toJson(QJsonDocument::Compact))); QVERIFY(q.exec());
            q.bindValue(0,"contest_handoff.state."+id); q.bindValue(1,"accepted"); QVERIFY(q.exec());
        }
        QVERIFY(db.connection().commit()); net.checkSkedReminders(now); QCOMPARE(messages.size(),64);
        QTRY_COMPARE_WITH_TIMEOUT(messages.size(),130,3000); QTest::qWait(100); QCOMPARE(messages.size(),130); QCOMPARE(db.qsoCount(),qint64(0));
    }
    void benchmarkSkedReminderScan()
    {
        if (!qEnvironmentVariableIsSet("DECOLOG_BENCH_SKED_SCAN")) QSKIP("Opt-in synthetic timing comparison");
        using namespace decolog::core; using decolog::app::NetController;
        QTemporaryDir dir; QVERIFY(dir.isValid()); QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());
        QCoreApplication::setOrganizationName("SkedBenchmark"); QCoreApplication::setApplicationName("Reminders"); QSettings().setValue("net/enabled",false);
        LogDatabase db; QVERIFY(db.open(dir.filePath("skeds.sqlite"))); QVERIFY(db.connection().transaction()); QSqlQuery q(db.connection());
        QVERIFY(q.prepare("INSERT INTO app_setting(key,value) VALUES(?,?)"));
        for (int i=1;i<=1500;++i) {
            const QString id=QStringLiteral("00000000-0000-0000-0000-%1").arg(i,12,16,QLatin1Char('0'));
            contesthandoff::Offer o{id,"event","sender","receiver",QStringLiteral("K%1AAA").arg(i),14025000,"2026-11-24T12:00:00Z"}; o.scheduledUtc="2030-01-01T12:00:00Z";
            const auto frame=contesthandoff::offerFrame(o); QVERIFY(!frame.isEmpty());
            q.bindValue(0,"contest_handoff.in."+id); q.bindValue(1,QString::fromUtf8(QJsonDocument(frame).toJson(QJsonDocument::Compact))); QVERIFY(q.exec());
            q.bindValue(0,"contest_handoff.state."+id); q.bindValue(1,"accepted"); QVERIFY(q.exec());
        }
        QVERIFY(db.connection().commit()); const auto now=QDateTime::fromString("2030-01-01T12:00:00Z",Qt::ISODate);
        for (int i=0;i<3;++i) {
            int reminders=0; NetController::Context ctx; ctx.db=&db; ctx.activity=[&](const QString& cat,const QString&,const QString&) { if(cat=="SKED") ++reminders; };
            NetController net(ctx); net.setIdForTests("receiver"); QVERIFY(net.configureHandoffScope("event").isEmpty());
            QElapsedTimer timer; timer.start(); net.checkSkedReminders(now); const auto elapsed=timer.elapsed(); QCOMPARE(reminders,64);
            qInfo() << "SKED_SCAN_BENCH offers=1500 reminders=" << reminders << "sample=" << i << "elapsed_ms=" << elapsed;
        }
        QCOMPARE(db.qsoCount(),qint64(0));
    }
    void skedReminderDoesNotStarveLaterOffersAfterFailures()
    {
        using namespace decolog::core; using decolog::app::NetController;
        QTemporaryDir dir; QVERIFY(dir.isValid()); QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());
        QCoreApplication::setOrganizationName("SkedFairnessTest"); QCoreApplication::setApplicationName("Reminders"); QSettings().setValue("net/enabled",false);
        LogDatabase db; QVERIFY(db.open(dir.filePath("skeds.sqlite"))); QString last;
        for (int i=1;i<=65;++i) {
            const QString id=QStringLiteral("00000000-0000-0000-0000-%1").arg(i,12,16,QLatin1Char('0'));
            contesthandoff::Offer o{id,"event","sender","receiver",QStringLiteral("K%1AAA").arg(i),14025000,"2026-11-24T12:00:00Z"};
            o.scheduledUtc="2030-01-01T12:00:00Z"; QVERIFY(contesthandoff::receiveOffer(db,"event","receiver","sender",contesthandoff::offerFrame(o)).ok);
            QVERIFY(contesthandoff::setIncomingState(db,"event","receiver",id,"accepted").ok); last=id;
        }
        QSqlQuery q(db.connection()); const QString lastKey="contest_handoff.reminder."+last;
        QVERIFY2(q.exec(QStringLiteral("CREATE TRIGGER reject_sked_reminders BEFORE INSERT ON app_setting WHEN NEW.key LIKE 'contest_handoff.reminder.%%' AND NEW.key != '%1' BEGIN SELECT RAISE(FAIL,'injected'); END").arg(lastKey)),qPrintable(q.lastError().text()));
        QStringList messages; NetController::Context ctx; ctx.db=&db; ctx.activity=[&](const QString& cat,const QString& message,const QString&) { if(cat=="SKED") messages << message; };
        NetController net(ctx); net.setIdForTests("receiver"); QVERIFY(net.configureHandoffScope("event").isEmpty());
        const auto now=QDateTime::fromString("2030-01-01T12:00:00Z",Qt::ISODate);
        net.checkSkedReminders(now); QVERIFY(messages.isEmpty());
        net.checkSkedReminders(now); QCOMPARE(messages.size(),1); QVERIFY(messages.first().contains("K65AAA"));
        net.checkSkedReminders(now); QCOMPARE(messages.size(),1); QCOMPARE(db.qsoCount(),qint64(0));
        QVERIFY(q.exec("DROP TRIGGER reject_sked_reminders")); net.checkSkedReminders(now); net.checkSkedReminders(now);
        QCOMPARE(messages.size(),65); net.checkSkedReminders(now); QCOMPARE(messages.size(),65);
    }
    void skedReminderFiresOnceAndSurvivesReopen()
    {
        using namespace decolog::core; using decolog::app::NetController;
        QTemporaryDir dir; QVERIFY(dir.isValid()); QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());
        QCoreApplication::setOrganizationName("SkedReminderTest"); QCoreApplication::setApplicationName("Reminders"); QSettings().setValue("net/enabled",false);
        LogDatabase db; const auto path=dir.filePath("sked.sqlite"); QVERIFY(db.open(path));
        contesthandoff::Offer o{"8f3b7450-8e62-4dbd-8671-001122334455","event","sender","receiver","K1ABC",14025000,"2026-11-24T12:00:00Z"};
        o.scheduledUtc="2026-11-25T18:30:00Z"; QVERIFY(contesthandoff::receiveOffer(db,"event","receiver","sender",contesthandoff::offerFrame(o)).ok);
        QStringList messages; NetController::Context ctx; ctx.db=&db; ctx.activity=[&](const QString& cat,const QString& message,const QString&) { if(cat=="SKED") messages << message; };
        NetController net(ctx); net.setIdForTests("receiver"); QVERIFY(net.configureHandoffScope("event").isEmpty());
        const auto scheduled=QDateTime::fromString(o.scheduledUtc,Qt::ISODate);
        net.checkSkedReminders(scheduled); QVERIFY(messages.isEmpty());
        QVERIFY(net.setHandoffState(o.id,"accepted").isEmpty()); net.checkSkedReminders(scheduled.addSecs(-301)); QVERIFY(messages.isEmpty());
        net.checkSkedReminders(scheduled.addSecs(-300)); QCOMPARE(messages.size(),1); QVERIFY(messages.first().contains("K1ABC"));
        net.checkSkedReminders(scheduled); QCOMPARE(messages.size(),1);
        db.close(); QVERIFY(db.open(path)); QVERIFY(net.configureHandoffScope("event").isEmpty()); net.checkSkedReminders(scheduled); QCOMPARE(messages.size(),1);
        NetController restarted(ctx); restarted.setIdForTests("receiver"); QVERIFY(restarted.configureHandoffScope("event").isEmpty());
        restarted.checkSkedReminders(scheduled); QCOMPARE(messages.size(),2);
        restarted.checkSkedReminders(scheduled); QCOMPARE(messages.size(),2);
        QCOMPARE(db.qsoCount(),qint64(0));
        db.close(); QVERIFY(db.open(dir.filePath("another.sqlite")));
        QVERIFY(contesthandoff::receiveOffer(db,"event","receiver","sender",contesthandoff::offerFrame(o)).ok);
        QVERIFY(contesthandoff::setIncomingState(db,"event","receiver",o.id,"accepted").ok);
        QVERIFY(net.configureHandoffScope("event").isEmpty()); net.checkSkedReminders(scheduled); QCOMPARE(messages.size(),3);
    }
    void addressedHandoffTravelsBetweenControllersAndPersists()
    {
        using namespace decolog::core; using decolog::app::NetController;
        QTemporaryDir dir; QVERIFY(dir.isValid()); QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());
        QCoreApplication::setOrganizationName("HandoffTest"); QCoreApplication::setApplicationName("Pair"); QSettings().setValue("net/enabled",false);
        LogDatabase source,target; QVERIFY(source.open(dir.filePath("source.sqlite"))); QVERIFY(target.open(dir.filePath("target.sqlite")));
        QUdpSocket p1,p2; QVERIFY(p1.bind(QHostAddress::LocalHost,0)); QVERIFY(p2.bind(QHostAddress::LocalHost,0)); const auto senderPort=p1.localPort(),receiverPort=p2.localPort(); p1.close(); p2.close();
        NetController::Context aCtx; aCtx.db=&source; NetController a(aCtx); NetController::Context bCtx; bCtx.db=&target; NetController b(bCtx);
        a.setIdForTests("sender"); b.setIdForTests("receiver"); a.setGroup("FIELD"); b.setGroup("FIELD"); a.setPort(senderPort); b.setPort(receiverPort);
        a.setTarget(QHostAddress::LocalHost); b.setTarget(QHostAddress::LocalHost); a.setEnabled(true); b.setEnabled(true);
        QVERIFY(a.configureHandoffScope("cqww-2026").isEmpty()); QVERIFY(b.configureHandoffScope("cqww-2026").isEmpty());
        QVERIFY(!a.sendHandoffOffer("sender","K1ABC",14025000).isEmpty()); QVERIFY(!a.sendHandoffOffer("receiver","bad call",14025000).isEmpty());
        QSignalSpy sent(&a,&NetController::handoffOfferSent), statuses(&b,&NetController::handoffStatusSent); QVERIFY(a.sendHandoffOffer("receiver","K1ABC",14025000).isEmpty()); QCOMPARE(sent.size(),1);
        QCOMPARE(a.handoffOutbox().size(),1); QCOMPARE(b.handoffInbox().size(),0);
        const auto offerId=a.handoffOutbox().first().toMap().value("id").toString(); QVERIFY(!offerId.isEmpty());
        QVERIFY(a.retryHandoffOffer(offerId).isEmpty()); QCOMPARE(sent.size(),2); QCOMPARE(sent.last().first().toJsonObject(),sent.first().first().toJsonObject());
        QCOMPARE(a.handoffOutbox().size(),1); QVERIFY(!a.retryHandoffOffer("missing").isEmpty());
        auto body=sent.first().first().toJsonObject(); QUdpSocket relay;
        auto deliver=[&](int seq) { QJsonObject packet{{"offer",body},{"seq",seq},{"session","relay"}}; const auto bytes=ContestNet::pack("FIELD","sender","handoff_offer_v1",packet); return relay.writeDatagram(bytes,QHostAddress::LocalHost,receiverPort); };
        QVERIFY(b.configureHandoffScope("other-contest").isEmpty()); QVERIFY(deliver(7)>0); QTest::qWait(100); QVERIFY(b.handoffInbox().isEmpty());
        QVERIFY(b.configureHandoffScope("cqww-2026").isEmpty());
        QVERIFY(deliver(1)>0); QTRY_COMPARE_WITH_TIMEOUT(b.handoffInbox().size(),1,3000);
        QTRY_COMPARE(statuses.size(),1);
        auto deliverStatus=[&](int index,int seq) { QJsonObject packet{{"offer",statuses.at(index).at(0).toJsonObject()},{"state",statuses.at(index).at(1).toString()},{"seq",seq},{"session","status-relay"}}; const auto bytes=ContestNet::pack("FIELD","receiver","handoff_state_v1",packet); return relay.writeDatagram(bytes,QHostAddress::LocalHost,senderPort); };
        QVERIFY(deliverStatus(0,1)>0); QTRY_COMPARE_WITH_TIMEOUT(a.handoffOutbox().first().toMap().value("state").toString(),QString("pending"),3000);
        QCOMPARE(b.handoffInbox().first().toMap().value("state").toString(),QString("pending"));
        QVERIFY(!b.setHandoffState(offerId,"completed").isEmpty()); QVERIFY(b.setHandoffState(offerId,"accepted").isEmpty());
        QCOMPARE(statuses.size(),2); QVERIFY(deliverStatus(1,2)>0); QTRY_COMPARE_WITH_TIMEOUT(a.handoffOutbox().first().toMap().value("state").toString(),QString("accepted"),3000);
        QVERIFY(deliver(2)>0); QTest::qWait(100); QCOMPARE(b.handoffInbox().size(),1);
        QCOMPARE(b.handoffInbox().first().toMap().value("call").toString(),QString("K1ABC"));
        QSqlQuery corrupt(target.connection()); QVERIFY(corrupt.exec("INSERT INTO app_setting(key,value) VALUES('contest_handoff.in.bad','broken')"));
        QVERIFY(!b.handoffError().isEmpty()); QVERIFY(b.handoffInbox().isEmpty());
        QVERIFY(corrupt.exec("DELETE FROM app_setting WHERE key='contest_handoff.in.bad'")); QVERIFY(b.handoffError().isEmpty());
        QCOMPARE(source.qsoCount(),qint64(0)); QCOMPARE(target.qsoCount(),qint64(0));
        a.setEnabled(false); b.setEnabled(false); source.close(); target.close(); QVERIFY(source.open(dir.filePath("source.sqlite"))); QVERIFY(target.open(dir.filePath("target.sqlite")));
        QVERIFY(a.handoffOutbox().isEmpty()); QVERIFY(b.handoffInbox().isEmpty()); QVERIFY(!a.handoffError().isEmpty());
        QVERIFY(a.configureHandoffScope("cqww-2026").isEmpty()); QVERIFY(b.configureHandoffScope("cqww-2026").isEmpty());
        QCOMPARE(a.handoffOutbox().size(),1); QCOMPARE(b.handoffInbox().size(),1);
        QCOMPARE(a.handoffOutbox().first().toMap().value("state").toString(),QString("accepted"));
        QCOMPARE(b.handoffInbox().first().toMap().value("state").toString(),QString("accepted"));
        QVERIFY(b.setHandoffState(offerId,"completed").isEmpty());
        a.setEnabled(true); b.setEnabled(true); QVERIFY(a.retryHandoffOffer(offerId).isEmpty()); QCOMPARE(sent.size(),3);
        QVERIFY(b.sendHandoffStatus(offerId).isEmpty()); const int completedIndex=statuses.size()-1;
        QVERIFY(deliverStatus(completedIndex,3)>0); QTRY_COMPARE_WITH_TIMEOUT(a.handoffOutbox().first().toMap().value("state").toString(),QString("completed"),3000);
        QVERIFY(deliverStatus(1,4)>0); QTest::qWait(100); QCOMPARE(a.handoffOutbox().first().toMap().value("state").toString(),QString("completed"));
        QVERIFY(deliver(3)>0); QTest::qWait(100); QCOMPARE(b.handoffInbox().size(),1);
        QVERIFY(!a.sendSkedOffer("receiver","K2ABC",14026000,"2026-11-25T18:30:00+01:00").isEmpty());
        QVERIFY(a.sendSkedOffer("receiver","K2ABC",14026000,"2026-11-25T18:30:00Z").isEmpty());
        body=sent.last().first().toJsonObject(); QCOMPARE(body.value("version").toInt(),2);
        QVERIFY(deliver(4)>0); QTRY_COMPARE_WITH_TIMEOUT(b.handoffInbox().size(),2,3000);
        bool foundSked=false; for (const auto& item:b.handoffInbox()) if (item.toMap().value("call").toString()=="K2ABC")
            foundSked=item.toMap().value("scheduledUtc").toString()=="2026-11-25T18:30:00Z";
        QVERIFY(foundSked); QCOMPARE(source.qsoCount(),qint64(0)); QCOMPARE(target.qsoCount(),qint64(0));
        QSignalSpy changed(&a,&NetController::handoffChanged); a.setGroup("OTHER"); QVERIFY(!changed.isEmpty());
        QVERIFY(!a.handoffError().isEmpty()); QVERIFY(a.handoffOutbox().isEmpty()); QVERIFY(!a.retryHandoffOffer(offerId).isEmpty());
        a.setGroup("FIELD"); QVERIFY(!a.handoffError().isEmpty()); QVERIFY(a.handoffOutbox().isEmpty());
        QVERIFY(a.configureHandoffScope("cqww-2026").isEmpty()); QCOMPARE(a.handoffOutbox().size(),2);
        changed.clear(); source.close(); QVERIFY(source.open(dir.filePath("source.sqlite")));
        QTRY_VERIFY_WITH_TIMEOUT(!changed.isEmpty(),3000); QVERIFY(!a.handoffError().isEmpty()); QVERIFY(a.handoffOutbox().isEmpty());
        a.setEnabled(false); b.setEnabled(false);
    }
    void experimentalQsoTransferBetweenControllersRecoversMissingFragment()
    {
        using namespace decolog::core; using decolog::app::NetController;
        QTemporaryDir dir; QVERIFY(dir.isValid()); QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());
        QCoreApplication::setOrganizationName("DecoNetTransferTest"); QCoreApplication::setApplicationName("Pair"); QSettings().setValue("net/enabled",false);
        LogDatabase source,destination; QVERIFY(source.open(dir.filePath("source.sqlite"))); QVERIFY(destination.open(dir.filePath("dest.sqlite")));
        AdifRecord record{{"CALL","K1AAA"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"COMMENT",QString(1800,'x')}};
        const auto q=source.insertQso(record,"fixture"); QVERIFY(q.id>0); const auto uuid=source.meta(q.id)->uuid;
        QUdpSocket p1,p2; QVERIFY(p1.bind(QHostAddress::LocalHost,0)); QVERIFY(p2.bind(QHostAddress::LocalHost,0)); const auto senderPort=p1.localPort(),receiverPort=p2.localPort(); p1.close(); p2.close();
        NetController::Context aCtx; aCtx.db=&source; NetController a(aCtx); NetController::Context bCtx; bCtx.db=&destination; NetController b(bCtx);
        a.setIdForTests("sender"); b.setIdForTests("receiver"); a.setGroup("FIELD"); b.setGroup("FIELD"); a.setPort(senderPort); b.setPort(receiverPort);
        a.setTarget(QHostAddress::LocalHost); b.setTarget(QHostAddress::LocalHost); a.setEnabled(true); b.setEnabled(true);
        QVERIFY(b.armExperimentalQsoReceiver("sender","transfer").isEmpty());
        QUdpSocket relay; int seq=0; QList<QJsonObject> captured; bool retries=false;
        auto forward=[&](const QString& from,const QString& type,QJsonObject body,quint16 port) {
            body.insert("seq",++seq); body.insert("session","relay"); const auto packet=ContestNet::pack("FIELD",from,type,body); relay.writeDatagram(packet,QHostAddress::LocalHost,port);
        };
        connect(&a,&NetController::experimentalQsoFrameSent,&relay,[&](const QString& type,const QJsonObject& frame) {
            if (type!="qso_chunk_v1") return; if (!retries) captured.append(frame); else forward("sender",type,frame,receiverPort);
        });
        connect(&b,&NetController::experimentalQsoFrameSent,&relay,[&](const QString& type,const QJsonObject& frame) { if (type=="qso_missing_v1") forward("receiver",type,frame,senderPort); });
        QVERIFY(a.sendExperimentalQso("receiver",uuid,"transfer").isEmpty()); QTRY_VERIFY_WITH_TIMEOUT(!a.experimentalQsoSending(),10000); QVERIFY(captured.size()>=4);
        for (int i=0;i<captured.size();++i) {
            auto altered=captured.at(i); if (i==0) { auto bytes=QByteArray::fromBase64(altered.value("data").toString().toLatin1()); bytes[0]='X'; altered.insert("data",QString::fromLatin1(bytes.toBase64())); }
            forward("sender","qso_chunk_v1",altered,receiverPort);
        }
        QTRY_VERIFY_WITH_TIMEOUT(b.experimentalQsoStatus().contains("integrity"),3000); QVERIFY(!b.experimentalQsoPreview().value("complete").toBool()); QVERIFY(!b.requestExperimentalQsoMissing().isEmpty());
        QSignalSpy stoppedRetries(&b,&NetController::experimentalQsoFrameSent); QTest::qWait(2200); QCOMPARE(stoppedRetries.size(),0);
        QVERIFY(b.armExperimentalQsoReceiver("sender","transfer").isEmpty());
        forward("intruder","qso_chunk_v1",captured.first(),receiverPort); QTest::qWait(80); QVERIFY(!b.experimentalQsoPreview().value("complete").toBool());
        for (int i=captured.size()-1;i>0;--i) forward("sender","qso_chunk_v1",captured.at(i),receiverPort);
        QTest::qWait(150); QVERIFY(!b.experimentalQsoPreview().value("complete").toBool()); QCOMPARE(destination.qsoCount(),qint64(0));
        retries=true; QVERIFY(b.requestExperimentalQsoMissing().isEmpty()); QTRY_VERIFY_WITH_TIMEOUT(b.experimentalQsoPreview().value("complete").toBool(),10000);
        QCOMPARE(b.experimentalQsoPreview().value("uuid").toString(),uuid); QCOMPARE(b.experimentalQsoPreview().value("call").toString(),QString("K1AAA"));
        QCOMPARE(destination.qsoCount(),qint64(0)); QVERIFY(b.reviewExperimentalQso()); QTRY_VERIFY_WITH_TIMEOUT(!b.qsoReviewBusy(),10000);
        QVERIFY(b.qsoReview().value("fromNetwork").toBool()); QVERIFY(!b.qsoReview().value("transportSecure").toBool()); QVERIFY(b.qsoReview().value("canInsert").toBool());
        QVERIFY(b.applyReviewedMissingQso().isEmpty()); QCOMPARE(destination.qsoCount(),qint64(1)); QCOMPARE(source.qsoCount(),qint64(1)); a.setEnabled(false); b.setEnabled(false); QVERIFY(b.experimentalQsoPreview().isEmpty());
    }
    void experimentalQsoTransferAutomaticallyRecoversAllInitialLoss()
    {
        using namespace decolog::core; using decolog::app::NetController;
        QTemporaryDir dir; QVERIFY(dir.isValid()); QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());
        QCoreApplication::setOrganizationName("DecoNetTransferTest"); QCoreApplication::setApplicationName("AutoRetry"); QSettings().setValue("net/enabled",false);
        LogDatabase source,destination; QVERIFY(source.open(dir.filePath("source.sqlite"))); QVERIFY(destination.open(dir.filePath("dest.sqlite")));
        const auto q=source.insertQso(AdifRecord{{"CALL","K1AAA"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"COMMENT",QString(1800,'x')}},"fixture"); QVERIFY(q.id>0);
        QUdpSocket p1,p2; QVERIFY(p1.bind(QHostAddress::LocalHost,0)); QVERIFY(p2.bind(QHostAddress::LocalHost,0)); const auto senderPort=p1.localPort(),receiverPort=p2.localPort(); p1.close(); p2.close();
        NetController::Context aCtx; aCtx.db=&source; NetController a(aCtx); NetController::Context bCtx; bCtx.db=&destination; NetController b(bCtx);
        a.setIdForTests("sender"); b.setIdForTests("receiver"); a.setGroup("FIELD"); b.setGroup("FIELD"); a.setPort(senderPort); b.setPort(receiverPort); a.setTarget(QHostAddress::LocalHost); b.setTarget(QHostAddress::LocalHost);
        QUdpSocket relay; int seq=0, firstRequests=0,missingRequests=0,retryFrames=0; bool dropInitial=true;
        auto forward=[&](const QString& from,const QString& type,QJsonObject body,quint16 port) { body.insert("seq",++seq); body.insert("session","auto-relay"); const auto bytes=ContestNet::pack("FIELD",from,type,body); relay.writeDatagram(bytes,QHostAddress::LocalHost,port); };
        connect(&a,&NetController::experimentalQsoFrameSent,&relay,[&](const QString& type,const QJsonObject& frame) { if (type!="qso_chunk_v1" || dropInitial) return; ++retryFrames; forward("sender",type,frame,receiverPort); });
        connect(&b,&NetController::experimentalQsoFrameSent,&relay,[&](const QString& type,const QJsonObject& frame) { if (type=="qso_first_v1") ++firstRequests; else if (type=="qso_missing_v1") ++missingRequests; else return; forward("receiver",type,frame,senderPort); });
        a.setEnabled(true); b.setEnabled(true); QVERIFY(b.armExperimentalQsoReceiver("sender","transfer").isEmpty()); QVERIFY(a.sendExperimentalQso("receiver",source.meta(q.id)->uuid,"transfer").isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!a.experimentalQsoSending(),10000); QVERIFY(!b.experimentalQsoPreview().value("complete").toBool()); dropInitial=false;
        QTRY_VERIFY_WITH_TIMEOUT(b.experimentalQsoPreview().value("complete").toBool(),9000); QVERIFY(firstRequests>0 && missingRequests>0); QCOMPARE(destination.qsoCount(),qint64(0));
        QTest::qWait(1100); const int before=retryFrames; for (int i=0;i<5;++i) forward("receiver","qso_first_v1",QJsonObject{{"version",1},{"transfer","transfer"},{"to","sender"}},senderPort);
        QTRY_VERIFY_WITH_TIMEOUT(retryFrames>before,3000); QTest::qWait(500); QCOMPARE(retryFrames-before,1);
        a.setEnabled(false); b.setEnabled(false);
    }
    void experimentalSecureQsoTransferRecoversInitialLoss()
    {
        using namespace decolog::core; using decolog::app::NetController;
        QTemporaryDir dir; QVERIFY(dir.isValid()); QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());
        QCoreApplication::setOrganizationName("DecoNetTransferTest"); QCoreApplication::setApplicationName("SecureRetry"); QSettings().setValue("net/enabled",false);
        LogDatabase source,destination; QVERIFY(source.open(dir.filePath("source.sqlite"))); QVERIFY(destination.open(dir.filePath("dest.sqlite")));
        const auto q=source.insertQso(AdifRecord{{"CALL","K1AAA"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"COMMENT",QString(1800,'x')}},"fixture"); QVERIFY(q.id>0);
        QUdpSocket p1,p2; QVERIFY(p1.bind(QHostAddress::LocalHost,0)); QVERIFY(p2.bind(QHostAddress::LocalHost,0)); const auto senderPort=p1.localPort(),receiverPort=p2.localPort(); p1.close(); p2.close();
        NetController::Context aCtx; aCtx.db=&source; NetController a(aCtx); NetController::Context bCtx; bCtx.db=&destination; NetController b(bCtx);
        a.setIdForTests("sender"); b.setIdForTests("receiver"); a.setGroup("FIELD"); b.setGroup("FIELD"); a.setPort(senderPort); b.setPort(receiverPort); a.setTarget(QHostAddress::LocalHost); b.setTarget(QHostAddress::LocalHost); a.setEnabled(true); b.setEnabled(true);
        const QString key(64,'a'); QVERIFY(a.setExperimentalQsoKey(key).isEmpty()); QVERIFY(b.setExperimentalQsoKey(key).isEmpty());
        QUdpSocket relay; int seq=0,requests=0,retryFrames=0; bool dropInitial=true;
        auto forward=[&](const QString& from,const QString& type,QJsonObject body,quint16 port) { body.insert("seq",++seq); body.insert("session","secure-auto-relay"); const auto bytes=ContestNet::pack("FIELD",from,type,body); relay.writeDatagram(bytes,QHostAddress::LocalHost,port); };
        connect(&a,&NetController::experimentalQsoFrameSent,&relay,[&](const QString& type,const QJsonObject& frame) { if (type!="qso_secure_v1" || dropInitial) return; ++retryFrames; forward("sender",type,frame,receiverPort); });
        connect(&b,&NetController::experimentalQsoFrameSent,&relay,[&](const QString& type,const QJsonObject& frame) { if (type!="qso_secure_v1") return; ++requests; forward("receiver",type,frame,senderPort); });
        QVERIFY(b.armExperimentalQsoReceiver("sender","transfer").isEmpty()); QVERIFY(a.sendExperimentalQso("receiver",source.meta(q.id)->uuid,"transfer").isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!a.experimentalQsoSending(),10000); QVERIFY(!b.experimentalQsoPreview().value("complete").toBool()); dropInitial=false;
        QTRY_VERIFY_WITH_TIMEOUT(b.experimentalQsoPreview().value("complete").toBool(),9000); QVERIFY(requests>=2 && retryFrames>=4); QCOMPARE(destination.qsoCount(),qint64(0));
        QTest::qWait(1100); const int before=retryFrames; const auto rawKey=QByteArray::fromHex(key.toLatin1());
        for (int i=0;i<5;++i) { const auto protectedRequest=contestsecure::seal(rawKey,"FIELD","receiver","sender","qso_first_v1",QJsonObject{{"version",1},{"transfer","transfer"},{"to","sender"}}); QVERIFY(protectedRequest.ok); forward("receiver","qso_secure_v1",protectedRequest.frame,senderPort); }
        QTRY_VERIFY_WITH_TIMEOUT(retryFrames>before,3000); QTest::qWait(500); QCOMPARE(retryFrames-before,1);
        a.setEnabled(false); b.setEnabled(false); QVERIFY(!a.experimentalQsoSecure() && !b.experimentalQsoSecure());
    }
    void experimentalSecureQsoTransferRejectsWrongKeyAndReplay()
    {
        using namespace decolog::core; using decolog::app::NetController;
        QTemporaryDir dir; QVERIFY(dir.isValid()); QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());
        QCoreApplication::setOrganizationName("DecoNetTransferTest"); QCoreApplication::setApplicationName("SecurePair"); QSettings().setValue("net/enabled",false);
        LogDatabase source,destination; QVERIFY(source.open(dir.filePath("source.sqlite"))); QVERIFY(destination.open(dir.filePath("dest.sqlite")));
        const auto q=source.insertQso(AdifRecord{{"CALL","K1AAA"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"COMMENT",QString(1800,'x')}},"fixture"); QVERIFY(q.id>0);
        QUdpSocket p1,p2; QVERIFY(p1.bind(QHostAddress::LocalHost,0)); QVERIFY(p2.bind(QHostAddress::LocalHost,0)); const auto senderPort=p1.localPort(),receiverPort=p2.localPort(); p1.close(); p2.close();
        NetController::Context aCtx; aCtx.db=&source; NetController a(aCtx); NetController::Context bCtx; bCtx.db=&destination; NetController b(bCtx);
        a.setIdForTests("sender"); b.setIdForTests("receiver"); a.setGroup("FIELD"); b.setGroup("FIELD"); a.setPort(senderPort); b.setPort(receiverPort); a.setTarget(QHostAddress::LocalHost); b.setTarget(QHostAddress::LocalHost); a.setEnabled(true); b.setEnabled(true);
        const QString key(64,'a'),wrong(64,'b'); QCOMPARE(a.generateExperimentalQsoKey().size(),64); QVERIFY(!a.setExperimentalQsoKey("invalid").isEmpty());
        QVERIFY(a.setExperimentalQsoKey(key).isEmpty()); QVERIFY(b.setExperimentalQsoKey(wrong).isEmpty()); QVERIFY(a.experimentalQsoSecure() && b.experimentalQsoSecure()); QVERIFY(b.armExperimentalQsoReceiver("sender","transfer").isEmpty());
        QSignalSpy sent(&a,&NetController::experimentalQsoFrameSent); QUdpSocket relay; int seq=0; QList<QJsonObject> frames;
        auto forward=[&](const QString& from,const QString& type,QJsonObject body,quint16 port) { body.insert("seq",++seq); body.insert("session","secure-relay"); const auto bytes=ContestNet::pack("FIELD",from,type,body); relay.writeDatagram(bytes,QHostAddress::LocalHost,port); };
        QVERIFY(a.sendExperimentalQso("receiver",source.meta(q.id)->uuid,"transfer").isEmpty()); QTRY_VERIFY_WITH_TIMEOUT(!a.experimentalQsoSending(),10000); QVERIFY(sent.size()>=4);
        int largestDatagram=0;
        for (const auto& item:sent) { QCOMPARE(item.at(0).toString(),QString("qso_secure_v1")); const auto frame=item.at(1).toJsonObject(); QVERIFY(!QJsonDocument(frame).toJson().contains("K1AAA"));
            auto envelope=frame; envelope.insert("seq",1); envelope.insert("session",QString(36,'s')); envelope.insert("protocol",2); envelope.insert("name","station");
            largestDatagram=qMax(largestDatagram,int(ContestNet::pack("FIELD","sender","qso_secure_v1",envelope).size())); frames.append(frame); forward("sender","qso_secure_v1",frame,receiverPort); }
        qInfo("Synthetic protected QSO largest datagram: %d bytes",largestDatagram); QVERIFY(largestDatagram<=1472);
        QTest::qWait(200); QVERIFY(!b.experimentalQsoPreview().value("complete").toBool());
        QVERIFY(b.setExperimentalQsoKey(key).isEmpty()); QVERIFY(b.armExperimentalQsoReceiver("sender","transfer").isEmpty());
        ContestNetInventory inventory; QVERIFY(inventory.begin(dir.filePath("source.sqlite"))); const auto page=inventory.findUuid(source.meta(q.id)->uuid); QVERIFY(page.ok && page.rows.size()==1);
        contestqso::Encoder plain("transfer",inventory.logIdentity(),page.rows.first()); QVERIFY(plain.ok());
        for (int i=0;i<plain.count();++i) { auto body=plain.frame(i); body.insert("to","receiver"); forward("sender","qso_chunk_v1",body,receiverPort); }
        QTest::qWait(120); QVERIFY(!b.experimentalQsoPreview().value("complete").toBool());
        for (const auto& frame:frames) forward("sender","qso_secure_v1",frame,receiverPort);
        QTRY_VERIFY_WITH_TIMEOUT(b.experimentalQsoPreview().value("complete").toBool(),5000); QCOMPARE(b.experimentalQsoPreview().value("call").toString(),QString("K1AAA"));
        QSignalSpy changed(&b,&NetController::experimentalQsoChanged); forward("sender","qso_secure_v1",frames.first(),receiverPort); QTest::qWait(200); QCOMPARE(changed.size(),0);
        QVERIFY(b.reviewExperimentalQso()); QTRY_VERIFY_WITH_TIMEOUT(!b.qsoReviewBusy(),10000); QVERIFY(b.qsoReview().value("ok").toBool()); QVERIFY(b.qsoReview().value("fromNetwork").toBool()); QVERIFY(b.qsoReview().value("canInsert").toBool());
        AdifRecord independent{{"CALL","K1BBB"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}}; QVERIFY(destination.insertQso(independent,"fixture").id>0);
        QVERIFY(!b.applyReviewedMissingQso().isEmpty()); QCOMPARE(destination.qsoCount(),qint64(1));
        QVERIFY(b.reviewExperimentalQso()); QTRY_VERIFY_WITH_TIMEOUT(!b.qsoReviewBusy(),10000); QVERIFY(b.qsoReview().value("canInsert").toBool());
        QVERIFY(b.applyReviewedMissingQso().isEmpty()); QCOMPARE(destination.qsoCount(),qint64(2)); QVERIFY(destination.idForUuid(source.meta(q.id)->uuid)>0);
        a.setStationName(QString(1000,'n')); const int beforeOversize=sent.size(); QVERIFY(a.sendExperimentalQso("receiver",source.meta(q.id)->uuid,"oversized").isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!a.experimentalQsoSending(),10000); QVERIFY(a.experimentalQsoStatus().contains("too large")); QCOMPARE(sent.size(),beforeOversize);
        QCOMPARE(destination.qsoCount(),qint64(2)); a.setEnabled(false); b.setEnabled(false);
    }
    void experimentalSecureQsoReviewAppliesEditAndDeletion()
    {
        using namespace decolog::core; using decolog::app::NetController;
        QTemporaryDir dir; QVERIFY(dir.isValid()); QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());
        QCoreApplication::setOrganizationName("DecoNetTransferTest"); QCoreApplication::setApplicationName("NetworkReview"); QSettings().setValue("net/enabled",false);
        LogDatabase source,target; QVERIFY(source.open(dir.filePath("source.sqlite"))); QVERIFY(target.open(dir.filePath("target.sqlite")));
        AdifRecord record{{"CALL","K1AAA"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"COMMENT",QString(2050,'x')},{"NAME","Remote"}};
        const auto aQso=source.insertQso(record,"fixture"); record.set("NAME","Local"); const auto bQso=target.insertQso(record,"fixture"); QVERIFY(aQso.id>0 && bQso.id>0); const auto uuid=source.meta(aQso.id)->uuid; QVERIFY(target.adoptUuid(bQso.id,uuid));
        QUdpSocket p1,p2; QVERIFY(p1.bind(QHostAddress::LocalHost,0)); QVERIFY(p2.bind(QHostAddress::LocalHost,0)); const auto senderPort=p1.localPort(),receiverPort=p2.localPort(); p1.close(); p2.close();
        int changes=0; bool deleted=false; NetController::Context aCtx; aCtx.db=&source; NetController a(aCtx); NetController::Context bCtx; bCtx.db=&target;
        bCtx.reviewedQsoChanged=[&](qint64 id,bool isDeleted,const QString&) { QCOMPARE(id,bQso.id); ++changes; deleted=isDeleted; }; NetController b(bCtx);
        a.setIdForTests("sender"); b.setIdForTests("receiver"); a.setGroup("FIELD"); b.setGroup("FIELD"); a.setPort(senderPort); b.setPort(receiverPort); a.setTarget(QHostAddress::LocalHost); b.setTarget(QHostAddress::LocalHost); a.setEnabled(true); b.setEnabled(true);
        const QString key(64,'a'); QVERIFY(a.setExperimentalQsoKey(key).isEmpty()); QVERIFY(b.setExperimentalQsoKey(key).isEmpty()); QUdpSocket relay; int seq=0;
        auto forward=[&](const QString& from,const QString& type,QJsonObject body,quint16 port) { body.insert("seq",++seq); body.insert("session","network-review-relay"); const auto bytes=ContestNet::pack("FIELD",from,type,body); relay.writeDatagram(bytes,QHostAddress::LocalHost,port); };
        connect(&a,&NetController::experimentalQsoFrameSent,&relay,[&](const QString& type,const QJsonObject& frame) { if (type=="qso_secure_v1") forward("sender",type,frame,receiverPort); });
        connect(&b,&NetController::experimentalQsoFrameSent,&relay,[&](const QString& type,const QJsonObject& frame) { if (type=="qso_secure_v1") forward("receiver",type,frame,senderPort); });
        QVERIFY(b.armExperimentalQsoReceiver("sender","edit").isEmpty()); QVERIFY(a.sendExperimentalQso("receiver",uuid,"edit").isEmpty()); QTRY_VERIFY_WITH_TIMEOUT(b.experimentalQsoPreview().value("complete").toBool(),10000);
        auto readLong=[&] {
            const auto fields=b.qsoReview().value("fields").toList();
            for (int i=0;i<fields.size();++i) for (const auto& part:{QStringLiteral("name"),QStringLiteral("local"),QStringLiteral("remote")}) {
                if (fields[i].toMap().value(part).toString().size()<=2048) continue; int offset=0;
                for (;;) { const auto page=b.qsoLongFieldPage(i,part,offset); QVERIFY(!page.isEmpty()); if (page.value("done").toBool()) break; offset=page.value("next").toInt(); }
            }
        };
        QVERIFY(b.reviewExperimentalQso()); QTRY_VERIFY_WITH_TIMEOUT(!b.qsoReviewBusy(),10000); QVERIFY(!b.qsoReview().value("canApply").toBool()); QVERIFY(b.qsoReview().value("hasLongFields").toBool()); readLong(); QVERIFY(b.qsoReview().value("canApply").toBool()); QVERIFY(b.qsoReview().value("transportSecure").toBool());
        auto local=*target.record(bQso.id); local.set("NAME","Concurrent"); QVERIFY(target.updateQso(bQso.id,local).status==InsertResult::Status::Inserted);
        QVERIFY(!b.applyReviewedExistingQso().isEmpty()); QCOMPARE(target.record(bQso.id)->value("NAME"),QString("Concurrent")); QCOMPARE(changes,0);
        QVERIFY(b.reviewExperimentalQso()); QTRY_VERIFY_WITH_TIMEOUT(!b.qsoReviewBusy(),10000); readLong(); QVERIFY(b.applyReviewedExistingQso().isEmpty()); QCOMPARE(target.record(bQso.id)->value("NAME"),QString("Remote")); QCOMPARE(changes,1); QVERIFY(!deleted);
        QVERIFY(source.softDeleteQso(aQso.id)); QVERIFY(b.armExperimentalQsoReceiver("sender","delete").isEmpty()); QVERIFY(a.sendExperimentalQso("receiver",uuid,"delete").isEmpty()); QTRY_VERIFY_WITH_TIMEOUT(b.experimentalQsoPreview().value("complete").toBool(),10000);
        QVERIFY(b.reviewExperimentalQso()); QTRY_VERIFY_WITH_TIMEOUT(!b.qsoReviewBusy(),10000); readLong(); QVERIFY(b.qsoReview().value("canApply").toBool()); QVERIFY(b.qsoReview().value("remoteDeleted").toBool());
        b.clearExperimentalQso(); QVERIFY(!b.applyReviewedExistingQso().isEmpty()); QVERIFY(!target.meta(bQso.id)->deleted);
        QVERIFY(b.setExperimentalQsoKey(key).isEmpty()); QVERIFY(b.armExperimentalQsoReceiver("sender","delete-retry").isEmpty()); QVERIFY(a.sendExperimentalQso("receiver",uuid,"delete-retry").isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(b.experimentalQsoPreview().value("complete").toBool(),10000); QVERIFY(b.reviewExperimentalQso()); QTRY_VERIFY_WITH_TIMEOUT(!b.qsoReviewBusy(),10000); readLong();
        QVERIFY(b.applyReviewedExistingQso().isEmpty()); QVERIFY(target.meta(bQso.id)->deleted); QCOMPARE(changes,2); QVERIFY(deleted); a.setEnabled(false); b.setEnabled(false);
    }
    void offlineQsoComparisonShowsDifferencesAndExpiresOnLocalEdit()
    {
        using namespace decolog::core; using decolog::app::NetController;
        QTemporaryDir dir; QVERIFY(dir.isValid()); QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());
        QCoreApplication::setOrganizationName("DecoComparisonTest"); QCoreApplication::setApplicationName("Preview"); QSettings().setValue("net/enabled",false);
        const auto localPath=dir.filePath("local.sqlite"),remotePath=dir.filePath("remote.sqlite");
        LogDatabase local,remote; QVERIFY(local.open(localPath)); QVERIFY(remote.open(remotePath));
        auto insert=[](LogDatabase& db,const QString& call,const QString& name,const QString& uuid) {
            const auto qso=db.insertQso(AdifRecord{{"CALL",call},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"NAME",name}},"fixture",{},false,0,false);
            return qso.id>0 && db.adoptUuid(qso.id,uuid) ? qso.id : qint64(0);
        };
        const auto localId=insert(local,"K1AAA","Local","shared"), remoteId=insert(remote,"K1AAA","Remote","shared"); QVERIFY(localId>0 && remoteId>0);
        QVERIFY(insert(local,"K1BBB","Only here","local-only")>0); QVERIFY(insert(remote,"K1CCC","Only there","remote-only")>0);
        NetController::Context ctx; ctx.db=&local; NetController net(ctx); QSignalSpy changes(&net,&NetController::qsoComparisonChanged);
        QVERIFY(net.startQsoComparison(QUrl::fromLocalFile(remotePath))); QTRY_VERIFY_WITH_TIMEOUT(!net.qsoComparisonBusy(),10000);
        const auto summary=net.qsoComparisonSummary(); QCOMPARE(summary.value("contentConflict").toInt(),1); QCOMPARE(summary.value("localOnly").toInt(),1); QCOMPARE(summary.value("remoteOnly").toInt(),1);
        const auto rows=net.qsoComparisonRows(); QCOMPARE(rows.size(),3); bool hasCall=false, hasNameDiff=false;
        for (const auto& item:rows) if (item.toMap().value("localCall").toString()=="K1AAA") {
            hasCall=true; for (const auto& field:item.toMap().value("fields").toList()) {
                const auto values=field.toMap(); if (values.value("name").toString()=="NAME" && values.value("local").toString()=="Local" && values.value("remote").toString()=="Remote") hasNameDiff=true;
            }
        }
        QVERIFY(hasCall); QVERIFY(hasNameDiff);
        QCOMPARE(local.record(localId)->value("NAME"),QString("Local")); QCOMPARE(remote.record(remoteId)->value("NAME"),QString("Remote"));
        auto changed=*local.record(localId); changed.set("NAME","After comparison"); QVERIFY(local.updateQso(localId,changed).status==InsertResult::Status::Inserted);
        QTRY_VERIFY_WITH_TIMEOUT(net.qsoComparisonSummary().value("stale").toBool(),4000); QVERIFY(changes.size()>0);
        net.cancelQsoComparison(); QVERIFY(net.qsoComparisonSummary().isEmpty()); QVERIFY(net.qsoComparisonRows().isEmpty());
    }
    void offlineQsoComparisonReviewsOneRecordWithoutChangingLogs()
    {
        using namespace decolog::core; using decolog::app::NetController;
        QTemporaryDir dir; QVERIFY(dir.isValid()); QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());
        QCoreApplication::setOrganizationName("DecoComparisonTest"); QCoreApplication::setApplicationName("RecordReview"); QSettings().setValue("net/enabled",false);
        const auto localPath=dir.filePath("local.sqlite"), remotePath=dir.filePath("remote.sqlite"); LogDatabase local,remote; QVERIFY(local.open(localPath)); QVERIFY(remote.open(remotePath));
        AdifRecord record{{"CALL","K1AAA"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"COMMENT",QString(400,'a')}};
        const auto a=local.insertQso(record,"fixture"); QVERIFY(a.id>0); record.set("COMMENT",QString(400,'b')); const auto b=remote.insertQso(record,"fixture"); QVERIFY(b.id>0);
        QVERIFY(local.adoptUuid(a.id,"shared")); QVERIFY(remote.adoptUuid(b.id,"shared")); NetController::Context ctx; ctx.db=&local; NetController net(ctx);
        QVERIFY(!net.reviewQsoComparison("shared")); QVERIFY(net.startQsoComparison(QUrl::fromLocalFile(remotePath))); QTRY_VERIFY_WITH_TIMEOUT(!net.qsoComparisonBusy(),10000);
        QVERIFY(net.reviewQsoComparison("shared")); QTRY_VERIFY_WITH_TIMEOUT(!net.qsoReviewBusy(),10000); const auto review=net.qsoReview(); QVERIFY2(review.value("ok").toBool(),qPrintable(review.value("error").toString()));
        bool found=false; for (const auto& item:review.value("fields").toList()) { const auto field=item.toMap(); if (field.value("name")=="COMMENT") { QCOMPARE(field.value("local").toString(),QString(400,'a')); QCOMPARE(field.value("remote").toString(),QString(400,'b')); found=true; } } QVERIFY(found);
        QCOMPARE(local.record(a.id)->value("COMMENT"),QString(400,'a')); QCOMPARE(remote.record(b.id)->value("COMMENT"),QString(400,'b'));
        record.set("COMMENT","remote changed"); QVERIFY(remote.updateQso(b.id,record).status==InsertResult::Status::Inserted);
        QVERIFY(!net.reviewQsoComparison("shared"));
    }
    void offlineComparisonImportsOnlyReviewedMissingRecord()
    {
        using namespace decolog::core; using decolog::app::NetController;
        QTemporaryDir dir; QVERIFY(dir.isValid()); QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());
        QCoreApplication::setOrganizationName("DecoComparisonTest"); QCoreApplication::setApplicationName("ImportReview"); QSettings().setValue("net/enabled",false);
        const auto localPath=dir.filePath("local.sqlite"), remotePath=dir.filePath("remote.sqlite"); LogDatabase local,remote; QVERIFY(local.open(localPath)); QVERIFY(remote.open(remotePath));
        AdifRecord record{{"CALL","K1AAA"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"COMMENT","review"}};
        const auto inserted=remote.insertQso(record,"fixture"); QVERIFY(inserted.id>0); const auto uuid=remote.meta(inserted.id)->uuid;
        int notifications=0; NetController::Context ctx; ctx.db=&local; ctx.reviewedQsoInserted=[&](qint64){ ++notifications; }; NetController net(ctx);
        QVERIFY(!net.applyReviewedMissingQso().isEmpty()); QCOMPARE(local.qsoCount(),qint64(0));
        QVERIFY(net.startQsoComparison(QUrl::fromLocalFile(remotePath))); QTRY_VERIFY_WITH_TIMEOUT(!net.qsoComparisonBusy(),10000);
        QVERIFY(net.reviewQsoComparison(uuid)); QTRY_VERIFY_WITH_TIMEOUT(!net.qsoReviewBusy(),10000); QVERIFY(net.qsoReview().value("canInsert").toBool());
        record.set("COMMENT","changed"); QVERIFY(remote.updateQso(inserted.id,record).status==InsertResult::Status::Inserted);
        QVERIFY(!net.applyReviewedMissingQso().isEmpty()); QCOMPARE(local.qsoCount(),qint64(0)); QCOMPARE(notifications,0);
        QVERIFY(net.startQsoComparison(QUrl::fromLocalFile(remotePath))); QTRY_VERIFY_WITH_TIMEOUT(!net.qsoComparisonBusy(),10000);
        QVERIFY(net.reviewQsoComparison(uuid)); QTRY_VERIFY_WITH_TIMEOUT(!net.qsoReviewBusy(),10000); QVERIFY(net.qsoReview().value("canInsert").toBool());
        QVERIFY(net.applyReviewedMissingQso().isEmpty()); QCOMPARE(local.qsoCount(),qint64(1)); QCOMPARE(notifications,1);
        const auto id=local.idForUuid(uuid); QVERIFY(id>0); QCOMPARE(local.record(id)->value("COMMENT"),QString("changed")); QVERIFY(net.qsoComparisonSummary().value("stale").toBool());
        QVERIFY(!net.applyReviewedMissingQso().isEmpty()); QCOMPARE(local.qsoCount(),qint64(1)); QCOMPARE(notifications,1);
    }
    void offlineComparisonAppliesOnlyReviewedChanges()
    {
        using namespace decolog::core; using decolog::app::NetController;
        QTemporaryDir dir; QVERIFY(dir.isValid()); QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());
        QCoreApplication::setOrganizationName("DecoComparisonTest"); QCoreApplication::setApplicationName("ApplyReview"); QSettings().setValue("net/enabled",false);
        const auto localPath=dir.filePath("local.sqlite"), remotePath=dir.filePath("remote.sqlite"); LogDatabase local,remote; QVERIFY(local.open(localPath)); QVERIFY(remote.open(remotePath));
        AdifRecord record{{"CALL","K1AAA"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"NAME","Local"}};
        const auto a=local.insertQso(record,"fixture"); record.set("NAME","Remote"); const auto b=remote.insertQso(record,"fixture"); QVERIFY(a.id>0 && b.id>0);
        QVERIFY(local.adoptUuid(a.id,"shared")); QVERIFY(remote.adoptUuid(b.id,"shared")); int notifications=0; bool deleted=false;
        NetController::Context ctx; ctx.db=&local; ctx.reviewedQsoChanged=[&](qint64 id,bool isDeleted,const QString&){ QCOMPARE(id,a.id); ++notifications; deleted=isDeleted; }; NetController net(ctx);
        QVERIFY(!net.applyReviewedExistingQso().isEmpty()); QVERIFY(net.startQsoComparison(QUrl::fromLocalFile(remotePath))); QTRY_VERIFY_WITH_TIMEOUT(!net.qsoComparisonBusy(),10000);
        QVERIFY(net.reviewQsoComparison("shared")); QTRY_VERIFY_WITH_TIMEOUT(!net.qsoReviewBusy(),10000); QVERIFY(net.qsoReview().value("canApply").toBool());
        auto edited=*local.record(a.id); edited.set("NAME","Concurrent"); QVERIFY(local.updateQso(a.id,edited).status==InsertResult::Status::Inserted);
        QVERIFY(!net.applyReviewedExistingQso().isEmpty()); QCOMPARE(local.record(a.id)->value("NAME"),QString("Concurrent")); QCOMPARE(notifications,0);
        QVERIFY(net.startQsoComparison(QUrl::fromLocalFile(remotePath))); QTRY_VERIFY_WITH_TIMEOUT(!net.qsoComparisonBusy(),10000); QVERIFY(net.reviewQsoComparison("shared")); QTRY_VERIFY_WITH_TIMEOUT(!net.qsoReviewBusy(),10000);
        QVERIFY(net.applyReviewedExistingQso().isEmpty()); QCOMPARE(local.record(a.id)->value("NAME"),QString("Remote")); QCOMPARE(notifications,1); QVERIFY(!deleted);
        QVERIFY(remote.softDeleteQso(b.id)); QVERIFY(net.startQsoComparison(QUrl::fromLocalFile(remotePath))); QTRY_VERIFY_WITH_TIMEOUT(!net.qsoComparisonBusy(),10000);
        QVERIFY(net.reviewQsoComparison("shared")); QTRY_VERIFY_WITH_TIMEOUT(!net.qsoReviewBusy(),10000); QVERIFY(net.qsoReview().value("canApply").toBool()); QVERIFY(net.qsoReview().value("remoteDeleted").toBool());
        QVERIFY(net.applyReviewedExistingQso().isEmpty()); QVERIFY(local.meta(a.id)->deleted); QCOMPARE(notifications,2); QVERIFY(deleted);
    }
    void offlineComparisonDoesNotApplyUnseenLongValues()
    {
        using namespace decolog::core; using decolog::app::NetController;
        QTemporaryDir dir; QVERIFY(dir.isValid()); QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());
        QCoreApplication::setOrganizationName("DecoComparisonTest"); QCoreApplication::setApplicationName("LongReview"); QSettings().setValue("net/enabled",false);
        const auto localPath=dir.filePath("local.sqlite"), remotePath=dir.filePath("remote.sqlite"); LogDatabase local,remote; QVERIFY(local.open(localPath)); QVERIFY(remote.open(remotePath));
        AdifRecord record{{"CALL","K1AAA"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"COMMENT",QString(2047,'x')+QString::fromUtf8("😀")+QStringLiteral("x")},{"NAME",QString(2050,'n')}};
        const auto q=remote.insertQso(record,"fixture"); QVERIFY(q.id>0); NetController::Context ctx; ctx.db=&local; NetController net(ctx);
        QVERIFY(net.startQsoComparison(QUrl::fromLocalFile(remotePath))); QTRY_VERIFY_WITH_TIMEOUT(!net.qsoComparisonBusy(),10000);
        QVERIFY(net.reviewQsoComparison(remote.meta(q.id)->uuid)); QTRY_VERIFY_WITH_TIMEOUT(!net.qsoReviewBusy(),10000);
        QVERIFY(net.qsoReview().value("ok").toBool()); QVERIFY(net.qsoReview().value("hasLongFields").toBool()); QVERIFY(!net.qsoReview().value("canInsert").toBool());
        QVERIFY(!net.applyReviewedMissingQso().isEmpty()); QCOMPARE(local.qsoCount(),qint64(0));
        int index=-1; const auto fields=net.qsoReview().value("fields").toList(); for (int i=0;i<fields.size();++i) if (fields[i].toMap().value("name")=="COMMENT") index=i; QVERIFY(index>=0);
        QVERIFY(net.qsoLongFieldPage(index,"remote",2048).isEmpty());
        const auto first=net.qsoLongFieldPage(index,"remote",0); QCOMPARE(first.value("text").toString(),QString(2047,'x')); QVERIFY(!first.value("done").toBool());
        QVERIFY(!net.qsoReview().value("canInsert").toBool());
        const auto last=net.qsoLongFieldPage(index,"remote",first.value("next").toInt()); QCOMPARE(last.value("text").toString(),QString::fromUtf8("😀x")); QVERIFY(last.value("done").toBool());
        QVERIFY(!net.qsoReview().value("canInsert").toBool()); int nameIndex=-1;
        for (int i=0;i<fields.size();++i) if (fields[i].toMap().value("name")=="NAME") nameIndex=i; QVERIFY(nameIndex>=0);
        const auto nameFirst=net.qsoLongFieldPage(nameIndex,"remote",0); QVERIFY(!nameFirst.isEmpty());
        const auto nameLast=net.qsoLongFieldPage(nameIndex,"remote",nameFirst.value("next").toInt()); QVERIFY(nameLast.value("done").toBool());
        QVERIFY(net.qsoReview().value("canInsert").toBool());
        QVERIFY(!net.qsoLongFieldPage(nameIndex,"remote",0).isEmpty()); QVERIFY(!net.qsoReview().value("canInsert").toBool());
        QVERIFY(net.qsoLongFieldPage(nameIndex,"remote",2048).value("done").toBool());
        QVERIFY(net.qsoReview().value("canInsert").toBool()); QVERIFY(net.applyReviewedMissingQso().isEmpty()); QCOMPARE(local.qsoCount(),qint64(1));
    }
    void offlineQsoComparisonCancelsWithoutPublishingPartialRows()
    {
        using namespace decolog::core; using decolog::app::NetController;
        QTemporaryDir dir; QVERIFY(dir.isValid()); QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());
        QCoreApplication::setOrganizationName("DecoComparisonTest"); QCoreApplication::setApplicationName("Cancel"); QSettings().setValue("net/enabled",false);
        const auto localPath=dir.filePath("local.sqlite"),remotePath=dir.filePath("remote.sqlite"); LogDatabase local,remote; QVERIFY(local.open(localPath)); QVERIFY(remote.open(remotePath));
        for (int i=0;i<200;++i) QVERIFY(local.insertQso(AdifRecord{{"CALL",QString("K%1TEST").arg(i)},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}},"fixture",{},false,0,false).id>0);
        NetController::Context ctx; ctx.db=&local; NetController net(ctx); QVERIFY(!net.startQsoComparison(QUrl::fromLocalFile(dir.filePath("missing.sqlite"))));
        QVERIFY(net.startQsoComparison(QUrl::fromLocalFile(remotePath))); net.cancelQsoComparison(); QTRY_VERIFY_WITH_TIMEOUT(!net.qsoComparisonBusy(),10000);
        QVERIFY(net.qsoComparisonRows().isEmpty()); QVERIFY(net.qsoComparisonSummary().value("error").toString().contains("cancelled")); QCOMPARE(local.qsoCount(),200); QCOMPARE(remote.qsoCount(),0);
    }
    void offlineQsoComparisonDetectsSelectedFileReplacement()
    {
        using namespace decolog::core; using decolog::app::NetController;
        QTemporaryDir dir; QVERIFY(dir.isValid()); QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());
        QCoreApplication::setOrganizationName("DecoComparisonTest"); QCoreApplication::setApplicationName("Replacement"); QSettings().setValue("net/enabled",false);
        const auto localPath=dir.filePath("local.sqlite"),remotePath=dir.filePath("remote.sqlite"),clone=dir.filePath("clone.sqlite"); LogDatabase local,remote; QVERIFY(local.open(localPath)); QVERIFY(remote.open(remotePath));
        QSqlQuery q(remote.connection()); q.prepare("VACUUM INTO ?"); q.addBindValue(clone); QVERIFY2(q.exec(),qPrintable(q.lastError().text())); q.finish();
        NetController::Context ctx; ctx.db=&local; NetController net(ctx); QVERIFY(net.startQsoComparison(QUrl::fromLocalFile(remotePath))); QTRY_VERIFY_WITH_TIMEOUT(!net.qsoComparisonBusy(),10000);
        QVERIFY(!net.qsoComparisonSummary().contains("error"));
        if (!QFile::rename(remotePath,remotePath+".original")) QSKIP("Platform does not allow replacing an open SQLite file");
        QVERIFY(QFile::rename(clone,remotePath)); QTRY_VERIFY_WITH_TIMEOUT(net.qsoComparisonSummary().value("stale").toBool(),4000);
    }
    void offlineQsoComparisonDetectsSelectedLogEditsAndQslUpdates()
    {
        using namespace decolog::core; using decolog::app::NetController;
        QTemporaryDir dir; QVERIFY(dir.isValid()); QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());
        QCoreApplication::setOrganizationName("DecoComparisonTest"); QCoreApplication::setApplicationName("RemoteChanges"); QSettings().setValue("net/enabled",false);
        const auto localPath=dir.filePath("local.sqlite"),remotePath=dir.filePath("remote.sqlite"); LogDatabase local,remote; QVERIFY(local.open(localPath)); QVERIFY(remote.open(remotePath));
        const auto qso=remote.insertQso(AdifRecord{{"CALL","K1AAA"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}},"fixture"); QVERIFY(qso.id>0);
        NetController::Context ctx; ctx.db=&local; NetController net(ctx); QVERIFY(net.startQsoComparison(QUrl::fromLocalFile(remotePath))); QTRY_VERIFY_WITH_TIMEOUT(!net.qsoComparisonBusy(),10000);
        QVERIFY(!net.qsoComparisonSummary().value("stale").toBool()); const auto identity=remote.openFileIdentity(); auto record=*remote.record(qso.id); record.set("NAME","Changed");
        QVERIFY(remote.updateQso(qso.id,record).status==InsertResult::Status::Inserted); QCOMPARE(remote.openFileIdentity(),identity); QVERIFY(remote.fileIdentityMatchesOpen());
        QTRY_VERIFY_WITH_TIMEOUT(net.qsoComparisonSummary().value("stale").toBool(),4000);
        QVERIFY(net.startQsoComparison(QUrl::fromLocalFile(remotePath))); QTRY_VERIFY_WITH_TIMEOUT(!net.qsoComparisonBusy(),10000); QVERIFY(!net.qsoComparisonSummary().value("stale").toBool());
        const auto revision=remote.meta(qso.id)->revision; QslState qsl; qsl.service="lotw"; qsl.sent="Y"; QVERIFY(remote.setQslState(qso.id,qsl)); QCOMPARE(remote.meta(qso.id)->revision,revision);
        QTRY_VERIFY_WITH_TIMEOUT(net.qsoComparisonSummary().value("stale").toBool(),4000);
    }
    void offlineQsoComparisonDetectsSelectedLogWriteDuringScan()
    {
        using namespace decolog::core; using decolog::app::NetController;
        QTemporaryDir dir; QVERIFY(dir.isValid()); QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());
        QCoreApplication::setOrganizationName("DecoComparisonTest"); QCoreApplication::setApplicationName("DuringScan"); QSettings().setValue("net/enabled",false);
        const auto localPath=dir.filePath("local.sqlite"),remotePath=dir.filePath("remote.sqlite"); LogDatabase local,remote; QVERIFY(local.open(localPath)); QVERIFY(remote.open(remotePath));
        const auto qso=remote.insertQso(AdifRecord{{"CALL","K1AAA"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}},"fixture"); QVERIFY(qso.id>0);
        NetController::Context ctx; ctx.db=&local; NetController net(ctx); QVERIFY(net.startQsoComparison(QUrl::fromLocalFile(remotePath)));
        auto record=*remote.record(qso.id); record.set("NAME","Changed during work"); QVERIFY(remote.updateQso(qso.id,record).status==InsertResult::Status::Inserted);
        QTRY_VERIFY_WITH_TIMEOUT(!net.qsoComparisonBusy(),10000); QVERIFY(!net.qsoComparisonSummary().contains("error")); QVERIFY(net.qsoComparisonSummary().value("stale").toBool());
    }
    void offlineQsoComparisonCountsBeyondPreviewLimit()
    {
        using namespace decolog::core; using decolog::app::NetController;
        QTemporaryDir dir; QVERIFY(dir.isValid()); QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());
        QCoreApplication::setOrganizationName("DecoComparisonTest"); QCoreApplication::setApplicationName("Limit"); QSettings().setValue("net/enabled",false);
        const auto localPath=dir.filePath("local.sqlite"),remotePath=dir.filePath("remote.sqlite"); LogDatabase local,remote; QVERIFY(local.open(localPath)); QVERIFY(remote.open(remotePath));
        for (int i=0;i<201;++i) QVERIFY(local.insertQso(AdifRecord{{"CALL",QString("K%1TEST").arg(i)},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}},"fixture",{},false,0,false).id>0);
        NetController::Context ctx; ctx.db=&local; NetController net(ctx); QVERIFY(net.startQsoComparison(QUrl::fromLocalFile(remotePath))); QTRY_VERIFY_WITH_TIMEOUT(!net.qsoComparisonBusy(),10000);
        QCOMPARE(net.qsoComparisonSummary().value("localOnly").toInt(),201); QCOMPARE(net.qsoComparisonSummary().value("differences").toInt(),201);
        QVERIFY(net.qsoComparisonSummary().value("truncated").toBool()); QCOMPARE(net.qsoComparisonRows().size(),200);
    }
    void delayedConsumedGrantDoesNotReplaceCurrentStatus()
    {
        using namespace decolog::core; using decolog::app::NetController;
        QTemporaryDir dir; QVERIFY(dir.isValid()); QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path()); QCoreApplication::setOrganizationName("DecoNetStaleGrantTest"); QCoreApplication::setApplicationName("Stale"); QSettings().setValue("net/enabled",false);
        LogDatabase db,authority; QVERIFY(db.open(dir.filePath("requester.sqlite"))); QVERIFY(authority.open(dir.filePath("authority.sqlite")));
        const auto old=db.beginContestSerialRequest("event","client-a","old","authority",5); QVERIFY(old.ok);
        const auto reply=contestserial::handleRequest(authority,"event","authority","client-a",contestserial::requestFrame(old)); QVERIFY(reply.ok);
        QVERIFY(db.acceptContestSerialGrant("event","client-a","old","authority",5).ok); QVERIFY(db.consumeContestSerialGrant("event","client-a","old","qso-old").ok);
        QVERIFY(db.beginContestSerialRequest("event","client-a","new","authority",6).ok); QVERIFY(db.acceptContestSerialGrant("event","client-a","new","authority",6).ok);
        QUdpSocket probe; QVERIFY(probe.bind(QHostAddress::LocalHost,0)); const int port=probe.localPort(); probe.close();
        NetController::Context ctx; ctx.db=&db; NetController net(ctx); net.setIdForTests("client-a"); net.setGroup("FIELD"); net.setPort(port); net.setTarget(QHostAddress::LocalHost);
        QVERIFY(net.configureDurableSerialAuthority("event","authority").isEmpty()); QSignalSpy accepted(&net,&NetController::durableSerialGrantAccepted);
        net.setEnabled(true); QTRY_VERIFY_WITH_TIMEOUT(net.durableSerialStatus().contains("grant: 6"),4000);
        QUdpSocket sender; auto body=reply.body; body.insert("seq",1); body.insert("session","delayed-authority");
        const auto frame=ContestNet::pack("FIELD","authority","serial_grant_v3",body); QCOMPARE(sender.writeDatagram(frame,QHostAddress::LocalHost,quint16(port)),qint64(frame.size()));
        QTest::qWait(300); QCOMPARE(accepted.size(),0); QVERIFY(net.durableSerialStatus().contains("grant: 6"));
        QCOMPARE(net.durableSerialGrantForQso().value("requestId").toString(),QString("new")); net.setEnabled(false);
    }
    void threeNodesRecoverLostAndReorderedGrantsAfterAuthorityRestart()
    {
        using namespace decolog::core; using decolog::app::NetController;
        QTemporaryDir dir; QVERIFY(dir.isValid()); QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path()); QCoreApplication::setOrganizationName("DecoNetThreeNodeTest"); QCoreApplication::setApplicationName("ThreeNode"); QSettings().setValue("net/enabled",false);
        const QString authorityPath=dir.filePath("authority.sqlite"), aPath=dir.filePath("a.sqlite"), bPath=dir.filePath("b.sqlite");
        LogDatabase authorityDb,aDb,bDb; QVERIFY(authorityDb.open(authorityPath)); QVERIFY(aDb.open(aPath)); QVERIFY(bDb.open(bPath));
        QUdpSocket p0,p1,p2; QVERIFY(p0.bind(QHostAddress::LocalHost,0)); QVERIFY(p1.bind(QHostAddress::LocalHost,0)); QVERIFY(p2.bind(QHostAddress::LocalHost,0));
        const int authorityPort=p0.localPort(), aPort=p1.localPort(), bPort=p2.localPort(); p0.close(); p1.close(); p2.close();
        NetController::Context aCtx; aCtx.db=&aDb; NetController a(aCtx); NetController::Context bCtx; bCtx.db=&bDb; NetController b(bCtx);
        a.setIdForTests("client-a"); b.setIdForTests("client-b"); a.setPort(aPort); b.setPort(bPort);
        for (auto* node : {&a,&b}) { node->setGroup("FIELD"); node->setTarget(QHostAddress::LocalHost); QVERIFY(node->configureDurableSerialAuthority("event","authority").isEmpty()); }
        QUdpSocket relay; int seq=0; int lostRequests=0, duplicateRequests=0; bool deliverGrants=false;
        QJsonObject heldA,heldB;
        auto forward=[&](const QString& from,const QString& type,QJsonObject body,int port) {
            body.insert("seq",++seq); body.insert("session","three-node-relay");
            const auto bytes=ContestNet::pack("FIELD",from,type,body); relay.writeDatagram(bytes,QHostAddress::LocalHost,quint16(port));
        };
        NetController::Context authorityCtx; authorityCtx.db=&authorityDb;
        std::unique_ptr<NetController> authority;
        auto createAuthority=[&] {
            QSettings().setValue("net/enabled",false); authority=std::make_unique<NetController>(authorityCtx);
            authority->setIdForTests("authority"); authority->setGroup("FIELD"); authority->setPort(authorityPort); authority->setTarget(QHostAddress::LocalHost);
            authority->configureDurableSerialAuthority("event","authority");
            connect(authority.get(),&NetController::durableSerialGrantReady,&relay,[&](const QJsonObject& body) {
                const QString node=body.value("requester").toString();
                if (!deliverGrants) { if (node=="client-a" && heldA.isEmpty()) heldA=body; if (node=="client-b" && heldB.isEmpty()) heldB=body; return; }
                forward("authority","serial_grant_v3",body,node=="client-a"?aPort:bPort);
            });
        };
        createAuthority();
        auto relayRequest=[&](const QString& id,const QJsonObject& body) {
            if (!authority || !authority->running()) { ++lostRequests; return; }
            forward(id,"serial_request_v3",body,authorityPort);
            if (duplicateRequests++==0) forward(id,"serial_request_v3",body,authorityPort);
        };
        connect(&a,&NetController::durableSerialRequestSent,&relay,[&](const QJsonObject& body) { relayRequest("client-a",body); });
        connect(&b,&NetController::durableSerialRequestSent,&relay,[&](const QJsonObject& body) { relayRequest("client-b",body); });
        QSignalSpy aSent(&a,&NetController::durableSerialRequestSent),bSent(&b,&NetController::durableSerialRequestSent);
        a.setEnabled(true); b.setEnabled(true); QVERIFY(a.requestDurableSerial(1).isEmpty()); QVERIFY(b.requestDurableSerial(1).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(aSent.size()>0 && bSent.size()>0,4000); QVERIFY(lostRequests>=2);
        QCOMPARE(aDb.activeContestSerialRequest("event","client-a").state,QString("pending")); QCOMPARE(bDb.activeContestSerialRequest("event","client-b").state,QString("pending"));
        authority->setEnabled(true); QTRY_VERIFY_WITH_TIMEOUT(!heldA.isEmpty() && !heldB.isEmpty(),12000);
        QVERIFY(heldA.value("serial").toInt()!=heldB.value("serial").toInt()); QVERIFY(duplicateRequests>0);
        authority->setEnabled(false); authority.reset(); authorityDb.close(); QVERIFY(authorityDb.open(authorityPath)); createAuthority(); authority->setEnabled(true);
        deliverGrants=true;
        forward("authority","serial_grant_v3",heldB,bPort); forward("authority","serial_grant_v3",heldB,bPort);
        QTRY_VERIFY_WITH_TIMEOUT(!a.durableSerialGrantForQso().isEmpty() && !b.durableSerialGrantForQso().isEmpty(),12000);
        QCOMPARE(a.durableSerialGrantForQso().value("serial").toInt(),heldA.value("serial").toInt());
        QCOMPARE(b.durableSerialGrantForQso().value("serial").toInt(),heldB.value("serial").toInt());
        QCOMPARE(a.durableSerialGrantForQso().value("requestId").toString(),heldA.value("requestId").toString());
        QCOMPARE(b.durableSerialGrantForQso().value("requestId").toString(),heldB.value("requestId").toString());
        a.setEnabled(false); b.setEnabled(false); authority->setEnabled(false);
        aDb.close(); bDb.close(); QVERIFY(aDb.open(aPath)); QVERIFY(bDb.open(bPath));
        const auto aSaved=aDb.activeContestSerialRequest("event","client-a"), bSaved=bDb.activeContestSerialRequest("event","client-b");
        QVERIFY(aSaved.ok); QVERIFY(bSaved.ok); QCOMPARE(aSaved.state,QString("granted")); QCOMPARE(bSaved.state,QString("granted"));
        QCOMPARE(aSaved.serial,heldA.value("serial").toInt()); QCOMPARE(bSaved.serial,heldB.value("serial").toInt());
        QCOMPARE(aSaved.requestId,heldA.value("requestId").toString()); QCOMPARE(bSaved.requestId,heldB.value("requestId").toString());
    }
    void selfAuthoritySharesDurableSequenceAndRecovers()
    {
        using namespace decolog::core; using decolog::app::NetController;
        QTemporaryDir dir; QVERIFY(dir.isValid()); QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path()); QCoreApplication::setOrganizationName("DecoNetSelfTest"); QCoreApplication::setApplicationName("Self"); QSettings().setValue("net/enabled",false);
        LogDatabase db; QVERIFY(db.open(dir.filePath("self.sqlite"))); QUdpSocket probe; QVERIFY(probe.bind(QHostAddress::LocalHost,0)); const int port=probe.localPort(); probe.close();
        NetController::Context ctx; ctx.db=&db; NetController net(ctx); net.setIdForTests("self"); net.setGroup("FIELD"); net.setPort(port); net.setTarget(QHostAddress::LocalHost);
        QVERIFY(net.configureDurableSerialAuthority("event","self").isEmpty()); net.setEnabled(true);
        QVERIFY(net.requestDurableSerial(10).isEmpty()); QTRY_VERIFY_WITH_TIMEOUT(!net.durableSerialGrantForQso().isEmpty(),4000);
        auto grant=net.durableSerialGrantForQso(); QCOMPARE(grant.value("serial").toInt(),10); const auto requestId=grant.value("requestId").toString();
        const auto peer=db.reserveContestSerial("event","remote","req-peer",1); QVERIFY(peer.ok); QCOMPARE(peer.serial,11);
        net.setEnabled(false); db.close(); QVERIFY(db.open(dir.filePath("self.sqlite")));
        QVERIFY(net.configureDurableSerialAuthority("event","self").isEmpty()); net.setEnabled(true); QTRY_VERIFY_WITH_TIMEOUT(net.durableSerialStatus().contains("Experimental grant: 10"),4000);
        grant=net.durableSerialGrantForQso(); QCOMPARE(grant.value("requestId").toString(),requestId);
        QVERIFY(db.consumeContestSerialGrant("event","self",requestId,"test-use").ok);
        QVERIFY(net.requestDurableSerial(1).isEmpty()); QTRY_VERIFY_WITH_TIMEOUT(net.durableSerialGrantForQso().value("serial").toInt()==12,4000); net.setEnabled(false);
    }
    void selfAuthorityRecoversReservationBeforeGrant()
    {
        using namespace decolog::core; using decolog::app::NetController;
        QTemporaryDir dir; QVERIFY(dir.isValid()); QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path()); QCoreApplication::setOrganizationName("DecoNetSelfRestartTest"); QCoreApplication::setApplicationName("SelfRestart"); QSettings().setValue("net/enabled",false);
        const QString path=dir.filePath("self.sqlite");
        { LogDatabase db; QVERIFY(db.open(path)); QVERIFY(db.beginContestSerialRequest("event","self","saved-request","self",5).ok); const auto held=db.reserveContestSerial("event","self","saved-request",5); QVERIFY(held.ok); QCOMPARE(held.serial,5); }
        LogDatabase db; QVERIFY(db.open(path)); QUdpSocket probe; QVERIFY(probe.bind(QHostAddress::LocalHost,0)); const int port=probe.localPort(); probe.close();
        NetController::Context ctx; ctx.db=&db; NetController net(ctx); net.setIdForTests("self"); net.setGroup("FIELD"); net.setPort(port); net.setTarget(QHostAddress::LocalHost);
        QVERIFY(net.configureDurableSerialAuthority("event","self").isEmpty()); net.setEnabled(true);
        QTRY_VERIFY_WITH_TIMEOUT(!net.durableSerialGrantForQso().isEmpty(),4000); const auto grant=net.durableSerialGrantForQso();
        QCOMPARE(grant.value("requestId").toString(),QString("saved-request")); QCOMPARE(grant.value("serial").toInt(),5);
        const auto peer=db.reserveContestSerial("event","remote","peer",1); QVERIFY(peer.ok); QCOMPARE(peer.serial,6); net.setEnabled(false);
    }
    void retryDoesNotCreateNewRequestAfterExternalConsumption()
    {
        using namespace decolog::core; using decolog::app::NetController;
        QTemporaryDir dir; QVERIFY(dir.isValid()); QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path()); QCoreApplication::setOrganizationName("DecoNetExternalTest"); QCoreApplication::setApplicationName("Retry"); QSettings().setValue("net/enabled",false);
        LogDatabase db; QVERIFY(db.open(dir.filePath("requester.sqlite"))); QUdpSocket probe; QVERIFY(probe.bind(QHostAddress::LocalHost,0)); const int port=probe.localPort(); probe.close();
        NetController::Context ctx; ctx.db=&db; NetController net(ctx); net.setIdForTests("client-a"); net.setGroup("FIELD"); net.setPort(port); net.setTarget(QHostAddress::LocalHost);
        QVERIFY(net.configureDurableSerialAuthority("event","authority").isEmpty()); QSignalSpy sent(&net,&NetController::durableSerialRequestSent); net.setEnabled(true);
        QVERIFY(net.requestDurableSerial(9).isEmpty()); QTRY_COMPARE_WITH_TIMEOUT(sent.size(),1,4000);
        const auto id=sent.at(0).at(0).toJsonObject().value("requestId").toString(); QVERIFY(db.acceptContestSerialGrant("event","client-a",id,"authority",9).ok);
        QVERIFY(db.consumeContestSerialGrant("event","client-a",id,"external-use").ok);
        QTest::qWait(2500); QCOMPARE(sent.size(),1); QVERIFY(net.durableSerialStatus().contains("no longer active"));
        QVERIFY(!db.activeContestSerialRequest("event","client-a").ok); net.setEnabled(false);
    }
    void configuredNetworkRecoversPendingRequestWithoutCreatingOne()
    {
        using namespace decolog::core; using decolog::app::NetController;
        QTemporaryDir dir; QVERIFY(dir.isValid()); QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path()); QCoreApplication::setOrganizationName("DecoNetResumeTest"); QCoreApplication::setApplicationName("Resume");
        QSettings().setValue("net/enabled",false); const QString path=dir.filePath("requester.sqlite");
        { LogDatabase db; QVERIFY(db.open(path)); QVERIFY(db.beginContestSerialRequest("event","client-a","persisted","authority",19).ok); }
        LogDatabase db; QVERIFY(db.open(path)); QUdpSocket probe; QVERIFY(probe.bind(QHostAddress::LocalHost,0)); const int port=probe.localPort(); probe.close();
        NetController::Context ctx; ctx.db=&db; NetController net(ctx); net.setIdForTests("client-a"); net.setGroup("FIELD"); net.setPort(port); net.setTarget(QHostAddress::LocalHost);
        QVERIFY(net.configureDurableSerialAuthority("event","authority").isEmpty()); QSignalSpy sent(&net,&NetController::durableSerialRequestSent);
        net.setEnabled(true); QTRY_COMPARE_WITH_TIMEOUT(sent.size(),1,4000);
        const auto frame=sent.at(0).at(0).toJsonObject(); QCOMPARE(frame.value("requestId").toString(),QString("persisted")); QCOMPARE(frame.value("minimum").toInt(),19);
        const auto saved=db.activeContestSerialRequest("event","client-a"); QCOMPARE(saved.state,QString("pending")); QCOMPARE(saved.serial,0);
        QVERIFY(net.configureDurableSerialAuthority("event","other-authority").isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(net.durableSerialStatus().contains("different authority"),4000); QCOMPARE(sent.size(),1);
        QVERIFY(net.configureDurableSerialAuthority("new-event","authority").isEmpty()); const auto count=sent.size(); QTest::qWait(800); QCOMPARE(sent.size(),count);
        QVERIFY(!db.activeContestSerialRequest("new-event","client-a").ok); net.setEnabled(false);
    }
    void twoControllersRecoverRequestAfterAuthorityStarts()
    {
        using namespace decolog::core; using decolog::app::NetController;
        QTemporaryDir dir; QVERIFY(dir.isValid()); QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path()); QCoreApplication::setOrganizationName("DecoNetPairTest"); QCoreApplication::setApplicationName("Pair");
        QSettings().setValue("net/enabled",false); LogDatabase requesterDb,authorityDb;
        QVERIFY(requesterDb.open(dir.filePath("requester.sqlite"))); QVERIFY(authorityDb.open(dir.filePath("authority.sqlite")));
        QUdpSocket probe; QVERIFY(probe.bind(QHostAddress::LocalHost,0)); const int port=probe.localPort();
        QUdpSocket authorityProbe; QVERIFY(authorityProbe.bind(QHostAddress::LocalHost,0)); const int authorityPort=authorityProbe.localPort(); probe.close(); authorityProbe.close();
        NetController::Context requesterCtx; requesterCtx.db=&requesterDb; NetController requester(requesterCtx);
        NetController::Context authorityCtx; authorityCtx.db=&authorityDb; NetController authority(authorityCtx);
        requester.setIdForTests("client-a"); authority.setIdForTests("authority");
        requester.setPort(port); authority.setPort(authorityPort);
        for (auto* net : {&requester,&authority}) { net->setGroup("FIELD"); net->setTarget(QHostAddress::LocalHost); QVERIFY(net->configureDurableSerialAuthority("event","authority").isEmpty()); }
        QUdpSocket relay; int seq=0;
        auto forward=[&](const QString& from,const QString& type,QJsonObject body,int targetPort) {
            body.insert("seq",++seq); body.insert("session","local-relay"); const auto bytes=ContestNet::pack("FIELD",from,type,body);
            relay.writeDatagram(bytes,QHostAddress::LocalHost,quint16(targetPort));
        };
        connect(&requester,&NetController::durableSerialRequestSent,&requester,[&](const QJsonObject& body) { if (authority.running()) forward("client-a","serial_request_v3",body,authorityPort); });
        connect(&authority,&NetController::durableSerialGrantReady,&authority,[&](const QJsonObject& body) { forward("authority","serial_grant_v3",body,port); });
        QSignalSpy sent(&requester,&NetController::durableSerialRequestSent), accepted(&requester,&NetController::durableSerialGrantAccepted);
        requester.setEnabled(true); QVERIFY(requester.running()); QVERIFY(requester.requestDurableSerial(11).isEmpty()); QTRY_COMPARE_WITH_TIMEOUT(sent.size(),1,10000);
        QTest::qWait(200); QCOMPARE(accepted.size(),0); auto pending=requesterDb.activeContestSerialRequest("event","client-a"); QVERIFY(pending.ok); QCOMPARE(pending.state,QString("pending"));
        authority.setEnabled(true); QVERIFY(authority.running());
        QTRY_COMPARE_WITH_TIMEOUT(accepted.size(),1,4000); QVERIFY(sent.size()>=2); auto granted=requesterDb.activeContestSerialRequest("event","client-a");
        QCOMPARE(granted.requestId,pending.requestId); QCOMPARE(granted.state,QString("granted")); QCOMPARE(granted.serial,11);
        const auto count=sent.size(); QTest::qWait(2500); QCOMPARE(sent.size(),count);
        requester.setEnabled(false); authority.setEnabled(false);
    }
    void requesterRetriesSamePersistedRequestWithoutAuthority()
    {
        using namespace decolog::core; using decolog::app::NetController;
        QTemporaryDir dir; QVERIFY(dir.isValid()); QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path()); QCoreApplication::setOrganizationName("DecoNetRequestTest"); QCoreApplication::setApplicationName("Request");
        QSettings().setValue("net/enabled",false); LogDatabase db; QVERIFY(db.open(dir.filePath("requester.sqlite")));
        QUdpSocket probe; QVERIFY(probe.bind(QHostAddress::LocalHost,0)); const int port=probe.localPort(); probe.close();
        NetController::Context ctx; ctx.db=&db; NetController net(ctx); net.setIdForTests("client-a"); net.setGroup("FIELD"); net.setPort(port); net.setTarget(QHostAddress::LocalHost);
        net.setTarget(QHostAddress::LocalHost); QVERIFY(net.configureDurableSerialAuthority("event","authority").isEmpty()); net.setEnabled(true); QVERIFY(net.running());
        QSignalSpy requests(&net,&NetController::durableSerialRequestSent);
        QVERIFY(net.requestDurableSerial(7).isEmpty()); QTRY_COMPARE_WITH_TIMEOUT(requests.size(),1,10000);
        const auto first=requests.at(0).at(0).toJsonObject(); const auto requestId=first.value("requestId").toString(); QVERIFY(!requestId.isEmpty());
        auto stored=db.activeContestSerialRequest("event","client-a"); QVERIFY(stored.ok); QCOMPARE(stored.state,QString("pending")); QCOMPARE(stored.serial,0); QCOMPARE(stored.requestId,requestId);
        QVERIFY(net.requestDurableSerial(7).isEmpty()); QTRY_COMPARE_WITH_TIMEOUT(requests.size(),2,10000);
        QCOMPARE(requests.at(1).at(0).toJsonObject().value("requestId").toString(),requestId);
        QVERIFY(net.requestDurableSerial(8).isEmpty()); QTRY_VERIFY_WITH_TIMEOUT(net.durableSerialStatus().contains("different authority or minimum"),10000);
        QCOMPARE(requests.size(),2); QCOMPARE(db.activeContestSerialRequest("event","client-a").requestId,requestId);
        db.close(); QVERIFY(db.open(dir.filePath("requester.sqlite")));
        QTRY_VERIFY_WITH_TIMEOUT(net.durableSerialStatus().contains("Retry stopped"),4000); QCOMPARE(requests.size(),2); net.setEnabled(false);
        stored=db.activeContestSerialRequest("event","client-a"); QCOMPARE(stored.state,QString("pending")); QCOMPARE(stored.requestId,requestId);
    }
    void requesterPersistsValidatedGrantFromUdp()
    {
        using namespace decolog::core; using decolog::app::NetController;
        QTemporaryDir dir; QVERIFY(dir.isValid()); QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path()); QCoreApplication::setOrganizationName("DecoNetGrantTest"); QCoreApplication::setApplicationName("Grant");
        QSettings().setValue("net/enabled",false);
        LogDatabase db, authority; QVERIFY(db.open(dir.filePath("requester.sqlite"))); QVERIFY(authority.open(dir.filePath("authority.sqlite")));
        auto req=db.beginContestSerialRequest("event","client-a","request-a","authority",7); QVERIFY(req.ok);
        const auto reply=contestserial::handleRequest(authority,"event","authority","client-a",contestserial::requestFrame(req)); QVERIFY(reply.ok);
        QUdpSocket probe; QVERIFY(probe.bind(QHostAddress::LocalHost,0)); const int port=probe.localPort(); probe.close();
        NetController::Context ctx; ctx.db=&db; NetController net(ctx); net.setIdForTests("client-a"); net.setGroup("FIELD"); net.setPort(port); net.setTarget(QHostAddress::LocalHost);
        QVERIFY(net.configureDurableSerialAuthority("event","authority").isEmpty()); QSignalSpy accepted(&net,&NetController::durableSerialGrantAccepted);
        net.setEnabled(true); QVERIFY(net.running()); QUdpSocket sender;
        auto transmit=[&](const QString& from,int seq) { auto body=reply.body; body.insert("seq",seq); body.insert("session","authority-session");
            const auto bytes=ContestNet::pack("FIELD",from,"serial_grant_v3",body); return sender.writeDatagram(bytes,QHostAddress::LocalHost,quint16(port))==bytes.size(); };
        QVERIFY(transmit("impostor",1)); QTest::qWait(150); QCOMPARE(accepted.size(),0);
        QCOMPARE(db.contestSerialRequest("event","client-a","request-a").state,QString("pending"));
        QVERIFY(transmit("authority",2)); QTRY_COMPARE_WITH_TIMEOUT(accepted.size(),1,10000);
        auto saved=db.contestSerialRequest("event","client-a","request-a"); QVERIFY(saved.ok); QCOMPARE(saved.state,QString("granted")); QCOMPARE(saved.serial,7);
        net.setEnabled(false); db.close(); QVERIFY(db.open(dir.filePath("requester.sqlite")));
        saved=db.contestSerialRequest("event","client-a","request-a"); QCOMPARE(saved.state,QString("granted")); QCOMPARE(saved.serial,7);
    }
    void explicitAuthorityPersistsRequestsAcrossUdpRetryAndRestart()
    {
        using namespace decolog::core; using decolog::app::NetController;
        QTemporaryDir dir; QVERIFY(dir.isValid()); QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path()); QCoreApplication::setOrganizationName("DecoNetTest"); QCoreApplication::setApplicationName("SerialAuthority");
        QSettings().setValue("net/enabled",false);
        const auto path=dir.filePath("authority.sqlite"); LogDatabase db,client; QVERIFY(db.open(path)); QVERIFY(client.open(dir.filePath("client.sqlite")));
        const auto req=client.beginContestSerialRequest("event","client-a","req-a","authority",10); QVERIFY(req.ok);
        QUdpSocket probe; QVERIFY(probe.bind(QHostAddress::LocalHost,0)); const int port=probe.localPort(); probe.close(); QUdpSocket sender;
        auto transmit=[&](int seq,const QJsonObject& payload,const QString& from=QStringLiteral("client-a")) {
            QJsonObject body=payload; body.insert("seq",seq); body.insert("session","client-session");
            const auto bytes=ContestNet::pack("FIELD",from,"serial_request_v3",body);
            return sender.writeDatagram(bytes,QHostAddress::LocalHost,quint16(port))==bytes.size();
        };
        { NetController::Context ctx; ctx.db=&db; NetController net(ctx); net.setIdForTests("authority"); net.setGroup("FIELD"); net.setPort(port); net.setTarget(QHostAddress::LocalHost);
          QVERIFY(net.configureDurableSerialAuthority("event","authority").isEmpty()); QSignalSpy grants(&net,&NetController::durableSerialGrantReady);
          net.setEnabled(true); QVERIFY(net.running()); QTRY_VERIFY_WITH_TIMEOUT(net.durableSerialStatus().contains("No saved serial request"),4000); const auto body=contestserial::requestFrame(req);
          QVERIFY(transmit(1,body)); QTRY_COMPARE_WITH_TIMEOUT(grants.size(),1,10000);
          const auto first=grants.at(0).at(0).toJsonObject(); QCOMPARE(first.value("serial").toInt(),10);
          QVERIFY(transmit(2,body)); QTRY_COMPARE_WITH_TIMEOUT(grants.size(),2,10000); QCOMPARE(grants.at(1).at(0).toJsonObject().value("serial").toInt(),10);
          auto wrong=body; wrong.insert("scope","other"); QVERIFY(transmit(3,wrong)); QTest::qWait(150); QCOMPARE(grants.size(),2); net.setEnabled(false); }
        db.close(); QVERIFY(db.open(path));
        { NetController::Context ctx; ctx.db=&db; NetController net(ctx); net.setIdForTests("authority"); net.setGroup("FIELD"); net.setPort(port); net.setTarget(QHostAddress::LocalHost);
          QVERIFY(net.configureDurableSerialAuthority("event","authority").isEmpty()); QSignalSpy grants(&net,&NetController::durableSerialGrantReady);
          net.setEnabled(true); QTRY_VERIFY_WITH_TIMEOUT(net.durableSerialStatus().contains("No saved serial request"),4000); QVERIFY(transmit(4,contestserial::requestFrame(req))); QTRY_COMPARE_WITH_TIMEOUT(grants.size(),1,10000);
          QCOMPARE(grants.at(0).at(0).toJsonObject().value("serial").toInt(),10);
          QVERIFY(!net.configureDurableSerialAuthority("event", "").isEmpty());
          QVERIFY(net.configureDurableSerialAuthority("event", "other-authority").isEmpty());
          QVERIFY(transmit(5,contestserial::requestFrame(req))); QTest::qWait(150); QCOMPARE(grants.size(),1);
          QVERIFY(net.configureDurableSerialAuthority("", "").isEmpty());
          QVERIFY(transmit(6,contestserial::requestFrame(req))); QTest::qWait(150); QCOMPARE(grants.size(),1); net.setEnabled(false); }
    }
    void queuedCallsKeepOrderAndRejectDuplicates()
    {
        decolog::app::NetController net({});
        QVERIFY(net.queueCall(" k1abc ").isEmpty());
        QVERIFY(net.queueCall("DL1XYZ/P").isEmpty());
        QVERIFY(!net.queueCall("K1ABC").isEmpty());
        QVERIFY(!net.queueCall("bad call").isEmpty());
        QCOMPARE(net.queuedCalls(),QStringList({"K1ABC","DL1XYZ/P"}));
        QVERIFY(net.moveQueuedCall(1,-1));
        QCOMPARE(net.takeQueuedCall(),QString("DL1XYZ/P"));
        QCOMPARE(net.queuedCalls(),QStringList({"K1ABC"}));
        QVERIFY(net.removeQueuedCall(0)); QVERIFY(net.takeQueuedCall().isEmpty());
    }
    void sessionMakesSequenceRestartSafe()
    {
        QCOMPARE(decolog::app::NetController::frameKey(QStringLiteral("session-a"), 1),
                 QStringLiteral("session-a:1"));
        QVERIFY(decolog::app::NetController::frameKey(QStringLiteral("session-a"), 1)
                != decolog::app::NetController::frameKey(QStringLiteral("session-b"), 1));
        QVERIFY(decolog::app::NetController::frameKey(QStringLiteral("legacy"), 1)
                != decolog::app::NetController::frameKey(QStringLiteral("session-a"), 1));
    }
};

QTEST_MAIN(TestNetController)
#include "tst_netcontroller.moc"
