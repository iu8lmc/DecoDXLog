#include "core/ContestReport.h"
#include "core/LogDatabase.h"
#include <QSqlQuery>
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
    void findsAlreadyCorrectedCallsWithoutConfusingExchangeCorrections()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        auto insert=[&](const QString &call) {
            return db.insertQso(AdifRecord{{"CALL",call},{"QSO_DATE","20241123"},{"TIME_ON","120145"},
                {"BAND","20m"},{"MODE","CW"}},"fixture",{},true,0,false).id;
        };
        const auto original=insert("DL1BAD"), corrected=insert("DL1GOOD"); QVERIFY(original>0); QVERIFY(corrected>0);
        contestreport::Entry row; row.kind="incorrect_call"; row.call="DL1BAD"; row.correction="DL1GOOD";
        row.date="2024-11-23"; row.time="1201"; row.frequencyKhz="14020"; row.mode="CW";
        auto found=contestreport::findCandidates(db,row); QVERIFY(found.error.isEmpty()); QCOMPARE(found.candidates.size(),2);
        QCOMPARE(found.candidates[0].id,original); QCOMPARE(found.candidates[0].matchBasis,QString("reported"));
        QCOMPARE(found.candidates[1].id,corrected); QCOMPARE(found.candidates[1].call,QString("DL1GOOD"));
        QCOMPARE(found.candidates[1].matchBasis,QString("corrected"));
        row.kind="incorrect_exchange"; found=contestreport::findCandidates(db,row); QCOMPARE(found.candidates.size(),1);
        row.kind="incorrect_call"; row.correction="DL1BAD"; found=contestreport::findCandidates(db,row); QCOMPARE(found.candidates.size(),1);
        QCOMPARE(db.qsoCount(),qint64(2));
    }
    void candidateExposesRecordedComparisonFieldsWithoutGuessing()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        const auto id=db.insertQso(AdifRecord{{"CALL","DL1ABC"},{"QSO_DATE","20241123"},{"TIME_ON","120145"},
            {"BAND","20m"},{"MODE","CW"},{"FREQ","14.021"},{"SRX_STRING","14"},{"CONTEST_ID","CQ-WW-CW"}},"fixture",{},true).id; QVERIFY(id>0);
        contestreport::Entry row; row.call="DL1ABC"; row.date="2024-11-23"; row.time="1201"; row.frequencyKhz="14020"; row.mode="CW";
        const auto found=contestreport::findCandidates(db,row); QVERIFY2(found.error.isEmpty(),qPrintable(found.error)); QCOMPARE(found.candidates.size(),1);
        const auto candidate=found.candidates.first(); QCOMPARE(candidate.revision,1); QCOMPARE(candidate.frequencyKhz,QString("14021"));
        QCOMPARE(candidate.receivedExchange,QString("14")); QCOMPARE(candidate.contestId,QString("CQ-WW-CW"));
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
    void configurableMinuteWindowCrossesMidnightAndIsBounded()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        auto add=[&](const QString &date,const QString &time) {
            return db.insertQso(AdifRecord{{"CALL","W1ABC"},{"QSO_DATE",date},{"TIME_ON",time},
                {"BAND","20m"},{"MODE","CW"}},"fixture",{},true,0,false).id;
        };
        const auto before=add("20241231","235959"), same=add("20250101","000030"), after=add("20250101","000100");
        QVERIFY(before>0 && same>0 && after>0);
        contestreport::Entry row; row.call="W1ABC"; row.date="2025-01-01"; row.time="0000"; row.frequencyKhz="14020"; row.mode="CW";
        auto exact=contestreport::findCandidates(db,row,0); QVERIFY(exact.error.isEmpty()); QCOMPARE(exact.candidates.size(),1); QCOMPARE(exact.candidates.first().id,same);
        auto widened=contestreport::findCandidates(db,row,1); QVERIFY(widened.error.isEmpty()); QCOMPARE(widened.candidates.size(),3);
        QCOMPARE(widened.candidates[0].id,same); QCOMPARE(widened.candidates[1].id,after); QCOMPARE(widened.candidates[2].id,before);
        QVERIFY(!contestreport::findCandidates(db,row,-1).error.isEmpty()); QVERIFY(!contestreport::findCandidates(db,row,6).error.isEmpty());
        QCOMPARE(db.qsoCount(),qint64(3));
    }

    void candidateSearchUsesUtcForOffsetAndLegacyTimes()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        for (int i=0;i<4;++i) QVERIFY(db.insertQso(AdifRecord{{"CALL","W1ABC"},{"QSO_DATE","20250101"},{"TIME_ON",QString("00000%1").arg(i)},{"BAND","20m"},{"MODE","CW"}},"fixture",{},true,0,false).id>0);
        QSqlQuery q(db.connection()); QVERIFY(q.exec("UPDATE qso SET qso_datetime_on=CASE id WHEN 1 THEN '2025-01-01T01:00:30+01:00' WHEN 2 THEN '2025-01-01T00:00:40' WHEN 3 THEN '2025-01-01T00:00:20+01:00' ELSE '2024-12-31T12:00:50-12:00' END"));
        contestreport::Entry row; row.call="W1ABC"; row.date="2025-01-01"; row.time="0000"; row.frequencyKhz="14020"; row.mode="CW";
        const auto found=contestreport::findCandidates(db,row,0); QVERIFY2(found.error.isEmpty(),qPrintable(found.error));
        QCOMPARE(found.candidates.size(),3); QCOMPARE(found.candidates[0].id,qint64(1)); QCOMPARE(found.candidates[1].id,qint64(2)); QCOMPARE(found.candidates[2].id,qint64(4));
    }
    void closestMinuteWinsWhenCandidateListIsTruncated()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        auto add=[&](const QString &time) {
            return db.insertQso(AdifRecord{{"CALL","W1ABC"},{"QSO_DATE","20241123"},{"TIME_ON",time},
                {"BAND","20m"},{"MODE","CW"}},"fixture",{},true,0,false).id;
        };
        for (int i=0;i<6;++i) QVERIFY(add(QString("1157%1").arg(i,2,10,QLatin1Char('0')))>0);
        const auto exact=add("120030"); QVERIFY(exact>0);
        contestreport::Entry row; row.call="W1ABC"; row.date="2024-11-23"; row.time="1200"; row.frequencyKhz="14020"; row.mode="CW";
        const auto found=contestreport::findCandidates(db,row,5); QVERIFY(found.error.isEmpty());
        QCOMPARE(found.candidates.size(),5); QVERIFY(found.truncated); QCOMPARE(found.candidates.first().id,exact);
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
        QVERIFY(matches.candidates.first().frequencyKhz.isEmpty()); QVERIFY(matches.candidates.first().receivedExchange.isEmpty());
        QVERIFY(matches.candidates.first().contestId.isEmpty());
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
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const QByteArray bytes=file.readAll(); const auto report=contestreport::parseCqWw(bytes);
        QVERIFY2(report.error.isEmpty(),qPrintable(report.error)); QCOMPARE(report.ownCall,QString("4O4A"));
        int nil=0, calls=0, exchange=0;
        for (const auto& entry : report.entries) { if (entry.kind=="not_in_log") ++nil; else if (entry.kind=="incorrect_call") ++calls; else if (entry.kind=="incorrect_exchange") ++exchange; }
        QCOMPARE(nil,14); QCOMPARE(calls,20); QCOMPARE(exchange,6); QCOMPARE(report.unrecognizedLines.size(),0);
        QVERIFY(!contestreport::parseCqWw("Call: K1ZZZ\n"+bytes).error.isEmpty());
        QVERIFY(!contestreport::parseCqWw("Call: K1ZZZ?\n"+bytes).error.isEmpty());
    }
    void acceptsExplicitIncorrectCallWithoutInventingCorrection()
    {
        const QByteArray text="Contest: 2022 CQ WORLD WIDE CW DX CONTEST\nCall: P33W\n"
            "****** Incorrect Call ******\n7020 CW 2022-11-26 0308 P33W 20 K8LDQ 05 incorrect call\n"
            "7020 CW 2022-11-26 0309 P33W 20 K8BAD 05 correct K8GOOD\n"
            "7020 CW 2022-11-26 0310 P33W 20 K8BAD 05 unknown annotation\n"
            "****** Not In Log ******\n7020 CW 2022-11-26 0311 P33W 20 K8BAD 05 incorrect call\n"
            "****** Incorrect Exchange Information ******\n7020 CW 2022-11-26 0312 P33W 20 K8BAD 05 incorrect call\n";
        const auto report=contestreport::parseCqWw(text); QVERIFY2(report.error.isEmpty(),qPrintable(report.error));
        QCOMPARE(report.entries.size(),2); QCOMPARE(report.entries[0].kind,QString("incorrect_call"));
        QVERIFY(report.entries[0].correction.isEmpty()); QCOMPARE(report.entries[1].correction,QString("K8GOOD"));
        QCOMPARE(report.unrecognizedLines.size(),3); QCOMPARE(report.unrecognizedLines[0],5);
    }
    void rejectsConflictingReportIdentity()
    {
        const QByteArray row="****** Not In Log ******\n14020 CW 2024-11-23 1201 K1ABC 05 DL1ABC 14\n";
        const QByteArray contest="Contest: 2024 CQ WORLD WIDE CW DX CONTEST\n";
        const QByteArray call="Call: K1ABC\n";
        QVERIFY(contestreport::parseCqWw(contest+call+row).error.isEmpty());
        QVERIFY(!contestreport::parseCqWw(contest+call+"Call: K2XYZ\n"+row).error.isEmpty());
        QVERIFY(!contestreport::parseCqWw(contest+"Contest: 2023 CQ WORLD WIDE CW DX CONTEST\n"+call+row).error.isEmpty());
        QVERIFY(!contestreport::parseCqWw(contest+call+"Call: K2XYZ?\n"+row).error.isEmpty());
        QVERIFY(!contestreport::parseCqWw(contest+call+"Contest: unknown edition\n"+row).error.isEmpty());
        QVERIFY(contestreport::parseCqWw(contest+contest+call+"Call: k1abc\n"+row).error.isEmpty());
        const auto body=contestreport::parseCqWw(contest+call+row+"Call: K2XYZ?\n");
        QVERIFY(body.error.isEmpty()); QCOMPARE(body.unrecognizedLines.size(),1);
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
