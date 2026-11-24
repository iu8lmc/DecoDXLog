#include "core/ActivityReport.h"
#include "core/LogDatabase.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QElapsedTimer>
#include <QLibrary>
#include <QSqlDriver>
#include <QTest>
using namespace decolog::core;
class TestActivityReport : public QObject {
    Q_OBJECT
private slots:
    void customReportGroupsByUtcDayAndHour()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); LogDatabase db; QVERIFY(db.open(dir.filePath("report.sqlite")));
        for (const auto* call : {"K1AAA","K1BBB","K1CCC"}) QVERIFY(db.insertQso(AdifRecord{{"CALL",call},{"QSO_DATE","20261231"},{"TIME_ON","2300"},{"BAND","20m"},{"MODE","CW"}},"fixture").id>0);
        QSqlQuery q(db.connection()); QVERIFY(q.exec("UPDATE qso SET qso_datetime_on=CASE call WHEN 'K1AAA' THEN '2027-01-01T01:10:00+02:00' WHEN 'K1BBB' THEN '2026-12-31T23:20:00Z' ELSE 'invalid' END"));
        const auto result=activityreport::runCustom(db.connection(),0,"",{"day","hour"},"qsos"); QVERIFY2(result.error.isEmpty(),qPrintable(result.error)); QCOMPARE(result.rows.size(),2);
        QCOMPARE(result.rows.at(0).toMap().value("d0").toString(),QString()); QCOMPARE(result.rows.at(0).toMap().value("value").toInt(),1);
        QCOMPARE(result.rows.at(1).toMap().value("d0").toString(),QString("2026-12-31")); QCOMPARE(result.rows.at(1).toMap().value("d1").toString(),QString("23")); QCOMPARE(result.rows.at(1).toMap().value("value").toInt(),2);
        QVERIFY(activityreport::customCsv(result,{"day","hour"},"qsos").contains("\"2026-12-31\",\"23\",2"));
        const auto filtered=activityreport::runCustom(db.connection(),2026,"20m",{"day","hour"},"qsos"); QVERIFY(filtered.error.isEmpty()); QCOMPARE(filtered.rows.size(),1); QCOMPARE(filtered.rows.first().toMap().value("value").toInt(),2);
        activityreport::Definition d; d.name="UTC hours"; d.kind="custom"; d.dimensions={"day","hour"}; d.aggregate="qsos"; QString error; QVERIFY2(activityreport::saveDefinition(db,d,&error),qPrintable(error));
        LogDatabase reopened; QVERIFY(reopened.open(dir.filePath("report.sqlite"))); const auto saved=activityreport::loadDefinition(reopened,d.id); QVERIFY(saved); QCOMPARE(saved->dimensions,QStringList({"day","hour"}));
    }

    void customReportFiltersAndPersistsMode()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); LogDatabase db; QVERIFY(db.open(dir.filePath("report.sqlite")));
        for (const auto& mode : {QStringLiteral("CW"),QStringLiteral("FT8"),QStringLiteral("FT4")}) QVERIFY(db.insertQso(AdifRecord{{"CALL","K1ABC"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE",mode}},"fixture",{},true,0,false).id>0);
        const auto all=activityreport::runCustom(db.connection(),2026,"20m",{"band"},"qsos"); QVERIFY(all.error.isEmpty()); QCOMPARE(all.rows.first().toMap().value("value").toInt(),3);
        const auto filtered=activityreport::runCustom(db.connection(),2026,"20m",{"band"},"qsos","FT8"); QVERIFY2(filtered.error.isEmpty(),qPrintable(filtered.error)); QCOMPARE(filtered.rows.first().toMap().value("value").toInt(),1);
        const auto csv=activityreport::customCsv(filtered,{"band"},"qsos","FT8"); QVERIFY(csv.contains("qso_count,filter_recorded_mode\r\n\"20m\",1,\"FT8\""));
        QVERIFY(!activityreport::customCsv(all,{"band"},"qsos").contains("filter_recorded_mode"));
        QVERIFY(!activityreport::runCustom(db.connection(),2026,"20m",{"band"},"qsos","FT8' OR 1=1").error.isEmpty());
        activityreport::Definition d; d.name="FT8 only"; d.kind="custom"; d.dimensions={"band"}; d.aggregate="qsos"; d.mode="FT8"; QString error; QVERIFY2(activityreport::saveDefinition(db,d,&error),qPrintable(error));
        LogDatabase reopened; QVERIFY(reopened.open(dir.filePath("report.sqlite"))); const auto saved=activityreport::loadDefinition(reopened,d.id); QVERIFY(saved); QCOMPARE(saved->mode,QString("FT8"));
    }
    void customReportCountsRecordedDxcc()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); LogDatabase db; QVERIFY(db.open(dir.filePath("report.sqlite")));
        for (int i=0;i<5;++i) QVERIFY(db.insertQso(AdifRecord{{"CALL",QString("K1DX%1").arg(i)},{"QSO_DATE","20260101"},{"TIME_ON",QString("120%1").arg(i)},{"BAND","20m"},{"MODE","CW"}},"fixture",{},true,0,false).id>0);
        QSqlQuery q(db.connection()); QVERIFY(q.exec("UPDATE qso SET dxcc=CASE call WHEN 'K1DX0' THEN 291 WHEN 'K1DX1' THEN 291 WHEN 'K1DX2' THEN 1 WHEN 'K1DX3' THEN 0 ELSE NULL END"));
        const auto result=activityreport::runCustom(db.connection(),2026,"20m",{"band"},"entities"); QVERIFY2(result.error.isEmpty(),qPrintable(result.error)); QCOMPARE(result.rows.first().toMap().value("value").toInt(),2);
        QVERIFY(activityreport::customCsv(result,{"band"},"entities").contains("distinct_recorded_dxcc\r\n\"20m\",2"));
        activityreport::Definition d; d.name="DXCC counts"; d.kind="custom"; d.dimensions={"band"}; d.aggregate="entities"; QString error; QVERIFY2(activityreport::saveDefinition(db,d,&error),qPrintable(error));
        LogDatabase reopened; QVERIFY(reopened.open(dir.filePath("report.sqlite"))); const auto saved=activityreport::loadDefinition(reopened,d.id); QVERIFY(saved); QCOMPARE(saved->aggregate,QString("entities"));
    }
    void customReportGroupsRecordedDxcc()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        for (int i=0;i<4;++i) QVERIFY(db.insertQso(AdifRecord{{"CALL",QString("K1DX%1").arg(i)},{"QSO_DATE","20260101"},{"TIME_ON",QString("120%1").arg(i)},{"BAND","20m"},{"MODE","CW"}},"fixture",{},true,0,false).id>0);
        QSqlQuery q(db.connection()); QVERIFY(q.exec("UPDATE qso SET dxcc=CASE call WHEN 'K1DX0' THEN 291 WHEN 'K1DX1' THEN 291 WHEN 'K1DX2' THEN 0 ELSE NULL END"));
        const auto result=activityreport::runCustom(db.connection(),2026,"20m",{"band","dxcc"},"qsos");
        QVERIFY2(result.error.isEmpty(),qPrintable(result.error)); QCOMPARE(result.rows.size(),2);
        QCOMPARE(result.rows.at(0).toMap().value("d1").toString(),QString()); QCOMPARE(result.rows.at(0).toMap().value("value").toInt(),2);
        QCOMPARE(result.rows.at(1).toMap().value("d1").toString(),QString("291")); QCOMPARE(result.rows.at(1).toMap().value("value").toInt(),2);
        QVERIFY(activityreport::customCsv(result,{"band","dxcc"},"qsos").contains("\"20m\",\"291\",2"));
    }
    void insertDuringActiveReportQuery()
    {
#ifndef Q_OS_MACOS
        QSKIP("Native progress hook requires verified system SQLite on macOS");
#endif
        if (!qEnvironmentVariableIsSet("DECOLOG_TEST_SYSTEM_SQLITE_AUTHORIZER")) QSKIP("Opt in only with verified Qt SQLite linkage");
        QLibrary sqlite(QStringLiteral("sqlite3")); using Progress=void (*)(void*,int,int (*)(void*),void*);
        const auto progress=reinterpret_cast<Progress>(sqlite.resolve("sqlite3_progress_handler")); if (!progress) QSKIP("SQLite progress hook unavailable");
        QTemporaryDir dir; QVERIFY(dir.isValid()); LogDatabase reader,writer; const auto path=dir.filePath("report.sqlite"); QVERIFY(reader.open(path)); QVERIFY(writer.open(path));
        QSqlQuery seed(reader.connection()); QVERIFY2(seed.exec("WITH RECURSIVE n(i) AS (VALUES(1) UNION ALL SELECT i+1 FROM n WHERE i<100000) INSERT INTO qso(uuid,call,qso_datetime_on,band,mode,created_at,updated_at,tx_pwr) SELECT printf('fixture-%d',i),printf('K%d',i),'2026-01-01T12:00:00Z','20m','CW','2026-01-01','2026-01-01',10 FROM n"),qPrintable(seed.lastError().text()));
        QSqlDatabase snapshot=reader.connection(); QString error; QVERIFY2(activityreport::beginVerifiedSnapshot(snapshot,reader.identity(),&error),qPrintable(error));
        QVariant handle=snapshot.driver()->handle(); if (QByteArray(handle.typeName())!="sqlite3*" || !handle.data()) QSKIP("SQLite native handle unavailable"); void* native=*static_cast<void**>(handle.data()); QVERIFY(native);
        struct State { LogDatabase* writer; bool entered=false; bool inserted=false; } state{&writer};
        const auto hook=+[](void* opaque) { auto* s=static_cast<State*>(opaque); if (!s->entered) { s->entered=true; s->inserted=s->writer->insertQso(AdifRecord{{"CALL","K9ACTIVE"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"TX_PWR","20"}},"fixture",{},true,0,false).id>0; } return 0; };
        progress(native,10000,hook,&state); const auto before=activityreport::runCustom(snapshot,2026,"20m",{"band"},"mean_power"); progress(native,0,nullptr,nullptr);
        QVERIFY(state.entered); QVERIFY(state.inserted); QVERIFY2(before.error.isEmpty(),qPrintable(before.error)); QCOMPARE(before.rows.first().toMap().value("samples").toInt(),100000); QCOMPARE(before.rows.first().toMap().value("value").toDouble(),10.0);
        QVERIFY(snapshot.rollback()); const auto after=activityreport::runCustom(reader.connection(),2026,"20m",{"band"},"mean_power"); QVERIFY2(after.error.isEmpty(),qPrintable(after.error)); QCOMPARE(after.rows.first().toMap().value("samples").toInt(),100001); QVERIFY(qAbs(after.rows.first().toMap().value("value").toDouble()-(1000020.0/100001.0))<1e-10);
    }
    void largeReportSnapshotExcludesLaterInsert()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const QString path=dir.filePath("large.sqlite"); LogDatabase reader, writer;
        QVERIFY(reader.open(path)); QVERIFY(writer.open(path)); QSqlQuery seed(reader.connection());
        QVERIFY2(seed.exec("WITH RECURSIVE n(i) AS (VALUES(1) UNION ALL SELECT i+1 FROM n WHERE i<100000) "
            "INSERT INTO qso(uuid,call,qso_datetime_on,band,mode,created_at,updated_at,tx_pwr) "
            "SELECT printf('fixture-%d',i),printf('K%d',i),'2026-01-01T12:00:00Z','20m','CW','2026-01-01','2026-01-01',10 FROM n"),qPrintable(seed.lastError().text()));
        QSqlDatabase snapshot=reader.connection(); QString error; QVERIFY2(activityreport::beginVerifiedSnapshot(snapshot,reader.identity(),&error),qPrintable(error));
        QElapsedTimer elapsed; elapsed.start();
        QVERIFY(writer.insertQso(AdifRecord{{"CALL","K9NEW"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"TX_PWR","20"}},"fixture",{},true,0,false).id>0);
        const qint64 insertMs=elapsed.elapsed(); const auto before=activityreport::runCustom(snapshot,2026,"20m",{"band"},"mean_power");
        QVERIFY2(before.error.isEmpty(),qPrintable(before.error)); QCOMPARE(before.rows.size(),1); QCOMPARE(before.rows.first().toMap().value("samples").toInt(),100000); QCOMPARE(before.rows.first().toMap().value("value").toDouble(),10.0);
        QVERIFY(snapshot.rollback()); const auto after=activityreport::runCustom(reader.connection(),2026,"20m",{"band"},"mean_power");
        QVERIFY2(after.error.isEmpty(),qPrintable(after.error)); QCOMPARE(after.rows.first().toMap().value("samples").toInt(),100001); QVERIFY(qAbs(after.rows.first().toMap().value("value").toDouble()-(1000020.0/100001.0))<1e-10);
        qInfo() << "100k report fixture insert_ms" << insertMs << "total_ms" << elapsed.elapsed();
    }
    void customReportMeanRecordedPower()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); LogDatabase db; QVERIFY(db.open(dir.filePath("report.sqlite")));
        for (int i=0;i<7;++i)
            QVERIFY(db.insertQso(AdifRecord{{"CALL",QString("K1AA%1").arg(i)},{"QSO_DATE","20260101"},{"TIME_ON",QString("120%1").arg(i)},{"BAND",i==5?"40m":i==6?"80m":"20m"},{"MODE","CW"}},"fixture",{},true).id>0);
        QSqlQuery q(db.connection());
        QVERIFY(q.exec("UPDATE qso SET tx_pwr=CASE call WHEN 'K1AA0' THEN 5 WHEN 'K1AA1' THEN 10 WHEN 'K1AA2' THEN 'bad' WHEN 'K1AA3' THEN -1 WHEN 'K1AA6' THEN 0 ELSE NULL END"));
        const auto result=activityreport::runCustom(db.connection(),2026,{}, {"band"},"mean_power");
        QVERIFY2(result.error.isEmpty(),qPrintable(result.error)); QCOMPARE(result.rows.size(),3);
        const auto a=result.rows.at(0).toMap(), b=result.rows.at(1).toMap();
        QCOMPARE(a.value("d0").toString(),QString("20m")); QCOMPARE(a.value("value").toDouble(),7.5); QCOMPARE(a.value("samples").toInt(),2); QCOMPARE(a.value("excluded").toInt(),3);
        QVERIFY(b.value("value").isNull()); QCOMPARE(b.value("samples").toInt(),0); QCOMPARE(b.value("excluded").toInt(),1);
        const auto zero=result.rows.at(2).toMap(); QCOMPARE(zero.value("d0").toString(),QString("80m")); QVERIFY(!zero.value("value").isNull()); QCOMPARE(zero.value("value").toDouble(),0.0); QCOMPARE(zero.value("samples").toInt(),1); QCOMPARE(zero.value("excluded").toInt(),0);
        const auto csv=activityreport::customCsv(result,{"band"},"mean_power"); QVERIFY(csv.contains("mean_tx_power_w,samples,excluded")); QVERIFY(csv.contains("\"20m\",7.5,2,3")); QVERIFY(csv.contains("\"40m\",,0,1")); QVERIFY(csv.contains("\"80m\",0,1,0"));
        activityreport::Definition saved; saved.name="Power"; saved.kind="custom"; saved.dimensions={"band"}; saved.aggregate="mean_power"; QString error;
        QVERIFY2(activityreport::saveDefinition(db,saved,&error),qPrintable(error)); LogDatabase reopened; QVERIFY(reopened.open(dir.filePath("report.sqlite"))); QCOMPARE(activityreport::loadDefinition(reopened,saved.id)->aggregate,QString("mean_power"));
    }
    void customReportCountsDistinctUtcDays()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); LogDatabase db; QVERIFY(db.open(dir.filePath("report.sqlite")));
        for (const auto& v : {QPair<QString,QString>{"K1AAA","20260101"},{"K1BBB","20260101"},{"K1CCC","20260102"},{"K1DDD","20260102"}})
            QVERIFY(db.insertQso(AdifRecord{{"CALL",v.first},{"QSO_DATE",v.second},{"TIME_ON","120000"},{"BAND","20m"},{"MODE","CW"}},"fixture",{},true).id>0);
        QSqlQuery q(db.connection()); QVERIFY(q.exec("UPDATE qso SET qso_datetime_on='2026-01-02T00:15:00+01:00' WHERE call='K1DDD'"));
        const auto days=activityreport::runCustom(db.connection(),2026,"20m",{"band"},"days");
        QVERIFY2(days.error.isEmpty(),qPrintable(days.error)); QCOMPARE(days.rows.size(),1); QCOMPARE(days.rows.first().toMap().value("value").toInt(),2);
        QVERIFY(activityreport::customCsv(days,{"band"},"days").contains("distinct_utc_days"));
        activityreport::Definition saved; saved.name="Active days"; saved.kind="custom"; saved.dimensions={"band"}; saved.aggregate="days"; QString error;
        QVERIFY2(activityreport::saveDefinition(db,saved,&error),qPrintable(error)); QCOMPARE(activityreport::loadDefinition(db,saved.id)->aggregate,QString("days"));
        LogDatabase reopened; QVERIFY(reopened.open(dir.filePath("report.sqlite")));
        const auto loaded=activityreport::loadDefinition(reopened,saved.id); QVERIFY(loaded); QCOMPARE(loaded->aggregate,QString("days"));
    }
    void reportSnapshotRejectsAnotherLogAndPinsData()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); LogDatabase first, other;
        QVERIFY(first.open(dir.filePath("first.sqlite"))); QVERIFY(other.open(dir.filePath("other.sqlite")));
        QVERIFY(first.identity()!=other.identity());
        QVERIFY(first.insertQso(AdifRecord{{"CALL","K1AAA"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}},"fixture",{},true,0,false).id>0);
        QString error; QSqlDatabase wrong=other.connection();
        QVERIFY(!activityreport::beginVerifiedSnapshot(wrong,first.identity(),&error)); QVERIFY(!error.isEmpty());
        QSqlDatabase snapshot=first.connection(); error.clear(); QVERIFY2(activityreport::beginVerifiedSnapshot(snapshot,first.identity(),&error),qPrintable(error));
        LogDatabase writer; QVERIFY(writer.open(dir.filePath("first.sqlite")));
        QVERIFY(writer.insertQso(AdifRecord{{"CALL","K1BBB"},{"QSO_DATE","20260101"},{"TIME_ON","1201"},{"BAND","20m"},{"MODE","CW"}},"fixture",{},true,0,false).id>0);
        auto result=activityreport::run(snapshot,2026,"20m"); QVERIFY(result.error.isEmpty()); QCOMPARE(result.rows.first().toMap().value("count").toInt(),1);
        QVERIFY(snapshot.rollback());
        auto refreshed=activityreport::run(first.connection(),2026,"20m"); QVERIFY(refreshed.error.isEmpty()); QCOMPARE(refreshed.rows.first().toMap().value("count").toInt(),2);
        QVERIFY(!activityreport::beginVerifiedSnapshot(snapshot,QString(),&error));
        QCOMPARE(first.qsoCount(),qint64(2)); QCOMPARE(other.qsoCount(),qint64(0));
    }
    void savedDefinitionsAreIndependentAndRejectStaleChanges()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const QString path=dir.filePath("reports.sqlite");
        LogDatabase first; QVERIFY(first.open(path)); first.setSetting("activity_report_v1",R"({"year":2024,"band":"20m"})");
        activityreport::Definition a; a.name="Band by mode"; a.kind="custom"; a.year=2026; a.band="20m"; a.dimensions={"band","mode"}; a.aggregate="calls";
        activityreport::Definition b; b.name="Club"; b.kind="club"; b.station="9H1SR"; QString error;
        QVERIFY2(activityreport::saveDefinition(first,a,&error),qPrintable(error)); QVERIFY2(activityreport::saveDefinition(first,b,&error),qPrintable(error)); QVERIFY(a.id!=b.id);
        LogDatabase second; QVERIFY(second.open(path)); auto stale=activityreport::loadDefinition(second,a.id,&error); QVERIFY(stale);
        auto changed=a; changed.name="Band by year"; changed.dimensions={"band","year"}; QVERIFY(activityreport::saveDefinition(first,changed,&error));
        stale->name="stale"; QVERIFY(!activityreport::saveDefinition(second,*stale,&error)); QVERIFY(!activityreport::removeDefinition(second,*stale,&error));
        const auto items=activityreport::listDefinitions(first,&error); QVERIFY2(error.isEmpty(),qPrintable(error)); QCOMPARE(items.size(),2);
        QCOMPARE(activityreport::loadDefinition(first,a.id)->name,QString("Band by year"));
        QVERIFY(activityreport::removeDefinition(first,b,&error)); QCOMPARE(activityreport::listDefinitions(first).size(),1);
        QVERIFY(first.setting("activity_report_v1").contains("2024"));
        changed.dimensions={"band","band"}; QVERIFY(!activityreport::saveDefinition(first,changed,&error)); QCOMPARE(activityreport::listDefinitions(first).size(),1);
        first.setSetting("saved_report.invalid","not json"); QVERIFY(activityreport::listDefinitions(first,&error).isEmpty()); QVERIFY(!error.isEmpty());
        QSqlQuery q(first.connection()); QVERIFY(q.exec("DELETE FROM app_setting WHERE key='saved_report.invalid'")); QCOMPARE(activityreport::listDefinitions(first).size(),1);
    }
    void savedDefinitionLimitDoesNotCreateAnUnreadableLibrary()
    {
        LogDatabase db; QVERIFY(db.open(":memory:")); QSqlQuery q(db.connection());
        QVERIFY(q.exec("WITH RECURSIVE n(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM n WHERE x<1000) INSERT INTO app_setting(key,value) SELECT printf('saved_report.fixture%04d',x),'{}' FROM n"));
        activityreport::Definition d; d.name="Beyond limit"; d.kind="activity"; QString error;
        QVERIFY(!activityreport::saveDefinition(db,d,&error)); QVERIFY(d.id.isEmpty());
        QVERIFY(q.exec("SELECT COUNT(*) FROM app_setting WHERE key LIKE 'saved_report.%'")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),1000);
    }
    void configurableDimensionsAndAggregatesAreBoundedAndSafe()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        auto add=[&](const char* call,const char* band,const char* district) { return db.insertQso(AdifRecord{{"CALL",call},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND",band},{"MODE","CW"},{"APP_DISTRICT",district}},"fixture",{},true,0,false).id; };
        QVERIFY(add("K1AAA","20m","North")>0); QVERIFY(add("K1AAA","20m","North")>0); QVERIFY(add("K1BBB","20m","North")>0); QVERIFY(add("K1CCC","40m","South")>0);
        const QStringList dims{"band","custom:APP_DISTRICT"};
        const auto count=activityreport::runCustom(db.connection(),2026,{},dims,"qsos"); QVERIFY2(count.error.isEmpty(),qPrintable(count.error));
        QCOMPARE(count.rows.size(),2); QCOMPARE(count.rows[0].toMap().value("d0").toString(),QString("20m")); QCOMPARE(count.rows[0].toMap().value("d1").toString(),QString("North")); QCOMPARE(count.rows[0].toMap().value("value").toInt(),3);
        const auto distinct=activityreport::runCustom(db.connection(),2026,"20m",dims,"calls"); QVERIFY(distinct.error.isEmpty()); QCOMPARE(distinct.rows.size(),1); QCOMPARE(distinct.rows[0].toMap().value("value").toInt(),2);
        const auto three=activityreport::runCustom(db.connection(),2026,"20m",{"year","custom:APP_DISTRICT","custom:APP_MISSING"},"qsos");
        QVERIFY2(three.error.isEmpty(),qPrintable(three.error)); QCOMPARE(three.rows.size(),1); QCOMPARE(three.rows.first().toMap().value("d0").toString(),QString("2026"));
        QCOMPARE(three.rows.first().toMap().value("d2").toString(),QString()); QCOMPARE(three.rows.first().toMap().value("value").toInt(),3);
        QVERIFY(activityreport::customCsv(distinct,dims,"calls").contains("\"20m\",\"North\",2"));
        QVERIFY(!activityreport::runCustom(db.connection(),2026,{},QStringList{"custom:APP_X');DROP TABLE qso;--"},"qsos").error.isEmpty());
        QVERIFY(!activityreport::runCustom(db.connection(),2026,{},QStringList{"band","band"},"qsos").error.isEmpty());
        QVERIFY(!activityreport::runCustom(db.connection(),2026,{},dims,"sql").error.isEmpty()); QCOMPARE(db.qsoCount(),qint64(4));
        QSqlQuery malformed(db.connection()); QVERIFY(malformed.exec("UPDATE qso SET adif_extra='broken' WHERE call='K1CCC'"));
        const auto missing=activityreport::runCustom(db.connection(),2026,"40m",dims,"qsos"); QVERIFY(missing.error.isEmpty()); QCOMPARE(missing.rows.first().toMap().value("d1").toString(),QString());
    }
    void annualClubActivityUsesRecordedStationAndOperator()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        auto add=[&](const char* call,const char* date,const char* station,const char* op) {
            return db.insertQso(AdifRecord{{"CALL",call},{"QSO_DATE",date},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"STATION_CALLSIGN",station},{"OPERATOR",op}},"fixture",{},true).id;
        };
        QVERIFY(add("K1AAA","20260101","9H1SR","9H1AA")>0); QVERIFY(add("K1BBB","20260102","9H1SR","9H1AA")>0);
        QVERIFY(add("K1CCC","20260102","9H1SR","")>0); QVERIFY(add("K1DDD","20250102","9H1SR","9H1BB")>0);
        QVERIFY(add("K1EEE","20260102","9H9ZZ","9H1AA")>0);
        const auto report=activityreport::runClub(db.connection(),2026,"20m","9h1sr"); QVERIFY2(report.error.isEmpty(),qPrintable(report.error));
        QCOMPARE(report.rows.size(),2); QCOMPARE(report.rows[0].toMap().value("operator").toString(),QString());
        QCOMPARE(report.rows[1].toMap().value("operator").toString(),QString("9H1AA")); QCOMPARE(report.rows[1].toMap().value("count").toInt(),2);
        QVERIFY(activityreport::clubCsv(report).contains("\"2026\",\"9H1SR\",\"9H1AA\",2"));
        const auto all=activityreport::runClub(db.connection(),0,{},{}); QVERIFY(all.error.isEmpty()); QCOMPARE(all.rows.size(),4);
        QCOMPARE(db.qsoCount(),qint64(5));
    }
    void confirmationTimesKeepServicesAndMissingDatesSeparate()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        for (const QString& call : {QString("K1AAA"),QString("K1BBB"),QString("K1CCC"),QString("K1DDD"),QString("K1EEE")})
            QVERIFY(db.insertQso(AdifRecord{{"CALL",call},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}},"fixture",{},true).id>0);
        QVERIFY(db.insertQso(AdifRecord{{"CALL","K1FFF"},{"QSO_DATE","20250101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}},"fixture",{},true).id>0);
        QVERIFY(db.insertQso(AdifRecord{{"CALL","K1GGG"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","40m"},{"MODE","CW"}},"fixture",{},true).id>0);
        QSqlQuery q(db.connection());
        QVERIFY(q.exec("INSERT INTO qsl_status(qso_id,service,rcvd,rcvd_date) VALUES (1,'card','Y','20260101'),(2,'card','Y','20260103'),(3,'card','Y',NULL),(4,'card','Y','20260230'),(5,'card','Y','20251231'),(1,'lotw','Y','20260111'),(2,'eqsl','N','20260104'),(3,'qrz','Y',NULL)"));
        QVERIFY(q.exec("INSERT INTO qsl_status(qso_id,service,rcvd,rcvd_date) VALUES (6,'card','Y','20250102'),(7,'card','Y','20260102')"));
        QVERIFY(q.exec("UPDATE qso SET qso_datetime_on='2026-01-01T12:00:00' WHERE call='K1BBB'"));
        const auto result=activityreport::runConfirmations(db.connection(),2026,"20m"); QVERIFY2(result.error.isEmpty(),qPrintable(result.error));
        QCOMPARE(result.rows.size(),3); const auto card=result.rows[0].toMap(); const auto lotw=result.rows[1].toMap();
        QCOMPARE(card.value("service").toString(),QString("card")); QCOMPARE(card.value("confirmed").toInt(),5);
        QCOMPARE(card.value("dated").toInt(),2); QCOMPARE(card.value("missing").toInt(),1); QCOMPARE(card.value("invalid").toInt(),1); QCOMPARE(card.value("earlier").toInt(),1);
        QCOMPARE(card.value("averageDays").toDouble(),1.0); QCOMPARE(lotw.value("averageDays").toDouble(),10.0);
        QVERIFY(!result.rows[2].toMap().contains("averageDays"));
        QVERIFY(activityreport::confirmationsCsv(result).contains("\"card\",5,2,1,1,1,1.00"));
        QCOMPARE(db.qsoCount(),qint64(7));
    }
    void groupsRecordedValuesAndExportsCsv()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        auto add=[&](const char* call,const char* date,const char* band,const char* antenna,const char* grid) {
            return db.insertQso(AdifRecord{{"CALL",call},{"QSO_DATE",date},{"TIME_ON","1200"},{"BAND",band},{"MODE","CW"},{"MY_ANTENNA",antenna},{"MY_GRIDSQUARE",grid}},"fixture",{},true).id;
        };
        QVERIFY(add("K1AAA","20260101","20m","Dipole, \"A\"","JM75")>0);
        QVERIFY(add("K1BBB","20260102","20m","Dipole, \"A\"","JM75")>0);
        QVERIFY(add("K1CCC","20260102","40m","","JM75")>0);
        QVERIFY(add("K1DDD","20250102","20m","Yagi","JN35")>0);
        const auto report=activityreport::run(db.connection(),2026,"20m"); QVERIFY2(report.error.isEmpty(),qPrintable(report.error));
        QCOMPARE(report.rows.size(),1); const auto row=report.rows.first().toMap();
        QCOMPARE(row.value("antenna").toString(),QString("Dipole, \"A\"")); QCOMPARE(row.value("grid").toString(),QString("JM75")); QCOMPARE(row.value("count").toInt(),2);
        QCOMPARE(activityreport::csv(report),QByteArray("antenna,band,own_grid,qso_count\r\n\"Dipole, \"\"A\"\"\",\"20m\",\"JM75\",2\r\n"));
        const auto all=activityreport::run(db.connection(),0,{}); QVERIFY(all.error.isEmpty()); QCOMPARE(all.rows.size(),3);
        const auto missing=activityreport::run(db.connection(),2026,"40m"); QCOMPARE(missing.rows.first().toMap().value("antenna").toString(),QString());
        QVERIFY(!activityreport::run(db.connection(),-1,{}).error.isEmpty());
        QSqlQuery q(db.connection()); QVERIFY(q.exec("UPDATE qso SET adif_extra='broken' WHERE call='K1CCC'"));
        const auto malformed=activityreport::run(db.connection(),2026,"40m"); QVERIFY2(malformed.error.isEmpty(),qPrintable(malformed.error));
        QCOMPARE(malformed.rows.first().toMap().value("grid").toString(),QString()); QCOMPARE(db.qsoCount(),qint64(4));
        QVERIFY(q.exec("UPDATE qso SET deleted=1 WHERE call='K1DDD'"));
        const auto live=activityreport::run(db.connection(),0,{}); QVERIFY(live.error.isEmpty()); QCOMPARE(live.rows.size(),2);
    }
    void capsLargeGroupCountAndRefusesPartialCsv()
    {
        LogDatabase db; QVERIFY(db.open(":memory:")); QSqlQuery q(db.connection());
        QVERIFY2(q.exec("WITH RECURSIVE n(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM n WHERE x<10001) "
                       "INSERT INTO qso(uuid,call,qso_datetime_on,band,mode,source,adif_extra,created_at,updated_at) "
                       "SELECT printf('u%05d',x),'K1AAA','2026-01-01T00:00:00Z','20m','CW','fixture',json_object('MY_ANTENNA',printf('antenna%05d',x)),'2026-01-01','2026-01-01' FROM n"),qPrintable(q.lastError().text()));
        const auto report=activityreport::run(db.connection(),2026,"20m"); QVERIFY2(report.error.isEmpty(),qPrintable(report.error));
        QCOMPARE(report.rows.size(),10000); QVERIFY(report.truncated); QVERIFY(activityreport::csv(report).isEmpty());
        const auto custom=activityreport::runCustom(db.connection(),2026,"20m",{"antenna"},"qsos"); QVERIFY(custom.error.isEmpty()); QVERIFY(custom.truncated); QCOMPARE(custom.rows.size(),10000); QVERIFY(activityreport::customCsv(custom,{"antenna"},"qsos").isEmpty());
    }
    void reportObservesTheReadSnapshotDuringAnotherWrite()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const QString path=dir.filePath("snapshot.sqlite");
        LogDatabase reader; QVERIFY(reader.open(path)); QSqlQuery q(reader.connection()); QVERIFY(q.exec("PRAGMA journal_mode=WAL")); q.finish();
        auto add=[](LogDatabase& db,const char* call) { return db.insertQso(AdifRecord{{"CALL",call},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}},"fixture",{},true).id; };
        QVERIFY(add(reader,"K1AAA")>0); LogDatabase writer; QVERIFY(writer.open(path));
        QVERIFY(reader.connection().transaction()); QVERIFY(q.exec("SELECT COUNT(*) FROM qso")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),1); q.finish();
        QVERIFY(add(writer,"K1BBB")>0);
        const auto before=activityreport::run(reader.connection(),2026,"20m"); QVERIFY2(before.error.isEmpty(),qPrintable(before.error));
        QCOMPARE(before.rows.size(),1); QCOMPARE(before.rows.first().toMap().value("count").toInt(),1);
        const auto customBefore=activityreport::runCustom(reader.connection(),2026,"20m",{"band"},"qsos"); QVERIFY(customBefore.error.isEmpty()); QCOMPARE(customBefore.rows.first().toMap().value("value").toInt(),1);
        QVERIFY(reader.connection().commit()); const auto after=activityreport::run(reader.connection(),2026,"20m");
        QVERIFY(after.error.isEmpty()); QCOMPARE(after.rows.first().toMap().value("count").toInt(),2);
        const auto customAfter=activityreport::runCustom(reader.connection(),2026,"20m",{"band"},"qsos"); QVERIFY(customAfter.error.isEmpty()); QCOMPARE(customAfter.rows.first().toMap().value("value").toInt(),2);
    }
};
QTEST_GUILESS_MAIN(TestActivityReport)
#include "tst_activityreport.moc"
