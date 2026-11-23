// La rete multi-operatore: messaggi impacchettati e ricevuti.
#include "core/ContestNet.h"
#include "core/ContestSerialProtocol.h"
#include "core/LogDatabase.h"

#include <QSignalSpy>
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
