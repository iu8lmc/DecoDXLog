// All identities and membership data below are invented. Files and caches live
// exclusively in QTemporaryDir and are removed automatically, including on failure.
#include "app/CallListController.h"
#include "core/CallList.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using decolog::core::CallList;
using decolog::app::CallListController;

class TestCallLists : public QObject {
    Q_OBJECT
    static bool write(const QString& path, const QByteArray& bytes) {
        QFile f(path);
        return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(bytes) == bytes.size();
    }
    static QString field(const QVariantList& entries, const QString& label, int row = 0) {
        for (const auto& f : entries.at(row).toMap().value("fields").toList())
            if (f.toMap().value("label").toString() == label) return f.toMap().value("value").toString();
        return {};
    }
private slots:
    void csvQuotingAndEncodings() {
        const auto data = CallList::parse(QByteArray("\xef\xbb\xbf") +
            "Call,Name,Club,Number,QTH\r\nzz0test1,\"Invented, \"\"Name\"\"\",Fake A,001,\"Line one\nLine two\"\r\n");
        QVERIFY2(data.error.isEmpty(), qPrintable(data.error));
        QCOMPARE(data.records, 1);
        QCOMPARE(field(data.calls.value("ZZ0TEST1"), "Name"), QString("Invented, \"Name\""));
        QCOMPARE(field(data.calls.value("ZZ0TEST1"), "QTH"), QString("Line one\nLine two"));
        QCOMPARE(field(data.calls.value("ZZ0TEST1"), "Number"), QString("001"));
        const QByteArray latin = "Call;Name\nZZ0TEST1;Invent\xe9\n";
        auto legacy = CallList::parse(latin);
        QVERIFY(legacy.error.isEmpty());
        QCOMPARE(legacy.preview.value("encoding").toString(), QString("Latin-1"));
        QCOMPARE(field(legacy.calls.value("ZZ0TEST1"), "Name"), QString::fromUtf8("Inventé"));
        QVERIFY(!CallList::parse(latin, {{"encoding", "utf8"}}).error.isEmpty());
        auto tab = CallList::parse("Call\tName\nZZ0TEST1\tFake\n");
        QCOMPARE(tab.records, 1);
        QCOMPARE(tab.preview.value("delimiter").toString(), QString("\t"));
    }
    void n1mmMembershipsAndMetadata() {
        const auto data = CallList::parse(
            "# imaginary memberships only\n!!Order!!,Call,Name,Misc,UserText,,,\n"
            "LATEST,,,2099-01-01\nRECORDS,,,2\nCLUBS,,,2\nFILE,,,C:\\fake.txt\n"
            "#SK ZZ0GONE,Gone,3,Fake A\nZZ0TEST1,Example,007,Fake A\n"
            "ZZ0TEST1,Example,007,Fake A\n!!AppendUserText!!\n"
            "!!Order!!,Call,Misc,Name,UserText\nZZ0TEST1,009,Other name,Fake B\n");
        QVERIFY2(data.error.isEmpty(), qPrintable(data.error));
        QCOMPARE(data.records, 2);
        QCOMPARE(data.calls.size(), 1);
        QCOMPARE(data.calls.value("ZZ0TEST1").size(), 2);
        QCOMPARE(field(data.calls.value("ZZ0TEST1"), "Misc", 1), QString("009"));
        QCOMPARE(field(data.calls.value("ZZ0TEST1"), "UserText", 1), QString("Fake B"));
        QVERIFY(!data.calls.contains("LATEST"));
        QVERIFY(!data.calls.contains("ZZ0GONE"));
        QVERIFY(data.preview.value("n1mm").toBool());
    }
    void mappingAndExactCalls() {
        const QByteArray csv = "Identity;Person;Association;Id;Place\nzz0test1/p;Fake;Example;001;Nowhere\n";
        auto preview = CallList::parse(csv, {}, true);
        QVERIFY(preview.error.isEmpty());
        QCOMPARE(preview.preview.value("callColumn").toInt(), -1);
        auto data = CallList::parse(csv, {{"callColumn", 0}, {"nameColumn", 1}, {"clubColumn", 2},
                                        {"memberColumn", 3}, {"qthColumn", 4}});
        QVERIFY(data.error.isEmpty());
        QCOMPARE(data.records, 1);
        QVERIFY(!data.calls.contains("ZZ0TEST1"));
        QCOMPARE(data.calls.value("ZZ0TEST1/P").first().toMap().value("club").toString(), QString("Example"));
        QCOMPARE(field(data.calls.value("ZZ0TEST1/P"), "MEMBER"), QString("001"));
        QCOMPARE(CallList::normalizedCall(" zz0test1/p "), QString("ZZ0TEST1/P"));
        QVERIFY(CallList::normalizedCall("ZZ0 TEST1").isEmpty());
        QCOMPARE(CallList::parse("ZZ0TEST1,Example\n", {{"header", false}}).records, 1);
        QCOMPARE(CallList::parse("Call\nZZ0TEST1\n").records, 1);
    }
    void malformed_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("unclosed") << QByteArray("Call,Name\nZZ0TEST1,\"broken\n");
        QTest::newRow("stray-quote") << QByteArray("Call,Name\nZZ0TEST1,ba\"d\n");
        QTest::newRow("trailing-junk") << QByteArray("Call,Name\nZZ0TEST1,\"bad\"junk\n");
        QTest::newRow("columns") << QByteArray("Call,Name\nZZ0TEST1,Fake,unexpected\n");
        QTest::newRow("invalid-calls") << QByteArray("Call,Name\nLATEST,Fake\nABC,Fake\n123,Fake\n");
        QTest::newRow("binary") << QByteArray("Call\0ZZ0TEST1", 13);
        QTest::newRow("oversize-field") << QByteArray("Call,Name\nZZ0TEST1,") + QByteArray(4097, 'a');
    }
    void malformed() {
        QFETCH(QByteArray, bytes);
        const auto result = CallList::parse(bytes);
        QVERIFY(!result.error.isEmpty());
        QVERIFY(result.calls.isEmpty());
    }
    void persistenceReloadAndIsolation() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString original = temp.filePath("fiction.csv");
        const QString storage = temp.filePath("cache");
        const QByteArray csv = "Call,Name,Club\nZZ0TEST1,Fake A,Imaginary A\n";
        QVERIFY(write(original, csv));
        QString firstId, secondId;
        {
            CallListController lists(storage);
            lists.start(); QTRY_VERIFY(!lists.busy());
            lists.inspect(QUrl::fromLocalFile(original), {}); QTRY_VERIFY(!lists.busy());
            QCOMPARE(lists.preview().value("records").toInt(), 1);
            lists.add(QUrl::fromLocalFile(original), "Fiction one", {}); QTRY_VERIFY(!lists.busy());
            QCOMPARE(lists.sources().size(), 1);
            firstId = lists.sources().first().toMap().value("id").toString();
            QCOMPARE(lists.lookup("zz0test1").size(), 1);
            QVERIFY(lists.lookup("ZZ0TEST1/P").isEmpty());
            QVERIFY(write(original, "Call,Name,Club\nZZ0TEST1,Fake B,Imaginary B\n"));
            lists.add(QUrl::fromLocalFile(original), "Fiction two", {}); QTRY_VERIFY(!lists.busy());
            secondId = lists.sources().last().toMap().value("id").toString();
            QCOMPARE(lists.lookup("ZZ0TEST1").size(), 2);
            // Invalid reload preserves both memory and the last known good disk copy.
            QVERIFY(write(original, "Call,Name\nZZ0TEST1,\"broken"));
            const auto before = lists.lookup("ZZ0TEST1");
            lists.reload(firstId); QTRY_VERIFY(!lists.busy());
            QVERIFY(lists.status().startsWith("Not applied"));
            QCOMPARE(lists.lookup("ZZ0TEST1"), before);
            lists.setEnabled(secondId, false); QTRY_VERIFY(!lists.busy());
            QCOMPARE(lists.lookup("ZZ0TEST1").size(), 1);
        }
        QVERIFY(QFile::remove(original));
        {
            CallListController restored(storage);
            restored.start(); QTRY_VERIFY(!restored.busy());
            QCOMPARE(restored.sources().size(), 2);
            QCOMPARE(restored.lookup("ZZ0TEST1").size(), 1);
            QVERIFY(!restored.sources().last().toMap().value("enabled").toBool());
            restored.reload(firstId); QTRY_VERIFY(!restored.busy());
            QCOMPARE(restored.lookup("ZZ0TEST1").size(), 1);
            // Successful reload replaces rather than accumulating stale records.
            QVERIFY(write(original, "Call,Name\nZZ0TEST2,Replacement\n"));
            restored.reload(firstId); QTRY_VERIFY(!restored.busy());
            QVERIFY(restored.lookup("ZZ0TEST1").isEmpty());
            QCOMPARE(restored.lookup("ZZ0TEST2").size(), 1);
            restored.setEnabled(secondId, true); QTRY_VERIFY(!restored.busy());
            QCOMPARE(restored.lookup("ZZ0TEST1").size(), 1);
            restored.remove(firstId); QTRY_VERIFY(!restored.busy());
            QVERIFY(QFile::exists(original));
            QCOMPARE(restored.sources().size(), 1);
            QCOMPARE(QDir(storage).entryList({"*.txt"}, QDir::Files).size(), 1);
        }
        {
            CallListController restored(storage);
            restored.start(); QTRY_VERIFY(!restored.busy());
            QCOMPARE(restored.sources().size(), 1);
            QCOMPARE(restored.lookup("ZZ0TEST1").size(), 1);
            restored.remove(secondId); QTRY_VERIFY(!restored.busy());
            QCOMPARE(restored.sources().size(), 0);
            QVERIFY(QDir(storage).entryList({"*.txt"}, QDir::Files).isEmpty());
        }
        const QString tempPath = temp.path();
        QVERIFY(temp.remove());
        QVERIFY(!QFile::exists(tempPath));
    }
    void storageFailureDoesNotReplace() {
        QTemporaryDir temp;
        const QString original = temp.filePath("fiction.csv");
        QVERIFY(write(original, "Call,Name\nZZ0TEST1,Example\n"));
        CallListController lists(temp.filePath("cache"));
        lists.add(QUrl::fromLocalFile(original), "Fiction", {}); QTRY_VERIFY(!lists.busy());
        QCOMPARE(lists.sources().size(), 1);
        const auto before = lists.sources();
        // Portable, deterministic write failure: manifest path occupied by a directory.
        const QString manifest = temp.filePath("cache/lists.json");
        QVERIFY(QFile::remove(manifest));
        QVERIFY(QDir().mkdir(manifest));
        lists.add(QUrl::fromLocalFile(original), "Should fail", {}); QTRY_VERIFY(!lists.busy());
        QCOMPARE(lists.sources(), before);
        QCOMPARE(QDir(temp.filePath("cache")).entryList({"*.txt"}, QDir::Files).size(), 1);
        QVERIFY(lists.status().startsWith("Not applied"));
    }
    void oversizedFileAndDirectoryAreRejected() {
        QTemporaryDir temp;
        const QString path = temp.filePath("oversized-fiction.csv");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.resize(CallList::maxBytes + 1));
        file.close();
        CallListController lists(temp.filePath("cache"));
        lists.inspect(QUrl::fromLocalFile(path), {}); QTRY_VERIFY(!lists.busy());
        QVERIFY(lists.preview().isEmpty());
        QVERIFY(lists.status().contains("32 MiB"));
        lists.add(QUrl::fromLocalFile(temp.path()), "Not a file", {}); QTRY_VERIFY(!lists.busy());
        QVERIFY(lists.sources().isEmpty());
        QVERIFY(lists.status().contains("regular file"));
    }
    void largeFileDoesNotBlockEventLoop() {
        QTemporaryDir temp;
        QByteArray csv("Call,Name,Club\n");
        for (int i = 0; i < 110000; ++i)
            csv += "ZZ0TEST" + QByteArray::number(i) + ",Fictional person,Imaginary club\n";
        const QString path = temp.filePath("large-fiction.csv");
        QVERIFY(write(path, csv));
        CallListController lists(temp.filePath("cache"));
        int ticks = 0;
        QTimer timer;
        connect(&timer, &QTimer::timeout, [&] { ++ticks; });
        timer.start(1);
        QElapsedTimer elapsed; elapsed.start();
        lists.add(QUrl::fromLocalFile(path), "Large fiction", {});
        QVERIFY(lists.busy());
        QVERIFY(elapsed.elapsed() < 500);
        QTRY_VERIFY_WITH_TIMEOUT(!lists.busy(), 30000);
        QVERIFY(ticks > 0);
        QCOMPARE(lists.sources().first().toMap().value("records").toInt(), 110000);
        QCOMPARE(lists.lookup("ZZ0TEST109999").size(), 1);
        qInfo() << "110000 invented records imported in" << elapsed.elapsed() << "ms; GUI ticks:" << ticks;
    }
    void teardownWhileLoading() {
        QTemporaryDir temp;
        const QString path = temp.filePath("fiction.csv");
        QVERIFY(write(path, "Call\nZZ0TEST1\n"));
        auto* lists = new CallListController(temp.filePath("cache"));
        lists->inspect(QUrl::fromLocalFile(path), {});
        delete lists;
        QCoreApplication::processEvents(); // No late callback to a dead QObject.
    }
};
QTEST_GUILESS_MAIN(TestCallLists)
#include "tst_calllists.moc"
