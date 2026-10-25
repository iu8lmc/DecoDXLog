// Real rigctld, with a loopback-only FTDX-3000 CAT simulator. No serial port,
// radio or RF is involved. This covers what a plain fake rigctld cannot:
// daemon connection lifetime and the actual CAT commands Hamlib generates.
#include "core/RigControl.h"

#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

using decolog::core::RigControl;

namespace {
class YaesuSimulator : public QTcpServer {
public:
    QStringList commands;
    YaesuSimulator()
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (auto* socket = nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this,
                        [this, socket, buffer = QByteArray()]() mutable {
                    buffer += socket->readAll();
                    while (buffer.contains(';')) {
                        const int end = buffer.indexOf(';');
                        const QByteArray cmd = buffer.left(end);
                        if (qEnvironmentVariableIsSet("DECODXLOG_RIG_DEBUG"))
                            qDebug() << "simulated CAT" << cmd;
                        buffer.remove(0, end + 1);
                        commands << QString::fromLatin1(cmd);
                        const QHash<QByteArray, QByteArray> replies = {
                            {"PS", "PS1;"}, {"AI", "AI0;"}, {"ID", "ID0462;"},
                            {"FA", "FA00018100000;"}, {"FB", "FB00018100000;"},
                            {"MD0", "MD03;"}, {"MD1", "MD13;"}, {"FR", "FR0;"},
                            {"FT", "FT0;"}, {"VS", "VS0;"}, {"TX", "TX0;"},
                            {"KS", "KS025;"}, {"ST", "ST0;"}, {"SH0", "SH008;"},
                            {"NA0", "NA00;"},
                            // 27-byte NewCAT status: memory, 8-digit frequency,
                            // clarifier, RX/TX clarifier flags, CW, VFO, tone.
                            {"IF", "IF" "001" "18100000" "+0000" "0" "0" "3" "0" "0" "00" "0" ";"}
                        };
                        if (replies.contains(cmd))
                            socket->write(replies.value(cmd));
                        else if (cmd.size() <= 3 && !cmd.startsWith("TX")
                                 && !cmd.startsWith("KY") && !cmd.startsWith("AI"))
                            socket->write("?;");
                    }
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
};

class Daemon : public QProcess {
public:
    ~Daemon() override
    {
        if (state() != NotRunning) {
            terminate();
            if (!waitForFinished(2000)) {
                kill();
                waitForFinished(2000);
            }
        }
    }
};
} // namespace

class TestRigHamlib : public QObject {
    Q_OBJECT
private slots:
    void repeatedStopNeverPlaysYaesuMemoryAgain()
    {
        const QString executable = QStandardPaths::findExecutable(QStringLiteral("rigctld"));
        if (executable.isEmpty())
            QSKIP("Optional integration test: rigctld is not installed");
        YaesuSimulator radio;
        QVERIFY(radio.listen(QHostAddress::LocalHost, 0));
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
        const quint16 port = probe.serverPort();
        probe.close();
        Daemon daemon;
        daemon.setProcessChannelMode(QProcess::MergedChannels);
        daemon.start(executable, {QStringLiteral("-m"), QStringLiteral("1037"),
                     QStringLiteral("-r"), QStringLiteral("127.0.0.1:%1").arg(radio.serverPort()),
                     QStringLiteral("-T"), QStringLiteral("127.0.0.1"),
                     QStringLiteral("-t"), QString::number(port),
                     QStringLiteral("-P"), QStringLiteral("RIG"),
                     QStringLiteral("-C"), QStringLiteral("timeout=200,retry=0")});
        QVERIFY(daemon.waitForStarted(2000));
        RigControl control;
        control.setYaesuKeyerMemoryMode(true);
        // CAT PTT already generates TX0; no redundant raw-CAT command.
        control.connectTo(QStringLiteral("127.0.0.1"), port);
        QTRY_COMPARE_WITH_TIMEOUT(control.mode(), QStringLiteral("CW"), 15000);
        QSignalSpy sent(&control, &RigControl::morseSent);
        control.sendMorse(QStringLiteral("CQ TEST"));
        QTRY_COMPARE_WITH_TIMEOUT(sent.count(), 1, 5000);
        // Newer Hamlib acknowledges enqueueing before its CW worker sends KY.
        QTRY_COMPARE_WITH_TIMEOUT(radio.commands.count(QStringLiteral("KY6")), 1, 5000);
        control.sendMorse(QStringLiteral("UP UP")); // must be cancelled
        for (int i = 0; i < 20; ++i)
            control.emergencyStop();
        QTRY_VERIFY(radio.commands.contains(QStringLiteral("TX0")));
        QTest::qWait(700);
        for (int i = 0; i < 4; ++i) {
            control.emergencyStop();
            QTest::qWait(200);
        }
        QCOMPARE(daemon.state(), QProcess::Running);
        QCOMPARE(radio.commands.filter(QStringLiteral("KM1")),
                 QStringList{QStringLiteral("KM1CQ TEST")});
        QCOMPARE(radio.commands.count(QStringLiteral("KY6")), 1);
        // Under load, further clicks may legitimately share a stop that has
        // not yet been acknowledged. Do not require one CAT write per click.
        QVERIFY(!radio.commands.contains(QStringLiteral("TX1")));
        QCOMPARE(sent.count(), 1);
        control.sendMorse(QStringLiteral("TU"));
        QTRY_COMPARE_WITH_TIMEOUT(sent.count(), 2, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(radio.commands.count(QStringLiteral("KY6")), 2, 5000);
        QVERIFY(radio.commands.contains(QStringLiteral("KM1TU")));
        control.emergencyStop();
        QTest::qWait(200);
        control.disconnectFromRig();
        // The same safety contract after losing the normal socket.
        const int offBefore = radio.commands.count(QStringLiteral("TX0"));
        for (int i = 0; i < 20; ++i)
            control.emergencyStop();
        QTRY_VERIFY(radio.commands.count(QStringLiteral("TX0")) > offBefore);
        QTest::qWait(500);
        QCOMPARE(daemon.state(), QProcess::Running);
        QCOMPARE(radio.commands.count(QStringLiteral("KY6")), 2);
    }
};

QTEST_GUILESS_MAIN(TestRigHamlib)
#include "tst_rig_hamlib.moc"
