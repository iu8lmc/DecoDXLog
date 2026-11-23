#include "core/CqMarathon.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QSqlQuery>
#include <QFile>
#include <QTest>

using namespace decolog::core::cqmarathon;

class TestCqMarathon : public QObject {
    Q_OBJECT
private slots:
    void specialEntitiesRequireMatchingRecordedRegionAndDxcc()
    {
        const QDateTime when(QDate(2026,3,1),QTime(12,0),QTimeZone::UTC);
        struct Case { int dxcc; const char* region; int code; };
        for (const auto &c : {Case{206,"IV",901}, {279,"SI",902}, {248,"AI",903},
                              {248,"SY",904}, {259,"BI",905}, {390,"ET",906}}) {
            Entry e{1,"TEST",c.dxcc,15,"20m","CW",when}; e.region=QString::fromLatin1(c.region).toLower();
            const auto result=select({e},{2026,"mixed",1}); QCOMPARE(result.selected.size(),1);
            QCOMPARE(result.selected.first().entityCode,c.code); QCOMPARE(result.selected.first().dxcc,c.dxcc);
            e.dxcc=291; QCOMPARE(select({e},{2026,"mixed",1}).selected.first().entityCode,291);
            e.dxcc=c.dxcc; e.timestamp=QDateTime(QDate(2025,3,1),QTime(12,0),QTimeZone::UTC);
            QCOMPARE(select({e},{2025,"mixed",1}).selected.first().entityCode,c.dxcc);
        }
    }
    void edition2026FiltersBandAndMobileDesignators()
    {
        const QDateTime when(QDate(2026,3,1),QTime(12,0),QTimeZone::UTC);
        const QList<Entry> entries{{1,"K1AAA",291,5,"20m","CW",when}, {2,"K1BBB",291,4,"2m","CW",when},
            {3,"K1CCC/AM",291,3,"20m","CW",when}, {4,"K1DDD/MM",291,2,"20m","CW",when},
            {5,"K1AM",291,1,"6m","CW",when}, {6,"K1EEE",291,6,"60m","CW",when}};
        const auto r=select(entries,{2026,"mixed",1}); QCOMPARE(r.zoneCandidates.size(),3);
        QCOMPARE(r.zoneCandidates[0].qsoId,qint64(1)); QCOMPARE(r.zoneCandidates[1].qsoId,qint64(5)); QCOMPARE(r.zoneCandidates[2].qsoId,qint64(6));
        QVERIFY(r.exclusions.join('\n').contains("2026 bands")); QVERIFY(r.exclusions.join('\n').contains("aeronautical")); QVERIFY(r.exclusions.join('\n').contains("maritime"));
        QVERIFY(!r.toJson().object().value("officialRulesVerified").toBool());
        Entry historic=entries[1]; historic.timestamp=QDateTime(QDate(2025,3,1),QTime(12,0),QTimeZone::UTC);
        QCOMPARE(select({historic},{2025,"mixed",1}).selected.size(),1); // do not apply unverified edition rules
    }
    void reviewAdifIncludesAllAlternativesAndPreservesFields()
    {
        using namespace decolog::core; LogDatabase db; QVERIFY(db.open(":memory:")); StationProfile p; p.name="A"; p.stationCallsign="K1ABC";
        const auto profile=db.saveStationProfile(p); QVERIFY(profile>0); StationProfile p2; p2.name="B"; p2.stationCallsign="K2ABC"; const auto other=db.saveStationProfile(p2); QVERIFY(other>0);
        auto add=[&](const char* call,const char* date,int dxcc,int zone,qint64 station) {
            return db.insertQso(AdifRecord{{"CALL",call},{"QSO_DATE",date},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},
                {"DXCC",QString::number(dxcc)},{"CQZ",QString::number(zone)},{"APP_TEST","kept"}},"fixture",{},true,station,false).id; };
        QVERIFY(add("K1AAA","20260101",291,5,profile)>0); QVERIFY(add("K1BBB","20260102",291,5,profile)>0);
        QVERIFY(add("K1CCC","20260103",0,3,profile)>0); QVERIFY(add("K1DDD","20250101",291,5,profile)>0); QVERIFY(add("K1EEE","20260101",291,5,other)>0); QVERIFY(add("K1BAD","20260101",291,5,profile)>0);
        QSqlQuery shift(db.connection()); QVERIFY(shift.exec("UPDATE qso SET qso_datetime_on='2027-01-01T00:30:00+01:00',qso_datetime_off='2027-01-01T00:35:00+01:00' WHERE call='K1AAA'"));
        QVERIFY(shift.exec("UPDATE qso SET qso_datetime_on='2026-01-01T12:00:00' WHERE call='K1BAD'"));
        const auto report=reviewAdif(db,2026,profile); QVERIFY2(report.error.isEmpty(),qPrintable(report.error)); QCOMPARE(report.count,3); QCOMPARE(report.invalidTimestamp,1);
        const QString sample=qEnvironmentVariable("DECOLOG_MARATHON_SAMPLE");
        if (!sample.isEmpty()) { QFile file(sample); QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate)); QCOMPARE(file.write(report.bytes),qint64(report.bytes.size())); }
        const auto parsed=adif::parse(report.bytes); QCOMPARE(parsed.records.size(),3);
        QSet<QString> calls; for (const auto &r:parsed.records) { calls.insert(r.value("CALL")); QCOMPARE(r.value("APP_TEST"),QString("kept")); }
        QCOMPARE(parsed.records[0].value("QSO_DATE"),QString("20261231")); QCOMPARE(parsed.records[0].value("TIME_ON"),QString("233000")); QCOMPARE(parsed.records[0].value("TIME_OFF"),QString("233500"));
        QVERIFY(calls.contains("K1AAA") && calls.contains("K1BBB") && calls.contains("K1CCC")); QCOMPARE(db.qsoCount(),qint64(6));
        QVERIFY(!reviewAdif(db,2026,0).error.isEmpty());
        QVERIFY(!reviewAdif(db,2026,profile,"wrong-identity").error.isEmpty()); QVERIFY(reviewAdif(db,2026,profile,db.identity()).error.isEmpty());
        QVERIFY(shift.exec("UPDATE qso SET mode='' WHERE call='K1BBB'")); QVERIFY(!reviewAdif(db,2026,profile).error.isEmpty());
    }
    void zoneDoesNotRequireRecordedDxcc()
    {
        const QDateTime when(QDate(2026,3,1),QTime(12,0),QTimeZone::UTC);
        const QList<Entry> entries{{1,"K1AAA",0,5,"20m","CW",when}, {2,"K2BBB",291,5,"20m","CW",when},
            {3,"K3CCC",0,0,"20m","CW",when}};
        const auto result=select(entries,{2026,"mixed",1}); QCOMPARE(result.zoneCandidates.size(),1); QCOMPARE(result.zoneCandidates.first().qsoId,qint64(1));
        QCOMPARE(result.selected.size(),1); QCOMPARE(result.selected.first().qsoId,qint64(2));
        QVERIFY(result.exclusions.join('\n').contains("no recorded entity or new valid zone"));
    }
    void modeCategoriesDistinguishPhoneCwAndDigital()
    {
        const QDateTime when(QDate(2026, 3, 1), QTime(12, 0), QTimeZone::UTC);
        const QList<Entry> entries{
            {1, QStringLiteral("K1AAA"), 291, 5, QStringLiteral("20m"), QStringLiteral("AM"), when},
            {2, QStringLiteral("DL1AAA"), 230, 14, QStringLiteral("20m"), QStringLiteral("USB"), when},
            {3, QStringLiteral("JA1AAA"), 339, 25, QStringLiteral("20m"), QStringLiteral("FT8"), when},
            {4, QStringLiteral("F1AAA"), 227, 14, QStringLiteral("20m"), QStringLiteral("CW-R"), when},
            {5, QStringLiteral("I1AAA"), 248, 15, QStringLiteral("20m"), QString(), when}
        };
        const auto phone = select(entries, {2026, QStringLiteral("phone"), 1});
        const auto digital = select(entries, {2026, QStringLiteral("digital"), 1});
        const auto cw = select(entries, {2026, QStringLiteral("cw"), 1});
        QCOMPARE(phone.selected.size(), 2);
        QCOMPARE(phone.selected.at(0).qsoId, qint64(1));
        QCOMPARE(phone.selected.at(1).qsoId, qint64(2));
        QCOMPARE(digital.selected.size(), 1);
        QCOMPARE(digital.selected.first().qsoId, qint64(3));
        QCOMPARE(cw.selected.size(), 1);
        QCOMPARE(cw.selected.first().qsoId, qint64(4));
    }

    void selectsZonesIndependentlyOfEntity()
    {
        const QDateTime when(QDate(2026, 3, 1), QTime(12, 0), QTimeZone::UTC);
        const QList<Entry> entries{
            {1, QStringLiteral("K1AAA"), 291, 5, QStringLiteral("20m"), QStringLiteral("CW"), when},
            {2, QStringLiteral("K6BBB"), 291, 3, QStringLiteral("20m"), QStringLiteral("CW"), when},
            {3, QStringLiteral("K7CCC"), 291, 3, QStringLiteral("20m"), QStringLiteral("CW"), when},
            {4, QStringLiteral("DL1ABC"), 230, 0, QStringLiteral("20m"), QStringLiteral("CW"), when},
            {5, QStringLiteral("JA1ABC"), 339, 41, QStringLiteral("20m"), QStringLiteral("CW"), when}
        };
        const auto json = select(entries, {2026, QStringLiteral("mixed"), 1}).toJson().object();
        const auto zones = json.value(QStringLiteral("zoneCandidates")).toArray();
        QCOMPARE(zones.size(), 2);
        QCOMPARE(zones.at(0).toObject().value(QStringLiteral("qsoId")).toInt(), 1);
        QCOMPARE(zones.at(1).toObject().value(QStringLiteral("qsoId")).toInt(), 2);
        QCOMPARE(zones.at(1).toObject().value(QStringLiteral("cqZone")).toInt(), 3);
        QCOMPARE(json.value(QStringLiteral("selected")).toArray().size(), 3);
        QVERIFY(!json.value(QStringLiteral("officialRulesVerified")).toBool(true));
    }

    void editionUsesUtcAcrossYearBoundary()
    {
        const auto local = QDateTime::fromString(QStringLiteral("2027-01-01T00:30:00+01:00"), Qt::ISODate);
        const auto utc = local.toUTC();
        Entry a{1, QStringLiteral("K1AAA"), 291, 5, QStringLiteral("20m"), QStringLiteral("CW"), local};
        Entry b = a;
        b.timestamp = utc;
        const Rules rules{2026, QStringLiteral("mixed"), 1};
        QCOMPARE(select({a}, rules).selected.size(), 1);
        QCOMPARE(select({b}, rules).selected.size(), 1);
        QCOMPARE(select({a}, rules).toJson().object().value(QStringLiteral("selected")),
                 select({b}, rules).toJson().object().value(QStringLiteral("selected")));
        a.timestamp = QDateTime::fromString(QStringLiteral("2026-12-31T23:30:00-01:00"), Qt::ISODate);
        b.timestamp = a.timestamp.toUTC();
        QVERIFY(select({a}, rules).selected.isEmpty());
        QVERIFY(select({b}, rules).selected.isEmpty());
        a.timestamp=QDateTime::fromString(QStringLiteral("2026-01-01T12:00:00"),Qt::ISODate);
        const auto unknown=select({a},rules); QVERIFY(unknown.selected.isEmpty()); QVERIFY(unknown.exclusions.join('\n').contains("UTC timestamp"));
    }

    void selectsDeterministicallyByEditionCategoryAndEntity()
    {
        const QDateTime t1(QDate(2026, 1, 2), QTime(10, 0), QTimeZone::UTC);
        const QDateTime t2(QDate(2026, 1, 2), QTime(11, 0), QTimeZone::UTC);
        QList<Entry> entries{
            {2, QStringLiteral("K1AAA"), 291, 5, QStringLiteral("20m"), QStringLiteral("CW"), t2},
            {1, QStringLiteral("K2BBB"), 291, 5, QStringLiteral("20m"), QStringLiteral("CW"), t1},
            {3, QStringLiteral("DL1ABC"), 230, 14, QStringLiteral("20m"), QStringLiteral("SSB"), t1},
            {4, QStringLiteral("JA1XYZ"), 339, 25, QStringLiteral("20m"), QStringLiteral("CW"), QDateTime(QDate(2026, 1, 3), QTime(9, 0), QTimeZone::UTC)},
        };
        Rules rules{2026, QStringLiteral("cw"), 1};
        const Result result = select(entries, rules);
        QCOMPARE(result.selected.size(), 2);
        QCOMPARE(result.selected.at(0).qsoId, qint64(1));
        QCOMPARE(result.selected.at(1).qsoId, qint64(4));
        QVERIFY(result.exclusions.join('\n').contains(QStringLiteral("entity limit")));
        QVERIFY(result.exclusions.join('\n').contains(QStringLiteral("category mismatch")));
        rules.stationProfileId = 7;
        Entry selected = entries.at(0);
        selected.stationProfileId = 7;
        Entry other = selected;
        other.qsoId = 10;
        other.stationProfileId = 8;
        const auto scoped = select({selected, other}, rules);
        QCOMPARE(scoped.selected.size(), 1);
        QVERIFY(scoped.exclusions.join('\n').contains(QStringLiteral("station profile")));
        const QJsonObject exported = result.toJson().object();
        QCOMPARE(exported.value(QStringLiteral("format")).toString(), QStringLiteral("decodxlog-cq-marathon-1"));
        QCOMPARE(exported.value(QStringLiteral("edition")).toInt(), 2026);
    }

    void invalidRulesAreRejectedExplicitly()
    {
        Rules bad{2026, QStringLiteral("unknown"), 1};
        const Result result = select({}, bad);
        QVERIFY(result.selected.isEmpty());
        QVERIFY(result.exclusions.first().contains(QStringLiteral("unsupported")));
    }
};

QTEST_MAIN(TestCqMarathon)
#include "tst_cqmarathon.moc"
