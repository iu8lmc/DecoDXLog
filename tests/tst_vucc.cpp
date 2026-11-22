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

    void preservesOwnBoundaryLocatorThroughImportExport()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString path = temp.filePath("log.sqlite");
        const QByteArray input = "<CALL:5>K1AAA<QSO_DATE:8>20260101<TIME_ON:4>1200<BAND:2>6m<MODE:2>CW"
                                 "<QSL_RCVD:1>Y<VUCC_GRIDS:9>EM98,FM08<MY_VUCC_GRIDS:9>JN52,JN53<EOR>";
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
        const auto preview = select(entries, Rules{{}, {}, "6m", 100});
        QCOMPARE(preview.eligible.size(), 2);
        const auto exported = preview.toJson().object().value("eligible").toArray().first().toObject();
        QCOMPARE(exported.value("reportedMyVuccGrids").toString(), QStringLiteral("JN52,JN53"));
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
        const Result result = select(entries, Rules{QStringLiteral("home"), QStringLiteral("JM67"), QStringLiteral("6m"), 100});
        QCOMPARE(result.eligible.size(), 2);
        QCOMPARE(result.eligible.at(0).grid, QStringLiteral("JN52"));
        QVERIFY(result.exclusions.join('\n').contains(QStringLiteral("already credited")));
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
