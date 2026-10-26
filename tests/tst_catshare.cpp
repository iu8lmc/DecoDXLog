// La CAT condivisa: il dialogo di rigctld come in Decodium 4.
#include "core/CatShare.h"
#include "core/RigLink.h"

#include <QSignalSpy>
#include <QTcpSocket>
#include <QTest>

using namespace decolog::core;

class FakeRig : public RigLink {
public:
    qint64 hz{14074000};
    QString m{"PKTUSB"};
    bool ptt{false};
    QStringList morse;
    QStringList calls;
    int stops{0};
    int keySpeed{24};
    bool power{true};
    bool tuner{false};
    double swrValue{1.4};
    double alcValue{0.62};
    double powerWatts{42.0};
    void setPowerOn(bool on) override { power = on; calls << QStringLiteral("power:%1").arg(on ? 1 : 0); }
    void setTunerOn(bool on) override { tuner = on; calls << QStringLiteral("tuner:%1").arg(on ? 1 : 0); }
    bool powerOn() const override { return power; }
    bool powerStateKnown() const override { return true; }
    double swr() const override { return swrValue; }
    bool swrKnown() const override { return true; }
    double alc() const override { return alcValue; }
    bool alcKnown() const override { return true; }
    double rfPowerWatts() const override { return powerWatts; }
    bool rfPowerKnown() const override { return true; }
    bool tunerOn() const override { return tuner; }
    bool tunerKnown() const override { return true; }
    void disconnectFromRig() override {}
    bool connected() const override { return true; }
    qint64 frequencyHz() const override { return hz; }
    QString mode() const override { return m; }
    int speedWpm() const override { return keySpeed; }
    QString status() const override { return {}; }
    void refresh() override {}
    void setFrequency(qint64 h) override { hz = h; }
    void setMode(const QString& mode) override { m = mode; calls << QStringLiteral("mode:%1").arg(mode); }
    void setPtt(bool on) override { ptt = on; calls << QStringLiteral("ptt:%1").arg(on ? 1 : 0); }
    void setSpeedWpm(int wpm) override { keySpeed = wpm; calls << QStringLiteral("speed:%1").arg(wpm); }
    void sendMorse(const QString& text) override { morse << text; calls << QStringLiteral("morse:%1").arg(text); }
    void stopMorse() override { ++stops; calls << QStringLiteral("stop"); }
};

class TestCatShare : public QObject {
    Q_OBJECT

private slots:
    void protocol()
    {
        FakeRig rig;
        CatShare share;
        share.setRigProvider([&rig]() -> RigLink* { return &rig; });
        QCOMPARE(share.handleLine("\\get_powerstat"), QString("1\n"));
        QCOMPARE(share.handleLine("\\chk_vfo"), QString("0\n"));       // non "CHKVFO 0"
        QCOMPARE(share.handleLine("\\get_lock_mode"), QString("0\nRPRT 0\n"));
        QVERIFY(share.handleLine("\\dump_state").endsWith("done\n"));
        QVERIFY(share.handleLine("\\dump_state").startsWith("1\n1\n2\n"));
        QCOMPARE(share.handleLine("f"), QString("14074000\n"));
        QCOMPARE(share.handleLine("+\\get_freq"), QString("14074000\n"));
        QCOMPARE(share.handleLine("m"), QString("PKTUSB\n3000\n"));
        QCOMPARE(share.handleLine("l SWR"), QString("1.400000\n"));
        QCOMPARE(share.handleLine("l ALC"), QString("0.620000\n"));
        QCOMPARE(share.handleLine("l KEYSPD"), QString("24.000000\n"));
        QCOMPARE(share.handleLine("+l RFPOWER_METER_WATTS"),
                 QString("get_level: RFPOWER_METER_WATTS\nLevel Value: 42.000000\nRPRT 0\n"));
        QCOMPARE(share.handleLine("u TUNER"), QString("0\n"));
        QCOMPARE(share.handleLine("+\\get_powerstat"),
                 QString("get_powerstat:\nPower Status: 1\nRPRT 0\n"));
        QCOMPARE(share.handleLine("+u TUNER"),
                 QString("get_func: TUNER\n0\nRPRT 0\n"));
        QCOMPARE(share.handleLine("v"), QString("VFOA\n"));
        QCOMPARE(share.handleLine("V VFOA"), QString("RPRT 0\n"));
        // Di serie si legge soltanto.
        QCOMPARE(share.handleLine("F 7074000"), QString("RPRT -1\n"));
        QCOMPARE(rig.hz, qint64(14074000));
        QVERIFY(share.handleLine("q").isNull());
        QCOMPARE(share.handleLine("\\foo"), QString("RPRT -11\n"));
    }

    void controlAndPtt()
    {
        FakeRig rig;
        CatShare share;
        share.setRigProvider([&rig]() -> RigLink* { return &rig; });
        share.configure(false, 4533, true, false);
        QCOMPARE(share.handleLine("F 7074000"), QString("RPRT 0\n"));
        QCOMPARE(rig.hz, qint64(7074000));
        QCOMPARE(share.handleLine("M USB 2400"), QString("RPRT 0\n"));
        QCOMPARE(rig.m, QString("USB"));
        QCOMPARE(share.handleLine("L KEYSPD 31"), QString("RPRT 0\n"));
        QCOMPARE(rig.keySpeed, 31);
        QCOMPARE(share.handleLine("l KEYSPD"), QString("31.000000\n"));
        QCOMPARE(share.handleLine("L KEYSPD 61"), QString("RPRT -1\n"));
        QCOMPARE(rig.keySpeed, 31);
        // La trasmissione ha un interruttore suo.
        QCOMPARE(share.handleLine("T 1"), QString("RPRT -1\n"));
        QVERIFY(!rig.ptt);
        QCOMPARE(share.handleLine("\\send_morse CQ TEST"), QString("RPRT -1\n"));
        QVERIFY(rig.morse.isEmpty());
        share.configure(false, 4533, true, true);
        QCOMPARE(share.handleLine("T 1"), QString("RPRT 0\n"));
        QVERIFY(rig.ptt);
        QCOMPARE(share.handleLine("t"), QString("1\n"));
        QCOMPARE(share.handleLine("\\send_morse CQ TEST"), QString("RPRT 0\n"));
        QCOMPARE(rig.m, QString("CW"));
        QCOMPARE(rig.morse, QStringList({QStringLiteral("CQ TEST")}));
        QVERIFY(rig.calls.indexOf(QStringLiteral("mode:CW"))
                < rig.calls.indexOf(QStringLiteral("morse:CQ TEST")));
        QCOMPARE(share.handleLine("b ?"), QString("RPRT 0\n"));
        QCOMPARE(rig.morse.last(), QString("?"));
        QCOMPARE(share.handleLine("b"), QString("RPRT -1\n"));
        QCOMPARE(share.handleLine("\\stop_morse"), QString("RPRT 0\n"));
        QVERIFY(!rig.ptt);
        QCOMPARE(rig.stops, 1);
        QCOMPARE(share.handleLine("t"), QString("0\n"));
        // Senza controllo, il PTT non passa neanche se chiesto.
        share.configure(false, 4533, false, true);
        QVERIFY(!share.allowPtt());

        share.configure(false, 4533, true, true);
        QCOMPARE(share.handleLine("\\set_powerstat 0"), QString("RPRT 0\n"));
        QVERIFY(!rig.power);
        QCOMPARE(share.handleLine("U TUNER 1"), QString("RPRT 0\n"));
        QVERIFY(rig.tuner);
        share.configure(false, 4533, true, false);
        QCOMPARE(share.handleLine("U TUNER 1"), QString("RPRT -1\n"));
    }

    void overTcp()
    {
        FakeRig rig;
        CatShare share;
        share.setRigProvider([&rig]() -> RigLink* { return &rig; });
        QTcpSocket probe;
        // Una porta libera alta.
        int port = 45330;
        QVERIFY(share.configure(true, port, false, false));
        QVERIFY(share.listening());
        QTcpSocket c;
        c.connectToHost("127.0.0.1", static_cast<quint16>(port));
        QVERIFY(c.waitForConnected(3000));
        c.write("\\chk_vfo\nf\n");
        QByteArray got;
        QTRY_VERIFY_WITH_TIMEOUT((got += c.readAll()).count('\n') >= 2, 3000);
        QCOMPARE(got, QByteArray("0\n14074000\n"));
        QTRY_COMPARE(share.clientCount(), 1);
        // Una seconda condivisione sulla stessa porta non parte, e dice perche'.
        CatShare second;
        QVERIFY(!second.configure(true, port, false, false));
        QVERIFY(!second.lastError().isEmpty());
        share.configure(false, port, false, false);
        QVERIFY(!share.listening());
    }

    void cwForwardingOverTcp()
    {
        FakeRig rig;
        CatShare share;
        share.setRigProvider([&rig]() -> RigLink* { return &rig; });
        const int port = 45331;
        QVERIFY(share.configure(true, port, true, true));

        QTcpSocket client;
        client.connectToHost("127.0.0.1", static_cast<quint16>(port));
        QVERIFY(client.waitForConnected(3000));
        client.write("\\send_morse CQ DE TEST\n\\stop_morse\n");
        QByteArray replies;
        QTRY_VERIFY_WITH_TIMEOUT((replies += client.readAll()).count('\n') >= 2, 3000);
        QCOMPARE(replies, QByteArray("RPRT 0\nRPRT 0\n"));
        QCOMPARE(rig.m, QString("CW"));
        QCOMPARE(rig.morse, QStringList({QStringLiteral("CQ DE TEST")}));
        QCOMPARE(rig.stops, 1);
        QVERIFY(!rig.ptt);
    }

    void cwSpeedForwardingOverTcp()
    {
        FakeRig rig;
        CatShare share;
        share.setRigProvider([&rig]() -> RigLink* { return &rig; });
        const int port = 45332;
        QVERIFY(share.configure(true, port, true, true));

        QTcpSocket client;
        client.connectToHost("127.0.0.1", static_cast<quint16>(port));
        QVERIFY(client.waitForConnected(3000));
        // This is the same short form used by Hamlib's rig_set_level() before
        // Decodium sends native Morse to a shared rigctld endpoint.
        client.write("\\dump_state\nL KEYSPD 18\nb TEST\n");
        QByteArray replies;
        QTRY_VERIFY_WITH_TIMEOUT((replies += client.readAll()).contains("RPRT 0\nRPRT 0\n"), 3000);
        QVERIFY(replies.contains("0x8130004000\n0x4000\n"));
        QCOMPARE(rig.keySpeed, 18);
        QCOMPARE(rig.morse, QStringList({QStringLiteral("TEST")}));
    }
};

QTEST_GUILESS_MAIN(TestCatShare)
#include "tst_catshare.moc"
