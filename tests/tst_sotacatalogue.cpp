#include "core/SotaCatalogue.h"
#include <QFile>
#include <QCryptographicHash>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QTcpServer>
#include <QTcpSocket>
using decolog::core::SotaCatalogue;
class TestSotaCatalogue : public QObject {
    Q_OBJECT
private slots:
    void liveOfficialUpdateWhenRequested()
    {
        if (qEnvironmentVariable("DECOLOG_SOTA_LIVE") != QLatin1String("1")) QSKIP("Live SOTA download not requested");
        QTemporaryDir dir; QVERIFY(dir.isValid()); SotaCatalogue catalogue(dir.filePath("live.sqlite"));
        QSignalSpy done(&catalogue, &SotaCatalogue::finished); QVERIFY(catalogue.updateFromOfficial());
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 120000);
        QVERIFY2(done.at(0).at(0).toBool(), qPrintable(done.at(0).at(1).toString()));
        const auto info = catalogue.info(); QCOMPARE(info.value("source").toString(), QString("official-https"));
        QVERIFY(info.value("count").toInt() > 100000); QVERIFY(info.value("sourceUrl").toString().startsWith("https://"));
        qInfo() << "live SOTA update" << info;
    }
    void officialSnapshotWhenProvided()
    {
        const QString path = qEnvironmentVariable("DECOLOG_SOTA_FIXTURE");
        if (path.isEmpty()) QSKIP("No external SOTA CSV fixture supplied");
        QTemporaryDir dir; QVERIFY(dir.isValid()); SotaCatalogue catalogue(dir.filePath("catalog.sqlite"));
        QSignalSpy done(&catalogue, &SotaCatalogue::finished);
        QVERIFY(catalogue.importFile(QUrl::fromLocalFile(path)));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 120000);
        QVERIFY2(done.at(0).at(0).toBool(), qPrintable(done.at(0).at(1).toString()));
        const auto info = catalogue.info(); QVERIFY(info.value("count").toInt() > 100000);
        QCOMPARE(catalogue.lookup("3B8/MU-001", QDate(2026, 11, 23)).value("status").toString(), QString("active"));
        qInfo() << "catalogue snapshot" << info;
    }
    void networkUpdatePreservesOldCatalogueOnFailure()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        const QByteArray csv("SOTA Summits List (Date=22/11/2026)\nSummitCode,AssociationName,RegionName,SummitName,AltM,AltFt,GridRef1,GridRef2,Longitude,Latitude,Points,BonusPoints,ValidFrom,ValidTo,ActivationCount,ActivationDate,ActivationCall\nI/LM-001,Italy,Lombardy,Monte Test,100,328,0,0,9,45,2,0,01/01/2020,31/12/2030,0,,\n");
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            while (auto* socket = server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [socket, csv] {
                    const QByteArray request = socket->readAll();
                    if (request.contains("GET /ok ")) socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(csv.size()) + "\r\nConnection: close\r\n\r\n" + csv);
                    else if (request.contains("GET /redirect ")) socket->write("HTTP/1.1 302 Found\r\nLocation: http://example.com/unsafe\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                    else if (request.contains("GET /redirect-ok ")) socket->write("HTTP/1.1 302 Found\r\nLocation: /ok\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                    else if (request.contains("GET /truncated ")) socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(csv.size() + 20) + "\r\nConnection: close\r\n\r\n" + csv);
                    else if (request.contains("GET /oversize ")) socket->write("HTTP/1.1 200 OK\r\nContent-Length: 134217729\r\nConnection: close\r\n\r\n");
                    else if (request.contains("GET /malformed ")) socket->write("HTTP/1.1 200 OK\r\nContent-Length: 3\r\nConnection: close\r\n\r\nbad");
                    else socket->write("HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                    socket->disconnectFromHost();
                });
            }
        });
        const auto url = [&](const QString& path) { return QUrl(QString("http://127.0.0.1:%1/%2").arg(server.serverPort()).arg(path)); };
        SotaCatalogue catalogue(dir.filePath("catalog.sqlite")); QSignalSpy done(&catalogue, &SotaCatalogue::finished);
        QVERIFY(catalogue.updateFromLoopbackForTest(url("ok")));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 10000); QVERIFY2(done.at(0).at(0).toBool(), qPrintable(done.at(0).at(1).toString()));
        QCOMPARE(catalogue.info().value("source").toString(), QString("network-unverified"));
        QCOMPARE(catalogue.lookup("I/LM-001", QDate(2026, 1, 1)).value("status").toString(), QString("active"));
        done.clear(); QVERIFY(catalogue.updateFromLoopbackForTest(url("redirect-ok")));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 10000); QVERIFY2(done.at(0).at(0).toBool(), qPrintable(done.at(0).at(1).toString()));
        for (const QString& path : {QString("bad"), QString("redirect"), QString("truncated"), QString("oversize"), QString("malformed")}) {
            done.clear(); QVERIFY(catalogue.updateFromLoopbackForTest(url(path)));
            QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 10000); QVERIFY(!done.at(0).at(0).toBool());
            QCOMPARE(catalogue.lookup("I/LM-001", QDate(2026, 1, 1)).value("status").toString(), QString("active"));
        }
        done.clear(); QVERIFY(catalogue.updateFromLoopbackForTest(url("ok"))); catalogue.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 10000); QVERIFY(!done.at(0).at(0).toBool());
        QCOMPARE(catalogue.lookup("I/LM-001", QDate(2026, 1, 1)).value("status").toString(), QString("active"));
        QVERIFY(!catalogue.updateFromLoopbackForTest(QUrl("http://example.com/")));
    }
    void importDoesNotReplaceUnrelatedExistingFile()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const auto source = dir.filePath("list.csv"), dest = dir.filePath("existing.sqlite");
        QFile f(source); QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("SOTA Summits List (Date=22/11/2026)\nSummitCode,AssociationName,RegionName,SummitName,AltM,AltFt,GridRef1,GridRef2,Longitude,Latitude,Points,BonusPoints,ValidFrom,ValidTo,ActivationCount,ActivationDate,ActivationCall\nI/LM-001,Italy,Lombardy,Monte Test,100,328,0,0,9,45,2,0,01/01/2020,31/12/2030,0,,\n"); f.close();
        QFile unrelated(dest); QVERIFY(unrelated.open(QIODevice::WriteOnly)); unrelated.write("unrelated user data"); unrelated.close();
        SotaCatalogue catalogue(dest); QSignalSpy done(&catalogue, &SotaCatalogue::finished);
        QVERIFY(catalogue.importFile(QUrl::fromLocalFile(source)));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 10000); QVERIFY(!done.at(0).at(0).toBool());
        QVERIFY(unrelated.open(QIODevice::ReadOnly)); QCOMPARE(unrelated.readAll(), QByteArray("unrelated user data"));
    }
    void importIsAtomicAndSupportsOfflineDateLookup()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const auto source = dir.filePath("summits.csv"), dest = dir.filePath("catalog.sqlite");
        QFile f(source); QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("SOTA Summits List (Date=22/11/2026)\nSummitCode,AssociationName,RegionName,SummitName,AltM,AltFt,GridRef1,GridRef2,Longitude,Latitude,Points,BonusPoints,ValidFrom,ValidTo,ActivationCount,ActivationDate,ActivationCall\nI/LM-001,Italy,Lombardy,\"Monte, Test\",100,328,0,0,9,45,2,0,01/01/2020,31/12/2030,0,,\nI/LM-002,Italy,Lombardy,Other,200,656,0,0,9,45,2,0,01/01/2021,31/12/2099,0,,\n"); f.close();
        SotaCatalogue catalogue(dest); QSignalSpy done(&catalogue, &SotaCatalogue::finished);
        QVERIFY(catalogue.importFile(QUrl::fromLocalFile(source))); QVERIFY(catalogue.busy());
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 10000); QVERIFY2(done.at(0).at(0).toBool(), qPrintable(done.at(0).at(1).toString()));
        const auto match = catalogue.lookup("i/lm-001", QDate(2026, 1, 1));
        QCOMPARE(match.value("status").toString(), QString("active")); QCOMPARE(match.value("name").toString(), QString("Monte, Test"));
        QCOMPARE(match.value("version").toString(), QString("2026-11-22"));
        QCOMPARE(catalogue.lookup("I/LM-001", QDate(2031, 1, 1)).value("status").toString(), QString("outsideValidity"));
        QCOMPARE(catalogue.lookup("I/LM-999", QDate(2026, 1, 1)).value("status").toString(), QString("notFound"));
        QCOMPARE(catalogue.info().value("source").toString(), QString("local-file-unverified"));
        QCOMPARE(catalogue.lookupIso("I/LM-001", "invalid").value("status").toString(), QString("invalidDate"));
        QVERIFY(!catalogue.importFile(QUrl::fromLocalFile(dest)));
        done.clear(); QVERIFY(catalogue.importFile(QUrl::fromLocalFile(source))); catalogue.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 10000); QVERIFY(!done.at(0).at(0).toBool());
        QCOMPARE(catalogue.lookup("I/LM-001", QDate(2026, 1, 1)).value("status").toString(), QString("active"));
        QVERIFY(f.open(QIODevice::ReadOnly)); const QByteArray original = f.readAll(); f.close();
        QCOMPARE(catalogue.info().value("sha256").toString(), QString::fromLatin1(QCryptographicHash::hash(original, QCryptographicHash::Sha256).toHex()));
        const QByteArray duplicate = original + original.mid(original.indexOf("I/LM-001,"), original.indexOf("I/LM-002,") - original.indexOf("I/LM-001,"));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate)); f.write(duplicate); f.close();
        done.clear(); QVERIFY(catalogue.importFile(QUrl::fromLocalFile(source)));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 10000); QVERIFY(!done.at(0).at(0).toBool());
        QCOMPARE(catalogue.lookup("I/LM-001", QDate(2026, 1, 1)).value("status").toString(), QString("active"));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate)); f.write("invalid\n"); f.close();
        done.clear(); QVERIFY(catalogue.importFile(QUrl::fromLocalFile(source)));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 10000); QVERIFY(!done.at(0).at(0).toBool());
        QVERIFY(f.remove());
        SotaCatalogue reopened(dest); QCOMPARE(reopened.lookup("I/LM-001", QDate(2026, 1, 1)).value("status").toString(), QString("active"));
        QByteArray newer = original; newer.replace("Date=22/11/2026", "Date=23/11/2026"); newer.replace("Monte, Test", "Monte, Updated");
        QVERIFY(f.open(QIODevice::WriteOnly)); QCOMPARE(f.write(newer), newer.size()); f.close();
        QSignalSpy updated(&reopened, &SotaCatalogue::finished);
        QVERIFY(reopened.importFile(QUrl::fromLocalFile(source))); QTRY_COMPARE_WITH_TIMEOUT(updated.size(), 1, 10000);
        QVERIFY2(updated.at(0).at(0).toBool(), qPrintable(updated.at(0).at(1).toString()));
        QCOMPARE(reopened.info().value("version").toString(), QString("2026-11-23"));
        QCOMPARE(reopened.lookup("I/LM-001", QDate(2026, 1, 1)).value("name").toString(), QString("Monte, Updated"));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "corrupt-sota-fixture"); db.setDatabaseName(dest); QVERIFY(db.open());
            QSqlQuery q(db); QVERIFY(q.exec("DELETE FROM metadata WHERE key='sha256'"));
        }
        QSqlDatabase::removeDatabase("corrupt-sota-fixture");
        QCOMPARE(reopened.lookup("I/LM-001", QDate(2026, 1, 1)).value("status").toString(), QString("unavailable"));
    }
};
QTEST_GUILESS_MAIN(TestSotaCatalogue)
#include "tst_sotacatalogue.moc"
