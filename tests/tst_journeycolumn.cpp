#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include "core/LogDatabase.h"

class TestJourneyColumn : public QObject {
    Q_OBJECT
private:
    static QString iniPath(const QTemporaryDir& dir)
    {
        return dir.filePath(QStringLiteral("settings/Decodium/DecoDXLog.ini"));
    }
    static void seed(const QTemporaryDir& dir, const QString& layout, bool migrated)
    {
        QDir().mkpath(QFileInfo(iniPath(dir)).absolutePath());
        QSettings s(iniPath(dir), QSettings::IniFormat);
        s.setValue(QStringLiteral("layout/columnLayout"), layout);
        if (migrated)
            s.setValue(QStringLiteral("layout/journeyColumnMigrated"), true);
        s.sync();
    }
    static bool launch(const QTemporaryDir& dir, const QString& probe, QByteArray& output)
    {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        env.insert(QStringLiteral("QT_QUICK_BACKEND"), QStringLiteral("software"));
        env.insert(QStringLiteral("DECODXLOG_DATA_ROOT"), dir.filePath(QStringLiteral("data")));
        env.insert(QStringLiteral("DECODXLOG_GRAB_MS"), QStringLiteral("2800"));
        QProcess app;
        app.setProcessEnvironment(env);
        app.setProcessChannelMode(QProcess::MergedChannels);
        app.start(QStringLiteral(DECOLOG_APP), {
            QStringLiteral("--software-renderer"), QStringLiteral("--settings"), dir.filePath(QStringLiteral("settings")),
            QStringLiteral("--db"), dir.filePath(QStringLiteral("test.sqlite")), QStringLiteral("--port"), QStringLiteral("0"),
            QStringLiteral("--show"), probe, QStringLiteral("--grab"), dir.filePath(QStringLiteral("screen.png"))
        });
        if (!app.waitForStarted(10000) || !app.waitForFinished(30000)) {
            output = app.errorString().toUtf8() + app.readAll();
            return false;
        }
        output = app.readAll();
        return app.exitStatus() == QProcess::NormalExit && app.exitCode() == 0;
    }
private slots:
    void migratesLegacyLayoutAndPersistsActualHeaderDrag()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        seed(dir, QStringLiteral("call,band"), false);
        QByteArray output;
        QVERIFY2(launch(dir, QStringLiteral("headermove:main:2:0"), output), output.constData());
        QVERIFY2(output.contains("PROBE headermove main 2->0"), output.constData());
        QSettings first(iniPath(dir), QSettings::IniFormat);
        QVERIFY(first.value(QStringLiteral("layout/journeyColumnMigrated")).toBool());
        QCOMPARE(first.value(QStringLiteral("layout/columnLayout")).toString(), QStringLiteral("journey,call,band"));

        QVERIFY2(launch(dir, QStringLiteral("headermove:main:0:2"), output), output.constData());
        QVERIFY2(output.contains("PROBE headermove main 0->2"), output.constData());
        first.sync();
        QCOMPARE(first.value(QStringLiteral("layout/columnLayout")).toString(), QStringLiteral("call,band,journey"));
    }

    void intentionallyHiddenColumnStaysHiddenAfterRestart()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        seed(dir, QStringLiteral("call,band"), true);
        QByteArray output;
        QVERIFY2(launch(dir, QStringLiteral("headermove:main:0:1"), output), output.constData());
        QVERIFY2(output.contains("PROBE headermove main 0->1"), output.constData());
        QSettings after(iniPath(dir), QSettings::IniFormat);
        QCOMPARE(after.value(QStringLiteral("layout/columnLayout")).toString(), QStringLiteral("band,call"));
    }

    void mapButtonUsesCorrectQsoAfterColumnMove()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        seed(dir, QStringLiteral("call,band"), false);
        qint64 older = 0, newer = 0;
        {
            decolog::core::LogDatabase db;
            QVERIFY(db.open(dir.filePath(QStringLiteral("test.sqlite"))));
            older = db.insertQso({{"CALL", "K1AAA"}, {"QSO_DATE", "20261118"}, {"TIME_ON", "1200"},
                                   {"BAND", "20m"}, {"MODE", "FT8"}}, "test").id;
            newer = db.insertQso({{"CALL", "K1BBB"}, {"QSO_DATE", "20261118"}, {"TIME_ON", "1201"},
                                   {"BAND", "20m"}, {"MODE", "FT8"}}, "test").id;
            QVERIFY(older > 0); QVERIFY(newer > 0);
        }
        QByteArray output;
        QVERIFY2(launch(dir, QStringLiteral("headermove:main:2:0"), output), output.constData());
        QSettings after(iniPath(dir), QSettings::IniFormat);
        QCOMPARE(after.value(QStringLiteral("layout/columnLayout")).toString(), QStringLiteral("journey,call,band"));
        QVERIFY2(launch(dir, QStringLiteral("journeyclick:0"), output), output.constData());
        const QByteArray first = "PROBE journeyclick expected=" + QByteArray::number(newer)
                                 + " opened=" + QByteArray::number(newer);
        QVERIFY2(output.contains(first), output.constData());
        QVERIFY2(launch(dir, QStringLiteral("journeyclick:1"), output), output.constData());
        const QByteArray second = "PROBE journeyclick expected=" + QByteArray::number(older)
                                  + " opened=" + QByteArray::number(older);
        QVERIFY2(output.contains(second), output.constData());
    }
};

QTEST_GUILESS_MAIN(TestJourneyColumn)
#include "tst_journeycolumn.moc"
