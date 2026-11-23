#include "core/QslPlanner.h"
#include <QTemporaryDir>
#include <QSqlQuery>
#include <QTest>
using namespace decolog::core;
class TestQslPlanner : public QObject {
    Q_OBJECT
private slots:
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
        QVERIFY(plan.candidates[0].alternativeQsoIds.contains(b));
        QCOMPARE(plan.candidates[0].benefits[0].award,QString("dxcc"));
        QCOMPARE(plan.candidates[0].benefits[0].alternativeQsoIds,QSet<qint64>({b}));
        QCOMPARE(plan.candidates[0].benefits[1].award,QString("waz"));
        QCOMPARE(plan.candidates[0].benefits[1].alternativeQsoIds,QSet<qint64>({b,c}));
        QCOMPARE(plan.candidates[0].manager,QString("M0ABC"));
        QslState unaccepted; unaccepted.service="eqsl"; unaccepted.sent="Y"; QVERIFY(db.setQslState(c,unaccepted));
        plan=qslplanner::build(db,filter,{"dxcc","waz"});
        QVERIFY(plan.candidates[0].benefits[1].pendingAlternativeQsoIds.isEmpty());
        QslState state; state.service="card"; state.sent="Y"; QVERIFY(db.setQslState(b,state));
        plan=qslplanner::build(db,filter,{"dxcc","waz"});
        QVERIFY(plan.candidates[1].pendingServices.contains("card"));
        QCOMPARE(plan.candidates[0].benefits[0].pendingAlternativeQsoIds,QSet<qint64>({b}));
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
