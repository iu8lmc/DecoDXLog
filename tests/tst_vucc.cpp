#include "core/Vucc.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QSqlQuery>
#include <QTest>

using namespace decolog::core::vucc;

class TestVucc : public QObject {
    Q_OBJECT
private slots:
    void comparesStoredObservationsWithoutGrantingCredit()
    {
        Entry e{1,"EM98","6m",{},{},true,"lotw"}; e.qsoUtc="2026-01-01T12:00:00Z";
        e.lotwGridObservations=QJsonArray{QJsonObject{{"grid","EM98AA"},{"vuccGrids",""}},
            QJsonObject{{"grid","FM18"},{"vuccGrids",""}}, QJsonObject{{"grid","EM98"},{"vuccGrids","EM98,FM08"}},
            QJsonObject{{"grid","EM98"},{"vuccGrids","EM98,FM09"}}};
        e.lotwGridObservationCount=4;
        const auto result=select({e},Rules{{},{},"6m",100}); QCOMPARE(result.eligible.size(),1);
        const auto obs=result.toJson().object().value("eligible").toArray().first().toObject().value("lotwGridObservations").toArray();
        QCOMPARE(obs.size(),4); QCOMPARE(obs[0].toObject().value("comparison").toString(),QString("included"));
        QCOMPARE(obs[1].toObject().value("comparison").toString(),QString("different"));
        QCOMPARE(obs[2].toObject().value("comparison").toString(),QString("included"));
        QCOMPARE(obs[3].toObject().value("comparison").toString(),QString("unusable"));
        QVERIFY(!result.officialRulesVerified);
    }
    void initialThresholdsMatchPublishedCategories()
    {
        QCOMPARE(initialThreshold("terrestrial","6m"),100); QCOMPARE(initialThreshold("terrestrial","2m"),100);
        QCOMPARE(initialThreshold("terrestrial","1.25m"),50); QCOMPARE(initialThreshold("terrestrial","70cm"),50);
        QCOMPARE(initialThreshold("terrestrial","33cm"),25); QCOMPARE(initialThreshold("terrestrial","23cm"),25);
        QCOMPARE(initialThreshold("terrestrial","13cm"),10);
        for (const auto &band:QStringList{"9cm","6cm","3cm","1.25cm","6mm","4mm","2.5mm","2mm","1mm"}) QCOMPARE(initialThreshold("terrestrial",band),5);
        QCOMPARE(initialThreshold("terrestrial","submm"),0); // broad ADIF band does not identify a qualifying laser contact
        QCOMPARE(initialThreshold("satellite",{}),100); QCOMPARE(initialThreshold("terrestrial","4m"),0); QCOMPARE(initialThreshold("invalid","6m"),0);
        const auto preview=select({},Rules{{},{},"70cm",123}); QVERIFY(preview.isValid()); QCOMPARE(preview.rules.target,123);
        QCOMPARE(preview.toJson().object().value("initialThreshold").toInt(),50);
    }
    void appliesVuccDateAndSupportedBandBoundaries()
    {
        decolog::core::LogDatabase db; QVERIFY(db.open(":memory:"));
        const QByteArray data="<CALL:5>K1AAA<QSO_DATE:8>19821231<TIME_ON:6>235959<BAND:2>6m<MODE:2>CW<GRIDSQUARE:4>EM10<QSL_RCVD:1>Y<EOR>"
            "<CALL:5>K1BBB<QSO_DATE:8>19830101<TIME_ON:6>000000<BAND:2>6m<MODE:2>CW<GRIDSQUARE:4>EM11<QSL_RCVD:1>Y<EOR>";
        QCOMPARE(db.importAdif(data).inserted,2); QString error; const auto entries=loadEntries(db,&error); QVERIFY2(error.isEmpty(),qPrintable(error));
        const auto result=select(entries,Rules{{},{},"6m",100}); QCOMPARE(result.eligible.size(),1); QCOMPARE(result.eligible.first().grid,QString("EM11"));
        QVERIFY(result.exclusions.join('\n').contains("1983")); QCOMPARE(result.eligible.first().qsoUtc,QString("1983-01-01T00:00:00Z"));
        QVERIFY(!select(entries,Rules{{},{},"20m",100}).isValid()); QVERIFY(!select(entries,Rules{{},{},"4m",100}).isValid());
        QVERIFY(select({},Rules{{},{},"2m",100}).isValid()); QVERIFY(select({},Rules{{},{},"1.25m",100}).isValid()); QVERIFY(select({},Rules{{},{},"submm",100}).isValid());
        QVERIFY(select({},Rules{{},{},"",100,"satellite"}).isValid());
        auto shifted=entries[1]; shifted.qsoUtc="1983-01-01T00:30:00+01:00"; QVERIFY(select({shifted},Rules{{},{},"6m",100}).eligible.isEmpty());
        shifted.qsoUtc="1982-12-31T23:30:00-01:00"; QCOMPARE(select({shifted},Rules{{},{},"6m",100}).eligible.size(),1);
        shifted.qsoUtc="1983-01-01T00:00:00"; QVERIFY(select({shifted},Rules{{},{},"6m",100}).eligible.isEmpty());
        QSqlQuery q(db.connection()); QVERIFY(q.exec("UPDATE qso SET qso_datetime_on='bad' WHERE call='K1BBB'"));
        const auto malformed=select(loadEntries(db),Rules{{},{},"6m",100}); QVERIFY(malformed.eligible.isEmpty()); QVERIFY(malformed.exclusions.join('\n').contains("UTC timestamp"));
        QVERIFY(q.exec("UPDATE qso SET qso_datetime_on='1983-01-01T00:00:00' WHERE call='K1BBB'"));
        const auto legacy=select(loadEntries(db),Rules{{},{},"6m",100}); QCOMPARE(legacy.eligible.size(),1);
        QCOMPARE(legacy.eligible.first().qsoUtc,QString("1983-01-01T00:00:00Z"));
        QVERIFY(q.exec("SELECT qso_datetime_on FROM qso WHERE call='K1BBB'")); QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(),QString("1983-01-01T00:00:00"));
    }
    void separatesTerrestrialAndSatelliteCandidates()
    {
        decolog::core::LogDatabase db; QVERIFY(db.open(":memory:"));
        const QByteArray data="<CALL:5>K1AAA<QSO_DATE:8>20260101<TIME_ON:4>1200<BAND:2>2m<MODE:2>CW<GRIDSQUARE:4>EM10<QSL_RCVD:1>Y<EOR>"
            "<CALL:5>K1BBB<QSO_DATE:8>20260101<TIME_ON:4>1201<BAND:2>2m<BAND_RX:4>70cm<PROP_MODE:3>SAT<SAT_NAME:5>AO-91<MODE:2>FM<GRIDSQUARE:4>EM11<QSL_RCVD:1>Y<EOR>"
            "<CALL:5>K1CCC<QSO_DATE:8>20260101<TIME_ON:4>1202<BAND:4>70cm<BAND_RX:2>2m<PROP_MODE:3>SAT<SAT_NAME:5>AO-92<MODE:2>FM<GRIDSQUARE:4>EM12<QSL_RCVD:1>Y<EOR>"
            "<CALL:5>K1DDD<QSO_DATE:8>20260101<TIME_ON:4>1203<BAND:2>2m<SAT_NAME:5>AO-91<MODE:2>FM<GRIDSQUARE:4>EM13<QSL_RCVD:1>Y<EOR>"
            "<CALL:5>K1EEE<QSO_DATE:8>20260101<TIME_ON:4>1204<BAND:2>2m<BAND_RX:4>70cm<MODE:2>FM<GRIDSQUARE:4>EM14<QSL_RCVD:1>Y<EOR>";
        QCOMPARE(db.importAdif(data).inserted,5); QString error; const auto entries=loadEntries(db,&error); QVERIFY2(error.isEmpty(),qPrintable(error));
        const auto terrestrial=select(entries,Rules{{},{},"2m",100,"terrestrial"}); QCOMPARE(terrestrial.eligible.size(),1);
        QCOMPARE(terrestrial.eligible.first().grid,QString("EM10")); QCOMPARE(terrestrial.eligible.first().classification,QString("terrestrial-unreported"));
        const auto satellite=select(entries,Rules{{},{},"",100,"satellite"}); QCOMPARE(satellite.eligible.size(),2);
        QCOMPARE(satellite.eligible[0].grid,QString("EM11")); QCOMPARE(satellite.eligible[1].grid,QString("EM12"));
        QCOMPARE(satellite.eligible[0].satName,QString("AO-91")); QVERIFY(satellite.exclusions.join('\n').contains("inconsistent satellite"));
        QVERIFY(terrestrial.exclusions.join('\n').contains("crossband")); QVERIFY(!satellite.officialRulesVerified);
        QVERIFY(!select(entries,Rules{{},{},"2m",100,"invalid"}).isValid()); QCOMPARE(db.qsoCount(),qint64(5));
    }
    void excludesUnsupportedElectronicConfirmationSources()
    {
        decolog::core::LogDatabase db; QVERIFY(db.open(":memory:"));
        const QByteArray data="<CALL:5>K1AAA<QSO_DATE:8>20260101<TIME_ON:4>1200<BAND:2>6m<MODE:2>CW<GRIDSQUARE:4>EM10<EQSL_QSL_RCVD:1>Y<EOR>"
            "<CALL:5>K1BBB<QSO_DATE:8>20260101<TIME_ON:4>1201<BAND:2>6m<MODE:2>CW<GRIDSQUARE:4>EM11<QSL_RCVD:1>Y<EOR>"
            "<CALL:5>K1CCC<QSO_DATE:8>20260101<TIME_ON:4>1202<BAND:2>6m<MODE:2>CW<GRIDSQUARE:4>EM12<LOTW_QSL_RCVD:1>Y<EOR>"
            "<CALL:5>K1DDD<QSO_DATE:8>20260101<TIME_ON:4>1203<BAND:2>6m<MODE:2>CW<GRIDSQUARE:4>EM13<QSL_RCVD:1>Y<EQSL_QSL_RCVD:1>Y<EOR>";
        QCOMPARE(db.importAdif(data).inserted,4); QString error; const auto entries=loadEntries(db,&error); QVERIFY2(error.isEmpty(),qPrintable(error));
        const auto result=select(entries,Rules{{},{},"6m",100}); QCOMPARE(result.eligible.size(),3); QVERIFY(result.exclusions.join('\n').contains("unsupported"));
        QCOMPARE(result.eligible[0].grid,QString("EM11")); QCOMPARE(result.eligible[0].evidence,QString("card"));
        QCOMPARE(result.eligible[1].grid,QString("EM12")); QCOMPARE(result.eligible[1].evidence,QString("lotw"));
        QCOMPARE(result.eligible[2].grid,QString("EM13")); QCOMPARE(result.eligible[2].evidence,QString("card")); QCOMPARE(result.eligible[2].otherEvidence,QString("eqsl"));
        Entry fake{99,"EM14","6m",{},{},true,"lotw-fake,eqsl"}; fake.qsoUtc="2026-01-01T12:00:00Z"; QVERIFY(select({fake},Rules{{},{},"6m",100}).eligible.isEmpty());
        Entry mixed{100,"EM15","6m",{},{},true,"LoTW, EQSL"}; mixed.qsoUtc="2026-01-01T12:00:00Z"; const auto selected=select({mixed},Rules{{},{},"6m",100});
        QCOMPARE(selected.eligible.size(),1); QCOMPARE(selected.eligible.first().evidence,QString("lotw")); QCOMPARE(selected.eligible.first().otherEvidence,QString("eqsl"));
        QVERIFY(!result.officialRulesVerified); QCOMPARE(db.qsoCount(),qint64(4));
    }
    void boundaryQsoLoadsAndCreditsEachAdjacentGrid()
    {
        decolog::core::LogDatabase db;
        QVERIFY(db.open(":memory:"));
        const QByteArray input = "<CALL:5>K1AAA<QSO_DATE:8>20260101<TIME_ON:4>1200<BAND:2>6m<MODE:2>CW"
                                 "<QSL_RCVD:1>Y<GRIDSQUARE:4>EM98<VUCC_GRIDS:19>EM98,FM08,EM97,FM07<EOR>";
        QCOMPARE(db.importAdif(input).inserted, 1);
        QString error;
        const auto entries = loadEntries(db, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const auto result = select(entries, Rules{{}, {}, "6m", 100});
        QCOMPARE(result.eligible.size(), 4);
        QStringList grids;
        for (const auto &entry : result.eligible) grids << entry.grid;
        QCOMPARE(grids, QStringList({"EM98", "FM08", "EM97", "FM07"}));
        QCOMPARE(decolog::core::adif::parse(db.exportAdif()).records.first().value("VUCC_GRIDS"),
                 QStringLiteral("EM98,FM08,EM97,FM07"));
    }

    void gridBoundariesRejectInvalidOrInconsistentLists()
    {
        auto make = [](qint64 id, const QString &primary, const QString &list) {
            Entry e{id, primary, "6m", {}, {}, true, "lotw"};
            e.qsoUtc="2026-01-01T12:00:00Z";
            e.grids = list;
            return e;
        };
        const Rules rules{{}, {}, "6m", 100};
        const auto check = [&](const QString &primary, const QString &list, int expected) {
            const auto result = select({make(1, primary, list)}, rules);
            QCOMPARE(result.eligible.size(), expected);
        };
        check("EM98", "EM98,FM08", 2);
        check("EM98", "EM98,FM07", 0); // diagonal only
        check("EM98", "EM98,EM98", 0);
        check("EM98", "EM98,FM08,EM97", 0);
        check("EM98", "EM98,FM08,EM97,FM09", 0);
        check("AA00", "EM98,FM08", 0);
        check("EM98", "EM98,FM08ZZ", 0);
        check("EM98", "EM98,FM08AA", 2); // adjacent mixed precisions
        check("EM98", "EM98,FM08BA", 0); // fine subsquare does not touch boundary
        check("EM98", "EM98,FM08AX,EM99XA,FM09AA", 4); // four mixed-precision corners
        check("", "IO83UG,IO83UF", 1); // two adjacent subsquares, one GRID4
        check("IO83UA", "IO83UG,IO83UF", 0);
        check("", "RJ90,AJ00", 2); // adjacent across the antimeridian
        check("JN52GZ", "", 0); // valid first four characters, invalid subsquare
    }

    void rejectsInvalidOwnLocatorClaims()
    {
        Entry entry{1,"EM98","6m",{},{},true,"lotw"}; entry.qsoUtc="2026-01-01T12:00:00Z";
        const Rules rules{{},{},"6m",100};
        entry.myGrid="JN52"; entry.myGrids="JN52,JN53"; QCOMPARE(select({entry},rules).eligible.size(),1);
        entry.myGrids="JN52,JN54"; const auto disjoint=select({entry},rules); QVERIFY(disjoint.eligible.isEmpty()); QVERIFY(disjoint.exclusions.join('\n').contains("operating locator"));
        entry.myGrids="JN53,JN54"; QVERIFY(select({entry},rules).eligible.isEmpty());
        entry.myGrids.clear(); entry.myGrid="BAD"; QVERIFY(select({entry},rules).eligible.isEmpty());
        entry.myGrid.clear(); QCOMPARE(select({entry},rules).eligible.size(),1);
        decolog::core::LogDatabase db; QVERIFY(db.open(":memory:"));
        QCOMPARE(db.importAdif("<CALL:5>K1ABC<QSO_DATE:8>20260101<TIME_ON:4>1200<BAND:2>6m<MODE:2>CW<GRIDSQUARE:4>EM98<QSL_RCVD:1>Y<MY_GRIDSQUARE:4>JN52<MY_VUCC_GRIDS:9>JN52,JN54<EOR>").inserted,1);
        const auto imported=select(loadEntries(db),rules); QVERIFY(imported.eligible.isEmpty());
        QVERIFY(imported.exclusions.join('\n').contains("operating locator"));
        QCOMPARE(decolog::core::adif::parse(db.exportAdif()).records.first().value("MY_VUCC_GRIDS"),QString("JN52,JN54"));
    }

    void preservesOwnBoundaryLocatorThroughImportExport()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString path = temp.filePath("log.sqlite");
        const QByteArray input = "<CALL:5>K1AAA<QSO_DATE:8>20260101<TIME_ON:4>1200<BAND:2>6m<MODE:2>CW"
                                 "<QSL_RCVD:1>Y<VUCC_GRIDS:9>EM98,FM08<MY_GRIDSQUARE:4>JN52<MY_VUCC_GRIDS:9>JN52,JN53<EOR>";
        {
            decolog::core::LogDatabase writer;
            QVERIFY(writer.open(path));
            QCOMPARE(writer.importAdif(input).inserted, 1);
        }
        decolog::core::LogDatabase db;
        QVERIFY(db.open(path));
        QString error;
        const auto entries = loadEntries(db, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().myGrids, QStringLiteral("JN52,JN53"));
        QCOMPARE(entries.first().myGrid, QStringLiteral("JN52"));
        QVERIFY(db.recordLotwGridObservation(entries.first().qsoId,"FM18","FM18,FM19",&error));
        const auto withObservation=select(loadEntries(db),Rules{{},{},"6m",100}).toJson().object().value("eligible").toArray().first().toObject();
        const auto observations=withObservation.value("lotwGridObservations").toArray(); QCOMPARE(observations.size(),1);
        QCOMPARE(observations.first().toObject().value("grid").toString(),QString("FM18"));
        QCOMPARE(observations.first().toObject().value("vuccGrids").toString(),QString("FM18,FM19"));
        QCOMPARE(withObservation.value("grid4").toString(),QString("EM98"));
        for (const auto &grid : {QString("FM20"),QString("FM21"),QString("FM22")}) QVERIFY(db.recordLotwGridObservation(entries.first().qsoId,grid,QStringLiteral(""),&error));
        const auto bounded=select(loadEntries(db),Rules{{},{},"6m",100}).toJson().object().value("eligible").toArray().first().toObject();
        QCOMPARE(bounded.value("lotwGridObservationCount").toInt(),4); QCOMPARE(bounded.value("lotwGridObservations").toArray().size(),3);
        QCOMPARE(bounded.value("lotwGridObservations").toArray().first().toObject().value("grid").toString(),QString("FM22"));
        const auto preview = select(entries, Rules{{}, {}, "6m", 100});
        QCOMPARE(preview.eligible.size(), 2);
        const auto exported = preview.toJson().object().value("eligible").toArray().first().toObject();
        QCOMPARE(exported.value("gridEvidence").toString(),QStringLiteral("local-qso-unverified"));
        QCOMPARE(exported.value("reportedMyVuccGrids").toString(), QStringLiteral("JN52,JN53"));
        QCOMPARE(exported.value("reportedMyGrid").toString(), QStringLiteral("JN52"));
        const auto record = decolog::core::adif::parse(db.exportAdif()).records.first();
        QCOMPARE(record.value("MY_VUCC_GRIDS"), QStringLiteral("JN52,JN53"));
        QCOMPARE(record.value("VUCC_GRIDS"), QStringLiteral("EM98,FM08"));
    }

    void malformedStoredGridMetadataDoesNotSilentlyCreditPrimary()
    {
        decolog::core::LogDatabase db;
        QVERIFY(db.open(":memory:"));
        const QByteArray input = "<CALL:5>K1AAA<QSO_DATE:8>20260101<TIME_ON:4>1200<BAND:2>6m<MODE:2>CW"
                                 "<QSL_RCVD:1>Y<GRIDSQUARE:4>EM98<EOR>";
        QCOMPARE(db.importAdif(input).inserted, 1);
        QSqlQuery q(db.connection());
        QVERIFY(q.exec("UPDATE qso SET adif_extra='{\"VUCC_GRIDS\":7}'"));
        QString error;
        QVERIFY(loadEntries(db, &error).isEmpty());
        QVERIFY(error.contains("invalid VUCC grid field type"));
    }

    void acceptsConfirmedEvidencePerBandAccountAndSite()
    {
        QList<Entry> entries{
            {1, QStringLiteral("JN52GW"), QStringLiteral("6m"), QStringLiteral("home"), QStringLiteral("JM67"), true, QStringLiteral("lotw")},
            {2, QStringLiteral("JO52GX"), QStringLiteral("6m"), QStringLiteral("home"), QStringLiteral("JM67"), true, QStringLiteral("card")},
            {3, QStringLiteral("JN52GW"), QStringLiteral("6m"), QStringLiteral("home"), QStringLiteral("JM67"), true, QStringLiteral("lotw")},
            {4, QStringLiteral("JP52GX"), QStringLiteral("6m"), QStringLiteral("home"), QStringLiteral("JM68"), true, QStringLiteral("lotw")},
            {5, QStringLiteral("JN52GX"), QStringLiteral("6m"), QStringLiteral("home"), QStringLiteral("JM67"), false, {}},
        };
        for (auto& entry : entries) entry.qsoUtc="2026-01-01T12:00:00Z";
        const Result result = select(entries, Rules{QStringLiteral("home"), QStringLiteral("JM67"), QStringLiteral("6m"), 100});
        QCOMPARE(result.eligible.size(), 2);
        QCOMPARE(result.eligible.at(0).grid, QStringLiteral("JN52"));
        QVERIFY(result.exclusions.join('\n').contains(QStringLiteral("already selected")));
        QVERIFY(result.exclusions.join('\n').contains(QStringLiteral("evidence")));
        QVERIFY(result.exclusions.join('\n').contains(QStringLiteral("site mismatch")));
        QCOMPARE(result.toJson().object().value(QStringLiteral("format")).toString(), QStringLiteral("decodxlog-vucc-1"));
    }

    void rejectsInvalidGridAndRules()
    {
        QVERIFY(validGrid4(QStringLiteral("JN52")));
        QVERIFY(!validGrid4(QStringLiteral("ZZ99")));
        const Result result = select({}, Rules{QStringLiteral("home"), {}, {}, 100});
        QVERIFY(result.eligible.isEmpty());
        QVERIFY(result.exclusions.first().contains(QStringLiteral("band is required")));
    }
};

QTEST_MAIN(TestVucc)
#include "tst_vucc.moc"
