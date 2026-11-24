// La rete multi-operatore: messaggi impacchettati e ricevuti.
#include "core/ContestNet.h"
#include "core/ContestNetSecure.h"
#include "../libs/monocypher/monocypher.h"
#include "core/ContestNetQsoTransfer.h"
#include "core/ContestSerialProtocol.h"
#include "core/ContestHandoff.h"
#include "core/LogDatabase.h"

#include <QSignalSpy>
#include <QSqlQuery>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>
#include <QUdpSocket>
#include <QTemporaryDir>

using namespace decolog::core;

class TestContestNet : public QObject {
    Q_OBJECT

private slots:
    void skedReminderIsDueAcceptedAndPersistent()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path=dir.filePath("reminders.sqlite"); LogDatabase db; QVERIFY(db.open(path));
        contesthandoff::Offer offer{"8f3b7450-8e62-4dbd-8671-001122334455","event","sender","receiver","K1ABC",14025000,"2026-11-24T12:00:00Z"};
        offer.scheduledUtc="2026-11-25T18:30:00Z"; const auto frame=contesthandoff::offerFrame(offer);
        QVERIFY(contesthandoff::receiveOffer(db,"event","receiver","sender",frame).ok);
        const auto scheduled=QDateTime::fromString(offer.scheduledUtc,Qt::ISODate);
        QVERIFY(!contesthandoff::claimSkedReminder(db,"event","receiver",offer.id,scheduled).ok);
        QVERIFY(contesthandoff::setIncomingState(db,"event","receiver",offer.id,"accepted").ok);
        QVERIFY(!contesthandoff::claimSkedReminder(db,"event","receiver",offer.id,scheduled.addSecs(-301)).ok);
        QVERIFY(!contesthandoff::claimSkedReminder(db,"event","receiver",offer.id,scheduled.addSecs(601)).ok);
        QVERIFY(!contesthandoff::claimSkedReminder(db,"wrong","receiver",offer.id,scheduled).ok);
        const auto claimed=contesthandoff::claimSkedReminder(db,"event","receiver",offer.id,scheduled.addSecs(-300)); QVERIFY2(claimed.ok,qPrintable(claimed.error)); QVERIFY(!claimed.replayed);
        QVERIFY(contesthandoff::claimSkedReminder(db,"event","receiver",offer.id,scheduled).replayed);
        db.close(); QVERIFY(db.open(path)); QVERIFY(contesthandoff::claimSkedReminder(db,"event","receiver",offer.id,scheduled).replayed);
        QCOMPARE(db.qsoCount(),qint64(0));
    }
    void skedPreservesUtcAndOfferCompatibility()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); LogDatabase a,b; QVERIFY(a.open(dir.filePath("a.sqlite"))); QVERIFY(b.open(dir.filePath("b.sqlite")));
        contesthandoff::Offer offer{"8f3b7450-8e62-4dbd-8671-001122334455","contest-2026","node-a","node-b","K1ABC",14025000,"2026-11-24T12:00:00Z"};
        QCOMPARE(contesthandoff::offerFrame(offer).value("version").toInt(),1);
        offer.scheduledUtc="2026-11-25T18:30:00Z"; const auto frame=contesthandoff::offerFrame(offer);
        QCOMPARE(frame.value("version").toInt(),2); QCOMPARE(frame.value("scheduledUtc").toString(),offer.scheduledUtc);
        QVERIFY(contesthandoff::storeOutgoing(a,offer).ok); QVERIFY(contesthandoff::receiveOffer(b,"contest-2026","node-b","node-a",frame).ok);
        QVERIFY(contesthandoff::receiveOffer(b,"contest-2026","node-b","node-a",frame).replayed);
        auto wrong=frame; wrong.insert("scheduledUtc","2026-11-25T18:31:00Z"); QVERIFY(!contesthandoff::receiveOffer(b,"contest-2026","node-b","node-a",wrong).ok);
        QVERIFY(contesthandoff::setIncomingState(b,"contest-2026","node-b",offer.id,"accepted").ok);
        QVERIFY(contesthandoff::receiveRemoteState(a,"contest-2026","node-a","node-b",frame,"accepted").ok);
        a.close(); b.close(); QVERIFY(a.open(dir.filePath("a.sqlite"))); QVERIFY(b.open(dir.filePath("b.sqlite")));
        QCOMPARE(contesthandoff::incomingOffers(b,"contest-2026","node-b").first().scheduledUtc,offer.scheduledUtc);
        QCOMPARE(contesthandoff::outgoingOffers(a,"contest-2026","node-a").first().state,QString("accepted"));
        offer.scheduledUtc="2026-11-25T18:30:00+01:00"; QVERIFY(contesthandoff::offerFrame(offer).isEmpty());
        offer.scheduledUtc="not-a-date"; QVERIFY(contesthandoff::offerFrame(offer).isEmpty());
        QCOMPARE(a.qsoCount(),qint64(0)); QCOMPARE(b.qsoCount(),qint64(0));
    }
    void handoffRemoteStateRejectsWrongPeerAndReordering()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path=dir.filePath("sender.sqlite"); LogDatabase sender; QVERIFY(sender.open(path));
        contesthandoff::Offer offer{"8f3b7450-8e62-4dbd-8671-001122334455","contest-2026","node-a","node-b","K1ABC",14025000,"2026-11-24T12:00:00Z"};
        QVERIFY(contesthandoff::storeOutgoing(sender,offer).ok); const auto frame=contesthandoff::offerFrame(offer);
        QVERIFY(!contesthandoff::receiveRemoteState(sender,"contest-2026","node-a","impostor",frame,"accepted").ok);
        QVERIFY(contesthandoff::receiveRemoteState(sender,"contest-2026","node-a","node-b",frame,"completed").ok);
        QVERIFY(contesthandoff::receiveRemoteState(sender,"contest-2026","node-a","node-b",frame,"accepted").replayed);
        QVERIFY(!contesthandoff::receiveRemoteState(sender,"contest-2026","node-a","node-b",frame,"declined").ok);
        auto modified=frame; modified.insert("call","K2ABC"); QVERIFY(!contesthandoff::receiveRemoteState(sender,"contest-2026","node-a","node-b",modified,"completed").ok);
        sender.close(); QVERIFY(sender.open(path)); const auto list=contesthandoff::outgoingOffers(sender,"contest-2026","node-a");
        QCOMPARE(list.size(),1); QCOMPARE(list.first().state,QString("completed")); QCOMPARE(sender.qsoCount(),qint64(0));
    }
    void handoffOfferIsAddressedIdempotentAndPersistent()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path=dir.filePath("handoff.sqlite"); LogDatabase recipient; QVERIFY(recipient.open(path));
        contesthandoff::Offer offer{"8f3b7450-8e62-4dbd-8671-001122334455","contest-2026","node-a","node-b","K1ABC",14025000,"2026-11-24T12:00:00Z"};
        const auto body=contesthandoff::offerFrame(offer); QVERIFY(!body.isEmpty());
        QString from,type; QJsonObject received; const auto packet=ContestNet::pack("FIELD","node-a","handoff_offer_v1",body);
        QVERIFY(ContestNet::unpack(packet,"FIELD",&from,&type,&received)); QCOMPARE(type,QString("handoff_offer_v1"));
        auto first=contesthandoff::receiveOffer(recipient,"contest-2026","node-b",from,received);
        QCOMPARE(contesthandoff::incomingOffers(recipient,"contest-2026","node-b").first().state,QString("pending"));
        QVERIFY(!contesthandoff::setIncomingState(recipient,"other-contest","node-b",offer.id,"accepted").ok);
        QVERIFY(!contesthandoff::setIncomingState(recipient,"contest-2026","node-b",offer.id,"completed").ok);
        QVERIFY(contesthandoff::setIncomingState(recipient,"contest-2026","node-b",offer.id,"accepted").ok);
        QCOMPARE(contesthandoff::incomingOffers(recipient,"contest-2026","node-b").first().state,QString("accepted"));
        QVERIFY(contesthandoff::setIncomingState(recipient,"contest-2026","node-b",offer.id,"completed").ok);
        QVERIFY(!contesthandoff::setIncomingState(recipient,"contest-2026","node-b",offer.id,"declined").ok);
        QVERIFY2(first.ok,qPrintable(first.error)); QVERIFY(!first.replayed);
        auto repeated=contesthandoff::receiveOffer(recipient,"contest-2026","node-b",from,received); QVERIFY(repeated.ok); QVERIFY(repeated.replayed);
        QCOMPARE(contesthandoff::incomingOffers(recipient,"contest-2026","node-b").first().state,QString("completed"));
        auto other=offer; other.id=QStringLiteral("8f3b7450-8e62-4dbd-8671-001122334456");
        QVERIFY(contesthandoff::receiveOffer(recipient,"contest-2026","node-b",from,contesthandoff::offerFrame(other)).ok);
        QVERIFY(contesthandoff::setIncomingState(recipient,"contest-2026","node-b",other.id,"declined").ok);
        QVERIFY(!contesthandoff::setIncomingState(recipient,"contest-2026","node-b",other.id,"accepted").ok);
        auto changed=received; changed.insert("frequencyHz",14026000); QVERIFY(!contesthandoff::receiveOffer(recipient,"contest-2026","node-b",from,changed).ok);
        QVERIFY(!contesthandoff::receiveOffer(recipient,"contest-2026","node-c",from,received).ok);
        QVERIFY(!contesthandoff::receiveOffer(recipient,"contest-2026","node-b","impostor",received).ok);
        QVERIFY(!contesthandoff::receiveOffer(recipient,"other-contest","node-b",from,received).ok);
        for (const auto &bad : {QJsonValue(-1),QJsonValue(14025000.5),QJsonValue(QStringLiteral("14025000"))}) {
            auto invalid=received; invalid.insert("frequencyHz",bad); QVERIFY(!contesthandoff::receiveOffer(recipient,"contest-2026","node-b",from,invalid).ok);
        }
        auto unexpected=received; unexpected.insert("extra",true); QVERIFY(!contesthandoff::receiveOffer(recipient,"contest-2026","node-b",from,unexpected).ok);
        recipient.close(); QVERIFY(recipient.open(path)); QString error; const auto offers=contesthandoff::incomingOffers(recipient,"contest-2026","node-b",&error);
        QCOMPARE(offers.first().state,QString("completed")); QCOMPARE(offers.last().state,QString("declined"));
        QVERIFY2(error.isEmpty(),qPrintable(error)); QCOMPARE(offers.size(),2); QCOMPARE(offers.first().call,QString("K1ABC")); QCOMPARE(offers.first().frequencyHz,qint64(14025000));
        repeated=contesthandoff::receiveOffer(recipient,"contest-2026","node-b",from,received); QVERIFY(repeated.ok); QVERIFY(repeated.replayed);
        QCOMPARE(recipient.qsoCount(),qint64(0));
    }
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
    void qsoTransferSupportsCompactChunksWithoutChangingLegacyFrames()
    {
        AdifRecord record{{"CALL","K1AAA"},{"COMMENT",QString(900,'x')}}; ContestNetInventoryRow row; row.uuid="qso"; row.revision=1;
        row.adifPayload=adif::writeRecord(record).toUtf8(); row.contentHash=ContestNetInventory::fingerprint(record);
        contestqso::Encoder legacy("transfer","log",row),compact("transfer","log",row,256),invalid("transfer","log",row,255);
        QVERIFY(legacy.ok() && compact.ok()); QVERIFY(!invalid.ok()); QVERIFY(!legacy.frame(0).contains("chunkSize")); QCOMPARE(compact.frame(0).value("chunkSize").toInt(),256);
        contestqso::Receiver receiver("sender","transfer"); QVERIFY(receiver.accept("sender",compact.frame(0)));
        auto altered=compact.frame(1); altered.insert("chunkSize",512); QVERIFY(!receiver.accept("sender",altered));
        altered=compact.frame(1); altered.insert("chunkSize",256.5); QVERIFY(!receiver.accept("sender",altered));
        for (int i=1;i<compact.count();++i) QVERIFY(receiver.accept("sender",compact.frame(i)));
        QVERIFY(receiver.complete()); QCOMPARE(receiver.row().adifPayload,row.adifPayload);
    }
    void qsoTransferRequestsMissingFragments()
    {
        AdifRecord record{{"CALL","K1AAA"},{"COMMENT",QString(40000,QLatin1Char('r'))}};
        ContestNetInventoryRow row; row.uuid="qso"; row.revision=2; row.adifPayload=adif::writeRecord(record).toUtf8(); row.contentHash=ContestNetInventory::fingerprint(record);
        contestqso::Encoder sender("transfer","log",row); QVERIFY(sender.ok());
        contestqso::Receiver receiver("node","transfer"); QVERIFY(receiver.accept("node",sender.frame(sender.count()-1)));
        auto req=receiver.missingRequest(0); QVERIFY(!req.isEmpty()); auto frames=sender.framesForRequest(req); QCOMPARE(frames.size(),32);
        auto bad=req; bad.insert("sha256",QString(64,QLatin1Char('0'))); QVERIFY(sender.framesForRequest(bad).isEmpty());
        auto changedRecord=record; changedRecord.set("COMMENT",QString(40000,QLatin1Char('s'))); auto changedRow=row;
        changedRow.adifPayload=adif::writeRecord(changedRecord).toUtf8(); changedRow.contentHash=ContestNetInventory::fingerprint(changedRecord);
        contestqso::Encoder changedSender("transfer","log",changedRow); QVERIFY(changedSender.ok()); QVERIFY(changedSender.framesForRequest(req).isEmpty());
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
        QVERIFY(target.recordLotwGridObservation(add.id,"JN58",""));
        auto staleObservation=contestqso::applyReviewed(target,target.identity(),remote,review); QVERIFY(!staleObservation.ok);
        QCOMPARE(target.record(add.id)->value("COMMENT"),QString("original"));
        review=contestqso::reviewExisting(target,target.identity(),remote); QVERIFY(review.ok);
        QSqlQuery observation(target.connection());
        observation.prepare("INSERT INTO lotw_grid_match_context(observation_id,distance_seconds,station_call,source_qso_time,rxqsl,recorded_at) SELECT id,1,'K1LOCAL','2026-01-01T12:00:00Z','','2026-01-01T12:00:00Z' FROM lotw_grid_observation WHERE qso_id=?");
        observation.addBindValue(add.id); QVERIFY(observation.exec());
        QVERIFY(!contestqso::applyReviewed(target,target.identity(),remote,review).ok);
        const auto other=target.insertQso(AdifRecord{{"CALL","K1BBB"},{"QSO_DATE","20260101"},{"TIME_ON","1300"},{"BAND","20m"},{"MODE","CW"}},"fixture"); QVERIFY(other.id>0);
        QVERIFY(target.recordLotwGridObservation(other.id,"JN59",""));
        review=contestqso::reviewExisting(target,target.identity(),remote); QVERIFY(review.ok);
        observation.prepare("INSERT INTO lotw_grid_observation_link(qso_id,observation_id,linked_at) SELECT ?,id,'2026-01-01T12:00:00Z' FROM lotw_grid_observation WHERE qso_id=?");
        observation.addBindValue(add.id); observation.addBindValue(other.id); QVERIFY(observation.exec());
        QVERIFY(!contestqso::applyReviewed(target,target.identity(),remote,review).ok);
        review=contestqso::reviewExisting(target,target.identity(),remote); QVERIFY(review.ok);
        QslState qsl; qsl.service="lotw"; qsl.sent="Y"; QVERIFY(target.setQslState(add.id,qsl));
        auto staleQsl=contestqso::applyReviewed(target,target.identity(),remote,review); QVERIFY(!staleQsl.ok);
        review=contestqso::reviewExisting(target,target.identity(),remote); QVERIFY(review.ok);
        qsl.remoteId="remote-service-id"; qsl.lastError="service error"; QVERIFY(target.setQslState(add.id,qsl));
        auto staleMetadata=contestqso::applyReviewed(target,target.identity(),remote,review); QVERIFY(!staleMetadata.ok);
        review=contestqso::reviewExisting(target,target.identity(),remote); QVERIFY(review.ok);
        QVERIFY(target.addQslAttachment("fixture image","card.jpg","image/jpeg",{add.id})>0);
        auto staleAttachment=contestqso::applyReviewed(target,target.identity(),remote,review); QVERIFY(!staleAttachment.ok);
        review=contestqso::reviewExisting(target,target.identity(),remote); QVERIFY(review.ok);
        QVERIFY(target.addQslAttachment("fixture image","card.jpg","image/jpeg",{add.id})>0);
        auto staleRelink=contestqso::applyReviewed(target,target.identity(),remote,review); QVERIFY(!staleRelink.ok);
        review=contestqso::reviewExisting(target,target.identity(),remote); QVERIFY(review.ok);
        QSqlQuery fault(target.connection()); QVERIFY(fault.exec("UPDATE qsl_attachment SET data=X'6669787475726520696D616746'"));
        auto staleBytes=contestqso::applyReviewed(target,target.identity(),remote,review); QVERIFY(!staleBytes.ok);
        QByteArray large(2097153,'x'); const auto largeId=target.addQslAttachment(large,"large.jpg","image/jpeg",{add.id}); QVERIFY(largeId>0);
        review=contestqso::reviewExisting(target,target.identity(),remote); QVERIFY(review.ok);
        large[1048576]='y'; fault.prepare("UPDATE qsl_attachment SET data=? WHERE id=?"); fault.addBindValue(large); fault.addBindValue(largeId); QVERIFY(fault.exec());
        QVERIFY(!contestqso::applyReviewed(target,target.identity(),remote,review).ok);
        review=contestqso::reviewExisting(target,target.identity(),remote); QVERIFY(review.ok);
        large[2097152]='z'; fault.prepare("UPDATE qsl_attachment SET data=? WHERE id=?"); fault.addBindValue(large); fault.addBindValue(largeId); QVERIFY(fault.exec());
        QVERIFY(!contestqso::applyReviewed(target,target.identity(),remote,review).ok);
        review=contestqso::reviewExisting(target,target.identity(),remote); QVERIFY(review.ok);
        QVERIFY(fault.exec("ALTER TABLE qso_enrichment RENAME TO hidden_enrichment"));
        auto unreadable=contestqso::applyReviewed(target,target.identity(),remote,review); QVERIFY(!unreadable.ok);
        QVERIFY(!contestqso::reviewExisting(target,target.identity(),remote).ok);
        QVERIFY(fault.exec("ALTER TABLE hidden_enrichment RENAME TO qso_enrichment"));
        review=contestqso::reviewExisting(target,target.identity(),remote); QVERIFY(review.ok);
        QVERIFY(fault.exec("ALTER TABLE lotw_grid_observation_link RENAME TO hidden_observation_link"));
        QVERIFY(!contestqso::applyReviewed(target,target.identity(),remote,review).ok);
        QVERIFY(!contestqso::reviewExisting(target,target.identity(),remote).ok);
        QVERIFY(fault.exec("ALTER TABLE hidden_observation_link RENAME TO lotw_grid_observation_link"));
        review=contestqso::reviewExisting(target,target.identity(),remote); QVERIFY(review.ok); QVERIFY(fault.exec("CREATE TRIGGER reject_review BEFORE UPDATE OF comment ON qso BEGIN SELECT RAISE(ABORT, 'injected'); END"));
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
    void threeNodeQsoRejoinRequiresExplicitConflictReview()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path=dir.filePath("origin.sqlite"); LogDatabase origin,one,two;
        QVERIFY(origin.open(path)); QVERIFY(one.open(dir.filePath("one.sqlite"))); QVERIFY(two.open(dir.filePath("two.sqlite")));
        AdifRecord record{{"CALL","K1AAA"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"COMMENT",QString(1800,'a')}};
        const auto inserted=origin.insertQso(record,"fixture"); QVERIFY(inserted.id>0); const QString uuid=origin.meta(inserted.id)->uuid;
        QUdpSocket sender,first,second; QVERIFY(sender.bind(QHostAddress::LocalHost,0)); QVERIFY(first.bind(QHostAddress::LocalHost,0)); QVERIFY(second.bind(QHostAddress::LocalHost,0));
        auto snapshot=[&](ContestNetInventoryRow& row,QString& identity) {
            ContestNetInventory inv; if (!inv.begin(path)) return false; const auto page=inv.findUuid(uuid); if (!page.ok || page.rows.size()!=1) return false;
            row=page.rows.first(); identity=inv.logIdentity(); return true;
        };
        auto receive=[&](const ContestNetInventoryRow& row,const QString& identity,const QString& transfer,QUdpSocket& destination,contestqso::Receiver& receiver,bool restartSource) {
            contestqso::Encoder encoder(transfer,identity,row); if (!encoder.ok() || encoder.count()<2) return false;
            auto send=[&](const QJsonObject& body) {
                const auto packet=ContestNet::pack("FIELD","origin","qso_chunk_v1",body);
                if (sender.writeDatagram(packet,QHostAddress::LocalHost,destination.localPort())!=packet.size() || !destination.waitForReadyRead(3000)) return false;
                QByteArray bytes(int(destination.pendingDatagramSize()),0); if (destination.readDatagram(bytes.data(),bytes.size())!=bytes.size()) return false;
                QString from,type; QJsonObject decoded; return ContestNet::unpack(bytes,"FIELD",&from,&type,&decoded) && type=="qso_chunk_v1" && receiver.accept(from,decoded);
            };
            // Omit index zero, send the rest backwards, and replay one fragment.
            for (int i=encoder.count()-1;i>0;--i) if (!send(encoder.frame(i))) return false;
            if (receiver.complete() || !send(encoder.frame(1))) return false;
            const auto request=ContestNet::pack("FIELD","peer","qso_missing_v1",receiver.missingRequest(0));
            if (destination.writeDatagram(request,QHostAddress::LocalHost,sender.localPort())!=request.size() || !sender.waitForReadyRead(3000)) return false;
            QByteArray bytes(int(sender.pendingDatagramSize()),0); if (sender.readDatagram(bytes.data(),bytes.size())!=bytes.size()) return false;
            QString from,type; QJsonObject decoded; if (!ContestNet::unpack(bytes,"FIELD",&from,&type,&decoded) || type!="qso_missing_v1") return false;
            QList<QJsonObject> retry;
            if (restartSource) {
                origin.close(); if (!origin.open(path)) return false;
                ContestNetInventoryRow reopened; QString reopenedIdentity; if (!snapshot(reopened,reopenedIdentity)) return false;
                contestqso::Encoder resumed(transfer,reopenedIdentity,reopened); if (!resumed.ok()) return false;
                retry=resumed.framesForRequest(decoded);
            } else retry=encoder.framesForRequest(decoded);
            if (retry.size()!=1) return false;
            return send(retry.first()) && receiver.complete();
        };
        ContestNetInventoryRow row; QString identity; QVERIFY(snapshot(row,identity));
        contestqso::Receiver initialOne("origin","initial-one"),initialTwo("origin","initial-two");
        QVERIFY(receive(row,identity,"initial-one",first,initialOne,true)); QVERIFY(contestqso::applyMissing(one,one.identity(),initialOne).ok);
        // Second station is partitioned until this point: no local QSO exists.
        QCOMPARE(two.idForUuid(uuid),qint64(0)); QVERIFY(receive(row,identity,"initial-two",second,initialTwo,false)); QVERIFY(contestqso::applyMissing(two,two.identity(),initialTwo).ok);
        const auto idOne=one.idForUuid(uuid),idTwo=two.idForUuid(uuid); QVERIFY(idOne>0 && idTwo>0);
        record.set("NAME","Origin edit"); QVERIFY(origin.updateQso(inserted.id,record).status==InsertResult::Status::Inserted); QVERIFY(snapshot(row,identity));
        contestqso::Receiver editOne("origin","edit-one"),editTwo("origin","edit-two"); QVERIFY(receive(row,identity,"edit-one",first,editOne,false)); QVERIFY(receive(row,identity,"edit-two",second,editTwo,false));
        auto reviewOne=contestqso::reviewExisting(one,one.identity(),editOne),reviewTwo=contestqso::reviewExisting(two,two.identity(),editTwo); QVERIFY(reviewOne.ok && reviewTwo.ok);
        auto concurrent=*two.record(idTwo); concurrent.set("NAME","Local edit"); QVERIFY(two.updateQso(idTwo,concurrent).status==InsertResult::Status::Inserted);
        QVERIFY(contestqso::applyReviewed(one,one.identity(),editOne,reviewOne).ok); QVERIFY(!contestqso::applyReviewed(two,two.identity(),editTwo,reviewTwo).ok);
        QCOMPARE(two.record(idTwo)->value("NAME"),QString("Local edit")); two.close(); QVERIFY(two.open(dir.filePath("two.sqlite")));
        reviewTwo=contestqso::reviewExisting(two,two.identity(),editTwo); QVERIFY(reviewTwo.ok); QVERIFY(contestqso::applyReviewed(two,two.identity(),editTwo,reviewTwo).ok);
        QCOMPARE(ContestNetInventory::fingerprint(*one.record(idOne)),ContestNetInventory::fingerprint(*two.record(idTwo)));
        QVERIFY(origin.softDeleteQso(inserted.id)); QVERIFY(snapshot(row,identity)); QVERIFY(row.deleted);
        contestqso::Receiver deleteOne("origin","delete-one"),deleteTwo("origin","delete-two"); QVERIFY(receive(row,identity,"delete-one",first,deleteOne,false)); QVERIFY(receive(row,identity,"delete-two",second,deleteTwo,false));
        reviewOne=contestqso::reviewExisting(one,one.identity(),deleteOne); reviewTwo=contestqso::reviewExisting(two,two.identity(),deleteTwo); QVERIFY(reviewOne.ok && reviewTwo.ok);
        concurrent=*two.record(idTwo); concurrent.set("NAME","Edit before delete"); QVERIFY(two.updateQso(idTwo,concurrent).status==InsertResult::Status::Inserted);
        QVERIFY(contestqso::applyReviewed(one,one.identity(),deleteOne,reviewOne).ok); QVERIFY(!contestqso::applyReviewed(two,two.identity(),deleteTwo,reviewTwo).ok); QVERIFY(!two.meta(idTwo)->deleted);
        reviewTwo=contestqso::reviewExisting(two,two.identity(),deleteTwo); QVERIFY(reviewTwo.ok); QVERIFY(contestqso::applyReviewed(two,two.identity(),deleteTwo,reviewTwo).ok);
        QVERIFY(one.meta(idOne)->deleted && two.meta(idTwo)->deleted);
        one.close(); two.close(); QVERIFY(one.open(dir.filePath("one.sqlite"))); QVERIFY(two.open(dir.filePath("two.sqlite")));
        QVERIFY(one.meta(idOne)->deleted && two.meta(idTwo)->deleted); QCOMPARE(one.idForUuid(uuid),idOne); QCOMPARE(two.idForUuid(uuid),idTwo);
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
    void monocypherXchachaMatchesPublishedVector()
    {
        // IRTF XChaCha draft-03, section A.3.1 (expired Internet-Draft).
        const auto key=QByteArray::fromHex("808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f");
        const auto nonce=QByteArray::fromHex("404142434445464748494a4b4c4d4e4f5051525354555657");
        const auto aad=QByteArray::fromHex("50515253c0c1c2c3c4c5c6c7");
        const auto cipher=QByteArray::fromHex("bd6d179d3e83d43b9576579493c0e939572a1700252bfaccbed2902c21396cbb731c7f1b0b4aa6440bf3a82f4eda7e39ae64c6708c54c216cb96b72e1213b4522f8c9ba40db5d945b11b69b982c1bb9e3f3fac2bc369488f76b2383565d3fff921f9664c97637da9768812f615c68b13b52e");
        const auto tag=QByteArray::fromHex("c0875924c1c7987947deafd8780acf49");
        const auto expected=QByteArray::fromHex("4c616469657320616e642047656e746c656d656e206f662074686520636c617373206f66202739393a204966204920636f756c64206f6666657220796f75206f6e6c79206f6e652074697020666f7220746865206675747572652c2073756e73637265656e20776f756c642062652069742e");
        QByteArray output(cipher.size(),Qt::Uninitialized);
        const auto p=[](const QByteArray& b){ return reinterpret_cast<const uint8_t*>(b.constData()); };
        QCOMPARE(crypto_aead_unlock(reinterpret_cast<uint8_t*>(output.data()),p(tag),p(key),p(nonce),p(aad),size_t(aad.size()),p(cipher),size_t(cipher.size())),0);
        QCOMPARE(output,expected);
    }
    void secureQsoFrameAuthenticatesRoutingAndContent()
    {
        const QByteArray key(32,'k'), wrong(32,'w'); const QJsonObject body{{"to","receiver"},{"transfer","t1"},{"data","private-qso"}};
        const auto sealed=contestsecure::seal(key,"FIELD","sender","receiver","qso_chunk_v1",body); QVERIFY2(sealed.ok,qPrintable(sealed.error));
        QVERIFY(!QJsonDocument(sealed.frame).toJson().contains("private-qso"));
        auto opened=contestsecure::open(key,"field","sender","receiver",sealed.frame); QVERIFY2(opened.ok,qPrintable(opened.error));
        QCOMPARE(opened.type,QString("qso_chunk_v1")); QCOMPARE(opened.body,body);
        QVERIFY(!contestsecure::open(wrong,"FIELD","sender","receiver",sealed.frame).ok);
        QVERIFY(!contestsecure::open(key,"OTHER","sender","receiver",sealed.frame).ok);
        QVERIFY(!contestsecure::open(key,"FIELD","impostor","receiver",sealed.frame).ok);
        QVERIFY(!contestsecure::open(key,"FIELD","sender","other",sealed.frame).ok);
        auto altered=sealed.frame; auto box=QByteArray::fromBase64(altered.value("box").toString().toLatin1()); box[0]=char(box[0]^1); altered.insert("box",QString::fromLatin1(box.toBase64()));
        QVERIFY(!contestsecure::open(key,"FIELD","sender","receiver",altered).ok);
        altered=sealed.frame; altered.insert("tag","!!!!"); QVERIFY(!contestsecure::open(key,"FIELD","sender","receiver",altered).ok);
        QVERIFY(!contestsecure::seal(QByteArray(31,'k'),"FIELD","sender","receiver","qso_chunk_v1",body).ok);
        QVERIFY(!contestsecure::seal(key,"FIELD","sender","other","qso_chunk_v1",body).ok);
        const auto again=contestsecure::seal(key,"FIELD","sender","receiver","qso_chunk_v1",body); QVERIFY(again.ok); QVERIFY(again.frame.value("nonce")!=sealed.frame.value("nonce"));
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
