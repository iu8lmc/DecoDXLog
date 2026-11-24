#include "core/DxCampaign.h"
#include "core/LogDatabase.h"
#include "core/Spots.h"
#include <QTimeZone>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QTest>
using namespace decolog::core;
class TestDxCampaign : public QObject {
    Q_OBJECT
private slots:
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
