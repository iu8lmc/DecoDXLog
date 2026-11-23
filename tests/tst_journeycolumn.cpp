#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include "core/LogDatabase.h"
#include "core/Activation.h"
#include "core/QslPlanner.h"
#include "core/WsjtxProtocol.h"
#include <QUdpSocket>
#include <QElapsedTimer>
#include "core/RadioNet.h"
#include "core/TesseractOcr.h"
#include "core/QslOcrAcquisition.h"
#include <QCryptographicHash>

class TestJourneyColumn : public QObject {
    Q_OBJECT
private:
    static QString iniPath(const QTemporaryDir& dir)
    {
        return dir.filePath(QStringLiteral("settings/Decodium/DecoDXLog.ini"));
    }
    static void seed(const QTemporaryDir& dir, const QString& layout, bool migrated)
    {
        QDir().mkpath(QFileInfo(iniPath(dir)).absolutePath());
        QSettings s(iniPath(dir), QSettings::IniFormat);
        s.setValue(QStringLiteral("layout/columnLayout"), layout);
        if (migrated)
            s.setValue(QStringLiteral("layout/journeyColumnMigrated"), true);
        s.sync();
    }
    static bool launch(const QTemporaryDir& dir, const QString& probe, QByteArray& output, int grabMs = 2800)
    {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        env.insert(QStringLiteral("QT_QUICK_BACKEND"), QStringLiteral("software"));
        env.insert(QStringLiteral("DECODXLOG_DATA_ROOT"), dir.filePath(QStringLiteral("data")));
        env.insert(QStringLiteral("DECODXLOG_GRAB_MS"), QString::number(grabMs));
        QProcess app;
        app.setProcessEnvironment(env);
        app.setProcessChannelMode(QProcess::MergedChannels);
        app.start(QStringLiteral(DECOLOG_APP), {
            QStringLiteral("--software-renderer"), QStringLiteral("--settings"), dir.filePath(QStringLiteral("settings")),
            QStringLiteral("--db"), dir.filePath(QStringLiteral("test.sqlite")), QStringLiteral("--port"), QStringLiteral("0"),
            QStringLiteral("--show"), probe, QStringLiteral("--grab"), dir.filePath(QStringLiteral("screen.png"))
        });
        if (!app.waitForStarted(10000) || !app.waitForFinished(40000)) {
            output = app.errorString().toUtf8() + app.readAll();
            return false;
        }
        output = app.readAll();
        return app.exitStatus() == QProcess::NormalExit && app.exitCode() == 0;
    }
private slots:
    void qslAlternativesDesktopPagesAndInvalidates()
    {
        using namespace decolog::core;
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
          for (int i=0;i<125;++i) QVERIFY(db.insertQso(AdifRecord{{"CALL",QString("K%1AA").arg(i)},{"QSO_DATE","20261001"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"DXCC","291"}},"import").id>0);
          QslState card; card.service="card"; card.sent="Y"; QVERIFY(db.setCardState(60,card)); }
        QByteArray output; QVERIFY2(launch(dir,"qslpagesprobe",output,7000),output.constData());
        QVERIFY2(output.contains("PROBE qslpages first") && output.contains("\"total\":124") && output.contains("\"offset\":0"),output.constData());
        QVERIFY2(output.contains("PROBE qslpages next") && output.contains("\"offset\":50"),output.constData());
        QVERIFY2(output.contains("\"id\":60,\"pending\":true"),output.constData());
        QVERIFY2(output.contains("PROBE qslpages last") && output.contains("\"offset\":100"),output.constData());
        QVERIFY2(output.contains("PROBE qslpages invalidated true") && output.contains("Plan is no longer current"),output.constData());
        QVERIFY2(!output.contains("TypeError"),output.constData());
    }
    void qslPlannerDesktopShowsBenefitsAndInvalidation()
    {
        using namespace decolog::core;
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
          auto insert=[&](const char* call,const char* dxcc) { return db.insertQso(AdifRecord{{"CALL",call},{"QSO_DATE","20261001"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"DXCC",dxcc},{"CQZ","5"},{"QSL_VIA","M0ABC"}},"import").id; };
          const qint64 first=insert("K1PAA","291"); QVERIFY(first>0); const qint64 pending=insert("K1PBB","291"); QVERIFY(pending>0); QVERIFY(insert("K1PCC","110")>0);
          EnrichmentEntry route; route.qsoId=first; route.field="QSL_VIA"; route.value="M0ABC"; route.source="fixture"; route.kind="received"; route.acquiredAt="2020-01-01T00:00:00Z"; QVERIFY(db.recordEnrichment({route}));
          QslState card; card.service="card"; card.via="B"; QVERIFY(db.setCardState(first,card)); card.sent="Y"; QVERIFY(db.setCardState(pending,card)); }
        QByteArray output; QVERIFY2(launch(dir,"qslplanprobe",output,7000),output.constData());
        QVERIFY2(output.contains("PROBE qslplan status 3 potential benefits · 3 candidate QSO"),output.constData());
        QVERIFY2(output.contains("K1PAA") && output.contains("M0ABC"),output.constData());
        QVERIFY2(output.contains("PROBE qslplan row K1PAA") && output.contains("DXCC 291"),output.constData());
        QVERIFY2(output.contains("Alternative: its benefits are covered by higher-priority candidates") && output.contains("\"priorityRank\":1") && output.contains("\"pendingMarginalCount\":2"),output.constData());
        QVERIFY2(output.contains("sent/unconfirmed alternatives: 2"),output.constData());
        QVERIFY2(output.contains("route evidence older than 180 days; verify") && output.contains("route source: fixture"),output.constData());
        QVERIFY2(output.contains("PROBE qslplan group M0ABC · bureau · QSO: 1, 2 · potential benefits: 2 · cost unknown"),output.constData());
        QVERIFY2(output.contains("PROBE qslplan saved cost") && output.contains("\"amount\":\"12.50\"") && output.contains("\"basis\":\"estimate\""),output.constData());
        QVERIFY2(output.contains("12.50 EUR · operator estimate"),output.constData());
        QVERIFY2(output.contains("PROBE qslplan paperqueue queue"),output.constData());
        QVERIFY2(output.contains("PROBE qslplan confirmation") && output.contains("\"uniqueBenefits\":1"),output.constData());
        QVERIFY2(output.contains("PROBE qslplan stale true"),output.constData());
        QVERIFY2(output.contains("PROBE qslplan band result") && output.contains("\"uniqueBenefits\":0"),output.constData());
        QVERIFY2(!output.contains("TypeError"),output.constData());
        QByteArray reopened; QVERIFY2(launch(dir,"qslplanrestoreprobe",reopened,4500),reopened.constData());
        QVERIFY2(reopened.contains("PROBE qslplan restored [\"dxcc\",\"waz\"]"),reopened.constData());
        QVERIFY2(reopened.contains("PROBE qslplan restored result") && reopened.contains("\"uniqueBenefits\":0"),reopened.constData());
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); const auto cost=qslplanner::routeCost(db,"M0ABC","B"); QVERIFY(cost.has_value()); QCOMPARE(cost->amount,QString("12.50")); }
    }
    void recoveredRecordsUseTheirJournalPeriod()
    {
        using namespace decolog::core;
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir, "", true);
        const QString path=dir.filePath("test.sqlite");
        const QDateTime now=QDateTime::currentDateTimeUtc();
        const QDateTime start=now.addSecs(-2400), change=now.addSecs(-1800), stop=now.addSecs(-1200);
        {
            LogDatabase db; QVERIFY(db.open(path));
            Activation a; a.active=true; a.kind=Activation::Kind::Pota; a.reference="IT-0001"; a.myGrid="JM75"; a.sessionUuid="historical-run"; a.startedAt=start;
            Activation b=a; b.reference="IT-0002"; b.myGrid="JM76";
            const auto json=[](const Activation& s) { return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(s.toMap())).toJson(QJsonDocument::Compact)); };
            QSqlQuery q(db.connection()); q.prepare("INSERT INTO activation_run(uuid,started_at,ended_at,initial_json,last_json) VALUES(?,?,?,?,?)");
            q.addBindValue(a.sessionUuid); q.addBindValue(start.toString(Qt::ISODateWithMs)); q.addBindValue(stop.toString(Qt::ISODateWithMs)); q.addBindValue(json(a)); q.addBindValue(json(b)); QVERIFY(q.exec());
            const auto event=[&](const QDateTime& at, const QString& kind, const Activation& s) { QSqlQuery e(db.connection()); e.prepare("INSERT INTO activation_run_change(session_uuid,changed_at,event,state_json) VALUES(?,?,?,?)"); e.addBindValue(a.sessionUuid); e.addBindValue(at.toString(Qt::ISODateWithMs)); e.addBindValue(kind); e.addBindValue(json(s)); return e.exec(); };
            QVERIFY(event(start,"start",a)); QVERIFY(event(change,"update",b)); QVERIFY(event(stop,"stop",b));
        }
        const auto record=[](const QString& call,const QDateTime& on,const QDateTime& off) { return AdifRecord{{"CALL",call},{"QSO_DATE",on.toString("yyyyMMdd")},{"TIME_ON",on.toString("HHmmss")},{"QSO_DATE_OFF",off.toString("yyyyMMdd")},{"TIME_OFF",off.toString("HHmmss")},{"BAND","20m"},{"MODE","CW"}}; };
        const QString log=dir.filePath("decodium_log.adi"); QFile f(log); QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("Decodium ADIF\n<EOH>\n");
        f.write(adif::writeRecord(record("K1HAA",change.addSecs(-300),change.addSecs(-270))).toUtf8());
        f.write(adif::writeRecord(record("K1HBB",change.addSecs(-10),change.addSecs(10))).toUtf8());
        f.write(adif::writeRecord(record("K1HCC",change.addSecs(120),change.addSecs(150))).toUtf8()); f.close();
        QSettings decodium(dir.filePath("settings/Decodium/Decodium3.ini"),QSettings::IniFormat); decodium.setValue("Logbooks/ActivePath",log); decodium.sync();
        QByteArray output; QVERIFY2(launch(dir,"",output,26000),output.constData());
        LogDatabase db; QVERIFY(db.open(path)); QSqlQuery q(db.connection());
        QVERIFY(q.exec("SELECT id,call,IFNULL(station_profile_id,0) FROM qso WHERE call IN ('K1HAA','K1HBB','K1HCC') ORDER BY call"));
        QVERIFY(q.next()); QCOMPARE(q.value(1).toString(),QString("K1HAA")); auto first=db.record(q.value(0).toLongLong()); QVERIFY(first.has_value());
        QCOMPARE(first->value("MY_POTA_REF"),QString("IT-0001")); QCOMPARE(first->value("MY_GRIDSQUARE"),QString("JM75")); QCOMPARE(first->value("APP_DECOLOG_ACTIVATION_UUID"),QString("historical-run")); QVERIFY(first->value("STX").isEmpty()); QCOMPARE(q.value(2).toLongLong(),qint64(0));
        QVERIFY(q.next()); QCOMPARE(q.value(1).toString(),QString("K1HBB")); auto middle=db.record(q.value(0).toLongLong()); QVERIFY(middle.has_value());
        QCOMPARE(middle->value("APP_DECOLOG_ACTIVATION_REVIEW"),QString("PERIOD_UNCERTAIN")); QVERIFY(middle->value("APP_DECOLOG_ACTIVATION_UUID").isEmpty()); QVERIFY(middle->value("MY_POTA_REF").isEmpty()); QCOMPARE(q.value(2).toLongLong(),qint64(0));
        QVERIFY(q.next()); QCOMPARE(q.value(1).toString(),QString("K1HCC")); auto last=db.record(q.value(0).toLongLong()); QVERIFY(last.has_value());
        QCOMPARE(last->value("MY_POTA_REF"),QString("IT-0002")); QCOMPARE(last->value("MY_GRIDSQUARE"),QString("JM76")); QVERIFY(last->value("STX").isEmpty()); QVERIFY(!q.next());
    }
    void udpActivationUsesSessionProfileWithoutOverwritingSourceFields()
    {
        using namespace decolog::core;
        QTemporaryDir dir; QVERIFY(dir.isValid()); const QString path = dir.filePath("test.sqlite");
        {
            LogDatabase db; QVERIFY(db.open(path));
            StationProfile profile; profile.name="UDP fixture"; profile.stationCallsign="9H1AA"; profile.operatorCall="9H1AA"; profile.myGridsquare="JN71DC"; profile.myRig="Fixture rig"; profile.myAntenna="Fixture antenna"; profile.defaultTxPwr=100; profile.isDefault=true;
            const qint64 profileId=db.saveStationProfile(profile); QVERIFY(profileId>0);
            Activation session; session.active=true; session.kind=Activation::Kind::Pota; session.reference="IT-0001";
            session.myGrid="JM75"; session.stationProfileId=profileId; session.startedAt=QDateTime::currentDateTimeUtc().addSecs(-60); session.sessionUuid="udp-session-fixture";
            const QString state=QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(session.toMap())).toJson(QJsonDocument::Compact));
            db.setSetting("activation.session", state); QSqlQuery q(db.connection());
            q.prepare("INSERT INTO activation_run(uuid,started_at,initial_json,last_json) VALUES(?,?,?,?)");
            q.addBindValue(session.sessionUuid); q.addBindValue(session.startedAt.toString(Qt::ISODate)); q.addBindValue(state); q.addBindValue(state); QVERIFY(q.exec());
        }
        quint16 port=0; { QUdpSocket probe; QVERIFY(probe.bind(QHostAddress::LocalHost, 0)); port=probe.localPort(); }
        QProcessEnvironment env=QProcessEnvironment::systemEnvironment(); env.insert("QT_QPA_PLATFORM", "offscreen");
        env.insert("QT_QUICK_BACKEND", "software"); env.insert("DECODXLOG_DATA_ROOT", dir.filePath("data"));
        env.insert("DECODXLOG_TRACE_ACTIVITY", "1"); env.insert("DECODXLOG_GRAB_MS", "9000");
        QProcess app; app.setProcessEnvironment(env); app.setProcessChannelMode(QProcess::MergedChannels);
        app.start(QStringLiteral(DECOLOG_APP), {"--software-renderer", "--settings", dir.filePath("settings"), "--db", path,
                                                "--port", QString::number(port), "--grab", dir.filePath("screen.png")});
        QVERIFY(app.waitForStarted(10000)); QByteArray output; QElapsedTimer timer; timer.start();
        while (!output.contains("[activity] UDP|info|") && timer.elapsed()<20000) { app.waitForReadyRead(100); output += app.readAll(); }
        QVERIFY2(output.contains("[activity] UDP|info|"), output.constData());
        const auto makeRecord = [](const QString& call, bool explicitFields) {
            const QDateTime now=QDateTime::currentDateTimeUtc();
            AdifRecord record{{"CALL",call},{"QSO_DATE",now.toString("yyyyMMdd")},{"TIME_ON",now.toString("HHmmss")},{"BAND","20m"},{"MODE","CW"}};
            if (explicitFields) { record.set("STATION_CALLSIGN","9H5ZZ"); record.set("OPERATOR","9H5ZZ"); record.set("MY_GRIDSQUARE","JN95"); record.set("TX_PWR", "25"); record.set("MY_RIG", "Sender rig"); }
            return (QStringLiteral("<EOH>\n") + adif::writeRecord(record)).toUtf8();
        };
        QUdpSocket clientA, clientB;
        const QByteArray a=wsjtx::buildLoggedAdif("Decodium-A", makeRecord("K1UAA",false));
        const QByteArray b=wsjtx::buildLoggedAdif("Decodium-B", makeRecord("K1UBB",true));
        AdifRecord conflict = adif::parse(makeRecord("K1UCC", false)).records.first();
        conflict.set("STATION_CALLSIGN", "9H5ZZ");
        const QByteArray c=wsjtx::buildLoggedAdif("Decodium-B", (QStringLiteral("<EOH>\n") + adif::writeRecord(conflict)).toUtf8());
        QCOMPARE(clientA.writeDatagram(a,QHostAddress::LocalHost,port), qint64(a.size()));
        QCOMPARE(clientB.writeDatagram(b,QHostAddress::LocalHost,port), qint64(b.size()));
        QCOMPARE(clientB.writeDatagram(c,QHostAddress::LocalHost,port), qint64(c.size()));
        wsjtx::QsoLogged structured; structured.dxCall="K1UDD"; structured.timeOn=QDateTime::currentDateTimeUtc(); structured.timeOff=structured.timeOn;
        structured.txFrequencyHz=14000000; structured.mode="CW"; structured.myCall="9H1AA"; structured.myGrid="JN80";
        const QByteArray d=wsjtx::buildQsoLogged("Decodium-A", structured);
        QCOMPARE(clientA.writeDatagram(d,QHostAddress::LocalHost,port), qint64(d.size()));
        QVERIFY2(app.waitForFinished(15000), app.errorString().toUtf8().constData()); output += app.readAll();
        QVERIFY2(app.exitStatus()==QProcess::NormalExit && app.exitCode()==0, output.constData());
        QVERIFY2(output.contains("profile is unassigned"), output.constData());
        LogDatabase db; QVERIFY(db.open(path)); QSqlQuery q(db.connection()); QVERIFY(q.exec("SELECT id,call,IFNULL(station_profile_id,0) FROM qso WHERE call IN ('K1UAA','K1UBB','K1UCC','K1UDD') ORDER BY call"));
        QVERIFY(q.next()); QCOMPARE(q.value(1).toString(), QString("K1UAA")); const auto first=db.record(q.value(0).toLongLong()); QVERIFY(first.has_value());
        QCOMPARE(first->value("MY_GRIDSQUARE"), QString("JM75")); QCOMPARE(first->value("STATION_CALLSIGN"), QString("9H1AA")); QCOMPARE(first->value("OPERATOR"), QString("9H1AA")); QVERIFY(q.value(2).toLongLong()>0);
        QVERIFY(first->value("TX_PWR").isEmpty()); QVERIFY(first->value("MY_RIG").isEmpty()); QVERIFY(first->value("MY_ANTENNA").isEmpty());
        QVERIFY(q.next()); QCOMPARE(q.value(1).toString(), QString("K1UBB")); const auto second=db.record(q.value(0).toLongLong()); QVERIFY(second.has_value());
        QCOMPARE(second->value("MY_GRIDSQUARE"), QString("JN95")); QCOMPARE(second->value("STATION_CALLSIGN"), QString("9H5ZZ")); QCOMPARE(second->value("OPERATOR"), QString("9H5ZZ")); QCOMPARE(q.value(2).toLongLong(), qint64(0));
        QCOMPARE(second->value("TX_PWR"), QString("25")); QCOMPARE(second->value("MY_RIG"), QString("Sender rig"));
        QVERIFY(q.next()); QCOMPARE(q.value(1).toString(), QString("K1UCC")); const auto third=db.record(q.value(0).toLongLong()); QVERIFY(third.has_value());
        QCOMPARE(third->value("STATION_CALLSIGN"), QString("9H5ZZ")); QVERIFY(third->value("OPERATOR").isEmpty()); QCOMPARE(q.value(2).toLongLong(), qint64(0));
        QVERIFY(q.next()); QCOMPARE(q.value(1).toString(), QString("K1UDD")); const auto fourth=db.record(q.value(0).toLongLong()); QVERIFY(fourth.has_value());
        QCOMPARE(fourth->value("MY_GRIDSQUARE"), QString("JN80")); QCOMPARE(fourth->value("STATION_CALLSIGN"), QString("9H1AA"));
        QCOMPARE(fourth->value("OPERATOR"), QString("9H1AA")); QVERIFY(q.value(2).toLongLong()>0);
        QVERIFY(!q.next());
    }
    void activationSiteOverridesProfileButPreservesExplicitGrid()
    {
        using namespace decolog::core;
        QTemporaryDir dir; QVERIFY(dir.isValid()); qint64 firstProfile=0, secondProfile=0;
        {
            LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
            StationProfile a; a.name="Fixture A"; a.stationCallsign="9H1AA"; a.operatorCall="9H1AA"; a.myGridsquare="JN71DC"; a.isDefault=true;
            StationProfile b; b.name="Fixture B"; b.stationCallsign="9H2BB"; b.operatorCall="9H2BB"; b.myGridsquare="JN72AA";
            firstProfile=db.saveStationProfile(a); secondProfile=db.saveStationProfile(b); QVERIFY(firstProfile>0); QVERIFY(secondProfile>0);
        }
        QByteArray output; QVERIFY2(launch(dir, QStringLiteral("activationprofileprobe"), output), output.constData());
        QVERIFY2(output.contains("start= update= errors=,,"), output.constData());
        LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QCOMPARE(db.qsoCount(), 3);
        QSqlQuery q(db.connection()); QVERIFY(q.exec("SELECT id,call,station_profile_id FROM qso WHERE deleted=0 ORDER BY id"));
        const QStringList calls{"K1SAA", "K1SBB", "K1SCC"}, grids{"JM75", "JN70", "JM76"}, operators{"9H1AA", "9H1AA", "9H2BB"};
        QString sessionUuid;
        for (int i=0; i<3; ++i) { QVERIFY(q.next()); QCOMPARE(q.value(1).toString(), calls.at(i));
            QCOMPARE(q.value(2).toLongLong(), i<2 ? firstProfile : secondProfile);
            const auto record = db.record(q.value(0).toLongLong()); QVERIFY(record.has_value());
            QCOMPARE(record->value("MY_GRIDSQUARE"), grids.at(i)); QCOMPARE(record->value("OPERATOR"), operators.at(i));
            QCOMPARE(record->value("MY_POTA_REF"), QString("IT-0001"));
            if (i==0) sessionUuid=record->value("APP_DECOLOG_ACTIVATION_UUID");
            QVERIFY(!sessionUuid.isEmpty()); QCOMPARE(record->value("APP_DECOLOG_ACTIVATION_UUID"), sessionUuid); }
    }
    void potaSnapshotCanBeImportedAndReviewedInDesktop()
    {
        using namespace decolog::core;
        QTemporaryDir dir; QVERIFY(dir.isValid()); QFile fixture(dir.filePath("test.sqlite.pota.csv"));
        QVERIFY(fixture.open(QIODevice::WriteOnly));
        fixture.write("reference,name,active,locationDesc\nUS-0015,Park,1,\"US-DC,US-MD\"\n"); fixture.close();
        {
            LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QSqlQuery q(db.connection());
            QVERIFY(q.exec("INSERT INTO activation_run(uuid,started_at,ended_at,initial_json,last_json) VALUES('pota-fixture','2020-01-01T00:00:00Z','2020-01-01T23:59:00Z','{}','{\"title\":\"POTA fixture\"}')"));
            AdifRecord record{{"CALL", "K1ABC"}, {"QSO_DATE", "20200101"}, {"TIME_ON", "120000"}, {"BAND", "20m"}, {"MODE", "CW"},
                              {"MY_POTA_REF", "US-0015@US-MD"}, {"APP_DECOLOG_ACTIVATION_UUID", "pota-fixture"}};
            QCOMPARE(db.insertQso(record, "manual", {}, true, 0, false).status, InsertResult::Status::Inserted);
        }
        QByteArray output; QVERIFY2(launch(dir, QStringLiteral("potacatalogueprobe"), output), output.constData());
        QVERIFY2(output.contains("PROBE potaImport success=true status=listedActive location=listed name=Park error="), output.constData());
        QVERIFY2(output.contains("PROBE potaGroup status=listedActive location=listed"), output.constData());
        QVERIFY2(output.contains("active in snapshot"), output.constData());
        QVERIFY(QFileInfo::exists(dir.filePath("data/pota-catalogue.sqlite")));
    }
    void sotaOfficialUpdateInDesktopWhenRequested()
    {
        if (qEnvironmentVariable("DECOLOG_SOTA_LIVE_UI") != QLatin1String("1")) QSKIP("Live desktop update not requested");
        QTemporaryDir dir; QVERIFY(dir.isValid()); QByteArray output;
        QVERIFY2(launch(dir, QStringLiteral("sotaupdateprobe"), output, 12000), output.constData());
        QVERIFY2(output.contains("PROBE sotaOnline success=true source=official-https"), output.constData());
        QVERIFY(QFileInfo::exists(dir.filePath("data/sota-catalogue.sqlite")));
    }
    void sotaCatalogueCanBeImportedAndCheckedInDesktop()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); QFile fixture(dir.filePath("test.sqlite.sota.csv"));
        QVERIFY(fixture.open(QIODevice::WriteOnly));
        fixture.write("SOTA Summits List (Date=22/11/2026)\nSummitCode,AssociationName,RegionName,SummitName,AltM,AltFt,GridRef1,GridRef2,Longitude,Latitude,Points,BonusPoints,ValidFrom,ValidTo,ActivationCount,ActivationDate,ActivationCall\nI/LM-001,Italy,Lombardy,Monte Test,100,328,0,0,9,45,2,0,01/01/2020,31/12/2030,0,,\n"); fixture.close();
        {
            using namespace decolog::core;
            LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
            QSqlQuery q(db.connection());
            QVERIFY(q.exec("INSERT INTO activation_run(uuid,started_at,ended_at,initial_json,last_json) VALUES('catalogue-fixture','2026-11-23T00:00:00Z','2026-11-23T23:59:00Z','{}','{\"title\":\"SOTA fixture\"}')"));
            AdifRecord record{{"CALL", "K1ABC"}, {"QSO_DATE", "20261123"}, {"TIME_ON", "120000"}, {"BAND", "20m"}, {"MODE", "CW"},
                              {"MY_SOTA_REF", "I/LM-001"}, {"APP_DECOLOG_ACTIVATION_UUID", "catalogue-fixture"}};
            QCOMPARE(db.insertQso(record, "manual", {}, true, 0, false).status, InsertResult::Status::Inserted);
        }
        QByteArray output; QVERIFY2(launch(dir, QStringLiteral("sotacatalogueprobe"), output), output.constData());
        QVERIFY2(output.contains("PROBE sotaImport success=true status=active name=Monte Test version=2026-11-22 error="), output.constData());
        QVERIFY2(output.contains("PROBE sotaGroup status=active"), output.constData());
        QVERIFY2(output.contains("SOTA list: listed for this day"), output.constData());
        QVERIFY2(output.contains("version=2026-11-22"), output.constData());
        QVERIFY(QFileInfo::exists(dir.filePath("data/sota-catalogue.sqlite")));
    }
    void activationExclusionsAreShownAndPaged()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        {
            using namespace decolog::core;
            LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
            QSqlQuery q(db.connection());
            QVERIFY(q.exec("INSERT INTO activation_run(uuid,started_at,ended_at,initial_json,last_json) VALUES('fixture-session','2026-11-01T00:00:00Z','2026-11-02T00:00:00Z','{}','{\"title\":\"Fixture\"}')"));
            qint64 last = 0;
            for (int i = 0; i < 101; ++i) {
                AdifRecord r{{"CALL", QString("K%1AAA").arg(i)}, {"QSO_DATE", "20261101"}, {"TIME_ON", "120000"},
                             {"BAND", "20m"}, {"MODE", "CW"}, {"APP_DECOLOG_ACTIVATION_UUID", "fixture-session"}};
                const auto inserted = db.insertQso(r, "manual", {}, true, 0, false);
                QCOMPARE(inserted.status, InsertResult::Status::Inserted); last = inserted.id;
            }
            q.prepare("UPDATE qso SET qso_datetime_on='invalid' WHERE id=?"); q.addBindValue(last); QVERIFY(q.exec());
            AdifRecord warning{{"CALL", "K1WARN"}, {"QSO_DATE", "20261101"}, {"TIME_ON", "120100"},
                               {"BAND", "20m"}, {"MODE", "CW"}, {"MY_SOTA_REF", "not-a-summit"},
                               {"APP_DECOLOG_ACTIVATION_UUID", "fixture-session"}};
            QCOMPARE(db.insertQso(warning, "manual", {}, true, 0, false).status, InsertResult::Status::Inserted);
        }
        QByteArray output;
        QVERIFY2(launch(dir, QStringLiteral("exclusionprobe"), output), output.constData());
        QVERIFY2(output.contains("PROBE exclusions total=101 first=K0AAA reason=missingReference"), output.constData());
        QVERIFY2(output.contains("PROBE exclusions next=K100AAA reason=missingDate page=1"), output.constData());
        QVERIFY2(output.contains("PROBE exclusions back=K0AAA page=0"), output.constData());
        QVERIFY2(output.contains("no operator programme reference"), output.constData());
        QVERIFY2(output.contains("missing or invalid UTC date"), output.constData());
        QVERIFY2(output.contains("reference does not match local syntax; verify it"), output.constData());
        QVERIFY2(!output.contains("TypeError: Cannot read property 'length' of undefined"), output.constData());
    }
    void multiProgramActivationKeepsOwnAndCounterpartReferencesDistinct()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QByteArray output;
        QVERIFY2(launch(dir, QStringLiteral("multiprogramprobe"), output), output.constData());
        QVERIFY2(output.contains("PROBE multiInvalid blocked=true"), output.constData());
        QVERIFY2(output.contains("PROBE multiStarted active=true sota=I/LM-001"), output.constData());
        QVERIFY2(output.contains("PROBE multiInvalidUpdate blocked=true sota=I/LM-001"), output.constData());
        QVERIFY2(output.contains("PROBE multiSaved count=2 firstError= secondError="), output.constData());
        QVERIFY2(output.contains("PROBE multiPreview reported=2 groups=6 error="), output.constData());
        QVERIFY2(output.contains("PROBE multiExport started=true"), output.constData());
        QVERIFY2(output.contains("PROBE multiExport step=0 success=true error="), output.constData());
        QVERIFY2(output.contains("PROBE closedRow exists=true"), output.constData());
        QVERIFY2(output.contains("PROBE closedPreview success=true reported=2"), output.constData());
        QVERIFY2(output.contains("PROBE closedExport started=true"), output.constData());
        QVERIFY2(output.contains("PROBE closedExport success=true error="), output.constData());
        QFile closed(dir.filePath("test.sqlite.closed.adi")); QVERIFY(closed.open(QIODevice::ReadOnly));
        QCOMPARE(decolog::core::adif::parse(closed.readAll()).records.size(), 2);
        QFile partition(dir.filePath("test.sqlite.probe.adi")); QVERIFY(partition.open(QIODevice::ReadOnly));
        const auto exported = decolog::core::adif::parse(partition.readAll()).records; QCOMPARE(exported.size(), 2);
        for (const auto& row : exported) { QCOMPARE(row.value("MY_POTA_REF"), QString("IT-0001"));
            QCOMPARE(row.value("MY_SIG"), QString("POTA")); QCOMPARE(row.value("MY_SIG_INFO"), QString("IT-0001")); }
        QVERIFY2(output.contains("PROBE multiExport SOTA started=true"), output.constData());
        QVERIFY2(output.contains("PROBE multiExport step=1 success=true error="), output.constData());
        QVERIFY2(output.contains("PROBE multiExport IOTA started=true"), output.constData());
        QVERIFY2(output.contains("PROBE multiExport step=2 success=true error="), output.constData());
        QFile sota(dir.filePath("test.sqlite.SOTA.adi")); QVERIFY(sota.open(QIODevice::ReadOnly));
        const auto summits = decolog::core::adif::parse(sota.readAll()).records; QCOMPARE(summits.size(), 1);
        QCOMPARE(summits.at(0).value("MY_SOTA_REF"), QString("I/LM-001"));
        QCOMPARE(summits.at(0).value("SOTA_REF"), QString("W2/WE-003"));
        QFile iota(dir.filePath("test.sqlite.IOTA.adi")); QVERIFY(iota.open(QIODevice::ReadOnly));
        const auto islands = decolog::core::adif::parse(iota.readAll()).records; QCOMPARE(islands.size(), 2);
        QCOMPARE(islands.at(0).value("MY_IOTA"), QString("EU-025"));
        decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QCOMPARE(db.qsoCount(), 2);
        QSqlQuery q(db.connection()); QVERIFY(q.exec("SELECT id,call FROM qso WHERE deleted=0 ORDER BY id"));
        QVERIFY(q.next()); QCOMPARE(q.value(1).toString(), QString("K1AAA"));
        const auto one = db.record(q.value(0).toLongLong()); QVERIFY(one.has_value());
        QCOMPARE(one->value("MY_POTA_REF"), QString("IT-0001,IT-0002"));
        QCOMPARE(one->value("MY_SOTA_REF"), QString("I/LM-001"));
        QCOMPARE(one->value("MY_WWFF_REF"), QString("IFF-0123"));
        QCOMPARE(one->value("POTA_REF"), QString("US-1234"));
        QCOMPARE(one->value("SOTA_REF"), QString("W2/WE-003"));
        QCOMPARE(one->value("WWFF_REF"), QString("KFF-4655"));
        QVERIFY(q.next()); QCOMPARE(q.value(1).toString(), QString("K1BBB"));
        const auto two = db.record(q.value(0).toLongLong()); QVERIFY(two.has_value());
        QCOMPARE(two->value("MY_SOTA_REF"), QString("I/LM-002"));
        QCOMPARE(two->value("MY_WWFF_REF"), QString("IFF-0123"));
        QCOMPARE(two->value("MY_SIG"), QString("POTA"));
        QVERIFY(!q.next());
    }
    void historicalEntryRequiresExplicitUtcAndIgnoresCurrentSessions()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        qint64 profileId = 0;
        {
            decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
            decolog::core::StationProfile profile;
            profile.name = "Historical fixture"; profile.stationCallsign = "9H1SR";
            profile.myGridsquare = "JN71DC"; profile.isDefault = true;
            profileId = db.saveStationProfile(profile); QVERIFY(profileId > 0);
        }
        QByteArray output;
        QVERIFY2(launch(dir, QStringLiteral("historicalprobe"), output), output.constData());
        QVERIFY2(output.contains("PROBE historicalBackendMissing blocked=true count=0"), output.constData());
        QVERIFY2(output.contains("PROBE historicalMissingTime count=0 kept=true"), output.constData());
        QVERIFY2(output.contains("PROBE historicalFirst count=1 date=2026-12-31 blankTime=true"), output.constData());
        QVERIFY2(output.contains("PROBE historicalEarlier count=1 blocked=true"), output.constData());
        QVERIFY2(output.contains("PROBE historicalNext count=2 date=2027-01-01"), output.constData());
        QVERIFY2(output.contains("PROBE historicalConfirmed count=3 rover=true activationError="), output.constData());
        QVERIFY2(output.contains("PROBE historicalProfile count=4"), output.constData());
        decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
        QCOMPARE(db.qsoCount(), 4);
        QSqlQuery q(db.connection());
        QVERIFY(q.exec("SELECT id,call,qso_datetime_on,station_profile_id FROM qso WHERE deleted=0 ORDER BY id"));
        const QStringList expectedCalls{"K1AAA", "K1BBB", "K1CCC"};
        const QStringList expectedDates{"2026-12-31T23:58:00Z", "2027-01-01T00:05:00Z", "2027-01-01T00:03:00Z"};
        for (int i = 0; i < 3; ++i) {
            QVERIFY(q.next()); QCOMPARE(q.value(1).toString(), expectedCalls.at(i));
            QCOMPARE(q.value(2).toString(), expectedDates.at(i)); QVERIFY(q.value(3).isNull());
            const auto record = db.record(q.value(0).toLongLong()); QVERIFY(record.has_value());
            QCOMPARE(record->value("APP_DECOLOG_ENTRY_CONTEXT"), QString("HISTORICAL"));
            QVERIFY(record->value("MY_GRIDSQUARE").isEmpty());
            QVERIFY(record->value("POTA_REF").isEmpty());
            QVERIFY(record->value("RST_SENT").isEmpty());
            QVERIFY(record->value("RST_RCVD").isEmpty());
        }
        QVERIFY(q.next()); QCOMPARE(q.value(1).toString(), QString("K1DDD"));
        QCOMPARE(q.value(2).toString(), QString("2027-01-01T00:04:00Z")); QCOMPARE(q.value(3).toLongLong(), profileId);
        const auto withProfile = db.record(q.value(0).toLongLong()); QVERIFY(withProfile.has_value());
        QCOMPARE(withProfile->value("MY_GRIDSQUARE"), QString("JN71DC"));
        QCOMPARE(withProfile->value("STATION_CALLSIGN"), QString("9H1SR"));
        QVERIFY(!q.next());
        const QByteArray adif = db.exportAdif();
        const auto exported = decolog::core::adif::parse(adif);
        QCOMPARE(exported.records.size(), 4);
        for (const auto &record : exported.records)
            QCOMPARE(record.value("APP_DECOLOG_ENTRY_CONTEXT"), QString("HISTORICAL"));
        decolog::core::LogDatabase imported; QVERIFY(imported.open(":memory:"));
        QCOMPARE(imported.importAdif(adif).inserted, 4);
        const auto roundtrip = decolog::core::adif::parse(imported.exportAdif());
        QCOMPARE(roundtrip.records.size(), 4);
        for (const auto &record : roundtrip.records)
            QCOMPARE(record.value("APP_DECOLOG_ENTRY_CONTEXT"), QString("HISTORICAL"));
    }
    void qslOcrDialogReadsImageWithoutChangingQso()
    {
        decolog::core::TesseractOcr engine;
        if (engine.executable().isEmpty()) QSKIP("Tesseract is not installed");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("card.png");
        const QString fixture = QFINDTESTDATA("fixtures/qsl-ocr-synthetic.png");
        QVERIFY(!fixture.isEmpty());
        QVERIFY(QFile::copy(fixture, path));
        {
            decolog::core::LogDatabase db;
            QVERIFY(db.open(dir.filePath("test.sqlite")));
            decolog::core::AdifRecord record{{"CALL", "K1ABC"}, {"QSO_DATE", "20260923"},
                                             {"TIME_ON", "143000"}, {"BAND", "20m"}, {"MODE", "SSB"}};
            QCOMPARE(db.insertQso(record, "manual", {}, true, 0, false).status, decolog::core::InsertResult::Status::Inserted);
            record.set("CALL", "K1ABO");
            QCOMPARE(db.insertQso(record, "manual", {}, true, 0, false).status, decolog::core::InsertResult::Status::Inserted);
        }
        QByteArray output;
        QVERIFY2(launch(dir, QStringLiteral("qslocrmatches:") + path, output), output.constData());
        QVERIFY2(output.contains("PROBE qslOcr success=true hasCall=true count=2"), output.constData());
        QVERIFY2(output.contains("PROBE qslMatches success=true count=1 first=K1ABC"), output.constData());
        decolog::core::LogDatabase db;
        QVERIFY(db.open(dir.filePath("test.sqlite")));
        QCOMPARE(db.qsoCount(), 2);
        QVERIFY(db.qslAttachments(1).isEmpty());
        QCOMPARE(db.record(1)->value("CALL"), QString("K1ABC"));
    }
    void qslOcrDialogShowsNoExactMatch()
    {
        decolog::core::TesseractOcr engine;
        if (engine.executable().isEmpty()) QSKIP("Tesseract is not installed");
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString path = dir.filePath("card.png");
        QVERIFY(QFile::copy(QFINDTESTDATA("fixtures/qsl-ocr-synthetic.png"), path));
        {
            decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
            decolog::core::AdifRecord record{{"CALL", "K1ABO"}, {"QSO_DATE", "20260923"},
                                             {"TIME_ON", "143000"}, {"BAND", "20m"}, {"MODE", "SSB"}};
            QCOMPARE(db.insertQso(record, "manual", {}, true, 0, false).status, decolog::core::InsertResult::Status::Inserted);
        }
        QByteArray output;
        QVERIFY2(launch(dir, QStringLiteral("qslocrmatches:") + path, output), output.constData());
        QVERIFY2(output.contains("PROBE qslMatches success=true count=0 first= noMatchNotice=true"), output.constData());
    }
    void qslOcrExplicitlyAttachesSnapshotToTwoQsos()
    {
        decolog::core::TesseractOcr engine;
        if (engine.executable().isEmpty()) QSKIP("Tesseract is not installed");
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString path = dir.filePath("card.png");
        QVERIFY(QFile::copy(QFINDTESTDATA("fixtures/qsl-ocr-synthetic.png"), path));
        QFile original(path); QVERIFY(original.open(QIODevice::ReadOnly)); const QByteArray bytes = original.readAll(); original.close();
        {
            decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
            for (const QString &time : {"143000", "153000"}) {
                decolog::core::AdifRecord record{{"CALL", "K1ABC"}, {"QSO_DATE", "20260923"},
                                                 {"TIME_ON", time}, {"BAND", "20m"}, {"MODE", "SSB"}};
                QCOMPARE(db.insertQso(record, "manual", {}, true, 0, false).status, decolog::core::InsertResult::Status::Inserted);
            }
        }
        QByteArray output;
        QVERIFY2(launch(dir, QStringLiteral("qslocrattach:") + path, output), output.constData());
        QVERIFY2(output.contains("PROBE qslMatches success=true count=2 first=K1ABC"), output.constData());
        QVERIFY2(output.contains("PROBE qslAcquire success=true selected=2 newLinks=2"), output.constData());
        decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
        QCOMPARE(db.qsoCount(), 2);
        const auto first = db.qslAttachments(1), second = db.qslAttachments(2);
        QCOMPARE(first.size(), 1); QCOMPARE(second.size(), 1);
        QCOMPARE(first.first().id, second.first().id);
        QCOMPARE(db.qslAttachmentData(first.first().id), bytes);
        QSqlQuery journal(db.connection()); QVERIFY(journal.exec("SELECT ocr_text FROM qsl_ocr_acquisition"));
        QVERIFY(journal.next()); QVERIFY(journal.value(0).toString().contains("K1ABC")); QVERIFY(!journal.next());
        QVERIFY(db.record(1)->value("QSL_RCVD").isEmpty());
    }
    void qslOcrHistoryUndoPreservesPreviouslyExistingLink()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString path = QFINDTESTDATA("fixtures/qsl-ocr-synthetic.png"); QVERIFY(!path.isEmpty());
        QFile image(path); QVERIFY(image.open(QIODevice::ReadOnly)); const QByteArray bytes = image.readAll();
        const QString hash = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        qint64 attachment = 0;
        {
            decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
            QVariantList selected;
            for (const QString &call : {"K1ABC", "K1DEF"}) {
                decolog::core::AdifRecord record{{"CALL", call}, {"QSO_DATE", "20260923"}, {"TIME_ON", "143000"}, {"BAND", "20m"}, {"MODE", "SSB"}};
                const auto inserted = db.insertQso(record, "manual", {}, true, 0, false);
                QCOMPARE(inserted.status, decolog::core::InsertResult::Status::Inserted);
                QSqlQuery q(db.connection()); q.prepare("SELECT uuid,revision FROM qso WHERE id=?"); q.addBindValue(inserted.id); QVERIFY(q.exec()); QVERIFY(q.next());
                selected << QVariantMap{{"id", inserted.id}, {"uuid", q.value(0)}, {"revision", q.value(1)}};
            }
            attachment = db.addQslAttachment(bytes, "card.png", "image/png", {1}); QVERIFY(attachment > 0);
            std::atomic_bool cancelled{false};
            const auto result = decolog::core::QslOcrAcquisition::execute(db.path(), path, hash, "card.png", "K1ABC K1DEF", selected, &cancelled);
            QVERIFY2(result.value("ok").toBool(), qPrintable(result.value("error").toString()));
            QCOMPARE(result.value("newLinks").toInt(), 1);
        }
        QByteArray output;
        QVERIFY2(launch(dir, QStringLiteral("qslocrundo"), output), output.constData());
        QVERIFY2(output.contains("PROBE qslUndo success=true removed=1"), output.constData());
        decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
        QCOMPARE(db.qsoCount(), 2); QCOMPARE(db.qslAttachments(1).size(), 1); QVERIFY(db.qslAttachments(2).isEmpty());
        QCOMPARE(db.qslAttachmentData(attachment), bytes);
        QSqlQuery q(db.connection()); QVERIFY(q.exec("SELECT undone_at FROM qsl_ocr_acquisition")); QVERIFY(q.next()); QVERIFY(!q.value(0).isNull());
    }
    void roverManualQsoRespectsHistoryAndExplicitOverride()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QByteArray output;
        QVERIFY2(launch(dir, QStringLiteral("roverprobe"), output), output.constData());
        QVERIFY2(output.contains("PROBE roverStarted=true grid=JN71DC"), output.constData());
        QVERIFY2(output.contains("PROBE roverFirst count=1 closed=true"), output.constData());
        QVERIFY2(output.contains("PROBE roverAmbiguous kept=true count=1"), output.constData());
        QVERIFY2(output.contains("PROBE roverOverride closed=true count=2"), output.constData());
        QVERIFY2(output.contains("PROBE roverActivation error= kept=true count=2"), output.constData());
        QVERIFY2(output.contains("PROBE roverActivationOverride closed=true count=3"), output.constData());
        decolog::core::LogDatabase db;
        QVERIFY(db.open(dir.filePath(QStringLiteral("test.sqlite"))));
        QCOMPARE(db.qsoCount(), 3);
        QSqlQuery q(db.connection());
        QVERIFY(q.exec("SELECT id,call FROM qso WHERE deleted=0 ORDER BY id"));
        QVERIFY(q.next()); QCOMPARE(q.value(1).toString(), QStringLiteral("K1AAA")); QCOMPARE(db.record(q.value(0).toLongLong())->value("MY_GRIDSQUARE"), QStringLiteral("JN71DC"));
        QVERIFY(q.next()); QCOMPARE(q.value(1).toString(), QStringLiteral("K1BBB")); QCOMPARE(db.record(q.value(0).toLongLong())->value("MY_GRIDSQUARE"), QStringLiteral("JM67AB"));
        QVERIFY(q.next()); QCOMPARE(q.value(1).toString(), QStringLiteral("K1CCC")); QCOMPARE(db.record(q.value(0).toLongLong())->value("MY_GRIDSQUARE"), QStringLiteral("JM67AB"));
        QVERIFY(!q.next());
        const auto exported = db.exportAdif();
        const auto parsed = decolog::core::adif::parse(exported);
        QCOMPARE(parsed.records.size(), 3);
        QCOMPARE(parsed.records.at(0).value("MY_GRIDSQUARE"), QStringLiteral("JN71DC"));
        QCOMPARE(parsed.records.at(1).value("MY_GRIDSQUARE"), QStringLiteral("JM67AB"));
        QCOMPARE(parsed.records.at(2).value("MY_GRIDSQUARE"), QStringLiteral("JM67AB"));
        decolog::core::LogDatabase imported;
        QVERIFY(imported.open(":memory:"));
        QCOMPARE(imported.importAdif(exported).inserted, 3);
        const auto roundtrip = decolog::core::adif::parse(imported.exportAdif());
        QCOMPARE(roundtrip.records.size(), 3);
        QCOMPARE(roundtrip.records.at(0).value("MY_GRIDSQUARE"), QStringLiteral("JN71DC"));
        QCOMPARE(roundtrip.records.at(1).value("MY_GRIDSQUARE"), QStringLiteral("JM67AB"));
        QCOMPARE(roundtrip.records.at(2).value("MY_GRIDSQUARE"), QStringLiteral("JM67AB"));
        QSqlQuery history(db.connection());
        QVERIFY(history.exec("SELECT COUNT(*) FROM rover_position")); QVERIFY(history.next()); QCOMPARE(history.value(0).toInt(), 2);
    }
    void vuccBoundaryPreviewInApplication()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        {
            decolog::core::LogDatabase db;
            QVERIFY(db.open(dir.filePath(QStringLiteral("test.sqlite"))));
            const QByteArray input = "<CALL:5>K1AAA<QSO_DATE:8>20260101<TIME_ON:4>1200<BAND:2>6m<MODE:2>CW"
                                     "<QSL_RCVD:1>Y<GRIDSQUARE:4>EM98<VUCC_GRIDS:19>EM98,FM08,EM97,FM07<EOR>";
            QCOMPARE(db.importAdif(input).inserted, 1);
        }
        QByteArray output;
        QVERIFY2(launch(dir, QStringLiteral("vuccboundary"), output), output.constData());
        const QByteArray marker = "PROBE vuccBoundary ";
        const int start = output.indexOf(marker);
        QVERIFY2(start >= 0, output.constData());
        const int end = output.indexOf('\n', start);
        const auto document = QJsonDocument::fromJson(output.mid(start + marker.size(), end - start - marker.size()));
        QVERIFY(!document.isNull());
        QCOMPARE(document.object().value("eligible").toArray().size(), 4);
        QVERIFY(!document.object().value("officialRulesVerified").toBool());
        decolog::core::LogDatabase db;
        QVERIFY(db.open(dir.filePath(QStringLiteral("test.sqlite"))));
        QCOMPARE(db.qsoCount(), 1);
    }

    void exportTrackerQsoFixture()
    {
        const QString path = qEnvironmentVariable("DECOLOG_TEST_SATELLITE_DB");
        if (path.isEmpty())
            QSKIP("Set DECOLOG_TEST_SATELLITE_DB to verify a saved tracker QSO");
        decolog::core::LogDatabase source;
        QVERIFY(source.open(path));
        const QByteArray exported = source.exportAdif("fixture");
        const auto document = decolog::core::adif::parse(exported);
        QCOMPARE(document.records.size(), 1);
        const auto &qso = document.records.first();
        QCOMPARE(qso.value("CALL"), QStringLiteral("K1AAA"));
        QCOMPARE(qso.value("FREQ"), QStringLiteral("145.998596"));
        QCOMPARE(qso.value("FREQ_RX"), QStringLiteral("435.004184"));
        QCOMPARE(qso.value("BAND"), QStringLiteral("2m"));
        QCOMPARE(qso.value("BAND_RX"), QStringLiteral("70cm"));
        QCOMPARE(qso.value("PROP_MODE"), QStringLiteral("SAT"));
        QCOMPARE(qso.value("SAT_NAME"), QStringLiteral("OSCAR-7"));
        QCOMPARE(qso.value("SAT_MODE"), QStringLiteral("V/U"));
        QCOMPARE(qso.value("MODE"), qEnvironmentVariable("DECOLOG_TEST_SATELLITE_MODE"));
        decolog::core::LogDatabase imported;
        QVERIFY(imported.open(":memory:"));
        QCOMPARE(imported.importAdif(exported).inserted, 1);
        const auto roundtrip = decolog::core::adif::parse(imported.exportAdif());
        QCOMPARE(roundtrip.records.size(), 1);
        QCOMPARE(roundtrip.records.first().value("FREQ_RX"), QStringLiteral("435.004184"));
        QCOMPARE(roundtrip.records.first().value("SAT_NAME"), QStringLiteral("OSCAR-7"));
        QCOMPARE(roundtrip.records.first().value("SAT_MODE"), QStringLiteral("V/U"));
    }

    void manualSatelliteSplitPersistsBothFrequencies()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QByteArray output;
        QVERIFY2(launch(dir, QStringLiteral("satellitesplit"), output), output.constData());
        QVERIFY2(output.contains("PROBE satelliteInvalid kept=true"), output.constData());
        QVERIFY2(output.contains("PROBE satelliteSaved closed=true"), output.constData());
        decolog::core::LogDatabase db;
        QVERIFY(db.open(dir.filePath(QStringLiteral("test.sqlite"))));
        QCOMPARE(db.qsoCount(), 1);
        QSqlQuery q(db.connection());
        QVERIFY(q.exec("SELECT freq,freq_rx,band,band_rx,sat_name,sat_mode,prop_mode FROM qso WHERE deleted=0"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toDouble(), 145.9);
        QCOMPARE(q.value(1).toDouble(), 435.25);
        QCOMPARE(q.value(2).toString(), QStringLiteral("2m"));
        QCOMPARE(q.value(3).toString(), QStringLiteral("70cm"));
        QCOMPARE(q.value(4).toString(), QStringLiteral("FO-29"));
        QCOMPARE(q.value(5).toString(), QStringLiteral("U/V"));
        QCOMPARE(q.value(6).toString(), QStringLiteral("SAT"));
        const QByteArray adif = db.exportAdif().toLower();
        QVERIFY(adif.contains("<freq_rx:10>435.250000"));
        QVERIFY(adif.contains("<band_rx:4>70cm"));
    }

    void radioNetReminderShowsWhenDue()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QByteArray output;
        QVERIFY2(launch(dir, QStringLiteral("radionet:reminder"), output), output.constData());
        QVERIFY2(output.contains("PROBE reminderScheduled=true"), output.constData());
        QVERIFY2(output.contains("PROBE reminderDue=true"), output.constData());
        decolog::core::LogDatabase db;
        QVERIFY(db.open(dir.filePath(QStringLiteral("test.sqlite"))));
        decolog::core::RadioNet net(&db);
        net.load();
        QVERIFY(net.reminder().value("scheduled").toBool());
        QVERIFY(net.reminder().value("due").toBool());
        QCOMPARE(db.qsoCount(), 0);
    }

    void radioNetNavigatesCompleteHistory()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        {
            decolog::core::LogDatabase db;
            QVERIFY(db.open(dir.filePath(QStringLiteral("test.sqlite"))));
            decolog::core::RadioNet net(&db);
            QVERIFY(net.start("History"));
            QVERIFY(net.checkIn("K1AAA", "Alice", ""));
            const qint64 id = net.members().first().toMap().value("id").toLongLong();
            for (int i = 0; i < 200; ++i)
                QVERIFY(net.setStatus(id, i % 2 ? "present" : "waiting"));
        }
        QByteArray output;
        QVERIFY2(launch(dir, QStringLiteral("radionet:history"), output), output.constData());
        QVERIFY2(output.contains("PROBE historyFirst count=200 older=true"), output.constData());
        QVERIFY2(output.contains("PROBE historyOlder count=2 newer=true older=false"), output.constData());
        QVERIFY2(output.contains("PROBE historyReturn count=200 newer=false"), output.constData());
        decolog::core::LogDatabase db;
        QVERIFY(db.open(dir.filePath(QStringLiteral("test.sqlite"))));
        QCOMPARE(db.qsoCount(), 0);
    }

    void radioNetExplicitSaveAndRepeatQso()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QByteArray output;
        QVERIFY2(launch(dir, QStringLiteral("radionet:save"), output), output.constData());
        QVERIFY2(output.contains("PROBE radioNetInvalid kept=true"), output.constData());
        QVERIFY2(output.contains("PROBE radioNetMismatch kept=true"), output.constData());
        QVERIFY2(output.contains("PROBE radioNetSaved closed=true"), output.constData());
        QVERIFY2(output.contains("PROBE radioNetRepeat closed=true"), output.constData());
        QVERIFY2(output.contains("PROBE radioNetReported count=2"), output.constData());
        QVERIFY2(output.contains("PROBE radioNetClosed kept=true"), output.constData());
        decolog::core::LogDatabase db;
        QVERIFY(db.open(dir.filePath(QStringLiteral("test.sqlite"))));
        QCOMPARE(db.qsoCount(), 2);
        QSqlQuery q(db.connection());
        QSqlQuery session(db.connection());
        QVERIFY(session.exec("SELECT uuid FROM radio_net_session"));
        QVERIFY(session.next());
        const QString sessionUuid = session.value(0).toString();
        QVERIFY(!sessionUuid.isEmpty());
        QVERIFY(q.exec("SELECT call,name,band,mode,id FROM qso WHERE deleted=0 ORDER BY id"));
        for (int i = 0; i < 2; ++i) {
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toString(), QStringLiteral("K1AAA"));
            QCOMPARE(q.value(1).toString(), QStringLiteral("Alice"));
            QCOMPARE(q.value(2).toString(), QStringLiteral("20m"));
            QCOMPARE(q.value(3).toString(), QStringLiteral("CW"));
            const auto record = db.record(q.value(4).toLongLong());
            QVERIFY(record.has_value());
            QCOMPARE(record->value("APP_DECOLOG_RADIO_NET_SESSION"), sessionUuid);
            QCOMPARE(record->value("APP_DECOLOG_RADIO_NET_CALL"), QStringLiteral("K1AAA"));
        }
        QVERIFY(!q.next());
        const auto exported = db.exportAdif();
        const auto originalRecords = decolog::core::adif::parse(exported).records;
        QCOMPARE(originalRecords.size(), 2);
        for (const auto &record : originalRecords) {
            QCOMPARE(record.value("APP_DECOLOG_RADIO_NET_SESSION"), sessionUuid);
            QCOMPARE(record.value("APP_DECOLOG_RADIO_NET_CALL"), QStringLiteral("K1AAA"));
        }
        decolog::core::LogDatabase imported;
        QVERIFY(imported.open(":memory:"));
        QCOMPARE(imported.importAdif(exported).inserted, 1); // identical contacts are deduplicated on import
        const auto records = decolog::core::adif::parse(imported.exportAdif()).records;
        QCOMPARE(records.size(), 1);
        for (const auto &record : records) {
            QCOMPARE(record.value("APP_DECOLOG_RADIO_NET_SESSION"), sessionUuid);
            QCOMPARE(record.value("APP_DECOLOG_RADIO_NET_CALL"), QStringLiteral("K1AAA"));
        }
    }

    void radioNetNavigatesReportedQsoHistory()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        {
            decolog::core::LogDatabase db;
            QVERIFY(db.open(dir.filePath("test.sqlite")));
            decolog::core::RadioNet net(&db);
            QVERIFY(net.start("QSO history"));
            QSqlQuery q(db.connection());
            QVERIFY(q.exec("SELECT uuid FROM radio_net_session"));
            QVERIFY(q.next());
            const QString uuid = q.value(0).toString();
            for (int i = 0; i < 202; ++i) {
                decolog::core::AdifRecord r{{"CALL", "K1AAA"}, {"QSO_DATE", "20260101"}, {"TIME_ON", "120000"},
                                             {"BAND", "20m"}, {"MODE", "CW"}, {"APP_DECOLOG_RADIO_NET_SESSION", uuid}};
                QCOMPARE(db.insertQso(r, "manual", {}, true, 0, false).status, decolog::core::InsertResult::Status::Inserted);
            }
        }
        QByteArray output;
        QVERIFY2(launch(dir, "radionet:qsohistory", output), output.constData());
        QVERIFY2(output.contains("PROBE qsoHistoryFirst count=200 older=true"), output.constData());
        QVERIFY2(output.contains("PROBE qsoHistoryOlder count=2 newer=true older=false"), output.constData());
        QVERIFY2(output.contains("PROBE qsoHistoryReturn count=200 newer=false"), output.constData());
    }

    void radioNetPreparesQsoWithoutSavingOrOverwritingDraft()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QByteArray output;
        QVERIFY2(launch(dir, QStringLiteral("radionet:qso"), output), output.constData());
        QVERIFY2(output.contains("PROBE radioNetQso visible=true call=K1AAA"), output.constData());
        QVERIFY2(output.contains("PROBE radioNetDraft preserved=true notice=true"), output.constData());
        QVERIFY2(output.contains("PROBE radioNetSummary participants=2 present=2 checkIns=2"), output.constData());
        decolog::core::LogDatabase db;
        QVERIFY(db.open(dir.filePath(QStringLiteral("test.sqlite"))));
        QCOMPARE(db.qsoCount(), 0);
        QSqlQuery q(db.connection());
        QVERIFY(q.exec("SELECT COUNT(*) FROM radio_net_member"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 2);
    }

    void radioNetRosterUiAndRestartDoNotCreateQsos()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QByteArray output;
        QVERIFY2(launch(dir, QStringLiteral("radionet:seed"), output), output.constData());
        QVERIFY2(output.contains("PROBE radioNet visible=true members=1 first=K1AAA"), output.constData());
        {
            decolog::core::LogDatabase db;
            QVERIFY(db.open(dir.filePath(QStringLiteral("test.sqlite"))));
            QCOMPARE(db.qsoCount(), 0);
            QSqlQuery q(db.connection());
            QVERIFY(q.exec("SELECT call,name,status FROM radio_net_member"));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toString(), QStringLiteral("K1AAA"));
            QCOMPARE(q.value(1).toString(), QStringLiteral("Alice"));
            QCOMPARE(q.value(2).toString(), QStringLiteral("present"));
        }
        QVERIFY2(launch(dir, QStringLiteral("radionet:resume"), output), output.constData());
        QVERIFY2(output.contains("PROBE radioNet visible=true members=1 first=K1AAA"), output.constData());
    }

    void importUndoDialogAcceptAndRejectAcrossRestart()
    {
        for (const bool accept : {false, true}) {
            QTemporaryDir dir; QVERIFY(dir.isValid());
            qint64 importedId = 0, changedId = 0, independentId = 0;
            {
                decolog::core::LogDatabase db;
                QVERIFY(db.open(dir.filePath(QStringLiteral("test.sqlite"))));
                const auto imported = db.importAdif(
                    "<CALL:5>K1AAA<QSO_DATE:8>20260101<TIME_ON:4>0900<BAND:3>20m<MODE:2>CW<EOR>"
                    "<CALL:5>K1CCC<QSO_DATE:8>20260101<TIME_ON:4>0930<BAND:3>20m<MODE:2>CW<EOR>");
                QCOMPARE(imported.inserted, 2);
                importedId = imported.insertedIds.first();
                changedId = imported.insertedIds.last();
                auto changed = db.record(changedId);
                QVERIFY(changed.has_value());
                changed->set(QStringLiteral("COMMENT"), QStringLiteral("review me"));
                QCOMPARE(db.updateQso(changedId, *changed).status, decolog::core::InsertResult::Status::Inserted);
                independentId = db.insertQso({{"CALL", "K1BBB"}, {"QSO_DATE", "20260101"},
                    {"TIME_ON", "1000"}, {"BAND", "20m"}, {"MODE", "CW"}}, "radio").id;
                QVERIFY(independentId > 0);
            }
            QByteArray output;
            QVERIFY2(launch(dir, accept ? QStringLiteral("undoimport") : QStringLiteral("undoimport:cancel"), output), output.constData());
            QVERIFY2(output.contains("PROBE importUndoPreview"), output.constData());
            QVERIFY2(output.contains("\"eligible\":1"), output.constData());
            QVERIFY2(output.contains("\"changed\":1"), output.constData());
            if (accept) {
                QVERIFY2(output.contains("PROBE importUndo "), output.constData());
                QVERIFY2(output.contains("\"changed\":1"), output.constData());
                QVERIFY2(output.contains("\"qsoId\":" + QByteArray::number(changedId)), output.constData());
                QVERIFY2(output.contains("PROBE importUndoView visible=true row=QSO "), output.constData());
                QVERIFY2(output.contains("K1CCC"), output.constData());
            } else {
                QVERIFY(!output.contains("PROBE importUndo "));
            }
            decolog::core::LogDatabase reopened;
            QVERIFY(reopened.open(dir.filePath(QStringLiteral("test.sqlite"))));
            QCOMPARE(reopened.meta(importedId)->deleted, accept);
            QVERIFY(!reopened.meta(changedId)->deleted);
            QVERIFY(!reopened.meta(independentId)->deleted);
        }
    }

    void migratesLegacyLayoutAndPersistsActualHeaderDrag()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        seed(dir, QStringLiteral("call,band"), false);
        QByteArray output;
        QVERIFY2(launch(dir, QStringLiteral("headermove:main:2:0"), output), output.constData());
        QVERIFY2(output.contains("PROBE headermove main 2->0"), output.constData());
        QSettings first(iniPath(dir), QSettings::IniFormat);
        QVERIFY(first.value(QStringLiteral("layout/journeyColumnMigrated")).toBool());
        QCOMPARE(first.value(QStringLiteral("layout/columnLayout")).toString(), QStringLiteral("journey,call,band"));

        QVERIFY2(launch(dir, QStringLiteral("headermove:main:0:2"), output), output.constData());
        QVERIFY2(output.contains("PROBE headermove main 0->2"), output.constData());
        first.sync();
        QCOMPARE(first.value(QStringLiteral("layout/columnLayout")).toString(), QStringLiteral("call,band,journey"));
    }

    void intentionallyHiddenColumnStaysHiddenAfterRestart()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        seed(dir, QStringLiteral("call,band"), true);
        QByteArray output;
        QVERIFY2(launch(dir, QStringLiteral("headermove:main:0:1"), output), output.constData());
        QVERIFY2(output.contains("PROBE headermove main 0->1"), output.constData());
        QSettings after(iniPath(dir), QSettings::IniFormat);
        QCOMPARE(after.value(QStringLiteral("layout/columnLayout")).toString(), QStringLiteral("band,call"));
    }

    void mapButtonUsesCorrectQsoAfterColumnMove()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        seed(dir, QStringLiteral("call,band"), false);
        qint64 older = 0, newer = 0;
        {
            decolog::core::LogDatabase db;
            QVERIFY(db.open(dir.filePath(QStringLiteral("test.sqlite"))));
            older = db.insertQso({{"CALL", "K1AAA"}, {"QSO_DATE", "20261118"}, {"TIME_ON", "1200"},
                                   {"BAND", "20m"}, {"MODE", "FT8"}}, "test").id;
            newer = db.insertQso({{"CALL", "K1BBB"}, {"QSO_DATE", "20261118"}, {"TIME_ON", "1201"},
                                   {"BAND", "20m"}, {"MODE", "FT8"}}, "test").id;
            QVERIFY(older > 0); QVERIFY(newer > 0);
        }
        QByteArray output;
        QVERIFY2(launch(dir, QStringLiteral("headermove:main:2:0"), output), output.constData());
        QSettings after(iniPath(dir), QSettings::IniFormat);
        QCOMPARE(after.value(QStringLiteral("layout/columnLayout")).toString(), QStringLiteral("journey,call,band"));
        QVERIFY2(launch(dir, QStringLiteral("journeyclick:0"), output), output.constData());
        const QByteArray first = "PROBE journeyclick expected=" + QByteArray::number(newer)
                                 + " opened=" + QByteArray::number(newer);
        QVERIFY2(output.contains(first), output.constData());
        QVERIFY2(launch(dir, QStringLiteral("journeyclick:1"), output), output.constData());
        const QByteArray second = "PROBE journeyclick expected=" + QByteArray::number(older)
                                  + " opened=" + QByteArray::number(older);
        QVERIFY2(output.contains(second), output.constData());
    }
};

QTEST_GUILESS_MAIN(TestJourneyColumn)
#include "tst_journeycolumn.moc"
