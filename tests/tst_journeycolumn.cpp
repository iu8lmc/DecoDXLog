#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include "core/LogDatabase.h"
#include "core/RadioNet.h"

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
    void roverManualQsoRespectsHistoryAndExplicitOverride()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QByteArray output;
        QVERIFY2(launch(dir, QStringLiteral("roverprobe"), output), output.constData());
        QVERIFY2(output.contains("PROBE roverStarted=true grid=JN71DC"), output.constData());
        QVERIFY2(output.contains("PROBE roverFirst count=1 closed=true"), output.constData());
        QVERIFY2(output.contains("PROBE roverAmbiguous kept=true count=1"), output.constData());
        QVERIFY2(output.contains("PROBE roverOverride closed=true count=2"), output.constData());
        QVERIFY2(output.contains("PROBE roverActivation error= kept=true count=2"), output.constData());
        QVERIFY2(output.contains("PROBE roverActivationOverride closed=true count=3"), output.constData());
        decolog::core::LogDatabase db;
        QVERIFY(db.open(dir.filePath(QStringLiteral("test.sqlite"))));
        QCOMPARE(db.qsoCount(), 3);
        QSqlQuery q(db.connection());
        QVERIFY(q.exec("SELECT id,call FROM qso WHERE deleted=0 ORDER BY id"));
        QVERIFY(q.next()); QCOMPARE(q.value(1).toString(), QStringLiteral("K1AAA")); QCOMPARE(db.record(q.value(0).toLongLong())->value("MY_GRIDSQUARE"), QStringLiteral("JN71DC"));
        QVERIFY(q.next()); QCOMPARE(q.value(1).toString(), QStringLiteral("K1BBB")); QCOMPARE(db.record(q.value(0).toLongLong())->value("MY_GRIDSQUARE"), QStringLiteral("JM67AB"));
        QVERIFY(q.next()); QCOMPARE(q.value(1).toString(), QStringLiteral("K1CCC")); QCOMPARE(db.record(q.value(0).toLongLong())->value("MY_GRIDSQUARE"), QStringLiteral("JM67AB"));
        QVERIFY(!q.next());
        const auto exported = db.exportAdif();
        const auto parsed = decolog::core::adif::parse(exported);
        QCOMPARE(parsed.records.size(), 3);
        QCOMPARE(parsed.records.at(0).value("MY_GRIDSQUARE"), QStringLiteral("JN71DC"));
        QCOMPARE(parsed.records.at(1).value("MY_GRIDSQUARE"), QStringLiteral("JM67AB"));
        QCOMPARE(parsed.records.at(2).value("MY_GRIDSQUARE"), QStringLiteral("JM67AB"));
        decolog::core::LogDatabase imported;
        QVERIFY(imported.open(":memory:"));
        QCOMPARE(imported.importAdif(exported).inserted, 3);
        const auto roundtrip = decolog::core::adif::parse(imported.exportAdif());
        QCOMPARE(roundtrip.records.size(), 3);
        QCOMPARE(roundtrip.records.at(0).value("MY_GRIDSQUARE"), QStringLiteral("JN71DC"));
        QCOMPARE(roundtrip.records.at(1).value("MY_GRIDSQUARE"), QStringLiteral("JM67AB"));
        QCOMPARE(roundtrip.records.at(2).value("MY_GRIDSQUARE"), QStringLiteral("JM67AB"));
        QSqlQuery history(db.connection());
        QVERIFY(history.exec("SELECT COUNT(*) FROM rover_position")); QVERIFY(history.next()); QCOMPARE(history.value(0).toInt(), 2);
    }
    void vuccBoundaryPreviewInApplication()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        {
            decolog::core::LogDatabase db;
            QVERIFY(db.open(dir.filePath(QStringLiteral("test.sqlite"))));
            const QByteArray input = "<CALL:5>K1AAA<QSO_DATE:8>20260101<TIME_ON:4>1200<BAND:2>6m<MODE:2>CW"
                                     "<QSL_RCVD:1>Y<GRIDSQUARE:4>EM98<VUCC_GRIDS:19>EM98,FM08,EM97,FM07<EOR>";
            QCOMPARE(db.importAdif(input).inserted, 1);
        }
        QByteArray output;
        QVERIFY2(launch(dir, QStringLiteral("vuccboundary"), output), output.constData());
        const QByteArray marker = "PROBE vuccBoundary ";
        const int start = output.indexOf(marker);
        QVERIFY2(start >= 0, output.constData());
        const int end = output.indexOf('\n', start);
        const auto document = QJsonDocument::fromJson(output.mid(start + marker.size(), end - start - marker.size()));
        QVERIFY(!document.isNull());
        QCOMPARE(document.object().value("eligible").toArray().size(), 4);
        QVERIFY(!document.object().value("officialRulesVerified").toBool());
        decolog::core::LogDatabase db;
        QVERIFY(db.open(dir.filePath(QStringLiteral("test.sqlite"))));
        QCOMPARE(db.qsoCount(), 1);
    }

    void exportTrackerQsoFixture()
    {
        const QString path = qEnvironmentVariable("DECOLOG_TEST_SATELLITE_DB");
        if (path.isEmpty())
            QSKIP("Set DECOLOG_TEST_SATELLITE_DB to verify a saved tracker QSO");
        decolog::core::LogDatabase source;
        QVERIFY(source.open(path));
        const QByteArray exported = source.exportAdif("fixture");
        const auto document = decolog::core::adif::parse(exported);
        QCOMPARE(document.records.size(), 1);
        const auto &qso = document.records.first();
        QCOMPARE(qso.value("CALL"), QStringLiteral("K1AAA"));
        QCOMPARE(qso.value("FREQ"), QStringLiteral("145.998596"));
        QCOMPARE(qso.value("FREQ_RX"), QStringLiteral("435.004184"));
        QCOMPARE(qso.value("BAND"), QStringLiteral("2m"));
        QCOMPARE(qso.value("BAND_RX"), QStringLiteral("70cm"));
        QCOMPARE(qso.value("PROP_MODE"), QStringLiteral("SAT"));
        QCOMPARE(qso.value("SAT_NAME"), QStringLiteral("OSCAR-7"));
        QCOMPARE(qso.value("SAT_MODE"), QStringLiteral("V/U"));
        QCOMPARE(qso.value("MODE"), qEnvironmentVariable("DECOLOG_TEST_SATELLITE_MODE"));
        decolog::core::LogDatabase imported;
        QVERIFY(imported.open(":memory:"));
        QCOMPARE(imported.importAdif(exported).inserted, 1);
        const auto roundtrip = decolog::core::adif::parse(imported.exportAdif());
        QCOMPARE(roundtrip.records.size(), 1);
        QCOMPARE(roundtrip.records.first().value("FREQ_RX"), QStringLiteral("435.004184"));
        QCOMPARE(roundtrip.records.first().value("SAT_NAME"), QStringLiteral("OSCAR-7"));
        QCOMPARE(roundtrip.records.first().value("SAT_MODE"), QStringLiteral("V/U"));
    }

    void manualSatelliteSplitPersistsBothFrequencies()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QByteArray output;
        QVERIFY2(launch(dir, QStringLiteral("satellitesplit"), output), output.constData());
        QVERIFY2(output.contains("PROBE satelliteInvalid kept=true"), output.constData());
        QVERIFY2(output.contains("PROBE satelliteSaved closed=true"), output.constData());
        decolog::core::LogDatabase db;
        QVERIFY(db.open(dir.filePath(QStringLiteral("test.sqlite"))));
        QCOMPARE(db.qsoCount(), 1);
        QSqlQuery q(db.connection());
        QVERIFY(q.exec("SELECT freq,freq_rx,band,band_rx,sat_name,sat_mode,prop_mode FROM qso WHERE deleted=0"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toDouble(), 145.9);
        QCOMPARE(q.value(1).toDouble(), 435.25);
        QCOMPARE(q.value(2).toString(), QStringLiteral("2m"));
        QCOMPARE(q.value(3).toString(), QStringLiteral("70cm"));
        QCOMPARE(q.value(4).toString(), QStringLiteral("FO-29"));
        QCOMPARE(q.value(5).toString(), QStringLiteral("U/V"));
        QCOMPARE(q.value(6).toString(), QStringLiteral("SAT"));
        const QByteArray adif = db.exportAdif().toLower();
        QVERIFY(adif.contains("<freq_rx:10>435.250000"));
        QVERIFY(adif.contains("<band_rx:4>70cm"));
    }

    void radioNetReminderShowsWhenDue()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QByteArray output;
        QVERIFY2(launch(dir, QStringLiteral("radionet:reminder"), output), output.constData());
        QVERIFY2(output.contains("PROBE reminderScheduled=true"), output.constData());
        QVERIFY2(output.contains("PROBE reminderDue=true"), output.constData());
        decolog::core::LogDatabase db;
        QVERIFY(db.open(dir.filePath(QStringLiteral("test.sqlite"))));
        decolog::core::RadioNet net(&db);
        net.load();
        QVERIFY(net.reminder().value("scheduled").toBool());
        QVERIFY(net.reminder().value("due").toBool());
        QCOMPARE(db.qsoCount(), 0);
    }

    void radioNetNavigatesCompleteHistory()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        {
            decolog::core::LogDatabase db;
            QVERIFY(db.open(dir.filePath(QStringLiteral("test.sqlite"))));
            decolog::core::RadioNet net(&db);
            QVERIFY(net.start("History"));
            QVERIFY(net.checkIn("K1AAA", "Alice", ""));
            const qint64 id = net.members().first().toMap().value("id").toLongLong();
            for (int i = 0; i < 200; ++i)
                QVERIFY(net.setStatus(id, i % 2 ? "present" : "waiting"));
        }
        QByteArray output;
        QVERIFY2(launch(dir, QStringLiteral("radionet:history"), output), output.constData());
        QVERIFY2(output.contains("PROBE historyFirst count=200 older=true"), output.constData());
        QVERIFY2(output.contains("PROBE historyOlder count=2 newer=true older=false"), output.constData());
        QVERIFY2(output.contains("PROBE historyReturn count=200 newer=false"), output.constData());
        decolog::core::LogDatabase db;
        QVERIFY(db.open(dir.filePath(QStringLiteral("test.sqlite"))));
        QCOMPARE(db.qsoCount(), 0);
    }

    void radioNetExplicitSaveAndRepeatQso()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QByteArray output;
        QVERIFY2(launch(dir, QStringLiteral("radionet:save"), output), output.constData());
        QVERIFY2(output.contains("PROBE radioNetInvalid kept=true"), output.constData());
        QVERIFY2(output.contains("PROBE radioNetMismatch kept=true"), output.constData());
        QVERIFY2(output.contains("PROBE radioNetSaved closed=true"), output.constData());
        QVERIFY2(output.contains("PROBE radioNetRepeat closed=true"), output.constData());
        QVERIFY2(output.contains("PROBE radioNetReported count=2"), output.constData());
        QVERIFY2(output.contains("PROBE radioNetClosed kept=true"), output.constData());
        decolog::core::LogDatabase db;
        QVERIFY(db.open(dir.filePath(QStringLiteral("test.sqlite"))));
        QCOMPARE(db.qsoCount(), 2);
        QSqlQuery q(db.connection());
        QSqlQuery session(db.connection());
        QVERIFY(session.exec("SELECT uuid FROM radio_net_session"));
        QVERIFY(session.next());
        const QString sessionUuid = session.value(0).toString();
        QVERIFY(!sessionUuid.isEmpty());
        QVERIFY(q.exec("SELECT call,name,band,mode,id FROM qso WHERE deleted=0 ORDER BY id"));
        for (int i = 0; i < 2; ++i) {
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toString(), QStringLiteral("K1AAA"));
            QCOMPARE(q.value(1).toString(), QStringLiteral("Alice"));
            QCOMPARE(q.value(2).toString(), QStringLiteral("20m"));
            QCOMPARE(q.value(3).toString(), QStringLiteral("CW"));
            const auto record = db.record(q.value(4).toLongLong());
            QVERIFY(record.has_value());
            QCOMPARE(record->value("APP_DECOLOG_RADIO_NET_SESSION"), sessionUuid);
            QCOMPARE(record->value("APP_DECOLOG_RADIO_NET_CALL"), QStringLiteral("K1AAA"));
        }
        QVERIFY(!q.next());
        const auto exported = db.exportAdif();
        const auto originalRecords = decolog::core::adif::parse(exported).records;
        QCOMPARE(originalRecords.size(), 2);
        for (const auto &record : originalRecords) {
            QCOMPARE(record.value("APP_DECOLOG_RADIO_NET_SESSION"), sessionUuid);
            QCOMPARE(record.value("APP_DECOLOG_RADIO_NET_CALL"), QStringLiteral("K1AAA"));
        }
        decolog::core::LogDatabase imported;
        QVERIFY(imported.open(":memory:"));
        QCOMPARE(imported.importAdif(exported).inserted, 1); // identical contacts are deduplicated on import
        const auto records = decolog::core::adif::parse(imported.exportAdif()).records;
        QCOMPARE(records.size(), 1);
        for (const auto &record : records) {
            QCOMPARE(record.value("APP_DECOLOG_RADIO_NET_SESSION"), sessionUuid);
            QCOMPARE(record.value("APP_DECOLOG_RADIO_NET_CALL"), QStringLiteral("K1AAA"));
        }
    }

    void radioNetNavigatesReportedQsoHistory()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        {
            decolog::core::LogDatabase db;
            QVERIFY(db.open(dir.filePath("test.sqlite")));
            decolog::core::RadioNet net(&db);
            QVERIFY(net.start("QSO history"));
            QSqlQuery q(db.connection());
            QVERIFY(q.exec("SELECT uuid FROM radio_net_session"));
            QVERIFY(q.next());
            const QString uuid = q.value(0).toString();
            for (int i = 0; i < 202; ++i) {
                decolog::core::AdifRecord r{{"CALL", "K1AAA"}, {"QSO_DATE", "20260101"}, {"TIME_ON", "120000"},
                                             {"BAND", "20m"}, {"MODE", "CW"}, {"APP_DECOLOG_RADIO_NET_SESSION", uuid}};
                QCOMPARE(db.insertQso(r, "manual", {}, true, 0, false).status, decolog::core::InsertResult::Status::Inserted);
            }
        }
        QByteArray output;
        QVERIFY2(launch(dir, "radionet:qsohistory", output), output.constData());
        QVERIFY2(output.contains("PROBE qsoHistoryFirst count=200 older=true"), output.constData());
        QVERIFY2(output.contains("PROBE qsoHistoryOlder count=2 newer=true older=false"), output.constData());
        QVERIFY2(output.contains("PROBE qsoHistoryReturn count=200 newer=false"), output.constData());
    }

    void radioNetPreparesQsoWithoutSavingOrOverwritingDraft()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QByteArray output;
        QVERIFY2(launch(dir, QStringLiteral("radionet:qso"), output), output.constData());
        QVERIFY2(output.contains("PROBE radioNetQso visible=true call=K1AAA"), output.constData());
        QVERIFY2(output.contains("PROBE radioNetDraft preserved=true notice=true"), output.constData());
        QVERIFY2(output.contains("PROBE radioNetSummary participants=2 present=2 checkIns=2"), output.constData());
        decolog::core::LogDatabase db;
        QVERIFY(db.open(dir.filePath(QStringLiteral("test.sqlite"))));
        QCOMPARE(db.qsoCount(), 0);
        QSqlQuery q(db.connection());
        QVERIFY(q.exec("SELECT COUNT(*) FROM radio_net_member"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 2);
    }

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
