#include "core/LogDatabase.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSqlQuery>
#include <QSqlError>
#include <QTimeZone>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUdpSocket>

using namespace decolog::core;

class TestBulkResponsiveness : public QObject {
    Q_OBJECT
    static bool deniedProbe(const QTemporaryDir& dir, const QString& testRoot, bool testFlag, QByteArray& output)
    {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("QT_QPA_PLATFORM", "offscreen"); env.insert("QT_QUICK_BACKEND", "software");
        env.insert("DECODXLOG_DATA_ROOT", testRoot);
        if (testFlag) env.insert("DECODXLOG_BULK_TEST_MODE", "1");
        else env.remove("DECODXLOG_BULK_TEST_MODE");
        QProcess app; app.setProcessEnvironment(env); app.setProcessChannelMode(QProcess::MergedChannels);
        app.start(QStringLiteral(DECOLOG_APP), {"--software-renderer", "--settings", dir.filePath("settings"),
                  "--db", dir.filePath("log.sqlite"), "--port", "0", "--show", "bulkcheck:delete"});
        if (!app.waitForStarted(10000) || !app.waitForFinished(20000)) { app.kill(); app.waitForFinished(5000); return false; }
        output = app.readAll();
        return app.exitStatus() == QProcess::NormalExit && app.exitCode() == 0
               && output.contains("PROBE bulkcheck denied");
    }
    static bool runProbe(const QTemporaryDir& dir, const QString& mode, QByteArray& output,
                         qint64& maxGap, qint64& elapsed, int& rows)
    {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("QT_QPA_PLATFORM", "offscreen");
        env.insert("QT_QUICK_BACKEND", "software");
        env.insert("DECODXLOG_DATA_ROOT", dir.path());
        env.insert("DECODXLOG_BULK_TEST_MODE", "1");
        env.insert("DECODXLOG_TRACE_ACTIVITY", "1");
        QProcess app;
        app.setProcessEnvironment(env);
        app.setProcessChannelMode(QProcess::MergedChannels);
        app.start(QStringLiteral(DECOLOG_APP), {
            "--software-renderer", "--settings", dir.filePath("settings"),
            "--db", dir.filePath("log.sqlite"), "--port", "0", "--show", "bulkcheck:" + mode
        });
        if (!app.waitForStarted(10000)) { output = app.errorString().toUtf8(); return false; }
        QElapsedTimer deadline; deadline.start();
        qint64 nextProgress = 15000;
        while (app.state() != QProcess::NotRunning && deadline.elapsed() < 300000) {
            app.waitForFinished(1000);
            if (deadline.elapsed() >= nextProgress) {
                qInfo() << "waiting for" << mode << "elapsed ms" << deadline.elapsed();
                nextProgress += 15000;
            }
        }
        if (app.state() != QProcess::NotRunning) {
            app.kill(); app.waitForFinished(5000);
            output = "Timed out awaiting bulk operation\n" + app.readAll();
            return false;
        }
        output = app.readAll();
        if (mode == QLatin1String("export-close") || mode == QLatin1String("bulk-close")) {
            rows = 1; maxGap = 0; elapsed = deadline.elapsed();
            return app.exitStatus() == QProcess::NormalExit && app.exitCode() == 0
                   && output.contains("PROBE " + mode.toUtf8() + " started=true");
        }
        const QString text = QString::fromUtf8(output);
        const QRegularExpression regex(QStringLiteral("PROBE bulkcheck mode=%1 rows=(\\d+) maxgap=(\\d+) elapsed=(\\d+)").arg(mode));
        const auto match = regex.match(text);
        if (!match.hasMatch() || output.contains("PROBE bulkcheck error=")
            || app.exitStatus() != QProcess::NormalExit || app.exitCode() != 0)
            return false;
        rows = match.captured(1).toInt(); maxGap = match.captured(2).toLongLong(); elapsed = match.captured(3).toLongLong();
        return true;
    }

private slots:
    void reviewedDuplicateMerge()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); QFile marker(dir.filePath(".decodxlog-bulk-test")); QVERIFY(marker.open(QIODevice::WriteOnly)); marker.write("synthetic-test-only\n"); marker.close();
        { LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
          for (const auto* call:{"K1AAA","K1AAA","K1BBB","K1BBB"}) {
              const auto r=db.insertQso({{"CALL",call},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}},"fixture",{},true,0,false); QVERIFY(r.id>0);
          } }
        QByteArray output; qint64 gap=0,elapsed=0; int rows=0;
        QVERIFY2(runProbe(dir,"merge",output,gap,elapsed,rows),output.constData()); QCOMPARE(rows,4);
        LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite"))); QCOMPARE(db.qsoCount(),qint64(2));
        QVERIFY(db.meta(2)->deleted); QVERIFY(db.meta(4)->deleted);
    }
    void callsignSearchAndBulkPaperConfirmation()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); QFile marker(dir.filePath(".decodxlog-bulk-test")); QVERIFY(marker.open(QIODevice::WriteOnly)); marker.write("synthetic-test-only\n"); marker.close();
        { LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
          for (int i=0;i<32;++i) { const QString time=QString("%1%2").arg(i/60,2,10,QLatin1Char('0')).arg(i%60,2,10,QLatin1Char('0'));
              const auto r=db.insertQso({{"CALL","J51A"},{"QSO_DATE","20260228"},{"TIME_ON",time},{"BAND","20m"},{"MODE","CW"}},"fixture",{},false,0,false); QVERIFY(r.id>0); }
          QVERIFY(db.insertQso({{"CALL","ZY0SAT"},{"QSO_DATE","20260301"},{"TIME_ON","2035"},{"BAND","20m"},{"MODE","SSB"},{"GRIDSQUARE","HJ51AM"}},"fixture").id>0);
          QVERIFY(db.insertQso({{"CALL","J51ABC"},{"QSO_DATE","20260301"},{"TIME_ON","2036"},{"BAND","20m"},{"MODE","SSB"}},"fixture").id>0); }
        QByteArray output; qint64 maxGap=0,elapsed=0; int rows=0; QVERIFY2(runProbe(dir,"paperqsl",output,maxGap,elapsed,rows),output.constData()); QCOMPARE(rows,32);
        QVERIFY2(output.contains("PROBE bulk result Changed: 32") || output.contains("PROBE bulk result Modificati: 32"),output.constData());
        LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite"))); QSqlQuery q(db.connection());
        QVERIFY(q.exec("SELECT COUNT(*) FROM qso JOIN qsl_status s ON s.qso_id=qso.id AND s.service='card' WHERE qso.call='J51A' AND s.rcvd='Y'")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),32);
        const auto other=db.record(db.latestIds("ZY0SAT",1).first()); QVERIFY(other); QVERIFY(other->value("QSL_RCVD").isEmpty());
        const auto similar=db.record(db.latestIds("J51ABC",1).first()); QVERIFY(similar); QVERIFY(similar->value("QSL_RCVD").isEmpty());
        db.close(); QByteArray again; QVERIFY2(runProbe(dir,"paperqsl",again,maxGap,elapsed,rows),again.constData()); QCOMPARE(rows,32);
        QVERIFY2((again.contains("Changed: 0") && again.contains("unchanged: 32")) || (again.contains("Modificati: 0") && again.contains("invariati: 32")),again.constData());
    }
    void paperQslBulkEventJournalAB()
    {
        for (int enabled=0; enabled<2; ++enabled) {
            QTemporaryDir dir; QVERIFY(dir.isValid());
            QFile marker(dir.filePath(".decodxlog-bulk-test")); QVERIFY(marker.open(QIODevice::WriteOnly)); marker.write("synthetic-test-only\n"); marker.close();
            { LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
              for (int i=0;i<32;++i) { const QString time=QString("%1%2").arg(i/60,2,10,QLatin1Char('0')).arg(i%60,2,10,QLatin1Char('0'));
                  QVERIFY(db.insertQso({{"CALL","J51A"},{"QSO_DATE","20260228"},{"TIME_ON",time},{"BAND","20m"},{"MODE","CW"}},"fixture",{},false,0,false).id>0); }
              QVERIFY(db.insertQso({{"CALL","ZY0SAT"},{"QSO_DATE","20260301"},{"TIME_ON","2035"},{"BAND","20m"},{"MODE","SSB"},{"GRIDSQUARE","HJ51AM"}},"fixture").id>0);
              QSqlQuery q(db.connection());
              if (!enabled) { QVERIFY(q.exec("DROP TRIGGER extension_qsl_insert")); QVERIFY(q.exec("DROP TRIGGER extension_qsl_update")); } }
            QByteArray output; qint64 gap=0,elapsed=0; int rows=0;
            for (int attempt=0;attempt<2;++attempt) {
                QVERIFY2(runProbe(dir,"paperqsl",output,gap,elapsed,rows),output.constData()); QCOMPARE(rows,32);
                LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite"))); QSqlQuery q(db.connection());
                QVERIFY(q.exec("SELECT COUNT(*) FROM qsl_status WHERE service='card' AND rcvd='Y'")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),32);
                QVERIFY(q.exec("SELECT COUNT(*) FROM extension_qso_event WHERE kind='confirmed' AND service='card'")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),enabled ? 32 : 0);
                QCOMPARE(db.qsoCount(),qint64(33)); const auto other=db.record(db.latestIds("ZY0SAT",1).first()); QVERIFY(other); QVERIFY(other->value("QSL_RCVD").isEmpty());
                qInfo() << "QSL_EVENT_AB enabled=" << enabled << "attempt=" << attempt << "max_gap_ms=" << gap << "elapsed_ms=" << elapsed;
            }
        }
    }

    void paperQslBulkEventsLargeAB()
    {
        if (!qEnvironmentVariableIsSet("DECODXLOG_BENCH_BULK_QSL_EVENTS")) QSKIP("opt-in 20k QSL event comparison");
        QTemporaryDir seed; QVERIFY(seed.isValid()); const QString seedPath=seed.filePath("seed.sqlite");
        { LogDatabase db; QVERIFY(db.open(seedPath)); QVERIFY(db.connection().transaction()); QSqlQuery q(db.connection());
          QVERIFY(q.prepare("INSERT INTO qso(uuid,call,qso_datetime_on,band,mode,created_at,updated_at) VALUES(?,'J51A',?,'20m','CW','2026-02-28T00:00:00Z','2026-02-28T00:00:00Z')"));
          for (int i=0;i<20000;++i) { q.bindValue(0,QStringLiteral("fixture-%1").arg(i)); q.bindValue(1,QDateTime(QDate(2026,2,28),QTime(0,0),QTimeZone::UTC).addSecs(i).toString(Qt::ISODate)); QVERIFY2(q.exec(),qPrintable(q.lastError().text())); }
          QVERIFY(db.connection().commit());
          QVERIFY(db.insertQso({{"CALL","ZY0SAT"},{"QSO_DATE","20260301"},{"TIME_ON","2035"},{"BAND","20m"},{"MODE","SSB"},{"GRIDSQUARE","HJ51AM"}},"fixture").id>0);
          QVERIFY(q.exec("PRAGMA wal_checkpoint(TRUNCATE)")); }
        for (int enabled=0;enabled<2;++enabled) {
            QTemporaryDir dir; QVERIFY(dir.isValid()); QVERIFY(QFile::copy(seedPath,dir.filePath("log.sqlite")));
            QFile marker(dir.filePath(".decodxlog-bulk-test")); QVERIFY(marker.open(QIODevice::WriteOnly)); marker.write("synthetic-test-only\n"); marker.close();
            if (!enabled) { LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite"))); QSqlQuery q(db.connection());
                QVERIFY(q.exec("DROP TRIGGER extension_qsl_insert")); QVERIFY(q.exec("DROP TRIGGER extension_qsl_update")); }
            QByteArray output; qint64 gap=0,elapsed=0; int rows=0;
            QVERIFY2(runProbe(dir,"paperqsl",output,gap,elapsed,rows),output.constData()); QCOMPARE(rows,20000);
            { LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite"))); QSqlQuery q(db.connection());
              QVERIFY(q.exec("SELECT COUNT(*) FROM qsl_status WHERE service='card' AND rcvd='Y'")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),20000);
              QVERIFY(q.exec("SELECT COUNT(*) FROM extension_qso_event WHERE kind='confirmed' AND service='card'")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),enabled?20000:0);
              QCOMPARE(db.qsoCount(),qint64(20001)); const auto other=db.record(db.latestIds("ZY0SAT",1).first()); QVERIFY(other); QVERIFY(other->value("QSL_RCVD").isEmpty()); }
            qInfo() << "QSL_EVENT_LARGE_AB enabled=" << enabled << "rows=" << rows << "max_gap_ms=" << gap << "elapsed_ms=" << elapsed;
        }
    }

    void deepScrollSingleBulkAndNoop()
    {
        if (qEnvironmentVariableIsSet("DECODXLOG_FOCUSED_FIXTURE")) QSKIP("running focused USB checks");
        QTemporaryDir dir; QVERIFY(dir.isValid());
        qInfo() << "synthetic fixture root" << dir.path();
        QFile marker(dir.filePath(".decodxlog-bulk-test"));
        QVERIFY(marker.open(QIODevice::WriteOnly));
        QCOMPARE(marker.write("synthetic-test-only\n"), qint64(20));
        marker.close();
        qint64 deepId = 0;
        {
            LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
            QByteArray adif;
            for (int i = 0; i < 20000; ++i) {
                const QByteArray call = "K" + QByteArray::number(i) + "ABC";
                adif += "<CALL:" + QByteArray::number(call.size()) + ">" + call
                        + "<QSO_DATE:8>20261117<TIME_ON:6>120000<BAND:3>20m<MODE:2>CW<STATION_CALLSIGN:6>K1TEST<EOR>\n";
            }
            QCOMPARE(db.importAdif(adif).inserted, 20000);
            QSqlQuery q(db.connection());
            QVERIFY(q.exec("INSERT INTO qsl_status (qso_id, service, sent, rcvd, remote_id) "
                           "SELECT id, 'crx', 'Y', 'N', '4:' || id FROM qso"));
            QVERIFY(q.exec("INSERT INTO qsl_status (qso_id, service, sent, rcvd) SELECT id, 'eqsl', 'Y', 'N' FROM qso"));
            QVERIFY(q.exec("UPDATE qso SET call=(SELECT call FROM qso WHERE id=1) WHERE id=2"));
            QVERIFY(q.exec("UPDATE qso SET call=(SELECT call FROM qso WHERE id=3) WHERE id=4"));
            QVERIFY(q.exec("SELECT id FROM qso ORDER BY qso_datetime_on DESC,id DESC LIMIT 1 OFFSET 10000"));
            QVERIFY(q.next()); deepId = q.value(0).toLongLong();
            QVERIFY(deepId > 0);
        }
        QByteArray rejected;
        QVERIFY2(deniedProbe(dir, dir.path(), false, rejected), rejected.constData());
        QTemporaryDir unrelatedRoot; QVERIFY(unrelatedRoot.isValid());
        QFile unrelatedMarker(unrelatedRoot.filePath(".decodxlog-bulk-test"));
        QVERIFY(unrelatedMarker.open(QIODevice::WriteOnly)); unrelatedMarker.write("synthetic-test-only\n"); unrelatedMarker.close();
        QVERIFY2(deniedProbe(dir, unrelatedRoot.path(), true, rejected), rejected.constData());
        {
            LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
            QCOMPARE(db.qsoCount(), 20000);
        }
        // Optional A: reproduce the former finishBulk loop on the GUI event
        // thread with a copy of the exact fixture. It deliberately performs
        // 20,000 autocommitted CRX updates; never run it on the operator's log.
        if (qEnvironmentVariableIsSet("DECODXLOG_TEST_BULK_LEGACY_A")) {
            QVERIFY(QFile::copy(dir.filePath("log.sqlite"), dir.filePath("baseline.sqlite")));
            LogDatabase old; QVERIFY(old.open(dir.filePath("baseline.sqlite")));
            QList<qint64> ids;
            QSqlQuery q(old.connection());
            QVERIFY(q.exec("SELECT id FROM qso ORDER BY id"));
            while (q.next()) ids.append(q.value(0).toLongLong());
            QCOMPARE(ids.size(), 20000);
            QElapsedTimer timer; timer.start(); qint64 last = 0, largest = 0; int ticks = 0;
            QTimer heartbeat; heartbeat.setInterval(10);
            connect(&heartbeat, &QTimer::timeout, this, [&] {
                const qint64 now = timer.elapsed();
                largest = qMax(largest, now - last); last = now; ++ticks;
            });
            heartbeat.start(); QTest::qWait(30);
            const qint64 begin = timer.elapsed();
            for (qint64 id : ids) old.queueRemoteEdit(id);
            const qint64 operation = timer.elapsed() - begin;
            QCoreApplication::processEvents();
            qInfo() << "A former finishBulk 20000 SQLite autocommits: elapsed ms" << operation
                    << "event-loop max gap ms" << largest << "heartbeat ticks" << ticks;
            QVERIFY(q.exec("SELECT COUNT(*) FROM qsl_status WHERE service='crx' AND sent='R'")); QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 20000);
        }
        auto verify = [&](int changed, int deepHistory) {
            LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
            QSqlQuery q(db.connection());
            QVERIFY(q.exec("SELECT COUNT(*) FROM qso WHERE comment = 'BULK-TEST'")); QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), changed);
            QVERIFY(q.exec("SELECT COUNT(*) FROM qso_history WHERE reason='bulk'")); QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), changed);
            QVERIFY(q.exec("SELECT COUNT(*) FROM qsl_status WHERE service='crx' AND sent='R' AND remote_id<>''")); QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), changed);
            QVERIFY(q.exec("SELECT COUNT(*) FROM qsl_status WHERE service='eqsl' AND sent='Y'")); QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 20000);
            QCOMPARE(db.record(deepId)->value("COMMENT"), QStringLiteral("BULK-TEST"));
            QCOMPARE(db.history(deepId).size(), deepHistory);
        };
        QByteArray output; qint64 maxGap = 0, elapsed = 0; int rows = 0;
        QVERIFY2(runProbe(dir, "one", output, maxGap, elapsed, rows), output.constData());
        QCOMPARE(rows, 1); qInfo() << "one selected at row 10000: max UI gap ms" << maxGap << "elapsed ms" << elapsed;
        QVERIFY2(maxGap < 3000, output.constData());
        verify(1, 1);
        QVERIFY2(runProbe(dir, "inline", output, maxGap, elapsed, rows), output.constData());
        QCOMPARE(rows, 1); qInfo() << "inline edit at row 10000: max UI gap ms" << maxGap << "elapsed ms" << elapsed;
        QVERIFY2(maxGap < 3000, output.constData());
        {
            LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
            QCOMPARE(db.record(deepId)->value("COMMENT"), QStringLiteral("INLINE-TEST"));
            QCOMPARE(db.history(deepId).size(), 2);
        }
        QVERIFY2(runProbe(dir, "restore", output, maxGap, elapsed, rows), output.constData());
        QCOMPARE(rows, 1); qInfo() << "restore at row 10000: max UI gap ms" << maxGap << "elapsed ms" << elapsed;
        QVERIFY2(maxGap < 3000, output.constData());
        verify(1, 3);
        QVERIFY2(runProbe(dir, "detail", output, maxGap, elapsed, rows), output.constData());
        QCOMPARE(rows, 1); qInfo() << "QSO detail save at row 10000: max UI gap ms" << maxGap;
        QVERIFY2(maxGap < 3000, output.constData());
        {
            LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
            QCOMPARE(db.record(deepId)->value("NAME"), QStringLiteral("DETAIL-TEST"));
            EnrichmentEntry enrichment;
            enrichment.qsoId = deepId; enrichment.field = "NAME"; enrichment.previousValue = "";
            enrichment.value = "DETAIL-TEST"; enrichment.source = "fixture"; enrichment.kind = "received";
            enrichment.acquiredAt = "2026-11-18T12:00:00.000Z";
            QVERIFY2(db.recordEnrichment({enrichment}), qPrintable(db.lastError()));
        }
        QVERIFY2(runProbe(dir, "undo", output, maxGap, elapsed, rows), output.constData());
        QCOMPARE(rows, 1); qInfo() << "undo enrichment at row 10000: max UI gap ms" << maxGap;
        QVERIFY2(maxGap < 3000, output.constData());
        {
            LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
            QVERIFY(db.record(deepId)->value("NAME").isEmpty());
            QVERIFY(db.enrichments(deepId).isEmpty());
        }
        QFile fixture(dir.filePath("fixture.pdf")); QVERIFY(fixture.open(QIODevice::WriteOnly));
        QCOMPARE(fixture.write("%PDF-1.4\n% synthetic fixture\n"), qint64(29)); fixture.close();
        QVERIFY2(runProbe(dir, "attachment", output, maxGap, elapsed, rows), output.constData());
        QCOMPARE(rows, 1); qInfo() << "attach QSL at row 10000: max UI gap ms" << maxGap;
        QVERIFY2(maxGap < 3000, output.constData());
        {
            LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
            const auto attachments = db.qslAttachments(deepId);
            QCOMPARE(attachments.size(), 1);
            QCOMPARE(db.qslAttachmentData(attachments.first().id), QByteArray("%PDF-1.4\n% synthetic fixture\n"));
        }
        QVERIFY2(runProbe(dir, "export", output, maxGap, elapsed, rows), output.constData());
        QCOMPARE(rows, 1); qInfo() << "export one QSO at row 10000: max UI gap ms" << maxGap;
        QVERIFY2(maxGap < 3000, output.constData());
        QFile exported(dir.filePath("one.adi")); QVERIFY(exported.open(QIODevice::ReadOnly));
        const QByteArray exportedData = exported.readAll();
        QVERIFY(exportedData.toUpper().contains("<EOR>"));
        const auto exportedDoc = adif::parse(exportedData);
        QCOMPARE(exportedDoc.records.size(), 1);
        QVERIFY(!exportedDoc.records.first().value("CALL").isEmpty());
        QFile existing(dir.filePath("close.adi")); QVERIFY(existing.open(QIODevice::WriteOnly));
        existing.write("previous export"); existing.close();
        QVERIFY2(runProbe(dir, "export-close", output, maxGap, elapsed, rows), output.constData());
        QVERIFY(existing.open(QIODevice::ReadOnly));
        const QByteArray closedData = existing.readAll();
        QVERIFY(closedData.toUpper().contains("<EOR>"));
        QCOMPARE(adif::parse(closedData).records.size(), 1); existing.close();
        qInfo() << "accepted ADIF export completes when application closes immediately";
        QVERIFY(QDir(dir.path()).mkdir("blocked"));
        QFile sentinel(dir.filePath("blocked/keep.txt")); QVERIFY(sentinel.open(QIODevice::WriteOnly));
        sentinel.write("keep"); sentinel.close();
        QVERIFY2(runProbe(dir, "export-fail", output, maxGap, elapsed, rows), output.constData());
        QCOMPARE(rows, 1); QVERIFY2(maxGap < 3000, output.constData());
        QVERIFY(sentinel.open(QIODevice::ReadOnly)); QCOMPARE(sentinel.readAll(), QByteArray("keep")); sentinel.close();
        struct StatusCase { QString probe; QString service; bool received; bool initiallySent; };
        const QList<StatusCase> statuses{
            {"eqsl", "eqsl", false, true}, {"qrz", "qrz", false, false},
            {"club", "clublog", false, false}, {"lotw", "lotw", false, false},
            {"hrd", "hrdlog", false, false}, {"card", "card", false, false},
            {"eqslrcvd", "eqsl", true, false}, {"lotwrcvd", "lotw", true, false},
            {"qrzrcvd", "qrz", true, false}, {"cardrcvd", "card", true, false}};
        int statusRevisions = 0;
        for (const auto& status : statuses) {
            const QStringList values = status.initiallySent ? QStringList{"N", "Y"} : QStringList{"Y", "N", "Y"};
            for (const QString& state : values) {
                const QString mode = "qsl-" + status.probe + "-" + state;
                QVERIFY2(runProbe(dir, mode, output, maxGap, elapsed, rows), output.constData());
                QCOMPARE(rows, 1); qInfo() << mode << "at row 10000: max UI gap ms" << maxGap;
                QVERIFY2(maxGap < 3000, output.constData());
                LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
                QString actual = QStringLiteral("N");
                for (const QslState& qsl : db.qslStatus(deepId))
                    if (qsl.service == status.service) actual = status.received ? qsl.rcvd : qsl.sent;
                QCOMPARE(actual, state);
                ++statusRevisions;
            }
        }
        QVERIFY2(runProbe(dir, "all", output, maxGap, elapsed, rows), output.constData());
        QCOMPARE(rows, 20000); qInfo() << "20000-contact edit: max UI gap ms" << maxGap << "elapsed ms" << elapsed;
        QVERIFY2(maxGap < 3000, output.constData());
        verify(20000, 5 + statusRevisions);
        QUdpSocket receiver; QVERIFY(receiver.bind(QHostAddress::LocalHost, 0));
        {
            QSettings settings(dir.filePath("settings/Decodium/DecoDXLog.ini"), QSettings::IniFormat);
            settings.setValue("ui/language", "en");
            settings.setValue("udp/forward", QStringLiteral("127.0.0.1:%1").arg(receiver.localPort()));
            settings.sync();
        }
        QVERIFY2(runProbe(dir, "udp", output, maxGap, elapsed, rows), output.constData());
        QCOMPARE(rows, 20000); qInfo() << "resend 20000 QSO to localhost UDP: max UI gap ms" << maxGap << "elapsed ms" << elapsed;
        QVERIFY2(maxGap < 3000, output.constData());
        QVERIFY2(output.contains("Resent 20000 QSO via UDP"), output.constData());
        QVERIFY(receiver.hasPendingDatagrams() || receiver.waitForReadyRead(5000));
        QByteArray datagram; datagram.resize(int(receiver.pendingDatagramSize()));
        QVERIFY(receiver.readDatagram(datagram.data(), datagram.size()) > 0);
        QVERIFY(datagram.contains("<contactinfo>"));
        QVERIFY(datagram.contains("<mycall>K1TEST</mycall>"));
        {
            QSettings settings(dir.filePath("settings/Decodium/DecoDXLog.ini"), QSettings::IniFormat);
            settings.setValue("callbook/provider", "hamqth"); settings.sync();
        }
        QVERIFY2(runProbe(dir, "callbookscan", output, maxGap, elapsed, rows), output.constData());
        QCOMPARE(rows, 1); qInfo() << "callbook scan cancellation on 20k: max UI gap ms" << maxGap;
        QVERIFY2(maxGap < 3000, output.constData());
        {
            QSettings settings(dir.filePath("settings/Decodium/DecoDXLog.ini"), QSettings::IniFormat);
            settings.setValue("callbook/provider", "off"); settings.sync();
        }
        QVERIFY2(runProbe(dir, "noop", output, maxGap, elapsed, rows), output.constData());
        QCOMPARE(rows, 20000); qInfo() << "20000-contact no-op: max UI gap ms" << maxGap << "elapsed ms" << elapsed;
        QVERIFY2(maxGap < 3000, output.constData());
        verify(20000, 5 + statusRevisions);
        const QList<QPair<QString, QPair<QString, QString>>> inlineFields{
            {"call", {"CALL", "K1NEW"}}, {"rst_sent", {"RST_SENT", "58"}}, {"rst_rcvd", {"RST_RCVD", "57"}},
            {"grid", {"GRIDSQUARE", "JN58AA"}}, {"name", {"NAME", "INLINE-NAME"}},
            {"qth", {"QTH", "INLINE-QTH"}}, {"comment", {"COMMENT", "INLINE-COMMENT"}}};
        for (const auto& field : inlineFields) {
            const QString mode = "inline-" + field.first;
            QVERIFY2(runProbe(dir, mode, output, maxGap, elapsed, rows), output.constData());
            QCOMPARE(rows, 1); qInfo() << mode << "at row 10000: max UI gap ms" << maxGap;
            QVERIFY2(maxGap < 3000, output.constData());
            LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
            QCOMPARE(db.record(deepId)->value(field.second.first), field.second.second);
        }
        int revision = 0;
        { LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite"))); revision = db.meta(deepId)->revision; }
        QVERIFY2(runProbe(dir, "inline-bad", output, maxGap, elapsed, rows), output.constData());
        QVERIFY2(maxGap < 3000, output.constData());
        { LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite"))); QCOMPARE(db.meta(deepId)->revision, revision); }
        QVERIFY2(runProbe(dir, "filtered", output, maxGap, elapsed, rows), output.constData());
        QVERIFY(rows > 0 && rows < 20000);
        qInfo() << "filtered and sorted bulk edit, rows" << rows << "max UI gap ms" << maxGap << "elapsed ms" << elapsed;
        QVERIFY2(maxGap < 3000, output.constData());
        {
            LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
            QSqlQuery q(db.connection());
            QVERIFY(q.exec("SELECT COUNT(*) FROM qso WHERE notes='FILTER-TEST'")); QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), rows);
        }
        int beforeDxcc = 0;
        {
            LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
            QSqlQuery q(db.connection()); QVERIFY(q.exec("SELECT COUNT(*) FROM qso WHERE dxcc>0")); QVERIFY(q.next());
            beforeDxcc = q.value(0).toInt();
        }
        QVERIFY2(runProbe(dir, "dxcc", output, maxGap, elapsed, rows), output.constData());
        QCOMPARE(rows, 20000); qInfo() << "fill DXCC on 20k contacts: max UI gap ms" << maxGap << "elapsed ms" << elapsed;
        QVERIFY2(maxGap < 3000, output.constData());
        {
            LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
            QSqlQuery q(db.connection()); QVERIFY(q.exec("SELECT COUNT(*) FROM qso WHERE dxcc>0")); QVERIFY(q.next());
            QVERIFY(q.value(0).toInt() > beforeDxcc);
        }
        {
            LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
            QSqlQuery q(db.connection());
            QVERIFY(q.exec("UPDATE qso SET name='Broken<NAME'"));
        }
        QVERIFY2(runProbe(dir, "repair", output, maxGap, elapsed, rows), output.constData());
        QCOMPARE(rows, 20000); qInfo() << "repair imported text on 20000: max UI gap ms" << maxGap << "elapsed ms" << elapsed;
        QVERIFY2(maxGap < 3000, output.constData());
        {
            LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
            QSqlQuery q(db.connection());
            QVERIFY(q.exec("SELECT COUNT(*) FROM qso WHERE name='Broken'")); QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 20000);
            QVERIFY(q.exec("SELECT COUNT(*) FROM qso_history WHERE reason='repair'")); QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 20000);
            QVERIFY(q.exec("SELECT COUNT(*) FROM qsl_status WHERE service='eqsl' AND sent='Y'")); QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 20000);
        }
        QVERIFY2(runProbe(dir, "tag", output, maxGap, elapsed, rows), output.constData());
        QCOMPARE(rows, 20000); qInfo() << "tag 20000 contacts: max UI gap ms" << maxGap << "elapsed ms" << elapsed;
        QVERIFY2(maxGap < 3000, output.constData());
        {
            LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
            QSqlQuery q(db.connection());
            QVERIFY(q.exec("SELECT COUNT(*) FROM qso WHERE tags='bulk-test'")); QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 20000);
            QVERIFY(q.exec("SELECT COUNT(*) FROM qso_history WHERE reason='tag'")); QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 20000);
        }
        QVERIFY2(runProbe(dir, "tagremove", output, maxGap, elapsed, rows), output.constData());
        QCOMPARE(rows, 20000); qInfo() << "remove tags from 20000 contacts: max UI gap ms" << maxGap << "elapsed ms" << elapsed;
        QVERIFY2(maxGap < 3000, output.constData());
        {
            LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
            QSqlQuery q(db.connection());
            QVERIFY(q.exec("SELECT COUNT(*) FROM qso WHERE IFNULL(tags,'')=''")); QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 20000);
        }
        QFile importFile(dir.filePath("import.adi")); QVERIFY(importFile.open(QIODevice::WriteOnly));
        const QByteArray importRow = "<CALL:7>K1NEWZZ<QSO_DATE:8>20000101<TIME_ON:6>120000<BAND:3>20m<MODE:2>CW<EOR>\n";
        QCOMPARE(importFile.write(importRow + importRow), qint64(importRow.size() * 2)); importFile.close();
        QVERIFY2(runProbe(dir, "import", output, maxGap, elapsed, rows), output.constData());
        QCOMPARE(rows, 2); qInfo() << "preview and import with duplicate in 20k log: max UI gap ms" << maxGap << "elapsed ms" << elapsed;
        QVERIFY2(maxGap < 3000, output.constData());
        {
            LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
            QCOMPARE(db.qsoCount(), 20001);
            QSqlQuery q(db.connection());
            QVERIFY(q.exec("SELECT COUNT(*) FROM qso WHERE call='K1NEWZZ' AND deleted=0")); QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 1);
        }
        QVERIFY2(runProbe(dir, "merge", output, maxGap, elapsed, rows), output.constData());
        QCOMPARE(rows, 4); qInfo() << "merge two duplicate pairs in 20k: max UI gap ms" << maxGap;
        QVERIFY2(maxGap < 3000, output.constData());
        {
            LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
            QCOMPARE(db.qsoCount(), 19999);
            QVERIFY(db.meta(2)->deleted); QVERIFY(db.meta(4)->deleted);
        }
        QVERIFY2(runProbe(dir, "delete-one", output, maxGap, elapsed, rows), output.constData());
        QCOMPARE(rows, 1); qInfo() << "delete one at row 10000: max UI gap ms" << maxGap;
        QVERIFY2(maxGap < 3000, output.constData());
        {
            const auto selected = QRegularExpression(QStringLiteral("PROBE delete-id=(\\d+)")).match(QString::fromUtf8(output));
            QVERIFY2(selected.hasMatch(), output.constData());
            const qint64 selectedId = selected.captured(1).toLongLong();
            LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
            QVERIFY(db.meta(selectedId)->deleted);
            QCOMPARE(db.qsoCount(), 19998);
            QSqlQuery q(db.connection());
            QVERIFY(q.exec("SELECT COUNT(*) FROM qso WHERE deleted=1")); QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 3);
        }
        {
            QSettings settings(dir.filePath("settings/Decodium/DecoDXLog.ini"), QSettings::IniFormat);
            settings.setValue("callbook/provider", "hamqth"); settings.sync();
        }
        QVERIFY2(runProbe(dir, "bulk-close", output, maxGap, elapsed, rows), output.constData());
        {
            LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
            QSqlQuery q(db.connection());
            QVERIFY(q.exec("SELECT COUNT(*) FROM qso WHERE deleted=0 AND notes='CLOSE-TEST'")); QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 19998);
        }
        {
            QSettings settings(dir.filePath("settings/Decodium/DecoDXLog.ini"), QSettings::IniFormat);
            settings.setValue("callbook/provider", "off"); settings.sync();
        }
        qInfo() << "accepted 19998-QSO bulk edit completes after immediate application close";
        QVERIFY2(runProbe(dir, "manual", output, maxGap, elapsed, rows), output.constData());
        QCOMPARE(rows, 1); QVERIFY2(maxGap < 3000, output.constData());
        QVERIFY2(output.contains("PROBE manual-model-count=19999"), output.constData());
        {
            LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
            QCOMPARE(db.qsoCount(), 19999);
            QSqlQuery q(db.connection());
            QVERIFY(q.exec("SELECT COUNT(*) FROM qso WHERE call='K1MANUAL' AND deleted=0")); QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 1);
        }
        qInfo() << "manual new QSO in ~20k log, no duplicate visible: max UI gap ms" << maxGap;
        QVERIFY2(runProbe(dir, "delete", output, maxGap, elapsed, rows), output.constData());
        QCOMPARE(rows, 19999); qInfo() << "delete 19999 remaining contacts: max UI gap ms" << maxGap << "elapsed ms" << elapsed;
        QVERIFY2(maxGap < 3000, output.constData());
        {
            LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
            QSqlQuery q(db.connection());
            QVERIFY(q.exec("SELECT COUNT(*) FROM qso WHERE deleted=1")); QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 20002);
            QVERIFY(q.exec("SELECT COUNT(*) FROM qso_history WHERE reason='delete'")); QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 20002);
            QVERIFY(q.exec("SELECT COUNT(*) FROM qsl_status WHERE service='crx' AND sent='D' AND remote_id<>''")); QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 20000);
        }
    }

    void focusedFinalOnCopiedFixture()
    {
        const QString fixture = qEnvironmentVariable("DECODXLOG_FOCUSED_FIXTURE");
        if (fixture.isEmpty()) QSKIP("set DECODXLOG_FOCUSED_FIXTURE to an isolated synthetic SQLite fixture");
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QFile marker(dir.filePath(".decodxlog-bulk-test")); QVERIFY(marker.open(QIODevice::WriteOnly));
        QCOMPARE(marker.write("synthetic-test-only\n"), qint64(20)); marker.close();
        QVERIFY(QFile::copy(fixture, dir.filePath("log.sqlite")));
        QByteArray output; qint64 maxGap = 0, elapsed = 0; int rows = 0;
        QVERIFY2(runProbe(dir, "one", output, maxGap, elapsed, rows), output.constData());
        QCOMPARE(rows, 1); QVERIFY(maxGap < 3000); qInfo() << "focused one edit gap" << maxGap;
        QVERIFY2(runProbe(dir, "all", output, maxGap, elapsed, rows), output.constData());
        QCOMPARE(rows, 20000); QVERIFY(maxGap < 3000); qInfo() << "focused 20k edit gap" << maxGap << "elapsed" << elapsed;
        {
            LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite"))); QSqlQuery q(db.connection());
            QVERIFY(q.exec("SELECT COUNT(*) FROM qso WHERE comment='BULK-TEST'")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 20000);
        }
        QVERIFY2(runProbe(dir, "export-close", output, maxGap, elapsed, rows), output.constData());
        QFile exported(dir.filePath("close.adi")); QVERIFY(exported.open(QIODevice::ReadOnly));
        const QByteArray content = exported.readAll(); QVERIFY(content.toUpper().contains("<EOR>"));
        QCOMPARE(adif::parse(content).records.size(), 1);
        {
            QSettings settings(dir.filePath("settings/Decodium/DecoDXLog.ini"), QSettings::IniFormat);
            settings.setValue("callbook/provider", "hamqth"); settings.sync();
        }
        QVERIFY2(runProbe(dir, "bulk-close", output, maxGap, elapsed, rows), output.constData());
        {
            LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite"))); QSqlQuery q(db.connection());
            QVERIFY(q.exec("SELECT COUNT(*) FROM qso WHERE notes='CLOSE-TEST'")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 20000);
        }
        qInfo() << "focused accepted bulk edit and ADIF export complete after immediate shutdown";
        QVERIFY2(runProbe(dir, "tag", output, maxGap, elapsed, rows), output.constData());
        QCOMPARE(rows, 20000); QVERIFY(maxGap < 3000); qInfo() << "focused tag 20k gap" << maxGap << "elapsed" << elapsed;
        {
            LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite"))); QSqlQuery q(db.connection());
            QVERIFY(q.exec("SELECT COUNT(*) FROM qso WHERE tags='bulk-test'")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 20000);
        }
        QVERIFY2(runProbe(dir, "tagremove", output, maxGap, elapsed, rows), output.constData());
        QCOMPARE(rows, 20000); QVERIFY(maxGap < 3000); qInfo() << "focused untag 20k gap" << maxGap << "elapsed" << elapsed;
        {
            LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite"))); QSqlQuery q(db.connection());
            QVERIFY(q.exec("SELECT COUNT(*) FROM qso WHERE IFNULL(tags,'')=''")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 20000);
        }
        QVERIFY2(runProbe(dir, "delete-one", output, maxGap, elapsed, rows), output.constData());
        const auto selected = QRegularExpression(QStringLiteral("PROBE delete-id=(\\d+)")).match(QString::fromUtf8(output));
        QVERIFY2(selected.hasMatch(), output.constData());
        {
            LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
            QVERIFY(db.meta(selected.captured(1).toLongLong())->deleted); QCOMPARE(db.qsoCount(), 19999);
        }
        qInfo() << "focused deep-row deletion gap" << maxGap;
    }
};

QTEST_GUILESS_MAIN(TestBulkResponsiveness)
#include "tst_bulkresponsiveness.moc"
