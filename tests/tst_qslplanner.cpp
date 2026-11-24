#include "core/QslPlanner.h"
#include <QTemporaryDir>
#include <QFile>
#include <QLibrary>
#include <QSqlDriver>
#include <cstring>
#include <QSqlQuery>
#include <QTest>
using namespace decolog::core;
class TestQslPlanner : public QObject {
    Q_OBJECT
private slots:
    void unverifiedElectronicBenefitsExcludePendingAndDeduplicate()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        auto add=[&](const char* call) { return db.insertQso({{"CALL",call},{"QSO_DATE","20261001"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"DXCC","291"}},"fixture").id; };
        const auto a=add("K1AAA"), b=add("K1BBB"), pending=add("K1CCC"); QVERIFY(a>0 && b>0 && pending>0);
        QslState state; state.service="lotw"; state.sent="Y"; QVERIFY(db.setQslState(pending,state));
        QslState queued; queued.service="lotw"; queued.sent="Q"; QVERIFY(db.setQslState(a,queued));
        const auto plan=qslplanner::build(db,{}, {"dxcc"}); QVERIFY2(plan.error.isEmpty(),qPrintable(plan.error));
        bool found=false; for (const auto& group:plan.unverifiedElectronic) if (group.service=="lotw") {
            found=true; QCOMPARE(group.qsoIds,QList<qint64>({a,b})); QCOMPARE(group.uniqueBenefits,1);
        } QVERIFY(found);
    }
    void rejectsMissingRouteProvenance()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        QVERIFY(db.insertQso({{"CALL","K1ABC"},{"QSO_DATE","20261001"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"DXCC","291"},{"QSL_VIA","M0ABC"}},"fixture").id>0);
        const auto before=qslplanner::build(db,{}, {"dxcc"}); QVERIFY2(before.error.isEmpty(),qPrintable(before.error)); QCOMPARE(before.candidates.size(),1);
        QSqlQuery q(db.connection()); QVERIFY(q.exec("DROP TABLE qso_enrichment"));
        const auto after=qslplanner::build(db,{}, {"dxcc"}); QVERIFY(!after.error.isEmpty()); QVERIFY(after.candidates.isEmpty());
    }
    void pendingElectronicGroupsDeduplicateBenefits()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        auto add=[&](const char *call) { return db.insertQso({{"CALL",call},{"QSO_DATE","20261001"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"DXCC","291"}},"fixture").id; };
        const auto a=add("K1AAA"), b=add("K1BBB"); QVERIFY(a>0 && b>0);
        QslState st; st.service="lotw"; st.sent="Y"; QVERIFY(db.setQslState(a,st)); QVERIFY(db.setQslState(b,st));
        auto plan=qslplanner::build(db,{}, {"dxcc"}); QVERIFY2(plan.error.isEmpty(),qPrintable(plan.error));
        QCOMPARE(plan.pendingElectronic.size(),1); QCOMPARE(plan.pendingElectronic.first().service,QString("lotw"));
        QCOMPARE(plan.pendingElectronic.first().qsoIds,QList<qint64>({a,b})); QCOMPARE(plan.pendingElectronic.first().uniqueBenefits,1);
        QSqlQuery received(db.connection()); received.prepare("UPDATE qsl_status SET rcvd='Y' WHERE qso_id=? AND service='lotw'");
        received.addBindValue(a); QVERIFY(received.exec()); plan=qslplanner::build(db,{}, {"dxcc"});
        QVERIFY(plan.pendingElectronic.isEmpty());
    }

    void planFailsWhenQslStatusCannotBeRead()
    {
#ifndef Q_OS_MACOS
        QSKIP("Native authorizer fixture requires the verified system SQLite driver on macOS");
#endif
        if (!qEnvironmentVariableIsSet("DECOLOG_TEST_SYSTEM_SQLITE_AUTHORIZER")) QSKIP("System SQLite authorizer test is opt-in");
        LogDatabase db; QVERIFY(db.open(":memory:")); const auto qso=db.insertQso({{"CALL","K1ABC"},{"QSO_DATE","20261001"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"DXCC","291"},{"QSL_VIA","M0ABC"}},"fixture"); QVERIFY(qso.id>0);
        QslState state; state.service="card"; state.sent="Y"; state.via="D"; QVERIFY(db.setCardState(qso.id,state));
        const auto normal=qslplanner::build(db,{}, {"dxcc"}); QVERIFY(normal.error.isEmpty()); QCOMPARE(normal.candidates.size(),1); QCOMPARE(normal.candidates.first().paperChannel,QString("D")); QVERIFY(normal.candidates.first().pendingServices.contains("card"));
        QLibrary sqlite(QStringLiteral("sqlite3")); using Authorizer=int(*)(void*,int,const char*,const char*,const char*,const char*); using SetAuthorizer=int(*)(void*,Authorizer,void*);
        const auto setAuthorizer=reinterpret_cast<SetAuthorizer>(sqlite.resolve("sqlite3_set_authorizer")); QVariant handle=db.connection().driver()->handle();
        if (!setAuthorizer || QByteArray(handle.typeName())!="sqlite3*" || !handle.data()) QSKIP("SQLite native authorizer unavailable");
        void* nativeDb=*static_cast<void**>(handle.data()); QVERIFY(nativeDb);
        const auto deny=+[](void*,int action,const char* table,const char* column,const char*,const char*) { return action==20 && table && column && std::strcmp(table,"qsl_status")==0 && std::strcmp(column,"last_error")==0 ? 1 : 0; };
        QCOMPARE(setAuthorizer(nativeDb,deny,nullptr),0); const auto failed=qslplanner::build(db,{}, {"dxcc"}); QCOMPARE(setAuthorizer(nativeDb,nullptr,nullptr),0);
        QVERIFY(!failed.error.isEmpty()); QVERIFY(failed.candidates.isEmpty()); QVERIFY(failed.paperRoutes.isEmpty());
    }
    void legacyRouteCostWritesAfterLogReplacement()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path=dir.filePath("log.sqlite"), clone=dir.filePath("clone.sqlite"); LogDatabase db; QVERIFY(db.open(path));
        QSqlQuery q(db.connection()); QVERIFY(q.exec("PRAGMA journal_mode=DELETE")); q.finish(); q.prepare("VACUUM INTO ?"); q.addBindValue(clone); QVERIFY(q.exec());
        db.close(); QVERIFY(QFile::rename(path,path+".old")); QVERIFY(QFile::rename(clone,path)); QVERIFY(db.open(path));
        QString error; QVERIFY(qslplanner::saveRouteCost(db,"M0ABC","B",{"12.50","EUR","estimate","",""},&error));
        QVERIFY(qslplanner::routeCost(db,"M0ABC","B"));
    }
    void guardedRouteCostRejectsReplacedLogAndExternalChanges()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path=dir.filePath("log.sqlite"), clone=dir.filePath("clone.sqlite"); LogDatabase db; QVERIFY(db.open(path));
        QSqlQuery q(db.connection()); QVERIFY(q.exec("PRAGMA journal_mode=DELETE")); q.finish(); q.prepare("VACUUM INTO ?"); q.addBindValue(clone); QVERIFY(q.exec());
        const auto old=qslplanner::routeCostOrigin(db); QVERIFY(!old.identity.isEmpty()); QVERIFY(!old.fileIdentity.isEmpty());
        db.close(); QVERIFY(QFile::rename(path,path+".old")); QVERIFY(QFile::rename(clone,path)); QVERIFY(db.open(path));
        QString error; const qslplanner::RouteCost cost{"12.50","EUR","estimate","",""};
        QVERIFY(!qslplanner::changeRouteCostGuarded(db,old,"M0ABC","B",cost,&error)); QVERIFY(!qslplanner::routeCost(db,"M0ABC","B"));
        QVERIFY(!qslplanner::changeRouteCostGuarded(db,old,"M0ABC","B",std::nullopt,&error));
        auto fresh=qslplanner::routeCostOrigin(db); QVERIFY2(qslplanner::changeRouteCostGuarded(db,fresh,"M0ABC","B",cost,&error),qPrintable(error));
        fresh=qslplanner::routeCostOrigin(db); { LogDatabase other; QVERIFY(other.open(path)); QSqlQuery update(other.connection()); QVERIFY(update.exec("INSERT INTO app_setting(key,value) VALUES('concurrent','yes')")); }
        QVERIFY(!qslplanner::changeRouteCostGuarded(db,fresh,"M0ABC","B",std::nullopt,&error)); QCOMPARE(qslplanner::routeCost(db,"M0ABC","B")->amount,QString("12.50"));
        fresh=qslplanner::routeCostOrigin(db); QVERIFY2(qslplanner::changeRouteCostGuarded(db,fresh,"M0ABC","B",std::nullopt,&error),qPrintable(error)); QVERIFY(!qslplanner::routeCost(db,"M0ABC","B"));
    }
    void selectionsAlsoRespectTheExactAwardAccount()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); LogDatabase db; QVERIFY(db.open(dir.filePath("accounts.sqlite"))); QString error;
        QVERIFY(qslplanner::saveGoalSelection(db,1,{"dxcc"},&error));
        QCOMPARE(*qslplanner::loadGoalSelectionForAccount(db,1,"account-A",&error),QStringList({"dxcc"}));
        QVERIFY(qslplanner::saveGoalSelectionForAccount(db,1,"account-A",{"waz"},&error));
        QVERIFY(qslplanner::saveGoalSelectionForAccount(db,1,"account-B",{"vucc"},&error));
        QCOMPARE(*qslplanner::loadGoalSelectionForAccount(db,1,"account-A",&error),QStringList({"waz"}));
        QCOMPARE(*qslplanner::loadGoalSelectionForAccount(db,1,"account-B",&error),QStringList({"vucc"}));
        QCOMPARE(*qslplanner::loadGoalSelectionForAccount(db,1,"account-a",&error),QStringList({"dxcc"}));
        QCOMPARE(*qslplanner::loadGoalSelection(db,1,&error),QStringList({"dxcc"}));
    }
    void accountFilterLimitsPlannerCandidates()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        StationProfile a; a.name="A"; a.stationCallsign="K1AAA"; a.awardAccount="account-A";
        StationProfile b; b.name="B"; b.stationCallsign="K1BBB"; b.awardAccount="account-B";
        const auto aId=db.saveStationProfile(a), bId=db.saveStationProfile(b); QVERIFY(aId>0 && bId>0);
        const auto first=db.insertQso({{"CALL","K1ONE"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"DXCC","291"}},"test",{},false,aId);
        const auto second=db.insertQso({{"CALL","K1TWO"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"DXCC","110"}},"test",{},false,bId);
        QVERIFY(first.id>0 && second.id>0);
        AwardFilter filter; filter.awardAccountUuid="account-A";
        const auto plan=qslplanner::build(db,filter,{"dxcc"}); QVERIFY2(plan.error.isEmpty(),qPrintable(plan.error));
        QCOMPARE(plan.candidates.size(),1); QCOMPARE(plan.candidates.first().qsoId,first.id);
        filter.awardAccountUuid="account-B";
        const auto other=qslplanner::build(db,filter,{"dxcc"}); QCOMPARE(other.candidates.size(),1); QCOMPARE(other.candidates.first().qsoId,second.id);
    }
    void selectionsAreScopedPerLogAndProfile()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); QString error;
        const QString a=dir.filePath("a.sqlite"), b=dir.filePath("b.sqlite");
        { LogDatabase first; QVERIFY(first.open(a));
          QVERIFY(!qslplanner::loadGoalSelection(first,0,&error)); QVERIFY(error.isEmpty());
          QVERIFY(qslplanner::saveGoalSelection(first,0,{"dxcc","was"},&error));
          QVERIFY(qslplanner::saveGoalSelection(first,3,{"vucc"},&error));
          QVERIFY(qslplanner::saveGoalSelection(first,4,{},&error));
          QVERIFY(!qslplanner::saveGoalSelection(first,-1,{"dxcc"},&error));
          QVERIFY(!qslplanner::saveGoalSelection(first,3,{"dxcc","dxcc"},&error));
          QCOMPARE(*qslplanner::loadGoalSelection(first,3,&error),QStringList({"vucc"}));
          QSqlQuery invalid(first.connection());
          QVERIFY(invalid.exec(QStringLiteral("INSERT INTO app_setting(key,value) VALUES('qslplanner.goals.v1.profile.9','not-json')")));
          QVERIFY(!qslplanner::loadGoalSelection(first,9,&error)); QVERIFY(!error.isEmpty()); }

        { LogDatabase first; QVERIFY(first.open(a));
          QCOMPARE(*qslplanner::loadGoalSelection(first,0,&error),QStringList({"dxcc","was"}));
          QCOMPARE(*qslplanner::loadGoalSelection(first,3,&error),QStringList({"vucc"}));
          QVERIFY(qslplanner::loadGoalSelection(first,4,&error)->isEmpty());
          QVERIFY(!qslplanner::loadGoalSelection(first,5,&error)); QVERIFY(error.isEmpty()); }
        { LogDatabase second; QVERIFY(second.open(b));
          QVERIFY(!qslplanner::loadGoalSelection(second,3,&error)); QVERIFY(error.isEmpty());
          QVERIFY(qslplanner::saveGoalSelection(second,3,{"wac"},&error)); }
        { LogDatabase first; QVERIFY(first.open(a)); QCOMPARE(*qslplanner::loadGoalSelection(first,3,&error),QStringList({"vucc"})); }
    }
    void pendingDatesAreReportedWithoutGuessing()
    {
        const QDate today(2026,11,23);
        auto state=[](const QString &date) { QslState s; s.service="card"; s.sent="Y"; s.sentDate=date; return s; };
        auto dated=qslplanner::pendingSince(state("20261015"),today); QCOMPARE(dated.dateStatus,QString("valid")); QCOMPARE(dated.date,QString("2026-10-15")); QCOMPARE(dated.days,39);
        QCOMPARE(qslplanner::pendingSince(state(""),today).dateStatus,QString("missing"));
        QCOMPARE(qslplanner::pendingSince(state("20260230"),today).dateStatus,QString("invalid"));
        QCOMPARE(qslplanner::pendingSince(state("20261124"),today).dateStatus,QString("future"));
        LogDatabase db; QVERIFY(db.open(":memory:")); const auto id=db.insertQso(AdifRecord{{"CALL","K1AAA"},{"QSO_DATE","20261001"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"DXCC","291"}},"fixture").id; QVERIFY(id>0);
        auto card=state("20261015"); QVERIFY(db.setCardState(id,card)); auto plan=qslplanner::build(db,{}, {"dxcc"});
        QCOMPARE(plan.candidates.size(),1); QCOMPARE(plan.candidates[0].pendingDetails.size(),1); QCOMPARE(plan.candidates[0].pendingDetails[0].date,QString("2026-10-15"));
        card.rcvd="Y"; QVERIFY(db.setCardState(id,card)); plan=qslplanner::build(db,{}, {"dxcc"}); QVERIFY(plan.candidates.isEmpty());
    }
    void sharedAlternativesArePagedWithoutDroppingRecords()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        for (int i=0;i<125;++i) {
            const auto result=db.insertQso(AdifRecord{{"CALL",QString("K%1AA").arg(i)},{"QSO_DATE","20261001"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"DXCC","291"}},"import");
            QVERIFY(result.id>0);
        }
        const auto plan=qslplanner::build(db,{}, {"dxcc"}); QCOMPARE(plan.candidates.size(),125);
        const auto& first=plan.candidates.first(); const auto& last=plan.candidates.last();
        QCOMPARE(first.benefits.first().group.data(),last.benefits.first().group.data());
        QCOMPARE(first.benefits.first().alternativeCount(first.qsoId),124);
        const auto firstPage=first.benefits.first().alternativePage(first.qsoId,0,100);
        const auto lastPage=first.benefits.first().alternativePage(first.qsoId,100,100);
        QCOMPARE(firstPage.size(),100); QCOMPARE(lastPage.size(),24); QVERIFY(!firstPage.contains(first.qsoId));
        QVERIFY(firstPage.last()<lastPage.first());
        const auto& middle=plan.candidates.at(49); const auto& benefit=middle.benefits.first();
        const auto pageA=benefit.alternativePage(middle.qsoId,0,50), pageB=benefit.alternativePage(middle.qsoId,50,50);
        QCOMPARE(pageA.size(),50); QCOMPARE(pageB.size(),50); QVERIFY(!pageA.contains(middle.qsoId));
        QVERIFY(!pageB.contains(middle.qsoId)); QVERIFY(pageA.last()<pageB.first());
    }
    void routeCostIsScopedValidatedAndPersistent()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const QString path=dir.filePath("log.sqlite");
        {
            LogDatabase db; QVERIFY(db.open(path)); QString error;
            qslplanner::RouteCost estimate{"12.50","EUR","estimate",{}, {}};
            QVERIFY2(qslplanner::saveRouteCost(db,"M0ABC","B",estimate,&error),qPrintable(error));
            QCOMPARE(qslplanner::routeCost(db," m0abc ","b")->amount,QString("12.50"));
            QVERIFY(!qslplanner::routeCost(db,"M0ABC","D"));
            qslplanner::RouteCost bad{"-1","EUR","estimate",{}, {}};
            QVERIFY(!qslplanner::saveRouteCost(db,"M0ABC","B",bad,&error));
            QCOMPARE(qslplanner::routeCost(db,"M0ABC","B")->amount,QString("12.50"));
            QSqlQuery trigger(db.connection()); QVERIFY(trigger.exec("CREATE TRIGGER reject_cost BEFORE UPDATE ON app_setting BEGIN SELECT RAISE(ABORT,'rejected'); END"));
            qslplanner::RouteCost rejected{"20","EUR","estimate",{}, {}};
            QVERIFY(!qslplanner::saveRouteCost(db,"M0ABC","B",rejected,&error)); QVERIFY(!error.isEmpty());
            QCOMPARE(qslplanner::routeCost(db,"M0ABC","B")->amount,QString("12.50")); QVERIFY(trigger.exec("DROP TRIGGER reject_cost"));
            bad={"10.00","EUR","sourced",{}, {}};
            QVERIFY(!qslplanner::saveRouteCost(db,"M0ABC","B",bad,&error));
            qslplanner::RouteCost sourced{"10.00","USD","sourced","operator checked price list","2026-10-01"};
            QVERIFY2(qslplanner::saveRouteCost(db,"M0ABC","D",sourced,&error),qPrintable(error));
        }
        LogDatabase reopened; QVERIFY(reopened.open(path));
        const auto cost=qslplanner::routeCost(reopened,"M0ABC","D"); QVERIFY(cost.has_value());
        QCOMPARE(cost->source,QString("operator checked price list")); QCOMPARE(cost->observedDate,QString("2026-10-01"));
        QString error; QVERIFY(qslplanner::clearRouteCost(reopened,"M0ABC","D",&error));
        QVERIFY(!qslplanner::routeCost(reopened,"M0ABC","D")); QVERIFY(qslplanner::routeCost(reopened,"M0ABC","B"));
    }
    void groupsOnlyRecordedManagerAndPaperChannel()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        auto add=[&](const char* call,const char* manager,const char* channel) {
            const qint64 id=db.insertQso(AdifRecord{{"CALL",call},{"QSO_DATE","20261001"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"DXCC","291"},{"CQZ","5"},{"QSL_VIA",manager}},"import").id;
            QslState state; state.service="card"; state.via=channel; if (id>0 && !db.setCardState(id,state)) return qint64(0); return id;
        };
        const qint64 a=add("K1AAA","M0ABC","B"), b=add("K1BBB","m0abc","B"), c=add("K1CCC","M0ABC","D");
        const qint64 d=add("K1DDD","","B"), e=add("K1EEE","M0ABC",""); QVERIFY(a && b && c && d && e);
        const auto plan=qslplanner::build(db,{}, {"dxcc","waz"});
        QCOMPARE(plan.paperRoutes.size(),2);
        QCOMPARE(plan.paperRoutes[0].manager,QString("M0ABC")); QCOMPARE(plan.paperRoutes[0].channel,QString("B"));
        QCOMPARE(plan.paperRoutes[0].qsoIds,QList<qint64>({a,b})); QCOMPARE(plan.paperRoutes[0].uniqueBenefits,2);
        QCOMPARE(plan.paperRoutes[1].channel,QString("D")); QCOMPARE(plan.paperRoutes[1].qsoIds,QList<qint64>({c}));
        QCOMPARE(plan.ungroupedPaperQsoIds,QList<qint64>({d,e}));
    }
    void configuredAwardRulesAreUsed()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        auto insert=[&](const char* call, const char* dxcc) {
            return db.insertQso(AdifRecord{{"CALL",call},{"QSO_DATE","20261001"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"DXCC",dxcc},{"CQZ","5"}},"import").id;
        };
        const qint64 valid=insert("K1AAA","291"), deleted=insert("K1BBB","110"); QVERIFY(valid>0 && deleted>0);
        AwardCalculator calculator; calculator.setDxccRules({{},[](int dxcc) { return dxcc==110; }});
        const auto package=awards::AwardRulePackage::fromJson(R"({"format":"decodxlog-award-rule-1","id":"test_local","title":"Local","source":"fixture","version":"1","validFrom":"2026-01-01","validTo":"2026-12-31","keyField":"DXCC","target":1})");
        QVERIFY(package.has_value()); calculator.setRulePackages({*package});
        const auto plan=qslplanner::build(db,{}, {"dxcc","test_local"},calculator);
        QVERIFY2(plan.error.isEmpty(),qPrintable(plan.error)); QCOMPARE(plan.uniqueBenefits,3);
        QCOMPARE(plan.candidates.size(),2); QCOMPARE(plan.candidates[0].benefits.size(),2);
        QCOMPARE(plan.candidates[1].benefits.size(),1); QCOMPARE(plan.candidates[1].benefits.first().award,QString("test_local"));
    }
    void alternativesAndConfirmationRebuild()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        auto add=[&](const char* call, int dxcc, int zone, const char* via) {
            return db.insertQso(AdifRecord{{"CALL",call},{"QSO_DATE","20261001"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"DXCC",QString::number(dxcc)},{"CQZ",QString::number(zone)},{"QSL_VIA",via}},"import").id;
        };
        const qint64 a=add("K1AAA",291,5,"M0ABC"), b=add("K1BBB",291,5,"M0ABC"), c=add("K1CCC",110,5,"");
        QVERIFY(a>0 && b>0 && c>0);
        AwardFilter filter; auto plan=qslplanner::build(db,filter,{"dxcc","waz"});
        QCOMPARE(plan.candidates.size(),3); QCOMPARE(plan.uniqueBenefits,3);
        QCOMPARE(plan.candidates[0].benefits.size(),2);
        QCOMPARE(plan.candidates[0].benefits[0].award,QString("dxcc"));
        QCOMPARE(plan.candidates[0].benefits[0].alternativePage(a,0,100),QList<qint64>({b}));
        QCOMPARE(plan.candidates[0].benefits[0].group.data(),plan.candidates[1].benefits[0].group.data());
        QCOMPARE(plan.candidates[0].benefits[1].award,QString("waz"));
        QCOMPARE(plan.candidates[0].benefits[1].alternativePage(a,0,100),QList<qint64>({b,c}));
        QCOMPARE(plan.candidates[0].benefits[1].alternativePage(a,1,1),QList<qint64>({c}));
        QCOMPARE(plan.candidates[1].benefits[1].alternativePage(b,0,100),QList<qint64>({a,c}));
        QCOMPARE(plan.candidates[0].priorityRank,1); QCOMPARE(plan.candidates[0].marginalBenefits.size(),2);
        QCOMPARE(plan.candidates[1].priorityRank,0); QCOMPARE(plan.candidates[2].priorityRank,2);
        QCOMPARE(plan.candidates[2].marginalBenefits.size(),1);
        QCOMPARE(plan.candidates[0].manager,QString("M0ABC"));
        QslState unaccepted; unaccepted.service="eqsl"; unaccepted.sent="Y"; QVERIFY(db.setQslState(c,unaccepted));
        plan=qslplanner::build(db,filter,{"dxcc","waz"});
        QVERIFY(plan.candidates[0].benefits[1].alternativePage(a,0,100,true).isEmpty());
        QslState state; state.service="card"; state.sent="Y"; QVERIFY(db.setQslState(b,state));
        plan=qslplanner::build(db,filter,{"dxcc","waz"});
        QVERIFY(plan.candidates[1].pendingServices.contains("card"));
        QCOMPARE(plan.candidates[1].priorityRank,1); QCOMPARE(plan.candidates[1].pendingMarginalCount,2);
        QCOMPARE(plan.candidates[0].priorityRank,0); QCOMPARE(plan.candidates[2].priorityRank,2);
        QCOMPARE(plan.candidates[0].benefits[0].alternativePage(a,0,100,true),QList<qint64>({b}));
        state.rcvd="Y"; QVERIFY(db.setCardState(b,state));
        plan=qslplanner::build(db,filter,{"dxcc","waz"});
        QCOMPARE(plan.uniqueBenefits,1); QCOMPARE(plan.candidates.size(),1); QCOMPARE(plan.candidates[0].qsoId,c);
        QVERIFY(plan.candidates[0].manager.isEmpty());
    }
    void profileAndRestartAreRecomputed()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const QString path=dir.filePath("log.sqlite");
        qint64 one=0,two=0;
        {
            LogDatabase db; QVERIFY(db.open(path));
            StationProfile first; first.name="First"; first.stationCallsign="9H1AA"; one=db.saveStationProfile(first);
            StationProfile second; second.name="Second"; second.stationCallsign="9H1BB"; two=db.saveStationProfile(second);
            QVERIFY(one>0 && two>0);
            auto insert=[&](const char* call, qint64 profile) {
                return db.insertQso(AdifRecord{{"CALL",call},{"QSO_DATE","20261001"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"DXCC","291"},{"CQZ","5"}},"import",{},false,profile).id;
            };
            QVERIFY(insert("K1AAA",one)>0); QVERIFY(insert("K1BBB",two)>0);
            AwardFilter filter; filter.stationProfileId=one; auto a=qslplanner::build(db,filter,{"dxcc","waz"});
            QCOMPARE(a.uniqueBenefits,2); QCOMPARE(a.candidates.size(),1); QCOMPARE(a.candidates.first().call,QString("K1AAA"));
            filter.stationProfileId=two; auto b=qslplanner::build(db,filter,{"dxcc","waz"});
            QCOMPARE(b.candidates.size(),1); QCOMPARE(b.candidates.first().call,QString("K1BBB"));
            QVERIFY(!qslplanner::build(db,filter,{"nonexistent"}).error.isEmpty());
        }
        LogDatabase reopened; QVERIFY(reopened.open(path)); AwardFilter filter; filter.stationProfileId=one;
        const auto plan=qslplanner::build(reopened,filter,{"dxcc","waz"});
        QVERIFY(plan.error.isEmpty()); QCOMPARE(plan.uniqueBenefits,2); QCOMPARE(plan.candidates.first().call,QString("K1AAA"));
    }
};
QTEST_GUILESS_MAIN(TestQslPlanner)
#include "tst_qslplanner.moc"
