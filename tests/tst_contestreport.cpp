#include "core/ContestReport.h"
#include "core/LogDatabase.h"
#include <QTest>
#include <QFile>
using namespace decolog::core;

class TestContestReport : public QObject {
    Q_OBJECT
private slots:
    void findsOnlyConservativeReadOnlyCandidates()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        auto make=[&](const QString& call,const QString& date,const QString& time,const QString& band,const QString& mode) {
            return db.insertQso(AdifRecord{{"CALL",call},{"QSO_DATE",date},{"TIME_ON",time},{"BAND",band},{"MODE",mode},
                {"STATION_CALLSIGN","K1ABC"}},"fixture",{},true,0,false).id;
        };
        const auto one=make("DL1ABC","20241123","120145","20m","CW"); QVERIFY(one>0);
        QVERIFY(make("DL1ABC","20241123","120159","40m","CW")>0);
        QVERIFY(make("DL1ABC","20241123","120200","20m","CW")>0);
        QVERIFY(make("DL1ABC","20241123","120101","20m","SSB")>0);
        const auto removed=make("DL1ABC","20241123","120130","20m","CW"); QVERIFY(removed>0); QVERIFY(db.softDeleteQso(removed));
        const auto parsed=contestreport::parseCqWw("Contest: 2024 CQ WORLD WIDE CW DX CONTEST\nCall: K1ABC\n****** Not In Log ******\n14020 CW 2024-11-23 1201 K1ABC 05 DL1ABC 14\n");
        QCOMPARE(parsed.entries.size(),1); auto result=contestreport::findCandidates(db,parsed.entries.first());
        QVERIFY2(result.error.isEmpty(),qPrintable(result.error)); QCOMPARE(result.candidates.size(),1);
        QCOMPARE(result.candidates.first().id,one); QCOMPARE(result.candidates.first().stationCall,QString("K1ABC")); QCOMPARE(result.candidates.first().stationSource,QString("qso")); QVERIFY(!result.truncated);
        QVERIFY(make("DL1ABC","20241123","120140","20m","CW")>0);
        result=contestreport::findCandidates(db,parsed.entries.first()); QCOMPARE(result.candidates.size(),2);
        auto unknown=parsed.entries.first(); unknown.mode="unsupported"; QVERIFY(!contestreport::findCandidates(db,unknown).error.isEmpty());
        QCOMPARE(db.qsoCount(),qint64(5));
    }
    void candidateSearchBoundsAmbiguityAndHandlesUtcMidnight()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        for (int i=0;i<7;++i) {
            AdifRecord r{{"CALL","W1ABC"},{"QSO_DATE","20241231"},{"TIME_ON",QString("2359%1").arg(i,2,10,QLatin1Char('0'))},
                {"BAND","20m"},{"MODE","CW"},{"STATION_CALLSIGN",i==0 ? "OTHER" : "K1ABC"}};
            QVERIFY(db.insertQso(r,"fixture",{},true,0,false).id>0);
        }
        QVERIFY(db.insertQso(AdifRecord{{"CALL","W1ABC"},{"QSO_DATE","20250101"},{"TIME_ON","000000"},{"BAND","20m"},{"MODE","CW"}},"fixture",{},true,0,false).id>0);
        contestreport::Entry entry; entry.call="W1ABC"; entry.date="2024-12-31"; entry.time="2359"; entry.frequencyKhz="14020"; entry.mode="CW";
        const auto candidates=contestreport::findCandidates(db,entry); QVERIFY2(candidates.error.isEmpty(),qPrintable(candidates.error));
        QCOMPARE(candidates.candidates.size(),5); QVERIFY(candidates.truncated);
        QCOMPARE(candidates.candidates.first().stationCall,QString("OTHER")); QCOMPARE(db.qsoCount(),qint64(8));
    }
    void candidateStationDistinguishesCurrentProfileFromQso()
    {
        LogDatabase db; QVERIFY(db.open(":memory:")); StationProfile profile; profile.name="station"; profile.stationCallsign="K1NEW";
        const auto profileId=db.saveStationProfile(profile); QVERIFY(profileId>0);
        const auto id=db.insertQso(AdifRecord{{"CALL","DL1ABC"},{"QSO_DATE","20241123"},{"TIME_ON","120100"},
                    {"BAND","20m"},{"MODE","SSB"}},"fixture",{},true,profileId,false).id; QVERIFY(id>0);
        contestreport::Entry row; row.call="DL1ABC"; row.date="2024-11-23"; row.time="1201"; row.frequencyKhz="14020"; row.mode="PH";
        const auto matches=contestreport::findCandidates(db,row); QVERIFY(matches.error.isEmpty()); QCOMPARE(matches.candidates.size(),1);
        QCOMPARE(matches.candidates.first().stationCall,QString("K1NEW")); QCOMPARE(matches.candidates.first().stationSource,QString("profile"));
    }
    void parsesKnownSectionsWithoutLosingUnknownLines()
    {
        const QByteArray text = "Contest: 2024 CQ WORLD WIDE CW DX CONTEST\nCall: K1ABC\n"
            "******** Not In Log ********\n 14020 CW 2024-11-23 1201 K1ABC 05 DL1ABC 14\n"
            "this line is not understood\n******** Incorrect Call ********\n"
            "  7020 CW 2024-11-23 1202 K1ABC 05 DL2BAD 14 correct DL2GOOD\n"
            "******** Incorrect Exchange Information ********\n"
            " 21020 CW 2024-11-23 1203 K1ABC 05 JA1ABC 25 correct  24\n"
            "******** Stations Copying K1ABC Incorrectly ********\n"
            " 14020 CW 2024-11-23 1204 DL3ABC 14 K1ABD 05\n";
        const auto result=contestreport::parseCqWw(text);
        QVERIFY2(result.error.isEmpty(),qPrintable(result.error));
        QCOMPARE(result.entries.size(),3);
        QCOMPARE(result.entries[0].kind,QString("not_in_log"));
        QCOMPARE(result.entries[1].call,QString("DL2BAD"));
        QCOMPARE(result.entries[1].correction,QString("DL2GOOD"));
        QCOMPARE(result.entries[2].kind,QString("incorrect_exchange"));
        QCOMPARE(result.entries[2].correction,QString("24"));
        QCOMPARE(result.lines.join("\n").toUtf8(),text);
        QVERIFY(result.unrecognizedLines.contains(4));
        QVERIFY(!result.unrecognizedLines.contains(10));
    }
    void publicCqWwReportWhenProvided()
    {
        const QString path=qEnvironmentVariable("CQWW_LCR_SAMPLE");
        if (path.isEmpty()) QSKIP("Optional public CQ WW sample not provided");
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto report=contestreport::parseCqWw(file.readAll());
        QVERIFY2(report.error.isEmpty(),qPrintable(report.error)); QCOMPARE(report.ownCall,QString("4O4A"));
        int nil=0, calls=0, exchange=0;
        for (const auto& entry : report.entries) { if (entry.kind=="not_in_log") ++nil; else if (entry.kind=="incorrect_call") ++calls; else if (entry.kind=="incorrect_exchange") ++exchange; }
        QCOMPARE(nil,14); QCOMPARE(calls,20); QCOMPARE(exchange,6); QCOMPARE(report.unrecognizedLines.size(),0);
    }
    void refusesUnrelatedAndOversizedInput()
    {
        QVERIFY(!contestreport::parseCqWw("not a CQ WW report\n").error.isEmpty());
        QVERIFY(!contestreport::parseCqWw("Contest: 2024 CQ WORLD WIDE CW DX CONTEST\nCall: K1ABC\nUnknown format\n").error.isEmpty());
        QVERIFY(!contestreport::parseCqWw(QByteArray(2*1024*1024+1,'x')).error.isEmpty());
    }
};
QTEST_GUILESS_MAIN(TestContestReport)
#include "tst_contestreport.moc"
