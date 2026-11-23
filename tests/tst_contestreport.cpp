#include "core/ContestReport.h"
#include <QTest>
#include <QFile>
using namespace decolog::core;

class TestContestReport : public QObject {
    Q_OBJECT
private slots:
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
