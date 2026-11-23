#include "app/NetController.h"
#include "core/ContestSerialProtocol.h"
#include "core/LogDatabase.h"

#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QSettings>
#include <QSqlQuery>
#include <QSqlError>
#include <QFile>
#include <QUdpSocket>
#include <memory>

class TestNetController : public QObject {
    Q_OBJECT
private slots:
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
        AdifRecord record{{"CALL","K1AAA"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"COMMENT",QString(2050,'x')}};
        const auto q=remote.insertQso(record,"fixture"); QVERIFY(q.id>0); NetController::Context ctx; ctx.db=&local; NetController net(ctx);
        QVERIFY(net.startQsoComparison(QUrl::fromLocalFile(remotePath))); QTRY_VERIFY_WITH_TIMEOUT(!net.qsoComparisonBusy(),10000);
        QVERIFY(net.reviewQsoComparison(remote.meta(q.id)->uuid)); QTRY_VERIFY_WITH_TIMEOUT(!net.qsoReviewBusy(),10000);
        QVERIFY(net.qsoReview().value("ok").toBool()); QVERIFY(net.qsoReview().value("hasLongFields").toBool()); QVERIFY(!net.qsoReview().value("canInsert").toBool());
        QVERIFY(!net.applyReviewedMissingQso().isEmpty()); QCOMPARE(local.qsoCount(),qint64(0));
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
