// Il cty.xml di Club Log: le entita' con le date (le Antille Olandesi fino al
// 2010, poi Curacao), le eccezioni di un nominativo per qualche giorno, le
// operazioni che non valgono, le zone che cambiano, i suffissi e i prefissi
// davanti al nominativo, e il file compresso come lo manda Club Log.
#include "core/ClubLogCty.h"
#include "core/ClubLogCtyRequest.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>
#include <QUrlQuery>

using namespace decolog::core;

namespace {

const QByteArray kCty = R"(<?xml version="1.0" encoding="UTF-8"?>
<clublog date="2026-09-01T00:00:00+00:00">
<entities>
  <entity><adif>1</adif><name>CANADA</name><prefix>VE</prefix><deleted>FALSE</deleted><cqz>5</cqz><cont>NA</cont></entity>
  <entity><adif>520</adif><name>NETHERLANDS ANTILLES</name><prefix>PJ2</prefix><deleted>TRUE</deleted><cqz>9</cqz><cont>SA</cont>
          <end>2010-10-09T23:59:59+00:00</end></entity>
  <entity><adif>517</adif><name>CURACAO</name><prefix>PJ2</prefix><deleted>FALSE</deleted><cqz>9</cqz><cont>SA</cont>
          <start>2010-10-10T00:00:00+00:00</start></entity>
  <entity><adif>29</adif><name>CANARY ISLANDS</name><prefix>EA8</prefix><deleted>FALSE</deleted><cqz>33</cqz><cont>AF</cont></entity>
  <entity><adif>224</adif><name>FINLAND</name><prefix>OH</prefix><deleted>FALSE</deleted><cqz>15</cqz><cont>EU</cont></entity>
  <entity><adif>175</adif><name>FRENCH POLYNESIA</name><prefix>FO</prefix><deleted>FALSE</deleted><cqz>32</cqz><cont>OC</cont></entity>
</entities>
<exceptions>
  <exception record="1"><call>TX5K</call><entity>FRENCH POLYNESIA</entity><adif>175</adif><cqz>32</cqz><cont>OC</cont>
            <start>2013-03-01T00:00:00+00:00</start><end>2013-03-20T23:59:59+00:00</end></exception>
</exceptions>
<prefixes>
  <prefix record="1"><call>VE</call><entity>CANADA</entity><adif>1</adif><cqz>5</cqz><cont>NA</cont></prefix>
  <prefix record="2"><call>PJ2</call><entity>NETHERLANDS ANTILLES</entity><adif>520</adif><cqz>9</cqz><cont>SA</cont>
         <end>2010-10-09T23:59:59+00:00</end></prefix>
  <prefix record="3"><call>PJ2</call><entity>CURACAO</entity><adif>517</adif><cqz>9</cqz><cont>SA</cont>
         <start>2010-10-10T00:00:00+00:00</start></prefix>
  <prefix record="4"><call>EA8</call><entity>CANARY ISLANDS</entity><adif>29</adif><cqz>33</cqz><cont>AF</cont></prefix>
  <prefix record="5"><call>OH</call><entity>FINLAND</entity><adif>224</adif><cqz>15</cqz><cont>EU</cont></prefix>
</prefixes>
<invalid_operations>
  <invalid record="1"><call>VE3BAD</call><start>2015-01-01T00:00:00+00:00</start><end>2015-12-31T23:59:59+00:00</end></invalid>
</invalid_operations>
<zone_exceptions>
  <zone_exception record="1"><call>VE8AA</call><zone>2</zone></zone_exception>
</zone_exceptions>
</clublog>)";

QDateTime at(int y, int m, int d)
{
    return QDateTime(QDate(y, m, d), QTime(12, 0), QTimeZone::UTC);
}

} // namespace

class TestClubLogCty : public QObject {
    Q_OBJECT

    ClubLogCty m_cty;

private slots:
    void legacyAndPersonalAuthentication()
    {
        const QNetworkRequest legacy = clubLogCtyRequest(QStringLiteral("legacy-password"), QStringLiteral("old-api-key"));
        QCOMPARE(legacy.url().host(), QStringLiteral("cdn.clublog.org"));
        QCOMPARE(QUrlQuery(legacy.url()).queryItemValue(QStringLiteral("api")), QStringLiteral("old-api-key"));
        QVERIFY(legacy.rawHeader("Authorization").isEmpty());
        const QNetworkRequest modern = clubLogCtyRequest(QStringLiteral("cl_fixture"), QStringLiteral("old-api-key"));
        QCOMPARE(modern.url().toString(), QStringLiteral("https://clublog.org/api/v1/reference/cty"));
        QCOMPARE(modern.rawHeader("Authorization"), QByteArray("Bearer cl_fixture"));
        QCOMPARE(modern.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(), int(QNetworkRequest::ManualRedirectPolicy));
        QVERIFY(!modern.url().toString().contains(QStringLiteral("cl_fixture")));
        QVERIFY(clubLogCtyRequest({}, {}).url().isEmpty());
    }

    void liveReferenceFileWhenProvided()
    {
        const QString path = qEnvironmentVariable("DECODXLOG_TEST_CLUBLOG_CTY");
        if (path.isEmpty()) QSKIP("No downloaded reference file supplied");
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QString error;
        const QByteArray data = ClubLogCty::gunzip(file.readAll(), &error);
        QVERIFY2(!data.isEmpty(), qPrintable(error));
        ClubLogCty cty;
        QVERIFY2(cty.load(data, &error), qPrintable(error));
        QVERIFY(cty.entityCount() > 300);
    }
    void initTestCase()
    {
        QString error;
        QVERIFY2(m_cty.load(kCty, &error), qPrintable(error));
        QCOMPARE(m_cty.entityCount(), 6);
        QCOMPARE(m_cty.date(), QDateTime(QDate(2026, 9, 1), QTime(0, 0), QTimeZone::UTC));
        QVERIFY(m_cty.entity(520)->deleted);
    }

    void theEntityOfThatDay()
    {
        // PJ2 nel 2005: le Antille Olandesi, che non ci sono piu'.
        CtyMatch m = m_cty.lookup("PJ2T", at(2005, 3, 1));
        QVERIFY(m.found);
        QCOMPARE(m.adif, 520);
        QVERIFY(m.deleted);
        QCOMPARE(m.name, QString("NETHERLANDS ANTILLES"));
        // Lo stesso nominativo nel 2015: Curacao.
        m = m_cty.lookup("PJ2T", at(2015, 3, 1));
        QCOMPARE(m.adif, 517);
        QVERIFY(!m.deleted);
        // Senza data: quello di adesso.
        QCOMPARE(m_cty.lookup("PJ2T", {}).adif, 517);
    }

    void exceptionsForAFewDays()
    {
        // TX5K a marzo 2013: Polinesia Francese; un altro mese non si sa.
        QCOMPARE(m_cty.lookup("TX5K", at(2013, 3, 10)).adif, 175);
        QVERIFY(!m_cty.lookup("TX5K", at(2014, 3, 10)).found);
    }

    void invalidOperations()
    {
        const CtyMatch m = m_cty.lookup("VE3BAD", at(2015, 6, 1));
        QVERIFY(!m.found);
        QVERIFY(m.invalid);
        // Prima e dopo vale come Canada.
        QCOMPARE(m_cty.lookup("VE3BAD", at(2016, 6, 1)).adif, 1);
    }

    void zonesAndPortables()
    {
        QCOMPARE(m_cty.lookup("VE8AA", at(2020, 1, 1)).cqz, 2);
        QCOMPARE(m_cty.lookup("VE3XX", at(2020, 1, 1)).cqz, 5);
        // Il prefisso davanti o dietro decide; i suffissi no.
        QCOMPARE(m_cty.lookup("EA8/OH2XX", at(2020, 1, 1)).adif, 29);
        QCOMPARE(m_cty.lookup("OH2XX/EA8", at(2020, 1, 1)).adif, 29);
        QCOMPARE(m_cty.lookup("OH2XX/P", at(2020, 1, 1)).adif, 224);
        QCOMPARE(m_cty.lookup("VE3XX/4", at(2020, 1, 1)).adif, 1);
        // In mare o in aria: nessuna entita'.
        const CtyMatch mm = m_cty.lookup("OH2XX/MM", at(2020, 1, 1));
        QVERIFY(!mm.found);
        QVERIFY(mm.noEntity);
        QVERIFY(!m_cty.lookup("ZZ9ZZ", at(2020, 1, 1)).found);
    }

    void compressedFile()
    {
        // Un gzip vero: "<clublog date=...></clublog>".
        const QByteArray gz = QByteArray::fromHex(
            "1f8b08000000000002ffb349ce294dcac94f5748492c49b55532323032d335b0d435300c3130b002236d30a96467a30f556a"
            "0700b4ef170634000000");
        QCOMPARE(ClubLogCty::gunzip(gz), QByteArray("<clublog date=\"2026-09-01T00:00:00+00:00\"></clublog>"));
        QString error;
        QVERIFY(ClubLogCty::gunzip("not gzip", &error).isEmpty());
        QVERIFY(!error.isEmpty());

        QTemporaryDir dir;
        const QString path = dir.filePath("cty.xml");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(kCty);
        f.close();
        ClubLogCty fromFile;
        QVERIFY(fromFile.loadFile(path, &error));
        QCOMPARE(fromFile.lookup("PJ2T", at(2005, 1, 1)).adif, 520);
        // Un file che non e' di Club Log.
        ClubLogCty wrong;
        QVERIFY(!wrong.load("<html></html>", &error));
        QVERIFY(wrong.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestClubLogCty)
#include "tst_clublogcty.moc"
