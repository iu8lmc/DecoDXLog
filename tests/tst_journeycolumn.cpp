#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QSqlQuery>
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
    void radioNetRosterUiAndRestartDoNotCreateQsos()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QByteArray output;
        QVERIFY2(launch(dir, QStringLiteral("radionet:seed"), output), output.constData());
        QVERIFY2(output.contains("PROBE radioNet visible=true members=1 first=K1AAA"), output.constData());
        {
            decolog::core::LogDatabase db;
            QVERIFY(db.open(dir.filePath(QStringLiteral("test.sqlite"))));
            QCOMPARE(db.qsoCount(), 0);
            QSqlQuery q(db.connection());
            QVERIFY(q.exec("SELECT call,name,status FROM radio_net_member"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toString(), QStringLiteral("K1AAA"));
            QCOMPARE(q.value(1).toString(), QStringLiteral("Alice"));
            QCOMPARE(q.value(2).toString(), QStringLiteral("present"));
        }
        QVERIFY2(launch(dir, QStringLiteral("radionet:resume"), output), output.constData());
        QVERIFY2(output.contains("PROBE radioNet visible=true members=1 first=K1AAA"), output.constData());
    }

    void importUndoDialogAcceptAndRejectAcrossRestart()
    {
        for (const bool accept : {false, true}) {
            QTemporaryDir dir; QVERIFY(dir.isValid());
            qint64 importedId = 0, changedId = 0, independentId = 0;
            {
                decolog::core::LogDatabase db;
                QVERIFY(db.open(dir.filePath(QStringLiteral("test.sqlite"))));
                const auto imported = db.importAdif(
                    "<CALL:5>K1AAA<QSO_DATE:8>20260101<TIME_ON:4>0900<BAND:3>20m<MODE:2>CW<EOR>"
                    "<CALL:5>K1CCC<QSO_DATE:8>20260101<TIME_ON:4>0930<BAND:3>20m<MODE:2>CW<EOR>");
                QCOMPARE(imported.inserted, 2);
                importedId = imported.insertedIds.first();
                changedId = imported.insertedIds.last();
                auto changed = db.record(changedId);
                QVERIFY(changed.has_value());
                changed->set(QStringLiteral("COMMENT"), QStringLiteral("review me"));
                QCOMPARE(db.updateQso(changedId, *changed).status, decolog::core::InsertResult::Status::Inserted);
                independentId = db.insertQso({{"CALL", "K1BBB"}, {"QSO_DATE", "20260101"},
                    {"TIME_ON", "1000"}, {"BAND", "20m"}, {"MODE", "CW"}}, "radio").id;
                QVERIFY(independentId > 0);
            }
            QByteArray output;
            QVERIFY2(launch(dir, accept ? QStringLiteral("undoimport") : QStringLiteral("undoimport:cancel"), output), output.constData());
            QVERIFY2(output.contains("PROBE importUndoPreview"), output.constData());
            QVERIFY2(output.contains("\"eligible\":1"), output.constData());
            QVERIFY2(output.contains("\"changed\":1"), output.constData());
            if (accept) {
                QVERIFY2(output.contains("PROBE importUndo "), output.constData());
                QVERIFY2(output.contains("\"changed\":1"), output.constData());
                QVERIFY2(output.contains("\"qsoId\":" + QByteArray::number(changedId)), output.constData());
                QVERIFY2(output.contains("PROBE importUndoView visible=true row=QSO "), output.constData());
                QVERIFY2(output.contains("K1CCC"), output.constData());
            } else {
                QVERIFY(!output.contains("PROBE importUndo "));
            }
            decolog::core::LogDatabase reopened;
            QVERIFY(reopened.open(dir.filePath(QStringLiteral("test.sqlite"))));
            QCOMPARE(reopened.meta(importedId)->deleted, accept);
            QVERIFY(!reopened.meta(changedId)->deleted);
            QVERIFY(!reopened.meta(independentId)->deleted);
        }
    }

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
