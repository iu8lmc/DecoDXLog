#include "core/CqMarathon.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QTest>

using namespace decolog::core::cqmarathon;

class TestCqMarathon : public QObject {
    Q_OBJECT
private slots:
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
