// Il log su DecoPort: pacchetti firmati (con un vettore calcolato a parte in
// Python, lo stesso che prova HH), registrazione, QSO, conferme, rinvii, ban.
#include "core/DecoPortLog.h"
#include "core/DecoPortMonitor.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QNetworkDatagram>
#include <QSignalSpy>
#include <QTest>
#include <QUdpSocket>

using namespace decolog::core;
namespace dpl = decolog::core::decoportlog;

namespace {

const QString kPassword = QStringLiteral("segreto-prova");

// Chi parla con il log: un client DecoPort qualunque (HH, per esempio).
struct Peer {
    QUdpSocket socket;

    bool open() { return socket.bind(QHostAddress::LocalHost, 0); }
    quint16 port() const { return socket.localPort(); }

    void send(quint8 type, const QByteArray& payload, const QByteArray& key, quint16 to, quint32 seq = 1)
    {
        socket.writeDatagram(dpl::buildPacket(type, 0x1234u, seq, payload, key, QDateTime::currentMSecsSinceEpoch()),
                             QHostAddress::LocalHost, to);
    }

    // Il prossimo pacchetto valido entro `timeoutMs`; nessuno se tace.
    std::optional<dpl::Packet> receive(const QByteArray& key, int timeoutMs = 1500)
    {
        // Il log sta nello stesso filo: si lascia girare il ciclo degli eventi mentre si aspetta.
        QElapsedTimer waited;
        waited.start();
        while (!socket.hasPendingDatagrams() && waited.elapsed() < timeoutMs)
            QTest::qWait(5);
        if (!socket.hasPendingDatagrams())
            return std::nullopt;
        dpl::Packet p;
        const QByteArray d = socket.receiveDatagram().data();
        if (dpl::parsePacket(d, key, QDateTime::currentSecsSinceEpoch(), &p) != dpl::Verdict::Ok)
            return std::nullopt;
        return p;
    }
};

} // namespace

class TestDecoPortLog : public QObject {
    Q_OBJECT

private slots:
    void keyAndPacketMatchTheReferenceVector()
    {
        // Calcolati a parte con hashlib/hmac di Python, secondo DECOPORT_PROTOCOL.md.
        QCOMPARE(dpl::deriveKey(kPassword).toHex(),
                 QByteArrayLiteral("f5f04a69c65369dc9cbad0133e2d03bb017ceb738e37b7e5e4a4f1705d24e419"));
        // Gli spazi ai bordi della password non contano, come in DecoPort.
        QCOMPARE(dpl::deriveKey(QStringLiteral("  segreto-prova \n")), dpl::deriveKey(kPassword));
        QVERIFY(dpl::deriveKey(QStringLiteral("   ")).isEmpty());

        const QByteArray key = dpl::deriveKey(kPassword);
        QByteArray payload;
        payload.append(QByteArray::fromHex("000000000000002a00"));
        payload.append("<CALL:5>IU8LM<EOR>");
        const QByteArray packet = dpl::buildPacket(dpl::LogQso, 0x0A0B0C0Du, 7, payload, key, 1790000000123);
        QCOMPARE(packet.toHex(),
                 QByteArrayLiteral("44505254011100030a0b0c0d000000076ab13b800754d4c0001b0000000000000000002a003c43414c4c3a35"
                                   "3e4955384c4d3c454f523eeebb612f25a3ece969522e348ba25446"));

        dpl::Packet p;
        QCOMPARE(dpl::parsePacket(packet, key, 1790000000, &p), dpl::Verdict::Ok);
        QCOMPARE(p.type, quint8(dpl::LogQso));
        QCOMPARE(p.streamId, 0x0A0B0C0Du);
        QCOMPARE(p.sequence, 7u);
        QCOMPARE(p.payload, payload);
    }

    void parseRefusesWhatItShould()
    {
        const QByteArray key = dpl::deriveKey(kPassword);
        const qint64 ms = 1790000000000;
        const QByteArray good = dpl::buildPacket(dpl::Hello, 1, 1, {}, key, ms);
        QCOMPARE(dpl::parsePacket(good, key, ms / 1000, nullptr), dpl::Verdict::Ok);

        // Un'altra password: firma sbagliata.
        QCOMPARE(dpl::parsePacket(good, dpl::deriveKey(QStringLiteral("un'altra")), ms / 1000, nullptr),
                 dpl::Verdict::BadSignature);
        // Un byte cambiato nel payload: la firma non regge.
        QByteArray tampered = dpl::buildPacket(dpl::LogQso, 1, 1, QByteArrayLiteral("abc"), key, ms);
        tampered[dpl::kHeaderBytes] = 'X';
        QCOMPARE(dpl::parsePacket(tampered, key, ms / 1000, nullptr), dpl::Verdict::BadSignature);
        // Senza firma: DecoPort non ha una modalita' in chiaro.
        QCOMPARE(dpl::parsePacket(dpl::buildPacket(dpl::Hello, 1, 1, {}, {}, ms), key, ms / 1000, nullptr),
                 dpl::Verdict::Unsigned);
        // Registrato e rimandato dopo mezzo minuto: scaduto, anche se la firma e' buona.
        QCOMPARE(dpl::parsePacket(good, key, ms / 1000 + 31, nullptr), dpl::Verdict::Stale);
        QCOMPARE(dpl::parsePacket(good, key, ms / 1000 - 31, nullptr), dpl::Verdict::Stale);
        QCOMPARE(dpl::parsePacket(good, key, ms / 1000 + 30, nullptr), dpl::Verdict::Ok);
        // Troncato o non DecoPort.
        QCOMPARE(dpl::parsePacket(good.left(20), key, ms / 1000, nullptr), dpl::Verdict::Malformed);
        QCOMPARE(dpl::parsePacket(QByteArrayLiteral("hello"), key, ms / 1000, nullptr), dpl::Verdict::Malformed);
        QByteArray notDecoPort = good;
        notDecoPort[0] = 'X';
        QCOMPARE(dpl::parsePacket(notDecoPort, key, ms / 1000, nullptr), dpl::Verdict::Malformed);
        // La lunghezza dichiarata oltre il datagramma non fa leggere fuori dal buffer.
        QByteArray lying = dpl::buildPacket(dpl::LogQso, 1, 1, QByteArrayLiteral("abc"), key, ms);
        lying[24] = char(0x7F);
        QCOMPARE(dpl::parsePacket(lying, key, ms / 1000, nullptr), dpl::Verdict::Malformed);
    }

    void payloadsRoundTrip()
    {
        dpl::Announce a;
        a.program = QStringLiteral("DecoDXLog");
        a.version = QStringLiteral("1.2.3");
        a.station = QStringLiteral("IU8LMC");
        a.sessionPort = 5561;
        const auto back = dpl::decodeAnnounce(dpl::encodeAnnounce(a));
        QVERIFY(back);
        QCOMPARE(back->program, a.program);
        QCOMPARE(back->version, a.version);
        QCOMPARE(back->station, a.station);
        QCOMPARE(back->sessionPort, quint16(5561));
        QVERIFY(!dpl::decodeAnnounce(QByteArrayLiteral("\x00\x00")));

        const QByteArray adif = QStringLiteral("<CALL:5>IU8LM<NAME:5>Pietà<EOR>").toUtf8();
        const auto q = dpl::decodeQso(dpl::encodeQso(123456789012, true, adif));
        QVERIFY(q);
        QCOMPARE(q->id, qint64(123456789012));
        QVERIFY(q->resent);
        QCOMPARE(q->adif, adif);
        QVERIFY(!dpl::decodeQso(QByteArrayLiteral("short")));

        QCOMPARE(dpl::decodeAck(dpl::encodeAck(77)).value_or(-1), qint64(77));
        QVERIFY(!dpl::decodeAck(QByteArrayLiteral("123")));

        QVERIFY(dpl::summarize(dpl::LogQso, dpl::encodeQso(9, false, QByteArrayLiteral("<CALL:6>IU8LMC<EOR>")))
                    .contains(QStringLiteral("IU8LMC")));
        QVERIFY(dpl::summarize(dpl::LogAnnounce, dpl::encodeAnnounce(a)).contains(QStringLiteral("5561")));
    }

    void theTrafficMonitorReadsTheLogPackets()
    {
        dpl::Announce a;
        a.program = QStringLiteral("DecoDXLog");
        a.version = QStringLiteral("1.2.3");
        a.station = QStringLiteral("IU8LMC");
        a.sessionPort = 5561;
        const auto announce = decoport::describe(decoport::buildPacket(dpl::LogAnnounce, 7, 1, dpl::encodeAnnounce(a)));
        QVERIFY(announce.valid);
        QCOMPARE(announce.typeName, QStringLiteral("LOG_ANNOUNCE"));
        QVERIFY(announce.summary.contains(QStringLiteral("DecoDXLog 1.2.3")));
        QVERIFY(announce.summary.contains(QStringLiteral("5561")));
        const auto qso = decoport::describe(
            decoport::buildPacket(dpl::LogQso, 7, 2, dpl::encodeQso(5, false, QByteArrayLiteral("<CALL:6>IU8LMC<EOR>"))));
        QCOMPARE(qso.typeName, QStringLiteral("LOG_QSO"));
        QVERIFY(qso.summary.contains(QStringLiteral("IU8LMC")));
        QCOMPARE(decoport::describe(decoport::buildPacket(dpl::LogAck, 7, 3, dpl::encodeAck(5))).typeName,
                 QStringLiteral("LOG_ACK"));
    }

    void doesNotStartWithoutAPassword()
    {
        DecoPortLogServer server;
        QVERIFY(!server.start(0, QString()));
        QVERIFY(!server.isListening());
        QVERIFY(!server.lastError().isEmpty());
    }

    void helloRegistersAndTheQsoGoesOutAndIsAcked()
    {
        DecoPortLogServer server;
        server.setAnnouncePort(0);
        server.setIdentity(QStringLiteral("9.9.9"), QStringLiteral("IU8LMC"));
        QVERIFY(server.start(0, kPassword));
        const QByteArray key = dpl::deriveKey(kPassword);

        Peer hh;
        QVERIFY(hh.open());
        // Nessuno e' registrato: il QSO non va da nessuna parte.
        QCOMPARE(server.publishQso(1, QStringLiteral("<CALL:3>XX1<EOR>")), 0);

        QSignalSpy clients(&server, &DecoPortLogServer::clientsChanged);
        hh.send(dpl::Hello, {}, key, server.port());
        // La risposta e' l'annuncio: HH sa di essere registrato e con chi parla.
        const auto reply = hh.receive(key);
        QVERIFY(reply);
        QCOMPARE(reply->type, quint8(dpl::LogAnnounce));
        const auto who = dpl::decodeAnnounce(reply->payload);
        QVERIFY(who);
        QCOMPARE(who->program, QStringLiteral("DecoDXLog"));
        QCOMPARE(who->version, QStringLiteral("9.9.9"));
        QCOMPARE(who->station, QStringLiteral("IU8LMC"));
        QCOMPARE(who->sessionPort, server.port());
        QCOMPARE(server.clientCount(), 1);
        QCOMPARE(clients.count(), 1);

        QSignalSpy acked(&server, &DecoPortLogServer::qsoAcked);
        const QString adif = QStringLiteral("<CALL:6>IU8LMC<BAND:3>20m<EOR>");
        QCOMPARE(server.publishQso(42, adif), 1);
        QCOMPARE(server.pendingCount(), 1);
        const auto qso = hh.receive(key);
        QVERIFY(qso);
        QCOMPARE(qso->type, quint8(dpl::LogQso));
        const auto decoded = dpl::decodeQso(qso->payload);
        QVERIFY(decoded);
        QCOMPARE(decoded->id, qint64(42));
        QVERIFY(!decoded->resent);
        QCOMPARE(QString::fromUtf8(decoded->adif), adif);

        hh.send(dpl::LogAck, dpl::encodeAck(42), key, server.port());
        QTRY_COMPARE(acked.count(), 1);
        QCOMPARE(acked.first().first().toLongLong(), qint64(42));
        QCOMPARE(server.pendingCount(), 0);
        QCOMPARE(server.sentCount(), 1);
        QCOMPARE(server.ackedCount(), 1);

        // Una conferma per un QSO che non c'e' non cambia niente.
        hh.send(dpl::LogAck, dpl::encodeAck(999), key, server.port());
        QTest::qWait(60);
        QCOMPARE(server.ackedCount(), 1);

        // Il keepalive tiene vivo il client e anche lui riceve l'annuncio.
        hh.send(dpl::Keepalive, {}, key, server.port(), 2);
        const auto beat = hh.receive(key);
        QVERIFY(beat);
        QCOMPARE(beat->type, quint8(dpl::LogAnnounce));

        hh.send(dpl::Bye, {}, key, server.port(), 3);
        QTRY_COMPARE(server.clientCount(), 0);
    }

    void aQsoWithoutAnAckIsResentThenGivenUp()
    {
        DecoPortLogServer server;
        server.setAnnouncePort(0);
        server.setTimings(1000, 60, 5000);
        QVERIFY(server.start(0, kPassword));
        const QByteArray key = dpl::deriveKey(kPassword);
        Peer hh;
        QVERIFY(hh.open());
        hh.send(dpl::Hello, {}, key, server.port());
        QVERIFY(hh.receive(key));   // l'annuncio
        QSignalSpy gaveUp(&server, &DecoPortLogServer::qsoGaveUp);

        QCOMPARE(server.publishQso(5, QStringLiteral("<CALL:3>XX1<EOR>")), 1);
        int copies = 0;
        bool resentFlag = false;
        while (auto p = hh.receive(key, 600)) {
            if (p->type != dpl::LogQso)
                continue;
            ++copies;
            if (const auto q = dpl::decodeQso(p->payload); q && q->resent)
                resentFlag = true;
        }
        QCOMPARE(copies, 3);   // tre tentativi in tutto
        QVERIFY(resentFlag);
        QTRY_COMPARE(gaveUp.count(), 1);
        QCOMPARE(server.pendingCount(), 0);
    }

    void aWrongPasswordGetsSilenceAndFiveTriesGetBanned()
    {
        DecoPortLogServer server;
        server.setAnnouncePort(0);
        QVERIFY(server.start(0, kPassword));
        const QByteArray good = dpl::deriveKey(kPassword);
        const QByteArray bad = dpl::deriveKey(QStringLiteral("sbagliata"));
        QSignalSpy diagnostic(&server, &DecoPortLogServer::diagnostic);

        Peer intruder;
        QVERIFY(intruder.open());
        for (int i = 0; i < 4; ++i)
            intruder.send(dpl::Hello, {}, bad, server.port(), i);
        QVERIFY(!intruder.receive(good, 200));   // nessuna risposta, mai
        QCOMPARE(server.clientCount(), 0);
        QCOMPARE(diagnostic.count(), 0);

        intruder.send(dpl::Hello, {}, bad, server.port(), 5);   // la quinta
        QTRY_COMPARE(diagnostic.count(), 1);
        QVERIFY(diagnostic.first().first().toString().contains(QStringLiteral("ignored")));
        // Da quel mittente non entra piu' nessuno, nemmeno con la password giusta.
        intruder.send(dpl::Hello, {}, good, server.port(), 6);
        QVERIFY(!intruder.receive(good, 200));
        QCOMPARE(server.clientCount(), 0);

        // Un altro mittente non ne soffre.
        Peer honest;
        QVERIFY(honest.open());
        honest.send(dpl::Hello, {}, good, server.port());
        QVERIFY(honest.receive(good));
        QCOMPARE(server.clientCount(), 1);
    }

    void aReplayedPacketIsNotAccepted()
    {
        DecoPortLogServer server;
        server.setAnnouncePort(0);
        QVERIFY(server.start(0, kPassword));
        const QByteArray key = dpl::deriveKey(kPassword);
        Peer hh;
        QVERIFY(hh.open());
        // Firma giusta, ma di un minuto fa.
        hh.socket.writeDatagram(dpl::buildPacket(dpl::Hello, 1, 1, {}, key, QDateTime::currentMSecsSinceEpoch() - 60000),
                                QHostAddress::LocalHost, server.port());
        QVERIFY(!hh.receive(key, 250));
        QCOMPARE(server.clientCount(), 0);
    }

    void aSilentClientIsDropped()
    {
        DecoPortLogServer server;
        server.setAnnouncePort(0);
        server.setTimings(1000, 1000, 250);
        QVERIFY(server.start(0, kPassword));
        const QByteArray key = dpl::deriveKey(kPassword);
        Peer hh;
        QVERIFY(hh.open());
        hh.send(dpl::Hello, {}, key, server.port());
        QVERIFY(hh.receive(key));
        QCOMPARE(server.clientCount(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(server.clientCount(), 0, 3000);
    }

    void theAnnouncementGoesToTheAnnouncePort()
    {
        Peer listener;   // come chi ascolta la 5560
        QVERIFY(listener.open());
        DecoPortLogServer server;
        server.setIdentity(QStringLiteral("1.0"), QStringLiteral("IU8LMC"));
        server.setAnnouncePort(listener.port());
        server.setTimings(100, 1000, 5000);
        QVERIFY(server.start(0, kPassword));
        const auto p = listener.receive(dpl::deriveKey(kPassword), 2000);
        QVERIFY(p);
        QCOMPARE(p->type, quint8(dpl::LogAnnounce));
        const auto who = dpl::decodeAnnounce(p->payload);
        QVERIFY(who);
        QCOMPARE(who->sessionPort, server.port());
    }

    void aTooLongRecordIsRefused()
    {
        DecoPortLogServer server;
        server.setAnnouncePort(0);
        QVERIFY(server.start(0, kPassword));
        const QByteArray key = dpl::deriveKey(kPassword);
        Peer hh;
        QVERIFY(hh.open());
        hh.send(dpl::Hello, {}, key, server.port());
        QVERIFY(hh.receive(key));
        QSignalSpy diagnostic(&server, &DecoPortLogServer::diagnostic);
        QCOMPARE(server.publishQso(1, QString(dpl::kMaxAdifBytes + 1, QLatin1Char('x'))), 0);
        QCOMPARE(diagnostic.count(), 1);
        QCOMPARE(server.publishQso(2, QString()), 0);
    }
};

QTEST_GUILESS_MAIN(TestDecoPortLog)
#include "tst_decoportlog.moc"
