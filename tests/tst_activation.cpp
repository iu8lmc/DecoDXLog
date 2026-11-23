// Attivazioni e contest: campi dell'attivatore sul QSO, numero progressivo,
// nome del file per POTA, e il giro completo di una sessione sul log.
#include "app/ActivationController.h"
#include "core/Activation.h"
#include "core/LogDatabase.h"
#include "core/SotaCatalogue.h"
#include "core/PotaCatalogue.h"

#include <QSqlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>

#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

using namespace decolog::core;
using decolog::app::ActivationController;

class TestActivation : public QObject {
    Q_OBJECT

private slots:
    void selectedContestFindingsAreReadOnlyAndBoundToLog()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
        const QByteArray a="Contest: 2024 CQ WORLD WIDE CW DX CONTEST\nCall: K1ABC\n****** Not In Log ******\n14020 CW 2024-11-23 1201 K1ABC 05 DL1AAA 14\nunknown line\n";
        const QByteArray b="Contest: 2025 CQ WORLD WIDE CW DX CONTEST\nCall: K2ABC\n****** Incorrect Call ******\n7020 CW 2025-11-22 1202 K2ABC 05 DL2BAD 14 correct DL2GOOD\n";
        QFile fa(dir.filePath("a.rpt")), fb(dir.filePath("b.rpt")); QVERIFY(fa.open(QIODevice::WriteOnly)); QCOMPARE(fa.write(a),a.size()); fa.close(); QVERIFY(fb.open(QIODevice::WriteOnly)); QCOMPARE(fb.write(b),b.size()); fb.close();
        ActivationController::Context ctx; ctx.db=&db; ActivationController act(std::move(ctx)); QSignalSpy done(&act,&ActivationController::contestFindingsFinished);
        auto start=[&] { return act.startContestFindings(QUrl::fromLocalFile(fa.fileName()),QUrl::fromLocalFile(fb.fileName()),"CQ-WW-CW","2024-11-23","2024-11-23","2025-11-22","2025-11-22"); };
        QVERIFY(start()); QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,5000); const auto r=done.last().at(0).toMap();
        QVERIFY2(r.value("error").toString().isEmpty(),qPrintable(r.value("error").toString())); QCOMPARE(r.value("nilA").toInt(),1); QCOMPARE(r.value("callB").toInt(),1); QCOMPARE(r.value("unknownA").toInt(),1); QCOMPARE(db.qsoCount(),qint64(0));
        QVERIFY(fa.open(QIODevice::ReadOnly)); QCOMPARE(fa.readAll(),a); fa.close(); QVERIFY(fb.open(QIODevice::ReadOnly)); QCOMPARE(fb.readAll(),b); fb.close();
        QVERIFY(start()); QVERIFY(db.open(dir.filePath("other.sqlite"))); QTRY_COMPARE_WITH_TIMEOUT(done.size(),2,5000); QVERIFY(!done.last().at(0).toMap().value("error").toString().isEmpty());
    }
    void recentContestQsoIncludesFullUtcForAudioReview()
    {
        LogDatabase db; QVERIFY(db.open(":memory:")); ActivationController::Context ctx; ctx.db=&db;
        ActivationController act(std::move(ctx)); QVERIFY(act.start({{"kind","contest"},{"contestId","CQ-WW-CW"}}).isEmpty());
        const auto at=QDateTime::currentDateTimeUtc().addSecs(2);
        const auto id=db.insertQso(AdifRecord{{"CALL","K1ABC"},{"QSO_DATE",at.toString("yyyyMMdd")},{"TIME_ON",at.toString("HHmmss")},
            {"BAND","20m"},{"MODE","CW"}},"fixture",{},true).id; QVERIFY(id>0);
        const auto recent=act.recentQsos(10); QCOMPARE(recent.size(),1);
        const auto row=recent.first().toMap(); QCOMPARE(row.value("id").toLongLong(),id);
        QCOMPARE(QDateTime::fromString(row.value("utc").toString(),Qt::ISODate).toSecsSinceEpoch(),at.toSecsSinceEpoch());
        QVERIFY(row.value("utc").toString().endsWith('Z'));
    }
    void contestGoalsRequireCurrentLoadedLogAndPersist()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
        ActivationController::Context ctx; ctx.db=&db; ActivationController act(std::move(ctx)); QSignalSpy done(&act,&ActivationController::contestGoalsFinished);
        auto request=[&](bool save,int a,int b,int ra,int rb) { return act.requestContestGoals("CQ-WW-CW","2024-11-23","2024-11-23","2025-11-22","2025-11-22",save,a,b,ra,rb); };
        QVERIFY(!request(true,5,1,0,0)); QVERIFY(request(false,0,0,0,0)); QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,5000);
        QVERIFY(done.last().at(0).toMap().value("error").toString().isEmpty());
        QVERIFY(request(true,5,1,0,0)); QTRY_COMPARE_WITH_TIMEOUT(done.size(),2,5000);
        const auto saved=done.last().at(0).toMap(); QVERIFY2(saved.value("error").toString().isEmpty(),qPrintable(saved.value("error").toString()));
        QCOMPARE(saved.value("revisionA").toInt(),1); QVERIFY(!request(true,8,2,0,0));
        QVERIFY(db.open(dir.filePath("other.sqlite"))); QVERIFY(!request(true,8,2,1,1));
        QVERIFY(db.open(dir.filePath("log.sqlite"))); QVERIFY(request(false,0,0,0,0)); QTRY_COMPARE_WITH_TIMEOUT(done.size(),3,5000);
        const auto loaded=done.last().at(0).toMap(); QCOMPARE(loaded.value("targetA").toInt(),5); QCOMPARE(loaded.value("targetB").toInt(),1);
        QVERIFY(act.requestContestGoals("CQ-WW-CW","2024-11-23","2024-11-23","2025-11-22","2025-11-22",false,0,0,1,1,true));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),4,5000); const auto removed=done.last().at(0).toMap();
        QVERIFY2(removed.value("error").toString().isEmpty(),qPrintable(removed.value("error").toString()));
        QVERIFY(removed.value("removed").toBool()); QCOMPARE(removed.value("revisionA").toInt(),0); QCOMPARE(db.qsoCount(),qint64(0));
    }
    void contestComparisonUsesCurrentLogAndRejectsLogSwitch()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
        QVERIFY(db.insertQso(AdifRecord{{"CALL","K1AAA"},{"QSO_DATE","20241123"},{"TIME_ON","120000"},
            {"BAND","20m"},{"MODE","CW"},{"CONTEST_ID","CQ-WW-CW"}},"fixture",{},true).id>0);
        ActivationController::Context ctx; ctx.db=&db; ActivationController act(std::move(ctx));
        QSignalSpy done(&act,&ActivationController::contestComparisonFinished);
        QVERIFY(act.startContestComparison("CQ-WW-CW","2024-11-23","2024-11-23","2025-11-22","2025-11-22"));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,5000); auto result=done.first().at(0).toMap();
        QVERIFY2(result.value("error").toString().isEmpty(),qPrintable(result.value("error").toString()));
        QCOMPARE(result.value("totalA").toInt(),1); QCOMPARE(result.value("totalB").toInt(),0);
        QVERIFY(act.startContestComparison("CQ-WW-CW","2024-11-23","2024-11-23","2025-11-22","2025-11-22"));
        QVERIFY(db.open(dir.filePath("other.sqlite"))); QTRY_COMPARE_WITH_TIMEOUT(done.size(),2,5000);
        QVERIFY(!done.last().at(0).toMap().value("error").toString().isEmpty());
    }
    void contestReportCandidateSearchIsReadOnlyAndBoundToLog()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
        const auto inserted=db.insertQso(AdifRecord{{"CALL","DL1ABC"},{"QSO_DATE","20241123"},{"TIME_ON","120145"},
          {"BAND","20m"},{"MODE","CW"},{"STATION_CALLSIGN","K1ABC"}},"fixture",{},true); QVERIFY(inserted.id>0);
        ActivationController::Context ctx; ctx.db=&db; ActivationController act(std::move(ctx));
        QSignalSpy done(&act,&ActivationController::contestReportCandidatesFinished);
        QVariantMap row{{"call","DL1ABC"},{"date","2024-11-23"},{"time","1201"},{"frequency","14020"},{"mode","CW"},{"line",4}};
        QVERIFY(act.startContestReportCandidates(row)); QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,5000);
        const auto result=done.first().at(0).toMap(); QVERIFY2(result.value("error").toString().isEmpty(),qPrintable(result.value("error").toString()));
        const auto matches=result.value("candidates").toList(); QCOMPARE(matches.size(),1); QCOMPARE(matches.first().toMap().value("id").toLongLong(),inserted.id);
        QCOMPARE(matches.first().toMap().value("stationCall").toString(),QString("K1ABC"));
        const auto candidate=matches.first().toMap();
        QVERIFY(act.isContestReportCandidateCurrent(candidate.value("id").toLongLong(),candidate.value("uuid").toString(),candidate.value("revision").toInt()));
        auto changed=*db.record(inserted.id); changed.set("COMMENT","later edit"); QCOMPARE(db.updateQso(inserted.id,changed).status,InsertResult::Status::Inserted);
        QVERIFY(!act.isContestReportCandidateCurrent(candidate.value("id").toLongLong(),candidate.value("uuid").toString(),candidate.value("revision").toInt()));
        QVERIFY(act.startContestReportCandidates(row)); QVERIFY(db.open(dir.filePath("other.sqlite"))); QTRY_COMPARE_WITH_TIMEOUT(done.size(),2,5000);
        QVERIFY(!done.last().at(0).toMap().value("error").toString().isEmpty()); QCOMPARE(db.qsoCount(),qint64(0));
    }
    void contestReportCorrectedCandidateIsMarkedAndGuarded()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
        const auto inserted=db.insertQso(AdifRecord{{"CALL","DL1GOOD"},{"QSO_DATE","20241123"},{"TIME_ON","120145"},
          {"BAND","20m"},{"MODE","CW"}},"fixture",{},true); QVERIFY(inserted.id>0);
        ActivationController::Context ctx; ctx.db=&db; ActivationController act(std::move(ctx));
        QSignalSpy done(&act,&ActivationController::contestReportCandidatesFinished);
        QVariantMap row{{"kind","incorrect_call"},{"call","DL1BAD"},{"correction","DL1GOOD"},
          {"date","2024-11-23"},{"time","1201"},{"frequency","14020"},{"mode","CW"},{"line",4}};
        QVERIFY(act.startContestReportCandidates(row)); QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,5000);
        const auto result=done.first().at(0).toMap(); QVERIFY(result.value("error").toString().isEmpty());
        const auto matches=result.value("candidates").toList(); QCOMPARE(matches.size(),1);
        const auto candidate=matches.first().toMap(); QCOMPARE(candidate.value("matchBasis").toString(),QString("corrected"));
        QCOMPARE(candidate.value("call").toString(),QString("DL1GOOD"));
        QVERIFY(act.isContestReportCandidateCurrent(candidate.value("id").toLongLong(),candidate.value("uuid").toString(),candidate.value("revision").toInt()));
        QCOMPARE(db.qsoCount(),qint64(1));
    }
    void archiveDoesNotRecreateRemovedDatabase()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const QString path=dir.filePath("active.sqlite"), moved=dir.filePath("moved.sqlite");
        const QString reportPath=dir.filePath("report.rpt"); QFile report(reportPath); QVERIFY(report.open(QIODevice::WriteOnly));
        report.write("Contest: 2024 CQ WORLD WIDE CW DX CONTEST\nCall: K1ABC\n****** Not In Log ******\n"); report.close();
        LogDatabase db; QVERIFY(db.open(path)); { QSqlQuery q(db.connection()); QVERIFY(q.exec("PRAGMA journal_mode=DELETE")); }
        ActivationController::Context ctx; ctx.db=&db; ActivationController act(std::move(ctx));
        QSignalSpy preview(&act,&ActivationController::contestReportFinished); QSignalSpy saved(&act,&ActivationController::contestReportArchiveFinished);
        QVERIFY(act.startContestReportPreview(QUrl::fromLocalFile(reportPath))); QTRY_COMPARE_WITH_TIMEOUT(preview.size(),1,5000);
        QVERIFY(QFile::rename(path,moved)); QVERIFY(act.archiveContestReportPreview()); QTRY_COMPARE_WITH_TIMEOUT(saved.size(),1,5000);
        QVERIFY(!saved.first().at(0).toBool()); QVERIFY(!QFile::exists(path));
    }
    void archiveRejectsDifferentDatabaseReplacedAtSamePath()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const QString first=dir.filePath("active.sqlite"), replacement=dir.filePath("replacement.sqlite"), moved=dir.filePath("original.sqlite");
        const QString reportPath=dir.filePath("report.rpt"); QFile report(reportPath); QVERIFY(report.open(QIODevice::WriteOnly));
        report.write("Contest: 2024 CQ WORLD WIDE CW DX CONTEST\nCall: K1ABC\n****** Not In Log ******\n"); report.close();
        { LogDatabase db; QVERIFY(db.open(replacement)); QSqlQuery q(db.connection()); QVERIFY(q.exec("PRAGMA journal_mode=DELETE")); }
        LogDatabase db; QVERIFY(db.open(first)); { QSqlQuery q(db.connection()); QVERIFY(q.exec("PRAGMA journal_mode=DELETE")); }
        ActivationController::Context ctx; ctx.db=&db; ActivationController act(std::move(ctx));
        QSignalSpy preview(&act,&ActivationController::contestReportFinished); QSignalSpy saved(&act,&ActivationController::contestReportArchiveFinished);
        QVERIFY(act.startContestReportPreview(QUrl::fromLocalFile(reportPath))); QTRY_COMPARE_WITH_TIMEOUT(preview.size(),1,5000);
        QVERIFY(QFile::rename(first,moved)); QVERIFY(QFile::rename(replacement,first));
        QVERIFY(act.archiveContestReportPreview()); QTRY_COMPARE_WITH_TIMEOUT(saved.size(),1,5000);
        QVERIFY(!saved.first().at(0).toBool()); LogDatabase verify; QVERIFY(verify.open(first)); QVERIFY(verify.contestReports().isEmpty());
    }
    void archivedContestReportExportIsExactAndProtectsLog()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const QString path=dir.filePath("log.sqlite");
        const QByteArray bytes="Contest: 2024 CQ WORLD WIDE CW DX CONTEST\r\nCall: K1ABC\r\n****** Not In Log ******\r\nunknown\r\n";
        LogDatabase db; QVERIFY(db.open(path)); QString error; const qint64 id=db.archiveContestReport(bytes,"file.rpt",&error); QVERIFY(id>0);
        ActivationController::Context ctx; ctx.db=&db; ActivationController act(std::move(ctx));
        QSignalSpy history(&act,&ActivationController::contestReportHistoryFinished); QSignalSpy exported(&act,&ActivationController::contestReportExportFinished);
        QVERIFY(act.startContestReportHistory()); QTRY_COMPARE_WITH_TIMEOUT(history.size(),1,5000);
        const auto row=history.first().at(0).toMap().value("reports").toList().first().toMap(); const auto sha=row.value("sha256").toString();
        QVERIFY(!act.startContestReportExport(id,sha,QUrl::fromLocalFile(path)));
        QVERIFY(!act.startContestReportExport(id,sha,QUrl::fromLocalFile(path+"-wal")));
        const QString alias=dir.filePath("alias.sqlite"); QVERIFY(QFile::link(path,alias));
        QVERIFY(!act.startContestReportExport(id,sha,QUrl::fromLocalFile(alias)));
        QVERIFY(!act.startContestReportExport(id,QStringLiteral("wrong"),QUrl::fromLocalFile(dir.filePath("wrong.rpt"))));
        const QString output=dir.filePath("copy.rpt"); QVERIFY(act.startContestReportExport(id,sha,QUrl::fromLocalFile(output)));
        QTRY_COMPARE_WITH_TIMEOUT(exported.size(),1,5000); QVERIFY2(exported.first().at(0).toBool(),qPrintable(exported.first().at(1).toString()));
        QFile copy(output); QVERIFY(copy.open(QIODevice::ReadOnly)); QCOMPARE(copy.readAll(),bytes);
        QCOMPARE(db.contestReportBytes(id),bytes); QCOMPARE(db.qsoCount(),qint64(0));
        QSqlQuery corrupt(db.connection()); corrupt.prepare("UPDATE contest_report_archive SET original=? WHERE id=?");
        corrupt.addBindValue(QByteArray("changed")); corrupt.addBindValue(id); QVERIFY(corrupt.exec());
        const QString guarded=dir.filePath("existing.rpt"); QFile existing(guarded); QVERIFY(existing.open(QIODevice::WriteOnly)); existing.write("keep"); existing.close();
        QVERIFY(act.startContestReportExport(id,sha,QUrl::fromLocalFile(guarded))); QTRY_COMPARE_WITH_TIMEOUT(exported.size(),2,5000);
        QVERIFY(!exported.last().at(0).toBool()); QVERIFY(existing.open(QIODevice::ReadOnly)); QCOMPARE(existing.readAll(),QByteArray("keep"));
    }
    void archivedContestReportHistoryReopensAndChecksIdentity()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const QString log=dir.filePath("log.sqlite");
        const QByteArray bytes="Contest: 2024 CQ WORLD WIDE CW DX CONTEST\nCall: K1ABC\n****** Not In Log ******\n"
            "14020 CW 2024-11-23 1201 K1ABC 05 DL1ABC 14\nunknown line\n";
        LogDatabase db; QVERIFY(db.open(log)); QString error; const qint64 id=db.archiveContestReport(bytes,"source.rpt",&error); QVERIFY2(id>0,qPrintable(error));
        ActivationController::Context ctx; ctx.db=&db; ActivationController act(std::move(ctx));
        QSignalSpy history(&act,&ActivationController::contestReportHistoryFinished);
        QSignalSpy preview(&act,&ActivationController::contestReportFinished);
        QVERIFY(act.startContestReportHistory()); QTRY_COMPARE_WITH_TIMEOUT(history.size(),1,5000);
        const auto list=history.first().at(0).toMap().value("reports").toList(); QCOMPARE(list.size(),1);
        const auto row=list.first().toMap(); QCOMPARE(row.value("id").toLongLong(),id);
        QVERIFY(!act.startArchivedContestReportPreview(id,QStringLiteral("wrong")));
        QVERIFY(act.startArchivedContestReportPreview(id,row.value("sha256").toString())); QTRY_COMPARE_WITH_TIMEOUT(preview.size(),1,5000);
        const auto result=preview.first().at(0).toMap(); QVERIFY2(result.value("error").toString().isEmpty(),qPrintable(result.value("error").toString()));
        QCOMPARE(result.value("total").toInt(),1); QCOMPARE(result.value("unknownTotal").toInt(),1);
        QCOMPARE(db.contestReportBytes(id),bytes); QCOMPARE(db.qsoCount(),qint64(0));
    }
    void archivedContestReportHistoryPagesWithoutLosingRows()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); LogDatabase db; QVERIFY(db.open(dir.filePath("paged.sqlite")));
        QVERIFY(db.connection().transaction()); QSqlQuery q(db.connection());
        q.prepare("INSERT INTO contest_report_archive(sha256,filename,contest,own_call,original,imported_at) VALUES(?,?,?,?,?,?)");
        for (int i=1;i<=101;++i) { q.bindValue(0,QString::number(i)); q.bindValue(1,"report.rpt"); q.bindValue(2,"contest"); q.bindValue(3,"K1ABC"); q.bindValue(4,QByteArray("fixture")); q.bindValue(5,"2026-01-01"); QVERIFY(q.exec()); }
        QVERIFY(db.connection().commit()); ActivationController::Context ctx; ctx.db=&db; ActivationController act(std::move(ctx));
        QSignalSpy done(&act,&ActivationController::contestReportHistoryFinished); QVERIFY(act.startContestReportHistory()); QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,5000);
        const auto first=done.last().at(0).toMap(); const auto rows=first.value("reports").toList(); QCOMPARE(rows.size(),100); QVERIFY(first.value("hasMore").toBool());
        const qint64 cursor=rows.last().toMap().value("id").toLongLong(); QVERIFY(act.startContestReportHistory(cursor)); QTRY_COMPARE_WITH_TIMEOUT(done.size(),2,5000);
        const auto second=done.last().at(0).toMap(); QCOMPARE(second.value("reports").toList().size(),1); QVERIFY(!second.value("hasMore").toBool());
        QVERIFY(!act.startArchivedContestReportPreview(rows.first().toMap().value("id").toLongLong(),rows.first().toMap().value("sha256").toString()));
    }
    void archivedContestReportHistoryRejectsLogSwitchAndCorruption()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path=dir.filePath("first.sqlite");
        const QByteArray bytes="Contest: 2024 CQ WORLD WIDE CW DX CONTEST\nCall: K1ABC\n****** Not In Log ******\n";
        LogDatabase db; QVERIFY(db.open(path)); QString error; const qint64 id=db.archiveContestReport(bytes,"report.rpt",&error); QVERIFY(id>0);
        ActivationController::Context ctx; ctx.db=&db; ActivationController act(std::move(ctx));
        QSignalSpy history(&act,&ActivationController::contestReportHistoryFinished); QSignalSpy preview(&act,&ActivationController::contestReportFinished);
        QVERIFY(act.startContestReportHistory()); QVERIFY(db.open(dir.filePath("second.sqlite"))); QTRY_COMPARE_WITH_TIMEOUT(history.size(),1,5000);
        QVERIFY(!history.last().at(0).toMap().value("error").toString().isEmpty());
        QVERIFY(db.open(path)); QVERIFY(act.startContestReportHistory()); QTRY_COMPARE_WITH_TIMEOUT(history.size(),2,5000);
        const auto row=history.last().at(0).toMap().value("reports").toList().first().toMap();
        QSqlQuery q(db.connection()); q.prepare("UPDATE contest_report_archive SET original=? WHERE id=?"); q.addBindValue(QByteArray("tampered")); q.addBindValue(id); QVERIFY(q.exec());
        QVERIFY(act.startArchivedContestReportPreview(id,row.value("sha256").toString())); QTRY_COMPARE_WITH_TIMEOUT(preview.size(),1,5000);
        QVERIFY(preview.last().at(0).toMap().value("error").toString().contains("damaged")); QCOMPARE(db.qsoCount(),qint64(0));
    }
    void archiveContestReportUsesReviewedSnapshotAndRejectsLogChange()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const QString filePath=dir.filePath("report.rpt");
        const QByteArray original="Contest: 2024 CQ WORLD WIDE CW DX CONTEST\nCall: K1ABC\n****** Not In Log ******\nunknown row\n";
        QFile file(filePath); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(original),original.size()); file.close();
        LogDatabase db; QVERIFY(db.open(dir.filePath("a.sqlite"))); ActivationController::Context ctx; ctx.db=&db; ActivationController act(std::move(ctx));
        QSignalSpy preview(&act,&ActivationController::contestReportFinished);
        QSignalSpy saved(&act,&ActivationController::contestReportArchiveFinished);
        QVERIFY(act.startContestReportPreview(QUrl::fromLocalFile(filePath))); QTRY_COMPARE_WITH_TIMEOUT(preview.size(),1,5000);
        QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate)); file.write("changed after preview"); file.close();
        QVERIFY(act.archiveContestReportPreview()); QTRY_COMPARE_WITH_TIMEOUT(saved.size(),1,5000);
        QVERIFY2(saved.first().at(0).toBool(),qPrintable(saved.first().at(1).toString()));
        const auto reports=db.contestReports(); QCOMPARE(reports.size(),1); QCOMPARE(db.contestReportBytes(reports.first().id),original);
        QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate)); file.write(original); file.close();
        QVERIFY(act.startContestReportPreview(QUrl::fromLocalFile(filePath))); QTRY_COMPARE_WITH_TIMEOUT(preview.size(),2,5000);
        QVERIFY(db.open(dir.filePath("b.sqlite"))); QVERIFY(!act.archiveContestReportPreview()); QVERIFY(db.contestReports().isEmpty());
        QVERIFY(db.open(dir.filePath("a.sqlite"))); QVERIFY(!act.archiveContestReportPreview()); QCOMPARE(db.contestReports().size(),1);
    }
    void cqWwReportPreviewIsReadOnlyAndShowsUnknownRows()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const QString path=dir.filePath("report.rpt");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray original="Contest: 2024 CQ WORLD WIDE CW DX CONTEST\nCall: K1ABC\n"
            "****** Incorrect Call ******\n 14020 CW 2024-11-23 1201 K1ABC 05 DL1BAD 14 correct DL1GOOD\n"
            "unexpected line\n****** Summary ******\nunchanged\n";
        QCOMPARE(file.write(original),original.size()); file.close();
        LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
        const auto qso=db.insertQso(AdifRecord{{"CALL","DL1BAD"},{"QSO_DATE","20241123"},{"TIME_ON","1201"},
              {"BAND","20m"},{"MODE","CW"}},"test",{},true); QVERIFY(qso.id>0);
        ActivationController::Context ctx; ctx.db=&db; ActivationController act(std::move(ctx));
        QSignalSpy done(&act,&ActivationController::contestReportFinished);
        QVERIFY(act.startContestReportPreview(QUrl::fromLocalFile(path)));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,5000);
        const auto result=done.first().at(0).toMap(); QVERIFY2(result.value("error").toString().isEmpty(),qPrintable(result.value("error").toString()));
        QCOMPARE(result.value("total").toInt(),1); QCOMPARE(result.value("unknownTotal").toInt(),1);
        QCOMPARE(result.value("entries").toList().first().toMap().value("call").toString(),QString("DL1BAD"));
        QCOMPARE(result.value("unknown").toList().first().toMap().value("text").toString(),QString("unexpected line"));
        QCOMPARE(db.qsoCount(),qint64(1)); QCOMPARE(db.record(qso.id)->value("CALL"),QString("DL1BAD"));
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(),original);
    }
    void cqWwReportPreviewBoundsRowsWithoutHidingTotals()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); QFile file(dir.filePath("large.rpt")); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("Contest: 2024 CQ WORLD WIDE CW DX CONTEST\nCall: K1ABC\n****** Not In Log ******\n");
        for (int i=0;i<501;++i) file.write("14020 CW 2024-11-23 1201 K1ABC 05 DL1ABC 14\n");
        for (int i=0;i<201;++i) file.write("unrecognised row\n");
        file.close(); ActivationController::Context ctx; ActivationController act(std::move(ctx));
        QSignalSpy done(&act,&ActivationController::contestReportFinished);
        QVERIFY(act.startContestReportPreview(QUrl::fromLocalFile(file.fileName()))); QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,5000);
        const auto result=done.first().at(0).toMap(); QVERIFY(result.value("error").toString().isEmpty());
        QCOMPARE(result.value("total").toInt(),501); QCOMPARE(result.value("unknownTotal").toInt(),201);
        QCOMPARE(result.value("entries").toList().size(),500); QCOMPARE(result.value("unknown").toList().size(),200);
    }
    void recoveredQsoUsesOnlyUnambiguousJournalPeriod()
    {
        LogDatabase db; QVERIFY(db.open(":memory:")); ActivationController::Context ctx; ctx.db=&db;
        ActivationController act(std::move(ctx));
        Activation first=Activation::fromMap({{"active",true},{"kind","pota"},{"reference","IT-0001"},{"myGrid","JM75"},{"sessionUuid","run-1"}});
        Activation second=first; second.reference="IT-0002"; second.myGrid="JM76";
        const auto json=[](const Activation& a) { return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(a.toMap())).toJson(QJsonDocument::Compact)); };
        QSqlQuery q(db.connection()); q.prepare("INSERT INTO activation_run(uuid,started_at,ended_at,initial_json,last_json) VALUES(?,?,?,?,?)");
        q.addBindValue("run-1"); q.addBindValue("2026-10-23T10:00:00.500Z"); q.addBindValue("2026-10-23T10:10:00.500Z"); q.addBindValue(json(first)); q.addBindValue(json(second)); QVERIFY(q.exec());
        auto event=[&](const QString& at, const QString& type, const Activation& state) {
            QSqlQuery e(db.connection()); e.prepare("INSERT INTO activation_run_change(session_uuid,changed_at,event,state_json) VALUES(?,?,?,?)");
            e.addBindValue("run-1"); e.addBindValue(at); e.addBindValue(type); e.addBindValue(json(state)); return e.exec(); };
        QVERIFY(event("2026-10-23T10:00:00.500Z","start",first));
        QVERIFY(event("2026-10-23T10:05:00.500Z","update",second));
        QVERIFY(event("2026-10-23T10:10:00.500Z","stop",second));
        auto record=[](const char* on, const char* off) { return AdifRecord{{"CALL","K1ABC"},{"QSO_DATE","20261023"},{"TIME_ON",QString::fromLatin1(on)},{"TIME_OFF",QString::fromLatin1(off)}}; };
        auto a=act.recoveryAssignment(record("100100","100200")); QCOMPARE(a.status, ActivationController::RecoveryAssignment::Status::Resolved);
        QCOMPARE(a.session.reference, QString("IT-0001")); QCOMPARE(a.session.myGrid, QString("JM75"));
        auto b=act.recoveryAssignment(record("100600","100700")); QCOMPARE(b.status, ActivationController::RecoveryAssignment::Status::Resolved);
        QCOMPARE(b.session.reference, QString("IT-0002")); QCOMPARE(b.session.myGrid, QString("JM76"));
        QCOMPARE(act.recoveryAssignment(record("100459","100501")).status, ActivationController::RecoveryAssignment::Status::Uncertain);
        QCOMPARE(act.recoveryAssignment(record("1004","1005")).status, ActivationController::RecoveryAssignment::Status::Uncertain);
        QCOMPARE(act.recoveryAssignment(record("bad","1002")).status, ActivationController::RecoveryAssignment::Status::Uncertain);
        QCOMPARE(act.recoveryAssignment(record("1001","")).status, ActivationController::RecoveryAssignment::Status::Uncertain);
        QCOMPARE(act.recoveryAssignment(record("101100","101200")).status, ActivationController::RecoveryAssignment::Status::None);
        QVERIFY(db.connection().transaction());
        QCOMPARE(act.recoveryAssignment(record("100100","100200")).status, ActivationController::RecoveryAssignment::Status::Resolved);
        QVERIFY(db.connection().rollback());
        AdifRecord conflicting=record("100100","100200"); conflicting.set("APP_DECOLOG_ACTIVATION_UUID","other-run");
        QCOMPARE(act.recoveryAssignment(conflicting).status, ActivationController::RecoveryAssignment::Status::Uncertain);
        QVERIFY(q.exec("INSERT INTO activation_run(uuid,started_at,ended_at,initial_json,last_json) VALUES('run-2','2026-10-23T10:01:00.000Z','2026-10-23T10:03:00.000Z','{}','{}')"));
        QCOMPARE(act.recoveryAssignment(record("100130","100200")).status, ActivationController::RecoveryAssignment::Status::Uncertain);
    }
    void activationHasStableSessionIdentity()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        ActivationController::Context ctx; ctx.db = &db;
        ActivationController act(std::move(ctx));
        QVERIFY(act.start({{"kind", "pota"}, {"reference", "IT-0001"}}).isEmpty());
        QVERIFY(!act.state().value("sessionUuid").toString().isEmpty());
    }
    void trackedLifecycleIsAtomicAndLegacyStaysUntracked()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        ActivationController::Context ctx; ctx.db = &db;
        ActivationController act(std::move(ctx));
        QSqlQuery q(db.connection());
        QVERIFY(q.exec("CREATE TRIGGER reject_run BEFORE INSERT ON activation_run BEGIN SELECT RAISE(ABORT,'rejected'); END"));
        QVERIFY(!act.start({{"kind", "pota"}, {"reference", "IT-0001"}}).isEmpty());
        QVERIFY(!act.active());
        QVERIFY(q.exec("DROP TRIGGER reject_run"));
        QVERIFY(act.start({{"kind", "pota"}, {"reference", "IT-0001"}}).isEmpty());
        const QString id = act.state().value("sessionUuid").toString(); QVERIFY(!id.isEmpty());
        AdifRecord first{{"CALL", "K1ABC"}}; act.session().applyTo(first, 0);
        QCOMPARE(first.value("APP_DECOLOG_ACTIVATION_UUID"), id);
        AdifRecord external{{"APP_DECOLOG_ACTIVATION_UUID", "external"}}; act.session().applyTo(external, 0);
        QCOMPARE(external.value("APP_DECOLOG_ACTIVATION_UUID"), QString("external"));
        auto updated = act.state(); updated["reference"] = "IT-0002"; updated["sessionUuid"] = "forged";
        QVERIFY(q.exec("CREATE TRIGGER reject_change BEFORE INSERT ON activation_run_change BEGIN SELECT RAISE(ABORT,'rejected'); END"));
        QVERIFY(!act.update(updated).isEmpty()); QCOMPARE(act.state().value("reference").toString(), QString("IT-0001"));
        act.stop(); QVERIFY(act.active());
        QVERIFY(q.exec("DROP TRIGGER reject_change"));
        QVERIFY(act.update(updated).isEmpty()); QCOMPARE(act.state().value("sessionUuid").toString(), id);
        act.stop(); QVERIFY(!act.active());
        QVERIFY(q.exec("SELECT ended_at, (SELECT COUNT(*) FROM activation_run_change WHERE session_uuid=activation_run.uuid) FROM activation_run"));
        QVERIFY(q.next()); QVERIFY(!q.value(0).toString().isEmpty()); QCOMPARE(q.value(1).toInt(), 3);
        db.setSetting("activation.session", R"({"active":true,"kind":"pota","reference":"IT-0003","startedAt":"2026-11-23T10:00:00Z"})");
        ActivationController::Context legacyContext; legacyContext.db = &db;
        ActivationController legacy(std::move(legacyContext)); legacy.load(); QVERIFY(legacy.active());
        QVERIFY(legacy.state().value("sessionUuid").toString().isEmpty());
        QVERIFY(!legacy.partitionPreview().value("error").toString().isEmpty());
        AdifRecord old{{"CALL", "K1OLD"}}; legacy.session().applyTo(old, 0);
        QVERIFY(old.value("APP_DECOLOG_ACTIVATION_UUID").isEmpty());
        legacy.stop(); QVERIFY(!legacy.active());
        QVERIFY(q.exec("SELECT COUNT(*) FROM activation_run")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 1);
    }
    void uncertainRecoveredContactIsExcludedUntilReviewed()
    {
        LogDatabase db; QVERIFY(db.open(":memory:")); ActivationController::Context ctx; ctx.db=&db; ActivationController act(std::move(ctx));
        QVERIFY(act.start({{"kind","pota"},{"reference","IT-0001"}}).isEmpty());
        AdifRecord qso{{"CALL","K1AAA"},{"QSO_DATE","20261101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}};
        act.session().applyTo(qso,0); qso.set("APP_DECOLOG_ACTIVATION_REVIEW","PERIOD_UNCERTAIN");
        const auto inserted=db.insertQso(qso,"fixture",{},true,0,false); QVERIFY(inserted.id>0);
        const auto pending=act.partitionPreview(); QCOMPARE(pending.value("reportedCount").toInt(),1); QVERIFY(pending.value("groups").toList().isEmpty());
        QCOMPARE(pending.value("uncertainPeriod").toInt(),1); const auto excluded=pending.value("excluded").toList(); QCOMPARE(excluded.size(),1); QCOMPARE(excluded.first().toMap().value("reason").toString(),QString("uncertainPeriod"));
        qso.remove("APP_DECOLOG_ACTIVATION_REVIEW"); QVERIFY(db.updateQso(inserted.id,qso).status==InsertResult::Status::Inserted);
        const auto reviewed=act.partitionPreview(); QCOMPARE(reviewed.value("groups").toList().size(),1); QCOMPARE(reviewed.value("uncertainPeriod").toInt(),0);
    }
    void previewGroupsReportedQsosWithoutAttributingUnrelatedContacts()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        ActivationController::Context ctx; ctx.db = &db;
        ActivationController act(std::move(ctx));
        QVERIFY(act.start({{"kind", "pota"}, {"reference", "IT-0001,IT-0002"},
                           {"programs", QVariantMap{{"sota", "I/LM-001"}}}}).isEmpty());
        AdifRecord one{{"CALL","K1AAA"},{"QSO_DATE","20261231"},{"TIME_ON","235900"}, {"BAND","20m"},{"MODE","CW"},{"OPERATOR","9H1AA"},{"MY_GRIDSQUARE","JN35"},{"POTA_REF","US-1234"}};
        act.session().applyTo(one, 0); QCOMPARE(db.insertQso(one, "manual", {}, true, 0, false).status, InsertResult::Status::Inserted);
        AdifRecord two{{"CALL","K1BBB"},{"QSO_DATE","20270101"},{"TIME_ON","000100"}, {"BAND","20m"},{"MODE","CW"},{"OPERATOR","9H1BB"},{"MY_GRIDSQUARE","JN36"}};
        act.session().applyTo(two, 0); QCOMPARE(db.insertQso(two, "manual", {}, true, 0, false).status, InsertResult::Status::Inserted);
        AdifRecord unrelated{{"CALL","K1CCC"},{"QSO_DATE","20270101"},{"TIME_ON","000200"},{"BAND","20m"},{"MODE","CW"},{"MY_POTA_REF","IT-0001"}};
        QCOMPARE(db.insertQso(unrelated, "manual", {}, true, 0, false).status, InsertResult::Status::Inserted);
        const QVariantMap preview = act.partitionPreview();
        QCOMPARE(preview.value("error").toString(), QString());
        QCOMPARE(preview.value("reportedCount").toInt(), 2);
        const auto groups = preview.value("groups").toList(); QCOMPARE(groups.size(), 6);
        QSet<QString> observed;
        for (const auto& v : groups) { const auto g = v.toMap(); QCOMPARE(g.value("count").toInt(), 1);
            observed.insert(g.value("program").toString()+"/"+g.value("reference").toString()+"/"+g.value("day").toString()+"/"+g.value("operator").toString()+"/"+g.value("grid").toString()); }
        QVERIFY(observed.contains("POTA/IT-0001/2026-12-31/9H1AA/JN35"));
        QVERIFY(observed.contains("POTA/IT-0002/2026-12-31/9H1AA/JN35"));
        QVERIFY(observed.contains("SOTA/I/LM-001/2027-01-01/9H1BB/JN36"));
        QVERIFY(!observed.contains("POTA/US-1234/2026-12-31/9H1AA/JN35"));
        AdifRecord noRef{{"CALL","K1DDD"},{"QSO_DATE","20270101"},{"TIME_ON","000300"},{"BAND","20m"},{"MODE","CW"},
                          {"APP_DECOLOG_ACTIVATION_UUID",act.state().value("sessionUuid").toString()}};
        QCOMPARE(db.insertQso(noRef, "manual", {}, true, 0, false).status, InsertResult::Status::Inserted);
        const auto again = act.partitionPreview(); QCOMPARE(again.value("missingReference").toInt(), 1);
        QCOMPARE(again.value("reportedCount").toInt(), 3);
        const auto excluded = again.value("excluded").toList(); QCOMPARE(excluded.size(), 1);
        QCOMPARE(excluded.at(0).toMap().value("call").toString(), QString("K1DDD"));
        QCOMPARE(excluded.at(0).toMap().value("reason").toString(), QString("missingReference"));
        AdifRecord damaged{{"CALL","K1EEE"},{"QSO_DATE","20270101"},{"TIME_ON","000400"},{"BAND","20m"},{"MODE","CW"}};
        act.session().applyTo(damaged, 0); const auto inserted = db.insertQso(damaged, "manual", {}, true, 0, false);
        QCOMPARE(inserted.status, InsertResult::Status::Inserted);
        QSqlQuery corrupt(db.connection()); corrupt.prepare("UPDATE qso SET qso_datetime_on='invalid' WHERE id=?");
        corrupt.addBindValue(inserted.id); QVERIFY(corrupt.exec());
        const auto withBadDate = act.partitionPreview(); QCOMPARE(withBadDate.value("missingDate").toInt(), 1);
        const auto badRows = withBadDate.value("excluded").toList(); QCOMPARE(badRows.size(), 2);
        QCOMPARE(badRows.at(1).toMap().value("call").toString(), QString("K1EEE"));
        QCOMPARE(badRows.at(1).toMap().value("reason").toString(), QString("missingDate"));
        QVERIFY(db.connection().transaction());
        QVERIFY(!act.partitionPreview().value("error").toString().isEmpty());
        QVERIFY(db.connection().rollback());
    }
    void previewReportsPotaSnapshotWithoutHistoricalClaims()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const QString source = dir.filePath("parks.csv"); QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly)); file.write("reference,name,active,locationDesc\nUS-0015,Park,1,\"US-DC,US-MD\"\nIT-0001,Old Park,0,IT-25\n"); file.close();
        LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite"))); ActivationController::Context ctx;
        ctx.db = &db; ctx.potaCataloguePath = dir.filePath("pota.sqlite"); ActivationController act(std::move(ctx));
        auto* catalogue = qobject_cast<PotaCatalogue*>(act.potaCatalogue()); QVERIFY(catalogue);
        QSignalSpy imported(catalogue, &PotaCatalogue::finished); QVERIFY(catalogue->importFile(QUrl::fromLocalFile(source)));
        QTRY_COMPARE_WITH_TIMEOUT(imported.size(), 1, 10000); QVERIFY(imported.at(0).at(0).toBool());
        QVERIFY(act.start({{"kind", "pota"}, {"reference", "US-0015@US-MD,US-0015@US-CA,IT-0001,US-9999"}}).isEmpty());
        AdifRecord record{{"CALL", "K1ABC"}, {"QSO_DATE", "20200101"}, {"TIME_ON", "235900"}, {"BAND", "20m"}, {"MODE", "CW"}};
        act.session().applyTo(record, 0); QCOMPARE(db.insertQso(record, "manual", {}, true, 0, false).status, InsertResult::Status::Inserted);
        const auto preview = act.partitionPreview(); const auto groups = preview.value("groups").toList(); QCOMPARE(groups.size(), 4);
        QCOMPARE(preview.value("potaCatalogue").toMap().value("source").toString(), QString("local-file-unverified"));
        QMap<QString, QVariantMap> rows; for (const auto& item : groups) { const auto g = item.toMap(); rows[g.value("reference").toString()] = g; }
        QCOMPARE(rows.value("US-0015@US-MD").value("potaSnapshotStatus").toString(), QString("listedActive"));
        QCOMPARE(rows.value("US-0015@US-MD").value("potaLocationStatus").toString(), QString("listed"));
        QCOMPARE(rows.value("US-0015@US-CA").value("potaLocationStatus").toString(), QString("notListed"));
        QCOMPARE(rows.value("IT-0001").value("potaSnapshotStatus").toString(), QString("listedInactive"));
        QCOMPARE(rows.value("US-9999").value("potaSnapshotStatus").toString(), QString("notFound"));
        QVERIFY(QFile::remove(dir.filePath("pota.sqlite")));
        const auto missing = act.partitionPreview(); QCOMPARE(missing.value("potaCatalogue").toMap().value("status").toString(), QString("unavailable"));
    }
    void previewChecksSotaCatalogueForEachUtcDay()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const QString source = dir.filePath("summits.csv"); QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("SOTA Summits List (Date=22/11/2026)\nSummitCode,AssociationName,RegionName,SummitName,AltM,AltFt,GridRef1,GridRef2,Longitude,Latitude,Points,BonusPoints,ValidFrom,ValidTo,ActivationCount,ActivationDate,ActivationCall\nI/LM-001,Italy,Lombardy,Monte Test,100,328,0,0,9,45,2,0,01/01/2020,31/12/2026,0,,\n"); file.close();
        LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite"))); ActivationController::Context ctx;
        ctx.db = &db; ctx.sotaCataloguePath = dir.filePath("sota.sqlite"); ActivationController act(std::move(ctx));
        auto* catalogue = qobject_cast<SotaCatalogue*>(act.sotaCatalogue()); QVERIFY(catalogue);
        QSignalSpy imported(catalogue, &SotaCatalogue::finished); QVERIFY(catalogue->importFile(QUrl::fromLocalFile(source)));
        QTRY_COMPARE_WITH_TIMEOUT(imported.size(), 1, 10000); QVERIFY(imported.at(0).at(0).toBool());
        QVERIFY(act.start({{"kind", "sota"}, {"reference", "I/LM-001"}}).isEmpty());
        for (const auto& day : {QString("20261231"), QString("20270101")}) {
            AdifRecord record{{"CALL", "K1ABC"}, {"QSO_DATE", day}, {"TIME_ON", "235900"}, {"BAND", "20m"}, {"MODE", "CW"}};
            act.session().applyTo(record, 0); QCOMPARE(db.insertQso(record, "manual", {}, true, 0, false).status, InsertResult::Status::Inserted);
        }
        AdifRecord unknown{{"CALL", "K1ZZZ"}, {"QSO_DATE", "20261231"}, {"TIME_ON", "235900"}, {"BAND", "20m"}, {"MODE", "CW"}, {"MY_SOTA_REF", "I/LM-999"}};
        act.session().applyTo(unknown, 0); QCOMPARE(db.insertQso(unknown, "manual", {}, true, 0, false).status, InsertResult::Status::Inserted);
        const auto preview = act.partitionPreview(); const auto groups = preview.value("groups").toList(); QCOMPARE(groups.size(), 3);
        QCOMPARE(preview.value("sotaCatalogue").toMap().value("version").toString(), QString("2026-11-22"));
        QMap<QString, QString> status;
        for (const auto& item : groups) { const auto g = item.toMap(); status[g.value("reference").toString()+"/"+g.value("day").toString()] = g.value("catalogueStatus").toString(); }
        QCOMPARE(status.value("I/LM-001/2026-12-31"), QString("active"));
        QCOMPARE(status.value("I/LM-001/2027-01-01"), QString("outsideValidity"));
        QCOMPARE(status.value("I/LM-999/2026-12-31"), QString("notFound"));
        QSignalSpy previewed(&act, &ActivationController::previewFinished); QVERIFY(act.startPartitionPreview());
        QTRY_COMPARE_WITH_TIMEOUT(previewed.size(), 1, 5000); QVERIFY(previewed.at(0).at(0).toBool());
        QCOMPARE(act.previewResult().value("sotaCatalogue").toMap().value("version").toString(), QString("2026-11-22"));
        imported.clear(); QVERIFY(catalogue->importFile(QUrl::fromLocalFile(source)));
        QVERIFY(act.previewResult().isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(imported.size(), 1, 10000); QVERIFY(imported.at(0).at(0).toBool());
        QVERIFY(QFile::remove(dir.filePath("sota.sqlite")));
        const auto missing = act.partitionPreview();
        QCOMPARE(missing.value("sotaCatalogue").toMap().value("status").toString(), QString("unavailable"));
        for (const auto& item : missing.value("groups").toList()) QCOMPARE(item.toMap().value("catalogueStatus").toString(), QString("unavailable"));
    }
    void previewWarnsAboutLocallyUnrecognizedOperatorReference()
    {
        LogDatabase db; QVERIFY(db.open(":memory:")); ActivationController::Context ctx; ctx.db = &db;
        ActivationController act(std::move(ctx)); QVERIFY(act.start({{"kind", "pota"}, {"reference", "IT-0001"}}).isEmpty());
        AdifRecord r{{"CALL","K1ABC"},{"QSO_DATE","20261231"},{"TIME_ON","230000"},{"BAND","20m"},{"MODE","CW"},
                     {"MY_SOTA_REF","not-a-summit"},{"SOTA_REF","W2/WE-003"}};
        act.session().applyTo(r, 0); QCOMPARE(db.insertQso(r, "manual", {}, true, 0, false).status, InsertResult::Status::Inserted);
        bool foundPota = false, foundSota = false;
        for (const auto& value : act.partitionPreview().value("groups").toList()) {
            const auto group = value.toMap();
            if (group.value("program") == "POTA") { foundPota = true; QVERIFY(!group.value("referenceSyntaxWarning").toBool()); }
            if (group.value("program") == "SOTA") { foundSota = true; QVERIFY(group.value("referenceSyntaxWarning").toBool());
                QCOMPARE(group.value("reference").toString(), QString("NOT-A-SUMMIT")); }
        }
        QVERIFY(foundPota); QVERIFY(foundSota); QCOMPARE(db.record(1)->value("SOTA_REF"), QString("W2/WE-003"));
    }
    void previewDistinguishesMissingCallIdentities()
    {
        LogDatabase db; QVERIFY(db.open(":memory:")); ActivationController::Context ctx; ctx.db = &db;
        ActivationController act(std::move(ctx)); QVERIFY(act.start({{"kind", "pota"}, {"reference", "IT-0001"}}).isEmpty());
        const QList<AdifRecord> inputs{
            AdifRecord{{"CALL","K1AAA"},{"QSO_DATE","20261231"},{"TIME_ON","230000"},{"BAND","20m"},{"MODE","CW"}},
            AdifRecord{{"CALL","K1BBB"},{"QSO_DATE","20261231"},{"TIME_ON","230100"},{"BAND","20m"},{"MODE","CW"},{"STATION_CALLSIGN","9H1AA"}},
            AdifRecord{{"CALL","K1CCC"},{"QSO_DATE","20261231"},{"TIME_ON","230200"},{"BAND","20m"},{"MODE","CW"},{"STATION_CALLSIGN","9H1AA"},{"OPERATOR","9H1AA"}}};
        for (auto record : inputs) { act.session().applyTo(record, 0); QCOMPARE(db.insertQso(record, "manual", {}, true, 0, false).status, InsertResult::Status::Inserted); }
        const auto groups = act.partitionPreview().value("groups").toList(); QCOMPARE(groups.size(), 2);
        for (const auto& item : groups) {
            const auto group = item.toMap();
            if (group.value("operator").toString().isEmpty()) {
                QCOMPARE(group.value("count").toInt(), 2); QCOMPARE(group.value("missingOperator").toInt(), 2);
                QCOMPARE(group.value("missingStationCall").toInt(), 1); QCOMPARE(group.value("missingCallIdentity").toInt(), 1);
            } else {
                QCOMPARE(group.value("count").toInt(), 1); QCOMPARE(group.value("missingCallIdentity").toInt(), 0);
                QCOMPARE(group.value("missingStationCall").toInt(), 0); QCOMPARE(group.value("missingOperator").toInt(), 0);
            }
        }
    }
    void previewScalesPastFormerLimit()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        LogDatabase db; QVERIFY(db.open(dir.filePath("large.sqlite")));
        ActivationController::Context ctx; ctx.db = &db; ActivationController act(std::move(ctx));
        QVERIFY(act.start({{"kind", "pota"}, {"reference", "IT-0001"}}).isEmpty());
        for (int n = 0; n < 1001; ++n) {
            AdifRecord r{{"CALL", QString("K%1AAA").arg(n)}, {"QSO_DATE", "20261231"},
                         {"TIME_ON", "235900"}, {"BAND", "20m"}, {"MODE", "CW"}};
            act.session().applyTo(r, 0);
            QCOMPARE(db.insertQso(r, "manual", {}, true, 0, false).status, InsertResult::Status::Inserted);
        }
        const auto result = act.partitionPreview();
        QVERIFY(result.value("error").toString().isEmpty());
        QCOMPARE(result.value("reportedCount").toInt(), 1001);
        QCOMPARE(result.value("groups").toList().at(0).toMap().value("count").toInt(), 1001);
        QSignalSpy finished(&act, &ActivationController::previewFinished);
        QVERIFY(act.startPartitionPreview());
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QVERIFY(finished.at(0).at(0).toBool());
        QCOMPARE(act.previewResult().value("reportedCount").toInt(), 1001);
        QCOMPARE(act.previewResult().value("groups").toList(), result.value("groups").toList());
    }
    void asynchronousPreviewCanCompleteCancelAndInvalidate()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
        ActivationController::Context ctx; ctx.db = &db; ActivationController act(std::move(ctx));
        QVERIFY(act.start({{"kind", "pota"}, {"reference", "IT-0001"}}).isEmpty());
        AdifRecord r{{"CALL", "K1ABC"}, {"QSO_DATE", "20261231"}, {"TIME_ON", "235900"}, {"BAND", "20m"}, {"MODE", "CW"}};
        act.session().applyTo(r, 0); QCOMPARE(db.insertQso(r, "manual", {}, true, 0, false).status, InsertResult::Status::Inserted);
        QSignalSpy finished(&act, &ActivationController::previewFinished);
        QVERIFY(act.startPartitionPreview()); QVERIFY(act.previewBusy());
        QVERIFY(!act.startPartitionPreview());
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QVERIFY(finished.takeFirst().at(0).toBool());
        QCOMPARE(act.previewResult().value("reportedCount").toInt(), 1);
        QVERIFY(act.startPartitionPreview()); act.cancelPartitionPreview();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QVERIFY(!finished.takeFirst().at(0).toBool());
        QVERIFY(!act.previewResult().value("error").toString().isEmpty());
        QVERIFY(act.startPartitionPreview()); act.invalidateScore();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QVERIFY(!finished.takeFirst().at(0).toBool());
        QVERIFY(!act.previewResult().value("error").toString().isEmpty());
        QVERIFY(act.previewResult().value("groups").toList().isEmpty());
    }
    void partitionExportIsIsolatedAndRejectsStalePreview()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
        ActivationController::Context ctx; ctx.db = &db; ActivationController act(std::move(ctx));
        QVERIFY(act.start({{"kind", "pota"}, {"reference", "IT-0001,IT-0002"},
                           {"programs", QVariantMap{{"wwff", "IFF-0123"}, {"sota", "I/LM-001"}, {"iota", "EU-025"}}}}).isEmpty());
        AdifRecord r{{"CALL", "K1ABC"}, {"QSO_DATE", "20261231"}, {"TIME_ON", "235900"},
                     {"BAND", "20m"}, {"MODE", "CW"}, {"POTA_REF", "US-1234"}, {"SOTA_REF", "W2/WE-003"}, {"IOTA", "NA-001"}, {"OPERATOR", "9H1AA"}};
        act.session().applyTo(r, 0); const auto inserted = db.insertQso(r, "manual", {}, true, 0, false);
        QCOMPARE(inserted.status, InsertResult::Status::Inserted);
        AdifRecord next{{"CALL", "K1DEF"}, {"QSO_DATE", "20270101"}, {"TIME_ON", "000100"},
                        {"BAND", "20m"}, {"MODE", "CW"}, {"OPERATOR", "9H1BB"}};
        act.session().applyTo(next, 0);
        QCOMPARE(db.insertQso(next, "manual", {}, true, 0, false).status, InsertResult::Status::Inserted);
        QSignalSpy previewed(&act, &ActivationController::previewFinished);
        QVERIFY(act.startPartitionPreview()); QTRY_COMPARE_WITH_TIMEOUT(previewed.size(), 1, 5000); QVERIFY(previewed.at(0).at(0).toBool());
        QVariantMap chosen, wwff, sota, iota;
        for (const auto& value : act.previewResult().value("groups").toList()) {
            const auto group = value.toMap();
            if (group.value("day") != "2026-12-31") continue;
            if (group.value("program") == "POTA" && group.value("reference") == "IT-0001") chosen = group;
            if (group.value("program") == "WWFF" && group.value("reference") == "IFF-0123") wwff = group;
            if (group.value("program") == "SOTA") sota = group;
            if (group.value("program") == "IOTA") iota = group;
        }
        QVERIFY(!chosen.isEmpty()); QVERIFY(!wwff.isEmpty()); QVERIFY(!sota.isEmpty()); QVERIFY(!iota.isEmpty());
        QCOMPARE(chosen.value("missingStationCall").toInt(), 1);
        QCOMPARE(chosen.value("missingCallIdentity").toInt(), 0);
        QCOMPARE(wwff.value("missingStationCall").toInt(), 1);
        QCOMPARE(wwff.value("missingOperator").toInt(), 0);
        QVERIFY(!act.startPartitionExport(chosen, QUrl::fromLocalFile(dir.filePath("log.sqlite"))));
        const QString path = dir.filePath("park.adi"); QSignalSpy exported(&act, &ActivationController::partitionExportFinished);
        QVERIFY(act.startPartitionExport(chosen, QUrl::fromLocalFile(path)));
        QTRY_COMPARE_WITH_TIMEOUT(exported.size(), 1, 5000); QVERIFY2(exported.at(0).at(0).toBool(), qPrintable(exported.at(0).at(1).toString()));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto data = file.readAll(); file.close();
        const auto records = adif::parse(data).records; QCOMPARE(records.size(), 1);
        QCOMPARE(records.at(0).value("MY_POTA_REF"), QString("IT-0001"));
        QCOMPARE(records.at(0).value("MY_SIG"), QString("POTA"));
        QCOMPARE(records.at(0).value("MY_SIG_INFO"), QString("IT-0001"));
        QCOMPARE(records.at(0).value("POTA_REF"), QString("US-1234"));
        QCOMPARE(db.record(inserted.id)->value("MY_POTA_REF"), QString("IT-0001,IT-0002"));
        exported.clear(); const QString wwffPath = dir.filePath("wwff.adi");
        QVERIFY(act.startPartitionExport(wwff, QUrl::fromLocalFile(wwffPath)));
        QTRY_COMPARE_WITH_TIMEOUT(exported.size(), 1, 5000); QVERIFY2(exported.at(0).at(0).toBool(), qPrintable(exported.at(0).at(1).toString()));
        QFile wwffFile(wwffPath); QVERIFY(wwffFile.open(QIODevice::ReadOnly)); const auto wwffRecords = adif::parse(wwffFile.readAll()).records;
        QCOMPARE(wwffRecords.size(), 1); QCOMPARE(wwffRecords.at(0).value("MY_SIG"), QString("WWFF"));
        QCOMPARE(wwffRecords.at(0).value("MY_SIG_INFO"), QString("IFF-0123"));
        QCOMPARE(wwffRecords.at(0).value("POTA_REF"), QString("US-1234"));
        for (const auto& group : {sota, iota}) {
            exported.clear(); const QString out = dir.filePath(group.value("program").toString() + ".adi");
            QVERIFY(act.startPartitionExport(group, QUrl::fromLocalFile(out)));
            QTRY_COMPARE_WITH_TIMEOUT(exported.size(), 1, 5000); QVERIFY2(exported.at(0).at(0).toBool(), qPrintable(exported.at(0).at(1).toString()));
            QFile output(out); QVERIFY(output.open(QIODevice::ReadOnly)); const auto selected = adif::parse(output.readAll()).records;
            QCOMPARE(selected.size(), 1); QCOMPARE(selected.at(0).value("MY_SOTA_REF"), QString("I/LM-001"));
            QCOMPARE(selected.at(0).value("SOTA_REF"), QString("W2/WE-003"));
            QCOMPARE(selected.at(0).value("MY_IOTA"), QString("EU-025")); QCOMPARE(selected.at(0).value("IOTA"), QString("NA-001"));
            QCOMPARE(selected.at(0).value("MY_SIG"), QString("POTA"));
        }
        r.set("APP_DECOLOG_ACTIVATION_REVIEW", "PERIOD_UNCERTAIN"); QCOMPARE(db.updateQso(inserted.id, r).status, InsertResult::Status::Inserted);
        exported.clear(); QVERIFY(act.startPartitionExport(chosen, QUrl::fromLocalFile(path)));
        QTRY_COMPARE_WITH_TIMEOUT(exported.size(), 1, 5000); QVERIFY(!exported.at(0).at(0).toBool());
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), data); file.close();
        r.remove("APP_DECOLOG_ACTIVATION_REVIEW"); r.set("COMMENT", "changed after preview"); QCOMPARE(db.updateQso(inserted.id, r).status, InsertResult::Status::Inserted);
        exported.clear(); QVERIFY(act.startPartitionExport(chosen, QUrl::fromLocalFile(path)));
        QTRY_COMPARE_WITH_TIMEOUT(exported.size(), 1, 5000); QVERIFY(!exported.at(0).at(0).toBool());
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), data); file.close();
        const QString sessionId = act.state().value("sessionUuid").toString();
        act.stop(); QVERIFY(!act.active());
        const auto runs = act.closedRuns(0); QCOMPARE(runs.size(), 1);
        QCOMPARE(runs.at(0).toMap().value("uuid").toString(), sessionId);
        previewed.clear(); QVERIFY(act.startRunPartitionPreview(sessionId));
        QTRY_COMPARE_WITH_TIMEOUT(previewed.size(), 1, 5000); QVERIFY(previewed.at(0).at(0).toBool());
        QVariantMap closedGroup;
        for (const auto& item : act.previewResult().value("groups").toList()) {
            const auto g = item.toMap();
            if (g.value("program") == "POTA" && g.value("reference") == "IT-0001" && g.value("day") == "2026-12-31") closedGroup = g;
        }
        QVERIFY(!closedGroup.isEmpty()); exported.clear();
        QVERIFY(act.startPartitionExport(closedGroup, QUrl::fromLocalFile(path)));
        QTRY_COMPARE_WITH_TIMEOUT(exported.size(), 1, 5000); QVERIFY(exported.at(0).at(0).toBool());
        LogDatabase reopened; QVERIFY(reopened.open(dir.filePath("log.sqlite")));
        ActivationController::Context reopenContext; reopenContext.db = &reopened;
        ActivationController restored(std::move(reopenContext)); restored.load(); QVERIFY(!restored.active());
        QCOMPARE(restored.closedRuns(0).at(0).toMap().value("uuid").toString(), sessionId);
        QSignalSpy reopenedPreview(&restored, &ActivationController::previewFinished);
        QVERIFY(restored.startRunPartitionPreview(sessionId));
        QTRY_COMPARE_WITH_TIMEOUT(reopenedPreview.size(), 1, 5000); QVERIFY(reopenedPreview.at(0).at(0).toBool());
        QCOMPARE(restored.previewResult().value("reportedCount").toInt(), 2);
    }
    void closedSessionListingIsPaged()
    {
        LogDatabase db; QVERIFY(db.open(":memory:")); QSqlQuery q(db.connection());
        QVERIFY(q.exec("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE x<102) INSERT INTO activation_run(uuid,started_at,ended_at,initial_json,last_json) SELECT printf('run-%d',x),'2026-01-01','2026-01-02','{}','{\"title\":\"Test\"}' FROM n"));
        ActivationController::Context ctx; ctx.db = &db; ActivationController act(std::move(ctx));
        const auto first = act.closedRuns(0); QCOMPARE(first.size(), 101);
        QCOMPARE(first.at(0).toMap().value("uuid").toString(), QString("run-102"));
        const auto second = act.closedRuns(first.at(99).toMap().value("rowid").toLongLong());
        QCOMPARE(second.size(), 2); QCOMPARE(second.at(1).toMap().value("uuid").toString(), QString("run-1"));
    }
    void simultaneousProgramReferencesStayIndependent()
    {
        const QVariantMap programs{{"sota", "I/LM-001"}, {"wwff", "IFF-0123"}};
        Activation a = Activation::fromMap({{"active", true}, {"kind", "pota"},
                                             {"reference", "IT-0001,IT-0002"}, {"programs", programs}});
        AdifRecord r{{"CALL", "K1ABC"}, {"POTA_REF", "US-1234"}, {"SOTA_REF", "W2/WE-003"}};
        a.applyTo(r, 0);
        QCOMPARE(r.value("MY_POTA_REF"), QString("IT-0001,IT-0002"));
        QCOMPARE(r.value("MY_SOTA_REF"), QString("I/LM-001"));
        QCOMPARE(r.value("MY_WWFF_REF"), QString("IFF-0123"));
        QCOMPARE(r.value("POTA_REF"), QString("US-1234"));
        QCOMPARE(r.value("SOTA_REF"), QString("W2/WE-003"));
        QCOMPARE(r.value("MY_SIG"), QString("POTA"));
        QCOMPARE(Activation::fromMap(a.toMap()).toMap().value("programs").toMap(), programs);
        AdifRecord already{{"CALL", "K1ABC"}, {"MY_SOTA_REF", "G/LD-003"}, {"WWFF_REF", "KFF-4655"}};
        a.applyTo(already, 0);
        QCOMPARE(already.value("MY_SOTA_REF"), QString("G/LD-003"));
        QCOMPARE(already.value("WWFF_REF"), QString("KFF-4655"));
    }
    void additionalProgramSyntaxIsChecked()
    {
        QVariantMap normalized;
        QString error;
        QVERIFY(Activation::normalizePrograms({{"pota", " it-0001, it-0002@it-25 "}, {"sota", " i/lm-001 "},
                                               {"wwff", " iff-0123 "}, {"iota", " eu-025 "}}, &normalized, &error));
        QCOMPARE(normalized.value("pota").toString(), QString("IT-0001,IT-0002@IT-25"));
        QCOMPARE(normalized.value("sota").toString(), QString("I/LM-001"));
        QCOMPARE(normalized.value("wwff").toString(), QString("IFF-0123"));
        QCOMPARE(normalized.value("iota").toString(), QString("EU-025"));
        QVERIFY(!Activation::normalizePrograms({{"pota", "IT-0001,it-0001"}}, &normalized, &error));
        QVERIFY(!Activation::normalizePrograms({{"pota", "IT-0001,"}}, &normalized, &error));
        QVERIFY(!Activation::normalizePrograms({{"sota", "I/LM-001,I/LM-002"}}, &normalized, &error));
        QVERIFY(!Activation::normalizePrograms({{"wwff", "IFF-123"}}, &normalized, &error));
        QVERIFY(!Activation::normalizePrograms({{"iota", "EU-000"}}, &normalized, &error));
        QVERIFY(!Activation::normalizePrograms({{"unknown", "X-0001"}}, &normalized, &error));
        QVERIFY(!Activation::normalizePrograms({{"sota", 123}}, &normalized, &error));
    }
    void multiProgramSessionPersistsAndOnlyAffectsFutureQsos()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString path = dir.filePath("log.sqlite");
        qint64 firstId = 0, secondId = 0;
        {
            LogDatabase db; QVERIFY(db.open(path));
            ActivationController::Context ctx; ctx.db = &db;
            ActivationController act(std::move(ctx)); act.load();
            QVERIFY(!act.start({{"kind", "pota"}, {"reference", "IT-0001,IT-0002"},
                                {"programs", QVariantMap{{"sota", "I/LM-001,I/LM-002"}}}}).isEmpty());
            QVERIFY(!act.active());
            QVERIFY(!act.start({{"kind", "pota"}, {"reference", "IT-0001"},
                                {"programs", QVariantMap{{"pota", "IT-0002"}}}}).isEmpty());
            QVERIFY(act.start({{"kind", "pota"}, {"reference", "IT-0001,IT-0002"},
                               {"programs", QVariantMap{{"sota", "i/lm-001"}, {"wwff", "iff-0123"}}}}).isEmpty());
            AdifRecord first{{"CALL", "K1ABC"}, {"QSO_DATE", "20261123"}, {"TIME_ON", "100000"}, {"BAND", "20m"}, {"MODE", "CW"}, {"SOTA_REF", "G/LD-003"}};
            act.session().applyTo(first, 0);
            const auto one = db.insertQso(first, "manual", {}, true, 0, false); QCOMPARE(one.status, InsertResult::Status::Inserted); firstId = one.id;
            QVariantMap changed = act.state();
            changed["programs"] = QVariantMap{{"sota", "I/LM-002"}, {"wwff", "IFF-0123"}};
            QVERIFY(act.update(changed).isEmpty());
            AdifRecord second{{"CALL", "K1DEF"}, {"QSO_DATE", "20261123"}, {"TIME_ON", "101000"}, {"BAND", "20m"}, {"MODE", "CW"}};
            act.session().applyTo(second, 0);
            const auto two = db.insertQso(second, "manual", {}, true, 0, false); QCOMPARE(two.status, InsertResult::Status::Inserted); secondId = two.id;
            changed["programs"] = QVariantMap{{"sota", "invalid"}};
            QVERIFY(!act.update(changed).isEmpty());
            QCOMPARE(act.state().value("programs").toMap().value("sota").toString(), QString("I/LM-002"));
            QVariantMap legacyUpdate = act.state(); legacyUpdate.remove("programs");
            QVERIFY(act.update(legacyUpdate).isEmpty());
            QCOMPARE(act.state().value("programs").toMap().value("sota").toString(), QString("I/LM-002"));
            QCOMPARE(db.record(firstId)->value("APP_DECOLOG_ACTIVATION_UUID"), act.state().value("sessionUuid").toString());
            QCOMPARE(db.record(secondId)->value("APP_DECOLOG_ACTIVATION_UUID"), act.state().value("sessionUuid").toString());
            QCOMPARE(db.record(firstId)->value("MY_SOTA_REF"), QString("I/LM-001"));
            QCOMPARE(db.record(firstId)->value("SOTA_REF"), QString("G/LD-003"));
            QCOMPARE(db.record(secondId)->value("MY_SOTA_REF"), QString("I/LM-002"));
        }
        {
            LogDatabase db; QVERIFY(db.open(path));
            ActivationController::Context ctx; ctx.db = &db;
            ActivationController act(std::move(ctx)); act.load(); QVERIFY(act.active());
            QCOMPARE(act.state().value("programs").toMap().value("sota").toString(), QString("I/LM-002"));
            QCOMPARE(db.record(firstId)->value("MY_SOTA_REF"), QString("I/LM-001"));
            QCOMPARE(db.record(secondId)->value("MY_WWFF_REF"), QString("IFF-0123"));
            const QByteArray exported = db.exportAdif();
            LogDatabase imported; QVERIFY(imported.open(":memory:")); QCOMPARE(imported.importAdif(exported).inserted, 2);
            const auto records = adif::parse(imported.exportAdif()).records; QCOMPARE(records.size(), 2);
            QCOMPARE(records.at(0).value("APP_DECOLOG_ACTIVATION_UUID"), act.state().value("sessionUuid").toString());
            QCOMPARE(records.at(0).value("MY_POTA_REF"), QString("IT-0001,IT-0002"));
            QCOMPARE(records.at(0).value("MY_SOTA_REF"), QString("I/LM-001"));
            QCOMPARE(records.at(0).value("MY_WWFF_REF"), QString("IFF-0123"));
            QCOMPARE(records.at(0).value("SOTA_REF"), QString("G/LD-003"));
        }
    }
    void potaFieldsAndFileName()
    {
        Activation a;
        a.active = true;
        a.kind = Activation::Kind::Pota;
        a.reference = "IT-1234";
        a.myGrid = "JN71DC";
        a.tag = "pota";
        QCOMPARE(a.requiredQsos(), 10);

        AdifRecord r{{"CALL", "K1ABC"}, {"BAND", "20m"}, {"MODE", "FT8"}};
        a.applyTo(r, 0);
        // I campi dell'attivatore, non quelli del cacciatore.
        QCOMPARE(r.value("MY_SIG"), QString("POTA"));
        QCOMPARE(r.value("MY_SIG_INFO"), QString("IT-1234"));
        QCOMPARE(r.value("MY_POTA_REF"), QString("IT-1234"));
        QVERIFY(r.value("POTA_REF").isEmpty());
        QCOMPARE(r.value("MY_GRIDSQUARE"), QString("JN71DC"));
        QCOMPARE(r.value("APP_DECOLOG_TAGS"), QString("pota"));
        QCOMPARE(a.exportFileName("IU8LMC/P", QDate(2026, 9, 17)), QString("IU8LMC_P@IT-1234-20260917.adi"));

        // Un campo che il QSO ha gia' non si tocca.
        AdifRecord mine{{"CALL", "K1ABC"}, {"MY_GRIDSQUARE", "JN70AA"}, {"APP_DECOLOG_TAGS", "pota,test"}};
        a.applyTo(mine, 0);
        QCOMPARE(mine.value("MY_GRIDSQUARE"), QString("JN70AA"));
        QCOMPARE(mine.value("APP_DECOLOG_TAGS"), QString("pota,test"));
    }

    void contestSerial()
    {
        Activation a;
        a.active = true;
        a.kind = Activation::Kind::Contest;
        a.contestId = "CQ-WW-SSB";
        a.serialEnabled = true;
        AdifRecord r{{"CALL", "K1ABC"}};
        a.applyTo(r, 7);
        QCOMPARE(r.value("CONTEST_ID"), QString("CQ-WW-SSB"));
        QCOMPARE(r.value("STX"), QString("7"));
        QCOMPARE(r.value("APP_DECOLOG_TAGS"), QString("cq-ww-ssb"));
        QCOMPARE(a.exportFileName("IU8LMC", QDate(2026, 9, 17)), QString("IU8LMC-CQ-WW-SSB-20260917.adi"));

        const Activation back = Activation::fromMap(a.toMap());
        QCOMPARE(back.contestId, a.contestId);
        QVERIFY(back.serialEnabled);
        // Le regole dell'edizione, la rete e il call history restano nella sessione.
        a.sharedSerial = true;
        a.rules = {{"submitHours", 72}};
        a.callHistory = "C:/x/cqww.txt";
        const Activation again = Activation::fromMap(a.toMap());
        QVERIFY(again.sharedSerial);
        QCOMPARE(again.rules.value("submitHours").toInt(), 72);
        QCOMPARE(again.callHistory, QString("C:/x/cqww.txt"));
    }

    void multipleActivationReferencesArePreserved()
    {
        Activation a;
        a.active = true;
        a.kind = Activation::Kind::Pota;
        a.reference = QStringLiteral("IT-0001,IT-0002");
        a.myGrid = QStringLiteral("JN70");

        AdifRecord r{{"CALL", "K1ABC"}};
        a.applyTo(r, 0);
        QCOMPARE(r.value("MY_SIG_INFO"), QStringLiteral("IT-0001,IT-0002"));
        QCOMPARE(r.value("MY_POTA_REF"), QStringLiteral("IT-0001,IT-0002"));

        const Activation restored = Activation::fromMap(a.toMap());
        QCOMPARE(restored.reference, a.reference);
    }

    void multipleActivationReferencesSurviveAdifExport()
    {
        LogDatabase db;
        QVERIFY(db.open(QStringLiteral(":memory:")));

        Activation a;
        a.active = true;
        a.kind = Activation::Kind::Pota;
        a.reference = QStringLiteral("IT-0001,IT-0002");
        a.myGrid = QStringLiteral("JN70");

        AdifRecord first{{"CALL", "K1ABC"}, {"QSO_DATE", "20261015"}, {"TIME_ON", "101500"},
                         {"BAND", "20m"}, {"MODE", "SSB"}};
        AdifRecord second{{"CALL", "W1AW"}, {"QSO_DATE", "20261015"}, {"TIME_ON", "101600"},
                          {"BAND", "40m"}, {"MODE", "CW"}};
        a.applyTo(first, 0);
        a.applyTo(second, 0);
        const auto one = db.insertQso(first, QStringLiteral("manual"), {}, true);
        const auto two = db.insertQso(second, QStringLiteral("manual"), {}, true);
        QCOMPARE(one.status, InsertResult::Status::Inserted);
        QCOMPARE(two.status, InsertResult::Status::Inserted);

        const AdifDocument exported = adif::parse(db.exportAdif(QList<qint64>{one.id, two.id}));
        QCOMPARE(exported.records.size(), 2);
        for (const AdifRecord& record : exported.records) {
            QCOMPARE(record.value(QStringLiteral("MY_SIG_INFO")), QStringLiteral("IT-0001,IT-0002"));
            QCOMPARE(record.value(QStringLiteral("MY_POTA_REF")), QStringLiteral("IT-0001,IT-0002"));
            QCOMPARE(record.value(QStringLiteral("MY_GRIDSQUARE")), QStringLiteral("JN70"));
        }
    }

    void callHistoryAndSharedSerials()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        QTemporaryDir dir;
        const QString file = dir.filePath("cqww.txt");
        QFile f(file);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("!!Order!!,Call,Name,CQZone\nK9ZZZ,BOB,4\n");
        f.close();

        ActivationController::Context ctx;
        ctx.db = &db;
        ctx.stationCall = [] { return QStringLiteral("IU8LMC"); };
        ctx.stationGrid = [] { return QStringLiteral("JN70"); };
        ctx.activeProfileId = [] { return qint64(0); };
        ctx.locate = [](const QString&) { return decolog::core::ContestStation{291, QStringLiteral("NA"), 5, 8}; };
        // La rete che distribuisce i numeri: da' il 100.
        int asked = 0;
        ctx.takeSerial = [&asked](int) { ++asked; return 100; };
        bool shared = false;
        ctx.shareSerials = [&shared](bool on) { shared = on; };
        ActivationController act(std::move(ctx));
        act.load();

        QVERIFY(act.start({{"kind", "contest"}, {"contestId", "CQ-WW-CW"}, {"serialEnabled", true},
                           {"sharedSerial", true}}).isEmpty());
        QVERIFY(shared);
        // Senza call history: la zona dal paese.
        QCOMPARE(act.suggestExchange("K9ZZZ").value("from").toString(), QString("cty"));
        QVERIFY(act.loadCallHistory(QUrl::fromLocalFile(file)).isEmpty());
        QCOMPARE(act.state().value("callHistoryCount").toInt(), 1);
        // Con: la zona che ha mandato l'anno scorso.
        const QVariantMap s = act.suggestExchange("K9ZZZ");
        QCOMPARE(s.value("value").toString(), QString("4"));
        QCOMPARE(s.value("from").toString(), QString("history"));

        // Il numero lo da' la rete, e il contatore riparte da li'.
        AdifRecord r{{"CALL", "K9ZZZ"}, {"QSO_DATE", "20261024"}, {"TIME_ON", "1200"}, {"BAND", "20m"}, {"MODE", "CW"}};
        act.applyTo(r);
        QCOMPARE(r.value("STX"), QString("100"));
        QCOMPARE(asked, 1);
        act.qsoLogged();
        QCOMPARE(act.nextSerial(), 101);

        // Le regole di questa edizione.
        QCOMPARE(act.editionRules().value("bands").toStringList().size(), 6);
        QVariantMap state = act.state();
        state["rules"] = QVariantMap{{"bands", QStringList{"40m", "20m"}}};
        act.update(state);
        QCOMPARE(act.editionRules().value("bands").toStringList(), QStringList({"40m", "20m"}));
        QVERIFY(act.editionRules().value("changed").toBool());
        // Il call history resta dopo un cambio della sessione.
        QCOMPARE(act.state().value("callHistoryCount").toInt(), 1);
        act.stop();
        QVERIFY(!shared);
    }

    void theScoreReadsTheExchangeAndFollowsTheLog()
    {
        // Il punteggio adesso si calcola una volta e si tiene in memoria: la
        // prova guarda che lo scambio ricevuto arrivi al conto (sta fra i campi
        // ADIF in piu', non in una colonna) e che un QSO corretto fuori dalla
        // sessione faccia rifare il conto invece di lasciarlo vecchio.
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        ActivationController::Context ctx;
        ctx.db = &db;
        ctx.stationCall = [] { return QStringLiteral("IU8LMC"); };
        ctx.stationGrid = [] { return QStringLiteral("JN70"); };
        ctx.activeProfileId = [] { return qint64(0); };
        ctx.station = [] { return decolog::core::ContestStation{248, QStringLiteral("EU"), 15, 28}; };
        ActivationController act(std::move(ctx));
        act.load();
        QVERIFY(act.start({{"kind", "contest"}, {"contestId", "IARU-HF"}}).isEmpty());

        auto log = [&db, &act](const char* call, int dxcc, const char* cont, int ituz, const char* srx) {
            AdifRecord r{{"CALL", call}, {"QSO_DATE", QDateTime::currentDateTimeUtc().toString("yyyyMMdd")},
                         {"TIME_ON", QDateTime::currentDateTimeUtc().toString("hhmmss")},
                         {"BAND", "20m"}, {"MODE", "CW"}, {"DXCC", QString::number(dxcc)},
                         {"CONT", cont}, {"ITUZ", QString::number(ituz)},
                         {"SRX", srx}, {"SRX_STRING", srx}};
            act.applyTo(r);
            const auto res = db.insertQso(r, "manual", {}, true);
            if (res.status == InsertResult::Status::Inserted)
                act.qsoLogged();
            return res;
        };

        // Una stazione HQ: si riconosce solo dallo scambio, "DARC" al posto
        // della zona. Se lo scambio non arrivasse al conto, varrebbe 3 punti
        // (stesso continente, altra zona) invece di 1, e porterebbe una zona
        // invece del moltiplicatore HQ.
        const auto hq = log("DA0HQ", 230, "EU", 28, "DARC");
        QCOMPARE(hq.status, InsertResult::Status::Inserted);
        QVariantMap score = act.score();
        QCOMPARE(score.value("points").toInt(), 1);
        QCOMPARE(score.value("multipliers").toInt(), 1);

        // Un americano: 5 punti e una zona nuova.
        QCOMPARE(log("W1AW", 291, "NA", 8, "8").status, InsertResult::Status::Inserted);
        score = act.score();
        QCOMPARE(score.value("points").toInt(), 6);
        QCOMPARE(score.value("multipliers").toInt(), 2);
        QCOMPARE(score.value("score").toInt(), 12);

        // Il QSO della HQ viene cancellato da un'altra parte del programma (la
        // tabella del log): la sessione non ne sa niente finche' non le si
        // dice che il log e' cambiato. Da li' il conto dev'essere quello nuovo.
        QSqlQuery q(db.connection());
        QVERIFY(q.exec(QStringLiteral("UPDATE qso SET deleted = 1 WHERE id = %1").arg(hq.id)));
        act.invalidateScore();
        score = act.score();
        QCOMPARE(score.value("points").toInt(), 5);
        QCOMPARE(score.value("multipliers").toInt(), 1);
    }

    void theExchangeIsSuggestedWhileTypingTheCall()
    {
        // Si scrive il nominativo e lo scambio si riempie da solo: la zona dal
        // paese, o quello che la stazione ha mandato l'ultima volta. Il
        // progressivo non si puo' sapere, e non si inventa.
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        // Un QSO di un anno fa con I2XYZ, nel 40/80: mando' MI.
        QVERIFY(db.insertQso({{"CALL", "I2XYZ"}, {"QSO_DATE", "20250105"}, {"TIME_ON", "0800"},
                              {"BAND", "40m"}, {"MODE", "SSB"}, {"DXCC", "248"},
                              {"CONTEST_ID", "ARI-40-80"}, {"SRX_STRING", "MI"}}, "import").status
                == InsertResult::Status::Inserted);
        // Uno con I8ABC, fuori dai contest, con la provincia nel campo STATE.
        QVERIFY(db.insertQso({{"CALL", "I8ABC"}, {"QSO_DATE", "20250301"}, {"TIME_ON", "0900"},
                              {"BAND", "20m"}, {"MODE", "FT8"}, {"DXCC", "248"}, {"STATE", "NA"}},
                             "import").status == InsertResult::Status::Inserted);
        // Una HQ nella IARU dell'anno scorso: mando' la sigla della societa'.
        QVERIFY(db.insertQso({{"CALL", "DA0HQ"}, {"QSO_DATE", "20250712"}, {"TIME_ON", "1300"},
                              {"BAND", "20m"}, {"MODE", "CW"}, {"DXCC", "230"}, {"ITUZ", "28"},
                              {"CONTEST_ID", "IARU-HF"}, {"SRX_STRING", "DARC"}}, "import").status
                == InsertResult::Status::Inserted);

        // W3RT nel CQ WW RTTY: zona e stato. W6XX fuori dai contest, con lo STATE.
        QVERIFY(db.insertQso({{"CALL", "W3RT"}, {"QSO_DATE", "20250927"}, {"TIME_ON", "1000"},
                              {"BAND", "20m"}, {"MODE", "RTTY"}, {"DXCC", "291"},
                              {"CONTEST_ID", "CQ-WW-RTTY"}, {"SRX_STRING", "05 PA"}}, "import").status
                == InsertResult::Status::Inserted);
        QVERIFY(db.insertQso({{"CALL", "W6XX"}, {"QSO_DATE", "20250401"}, {"TIME_ON", "1000"},
                              {"BAND", "20m"}, {"MODE", "FT8"}, {"DXCC", "291"}, {"STATE", "CA"}},
                             "import").status == InsertResult::Status::Inserted);

        ActivationController::Context ctx;
        ctx.db = &db;
        ctx.stationCall = [] { return QStringLiteral("IU8LMC"); };
        ctx.stationGrid = [] { return QStringLiteral("JN70"); };
        ctx.activeProfileId = [] { return qint64(0); };
        ctx.station = [] { return decolog::core::ContestStation{248, QStringLiteral("EU"), 15, 28}; };
        ctx.locate = [](const QString& call) {
            if (call.startsWith(QStringLiteral("W")))
                return decolog::core::ContestStation{291, QStringLiteral("NA"), 5, 8};
            if (call.startsWith(QStringLiteral("I")))
                return decolog::core::ContestStation{248, QStringLiteral("EU"), 15, 28};
            if (call.startsWith(QStringLiteral("D")))
                return decolog::core::ContestStation{230, QStringLiteral("EU"), 14, 28};
            return decolog::core::ContestStation{};
        };
        ActivationController act(std::move(ctx));
        act.load();

        // CQ WW RTTY: la zona e, per gli americani, lo stato: dalla gara
        // dell'anno scorso o dallo STATE del log. Un europeo solo la zona.
        QVERIFY(act.start({{"kind", "contest"}, {"contestId", "CQ-WW-RTTY"}}).isEmpty());
        QCOMPARE(act.suggestExchange(QStringLiteral("W3RT")).value("value").toString(), QString("5 PA"));
        QCOMPARE(act.suggestExchange(QStringLiteral("W6XX")).value("value").toString(), QString("5 CA"));
        QCOMPARE(act.suggestExchange(QStringLiteral("DL9ZZT")).value("value").toString(), QString("14"));
        act.stop();
        // E lo "05 PA" del RTTY vale zona 5 anche per il CQ WW in CW.
        QVERIFY(act.start({{"kind", "contest"}, {"contestId", "CQ-WW-CW"}}).isEmpty());
        QCOMPARE(act.suggestExchange(QStringLiteral("W3RT")).value("value").toString(), QString("5"));
        act.stop();

        // CQ WW: la zona CQ dal paese.
        QVERIFY(act.start({{"kind", "contest"}, {"contestId", "CQ-WW-CW"}}).isEmpty());
        QVariantMap s = act.suggestExchange(QStringLiteral("W1AW"));
        QCOMPARE(s.value("value").toString(), QString("5"));
        QCOMPARE(s.value("from").toString(), QString("cty"));
        act.stop();

        // IARU: la HQ ha mandato DARC, e DARC si suggerisce; un americano la
        // sua zona ITU.
        QVERIFY(act.start({{"kind", "contest"}, {"contestId", "IARU-HF"}}).isEmpty());
        s = act.suggestExchange(QStringLiteral("DA0HQ"));
        QCOMPARE(s.value("value").toString(), QString("DARC"));
        QCOMPARE(s.value("from").toString(), QString("log"));
        QCOMPARE(act.suggestExchange(QStringLiteral("W1AW")).value("value").toString(), QString("8"));
        act.stop();

        // 40/80: la provincia di un QSO di gara, o quella del campo STATE.
        QVERIFY(act.start({{"kind", "contest"}, {"contestId", "ARI-40-80"}}).isEmpty());
        s = act.suggestExchange(QStringLiteral("I2XYZ"));
        QCOMPARE(s.value("value").toString(), QString("MI"));
        QCOMPARE(s.value("from").toString(), QString("log"));
        QCOMPARE(act.suggestExchange(QStringLiteral("I8ABC")).value("value").toString(), QString("NA"));
        // Un italiano mai lavorato: non si sa, e non si indovina.
        QVERIFY(act.suggestExchange(QStringLiteral("I1NEW")).value("value").toString().isEmpty());
        act.stop();

        // WPX: il progressivo non si puo' sapere prima.
        QVERIFY(act.start({{"kind", "contest"}, {"contestId", "CQ-WPX-CW"}}).isEmpty());
        QVERIFY(act.suggestExchange(QStringLiteral("W1AW")).value("value").toString().isEmpty());
        act.stop();
    }

    void theContestKnowsWhatEachSpotIsWorth()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        ActivationController::Context ctx;
        ctx.db = &db;
        ctx.stationCall = [] { return QStringLiteral("IU8LMC"); };
        ctx.stationGrid = [] { return QStringLiteral("JN70"); };
        ctx.activeProfileId = [] { return qint64(0); };
        // Io sono in Italia: Europa, zona CQ 15.
        ctx.station = [] { return decolog::core::ContestStation{248, QStringLiteral("EU"), 15, 28}; };
        // Un cty.csv in miniatura: quel tanto che basta alla prova.
        ctx.locate = [](const QString& call) {
            if (call.startsWith(QStringLiteral("W")))
                return decolog::core::ContestStation{291, QStringLiteral("NA"), 5, 8};
            if (call.startsWith(QStringLiteral("JA")))
                return decolog::core::ContestStation{339, QStringLiteral("AS"), 25, 45};
            if (call.startsWith(QStringLiteral("DL")))
                return decolog::core::ContestStation{230, QStringLiteral("EU"), 14, 28};
            return decolog::core::ContestStation{};
        };
        ActivationController act(std::move(ctx));
        act.load();
        QVERIFY(act.start({{"kind", "contest"}, {"contestId", "CQ-WW-CW"},
                           {"serialEnabled", true}}).isEmpty());

        // Prima di lavorare qualcuno, ogni spot porta due moltiplicatori nuovi.
        const QVariantMap before = act.spotValue(QStringLiteral("W1AW"), QStringLiteral("20m"),
                                                 QStringLiteral("CW"));
        QCOMPARE(before.value("points").toInt(), 3);
        QCOMPARE(before.value("newMultiplier").toBool(), true);
        QCOMPARE(before.value("duplicate").toBool(), false);

        auto log = [&db, &act](const char* call, const char* band, const char* mode,
                               int dxcc, const char* cont, int cqz) {
            AdifRecord r{{"CALL", call}, {"QSO_DATE", QDateTime::currentDateTimeUtc().toString("yyyyMMdd")},
                         {"TIME_ON", QDateTime::currentDateTimeUtc().toString("hhmmss")},
                         {"BAND", band}, {"MODE", mode}, {"DXCC", QString::number(dxcc)},
                         {"CONT", cont}, {"CQZ", QString::number(cqz)}};
            act.applyTo(r);
            const auto res = db.insertQso(r, "manual", {}, true);
            if (res.status == InsertResult::Status::Inserted)
                act.qsoLogged();
            return res;
        };
        QCOMPARE(log("W1AW", "20m", "CW", 291, "NA", 5).status, InsertResult::Status::Inserted);

        // Adesso il punteggio c'e': 3 punti, zona 5 e paese 291 sui 20 metri.
        const QVariantMap score = act.score();
        QCOMPARE(score.value("valid").toBool(), true);
        QCOMPARE(score.value("points").toInt(), 3);
        QCOMPARE(score.value("multipliers").toInt(), 2);
        QCOMPARE(score.value("score").toInt(), 6);

        // Un altro americano sulla stessa banda non porta piu' niente: stessa
        // zona, stesso paese. E' quello che il cluster deve smettere di segnare.
        const QVariantMap again = act.spotValue(QStringLiteral("W2XX"), QStringLiteral("20m"),
                                                QStringLiteral("CW"));
        QCOMPARE(again.value("points").toInt(), 3);
        QCOMPARE(again.value("newMultiplier").toBool(), false);
        // Ma sui 40 metri sono moltiplicatori nuovi: nel CQ WW si contano per
        // banda, ed e' per questo che si cambia banda.
        const QVariantMap lower = act.spotValue(QStringLiteral("W2XX"), QStringLiteral("40m"),
                                                QStringLiteral("CW"));
        QCOMPARE(lower.value("newMultiplier").toBool(), true);
        // Lo stesso nominativo gia' lavorato su quella banda e' un duplicato.
        const QVariantMap dupe = act.spotValue(QStringLiteral("W1AW"), QStringLiteral("20m"),
                                               QStringLiteral("CW"));
        QCOMPARE(dupe.value("duplicate").toBool(), true);

        // Il giapponese porta zona e paese nuovi, e vale 3 punti.
        const QVariantMap japan = act.spotValue(QStringLiteral("JA1ABC"), QStringLiteral("20m"),
                                                QStringLiteral("CW"));
        QCOMPARE(japan.value("points").toInt(), 3);
        QCOMPARE(japan.value("newMultiplier").toBool(), true);

        // Il tedesco e' nel mio continente: 1 punto.
        const QVariantMap german = act.spotValue(QStringLiteral("DL9ZZT"), QStringLiteral("20m"),
                                                 QStringLiteral("CW"));
        QCOMPARE(german.value("points").toInt(), 1);

        // E il contest e' in CW: la finestra della telegrafia serve.
        QVERIFY(act.isCwContest());
    }

    void aContestWithoutRulesHasNoScoreAndSaysIt()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        ActivationController::Context ctx;
        ctx.db = &db;
        ctx.stationCall = [] { return QStringLiteral("IU8LMC"); };
        ctx.stationGrid = [] { return QStringLiteral("JN70"); };
        ctx.activeProfileId = [] { return qint64(0); };
        ActivationController act(std::move(ctx));
        act.load();
        QVERIFY(act.start({{"kind", "contest"}, {"contestId", "AL-QSO-PARTY"}}).isEmpty());

        const QVariantMap score = act.score();
        QCOMPARE(score.value("valid").toBool(), false);
        QCOMPARE(score.value("score").toInt(), 0);
        // E nessuno spot risulta moltiplicatore: meglio niente che un numero
        // inventato.
        QCOMPARE(act.spotValue(QStringLiteral("W1AW"), QStringLiteral("20m"),
                               QStringLiteral("SSB")).value("newMultiplier").toBool(), false);
        // Un contest in SSB non apre la finestra della telegrafia.
        QVERIFY(!act.isCwContest());
    }

    void activationReferencesRejectEmptyAndDuplicateItems()
    {
        LogDatabase db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        ActivationController::Context ctx;
        ctx.db = &db;
        ctx.stationGrid = [] { return QStringLiteral("JN70"); };
        ctx.activeProfileId = [] { return qint64(0); };
        ActivationController act(std::move(ctx));
        act.load();

        QVERIFY(act.start({{"kind", "pota"}, {"reference", "IT-0001,,IT-0002"}})
                    .contains(QStringLiteral("empty"), Qt::CaseInsensitive));
        QVERIFY(act.start({{"kind", "pota"}, {"reference", "IT-0001, it-0001"}})
                    .contains(QStringLiteral("duplicate"), Qt::CaseInsensitive));
        QVERIFY(act.start({{"kind", "pota"}, {"reference", " it-0001, IT-0002 "}}).isEmpty());
        QCOMPARE(act.state().value(QStringLiteral("reference")).toString(),
                 QStringLiteral("IT-0001,IT-0002"));
        act.update({{"kind", "pota"}, {"reference", "IT-0001,,IT-0002"}});
        QCOMPARE(act.state().value(QStringLiteral("reference")).toString(),
                 QStringLiteral("IT-0001,IT-0002"));
    }

    void sessionOnTheLog()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        QStringList notes;
        ActivationController::Context ctx;
        ctx.db = &db;
        ctx.stationCall = [] { return QStringLiteral("IU8LMC/P"); };
        ctx.stationGrid = [] { return QStringLiteral("JN71DC"); };
        ctx.activeProfileId = [] { return qint64(0); };
        ctx.activity = [&notes](const QString&, const QString& text, const QString&) { notes << text; };
        ActivationController act(std::move(ctx));
        act.load();
        QVERIFY(!act.active());

        // Senza referenza non parte.
        QVERIFY(!act.start({{"kind", "pota"}}).isEmpty());
        QVERIFY(act.start({{"kind", "pota"}, {"reference", "it-1234"}, {"serialEnabled", true}}).isEmpty());
        QVERIFY(act.active());
        QCOMPARE(act.requiredQsos(), 10);
        QCOMPARE(act.nextSerial(), 1);
        QCOMPARE(act.suggestedFileName().left(18), QString("IU8LMC_P@IT-1234-2"));

        auto log = [&db, &act](const char* call, const char* band, const char* mode) {
            AdifRecord r{{"CALL", call}, {"QSO_DATE", QDateTime::currentDateTimeUtc().toString("yyyyMMdd")},
                         {"TIME_ON", QDateTime::currentDateTimeUtc().toString("hhmmss")},
                         {"BAND", band}, {"MODE", mode}};
            act.applyTo(r);
            const auto res = db.insertQso(r, "manual", {}, true);
            if (res.status == InsertResult::Status::Inserted)
                act.qsoLogged();
            return res;
        };

        QCOMPARE(log("K1ABC", "20m", "FT8").status, InsertResult::Status::Inserted);
        QCOMPARE(act.qsoCount(), 1);
        QCOMPARE(act.nextSerial(), 2);
        QCOMPARE(log("JA1XX", "20m", "FT8").status, InsertResult::Status::Inserted);
        QCOMPARE(act.qsoCount(), 2);
        QCOMPARE(act.uniqueCalls(), 2);

        // Dentro l'attivazione K1ABC su 20m FT8 e' un duplicato, su 40m no.
        QVERIFY(act.isDuplicate("k1abc", "20M", "FT8"));
        QVERIFY(!act.isDuplicate("K1ABC", "40m", "FT8"));
        QVERIFY(!act.isDuplicate("K1ABC", "20m", "CW"));

        // Il primo QSO ha la referenza e il numero.
        const auto first = db.record(db.qsosToUpload("lotw").first());
        QVERIFY(first);
        QCOMPARE(first->value("MY_SIG_INFO"), QString("IT-1234"));
        QCOMPARE(first->value("STX"), QString("1"));
        QCOMPARE(first->value("APP_DECOLOG_TAGS"), QString("pota"));

        QCOMPARE(act.qsoIds().size(), 2);
        QCOMPARE(act.perBand().size(), 1);

        // La sessione sopravvive alla chiusura del programma.
        ActivationController again(ActivationController::Context{&db, ctx.stationCall, ctx.stationGrid,
                                                                 ctx.activeProfileId, {}, {}});
        again.load();
        QVERIFY(again.active());
        QCOMPARE(again.session().reference, QString("IT-1234"));
        QCOMPARE(again.qsoCount(), 2);
        QCOMPARE(again.nextSerial(), 3);

        act.stop();
        QVERIFY(!act.active());
        QVERIFY(!act.isDuplicate("K1ABC", "20m", "FT8"));
        QVERIFY(notes.last().contains("2"));
    }
};

QTEST_GUILESS_MAIN(TestActivation)
#include "tst_activation.moc"
