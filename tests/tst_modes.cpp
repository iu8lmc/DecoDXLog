// I modi come li dice l'operatore e come li vuole la radio: FT8 per chi opera
// e' un modo, per Hamlib e' una banda laterale con i dati dentro.
#include "core/Modes.h"
#include "core/Adif.h"

#include <QTest>

using namespace decolog::core;

class TestModes : public QObject {
    Q_OBJECT

private slots:
    void clusterFtModesUseUsbWithoutChangingRtty()
    {
        for (const auto* mode : {"FT8", "FT4", "FT2", " ft8 "})
            QCOMPARE(modes::catForClusterSpot(QLatin1String(mode), 7.074), QString("USB"));
        QCOMPARE(modes::catForClusterSpot("RTTY", 14.09), QString("RTTY"));
        QCOMPARE(modes::catForClusterSpot("RTTY-R", 14.09), QString("RTTYR"));
        QCOMPARE(modes::catForClusterSpot("CW", 7.03), QString("CW"));
        QCOMPARE(modes::catForClusterSpot("SSB", 7.12), QString("LSB"));
        QCOMPARE(modes::catFor("FT8", 7.074), QString("PKTUSB"));
    }

    void cwAndPhoneGoStraightThrough()
    {
        QCOMPARE(modes::catFor(QStringLiteral("CW")), QStringLiteral("CW"));
        QCOMPARE(modes::catFor(QStringLiteral("CW-R")), QStringLiteral("CWR"));
        QCOMPARE(modes::catFor(QStringLiteral("USB")), QStringLiteral("USB"));
        QCOMPARE(modes::catFor(QStringLiteral("LSB")), QStringLiteral("LSB"));
        QCOMPARE(modes::catFor(QStringLiteral("AM")), QStringLiteral("AM"));
        QCOMPARE(modes::catFor(QStringLiteral("FM")), QStringLiteral("FM"));
    }

    void everyDigitalModeIsDataOnUpperSideband()
    {
        for (const auto* name : {"FT8", "FT4", "FT2", "JTTY", "JS8", "JT65", "Q65", "MSK144", "PSK31", "OLIVIA"})
            QCOMPARE(modes::catFor(QLatin1String(name)), QStringLiteral("PKTUSB"));
    }

    void rttyKeepsItsOwnName()
    {
        QCOMPARE(modes::catFor(QStringLiteral("RTTY")), QStringLiteral("RTTY"));
        QCOMPARE(modes::catFor(QStringLiteral("RTTY-R")), QStringLiteral("RTTYR"));
    }

    void lowercaseAndSpacesDoNotMatter()
    {
        QCOMPARE(modes::catFor(QStringLiteral(" ft8 ")), QStringLiteral("PKTUSB"));
        QCOMPARE(modes::catFor(QStringLiteral("cw")), QStringLiteral("CW"));
    }

    void aSubmodeCountsAsItsMode()
    {
        // Decodium manda anche i sottomodi: FT8-DX resta un FT8.
        QCOMPARE(modes::catFor(QStringLiteral("FT8-DX")), QStringLiteral("PKTUSB"));
        QCOMPARE(modes::catFor(QStringLiteral("PSK31-A")), QStringLiteral("PKTUSB"));
    }

    void whatTheRadioAlreadySaysIsKept()
    {
        QCOMPARE(modes::catFor(QStringLiteral("PKTUSB")), QStringLiteral("PKTUSB"));
        QCOMPARE(modes::catFor(QStringLiteral("CWR")), QStringLiteral("CWR"));
    }

    void whatIsNotKnownDoesNoHarm()
    {
        // Senza un modo, o con uno che non si conosce, si resta su USB: e' la
        // scelta che non mette la radio in uno stato strano.
        QCOMPARE(modes::catFor(QString()), QStringLiteral("USB"));
        QCOMPARE(modes::catFor(QStringLiteral("SSB")), QStringLiteral("USB"));
        QCOMPARE(modes::catFor(QStringLiteral("PHONE")), QStringLiteral("USB"));
        QCOMPARE(modes::catFor(QStringLiteral("DATA")), QStringLiteral("PKTUSB"));
        QCOMPARE(modes::catFor(QStringLiteral("QUALCOSA")), QStringLiteral("USB"));
    }

    void theMenuHasCwPhoneAndDigital()
    {
        const auto all = modes::all();
        QVERIFY(all.size() > 20);
        QCOMPARE(all.first().name, QStringLiteral("CW"));   // primo: chi va in CW lo trova subito
        int cw = 0, voice = 0, data = 0;
        for (const auto& e : all) {
            if (e.group == QLatin1String("cw")) ++cw;
            else if (e.group == QLatin1String("voice")) ++voice;
            else if (e.group == QLatin1String("data")) ++data;
        }
        QVERIFY(cw >= 2);
        QVERIFY(voice >= 4);
        QVERIFY(data >= 10);
        // Ogni voce ha un nome e un modo per la radio: niente buchi nel menu.
        for (const auto& e : all) {
            QVERIFY(!e.name.isEmpty());
            QVERIFY(!e.cat.isEmpty());
            QCOMPARE(modes::catFor(e.name), e.cat);
        }
    }

    // "SSB" non dice quale banda laterale: sotto i 10 MHz si parla in LSB,
    // sopra in USB. Lo sanno tutti in aria, e adesso anche il programma —
    // prima uno spot in fonia sui 40 metri portava la radio in USB.
    void ssbPicksTheSideTheBandUses()
    {
        QCOMPARE(modes::catFor("SSB", 3.750), QString("LSB"));
        QCOMPARE(modes::catFor("SSB", 7.120), QString("LSB"));
        QCOMPARE(modes::catFor("SSB", 14.205), QString("USB"));
        QCOMPARE(modes::catFor("SSB", 28.400), QString("USB"));
        QCOMPARE(modes::catFor("PHONE", 1.845), QString("LSB"));
        // Chi ha scritto USB o LSB sapeva quello che voleva: non si tocca.
        QCOMPARE(modes::catFor("USB", 7.120), QString("USB"));
        QCOMPARE(modes::catFor("LSB", 14.205), QString("LSB"));
        // Senza frequenza vale il caso generale di sempre.
        QCOMPARE(modes::catFor("SSB", 0), modes::catFor("SSB"));
        // Gli altri modi non cambiano per la banda.
        QCOMPARE(modes::catFor("CW", 3.550), QString("CW"));
        QCOMPARE(modes::catFor("FT8", 7.074), QString("PKTUSB"));
    }

    // Il gruppo per la griglia banda x modo: CW, fonia, digitale. E' lo stesso
    // criterio dei diplomi, e quello che non si conosce finisce fra i digitali.
    void groupsLikeTheAwards()
    {
        QCOMPARE(modes::groupFor("CW"), QString("CW"));
        QCOMPARE(modes::groupFor("cw-r"), QString("CW"));
        QCOMPARE(modes::groupFor("SSB"), QString("PHONE"));
        QCOMPARE(modes::groupFor("usb"), QString("PHONE"));
        QCOMPARE(modes::groupFor("LSB"), QString("PHONE"));
        QCOMPARE(modes::groupFor("AM"), QString("PHONE"));
        QCOMPARE(modes::groupFor("FM"), QString("PHONE"));
        QCOMPARE(modes::groupFor("FT8"), QString("DATA"));
        QCOMPARE(modes::groupFor("RTTY"), QString("DATA"));
        QCOMPARE(modes::groupFor(" MFSK "), QString("DATA"));
        QCOMPARE(modes::groupFor("FT2"), QString("DATA"));
        QCOMPARE(modes::groupFor(""), QString("DATA"));
    }

    void adifSubmodesAreNormalizedWithoutGuessing()
    {
        QCOMPARE(modes::displayName("SSB"), QString("SSB"));
        QCOMPARE(modes::displayName("USB"), QString("USB"));
        QCOMPARE(modes::displayName("LSB"), QString("LSB"));
        QCOMPARE(modes::displayName("CW-R"), QString("CW-R"));
        QCOMPARE(modes::displayName("CW", "CWR"), QString("CW"));
        QCOMPARE(modes::displayName("MFSK", "FT4"), QString("FT4"));
        QCOMPARE(modes::displayName("MFSK", "FT8"), QString("FT8"));
        QCOMPARE(modes::displayName("MFSK", "FT2"), QString("FT2"));
        QCOMPARE(modes::displayName("MFSK", "UNKNOWN"), QString("MFSK"));
        QCOMPARE(modes::displayName("DMR", "C4FM"), QString("DMR"));
        QCOMPARE(modes::groupFor("DMR"), QString("PHONE"));
        QCOMPARE(modes::groupFor("C4FM"), QString("PHONE"));
        QCOMPARE(modes::groupFor("RTTY"), QString("DATA"));
    }

    void adifAndAwardGroupsStayAligned()
    {
        const QList<QPair<QString, QString>> cases{
            {QStringLiteral("CW"), {}}, {QStringLiteral("CW-R"), {}},
            {QStringLiteral("SSB"), QStringLiteral("USB")},
            {QStringLiteral("FM"), {}}, {QStringLiteral("DMR"), {}},
            {QStringLiteral("C4FM"), {}}, {QStringLiteral("FT8"), {}},
            {QStringLiteral("MFSK"), QStringLiteral("FT2")},
            {QStringLiteral("UNKNOWN-MODE"), {}}};
        for (const auto& pair : cases)
            QCOMPARE(modes::groupFor(pair.first), adif::modeGroup(pair.first, pair.second));
    }
};

QTEST_MAIN(TestModes)
#include "tst_modes.moc"
