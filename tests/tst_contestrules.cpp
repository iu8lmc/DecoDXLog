// DecoDXLog — le regole dei contest: punti, moltiplicatori, scambio.
//
// Ogni numero qui dentro viene dal regolamento, e il commento dice da quale
// punto: un punteggio sbagliato non si vede guardando lo schermo, si vede
// quando arriva la classifica, e allora e' tardi.
#include "core/CallHistory.h"
#include "core/ContestRules.h"
#include "core/SharedSerials.h"

#include <QTest>
#include <QTimeZone>

using namespace decolog::core;
using namespace decolog::core::contestrules;

namespace {

// Io sono IU8LMC: Italia, Europa, zona CQ 15, zona ITU 28.
ContestStation me()
{
    return ContestStation{248, QStringLiteral("EU"), 15, 28};
}

ContestQso qso(const QString& call, int dxcc, const QString& cont, const QString& band,
               const QString& mode = QStringLiteral("CW"), const QString& exchange = {},
               int cqZone = 0, int ituZone = 0)
{
    ContestQso q;
    q.call = call;
    q.dxcc = dxcc;
    q.continent = cont;
    q.band = band;
    q.mode = mode;
    q.exchange = exchange;
    q.cqZone = cqZone;
    q.ituZone = ituZone;
    return q;
}

} // namespace

class TestContestRules : public QObject {
    Q_OBJECT

private slots:
    void arrl10MultiplierFoundationUsesOfficialCategories()
    {
        using Origin=Arrl10Origin;
        QCOMPARE(arrl10Points("CW"),4); QCOMPARE(arrl10Points("SSB"),2);
        QCOMPARE(arrl10Points("FM"),2); QCOMPARE(arrl10Points("FT8"),0);
        const auto ct=arrl10Multiplier(Origin::UnitedStates,"CT",0,"SSB");
        QVERIFY(!ct.isEmpty()); QCOMPARE(arrl10Multiplier(Origin::UnitedStates,"ct",0,"USB"),ct);
        QVERIFY(arrl10Multiplier(Origin::UnitedStates,"CT",0,"CW")!=ct);
        QVERIFY(!arrl10Multiplier(Origin::UnitedStates,"AK",0,"CW").isEmpty());
        QVERIFY(!arrl10Multiplier(Origin::UnitedStates,"HI",0,"CW").isEmpty());
        QVERIFY(!arrl10Multiplier(Origin::UnitedStates,"DC",0,"CW").isEmpty());
        QVERIFY(arrl10Multiplier(Origin::UnitedStates,"PR",0,"CW").isEmpty());
        QVERIFY(!arrl10Multiplier(Origin::Canada,"LB",0,"CW").isEmpty());
        QVERIFY(!arrl10Multiplier(Origin::Canada,"NT",0,"CW").isEmpty());
        QVERIFY(arrl10Multiplier(Origin::Canada,"NWT",0,"CW").isEmpty());
        QVERIFY(!arrl10Multiplier(Origin::Mexico,"CMX",0,"CW").isEmpty());
        QVERIFY(arrl10Multiplier(Origin::Mexico,"CT",0,"CW").isEmpty());
        QVERIFY(!arrl10Multiplier(Origin::Dx,"",248,"CW").isEmpty());
        QVERIFY(arrl10Multiplier(Origin::Dx,"",0,"CW").isEmpty());
        QVERIFY(!arrl10Multiplier(Origin::Maritime,"2",0,"CW").isEmpty());
        QVERIFY(arrl10Multiplier(Origin::Maritime,"4",0,"CW").isEmpty());
        QVERIFY(arrl10Multiplier(Origin::UnitedStates,"CT",0,"FT8").isEmpty());
        QCOMPARE(1305*arrl10Points("SSB")+930*arrl10Points("CW"),6330);
        QCOMPARE((1305*arrl10Points("SSB")+930*arrl10Points("CW"))*140,886200);
        QVERIFY(!forId("ARRL-10").valid); // the full rule and desktop workflow are not implemented yet
    }
    void aContestWithoutRulesSaysSo()
    {
        // Duecentocinquanta contest hanno un identificativo, dieci hanno le
        // regole: gli altri non devono dare un punteggio inventato.
        const ContestRules unknown = forId(QStringLiteral("AL-QSO-PARTY"));
        QVERIFY(!unknown.valid);
        QCOMPARE(points(unknown, qso("W1AW", 291, "NA", "20m"), me()), 0);
        QVERIFY(multipliers(unknown, qso("W1AW", 291, "NA", "20m"), me()).isEmpty());
        QVERIFY(checkExchange(unknown, QStringLiteral("qualunque cosa")).isEmpty());

        QVERIFY(forId(QStringLiteral("CQ-WW-SSB")).valid);
        QCOMPARE(known().size(), 10);
    }

    void unknownCqVariantsDoNotInheritScoring()
    {
        for (const QString& id : {QStringLiteral("CQ-WW-VHF"),QStringLiteral("CQ-WW-RTTY-OTHER"),
                                  QStringLiteral("CQ-WPX-DIGITAL"),QStringLiteral("CQ-WPX-CW-OTHER"),QStringLiteral("CQ-WW-")}) {
            const auto rules=forId(id);
            QVERIFY2(!rules.valid,qPrintable(id));
            QCOMPARE(points(rules,qso("W1AW",291,"NA","20m"),me()),0);
            QVERIFY(multipliers(rules,qso("W1AW",291,"NA","20m"),me()).isEmpty());
            auto forged=rules; forged.valid=true;
            QCOMPARE(points(forged,qso("W1AW",291,"NA","20m"),me()),0);
            QVERIFY(multipliers(forged,qso("W1AW",291,"NA","20m"),me()).isEmpty());
        }
        for (const auto& id : known()) QVERIFY2(forId(id).valid,qPrintable(id));
        QVERIFY(forId(" cq-ww-cw ").valid); QVERIFY(forId("cq-wpx-rtty").valid);
    }

    void cqWorldWideCountsTheZoneAndTheCountry()
    {
        const ContestRules r = forId(QStringLiteral("CQ-WW-CW"));
        QCOMPARE(r.exchange, ContestRules::Exchange::CqZone);

        // Regolamento VII: stesso paese 0 punti, ma conta per il moltiplicatore.
        QCOMPARE(points(r, qso("IK0ABC", 248, "EU", "20m", "CW", "15", 15), me()), 0);
        // Stesso continente, paese diverso: 1 punto.
        QCOMPARE(points(r, qso("DL9ZZT", 230, "EU", "20m", "CW", "14", 14), me()), 1);
        // Continente diverso: 3 punti.
        QCOMPARE(points(r, qso("W1AW", 291, "NA", "20m", "CW", "5", 5), me()), 3);

        // I moltiplicatori sono due, e contano per banda.
        const QStringList mults = multipliers(r, qso("W1AW", 291, "NA", "20m", "CW", "5", 5), me());
        QCOMPARE(mults.size(), 2);
        QVERIFY(mults.at(0).contains(QStringLiteral("zona 5")));
        QVERIFY(mults.at(0).endsWith(QStringLiteral("|20m")));
        QVERIFY(mults.at(1).contains(QStringLiteral("paese 291")));
        // La stessa zona su un'altra banda e' un altro moltiplicatore.
        const QStringList other = multipliers(r, qso("W2XX", 291, "NA", "40m", "CW", "5", 5), me());
        QVERIFY(other.at(0) != mults.at(0));
        // Conta la zona mandata, non quella che il cty.csv da' al prefisso.
        QVERIFY(multipliers(r, qso("W6ABC", 291, "NA", "20m", "CW", "3", 5), me())
                    .contains(QStringLiteral("zona 3|20m")));
        // Senza scambio (uno spot) si usa quella del cty.csv.
        QVERIFY(multipliers(r, qso("W6ABC", 291, "NA", "20m", "CW", "", 5), me())
                    .contains(QStringLiteral("zona 5|20m")));
        // E il proprio paese fa moltiplicatore anche se il QSO vale zero punti.
        QCOMPARE(multipliers(r, qso("IK0ABC", 248, "EU", "20m", "CW", "15", 15), me()).size(), 2);
    }

    void inNorthAmericaTheSameContinentIsWorthTwo()
    {
        // Regolamento VII: fra due stazioni del Nord America un QSO dentro il
        // continente vale 2 punti invece di 1.
        const ContestRules r = forId(QStringLiteral("CQ-WW-SSB"));
        const ContestStation american{291, QStringLiteral("NA"), 5, 8};
        QCOMPARE(points(r, qso("VE3ABC", 1, "NA", "20m", "SSB", "5", 5), american), 2);
        // Ma per un europeo il QSO dentro l'Europa resta 1 punto.
        QCOMPARE(points(r, qso("DL9ZZT", 230, "EU", "20m", "SSB", "14", 14), me()), 1);
    }

    void wpxPaysDoubleOnTheLowBands()
    {
        const ContestRules r = forId(QStringLiteral("CQ-WPX-CW"));
        QCOMPARE(r.exchange, ContestRules::Exchange::Serial);

        // Regolamento VI: continente diverso 3 punti sulle bande alte, 6 sulle
        // basse; stesso continente 1 e 2; il proprio paese 1 su tutte.
        QCOMPARE(points(r, qso("W1AW", 291, "NA", "20m", "CW", "001"), me()), 3);
        QCOMPARE(points(r, qso("W1AW", 291, "NA", "40m", "CW", "001"), me()), 6);
        QCOMPARE(points(r, qso("DL9ZZT", 230, "EU", "20m", "CW", "001"), me()), 1);
        QCOMPARE(points(r, qso("DL9ZZT", 230, "EU", "80m", "CW", "001"), me()), 2);
        QCOMPARE(points(r, qso("IK0ABC", 248, "EU", "20m", "CW", "001"), me()), 1);
        QCOMPARE(points(r, qso("IK0ABC", 248, "EU", "160m", "CW", "001"), me()), 1);

        // Il moltiplicatore e' il prefisso, e si conta una volta sola: la stessa
        // stazione su un'altra banda non ne porta un altro.
        const QStringList first = multipliers(r, qso("DL9ZZT", 230, "EU", "20m"), me());
        const QStringList again = multipliers(r, qso("DL9ZZT", 230, "EU", "40m"), me());
        QCOMPARE(first.size(), 1);
        QCOMPARE(first, again);
        QVERIFY(first.first().contains(QStringLiteral("DL9")));
    }

    void cqWorldWideRttyHasItsOwnRules()
    {
        // Il CQ WW RTTY non e' il CQ WW in un altro modo: punti, moltiplicatori,
        // bande, scadenza e portale sono suoi (cqwwrtty.com/rules).
        const ContestRules r = forId(QStringLiteral("CQ-WW-RTTY"));
        QCOMPARE(r.exchange, ContestRules::Exchange::CqZoneQth);
        QCOMPARE(r.submitUrl, QStringLiteral("https://www.cqwwrtty.com/logcheck/"));
        QCOMPARE(r.submitHours, 48);
        QVERIFY(!r.bands.contains(QStringLiteral("160m")));

        // Stesso paese 1 punto, stesso continente 2, continente diverso 3.
        QCOMPARE(points(r, qso("IK0ABC", 248, "EU", "20m", "RTTY", "15", 15), me()), 1);
        QCOMPARE(points(r, qso("DL9ZZT", 230, "EU", "20m", "RTTY", "14", 14), me()), 2);
        QCOMPARE(points(r, qso("W1AW", 291, "NA", "20m", "RTTY", "05 CT", 5), me()), 3);
        // Niente eccezione per il Nord America: fra due americani 2, non 4.
        const ContestStation american{291, QStringLiteral("NA"), 5, 8};
        QCOMPARE(points(r, qso("VE3ABC", 1, "NA", "20m", "RTTY", "04 ON", 4), american), 2);
        // I 160 metri non sono una banda della gara.
        QCOMPARE(points(r, qso("W1AW", 291, "NA", "160m", "RTTY", "05 CT", 5), me()), 0);
        QVERIFY(multipliers(r, qso("W1AW", 291, "NA", "160m", "RTTY", "05 CT", 5), me()).isEmpty());

        // Tre moltiplicatori: zona, paese e il QTH delle stazioni W/VE.
        const QStringList usa = multipliers(r, qso("W1AW", 291, "NA", "20m", "RTTY", "05 CT"), me());
        QCOMPARE(usa.size(), 3);
        QVERIFY(usa.at(0).startsWith(QStringLiteral("zona 5|")));
        QVERIFY(usa.contains(QStringLiteral("qth CT|20m")));
        // Le aree canadesi, anche scritte come nel log (NL per NF, NT per NWT).
        QVERIFY(multipliers(r, qso("VO1AA", 1, "NA", "20m", "RTTY", "05 NL"), me())
                    .contains(QStringLiteral("qth NF|20m")));
        QVERIFY(multipliers(r, qso("VE8AA", 1, "NA", "20m", "RTTY", "01 NT"), me())
                    .contains(QStringLiteral("qth NWT|20m")));
        // Senza QTH nello scambio vale lo STATE del log.
        ContestQso fromState = qso("K6XX", 291, "NA", "40m", "RTTY", "03", 3);
        fromState.state = QStringLiteral("CA");
        QVERIFY(multipliers(r, fromState, me()).contains(QStringLiteral("qth CA|40m")));
        // Alaska e Hawaii contano solo come paese; gli altri paesi non hanno QTH.
        QCOMPARE(multipliers(r, qso("KL7AA", 6, "NA", "20m", "RTTY", "01 AK"), me()).size(), 2);
        QCOMPARE(multipliers(r, qso("DL9ZZT", 230, "EU", "20m", "RTTY", "14"), me()).size(), 2);
        // Una sigla canadese da una stazione USA non e' un QTH.
        QCOMPARE(multipliers(r, qso("W1AW", 291, "NA", "20m", "RTTY", "05 ON"), me()).size(), 2);
        QCOMPARE(wveQths().size(), 49 + 14);

        // Lo scambio: la zona, e per W/VE anche il QTH.
        QVERIFY(checkExchange(r, QStringLiteral("15")).isEmpty());
        QVERIFY(checkExchange(r, QStringLiteral("05 MA")).isEmpty());
        QVERIFY(checkExchange(r, QStringLiteral("04 PEI")).isEmpty());
        QVERIFY(!checkExchange(r, QStringLiteral("05 XX")).isEmpty());
        QVERIFY(!checkExchange(r, QStringLiteral("55 MA")).isEmpty());
        QCOMPARE(exchangeZone(QStringLiteral("05 MA")), 5);
        QCOMPARE(exchangeZone(QStringLiteral("MA")), 0);
    }

    void cqWpxRttyDoublesEvenInTheOwnCountry()
    {
        // cqwpxrtty.com/rules: 1, 2 e 3 punti su 20, 15 e 10 metri, il doppio
        // su 40 e 80. Niente 160, niente eccezione per il Nord America.
        const ContestRules r = forId(QStringLiteral("CQ-WPX-RTTY"));
        QCOMPARE(r.submitUrl, QStringLiteral("https://www.cqwpxrtty.com/logcheck/"));
        QCOMPARE(r.submitHours, 48);
        QCOMPARE(points(r, qso("IK0ABC", 248, "EU", "20m", "RTTY", "001"), me()), 1);
        QCOMPARE(points(r, qso("IK0ABC", 248, "EU", "40m", "RTTY", "001"), me()), 2);
        QCOMPARE(points(r, qso("DL9ZZT", 230, "EU", "15m", "RTTY", "001"), me()), 2);
        QCOMPARE(points(r, qso("DL9ZZT", 230, "EU", "80m", "RTTY", "001"), me()), 4);
        QCOMPARE(points(r, qso("W1AW", 291, "NA", "10m", "RTTY", "001"), me()), 3);
        QCOMPARE(points(r, qso("W1AW", 291, "NA", "40m", "RTTY", "001"), me()), 6);
        const ContestStation american{291, QStringLiteral("NA"), 5, 8};
        QCOMPARE(points(r, qso("VE3ABC", 1, "NA", "20m", "RTTY", "001"), american), 2);
        QCOMPARE(points(r, qso("W1AW", 291, "NA", "160m", "RTTY", "001"), me()), 0);
    }

    void allCqContestsWantTheLogWithin48Hours()
    {
        for (const char* id : {"CQ-WW-CW", "CQ-WW-SSB", "CQ-WW-RTTY", "CQ-WPX-CW", "CQ-WPX-SSB", "CQ-WPX-RTTY"})
            QCOMPARE(forId(QLatin1String(id)).submitHours, 48);
        QCOMPARE(forId(QStringLiteral("CQ-WW-CW")).submitUrl, QStringLiteral("https://www.cqww.com/logcheck/"));
        // I WARC non sono bande da contest.
        QCOMPARE(points(forId(QStringLiteral("CQ-WW-CW")), qso("W1AW", 291, "NA", "30m", "CW", "5", 5), me()), 0);
        QCOMPARE(points(forId(QStringLiteral("CQ-WW-CW")), qso("W1AW", 291, "NA", "160m", "CW", "5", 5), me()), 3);
    }

    void iaruCountsZonesAndHeadquarters()
    {
        const ContestRules r = forId(QStringLiteral("IARU-HF"));
        QCOMPARE(r.exchange, ContestRules::Exchange::ItuZone);

        // Regolamento 5.1: la propria zona 1 punto, una HQ 1 punto, stessa zona
        // altro continente 1, stesso continente altra zona 3, tutto diverso 5.
        QCOMPARE(points(r, qso("IK0ABC", 248, "EU", "20m", "CW", "28", 0, 28), me()), 1);
        QCOMPARE(points(r, qso("DA0HQ", 230, "EU", "20m", "CW", "DARC"), me()), 1);
        QCOMPARE(points(r, qso("DL9ZZT", 230, "EU", "20m", "CW", "28", 0, 28), me()), 1);
        QCOMPARE(points(r, qso("SM3ABC", 284, "EU", "20m", "CW", "18", 0, 18), me()), 3);
        QCOMPARE(points(r, qso("W1AW", 291, "NA", "20m", "CW", "8", 0, 8), me()), 5);

        // 5.2: le zone e le HQ sono moltiplicatori per banda, e una HQ non porta
        // anche la zona.
        const QStringList hq = multipliers(r, qso("DA0HQ", 230, "EU", "20m", "CW", "DARC"), me());
        QCOMPARE(hq.size(), 1);
        QVERIFY(hq.first().startsWith(QStringLiteral("hq DARC")));
        const QStringList zone = multipliers(r, qso("W1AW", 291, "NA", "20m", "CW", "8", 0, 8), me());
        QCOMPARE(zone.size(), 1);
        QVERIFY(zone.first().startsWith(QStringLiteral("zona 8")));
    }

    void ariDxPaysTenForAnItalianStation()
    {
        const ContestRules r = forId(QStringLiteral("ARI-DX"));
        // Una stazione italiana vale 10 punti, anche per un italiano.
        QCOMPARE(points(r, qso("IK0ABC", 248, "EU", "20m", "SSB", "RM"), me()), 10);
        // Anche la Sardegna, che e' un'altra entita' DXCC ma lo stesso paese.
        QCOMPARE(points(r, qso("IS0ABC", 225, "EU", "20m", "SSB", "CA"), me()), 10);
        QCOMPARE(points(r, qso("DL9ZZT", 230, "EU", "20m", "SSB", "001"), me()), 1);
        QCOMPARE(points(r, qso("W1AW", 291, "NA", "20m", "SSB", "001"), me()), 3);

        // I moltiplicatori: le province delle italiane, i paesi delle altre.
        const QStringList province = multipliers(r, qso("IK0ABC", 248, "EU", "20m", "SSB", "RM"), me());
        QCOMPARE(province.size(), 1);
        QVERIFY(province.first().startsWith(QStringLiteral("prov RM")));
        const QStringList country = multipliers(r, qso("W1AW", 291, "NA", "20m", "SSB", "001"), me());
        QCOMPARE(country.size(), 1);
        QVERIFY(country.first().startsWith(QStringLiteral("paese 291")));
        // L'Italia non e' un moltiplicatore-paese: se la provincia non si capisce
        // non si conta niente.
        QVERIFY(multipliers(r, qso("IK0ABC", 248, "EU", "20m", "SSB", "boh"), me()).isEmpty());
    }

    void theAriSectionsContestPaysByBand()
    {
        const ContestRules r = forId(QStringLiteral("ARI-SEZIONI"));
        QCOMPARE(r.exchange, ContestRules::Exchange::AriSection);

        // Punto 7: 1 punto sui 40, 2 su 80 e 20, 3 su 160 e 15, 4 sui 10.
        QCOMPARE(points(r, qso("IK2ABC", 248, "EU", "40m", "SSB", "L01"), me()), 1);
        QCOMPARE(points(r, qso("IK2ABC", 248, "EU", "80m", "SSB", "L01"), me()), 2);
        QCOMPARE(points(r, qso("IK2ABC", 248, "EU", "20m", "SSB", "L01"), me()), 2);
        QCOMPARE(points(r, qso("IK2ABC", 248, "EU", "160m", "SSB", "L01"), me()), 3);
        QCOMPARE(points(r, qso("IK2ABC", 248, "EU", "15m", "SSB", "L01"), me()), 3);
        QCOMPARE(points(r, qso("IK2ABC", 248, "EU", "10m", "SSB", "L01"), me()), 4);
        // Una banda che il contest non usa non porta punti.
        QCOMPARE(points(r, qso("IK2ABC", 248, "EU", "30m", "SSB", "L01"), me()), 0);

        // Punto 8: il codice ASC conta fino a tre volte per banda, una per modo.
        const QStringList ssb = multipliers(r, qso("IK2ABC", 248, "EU", "20m", "SSB", "L01"), me());
        const QStringList cw = multipliers(r, qso("IK2DEF", 248, "EU", "20m", "CW", "L01"), me());
        const QStringList rtty = multipliers(r, qso("IK2GHI", 248, "EU", "20m", "RTTY", "L01"), me());
        QCOMPARE(ssb.size(), 1);
        QVERIFY(ssb != cw && cw != rtty && ssb != rtty);
        // Lo stesso codice nello stesso modo e sulla stessa banda e' lo stesso.
        QCOMPARE(multipliers(r, qso("IK2XYZ", 248, "EU", "20m", "SSB", "L 01"), me()), ssb);
    }

    void the4080PaysByMode()
    {
        const ContestRules r = forId(QStringLiteral("ARI-40-80"));
        // CW 3 punti, RTTY 2, SSB 1.
        QCOMPARE(points(r, qso("IK2ABC", 248, "EU", "40m", "CW", "MI"), me()), 3);
        QCOMPARE(points(r, qso("IK2ABC", 248, "EU", "40m", "RTTY", "MI"), me()), 2);
        QCOMPARE(points(r, qso("IK2ABC", 248, "EU", "80m", "SSB", "MI"), me()), 1);
        // I moltiplicatori sono le province, per banda e per modo.
        const QStringList a = multipliers(r, qso("IK2ABC", 248, "EU", "40m", "CW", "MI"), me());
        const QStringList b = multipliers(r, qso("IK2DEF", 248, "EU", "40m", "SSB", "MI"), me());
        QCOMPARE(a.size(), 1);
        QVERIFY(a != b);
    }

    void theExchangeIsCheckedBeforeItGoesInTheLog()
    {
        // Una zona CQ sta fra 1 e 40: "55" e' un errore di battitura, e dirlo
        // adesso costa meno che ritrovarlo nel log a gara finita.
        const ContestRules ww = forId(QStringLiteral("CQ-WW-SSB"));
        QVERIFY(checkExchange(ww, QStringLiteral("15")).isEmpty());
        QVERIFY(!checkExchange(ww, QStringLiteral("55")).isEmpty());
        QVERIFY(!checkExchange(ww, QStringLiteral("EU")).isEmpty());
        QVERIFY(!checkExchange(ww, QString()).isEmpty());

        const ContestRules wpx = forId(QStringLiteral("CQ-WPX-CW"));
        QVERIFY(checkExchange(wpx, QStringLiteral("001")).isEmpty());
        QVERIFY(!checkExchange(wpx, QStringLiteral("RM")).isEmpty());

        // Nello IARU una sigla di societa' va bene quanto una zona.
        const ContestRules iaru = forId(QStringLiteral("IARU-HF"));
        QVERIFY(checkExchange(iaru, QStringLiteral("28")).isEmpty());
        QVERIFY(checkExchange(iaru, QStringLiteral("ARRL")).isEmpty());
        QVERIFY(!checkExchange(iaru, QStringLiteral("99")).isEmpty());

        // Nell'ARI DX la provincia o il progressivo.
        const ContestRules ari = forId(QStringLiteral("ARI-DX"));
        QVERIFY(checkExchange(ari, QStringLiteral("NA")).isEmpty());
        QVERIFY(checkExchange(ari, QStringLiteral("027")).isEmpty());
        QVERIFY(!checkExchange(ari, QStringLiteral("ZZ")).isEmpty());

        // Il codice ASC e' una lettera e due cifre.
        const ContestRules sez = forId(QStringLiteral("ARI-SEZIONI"));
        QVERIFY(checkExchange(sez, QStringLiteral("L01")).isEmpty());
        QVERIFY(checkExchange(sez, QStringLiteral("r 01")).isEmpty());
        QVERIFY(!checkExchange(sez, QStringLiteral("L1")).isEmpty());
        QVERIFY(!checkExchange(sez, QStringLiteral("123")).isEmpty());
    }

    void theEditionChangesTheRules()
    {
        // Il WPX: 36 ore su 48 da singolo, pause di almeno un'ora.
        const ContestRules wpx = forId(QStringLiteral("CQ-WPX-CW"));
        QCOMPARE(wpx.maxOperatingHours, 36);
        QCOMPARE(wpx.minOffMinutes, 60);
        QCOMPARE(forId(QStringLiteral("CQ-WPX-RTTY")).maxOperatingHours, 30);
        QCOMPARE(forId(QStringLiteral("CQ-WW-CW")).maxOperatingHours, 0);
        // Quest'anno: senza i 160 metri e il log entro tre giorni.
        const ContestRules changed = withOverrides(forId(QStringLiteral("CQ-WW-CW")),
                                                   {{"bands", QStringList{"80M", " 40m", "20m", "15m", "10m"}},
                                                    {"submitHours", 72}, {"maxOperatingHours", 30}});
        QCOMPARE(changed.bands, QStringList({"80m", "40m", "20m", "15m", "10m"}));
        QCOMPARE(changed.submitHours, 72);
        QCOMPARE(changed.maxOperatingHours, 30);
        QCOMPARE(changed.exchange, ContestRules::Exchange::CqZone);   // il resto com'era
        // Una gara senza scheda resta senza scheda.
        QVERIFY(!withOverrides(forId(QStringLiteral("NOPE")), {{"submitHours", 5}}).valid);
    }

    void anEditionCannotEnableUnsupportedBands()
    {
        const ContestRules base = forId(QStringLiteral("CQ-WW-CW"));
        const ContestRules invalid = withOverrides(base, {
            {"bands", QStringList{"20m", "30m"}}
        });
        QVERIFY(!invalid.valid);
        QCOMPARE(points(invalid, qso("W1AW", 291, "NA", "20m", "CW", "5", 5), me()), 0);

        const ContestRules valid = withOverrides(base, {
            {"bands", QStringList{"20M", "40m"}}
        });
        QVERIFY(valid.valid);
        QCOMPARE(valid.bands, QStringList({"20m", "40m"}));
    }

    void theTimeOnTheAir()
    {
        const ContestRules wpx = forId(QStringLiteral("CQ-WPX-SSB"));
        const QDateTime t0(QDate(2026, 3, 28), QTime(0, 0), QTimeZone::UTC);
        // Tre ore di fila (un QSO ogni 10 minuti), una pausa di 50 minuti (non
        // conta: sotto l'ora), un'ora, una pausa di due ore (conta), un'altra ora.
        QList<QDateTime> times;
        QDateTime at = t0;
        auto run = [&](int minutes) {
            for (int m = 0; m <= minutes; m += 10)
                times << at.addSecs(m * 60);
            at = times.last();
        };
        run(180);
        at = at.addSecs(50 * 60);
        run(60);
        at = at.addSecs(120 * 60);
        run(60);
        const OperatingTime t = operatingTime(wpx, times);
        QCOMPARE(t.breaks, 1);
        QCOMPARE(t.offMinutes, 120);
        QCOMPARE(t.onMinutes, 180 + 50 + 60 + 60);
        QCOMPARE(t.maxMinutes, 36 * 60);
        QVERIFY(!t.over());
        // Con un limite di quattro ore si e' fuori.
        ContestRules tight = wpx;
        tight.maxOperatingHours = 4;
        QVERIFY(operatingTime(tight, times).over());
        QCOMPARE(operatingTime(wpx, {t0}).onMinutes, 0);
    }

    void callHistoryFiles()
    {
        CallHistory h;
        QVERIFY(h.loadText(QStringLiteral(
            "# CQ WW call history, esempio\n"
            "!!Order!!,Call,Name,Loc1,CQZone,ITUZone,Exch1,State,\n"
            "W1AW,HIRAM,FN31,5,8,,CT,\n"
            "iu8lmc,MARCO,JN71,15,28,NA,,\n"
            "# a meta' file cambia l'ordine\n"
            "!!Order!!,Call,Exch1\n"
            "DA0HQ,DARC\n")));
        QCOMPARE(h.size(), 3);
        QCOMPARE(h.find(QStringLiteral("w1aw")).value(QStringLiteral("NAME")), QString("HIRAM"));
        QCOMPARE(h.value(QStringLiteral("IU8LMC"), {QStringLiteral("CQZONE")}), QString("15"));
        QCOMPARE(h.value(QStringLiteral("IU8LMC"), {QStringLiteral("STATE"), QStringLiteral("EXCH1")}), QString("NA"));
        QCOMPARE(h.value(QStringLiteral("DA0HQ"), {QStringLiteral("EXCH1")}), QString("DARC"));
        QVERIFY(h.find(QStringLiteral("K1ZZ")).isEmpty());
        // Senza !!Order!! non si capisce niente.
        CallHistory none;
        QVERIFY(!none.loadText(QStringLiteral("W1AW,HIRAM\n")));
    }

    void oneSerialForTheWholeNetwork()
    {
        // Il distributore: il PC con l'identita' piu' piccola fra i presenti.
        QVERIFY(SharedSerials::isServer(QStringLiteral("a1"), {QStringLiteral("b2"), QStringLiteral("c3")}));
        QVERIFY(!SharedSerials::isServer(QStringLiteral("b2"), {QStringLiteral("a1")}));
        QVERIFY(SharedSerials::isServer(QStringLiteral("b2"), {}));

        SharedSerials server;
        SharedSerials runner;
        // Il distributore da' 1 al secondo PC, poi usa 2 per se'.
        runner.reserve(server.serve());
        QCOMPARE(server.take(1, true, true), 2);
        // Il secondo PC usa quello che ha avuto, anche se il suo contatore dice 1.
        QCOMPARE(runner.take(1, false, true), 1);
        // Un QSO della rete con il 7: il prossimo del distributore e' 8.
        server.note(7);
        QCOMPARE(server.serve(), 8);
        // Senza numero in tasca e senza rete: il piu' alto visto piu' uno.
        runner.note(8);
        QVERIFY(!runner.hasReserved());
        QCOMPARE(runner.take(3, false, false), 9);
        QCOMPARE(runner.highest(), 9);
    }

    void serverRecordsActualIssuedNumberWhenLocalCounterIsAhead()
    {
        SharedSerials server;
        QCOMPARE(server.take(100, true, true), 100);
        QCOMPARE(server.highest(), 100);
        QCOMPARE(server.serve(), 101);
        QVERIFY(server.noteRemote(100));
    }

    void splitNetworkReportsSerialCollision()
    {
        SharedSerials first;
        SharedSerials second;
        QCOMPARE(first.take(1, false, false), 1);
        QCOMPARE(second.take(1, false, false), 1);
        QVERIFY(first.noteRemote(1));
        QVERIFY(first.collisionDetected());
        QVERIFY(!second.noteRemote(2));
        QVERIFY(!second.collisionDetected());
    }
};

QTEST_GUILESS_MAIN(TestContestRules)
#include "tst_contestrules.moc"
