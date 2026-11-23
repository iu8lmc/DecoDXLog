#include "core/CqMarathon.h"
#include "core/LogDatabase.h"

#include <QTest>
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlQuery>

using namespace decolog::core;

class TestCqMarathonDb : public QObject {
    Q_OBJECT
private slots:
    void adifContainerModesKeepVoiceAndDataDistinct()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        for (const auto& item : {qMakePair("MFSK","FT4"),qMakePair("DIGITALVOICE","DMR")}) {
            AdifRecord rec{{"CALL",item.first==QString("MFSK")?"K1AAA":"K2BBB"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},
                           {"BAND","20m"},{"MODE",item.first},{"SUBMODE",item.second},{"DXCC","291"},{"CQZ",item.first==QString("MFSK")?"5":"3"}};
            QCOMPARE(db.insertQso(rec,"test").status,InsertResult::Status::Inserted);
        }
        const auto entries=cqmarathon::loadEntries(db);
        QCOMPARE(cqmarathon::select(entries,{2026,"digital",1}).selected.first().call,QString("K1AAA"));
        QCOMPARE(cqmarathon::select(entries,{2026,"phone",1}).selected.first().call,QString("K2BBB"));
    }
    void recordedFrequencyConflictsAreVisible()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        for (const auto& item : {qMakePair("K1AAA","7.074"),qMakePair("K2BBB","14.074"),qMakePair("K3CCC","")}) {
            AdifRecord rec{{"CALL",item.first},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},
                           {"DXCC","291"},{"CQZ",QString::number(1+db.qsoCount())},{"FREQ",item.second}};
            QCOMPARE(db.insertQso(rec,"test").status,InsertResult::Status::Inserted);
        }
        const auto result=cqmarathon::select(cqmarathon::loadEntries(db),{2026,"mixed",1});
        QCOMPARE(result.zoneCandidates.size(),3);
        const auto warnings=result.warnings.join('\n');
        QVERIFY(warnings.contains("QSO 1: recorded frequency 7.074 MHz outside recorded band 20m"));
        QVERIFY(!warnings.contains("QSO 2: recorded frequency")); QVERIFY(!warnings.contains("QSO 3: recorded frequency"));
        QSqlQuery update(db.connection()); QVERIFY(update.exec("UPDATE qso SET freq=14.35 WHERE call='K1AAA'"));
        QVERIFY(update.exec("UPDATE qso SET freq='invalid' WHERE call='K3CCC'"));
        const auto updated=cqmarathon::select(cqmarathon::loadEntries(db),{2026,"mixed",1});
        QVERIFY(!updated.warnings.join('\n').contains("QSO 1: recorded frequency"));
        QVERIFY(updated.warnings.join('\n').contains("QSO 3: invalid recorded frequency"));
        QVERIFY(update.exec("UPDATE qso SET freq=14.350001 WHERE call='K1AAA'"));
        QVERIFY(cqmarathon::select(cqmarathon::loadEntries(db),{2026,"mixed",1}).warnings.join('\n').contains("QSO 1: recorded frequency"));
    }
    void excludesRecordedRelayPropagationFor2026()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        for (const auto& item : {qMakePair("K1AAA","RPT"),qMakePair("K2BBB","ECH"),qMakePair("K3CCC","IRL"),
                                 qMakePair("K4DDD","INTERNET"),qMakePair("K5EEE","SAT"),qMakePair("K6FFF","EME"),qMakePair("K7GGG","")}) {
            AdifRecord rec{{"CALL",item.first},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},
                           {"DXCC","291"},{"CQZ",QString::number(1+db.qsoCount())},{"PROP_MODE",item.second}};
            QCOMPARE(db.insertQso(rec,"test").status,InsertResult::Status::Inserted);
        }
        const auto result=cqmarathon::select(cqmarathon::loadEntries(db),{2026,"mixed",1});
        QCOMPARE(result.zoneCandidates.size(),2); QCOMPARE(result.zoneCandidates.first().call,QString("K6FFF"));
        QCOMPARE(result.zoneCandidates.last().call,QString("K7GGG"));
        QVERIFY(result.exclusions.join('\n').contains("recorded propagation RPT"));
        QVERIFY(result.exclusions.join('\n').contains("recorded propagation SAT"));
    }
    void warnsWhenRecordedOperatingGridsDiffer()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        for (const auto& item : {qMakePair("K1AAA","JM75FV"),qMakePair("K2BBB","JN58TD"),qMakePair("K3CCC","")}) {
            AdifRecord record{{"CALL",item.first},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},
                              {"DXCC","291"},{"CQZ","5"},{"MY_GRIDSQUARE",item.second}};
            QCOMPARE(db.insertQso(record,"test").status,InsertResult::Status::Inserted);
        }
        const auto result=cqmarathon::select(cqmarathon::loadEntries(db),{2026,"mixed",1});
        QCOMPARE(result.locationGrids,QStringList({"JM75","JN58"}));
        QCOMPARE(result.locationMissing,1); QCOMPARE(result.locationInvalid,0);
        QVERIFY(result.warnings.join('\n').contains("Multiple recorded own grid squares"));
        QCOMPARE(result.selected.size(),1);
        QSqlQuery update(db.connection()); QVERIFY(update.exec("UPDATE qso SET adif_extra='{\"MY_GRIDSQUARE\":7}' WHERE call='K3CCC'"));
        const auto invalid=cqmarathon::select(cqmarathon::loadEntries(db),{2026,"mixed",1});
        QCOMPARE(invalid.locationMissing,0); QCOMPARE(invalid.locationInvalid,1);
        const auto json=invalid.toJson().object(); QCOMPARE(json.value("locationGrids").toArray().size(),2);
    }
    void waeRegionsAreDistinctFromParentDxcc()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        const QList<QPair<QString, QString>> cases{{"I1AAA", ""}, {"IT9AAA", "SY"}, {"IG9AAA", "AI"}};
        for (const auto &item : cases) {
            AdifRecord record{{"CALL", item.first}, {"QSO_DATE", "20260101"}, {"TIME_ON", "1200"},
                              {"BAND", "20m"}, {"MODE", "CW"}, {"DXCC", "248"}, {"REGION", item.second}};
            QCOMPARE(db.insertQso(record, "test").status, InsertResult::Status::Inserted);
        }
        const auto result = cqmarathon::select(cqmarathon::loadEntries(db), {2026, "mixed", 1});
        QCOMPARE(result.selected.size(), 3);
        const auto rows = result.toJson().object().value("selected").toArray();
        QCOMPARE(rows.at(1).toObject().value("entityCode").toInt(), 904);
        QCOMPARE(rows.at(2).toObject().value("entityCode").toInt(), 903);
    }
    void persistedProfilesKeepCandidatesSeparate()
    {
        LogDatabase db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        StationProfile home; home.name = QStringLiteral("Home"); home.stationCallsign = QStringLiteral("IU8LMC");
        StationProfile portable; portable.name = QStringLiteral("Portable"); portable.stationCallsign = QStringLiteral("IU8LMC/P");
        const qint64 homeId = db.saveStationProfile(home);
        const qint64 portableId = db.saveStationProfile(portable);
        QVERIFY(homeId > 0 && portableId > 0 && homeId != portableId);
        AdifRecord first{{"CALL", "K1AAA"}, {"QSO_DATE", "20260101"}, {"TIME_ON", "1200"},
                         {"BAND", "20m"}, {"MODE", "CW"}, {"DXCC", "291"}, {"CQZ", "5"}};
        AdifRecord second{{"CALL", "DL1AAA"}, {"QSO_DATE", "20260101"}, {"TIME_ON", "1200"},
                          {"BAND", "20m"}, {"MODE", "CW"}, {"DXCC", "230"}, {"CQZ", "14"}};
        QCOMPARE(db.insertQso(first, "test", {}, false, homeId).status, InsertResult::Status::Inserted);
        QCOMPARE(db.insertQso(second, "test", {}, false, portableId).status, InsertResult::Status::Inserted);
        QString error;
        const auto entries = cqmarathon::loadEntries(db, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const auto all = cqmarathon::select(entries, {2026, QStringLiteral("mixed"), 1, 0});
        const auto homeOnly = cqmarathon::select(entries, {2026, QStringLiteral("mixed"), 1, homeId});
        QCOMPARE(all.selected.size(), 2);
        QCOMPARE(homeOnly.selected.size(), 1);
        QCOMPARE(homeOnly.selected.first().call, QStringLiteral("K1AAA"));
        QCOMPARE(homeOnly.zoneCandidates.size(), 1);
        QCOMPARE(homeOnly.toJson().object().value(QStringLiteral("stationProfileId")).toInt(), int(homeId));
    }

    void zoneCandidatesUsePersistedCqZones()
    {
        LogDatabase db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        for (const auto& values : {qMakePair("K1AAA", "5"), qMakePair("K6BBB", "3")}) {
            AdifRecord record{{"CALL", values.first}, {"QSO_DATE", "20260101"}, {"TIME_ON", "1200"},
                              {"BAND", "20m"}, {"MODE", "CW"}, {"DXCC", "291"}, {"CQZ", values.second}};
            QCOMPARE(db.insertQso(record, "test").status, InsertResult::Status::Inserted);
        }
        QString error;
        const auto entries = cqmarathon::loadEntries(db, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const auto json = cqmarathon::select(entries, {2026, QStringLiteral("mixed"), 1}).toJson().object();
        QCOMPARE(json.value(QStringLiteral("selected")).toArray().size(), 1);
        QCOMPARE(json.value(QStringLiteral("zoneCandidates")).toArray().size(), 2);
    }

    void loadsOnlyLiveQsosAndFeedsSelector()
    {
        LogDatabase db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        const auto add = [&db](const char* call, const char* date, const char* dxcc, const char* mode) {
            AdifRecord record{{"CALL", call}, {"QSO_DATE", date}, {"TIME_ON", "1200"},
                              {"BAND", "20m"}, {"MODE", mode}, {"DXCC", dxcc}};
            QCOMPARE(db.insertQso(record, "test").status, InsertResult::Status::Inserted);
        };
        add("K1AAA", "20260101", "291", "CW");
        add("K2BBB", "20260102", "291", "CW");
        add("DL1ABC", "20260103", "230", "SSB");
        QString error;
        const auto entries = cqmarathon::loadEntries(db, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(entries.size(), 3);
        const cqmarathon::Result result = cqmarathon::select(entries, {2026, QStringLiteral("cw"), 1});
        QCOMPARE(result.selected.size(), 1);
        QCOMPARE(result.selected.first().call, QStringLiteral("K1AAA"));
        QVERIFY(result.toJson().toJson().contains("decodxlog-cq-marathon-1"));
    }
};

QTEST_MAIN(TestCqMarathonDb)
#include "tst_cqmarathon_db.moc"
