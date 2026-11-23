#include "core/PredictTracker.h"
#include "core/Tle.h"
#include <QSignalSpy>
#include <QTest>
#include <QUdpSocket>
#include <limits>
using namespace decolog::core;
static QByteArray checksummed(QByteArray line)
{
    Q_ASSERT(line.size()==69);
    int sum=0; for (int i=0;i<68;++i) { const char c=line[i]; if (c>='0' && c<='9') sum+=c-'0'; else if (c=='-') ++sum; }
    line[68]=char('0'+sum%10); return line;
}
class TestPredictTracker : public QObject {
    Q_OBJECT
  private slots:
    void pairsManualLinearAndIndependentTransponders()
    {
        PredictTracker tracker;
        const auto independent=tracker.nominalTransponderPair(145.95,435.85,0,0,"independent");
        QVERIFY(independent.value("ok").toBool()); QCOMPARE(independent.value("txMHz").toDouble(),145.95); QCOMPARE(independent.value("rxMHz").toDouble(),435.85);
        const auto inverted=tracker.nominalTransponderPair(145.95,435.85,10,50,"inverting");
        QVERIFY(inverted.value("ok").toBool()); QVERIFY(qAbs(inverted.value("txMHz").toDouble()-145.96)<1e-9); QVERIFY(qAbs(inverted.value("rxMHz").toDouble()-435.84)<1e-9);
        const auto straight=tracker.nominalTransponderPair(145.95,435.85,-10,50,"noninverting");
        QVERIFY(straight.value("ok").toBool()); QVERIFY(qAbs(straight.value("txMHz").toDouble()-145.94)<1e-9); QVERIFY(qAbs(straight.value("rxMHz").toDouble()-435.84)<1e-9);
        QVERIFY(!tracker.nominalTransponderPair(145.95,435.85,51,50,"inverting").value("ok").toBool());
        QVERIFY(!tracker.nominalTransponderPair(145.95,435.85,1,0,"independent").value("ok").toBool());
        QVERIFY(!tracker.nominalTransponderPair(145.95,435.85,0,50,"unknown").value("ok").toBool());
        QVERIFY(!tracker.nominalTransponderPair(std::numeric_limits<double>::quiet_NaN(),435.85,0,0,"independent").value("ok").toBool());
        QVERIFY(!tracker.nominalTransponderPair(145.95,435.85,-51,50,"noninverting").value("ok").toBool());
        QVERIFY(!tracker.nominalTransponderPair(0.001,435.85,-10,50,"inverting").value("ok").toBool());
    }
    void tleIntegrityChecksRejectCorruptionAndMismatchedSatellites()
    {
        const QByteArray one="1 23455U 94089A   97320.90946019  .00000140  00000-0  10191-3 0  2621";
        const QByteArray two="2 23455  99.0090 272.6745 0008546 223.1686 136.8816 14.11711747148495";
        const auto valid=tle::inspect(one,two); QVERIFY2(valid.ok,qPrintable(valid.error));
        QCOMPARE(valid.catalogue,QString("23455")); QCOMPARE(valid.epoch.date(),QDate(1997,11,16));
        auto corrupt=one; corrupt[30]=corrupt[30]=='1' ? '2' : '1'; QVERIFY(!tle::inspect(corrupt,two).ok);
        auto wrong=two; wrong[6]='6'; int sum=0; for (int i=0;i<68;++i) { const char c=wrong[i]; if (c>='0' && c<='9') sum+=c-'0'; else if (c=='-') ++sum; } wrong[68]=char('0'+sum%10);
        QVERIFY(!tle::inspect(one,wrong).ok); QVERIFY(!tle::inspect(one,two.left(32)).ok);
    }
    void listsSatellitesAndRejectsStoppedTracking()
    {
        PredictTracker tracker;
        QVERIFY(tracker.metaObject()->indexOfMethod("requestSatellites(QString,int)") >= 0);
        QVERIFY(tracker.metaObject()->indexOfProperty("satellites") >= 0);
        QUdpSocket server;
        QVERIFY(server.bind(QHostAddress::LocalHost, 0));
        QByteArray mode = "MULTI\n";
        QByteArray list = "OSCAR-7\nFO-29 (JAS-2)\n\n";
        bool changeMode = false;
        QStringList commands;
        connect(&server, &QUdpSocket::readyRead, &server, [&] {
            while (server.hasPendingDatagrams()) {
                QByteArray request(int(server.pendingDatagramSize()), Qt::Uninitialized);
                QHostAddress from;
                quint16 port;
                server.readDatagram(request.data(), request.size(), &from, &port);
                commands << QString::fromLatin1(request).trimmed();
                server.writeDatagram(request.startsWith("GET_LIST") ? list : mode, from, port);
                if (changeMode && request.startsWith("GET_LIST"))
                    mode = "NONE\n";
            }
        });
        QSignalSpy done(&tracker, &PredictTracker::satellitesFinished);
        QVERIFY(tracker.requestSatellites("127.0.0.1", server.localPort()));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 3000);
        QVERIFY(done.takeFirst().at(0).toBool());
        QCOMPARE(tracker.satellites(), QStringList({"OSCAR-7", "FO-29 (JAS-2)"}));
        QCOMPARE(commands, QStringList({"GET_MODE", "GET_LIST", "GET_MODE"}));
        mode = "FO-29 (JAS-2)\n";
        QVERIFY(tracker.requestSatellites("127.0.0.1", server.localPort()));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 3000);
        QVERIFY(done.takeFirst().at(0).toBool());
        QCOMPARE(tracker.satellites(), QStringList({"FO-29 (JAS-2)"}));
        mode = "MULTI\n";
        changeMode = true;
        QVERIFY(tracker.requestSatellites("127.0.0.1", server.localPort()));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 3000);
        QVERIFY(!done.takeFirst().at(0).toBool());
        QVERIFY(tracker.satellites().isEmpty());
        changeMode = false;
        mode = "NONE\n";
        QVERIFY(tracker.requestSatellites("127.0.0.1", server.localPort()));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 3000);
        QVERIFY(!done.takeFirst().at(0).toBool());
        QVERIFY(tracker.satellites().isEmpty());
        mode = "MULTI\n";
        list = "Huh?\n";
        QVERIFY(tracker.requestSatellites("127.0.0.1", server.localPort()));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 3000);
        QVERIFY(!done.takeFirst().at(0).toBool());
        QVERIFY(tracker.satellites().isEmpty());
        list = "\n\n";
        QVERIFY(tracker.requestSatellites("127.0.0.1", server.localPort()));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 3000);
        QVERIFY(!done.takeFirst().at(0).toBool());
        QVERIFY(tracker.satellites().isEmpty());
        const quint16 port = server.localPort();
        server.close();
        QVERIFY(tracker.requestSatellites("127.0.0.1", port));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 3000);
        QVERIFY(!done.takeFirst().at(0).toBool());
        QVERIFY(tracker.satellites().isEmpty());
    }
    void rejectsTleChangeDuringSnapshot()
    {
        QUdpSocket server; QVERIFY(server.bind(QHostAddress::LocalHost,0)); int tleCount=0; QStringList commands;
        connect(&server,&QUdpSocket::readyRead,&server,[&] {
            while (server.hasPendingDatagrams()) {
                QByteArray request(int(server.pendingDatagramSize()),Qt::Uninitialized); QHostAddress host; quint16 port;
                server.readDatagram(request.data(),request.size(),&host,&port); commands << QString::fromLatin1(request).trimmed(); QByteArray response;
                if (request.startsWith("GET_MODE")) response="MULTI\n";
                else if (request.startsWith("GET_SAT ")) response="OSCAR-7\n1\n2\n3\n4\n1800000000\n6\n7\n8\n9\n10\nD\n";
                else if (request.startsWith("GET_DOPPLER ")) response="10\n";
                else {
                    ++tleCount;
                    auto line=QByteArray("1 07530U 74089B   18126.90857181 -.00000043  00000-0  11708-4 0  9999");
                    if (tleCount>1) line[30]='2';
                    response=QByteArray("OSCAR-7\n")+checksummed(line)+"\n"+checksummed("2 07530 101.6842  94.8539 0012518  56.0919 329.7963 12.53632314989425")+"\n";
                }
                server.writeDatagram(response,host,port);
            }
        });
        PredictTracker tracker; QSignalSpy done(&tracker,&PredictTracker::finished);
        QVERIFY(tracker.request("127.0.0.1",server.localPort(),"OSCAR-7")); QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,3000);
        QVERIFY(!done.first().at(0).toBool()); QVERIFY(tracker.snapshot().isEmpty()); QCOMPARE(tleCount,2);
        QCOMPARE(commands,QStringList({"GET_MODE","GET_TLE OSCAR-7","GET_SAT OSCAR-7","GET_DOPPLER OSCAR-7","GET_TLE OSCAR-7"}));
    }
    void snapshotAgeStartsAtPositionReceipt()
    {
        QUdpSocket server; QVERIFY(server.bind(QHostAddress::LocalHost,0));
        connect(&server,&QUdpSocket::readyRead,&server,[&] {
            while (server.hasPendingDatagrams()) {
                QByteArray request(int(server.pendingDatagramSize()),Qt::Uninitialized); QHostAddress host; quint16 port;
                server.readDatagram(request.data(),request.size(),&host,&port);
                if (request.startsWith("GET_DOPPLER ")) {
                    QTimer::singleShot(350,&server,[&server,host,port] { server.writeDatagram("10\n",host,port); });
                    continue;
                }
                QByteArray response;
                if (request.startsWith("GET_MODE")) response="MULTI\n";
                else if (request.startsWith("GET_SAT ")) response="OSCAR-7\n1\n2\n3\n4\n1800000000\n6\n7\n8\n9\n10\nD\n";
                else response="OSCAR-7\n1 07530U 74089B   18126.90857181 -.00000043  00000-0  11708-4 0  9999\n2 07530 101.6842  94.8539 0012518  56.0919 329.7963 12.53632314989425\n";
                server.writeDatagram(response,host,port);
            }
        });
        PredictTracker tracker; QSignalSpy done(&tracker,&PredictTracker::finished);
        QVERIFY(tracker.request("127.0.0.1",server.localPort(),"OSCAR-7")); QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,3000);
        QVERIFY2(done.first().at(0).toBool(),qPrintable(done.first().at(1).toString()));
        const auto received=tracker.snapshot().value("receivedAt").toDateTime(); QVERIFY(received.isValid());
        QVERIFY(received.msecsTo(QDateTime::currentDateTimeUtc())>=300);
    }
    void rejectsChangeBetweenTwoOtherwiseSupportedTrackingModes()
    {
        QUdpSocket server; QVERIFY(server.bind(QHostAddress::LocalHost,0)); int modes=0;
        connect(&server,&QUdpSocket::readyRead,&server,[&] {
            while (server.hasPendingDatagrams()) {
                QByteArray request(int(server.pendingDatagramSize()),Qt::Uninitialized); QHostAddress host; quint16 port;
                server.readDatagram(request.data(),request.size(),&host,&port); QByteArray response;
                if (request.startsWith("GET_MODE")) response=++modes==1 ? "MULTI\n" : "OSCAR-7\n";
                else if (request.startsWith("GET_SAT ")) response="OSCAR-7\n1\n2\n3\n4\n1800000000\n6\n7\n8\n9\n10\nD\n";
                else if (request.startsWith("GET_DOPPLER ")) response="10\n";
                else response="OSCAR-7\n1 07530U 74089B   18126.90857181 -.00000043  00000-0  11708-4 0  9999\n2 07530 101.6842  94.8539 0012518  56.0919 329.7963 12.53632314989425\n";
                server.writeDatagram(response,host,port);
            }
        });
        PredictTracker tracker; QSignalSpy done(&tracker,&PredictTracker::finished);
        QVERIFY(tracker.request("127.0.0.1",server.localPort(),"OSCAR-7")); QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,3000);
        QVERIFY(!done.first().at(0).toBool()); QVERIFY(tracker.snapshot().isEmpty()); QCOMPARE(modes,2);
    }
    void rejectsCorruptTleWithoutPublishingSnapshot()
    {
        QUdpSocket server; QVERIFY(server.bind(QHostAddress::LocalHost,0));
        connect(&server,&QUdpSocket::readyRead,&server,[&] {
            while (server.hasPendingDatagrams()) {
                QByteArray request(int(server.pendingDatagramSize()),Qt::Uninitialized); QHostAddress host; quint16 port;
                server.readDatagram(request.data(),request.size(),&host,&port); QByteArray response;
                if (request.startsWith("GET_MODE")) response="MULTI\n";
                else if (request.startsWith("GET_SAT ")) response="OSCAR-7\n1\n2\n3\n4\n1800000000\n6\n7\n8\n9\n10\nD\n";
                else if (request.startsWith("GET_DOPPLER ")) response="10\n";
                else response="OSCAR-7\n1 07530U 74089B   18126.90857181 -.00000043  00000-0  11708-4 0  9998\n2 07530 101.6842  94.8539 0012518  56.0919 329.7963 12.53632314989425\n";
                server.writeDatagram(response,host,port);
            }
        });
        PredictTracker tracker; QSignalSpy done(&tracker,&PredictTracker::finished);
        QVERIFY(tracker.request("127.0.0.1",server.localPort(),"OSCAR-7")); QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,3000);
        QVERIFY(!done.first().at(0).toBool()); QVERIFY(tracker.snapshot().isEmpty());
    }
    void publicStatusStartsWithoutSnapshot()
    {
        PredictTracker tracker;
        const auto state=tracker.publicStatus();
        QCOMPARE(state.value("available").toBool(),false);
        QCOMPARE(state.value("fresh").toBool(),false);
        QCOMPARE(state.value("orbitalFresh").toBool(),false);
        QCOMPARE(state.value("busy").toBool(),false);
        QVERIFY(!state.contains("satellite"));
    }
    void exposesSnapshotAndBusyToDesktop()
    {
        PredictTracker tracker;
        QVERIFY(tracker.metaObject()->indexOfProperty("snapshot") >= 0);
        QVERIFY(tracker.metaObject()->indexOfProperty("busy") >= 0);
        QVERIFY(tracker.metaObject()->indexOfMethod("request(QString,int,QString)") >= 0);
        QVERIFY(tracker.metaObject()->indexOfMethod("correctedFrequencies(double,double)") >= 0);
    }
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
        const QDate epochDate = QDateTime::currentDateTimeUtc().date().addDays(-1);
        QByteArray epoch = QStringLiteral("%1%2.00000000")
                               .arg(epochDate.year() % 100, 2, 10, QLatin1Char('0'))
                               .arg(epochDate.dayOfYear(), 3, 10, QLatin1Char('0'))
                               .toLatin1();
        QByteArray doppler = "961.742249\n";
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
                    reply = doppler;
                else if (request.startsWith("GET_TLE "))
                    reply = QByteArray("FO-29\n") + checksummed(QByteArray("1 24278U 96046B   ") + epoch +
                            "  .00000000  00000-0  00000-0 0  9991") + "\n" +
                            checksummed("2 24278  98.5000  80.0000 0350000  40.0000 320.0000 13.50000000100000") + "\n";
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
        QVERIFY(tracker.publicStatus().value("available").toBool());
        QVERIFY(tracker.publicStatus().value("fresh").toBool());
        QVERIFY(tracker.publicStatus().value("orbitalFresh").toBool());
        QCOMPARE(tracker.publicStatus().value("tleCatalogue").toString(),QString("24278"));
        QCOMPARE(snapshot.value("azimuth").toDouble(), 203.75);
        QCOMPARE(snapshot.value("elevation").toDouble(), -60.01);
        QCOMPARE(snapshot.value("doppler100MHz").toDouble(), 961.742249);
        QVERIFY(snapshot.value("tleEpoch").toDateTime().isValid());
        const QVariantMap corrected = tracker.correctedFrequencies(146.0, 435.0);
        QVERIFY2(corrected.value("ok").toBool(), qPrintable(corrected.value("error").toString()));
        QVERIFY(qAbs(corrected.value("txMHz").toDouble() - 145.99859585631646) < 1e-9);
        QVERIFY(qAbs(corrected.value("rxMHz").toDouble() - 435.00418357878315) < 1e-9);
        QVERIFY(!tracker.correctedFrequencies(-1, 435).value("ok").toBool());
        QCOMPARE(commands,
                 QStringList({"GET_MODE", "GET_TLE FO-29", "GET_SAT FO-29", "GET_DOPPLER FO-29", "GET_TLE FO-29", "GET_MODE"}));
        doppler = "-961.742249\n";
        done.clear();
        QVERIFY(tracker.request("127.0.0.1", server.localPort(), "FO-29"));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 3000);
        QVERIFY(done.first().at(0).toBool());
        const QVariantMap receding = tracker.correctedFrequencies(146.0, 435.0);
        QVERIFY(receding.value("ok").toBool());
        QVERIFY(qAbs(receding.value("txMHz").toDouble() - 146.00140414368354) < 1e-9);
        QVERIFY(qAbs(receding.value("rxMHz").toDouble() - 434.99581642121685) < 1e-9);
        QTest::qWait(10500);
        QVERIFY(!tracker.correctedFrequencies(146, 435).value("ok").toBool());
        QVERIFY(!tracker.publicStatus().value("fresh").toBool());
        epoch = "18001.00000000";
        done.clear();
        QVERIFY(tracker.request("127.0.0.1", server.localPort(), "FO-29"));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 3000);
        QVERIFY(done.first().at(0).toBool());
        QVERIFY(!tracker.correctedFrequencies(146, 435).value("ok").toBool());
        QVERIFY(!tracker.publicStatus().value("orbitalFresh").toBool());
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
                    reply = QByteArray("FO-29\n") + checksummed("1 24278U 96046B   26326.50000000  .00000000  00000-0  00000-0 0  9991") + "\n" +
                            checksummed("2 24278  98.5000  80.0000 0350000  40.0000 320.0000 13.50000000100000") + "\n";
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
