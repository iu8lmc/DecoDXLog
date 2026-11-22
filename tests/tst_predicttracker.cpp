#include "core/PredictTracker.h"
#include <QSignalSpy>
#include <QTest>
#include <QUdpSocket>
using namespace decolog::core;
class TestPredictTracker : public QObject {
    Q_OBJECT
  private slots:
    void actualPredictServerWhenConfigured()
    {
        const QByteArray value = qgetenv("DECOLOG_TEST_PREDICT_PORT");
        if (value.isEmpty())
            QSKIP("Set DECOLOG_TEST_PREDICT_PORT for a running isolated PREDICT server");
        bool ok = false;
        const uint port = value.toUInt(&ok);
        QVERIFY(ok && port > 0 && port < 65536);
        PredictTracker tracker;
        QSignalSpy done(&tracker, &PredictTracker::finished);
        QVERIFY(tracker.request("127.0.0.1", quint16(port), "OSCAR-7"));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 6000);
        QVERIFY2(done.first().at(0).toBool(), qPrintable(done.first().at(1).toString()));
        QCOMPARE(tracker.snapshot().value("satellite").toString(), QString("OSCAR-7"));
        QCOMPARE(tracker.snapshot().value("tleEpoch").toDateTime().date().year(), 2018);
    }
    void trackerReadsLiveSnapshotWithoutControlCommands()
    {
        QUdpSocket server;
        QVERIFY(server.bind(QHostAddress::LocalHost, 0));
        QStringList commands;
        connect(&server, &QUdpSocket::readyRead, &server, [&] {
            while (server.hasPendingDatagrams()) {
                QByteArray request;
                request.resize(int(server.pendingDatagramSize()));
                QHostAddress from;
                quint16 port;
                server.readDatagram(request.data(), request.size(), &from, &port);
                commands << QString::fromLatin1(request).trimmed();
                QByteArray reply;
                if (request.startsWith("GET_MODE"))
                    reply = "MULTI\n";
                else if (request.startsWith("GET_SAT "))
                    reply = "FO-29\n151.79\n-70.04\n203.75\n-60.01\n1800000000\n5988.49\n11881.33\n773.71\n7."
                            "47\n6292\nD\n279.37\n-32.42\n360.00\n";
                else if (request.startsWith("GET_DOPPLER "))
                    reply = "961.742249\n";
                else if (request.startsWith("GET_TLE "))
                    reply = "FO-29\n1 24278U 96046B   26326.50000000  .00000000  00000-0  00000-0 0  9991\n2 24278  "
                            "98.5000  80.0000 0350000  40.0000 320.0000 13.50000000100000\n";
                server.writeDatagram(reply, from, port);
            }
        });
        PredictTracker tracker;
        QSignalSpy done(&tracker, &PredictTracker::finished);
        QVERIFY(tracker.request("127.0.0.1", server.localPort(), "FO-29"));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 3000);
        QVERIFY2(done.first().at(0).toBool(), qPrintable(done.first().at(1).toString()));
        const auto snapshot = tracker.snapshot();
        QCOMPARE(snapshot.value("satellite").toString(), QString("FO-29"));
        QCOMPARE(snapshot.value("azimuth").toDouble(), 203.75);
        QCOMPARE(snapshot.value("elevation").toDouble(), -60.01);
        QCOMPARE(snapshot.value("doppler100MHz").toDouble(), 961.742249);
        QVERIFY(snapshot.value("tleEpoch").toDateTime().isValid());
        QCOMPARE(commands,
                 QStringList({"GET_MODE", "GET_SAT FO-29", "GET_DOPPLER FO-29", "GET_TLE FO-29", "GET_MODE"}));
    }
    void rejectsInvalidNamesAndTrackingModeChange()
    {
        QUdpSocket server;
        QVERIFY(server.bind(QHostAddress::LocalHost, 0));
        int modes = 0;
        connect(&server, &QUdpSocket::readyRead, &server, [&] {
            while (server.hasPendingDatagrams()) {
                QByteArray request(int(server.pendingDatagramSize()), Qt::Uninitialized);
                QHostAddress from;
                quint16 port;
                server.readDatagram(request.data(), request.size(), &from, &port);
                QByteArray reply;
                if (request.startsWith("GET_MODE"))
                    reply = ++modes == 1 ? "MULTI\n" : "NONE\n";
                else if (request.startsWith("GET_SAT "))
                    reply = "FO-29\n1\n2\n3\n4\n1800000000\n6\n7\n8\n9\n10\nD\n";
                else if (request.startsWith("GET_DOPPLER "))
                    reply = "10\n";
                else if (request.startsWith("GET_TLE "))
                    reply = "FO-29\n1 24278U 96046B   26326.50000000  .00000000  00000-0  00000-0 0  9991\n2 24278  "
                            "98.5000\n";
                server.writeDatagram(reply, from, port);
            }
        });
        PredictTracker tracker;
        QSignalSpy done(&tracker, &PredictTracker::finished);
        QVERIFY(!tracker.request("127.0.0.1", server.localPort(), "FO-29\nGET_MODE"));
        QVERIFY(tracker.request("127.0.0.1", server.localPort(), "FO-29"));
        QVERIFY(!tracker.request("127.0.0.1", server.localPort(), "FO-29"));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 3000);
        QVERIFY(!done.first().at(0).toBool());
        QVERIFY(tracker.snapshot().isEmpty());
    }
    void rejectsStoppedAndUnreachableServers()
    {
        QUdpSocket server;
        QVERIFY(server.bind(QHostAddress::LocalHost, 0));
        connect(&server, &QUdpSocket::readyRead, &server, [&] {
            QByteArray request;
            request.resize(int(server.pendingDatagramSize()));
            QHostAddress from;
            quint16 port;
            server.readDatagram(request.data(), request.size(), &from, &port);
            server.writeDatagram("NONE\n", from, port);
        });
        PredictTracker tracker;
        QSignalSpy done(&tracker, &PredictTracker::finished);
        QVERIFY(tracker.request("127.0.0.1", server.localPort(), "FO-29"));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 3000);
        QVERIFY(!done.first().at(0).toBool());
        QVERIFY(tracker.snapshot().isEmpty());
        const quint16 port = server.localPort();
        server.close();
        done.clear();
        QVERIFY(tracker.request("127.0.0.1", port, "FO-29"));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 3000);
        QVERIFY(!done.first().at(0).toBool());
        QVERIFY(tracker.snapshot().isEmpty());
    }
};
QTEST_GUILESS_MAIN(TestPredictTracker)
#include "tst_predicttracker.moc"
