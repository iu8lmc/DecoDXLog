#include "core/PredictPassPlanner.h"
#include <QDateTime>
#include <QSignalSpy>
#include <QTest>
#include <QUdpSocket>
using namespace decolog::core;
class TestPredictPass : public QObject {
    Q_OBJECT
  private slots:
    void actualPredictWhenConfigured()
    {
        const QByteArray portText = qgetenv("DECOLOG_TEST_PREDICT_PORT");
        if (portText.isEmpty())
            QSKIP("Set DECOLOG_TEST_PREDICT_PORT for an isolated PREDICT server");
        bool ok = false;
        const int port = portText.toInt(&ok);
        QVERIFY(ok && port > 0 && port <= 65535);
        PredictPassPlanner planner;
        QSignalSpy done(&planner, &PredictPassPlanner::finished);
        QVERIFY(planner.request("127.0.0.1", port, "OSCAR-7", QDateTime::currentSecsSinceEpoch()));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 12000);
        QVERIFY2(done.first().at(0).toBool(), qPrintable(done.first().at(1).toString()));
        QVERIFY(planner.pass().value("los").toLongLong() > planner.pass().value("aos").toLongLong());
        QCOMPARE(planner.pass().value("tleEpoch").toDateTime().date().year(), 2018);
    }
    void obtainsBoundedPassWithoutRadioCommands()
    {
        QUdpSocket server;
        QVERIFY(server.bind(QHostAddress::LocalHost, 0));
        QStringList commands;
        connect(&server, &QUdpSocket::readyRead, &server, [&] {
            while (server.hasPendingDatagrams()) {
                QByteArray cmd(int(server.pendingDatagramSize()), Qt::Uninitialized);
                QHostAddress from;
                quint16 port;
                server.readDatagram(cmd.data(), cmd.size(), &from, &port);
                commands << QString::fromLatin1(cmd).trimmed();
                if (cmd.startsWith("GET_TLE")) {
                    server.writeDatagram(
                        "OSCAR-7\n1 07530U 74089B   18126.90857181 -.00000043  00000-0  11708-4 0  9999\n2 07530 "
                        "101.6842  94.8539 0012518  56.0919 329.7963 12.53632314989425\n",
                        from, port);
                } else if (cmd.startsWith("PREDICT")) {
                    server.writeDatagram("1800000000 Fri 15Jan27 08:00:00 0 183 240 22 75 2135 38914 *\n", from, port);
                    server.writeDatagram("1800000060 Fri 15Jan27 08:01:00 30 174 243 25 72", from, port);
                    server.writeDatagram(" 1794 38914 *\n", from, port);
                    server.writeDatagram("1800000120 Fri 15Jan27 08:02:00 0 160 246 28 69 1517 38914 *\n", from, port);
                    server.writeDatagram("\x1a\n", from, port);
                }
            }
        });
        PredictPassPlanner planner;
        QSignalSpy finished(&planner, &PredictPassPlanner::finished);
        QVERIFY(planner.request("127.0.0.1", server.localPort(), "OSCAR-7", 1800000000));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 6000);
        QVERIFY2(finished.first().at(0).toBool(), qPrintable(finished.first().at(1).toString()));
        QCOMPARE(planner.pass().value("aos").toLongLong(), qint64(1800000000));
        QCOMPARE(planner.pass().value("los").toLongLong(), qint64(1800000120));
        QCOMPARE(planner.pass().value("maxElevation").toInt(), 30);
        QCOMPARE(commands, QStringList({"GET_TLE OSCAR-7", "PREDICT \"OSCAR-7\" 1800000000", "GET_TLE OSCAR-7"}));
    }
    void rejectsUnsafeOrIncompleteDataAndCancels()
    {
        QUdpSocket server;
        QVERIFY(server.bind(QHostAddress::LocalHost, 0));
        int scenario = 0;
        int tleCount = 0;
        const QByteArray tle = "OSCAR-7\n1 07530U 74089B   18126.90857181 -.00000043  00000-0  11708-4 0  9999\n2 "
                               "07530 101.6842  94.8539 0012518  56.0919 329.7963 12.53632314989425\n";
        connect(&server, &QUdpSocket::readyRead, &server, [&] {
            while (server.hasPendingDatagrams()) {
                QByteArray cmd(int(server.pendingDatagramSize()), Qt::Uninitialized);
                QHostAddress from;
                quint16 port;
                server.readDatagram(cmd.data(), cmd.size(), &from, &port);
                if (scenario == 3)
                    continue;
                if (cmd.startsWith("GET_TLE")) {
                    ++tleCount;
                    server.writeDatagram(scenario == 2 && tleCount == 2 ? tle + "changed\n" : tle, from, port);
                } else {
                    server.writeDatagram("1800000000 Fri 15Jan27 08:00:00 0 183 240 22 75 2135 38914 *\n", from, port);
                    if (scenario == 0)
                        server.writeDatagram("1800000060 Fri 15Jan27 08:01:00 30 174 243 25 72 1794 38914 *\n", from,
                                             port);
                    else if (scenario == 1)
                        server.writeDatagram("1799999999 Fri 15Jan27 07:59:59 0 174 243 25 72 1794 38914 *\n", from,
                                             port);
                    else
                        server.writeDatagram("1800000060 Fri 15Jan27 08:01:00 0 174 243 25 72 1794 38914 *\n", from,
                                             port);
                    server.writeDatagram("\x1a\n", from, port);
                }
            }
        });
        PredictPassPlanner planner;
        QSignalSpy done(&planner, &PredictPassPlanner::finished);
        QVERIFY(!planner.request("127.0.0.1", server.localPort(), "OSCAR-7; touch /tmp/unsafe", 1800000000));
        for (scenario = 0; scenario < 3; ++scenario) {
            tleCount = 0;
            QVERIFY(planner.request("127.0.0.1", server.localPort(), "OSCAR-7", 1800000000));
            QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 6000);
            QVERIFY(!done.takeFirst().at(0).toBool());
            QVERIFY(planner.pass().isEmpty());
        }
        scenario = 3;
        QVERIFY(planner.request("127.0.0.1", server.localPort(), "OSCAR-7", 1800000000));
        planner.cancel();
        QCOMPARE(done.size(), 1);
        QVERIFY(!done.takeFirst().at(0).toBool());
        QVERIFY(!planner.busy());
    }
};
QTEST_GUILESS_MAIN(TestPredictPass)
#include "tst_predictpass.moc"
