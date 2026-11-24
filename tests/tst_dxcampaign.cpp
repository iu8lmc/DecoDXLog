#include "core/DxCampaign.h"
#include "core/LogDatabase.h"
#include "core/Spots.h"
#include <QTimeZone>
#include <QSqlQuery>
#include <QSqlError>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QTest>
using namespace decolog::core;
class TestDxCampaign : public QObject {
    Q_OBJECT
private slots:
    void campaignHistoryPagesAndRejectsCorruptRows()
    {
        LogDatabase db; QVERIFY(db.open(":memory:")); dxcampaign::Campaign c; c.title="Pages"; c.calls={"3Y0J"};
        c.start=QDate(2026,1,1); c.end=c.start; c.source="fixture"; c.verifiedAt=QDateTime::fromString("2025-12-31T12:00:00Z",Qt::ISODate);
        c.bands={"20m"}; c.modes={"CW"}; QString error; QVERIFY(dxcampaign::save(db,c,&error));
        for (int i=0;i<55;++i) { c.title=QString("Page %1").arg(i); QVERIFY2(dxcampaign::save(db,c,&error),qPrintable(error)); }
        const auto first=dxcampaign::history(db,c.id,c.revision,51,&error); QVERIFY2(error.isEmpty(),qPrintable(error)); QCOMPARE(first.size(),51); QCOMPARE(first.first().revision,55); QCOMPARE(first.last().revision,5);
        const auto second=dxcampaign::history(db,c.id,first.at(49).revision,51,&error); QVERIFY2(error.isEmpty(),qPrintable(error)); QCOMPARE(second.size(),5); QCOMPARE(second.first().revision,5); QCOMPARE(second.last().revision,1);
        QSqlQuery q(db.connection()); q.prepare("SELECT value FROM app_setting WHERE key=?"); q.addBindValue("dx_campaign_revision."+c.id+".1"); QVERIFY(q.exec()); QVERIFY(q.next());
        auto legacy=QJsonDocument::fromJson(q.value(0).toByteArray()).object(); legacy.remove("supersededAt");
        q.prepare("UPDATE app_setting SET value=? WHERE key=?"); q.addBindValue(QString::fromUtf8(QJsonDocument(legacy).toJson(QJsonDocument::Compact))); q.addBindValue("dx_campaign_revision."+c.id+".1"); QVERIFY(q.exec());
        const auto oldPage=dxcampaign::history(db,c.id,2,1,&error); QVERIFY2(error.isEmpty(),qPrintable(error)); QCOMPARE(oldPage.size(),1); QVERIFY(!oldPage.first().supersededAt.isValid());
        q.prepare("UPDATE app_setting SET value='broken' WHERE key=?"); q.addBindValue("dx_campaign_revision."+c.id+".1"); QVERIFY(q.exec());
        QVERIFY(dxcampaign::history(db,c.id,5,51,&error).isEmpty()); QVERIFY(!error.isEmpty());
    }
    void campaignHistoryRejectsStaleAndRollsBackFailedUpdate()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path=dir.filePath("concurrent.sqlite");
        LogDatabase first,second; QVERIFY(first.open(path)); QVERIFY(second.open(path)); dxcampaign::Campaign c;
        c.title="Concurrent"; c.calls={"3Y0J"}; c.start=QDate(2026,1,1); c.end=QDate(2026,1,3); c.source="fixture";
        c.verifiedAt=QDateTime::fromString("2025-12-31T12:00:00Z",Qt::ISODate); c.bands={"20m"}; c.modes={"CW"}; QString error;
        QVERIFY2(dxcampaign::save(first,c,&error),qPrintable(error)); auto stale=*dxcampaign::load(second,c.id);
        c.end=QDate(2026,1,8); QVERIFY2(dxcampaign::save(first,c,&error),qPrintable(error));
        stale.calls << "3Y0K"; QVERIFY(!dxcampaign::save(second,stale,&error)); QCOMPARE(stale.revision,1);
        auto page=dxcampaign::history(second,c.id,3,10,&error); QVERIFY2(error.isEmpty(),qPrintable(error)); QCOMPARE(page.size(),1); QCOMPARE(page.first().end,QDate(2026,1,3));
        QSqlQuery q(first.connection()); QVERIFY(q.exec("CREATE TRIGGER reject_campaign_update BEFORE UPDATE ON app_setting WHEN NEW.key LIKE 'dx_campaign.%' BEGIN SELECT RAISE(ABORT,'update fault'); END"));
        c.end=QDate(2026,1,10); QVERIFY(!dxcampaign::save(first,c,&error)); QCOMPARE(c.revision,2);
        page=dxcampaign::history(second,c.id,3,10,&error); QVERIFY2(error.isEmpty(),qPrintable(error)); QCOMPARE(page.size(),1);
        const auto saved=dxcampaign::load(second,c.id); QVERIFY(saved); QCOMPARE(saved->end,QDate(2026,1,8)); QCOMPARE(saved->revision,2);
        QVERIFY(q.exec("DROP TRIGGER reject_campaign_update")); QVERIFY2(dxcampaign::save(first,c,&error),qPrintable(error));
        page=dxcampaign::history(second,c.id,4,10,&error); QCOMPARE(page.size(),2); QCOMPARE(page.first().end,QDate(2026,1,8));
    }
    void campaignRevisionsSurviveUpdatesAndRestart()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path=dir.filePath("history.sqlite"); QString id;
        { LogDatabase db; QVERIFY(db.open(path)); dxcampaign::Campaign c;
          c.title="History"; c.calls={"3Y0J"}; c.start=QDate(2026,1,1); c.end=QDate(2026,1,3); c.source="fixture";
          c.verifiedAt=QDateTime::fromString("2025-12-31T12:00:00Z",Qt::ISODate); c.bands={"20m"}; c.modes={"CW"};
          QString error; QVERIFY2(dxcampaign::save(db,c,&error),qPrintable(error)); id=c.id;
          c.calls << "3Y0K"; c.end=QDate(2026,1,8); QVERIFY2(dxcampaign::save(db,c,&error),qPrintable(error));
          c.archived=true; QVERIFY2(dxcampaign::save(db,c,&error),qPrintable(error));
          auto page=dxcampaign::history(db,id,c.revision,1,&error); QVERIFY2(error.isEmpty(),qPrintable(error)); QCOMPARE(page.size(),1); QCOMPARE(page.first().revision,2); QCOMPARE(page.first().calls.size(),2);
          page=dxcampaign::history(db,id,page.first().revision,1,&error); QVERIFY2(error.isEmpty(),qPrintable(error)); QCOMPARE(page.size(),1); QCOMPARE(page.first().revision,1); QCOMPARE(page.first().calls.size(),1);
          QSqlQuery q(db.connection()); q.prepare("SELECT value FROM app_setting WHERE key=?"); q.addBindValue("dx_campaign_revision."+id+".1"); QVERIFY(q.exec()); QVERIFY(q.next());
          const auto first=QJsonDocument::fromJson(q.value(0).toByteArray()).object(); QCOMPARE(first.value("end").toString(),QStringLiteral("2026-01-03")); QCOMPARE(first.value("revision").toInt(),1);
          const auto superseded=QDateTime::fromString(first.value("supersededAt").toString(),Qt::ISODateWithMs); QVERIFY(superseded.isValid()); QVERIFY(qAbs(superseded.secsTo(QDateTime::currentDateTimeUtc()))<60);
          QVERIFY(q.exec("CREATE TRIGGER reject_campaign_history BEFORE INSERT ON app_setting WHEN NEW.key LIKE 'dx_campaign_revision.%' BEGIN SELECT RAISE(ABORT,'history fault'); END"));
          c.end=QDate(2026,1,10); QVERIFY(!dxcampaign::save(db,c,&error)); QCOMPARE(c.revision,3);
          const auto current=dxcampaign::load(db,id); QVERIFY(current); QCOMPARE(current->end,QDate(2026,1,8)); }
        LogDatabase db; QVERIFY(db.open(path)); QSqlQuery q(db.connection()); q.prepare("SELECT COUNT(*) FROM app_setting WHERE key LIKE ?"); q.addBindValue("dx_campaign_revision."+id+".%"); QVERIFY(q.exec()); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),2);
    }
    void campaignReviewContextRejectsReopenAndReplacement()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path=dir.filePath("review.sqlite"), copy=dir.filePath("copy.sqlite");
        LogDatabase db; QVERIFY(db.open(path)); auto first=dxcampaign::captureReviewContext(db); QVERIFY(first); QVERIFY(dxcampaign::reviewContextCurrent(db,*first));
        db.close(); QVERIFY(QFile::copy(path,copy)); QVERIFY(db.open(path)); QCOMPARE(db.path(),first->path); QVERIFY(!dxcampaign::reviewContextCurrent(db,*first));
        auto reopened=dxcampaign::captureReviewContext(db); QVERIFY(reopened); QVERIFY(dxcampaign::reviewContextCurrent(db,*reopened));
        QVERIFY(QFile::rename(path,dir.filePath("original.sqlite"))); QVERIFY(QFile::copy(copy,path));
        QCOMPARE(db.path(),reopened->path); QVERIFY(!dxcampaign::reviewContextCurrent(db,*reopened)); QVERIFY(!dxcampaign::captureReviewContext(db));
    }
    void damagedCampaignDoesNotHideHealthyCampaign()
    {
        LogDatabase db; QVERIFY(db.open(":memory:")); dxcampaign::Campaign c;
        c.title="Healthy"; c.calls={"3Y0J"}; c.start=QDate(2026,1,1); c.end=c.start; c.source="fixture";
        c.verifiedAt=QDateTime::fromString("2025-12-31T12:00:00Z",Qt::ISODate); c.bands={"20m"}; c.modes={"CW"}; c.watched=true;
        QString error; QVERIFY(dxcampaign::save(db,c,&error));
        QSqlQuery q(db.connection()); QVERIFY(q.exec("INSERT INTO app_setting(key,value) VALUES('dx_campaign.broken','not json')"));
        int invalid=0; const auto campaigns=dxcampaign::list(db,&error,&invalid);
        QVERIFY2(error.isEmpty(),qPrintable(error)); QCOMPARE(invalid,1); QCOMPARE(campaigns.size(),1);
        QCOMPARE(campaigns.first().id,c.id); QVERIFY(campaigns.first().watched);
        QVERIFY(dxcampaign::list(db,&error).isEmpty()); QVERIFY(!error.isEmpty());
        QVERIFY(q.exec("DELETE FROM app_setting WHERE key='dx_campaign.broken'"));
        QCOMPARE(dxcampaign::list(db,&error,&invalid).size(),1); QCOMPARE(invalid,0); QVERIFY(error.isEmpty());
    }
    void malformedWatchFlagsAreNotTreatedAsFalse()
    {
        LogDatabase db; QVERIFY(db.open(":memory:")); dxcampaign::Campaign c;
        c.title="Watch"; c.calls={"3Y0J"}; c.start=QDate(2026,1,1); c.end=QDate(2026,1,2);
        c.source="fixture"; c.verifiedAt=QDateTime::fromString("2025-12-31T12:00:00Z",Qt::ISODate); c.bands={"20m"}; c.modes={"CW"}; c.archived=true;
        QString error; QVERIFY(dxcampaign::save(db,c,&error));
        QSqlQuery q(db.connection()); q.prepare("SELECT value FROM app_setting WHERE key=?"); q.addBindValue("dx_campaign."+c.id); QVERIFY(q.exec()); QVERIFY(q.next());
        const auto original=QJsonDocument::fromJson(q.value(0).toString().toUtf8()).object();
        for (const auto* key : {"archived","watched","watchGoalsOnly"}) {
            auto json=original; json[key]=QStringLiteral("true"); QSqlQuery update(db.connection());
            update.prepare("UPDATE app_setting SET value=? WHERE key=?"); update.addBindValue(QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact))); update.addBindValue("dx_campaign."+c.id); QVERIFY(update.exec());
            QVERIFY(!dxcampaign::load(db,c.id,&error)); QVERIFY(!error.isEmpty());
            QVERIFY(dxcampaign::list(db,&error).isEmpty()); QVERIFY(!error.isEmpty());
            QVERIFY(!dxcampaign::save(db,c,&error));
        }
        QSqlQuery restore(db.connection()); restore.prepare("UPDATE app_setting SET value=? WHERE key=?"); restore.addBindValue(QString::fromUtf8(QJsonDocument(original).toJson(QJsonDocument::Compact))); restore.addBindValue("dx_campaign."+c.id); QVERIFY(restore.exec());
        const auto restored=dxcampaign::load(db,c.id,&error); QVERIFY(restored); QVERIFY(restored->archived);
    }
    void campaignCountsLegacyUnzonedUtcQso()
    {
        LogDatabase db; QVERIFY(db.open(":memory:")); dxcampaign::Campaign c;
        c.title="Legacy UTC"; c.calls={"3Y0J"}; c.start=QDate(2026,1,1); c.end=c.start;
        c.source="fixture"; c.verifiedAt=QDateTime::fromString("2025-12-31T12:00:00Z",Qt::ISODate);
        c.bands={"20m"}; c.modes={"CW"}; c.goals={{"20m","CW","worked"}};
        const auto id=db.insertQso(AdifRecord{{"CALL","3Y0J"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}},"fixture").id; QVERIFY(id>0);
        QSqlQuery q(db.connection()); QVERIFY(q.exec("UPDATE qso SET qso_datetime_on='2026-01-01T12:00:00'"));
        QString error; const auto cells=dxcampaign::matrix(db,c,&error); QVERIFY2(error.isEmpty(),qPrintable(error)); QCOMPARE(cells.size(),1);
        QCOMPARE(cells.first().worked,1); QVERIFY(!cells.first().needed);
        EnrichedSpot spot; spot.spot.band="20m"; spot.spot.mode="CW";
        const auto needed=dxcampaign::goalNeeded(db,c,spot,&error); QVERIFY2(needed,qPrintable(error)); QVERIFY(!*needed);
        QVERIFY(q.exec("UPDATE qso SET qso_datetime_on='2025-12-31T23:59:00'"));
        QCOMPARE(dxcampaign::matrix(db,c,&error).first().worked,0);
        const auto outside=dxcampaign::goalNeeded(db,c,spot,&error); QVERIFY(outside); QVERIFY(*outside);
        QVERIFY(q.exec("UPDATE qso SET qso_datetime_on='bad timestamp'"));
        QCOMPARE(dxcampaign::matrix(db,c,&error).first().worked,0);
        const auto invalid=dxcampaign::goalNeeded(db,c,spot,&error); QVERIFY(invalid); QVERIFY(*invalid);
    }
    void externalWriteInvalidatesPendingGoalAlertSnapshot()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("watch.sqlite"));
        LogDatabase observer; QVERIFY(observer.open(path));
        LogDatabase writer; QVERIFY(writer.open(path));
        const quint64 generation = observer.openGeneration();
        const qint64 version = observer.dataVersion(); QVERIFY(version >= 0);
        QVERIFY(dxcampaign::watchSnapshotCurrent(observer, generation, version));
        const auto saved = writer.insertQso({{"CALL", "3Y0J"}, {"QSO_DATE", "20260101"}, {"TIME_ON", "1200"},
                                             {"BAND", "20m"}, {"MODE", "CW"}}, "test");
        QCOMPARE(saved.status, InsertResult::Status::Inserted);
        // The previous path/generation-only guard accepts this stale result.
        QCOMPARE(observer.openGeneration(), generation);
        QVERIFY(!dxcampaign::watchSnapshotCurrent(observer, generation, version));
        const qint64 refreshed = observer.dataVersion(); QVERIFY(refreshed >= 0);
        QVERIFY(dxcampaign::watchSnapshotCurrent(observer, generation, refreshed));
        observer.close();
        QVERIFY(!dxcampaign::watchSnapshotCurrent(observer, generation, refreshed));
    }
    void benchmarkCampaignUtcWindow()
    {
        if (!qEnvironmentVariableIsSet("DECOLOG_BENCH_CAMPAIGN_UTC")) QSKIP("Opt-in synthetic benchmark");
        QTemporaryDir dir; QVERIFY(dir.isValid()); LogDatabase db; QVERIFY(db.open(dir.filePath("large.sqlite")));
        dxcampaign::Campaign c; c.title="Benchmark"; c.calls={"K1ABC"}; c.start=QDate(2027,1,1); c.end=c.start;
        c.source="fixture"; c.verifiedAt=QDateTime::fromString("2026-12-31T12:00:00Z",Qt::ISODate); c.bands={"20m"}; c.modes={"CW"}; c.goals={{"20m","CW","confirmed"}};
        QSqlQuery q(db.connection()); QVERIFY2(q.exec("WITH RECURSIVE n(i) AS (SELECT 0 UNION ALL SELECT i+1 FROM n WHERE i<99999) "
            "INSERT INTO qso(uuid,call,qso_datetime_on,band,mode,created_at,updated_at) SELECT printf('bench-%d',i),'K1ABC', "
            "CASE i%3 WHEN 0 THEN '2026-12-31T12:00:00Z' WHEN 1 THEN '2027-01-01T12:00:00Z' ELSE '2027-01-02T12:00:00Z' END, "
            "'20m','CW','2027-01-01T12:00:00Z','2027-01-01T12:00:00Z' FROM n"),qPrintable(q.lastError().text()));
        EnrichedSpot spot; spot.spot.band="20m"; spot.spot.mode="CW"; QString error; QElapsedTimer timer; timer.start();
        const auto cells=dxcampaign::matrix(db,c,&error); const qint64 matrixMs=timer.elapsed(); QVERIFY2(error.isEmpty(),qPrintable(error)); QCOMPARE(cells.first().worked,33333);
        timer.restart(); const auto needed=dxcampaign::goalNeeded(db,c,spot,&error); const qint64 goalMs=timer.elapsed(); QVERIFY2(needed,qPrintable(error)); QVERIFY(*needed);
        qInfo("CAMPAIGN_UTC_BENCH qso=100000 matrix_ms=%lld goal_ms=%lld",static_cast<long long>(matrixMs),static_cast<long long>(goalMs));
    }

    void campaignMatchesUtcDayAcrossOffsetBoundaries()
    {
        LogDatabase db; QVERIFY(db.open(":memory:")); dxcampaign::Campaign c;
        c.title="UTC boundary"; c.calls={"K1ABC"}; c.start=QDate(2027,1,1); c.end=c.start; c.source="fixture";
        c.verifiedAt=QDateTime::fromString("2026-12-31T12:00:00Z",Qt::ISODate); c.bands={"20m"}; c.modes={"CW"}; c.goals={{"20m","CW","worked"}};
        QString error; QVERIFY2(dxcampaign::save(db,c,&error),qPrintable(error));
        auto add=[&](const QString& iso) { auto r=db.insertQso(AdifRecord{{"CALL","K1ABC"},{"QSO_DATE","20270101"},{"TIME_ON","120000"},{"BAND","20m"},{"MODE","CW"}},"fixture",{},true,0,false); if(r.id>0) { QSqlQuery q(db.connection()); q.prepare("UPDATE qso SET qso_datetime_on=? WHERE id=?"); q.addBindValue(iso); q.addBindValue(r.id); if(!q.exec()) return qint64(0); } return r.id; };
        const auto confirmedId=add("2026-12-31T23:30:00-02:00"); QVERIFY(confirmedId>0); QVERIFY(add("2027-01-02T00:30:00+02:00")>0);
        QVERIFY(add("2027-01-01T00:30:00+02:00")>0); QVERIFY(add("2027-01-01T23:30:00-02:00")>0);
        EnrichedSpot spot; spot.spot.band="20m"; spot.spot.mode="CW";
        const auto needed=dxcampaign::goalNeeded(db,c,spot,&error); QVERIFY2(needed,qPrintable(error)); QVERIFY(!*needed);
        const auto cells=dxcampaign::matrix(db,c,&error); QVERIFY2(error.isEmpty(),qPrintable(error)); QCOMPARE(cells.size(),1);
        QCOMPARE(cells.first().worked,2); QVERIFY(!cells.first().needed);
        QslState card; card.service="card"; card.rcvd="Y"; QVERIFY(db.setCardState(confirmedId,card));
        c.goals={{"20m","CW","confirmed"}}; const auto confirmation=dxcampaign::goalNeeded(db,c,spot,&error);
        QVERIFY2(confirmation,qPrintable(error)); QVERIFY(!*confirmation);
        const auto confirmedCells=dxcampaign::matrix(db,c,&error); QCOMPARE(confirmedCells.first().confirmed,1); QVERIFY(!confirmedCells.first().needed);
    }

    void explicitGoalsTrackWorkedAndConfirmedSeparately()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const QString path=dir.filePath("goals.sqlite"); QString id;
        {
            LogDatabase db; QVERIFY(db.open(path)); dxcampaign::Campaign c;
            c.title="Goals"; c.calls={"ZZ1AAA","ZZ1BBB"}; c.start=QDate(2026,1,1); c.end=QDate(2026,1,7);
            c.source="fixture"; c.verifiedAt=QDateTime::fromString("2025-12-31T10:00:00Z",Qt::ISODate);
            c.bands={"20m","40m"}; c.modes={"CW","FT8"}; c.goals={{"20m","CW","worked"},{"40m","FT8","confirmed"}};
            QString error; QVERIFY2(dxcampaign::save(db,c,&error),qPrintable(error)); id=c.id;
            auto cells=dxcampaign::matrix(db,c,&error); QCOMPARE(cells[0].target,QString("worked")); QVERIFY(cells[0].needed);
            QVERIFY(cells[1].target.isEmpty()); QCOMPARE(cells[3].target,QString("confirmed")); QVERIFY(cells[3].needed);
            const auto a=db.insertQso(AdifRecord{{"CALL","ZZ1AAA"},{"QSO_DATE","20260102"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}},"import");
            const auto b=db.insertQso(AdifRecord{{"CALL","ZZ1BBB"},{"QSO_DATE","20260103"},{"TIME_ON","1200"},{"BAND","40m"},{"MODE","FT8"}},"import"); QVERIFY(a.id>0 && b.id>0);
            cells=dxcampaign::matrix(db,c,&error); QVERIFY(!cells[0].needed); QVERIFY(cells[3].needed);
            QslState card; card.service="card"; card.rcvd="Y"; QVERIFY(db.setCardState(b.id,card));
            cells=dxcampaign::matrix(db,c,&error); QVERIFY(!cells[3].needed);
            auto bad=c; bad.goals << dxcampaign::Goal{"20m","CW","confirmed"}; QVERIFY(!dxcampaign::save(db,bad,&error));
        }
        LogDatabase db; QVERIFY(db.open(path)); const auto c=dxcampaign::load(db,id); QVERIFY(c); QCOMPARE(c->goals.size(),2);
        QVERIFY(!dxcampaign::matrix(db,*c).first().needed);
    }
    void watchRequiresOptInActivePeriodAndProfile()
    {
        LogDatabase db; QVERIFY(db.open(":memory:")); const auto now=QDateTime::currentDateTimeUtc();
        dxcampaign::Campaign c; c.title="Watch fixture"; c.calls={"3Y0J"}; c.start=now.date().addDays(-1); c.end=now.date().addDays(1);
        c.source="fixture"; c.verifiedAt=now; c.bands={"20m"}; c.modes={"CW"}; c.stationProfileId=4;
        EnrichedSpot e; e.spot.dxCall="3Y0J"; e.spot.band="20m"; e.spot.mode="CW"; e.spot.time=now.addSecs(-60);
        QVERIFY(!dxcampaign::watchMatches(c,e,now,4)); c.watched=true;
        QVERIFY(dxcampaign::watchMatches(c,e,now,4)); QVERIFY(!dxcampaign::watchMatches(c,e,now,5));
        c.archived=true; QVERIFY(!dxcampaign::watchMatches(c,e,now,4)); c.archived=false;
        c.end=now.date().addDays(-1); e.spot.time=now.addDays(-1); QVERIFY(!dxcampaign::watchMatches(c,e,now,4));
        c.end=now.date().addDays(1); e.spot.time=now.addSecs(-60);
        QString error; QVERIFY2(dxcampaign::save(db,c,&error),qPrintable(error)); const auto saved=dxcampaign::load(db,c.id); QVERIFY(saved); QVERIFY(saved->watched);
        QSqlQuery q(db.connection()); q.prepare("SELECT value FROM app_setting WHERE key=?"); q.addBindValue("dx_campaign."+c.id); QVERIFY(q.exec()); QVERIFY(q.next());
        auto json=QJsonDocument::fromJson(q.value(0).toString().toUtf8()).object(); json.remove("watched"); json.remove("goals");
        QSqlQuery update(db.connection()); update.prepare("UPDATE app_setting SET value=? WHERE key=?"); update.addBindValue(QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact))); update.addBindValue("dx_campaign."+c.id); QVERIFY(update.exec());
        const auto old=dxcampaign::load(db,c.id); QVERIFY(old); QVERIFY(!old->watched); QVERIFY(old->goals.isEmpty());
    }
    void spotsRequireExactCallCampaignPeriodAndFreshness()
    {
        dxcampaign::Campaign c; c.title="Trip"; c.calls={"ZZ1AAA","ZZ1BBB"}; c.start=QDate(2026,1,2); c.end=QDate(2026,1,3);
        c.source="fixture"; c.verifiedAt=QDateTime::fromString("2026-01-01T00:00:00Z",Qt::ISODate); c.bands={"20m"}; c.modes={"CW","SSB"};
        const QDateTime now(QDate(2026,1,2),QTime(0,10),QTimeZone::UTC);
        auto make=[&](const QString& call,const QDateTime& time,const QString& band,const QString& mode) { EnrichedSpot e; e.spot.dxCall=call; e.spot.time=time; e.spot.band=band; e.spot.mode=mode; return e; };
        const auto fresh=make("ZZ1AAA",now.addSecs(-30),"20m","CW");
        const auto second=make("zz1bbb",now.addSecs(-60),"20m","USB");
        const auto stale=make("ZZ1AAA",now.addSecs(-31*60),"20m","CW");
        const auto before=make("ZZ1AAA",now.addSecs(-11*60),"20m","CW");
        const auto future=make("ZZ1AAA",now.addSecs(1),"20m","CW");
        const auto other=make("ZZ1AAA/P",now,"20m","CW");
        const auto wrongBand=make("ZZ1AAA",now,"40m","CW"), unknown=make("ZZ1AAA",now,"20m","");
        const auto fm=make("ZZ1AAA",now,"20m","FM"), ambiguous=make("ZZ1AAA",now,"20m","DIGI");
        const auto matches=dxcampaign::freshSpots(c,{fresh,second,stale,before,future,other,wrongBand,unknown,fm,ambiguous},now,30);
        QCOMPARE(matches.size(),2); QCOMPARE(matches[0].spot.dxCall,QString("ZZ1AAA")); QCOMPARE(matches[1].spot.dxCall,QString("zz1bbb"));
        c.start=QDate(2026,1,1); const auto atLimit=make("ZZ1AAA",now.addSecs(-1800),"20m","CW");
        const auto pastLimit=make("ZZ1AAA",now.addMSecs(-1800001),"20m","CW");
        QCOMPARE(dxcampaign::freshSpots(c,{atLimit,pastLimit},now,30).size(),1);
        c.archived=true; QVERIFY(dxcampaign::freshSpots(c,{fresh},now,30).isEmpty());
    }
    void spotModesResolveToTheSameMatrixCell()
    {
        dxcampaign::Campaign c; c.modes={"SSB","USB","CW"};
        QCOMPARE(dxcampaign::matrixMode(c,"usb"),QString("USB"));
        QCOMPARE(dxcampaign::matrixMode(c,"lsb"),QString("SSB"));
        QCOMPARE(dxcampaign::matrixMode(c,"FM"),QString());
        c.modes={"USB","LSB"}; QCOMPARE(dxcampaign::matrixMode(c,"LSB"),QString("LSB"));
    }
    void goalOnlyWatchUsesOpenCellAndKeepsLegacyDefault()
    {
        LogDatabase db; QVERIFY(db.open(":memory:")); const auto now=QDateTime::currentDateTimeUtc();
        dxcampaign::Campaign c; c.title="Watch goals"; c.calls={"3Y0J"}; c.start=now.date().addDays(-1); c.end=now.date().addDays(1);
        c.source="fixture"; c.verifiedAt=now; c.bands={"20m","40m"}; c.modes={"SSB","USB"}; c.watched=true;
        c.goals={{"20m","SSB","confirmed"},{"20m","USB","worked"}};
        EnrichedSpot spot; spot.spot.dxCall="3Y0J"; spot.spot.time=now; spot.spot.band="20m"; spot.spot.mode="USB";
        QString error; QVERIFY(dxcampaign::save(db,c,&error)); auto saved=dxcampaign::load(db,c.id); QVERIFY(saved); QVERIFY(!saved->watchGoalsOnly);
        QVERIFY(dxcampaign::watchGoalNeeded(c,dxcampaign::matrix(db,c),spot));
        QVERIFY(db.insertQso(AdifRecord{{"CALL","3Y0J"},{"QSO_DATE",now.date().toString("yyyyMMdd")},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","USB"}},"import").id>0);
        QVERIFY(!dxcampaign::watchGoalNeeded(c,dxcampaign::matrix(db,c),spot));
        spot.spot.mode="LSB"; QVERIFY(dxcampaign::watchGoalNeeded(c,dxcampaign::matrix(db,c),spot));
        spot.spot.band="40m"; QVERIFY(!dxcampaign::watchGoalNeeded(c,dxcampaign::matrix(db,c),spot));
        c.watchGoalsOnly=true; QVERIFY(dxcampaign::save(db,c,&error)); saved=dxcampaign::load(db,c.id); QVERIFY(saved); QVERIFY(saved->watchGoalsOnly);
    }
    void focusedGoalCheckAgreesWithMatrixOnLargeLog()
    {
        LogDatabase db; QVERIFY(db.open(":memory:")); dxcampaign::Campaign c;
        c.title="Large fixture"; c.calls={"3Y0J"}; c.start=QDate(2026,1,1); c.end=QDate(2026,12,31);
        c.source="fixture"; c.verifiedAt=QDateTime::fromString("2026-01-01T00:00:00Z",Qt::ISODate);
        c.bands={"20m","40m"}; c.modes={"SSB","USB","CW"}; c.goals={{"20m","USB","worked"},{"40m","CW","confirmed"},{"20m","SSB","confirmed"}};
        QVERIFY(db.connection().transaction());
        for (int i=0;i<4000;++i) {
            const auto at=QDateTime(QDate(2026,1,2),QTime(0,0),QTimeZone::UTC).addSecs(i*180);
            QVERIFY(db.insertQso(AdifRecord{{"CALL","3Y0J"},{"QSO_DATE",at.date().toString("yyyyMMdd")},{"TIME_ON",at.time().toString("hhmm")},{"BAND",i%2?"40m":"20m"},{"MODE",i%2?"CW":"USB"}},"import").id>0);
        }
        QVERIFY(db.connection().commit());
        QCOMPARE(db.qsoCount(),qint64(4000));
        EnrichedSpot spot; spot.spot.band="20m"; spot.spot.mode="USB"; QString error;
        for (const auto& item : QList<QPair<QString,QString>>{{"20m","USB"},{"20m","LSB"},{"40m","CW"},{"40m","USB"}}) {
            spot.spot.band=item.first; spot.spot.mode=item.second;
            const auto focused=dxcampaign::goalNeeded(db,c,spot,&error); QVERIFY2(focused,qPrintable(error));
            QCOMPARE(*focused,dxcampaign::watchGoalNeeded(c,dxcampaign::matrix(db,c),spot));
        }
        c.stationProfileId=999; spot.spot.band="20m"; spot.spot.mode="USB";
        const auto otherProfile=dxcampaign::goalNeeded(db,c,spot,&error); QVERIFY(otherProfile && *otherProfile);
        QCOMPARE(*otherProfile,dxcampaign::watchGoalNeeded(c,dxcampaign::matrix(db,c),spot)); c.stationProfileId=0;
        spot.spot.band="40m"; spot.spot.mode="CW"; QElapsedTimer absentTimer; absentTimer.start();
        for (int i=0;i<10;++i) QVERIFY(dxcampaign::watchGoalNeeded(c,dxcampaign::matrix(db,c),spot));
        const qint64 absentFull=absentTimer.nsecsElapsed(); absentTimer.restart();
        for (int i=0;i<10;++i) { const auto value=dxcampaign::goalNeeded(db,c,spot,&error); QVERIFY(value && *value); }
        qInfo("Open confirmation scan 4000 QSO, 10 runs: matrix=%lld us focused=%lld us",absentFull/1000,absentTimer.nsecsElapsed()/1000);
        QslState card; card.service="card"; card.rcvd="Y"; QVERIFY(db.setCardState(2,card));
        spot.spot.band="40m"; spot.spot.mode="CW"; const auto confirmed=dxcampaign::goalNeeded(db,c,spot,&error);
        QVERIFY2(confirmed,qPrintable(error)); QCOMPARE(*confirmed,dxcampaign::watchGoalNeeded(c,dxcampaign::matrix(db,c),spot)); QVERIFY(!*confirmed);
        spot.spot.band="20m"; spot.spot.mode="USB"; QElapsedTimer timer; timer.start();
        for (int i=0;i<10;++i) QVERIFY(!dxcampaign::watchGoalNeeded(c,dxcampaign::matrix(db,c),spot));
        const qint64 full=timer.nsecsElapsed(); timer.restart();
        for (int i=0;i<10;++i) { const auto value=dxcampaign::goalNeeded(db,c,spot,&error); QVERIFY(value && !*value); }
        qInfo("Goal scan 4000 QSO, 10 runs: matrix=%lld us focused=%lld us",full/1000,timer.nsecsElapsed()/1000);
    }
    void matrixRespectsProfileAndRejectsInvalidChanges()
    {
        LogDatabase db; QVERIFY(db.open(":memory:")); StationProfile a; a.name="A"; a.stationCallsign="9H1AA"; const qint64 first=db.saveStationProfile(a);
        StationProfile b; b.name="B"; b.stationCallsign="9H1BB"; const qint64 second=db.saveStationProfile(b); QVERIFY(first>0 && second>0);
        auto add=[&](const char* call,qint64 profile) { return db.insertQso(AdifRecord{{"CALL",call},{"QSO_DATE","20260102"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}},"import",{},false,profile).id; };
        QVERIFY(add("ZZ1AAA",first)>0); QVERIFY(add("ZZ1BBB",second)>0);
        dxcampaign::Campaign c; c.title="Trip"; c.calls={"ZZ1AAA","ZZ1BBB"}; c.start=QDate(2026,1,1); c.end=QDate(2026,1,7); c.source="fixture";
        c.verifiedAt=QDateTime::fromString("2025-12-31T12:00:00Z",Qt::ISODate); c.bands={"20m"}; c.modes={"CW"}; c.stationProfileId=first;
        QString error; QVERIFY2(dxcampaign::save(db,c,&error),qPrintable(error)); QCOMPARE(dxcampaign::matrix(db,c,&error).first().worked,1);
        c.stationProfileId=second; QVERIFY(dxcampaign::save(db,c,&error)); QCOMPARE(dxcampaign::matrix(db,c,&error).first().worked,1);
        c.calls << "ZZ1BBB"; QVERIFY(!dxcampaign::save(db,c,&error)); QVERIFY(!error.isEmpty()); QCOMPARE(dxcampaign::list(db,&error).size(),1);
        const auto stored=dxcampaign::load(db,c.id); QVERIFY(stored); QCOMPARE(stored->revision,2); QCOMPARE(stored->calls.size(),2);
    }
    void sidebandMapsToSsbWithoutConflatingFm()
    {
        LogDatabase db; QVERIFY(db.open(":memory:")); dxcampaign::Campaign c; c.title="Trip"; c.calls={"ZZ1AAA","ZZ1BBB"};
        c.start=QDate(2026,1,1); c.end=QDate(2026,1,2); c.source="fixture"; c.verifiedAt=QDateTime::fromString("2025-12-31T12:00:00Z",Qt::ISODate);
        c.bands={"20m"}; c.modes={"SSB"}; QString error; QVERIFY2(dxcampaign::save(db,c,&error),qPrintable(error));
        QVERIFY(db.insertQso(AdifRecord{{"CALL","ZZ1AAA"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","SSB"},{"SUBMODE","USB"}},"import").id>0);
        QVERIFY(db.insertQso(AdifRecord{{"CALL","ZZ1BBB"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","FM"}},"import").id>0);
        QCOMPARE(dxcampaign::matrix(db,c,&error).first().worked,1);
        c.modes << "USB"; QVERIFY(dxcampaign::save(db,c,&error)); const auto matrix=dxcampaign::matrix(db,c,&error);
        QCOMPARE(matrix.size(),2); QCOMPARE(matrix[0].worked,0); QCOMPARE(matrix[1].worked,1);
    }
    void independentPeriodsMultiCallConfirmationAndRestart()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const QString path=dir.filePath("log.sqlite"); QString id;
        {
            LogDatabase db; QVERIFY(db.open(path));
            dxcampaign::Campaign first; first.title="Spring"; first.calls={"ZZ1AAA","ZZ1BBB"}; first.start=QDate(2026,1,1); first.end=QDate(2026,1,7); first.source="fixture"; first.verifiedAt=QDateTime::fromString("2025-12-31T12:00:00Z",Qt::ISODate); first.bands={"20m","40m"}; first.modes={"CW","FT8"};
            QString error; QVERIFY2(dxcampaign::save(db,first,&error),qPrintable(error)); id=first.id; QCOMPARE(first.revision,1);
            auto second=first; second.id.clear(); second.revision=0; second.title="Autumn"; second.start=QDate(2026,10,1); second.end=QDate(2026,10,7); QVERIFY(dxcampaign::save(db,second,&error)); QVERIFY(second.id!=id);
            auto add=[&](const char* call,const char* date,const char* band,const char* mode) { return db.insertQso(AdifRecord{{"CALL",call},{"QSO_DATE",date},{"TIME_ON","1200"},{"BAND",band},{"MODE",mode}},"import").id; };
            QVERIFY(add("ZZ1AAA","20260102","20m","CW")>0); const qint64 b=add("ZZ1BBB","20260103","40m","FT8"); QVERIFY(b>0);
            QVERIFY(db.insertQso(AdifRecord{{"CALL","ZZ1BBB"},{"QSO_DATE","20260101"},{"TIME_ON","000000"},{"BAND","20m"},{"MODE","FT8"}},"import").id>0);
            QVERIFY(db.insertQso(AdifRecord{{"CALL","ZZ1BBB"},{"QSO_DATE","20260108"},{"TIME_ON","000000"},{"BAND","20m"},{"MODE","FT8"}},"import").id>0);
            QVERIFY(add("ZZ1AAA","20261002","20m","FT8")>0); QVERIFY(add("ZZ1AAA","20260108","40m","CW")>0);
            auto matrix=dxcampaign::matrix(db,first,&error); QCOMPARE(matrix.size(),4);
            QCOMPARE(matrix[0].worked,1); QCOMPARE(matrix[0].confirmed,0); QCOMPARE(matrix[1].worked,1); QCOMPARE(matrix[3].worked,1);
            QslState card; card.service="card"; card.rcvd="Y"; QVERIFY(db.setCardState(b,card));
            matrix=dxcampaign::matrix(db,first,&error); QCOMPARE(matrix[3].confirmed,1);
            auto autumn=dxcampaign::matrix(db,second,&error); QCOMPARE(autumn[1].worked,1); QCOMPARE(autumn[0].worked,0);
            auto stale=first; first.end=QDate(2026,1,8); QVERIFY(dxcampaign::save(db,first,&error)); QCOMPARE(first.revision,2);
            QVERIFY(!dxcampaign::save(db,stale,&error)); QCOMPARE(dxcampaign::matrix(db,first,&error)[2].worked,1);
            first.archived=true; QVERIFY(dxcampaign::save(db,first,&error)); QVERIFY(dxcampaign::load(db,id)->archived);
            first.archived=false; QVERIFY(dxcampaign::save(db,first,&error));
        }
        LogDatabase reopened; QVERIFY(reopened.open(path)); const auto restored=dxcampaign::load(reopened,id); QVERIFY(restored); QCOMPARE(restored->calls.size(),2); QCOMPARE(restored->revision,4); QVERIFY(!restored->archived);
    }
};
QTEST_GUILESS_MAIN(TestDxCampaign)
#include "tst_dxcampaign.moc"
