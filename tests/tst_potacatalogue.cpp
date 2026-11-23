#include "core/PotaCatalogue.h"
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
using decolog::core::PotaCatalogue;
class TestPotaCatalogue : public QObject {
    Q_OBJECT
private slots:
    void publishedSnapshotWhenProvided()
    {
        const QString path = qEnvironmentVariable("DECOLOG_POTA_FIXTURE");
        if (path.isEmpty()) QSKIP("No external POTA CSV fixture supplied");
        QTemporaryDir dir; QVERIFY(dir.isValid()); PotaCatalogue catalogue(dir.filePath("pota.sqlite"));
        QSignalSpy done(&catalogue, &PotaCatalogue::finished); QVERIFY(catalogue.importFile(QUrl::fromLocalFile(path)));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 120000); QVERIFY2(done.at(0).at(0).toBool(), qPrintable(done.at(0).at(1).toString()));
        QVERIFY(catalogue.info().value("count").toInt() > 100000);
        QCOMPARE(catalogue.lookup("US-0015@US-MD").value("locationStatus").toString(), QString("listed"));
        qInfo() << "POTA snapshot" << catalogue.info();
    }
    void refusesUnrelatedFileAndCancellation()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto source=dir.filePath("source.csv"), dest=dir.filePath("other.sqlite");
        QFile file(source); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("reference,name,active,locationDesc\nUS-0015,Park,1,US-MD\n"); file.close();
        QFile other(dest); QVERIFY(other.open(QIODevice::WriteOnly)); other.write("user data"); other.close();
        PotaCatalogue catalogue(dest); QSignalSpy done(&catalogue, &PotaCatalogue::finished);
        QVERIFY(catalogue.importFile(QUrl::fromLocalFile(source))); QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 10000); QVERIFY(!done.at(0).at(0).toBool());
        QVERIFY(other.open(QIODevice::ReadOnly)); QCOMPARE(other.readAll(), QByteArray("user data")); other.close();
        PotaCatalogue separate(dir.filePath("separate.sqlite")); QSignalSpy cancelled(&separate, &PotaCatalogue::finished);
        QVERIFY(separate.importFile(QUrl::fromLocalFile(source))); separate.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(cancelled.size(), 1, 10000); QVERIFY(!cancelled.at(0).at(0).toBool());
        QCOMPARE(separate.info().value("status").toString(), QString("unavailable"));
    }
    void offlineSnapshotAndLocations()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto source = dir.filePath("parks.csv"), dest = dir.filePath("pota.sqlite");
        QFile f(source); QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("\"reference\",\"name\",\"active\",\"entityId\",\"locationDesc\",\"latitude\",\"longitude\",\"grid\"\n\"US-0015\",\"Park, Trail\",\"1\",\"291\",\"US-DC,US-MD,US-WV\",\"39\",\"-77\",\"FM19\"\n\"IT-0001\",\"Old Park\",\"0\",\"248\",\"IT-25\",\"45\",\"9\",\"JN45\"\n\"FR-0001\",\"Unknown Location\",\"1\",\"227\",\"\",\"45\",\"1\",\"JN05\"\n"); f.close();
        PotaCatalogue catalogue(dest); QSignalSpy done(&catalogue, &PotaCatalogue::finished);
        QVERIFY(catalogue.importFile(QUrl::fromLocalFile(source))); QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 10000);
        QVERIFY2(done.at(0).at(0).toBool(), qPrintable(done.at(0).at(1).toString()));
        const auto park = catalogue.lookup("us-0015@us-md");
        QCOMPARE(park.value("status").toString(), QString("listedActive")); QCOMPARE(park.value("locationStatus").toString(), QString("listed"));
        QCOMPARE(park.value("name").toString(), QString("Park, Trail"));
        QCOMPARE(catalogue.lookup("US-0015@US-CA").value("locationStatus").toString(), QString("notListed"));
        QCOMPARE(catalogue.lookup("IT-0001").value("status").toString(), QString("listedInactive"));
        QCOMPARE(catalogue.lookup("US-9999").value("status").toString(), QString("notFound"));
        QCOMPARE(catalogue.lookup("FR-0001@FR-75").value("locationStatus").toString(), QString("unknown"));
        QVERIFY(!catalogue.info().contains("version"));
        QCOMPARE(catalogue.info().value("source").toString(), QString("local-file-unverified"));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate)); f.write("reference,name,active,locationDesc\nUS-0020,broken\"quote,1,US-MD\n"); f.close();
        done.clear(); QVERIFY(catalogue.importFile(QUrl::fromLocalFile(source))); QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 10000);
        QVERIFY(!done.at(0).at(0).toBool()); QCOMPARE(catalogue.lookup("US-0015").value("status").toString(), QString("listedActive"));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate)); f.write("broken\n"); f.close();
        done.clear(); QVERIFY(catalogue.importFile(QUrl::fromLocalFile(source))); QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 10000);
        QVERIFY(!done.at(0).at(0).toBool()); QVERIFY(f.remove());
        PotaCatalogue reopened(dest); QCOMPARE(reopened.lookup("US-0015").value("status").toString(), QString("listedActive"));
    }
};
QTEST_GUILESS_MAIN(TestPotaCatalogue)
#include "tst_potacatalogue.moc"
