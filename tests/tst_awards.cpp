// Award: prefissi WPX e conteggi lavorati/confermati per banda, modo e conferma.
#include "core/Awards.h"
#include "core/LogDatabase.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using namespace decolog::core;

class TestAwards : public QObject {
    Q_OBJECT

    static AwardResult find(const QList<AwardResult>& all, const QString& id)
    {
        for (const auto& r : all) {
            if (r.id == id)
                return r;
        }
        return {};
    }

private slots:
    void hawaiiLotwAndInferredState()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        const auto result = db.insertQso(AdifRecord{{"CALL", "AH6U"}, {"QSO_DATE", "20261113"},
            {"TIME_ON", "170215"}, {"BAND", "20m"}, {"MODE", "FT8"}, {"DXCC", "110"},
            {"STATE", "HI"}, {"LOTW_QSL_RCVD", "Y"}}, QStringLiteral("import"));
        QVERIFY(result.id > 0);
        AwardFilter filter;
        filter.band = QStringLiteral("20m");
        const AwardCalculator calc;
        auto was = find(calc.compute(db, filter), QStringLiteral("was"));
        QCOMPARE(was.worked(), 1);
        QCOMPARE(was.confirmed(), 1);
        QVERIFY(db.recordEnrichment({EnrichmentEntry{result.id, QStringLiteral("STATE"), {}, QStringLiteral("HI"),
            QStringLiteral("callbook"), QStringLiteral("inferred"), QStringLiteral("2026-11-13T17:03:00Z"), {}, {}}}));
        was = find(calc.compute(db, filter), QStringLiteral("was"));
        QCOMPARE(was.worked(), 0);
        QCOMPARE(was.confirmed(), 0);
        QVERIFY(db.confirmInferredState(result.id));
        was = find(calc.compute(db, filter), QStringLiteral("was"));
        QCOMPARE(was.worked(), 1);
        QCOMPARE(was.confirmed(), 1);
        QCOMPARE(db.enrichments(result.id).size(), 2);
        QVERIFY(!db.confirmInferredState(result.id));
        const auto other = db.insertQso(AdifRecord{{"CALL", "WH6S"}, {"QSO_DATE", "20261112"},
            {"TIME_ON", "172900"}, {"BAND", "40m"}, {"MODE", "FT8"}, {"DXCC", "110"},
            {"STATE", "HI"}}, QStringLiteral("import"));
        QVERIFY(other.id > 0);
        QVERIFY(db.recordEnrichment({EnrichmentEntry{other.id, QStringLiteral("STATE"), {}, QStringLiteral("HI"),
            QStringLiteral("callbook"), QStringLiteral("inferred"), QStringLiteral("2026-11-13T17:03:00Z"), {}, {}}}));
        QCOMPARE(db.bulkEdit({other.id}, QStringLiteral("STATE"), QStringLiteral("HI"), true).changed, 0);
        QCOMPARE(db.bulkEdit({other.id}, QStringLiteral("STATE"), QStringLiteral("HI"), false).changed, 1);
        filter.band = QStringLiteral("40m");
        was = find(calc.compute(db, filter), QStringLiteral("was"));
        QCOMPARE(was.worked(), 1);
        QCOMPARE(was.confirmed(), 0);
        QCOMPARE(db.confirmedIds(QStringLiteral("lotw")).contains(other.id), false);
    }

    void wpx_data()
    {
        QTest::addColumn<QString>("call");
        QTest::addColumn<QString>("prefix");
        QTest::newRow("simple") << "N8BJQ" << "N8";
        QTest::newRow("two letters") << "WB2ABC" << "WB2";
        QTest::newRow("digit first") << "4X1AB" << "4X1";
        QTest::newRow("uk") << "2E0ABC" << "2E0";
        QTest::newRow("two digits") << "S57AB" << "S57";
        QTest::newRow("italy portable") << "IU8LMC/P" << "IU8";
        QTest::newRow("prefix first") << "EA8/OH2XX" << "EA8";
        QTest::newRow("prefix last") << "N8BJQ/KH6" << "KH6";
        QTest::newRow("no digit") << "LX/DL1ABC" << "LX0";
        QTest::newRow("area change") << "W1AW/4" << "W4";
        QTest::newRow("lowercase") << "ja1zzz" << "JA1";
    }

    void wpx()
    {
        QFETCH(QString, call);
        QFETCH(QString, prefix);
        QCOMPARE(awards::wpxPrefix(call), prefix);
    }

    void countsWorkedAndConfirmed()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        auto qso = [&db](const char* call, const char* date, const char* band, const char* mode, const char* submode,
                         int dxcc, int cqz, std::initializer_list<AdifField> extra) {
            AdifRecord r{{"CALL", call}, {"QSO_DATE", date}, {"TIME_ON", "1200"}, {"BAND", band}, {"MODE", mode},
                         {"SUBMODE", submode}, {"DXCC", QString::number(dxcc)}, {"CQZ", QString::number(cqz)}};
            for (const auto& f : extra)
                r.set(f.name, f.value);
            QCOMPARE(db.insertQso(r, "import").status, InsertResult::Status::Inserted);
        };
        qso("W1AW", "20260101", "20m", "MFSK", "FT2", 291, 5, {{"STATE", "CT"}, {"GRIDSQUARE", "FN31pr"}, {"LOTW_QSL_RCVD", "Y"}});
        qso("K6XX", "20260102", "40m", "FT8", "", 291, 3, {{"STATE", "CA"}, {"GRIDSQUARE", "CM87"}});
        qso("KH6ABC", "20260103", "20m", "SSB", "USB", 110, 31, {{"STATE", "HI"}, {"QSL_RCVD", "Y"}});
        qso("EA8/OH2XX", "20260104", "15m", "CW", "", 29, 33, {{"IOTA", "AF-004"}, {"POTA_REF", "EA-0012"}, {"EQSL_QSL_RCVD", "Y"}});
        qso("VE3XX", "20260105", "20m", "MFSK", "FT2", 1, 4, {{"STATE", "ON"}});   // non e' USA: niente WAS

        const AwardCalculator calc([](int dxcc) { return dxcc == 291 ? QStringLiteral("United States") : QString(); });
        AwardFilter all;
        const auto results = calc.compute(db, all);
        QCOMPARE(results.size(), AwardCalculator::awardIds().size());

        const auto dxcc = find(results, "dxcc");
        QCOMPARE(dxcc.worked(), 4);           // 291, 110, 29, 1
        QCOMPARE(dxcc.confirmed(), 2);        // 291 (LoTW), 110 (cartolina); eQSL non conta
        const auto usa = std::find_if(dxcc.items.cbegin(), dxcc.items.cend(), [](const AwardItem& i) { return i.key == "291"; });
        QCOMPARE(usa->name, QString("United States"));
        QCOMPARE(usa->qsoCount, 2);
        QCOMPARE(usa->bandsWorked, QSet<QString>({"20m", "40m"}));
        QCOMPARE(usa->bandsConfirmed, QSet<QString>({"20m"}));
        QCOMPARE(usa->firstCall, QString("W1AW"));

        QCOMPARE(find(results, "ft2").worked(), 2);
        QCOMPARE(find(results, "waz").worked(), 5);
        const auto was = find(results, "was");
        QCOMPARE(was.worked(), 3);            // CT, CA, HI (ON e' in Canada)
        QCOMPARE(was.items.first().name, QString("California"));
        QCOMPARE(find(results, "grids").worked(), 2);
        QCOMPARE(find(results, "iota").worked(), 1);
        QCOMPARE(find(results, "pota").worked(), 1);
        QCOMPARE(find(results, "wpx").worked(), 5);

        // Anche eQSL come conferma: vale per gli altri diplomi, non per il
        // DXCC, che per l'ARRL si conferma solo con LoTW o cartolina.
        AwardFilter withEqsl;
        withEqsl.confirmEqsl = true;
        QCOMPARE(find(calc.compute(db, withEqsl), "dxcc").confirmed(), 2);
        QCOMPARE(find(calc.compute(db, withEqsl), "iota").confirmed(), 1);
        QCOMPARE(find(calc.compute(db, all), "iota").confirmed(), 0);

        // Solo 20m, solo FT2.
        AwardFilter band20;
        band20.band = "20m";
        QCOMPARE(find(calc.compute(db, band20), "dxcc").worked(), 3);
        AwardFilter ft2;
        ft2.modeGroup = "FT2";
        QCOMPARE(find(calc.compute(db, ft2), "dxcc").worked(), 2);
        AwardFilter phone;
        phone.modeGroup = "PHONE";
        QCOMPARE(find(calc.compute(db, phone), "dxcc").worked(), 1);
        AwardFilter digital;
        digital.modeGroup = "DIGITAL";
        QCOMPARE(find(calc.compute(db, digital), "dxcc").worked(), 2);

        // Band slot: su 20m tre entita' lavorate (291, 110, 1), due confermate.
        const auto totals = dxcc.bandTotals({"20m", "40m", "15m", "10m"});
        QCOMPARE(totals.size(), 4);
        QCOMPARE(totals.at(0).worked, 3);
        QCOMPARE(totals.at(0).confirmed, 2);
        QCOMPARE(totals.at(1).worked, 1);
        QCOMPARE(totals.at(1).confirmed, 0);
        QCOMPARE(totals.at(3).worked, 0);
    }

    void sharedFixtureMatchesCloudContract()
    {
        QFile file(QStringLiteral(DECO_DXLOG_SOURCE_DIR) + QStringLiteral("/tests/fixtures/awards_shared.json"));
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        const QJsonArray qsos = root.value(QStringLiteral("qsos")).toArray();
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        for (const auto value : qsos) {
            const QJsonObject q = value.toObject();
            AdifRecord r;
            for (auto it = q.begin(); it != q.end(); ++it)
                r.set(it.key(), it.value().toString());
            QCOMPARE(db.insertQso(r, "shared-fixture").status, InsertResult::Status::Inserted);
        }
        const auto results = AwardCalculator().compute(db, AwardFilter{});
        const auto expected = root.value(QStringLiteral("expected")).toObject();
        QCOMPARE(find(results, "dxcc").worked(), expected.value("dxcc_worked").toInt());
        QCOMPARE(find(results, "dxcc").confirmed(), expected.value("dxcc_confirmed").toInt());
        QCOMPARE(find(results, "wac").worked(), expected.value("wac_worked").toInt());
        QCOMPARE(find(results, "waz").worked(), expected.value("waz_worked").toInt());
        AwardFilter band;
        band.band = QStringLiteral("20m");
        QCOMPARE(find(AwardCalculator().compute(db, band), "dxcc").worked(), expected.value("20m_dxcc").toInt());
        AwardFilter mode;
        mode.modeGroup = QStringLiteral("CW");
        QCOMPARE(find(AwardCalculator().compute(db, mode), "dxcc").worked(), expected.value("cw_dxcc").toInt());
        QCOMPARE(db.qsoCount(), expected.value("qsos").toInt());
        const auto byBand = db.countByBand();
        auto bandCount = [&byBand](const QString& band) {
            for (const auto& row : byBand)
                if (row.key == band)
                    return row.count;
            return 0;
        };
        QCOMPARE(bandCount(QStringLiteral("20m")), expected.value("20m_qsos").toInt());
        QCOMPARE(bandCount(QStringLiteral("40m")), expected.value("40m_qsos").toInt());
        const auto qsl = db.qslSummary();
        const auto lotw = std::find_if(qsl.cbegin(), qsl.cend(), [](const QVariantMap& row) {
            return row.value(QStringLiteral("service")).toString() == QLatin1String("lotw");
        });
        QVERIFY(lotw != qsl.cend());
        QCOMPARE(lotw->value(QStringLiteral("sent")).toInt(), expected.value("lotw_sent").toInt());
        QCOMPARE(lotw->value(QStringLiteral("confirmed")).toInt(), expected.value("lotw_confirmed").toInt());
    }

    void wacDoesNotCountAntarcticaAsAnOfficialContinent()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        const QStringList continents{"EU", "NA", "SA", "AS", "AF", "AN", "OC"};
        int day = 1;
        for (const QString& continent : continents) {
            AdifRecord r{{"CALL", QStringLiteral("TEST%1").arg(day)},
                         {"QSO_DATE", QStringLiteral("202601%1").arg(day, 2, 10, QLatin1Char('0'))},
                         {"TIME_ON", "1200"}, {"BAND", "20m"}, {"MODE", "CW"}, {"CONT", continent}};
            QCOMPARE(db.insertQso(r, "test").status, InsertResult::Status::Inserted);
            ++day;
        }

        const AwardCalculator calc;
        const AwardResult wac = find(calc.compute(db, AwardFilter{}), "wac");
        // AN può restare visibile come elemento lavorato, ma non può aumentare
        // il conteggio ufficiale WAC. OC completa invece il sesto continente.
        QCOMPARE(wac.worked(), 7);
        QCOMPARE(wac.worked(true), 6);
        QCOMPARE(wac.confirmed(true), 0);
        const auto totals = wac.bandTotals({QStringLiteral("20m")}, true);
        QCOMPARE(totals.size(), 1);
        QCOMPARE(totals.first().worked, 6);
        const auto antarctica = std::find_if(wac.items.cbegin(), wac.items.cend(),
                                             [](const AwardItem& item) { return item.key == QStringLiteral("AN"); });
        QVERIFY(antarctica != wac.items.cend());
        QCOMPARE(antarctica->countsForAward, false);
    }

    void eachAwardHasItsConfirmations()
    {
        // Le regole del diploma dentro le scelte generali: DXCC e WAS solo
        // LoTW e cartolina; gli altri quelle generali.
        AwardFilter general;
        QCOMPARE(awards::creditsFor("dxcc", general), QStringList({"lotw", "card"}));
        QCOMPARE(awards::creditsFor("waz", general), QStringList({"lotw", "card"}));
        general.confirmEqsl = true;
        QCOMPARE(awards::creditsFor("dxcc", general), QStringList({"lotw", "card"}));
        QCOMPARE(awards::creditsFor("waz", general), QStringList({"lotw", "card", "eqsl"}));
        // Chi spegne LoTW lo spegne anche per il DXCC.
        general.confirmLotw = false;
        QCOMPARE(awards::creditsFor("dxcc", general), QStringList({"card"}));
        // Scelte a mano: vincono su tutto, anche la lista vuota.
        general.credits.insert("dxcc", {"eqsl", "qrz"});
        general.credits.insert("wpx", {});
        QCOMPARE(awards::creditsFor("dxcc", general), QStringList({"eqsl", "qrz"}));
        QVERIFY(awards::creditsFor("wpx", general).isEmpty());

        // Nel conto: un QSO confermato solo da QRZ e uno solo da eQSL.
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        AdifRecord a{{"CALL", "JA1XX"}, {"QSO_DATE", "20260301"}, {"TIME_ON", "1200"}, {"BAND", "20m"},
                     {"MODE", "FT8"}, {"DXCC", "339"}, {"CQZ", "25"}, {"QRZCOM_QSO_DOWNLOAD_STATUS", "Y"}};
        AdifRecord b{{"CALL", "ZS6XX"}, {"QSO_DATE", "20260302"}, {"TIME_ON", "1200"}, {"BAND", "20m"},
                     {"MODE", "FT8"}, {"DXCC", "462"}, {"CQZ", "38"}, {"EQSL_QSL_RCVD", "Y"}};
        QCOMPARE(db.insertQso(a, "import").status, InsertResult::Status::Inserted);
        QCOMPARE(db.insertQso(b, "import").status, InsertResult::Status::Inserted);
        const AwardCalculator calc;
        AwardFilter filter;
        QCOMPARE(find(calc.compute(db, filter), "dxcc").confirmed(), 0);
        QCOMPARE(find(calc.compute(db, filter), "waz").confirmed(), 0);
        filter.credits.insert("waz", {"qrz"});
        filter.credits.insert("dxcc", {"eqsl"});
        const auto results = calc.compute(db, filter);
        QCOMPARE(find(results, "waz").confirmed(), 1);    // la zona 25, da QRZ
        QCOMPARE(find(results, "dxcc").confirmed(), 1);   // 462, da eQSL
        QCOMPARE(find(results, "wpx").confirmed(), 0);    // le generali: LoTW e cartolina
    }

    void sixtyMetersCountUnlessTheOperatorWantsArrlRules()
    {
        // Il profilo ufficiale esclude i 60 metri da DXCC e WAS; l'override
        // personale li riattiva. Per gli altri diplomi (WAZ) valgono sempre.
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        auto qso = [&db](const char* call, const char* band, int dxcc, int cqz, const char* state) {
            AdifRecord r{{"CALL", call}, {"QSO_DATE", "20260301"}, {"TIME_ON", "1200"}, {"BAND", band},
                         {"MODE", "CW"}, {"DXCC", QString::number(dxcc)}, {"CQZ", QString::number(cqz)},
                         {"LOTW_QSL_RCVD", "Y"}};
            if (*state)
                r.set("STATE", state);
            QCOMPARE(db.insertQso(r, "import").status, InsertResult::Status::Inserted);
        };
        qso("W1AW", "20m", 291, 5, "CT");
        qso("DL1ABC", "60m", 230, 14, "");
        qso("K5XX", "60m", 291, 4, "TX");

        const AwardCalculator calc;
        AwardFilter personal;
        personal.count60m = true;
        const auto counted = calc.compute(db, personal);
        QCOMPARE(find(counted, "dxcc").worked(), 2);  // 291 e 230, anche dai 60
        QCOMPARE(find(counted, "dxcc").bandTotals({"60m"}).first().worked, 2);
        QCOMPARE(find(counted, "was").worked(), 2);   // CT e TX
        QVERIFY(find(counted, "dxcc").requirement.contains(QStringLiteral("60 m")));

        AwardFilter arrl;
        arrl.count60m = false;
        const auto results = calc.compute(db, arrl);
        const auto dxcc = find(results, "dxcc");
        QCOMPARE(dxcc.worked(), 1);           // solo 291 dai 20 metri
        QCOMPARE(dxcc.confirmed(), 1);
        QCOMPARE(dxcc.bandTotals({"60m"}).first().worked, 0);
        QVERIFY(!dxcc.requirement.isEmpty());
        QCOMPARE(find(results, "was").worked(), 1);   // CT si', TX dai 60 no
        QCOMPARE(find(results, "waz").worked(), 3);   // il WAZ non e' dell'ARRL
    }

    void aNewAwardProfileUsesOfficialRulesForSixtyMeters()
    {
        const AwardFilter freshProfile;
        QVERIFY(!freshProfile.count60m);
    }

    void satelliteQsosStayInTheLogButDoNotCountForTerrestrialDxccAwards()
    {
        LogDatabase db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        AdifRecord r{{"CALL", "AO-TEST"}, {"QSO_DATE", "20260303"}, {"TIME_ON", "1200"},
                     {"BAND", "2m"}, {"MODE", "FM"}, {"DXCC", "291"}, {"STATE", "CT"},
                     {"PROP_MODE", "SAT"}, {"SAT_NAME", "AO-91"}, {"LOTW_QSL_RCVD", "Y"}};
        QCOMPARE(db.insertQso(r, QStringLiteral("import")).status, InsertResult::Status::Inserted);

        const AwardCalculator calc;
        const auto results = calc.compute(db, AwardFilter{});
        QCOMPARE(find(results, "dxcc").worked(), 0);
        QCOMPARE(find(results, "dxcc_sat").worked(), 1);
        QCOMPARE(find(results, "dxcc_sat").confirmed(), 1);
        QCOMPARE(find(results, "was").worked(), 0);
        QVERIFY(find(results, "dxcc").requirement.contains(QStringLiteral("Satellite")));
        // La validita' per il diploma non deve cancellare ne' alterare il QSO.
        QCOMPARE(db.qsoCount(), 1);
    }

    void satelliteDxccSeparatesConfirmedAndTerrestrial()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        auto add = [&db](const char* call, const char* dxcc, const char* band, const char* prop, const char* lotw, const char* eqsl) {
            AdifRecord r{{"CALL", call}, {"QSO_DATE", "20261113"}, {"TIME_ON", "120000"}, {"BAND", band},
                         {"MODE", "FT8"}, {"DXCC", dxcc}, {"PROP_MODE", prop}, {"LOTW_QSL_RCVD", lotw}, {"EQSL_QSL_RCVD", eqsl}};
            QCOMPARE(db.insertQso(r, "import").status, InsertResult::Status::Inserted);
        };
        add("C50A", "422", "13cm", "SAT", "Y", "N");
        add("S21WD", "305", "13cm", "SAT", "N", "Y");
        add("K1ABC", "291", "20m", "", "Y", "N");
        AwardFilter f; f.confirmEqsl = true;
        auto results = AwardCalculator().compute(db, f);
        QCOMPARE(find(results, "dxcc").worked(), 1);
        QCOMPARE(find(results, "dxcc").confirmed(), 1);
        QCOMPARE(find(results, "dxcc_sat").worked(), 2);
        QCOMPARE(find(results, "dxcc_sat").confirmed(), 1);
        f.band = "20m";
        QCOMPARE(find(AwardCalculator().compute(db, f), "dxcc_sat").worked(), 0);
        f.band = "13cm";
        f.confirmLotw = false;
        QCOMPARE(find(AwardCalculator().compute(db, f), "dxcc_sat").confirmed(), 0);
    }

    void contributionIdsFollowAwardAndFilters()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        const auto a = db.insertQso(AdifRecord{{"CALL", "KH6AAA"}, {"QSO_DATE", "20261101"}, {"TIME_ON", "120000"},
            {"BAND", "20m"}, {"MODE", "FT8"}, {"DXCC", "110"}, {"STATE", "HI"}}, "import");
        const auto b = db.insertQso(AdifRecord{{"CALL", "KH6BBB"}, {"QSO_DATE", "20261102"}, {"TIME_ON", "120000"},
            {"BAND", "40m"}, {"MODE", "CW"}, {"DXCC", "110"}, {"STATE", "HI"}}, "import");
        const auto c = db.insertQso(AdifRecord{{"CALL", "K1AAA"}, {"QSO_DATE", "20261103"}, {"TIME_ON", "120000"},
            {"BAND", "20m"}, {"MODE", "FT8"}, {"DXCC", "291"}, {"STATE", "CT"}}, "import");
        QVERIFY(a.id > 0 && b.id > 0 && c.id > 0);
        AwardContributions contributions; contributions.awards.insert("was");
        AwardFilter filter;
        AwardCalculator().compute(db, filter, &contributions);
        QCOMPARE(contributions.qsoIds.value("was").value("HI"), QSet<qint64>({a.id, b.id}));
        QCOMPARE(contributions.qsoIds.value("was").value("CT"), QSet<qint64>({c.id}));
        filter.band = "20m";
        AwardCalculator().compute(db, filter, &contributions);
        QCOMPARE(contributions.qsoIds.value("was").value("HI"), QSet<qint64>({a.id}));
    }

    void satelliteDxccExternalAdif()
    {
        const QString path = qEnvironmentVariable("DECODXLOG_TEST_SAT_ADIF");
        if (path.isEmpty()) QSKIP("set DECODXLOG_TEST_SAT_ADIF to an isolated ADIF fixture");
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
        LogDatabase db; QVERIFY(db.open(":memory:"));
        const auto imported = db.importAdif(file.readAll());
        QCOMPARE(imported.inserted, 15);
        const auto results = AwardCalculator().compute(db, AwardFilter{});
        QCOMPARE(find(results, "dxcc").worked(), 0);
        QCOMPARE(find(results, "dxcc_sat").worked(), 12);
        QCOMPARE(find(results, "dxcc_sat").confirmed(), 4);
    }

    void filterByProfileAndTag()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        const qint64 home = db.saveStationProfile(StationProfile{.name = "Casa", .stationCallsign = "IU8LMC"});
        const qint64 park = db.saveStationProfile(StationProfile{.name = "Parco", .stationCallsign = "IU8LMC/P"});
        auto qso = [&db](const char* call, int dxcc, qint64 profile, const char* tags) {
            AdifRecord r{{"CALL", call}, {"QSO_DATE", "20260101"}, {"TIME_ON", "1200"}, {"BAND", "20m"},
                         {"MODE", "FT8"}, {"DXCC", QString::number(dxcc)}, {"APP_DECOLOG_TAGS", tags}};
            QCOMPARE(db.insertQso(r, "import", {}, false, profile).status, InsertResult::Status::Inserted);
        };
        qso("W1AW", 291, home, "");
        qso("JA1XX", 339, park, "pota,Field Day");
        qso("EA8ABC", 29, park, "sota");

        const AwardCalculator calc;
        AwardFilter byProfile;
        byProfile.stationProfileId = park;
        QCOMPARE(find(calc.compute(db, byProfile), "dxcc").worked(), 2);
        AwardFilter byTag;
        byTag.tag = "POTA";
        const auto pota = find(calc.compute(db, byTag), "dxcc");
        QCOMPARE(pota.worked(), 1);
        QCOMPARE(pota.items.first().key, QString("339"));
        byTag.tag = "field day";
        QCOMPARE(find(calc.compute(db, byTag), "dxcc").worked(), 1);
    }

    void officialCountsDoNotMixExplicitStationAccounts()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        StationProfile home{.name = "Casa", .stationCallsign = "IU8LMC", .myDxcc = 248};
        StationProfile portable{.name = "Portatile", .stationCallsign = "IU8LMC/P", .myDxcc = 291};
        const qint64 homeId = db.saveStationProfile(home);
        const qint64 portableId = db.saveStationProfile(portable);
        auto add = [&db](const char* call, int dxcc, qint64 profile) {
            AdifRecord r{{"CALL", call}, {"QSO_DATE", "20260101"}, {"TIME_ON", "1200"},
                         {"BAND", "20m"}, {"MODE", "CW"}, {"DXCC", QString::number(dxcc)}};
            QCOMPARE(db.insertQso(r, "import", {}, false, profile).status, InsertResult::Status::Inserted);
        };
        add("EA5ABC",  29,         homeId);
        add("W1AW",    291,         portableId);

        const AwardCalculator calc;
        AwardFilter homeFilter;
        homeFilter.stationProfileId = homeId;
        AwardFilter portableFilter;
        portableFilter.stationProfileId = portableId;
        QCOMPARE(find(calc.compute(db, homeFilter), "dxcc").worked(), 1);
        QCOMPARE(find(calc.compute(db, portableFilter), "dxcc").worked(), 1);
        // Il totale ufficiale di ciascun account non ingloba l'altro profilo.
        QVERIFY(find(calc.compute(db, homeFilter), "dxcc").items.first().key !=
                find(calc.compute(db, portableFilter), "dxcc").items.first().key);
    }

    void historicalAwardAccountFiltersByProfileAndValidity()
    {
        LogDatabase db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        StationProfile home;
        home.name = QStringLiteral("Casa storica");
        home.stationCallsign = QStringLiteral("IU8LMC");
        home.uuid = QStringLiteral("account-home");
        home.awardAccount = QStringLiteral("account-home");
        home.operatingLocation = QStringLiteral("JN70");
        home.validFrom = QStringLiteral("2020-01-01");
        home.validTo = QStringLiteral("2025-12-31");
        const qint64 homeId = db.saveStationProfile(home);
        QVERIFY2(homeId > 0, qPrintable(db.lastError()));
        auto add = [&db, homeId](const char* call, const char* date) {
            AdifRecord r{{"CALL", call}, {"QSO_DATE", date}, {"TIME_ON", "1200"},
                         {"BAND", "20m"}, {"MODE", "CW"}, {"DXCC", "291"}};
            QCOMPARE(db.insertQso(r, "test", {}, false, homeId).status, InsertResult::Status::Inserted);
        };
        add("W1OLD", "20250101");
        add("W1NEW", "20260101");

        AwardFilter account;
        account.awardAccountUuid = QStringLiteral("account-home");
        const AwardResult dxcc = find(AwardCalculator().compute(db, account), "dxcc");
        QCOMPARE(dxcc.worked(), 1);
        QCOMPARE(dxcc.items.first().firstCall, QStringLiteral("W1OLD"));
    }

    // ── WAC, WAJA, AJD ───────────────────────────────────────────────────────

    void theSixContinentsAreCountedForWac()
    {
        LogDatabase db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        auto worked = [&db](const QString& call, const QString& cont, const QString& band) {
            AdifRecord r;
            r.set(QStringLiteral("CALL"), call);
            r.set(QStringLiteral("QSO_DATE"), QStringLiteral("20260918"));
            r.set(QStringLiteral("TIME_ON"), QStringLiteral("120000"));
            r.set(QStringLiteral("BAND"), band);
            r.set(QStringLiteral("MODE"), QStringLiteral("CW"));
            r.set(QStringLiteral("CONT"), cont);
            r.set(QStringLiteral("DXCC"), QStringLiteral("100"));
            db.insertQso(r, QStringLiteral("test"));
        };
        worked(QStringLiteral("DL9ZZT"), QStringLiteral("EU"), QStringLiteral("20m"));
        worked(QStringLiteral("W1AW"), QStringLiteral("NA"), QStringLiteral("20m"));
        worked(QStringLiteral("PY2ABC"), QStringLiteral("SA"), QStringLiteral("40m"));
        worked(QStringLiteral("JA1ABC"), QStringLiteral("AS"), QStringLiteral("20m"));
        worked(QStringLiteral("ZS6ABC"), QStringLiteral("AF"), QStringLiteral("15m"));
        worked(QStringLiteral("VK3ABC"), QStringLiteral("OC"), QStringLiteral("20m"));
        // L'Antartide non fa numero per il diploma, ma si vede.
        worked(QStringLiteral("DP1POL"), QStringLiteral("AN"), QStringLiteral("20m"));

        AwardCalculator calc;
        const auto results = calc.compute(db, AwardFilter{});
        const auto wac = std::find_if(results.cbegin(), results.cend(),
                                      [](const AwardResult& r) { return r.id == QLatin1String("wac"); });
        QVERIFY(wac != results.cend());
        QCOMPARE(wac->worked(), 7);      // sei continenti piu' l'Antartide, visibili
        QCOMPARE(wac->worked(true), 6);  // AN resta fuori dal conteggio ufficiale
        QCOMPARE(wac->target, 6);
        // Il WAC si fa banda per banda: il 20 metri ne ha cinque.
        const auto totals = wac->bandTotals({QStringLiteral("20m"), QStringLiteral("40m")});
        QCOMPARE(totals.at(0).worked, 5);
        QCOMPARE(totals.at(1).worked, 1);
        const auto officialTotals = wac->bandTotals({QStringLiteral("20m"), QStringLiteral("40m")}, true);
        QCOMPARE(officialTotals.at(0).worked, 4);
        QCOMPARE(officialTotals.at(1).worked, 1);
    }

    void awardFiltersUseTheCanonicalModeGroups()
    {
        LogDatabase db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        auto add = [&db](const char* call, const char* mode, const char* submode, const char* dxcc) {
            AdifRecord r{{"CALL", call}, {"QSO_DATE", "20260101"}, {"TIME_ON", "1200"},
                         {"BAND", "20m"}, {"MODE", mode}, {"DXCC", dxcc}};
            if (submode && *submode)
                r.set(QStringLiteral("SUBMODE"), QString::fromLatin1(submode));
            QCOMPARE(db.insertQso(r, QStringLiteral("test")).status, InsertResult::Status::Inserted);
        };
        add("FT8SUB", "MFSK", "FT8", "291");
        add("C4FMSUB", "C4FM", "", "291");
        add("DMRSUB", "DMR", "", "291");
        add("UNKNOWN", "MFSK", "NOT-A-MODE", "230");

        AwardCalculator calc;
        AwardFilter ft8;
        ft8.modeGroup = QStringLiteral("FT8");
        QCOMPARE(find(calc.compute(db, ft8), "dxcc").worked(), 1);
        AwardFilter phone;
        phone.modeGroup = QStringLiteral("PHONE");
        QCOMPARE(find(calc.compute(db, phone), "dxcc").worked(), 1);
        AwardFilter digital;
        digital.modeGroup = QStringLiteral("DIGITAL");
        QCOMPARE(find(calc.compute(db, digital), "dxcc").worked(), 2);
    }

    void japanesePrefecturesAndDistricts()
    {
        LogDatabase db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        auto worked = [&db](const QString& call, const QString& state) {
            AdifRecord r;
            r.set(QStringLiteral("CALL"), call);
            r.set(QStringLiteral("QSO_DATE"), QStringLiteral("20260918"));
            r.set(QStringLiteral("TIME_ON"), QStringLiteral("120000"));
            r.set(QStringLiteral("BAND"), QStringLiteral("20m"));
            r.set(QStringLiteral("MODE"), QStringLiteral("CW"));
            r.set(QStringLiteral("DXCC"), QStringLiteral("339"));
            r.set(QStringLiteral("STATE"), state);
            db.insertQso(r, QStringLiteral("test"));
        };
        worked(QStringLiteral("JA1ABC"), QStringLiteral("12"));       // Chiba, distretto 1
        worked(QStringLiteral("JA3XYZ"), QStringLiteral("25"));       // Osaka, distretto 3
        worked(QStringLiteral("JH1QRS"), QStringLiteral("JA12"));     // la stessa Chiba, scritta cosi'
        worked(QStringLiteral("JA0TUV"), QStringLiteral("09"));       // Nagano, distretto 0

        AwardCalculator calc;
        const auto results = calc.compute(db, AwardFilter{});
        const auto waja = std::find_if(results.cbegin(), results.cend(),
                                       [](const AwardResult& r) { return r.id == QLatin1String("waja"); });
        const auto ajd = std::find_if(results.cbegin(), results.cend(),
                                      [](const AwardResult& r) { return r.id == QLatin1String("ajd"); });
        QVERIFY(waja != results.cend() && ajd != results.cend());
        QCOMPARE(waja->worked(), 3);     // Chiba contata una volta sola
        QCOMPARE(waja->target, 47);
        QCOMPARE(ajd->worked(), 3);      // distretti 1, 3, 0
        QCOMPARE(ajd->target, 10);
        // Il nome della prefettura si vede, non solo il numero.
        const auto chiba = std::find_if(waja->items.cbegin(), waja->items.cend(),
                                        [](const AwardItem& i) { return i.key == QLatin1String("12"); });
        QVERIFY(chiba != waja->items.cend());
        QCOMPARE(chiba->name, QStringLiteral("Chiba"));
    }

    void thePrefectureIsReadHoweverItIsWritten()
    {
        QCOMPARE(awards::japanPrefecture(QStringLiteral("12")), QStringLiteral("12"));
        QCOMPARE(awards::japanPrefecture(QStringLiteral("JA12")), QStringLiteral("12"));
        QCOMPARE(awards::japanPrefecture(QStringLiteral("12 Chiba")), QStringLiteral("12"));
        QCOMPARE(awards::japanPrefecture(QStringLiteral("01")), QStringLiteral("01"));
        QCOMPARE(awards::japanPrefecture(QStringLiteral("7")), QStringLiteral("07"));
        // Fuori dai 47 non e' una prefettura.
        QVERIFY(awards::japanPrefecture(QStringLiteral("48")).isEmpty());
        QVERIFY(awards::japanPrefecture(QStringLiteral("SN")).isEmpty());
        QVERIFY(awards::japanPrefecture(QString()).isEmpty());
    }

    void theDistrictIsTheDigitOfTheCallsign()
    {
        QCOMPARE(awards::japanDistrict(QStringLiteral("JA1ABC")), QStringLiteral("1"));
        QCOMPARE(awards::japanDistrict(QStringLiteral("JA0TUV")), QStringLiteral("0"));
        QCOMPARE(awards::japanDistrict(QStringLiteral("7K4XYZ")), QStringLiteral("4"));
        QCOMPARE(awards::japanDistrict(QStringLiteral("JH1QRS/3")), QStringLiteral("1"));
        QVERIFY(awards::japanDistrict(QString()).isEmpty());
    }

    void workedAllAfricaCountsTheAfricanEntities()
    {
        LogDatabase db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        auto worked = [&db](const QString& call, const QString& cont, int dxcc, const QString& band) {
            AdifRecord r;
            r.set(QStringLiteral("CALL"), call);
            r.set(QStringLiteral("QSO_DATE"), QStringLiteral("20260918"));
            r.set(QStringLiteral("TIME_ON"), QStringLiteral("120000"));
            r.set(QStringLiteral("BAND"), band);
            r.set(QStringLiteral("MODE"), QStringLiteral("CW"));
            r.set(QStringLiteral("CONT"), cont);
            r.set(QStringLiteral("DXCC"), QString::number(dxcc));
            db.insertQso(r, QStringLiteral("test"));
        };
        worked(QStringLiteral("ZS6ABC"), QStringLiteral("AF"), 462, QStringLiteral("20m"));   // Sudafrica
        worked(QStringLiteral("7X2ABC"), QStringLiteral("AF"), 400, QStringLiteral("20m"));   // Algeria
        worked(QStringLiteral("ZS6XYZ"), QStringLiteral("AF"), 462, QStringLiteral("40m"));   // ancora Sudafrica
        worked(QStringLiteral("DL9ZZT"), QStringLiteral("EU"), 230, QStringLiteral("20m"));   // non conta
        worked(QStringLiteral("ZD8ABC"), QStringLiteral("AF"), 0, QStringLiteral("20m"));     // senza DXCC, non conta

        AwardCalculator calc;
        const auto results = calc.compute(db, AwardFilter{});
        const auto waac = std::find_if(results.cbegin(), results.cend(),
                                       [](const AwardResult& r) { return r.id == QLatin1String("waac"); });
        QVERIFY(waac != results.cend());
        QCOMPARE(waac->worked(), 2);     // due paesi africani, non tre QSO
        // E si legge banda per banda, come gli altri diplomi.
        const auto totals = waac->bandTotals({QStringLiteral("20m"), QStringLiteral("40m")});
        QCOMPARE(totals.at(0).worked, 2);
        QCOMPARE(totals.at(1).worked, 1);
    }

    void historicalDxccRulesExcludeOnlyTheInvalidOperation()
    {
        LogDatabase db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        auto add = [&db](const char* call, const char* date, int dxcc) {
            AdifRecord r{{"CALL", call}, {"QSO_DATE", date}, {"TIME_ON", "1200"},
                         {"BAND", "20m"}, {"MODE", "CW"}, {"DXCC", QString::number(dxcc)}};
            QCOMPARE(db.insertQso(r, QStringLiteral("test")).status, InsertResult::Status::Inserted);
        };
        add("OLD1", "19900101", 291);
        add("NEW1", "20260101", 291);
        add("DELETED", "20260101", 999);

        AwardCalculator calc;
        calc.setDxccRules({
            [](const QString& call, const QDateTime& when) {
                return call == QLatin1String("OLD1") && when.date() < QDate(2000, 1, 1);
            },
            [](int dxcc) { return dxcc == 999; },
        });
        const AwardResult dxcc = find(calc.compute(db, AwardFilter{}), "dxcc");
        QCOMPARE(dxcc.worked(), 1);
        QVERIFY(std::none_of(dxcc.items.cbegin(), dxcc.items.cend(), [](const AwardItem& item) {
            return item.key == QLatin1String("291") && item.qsoCount > 1;
        }));
        // Le righe restano nel log: le regole del diploma non cancellano dati.
        QCOMPARE(db.qsoCount(), 3);
    }

    void japaneseCitiesAndGuns()
    {
        LogDatabase db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        auto worked = [&db](const QString& call, const QString& county, const QString& band) {
            AdifRecord r;
            r.set(QStringLiteral("CALL"), call);
            r.set(QStringLiteral("QSO_DATE"), QStringLiteral("20260918"));
            r.set(QStringLiteral("TIME_ON"), QStringLiteral("120000"));
            r.set(QStringLiteral("BAND"), band);
            r.set(QStringLiteral("MODE"), QStringLiteral("CW"));
            r.set(QStringLiteral("DXCC"), QStringLiteral("339"));
            r.set(QStringLiteral("CNTY"), county);
            db.insertQso(r, QStringLiteral("test"));
        };
        worked(QStringLiteral("JA1ABC"), QStringLiteral("1001"), QStringLiteral("20m"));    // citta', Tokyo
        worked(QStringLiteral("JA1DEF"), QStringLiteral("100105"), QStringLiteral("20m"));  // quartiere, Tokyo
        worked(QStringLiteral("JA1GHI"), QStringLiteral("JCC 1001"), QStringLiteral("40m")); // la stessa citta'
        worked(QStringLiteral("JA3JKL"), QStringLiteral("25007"), QStringLiteral("20m"));   // gun, Osaka
        worked(QStringLiteral("JA3MNO"), QStringLiteral("99001"), QStringLiteral("20m"));   // prefettura che non esiste

        AwardCalculator calc;
        const auto results = calc.compute(db, AwardFilter{});
        const auto jcc = std::find_if(results.cbegin(), results.cend(),
                                      [](const AwardResult& r) { return r.id == QLatin1String("jcc"); });
        const auto jcg = std::find_if(results.cbegin(), results.cend(),
                                      [](const AwardResult& r) { return r.id == QLatin1String("jcg"); });
        QVERIFY(jcc != results.cend() && jcg != results.cend());
        QCOMPARE(jcc->worked(), 2);      // 1001 una volta sola, piu' il quartiere 100105
        QCOMPARE(jcc->target, 100);
        QCOMPARE(jcg->worked(), 1);      // solo il gun di Osaka: 99001 non e' una prefettura
        QCOMPARE(jcg->target, 100);
        // Il numero porta con se' il nome della prefettura.
        const auto tokyo = std::find_if(jcc->items.cbegin(), jcc->items.cend(),
                                        [](const AwardItem& i) { return i.key == QLatin1String("1001"); });
        QVERIFY(tokyo != jcc->items.cend());
        QCOMPARE(tokyo->name, QStringLiteral("Tokyo"));
        // E si legge banda per banda: la citta' 1001 e' stata fatta su due bande.
        const auto totals = jcc->bandTotals({QStringLiteral("20m"), QStringLiteral("40m")});
        QCOMPARE(totals.at(0).worked, 2);
        QCOMPARE(totals.at(1).worked, 1);
    }

    void italianProvincesAreCountedForWaip()
    {
        LogDatabase db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        auto worked = [&db](const QString& call, int dxcc, const QString& state, const QString& band) {
            AdifRecord r;
            r.set(QStringLiteral("CALL"), call);
            r.set(QStringLiteral("QSO_DATE"), QStringLiteral("20260918"));
            r.set(QStringLiteral("TIME_ON"), QStringLiteral("120000"));
            r.set(QStringLiteral("BAND"), band);
            r.set(QStringLiteral("MODE"), QStringLiteral("CW"));
            r.set(QStringLiteral("DXCC"), QString::number(dxcc));
            r.set(QStringLiteral("STATE"), state);
            db.insertQso(r, QStringLiteral("test"));
        };
        worked(QStringLiteral("IU8LMC"), 248, QStringLiteral("NA"), QStringLiteral("20m"));
        worked(QStringLiteral("IK0ABC"), 248, QStringLiteral("RM"), QStringLiteral("20m"));
        worked(QStringLiteral("IZ8XYZ"), 248, QStringLiteral("na"), QStringLiteral("40m"));  // la stessa, in minuscolo
        // La Sardegna e' un'altra entita' DXCC, ma le sue province contano.
        worked(QStringLiteral("IS0ABC"), 225, QStringLiteral("CA"), QStringLiteral("20m"));
        // Una provincia che non esiste, e una stazione che italiana non e'.
        worked(QStringLiteral("IK1QQQ"), 248, QStringLiteral("ZZ"), QStringLiteral("20m"));
        worked(QStringLiteral("DL9ZZT"), 230, QStringLiteral("NA"), QStringLiteral("20m"));

        AwardCalculator calc;
        const auto results = calc.compute(db, AwardFilter{});
        const auto waip = std::find_if(results.cbegin(), results.cend(),
                                       [](const AwardResult& r) { return r.id == QLatin1String("waip"); });
        QVERIFY(waip != results.cend());
        QCOMPARE(waip->worked(), 3);      // NA, RM, CA — non sei QSO
        QCOMPARE(waip->total, 110);
        QCOMPARE(waip->target, 75);
        // La sigla porta con se' il nome, che e' quello che si legge nell'elenco.
        const auto napoli = std::find_if(waip->items.cbegin(), waip->items.cend(),
                                         [](const AwardItem& i) { return i.key == QLatin1String("NA"); });
        QVERIFY(napoli != waip->items.cend());
        QCOMPARE(napoli->name, QStringLiteral("Napoli"));
        // E si legge banda per banda, come gli altri diplomi.
        const auto totals = waip->bandTotals({QStringLiteral("20m"), QStringLiteral("40m")});
        QCOMPARE(totals.at(0).worked, 3);
        QCOMPARE(totals.at(1).worked, 1);
    }

    void theProvinceIsReadHoweverItIsWritten()
    {
        QCOMPARE(awards::italianProvince(QStringLiteral("NA")), QStringLiteral("NA"));
        QCOMPARE(awards::italianProvince(QStringLiteral("na")), QStringLiteral("NA"));
        QCOMPARE(awards::italianProvince(QStringLiteral("I-NA")), QStringLiteral("NA"));
        QCOMPARE(awards::italianProvince(QStringLiteral("NA Napoli")), QStringLiteral("NA"));
        // Carbonia-Iglesias non c'e' piu': i QSO di allora vanno a Sud Sardegna,
        // invece di restare senza provincia.
        QCOMPARE(awards::italianProvince(QStringLiteral("CI")), QStringLiteral("SU"));
        // Le sarde soppresse che il WAIP conta ancora ci sono tutte e tre.
        QCOMPARE(awards::italianProvince(QStringLiteral("OG")), QStringLiteral("OG"));
        QCOMPARE(awards::italianProvince(QStringLiteral("OT")), QStringLiteral("OT"));
        QCOMPARE(awards::italianProvince(QStringLiteral("VS")), QStringLiteral("VS"));
        // Quello che non e' una provincia resta fuori: contarlo vorrebbe dire
        // dire a chi guarda che ha una provincia che non ha lavorato.
        QVERIFY(awards::italianProvince(QStringLiteral("ZZ")).isEmpty());
        QVERIFY(awards::italianProvince(QStringLiteral("")).isEmpty());
        QVERIFY(awards::italianProvince(QStringLiteral("12")).isEmpty());
        // E sono centodieci, come dice il regolamento.
        QCOMPARE(awards::italianProvinces().size(), 110);
    }

    void castlesAreCountedForDci()
    {
        LogDatabase db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        auto worked = [&db](const QString& call, const QString& sig, const QString& sigInfo,
                            const QString& comment, const QString& band) {
            AdifRecord r;
            r.set(QStringLiteral("CALL"), call);
            r.set(QStringLiteral("QSO_DATE"), QStringLiteral("20260918"));
            r.set(QStringLiteral("TIME_ON"), QStringLiteral("120000"));
            r.set(QStringLiteral("BAND"), band);
            r.set(QStringLiteral("MODE"), QStringLiteral("CW"));
            r.set(QStringLiteral("DXCC"), QStringLiteral("248"));
            if (!sig.isEmpty())
                r.set(QStringLiteral("SIG"), sig);
            if (!sigInfo.isEmpty())
                r.set(QStringLiteral("SIG_INFO"), sigInfo);
            if (!comment.isEmpty())
                r.set(QStringLiteral("COMMENT"), comment);
            db.insertQso(r, QStringLiteral("test"));
        };
        // Il log fatto bene: il programma e il riferimento al posto loro.
        worked(QStringLiteral("IK0ABC"), QStringLiteral("DCI"), QStringLiteral("PR001"),
               QString(), QStringLiteral("20m"));
        // Lo stesso castello su un'altra banda, scritto come capita.
        worked(QStringLiteral("IK0DEF"), QString(), QString(),
               QStringLiteral("DCI PR-001 tnx"), QStringLiteral("40m"));
        // Un altro castello, in minuscolo nel commento.
        worked(QStringLiteral("IZ8GHI"), QString(), QString(),
               QStringLiteral("dci na015"), QStringLiteral("20m"));
        // Un numero qualunque nel commento non e' un castello.
        worked(QStringLiteral("IW1JKL"), QString(), QString(),
               QStringLiteral("TNX 001 73"), QStringLiteral("20m"));
        // E nemmeno una provincia che non esiste.
        worked(QStringLiteral("IK2MNO"), QStringLiteral("DCI"), QStringLiteral("ZZ001"),
               QString(), QStringLiteral("20m"));

        AwardCalculator calc;
        const auto results = calc.compute(db, AwardFilter{});
        const auto dci = std::find_if(results.cbegin(), results.cend(),
                                      [](const AwardResult& r) { return r.id == QLatin1String("dci"); });
        QVERIFY(dci != results.cend());
        QCOMPARE(dci->worked(), 2);       // PR001 una volta sola, piu' NA015
        // Il traguardo e' quello del regolamento: 30 castelli.
        QCOMPARE(dci->target, 30);
        const auto parma = std::find_if(dci->items.cbegin(), dci->items.cend(),
                                        [](const AwardItem& i) { return i.key == QLatin1String("PR001"); });
        QVERIFY(parma != dci->items.cend());
        QCOMPARE(parma->name, QStringLiteral("Parma"));
        QCOMPARE(parma->qsoCount, 2);
        const auto totals = dci->bandTotals({QStringLiteral("20m"), QStringLiteral("40m")});
        QCOMPARE(totals.at(0).worked, 2);
        QCOMPARE(totals.at(1).worked, 1);
    }

    // Con DECODXLOG_BENCH_DB=<un log vero>: quanto ci mette a contare tutti i diplomi.
    void benchRealLog()
    {
        const QString path = qEnvironmentVariable("DECODXLOG_BENCH_DB");
        if (path.isEmpty())
            QSKIP("DECODXLOG_BENCH_DB not set");
        LogDatabase db;
        QVERIFY(db.open(path));
        const AwardCalculator calc;
        QElapsedTimer t;
        t.start();
        const auto results = calc.compute(db, AwardFilter{});
        qInfo() << "compute:" << t.elapsed() << "ms," << results.size() << "awards";
    }

    void editsAndDeletesMatchAFullRecompute()
    {
        const auto makeQso = [](const QString& call, const QString& band, const QString& dxcc) {
            return AdifRecord{{QStringLiteral("CALL"), call},
                              {QStringLiteral("QSO_DATE"), QStringLiteral("20260101")},
                              {QStringLiteral("TIME_ON"), QStringLiteral("120000")},
                              {QStringLiteral("BAND"), band},
                              {QStringLiteral("MODE"), QStringLiteral("CW")},
                              {QStringLiteral("DXCC"), dxcc},
                              {QStringLiteral("CQZ"), QStringLiteral("5")}};
        };
        LogDatabase edited;
        LogDatabase rebuilt;
        QVERIFY(edited.open(QStringLiteral(":memory:")));
        QVERIFY(rebuilt.open(QStringLiteral(":memory:")));

        const AdifRecord first = makeQso(QStringLiteral("W1AW"), QStringLiteral("20m"), QStringLiteral("291"));
        const AdifRecord second = makeQso(QStringLiteral("EA8XX"), QStringLiteral("40m"), QStringLiteral("29"));
        const AdifRecord replacement = makeQso(QStringLiteral("KH6XX"), QStringLiteral("15m"), QStringLiteral("110"));
        const auto a = edited.insertQso(first, QStringLiteral("test"));
        const auto b = edited.insertQso(second, QStringLiteral("test"));
        QVERIFY(a.status == InsertResult::Status::Inserted);
        QVERIFY(b.status == InsertResult::Status::Inserted);
        QVERIFY(edited.updateQso(a.id, replacement).status == InsertResult::Status::Inserted);
        QVERIFY(edited.softDeleteQso(b.id));
        QVERIFY(rebuilt.insertQso(replacement, QStringLiteral("rebuild")).status == InsertResult::Status::Inserted);

        const auto actual = AwardCalculator().compute(edited, AwardFilter{});
        const auto expected = AwardCalculator().compute(rebuilt, AwardFilter{});
        QCOMPARE(actual.size(), expected.size());
        for (int i = 0; i < actual.size(); ++i) {
            QCOMPARE(actual.at(i).id, expected.at(i).id);
            QCOMPARE(actual.at(i).worked(), expected.at(i).worked());
            QCOMPARE(actual.at(i).confirmed(), expected.at(i).confirmed());
            QCOMPARE(actual.at(i).target, expected.at(i).target);
            QCOMPARE(actual.at(i).total, expected.at(i).total);
            QCOMPARE(actual.at(i).requirement, expected.at(i).requirement);
        }
    }

    // Benchmark riproducibile: DECODXLOG_BENCH_SYNTHETIC=10000,100000,1000000.
    // Misura importazione, una query temporale rappresentativa e ricalcolo
    // completo dei diplomi.
    void benchSyntheticLog()
    {
        const QString setting = qEnvironmentVariable("DECODXLOG_BENCH_SYNTHETIC");
        if (setting.isEmpty())
            QSKIP("DECODXLOG_BENCH_SYNTHETIC not set");
        for (const QString& token : setting.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            bool ok = false;
            const int count = token.trimmed().toInt(&ok);
            if (!ok || count <= 0)
                QFAIL(qPrintable(QStringLiteral("invalid synthetic size: %1").arg(token)));
            LogDatabase db;
            QVERIFY(db.open(":memory:"));
            QElapsedTimer timer;
            timer.start();
            for (int i = 0; i < count; ++i) {
                const int seconds = i % 86400;
                const AdifRecord record{
                    {"CALL", QStringLiteral("TEST%1AA").arg(i % 100000, 5, 10, QLatin1Char('0'))},
                    {"QSO_DATE", QStringLiteral("202601%1").arg((i % 28) + 1, 2, 10, QLatin1Char('0'))},
                    {"TIME_ON", QStringLiteral("%1%2%3").arg(seconds / 3600, 2, 10, QLatin1Char('0'))
                                                            .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
                                                            .arg(seconds % 60, 2, 10, QLatin1Char('0'))},
                    {"BAND", (i % 2) ? "20m" : "40m"}, {"MODE", (i % 3) ? "FT8" : "CW"},
                    {"DXCC", QString::number((i % 300) + 1)}, {"CQZ", QString::number((i % 40) + 1)}};
                const auto inserted = db.insertQso(record, "synthetic");
                QVERIFY2(inserted.status == InsertResult::Status::Inserted, qPrintable(inserted.message));
            }
            const qint64 importMs = timer.elapsed();
            timer.restart();
            const auto ids = db.qsoIdsBetween(QStringLiteral("2026-01-01T00:00:00"),
                                              QStringLiteral("2026-12-31T23:59:59"));
            const qint64 queryMs = timer.elapsed();
            QCOMPARE(ids.size(), count);
            timer.restart();
            const auto results = AwardCalculator().compute(db, AwardFilter{});
            const qint64 computeMs = timer.elapsed();
            qInfo() << "synthetic:" << count << "qsos, import:" << importMs
                    << "ms, query:" << queryMs << "ms, compute:" << computeMs
                    << "ms, awards:" << results.size();
        }
    }

    void italianRegionsAndSigAwards()
    {
        QCOMPARE(awards::italianRegionNames().size(), 20);
        const QString none;
        QCOMPARE(awards::sigReference("IIA", "IIA", "li-001", none, none), QStringLiteral("LI-001"));
        QCOMPARE(awards::sigReference("DIFI", none, none, "difi: SA 12 tnx", none), QStringLiteral("SA-12"));
        QVERIFY(awards::sigReference("IIA", "DCI", "PR001", none, none).isEmpty());
        QVERIFY(awards::sigReference("DAI", none, none, "TNX 001", none).isEmpty());

        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        auto qso = [&db](const char* call, const char* state, int dxcc, std::initializer_list<AdifField> extra) {
            AdifRecord r{{"CALL", call}, {"QSO_DATE", "20260101"}, {"TIME_ON", "1200"}, {"BAND", "40m"},
                         {"MODE", "SSB"}, {"DXCC", QString::number(dxcc)}, {"STATE", state}};
            for (const auto& f : extra)
                r.set(f.name, f.value);
            QCOMPARE(db.insertQso(r, "import").status, InsertResult::Status::Inserted);
        };
        qso("IK8ABC", "NA", 248, {{"SIG", "IIA"}, {"SIG_INFO", "NA-004"}});
        qso("IK8XYZ", "SA", 248, {{"COMMENT", "DIFI SA-12"}});
        qso("IS0AAA", "CA", 225, {});
        qso("IK1AAA", "TO", 248, {});
        const AwardCalculator calc;
        const auto results = calc.compute(db, AwardFilter{});
        auto find = [&results](const QString& id) {
            for (const auto& r : results)
                if (r.id == id) return r;
            return AwardResult{};
        };
        const AwardResult wair = find("wair");
        QCOMPARE(wair.worked(), 3);       // Campania, Sardegna, Piemonte
        QVERIFY(wair.requirement.contains("Lazio"));
        QCOMPARE(find("iia").worked(), 1);
        QCOMPARE(find("iia").items.first().key, QStringLiteral("NA-004"));
        QCOMPARE(find("iia").items.first().name, QStringLiteral("Napoli"));
        QCOMPARE(find("difi").worked(), 1);
        QCOMPARE(find("dai").worked(), 0);
    }

    void theCastleReferenceIsReadHoweverItIsWritten()
    {
        const QString none;
        QCOMPARE(awards::dciReference(QStringLiteral("DCI"), QStringLiteral("PR001"), none, none),
                 QStringLiteral("PR001"));
        QCOMPARE(awards::dciReference(QStringLiteral("dci"), QStringLiteral("pr-001"), none, none),
                 QStringLiteral("PR001"));
        QCOMPARE(awards::dciReference(none, none, QStringLiteral("DCI PR001"), none),
                 QStringLiteral("PR001"));
        QCOMPARE(awards::dciReference(none, none, none, QStringLiteral("attivo dci: NA-015 oggi")),
                 QStringLiteral("NA015"));
        // Senza la parola DCI davanti, nel testo libero non si indovina.
        QVERIFY(awards::dciReference(none, none, QStringLiteral("PR001"), none).isEmpty());
        QVERIFY(awards::dciReference(none, none, QStringLiteral("TNX 001"), none).isEmpty());
        // Una provincia che non esiste non e' un castello.
        QVERIFY(awards::dciReference(QStringLiteral("DCI"), QStringLiteral("ZZ001"), none, none).isEmpty());
        // Un altro programma nello stesso campo non diventa un castello.
        QVERIFY(awards::dciReference(QStringLiteral("GMA"), QStringLiteral("I/PI-001"), none, none).isEmpty());
    }

    void theDciRulesDecideWhichQsosCount()
    {
        LogDatabase db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        auto worked = [&db](const QString& call, const QString& date, const QString& band,
                            const QString& mode, const QString& ref) {
            AdifRecord r;
            r.set(QStringLiteral("CALL"), call);
            r.set(QStringLiteral("QSO_DATE"), date);
            r.set(QStringLiteral("TIME_ON"), QStringLiteral("120000"));
            r.set(QStringLiteral("BAND"), band);
            r.set(QStringLiteral("MODE"), mode);
            r.set(QStringLiteral("DXCC"), QStringLiteral("248"));
            r.set(QStringLiteral("SIG"), QStringLiteral("DCI"));
            r.set(QStringLiteral("SIG_INFO"), ref);
            db.insertQso(r, QStringLiteral("test"));
        };
        // Questo conta: dopo il 2001, in HF, in CW.
        worked(QStringLiteral("IK0ABC"), QStringLiteral("20260218"), QStringLiteral("20m"),
               QStringLiteral("CW"), QStringLiteral("RM012"));
        // Il diploma vale dai QSO del 1 gennaio 2001: prima no.
        worked(QStringLiteral("IK0DEF"), QStringLiteral("20001231"), QStringLiteral("20m"),
               QStringLiteral("CW"), QStringLiteral("RM013"));
        // Il primo giorno buono invece si', ed e' il confine giusto.
        worked(QStringLiteral("IK0GHI"), QStringLiteral("20010101"), QStringLiteral("20m"),
               QStringLiteral("SSB"), QStringLiteral("RM014"));
        // I modi sono SSB, CW e digitale: la FM no.
        worked(QStringLiteral("IZ8JKL"), QStringLiteral("20260218"), QStringLiteral("20m"),
               QStringLiteral("FM"), QStringLiteral("NA016"));
        // E le bande vanno dai 160 ai 2 metri: i 70 centimetri sono fuori.
        worked(QStringLiteral("IZ8MNO"), QStringLiteral("20260218"), QStringLiteral("70cm"),
               QStringLiteral("SSB"), QStringLiteral("NA017"));
        // I 2 metri invece sono dentro, ed e' l'altro confine.
        worked(QStringLiteral("IZ8PQR"), QStringLiteral("20260218"), QStringLiteral("2m"),
               QStringLiteral("SSB"), QStringLiteral("NA018"));

        AwardCalculator calc;
        const auto results = calc.compute(db, AwardFilter{});
        const auto dci = std::find_if(results.cbegin(), results.cend(),
                                      [](const AwardResult& r) { return r.id == QLatin1String("dci"); });
        QVERIFY(dci != results.cend());
        QCOMPARE(dci->worked(), 3);       // RM012, RM014, NA018
    }

    void theDciSaysHowFarTheDiplomaIs()
    {
        LogDatabase db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        auto worked = [&db](const QString& call, const QString& ref) {
            AdifRecord r;
            r.set(QStringLiteral("CALL"), call);
            r.set(QStringLiteral("QSO_DATE"), QStringLiteral("20260218"));
            r.set(QStringLiteral("TIME_ON"), QStringLiteral("120000"));
            r.set(QStringLiteral("BAND"), QStringLiteral("20m"));
            r.set(QStringLiteral("MODE"), QStringLiteral("CW"));
            r.set(QStringLiteral("DXCC"), QStringLiteral("248"));
            r.set(QStringLiteral("SIG"), QStringLiteral("DCI"));
            r.set(QStringLiteral("SIG_INFO"), ref);
            db.insertQso(r, QStringLiteral("test"));
        };
        worked(QStringLiteral("IK0ABC"), QStringLiteral("RM012"));   // Lazio
        worked(QStringLiteral("IZ8DEF"), QStringLiteral("NA015"));   // Campania
        worked(QStringLiteral("IK2GHI"), QStringLiteral("MI003"));   // Lombardia
        worked(QStringLiteral("IK2JKL"), QStringLiteral("BG007"));   // ancora Lombardia

        AwardCalculator calc;
        auto results = calc.compute(db, AwardFilter{});
        auto dci = std::find_if(results.cbegin(), results.cend(),
                                [](const AwardResult& r) { return r.id == QLatin1String("dci"); });
        QVERIFY(dci != results.cend());
        // Quattro castelli, ma tre regioni: Bergamo e Milano sono la stessa.
        QVERIFY2(dci->requirement.contains(QStringLiteral("4")), qPrintable(dci->requirement));
        QVERIFY2(dci->requirement.contains(QStringLiteral("3")), qPrintable(dci->requirement));
        // E Cuneo non c'e' ancora: il regolamento ne vuole uno.
        const QString withoutCuneo = dci->requirement;

        worked(QStringLiteral("IK1MNO"), QStringLiteral("CN001"));
        results = calc.compute(db, AwardFilter{});
        dci = std::find_if(results.cbegin(), results.cend(),
                           [](const AwardResult& r) { return r.id == QLatin1String("dci"); });
        QVERIFY(dci->requirement != withoutCuneo);
        // Il castello di Cuneo conta anche per il diploma della sua provincia.
        const auto dcpc = std::find_if(results.cbegin(), results.cend(),
                                       [](const AwardResult& r) { return r.id == QLatin1String("dcpc"); });
        QVERIFY(dcpc != results.cend());
        QCOMPARE(dcpc->worked(), 1);
        QCOMPARE(dcpc->target, 10);
    }

    void everyProvinceHasItsRegion()
    {
        // Le venti regioni ci sono tutte, e ogni provincia ne ha una: se una
        // sigla restasse senza, il DCI conterebbe una regione in meno di quelle
        // davvero lavorate.
        QCOMPARE(awards::italianRegions().size(), awards::italianProvinces().size());
        QSet<QString> regions;
        for (const QString& province : awards::italianProvinces().keys()) {
            const QString region = awards::italianRegions().value(province);
            QVERIFY2(!region.isEmpty(), qPrintable(province));
            regions.insert(region);
        }
        QCOMPARE(regions.size(), 20);
        QCOMPARE(awards::italianRegions().value(QStringLiteral("CN")), QStringLiteral("Piemonte"));
        QCOMPARE(awards::italianRegions().value(QStringLiteral("NA")), QStringLiteral("Campania"));
    }

    void theJarlNumberIsReadHoweverItIsWritten()
    {
        QCOMPARE(awards::japanJarlCode(QStringLiteral("1001")), QStringLiteral("1001"));
        QCOMPARE(awards::japanJarlCode(QStringLiteral("10-01")), QStringLiteral("1001"));
        QCOMPARE(awards::japanJarlCode(QStringLiteral("JCC 1001")), QStringLiteral("1001"));
        QCOMPARE(awards::japanJarlCode(QStringLiteral("100105")), QStringLiteral("100105"));
        QCOMPARE(awards::japanJarlCode(QStringLiteral("01001")), QStringLiteral("01001"));
        // Troppo corto, troppo lungo, o una prefettura che non c'e'.
        QVERIFY(awards::japanJarlCode(QStringLiteral("100")).isEmpty());
        QVERIFY(awards::japanJarlCode(QStringLiteral("1234567")).isEmpty());
        QVERIFY(awards::japanJarlCode(QStringLiteral("99001")).isEmpty());
        QVERIFY(awards::japanJarlCode(QStringLiteral("Marion")).isEmpty());
        // Cinque cifre vuol dire distretto, quattro o sei vuol dire citta'.
        QVERIFY(awards::isJapanGun(QStringLiteral("25007")));
        QVERIFY(!awards::isJapanGun(QStringLiteral("1001")));
        QVERIFY(!awards::isJapanGun(QStringLiteral("100105")));
    }

    void declarativeRulePackageIsVersionedBoundedAndAddsAnAward()
    {
        QString error;
        const auto package = awards::AwardRulePackage::fromJson(
            R"({"format":"decodxlog-award-rule-1","id":"club_dx","title":"Club DX","source":"club.example/rules","version":"2026.1","validFrom":"2026-01-01","validTo":"2026-12-31","keyField":"DXCC","target":2,"total":340})",
            &error);
        QVERIFY2(package.has_value(), qPrintable(error));
        const auto reformatted = awards::AwardRulePackage::fromJson(
            R"({ "total":340, "target":2, "keyField":"DXCC", "validTo":"2026-12-31", "validFrom":"2026-01-01", "version":"2026.1", "source":"club.example/rules", "title":"Club DX", "id":"club_dx", "format":"decodxlog-award-rule-1" })",
            &error);
        QVERIFY2(reformatted.has_value(), qPrintable(error));
        QCOMPARE(reformatted->fingerprint, package->fingerprint);

        const auto unsafe = awards::AwardRulePackage::fromJson(
            R"({"format":"decodxlog-award-rule-1","id":"unsafe","title":"Unsafe","source":"x","version":"1","validFrom":"2026-01-01","validTo":"2026-12-31","keyField":"EXEC","target":1})",
            &error);
        QVERIFY(!unsafe.has_value());
        const auto compound = awards::AwardRulePackage::fromJson(
            R"({"format":"decodxlog-award-rule-1","id":"compound_dx","title":"Compound DX","source":"club.example/rules","version":"1","validFrom":"2026-01-01","validTo":"2026-12-31","keyField":"DXCC","conditions":[{"field":"BAND","values":["20m","40m"]},{"field":"STATE","values":"CT"}],"target":1})",
            &error);
        QVERIFY2(compound.has_value(), qPrintable(error));
        const auto unsupportedCondition = awards::AwardRulePackage::fromJson(
            R"({"format":"decodxlog-award-rule-1","id":"bad_condition","title":"Bad","source":"x","version":"1","validFrom":"2026-01-01","validTo":"2026-12-31","keyField":"DXCC","conditions":[{"field":"SCRIPT","values":["x"]}]})",
            &error);
        QVERIFY(!unsupportedCondition.has_value());
        const auto unsafeVersion = awards::AwardRulePackage::fromJson(
            R"({"format":"decodxlog-award-rule-1","id":"unsafe_version","title":"Unsafe","source":"x","version":"../escape","validFrom":"2026-01-01","validTo":"2026-12-31","keyField":"DXCC","target":1})",
            &error);
        QVERIFY(!unsafeVersion.has_value());

        awards::AwardRuleCatalog catalog;
        QVERIFY(catalog.add(*package, &error));
        QVERIFY(catalog.add(*package, &error)); // idempotente, stesso fingerprint
        auto changedJson = QByteArray(
            R"({"format":"decodxlog-award-rule-1","id":"club_dx","title":"Club DX changed","source":"club.example/rules","version":"2026.1","validFrom":"2026-01-01","validTo":"2026-12-31","keyField":"DXCC","target":2,"total":340})");
        const auto changed = awards::AwardRulePackage::fromJson(changedJson, &error);
        QVERIFY(changed.has_value());
        QVERIFY(!catalog.add(*changed, &error));
        QVERIFY(error.contains(QStringLiteral("same rule version")));

        const auto overlapping = awards::AwardRulePackage::fromJson(
            R"({"format":"decodxlog-award-rule-1","id":"club_dx","title":"Club DX 2026b","source":"club.example/rules","version":"2026.2","validFrom":"2026-06-01","validTo":"2027-06-30","keyField":"DXCC","target":3,"total":340})",
            &error);
        QVERIFY(overlapping.has_value());
        QVERIFY(!catalog.add(*overlapping, &error));
        QVERIFY(error.contains(QStringLiteral("overlapping")));

        QTemporaryDir rulesDir;
        QVERIFY(rulesDir.isValid());
        const QByteArray originalJson =
            R"({"format":"decodxlog-award-rule-1","id":"persisted","title":"Persisted","source":"club.example/rules","version":"1","validFrom":"2026-01-01","validTo":"2026-12-31","keyField":"DXCC","target":1,"total":340})";
        const QString rulePath = QDir(rulesDir.path()).filePath(QStringLiteral("persisted.json"));
        QFile ruleFile(rulePath);
        QVERIFY(ruleFile.open(QIODevice::WriteOnly));
        QCOMPARE(ruleFile.write(originalJson), originalJson.size());
        ruleFile.close();
        QStringList loadErrors;
        const auto firstLoad = awards::AwardRuleCatalog::loadDirectory(rulesDir.path(), &loadErrors);
        QVERIFY2(loadErrors.isEmpty(), qPrintable(loadErrors.join('\n')));
        QCOMPARE(firstLoad.packages().size(), 1);
        const QString originalFingerprint = firstLoad.packages().first().fingerprint;
        QVERIFY(QFileInfo::exists(QDir(rulesDir.path()).filePath(QStringLiteral(".history/persisted-1-%1.json").arg(originalFingerprint.left(16)))));

        QVERIFY(ruleFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QByteArray persistedChangedJson = originalJson;
        persistedChangedJson.replace("Persisted", "Changed");
        QCOMPARE(ruleFile.write(persistedChangedJson), persistedChangedJson.size());
        ruleFile.close();
        loadErrors.clear();
        const auto secondLoad = awards::AwardRuleCatalog::loadDirectory(rulesDir.path(), &loadErrors);
        QCOMPARE(secondLoad.packages().size(), 1);
        QCOMPARE(secondLoad.packages().first().fingerprint, originalFingerprint);
        QVERIFY(!loadErrors.isEmpty());

        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        const auto add = [&db](const char* call, const char* date, const char* dxcc) {
            AdifRecord record{{"CALL", call}, {"QSO_DATE", date}, {"TIME_ON", "1200"},
                              {"BAND", "20m"}, {"MODE", "CW"}, {"DXCC", dxcc}};
            QCOMPARE(db.insertQso(record, "test").status, InsertResult::Status::Inserted);
        };
        add("W1AW", "20260102", "291");
        add("DL1ABC", "20270102", "230"); // fuori dalla validita' del pacchetto
        AdifRecord compoundRecord{{"CALL", "W1CT"}, {"QSO_DATE", "20260103"}, {"TIME_ON", "1200"},
                                   {"BAND", "20m"}, {"MODE", "CW"}, {"DXCC", "291"}, {"STATE", "CT"}};
        QCOMPARE(db.insertQso(compoundRecord, "test").status, InsertResult::Status::Inserted);

        AwardCalculator calculator;
        calculator.setRulePackages({*package, *compound});
        const auto results = calculator.compute(db, AwardFilter{});
        const auto club = find(results, QStringLiteral("club_dx"));
        QCOMPARE(club.title, QStringLiteral("Club DX"));
        QCOMPARE(club.worked(), 1);
        QCOMPARE(club.target, 2);
        QCOMPARE(club.total, 340);
        const auto compoundResult = find(results, QStringLiteral("compound_dx"));
        QCOMPARE(compoundResult.worked(), 1);
    }

    void awardClaimSeparatesConfirmationFromSubmissionAndCredit()
    {
        awards::AwardClaim claim;
        claim.id = QStringLiteral("claim-1");
        claim.awardId = QStringLiteral("dxcc");
        claim.accountId = QStringLiteral("home-account");
        claim.category = QStringLiteral("mixed");
        claim.ruleVersion = QStringLiteral("arrl-2026");
        claim.qsoIds = {12, 34};
        claim.createdAt = QDateTime::fromString(QStringLiteral("2026-10-15T12:00:00Z"), Qt::ISODate);
        QString error;

        // Una conferma LoTW può rendere un QSO eleggibile, ma non crea questa domanda.
        QVERIFY(!claim.transitionTo(awards::AwardClaim::State::Credited, claim.createdAt, {}, &error));
        QVERIFY(claim.transitionTo(awards::AwardClaim::State::Submitted, claim.createdAt, {}, &error));
        QVERIFY(!claim.transitionTo(awards::AwardClaim::State::Credited, claim.createdAt, {}, &error));
        QVERIFY(claim.transitionTo(awards::AwardClaim::State::Verified, claim.createdAt.addSecs(60),
                                  QStringLiteral("official review"), &error));
        QVERIFY(claim.transitionTo(awards::AwardClaim::State::Credited, claim.createdAt.addSecs(120),
                                  QStringLiteral("credit granted"), &error));
        QCOMPARE(claim.qsoIds, QList<qint64>({12, 34}));

        const auto restored = awards::AwardClaim::fromJson(claim.toJson(), &error);
        QVERIFY2(restored.has_value(), qPrintable(error));
        QCOMPARE(restored->state, awards::AwardClaim::State::Credited);
        QCOMPARE(restored->awardId, QStringLiteral("dxcc"));
        QCOMPARE(restored->qsoIds, claim.qsoIds);
        QCOMPARE(restored->outcome, QStringLiteral("credit granted"));
        auto credited = *restored;
        QVERIFY(!credited.transitionTo(awards::AwardClaim::State::Submitted,
                                       claim.createdAt.addSecs(180), {}, &error));

        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        error.clear();
        QVERIFY(awards::AwardClaimStore::save(db, {claim}, &error));
        const auto persisted = awards::AwardClaimStore::load(db, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(persisted.size(), 1);
        QCOMPARE(persisted.first().id, QStringLiteral("claim-1"));
        QCOMPARE(persisted.first().state, awards::AwardClaim::State::Credited);
        QCOMPARE(persisted.first().qsoIds, QList<qint64>({12, 34}));

        awards::AwardClaim rejected;
        rejected.id = QStringLiteral("claim-rejected");
        rejected.awardId = QStringLiteral("was");
        rejected.ruleVersion = QStringLiteral("arrl-2026");
        rejected.qsoIds = {99};
        rejected.createdAt = claim.createdAt;
        QVERIFY(rejected.transitionTo(awards::AwardClaim::State::Submitted,
                                      claim.createdAt, {}, &error));
        rejected.evidence = QStringLiteral("https://example.invalid/review/99");
        QVERIFY(rejected.transitionTo(awards::AwardClaim::State::Rejected,
                                      claim.createdAt.addSecs(30), QStringLiteral("not eligible"), &error));
        QVERIFY(!rejected.transitionTo(awards::AwardClaim::State::Credited,
                                       claim.createdAt.addSecs(60), {}, &error));
        const auto rejectedRestored = awards::AwardClaim::fromJson(rejected.toJson(), &error);
        QVERIFY2(rejectedRestored.has_value(), qPrintable(error));
        QCOMPARE(rejectedRestored->state, awards::AwardClaim::State::Rejected);
        QCOMPARE(rejectedRestored->evidence, QStringLiteral("https://example.invalid/review/99"));
    }

    void externalClaimResultsReconcilePerQsoAndAreIdempotent()
    {
        LogDatabase db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        awards::AwardClaim claim;
        claim.id = QStringLiteral("claim-external");
        claim.awardId = QStringLiteral("dxcc");
        claim.accountId = QStringLiteral("home-account");
        claim.category = QStringLiteral("mixed");
        claim.ruleVersion = QStringLiteral("arrl-2026");
        claim.qsoIds = {11, 12, 13};
        claim.createdAt = QDateTime::currentDateTimeUtc();
        QString error;
        QVERIFY(claim.transitionTo(awards::AwardClaim::State::Submitted,
                                   claim.createdAt, {}, &error));
        QVERIFY(awards::AwardClaimStore::save(db, {claim}, &error));
        const QByteArray result = R"({"claimId":"claim-external","provider":"arrl","accountId":"home-account","category":"mixed","decision":"verified","outcome":"partial","evidence":"case/2026-42","creditedQsoIds":[11,12],"rejectedQsoIds":[13]})";
        QVERIFY2(awards::AwardClaimStore::reconcileExternalResult(db, result, &error), qPrintable(error));
        auto claims = awards::AwardClaimStore::load(db, &error);
        QCOMPARE(claims.size(), 1);
        QCOMPARE(claims.first().state, awards::AwardClaim::State::Verified);
        QCOMPARE(claims.first().externalProvider, QStringLiteral("arrl"));
        QCOMPARE(claims.first().creditedQsoIds, QList<qint64>({11, 12}));
        QCOMPARE(claims.first().rejectedQsoIds, QList<qint64>({13}));
        QVERIFY(awards::AwardClaimStore::reconcileExternalResult(db, result, &error));
        const QByteArray conflicting = R"({"claimId":"claim-external","provider":"other","decision":"credited","evidence":"other-case"})";
        QVERIFY(!awards::AwardClaimStore::reconcileExternalResult(db, conflicting, &error));
        QVERIFY(error.contains(QStringLiteral("another provider")));
        const QByteArray wrongAccount = R"({"claimId":"claim-external","provider":"arrl","accountId":"other-account","category":"mixed","decision":"verified","evidence":"case/2026-42"})";
        QVERIFY(!awards::AwardClaimStore::reconcileExternalResult(db, wrongAccount, &error));
        QVERIFY(error.contains(QStringLiteral("account")));
    }

    void gridCountIsExplicitlyStatisticalNotVucc()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        AwardCalculator calculator;
        const auto results = calculator.compute(db, AwardFilter{});
        const auto grids = find(results, QStringLiteral("grids"));
        QVERIFY(!grids.officialRules);
        QCOMPARE(grids.rulesSource, QStringLiteral("https://www.arrl.org/vucc"));
        QCOMPARE(grids.verifier, QStringLiteral("ARRL VUCC award manager"));
        QCOMPARE(grids.title, QStringLiteral("Grid statistics"));
        QVERIFY(!AwardCalculator::awardIds().contains(QStringLiteral("vucc")));
        QVERIFY(!AwardCalculator::awardIds().contains(QStringLiteral("cq_marathon")));
    }

    void unverifiedCountersNeverClaimOfficialEligibility()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        AwardCalculator calculator;
        const auto results = calculator.compute(db, AwardFilter{});
        QVERIFY(!results.isEmpty());
        for (const AwardResult& result : results) {
            if (!result.officialRules)
                QVERIFY2(result.rulesSource.isEmpty() || !result.verifier.isEmpty(),
                         qPrintable(result.id));
        }
    }

    void inferredLocationDoesNotBecomeHistoricalAwardEvidence()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        const InsertResult inserted = db.insertQso(
            AdifRecord{{"CALL", "K1ABC"}, {"QSO_DATE", "20260101"}, {"TIME_ON", "1200"},
                       {"BAND", "20m"}, {"MODE", "CW"}, {"DXCC", "291"},
                       {"CQZ", "5"}, {"STATE", "MA"}, {"GRIDSQUARE", "FN42"}},
            QStringLiteral("manual"));
        QVERIFY(inserted.id > 0);
        QVERIFY(db.recordEnrichment({EnrichmentEntry{inserted.id, QStringLiteral("DXCC"), {}, QStringLiteral("291"),
                                                     QStringLiteral("callbook"), QStringLiteral("inferred"),
                                                     QStringLiteral("2026-10-15T12:00:00Z"), {}, {}}}));

        AwardCalculator calculator;
        const auto results = calculator.compute(db, AwardFilter{});
        const auto dxcc = find(results, QStringLiteral("dxcc"));
        QVERIFY(std::none_of(dxcc.items.cbegin(), dxcc.items.cend(), [](const AwardItem& item) {
            return item.key == QStringLiteral("291");
        }));
    }
};

QTEST_GUILESS_MAIN(TestAwards)
#include "tst_awards.moc"
