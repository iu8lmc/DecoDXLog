// Venire da un altro programma: un foglio di calcolo, il database di N1MM
// Logger+, le conferme di QRZ scritte nei campi suoi. E poi mettere in ordine
// quello che e' arrivato: un campo cambiato su molti QSO, i doppioni uniti.
#include "core/LogDatabase.h"
#include "core/LogMigration.h"

#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

using namespace decolog::core;

class TestMigration : public QObject
{
    Q_OBJECT

private slots:
    void cancellingPreviewLeavesSourceUntouched()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        for (int i=0;i<300;++i) QVERIFY(db.insertQso(AdifRecord{{"CALL",QString("K%1AAA").arg(i)},{"QSO_DATE","20261101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}},"fixture",{},true,0,false).id>0);
        const QByteArray incoming="<CALL:5>K1NEW<QSO_DATE:8>20261101<TIME_ON:4>1200<BAND:3>20m<MODE:2>CW<EOR>";
        ImportResult result; QString error; int checks=0;
        QVERIFY(!migration::estimateImport(db,incoming,&result,&error,[&] { return ++checks>130; }));
        QVERIFY(error.contains("cancel",Qt::CaseInsensitive)); QVERIFY(checks>130); QCOMPARE(db.qsoCount(),qint64(300));
        QVERIFY2(migration::estimateImport(db,incoming,&result,&error),qPrintable(error)); QCOMPARE(result.inserted,1); QCOMPARE(db.qsoCount(),qint64(300));
        LogDatabase empty; QVERIFY(empty.open(":memory:")); AdifDocument document;
        for (int i=0;i<300;++i) document.records.append(AdifRecord{{"CALL",QString("W%1XYZ").arg(i)},{"QSO_DATE","20261101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}});
        checks=0; const QByteArray large=adif::writeDocument(document);
        QVERIFY(!migration::estimateImport(empty,large,&result,&error,[&] { return ++checks>40; }));
        QVERIFY(result.cancelled); QCOMPARE(empty.qsoCount(),qint64(0));
        QVERIFY2(migration::estimateImport(empty,large,&result,&error),qPrintable(error)); QCOMPARE(result.inserted,300); QCOMPARE(empty.qsoCount(),qint64(0));
        checks=0; const auto interrupted=empty.importAdif(large,"fixture",0,{},50,[&] { return ++checks>80; });
        QVERIFY(interrupted.cancelled); QCOMPARE(interrupted.inserted,50); QCOMPARE(empty.qsoCount(),qint64(50));
    }
    void estimatePreservesExistingDuplicatesForTimeWindow()
    {
        LogDatabase db; QVERIFY(db.open(":memory:")); db.setDedupWindows(60,60);
        auto add=[&](const char* time) { return db.insertQso(AdifRecord{{"CALL","K1ABC"},{"QSO_DATE","20261101"},{"TIME_ON",time},{"BAND","20m"},{"MODE","CW"}},"fixture",{},true,0,false).id; };
        QVERIFY(add("120000")>0); QVERIFY(add("120100")>0);
        const QByteArray incoming="<CALL:5>K1ABC<QSO_DATE:8>20261101<TIME_ON:6>120200<BAND:3>20m<MODE:2>CW<EOR>";
        LogDatabase oldProbe; QVERIFY(oldProbe.open(":memory:")); oldProbe.setDedupWindows(60,60);
        QCOMPARE(oldProbe.importAdif(db.exportAdif(),"preview").inserted,1);
        QCOMPARE(oldProbe.importAdif(incoming,"preview").inserted,1); // old preview gave the wrong answer
        QString error; ImportResult estimate; QVERIFY2(migration::estimateImport(db,incoming,&estimate,&error),qPrintable(error));
        QCOMPARE(estimate.inserted,0); QCOMPARE(estimate.duplicates,1); QCOMPARE(db.qsoCount(),qint64(2));
        const auto actual=db.importAdif(incoming,"fixture"); QCOMPARE(actual.inserted,estimate.inserted); QCOMPARE(actual.duplicates,estimate.duplicates);
        const QByteArray batch="<CALL:5>K2ABC<QSO_DATE:8>20261102<TIME_ON:4>1200<BAND:3>20m<MODE:2>CW<EOR>"
            "<CALL:5>K2ABC<QSO_DATE:8>20261102<TIME_ON:4>1200<BAND:3>20m<MODE:2>CW<EOR>"
            "<CALL:5>K3ABC<QSO_DATE:8>20261302<TIME_ON:4>1200<BAND:3>20m<MODE:2>CW<EOR>";
        QVERIFY2(migration::estimateImport(db,batch,&estimate,&error),qPrintable(error)); QCOMPARE(db.qsoCount(),qint64(2));
        const auto batchActual=db.importAdif(batch,"fixture"); QCOMPARE(estimate.inserted,batchActual.inserted); QCOMPARE(estimate.duplicates,batchActual.duplicates); QCOMPARE(estimate.invalid,batchActual.invalid);
        QCOMPARE(estimate.inserted,1); QCOMPARE(estimate.duplicates,1); QCOMPARE(estimate.invalid,1);
    }
    void anonymizedVendorMatrixPreservesSemanticAndExtraFields()
    {
        const QString path = QFINDTESTDATA("fixtures/migration_matrix.json");
        QVERIFY2(!path.isEmpty(), "migration fixture matrix is missing");
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject matrix = QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(matrix.value(QStringLiteral("format")).toString(), QStringLiteral("decodxlog-migration-matrix-1"));
        QVERIFY(!matrix.value(QStringLiteral("realVendorExportsVerified")).toBool());
        QCOMPARE(matrix.value(QStringLiteral("sourceEvidence")).toString(), QStringLiteral("synthetic-anonymized-fixture"));
        QCOMPARE(matrix.value(QStringLiteral("verificationScope")).toString(), QStringLiteral("parser-preservation-only"));
        const QJsonArray cases = matrix.value(QStringLiteral("cases")).toArray();
        QCOMPARE(cases.size(), 3);
        for (const QJsonValue& value : cases) {
            const QJsonObject item = value.toObject();
            QVERIFY(!item.value(QStringLiteral("source")).toString().isEmpty());
            QCOMPARE(item.value(QStringLiteral("evidence")).toString(), QStringLiteral("synthetic-anonymized-fixture"));
            QVERIFY(!item.value(QStringLiteral("version")).toString().isEmpty());
            QVERIFY(item.value(QStringLiteral("mustPreserve")).isArray());
            const QString format = item.value(QStringLiteral("format")).toString();
            QList<AdifRecord> records;
            if (format == QLatin1String("ADIF"))
                records = adif::parse(item.value(QStringLiteral("input")).toString().toUtf8()).records;
            else if (format == QLatin1String("CSV")) {
                QString error;
                records = migration::readCsv(item.value(QStringLiteral("input")).toString().toUtf8(), &error);
                QVERIFY2(error.isEmpty(), qPrintable(error));
            } else {
                QFAIL("unsupported matrix parser format");
            }
            QCOMPARE(records.size(), 1);
            const AdifRecord& record = records.first();
            for (const QJsonValue& field : item.value(QStringLiteral("mustPreserve")).toArray())
                QVERIFY2(record.contains(field.toString()), qPrintable(field.toString()));
            QVERIFY(!record.value(QStringLiteral("CALL")).isEmpty());
            QVERIFY(!record.value(QStringLiteral("QSO_DATE")).isEmpty());
            QVERIFY(!record.value(QStringLiteral("TIME_ON")).isEmpty());
        }
    }

    void aSpreadsheetInItalian()
    {
        // Punto e virgola, virgola decimale, kHz, la data all'italiana.
        const QByteArray csv = "\xEF\xBB\xBFNominativo;Data;Ora;Frequenza;Modo;RST inviato;RST ricevuto;Nome;Locatore\n"
                               "iu8lmc;28/09/2026;12:03;14074,5;ft8;-10;-05;Marco;jn70\n"
                               "\n"
                               "K1ABC;29/09/2026;0915;7,074;FT8;;;\"Rossi, Mario\";\n";
        QString error;
        const QList<AdifRecord> r = migration::readCsv(csv, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(r.size(), 2);
        QCOMPARE(r.at(0).value("CALL"), QString("IU8LMC"));
        QCOMPARE(r.at(0).value("QSO_DATE"), QString("20260928"));
        QCOMPARE(r.at(0).value("TIME_ON"), QString("120300"));
        QCOMPARE(r.at(0).value("FREQ").toDouble(), 14.0745);
        QCOMPARE(r.at(0).value("BAND"), QString("20m"));
        QCOMPARE(r.at(0).value("MODE"), QString("FT8"));
        QCOMPARE(r.at(0).value("RST_SENT"), QString("-10"));
        QCOMPARE(r.at(0).value("RST_RCVD"), QString("-05"));
        QCOMPARE(r.at(0).value("GRIDSQUARE"), QString("JN70"));
        // I MHz restano MHz, e le virgolette tengono insieme nome e cognome.
        QCOMPARE(r.at(1).value("FREQ").toDouble(), 7.074);
        QCOMPARE(r.at(1).value("BAND"), QString("40m"));
        QCOMPARE(r.at(1).value("TIME_ON"), QString("091500"));
        QCOMPARE(r.at(1).value("NAME"), QString("Rossi, Mario"));
        QVERIFY(r.at(1).value("RST_SENT").isEmpty());
    }

    void aSpreadsheetInEnglishWithAdifNames()
    {
        // Data e ora nella stessa colonna, un campo col nome ADIF, un campo
        // che non si conosce (resta fuori).
        const QByteArray csv = "Call,Date,Band,Mode,MY_GRIDSQUARE,Whatever\n"
                               "W1AW,2026-09-28 18:45:10,20M,CW,JN70FU,xyz\n";
        const QList<AdifRecord> r = migration::readCsv(csv);
        QCOMPARE(r.size(), 1);
        QCOMPARE(r.at(0).value("QSO_DATE"), QString("20260928"));
        QCOMPARE(r.at(0).value("TIME_ON"), QString("184510"));
        QCOMPARE(r.at(0).value("BAND"), QString("20M"));
        QCOMPARE(r.at(0).value("MY_GRIDSQUARE"), QString("JN70FU"));
        QVERIFY(r.at(0).value("WHATEVER").isEmpty());

        // Senza una colonna del nominativo non si importa niente, e si dice perche'.
        QString error;
        QVERIFY(migration::readCsv("Date,Band\n2026-09-28,20m\n", &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void anN1mmDatabase()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("contest.s3db");
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "make-n1mm");
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("CREATE TABLE DXLOG (ID TEXT, TS TEXT, Call TEXT, Freq REAL, Mode TEXT, Snt TEXT, "
                           "Rcv TEXT, SntNr INTEGER, RcvNr INTEGER, Exchange1 TEXT, Sect TEXT, Zn INTEGER, "
                           "Name TEXT, Comment TEXT, GridSquare TEXT, Operator TEXT, ContestName TEXT, "
                           "StationPrefix TEXT)"));
            QVERIFY(q.exec("INSERT INTO DXLOG VALUES ('a1b2', '2026-10-24 00:01:02', 'k9zzz', 14025.5, 'CW', "
                           "'599', '599', 1, 0, '', '', 4, '', '', '', 'iu8lmc', 'CQ-WW-CW', 'IU8LMC')"));
            QVERIFY(q.exec("INSERT INTO DXLOG VALUES ('c3d4', '2026-10-24 00:03:00', 'DL1AA', 7150, 'LSB', "
                           "'59', '59', 2, 15, 'DARC', '', 14, 'Hans', 'bello', 'jo62', 'IU8LMC', 'CQ-WW-CW', "
                           "'IU8LMC')"));
            db.close();
        }
        QSqlDatabase::removeDatabase("make-n1mm");

        QString error, kind;
        const QByteArray adif = migration::asAdif(path, &error, &kind);
        QVERIFY2(!adif.isEmpty(), qPrintable(error));
        QCOMPARE(kind, QString("n1mm"));
        const AdifDocument doc = adif::parse(adif);
        QCOMPARE(doc.records.size(), 2);
        const AdifRecord first = doc.records.at(0);
        QCOMPARE(first.value("CALL"), QString("K9ZZZ"));
        QCOMPARE(first.value("QSO_DATE"), QString("20261024"));
        QCOMPARE(first.value("TIME_ON"), QString("000102"));
        QCOMPARE(first.value("FREQ").toDouble(), 14.0255);
        QCOMPARE(first.value("BAND"), QString("20m"));
        QCOMPARE(first.value("STX"), QString("1"));
        QVERIFY(first.value("SRX").isEmpty());          // lo 0 di N1MM non e' un numero
        QCOMPARE(first.value("CQZ"), QString("4"));
        QCOMPARE(first.value("CONTEST_ID"), QString("CQ-WW-CW"));
        QCOMPARE(first.value("APP_N1MM_ID"), QString("a1b2"));
        QVERIFY(first.value("NAME").isEmpty());
        const AdifRecord second = doc.records.at(1);
        QCOMPARE(second.value("MODE"), QString("SSB"));
        QCOMPARE(second.value("SUBMODE"), QString("LSB"));
        QCOMPARE(second.value("SRX"), QString("15"));
        QCOMPARE(second.value("SRX_STRING"), QString("DARC"));
        QCOMPARE(second.value("GRIDSQUARE"), QString("JO62"));

        // Un database che non e' di N1MM.
        const QString other = dir.filePath("other.db");
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "make-other");
            db.setDatabaseName(other);
            QVERIFY(db.open());
            QSqlQuery(db).exec("CREATE TABLE t (x INTEGER)");
            db.close();
        }
        QSqlDatabase::removeDatabase("make-other");
        error.clear();
        QVERIFY(migration::asAdif(other, &error).isEmpty());
        QVERIFY(error.contains("N1MM"));
    }

    void anAdifFileStaysAsItIs()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("log.adi");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        const QByteArray data = "<CALL:4>W1AW <QSO_DATE:8>20260928 <TIME_ON:4>1200 <BAND:3>20m <MODE:2>CW <EOR>\n";
        f.write(data);
        f.close();
        QString kind;
        QCOMPARE(migration::asAdif(path, nullptr, &kind), data);
        QCOMPARE(kind, QString("adif"));
        QString error;
        QVERIFY(migration::asAdif(dir.filePath("missing.csv"), &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void qrzConfirmationsSurviveTheImport()
    {
        AdifRecord r{{"CALL", "W1AW"}, {"APP_QRZLOG_STATUS", "C"}, {"APP_QRZLOG_QSLDATE", "20260101"}};
        migration::mapProgramFields(r);
        QCOMPARE(r.value("QRZCOM_QSO_DOWNLOAD_STATUS"), QString("Y"));
        QCOMPARE(r.value("QRZCOM_QSO_DOWNLOAD_DATE"), QString("20260101"));
        // Non confermato: niente.
        AdifRecord n{{"CALL", "W1AW"}, {"APP_QRZLOG_STATUS", "N"}};
        migration::mapProgramFields(n);
        QVERIFY(n.value("QRZCOM_QSO_DOWNLOAD_STATUS").isEmpty());

        // E importando l'ADIF del logbook di QRZ la conferma arriva nel log.
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        const ImportResult result = db.importAdif(
            "<CALL:4>W1AW <QSO_DATE:8>20251231 <TIME_ON:4>1200 <BAND:3>20m <MODE:2>CW "
            "<APP_QRZLOG_STATUS:1>C <APP_QRZLOG_QSLDATE:8>20260101 <EOR>\n");
        QCOMPARE(result.inserted, 1);
        const QList<qint64> ids = db.latestIds("W1AW", 1);
        QCOMPARE(ids.size(), 1);
        bool found = false;
        for (const QslState& s : db.qslStatus(ids.first())) {
            if (s.service == QLatin1String("qrz")) {
                QCOMPARE(s.rcvd, QString("Y"));
                QCOMPARE(s.rcvdDate, QString("20260101"));
                found = true;
            }
        }
        QVERIFY(found);
    }

    void oneFieldOnManyQso()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        QList<qint64> ids;
        for (const char* t : {"1200", "1300", "1400"}) {
            AdifRecord r{{"CALL", "W1AW"}, {"QSO_DATE", "20260928"}, {"TIME_ON", t}, {"BAND", "20m"}, {"MODE", "CW"}};
            if (QByteArray(t) == "1400")
                r.set("MY_GRIDSQUARE", "JN71AA");
            const InsertResult res = db.insertQso(r, "import");
            QCOMPARE(res.status, InsertResult::Status::Inserted);
            ids << res.id;
        }
        // Solo dove manca: il terzo ha gia' il suo locatore.
        BulkEditResult b = db.bulkEdit(ids, "MY_GRIDSQUARE", "JN70FU", true);
        QCOMPARE(b.changed, 2);
        QCOMPARE(b.unchanged, 1);
        QCOMPARE(b.changedIds, QList<qint64>({ids.at(0), ids.at(1)}));
        QCOMPARE(db.record(ids.at(0))->value("MY_GRIDSQUARE"), QString("JN70FU"));
        QCOMPARE(db.record(ids.at(2))->value("MY_GRIDSQUARE"), QString("JN71AA"));
        // Il valore di prima resta nello storico.
        QCOMPARE(db.history(ids.at(0)).size(), 1);
        QCOMPARE(db.meta(ids.at(0))->revision, 2);

        // Su tutti: anche il terzo. Una seconda volta non cambia niente.
        b = db.bulkEdit(ids, "MY_GRIDSQUARE", "JN70FU", false);
        QCOMPARE(b.changed, 1);
        QCOMPARE(db.bulkEdit(ids, "MY_GRIDSQUARE", "JN70FU", false).changed, 0);

        // Le QSL spedite: la data di oggi.
        b = db.bulkEdit(ids, "QSL_SENT", "Y", true);
        QCOMPARE(b.changed, 3);
        const QString today = QDateTime::currentDateTimeUtc().date().toString("yyyyMMdd");
        QCOMPARE(db.record(ids.at(1))->value("QSLSDATE"), today);
        // Un valore vuoto svuota il campo.
        QCOMPARE(db.bulkEdit({ids.at(0)}, "MY_GRIDSQUARE", "", false).changed, 1);
        QVERIFY(db.record(ids.at(0))->value("MY_GRIDSQUARE").isEmpty());
        // Un valore sbagliato non passa, e si dice su quale QSO.
        b = db.bulkEdit({ids.at(1)}, "MODE", "", false);
        QCOMPARE(b.failed, 1);
        QVERIFY(b.errors.first().startsWith("W1AW"));
        // Il profilo di stazione.
        StationProfile home;
        home.name = "Casa";
        home.stationCallsign = "IU8LMC";
        const qint64 profile = db.saveStationProfile(home);
        QVERIFY(profile > 0);
        b = db.bulkEdit(ids, "@profile", QString::number(profile), false);
        QVERIFY2(b.errors.isEmpty(), qPrintable(b.errors.join("; ")));
        QCOMPARE(b.changed, 3);
        QCOMPARE(db.meta(ids.at(2))->stationProfileId, profile);
        QCOMPARE(db.bulkEdit(ids, "@profile", "0", true).changed, 0);
    }

    void bulkEditQueuesRemoteChangesAndOnlyPendingServices()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        const qint64 id = db.insertQso({{"CALL", "W1AW"}, {"QSO_DATE", "20261118"}, {"TIME_ON", "1200"},
                                       {"BAND", "20m"}, {"MODE", "CW"}}, "test").id;
        QVERIFY(id > 0);
        for (const QString& service : {QStringLiteral("eqsl"), QStringLiteral("clublog"), QStringLiteral("crx")}) {
            QslState state; state.service = service; state.sent = "Y";
            if (service == QLatin1String("crx")) state.remoteId = "42";
            QVERIFY(db.setQslState(id, state));
        }
        const QStringList automatic{"eqsl", "clublog", "crx"};
        const auto edited = db.bulkEdit({id}, "COMMENT", "test", false, {}, automatic);
        QCOMPARE(edited.changed, 1);
        QCOMPARE(edited.pendingServices, QStringList{"crx"});
        QCOMPARE(db.qsosToUpload("eqsl"), QList<qint64>{});
        QCOMPARE(db.qsosToUpload("clublog"), QList<qint64>{});
        QCOMPARE(db.qsosToUpload("crx"), QList<qint64>{id});
        QCOMPARE(db.history(id).size(), 1);
        const auto unset = db.bulkEdit({id}, "EQSL_QSL_SENT", "N", false, {}, automatic);
        QCOMPARE(unset.changed, 1);
        QVERIFY(unset.pendingServices.contains("eqsl"));
        QVERIFY(unset.pendingServices.contains("crx"));
        QVERIFY(!unset.pendingServices.contains("clublog"));
    }

    void bulkEditReleasesLocksDuringUnchangedRuns()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString path = dir.filePath("bulk.sqlite");
        LogDatabase db; QVERIFY(db.open(path));
        QList<qint64> ids;
        for (int i = 0; i < 401; ++i) {
            AdifRecord r{{"CALL", QStringLiteral("K%1ABC").arg(i)}, {"QSO_DATE", "20261118"},
                         {"TIME_ON", "1200"}, {"BAND", "20m"}, {"MODE", "CW"}};
            if (i > 0) r.set("COMMENT", "already set");
            const auto inserted = db.insertQso(r, "test");
            QVERIFY(inserted.id > 0); ids << inserted.id;
        }
        LogDatabase other; QVERIFY(other.open(path));
        int firstProgress = 0;
        bool otherWriterSucceeded = false;
        const auto result = db.bulkEdit(ids, "COMMENT", "new", true, [&](int done, int) {
            if (firstProgress) return;
            firstProgress = done;
            QSqlQuery q(other.connection());
            otherWriterSucceeded = q.exec(QStringLiteral("UPDATE qso SET dirty = 1 WHERE id = %1").arg(ids.last()));
        });
        QVERIFY(firstProgress > 0 && firstProgress <= 200);
        QVERIFY(otherWriterSucceeded);
        QCOMPARE(result.changed, 1);
        QCOMPARE(result.unchanged, 400);
        QCOMPARE(result.failed, 0);
    }

    void bulkEditRollsBackFailedRemoteQueuePerQso()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        const qint64 a = db.insertQso({{"CALL", "K1AAA"}, {"QSO_DATE", "20261118"}, {"TIME_ON", "1200"},
                                       {"BAND", "20m"}, {"MODE", "CW"}}, "test").id;
        const qint64 b = db.insertQso({{"CALL", "K1BBB"}, {"QSO_DATE", "20261118"}, {"TIME_ON", "1201"},
                                       {"BAND", "20m"}, {"MODE", "CW"}}, "test").id;
        QslState state; state.service = "crx"; state.sent = "Y"; state.remoteId = "42";
        QVERIFY(db.setQslState(a, state));
        QSqlQuery trigger(db.connection());
        QVERIFY(trigger.exec(QStringLiteral("CREATE TRIGGER reject_remote BEFORE UPDATE OF sent ON qsl_status "
                                             "WHEN NEW.qso_id = %1 BEGIN SELECT RAISE(ABORT, 'fixture failure'); END").arg(a)));
        const auto result = db.bulkEdit({a, b}, "COMMENT", "test", false, {}, {"crx"});
        QCOMPARE(result.changed, 1);
        QCOMPARE(result.changedIds, QList<qint64>{b});
        QCOMPARE(result.failed, 1);
        QVERIFY(db.record(a)->value("COMMENT").isEmpty());
        QCOMPARE(db.meta(a)->revision, 1);
        QCOMPARE(db.history(a).size(), 0);
        QCOMPARE(db.qslStatus(a).first().sent, QStringLiteral("Y"));
        QCOMPARE(db.record(b)->value("COMMENT"), QStringLiteral("test"));
    }

    void bulkEditStopsWhenTriggerRollsBackTheTransaction()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        QList<qint64> ids;
        for (int i = 0; i < 3; ++i) {
            const auto inserted = db.insertQso({{"CALL", QStringLiteral("K%1ABC").arg(i)},
                {"QSO_DATE", "20261118"}, {"TIME_ON", "1200"}, {"BAND", "20m"}, {"MODE", "CW"}}, "test");
            QVERIFY(inserted.id > 0); ids << inserted.id;
            QslState state; state.service = "crx"; state.sent = "Y"; state.remoteId = "42";
            QVERIFY(db.setQslState(inserted.id, state));
        }
        QSqlQuery trigger(db.connection());
        QVERIFY(trigger.exec(QStringLiteral("CREATE TRIGGER abort_bulk_transaction BEFORE UPDATE OF sent ON qsl_status "
            "WHEN NEW.qso_id = %1 BEGIN SELECT RAISE(ROLLBACK, 'fixture rollback'); END").arg(ids.at(1))));
        const auto result = db.bulkEdit(ids, "COMMENT", "test", false, {}, {"crx"});
        QCOMPARE(result.changed, 0);
        QCOMPARE(result.failed, 3);
        QCOMPARE(result.unchanged, 0);
        QVERIFY(result.changedIds.isEmpty());
        QVERIFY(result.pendingServices.isEmpty());
        QVERIFY(!result.errors.isEmpty());
        for (const qint64 id : ids) {
            QVERIFY(db.record(id)->value("COMMENT").isEmpty());
            QCOMPARE(db.meta(id)->revision, 1);
            QCOMPARE(db.history(id).size(), 0);
            QCOMPARE(db.qslStatus(id).first().sent, QStringLiteral("Y"));
        }
        QVERIFY(db.qsosToUpload("crx").isEmpty());
    }

    void duplicatesAreFoundAndMerged()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        // Il log non li scarta gia' all'ingresso: finestra di un secondo.
        db.setDedupWindows(1, 1);
        auto add = [&db](const char* band, const char* mode, const char* sub, const char* time,
                         const QList<AdifField>& more = {}) {
            AdifRecord r{{"CALL", "W1AW"}, {"QSO_DATE", "20260928"}, {"TIME_ON", time}, {"BAND", band}, {"MODE", mode}};
            if (*sub)
                r.set("SUBMODE", sub);
            for (const AdifField& f : more)
                r.set(f.name, f.value);
            return db.insertQso(r, "import").id;
        };
        const qint64 a = add("20m", "FT8", "", "120000", {{"GRIDSQUARE", "FN31"}});
        const qint64 b = add("20m", "MFSK", "FT4", "120300",
                             {{"NAME", "HIRAM"}, {"LOTW_QSL_RCVD", "Y"}, {"LOTW_QSLRDATE", "20261001"},
                              {"APP_DECOLOG_TAGS", "pota"}, {"GRIDSQUARE", "FN42"}});
        add("20m", "CW", "", "120400");         // un altro genere di modo
        add("40m", "FT8", "", "120100");        // un'altra banda
        add("20m", "FT8", "", "140000");        // troppo lontano
        QVERIFY(a > 0 && b > 0);

        QCOMPARE(db.duplicateGroups(60).size(), 0);          // entro un minuto: niente
        const QList<QList<qint64>> groups = db.duplicateGroups(300);
        QCOMPARE(groups.size(), 1);
        QCOMPARE(groups.first(), QList<qint64>({a, b}));

        // Il riferimento remoto di b (il QSO che CRX ha gia') passa ad a.
        QslState crx;
        crx.service = "crx";
        crx.sent = "Y";
        crx.remoteId = "R1";
        QVERIFY(db.setQslState(b, crx));

        const InsertResult m = db.mergeQsos(a, {b});
        QCOMPARE(m.status, InsertResult::Status::Inserted);
        const AdifRecord kept = *db.record(a);
        QCOMPARE(kept.value("GRIDSQUARE"), QString("FN31"));    // il suo resta
        QCOMPARE(kept.value("NAME"), QString("HIRAM"));         // il resto arriva
        QCOMPARE(kept.value("LOTW_QSL_RCVD"), QString("Y"));
        QCOMPARE(kept.value("LOTW_QSLRDATE"), QString("20261001"));
        QCOMPARE(kept.value("APP_DECOLOG_TAGS"), QString("pota"));
        QVERIFY(db.meta(b)->deleted);
        const QList<HistoryEntry> history = db.history(a);
        QVERIFY(std::any_of(history.cbegin(), history.cend(), [](const HistoryEntry& h) { return h.reason == "merge"; }));
        QString remoteA, remoteB, sentB;
        for (const QslState& s : db.qslStatus(a))
            if (s.service == "crx")
                remoteA = s.remoteId;
        for (const QslState& s : db.qslStatus(b))
            if (s.service == "crx") {
                remoteB = s.remoteId;
                sentB = s.sent;
            }
        QCOMPARE(remoteA, QString("R1"));
        QVERIFY(remoteB.isEmpty());
        QVERIFY(sentB != "D");        // CRX non deve cancellare l'unica copia

        QCOMPARE(db.duplicateGroups(300).size(), 0);
        // Chi e' stato unito non torna da un recupero.
        QVERIFY(db.knowsQso(AdifRecord{{"CALL", "W1AW"}, {"QSO_DATE", "20260928"}, {"TIME_ON", "120300"},
                                       {"BAND", "20m"}, {"MODE", "MFSK"}, {"SUBMODE", "FT4"}}));
        // Unire un QSO gia' cancellato non fa niente.
        QVERIFY(db.mergeQsos(a, {b}).status != InsertResult::Status::Inserted);
    }
};

QTEST_GUILESS_MAIN(TestMigration)
#include "tst_migration.moc"
