// Entita' DXCC dal nominativo, con il cty.csv incluso nelle risorse.
#include "core/Countries.h"

#include <QFile>
#include <QTest>
#include <algorithm>

using namespace decolog::core;

class TestCountries : public QObject {
    Q_OBJECT

    Countries m_countries;

private slots:
    void initTestCase()
    {
        QFile f(":/decolog/cty.csv");
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(m_countries.load(f.readAll()));
        QVERIFY(m_countries.entityCount() > 330);
        const auto list = m_countries.entities();
        QCOMPARE(list.size(), m_countries.entityCount());
        QVERIFY(std::is_sorted(list.cbegin(), list.cend(), [](const DxccEntity& a, const DxccEntity& b) { return a.dxcc < b.dxcc; }));
        QVERIFY(m_countries.version().startsWith("VER"));
    }

    void lookup_data()
    {
        QTest::addColumn<QString>("call");
        QTest::addColumn<int>("dxcc");
        QTest::addColumn<QString>("name");
        QTest::addColumn<int>("cq");

        QTest::newRow("italy") << "IU8LMC" << 248 << "Italy" << 15;
        QTest::newRow("sicily is still italy") << "IT9ABC" << 248 << "Italy" << 15;
        QTest::newRow("portable") << "IU8LMC/P" << 248 << "Italy" << 15;
        QTest::newRow("canary prefix first") << "EA8/OH2XX" << 29 << "Canary Islands" << 33;
        QTest::newRow("canary prefix last") << "OH2XX/EA8" << 29 << "Canary Islands" << 33;
        QTest::newRow("usa area digit") << "W1AW/4" << 291 << "United States" << 5;
        QTest::newRow("hawaii") << "KH6ABC" << 110 << "Hawaii" << 31;
        QTest::newRow("croatia") << "9A3XY" << 497 << "Croatia" << 15;
        QTest::newRow("japan") << "JA1ZZZ" << 339 << "Japan" << 25;
        QTest::newRow("asiatic russia zone override") << "UA9ABC" << 15 << "Asiatic Russia" << 17;
        QTest::newRow("germany lowercase") << "dl2abc" << 230 << "Fed. Rep. of Germany" << 14;
    }

    void lookup()
    {
        QFETCH(QString, call);
        QFETCH(int, dxcc);
        QFETCH(QString, name);
        QFETCH(int, cq);
        const auto e = m_countries.lookup(call);
        QVERIFY2(e, qPrintable(call));
        QCOMPARE(e->dxcc, dxcc);
        QCOMPARE(e->name, name);
        QCOMPARE(e->cqZone, cq);
    }

    void noEntity()
    {
        QVERIFY(!m_countries.lookup("DL2ABC/MM"));
        QVERIFY(!m_countries.lookup(""));
        QVERIFY(!m_countries.lookup("/P"));
    }

    void longitudeIsEastPositive()
    {
        const auto jp = m_countries.lookup("JA1ZZZ");
        QVERIFY(jp->lon > 100);
        const auto us = m_countries.lookup("W1AW");
        QVERIFY(us->lon < -60);
    }

    // Le eccezioni dell'utente vincono sul cty.csv, sia per nominativo esatto
    // sia per prefisso; le righe sbagliate si saltano senza rompere le altre.
    void exceptionsWin()
    {
        Countries copy = m_countries;
        QCOMPARE(copy.lookup("E51MWA")->name, QString("South Cook Islands"));
        const int applied = copy.addExceptions(
            "# commento\n"
            "=E51MWA 191\n"
            "XX9ZZ 999999\n"       // DXCC sconosciuto: saltata
            "bogus\n"              // una sola colonna: saltata
            "=E51WL 230 extra\n"   // troppe colonne: saltata
            "ZZ1 248  # prefisso\n");
        QCOMPARE(applied, 2);
        const auto north = copy.lookup("E51MWA");
        QVERIFY(north);
        QCOMPARE(north->dxcc, 191);
        QCOMPARE(north->name, QString("North Cook Islands"));
        QCOMPARE(copy.lookup("ZZ1ABC")->dxcc, 248);
        QCOMPARE(copy.lookup("E51XYZ")->dxcc, 234); // il resto del prefisso resta com'era
        QCOMPARE(m_countries.lookup("E51MWA")->dxcc, 234); // l'originale non cambia
    }
};

QTEST_GUILESS_MAIN(TestCountries)
#include "tst_countries.moc"
