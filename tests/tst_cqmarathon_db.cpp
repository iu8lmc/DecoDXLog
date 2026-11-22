#include "core/CqMarathon.h"
#include "core/LogDatabase.h"

#include <QTest>
#include <QJsonArray>
#include <QJsonObject>

using namespace decolog::core;

class TestCqMarathonDb : public QObject {
    Q_OBJECT
private slots:
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
