// Database: schema, inserimento, duplicati e import/export senza perdite.
#include "core/LogDatabase.h"
#include "core/Maidenhead.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QSqlDatabase>
#include <QUrl>

#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

using namespace decolog::core;

namespace {

// La versione dello schema, che sta in db/schema.sql. Sta qui una volta sola:
// chi aggiunge una colonna cambia il file, l'ultimo passo di migrate() e questo
// numero, e non va a caccia di tre QCOMPARE sparsi.
constexpr int kSchemaVersion = 11;

// Confronto semantico: i numeri come numeri (14.074 == 14.074000), le ore a
// quattro cifre come le stesse ore con i secondi a zero.
QString canonical(const QString& name, const QString& value)
{
    static const QStringList numeric{"FREQ", "FREQ_RX", "TX_PWR", "DXCC", "CQZ", "ITUZ"};
    if (numeric.contains(name)) {
        bool ok = false;
        const double v = value.toDouble(&ok);
        if (ok)
            return QString::number(v, 'f', 6);
    }
    if ((name == "TIME_ON" || name == "TIME_OFF") && value.size() == 4)
        return value + "00";
    if (name == "BAND" || name == "BAND_RX")
        return value.toLower();
    if (name == "CALL" || name == "MODE" || name == "SUBMODE")
        return value.toUpper();
    return value;
}

QMap<QString, QString> fieldMap(const AdifRecord& r)
{
    QMap<QString, QString> m;
    for (const auto& f : r.fields())
        m.insert(f.name, canonical(f.name, f.value));
    return m;
}

} // namespace

class TestDatabase : public QObject {
    Q_OBJECT

private slots:
    void schemaIsApplied()
    {
        LogDatabase db;
        QVERIFY2(db.open(":memory:"), qPrintable(db.lastError()));
        QCOMPARE(db.schemaVersion(), kSchemaVersion);
        QCOMPARE(db.qsoCount(), 0);
    }

    void apiIdempotencySurvivesReopenAndRejectsBodyChanges()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("api.sqlite"));
        const ApiIdempotencyRecord stored{"body-a", 201, "application/json", "{\"ok\":true}",
                                          QStringLiteral("2026-10-18T12:00:00Z")};
        {
            LogDatabase db;
            QVERIFY(db.open(path));
            QVERIFY(db.saveApiIdempotency(QStringLiteral("retry-1"), stored));
            const auto found = db.apiIdempotency(QStringLiteral("retry-1"));
            QVERIFY(found);
            QCOMPARE(found->requestBody, stored.requestBody);
            QCOMPARE(found->responseBody, stored.responseBody);
        }
        {
            LogDatabase reopened;
            QVERIFY(reopened.open(path));
            const auto found = reopened.apiIdempotency(QStringLiteral("retry-1"));
            QVERIFY(found);
            QCOMPARE(found->status, 201);
            QCOMPARE(found->contentType, QByteArray("application/json"));
            QVERIFY(!reopened.apiIdempotency(QStringLiteral("missing")));
            QVERIFY(found->requestBody != QByteArray("body-b"));
        }
    }

    void expiredApiIdempotencyEntryIsRemovedOnLookup()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        const ApiIdempotencyRecord expired{"body", 201, "application/json", "{}",
                                          QStringLiteral("2020-01-01T00:00:00Z")};
        QVERIFY(db.saveApiIdempotency(QStringLiteral("expired"), expired));
        QVERIFY(!db.apiIdempotency(QStringLiteral("expired")));
        QSqlQuery q(db.connection());
        QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM api_idempotency WHERE key = 'expired'")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 0);
    }

    void apiIdempotencyKeepsTheFirstWriter()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        const ApiIdempotencyRecord first{"body-a", 201, "application/json", "{\"id\":1}", {}};
        const ApiIdempotencyRecord second{"body-b", 201, "application/json", "{\"id\":2}", {}};
        QVERIFY(db.saveApiIdempotency(QStringLiteral("race"), first));
        QVERIFY(!db.saveApiIdempotency(QStringLiteral("race"), second));
        const auto stored = db.apiIdempotency(QStringLiteral("race"));
        QVERIFY(stored);
        QCOMPARE(stored->requestBody, QByteArray("body-a"));
        QCOMPARE(stored->responseBody, QByteArray("{\"id\":1}"));
    }

    void insertAndReadBack()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        AdifRecord r{{"CALL", "dl1ab"}, {"QSO_DATE", "20260917"}, {"TIME_ON", "101500"},
                     {"FREQ", "14.084"}, {"MODE", "FT2"}, {"RST_SENT", "-10"},
                     {"STATION_CALLSIGN", "IU8LMC"}, {"APP_DECODIUM_DT", "0.2"}};
        const InsertResult res = db.insertQso(r, "udp_decodium", "Decodium FT2 1.0.637");
        QCOMPARE(res.status, InsertResult::Status::Inserted);

        QSqlQuery q(db.connection());
        QVERIFY(q.exec("SELECT call, band, mode, submode, freq, qso_datetime_on, uuid, revision, dirty, adif_extra FROM qso"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(), QString("DL1AB"));
        QCOMPARE(q.value(1).toString(), QString("20m"));     // ricavata dalla frequenza
        QCOMPARE(q.value(2).toString(), QString("MFSK"));
        QCOMPARE(q.value(3).toString(), QString("FT2"));
        QCOMPARE(q.value(4).toDouble(), 14.084);
        QCOMPARE(q.value(5).toString(), QString("2026-09-17T10:15:00Z"));
        QCOMPARE(q.value(6).toString().size(), 36);
        QCOMPARE(q.value(7).toInt(), 1);
        QCOMPARE(q.value(8).toInt(), 1);
        QVERIFY(q.value(9).toString().contains("APP_DECODIUM_DT"));

        const auto back = db.record(res.id);
        QVERIFY(back);
        QCOMPARE(back->value("STATION_CALLSIGN"), QString("IU8LMC"));
        QCOMPARE(back->value("SUBMODE"), QString("FT2"));
    }

    void localWriteDoesNotWaitForUnavailableService()
    {
        // Contratto architetturale DXL-032: un servizio esterno lento o
        // irraggiungibile non deve diventare una dipendenza del salvataggio
        // locale. La richiesta e' volutamente solo avviata; non si attende
        // alcuna risposta per poter scrivere il QSO.
        QNetworkAccessManager services;
        QNetworkReply* reply = services.get(
            QNetworkRequest(QUrl(QStringLiteral("http://192.0.2.1:9/decolog-service-timeout"))));

        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        AdifRecord r{{"CALL", "K1TEST"}, {"QSO_DATE", "20260917"}, {"TIME_ON", "101500"},
                     {"BAND", "20m"}, {"MODE", "CW"}};
        QElapsedTimer timer;
        timer.start();
        const InsertResult result = db.insertQso(r, "manual");
        QVERIFY2(timer.elapsed() < 500, "il salvataggio locale attende un servizio esterno");
        QCOMPARE(result.status, InsertResult::Status::Inserted);
        QCOMPARE(db.qsoCount(), 1);
        reply->abort();
        reply->deleteLater();
    }

    void offlineQsosSurviveCloseAndReopen()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("offline.sqlite"));
        qint64 id = 0;
        {
            LogDatabase db;
            QVERIFY2(db.open(path), qPrintable(db.lastError()));
            const auto result = db.insertQso(
                {{"CALL", "K1OFF"}, {"QSO_DATE", "20261015"}, {"TIME_ON", "120000"},
                 {"BAND", "20m"}, {"MODE", "CW"}, {"QSL_RCVD", "Y"}},
                QStringLiteral("manual"));
            QCOMPARE(result.status, InsertResult::Status::Inserted);
            id = result.id;
            QCOMPARE(db.dirtyQsos(), QList<qint64>{id});
            db.close();
        }
        {
            LogDatabase reopened;
            QVERIFY2(reopened.open(path), qPrintable(reopened.lastError()));
            QCOMPARE(reopened.qsoCount(), 1);
            const auto record = reopened.record(id);
            QVERIFY(record);
            QCOMPARE(record->value(QStringLiteral("CALL")), QStringLiteral("K1OFF"));
            QCOMPARE(record->value(QStringLiteral("QSL_RCVD")), QStringLiteral("Y"));
            QCOMPARE(reopened.dirtyQsos(), QList<qint64>{id});
        }
    }

    void rejectsIncompleteRecords()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        const InsertResult res = db.insertQso({{"CALL", "K1AB"}, {"MODE", "CW"}}, "import");
        QCOMPARE(res.status, InsertResult::Status::Invalid);
        QVERIFY(res.message.contains("QSO_DATE"));
        QCOMPARE(db.qsoCount(), 0);
    }

    void duplicateWindow()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        AdifRecord r{{"CALL", "K1AB"}, {"QSO_DATE", "20260917"}, {"TIME_ON", "101500"},
                     {"BAND", "40m"}, {"MODE", "MFSK"}, {"SUBMODE", "FT2"}};
        QCOMPARE(db.insertQso(r, "udp_decodium").status, InsertResult::Status::Inserted);

        r.set("TIME_ON", "101640");  // +100 s: stesso QSO
        QCOMPARE(db.insertQso(r, "udp_decodium").status, InsertResult::Status::Duplicate);

        r.set("TIME_ON", "102000");  // +5 min: un altro QSO da Decodium...
        QCOMPARE(db.insertQso(r, "udp_decodium").status, InsertResult::Status::Inserted);

        r.set("TIME_ON", "102800");  // ...ma a mano la finestra e' ±10 min
        QCOMPARE(db.insertQso(r, "manual", {}, true).status, InsertResult::Status::Duplicate);

        r.set("SUBMODE", "FT4");     // altro sottomodo: non e' un doppione
        QCOMPARE(db.insertQso(r, "udp_decodium").status, InsertResult::Status::Inserted);

        // Senza sottomodo (FT8, CW, SSB) il doppione si trova lo stesso.
        AdifRecord ft8{{"CALL", "W1AW"}, {"QSO_DATE", "20260917"}, {"TIME_ON", "110000"},
                       {"BAND", "20m"}, {"MODE", "FT8"}};
        QCOMPARE(db.insertQso(ft8, "udp_wsjtx").status, InsertResult::Status::Inserted);
        ft8.set("TIME_ON", "110030");
        QCOMPARE(db.insertQso(ft8, "udp_wsjtx").status, InsertResult::Status::Duplicate);
        AdifRecord cw{{"CALL", "W1AW"}, {"QSO_DATE", "20260917"}, {"TIME_ON", "111500"},
                      {"BAND", "20m"}, {"MODE", "CW"}};
        QCOMPARE(db.insertQso(cw, "manual", {}, true).status, InsertResult::Status::Inserted);
        QCOMPARE(db.insertQso(cw, "manual", {}, true).status, InsertResult::Status::Duplicate);
        QCOMPARE(db.qsoCount(), 5);
    }

    void importExportIsLossless()
    {
        const QByteArray original =
            "Exported by some logger\n<ADIF_VER:5>3.1.4<PROGRAMID:4>Test<EOH>\n"
            "<CALL:5>JA1XY<QSO_DATE:8>20240301<TIME_ON:4>0712<QSO_DATE_OFF:8>20240301<TIME_OFF:6>071400"
            "<BAND:3>20M<FREQ:6>14.074<MODE:3>FT8<RST_SENT:3>-08<RST_RCVD:3>-15<GRIDSQUARE:6>PM95vq"
            "<NAME:5>Taro <COUNTRY:5>Japan<DXCC:3>339<CQZ:2>25<ITUZ:2>45<CONT:2>AS<TX_PWR:3>100"
            "<LOTW_QSL_SENT:1>Y<QSL_VIA:6>bureau<MY_SOTA_REF:9>I/CM-123 <USERDEF1:3>abc<EOR>\n"
            "<CALL:4>W1AW<QSO_DATE:8>20240302<TIME_ON:6>180000<FREQ:5>7.030<MODE:2>CW"
            "<RST_SENT:3>599<RST_RCVD:3>579<DXCC:3>bad<COMMENT:13>nice ;) <qso><NOTES:18>line one\r\nline two<EOR>\n";

        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        const ImportResult ir = db.importAdif(original);
        QCOMPARE(ir.inserted, 2);
        QCOMPARE(ir.invalid, 0);

        const AdifDocument before = adif::parse(original);
        const AdifDocument after = adif::parse(db.exportAdif("test"));
        QCOMPARE(after.records.size(), before.records.size());

        for (int i = 0; i < before.records.size(); ++i) {
            QMap<QString, QString> expected = fieldMap(before.records.at(i));
            QMap<QString, QString> actual = fieldMap(after.records.at(i));
            // La banda mancante viene ricavata: e' un'aggiunta, non una perdita.
            if (!expected.contains("BAND"))
                actual.remove("BAND");
            QCOMPARE(actual, expected);
        }
    }

    void qslStatusRoundTrip()
    {
        const QByteArray original =
            "<CALL:4>K1AB<QSO_DATE:8>20260101<TIME_ON:4>1000<BAND:3>20m<MODE:3>FT8"
            "<LOTW_QSL_SENT:1>Y<LOTW_QSLSDATE:8>20260102<LOTW_QSL_RCVD:1>N"
            "<EQSL_QSL_RCVD:1>Y<CLUBLOG_QSO_UPLOAD_STATUS:1>M<QSL_SENT:1>Q<EOR>";
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        QCOMPARE(db.importAdif(original).inserted, 1);

        QSqlQuery q(db.connection());
        QVERIFY(q.exec("SELECT service, sent, sent_date, rcvd FROM qsl_status ORDER BY service"));
        QStringList rows;
        while (q.next())
            rows << q.value(0).toString() + ":" + q.value(1).toString() + q.value(2).toString() + q.value(3).toString();
        QCOMPARE(rows, QStringList({"card:QN", "clublog:MN", "eqsl:NY", "lotw:Y20260102N"}));

        const AdifDocument after = adif::parse(db.exportAdif());
        QCOMPARE(fieldMap(after.records.first()), fieldMap(adif::parse(original).records.first()));
    }

    void aDateBefore1930IsNotADate()
    {
        // Il 30/12/1899 e' lo zero delle date di Excel: chi lo scrive nell'ADIF
        // ha perso la data vera. L'ADIF vuole date dal 1930: il QSO si rifiuta
        // e si dice perche', invece di entrare nel log con una data finta.
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        const ImportResult r = db.importAdif(
            "<CALL:6>SP5SMY<QSO_DATE:8>18991230<TIME_ON:4>0840<BAND:3>20m<MODE:3>FT8<EOR>"
            "<CALL:5>F4FSY<QSO_DATE:8>19300101<TIME_ON:4>0841<BAND:3>40m<MODE:3>FT8<EOR>");
        QCOMPARE(r.inserted, 1);
        QCOMPARE(r.invalid, 1);
        QVERIFY(r.errors.first().contains("18991230"));
        QVERIFY(LogDatabase::isoFromAdif("18991230", "0840").isEmpty());
        QCOMPARE(LogDatabase::isoFromAdif("19300101", "0841"), QString("1930-01-01T08:41:00Z"));
    }

    void importReportCoversRecordsQslUtcAndPreservedFields()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        const ImportResult r = db.importAdif(
            "<CALL:4>K1AA<QSO_DATE:8>20260101<TIME_ON:4>1000<BAND:3>20m<MODE:3>FT8"
            "<LOTW_QSL_RCVD:1>Y<USERDEF1:3>abc<EOR>"
            "<CALL:4>K1AB<BAND:3>40m<MODE:3>CW<EOR>");
        QCOMPARE(r.read, 2);
        QCOMPARE(r.inserted, 1);
        QCOMPARE(r.invalid, 1);
        QCOMPARE(r.qslRecords, 1);
        QCOMPARE(r.missingUtc, 1);
        QVERIFY(r.preservedExtraFields.contains(QStringLiteral("USERDEF1")));
        QCOMPARE(r.insertedIds.size(), 1);
        QVERIFY(db.meta(r.insertedIds.first()).has_value());
        QVERIFY(db.softDeleteQso(r.insertedIds.first()));
        QCOMPARE(db.qsoCount(), 0);
    }

    void enrichmentKeepsFieldProvenanceAndValidity()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        const auto inserted = db.insertQso({{"CALL", "K1AA"}, {"QSO_DATE", "20260101"},
                                            {"TIME_ON", "1000"}, {"BAND", "20m"}, {"MODE", "FT8"}}, "udp");
        QVERIFY(inserted.id > 0);
        EnrichmentEntry entry{inserted.id, "NAME", "", "Alice", "callbook", "inferred",
                              "2026-10-15T12:00:00.000Z", "20260101", ""};
        QVERIFY2(db.recordEnrichment({entry}), qPrintable(db.lastError()));
        const auto rows = db.enrichments(inserted.id);
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().source, QStringLiteral("callbook"));
        QCOMPARE(rows.first().kind, QStringLiteral("inferred"));
        QCOMPARE(rows.first().validFrom, QStringLiteral("20260101"));
        QVERIFY(db.undoEnrichment(inserted.id, QStringLiteral("NAME")));
        QVERIFY(db.record(inserted.id)->value(QStringLiteral("NAME")).isEmpty());
        QVERIFY(db.enrichments(inserted.id).isEmpty());
        QVERIFY(!db.history(inserted.id).isEmpty());
    }

    void aBigImportGoesInBlocks()
    {
        // A blocchi: dopo ogni blocco si sa a che punto si e', e un doppione
        // dentro lo stesso file si riconosce anche fra un blocco e l'altro.
        const QByteArray adif =
            "<CALL:4>K1AA<QSO_DATE:8>20260101<TIME_ON:4>1000<BAND:3>20m<MODE:3>FT8<EOR>"
            "<CALL:4>K1AB<QSO_DATE:8>20260101<TIME_ON:4>1001<BAND:3>20m<MODE:3>FT8<EOR>"
            "<CALL:4>K1AC<QSO_DATE:8>20260101<TIME_ON:4>1002<BAND:3>20m<MODE:3>FT8<EOR>"
            "<CALL:4>K1AA<QSO_DATE:8>20260101<TIME_ON:4>1000<BAND:3>20m<MODE:3>FT8<EOR>"
            "<CALL:4>K1AD<QSO_DATE:8>20260101<TIME_ON:4>1003<BAND:3>20m<MODE:3>FT8<EOR>";
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        QList<QPair<int, int>> steps;
        const ImportResult r = db.importAdif(adif, "import", 0,
                                             [&steps](int done, int total) { steps << qMakePair(done, total); }, 2);
        QCOMPARE(r.inserted, 4);
        QCOMPARE(r.duplicates, 1);
        QCOMPARE(steps, (QList<QPair<int, int>>{{2, 5}, {4, 5}, {5, 5}}));
        QCOMPARE(db.qsoCount(), 4);
    }

    void aSelectionExportsLikeTheWholeLog()
    {
        // L'esportazione di una selezione va a blocchi di cinquecento: con 1203
        // QSO ci sono tre blocchi, e ogni QSO deve uscire uguale a come esce
        // dall'esportazione di tutto il log, nell'ordine chiesto.
        QByteArray adif;
        for (int i = 0; i < 1203; ++i) {
            const QByteArray call = "K" + QByteArray::number(i % 10) + "A" + QByteArray::number(i);
            adif += "<CALL:" + QByteArray::number(call.size()) + ">" + call
                    + "<QSO_DATE:8>" + QDate(2026, 1, 1).addDays(i % 300).toString("yyyyMMdd").toLatin1()
                    + "<TIME_ON:4>" + QByteArray::number(i % 24).rightJustified(2, '0') + "00"
                    + "<BAND:3>20m<MODE:3>FT8"
                    + (i % 3 == 0 ? QByteArray("<LOTW_QSL_RCVD:1>Y") : QByteArray())
                    + (i % 7 == 0 ? QByteArray("<QSL_SENT:1>Y<QSL_SENT_VIA:1>B") : QByteArray()) + "<EOR>\n";
        }
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        QCOMPARE(db.importAdif(adif).inserted, 1203);

        const AdifDocument all = adif::parse(db.exportAdif());
        QCOMPARE(all.records.size(), 1203);
        QList<qint64> ids;
        QSqlQuery q(db.connection());
        QVERIFY(q.exec("SELECT id FROM qso WHERE deleted = 0 ORDER BY qso_datetime_on, id"));
        while (q.next())
            ids << q.value(0).toLongLong();
        const AdifDocument chosen = adif::parse(db.exportAdif(ids));
        QCOMPARE(chosen.records.size(), 1203);
        for (int i = 0; i < 1203; ++i)
            QCOMPARE(fieldMap(chosen.records.at(i)), fieldMap(all.records.at(i)));
        // All'incontrario, e l'ordine resta quello chiesto.
        std::reverse(ids.begin(), ids.end());
        const AdifDocument reversed = adif::parse(db.exportAdif(ids));
        QCOMPARE(fieldMap(reversed.records.first()), fieldMap(all.records.last()));
        QCOMPARE(fieldMap(reversed.records.last()), fieldMap(all.records.first()));
        // Un QSO come record() lo restituisce da solo.
        QCOMPARE(fieldMap(*db.record(ids.last())), fieldMap(all.records.first()));
    }

    // «Dal … al …» per esportare: estremi compresi, il giorno di «al» intero (anche
    // le 23:59), un estremo vuoto aperto, i cancellati fuori, e nell'ordine di tempo.
    void aPeriodTakesWholeDaysAndSkipsTheDeleted()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        const auto qso = [](const char* call, const char* date, const char* time) {
            return QByteArray("<CALL:") + QByteArray::number(int(qstrlen(call))) + ">" + call + "<QSO_DATE:8>" + date
                   + "<TIME_ON:4>" + time + "<BAND:3>20m<MODE:3>FT8<EOR>";
        };
        // Fuori ordine di proposito: l'ordine dell'export e' quello del tempo.
        QByteArray adif = qso("K4DD", "20260430", "2359") + qso("K1AA", "20260331", "2359")
                          + qso("K3CC", "20260415", "1200") + qso("K5EE", "20260501", "0000")
                          + qso("K2BB", "20260401", "0000") + qso("K6FF", "20260410", "0900");
        QCOMPARE(db.importAdif(adif).inserted, 6);
        QList<qint64> all = db.qsoIdsBetween({}, {});
        QCOMPARE(all.size(), 6);
        // Il QSO delle 09:00 del 10 aprile viene cancellato: non conta in nessun periodo.
        for (qint64 id : all) {
            if (db.record(id)->value("CALL") == QLatin1String("K6FF"))
                QVERIFY(db.softDeleteQso(id));
        }
        const auto calls = [&](const QString& from, const QString& to) {
            QStringList out;
            const AdifDocument doc = adif::parse(db.exportAdif(db.qsoIdsBetween(from, to)));
            for (const AdifRecord& r : doc.records)
                out << r.value("CALL");
            return out;
        };

        QCOMPARE(db.qsoCountBetween({}, {}), 5);
        // Aprile intero: dal primo (00:00) all'ultimo giorno (23:59), non un minuto fuori.
        QCOMPARE(calls("2026-04-01", "2026-04-30"), (QStringList{"K2BB", "K3CC", "K4DD"}));
        QCOMPARE(db.qsoCountBetween("2026-04-01", "2026-04-30"), 3);
        // Un giorno solo: dal 30 al 30.
        QCOMPARE(calls("2026-04-30", "2026-04-30"), (QStringList{"K4DD"}));
        // Estremi aperti.
        QCOMPARE(calls("2026-04-15", {}), (QStringList{"K3CC", "K4DD", "K5EE"}));
        QCOMPARE(calls({}, "2026-04-01"), (QStringList{"K1AA", "K2BB"}));
        // Un periodo senza QSO non esporta niente (e non tutto il log).
        QCOMPARE(db.qsoCountBetween("2027-01-01", "2027-12-31"), 0);
        QVERIFY(db.qsoIdsBetween("2027-01-01", "2027-12-31").isEmpty());
        // Al contrario non c'e' niente: lo dice chi chiede, qui il risultato e' vuoto.
        QVERIFY(db.qsoIdsBetween("2026-04-30", "2026-04-01").isEmpty());
    }

    // Il nominativo di un QSO e' stato corretto: i servizi che avevano il QSO sbagliato, senza
    // conferma, tornano «da mandare»; i confermati e le cartoline restano come sono.
    void correctingTheCallSendsTheQsoAgain()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        QCOMPARE(db.importAdif("<CALL:5>K1ABD<QSO_DATE:8>20260101<TIME_ON:4>1000<BAND:3>20m<MODE:3>FT8<EOR>").inserted, 1);
        const qint64 id = db.qsosToUpload("lotw").first();
        const auto state = [&](const char* service, const char* sent, const char* rcvd = "N", const char* remote = "") {
            QslState st;
            st.service = service;
            st.sent = sent;
            st.rcvd = rcvd;
            st.remoteId = remote;
            QVERIFY(db.setQslState(id, st));
        };
        state("lotw", "Y");                       // mandato, senza conferma
        state("qrz", "Y", "N", "4711");           // mandato, con il suo id
        state("clublog", "Y");
        state("eqsl", "Y", "Y");                  // confermato: il nominativo era giusto
        state("hrdlog", "N");                     // non ancora mandato: non c'e' niente da rimandare
        QslState card;
        card.service = "card";
        card.sent = "Y";
        card.via = "B";
        QVERIFY(db.setQslState(id, card));
        // La conferma di LoTW non c'e': la correzione la rimanda.
        QCOMPARE(db.markSentForResend(id), (QStringList{"clublog", "lotw", "qrz"}));
        const auto sentOf = [&](const QString& service) {
            for (const QslState& s : db.qslStatus(id)) {
                if (s.service == service)
                    return s.sent;
            }
            return QString();
        };
        QCOMPARE(sentOf("lotw"), QString("R"));
        QCOMPARE(sentOf("qrz"), QString("R"));
        QCOMPARE(sentOf("clublog"), QString("R"));
        QCOMPARE(sentOf("eqsl"), QString("Y"));
        QCOMPARE(sentOf("hrdlog"), QString("N"));
        QCOMPARE(sentOf("card"), QString("Y"));
        // Tornano fra i da-mandare, anche da dietro la data limite: QRZ ha gia' un suo id.
        QCOMPARE(db.qsosToUpload("lotw"), QList<qint64>{id});
        QCOMPARE(db.qsosToUpload("qrz", 0, QDate(2026, 6, 1)), QList<qint64>{id});
        // Una seconda correzione non trova piu' niente da rimandare.
        QVERIFY(db.markSentForResend(id).isEmpty());
        // E un QSO senza stati non da' errori.
        QVERIFY(db.markSentForResend(id + 99).isEmpty());
    }

    // Lo stesso QSO a mano si puo' riscrivere: con il controllo spento entra, con il controllo
    // acceso (Decodium, UDP) e' un doppione come sempre.
    void activationCanLogTheSameContactAgain()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        AdifRecord r{{"CALL", "IK0XYZ"}, {"QSO_DATE", "20260101"}, {"TIME_ON", "1000"}, {"BAND", "20m"}, {"MODE", "SSB"}};
        QCOMPARE(db.insertQso(r, "manual", "test", true).status, InsertResult::Status::Inserted);
        QCOMPARE(db.insertQso(r, "manual", "test", true).status, InsertResult::Status::Duplicate);
        QCOMPARE(db.insertQso(r, "manual", "test", true, 0, false).status, InsertResult::Status::Inserted);
        QCOMPARE(db.qsoCount(), 2);
    }

    void crxFollowsEditsAndDeletions()
    {
        // CRX corregge e cancella: un QSO gia' li' e poi corretto torna in
        // coda, uno cancellato qui resta da togliere anche li'.
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        QCOMPARE(db.importAdif("<CALL:4>K1AB<QSO_DATE:8>20260101<TIME_ON:4>1000<BAND:3>20m<MODE:3>FT8<EOR>"
                               "<CALL:4>K2CD<QSO_DATE:8>20260101<TIME_ON:4>1100<BAND:3>20m<MODE:3>FT8<EOR>")
                     .inserted, 2);
        const QList<qint64> ids = db.qsosToUpload("crx");
        QCOMPARE(ids.size(), 2);
        for (const qint64 id : ids) {
            QslState st;
            st.service = "crx";
            st.sent = "Y";
            st.remoteId = QString("1:%1").arg(100 + id);
            QVERIFY(db.setQslState(id, st));
        }
        QCOMPARE(db.uploadPendingCount("crx"), 0);

        db.queueRemoteEdit(ids.at(0));
        QCOMPARE(db.qsosToUpload("crx"), QList<qint64>{ids.at(0)});
        // Anche se e' piu' vecchio del "da quel giorno": CRX ce l'ha gia'.
        QCOMPARE(db.qsosToUpload("crx", 0, QDate(2026, 6, 1)), QList<qint64>{ids.at(0)});

        QVERIFY(db.softDeleteQso(ids.at(1)));
        const auto gone = db.remoteDeletions("crx");
        QCOMPARE(gone.size(), 1);
        QCOMPARE(gone.first().first, ids.at(1));
        QCOMPARE(gone.first().second, QString("1:%1").arg(100 + ids.at(1)));
        QCOMPARE(db.uploadPendingCount("crx"), 2);

        QVERIFY(db.forgetRemote(ids.at(1), "crx"));
        QVERIFY(db.remoteDeletions("crx").isEmpty());
        QCOMPARE(db.uploadPendingCount("crx"), 1);
        // Gli altri servizi non si toccano: LoTW non sa correggere.
        QslState lotw;
        lotw.service = "lotw";
        lotw.sent = "Y";
        lotw.remoteId = "x";
        QVERIFY(db.setQslState(ids.at(0), lotw));
        db.queueRemoteEdit(ids.at(0));
        QVERIFY(db.qsosToUpload("lotw").isEmpty());
    }

    void editKeepsHistoryAndRestores()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        const auto ins = db.insertQso({{"CALL", "9A3XY"}, {"QSO_DATE", "20260916"}, {"TIME_ON", "145215"},
                                       {"BAND", "20m"}, {"MODE", "FT2"}, {"NAME", "Ivan"}}, "udp_decodium");
        QCOMPARE(ins.status, InsertResult::Status::Inserted);

        AdifRecord edited = *db.record(ins.id);
        edited.set("NAME", "Ivan Horvat");
        edited.set("LOTW_QSL_SENT", "Y");
        QCOMPARE(db.updateQso(ins.id, edited).status, InsertResult::Status::Inserted);

        const auto m = db.meta(ins.id);
        QCOMPARE(m->revision, 2);
        QVERIFY(m->dirty);
        QCOMPARE(db.record(ins.id)->value("NAME"), QString("Ivan Horvat"));
        QCOMPARE(db.qslStatus(ins.id).size(), 1);

        const auto hist = db.history(ins.id);
        QCOMPARE(hist.size(), 1);
        QCOMPARE(hist.first().revision, 1);
        QCOMPARE(hist.first().record.value("NAME"), QString("Ivan"));

        QCOMPARE(db.restoreRevision(ins.id, hist.first().id).status, InsertResult::Status::Inserted);
        QCOMPARE(db.record(ins.id)->value("NAME"), QString("Ivan"));
        QCOMPARE(db.qslStatus(ins.id).size(), 0);
        QCOMPARE(db.meta(ins.id)->revision, 3);
        QCOMPARE(db.history(ins.id).size(), 2);

        // Dentro una transazione aperta da chi chiama (import, correzioni in blocco).
        QVERIFY(db.connection().transaction());
        AdifRecord again = *db.record(ins.id);
        again.set("DXCC", "497");
        QCOMPARE(db.updateQso(ins.id, again).status, InsertResult::Status::Inserted);
        QVERIFY(db.connection().commit());
        QCOMPARE(db.record(ins.id)->value("DXCC"), QString("497"));
        QCOMPARE(db.meta(ins.id)->revision, 4);

        QVERIFY(db.softDeleteQso(ins.id));
        QCOMPARE(db.qsoCount(), 0);
        QVERIFY(db.meta(ins.id)->deleted);
        QCOMPARE(db.history(ins.id).first().reason, QString("delete"));
        QCOMPARE(db.idsWithoutDxcc().size(), 0);
    }

    void stationProfiles()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        StationProfile home;
        home.name = "Home JN71DC";
        home.stationCallsign = "iu8lmc";
        home.myGridsquare = "JN71DC";
        home.isDefault = true;
        const qint64 homeId = db.saveStationProfile(home);
        QVERIFY(homeId > 0);

        StationProfile portable = home;
        portable.name = "Portable /P";
        portable.stationCallsign = "IU8LMC/P";
        const qint64 portableId = db.saveStationProfile(portable);
        QVERIFY(portableId > 0);

        // Il secondo predefinito toglie il titolo al primo.
        auto profiles = db.stationProfiles();
        QCOMPARE(profiles.size(), 2);
        QCOMPARE(db.stationProfile(homeId)->isDefault, false);
        QCOMPARE(db.stationProfile(portableId)->isDefault, true);
        QCOMPARE(db.stationProfile(homeId)->stationCallsign, QString("IU8LMC"));
        QCOMPARE(db.profileForCallsign("iu8lmc/p"), portableId);

        db.insertQso({{"CALL", "K1AB"}, {"QSO_DATE", "20260101"}, {"TIME_ON", "1000"}, {"BAND", "20m"}, {"MODE", "SSB"}},
                     "manual", {}, true, homeId);
        QCOMPARE(db.stationProfile(homeId)->qsoCount, 1);

        QVERIFY(db.deleteStationProfile(homeId));
        QCOMPARE(db.stationProfiles(false).size(), 1);
        QCOMPARE(db.stationProfile(homeId)->qsoCount, 1);
    }

    void routesSimultaneousOperatorQsosToSeparateProfiles()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));

        StationProfile gbc;
        gbc.name = QStringLiteral("YL3GBC");
        gbc.stationCallsign = QStringLiteral("YL3GBC");
        gbc.operatorCall = QStringLiteral("YL3GBC");
        const qint64 gbcId = db.saveStationProfile(gbc);
        QVERIFY(gbcId > 0);

        StationProfile aug;
        aug.name = QStringLiteral("YL3AUG");
        aug.stationCallsign = QStringLiteral("YL3AUG");
        aug.operatorCall = QStringLiteral("YL3AUG");
        const qint64 augId = db.saveStationProfile(aug);
        QVERIFY(augId > 0);

        // A/B: due identita' esterne contemporanee, due profili diversi.
        QCOMPARE(db.profileForIdentity(QStringLiteral("YL3GBC"), QString()), gbcId);
        QCOMPARE(db.profileForIdentity(QStringLiteral("YL3AUG"), QString()), augId);
        QCOMPARE(db.profileForIdentity(QString(), QStringLiteral("YL3AUG")), augId);

        const auto a = db.insertQso({{"CALL", "K1GBC"}, {"QSO_DATE", "20261026"},
                                     {"TIME_ON", "100000"}, {"BAND", "20m"}, {"MODE", "CW"},
                                     {"STATION_CALLSIGN", "YL3GBC"}},
                                    QStringLiteral("udp_decodium"), QStringLiteral("Decodium"), false, gbcId);
        const auto b = db.insertQso({{"CALL", "K1AUG"}, {"QSO_DATE", "20261026"},
                                     {"TIME_ON", "100100"}, {"BAND", "20m"}, {"MODE", "CW"},
                                     {"STATION_CALLSIGN", "YL3AUG"}},
                                    QStringLiteral("udp_decodium"), QStringLiteral("Decodium"), false, augId);
        QCOMPARE(a.status, InsertResult::Status::Inserted);
        QCOMPARE(b.status, InsertResult::Status::Inserted);
        QCOMPARE(db.meta(a.id)->stationProfileId, gbcId);
        QCOMPARE(db.meta(b.id)->stationProfileId, augId);
        QCOMPARE(db.stationProfile(gbcId)->qsoCount, 1);
        QCOMPARE(db.stationProfile(augId)->qsoCount, 1);
    }

    void refusesAmbiguousOperatorRouting()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        StationProfile home;
        home.name = QStringLiteral("Home");
        home.stationCallsign = QStringLiteral("YL3GBC");
        home.operatorCall = QStringLiteral("YL3GBC");
        const qint64 homeId = db.saveStationProfile(home);
        QVERIFY(homeId > 0);
        StationProfile portable = home;
        portable.id = 0;
        portable.uuid.clear();
        portable.name = QStringLiteral("Portable");
        portable.stationCallsign = QStringLiteral("YL3GBC/P");
        QVERIFY(db.saveStationProfile(portable) > 0);

        // Con il solo OPERATOR non si conosce il luogo: mai scegliere in modo
        // arbitrario un profilo, mentre il nominativo completo resta risolvibile.
        QCOMPARE(db.profileForIdentity(QString(), QStringLiteral("YL3GBC")), qint64(0));
        QCOMPARE(db.profileForIdentity(QStringLiteral("YL3GBC/P"), QStringLiteral("YL3GBC")),
                 db.profileForCallsign(QStringLiteral("YL3GBC/P")));
    }

    void awardStatsAndBackup()
    {
        LogDatabase db;
        const QString dir = QDir::temp().filePath("decolog-test-" + QString::number(QCoreApplication::applicationPid()));
        QDir().mkpath(dir);
        QVERIFY(db.open(dir + "/log.sqlite"));
        const auto a = db.insertQso({{"CALL", "9A3XY"}, {"QSO_DATE", "20260916"}, {"TIME_ON", "1452"}, {"BAND", "20m"},
                                     {"MODE", "FT2"}, {"DXCC", "497"}, {"GRIDSQUARE", "JN75WS"}, {"LOTW_QSL_RCVD", "Y"}}, "udp_decodium");
        const auto b = db.insertQso({{"CALL", "9A1AA"}, {"QSO_DATE", "20260916"}, {"TIME_ON", "1500"}, {"BAND", "40m"},
                                     {"MODE", "FT2"}, {"DXCC", "497"}, {"GRIDSQUARE", "JN85"}}, "udp_decodium");
        db.insertQso({{"CALL", "W1AW"}, {"QSO_DATE", "20260916"}, {"TIME_ON", "1510"}, {"BAND", "20m"},
                      {"MODE", "FT8"}, {"DXCC", "291"}, {"GRIDSQUARE", "FN31"}}, "udp_decodium");
        db.insertQso({{"CALL", "EA8XX"}, {"QSO_DATE", "20260916"}, {"TIME_ON", "1520"}, {"BAND", "20m"},
                      {"MODE", "SSB"}, {"SUBMODE", "USB"}}, "manual");

        const Ft2Award award = db.ft2Award();
        QCOMPARE(award.qsos, 2);
        QCOMPARE(award.dxccWorked, 1);
        QCOMPARE(award.dxccConfirmed, 1);
        QCOMPARE(award.gridsWorked, 2);
        QCOMPARE(award.gridsConfirmed, 1);
        QVERIFY(db.isFirstFt2Dxcc(a.id));    // 14:52, prima di b
        QVERIFY(!db.isFirstFt2Dxcc(b.id));   // 15:00, stesso DXCC gia' lavorato

        const auto byBand = db.countByBand();
        QCOMPARE(byBand.size(), 2);
        QCOMPARE(byBand.first().key, QString("40m"));
        QCOMPARE(db.countByMode().first().key, QString("FT2"));
        QStringList modes;
        for (const auto& row : db.countByMode())
            modes << row.key;
        QVERIFY(modes.contains("SSB"));    // SSB/USB si legge SSB
        QVERIFY(!modes.contains("USB"));

        const QString copy = dir + "/backup.sqlite";
        QVERIFY2(db.backupTo(copy), qPrintable(db.lastError()));
        LogDatabase restored;
        QVERIFY(restored.open(copy));
        QCOMPARE(restored.qsoCount(), 4);
        restored.close();
        db.close();
        QDir(dir).removeRecursively();
    }

    void everythingGoesBackInTheCloudQueue()
    {
        LogDatabase db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        const qint64 a = db.insertQso({{"CALL", "K1ABC"}, {"QSO_DATE", "20260101"}, {"TIME_ON", "1200"},
                                       {"BAND", "20m"}, {"MODE", "FT8"}}, "import").id;
        QVERIFY(a > 0);
        // Appena scritto e' da mandare; quando il Cloud l'ha preso, non piu'.
        QVERIFY(db.markSynced(a, 1));
        QCOMPARE(db.dirtyQsos().size(), 0);

        // Svuotato il Cloud, lassu' non c'e' piu' niente: tutto torna in coda.
        QCOMPARE(db.markAllDirty(), 1);
        QCOMPARE(db.dirtyQsos().size(), 1);
    }

    void maidenheadMath()
    {
        const auto jn71 = maidenhead::toLatLon("JN71DC");
        const auto jn75 = maidenhead::toLatLon("jn75ws");
        QVERIFY(jn71 && jn75);
        QVERIFY(qAbs(jn71->lat - 41.1042) < 0.01);
        QVERIFY(qAbs(jn71->lon - 14.2917) < 0.01);
        const double km = maidenhead::distanceKm(*jn71, *jn75);
        QVERIFY2(km > 520 && km < 550, qPrintable(QString::number(km)));
        const double az = maidenhead::azimuthDeg(*jn71, *jn75);
        QVERIFY2(az > 0 && az < 30, qPrintable(QString::number(az)));
        QVERIFY(!maidenhead::toLatLon("ZZ00"));
        QVERIFY(!maidenhead::toLatLon("JN7"));

        // E la strada all'indietro: dalla posizione al locatore, e ritorno.
        QCOMPARE(maidenhead::fromLatLon(41.1042, 14.2917), QStringLiteral("JN71DC"));
        QCOMPARE(maidenhead::fromLatLon(41.1042, 14.2917, 4), QStringLiteral("JN71"));
        QCOMPARE(maidenhead::fromLatLon(51.05, 13.74), QStringLiteral("JO61UB"));
        QCOMPARE(maidenhead::fromLatLon(0.0, 0.0, 4), QStringLiteral("JJ00"));
        const auto back = maidenhead::toLatLon(maidenhead::fromLatLon(-33.87, 151.21));
        QVERIFY(back);
        QVERIFY(qAbs(back->lat + 33.87) < 0.03 && qAbs(back->lon - 151.21) < 0.06);
        // Una posizione che non sta nel mondo non fa un locatore.
        QVERIFY(maidenhead::fromLatLon(95.0, 0.0).isEmpty());
        QVERIFY(maidenhead::fromLatLon(0.0, -200.0).isEmpty());
    }

    void migrationFromV1()
    {
        const QString path = QDir::temp().filePath("decolog-v1-test.sqlite");
        QFile::remove(path);
        {
            QSqlDatabase raw = QSqlDatabase::addDatabase("QSQLITE", "v1");
            raw.setDatabaseName(path);
            QVERIFY(raw.open());
            QSqlQuery q(raw);
            QVERIFY(q.exec("CREATE TABLE schema_version (version INTEGER NOT NULL, applied_at TEXT)"));
            QVERIFY(q.exec("INSERT INTO schema_version (version) VALUES (1)"));
            QVERIFY(q.exec("CREATE TABLE qso (id INTEGER PRIMARY KEY, call TEXT NOT NULL, notes TEXT)"));
            // La v1 aveva gia' gli stati QSL, senza la colonna della via.
            QVERIFY(q.exec("CREATE TABLE qsl_status (qso_id INTEGER NOT NULL, service TEXT NOT NULL, "
                           "sent TEXT NOT NULL DEFAULT 'N', sent_date TEXT, rcvd TEXT NOT NULL DEFAULT 'N', "
                           "rcvd_date TEXT, remote_id TEXT, last_error TEXT, PRIMARY KEY (qso_id, service))"));
            raw.close();
        }
        QSqlDatabase::removeDatabase("v1");
        {
            LogDatabase db;
            QVERIFY2(db.open(path), qPrintable(db.lastError()));
            QCOMPARE(db.schemaVersion(), kSchemaVersion);
            QSqlQuery q(db.connection());
            QVERIFY(q.exec("SELECT COUNT(*) FROM pragma_table_info('qso') WHERE name = 'tags'") && q.next());
            QCOMPARE(q.value(0).toInt(), 1);
            QVERIFY(q.exec("SELECT COUNT(*) FROM pragma_table_info('qsl_status') WHERE name = 'via'") && q.next());
            QCOMPARE(q.value(0).toInt(), 1);
            QVERIFY(q.exec("SELECT COUNT(*) FROM pragma_table_info('qso') WHERE name = 'sat_mode'") && q.next());
            QCOMPARE(q.value(0).toInt(), 1);
            // L'ultimo passo, quello del riferimento di un programma (il DCI dei
            // castelli): un log vecchio deve arrivare fino a qui, non fermarsi a
            // meta' strada.
            QVERIFY(q.exec("SELECT COUNT(*) FROM pragma_table_info('qso') WHERE name = 'sig_info'") && q.next());
            QCOMPARE(q.value(0).toInt(), 1);
            db.close();
        }
        {
            // Un log con il sottomodo prende anche il suo indice (v6).
            LogDatabase fresh;
            QVERIFY(fresh.open(":memory:"));
            QSqlQuery q(fresh.connection());
            QVERIFY(q.exec("SELECT COUNT(*) FROM sqlite_master WHERE type = 'index' AND name = 'idx_qso_submode'")
                    && q.next());
            QCOMPARE(q.value(0).toInt(), 1);
        }
        {
            LogDatabase db;
            // Una seconda apertura non rifa la migrazione.
            QVERIFY(db.open(path));
            QCOMPARE(db.schemaVersion(), kSchemaVersion);
        }
        QFile::remove(path);
    }

    void tags()
    {
        QCOMPARE(LogDatabase::splitTags(" pota , Field  Day,POTA,,portable"), QStringList({"pota", "Field Day", "portable"}));

        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        const qint64 a = db.insertQso({{"CALL", "K1AB"}, {"QSO_DATE", "20260101"}, {"TIME_ON", "1000"}, {"BAND", "20m"},
                                       {"MODE", "FT8"}, {"APP_DECOLOG_TAGS", "pota, pota ,portable"}}, "import").id;
        const qint64 b = db.insertQso({{"CALL", "K2AB"}, {"QSO_DATE", "20260102"}, {"TIME_ON", "1000"}, {"BAND", "20m"},
                                       {"MODE", "FT8"}}, "import").id;
        QCOMPARE(db.record(a)->value("APP_DECOLOG_TAGS"), QString("pota,portable"));

        QCOMPARE(db.setTag({a, b}, "Field Day", true), 2);
        QCOMPARE(db.setTag({a, b}, "field day", true), 0);   // gia' presente, maiuscole a parte
        QCOMPARE(db.record(b)->value("APP_DECOLOG_TAGS"), QString("Field Day"));
        QCOMPARE(db.history(b).first().reason, QString("tag"));

        QCOMPARE(db.setTag({a}, "POTA", false), 1);
        QCOMPARE(db.record(a)->value("APP_DECOLOG_TAGS"), QString("portable,Field Day"));

        const auto counts = db.tagCounts();
        QCOMPARE(counts.size(), 2);
        QCOMPARE(counts.at(0).key, QString("Field Day"));
        QCOMPARE(counts.at(0).count, 2);

        // Le etichette tornano nell'export ADIF. Il tipo si scrive per esteso:
        // con le sole graffe un compilatore puo' vederci anche la versione che
        // prende una stringa, e su Linux si ferma li'.
        QVERIFY(db.exportAdif(QList<qint64>{a}).contains("<app_decolog_tags:18>portable,Field Day"));
    }

    void workedBefore()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        db.insertQso({{"CALL", "K1AB"}, {"QSO_DATE", "20260101"}, {"TIME_ON", "1000"}, {"BAND", "20m"}, {"MODE", "FT8"}, {"NAME", "Bob"}}, "import");
        db.insertQso({{"CALL", "K1AB"}, {"QSO_DATE", "20260201"}, {"TIME_ON", "1000"}, {"BAND", "40m"}, {"MODE", "MFSK"}, {"SUBMODE", "FT2"}}, "import");
        const WorkedBefore wb = db.workedBefore("k1ab");
        QCOMPARE(wb.count, 2);
        QCOMPARE(wb.bands, QStringList({"40m", "20m"}));
        QCOMPARE(wb.lastMode, QString("FT2"));
        QCOMPARE(wb.name, QString("Bob"));
        QCOMPARE(db.workedBefore("N0NE").count, 0);
    }

    // La griglia banda x modo: una casella per banda e gruppo di modo, con le
    // conferme messe insieme da servizi diversi dello stesso QSO.
    void bandModeSlots()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        // Due digitali diversi sulla stessa banda: una casella sola.
        QCOMPARE(db.importAdif("<CALL:6>JA1ZZZ<QSO_DATE:8>20260101<TIME_ON:4>1000<BAND:3>20m<MODE:3>FT8"
                               "<DXCC:3>339<LOTW_QSL_RCVD:1>Y<EOR>"
                               "<CALL:6>JA1ZZZ<QSO_DATE:8>20260102<TIME_ON:4>1000<BAND:3>20m<MODE:4>RTTY"
                               "<DXCC:3>339<EQSL_QSL_RCVD:1>Y<EOR>"
                               // Stessa banda, ma in CW: casella a parte.
                               "<CALL:6>JA1ZZZ<QSO_DATE:8>20260103<TIME_ON:4>1000<BAND:3>20m<MODE:2>CW"
                               "<DXCC:3>339<EOR>"
                               // La fonia, e un'altra stazione della stessa entita'.
                               "<CALL:6>JA1ZZZ<QSO_DATE:8>20260104<TIME_ON:4>1000<BAND:3>40m<MODE:3>SSB"
                               "<DXCC:3>339<EOR>"
                               "<CALL:6>JA3ABC<QSO_DATE:8>20260105<TIME_ON:4>1000<BAND:3>15m<MODE:3>SSB"
                               "<DXCC:3>339<EOR>").inserted, 5);

        auto describe = [](const QList<LogDatabase::BandModeSlot>& list) {
            QStringList out;
            for (const auto& s : list) {
                QString marks;
                if (s.lotw) marks += "L";
                if (s.eqsl) marks += "e";
                if (s.card) marks += "K";
                out << s.band + "/" + s.group + "/" + QString::number(s.count) + (marks.isEmpty() ? "" : "/" + marks);
            }
            out.sort();
            return out;
        };

        QCOMPARE(describe(db.bandModeSlotsForCall("ja1zzz")),
                 QStringList({"20m/CW/1", "20m/DATA/2/Le", "40m/PHONE/1"}));
        // L'entita' aggiunge quello che hanno fatto le altre stazioni.
        QCOMPARE(describe(db.bandModeSlotsForDxcc(339)),
                 QStringList({"15m/PHONE/1", "20m/CW/1", "20m/DATA/2/Le", "40m/PHONE/1"}));
        QVERIFY(db.bandModeSlotsForCall("N0NE").isEmpty());
        QVERIFY(db.bandModeSlotsForDxcc(0).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestDatabase)
#include "tst_database.moc"
