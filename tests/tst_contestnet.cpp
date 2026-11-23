// La rete multi-operatore: messaggi impacchettati e ricevuti.
#include "core/ContestNet.h"
#include "core/ContestNetQsoTransfer.h"
#include "core/ContestSerialProtocol.h"
#include "core/LogDatabase.h"

#include <QSignalSpy>
#include <QSqlQuery>
#include <QJsonArray>
#include <QTest>
#include <QUdpSocket>
#include <QTemporaryDir>

using namespace decolog::core;

class TestContestNet : public QObject {
    Q_OBJECT

private slots:
    void durableSerialFramesSurviveLossReorderAndRestart()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); LogDatabase server,a,b,c;
        QVERIFY(server.open(dir.filePath("server.sqlite"))); QVERIFY(a.open(dir.filePath("a.sqlite")));
        QVERIFY(b.open(dir.filePath("b.sqlite"))); QVERIFY(c.open(dir.filePath("c.sqlite")));
        auto qa=a.beginContestSerialRequest("event","a","a1","authority",1);
        auto qb=b.beginContestSerialRequest("event","b","b1","authority",1);
        auto qc=c.beginContestSerialRequest("event","c","c1","authority",1); QVERIFY(qa.ok && qb.ok && qc.ok);
        auto send=[&](const ContestSerialRequest& request) {
            QString from,type; QJsonObject body;
            const auto datagram=ContestNet::pack("FIELD",request.requester,"serial_request_v3",contestserial::requestFrame(request));
            if (!ContestNet::unpack(datagram,"FIELD",&from,&type,&body) || type!="serial_request_v3") return contestserial::Reply{};
            return contestserial::handleRequest(server,"event","authority",from,body);
        };
        const auto ga=send(qa), gb=send(qb), gc=send(qc); QVERIFY2(ga.ok,qPrintable(ga.error)); QVERIFY(gb.ok && gc.ok);
        auto accept=[&](LogDatabase& client,const QString& requester,const QString& sender,const QJsonObject& grant) {
            QString from,type; QJsonObject body; const auto datagram=ContestNet::pack("FIELD",sender,"serial_grant_v3",grant);
            if (!ContestNet::unpack(datagram,"FIELD",&from,&type,&body) || type!="serial_grant_v3") return ContestSerialRequest{};
            return contestserial::acceptGrant(client,"event",requester,"authority",from,body);
        };
        QVERIFY(accept(c,"c","authority",gc.body).ok);
        QVERIFY(accept(b,"b","authority",gb.body).ok);
        QVERIFY(accept(b,"b","authority",gb.body).replayed);
        QVERIFY(!accept(a,"a","impostor",ga.body).ok);
        a.close(); server.close(); QVERIFY(a.open(dir.filePath("a.sqlite"))); QVERIFY(server.open(dir.filePath("server.sqlite")));
        qa=a.activeContestSerialRequest("event","a"); QVERIFY(qa.ok);
        const auto retry=send(qa); QVERIFY(retry.ok && retry.replayed); QCOMPARE(retry.body.value("serial"),ga.body.value("serial"));
        QVERIFY(accept(a,"a","authority",retry.body).ok);
        QCOMPARE(a.activeContestSerialRequest("event","a").serial,1);
        QVERIFY(a.consumeContestSerialGrant("event","a","a1","intent-a").ok);
        QVERIFY(accept(a,"a","authority",retry.body).replayed);
    }
    void serialFramesRejectMalformedAndMisroutedMessages()
    {
        LogDatabase authority,client; QVERIFY(authority.open(":memory:")); QVERIFY(client.open(":memory:"));
        const auto pending=client.beginContestSerialRequest("event","node","request","server",10); QVERIFY(pending.ok);
        const auto base=contestserial::requestFrame(pending); QVERIFY(!base.isEmpty());
        QVERIFY(!contestserial::handleRequest(authority,"other","server","node",base).ok);
        QVERIFY(!contestserial::handleRequest(authority,"event","server","different-node",base).ok);
        auto wrongVersion=base; wrongVersion.insert("serialProtocol",1.5);
        QVERIFY(!contestserial::handleRequest(authority,"event","server","node",wrongVersion).ok);
        for (const QJsonValue invalid : {QJsonValue(1.5),QJsonValue(0),QJsonValue(QStringLiteral("10")),QJsonValue(1e20)}) {
            auto malformed=base; malformed.insert("minimum",invalid);
            QVERIFY(!contestserial::handleRequest(authority,"event","server","node",malformed).ok);
        }
        const auto granted=contestserial::handleRequest(authority,"event","server","node",base); QVERIFY(granted.ok);
        QCOMPARE(granted.body.value("serial").toInt(),10);
        auto altered=base; altered.insert("minimum",11); QVERIFY(!contestserial::handleRequest(authority,"event","server","node",altered).ok);
        altered=granted.body; altered.insert("minimum",9);
        QVERIFY(!contestserial::acceptGrant(client,"event","node","server","server",altered).ok);
        altered=granted.body; altered.insert("serial",1.25);
        QVERIFY(!contestserial::acceptGrant(client,"event","node","server","server",altered).ok);
        QCOMPARE(client.activeContestSerialRequest("event","node").state,QString("pending"));
        QVERIFY(contestserial::acceptGrant(client,"event","node","server","server",granted.body).ok);
        altered=granted.body; altered.insert("serial",11);
        QVERIFY(!contestserial::acceptGrant(client,"event","node","server","server",altered).ok);
        QCOMPARE(client.activeContestSerialRequest("event","node").serial,10);
    }
    void durableSerialFrameRoundTripOverLoopbackUdp()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); LogDatabase server,client;
        QVERIFY(server.open(dir.filePath("authority.sqlite"))); QVERIFY(client.open(dir.filePath("client.sqlite")));
        const auto pending=client.beginContestSerialRequest("event","node","request","authority",1); QVERIFY(pending.ok);
        QUdpSocket probe; QVERIFY(probe.bind(QHostAddress::LocalHost,0)); const auto port=probe.localPort(); probe.close();
        ContestNet net; QVERIFY(net.start(port,"FIELD","authority")); QUdpSocket sender,grantSender;
        QVERIFY(sender.bind(QHostAddress::LocalHost,0)); int received=0;
        QObject::connect(&net,&ContestNet::received,&net,[&](const QString& from,const QString& type,const QJsonObject& body,const QHostAddress&) {
            if (type!=QLatin1String("serial_request_v3")) return;
            const auto grant=contestserial::handleRequest(server,"event","authority",from,body);
            if (!grant.ok) return;
            ++received;
            if (received>1) grantSender.writeDatagram(ContestNet::pack("FIELD","authority","serial_grant_v3",grant.body),QHostAddress::LocalHost,sender.localPort());
        });
        const auto datagram=ContestNet::pack("FIELD","node","serial_request_v3",contestserial::requestFrame(pending));
        QCOMPARE(sender.writeDatagram(datagram,QHostAddress::LocalHost,port),qint64(datagram.size())); QTRY_COMPARE(received,1);
        QVERIFY(!sender.hasPendingDatagrams()); server.close(); QVERIFY(server.open(dir.filePath("authority.sqlite")));
        QCOMPARE(sender.writeDatagram(datagram,QHostAddress::LocalHost,port),qint64(datagram.size())); QTRY_COMPARE(received,2);
        QTRY_VERIFY(sender.hasPendingDatagrams()); QByteArray bytes; bytes.resize(int(sender.pendingDatagramSize()));
        QVERIFY(sender.readDatagram(bytes.data(),bytes.size())>0); QString from,type; QJsonObject body;
        QVERIFY(ContestNet::unpack(bytes,"FIELD",&from,&type,&body)); QCOMPARE(type,QString("serial_grant_v3"));
        const auto accepted=contestserial::acceptGrant(client,"event","node","authority",from,body); QVERIFY2(accepted.ok,qPrintable(accepted.error)); QCOMPARE(accepted.serial,1);
    }
    void qsoTransferReordersAndRejectsCorruption()
    {
        AdifRecord record{{"CALL", "9H1SR"}, {"COMMENT", QString(1500, QLatin1Char('x'))}};
        ContestNetInventoryRow row; row.uuid="qso-1"; row.revision=3;
        row.adifPayload=adif::writeRecord(record).toUtf8(); row.contentHash=ContestNetInventory::fingerprint(record);
        contestqso::Encoder encoder("transfer-1", "log-1", row);
        QVERIFY2(encoder.ok(), qPrintable(encoder.error())); QVERIFY(encoder.count()>2);
        contestqso::Receiver receiver("node-1", "transfer-1");
        for (int i=encoder.count()-1; i>0; --i) {
            auto frame=encoder.frame(i); const auto packed=ContestNet::pack("FIELD","node-1","qso_chunk_v1",frame);
            QVERIFY(packed.size()<1400); QString from,type; QJsonObject decoded;
            QVERIFY(ContestNet::unpack(packed,"FIELD",&from,&type,&decoded));
            QVERIFY(receiver.accept(from,decoded)); QVERIFY(!receiver.complete());
            QVERIFY(receiver.accept(from,decoded));
        }
        QVERIFY(!receiver.accept("impostor",encoder.frame(0))); QVERIFY(!receiver.complete());
        QVERIFY(receiver.accept("node-1",encoder.frame(0))); QVERIFY(receiver.complete());
        QCOMPARE(receiver.row().adifPayload,row.adifPayload); QCOMPARE(receiver.row().contentHash,row.contentHash);
        contestqso::Receiver damaged("node-1","transfer-1");
        for (int i=0;i<encoder.count();++i) {
            auto frame=encoder.frame(i);
            if (i==0) { auto bytes=QByteArray::fromBase64(frame.value("data").toString().toLatin1()); bytes[0]='Z'; frame.insert("data",QString::fromLatin1(bytes.toBase64())); }
            const bool accepted=damaged.accept("node-1",frame);
            if (i==encoder.count()-1) QVERIFY(!accepted); else QVERIFY(accepted);
        }
        QVERIFY(!damaged.complete()); QVERIFY(damaged.row().adifPayload.isEmpty());
        contestqso::Receiver invalid("node-1","transfer-1"); auto frame=encoder.frame(0); frame.insert("count",1e20);
        QVERIFY(!invalid.accept("node-1",frame));
    }
    void qsoTransferOverLoopbackWithLossAndRetry()
    {
        AdifRecord record{{"CALL", "9H1SR"}, {"COMMENT", QString(1400, QLatin1Char('a'))}};
        ContestNetInventoryRow row; row.uuid="qso-1"; row.revision=1; row.adifPayload=adif::writeRecord(record).toUtf8();
        row.contentHash=ContestNetInventory::fingerprint(record);
        contestqso::Encoder encoder("transfer-2","log-1",row); QVERIFY(encoder.ok());
        QUdpSocket probe; QVERIFY(probe.bind(QHostAddress::LocalHost,0)); const auto port=probe.localPort(); probe.close();
        ContestNet net; QVERIFY(net.start(port,"FIELD","receiver")); QUdpSocket sender;
        contestqso::Receiver receiver("node-1","transfer-2"); int accepted=0;
        connect(&net,&ContestNet::received,&net,[&](const QString& from,const QString& type,const QJsonObject& body,const QHostAddress&) {
            if (type=="qso_chunk_v1" && receiver.accept(from,body)) ++accepted;
        });
        auto send=[&](int index) { const auto bytes=ContestNet::pack("FIELD","node-1","qso_chunk_v1",encoder.frame(index)); return sender.writeDatagram(bytes,QHostAddress::LocalHost,port)==bytes.size(); };
        for (int i=encoder.count()-1;i>0;--i) QVERIFY(send(i));
        QTRY_COMPARE(accepted,encoder.count()-1); QVERIFY(!receiver.complete());
        QVERIFY(send(1)); QTRY_COMPARE(accepted,encoder.count()); QVERIFY(!receiver.complete());
        QVERIFY(send(0)); QTRY_VERIFY(receiver.complete()); QCOMPARE(receiver.row().adifPayload,row.adifPayload);
    }
    void qsoTransferRequestsMissingFragments()
    {
        AdifRecord record{{"CALL","K1AAA"},{"COMMENT",QString(40000,QLatin1Char('r'))}};
        ContestNetInventoryRow row; row.uuid="qso"; row.revision=2; row.adifPayload=adif::writeRecord(record).toUtf8(); row.contentHash=ContestNetInventory::fingerprint(record);
        contestqso::Encoder sender("transfer","log",row); QVERIFY(sender.ok());
        contestqso::Receiver receiver("node","transfer"); QVERIFY(receiver.accept("node",sender.frame(sender.count()-1)));
        auto req=receiver.missingRequest(0); QVERIFY(!req.isEmpty()); auto frames=sender.framesForRequest(req); QCOMPARE(frames.size(),32);
        auto bad=req; bad.insert("sha256",QString(64,QLatin1Char('0'))); QVERIFY(sender.framesForRequest(bad).isEmpty());
        for (const auto& frame:frames) QVERIFY(receiver.accept("node",frame));
        QVERIFY(!receiver.complete());
        while (!receiver.complete()) {
            req=receiver.missingRequest(0); frames=sender.framesForRequest(req); QVERIFY(!frames.isEmpty());
            for (const auto& frame:frames) QVERIFY(receiver.accept("node",frame));
        }
        QVERIFY(receiver.missingRequest(0).isEmpty()); QCOMPARE(receiver.row().adifPayload,row.adifPayload);
        bad=req; bad.insert("indices",QJsonArray{0,0}); QVERIFY(sender.framesForRequest(bad).isEmpty());
        bad=req; bad.insert("indices",QJsonArray{1.5}); QVERIFY(sender.framesForRequest(bad).isEmpty());
        bad=req; bad.insert("indices",QJsonArray{sender.count()}); QVERIFY(sender.framesForRequest(bad).isEmpty());
    }
    void qsoTransferMissingRequestOverUdp()
    {
        AdifRecord record{{"CALL","K1AAA"},{"COMMENT",QString(1800,QLatin1Char('u'))}};
        ContestNetInventoryRow row; row.uuid="qso"; row.revision=1; row.adifPayload=adif::writeRecord(record).toUtf8(); row.contentHash=ContestNetInventory::fingerprint(record);
        contestqso::Encoder encoder("transfer","log",row); QVERIFY(encoder.ok()); contestqso::Receiver receiver("sender","transfer");
        QUdpSocket source, destination; QVERIFY(source.bind(QHostAddress::LocalHost,0)); QVERIFY(destination.bind(QHostAddress::LocalHost,0));
        auto first=ContestNet::pack("FIELD","sender","qso_chunk_v1",encoder.frame(0)); QCOMPARE(source.writeDatagram(first,QHostAddress::LocalHost,destination.localPort()),qint64(first.size()));
        QTRY_VERIFY(destination.hasPendingDatagrams()); QByteArray data(int(destination.pendingDatagramSize()),0); destination.readDatagram(data.data(),data.size());
        QString from,type; QJsonObject body; QVERIFY(ContestNet::unpack(data,"FIELD",&from,&type,&body)); QVERIFY(receiver.accept(from,body)); QVERIFY(!receiver.complete());
        auto request=ContestNet::pack("FIELD","receiver","qso_missing_v1",receiver.missingRequest(0)); QCOMPARE(destination.writeDatagram(request,QHostAddress::LocalHost,source.localPort()),qint64(request.size()));
        QTRY_VERIFY(source.hasPendingDatagrams()); data.resize(int(source.pendingDatagramSize())); source.readDatagram(data.data(),data.size());
        QVERIFY(ContestNet::unpack(data,"FIELD",&from,&type,&body)); QCOMPARE(from,QString("receiver")); QCOMPARE(type,QString("qso_missing_v1"));
        const auto frames=encoder.framesForRequest(body); QCOMPARE(frames.size(),encoder.count()-1);
        for (const auto& frame:frames) { const auto packet=ContestNet::pack("FIELD","sender","qso_chunk_v1",frame); QCOMPARE(source.writeDatagram(packet,QHostAddress::LocalHost,destination.localPort()),qint64(packet.size())); }
        for (int i=0;i<frames.size();++i) { QTRY_VERIFY(destination.hasPendingDatagrams()); data.resize(int(destination.pendingDatagramSize())); destination.readDatagram(data.data(),data.size()); QVERIFY(ContestNet::unpack(data,"FIELD",&from,&type,&body)); QVERIFY(receiver.accept(from,body)); }
        QVERIFY(receiver.complete()); QCOMPARE(receiver.row().adifPayload,row.adifPayload);
    }
    void qsoTransferAcceptsInventorySnapshot()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path=dir.filePath("source.sqlite"); LogDatabase db; QVERIFY(db.open(path));
        AdifRecord record{{"CALL","K1AAA"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"COMMENT",QString::fromUtf8("Città 😀 <tag>")}};
        const auto inserted=db.insertQso(record,"fixture"); QVERIFY(inserted.id>0);
        ContestNetInventory inventory; QVERIFY(inventory.begin(path)); const auto page=inventory.next(10); QVERIFY(page.ok); QCOMPARE(page.rows.size(),1);
        const auto original=page.rows.first(); contestqso::Encoder encoder("transfer",inventory.logIdentity(),original); QVERIFY2(encoder.ok(),qPrintable(encoder.error()));
        contestqso::Receiver receiver("node","transfer"); for (int i=0;i<encoder.count();++i) QVERIFY(receiver.accept("node",encoder.frame(i)));
        QVERIFY(receiver.complete()); QCOMPARE(receiver.row().uuid,original.uuid); QCOMPARE(receiver.row().adifPayload,original.adifPayload);
        QCOMPARE(receiver.sourceLog(),inventory.logIdentity()); QCOMPARE(db.record(inserted.id)->value("COMMENT"),record.value("COMMENT"));
    }
    void qsoTransferApplyMissingIsAtomicAndGuarded()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); LogDatabase source,target; QVERIFY(source.open(dir.filePath("source.sqlite"))); QVERIFY(target.open(dir.filePath("target.sqlite")));
        AdifRecord record{{"CALL","K1AAA"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"COMMENT","test"}};
        auto inserted=source.insertQso(record,"fixture"); QVERIFY(inserted.id>0); ContestNetInventory inventory; QVERIFY(inventory.begin(dir.filePath("source.sqlite")));
        const auto page=inventory.next(10); QVERIFY(page.ok); const auto original=page.rows.first(); contestqso::Encoder encoder("transfer",inventory.logIdentity(),original);
        contestqso::Receiver receiver("node","transfer"); for (int i=0;i<encoder.count();++i) QVERIFY(receiver.accept("node",encoder.frame(i)));
        auto result=contestqso::applyMissing(target,"wrong-log",receiver); QVERIFY(!result.ok); QCOMPARE(target.idForUuid(original.uuid),qint64(0));
        result=contestqso::applyMissing(target,target.identity(),receiver); QVERIFY2(result.ok,qPrintable(result.error)); QVERIFY(result.inserted);
        const auto id=target.idForUuid(original.uuid); QVERIFY(id>0); QCOMPARE(ContestNetInventory::fingerprint(*target.record(id)),original.contentHash);
        const auto changes=target.contestNetChangesAfter(0,100); QVERIFY(changes.ok); QCOMPARE(changes.changes.size(),1); QCOMPARE(changes.changes.first().qsoUuid,original.uuid); QVERIFY(changes.changes.first().previousQsoUuid.isEmpty());
        result=contestqso::applyMissing(target,target.identity(),receiver); QVERIFY(result.ok); QVERIFY(!result.inserted); QCOMPARE(target.idForUuid(original.uuid),id);
        contestqso::Receiver partial("node","another"); result=contestqso::applyMissing(target,target.identity(),partial); QVERIFY(!result.ok);
        auto edited=*target.record(id); edited.set("COMMENT","local edit"); QVERIFY(target.updateQso(id,edited).status==InsertResult::Status::Inserted);
        result=contestqso::applyMissing(target,target.identity(),receiver); QVERIFY(!result.ok); QCOMPARE(target.record(id)->value("COMMENT"),QString("local edit"));
        LogDatabase failed; QVERIFY(failed.open(dir.filePath("failed.sqlite"))); QSqlQuery q(failed.connection());
        QVERIFY(q.exec("CREATE TRIGGER reject_identity BEFORE UPDATE OF uuid ON qso BEGIN SELECT RAISE(ABORT, 'injected'); END"));
        result=contestqso::applyMissing(failed,failed.identity(),receiver); QVERIFY(!result.ok); QCOMPARE(failed.latestIds(QString(),100).size(),0);
        QVERIFY(q.exec("SELECT count(*) FROM contest_net_change")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),0);
        QVERIFY(q.exec("DROP TRIGGER reject_identity")); result=contestqso::applyMissing(failed,failed.identity(),receiver); QVERIFY2(result.ok,qPrintable(result.error));
        LogDatabase duplicate; QVERIFY(duplicate.open(dir.filePath("duplicate.sqlite"))); QVERIFY(duplicate.insertQso(record,"fixture").id>0);
        result=contestqso::applyMissing(duplicate,duplicate.identity(),receiver); QVERIFY(!result.ok); QCOMPARE(duplicate.latestIds(QString(),100).size(),1);
    }
    void reviewedQsoChangesRejectStaleLocalState()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path=dir.filePath("source.sqlite"); LogDatabase source,target;
        QVERIFY(source.open(path)); QVERIFY(target.open(dir.filePath("target.sqlite")));
        AdifRecord record{{"CALL","K1AAA"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"COMMENT","original"}};
        const auto initial=source.insertQso(record,"fixture"); QVERIFY(initial.id>0);
        auto receive=[&](const QString& transfer) {
            ContestNetInventory inventory; if (!inventory.begin(path)) return contestqso::Receiver("node",transfer);
            const auto page=inventory.next(10); contestqso::Receiver receiver("node",transfer); if (!page.ok || page.rows.isEmpty()) return receiver;
            contestqso::Encoder encoder(transfer,inventory.logIdentity(),page.rows.first()); for (int i=0;i<encoder.count();++i) receiver.accept("node",encoder.frame(i)); return receiver;
        };
        auto original=receive("initial"); QVERIFY(original.complete()); const auto add=contestqso::applyMissing(target,target.identity(),original); QVERIFY(add.ok);
        record.set("COMMENT","remote edit"); QVERIFY(source.updateQso(initial.id,record).status==InsertResult::Status::Inserted);
        auto remote=receive("edit"); QVERIFY(remote.complete()); auto review=contestqso::reviewExisting(target,target.identity(),remote); QVERIFY2(review.ok,qPrintable(review.error));
        QslState qsl; qsl.service="lotw"; qsl.sent="Y"; QVERIFY(target.setQslState(add.id,qsl));
        auto staleQsl=contestqso::applyReviewed(target,target.identity(),remote,review); QVERIFY(!staleQsl.ok);
        review=contestqso::reviewExisting(target,target.identity(),remote); QVERIFY(review.ok);
        QSqlQuery fault(target.connection()); QVERIFY(fault.exec("CREATE TRIGGER reject_review BEFORE UPDATE OF comment ON qso BEGIN SELECT RAISE(ABORT, 'injected'); END"));
        auto failed=contestqso::applyReviewed(target,target.identity(),remote,review); QVERIFY(!failed.ok); QCOMPARE(target.record(add.id)->value("COMMENT"),QString("original"));
        QVERIFY(fault.exec("DROP TRIGGER reject_review"));
        auto local=*target.record(add.id); local.set("COMMENT","local edit"); QVERIFY(target.updateQso(add.id,local).status==InsertResult::Status::Inserted);
        auto changed=contestqso::applyReviewed(target,target.identity(),remote,review); QVERIFY(!changed.ok); QCOMPARE(target.record(add.id)->value("COMMENT"),QString("local edit"));
        review=contestqso::reviewExisting(target,target.identity(),remote); QVERIFY(review.ok); changed=contestqso::applyReviewed(target,target.identity(),remote,review);
        QVERIFY2(changed.ok,qPrintable(changed.error)); QCOMPARE(target.record(add.id)->value("COMMENT"),QString("remote edit"));
        QVERIFY(source.softDeleteQso(initial.id)); auto deletion=receive("delete"); QVERIFY(deletion.complete()); QVERIFY(deletion.row().deleted);
        review=contestqso::reviewExisting(target,target.identity(),deletion); QVERIFY(review.ok); changed=contestqso::applyReviewed(target,target.identity(),deletion,review);
        QVERIFY2(changed.ok,qPrintable(changed.error)); QVERIFY(target.meta(add.id)->deleted);
    }
    void qsoTransferRejectsMalformedFragments()
    {
        AdifRecord record{{"CALL","9H1SR"},{"COMMENT",QString(700,QLatin1Char('b'))}};
        ContestNetInventoryRow row; row.uuid="qso"; row.revision=2; row.adifPayload=adif::writeRecord(record).toUtf8(); row.contentHash=ContestNetInventory::fingerprint(record);
        contestqso::Encoder encoder("transfer","log",row); QVERIFY(encoder.ok());
        for (const QJsonValue value : {QJsonValue(1.5), QJsonValue(-1), QJsonValue("1"), QJsonValue(1e20)}) {
            contestqso::Receiver r("node","transfer"); auto f=encoder.frame(0); f.insert("size",value); QVERIFY(!r.accept("node",f));
        }
        contestqso::Receiver r("node","transfer"); auto f=encoder.frame(0); f.insert("data","!!!!"); QVERIFY(!r.accept("node",f));
        QVERIFY(r.accept("node",encoder.frame(0))); f=encoder.frame(0); auto bytes=QByteArray::fromBase64(f.value("data").toString().toLatin1()); bytes[0]='z'; f.insert("data",QString::fromLatin1(bytes.toBase64()));
        QVERIFY(!r.accept("node",f)); QVERIFY(!r.complete()); QVERIFY(r.accept("node",encoder.frame(1))); QVERIFY(r.complete());
        row.adifPayload=QByteArray(4*1024*1024+1,'x'); contestqso::Encoder large("transfer","log",row); QVERIFY(!large.ok()); QCOMPARE(large.count(),0);
    }
    void packUnpack()
    {
        const QByteArray d = ContestNet::pack("FIELDDAY", "pc1", "gab", QJsonObject{{"text", "QRV 40m"}});
        QString from, type;
        QJsonObject body;
        QVERIFY(ContestNet::unpack(d, "fieldday", &from, &type, &body));
        QCOMPARE(from, QString("pc1"));
        QCOMPARE(type, QString("gab"));
        QCOMPARE(body.value("text").toString(), QString("QRV 40m"));
        QVERIFY(!body.contains("_net"));
        // Un'altra rete sulla stessa LAN non si sente.
        QVERIFY(!ContestNet::unpack(d, "OTHER", &from, &type, &body));
        QVERIFY(!ContestNet::unpack("hello world", "FIELDDAY", &from, &type, &body));
    }

    void receiveFromAnotherPc()
    {
        ContestNet net;
        // Una porta libera.
        QUdpSocket probe;
        QVERIFY(probe.bind(QHostAddress::LocalHost, 0));
        const quint16 port = probe.localPort();
        probe.close();
        QVERIFY(net.start(port, "M2", "me"));
        QSignalSpy spy(&net, &ContestNet::received);
        QUdpSocket other;
        other.writeDatagram(ContestNet::pack("M2", "run2", "qso", QJsonObject{{"adif", "<CALL:4>W1AW<EOR>"}}),
                            QHostAddress::LocalHost, port);
        // Il proprio messaggio, tornato indietro, si ignora.
        other.writeDatagram(ContestNet::pack("M2", "me", "gab", QJsonObject{{"text", "echo"}}), QHostAddress::LocalHost, port);
        QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toString(), QString("run2"));
        QCOMPARE(spy.first().at(1).toString(), QString("qso"));
        QTest::qWait(200);
        QCOMPARE(spy.count(), 1);
        net.stop();
        QVERIFY(!net.running());
    }
};

QTEST_GUILESS_MAIN(TestContestNet)
#include "tst_contestnet.moc"
