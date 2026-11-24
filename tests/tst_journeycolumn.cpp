#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include "core/LogDatabase.h"
#include "core/ContestNet.h"
#include "core/ContestHandoff.h"
#include "core/ContestNetSecure.h"
#include "core/ContestNetQsoTransfer.h"
#include "core/CqMarathon.h"
#include "core/Activation.h"
#include "core/ContestAudioFile.h"
#include "core/QslPlanner.h"
#include "core/DxCampaign.h"
#include "core/ActivityReport.h"
#include "core/NewQsoFields.h"
#include "core/WsjtxProtocol.h"
#include <QUdpSocket>
#include <QElapsedTimer>
#include "core/RadioNet.h"
#include "core/RoverSession.h"
#include "core/TesseractOcr.h"
#include "core/QslOcrAcquisition.h"
#include <QCryptographicHash>
#include "core/FileIdentity.h"
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif
#include <algorithm>
#include <atomic>
#include <thread>

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
    static bool launch(const QTemporaryDir& dir, const QString& probe, QByteArray& output, int grabMs = 2800, bool traceActivity = false)
    {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        env.insert(QStringLiteral("QT_QUICK_BACKEND"), QStringLiteral("software"));
        env.insert(QStringLiteral("DECODXLOG_DATA_ROOT"), dir.filePath(QStringLiteral("data")));
        env.insert(QStringLiteral("DECODXLOG_GRAB_MS"), QString::number(grabMs));
        if (traceActivity) env.insert(QStringLiteral("DECODXLOG_TRACE_ACTIVITY"),QStringLiteral("1"));
        if (probe.startsWith(QStringLiteral("netreceive")) || probe==QLatin1String("nethandoffprobe")) env.insert(QStringLiteral("DECODXLOG_TEST_LOOPBACK"),QStringLiteral("1"));
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
    void initTestCase()
    {
#ifdef Q_OS_WIN
        qputenv("DECODXLOG_TEST_UTF8_LOG","1");
#endif
    }
    void stationNetworkReceivedQsoReview_data()
    {
        QTest::addColumn<bool>("accept"); QTest::addColumn<QString>("kind"); QTest::addColumn<bool>("secure");
        QTest::newRow("reject insert") << false << QStringLiteral("insert") << true; QTest::newRow("accept insert") << true << QStringLiteral("insert") << true;
        QTest::newRow("reject edit") << false << QStringLiteral("edit") << true; QTest::newRow("accept edit") << true << QStringLiteral("edit") << true;
        QTest::newRow("reject delete") << false << QStringLiteral("delete") << true; QTest::newRow("accept delete") << true << QStringLiteral("delete") << true;
        QTest::newRow("reject clear insert") << false << QStringLiteral("insert") << false; QTest::newRow("accept clear insert") << true << QStringLiteral("insert") << false;
    }
    void stationNetworkReceivedQsoReview()
    {
        using namespace decolog::core; QFETCH(bool,accept); QFETCH(QString,kind); QFETCH(bool,secure); QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        QUdpSocket probe; QVERIFY(probe.bind(QHostAddress::LocalHost,0)); const auto port=probe.localPort(); probe.close();
        { QSettings settings(iniPath(dir),QSettings::IniFormat); settings.setValue("net/enabled",false); settings.setValue("net/id","receiver"); settings.setValue("net/group","FIELD"); settings.setValue("net/port",port); settings.setValue("ui/language",accept?"it":"en"); settings.sync(); }
        ContestNetInventoryRow row; QString logIdentity; qint64 localId=0;
        AdifRecord record{{"CALL","K1AAA"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"COMMENT","network review"},{"NAME","Remote"}};
        { LogDatabase source; const auto path=dir.filePath("source.sqlite"); QVERIFY(source.open(path)); const auto inserted=source.insertQso(record,"fixture"); QVERIFY(inserted.id>0);
          if (kind=="delete") QVERIFY(source.softDeleteQso(inserted.id));
          ContestNetInventory inv; QVERIFY(inv.begin(path)); const auto page=inv.next(1); QVERIFY(page.ok && page.rows.size()==1); row=page.rows.first(); logIdentity=inv.logIdentity(); }
        if (kind!="insert") { LogDatabase local; QVERIFY(local.open(dir.filePath("test.sqlite"))); record.set("NAME","Local"); const auto inserted=local.insertQso(record,"fixture"); QVERIFY(inserted.id>0); localId=inserted.id; QVERIFY(local.adoptUuid(localId,row.uuid)); }
        std::atomic_bool stop{false}; std::thread sender([&] {
            QUdpSocket socket; contestqso::Encoder encoder("test-transfer",logIdentity,row,secure?256:512); const auto key=QByteArray::fromHex(QByteArray(64,'a')); qint64 seq=0;
            while (!stop.load()) { for (int i=0;i<encoder.count();++i) {
                auto frame=encoder.frame(i); frame.insert("to","receiver"); QString type=QStringLiteral("qso_chunk_v1");
                if (secure) { const auto sealed=contestsecure::seal(key,"FIELD","sender","receiver",type,frame); if (!sealed.ok) continue; frame=sealed.frame; type=QStringLiteral("qso_secure_v1"); }
                frame.insert("seq",++seq); frame.insert("session","app-test"); const auto packet=ContestNet::pack("FIELD","sender",type,frame); socket.writeDatagram(packet,QHostAddress::LocalHost,port);
            } QThread::msleep(80); }
        });
        QByteArray output; const bool launched=launch(dir,"netreceive"+(secure?QString():QStringLiteral("clear"))+kind+(accept?"accept":"cancel"),output,6500); stop.store(true); sender.join();
        QVERIFY2(launched,output.constData()); QVERIFY2(output.contains("PROBE netreceive ") && output.contains("\"fromNetwork\":true") && output.contains(secure?"\"secure\":true":"\"secure\":false") && output.contains("\"sender\":\"sender\"") && output.contains(logIdentity.toUtf8()) &&
            output.contains(secure?(accept?"chiave condivisa":"shared key"):(accept?"senza autenticazione":"without authentication")) &&
            output.contains(accept?"log corrente del mittente":"sender's current log") && output.contains(kind=="insert"?"\"canInsert\":true":"\"canApply\":true") && output.contains("PROBE netreceive choice true") && !output.contains("TypeError"),output.constData());
        const int expectedCount=kind=="insert"?(accept?1:0):(kind=="delete" && accept?0:1);
        QVERIFY2(output.contains("PROBE netreceive model "+QByteArray::number(expectedCount)),output.constData());
        LogDatabase local; QVERIFY(local.open(dir.filePath("test.sqlite")));
        if (kind=="insert") { QCOMPARE(local.qsoCount(),qint64(accept?1:0)); if (accept) QVERIFY(local.idForUuid(row.uuid)>0); }
        else { QVERIFY(local.meta(localId)); QCOMPARE(local.meta(localId)->deleted,accept && kind=="delete");
            if (kind=="edit") QCOMPARE(local.record(localId)->value("NAME"),accept?QString("Remote"):QString("Local")); }

    }
    void stationNetworkHandoffDialog_data()
    {
        QTest::addColumn<QString>("language"); QTest::addColumn<QByteArray>("warning");
        QTest::newRow("English") << QStringLiteral("en") << QByteArray("not authenticated");
        QTest::newRow("Italian") << QStringLiteral("it") << QByteArray("non sono autenticate");
    }
    void stationNetworkHandoffDialog()
    {
        QFETCH(QString,language); QFETCH(QByteArray,warning);
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true); QByteArray output;
        { QSettings settings(iniPath(dir),QSettings::IniFormat); settings.setValue("ui/language",language); settings.setValue("net/id","receiver"); settings.sync(); }
        { using namespace decolog::core; LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
          contesthandoff::Offer offer{"8f3b7450-8e62-4dbd-8671-001122334455","test-event","sender","receiver","K1AAA",14025000,"2026-11-24T12:00:00Z"};
          QVERIFY(contesthandoff::receiveOffer(db,"test-event","receiver","sender",contesthandoff::offerFrame(offer)).ok);
          offer.id="8f3b7450-8e62-4dbd-8671-001122334456"; offer.call="K2BBB"; offer.scheduledUtc=QDateTime::currentDateTimeUtc().addSecs(120).toString(Qt::ISODate);
          QVERIFY(contesthandoff::receiveOffer(db,"test-event","receiver","sender",contesthandoff::offerFrame(offer)).ok);
          QVERIFY(contesthandoff::setIncomingState(db,"test-event","receiver",offer.id,"accepted").ok); }
        QVERIFY2(launch(dir,"nethandoffprobe",output,3500,true),output.constData());
        QVERIFY2(output.contains("PROBE nethandoff") && output.contains("\"visible\":true") && output.contains("\"scope\":\"test-event\"") &&
                 output.contains("\"offers\":2") && output.contains("\"scheduled\":1") && output.contains("\"history\":2") &&
                 output.contains("\"historyVisible\":true") && output.contains("\"historyRows\":2") && output.contains("\"historyInvalid\":true") && output.contains("\"call\":\"K1ABC\"") && output.contains(warning) &&
                 output.contains("\"error\":\"\"") && output.contains("\"before\":\"pending\"") && output.contains("\"after\":\"accepted\"") &&
                 output.contains("\"stateError\":\"\"") && output.contains("\"stale\":true") && output.contains("\"staleEnabled\":false") &&
                 output.contains("\"staleRows\":0") && output.contains("[activity] SKED|warning|") && output.contains("K2BBB") && !output.contains("TypeError"),output.constData());
        { using namespace decolog::core; LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
          const auto offers=contesthandoff::incomingOffers(db,"test-event","receiver"); QCOMPARE(offers.size(),2); QCOMPARE(offers.first().state,QString("accepted")); }
        QByteArray reopened; QVERIFY2(launch(dir,"nethandoffprobe",reopened,3500,true),reopened.constData());
        QVERIFY2(reopened.contains("[activity] SKED|warning|") && reopened.contains("K2BBB") && !reopened.contains("TypeError"),reopened.constData());
    }
    void stationNetworkExperimentalQsoDialog_data()
    {
        QTest::addColumn<QString>("language"); QTest::addColumn<QByteArray>("warning"); QTest::addColumn<bool>("secure");
        QTest::newRow("English clear") << QStringLiteral("en") << QByteArray("QSO data is not encrypted") << false;
        QTest::newRow("Italian clear") << QStringLiteral("it") << QByteArray("i dati QSO non sono cifrati") << false;
        QTest::newRow("English protected") << QStringLiteral("en") << QByteArray("Legacy QSO and other network messages remain unencrypted") << true;
        QTest::newRow("Italian protected") << QStringLiteral("it") << QByteArray("gli altri messaggi di rete rimangono non cifrati") << true;
    }
    void stationNetworkExperimentalQsoDialog()
    {
        QFETCH(QString,language); QFETCH(QByteArray,warning); QFETCH(bool,secure); QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { QSettings settings(iniPath(dir),QSettings::IniFormat); settings.setValue("ui/language",language); settings.sync(); }
        QByteArray output; QVERIFY2(launch(dir,secure?"netwiresecureprobe":"netwireprobe",output,3500),output.constData());
        QVERIFY2(output.contains("PROBE netwire") && output.contains("\"visible\":true") && output.contains(warning) && output.contains(secure?"\"secure\":true":"\"secure\":false") && output.contains("\"expiryGuard\":true") && !output.contains(QByteArray(64,'a')) && !output.contains("TypeError"),output.constData());
    }
    void stationNetworkComparisonDialogShowsOfflineResult_data()
    {
        QTest::addColumn<QString>("language"); QTest::addColumn<QByteArray>("expectedTitle"); QTest::addColumn<QByteArray>("expectedField");
        QTest::newRow("English") << QStringLiteral("en") << QByteArray("Compare station logs") << QByteArray("NAME: current Local");
        QTest::newRow("Italian") << QStringLiteral("it") << QByteArray("Confronta i log di stazione") << QByteArray("NAME: attuale Local");
    }
    void stationNetworkComparisonDialogShowsOfflineResult()
    {
        using namespace decolog::core; QFETCH(QString,language); QFETCH(QByteArray,expectedTitle); QFETCH(QByteArray,expectedField); QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { QSettings settings(iniPath(dir),QSettings::IniFormat); settings.setValue("ui/language",language); settings.sync(); }
        const auto localPath=dir.filePath("test.sqlite"),remotePath=dir.filePath("remote.sqlite"); LogDatabase local,remote; QVERIFY(local.open(localPath)); QVERIFY(remote.open(remotePath));
        const auto add=[](LogDatabase& db,const QString& name) { auto qso=db.insertQso(AdifRecord{{"CALL","K1AAA"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"NAME",name}},"fixture"); return qso.id>0 && db.adoptUuid(qso.id,"shared"); };
        QVERIFY(add(local,"Local")); QVERIFY(add(remote,"Remote")); local.close(); remote.close();
        QByteArray output; QVERIFY2(launch(dir,"netcompareprobe:"+remotePath,output,4500),output.constData());
        QVERIFY2(output.contains("PROBE netcompare") && output.contains("\"visible\":true") && output.contains(expectedTitle) && output.contains("\"contentConflict\":1") && output.contains("\"rows\":1") && output.contains("\"firstFields\":1"),output.constData());
        QVERIFY2(output.contains(expectedField),output.constData());
        QVERIFY2(output.contains("PROBE netreview") && output.contains("\"ok\":true") && output.contains("\"fields\":") && !output.contains("TypeError"),output.constData());
    }
    void stationNetworkImportConfirmation_data()
    {
        QTest::addColumn<QString>("language"); QTest::addColumn<bool>("accept");
        QTest::newRow("reject English") << QStringLiteral("en") << false;
        QTest::newRow("accept Italian") << QStringLiteral("it") << true;
    }
    void stationNetworkImportConfirmation()
    {
        using namespace decolog::core; QFETCH(QString,language); QFETCH(bool,accept); QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { QSettings settings(iniPath(dir),QSettings::IniFormat); settings.setValue("ui/language",language); settings.sync(); }
        const auto localPath=dir.filePath("test.sqlite"), remotePath=dir.filePath("remote.sqlite"); QString uuid;
        { LogDatabase remote; QVERIFY(remote.open(remotePath)); const auto q=remote.insertQso(AdifRecord{{"CALL","K1AAA"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"NAME","Remote"}},"fixture"); QVERIFY(q.id>0); uuid=remote.meta(q.id)->uuid; }
        QByteArray output; QVERIFY2(launch(dir,(accept?"netimportprobe:":"netimportcancelprobe:")+remotePath,output,4500),output.constData());
        QVERIFY2(output.contains("PROBE netimport true") && !output.contains("TypeError"),output.constData());
        LogDatabase local; QVERIFY(local.open(localPath)); QCOMPARE(local.qsoCount(),qint64(accept?1:0)); if (accept) QVERIFY(local.idForUuid(uuid)>0);
    }
    void stationNetworkReviewsLongFieldBeforeImport()
    {
        using namespace decolog::core; QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        const auto localPath=dir.filePath("test.sqlite"),remotePath=dir.filePath("remote.sqlite");
        const auto longValue=QString(2047,'x')+QString::fromUtf8("😀")+QStringLiteral("x");
        { LogDatabase remote; QVERIFY(remote.open(remotePath)); const auto q=remote.insertQso(AdifRecord{{"CALL","K1AAA"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"COMMENT",longValue}},"fixture"); QVERIFY(q.id>0); }
        QByteArray output; QVERIFY2(launch(dir,"netlongprobe:"+remotePath,output,4500),output.constData());
        QVERIFY2(output.contains("PROBE netlong true") && output.contains("PROBE netimport true") && !output.contains("TypeError"),output.constData());
        LogDatabase local; QVERIFY(local.open(localPath)); const auto ids=local.latestIds("K1AAA",10); QCOMPARE(ids.size(),1); QCOMPARE(local.record(ids.first())->value("COMMENT"),longValue);
    }
    void stationNetworkApplyConfirmation_data()
    {
        QTest::addColumn<bool>("accept"); QTest::addColumn<bool>("deletion");
        QTest::newRow("reject edit") << false << false; QTest::newRow("accept edit") << true << false;
        QTest::newRow("reject deletion") << false << true; QTest::newRow("accept deletion") << true << true;
    }
    void stationNetworkApplyConfirmation()
    {
        using namespace decolog::core; QFETCH(bool,accept); QFETCH(bool,deletion); QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        const auto localPath=dir.filePath("test.sqlite"), remotePath=dir.filePath("remote.sqlite"); qint64 localId=0;
        auto add=[](LogDatabase& db,const QString& name) { const auto q=db.insertQso(AdifRecord{{"CALL","K1AAA"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"NAME",name}},"fixture"); return q.id>0 && db.adoptUuid(q.id,"shared")?q.id:qint64(0); };
        { LogDatabase local,remote; QVERIFY(local.open(localPath)); QVERIFY(remote.open(remotePath)); localId=add(local,"Local"); const auto remoteId=add(remote,"Remote"); QVERIFY(localId>0 && remoteId>0); if (deletion) QVERIFY(remote.softDeleteQso(remoteId)); }
        QByteArray output; QVERIFY2(launch(dir,(accept?"netapplyprobe:":"netapplycancelprobe:")+remotePath,output,4500),output.constData());
        QVERIFY2(output.contains("PROBE netapply true") && !output.contains("TypeError"),output.constData());
        LogDatabase local; QVERIFY(local.open(localPath)); QVERIFY(local.meta(localId)); QCOMPARE(local.meta(localId)->deleted,accept && deletion);
        if (!deletion) QCOMPARE(local.record(localId)->value("NAME"),accept?QString("Remote"):QString("Local"));
    }
    void contestSerialEntryUsesSelectedGrant_data()
    {
        QTest::addColumn<QString>("probe");
        QTest::newRow("contest desk") << QStringLiteral("contestserialpanelprobe");
        QTest::newRow("contest window") << QStringLiteral("contestserialwindowprobe");
    }
    void contestSerialEntryUsesSelectedGrant()
    {
        using namespace decolog::core; QFETCH(QString,probe); QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { QSettings s(iniPath(dir),QSettings::IniFormat); s.setValue("net/id","client-a"); s.setValue("net/enabled",false); s.sync(); }
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QVERIFY(db.beginContestSerialRequest("event","client-a","req","authority",10).ok); QVERIFY(db.acceptContestSerialGrant("event","client-a","req","authority",10).ok); }
        QByteArray output; QVERIFY2(launch(dir,probe,output,4000),output.constData());
        QVERIFY2(output.contains("\"macro\":\"10\"") && output.contains("\"staleRejected\":true") && output.contains("\"error\":\"\""),output.constData());
        LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); const auto grant=db.contestSerialRequest("event","client-a","req"); QCOMPARE(grant.state,QString("consumed"));
        QSqlQuery q(db.connection()); QVERIFY(q.exec("SELECT id,uuid FROM qso WHERE call='K1CONTEST'")); QVERIFY(q.next()); const auto record=db.record(q.value(0).toLongLong()); QVERIFY(record); QCOMPARE(record->value("STX"),QString("10")); QCOMPARE(q.value(1).toString(),grant.useId);
    }
    void experimentalSerialGrantIsBoundByManualQso()
    {
        using namespace decolog::core; QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { QSettings s(iniPath(dir),QSettings::IniFormat); s.setValue("net/id","client-a"); s.setValue("net/enabled",false); s.sync(); }
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QVERIFY(db.beginContestSerialRequest("event","client-a","req","authority",10).ok); QVERIFY(db.acceptContestSerialGrant("event","client-a","req","authority",10).ok); }
        QByteArray output; QVERIFY2(launch(dir,"serialqsoprobe",output,3500),output.constData());
        QVERIFY2(output.contains("PROBE serialQso setup= config= result="),output.constData());
        QVERIFY2(output.contains("shown=10 available=true checkedAfter=false"),output.constData());
        QVERIFY2(output.contains("missing=No matching experimental serial grant is available") && output.contains("legacy="),output.constData());
        QVERIFY2(output.contains("stale=The selected serial grant changed"),output.constData());
        LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); const auto grant=db.contestSerialRequest("event","client-a","req");
        QCOMPARE(grant.state,QString("consumed")); QVERIFY(!grant.useId.isEmpty()); QSqlQuery q(db.connection());
        q.prepare("SELECT id,uuid FROM qso WHERE call='K1PROBE'"); QVERIFY(q.exec()); QVERIFY(q.next()); const auto stored=db.record(q.value(0).toLongLong()); QVERIFY(stored); QCOMPARE(stored->value("STX"),QString("10")); QCOMPARE(q.value(1).toString(),grant.useId);
        QVERIFY(q.exec("SELECT COUNT(*) FROM qso WHERE call='K1NOGRANT'")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),0);
        QVERIFY(q.exec("SELECT id FROM qso WHERE call='K1LEGACY'")); QVERIFY(q.next()); const auto legacy=db.record(q.value(0).toLongLong()); QVERIFY(legacy); QCOMPARE(legacy->value("STX"),QString("11"));
    }
    void qslCostEditorRejectsReplacedCloneAtSamePath()
    {
        using namespace decolog::core; QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        const QString path=dir.filePath("test.sqlite"), clone=dir.filePath("clone.sqlite");
        { LogDatabase db; QVERIFY(db.open(path)); QSqlQuery q(db.connection()); QVERIFY(q.exec("PRAGMA journal_mode=DELETE")); q.finish();
          const auto qso=db.insertQso({{"CALL","K1PAA"},{"QSO_DATE","20261001"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"DXCC","291"},{"CQZ","5"},{"QSL_VIA","M0ABC"}},"fixture"); QVERIFY(qso.id>0);
          QslState card; card.service="card"; card.via="B"; QVERIFY(db.setCardState(qso.id,card)); QString error; QVERIFY(qslplanner::saveRouteCost(db,"M0ABC","B",{"9.99","EUR","estimate","",""},&error));
          q.prepare("VACUUM INTO ?"); q.addBindValue(clone); QVERIFY(q.exec()); }
        QProcessEnvironment env=QProcessEnvironment::systemEnvironment(); env.insert("QT_QPA_PLATFORM","offscreen"); env.insert("QT_QUICK_BACKEND","software"); env.insert("DECODXLOG_DATA_ROOT",dir.filePath("data")); env.insert("DECODXLOG_GRAB_MS","14000");
        QProcess app; app.setProcessEnvironment(env); app.setProcessChannelMode(QProcess::MergedChannels);
        app.start(QStringLiteral(DECOLOG_APP),{"--software-renderer","--settings",dir.filePath("settings"),"--db",path,"--port","0","--show","qslcostfileprobe","--grab",dir.filePath("screen.png")});
        QVERIFY(app.waitForStarted(10000)); QByteArray output; QElapsedTimer timer; timer.start();
        while (!output.contains("PROBE cost file ready") && timer.elapsed()<9000) { app.waitForReadyRead(200); output+=app.readAll(); }
        QVERIFY2(output.contains("PROBE cost file ready"),output.constData());
        if (!QFile::rename(path,path+".original")) { app.kill(); app.waitForFinished(); QSKIP("Platform does not allow replacing an open SQLite file"); }
        QVERIFY(QFile::rename(clone,path)); timer.restart();
        while (!output.contains("PROBE cost file clear") && timer.elapsed()<7000) { app.waitForReadyRead(200); output+=app.readAll(); }
        QVERIFY2(output.contains("PROBE cost file save true") && output.contains("PROBE cost file clear true") && (output.contains("The reviewed log or plan changed") || output.contains("Il log o il piano verificato")),output.constData());
        app.kill(); app.waitForFinished();
        LogDatabase db; QVERIFY(db.open(path)); const auto cost=qslplanner::routeCost(db,"M0ABC","B"); QVERIFY(cost); QCOMPARE(cost->amount,QString("9.99"));
    }
    void qslPlannerRejectsReplacedCloneAtSamePath()
    {
        using namespace decolog::core; QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        const QString path=dir.filePath("test.sqlite"), clone=dir.filePath("clone.sqlite");
        { LogDatabase db; QVERIFY(db.open(path)); QSqlQuery q(db.connection()); QVERIFY(q.exec("PRAGMA journal_mode=DELETE")); q.finish();
          QVERIFY(db.insertQso(AdifRecord{{"CALL","K1AAA"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"DXCC","291"},{"CQZ","5"}},"fixture").id>0);
          q.prepare("VACUUM INTO ?"); q.addBindValue(clone); QVERIFY(q.exec()); }
        QProcessEnvironment env=QProcessEnvironment::systemEnvironment(); env.insert("QT_QPA_PLATFORM","offscreen"); env.insert("QT_QUICK_BACKEND","software");
        env.insert("DECODXLOG_DATA_ROOT",dir.filePath("data")); env.insert("DECODXLOG_GRAB_MS","14000");
        QProcess app; app.setProcessEnvironment(env); app.setProcessChannelMode(QProcess::MergedChannels);
        app.start(QStringLiteral(DECOLOG_APP),{QStringLiteral("--software-renderer"),QStringLiteral("--settings"),dir.filePath("settings"),
            QStringLiteral("--db"),path,QStringLiteral("--port"),QStringLiteral("0"),QStringLiteral("--show"),QStringLiteral("qslexternalprobe"),QStringLiteral("--grab"),dir.filePath("screen.png")});
        QVERIFY(app.waitForStarted(10000)); QByteArray output; QElapsedTimer timer; timer.start();
        while (!output.contains("PROBE qslExternal ready") && timer.elapsed()<9000) { app.waitForReadyRead(200); output+=app.readAll(); }
        QVERIFY2(output.contains("PROBE qslExternal ready"),output.constData());
        if (!QFile::rename(path,path+".original")) { app.kill(); app.waitForFinished(); QSKIP("Platform does not allow replacing an open SQLite file"); }
        QVERIFY(QFile::rename(clone,path));
        timer.restart(); while (!output.contains("PROBE qslExternal error") && timer.elapsed()<7000) { app.waitForReadyRead(200); output+=app.readAll(); }
        QVERIFY2(output.contains("PROBE qslExternal stale") && output.contains("Plan is no longer current") && output.contains("Log file changed; reopen it before planning"),output.constData());
        app.kill(); app.waitForFinished();
    }
    void qslPlannerDetectsExternalLogChanges()
    {
        using namespace decolog::core; QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
          QVERIFY(db.insertQso(AdifRecord{{"CALL","K1AAA"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"DXCC","291"},{"CQZ","5"}},"fixture").id>0); }
        QProcessEnvironment env=QProcessEnvironment::systemEnvironment(); env.insert("QT_QPA_PLATFORM","offscreen"); env.insert("QT_QUICK_BACKEND","software");
        env.insert("DECODXLOG_DATA_ROOT",dir.filePath("data")); env.insert("DECODXLOG_GRAB_MS","14000");
        QProcess app; app.setProcessEnvironment(env); app.setProcessChannelMode(QProcess::MergedChannels);
        app.start(QStringLiteral(DECOLOG_APP),{QStringLiteral("--software-renderer"),QStringLiteral("--settings"),dir.filePath("settings"),
            QStringLiteral("--db"),dir.filePath("test.sqlite"),QStringLiteral("--port"),QStringLiteral("0"),QStringLiteral("--show"),QStringLiteral("qslexternalprobe"),QStringLiteral("--grab"),dir.filePath("screen.png")});
        QVERIFY(app.waitForStarted(10000)); QByteArray output; QElapsedTimer timer; timer.start();
        while (!output.contains("PROBE qslExternal ready") && timer.elapsed()<9000) { app.waitForReadyRead(200); output+=app.readAll(); }
        QVERIFY2(output.contains("PROBE qslExternal ready"),output.constData());
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QSqlQuery q(db.connection()); QVERIFY(q.exec("UPDATE qso SET call='K1CHANGED' WHERE call='K1AAA'")); }
        timer.restart(); while (!output.contains("PROBE qslExternal refreshed") && timer.elapsed()<7000) { app.waitForReadyRead(200); output+=app.readAll(); }
        QVERIFY2(output.contains("PROBE qslExternal guard") && output.contains("Plan is no longer current") && output.contains("PROBE qslExternal stale") && output.contains("PROBE qslExternal refreshed") && output.contains("K1CHANGED"),output.constData());
        app.kill(); app.waitForFinished();
    }
    void marathonUnknownModeRequiresClassification()
    {
        using namespace decolog::core; QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true); qint64 profile=0;
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); StationProfile p; p.name="Marathon"; p.stationCallsign="K1XYZ"; profile=db.saveStationProfile(p); QVERIFY(profile>0);
          for (const auto& item : {qMakePair("K1AAA","MYSTERY"),qMakePair("K2BBB","FT8")})
            QVERIFY(db.insertQso(AdifRecord{{"CALL",item.first},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE",item.second},{"DXCC","291"},{"CQZ",item.first==QString("K1AAA")?"5":"3"}},"fixture",{},true,profile,false).id>0); }
        QByteArray output; QVERIFY2(launch(dir,"marathonmodeprobe:"+QString::number(profile),output,4500),output.constData());
        QVERIFY2(output.contains("digital=1 excluded=QSO 1: unrecognised mode classification") && output.contains("mixed=2 warnings=QSO 1: unrecognised mode MYSTERY") && !output.contains("TypeError"),output.constData());
    }
    void marathonPriorReviewRestoresWithFrequencyRecheck()
    {
        using namespace decolog::core; QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true); qint64 profile=0;
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); StationProfile p; p.name="Marathon"; p.stationCallsign="K1XYZ"; profile=db.saveStationProfile(p); QVERIFY(profile>0);
          for (const auto& item : {qMakePair("K1AAA","5"),qMakePair("K2BBB","3"),qMakePair("K3CCC","5")})
            QVERIFY(db.insertQso(AdifRecord{{"CALL",item.first},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"FREQ","7.074"},{"MODE","CW"},{"DXCC","291"},{"CQZ",item.second}},"fixture",{},true,profile,false).id>0); }
        const QString path=dir.filePath("review.json"); QByteArray output;
        QVERIFY2(launch(dir,"marathonroundtripprobe:"+path+":"+QString::number(profile),output,4500),output.constData());
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); auto base=cqmarathon::select(cqmarathon::loadEntries(db),{2026,"mixed",1,profile}).toJson().object();
          for (const QString& key : {QStringLiteral("selected"),QStringLiteral("zoneCandidates"),QStringLiteral("entityAlternatives"),QStringLiteral("zoneAlternatives")}) {
              QJsonArray rows; for (const auto& value : base.value(key).toArray()) { auto row=value.toObject(); row.remove("frequencyMhz"); row.remove("invalidFrequency"); rows.append(row); } base.insert(key,rows); }
          QJsonArray warnings; for (const auto& w : base.value("warnings").toArray()) if (!w.toString().contains(": recorded frequency ")) warnings.append(w); base.insert("warnings",warnings);
          const auto identity=db.identity().toUtf8(); const auto raw=QByteArray::number(identity.size())+':'+identity+QJsonDocument(base).toJson(QJsonDocument::Compact);
          QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); auto review=QJsonDocument::fromJson(file.readAll()).object(); file.close();
          review.insert("previewSnapshot",QString::fromLatin1(QCryptographicHash::hash(raw,QCryptographicHash::Sha256).toHex()));
          QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate)); QVERIFY(file.write(QJsonDocument(review).toJson())>0); }
        QVERIFY2(launch(dir,"marathonimportprobe:"+path+":"+QString::number(profile),output,4500),output.constData());
        QVERIFY2(output.contains("error= before=1,1 after=2,3 notice=Restored an older review; recheck current location, propagation and frequency evidence"),output.constData());
    }
    void marathonReviewShowsFrequencyBandConflict()
    {
        using namespace decolog::core; QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true); qint64 profile=0;
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); StationProfile p; p.name="Marathon"; p.stationCallsign="K1XYZ"; profile=db.saveStationProfile(p); QVERIFY(profile>0);
          QVERIFY(db.insertQso(AdifRecord{{"CALL","K1AAA"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"FREQ","7.074"},{"MODE","CW"},{"DXCC","291"},{"CQZ","5"}},"fixture",{},true,profile,false).id>0); }
        QByteArray output; QVERIFY2(launch(dir,"marathonreviewprobe:"+QString::number(profile),output,4500),output.constData());
        QVERIFY2(output.contains("recorded frequency 7.074 MHz outside recorded band 20m") && output.contains("entity=QSO 1") && output.contains("7.074 MHz") && !output.contains("TypeError"),output.constData());
    }
    void marathonOldReviewRestoresOnlyIfPropagationStillEligible()
    {
        using namespace decolog::core; QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true); qint64 profile=0;
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); StationProfile p; p.name="Marathon"; p.stationCallsign="K1XYZ"; profile=db.saveStationProfile(p); QVERIFY(profile>0);
          for (const auto& item : {qMakePair("K1AAA","5"), qMakePair("K2BBB","3"), qMakePair("K3CCC","5")})
            QVERIFY(db.insertQso(AdifRecord{{"CALL",item.first},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"DXCC","291"},{"CQZ",item.second},{"PROP_MODE","EME"}},"fixture",{},true,profile,false).id>0); }
        const QString path=dir.filePath("review.json"); QByteArray output;
        QVERIFY2(launch(dir,"marathonroundtripprobe:"+path+":"+QString::number(profile),output,4500),output.constData());
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); auto base=cqmarathon::select(cqmarathon::loadEntries(db),{2026,"mixed",1,profile}).toJson().object();
          for (const QString& key : {QStringLiteral("selected"),QStringLiteral("zoneCandidates"),QStringLiteral("entityAlternatives"),QStringLiteral("zoneAlternatives")}) {
              QJsonArray rows; for (const auto& value : base.value(key).toArray()) { auto row=value.toObject(); row.remove("propMode"); row.remove("satName"); rows.append(row); } base.insert(key,rows); }
          const auto identity=db.identity().toUtf8(); const auto raw=QByteArray::number(identity.size())+':'+identity+QJsonDocument(base).toJson(QJsonDocument::Compact);
          QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); auto review=QJsonDocument::fromJson(file.readAll()).object(); file.close();
          review.insert("previewSnapshot",QString::fromLatin1(QCryptographicHash::hash(raw,QCryptographicHash::Sha256).toHex()));
          QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate)); QVERIFY(file.write(QJsonDocument(review).toJson())>0); }
        QVERIFY2(launch(dir,"marathonimportprobe:"+path+":"+QString::number(profile),output,4500),output.constData());
        QVERIFY2(output.contains("error= before=1,1 after=2,3"),output.constData());
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QSqlQuery update(db.connection()); QVERIFY(update.exec("UPDATE qso SET prop_mode='RPT' WHERE call='K3CCC'")); }
        QVERIFY2(launch(dir,"marathonimportprobe:"+path+":"+QString::number(profile),output,4500),output.constData());
        QVERIFY2(output.contains("error=Review file does not match the current log and preview before=1,1 after=1,1"),output.constData());
    }
    void marathonReviewShowsRecordedRelayExclusion()
    {
        using namespace decolog::core; QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true); qint64 profile=0;
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); StationProfile p; p.name="Marathon"; p.stationCallsign="K1XYZ"; profile=db.saveStationProfile(p); QVERIFY(profile>0);
          QVERIFY(db.insertQso(AdifRecord{{"CALL","K1AAA"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","FM"},{"DXCC","291"},{"CQZ","5"},{"PROP_MODE","RPT"}},"fixture",{},true,profile,false).id>0);
          QVERIFY(db.insertQso(AdifRecord{{"CALL","K2BBB"},{"QSO_DATE","20260101"},{"TIME_ON","1201"},{"BAND","20m"},{"MODE","CW"},{"DXCC","291"},{"CQZ","3"}},"fixture",{},true,profile,false).id>0); }
        QByteArray output; QVERIFY2(launch(dir,"marathonreviewprobe:"+QString::number(profile),output,4500),output.constData());
        QVERIFY2(output.contains("zone=QSO 2 · K2BBB · CQ zone 3") && output.contains("recorded propagation RPT") && !output.contains("TypeError"),output.constData());
    }
    void marathon2026LocksOneEntityAndKeepsZones()
    {
        using namespace decolog::core; QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true); qint64 profile=0;
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); StationProfile p; p.name="Marathon"; p.stationCallsign="K1XYZ"; profile=db.saveStationProfile(p); QVERIFY(profile>0);
          for (const auto& item : {qMakePair("K1AAA","5"),qMakePair("K2BBB","3")})
            QVERIFY(db.insertQso(AdifRecord{{"CALL",item.first},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"DXCC","291"},{"CQZ",item.second}},"fixture",{},true,profile,false).id>0); }
        QByteArray output; QVERIFY2(launch(dir,"marathonlimitprobe:"+QString::number(profile),output,4500),output.constData());
        QVERIFY2(output.contains("before=2,true after=1,false entities=1 zones=2") && !output.contains("TypeError"),output.constData());
    }
    void marathonSingleBandPreviewAndJson()
    {
        using namespace decolog::core; QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true); qint64 profile=0;
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); StationProfile p; p.name="Marathon"; p.stationCallsign="K1XYZ"; profile=db.saveStationProfile(p); QVERIFY(profile>0);
          for (const auto& item : {qMakePair("K1AAA","20m"),qMakePair("DL1AAA","40m")})
            QVERIFY(db.insertQso(AdifRecord{{"CALL",item.first},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND",item.second},{"MODE","CW"},{"DXCC","291"},{"CQZ","5"}},"fixture",{},true,profile,false).id>0); }
        const QString path=dir.filePath("review.json"); QByteArray output;
        QVERIFY2(launch(dir,"marathonbandprobe:"+path+":"+QString::number(profile),output,4500),output.constData());
        QVERIFY2(output.contains("band=20m count=1 zones=1 saved= loaded=") && output.contains("band mismatch") && !output.contains("TypeError"),output.constData());
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto json=QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(json.value("band").toString(),QString("20m")); QCOMPARE(json.value("selected").toArray().size(),1);
    }
    void marathonReviewShowsOperatingGridEvidence()
    {
        using namespace decolog::core; QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true); qint64 profile=0;
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); StationProfile p; p.name="Marathon"; p.stationCallsign="K1XYZ"; profile=db.saveStationProfile(p); QVERIFY(profile>0);
          for (const auto& item : {qMakePair("K1AAA","JM75FV"), qMakePair("K2BBB","JN58TD"),qMakePair("K3CCC","")})
            QVERIFY(db.insertQso(AdifRecord{{"CALL",item.first},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"DXCC","291"},{"CQZ","5"},{"MY_GRIDSQUARE",item.second}},"fixture",{},true,profile,false).id>0); }
        QByteArray output; QVERIFY2(launch(dir,"marathonreviewprobe:"+QString::number(profile),output,4500),output.constData());
        QVERIFY2(output.contains("Recorded own grid squares: JM75, JN58 · missing: 1 · invalid: 0") && !output.contains("TypeError"),output.constData());
    }
    void marathonReviewedJsonRoundTripRejectsChangedLog()
    {
        using namespace decolog::core; QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true); qint64 profile=0;
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); StationProfile p; p.name="Marathon"; p.stationCallsign="K1XYZ"; profile=db.saveStationProfile(p); QVERIFY(profile>0);
          for (const auto& item : {qMakePair("K1AAA","5"), qMakePair("K2BBB","3"), qMakePair("K3CCC","5")})
            QVERIFY(db.insertQso(AdifRecord{{"CALL",item.first},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"DXCC","291"},{"CQZ",item.second}},"fixture",{},true,profile,false).id>0); }
        const QString path=dir.filePath("review.json"); QByteArray output;
        QVERIFY2(launch(dir,"marathonroundtripprobe:"+path+":"+QString::number(profile),output,4500),output.constData());
        QVERIFY2(output.contains("saved= before=1,1 loaded= after=2,3") && !output.contains("TypeError"),output.constData());
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); auto base=cqmarathon::select(cqmarathon::loadEntries(db),{2026,"mixed",1,profile}).toJson().object();
          base.remove("locationGrids"); base.remove("locationMissing"); base.remove("locationInvalid");
          for (const QString& key : {QStringLiteral("selected"),QStringLiteral("zoneCandidates"),QStringLiteral("entityAlternatives"),QStringLiteral("zoneAlternatives")}) {
              QJsonArray rows; for (const auto& value : base.value(key).toArray()) { auto row=value.toObject(); row.remove("myGrid"); rows.append(row); } base.insert(key,rows); }
          const auto identity=db.identity().toUtf8(); const auto raw=QByteArray::number(identity.size())+':'+identity+QJsonDocument(base).toJson(QJsonDocument::Compact);
          const auto oldSnapshot=QString::fromLatin1(QCryptographicHash::hash(raw,QCryptographicHash::Sha256).toHex());
          QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); auto review=QJsonDocument::fromJson(file.readAll()).object(); file.close(); review.insert("previewSnapshot",oldSnapshot);
          QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate)); QVERIFY(file.write(QJsonDocument(review).toJson())>0); }
        QVERIFY2(launch(dir,"marathonimportprobe:"+path+":"+QString::number(profile),output,4500),output.constData());
        QVERIFY2(output.contains("error= before=1,1 after=2,3") && !output.contains("TypeError"),output.constData());
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QSqlQuery update(db.connection()); QVERIFY(update.exec("UPDATE qso SET call='K3CHANGED' WHERE call='K3CCC'")); }
        QVERIFY2(launch(dir,"marathonimportprobe:"+path+":"+QString::number(profile),output,4500),output.constData());
        QVERIFY2(output.contains("error=Review file does not match the current log and preview before=1,1 after=1,1") && !output.contains("TypeError"),output.constData());
    }
    void marathonReviewedJsonExportsIndependentChoices()
    {
        using namespace decolog::core; QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true); qint64 profile=0;
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); StationProfile p; p.name="Marathon"; p.stationCallsign="K1XYZ"; profile=db.saveStationProfile(p); QVERIFY(profile>0);
          for (const auto& item : {qMakePair("K1AAA","5"), qMakePair("K2BBB","3"), qMakePair("K3CCC","5")})
            QVERIFY(db.insertQso(AdifRecord{{"CALL",item.first},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"DXCC","291"},{"CQZ",item.second}},"fixture",{},true,profile,false).id>0); }
        const QString path=dir.filePath("review.json"); QByteArray output;
        QVERIFY2(launch(dir,"marathonexportreviewprobe:"+path+":"+QString::number(profile)+":"+dir.filePath("test.sqlite"),output,4500),output.constData());
        QVERIFY2(output.contains("PROBE marathonReviewedExport success= stale=The preview changed") && output.contains("protected=Choose a file other than the log database oldProtected=false changed=The preview changed") && !output.contains("TypeError"),output.constData());
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto json=QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(json.value("selectionKind").toString(),QString("reviewed"));
        QCOMPARE(json.value("selected").toArray().at(0).toObject().value("qsoId").toInt(),2);
        QCOMPARE(json.value("zoneCandidates").toArray().at(0).toObject().value("qsoId").toInt(),3);
        QCOMPARE(json.value("zoneCandidates").toArray().at(1).toObject().value("qsoId").toInt(),2);
        LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QCOMPARE(db.qsoCount(),qint64(3));
    }
    void marathonReplacementsRemainIndependent()
    {
        using namespace decolog::core; QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true); qint64 profile=0;
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); StationProfile p; p.name="Marathon"; p.stationCallsign="K1XYZ"; profile=db.saveStationProfile(p); QVERIFY(profile>0);
          for (const auto& item : {qMakePair("K1AAA","5"), qMakePair("K2BBB","3"), qMakePair("K3CCC","5")})
            QVERIFY(db.insertQso(AdifRecord{{"CALL",item.first},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"DXCC","291"},{"CQZ",item.second}},"fixture",{},true,profile,false).id>0); }
        QByteArray output; QVERIFY2(launch(dir,"marathonalternativeprobe:"+QString::number(profile),output,4500),output.constData());
        QVERIFY2(output.contains("before=1,1 entityChanged=true zoneAfterEntity=1 zoneChanged=true entity=QSO 2") && output.contains("zone=QSO 3") && !output.contains("TypeError"),output.constData());
        LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QCOMPARE(db.qsoCount(),qint64(3));
    }
    void marathonReviewDisplaysEntityZoneWarning()
    {
        using namespace decolog::core; QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true); qint64 profile=0;
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); StationProfile p; p.name="Marathon"; p.stationCallsign="K1XYZ"; profile=db.saveStationProfile(p); QVERIFY(profile>0);
          QVERIFY(db.insertQso(AdifRecord{{"CALL","I1AAA"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"DXCC","248"},{"CQZ","5"}},"fixture",{},true,profile,false).id>0); }
        QByteArray output; QVERIFY2(launch(dir,"marathonreviewprobe:"+QString::number(profile),output,4500),output.constData());
        QVERIFY2(output.contains("CQ zone 5 is not listed for entity 248") && output.contains("Marathon entity 248") && !output.contains("TypeError"),output.constData());
    }
    void marathonReviewShowsUnknownEntityAndKeepsZone()
    {
        using namespace decolog::core; QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true); qint64 profile=0;
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); StationProfile p; p.name="Marathon"; p.stationCallsign="K1XYZ"; profile=db.saveStationProfile(p); QVERIFY(profile>0);
          QVERIFY(db.insertQso(AdifRecord{{"CALL","K1AAA"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"DXCC","999"},{"CQZ","5"}},"fixture",{},true,profile,false).id>0); }
        QByteArray output; QVERIFY2(launch(dir,"marathonreviewprobe:"+QString::number(profile),output,4500),output.constData());
        QVERIFY2(output.contains("CQ zone 5 · DXCC 999") && output.contains("entity code 999 not in published list v1.1; zone considered independently") && !output.contains("TypeError"),output.constData());
    }
    void marathonReviewShowsZoneWithoutEntityAndExcludedBand()
    {
        using namespace decolog::core; QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true); qint64 profile=0;
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); StationProfile p; p.name="Marathon"; p.stationCallsign="K1XYZ"; profile=db.saveStationProfile(p); QVERIFY(profile>0);
          QVERIFY(db.insertQso(AdifRecord{{"CALL","K1AAA"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"CQZ","5"}},"fixture",{},true,profile,false).id>0);
          QVERIFY(db.insertQso(AdifRecord{{"CALL","K1BBB"},{"QSO_DATE","20260101"},{"TIME_ON","1201"},{"BAND","2m"},{"MODE","CW"},{"DXCC","291"},{"CQZ","4"}},"fixture",{},true,profile,false).id>0);
          QVERIFY(db.insertQso(AdifRecord{{"CALL","IT9AAA"},{"QSO_DATE","20260101"},{"TIME_ON","1202"},{"BAND","20m"},{"MODE","CW"},{"DXCC","248"},{"REGION","SY"},{"CQZ","15"}},"fixture",{},true,profile,false).id>0); }
        QByteArray output; QVERIFY2(launch(dir,"marathonreviewprobe:"+QString::number(profile),output,4500),output.constData());
        QVERIFY2((output.contains("PROBE marathonReview open=true zone=QSO 1 · K1AAA · CQ zone 5 · DXCC unknown") || output.contains("PROBE marathonReview open=true zone=QSO 1 · K1AAA · CQ zone 5 · DXCC sconosciuto")) && output.contains("outside supported 2026 bands") && output.contains("Marathon entity 904 · DXCC 248 · REGION SY") && !output.contains("TypeError"),output.constData());
        LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QCOMPARE(db.qsoCount(),qint64(3));
    }
    void marathonReviewAdifExportsYearAndAlternatives()
    {
        using namespace decolog::core; QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true); qint64 profile=0;
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); StationProfile a; a.name="Marathon A"; a.stationCallsign="K1ABC"; profile=db.saveStationProfile(a); QVERIFY(profile>0);
          StationProfile b; b.name="Marathon B"; b.stationCallsign="K2ABC"; const auto other=db.saveStationProfile(b); QVERIFY(other>0);
          auto add=[&](const char* call,const char* date,qint64 p) { return db.insertQso(AdifRecord{{"CALL",call},{"QSO_DATE",date},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"DXCC","291"},{"CQZ","5"},{"APP_TEST","kept"}},"fixture",{},true,p,false).id; };
          QVERIFY(add("K1AAA","20260101",profile)>0); QVERIFY(add("K1BBB","20260102",profile)>0); QVERIFY(add("K1CCC","20250101",profile)>0); QVERIFY(add("K1DDD","20260101",other)>0); }
        const QString outputPath=dir.filePath("marathon.adi"); QByteArray output;
        QVERIFY2(launch(dir,QStringLiteral("marathonadifprobe:")+outputPath+":"+QString::number(profile),output,5000),output.constData());
        QVERIFY2(output.contains("PROBE marathonAdif success=true count=2 invalid=0 error=") && !output.contains("TypeError"),output.constData());
        QFile file(outputPath); QVERIFY(file.open(QIODevice::ReadOnly)); const auto doc=adif::parse(file.readAll()); QCOMPARE(doc.records.size(),2);
        QCOMPARE(doc.records[0].value("CALL"),QString("K1AAA")); QCOMPARE(doc.records[1].value("CALL"),QString("K1BBB")); QCOMPARE(doc.records[0].value("APP_TEST"),QString("kept"));
        QByteArray guarded; QVERIFY2(launch(dir,QStringLiteral("marathonadifprobe:")+dir.filePath("test.sqlite")+":"+QString::number(profile),guarded,4000),guarded.constData());
        QVERIFY2(guarded.contains("PROBE marathonAdif start=Choose a file other than the log database"),guarded.constData());
        LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QCOMPARE(db.qsoCount(),qint64(4));
    }
    void vuccThresholdCanBeAppliedWithoutOverwritingCustomGoal()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true); QByteArray output;
        QVERIFY2(launch(dir,"vuccthresholdprobe",output,4000),output.constData());
        QVERIFY2(output.contains("PROBE vuccThreshold before=125 minimum=50 label=ARRL initial threshold: 50 grids") &&
                 output.contains("PROBE vuccThreshold applied=50 next=10 field=50") && output.contains("PROBE vuccThreshold unsupported=0 enabled=false") &&
                 output.contains("PROBE vuccThreshold satellite=100 target=50") && !output.contains("TypeError"),output.constData());
        decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QCOMPARE(db.qsoCount(),qint64(0));
    }
    void vuccReviewShowsDateBoundaryExclusion()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
          const QByteArray data="<CALL:5>K1AAA<QSO_DATE:8>19821231<TIME_ON:6>235959<BAND:2>6m<MODE:2>CW<GRIDSQUARE:4>EM10<QSL_RCVD:1>Y<EOR>"
              "<CALL:5>K1BBB<QSO_DATE:8>19830101<TIME_ON:6>000000<BAND:2>6m<MODE:2>CW<GRIDSQUARE:4>EM11<QSL_RCVD:1>Y<EOR>";
          QCOMPARE(db.importAdif(data).inserted,2);
          QSqlQuery q(db.connection()); QVERIFY(q.exec("UPDATE qso SET qso_datetime_on='1983-01-01T00:00:00' WHERE call='K1BBB'")); }
        QByteArray output; QVERIFY2(launch(dir,"vuccreviewprobe",output,4500),output.constData());
        QVERIFY2(output.contains("PROBE vuccReview open=true row=EM11") && output.contains("1983-01-01T00:00:00Z") && output.contains("before 1983-01-01 UTC") && !output.contains("TypeError"),output.constData());
        decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QCOMPARE(db.qsoCount(),qint64(2));
    }
    void vuccReviewShowsInvalidOwnLocator()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
          const QByteArray data="<CALL:5>K1AAA<QSO_DATE:8>20260101<TIME_ON:4>1200<BAND:2>6m<MODE:2>CW<GRIDSQUARE:4>EM10<QSL_RCVD:1>Y<MY_GRIDSQUARE:4>JN52<MY_VUCC_GRIDS:9>JN52,JN54<EOR>"
              "<CALL:5>K1BBB<QSO_DATE:8>20260101<TIME_ON:4>1201<BAND:2>6m<MODE:2>CW<GRIDSQUARE:4>EM11<QSL_RCVD:1>Y<MY_GRIDSQUARE:4>JN52<MY_VUCC_GRIDS:9>JN52,JN53<EOR>";
          const auto imported=db.importAdif(data); QCOMPARE(imported.inserted,2);
          QString error; QVERIFY2(db.recordLotwGridObservation(imported.insertedIds.last(),"FM18","FM18,FM19",&error),qPrintable(error)); }
        QByteArray output; QVERIFY2(launch(dir,"vuccreviewprobe",output,4500),output.constData());
        QVERIFY2(output.contains("PROBE vuccReview open=true row=EM11") && output.contains("QSO 1: invalid or inconsistent operating locator data") && !output.contains("TypeError"),output.constData());
        QVERIFY2(output.contains("FM18") && output.contains("FM18,FM19") && (output.contains("match unverified") || output.contains("abbinamento non verificato")),output.constData());
        QVERIFY2(output.contains("different grid") || output.contains("locator diverso"),output.constData());
        QVERIFY2(output.contains("gridEvidence=local-qso-unverified") && (output.contains("their presence on the confirmation has not been verified") || output.contains("la loro presenza sulla conferma non è stata verificata")),output.constData());
        decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QCOMPARE(db.qsoCount(),qint64(2));
    }

    void vuccShowsObservationFromMergedQso()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
          const decolog::core::AdifRecord r{{"CALL","K1BBB"},{"QSO_DATE","20260101"},{"TIME_ON","1201"},{"BAND","6m"},{"MODE","CW"},{"GRIDSQUARE","EM11"},{"QSL_RCVD","Y"}};
          const auto first=db.insertQso(r,"fixture",{},true,0,false).id, second=db.insertQso(r,"fixture",{},true,0,false).id; QVERIFY(first>0 && second>0);
          QString error; for (int i=0;i<55;++i) QVERIFY(db.recordLotwGridObservation(second,QString("FN%1").arg(i,2,10,QLatin1Char('0')),QStringLiteral(""),&error));
          QCOMPARE(db.mergeQsos(first,{second}).status,decolog::core::InsertResult::Status::Inserted); }
        QByteArray output; QVERIFY2(launch(dir,"vuccreviewprobe",output,4500),output.constData());
        QVERIFY2(output.contains("FN54") && output.contains("PROBE vuccHistoryContext") &&
                 (output.contains("from previously merged QSO 2") || output.contains("dal QSO 2 precedentemente unito")) && !output.contains("TypeError"),output.constData());
        QByteArray historyLine; for (const auto& line:output.split('\n')) if (line.contains("PROBE vuccHistoryContext")) historyLine=line;
        QVERIFY2(historyLine.contains("from previously merged QSO 2") || historyLine.contains("dal QSO 2 precedentemente unito"),output.constData());
    }

    void vuccObservationHistoryPagesInDesktop()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
          const auto imported=db.importAdif("<CALL:5>K1BBB<QSO_DATE:8>20260101<TIME_ON:4>1201<BAND:2>6m<MODE:2>CW<GRIDSQUARE:4>EM11<QSL_RCVD:1>Y<EOR>");
          QCOMPARE(imported.inserted,1); QString error;
          for (int i=0;i<55;++i) QVERIFY(db.recordLotwGridObservation(imported.insertedIds.first(),QString("FN%1").arg(i,2,10,QLatin1Char('0')),QStringLiteral(""),&error));
          const decolog::core::AdifRecord confirmation{{"CALL","K1BBB"},{"QSO_DATE","20260101"},{"TIME_ON","120200"},{"BAND","6m"},{"MODE","CW"},{"GRIDSQUARE","FN54"},{"STATION_CALLSIGN","IU8LMC"},{"APP_LOTW_RXQSL","2026-01-02 10:00:00"}};
          QCOMPARE(db.applyConfirmation("lotw",confirmation,1800,{}, {},true).status,decolog::core::ConfirmationResult::Status::Confirmed); }
        QByteArray output; QVERIFY2(launch(dir,"vuccreviewprobe",output,4500),output.constData());
        QVERIFY2(output.contains("PROBE vuccHistory first=50 next=true") && output.contains("PROBE vuccHistory second=5 next=false") && !output.contains("TypeError"),output.constData());
        QVERIFY2(output.contains("PROBE vuccHistoryContext") && (output.contains("60 seconds apart") || output.contains("distanza di 60 secondi")) && output.contains("IU8LMC") && output.contains("2026-01-02 10:00:00"),output.constData());
    }

    void vuccSatellitePreviewUsesSeparateCategory()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
          const QByteArray data="<CALL:5>K1AAA<QSO_DATE:8>20260101<TIME_ON:4>1200<BAND:2>2m<MODE:2>CW<GRIDSQUARE:4>EM10<QSL_RCVD:1>Y<EOR>"
              "<CALL:5>K1BBB<QSO_DATE:8>20260101<TIME_ON:4>1201<BAND:2>2m<BAND_RX:4>70cm<PROP_MODE:3>SAT<SAT_NAME:5>AO-91<MODE:2>FM<GRIDSQUARE:4>EM11<QSL_RCVD:1>Y<EOR>";
          QCOMPARE(db.importAdif(data).inserted,2); }
        QByteArray output; QVERIFY2(launch(dir,"vuccsatelliteprobe",output,4500),output.constData());
        QVERIFY2(output.contains("PROBE vuccReview open=true row=EM11") && output.contains("satellite AO-91") && output.contains("TX 2m / RX 70cm") && output.contains("propagation scope mismatch") && !output.contains("TypeError"),output.constData());
        decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QCOMPARE(db.qsoCount(),qint64(2));
    }
    void vuccReviewShowsAllowedAndUnsupportedEvidence()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
          const QByteArray data="<CALL:5>K1AAA<QSO_DATE:8>20260101<TIME_ON:4>1200<BAND:2>6m<MODE:2>CW<GRIDSQUARE:4>EM10<EQSL_QSL_RCVD:1>Y<EOR>"
              "<CALL:5>K1BBB<QSO_DATE:8>20260101<TIME_ON:4>1201<BAND:2>6m<MODE:2>CW<GRIDSQUARE:4>EM11<QSL_RCVD:1>Y<EQSL_QSL_RCVD:1>Y<EOR>";
          QCOMPARE(db.importAdif(data).inserted,2); }
        QByteArray output; QVERIFY2(launch(dir,"vuccreviewprobe",output,4500),output.constData());
        QVERIFY2(output.contains("PROBE vuccReview open=true row=EM11") && output.contains("local confirmation: card") && output.contains("other recorded: eqsl") && output.contains("unsupported confirmation sources: eqsl") && !output.contains("TypeError"),output.constData());
        decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QCOMPARE(db.qsoCount(),qint64(2));
    }
    void lotwCheckpointReviewRequiresExplicitConfirmation()
    {
        using namespace decolog::core; QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true); QString identity,uuid;
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); const auto qso=db.insertQso({{"CALL","K1ABC"},{"QSO_DATE","20261101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}},"fixture"); QVERIFY(qso.id>0); identity=db.identity(); uuid=db.meta(qso.id)->uuid; }
        QJsonObject checkpoint{{"version",1},{"nonce","synthetic-nonce"},{"logIdentity",identity},{"logPath",dir.filePath("test.sqlite")},{"location","test"},{"sha256",QString(64,'a')},
            {"contacts",QJsonArray{QJsonObject{{"id","1"},{"uuid",uuid},{"revision",1},{"call","K1ABC"},{"date","20261101"},{"time","1200"},{"band","20m"},{"mode","CW"},{"submode",""}}}}};
        { QSettings settings(iniPath(dir),QSettings::IniFormat); settings.setValue("lotwPendingUpload/checkpoint",QJsonDocument(checkpoint).toJson(QJsonDocument::Compact)); settings.sync(); }
        const QString capture=qEnvironmentVariable("DECOLOG_LOTW_UI_CAPTURE");
        if (!capture.isEmpty()) { QByteArray visual; QVERIFY2(launch(dir,"lotwcheckpointview",visual,3000),visual.constData());
            const QString source=dir.filePath("screen-2.png"); QVERIFY2(QFile::copy(source,capture),qPrintable(source)); }
        QByteArray output; QVERIFY2(launch(dir,"lotwcheckpointprobe",output,4500),output.constData());
        QVERIFY2(output.contains("PROBE lotwCheckpoint shown=true count=1 call=true action=true") && output.contains("PROBE lotwCheckpoint confirm=true") && output.contains("PROBE lotwCheckpoint cancelled=true") && output.contains("PROBE lotwCheckpoint cleared=true") && !output.contains("TypeError"),output.constData());
        QSettings verify(iniPath(dir),QSettings::IniFormat); QVERIFY(!verify.contains("lotwPendingUpload/checkpoint")); LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QCOMPARE(db.qsosToUpload("lotw",0).size(),1);
    }

    void importPreviewCanCancelAndRestart()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true); const auto path=dir.filePath("source.adi"); QFile input(path);
        QVERIFY(input.open(QIODevice::WriteOnly)); input.write("<CALL:5>K1OLD<QSO_DATE:8>20261101<TIME_ON:4>1200<BAND:3>20m<MODE:2>CW<EOR>\n"); input.close();
        QByteArray output; QVERIFY2(launch(dir,"importcancelbusyprobe:"+path,output,7000),output.constData());
        QVERIFY2(output.contains("PROBE importcancel busy=true cleared=true") && output.count("PROBE importsnapshot ready")==1 && !output.contains("TypeError"),output.constData());
        decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QCOMPARE(db.qsoCount(),qint64(1));
    }
    void importPreviewRejectsExternalLogChange()
    {
        using namespace decolog::core; QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        const auto path=dir.filePath("source.adi"); QFile input(path); QVERIFY(input.open(QIODevice::WriteOnly));
        input.write("<CALL:5>K1OLD<QSO_DATE:8>20261101<TIME_ON:4>1200<BAND:3>20m<MODE:2>CW<EOR>\n"); input.close();
        auto env=QProcessEnvironment::systemEnvironment(); env.insert("QT_QPA_PLATFORM","offscreen"); env.insert("QT_QUICK_BACKEND","software");
        env.insert("DECODXLOG_DATA_ROOT",dir.filePath("data")); env.insert("DECODXLOG_GRAB_MS","7000"); QProcess app;
        app.setProcessEnvironment(env); app.setProcessChannelMode(QProcess::MergedChannels);
        app.start(QStringLiteral(DECOLOG_APP),{"--software-renderer","--settings",dir.filePath("settings"),"--db",dir.filePath("test.sqlite"),"--port","0","--show","importsnapshotprobe:"+path,"--grab",dir.filePath("screen.png")});
        QVERIFY(app.waitForStarted(10000)); QByteArray output; QElapsedTimer timer; timer.start();
        while (!output.contains("PROBE importsnapshot ready") && timer.elapsed()<10000 && app.state()!=QProcess::NotRunning) { app.waitForReadyRead(200); output+=app.readAll(); }
        QVERIFY2(output.contains("PROBE importsnapshot ready"),output.constData());
        { LogDatabase external; QVERIFY(external.open(dir.filePath("test.sqlite")));
          QVERIFY(external.insertQso(AdifRecord{{"CALL","K2EXT"},{"QSO_DATE","20261101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}},"external",{},true,0,false).id>0); }
        QVERIFY(app.waitForFinished(20000)); output+=app.readAll(); QVERIFY2(app.exitStatus()==QProcess::NormalExit && app.exitCode()==0,output.constData());
        LogDatabase verify; QVERIFY(verify.open(dir.filePath("test.sqlite"))); QCOMPARE(verify.qsoCount(),qint64(1));
        const auto ids=verify.latestIds({},10); QCOMPARE(ids.size(),1); const auto record=verify.record(ids.first()); QVERIFY(record); QCOMPARE(record->value("CALL"),QString("K2EXT"));
    }
    void importPreviewLongFieldListRemainsScrollable()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true); const QString path=dir.filePath("many.adi");
        QFile input(path); QVERIFY(input.open(QIODevice::WriteOnly)); QByteArray data="<CALL:5>K1AAA<QSO_DATE:8>20261101<TIME_ON:4>1200<BAND:3>20m<MODE:2>CW";
        for (int i=0;i<150;++i) data += QString("<APP_VENDOR_FIELD_%1:1>X").arg(i,3,10,QLatin1Char('0')).toUtf8();
        data += "<APP_EQSL_QSL_RCVD:1>Y<EOR>\n"; input.write(data); input.close();
        QByteArray output; QVERIFY2(launch(dir,"importlongprobe:"+path,output,4000),output.constData());
        QVERIFY2(output.contains("PROBE importlong scroll=true bottom=true buttons=true last=true") && !output.contains("TypeError"),output.constData());
        decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QCOMPARE(db.qsoCount(),qint64(0));
    }
    void importPreviewExplainsProgramFieldMappings()
    {
        using namespace decolog::core; QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        const QString path=dir.filePath("mapping.adi"); QFile input(path); QVERIFY(input.open(QIODevice::WriteOnly));
        input.write("<CALL:5>K1AAA<QSO_DATE:8>20261101<TIME_ON:4>1200<BAND:3>20m<MODE:2>CW<APP_QRZLOG_STATUS:1>C<APP_QRZLOG_QSLDATE:8>20261102<APP_EQSL_QSL_RCVD:1>Y<EOR>\n"
                    "<CALL:5>K1BBB<QSO_DATE:8>20261101<TIME_ON:4>1200<BAND:3>20m<MODE:2>CW<APP_EQSL_QSL_RCVD:1>Y<EQSL_QSL_RCVD:1>N<EOR>\n"); input.close();
        QByteArray output; QVERIFY2(launch(dir,"importsnapshotprobe:"+path,output,7000),output.constData());
        QVERIFY2(output.contains("\"qslRecords\":2") && output.contains("\"mappedFields\"") && output.contains("APP_QRZLOG_STATUS -> QRZCOM_QSO_DOWNLOAD_STATUS: 1") && output.contains("APP_QRZLOG_QSLDATE -> QRZCOM_QSO_DOWNLOAD_DATE: 1") && output.contains("APP_EQSL_QSL_RCVD -> EQSL_QSL_RCVD: 1"),output.constData());
        LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QCOMPARE(db.qsoCount(),qint64(2));
        const auto first=db.latestIds("K1AAA",1); const auto second=db.latestIds("K1BBB",1); QCOMPARE(first.size(),1); QCOMPARE(second.size(),1);
        QMap<QString,QslState> firstStates,secondStates; for (const auto& state:db.qslStatus(first.first())) firstStates.insert(state.service,state); for (const auto& state:db.qslStatus(second.first())) secondStates.insert(state.service,state);
        QCOMPARE(firstStates.value("qrz").rcvd,QString("Y")); QCOMPARE(firstStates.value("eqsl").rcvd,QString("Y")); QCOMPARE(secondStates.value("eqsl").rcvd,QString("N"));
    }
    void importPreviewReportsAndPreservesProprietaryFields()
    {
        using namespace decolog::core; QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        const QString path=dir.filePath("proprietary.adi"); QFile input(path); QVERIFY(input.open(QIODevice::WriteOnly));
        input.write("<CALL:5>K1ABC<QSO_DATE:8>20261101<TIME_ON:4>1200<BAND:3>20m<MODE:2>CW<APP_VENDOR_NOTE:6>secret<EOR>\n"); input.close();
        QByteArray output; QVERIFY2(launch(dir,"importsnapshotprobe:"+path,output,7000),output.constData());
        QVERIFY2(output.contains("\"extraFields\":[\"APP_VENDOR_NOTE\"]"),output.constData());
        LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); const auto ids=db.latestIds({},10); QCOMPARE(ids.size(),1);
        const auto record=db.record(ids.first()); QVERIFY(record); QCOMPARE(record->value("APP_VENDOR_NOTE"),QString("secret"));
    }
    void importPreviewUsesAllExistingDuplicateKeys()
    {
        using namespace decolog::core; QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
          for (const QString &time:QStringList{"120000","120200"}) QVERIFY(db.insertQso(AdifRecord{{"CALL","K1ABC"},{"QSO_DATE","20261101"},{"TIME_ON",time},{"BAND","20m"},{"MODE","CW"}},"fixture",{},true,0,false).id>0); }
        const QString path=dir.filePath("duplicate.adi"); QFile input(path); QVERIFY(input.open(QIODevice::WriteOnly));
        input.write("<CALL:5>K1ABC<QSO_DATE:8>20261101<TIME_ON:6>120400<BAND:3>20m<MODE:2>CW<EOR>\n"); input.close();
        QByteArray output; QVERIFY2(launch(dir,"importsnapshotprobe:"+path,output,7000),output.constData());
        QVERIFY2(output.contains("\"estimatedDuplicates\":1") && output.contains("\"estimatedInserted\":0") && !output.contains("TypeError"),output.constData());
        LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QCOMPARE(db.qsoCount(),qint64(2));
    }
    void importUsesReviewedSnapshotWhenSourceChanges_data()
    {
        QTest::addColumn<bool>("removeSource"); QTest::addColumn<bool>("cancel"); QTest::newRow("modified") << false << false; QTest::newRow("removed") << true << false; QTest::newRow("cancelled") << false << true;
    }
    void importUsesReviewedSnapshotWhenSourceChanges()
    {
        QFETCH(bool,removeSource); QFETCH(bool,cancel);
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true); const QString path=dir.filePath("source.adi");
        QFile input(path); QVERIFY(input.open(QIODevice::WriteOnly)); input.write("<CALL:5>K1OLD<QSO_DATE:8>20261101<TIME_ON:4>1200<BAND:3>20m<MODE:2>CW<EOR>\n"); input.close();
        auto env=QProcessEnvironment::systemEnvironment(); env.insert("QT_QPA_PLATFORM","offscreen"); env.insert("QT_QUICK_BACKEND","software");
        env.insert("DECODXLOG_DATA_ROOT",dir.filePath("data")); env.insert("DECODXLOG_GRAB_MS","7000");
        QProcess app; app.setProcessEnvironment(env); app.setProcessChannelMode(QProcess::MergedChannels);
        app.start(QStringLiteral(DECOLOG_APP),{"--software-renderer","--settings",dir.filePath("settings"),"--db",dir.filePath("test.sqlite"),"--port","0","--show",(cancel ? "importcancelprobe:" : "importsnapshotprobe:")+path,"--grab",dir.filePath("screen.png")});
        QVERIFY(app.waitForStarted(10000)); QByteArray output; QElapsedTimer timer; timer.start();
        while (!output.contains("PROBE importsnapshot ready") && timer.elapsed()<10000 && app.state()!=QProcess::NotRunning) { app.waitForReadyRead(200); output+=app.readAll(); }
        QVERIFY2(output.contains("PROBE importsnapshot ready"),output.constData());
        if (removeSource) QVERIFY(input.remove()); else { QVERIFY(input.open(QIODevice::WriteOnly|QIODevice::Truncate)); input.write("<CALL:5>K1NEW<QSO_DATE:8>20261101<TIME_ON:4>1200<BAND:3>20m<MODE:2>CW<EOR>\n"); input.close(); }
        QVERIFY(app.waitForFinished(20000)); output+=app.readAll(); QVERIFY2(app.exitStatus()==QProcess::NormalExit && app.exitCode()==0,output.constData());
        decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QCOMPARE(db.qsoCount(),qint64(cancel ? 0 : 1)); if (cancel) return;
        const auto ids=db.latestIds({},10); QCOMPARE(ids.size(),1); const auto record=db.record(ids.first()); QVERIFY(record); QCOMPARE(record->value("CALL"),QString("K1OLD"));
    }
    void selectedContestFindingsInActualApp()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        const QByteArray a="Contest: 2024 CQ WORLD WIDE CW DX CONTEST\nCall: K1ABC\n****** Not In Log ******\n14020 CW 2024-11-23 1201 K1ABC 05 DL1AAA 14\nunknown line\n****** Incorrect Call ******\n7020 CW 2024-11-23 1202 K1ABC 05 DL1BAD 14 incorrect call\n";
        const QByteArray b="Contest: 2025 CQ WORLD WIDE CW DX CONTEST\nCall: K2ABC\n****** Incorrect Call ******\n7020 CW 2025-11-22 1202 K2ABC 05 DL2BAD 14 correct DL2GOOD\n";
        QFile fa(dir.filePath("a.rpt")), fb(dir.filePath("b.rpt")); QVERIFY(fa.open(QIODevice::WriteOnly)); QCOMPARE(fa.write(a),a.size()); fa.close(); QVERIFY(fb.open(QIODevice::WriteOnly)); QCOMPARE(fb.write(b),b.size()); fb.close();
        QByteArray output; QVERIFY2(launch(dir,"contestfindingsprobe:"+dir.path(),output,4500),output.constData());
        QVERIFY2(output.contains("PROBE contestFindings nilA=1 callA=1 callB=1 unknownA=1 error=") && !output.contains("TypeError"),output.constData());
        QByteArray stale; QVERIFY2(launch(dir,"contestfindingsstaleprobe:"+dir.path(),stale,4500),stale.constData());
        QVERIFY2(stale.contains("PROBE contestFindings stale=true") && !stale.contains("TypeError"),stale.constData());
        QVERIFY(fa.open(QIODevice::ReadOnly)); QCOMPARE(fa.readAll(),a); QVERIFY(fb.open(QIODevice::ReadOnly)); QCOMPARE(fb.readAll(),b);
        decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QCOMPARE(db.qsoCount(),qint64(0));
    }
    void contestAudioControlsUseVirtualInputWhenConfigured()
    {
        if (qEnvironmentVariable("DECOLOG_TEST_LOOPBACK") != QLatin1String("BlackHole 2ch")) QSKIP("Set DECOLOG_TEST_LOOPBACK=BlackHole 2ch");
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true); QByteArray output;
        QVERIFY2(launch(dir,QStringLiteral("contestaudioprobe:")+dir.path(),output,5000),output.constData());
        QVERIFY2(output.contains("PROBE contestAudio started=true dialog=true") && output.contains("PROBE contestAudio stopped=true") &&
                 !output.contains("TypeError"),output.constData());
        const auto recordings=QDir(dir.path()).entryInfoList({"contest-audio-*.wav"},QDir::Files);
        QCOMPARE(recordings.size(),1); QVERIFY(recordings.first().size()>44);
        decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QCOMPARE(db.qsoCount(),qint64(0));
    }
    void contestAudioPlaybackControlsWhenConfigured()
    {
        if (qEnvironmentVariable("DECOLOG_TEST_LOOPBACK") != QLatin1String("BlackHole 2ch")) QSKIP("Set DECOLOG_TEST_LOOPBACK=BlackHole 2ch");
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        decolog::core::ContestAudioFile writer; const auto start=QDateTime::fromString("2026-11-23T12:00:00Z",Qt::ISODate);
        QVERIFY(writer.start(dir.path(),192000,start,48000)); QCOMPARE(writer.append(QByteArray(192000,'a')).written,qint64(192000));
        QString path; QVERIFY(writer.finish(&path)); QByteArray output;
        QVERIFY2(launch(dir,QStringLiteral("contestaudioplayprobe:")+path,output,5000),output.constData());
        QVERIFY2(output.contains("PROBE contestPlayback playing=true qso=K1ABC") && output.contains("PROBE contestPlayback stopped=true") && output.contains("PROBE contestSeek advanced=true rewound=true") &&
                 !output.contains("TypeError"),output.constData());
        decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QCOMPARE(db.qsoCount(),qint64(0));
    }
    void contestAudioRecoversPartialThroughApp()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        decolog::core::ContestAudioFile writer; QVERIFY(writer.start(dir.path(),64,QDateTime::currentDateTimeUtc()));
        QCOMPARE(writer.append(QByteArray::fromHex("01020304")).written,qint64(4)); const QString partial=writer.partialPath(); writer.abandon();
        QByteArray output; QVERIFY2(launch(dir,QStringLiteral("contestaudiorecoverprobe:")+partial,output,4000),output.constData());
        QVERIFY2(output.contains("PROBE contestRecover success=true") && !output.contains("TypeError"),output.constData());
        QVERIFY(!QFile::exists(partial)); QFile wav(partial.left(partial.size()-5)); QVERIFY(wav.open(QIODevice::ReadOnly));
        QCOMPARE(wav.readAll().mid(44),QByteArray::fromHex("01020304"));
        decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QCOMPARE(db.qsoCount(),qint64(0));
    }
    void contestAudioFindsRecordingThroughApp()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        decolog::core::ContestAudioFile writer; const auto start=QDateTime::fromString("2026-11-23T12:00:00Z",Qt::ISODate);
        QVERIFY(writer.start(dir.path(),64000,start)); QCOMPARE(writer.append(QByteArray(64000,'x')).written,qint64(64000));
        QString path; QVERIFY(writer.finish(&path)); QByteArray output;
        QVERIFY2(launch(dir,QStringLiteral("contestaudiofindprobe:")+dir.path(),output,4000),output.constData());
        QVERIFY2(output.contains("PROBE contestFind count=1 selected=true") && !output.contains("TypeError"),output.constData());
        QVERIFY(QFile::exists(path)); decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QCOMPARE(db.qsoCount(),qint64(0));
    }
    void contestAudioRemembersFolderAndFindsAfterRestart()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        decolog::core::ContestAudioFile writer; const auto start=QDateTime::fromString("2026-11-23T12:00:00Z",Qt::ISODate);
        QVERIFY(writer.start(dir.path(),64000,start)); QCOMPARE(writer.append(QByteArray(64000,'x')).written,qint64(64000));
        QString path; QVERIFY(writer.finish(&path)); QByteArray first,second;
        QVERIFY2(launch(dir,QStringLiteral("contestaudiofindprobe:")+dir.path(),first,4000),first.constData());
        QVERIFY2(first.contains("PROBE contestFind count=1 selected=true"),first.constData());
        QVERIFY2(launch(dir,QStringLiteral("contestaudiorestoreprobe"),second,4000),second.constData());
        QVERIFY2(second.contains("PROBE contestAudio restored=true") && second.contains("PROBE contestFind count=1 selected=true") && !second.contains("TypeError"),second.constData());
        QVERIFY(QFile::exists(path)); decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QCOMPARE(db.qsoCount(),qint64(0));
    }
    void contestAudioRepeatsStaleSearchForNewQso()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        decolog::core::ContestAudioFile writer; const auto start=QDateTime::fromString("2026-11-23T12:00:00Z",Qt::ISODate);
        QVERIFY(writer.start(dir.path(),64000,start)); QCOMPARE(writer.append(QByteArray(64000,'x')).written,qint64(64000));
        QString path; QVERIFY(writer.finish(&path)); QByteArray output;
        QVERIFY2(launch(dir,QStringLiteral("contestaudiostaleprobe:")+dir.path(),output,4000),output.constData());
        QVERIFY2(output.contains("PROBE contestFind count=1 selected=true qso=2026-11-23T12:00:00.500Z") && !output.contains("TypeError"),output.constData());
        QVERIFY(QFile::exists(path));
    }
    void contestAudioDevicesSurviveAppRestartWhenConfigured()
    {
        if (qEnvironmentVariable("DECOLOG_TEST_LOOPBACK") != QLatin1String("BlackHole 2ch")) QSKIP("Set DECOLOG_TEST_LOOPBACK=BlackHole 2ch");
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true); QByteArray capture,playback,restored;
        QVERIFY2(launch(dir,QStringLiteral("contestaudioprobe:")+dir.path(),capture,4000),capture.constData());
        QVERIFY2(capture.contains("PROBE contestAudio started=true"),capture.constData());
        decolog::core::ContestAudioFile writer; const auto start=QDateTime::fromString("2026-11-23T12:00:00Z",Qt::ISODate);
        QVERIFY(writer.start(dir.path(),64000,start)); QCOMPARE(writer.append(QByteArray(64000,'x')).written,qint64(64000));
        QString path; QVERIFY(writer.finish(&path));
        QVERIFY2(launch(dir,QStringLiteral("contestaudioplayprobe:")+path,playback,4000),playback.constData());
        QVERIFY2(playback.contains("PROBE contestPlayback playing=true"),playback.constData());
        QVERIFY2(launch(dir,QStringLiteral("contestaudiodevicesprobe"),restored,3000),restored.constData());
        QVERIFY2(restored.contains("PROBE contestDevices input=BlackHole 2ch output=BlackHole 2ch") && !restored.contains("TypeError"),restored.constData());
    }
    void contestAudioDiscoversAndRecoversPartialThroughApp()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        decolog::core::ContestAudioFile writer; QVERIFY(writer.start(dir.path(),64,QDateTime::currentDateTimeUtc()));
        QCOMPARE(writer.append(QByteArray::fromHex("01020304")).written,qint64(4)); const QString partial=writer.partialPath(); writer.abandon();
        QByteArray output; QVERIFY2(launch(dir,QStringLiteral("contestaudiopartialsprobe:")+dir.path(),output,4000),output.constData());
        QVERIFY2(output.contains("PROBE contestPartials count=1 recovered=true") && !output.contains("TypeError"),output.constData());
        QVERIFY(!QFile::exists(partial)); QFile wav(partial.left(partial.size()-5)); QVERIFY(wav.open(QIODevice::ReadOnly));
        QCOMPARE(wav.readAll().mid(44),QByteArray::fromHex("01020304"));
        decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QCOMPARE(db.qsoCount(),qint64(0));
    }
    void contestGoalsPersistAcrossActualAppRestart()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true); QByteArray saved,loaded;
        QVERIFY2(launch(dir,"contestsavegoalsprobe",saved,4500),saved.constData());
        QVERIFY2(saved.contains("PROBE savedContestGoals saved=true a=5 b=1") && !saved.contains("TypeError"),saved.constData());
        QVERIFY2(launch(dir,"contestloadgoalsprobe",loaded,4500),loaded.constData());
        QVERIFY2(loaded.contains("PROBE savedContestGoals saved=false a=5 b=1") && !loaded.contains("TypeError"),loaded.constData());
        QByteArray removed,empty;
        QVERIFY2(launch(dir,"contestremovegoalsprobe",removed,4500),removed.constData());
        QVERIFY2(removed.contains("PROBE savedContestGoals saved=false a=0 b=0 removed=true") && !removed.contains("TypeError"),removed.constData());
        QVERIFY2(launch(dir,"contestloadgoalsprobe",empty,4500),empty.constData());
        QVERIFY2(empty.contains("PROBE savedContestGoals saved=false a=0 b=0 removed=false") && !empty.contains("TypeError"),empty.constData());
        decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QCOMPARE(db.qsoCount(),qint64(0));
    }
    void contestComparisonShowsTwoPeriodsInActualApp()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true); const auto log=dir.filePath("test.sqlite");
        { decolog::core::LogDatabase db; QVERIFY(db.open(log));
          for (const auto &date:QStringList{"20241123","20241123","20251122"})
            QVERIFY(db.insertQso(decolog::core::AdifRecord{{"CALL","K1ABC"},{"QSO_DATE",date},{"TIME_ON","120000"},
                {"BAND","20m"},{"MODE","CW"},{"CONTEST_ID","CQ-WW-CW"},{"OPERATOR","K1OP"}},"fixture",{},true,0,false).id>0); }
        QByteArray output; QVERIFY2(launch(dir,"contestcomparisonprobe",output,4500),output.constData());
        QVERIFY2(output.contains("PROBE contestComparison a=2 b=1 rows=5 error=") && output.contains("PROBE contestDay Day 1 · A 2024-11-23 · B 2025-11-22 a=2 b=1") &&
                 output.contains("PROBE contestGoals a=2/5:40:3 b=1/1:100:0") && output.contains("PROBE contestGoals edge=true") && !output.contains("TypeError"),output.constData());
        QByteArray stale; QVERIFY2(launch(dir,"contestcomparisonstaleprobe",stale,4500),stale.constData());
        QVERIFY2(stale.contains("PROBE contestComparison stale=true") && !stale.contains("TypeError"),stale.constData());
        decolog::core::LogDatabase db; QVERIFY(db.open(log)); QCOMPARE(db.qsoCount(),qint64(3));
    }
    void contestReportFindsCandidateInActualApp()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true); const auto log=dir.filePath("test.sqlite");
        { decolog::core::LogDatabase db; QVERIFY(db.open(log));
          QVERIFY(db.insertQso(decolog::core::AdifRecord{{"CALL","DL1ABC"},{"QSO_DATE","20241123"},{"TIME_ON","120145"},
            {"BAND","20m"},{"MODE","CW"},{"STATION_CALLSIGN","K1ABC"}},"fixture",{},true).id>0);
          QSqlQuery q(db.connection()); QVERIFY(q.exec("UPDATE qso SET qso_datetime_on='2024-11-23T13:01:45+01:00'")); }
        const QString path=dir.filePath("report.rpt"); QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("Contest: 2024 CQ WORLD WIDE CW DX CONTEST\nCall: K1ABC\n****** Not In Log ******\n14020 CW 2024-11-23 1201 K1ABC 05 DL1ABC 14\n"); file.close();
        QByteArray output; QVERIFY2(launch(dir,QStringLiteral("cqwwopenprobe:")+path,output,5000),output.constData());
        QVERIFY2(output.contains("PROBE lcr candidates 1 ") && output.contains("PROBE lcr opened true") && !output.contains("TypeError"),output.constData());
        decolog::core::LogDatabase db; QVERIFY(db.open(log)); QCOMPARE(db.qsoCount(),qint64(1));
    }
    void contestReportFindsCorrectedCallInActualApp()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true); const auto log=dir.filePath("test.sqlite");
        { decolog::core::LogDatabase db; QVERIFY(db.open(log));
          QVERIFY(db.insertQso(decolog::core::AdifRecord{{"CALL","DL1GOOD"},{"QSO_DATE","20241123"},{"TIME_ON","120145"},
            {"BAND","20m"},{"MODE","CW"},{"STATION_CALLSIGN","K1ABC"}},"fixture",{},true).id>0); }
        const QString path=dir.filePath("report.rpt"); QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("Contest: 2024 CQ WORLD WIDE CW DX CONTEST\nCall: K1ABC\n****** Incorrect Call ******\n14020 CW 2024-11-23 1201 K1ABC 05 DL1BAD 14 correct DL1GOOD\n"); file.close();
        QByteArray output; QVERIFY2(launch(dir,QStringLiteral("cqwwopenprobe:")+path,output,5000),output.constData());
        QVERIFY2(output.contains("PROBE lcr candidates 1 ") && output.contains("PROBE lcr basis corrected call DL1GOOD") &&
                 output.contains("PROBE lcr opened true") && !output.contains("TypeError"),output.constData());
        decolog::core::LogDatabase db; QVERIFY(db.open(log)); QCOMPARE(db.qsoCount(),qint64(1));
    }
    void contestReportWidensTimeWindowInActualApp()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true); const auto log=dir.filePath("test.sqlite");
        { decolog::core::LogDatabase db; QVERIFY(db.open(log));
          QVERIFY(db.insertQso(decolog::core::AdifRecord{{"CALL","DL1ABC"},{"QSO_DATE","20241123"},{"TIME_ON","120059"},
            {"BAND","20m"},{"MODE","CW"}},"fixture",{},true).id>0); }
        const QString path=dir.filePath("report.rpt"); QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("Contest: 2024 CQ WORLD WIDE CW DX CONTEST\nCall: K1ABC\n****** Not In Log ******\n14020 CW 2024-11-23 1201 K1ABC 05 DL1ABC 14\n"); file.close();
        QByteArray output; QVERIFY2(launch(dir,QStringLiteral("cqwwwindowprobe:")+path,output,5000),output.constData());
        QVERIFY2(output.contains("PROBE lcr exact 0") && output.contains("PROBE lcr window 1 count 1") &&
                 output.contains("PROBE lcr opened true") && !output.contains("TypeError"),output.constData());
        decolog::core::LogDatabase db; QVERIFY(db.open(log)); QCOMPARE(db.qsoCount(),qint64(1));
    }
    void contestReportShowsExactMinuteBeforeTruncatedEarlierCandidates()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true); const auto log=dir.filePath("test.sqlite");
        { decolog::core::LogDatabase db; QVERIFY(db.open(log));
          for (int i=0;i<6;++i) QVERIFY(db.insertQso(decolog::core::AdifRecord{{"CALL","DL1ABC"},{"QSO_DATE","20241123"},
            {"TIME_ON",QString("1200%1").arg(i,2,10,QLatin1Char('0'))},{"BAND","20m"},{"MODE","CW"}},"fixture",{},true,0,false).id>0);
          QVERIFY(db.insertQso(decolog::core::AdifRecord{{"CALL","DL1ABC"},{"QSO_DATE","20241123"},{"TIME_ON","120130"},
            {"BAND","20m"},{"MODE","CW"}},"fixture",{},true,0,false).id>0); }
        const QString path=dir.filePath("report.rpt"); QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("Contest: 2024 CQ WORLD WIDE CW DX CONTEST\nCall: K1ABC\n****** Not In Log ******\n14020 CW 2024-11-23 1201 K1ABC 05 DL1ABC 14\n"); file.close();
        QByteArray output; QVERIFY2(launch(dir,QStringLiteral("cqwwwindowprobe:")+path,output,5000),output.constData());
        QVERIFY2(output.contains("PROBE lcr exact 1") && output.contains("PROBE lcr window 1 count 5") &&
                 output.contains("PROBE lcr closest 2024-11-23T12:01:30Z truncated true") && output.contains("PROBE lcr opened true") &&
                 !output.contains("TypeError"),output.constData());
        decolog::core::LogDatabase db; QVERIFY(db.open(log)); QCOMPARE(db.qsoCount(),qint64(7));
    }
    void contestReportHistoryExportsOriginalBytes()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true); const auto log=dir.filePath("test.sqlite");
        const QByteArray source="Contest: 2024 CQ WORLD WIDE CW DX CONTEST\r\nCall: K1ABC\r\n****** Not In Log ******\r\nunknown row\r\n";
        { decolog::core::LogDatabase db; QVERIFY(db.open(log)); QString error; QVERIFY2(db.archiveContestReport(source,"source.rpt",&error)>0,qPrintable(error)); }
        const QString outputPath=dir.filePath("exported.rpt"); QByteArray output;
        QVERIFY2(launch(dir,QStringLiteral("cqwwexportprobe:")+outputPath,output,5000),output.constData());
        QVERIFY2(output.contains("PROBE lcr exported true") && !output.contains("TypeError"),output.constData());
        QFile file(outputPath); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(),source);
        decolog::core::LogDatabase db; QVERIFY(db.open(log)); QCOMPARE(db.qsoCount(),qint64(0)); QCOMPARE(db.contestReports().size(),1);
    }
    void contestReportHistoryReopensAfterRestart()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true); const auto log=dir.filePath("test.sqlite");
        const QByteArray source="Contest: 2024 CQ WORLD WIDE CW DX CONTEST\nCall: K1ABC\n****** Not In Log ******\n"
            "14020 CW 2024-11-23 1201 K1ABC 05 DL1ABC 14\nunknown row\n";
        { decolog::core::LogDatabase db; QVERIFY(db.open(log)); QString error; QVERIFY2(db.archiveContestReport(source,"source.rpt",&error)>0,qPrintable(error)); }
        QByteArray output; QVERIFY2(launch(dir,"cqwwhistoryprobe",output,5000),output.constData());
        QVERIFY2(output.contains("PROBE lcr history 1 ") && output.contains("PROBE lcr reopened 1 1 ") && !output.contains("TypeError"),output.constData());
        decolog::core::LogDatabase db; QVERIFY(db.open(log)); const auto reports=db.contestReports(); QCOMPARE(reports.size(),1);
        QCOMPARE(db.contestReportBytes(reports.first().id),source); QCOMPARE(db.qsoCount(),qint64(0));
    }
    void contestReportArchiveSavesReviewedCopy()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        const QString path=dir.filePath("report.rpt"); QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray source="Contest: 2024 CQ WORLD WIDE CW DX CONTEST\nCall: K1ABC\n****** Not In Log ******\nunknown row\n";
        QCOMPARE(file.write(source),source.size()); file.close(); QByteArray output;
        QVERIFY2(launch(dir,QStringLiteral("cqwwarchiveprobe:")+path,output,5000),output.constData());
        QVERIFY2(output.contains("PROBE lcr archive true") && !output.contains("TypeError"),output.constData());
        decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); const auto reports=db.contestReports();
        QCOMPARE(reports.size(),1); QCOMPARE(db.contestReportBytes(reports.first().id),source); QCOMPARE(db.qsoCount(),qint64(0));
    }
    void contestReportReviewShowsRecognisedAndUnknownLines()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        const QString path=dir.filePath("report.rpt"); QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray source="Contest: 2024 CQ WORLD WIDE CW DX CONTEST\nCall: K1ABC\n"
            "****** Incorrect Call ******\n 14020 CW 2024-11-23 1201 K1ABC 05 DL1BAD 14 correct DL1GOOD\n"
            "unknown row\n****** Summary ******\nother text\n";
        QCOMPARE(file.write(source),source.size()); file.close();
        QByteArray output; QVERIFY2(launch(dir,QStringLiteral("cqwwreportprobe:")+path,output,4000),output.constData());
        QVERIFY2(output.contains("PROBE lcr ready 1 1 2 ") && !output.contains("TypeError"),output.constData());
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(),source);
        decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QCOMPARE(db.qsoCount(),qint64(0));
    }
    void callQueueDoesNotOverwriteCurrentQso()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        QByteArray output; QVERIFY2(launch(dir,"callqueueprobe",output,3500),output.constData());
        QVERIFY2(output.contains("PROBE callqueue blocked=true current=K1BUSY count=2 duplicate=true"),output.constData());
        QVERIFY2(output.contains("PROBE callqueue loaded=true current=DL1XYZ/P next=K1ABC qsos=0"),output.constData());
    }
    void qsoEditorWarnsWhenDerivedInputsChange()
    {
        using namespace decolog::core; QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true); QString originalTrace;
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
          const QVariantList defs{QVariantMap{{"name","APP_A"},{"type","number"}},QVariantMap{{"name","APP_B"},{"type","number"},{"formula","{APP_A} * 2"}},QVariantMap{{"name","APP_C"},{"type","number"},{"formula","{APP_B} + 1"}}};
          QVariantMap values{{"APP_A","5"}}; const auto computed=newqso::computeDerivedValues(defs,values,QLocale::c()); QVERIFY(computed.error.isEmpty());
          for (auto it=computed.values.cbegin();it!=computed.values.cend();++it) values.insert(it.key(),it.value());
          originalTrace=newqso::makeDerivedTrace(defs,values,computed.values,QLocale::c()); QVERIFY(!originalTrace.isEmpty());
          AdifRecord record{{"CALL","K1DER"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}};
          for (auto it=values.cbegin();it!=values.cend();++it) record.set(it.key(),it.value().toString()); record.set("APP_DECOLOG_DERIVED_V1",originalTrace);
          QVERIFY(db.insertQso(record,"fixture",{},true).id>0); }
        QByteArray output; QVERIFY2(launch(dir,"derivededitprobe",output,4000),output.constData());
        QVERIFY2(output.contains("PROBE derived before matches") && output.contains("PROBE derived editing stale true APP_B,APP_C") && output.contains("PROBE derived saved stale true"),output.constData());
        QByteArray reopened; QVERIFY2(launch(dir,"derivedrestoreprobe",reopened,3000),reopened.constData());
        QVERIFY2(reopened.contains("PROBE derived restored stale true APP_B,APP_C"),reopened.constData());
        LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); const auto record=db.record(1); QVERIFY(record);
        QCOMPARE(record->value("APP_A"),QString("6")); QCOMPARE(record->value("APP_B"),QString("10")); QCOMPARE(record->value("APP_C"),QString("11"));
        QCOMPARE(record->value("APP_DECOLOG_DERIVED_V1"),originalTrace); QCOMPARE(db.qsoCount(),qint64(1));
        const QString previewImage=dir.filePath("derived-popup.png"); QByteArray previewOutput; QVERIFY2(launch(dir,QStringLiteral("derivedpreviewprobe:")+previewImage,previewOutput,3000),previewOutput.constData());
        QVERIFY2(previewOutput.contains("PROBE derived visual preview true 2") && previewOutput.contains("PROBE derived image true") && !previewOutput.contains("TypeError"),previewOutput.constData());
        const QString imageCopy=qEnvironmentVariable("DECO_DERIVED_PREVIEW_PNG"); if (!imageCopy.isEmpty()) QVERIFY(QFile::copy(previewImage,imageCopy));
        QByteArray recalculated; QVERIFY2(launch(dir,"derivedrecalcprobe",recalculated,4000),recalculated.constData());
        QVERIFY2(recalculated.contains("PROBE derived preview true") && recalculated.contains("db=10") && recalculated.contains("PROBE derived cancelled 10"),recalculated.constData());
        QVERIFY2(recalculated.contains("PROBE derived stale preview The QSO or draft changed; review the calculation again. 10"),recalculated.constData());
        QVERIFY2(recalculated.contains("PROBE derived changed QSO  The QSO or draft changed; review the calculation again. 10"),recalculated.constData());
        QVERIFY2(recalculated.contains("PROBE derived guarded save  The QSO changed; reload and review before saving. db=10"),recalculated.constData());
        QVERIFY2(recalculated.contains("PROBE derived draft 12 13 matches db=10") && recalculated.contains("PROBE derived recalculated matches 12 13"),recalculated.constData());
        const auto after=db.record(1); QVERIFY(after); QCOMPARE(after->value("APP_A"),QString("6")); QCOMPARE(after->value("APP_B"),QString("12")); QCOMPARE(after->value("APP_C"),QString("13"));
        QVERIFY(after->value("APP_DECOLOG_DERIVED_V1")!=originalTrace); QCOMPARE(after->value("COMMENT"),QString("late edit"));
        const auto history=db.history(1); QVERIFY(std::any_of(history.cbegin(),history.cend(),[](const HistoryEntry& h) { return h.record.value("APP_A")=="6" && h.record.value("APP_B")=="10"; }));
    }
    void namedReportsSurviveRestartAndSupportExplicitRemoval()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
          for (const QString& call : {QString("K1AAA"),QString("K1BBB")}) QVERIFY(db.insertQso(decolog::core::AdifRecord{{"CALL",call},{"QSO_DATE","20260102"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}},"fixture",{},true).id>0);
          db.setSetting("activity_report_v1",R"({"year":2024,"band":"40m"})"); }
        QByteArray created; QVERIFY2(launch(dir,"namedreportprobe",created,5000),created.constData());
        QVERIFY2(created.contains("PROBE named saved [\"A band mode\",\"B club\"]"),created.constData());
        QByteArray restored; QVERIFY2(launch(dir,"namedrestoreprobe",restored,5000),restored.constData());
        QVERIFY2(restored.contains("PROBE named restored [\"A band mode\",\"B club\"]") && restored.contains("PROBE named dimensions [\"band\",\"mode\"]"),restored.constData());
        QVERIFY2(restored.contains("PROBE named result") && restored.contains("\"value\":2") && restored.contains("PROBE named after remove [\"A band mode\"]"),restored.constData());
        decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QString error;
        const auto reports=decolog::core::activityreport::listDefinitions(db,&error); QVERIFY2(error.isEmpty(),qPrintable(error)); QCOMPARE(reports.size(),1); QCOMPARE(reports.first().name,QString("A band mode"));
        QVERIFY(db.setting("activity_report_v1").contains("2024")); QCOMPARE(db.qsoCount(),qint64(2));
    }
    void customReportGroupsAdifFieldsAndExports()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
          for (const QString& call : {QString("K1AAA"),QString("K1AAA"),QString("K1BBB")})
              QVERIFY(db.insertQso(decolog::core::AdifRecord{{"CALL",call},{"QSO_DATE","20260102"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"APP_DISTRICT","North"}},"fixture",{},true,0,false).id>0); }
        const QString file=dir.filePath("custom.csv"); QByteArray output;
        QVERIFY2(launch(dir,QStringLiteral("customreportprobe:")+file,output,5000),output.constData());
        QVERIFY2(output.contains("PROBE activityreport rendered 20m · North · 2"),output.constData());
        QVERIFY2(output.contains("\"aggregate\":\"calls\"") && output.contains("\"dimensions\":[\"band\",\"custom:APP_DISTRICT\"]"),output.constData());
        QFile csv(file); QVERIFY(csv.open(QIODevice::ReadOnly)); QCOMPARE(csv.readAll(),QByteArray("\"band\",\"custom:APP_DISTRICT\",distinct_calls\r\n\"20m\",\"North\",2\r\n"));
        const QString pdfPath=dir.filePath("custom.pdf"); QByteArray pdfOutput; QVERIFY2(launch(dir,QStringLiteral("custompdfprobe:")+pdfPath,pdfOutput,5000),pdfOutput.constData());
        QVERIFY2(pdfOutput.contains("PROBE activityreport pdf success=true"),pdfOutput.constData()); QFile pdf(pdfPath); QVERIFY(pdf.open(QIODevice::ReadOnly)); QCOMPARE(pdf.read(5),QByteArray("%PDF-")); pdf.close();
        const QString pdfCopy=qEnvironmentVariable("DECO_CUSTOM_PDF_COPY"); if (!pdfCopy.isEmpty()) QVERIFY(QFile::copy(pdfPath,pdfCopy));
        QByteArray restored; QVERIFY2(launch(dir,"customrestoreprobe",restored,5000),restored.constData());
        QVERIFY2(restored.contains("PROBE custom restored [\"band\",\"custom:APP_DISTRICT\"]") && restored.contains("PROBE custom restored result") && restored.contains("\"d1\":\"North\"") && restored.contains("\"value\":2"),restored.constData());
        decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QCOMPARE(db.qsoCount(),qint64(3));
    }
    void activityReportExportsMultipagePdf()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
          for (int i=0;i<100;++i) {
              const QString antenna=QString("Antenna %1").arg(i,3,10,QLatin1Char('0')) + (i==0 ? QString("\n")+QString(200,QLatin1Char('X')) : QString());
              QVERIFY(db.insertQso(decolog::core::AdifRecord{{"CALL",QString("K%1AA").arg(i)},{"QSO_DATE","20260102"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"MY_ANTENNA",antenna},{"MY_GRIDSQUARE","JM75"}},"fixture",{},true).id>0);
          } }
        const QString file=dir.filePath("activity.pdf"); QByteArray output;
        QVERIFY2(launch(dir,QStringLiteral("activitypdfprobe:")+file,output,7000),output.constData());
        QVERIFY2(output.contains("PROBE activityreport pdf success=true"),output.constData());
        QVERIFY2(output.contains("PROBE activityreport pdf protected Choose a file other than the log database"),output.constData());
        QFile pdf(file); QVERIFY(pdf.open(QIODevice::ReadOnly)); QVERIFY(pdf.read(5)==QByteArray("%PDF-")); QVERIFY(pdf.size()>1000); pdf.close();
        const QString pdfinfo=QStandardPaths::findExecutable("pdfinfo");
        if (!pdfinfo.isEmpty()) { QProcess p; p.start(pdfinfo,{file}); QVERIFY(p.waitForFinished(10000)); QCOMPARE(p.exitCode(),0);
            const auto match=QRegularExpression("Pages:\\s+(\\d+)").match(QString::fromUtf8(p.readAllStandardOutput())); QVERIFY(match.hasMatch()); QVERIFY(match.captured(1).toInt()>1); }
        const QString pdftotext=QStandardPaths::findExecutable("pdftotext");
        if (!pdftotext.isEmpty()) { QProcess p; p.start(pdftotext,{"-layout",file,"-"}); QVERIFY(p.waitForFinished(10000)); QCOMPARE(p.exitCode(),0);
            const QByteArray text=p.readAllStandardOutput(); QVERIFY(text.contains("Antenna 099")); QVERIFY(text.contains("Snapshot")); QVERIFY(text.contains("Page 1")); }
        const QString copy=qEnvironmentVariable("DECO_REPORT_PDF_COPY"); if (!copy.isEmpty()) QVERIFY(QFile::copy(file,copy));
        decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QCOMPARE(db.qsoCount(),qint64(100));
    }
    void activityReportRunsAndExportsRecordedStationData()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
          for (const QString& call : {QString("K1AAA"),QString("K1BBB")})
              QVERIFY(db.insertQso(decolog::core::AdifRecord{{"CALL",call},{"QSO_DATE","20260102"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"MY_ANTENNA","Dipole"},{"MY_GRIDSQUARE","JM75"}},"fixture",{},true).id>0);
          QVERIFY(db.insertQso(decolog::core::AdifRecord{{"CALL","K1CCC"},{"QSO_DATE","20250102"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"MY_ANTENNA","Yagi"}},"fixture",{},true).id>0);
          db.setSetting("activity_report_v1",R"({"year":2024,"band":"40m"})"); }
        const QString file=dir.filePath("report.csv"); QByteArray output;
        QVERIFY2(launch(dir,QStringLiteral("activityreportprobe:")+file,output,5000),output.constData());
        QVERIFY2(output.contains("PROBE activityreport result") && output.contains("\"antenna\":\"Dipole\"") && output.contains("\"count\":2") && output.contains("PROBE activityreport export "),output.constData());
        QVERIFY2(output.contains("PROBE activityreport settings {\"band\":\"20m\",\"kind\":\"activity\",\"year\":2026}"),output.constData());
        QVERIFY2(output.contains("PROBE activityreport prior settings {\"band\":\"40m\",\"kind\":\"activity\",\"year\":2024}"),output.constData());
        QVERIFY2(output.contains("PROBE activityreport protected Choose a file other than the log database"),output.constData());
        QVERIFY2(output.contains("PROBE activityreport rendered Dipole · 20m · JM75 · 2"),output.constData());
        QFile csv(file); QVERIFY(csv.open(QIODevice::ReadOnly)); QCOMPARE(csv.readAll(),QByteArray("antenna,band,own_grid,qso_count\r\n\"Dipole\",\"20m\",\"JM75\",2\r\n"));
        decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QCOMPARE(db.qsoCount(),qint64(3)); QVERIFY(db.setting("activity_report_v1").contains("2026"));
    }
    void confirmationReportShowsMissingDatesAndExports()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
          for (const QString& call : {QString("K1AAA"),QString("K1BBB")})
              QVERIFY(db.insertQso(decolog::core::AdifRecord{{"CALL",call},{"QSO_DATE","20260102"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}},"fixture",{},true).id>0);
          QSqlQuery q(db.connection()); QVERIFY(q.exec("INSERT INTO qsl_status(qso_id,service,rcvd,rcvd_date) VALUES(1,'card','Y','20260104'),(2,'card','Y',NULL)")); }
        const QString file=dir.filePath("confirmation.csv"); QByteArray output;
        QVERIFY2(launch(dir,QStringLiteral("confirmationreportprobe:")+file,output,5000),output.constData());
        QVERIFY2(output.contains("PROBE activityreport rendered card · 2 · 1 · 1 · 0 · 0 · 2.00"),output.constData());
        QVERIFY2(output.contains("PROBE activityreport settings {\"band\":\"20m\",\"kind\":\"confirmation\",\"year\":2026}"),output.constData());
        QFile csv(file); QVERIFY(csv.open(QIODevice::ReadOnly)); QCOMPARE(csv.readAll(),QByteArray("service,confirmed,dated,missing_date,invalid_date,earlier_date,average_days\r\n\"card\",2,1,1,0,0,2.00\r\n"));
        const QString pdfPath=dir.filePath("confirmation.pdf"); QByteArray pdfOutput; QVERIFY2(launch(dir,QStringLiteral("confirmationpdfprobe:")+pdfPath,pdfOutput,5000),pdfOutput.constData());
        QVERIFY2(pdfOutput.contains("PROBE activityreport pdf success=true"),pdfOutput.constData()); QFile pdf(pdfPath); QVERIFY(pdf.open(QIODevice::ReadOnly)); QCOMPARE(pdf.read(5),QByteArray("%PDF-")); pdf.close();
        const QString pdfCopy=qEnvironmentVariable("DECO_CONFIRM_PDF_COPY"); if (!pdfCopy.isEmpty()) QVERIFY(QFile::copy(pdfPath,pdfCopy));
        decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QCOMPARE(db.qsoCount(),qint64(2));
    }
    void clubReportFiltersRecordedStationAndExports()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
          for (const QString& call : {QString("K1AAA"),QString("K1BBB")})
              QVERIFY(db.insertQso(decolog::core::AdifRecord{{"CALL",call},{"QSO_DATE","20260102"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"STATION_CALLSIGN","9H1SR"},{"OPERATOR","9H1AA"}},"fixture",{},true).id>0);
          QVERIFY(db.insertQso(decolog::core::AdifRecord{{"CALL","K1CCC"},{"QSO_DATE","20260102"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"STATION_CALLSIGN","9H9ZZ"},{"OPERATOR","9H1AA"}},"fixture",{},true).id>0); }
        const QString file=dir.filePath("club.csv"); QByteArray output;
        QVERIFY2(launch(dir,QStringLiteral("clubreportprobe:")+file,output,5000),output.constData());
        QVERIFY2(output.contains("PROBE activityreport rendered 2026 · 9H1SR · 9H1AA · 2"),output.constData());
        QVERIFY2(output.contains("PROBE activityreport settings {\"band\":\"20m\",\"kind\":\"club\",\"station\":\"9H1SR\",\"year\":2026}"),output.constData());
        QFile csv(file); QVERIFY(csv.open(QIODevice::ReadOnly)); QCOMPARE(csv.readAll(),QByteArray("utc_year,station_callsign,operator,qso_count\r\n\"2026\",\"9H1SR\",\"9H1AA\",2\r\n"));
        const QString pdfPath=dir.filePath("club.pdf"); QByteArray pdfOutput; QVERIFY2(launch(dir,QStringLiteral("clubpdfprobe:")+pdfPath,pdfOutput,5000),pdfOutput.constData());
        QVERIFY2(pdfOutput.contains("PROBE activityreport pdf success=true"),pdfOutput.constData()); QFile pdf(pdfPath); QVERIFY(pdf.open(QIODevice::ReadOnly)); QCOMPARE(pdf.read(5),QByteArray("%PDF-")); pdf.close();
        const QString pdfCopy=qEnvironmentVariable("DECO_CLUB_PDF_COPY"); if (!pdfCopy.isEmpty()) QVERIFY(QFile::copy(pdfPath,pdfCopy));
        decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QCOMPARE(db.qsoCount(),qint64(3));
    }
    void typedCustomFieldsValidateBeforeSavingQso()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true); QByteArray output;
        QVERIFY2(launch(dir,"typedfieldsprobe",output,4500),output.constData());
        QVERIFY2(output.contains("PROBE typed editor number W") && output.contains("\"type\":\"number\"") && output.contains("\"unit\":\"W\""),output.constData());
        QVERIFY2(output.contains("\"invalid\":\"APP_TYPED_POWER:") && output.contains("\"missing\":\"APP_TYPED_NOTE:") && output.contains("\"valid\":\"\""),output.constData());
        QVERIFY2(output.contains("\"supplied\":\"APP_TYPED_DOUBLE:") && output.contains("\"formula\":\"{APP_TYPED_POWER} * 2\""),output.constData());
        QVERIFY2(output.contains("\"conflict\":\"APP_DECOLOG_DERIVED_V1 conflicts"),output.constData());
        decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QCOMPARE(db.qsoCount(),qint64(1));
        const auto record=db.record(1); QVERIFY(record); QCOMPARE(record->value("APP_TYPED_POWER"),QString("12")); QCOMPARE(record->value("APP_TYPED_NOTE"),QString("portable"));
        QCOMPARE(record->value("APP_TYPED_DOUBLE"),QString("24"));
        const auto trace=QJsonDocument::fromJson(record->value("APP_DECOLOG_DERIVED_V1").toUtf8()).object(); QCOMPARE(trace.value("version").toInt(),2);
        QVERIFY(!trace.value("inputLocale").toString().isEmpty()); QCOMPARE(trace.value("fields").toObject().value("APP_TYPED_DOUBLE").toObject().value("expression").toString(),QString("{APP_TYPED_POWER} * 2"));
        QCOMPARE(trace.value("fields").toObject().value("APP_TYPED_DOUBLE").toObject().value("inputs").toObject().value("APP_TYPED_POWER").toString(),QString("12"));
        QVERIFY(db.exportAdif(QList<qint64>{1}).contains("<app_typed_power:2>12"));
        QVERIFY(db.exportAdif(QList<qint64>{1}).contains("<app_typed_double:2>24"));
        decolog::core::LogDatabase roundtrip; QVERIFY(roundtrip.open(":memory:")); QCOMPARE(roundtrip.importAdif(db.exportAdif(QList<qint64>{1})).inserted,1);
        const auto imported=roundtrip.record(1); QVERIFY(imported); QCOMPARE(imported->value("APP_DECOLOG_DERIVED_V1"),record->value("APP_DECOLOG_DERIVED_V1"));
        QSettings settings(iniPath(dir),QSettings::IniFormat); const auto defs=decolog::core::newqso::decodeCustomFields(settings.value("ui/newQso/customFields").toString());
        QVERIFY(std::any_of(defs.cbegin(),defs.cend(),[](const QVariant& v) { const auto d=v.toMap(); return d.value("name")=="APP_TYPED_DOUBLE" && d.value("formula")=="{APP_TYPED_POWER} * 2"; }));
    }
    void duplicateReviewGuard_data()
    {
        QTest::addColumn<bool>("changeQsl"); QTest::newRow("unchanged") << false; QTest::newRow("changed-qsl") << true;
    }
    void duplicateReviewGuard()
    {
        QFETCH(bool,changeQsl); using namespace decolog::core; QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        const QString path=dir.filePath("test.sqlite");
        { LogDatabase db; QVERIFY(db.open(path)); const AdifRecord r{{"CALL","K1ABC"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}};
          QVERIFY(db.insertQso(r,"fixture",{},true,0,false).id>0); QVERIFY(db.insertQso(r,"fixture",{},true,0,false).id>0); }
        QProcessEnvironment env=QProcessEnvironment::systemEnvironment(); env.insert("QT_QPA_PLATFORM","offscreen"); env.insert("QT_QUICK_BACKEND","software");
        env.insert("DECODXLOG_DATA_ROOT",dir.filePath("data")); env.insert("DECODXLOG_GRAB_MS","7000");
        QProcess app; app.setProcessEnvironment(env); app.setProcessChannelMode(QProcess::MergedChannels);
        app.start(QStringLiteral(DECOLOG_APP),{"--software-renderer","--settings",dir.filePath("settings"),"--db",path,"--port","0","--show","duplicateguardprobe","--grab",dir.filePath("screen.png")});
        QVERIFY(app.waitForStarted(10000)); QByteArray output; QElapsedTimer timer; timer.start();
        while (!output.contains("PROBE duplicateguard ready") && timer.elapsed()<15000) { app.waitForReadyRead(100); output+=app.readAll(); }
        QVERIFY2(output.contains("PROBE duplicateguard ready"),output.constData());
        if (changeQsl) { LogDatabase db; QVERIFY(db.open(path)); QSqlQuery q(db.connection()); QVERIFY(q.exec("INSERT INTO qsl_status(qso_id,service,rcvd) VALUES(2,'card','Y')")); }
        QVERIFY(app.waitForFinished(20000)); output+=app.readAll(); QCOMPARE(app.exitStatus(),QProcess::NormalExit); QCOMPARE(app.exitCode(),0);
        QVERIFY2(output.contains(changeQsl?"PROBE duplicateguard accepted=false":"PROBE duplicateguard accepted=true"),output.constData());
        LogDatabase check; QVERIFY(check.open(path)); QCOMPARE(check.qsoCount(),qint64(changeQsl?2:1));
    }

    void externalCampaignAppearsWithoutRestart_data()
    {
        QTest::addColumn<bool>("editExisting"); QTest::newRow("created") << false; QTest::newRow("changed-callsign") << true;
    }
    void externalCampaignAppearsWithoutRestart()
    {
        QFETCH(bool,editExisting);
        using namespace decolog::core; QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        const QString path=dir.filePath("test.sqlite");
        QString campaignId;
        { LogDatabase db; QVERIFY(db.open(path)); if (editExisting) { const auto now=QDateTime::currentDateTimeUtc(); dxcampaign::Campaign c;
          c.title="External watch"; c.calls={"3Y0P"}; c.start=now.date().addDays(-1); c.end=now.date().addDays(1);
          c.source="fixture"; c.verifiedAt=now; c.bands={"20m"}; c.modes={"CW"}; c.watched=true;
          QString error; QVERIFY2(dxcampaign::save(db,c,&error),qPrintable(error)); campaignId=c.id; } }
        QProcessEnvironment env=QProcessEnvironment::systemEnvironment(); env.insert("QT_QPA_PLATFORM","offscreen");
        env.insert("QT_QUICK_BACKEND","software"); env.insert("DECODXLOG_DATA_ROOT",dir.filePath("data"));
        env.insert("DECODXLOG_GRAB_MS","6500"); env.insert("DECODXLOG_TRACE_ACTIVITY","1");
        QProcess app; app.setProcessEnvironment(env); app.setProcessChannelMode(QProcess::MergedChannels);
        app.start(QStringLiteral(DECOLOG_APP),{"--software-renderer","--settings",dir.filePath("settings"),"--db",path,"--port","0","--show","dxexternalwatchprobe","--grab",dir.filePath("screen.png")});
        QVERIFY(app.waitForStarted(10000)); QByteArray output; QElapsedTimer timer; timer.start();
        while (!output.contains("PROBE dxexternal ready") && timer.elapsed()<15000) { app.waitForReadyRead(100); output+=app.readAll(); }
        QVERIFY2(output.contains("PROBE dxexternal ready"),output.constData());
        { LogDatabase db; QVERIFY(db.open(path)); const auto now=QDateTime::currentDateTimeUtc(); dxcampaign::Campaign c;
          if (editExisting) { const auto saved=dxcampaign::load(db,campaignId); QVERIFY(saved); c=*saved; }
          else { c.title="External watch"; c.start=now.date().addDays(-1); c.end=now.date().addDays(1);
            c.source="fixture"; c.verifiedAt=now; c.bands={"20m"}; c.modes={"CW"}; c.watched=true; }
          c.calls={"3Y0J"};
          QString error; QVERIFY2(dxcampaign::save(db,c,&error),qPrintable(error)); }
        QVERIFY(app.waitForFinished(20000)); output+=app.readAll(); QCOMPARE(app.exitStatus(),QProcess::NormalExit); QCOMPARE(app.exitCode(),0);
        QVERIFY2(output.contains("PROBE dxexternal sent") && output.contains("External watch: 3Y0J"),output.constData());
    }

    void externalWriteDiscardsPendingGoalAlertInActualApp_data()
    {
        QTest::addColumn<bool>("satisfiesGoal");
        QTest::newRow("goal-satisfied") << true;
        QTest::newRow("unrelated-write") << false;
    }
    void externalWriteDiscardsPendingGoalAlertInActualApp()
    {
        QFETCH(bool, satisfiesGoal);
        using namespace decolog::core;
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        const QString path=dir.filePath("test.sqlite");
        { LogDatabase db; QVERIFY(db.open(path)); const auto now=QDateTime::currentDateTimeUtc();
          dxcampaign::Campaign c; c.title="Race watch"; c.calls={"3Y0J"}; c.start=now.date().addDays(-1); c.end=now.date().addDays(1);
          c.source="synthetic"; c.verifiedAt=now; c.bands={"20m"}; c.modes={"CW"}; c.watched=true; c.watchGoalsOnly=true;
          c.goals={{"20m","CW","worked"}}; QString error; QVERIFY2(dxcampaign::save(db,c,&error),qPrintable(error)); }
        QProcessEnvironment env=QProcessEnvironment::systemEnvironment();
        env.insert("QT_QPA_PLATFORM","offscreen"); env.insert("QT_QUICK_BACKEND","software");
        env.insert("DECODXLOG_DATA_ROOT",dir.filePath("data")); env.insert("DECODXLOG_GRAB_MS","9000"); env.insert("DECODXLOG_TRACE_ACTIVITY","1");
        env.insert("DECOLOG_TEST_DX_WATCH_TRACE","1");
        QProcess app; app.setProcessEnvironment(env); app.setProcessChannelMode(QProcess::MergedChannels);
        app.start(QStringLiteral(DECOLOG_APP),{"--software-renderer","--settings",dir.filePath("settings"),"--db",path,"--port","0","--show","dxgoalraceprobe","--grab",dir.filePath("screen.png")});
        QVERIFY(app.waitForStarted(10000));
        QByteArray output; QElapsedTimer timer; timer.start();
        while (!output.contains("PROBE dxgoalrace queued") && timer.elapsed()<15000) {
            app.waitForReadyRead(100); output += app.readAll();
        }
        QVERIFY2(output.contains("PROBE dxgoalrace queued"), output.constData());
        while (!output.contains("PROBE dxgoalrace worker needed=1") && timer.elapsed()<15000) {
            app.waitForReadyRead(100); output += app.readAll();
        }
        QVERIFY2(output.contains("PROBE dxgoalrace worker needed=1"), output.constData());
        QVERIFY2(!output.contains("PROBE dxgoalrace resumed"), output.constData());
        { LogDatabase writer; QVERIFY(writer.open(path)); const auto now=QDateTime::currentDateTimeUtc();
          const auto qso=writer.insertQso({{"CALL",satisfiesGoal ? "3Y0J" : "K1OTHER"},{"QSO_DATE",now.date().toString("yyyyMMdd")},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}},"fixture");
          QCOMPARE(qso.status,InsertResult::Status::Inserted); }
        output += app.readAll();
        QVERIFY2(!output.contains("PROBE dxgoalrace resumed"), output.constData());
        QVERIFY(app.waitForFinished(20000)); output+=app.readAll();
        QVERIFY2(output.contains("PROBE dxgoalrace resumed"),output.constData());
        QCOMPARE(app.exitStatus(),QProcess::NormalExit); QCOMPARE(app.exitCode(),0);
        QVERIFY2(output.count("[activity] DXCAMPAIGN|highlight|")== (satisfiesGoal ? 0 : 1),output.constData());
    }

    void reportContextRejectsHardLinkToLog()
    {
#ifndef Q_OS_UNIX
        QSKIP("Hard-link fixture uses POSIX link");
#endif
#ifdef Q_OS_UNIX
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true); const QString log=dir.filePath("test.sqlite"), csv=dir.filePath("empty.csv"), alias=csv+".alias";
        { decolog::core::LogDatabase db; QVERIFY(db.open(log)); QVERIFY(db.insertQso(decolog::core::AdifRecord{{"CALL","K1ABC"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}},"fixture").id>0); }
        QCOMPARE(::link(QFile::encodeName(log).constData(),QFile::encodeName(alias).constData()),0);
        const auto before=decolog::core::fileidentity::read(log); QVERIFY(!before.isEmpty()); QCOMPARE(decolog::core::fileidentity::read(alias),before);
        QByteArray output; QVERIFY2(launch(dir,QStringLiteral("activitymodealiasprobe:")+csv,output,5500),output.constData());
        QVERIFY2(output.contains("PROBE activityreport alias rejected=true csv=true pdf=true"),output.constData());
        QCOMPARE(decolog::core::fileidentity::read(alias),before); QCOMPARE(decolog::core::fileidentity::read(log),before);
#endif
    }

    void reportExportsRejectSymlinkToWal()
    {
#ifndef Q_OS_UNIX
        QSKIP("Symbolic-link fixture uses POSIX symlink");
#endif
#ifdef Q_OS_UNIX
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true); const QString log=dir.filePath("test.sqlite"), csv=dir.filePath("empty.csv"), alias=csv+".alias";
        { decolog::core::LogDatabase db; QVERIFY(db.open(log)); QVERIFY(db.insertQso(decolog::core::AdifRecord{{"CALL","K1ABC"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}},"fixture").id>0); }
        QCOMPARE(::symlink(QFile::encodeName(log+"-wal").constData(),QFile::encodeName(alias).constData()),0);
        QByteArray output; QVERIFY2(launch(dir,QStringLiteral("activitymodealiasprobe:")+csv,output,5500),output.constData());
        QVERIFY2(output.contains("PROBE activityreport alias rejected=true csv=true pdf=true"),output.constData());
        decolog::core::LogDatabase db; QVERIFY(db.open(log)); QCOMPARE(db.qsoCount(),qint64(1));
#endif
    }

    void emptyCsvReportContextPreservesFilters()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true); const auto csvPath=dir.filePath("empty.csv");
        { decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
          QVERIFY(db.insertQso(decolog::core::AdifRecord{{"CALL","K1ABC"},{"QSO_DATE","20260101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}},"fixture").id>0); }
        QByteArray output; QVERIFY2(launch(dir,QStringLiteral("activitymodeprobe:")+csvPath,output,5500),output.constData());
        QVERIFY2(output.contains("PROBE activityreport context "),output.constData());
        QVERIFY2(output.contains("PROBE activityreport context protected Choose a file other than the log database") || output.contains("PROBE activityreport context protected Scegli un file diverso dal database"),output.constData());
        QFile csv(csvPath); QVERIFY(csv.open(QIODevice::ReadOnly)); const auto data=csv.readAll(); QVERIFY(!data.contains("FT8"));
        QFile context(csvPath+".context.json"); QVERIFY(context.open(QIODevice::ReadOnly)); const auto object=QJsonDocument::fromJson(context.readAll()).object();
        QCOMPARE(object.value("format").toString(),QString("decodxlog-report-context/1")); QCOMPARE(object.value("mode").toString(),QString("FT8"));
        QCOMPARE(object.value("utcYear").toInt(),2026); QCOMPARE(object.value("band").toString(),QString("20m")); QCOMPARE(object.value("rows").toInt(),0);
        QCOMPARE(object.value("csvSha256").toString(),QString::fromLatin1(QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex()));
        QVERIFY(!object.contains("path") && !object.contains("logIdentity"));
    }

    void customReportRestoresUtcDayAndHour()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
          for (const auto* call : {"K1AAA","K1BBB"}) QVERIFY(db.insertQso(decolog::core::AdifRecord{{"CALL",call},{"QSO_DATE","20261231"},{"TIME_ON","2300"},{"BAND","20m"},{"MODE","CW"}},"fixture").id>0);
          QSqlQuery q(db.connection()); QVERIFY(q.exec("UPDATE qso SET qso_datetime_on=CASE call WHEN 'K1AAA' THEN '2027-01-01T01:10:00+02:00' ELSE '2026-12-31T23:20:00Z' END")); }
        QByteArray output; QVERIFY2(launch(dir,"activityutcprobe",output,5000),output.constData());
        QVERIFY2(output.contains("PROBE UTC grouping restored [\"day\",\"hour\"]"),output.constData());
        QVERIFY2(output.contains("\"d0\":\"2026-12-31\"") && output.contains("\"d1\":\"23\"") && output.contains("\"value\":2"),output.constData());
        QVERIFY2(!output.contains("TypeError"),output.constData());
    }

    void watchedCampaignRecognizesOffsetQsoAtUtcBoundary()
    {
        using namespace decolog::core; QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        {
            LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); const auto now=QDateTime::currentDateTimeUtc();
            dxcampaign::Campaign c; c.title="UTC watch"; c.calls={"3Y0J"}; c.start=now.date(); c.end=now.date();
            c.source="fixture"; c.verifiedAt=now; c.bands={"20m","40m","15m"}; c.modes={"CW"}; c.watched=true; c.watchGoalsOnly=true;
            c.goals={{"20m","CW","worked"}}; QString error; QVERIFY2(dxcampaign::save(db,c,&error),qPrintable(error));
            auto r=db.insertQso(AdifRecord{{"CALL","3Y0J"},{"QSO_DATE",now.date().toString("yyyyMMdd")},{"TIME_ON","2230"},{"BAND","20m"},{"MODE","CW"}},"fixture",{},true,0,false); QVERIFY(r.id>0);
            QSqlQuery q(db.connection()); q.prepare("UPDATE qso SET qso_datetime_on=? WHERE id=?"); q.addBindValue(now.date().addDays(1).toString(Qt::ISODate)+"T00:30:00+02:00"); q.addBindValue(r.id); QVERIFY(q.exec());
            QVERIFY(db.insertQso(AdifRecord{{"CALL","K1ZZZ"},{"QSO_DATE",now.date().toString("yyyyMMdd")},{"TIME_ON","1200"},{"BAND","40m"},{"MODE","CW"}},"fixture",{},true,0,false).id>0);
        }
        QByteArray output; QVERIFY2(launch(dir,"dxgoalwatchprobe",output,5500,true),output.constData());
        QVERIFY2(output.contains("PROBE dxgoalwatch done"),output.constData());
        QVERIFY2(output.count("[activity] DXCAMPAIGN|highlight|")==0,output.constData());
    }

    void watchedCampaignCanAlertOnlyForOpenGoals()
    {
        using namespace decolog::core;
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); const auto now=QDateTime::currentDateTimeUtc();
          dxcampaign::Campaign c; c.title="Goal watch"; c.calls={"3Y0J","3Y0K"}; c.start=now.date().addDays(-1); c.end=now.date().addDays(1);
          c.source="synthetic"; c.verifiedAt=now; c.bands={"20m","40m","15m"}; c.modes={"CW"}; c.watched=true; c.watchGoalsOnly=true;
          c.goals={{"20m","CW","worked"},{"40m","CW","confirmed"}}; QString error; QVERIFY2(dxcampaign::save(db,c,&error),qPrintable(error));
          for (const char* band : {"20m","40m"}) QVERIFY(db.insertQso(AdifRecord{{"CALL","3Y0J"},{"QSO_DATE",now.date().toString("yyyyMMdd")},{"TIME_ON","1200"},{"BAND",band},{"MODE","CW"}},"import").id>0); }
        QByteArray output; QVERIFY2(launch(dir,"dxgoalwatchprobe",output,5500,true),output.constData());
        QVERIFY2(output.contains("PROBE dxgoalwatch done"),output.constData());
        QCOMPARE(output.count("[activity] DXCAMPAIGN|highlight|"),1);
        const auto activityLines = output.split('\n');
        QByteArray alert;
        for (const auto &line : activityLines)
            if (line.contains("[activity] DXCAMPAIGN|highlight|")) alert = line;
        QVERIFY2(alert.contains("Goal watch: 3Y0J") && alert.contains("40m") && !alert.contains("20m") && !alert.contains("15m") && !alert.contains("3Y0K"), output.constData());
        QByteArray reopened; QVERIFY2(launch(dir,"dxgoalwatchprobe",reopened,5500,true),reopened.constData());
        QCOMPARE(reopened.count("[activity] DXCAMPAIGN|highlight|"),0);
    }
    void damagedCampaignDoesNotSuppressHealthyWatch()
    {
        using namespace decolog::core;
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); StationProfile p; p.name="A"; p.stationCallsign="9H1AA"; p.isDefault=true;
          QCOMPARE(db.saveStationProfile(p),qint64(1)); const auto now=QDateTime::currentDateTimeUtc();
          dxcampaign::Campaign c; c.title="Healthy"; c.calls={"3Y0J"}; c.start=now.date().addDays(-1); c.end=now.date().addDays(1);
          c.source="fixture"; c.verifiedAt=now; c.bands={"20m"}; c.modes={"CW"}; c.watched=true; c.stationProfileId=1;
          QString error; QVERIFY2(dxcampaign::save(db,c,&error),qPrintable(error));
          QSqlQuery q(db.connection()); QVERIFY(q.exec("INSERT INTO app_setting(key,value) VALUES('dx_campaign.broken','not json')")); }
        QByteArray output; QVERIFY2(launch(dir,"dxcampaignwatchprobe",output,5000,true),output.constData());
        QVERIFY2(output.contains("Healthy: 3Y0J") && output.contains("[activity] DXCAMPAIGN|warning|"),output.constData());
        QCOMPARE(output.count("[activity] DXCAMPAIGN|highlight|"),1);
    }
    void watchedCampaignAlertsRespectFiltersProfileAndRestart()
    {
        using namespace decolog::core;
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); StationProfile a; a.name="A"; a.stationCallsign="9H1AA"; a.isDefault=true;
          StationProfile b; b.name="B"; b.stationCallsign="9H1BB"; const qint64 first=db.saveStationProfile(a),second=db.saveStationProfile(b); QCOMPARE(first,qint64(1)); QCOMPARE(second,qint64(2));
          const auto now=QDateTime::currentDateTimeUtc(); dxcampaign::Campaign c; c.title="Tracked"; c.calls={"3Y0J","3Y0K"}; c.start=now.date().addDays(-1); c.end=now.date().addDays(1);
          c.source="synthetic"; c.verifiedAt=now; c.bands={"20m"}; c.modes={"CW"}; c.watched=true; c.stationProfileId=first;
          QString error; QVERIFY2(dxcampaign::save(db,c,&error),qPrintable(error));
          c.id.clear(); c.revision=0; c.title="Other profile"; c.calls={"3Y0P"}; c.stationProfileId=second; QVERIFY(dxcampaign::save(db,c,&error));
          c.id.clear(); c.revision=0; c.title="Not watched"; c.calls={"3Y0J"}; c.watched=false; c.stationProfileId=first; QVERIFY(dxcampaign::save(db,c,&error));
          c.id.clear(); c.revision=0; c.title="Archived"; c.watched=true; c.archived=true; QVERIFY(dxcampaign::save(db,c,&error)); }
        QByteArray output; QVERIFY2(launch(dir,"dxcampaignwatchprobe",output,5000,true),output.constData());
        QCOMPARE(output.count("[activity] DXCAMPAIGN|highlight|"),2);
        QVERIFY2(output.contains("Tracked: 3Y0J") && output.contains("Other profile: 3Y0P"),output.constData());
        QVERIFY2(!output.contains("Not watched:") && !output.contains("Archived:") && !output.contains("Tracked: 3Y0K"),output.constData());
        QByteArray reopened; QVERIFY2(launch(dir,"dxcampaignwatchprobe",reopened,5000,true),reopened.constData());
        QCOMPARE(reopened.count("[activity] DXCAMPAIGN|highlight|"),2);
    }
    void dxCampaignShowsOnlyMatchingFilteredRankedSpots()
    {
        using namespace decolog::core;
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); dxcampaign::Campaign c;
          const auto now=QDateTime::currentDateTimeUtc(); c.title="Live fixture"; c.calls={"3Y0J","3Y0K"}; c.start=now.date().addDays(-1); c.end=now.date().addDays(1);
          c.source="synthetic"; c.verifiedAt=now; c.bands={"20m","40m"}; c.modes={"CW"}; c.goals={{"20m","CW","worked"},{"40m","CW","worked"}};
          QVERIFY(db.insertQso(AdifRecord{{"CALL","3Y0J"},{"QSO_DATE",now.date().toString("yyyyMMdd")},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}},"import").id>0);
          QString error; QVERIFY2(dxcampaign::save(db,c,&error),qPrintable(error)); }
        QByteArray output; QVERIFY2(launch(dir,"dxcampaignspotprobe",output,6000),output.constData());
        QVERIFY2(output.contains("PROBE dxspots matching") && output.contains("\"call\":\"3Y0J\"") && output.contains("\"spotKey\":\"3Y0J|20m|CW\""),output.constData());
        QVERIFY2(output.contains("PROBE dxspots goalorder 3Y0K:true,3Y0J:false"),output.constData());
        QVERIFY2(output.contains("PROBE dxspots rendered 3Y0K") && output.contains("PROBE dxspots filtered []"),output.constData());
        QVERIFY2(!output.contains("\"call\":\"K1XYZ\"") && !output.contains("TypeError"),output.constData());
    }
    void dxCampaignMatrixRefreshesOnExternalWrite()
    {
        using namespace decolog::core; QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        const auto path=dir.filePath("test.sqlite");
        { LogDatabase db; QVERIFY(db.open(path)); dxcampaign::Campaign c; c.title="Spring"; c.calls={"ZZ1AAA"};
          c.start=QDate(2026,1,1); c.end=QDate(2026,1,7); c.source="fixture";
          c.verifiedAt=QDateTime::fromString("2025-12-31T10:00:00Z",Qt::ISODate); c.bands={"20m"}; c.modes={"CW"}; c.goals={{"20m","CW","worked"}};
          QString error; QVERIFY2(dxcampaign::save(db,c,&error),qPrintable(error)); }
        QProcessEnvironment env=QProcessEnvironment::systemEnvironment(); env.insert("QT_QPA_PLATFORM","offscreen"); env.insert("QT_QUICK_BACKEND","software"); env.insert("DECODXLOG_DATA_ROOT",dir.filePath("data")); env.insert("DECODXLOG_GRAB_MS","8000");
        QProcess app; app.setProcessEnvironment(env); app.setProcessChannelMode(QProcess::MergedChannels);
        app.start(QStringLiteral(DECOLOG_APP),{"--software-renderer","--settings",dir.filePath("settings"),"--db",path,"--port","0","--show","dxmatrixexternalprobe","--grab",dir.filePath("screen.png")});
        QVERIFY(app.waitForStarted(10000)); QByteArray output; QElapsedTimer timer; timer.start();
        while (!output.contains("PROBE dxmatrixexternal ready") && timer.elapsed()<15000 && app.state()!=QProcess::NotRunning) { app.waitForReadyRead(100); output+=app.readAll(); }
        QVERIFY2(output.contains("PROBE dxmatrixexternal ready") && output.contains("\"worked\":0"),output.constData());
        { LogDatabase writer; QVERIFY(writer.open(path)); QVERIFY(writer.insertQso({{"CALL","ZZ1AAA"},{"QSO_DATE","20260103"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}},"fixture").id>0); }
        QVERIFY(app.waitForFinished(20000)); output+=app.readAll(); QCOMPARE(app.exitStatus(),QProcess::NormalExit); QCOMPARE(app.exitCode(),0);
        const auto result=output.mid(output.indexOf("PROBE dxmatrixexternal result"));
        QVERIFY2(result.contains("\"worked\":1") && result.contains("\"needed\":false") && !result.contains("TypeError"),output.constData());
    }
    void dxCampaignMatrixRejectsReplacedLog()
    {
#ifndef Q_OS_UNIX
        QSKIP("This fixture replaces an open file and is currently tested on Unix only");
#else
        using namespace decolog::core; QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { QSettings settings(iniPath(dir),QSettings::IniFormat); settings.setValue("ui/language","en"); settings.sync(); }
        const auto path=dir.filePath("test.sqlite"), copy=dir.filePath("copy.sqlite"), moved=dir.filePath("original.sqlite");
        { LogDatabase db; QVERIFY(db.open(path)); dxcampaign::Campaign c; c.title="Spring"; c.calls={"ZZ1AAA"};
          c.start=QDate(2026,1,1); c.end=QDate(2026,1,7); c.source="fixture";
          c.verifiedAt=QDateTime::fromString("2025-12-31T10:00:00Z",Qt::ISODate); c.bands={"20m"}; c.modes={"CW"}; QString error; QVERIFY2(dxcampaign::save(db,c,&error),qPrintable(error)); }
        QVERIFY(QFile::copy(path,copy));
        { LogDatabase db; QVERIFY(db.open(copy)); QVERIFY(db.insertQso({{"CALL","ZZ1AAA"},{"QSO_DATE","20260103"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}},"fixture").id>0); }
        QProcessEnvironment env=QProcessEnvironment::systemEnvironment(); env.insert("QT_QPA_PLATFORM","offscreen"); env.insert("QT_QUICK_BACKEND","software"); env.insert("DECODXLOG_DATA_ROOT",dir.filePath("data")); env.insert("DECODXLOG_GRAB_MS","7000");
        QProcess app; app.setProcessEnvironment(env); app.setProcessChannelMode(QProcess::MergedChannels);
        app.start(QStringLiteral(DECOLOG_APP),{"--software-renderer","--settings",dir.filePath("settings"),"--db",path,"--port","0","--show","dxmatrixcontextprobe","--grab",dir.filePath("screen.png")});
        QVERIFY(app.waitForStarted(10000)); QByteArray output; QElapsedTimer timer; timer.start();
        while (!output.contains("PROBE dxmatrix ready") && timer.elapsed()<15000 && app.state()!=QProcess::NotRunning) { app.waitForReadyRead(100); output+=app.readAll(); }
        QVERIFY2(output.contains("PROBE dxmatrix ready") && output.contains("\"worked\":0"),output.constData());
        QVERIFY(QFile::rename(path,moved)); QVERIFY(QFile::copy(copy,path));
        QVERIFY(app.waitForFinished(20000)); output+=app.readAll(); QCOMPARE(app.exitStatus(),QProcess::NormalExit); QCOMPARE(app.exitCode(),0);
        QVERIFY2(output.contains("PROBE dxmatrix result {\"error\":\"Log changed\",\"rows\":[]}") && !output.contains("TypeError"),output.constData());
#endif
    }
    void dxCampaignDateReviewRejectsReplacedLog()
    {
#ifndef Q_OS_UNIX
        QSKIP("This fixture replaces an open file and is currently tested on Unix only");
#else
        using namespace decolog::core; QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { QSettings settings(iniPath(dir),QSettings::IniFormat); settings.setValue("ui/language","en"); settings.sync(); }
        const auto path=dir.filePath("test.sqlite"), copy=dir.filePath("copy.sqlite"), moved=dir.filePath("original.sqlite"); QString id;
        { LogDatabase db; QVERIFY(db.open(path)); dxcampaign::Campaign c; c.title="Spring"; c.calls={"ZZ1AAA"};
          c.start=QDate(2026,1,1); c.end=QDate(2026,1,7); c.source="fixture";
          c.verifiedAt=QDateTime::fromString("2025-12-31T10:00:00Z",Qt::ISODate); c.bands={"20m"}; c.modes={"CW"};
          QString error; QVERIFY2(dxcampaign::save(db,c,&error),qPrintable(error)); id=c.id; }
        QVERIFY(QFile::copy(path,copy)); QProcessEnvironment env=QProcessEnvironment::systemEnvironment();
        env.insert("QT_QPA_PLATFORM","offscreen"); env.insert("QT_QUICK_BACKEND","software"); env.insert("DECODXLOG_DATA_ROOT",dir.filePath("data")); env.insert("DECODXLOG_GRAB_MS","7000");
        QProcess app; app.setProcessEnvironment(env); app.setProcessChannelMode(QProcess::MergedChannels);
        app.start(QStringLiteral(DECOLOG_APP),{"--software-renderer","--settings",dir.filePath("settings"),"--db",path,"--port","0","--show","dxdatescontextprobe","--grab",dir.filePath("screen.png")});
        QVERIFY(app.waitForStarted(10000)); QByteArray output; QElapsedTimer timer; timer.start();
        while (!output.contains("PROBE dxcontext ready") && timer.elapsed()<15000 && app.state()!=QProcess::NotRunning) { app.waitForReadyRead(100); output+=app.readAll(); }
        QVERIFY2(output.contains("PROBE dxcontext ready true"),output.constData());
        QVERIFY(QFile::rename(path,moved)); QVERIFY(QFile::copy(copy,path));
        QVERIFY(app.waitForFinished(20000)); output+=app.readAll(); QCOMPARE(app.exitStatus(),QProcess::NormalExit); QCOMPARE(app.exitCode(),0);
        QVERIFY2(output.contains("PROBE dxcontext result true") && output.contains("The log changed; reopen the campaign review.") && !output.contains("TypeError"),output.constData());
        LogDatabase reopened; QVERIFY(reopened.open(path)); const auto campaign=dxcampaign::load(reopened,id); QVERIFY(campaign); QCOMPARE(campaign->end,QDate(2026,1,7));
#endif
    }
    void dxCampaignDateReviewChangesOnlyChosenExpedition()
    {
        using namespace decolog::core;
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true); QString firstId, secondId;
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
          dxcampaign::Campaign c; c.title="Spring"; c.calls={"ZZ1AAA"}; c.start=QDate(2026,1,1); c.end=QDate(2026,1,7);
          c.source="original source"; c.verifiedAt=QDateTime::fromString("2025-12-31T10:00:00Z",Qt::ISODate); c.bands={"20m"}; c.modes={"CW"}; c.watched=true; c.watchGoalsOnly=true;
          QString error; QVERIFY2(dxcampaign::save(db,c,&error),qPrintable(error)); firstId=c.id;
          c.id.clear(); c.revision=0; c.title="Autumn"; c.start=QDate(2026,10,1); c.end=QDate(2026,10,7); c.watched=false;
          QVERIFY2(dxcampaign::save(db,c,&error),qPrintable(error)); secondId=c.id;
          QVERIFY(db.insertQso(AdifRecord{{"CALL","ZZ1AAA"},{"QSO_DATE","20260108"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}},"import").id>0); }
        QByteArray output; QVERIFY2(launch(dir,"dxdatesprobe",output,7000),output.constData());
        QVERIFY2(output.contains("PROBE dxdates before") && output.contains("PROBE dxdates comparison") && output.contains("2026-01-07") && output.contains("2026-01-10"),output.constData());
        QVERIFY2(output.contains("PROBE dxdates cancel true") && output.contains("PROBE dxdates stale true") && output.contains("reload before saving"),output.constData());
        QVERIFY2(output.contains("PROBE dxdates after") && output.contains("PROBE dxdates matrix") && output.contains("\"worked\":1"),output.constData());
        QVERIFY2(!output.contains("TypeError"),output.constData());
        LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
        const auto first=dxcampaign::load(db,firstId), second=dxcampaign::load(db,secondId); QVERIFY(first && second);
        QCOMPARE(first->end,QDate(2026,1,10)); QCOMPARE(first->revision,3); QCOMPARE(first->calls,QStringList({"ZZ1AAA"})); QVERIFY(first->watched); QVERIFY(first->watchGoalsOnly);
        QCOMPARE(first->source,QString("https://www.danplanet.com/dxcal.ics")); QCOMPARE(first->verifiedAt.toString(Qt::ISODate),QString("2026-01-09T10:00:00Z"));
        QCOMPARE(second->end,QDate(2026,10,7)); QCOMPARE(second->revision,1); QCOMPARE(second->source,QString("original source")); QCOMPARE(db.qsoCount(),1);
    }
    void dxCampaignCalendarCreatesEditablePersistentMatrix()
    {
        using namespace decolog::core;
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
          const auto a=db.insertQso(AdifRecord{{"CALL","ZZ1AAA"},{"QSO_DATE","20260102"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}},"import"); QVERIFY(a.id>0);
          const auto b=db.insertQso(AdifRecord{{"CALL","ZZ1BBB"},{"QSO_DATE","20260103"},{"TIME_ON","1200"},{"BAND","40m"},{"MODE","FT8"}},"import"); QVERIFY(b.id>0);
          QslState card; card.service="card"; card.rcvd="Y"; QVERIFY(db.setCardState(b.id,card)); }
        QByteArray output; QVERIFY2(launch(dir,"dxcampaignprobe",output,12000),output.constData());
        QVERIFY2(output.contains("PROBE dxcampaign saved") && output.contains("ZZ1AAA") && output.contains("ZZ1BBB"),output.constData());
        QVERIFY2(output.contains("PROBE dxcampaign matrix") && output.contains("\"confirmed\":1") && output.contains("\"worked\":1"),output.constData());
        QVERIFY2(output.contains("PROBE dxcampaign goal draft") && output.contains("PROBE dxcampaign goal open") && output.contains("\"needed\":true"),output.constData());
        QVERIFY2(output.contains("PROBE dxcampaign archived true") && output.contains("PROBE dxcampaign reopened true"),output.constData());
        QVERIFY2(output.contains("PROBE dxcampaign new confirmation [{\"band\":\"20m\",\"confirmed\":1,\"mode\":\"CW\",\"needed\":false"),output.constData());
        QVERIFY2(!output.contains("TypeError"),output.constData());
        QByteArray restored; QVERIFY2(launch(dir,"dxcampaignrestoreprobe",restored,5000),restored.constData());
        QVERIFY2(restored.contains("PROBE dxcampaign restored matrix") && restored.contains("PROBE dxcampaign rendered 20m · CW · confirmed (1/1)"),restored.constData());
        LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QString error; const auto campaigns=dxcampaign::list(db,&error);
        QVERIFY2(error.isEmpty(),qPrintable(error)); QCOMPARE(campaigns.size(),1); QCOMPARE(campaigns.first().calls.size(),2);
        QCOMPARE(campaigns.first().source,QString("https://www.danplanet.com/dxcal.ics")); QCOMPARE(campaigns.first().revision,4); QVERIFY(!campaigns.first().archived); QVERIFY(campaigns.first().watched); QVERIFY(campaigns.first().watchGoalsOnly);
        QCOMPARE(campaigns.first().goals.size(),1); QCOMPARE(campaigns.first().goals.first().target,QString("confirmed"));
    }
    void roverReadFailurePreventsImplicitLocator()
    {
        using namespace decolog::core; QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { QSettings settings(iniPath(dir),QSettings::IniFormat); settings.setValue("ui/language","en"); settings.sync(); }
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); RoverSession rover(&db); QVERIFY(rover.start("Trip","JN71"));
          QSqlQuery q(db.connection()); QVERIFY(q.exec("DROP TABLE rover_position")); }
        QByteArray output; QVERIFY2(launch(dir,"roverreaderrorprobe",output,4000),output.constData());
        QVERIFY2(output.contains("PROBE rover read error=Cannot read rover session:") && output.contains("count=0"),output.constData());
    }
    void qslImportExposesAmbiguousConfirmations()
    {
        using namespace decolog::core; QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { QSettings settings(iniPath(dir),QSettings::IniFormat); settings.setValue("ui/language","en"); settings.sync(); }
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
          db.setSetting("qsl.import_summary",R"({"runs":[{"label":"LoTW","when":"2026-01-01","confirmed":0,"already":0,"notFound":0,"ambiguous":1,"invalid":2,"databaseErrors":3}],"rows":[{"kind":"ambiguous","label":"LoTW","service":"lotw","call":"K1ABC","band":"20m","mode":"CW","utc":"2026-01-01","reason":"Multiple candidates","candidateIds":[1,2]},{"kind":"invalid","label":"LoTW","service":"lotw","call":"K1BAD","reason":"invalid"},{"kind":"database_error","label":"LoTW","service":"lotw","call":"K1ERR","reason":"database failure"}]})"); }
        QByteArray output; QVERIFY2(launch(dir,"qslimportambiguousprobe",output,4000),output.constData());
        QVERIFY2(output.contains("PROBE qsl import ambiguous count=1 label=ambiguous") && output.contains("1 ambiguous") && output.contains("QSO IDs: 1, 2"),output.constData());
        QVERIFY2(output.contains("PROBE qsl import errors count=2 label=database error") && output.contains("2 invalid") && output.contains("3 database errors"),output.constData());
    }
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
    void qslPlannerGoalsFollowStationProfileAcrossRestart()
    {
        using namespace decolog::core;
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { QSettings settings(iniPath(dir),QSettings::IniFormat); settings.setValue("qslPlanner/goalsJson",QStringLiteral("[\"wac\"]")); settings.sync(); }
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
          StationProfile a; a.name="A"; a.stationCallsign="K1AAA";
          StationProfile b; b.name="B"; b.stationCallsign="K1BBB";
          QCOMPARE(db.saveStationProfile(a),qint64(1)); QCOMPARE(db.saveStationProfile(b),qint64(2)); }
        QByteArray output; QVERIFY2(launch(dir,"qslscopesprobe",output,4500),output.constData());
        QVERIFY2(output.contains("PROBE qslscopes") && output.contains("\"first\":[\"dxcc\",\"waz\"]") &&
                 output.contains("\"secondInitial\":[\"wac\"]") && output.contains("\"second\":[\"vucc\"]") &&
                 output.contains("\"back\":[\"dxcc\",\"waz\"]") && output.contains("\"error\":\"\""),output.constData());
        QByteArray reopened; QVERIFY2(launch(dir,"qslscopesprobe:restore",reopened,4500),reopened.constData());
        QVERIFY2(reopened.contains("\"first\":[\"dxcc\",\"waz\"]") &&
                 reopened.contains("\"secondInitial\":[\"vucc\"]") && reopened.contains("\"back\":[\"dxcc\",\"waz\"]"),reopened.constData());
        QSettings settings(iniPath(dir),QSettings::IniFormat);
        QCOMPARE(settings.value("qslPlanner/goalsJson").toString(),QStringLiteral("[\"wac\"]"));
    }

    void importGuideOffersSourcesWithoutChangingLog()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true); QByteArray output;
        QVERIFY2(launch(dir,"importguideprobe",output,4500),output.constData());
        QVERIFY2(output.contains("PROBE importguide") && output.contains("Log4OM") && output.contains("DXKeeper") &&
                 output.contains("Logger32") && output.contains("Ham Radio Deluxe") && output.contains("MacLoggerDX") &&
                 output.contains("N1MM Logger+") && output.contains("ADIF") && output.contains("s3db") &&
                 output.contains("\"backupNotePresent\":true") && output.contains("\"sourceCount\":7"),output.constData());
        const QByteArray marker("PROBE importguide "); const int start=output.indexOf(marker); QVERIFY(start>=0);
        const int end=output.indexOf('\n',start); QVERIFY(end>start);
        const auto info=QJsonDocument::fromJson(output.mid(start+marker.size(),end-start-marker.size())).object();
        QCOMPARE(info.value("generic").toArray().size(),5);
        QCOMPARE(info.value("adif").toArray().size(),2);
        QCOMPARE(info.value("n1mm").toArray().size(),3);
        QVERIFY(info.value("adif").toArray().first().toString().contains("*.adi"));
        QVERIFY(!info.value("adif").toArray().first().toString().contains("*.s3db"));
        QVERIFY(info.value("n1mm").toArray().first().toString().contains("*.s3db"));
        decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QCOMPARE(db.qsoCount(),qint64(0));
    }

    void qslPlannerGoalsFollowAwardAccountAcrossRestart()
    {
        using namespace decolog::core;
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
          StationProfile p; p.name="Station"; p.stationCallsign="K1AAA"; p.awardAccount="account-A"; QCOMPARE(db.saveStationProfile(p),qint64(1));
          StationProfile q; q.name="Second"; q.stationCallsign="K1BBB"; q.awardAccount="account-B"; QCOMPARE(db.saveStationProfile(q),qint64(2));
          StationProfile r; r.name="Third"; r.stationCallsign="K1CCC"; r.awardAccount="account-A"; QCOMPARE(db.saveStationProfile(r),qint64(3));
          StationProfile t; t.name="Case"; t.stationCallsign="K1DDD"; t.awardAccount="account-a"; QCOMPARE(db.saveStationProfile(t),qint64(4));
          QString error; QVERIFY(qslplanner::saveGoalSelection(db,1,{"dxcc"},&error)); }
        QByteArray output; QVERIFY2(launch(dir,"qslaccountsprobe",output,4500),output.constData());
        QVERIFY2(output.contains("\"firstInitial\":[\"dxcc\"]") && output.contains("\"first\":[\"waz\"]") &&
                 output.contains("\"secondInitial\":[\"dxcc\"]") && output.contains("\"second\":[\"vucc\"]") &&
                 output.contains("\"back\":[\"waz\"]") && output.contains("\"error\":\"\"") &&
                 output.contains("\"options\":[\"\",\"account-A\",\"account-B\",\"account-a\"]") &&
                 output.contains("\"field\":\"account-A\"") && output.contains("\"manual\":\"manual-X\""),output.constData());
        QByteArray restored; QVERIFY2(launch(dir,"qslaccountsprobe:restore",restored,4500),restored.constData());
        QVERIFY2(restored.contains("\"firstInitial\":[\"waz\"]") && restored.contains("\"secondInitial\":[\"vucc\"]") &&
                 restored.contains("\"back\":[\"waz\"]"),restored.constData());
    }

    void qslCostEditorRejectsRebuiltPlan()
    {
        using namespace decolog::core; QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
          const auto qso=db.insertQso({{"CALL","K1PAA"},{"QSO_DATE","20261001"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"DXCC","291"},{"CQZ","5"},{"QSL_VIA","M0ABC"}},"fixture"); QVERIFY(qso.id>0);
          QslState card; card.service="card"; card.via="B"; QVERIFY(db.setCardState(qso.id,card)); QString error; QVERIFY(qslplanner::saveRouteCost(db,"M0ABC","B",{"9.99","EUR","estimate","",""},&error)); }
        QByteArray output; QVERIFY2(launch(dir,"qslstalecostprobe",output,5500),output.constData());
        QVERIFY2(output.contains("PROBE stale cost opened true") && output.contains("PROBE stale cost save true") && output.contains("PROBE stale cost clear true") && (output.contains("The reviewed log or plan changed") || output.contains("Il log o il piano verificato")),output.constData());
        LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); const auto cost=qslplanner::routeCost(db,"M0ABC","B"); QVERIFY(cost); QCOMPARE(cost->amount,QString("9.99"));
    }
    void qslPlannerDesktopReportsMissingProvenance()
    {
        using namespace decolog::core;
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
          QVERIFY(db.insertQso({{"CALL","K1ABC"},{"QSO_DATE","20261001"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"DXCC","291"},{"QSL_VIA","M0ABC"}},"fixture").id>0);
          QSqlQuery q(db.connection()); QVERIFY(q.exec("DROP TABLE qso_enrichment")); }
        QByteArray output; QVERIFY2(launch(dir,"qslplanerrorprobe",output,4500),output.constData());
        QVERIFY2(output.contains("PROBE qsl plan error status Cannot read QSL route provenance while planning:") && output.contains("visible=true candidates=0"),output.constData());
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
          QslState card; card.service="card"; card.via="B"; QVERIFY(db.setCardState(first,card)); card.sent="Y"; card.sentDate="20261015"; QVERIFY(db.setCardState(pending,card));
          QslState electronic; electronic.service="lotw"; electronic.sent="Y"; QVERIFY(db.setQslState(pending,electronic)); }
        QByteArray output; QVERIFY2(launch(dir,"qslplanprobe",output,7000),output.constData());
        QVERIFY2(output.contains("PROBE qslplan status 3 potential benefits · 3 candidate QSO"),output.constData());
        QVERIFY2(output.contains("PROBE qslplan electronic LOTW: 1 QSO · 2 potential benefits") || output.contains("PROBE qslplan electronic LOTW: 1 QSO · 2 benefici potenziali"),output.constData());
        QVERIFY2(output.contains("PROBE qslplan filtered count=1 id=2") && output.contains("PROBE qslplan all count=3"), output.constData());
        QVERIFY2(output.contains("K1PAA") && output.contains("M0ABC"),output.constData());
        QVERIFY2(output.contains("\"date\":\"2026-10-15\"") && output.contains("\"dateStatus\":\"valid\"") && output.contains("PROBE qslplan pending card: recorded sent 2026-10-15"),output.constData());
        QVERIFY2(output.contains("PROBE qslplan row K1PAA") && output.contains("DXCC 291"),output.constData());
        QVERIFY2(output.contains("Alternative: its benefits are covered by higher-priority candidates") && output.contains("\"priorityRank\":1") && output.contains("\"pendingMarginalCount\":2"),output.constData());
        QVERIFY2(output.contains("sent/unconfirmed alternatives: 2"),output.constData());
        QVERIFY2(output.contains("route evidence older than 180 days; verify") && output.contains("route source: fixture"),output.constData());
        QVERIFY2(output.contains("PROBE qslplan group M0ABC · bureau · QSO: 1, 2 · potential benefits: 2 · cost unknown"),output.constData());
        QVERIFY2(output.contains("PROBE qslplan saved cost") && output.contains("\"amount\":\"12.50\"") && output.contains("\"basis\":\"estimate\""),output.constData());
        QVERIFY2(output.contains("12.50 EUR · operator estimate"),output.constData());
        QVERIFY2(output.contains("PROBE qslplan queue cancelled true"),output.constData());
        QVERIFY2(output.contains("PROBE qslplan enqueue 1 QSO"),output.constData());
        QVERIFY2(output.contains("PROBE qslplan paperqueue queue"),output.constData());
        QVERIFY2(output.contains("PROBE qslplan confirmation") && output.contains("\"uniqueBenefits\":1"),output.constData());
        QVERIFY2(output.contains("PROBE qslplan stale true"),output.constData());
        QVERIFY2(output.contains("PROBE qslplan band result") && output.contains("\"uniqueBenefits\":0"),output.constData());
        QVERIFY2(!output.contains("TypeError"),output.constData());
        QByteArray reopened; QVERIFY2(launch(dir,"qslplanrestoreprobe",reopened,4500),reopened.constData());
        QVERIFY2(reopened.contains("PROBE qslplan restored [\"dxcc\",\"waz\"]"),reopened.constData());
        QVERIFY2(reopened.contains("PROBE qslplan restored result") && reopened.contains("\"uniqueBenefits\":0"),reopened.constData());
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); const auto cost=qslplanner::routeCost(db,"M0ABC","B"); QVERIFY(cost.has_value()); QCOMPARE(cost->amount,QString("12.50")); QCOMPARE(db.qslStatus(1).first().sent,QString("Q")); }
    }
    void recoveredActivationReviewIsExplicitAndGuarded()
    {
        using namespace decolog::core;
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
          QVERIFY(db.insertQso({{"CALL","K1HBB"},{"QSO_DATE","20261101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"APP_DECOLOG_ACTIVATION_REVIEW","PERIOD_UNCERTAIN"},{"APP_DECOLOG_ACTIVATION_UUID","source-session"}},"fixture").id>0); }
        QByteArray output; QVERIFY2(launch(dir,"activationreviewprobe",output,4000),output.constData());
        QVERIFY2(output.contains("PROBE activation review shown true") && output.contains("PROBE activation review cancel PERIOD_UNCERTAIN") && output.contains("PROBE activation review stale true") && output.contains("PROBE activation review saved true"),output.constData());
        LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); const auto qso=db.record(1); QVERIFY(qso);
        QVERIFY(qso->value("APP_DECOLOG_ACTIVATION_REVIEW").isEmpty()); QCOMPARE(qso->value("MY_POTA_REF"),QString("IT-0002")); QCOMPARE(qso->value("MY_GRIDSQUARE"),QString("JM76"));
        QCOMPARE(qso->value("COMMENT"),QString("concurrent review edit")); QVERIFY(!qso->value("APP_DECOLOG_ACTIVATION_REVIEWED_AT").isEmpty());
        QVERIFY(qso->value("APP_DECOLOG_ACTIVATION_UUID").isEmpty());
    }
    void recoveredActivationCanBeAssignedToRecordedSession()
    {
        using namespace decolog::core;
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
          QVERIFY(db.insertQso({{"CALL","K1HBB"},{"QSO_DATE","20261101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"APP_DECOLOG_ACTIVATION_REVIEW","PERIOD_UNCERTAIN"}},"fixture").id>0);
          QSqlQuery q(db.connection()); QVERIFY(q.exec("INSERT INTO activation_run(uuid,started_at,ended_at,initial_json,last_json) VALUES('tracked-session','2026-11-01T11:00:00Z','2026-11-01T13:00:00Z','{}','{}')"));
          QVERIFY(q.exec("INSERT INTO activation_run_change(session_uuid,changed_at,event,state_json) VALUES('tracked-session','2026-11-01T11:00:00Z','start','{\"reference\":\"IT-0002\",\"myGrid\":\"JM76\"}')")); }
        QByteArray output; QVERIFY2(launch(dir,"activationassignprobe",output,4000),output.constData());
        QVERIFY2(output.contains("PROBE activation assignment sessions 2") && output.contains("PROBE activation history rows 1 reference=IT-0002") && output.contains("PROBE activation assignment saved true"),output.constData());
        LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); const auto qso=db.record(1); QVERIFY(qso);
        QCOMPARE(qso->value("APP_DECOLOG_ACTIVATION_UUID"),QString("tracked-session")); QCOMPARE(qso->value("MY_POTA_REF"),QString("IT-0002")); QVERIFY(qso->value("APP_DECOLOG_ACTIVATION_REVIEW").isEmpty());
    }
    void recoveredActivationAssignmentAcrossSessionPages()
    {
        using namespace decolog::core;
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
          QVERIFY(db.insertQso({{"CALL","K1HBB"},{"QSO_DATE","20261101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"APP_DECOLOG_ACTIVATION_REVIEW","PERIOD_UNCERTAIN"}},"fixture").id>0);
          QSqlQuery q(db.connection()); QVERIFY(q.prepare("INSERT INTO activation_run(uuid,started_at,ended_at,initial_json,last_json) VALUES(?, '2026-11-01T11:00:00Z','2026-11-01T13:00:00Z','{}','{}')"));
          for (int i=0;i<105;++i) { q.bindValue(0,QString("session-%1").arg(i,3,10,QLatin1Char('0'))); QVERIFY(q.exec()); } }
        QByteArray output; QVERIFY2(launch(dir,"activationpageprobe",output,4000),output.constData());
        QVERIFY2(output.contains("PROBE activation page first 101 next=true") && output.contains("PROBE activation page second 6 next=false") && output.contains("PROBE activation page retained session-000 page=0") && output.contains("PROBE activation page saved session-000 error="),output.constData());
        LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); const auto qso=db.record(1); QVERIFY(qso); QCOMPARE(qso->value("APP_DECOLOG_ACTIVATION_UUID"),QString("session-000"));
    }
    void recoveredActivationHistoryNavigatesPages()
    {
        using namespace decolog::core;
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
          QVERIFY(db.insertQso({{"CALL","K1HBB"},{"QSO_DATE","20261101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"},{"APP_DECOLOG_ACTIVATION_REVIEW","PERIOD_UNCERTAIN"}},"fixture").id>0);
          QSqlQuery q(db.connection()); QVERIFY(q.exec("INSERT INTO activation_run(uuid,started_at,ended_at,initial_json,last_json) VALUES('tracked-session','2026-11-01T11:00:00Z','2026-11-01T13:00:00Z','{}','{}')"));
          QVERIFY(q.prepare("INSERT INTO activation_run_change(session_uuid,changed_at,event,state_json) VALUES('tracked-session','2026-11-01T12:00:00Z','update',?)"));
          for (int i=0;i<55;++i) { q.bindValue(0,QString("{\"kind\":\"pota\",\"reference\":\"IT-%1\",\"myGrid\":\"JM75\",\"stationProfileId\":2}").arg(i,4,10,QLatin1Char('0'))); QVERIFY(q.exec()); } }
        const auto capture=qEnvironmentVariable("DECOLOG_ACTIVATION_HISTORY_CAPTURE");
        QByteArray output; QVERIFY2(launch(dir,QStringLiteral("activationhistorypageprobe")+(capture.isEmpty()?QString():QStringLiteral(":")+capture),output,4000),output.constData());
        QVERIFY2(output.contains("PROBE activation events first 50 next=true") && output.contains("PROBE activation events second 5 next=false") && output.contains("PROBE activation events back 50 first=IT-0000"),output.constData());
        if (!capture.isEmpty()) { QVERIFY2(output.contains("PROBE activation history image true"),output.constData()); QVERIFY(QFileInfo(capture).size()>0); }
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
                              {"MY_POTA_REF", "US-0015@US-MD"}, {"POTA_REF", "US-0003,US-0004"}, {"APP_DECOLOG_ACTIVATION_UUID", "pota-fixture"}};
            QCOMPARE(db.insertQso(record, "manual", {}, true, 0, false).status, InsertResult::Status::Inserted);
        }
        QByteArray output; QVERIFY2(launch(dir, QStringLiteral("potacatalogueprobe"), output), output.constData());
        QVERIFY2(output.contains("PROBE potaImport success=true status=listedActive location=listed name=Park error="), output.constData());
        QVERIFY2(output.contains("PROBE potaGroup status=listedActive location=listed"), output.constData());
        QVERIFY2(output.contains("active in snapshot"), output.constData());
        QVERIFY2(output.contains("correspondent park needs review: 1 QSO") ||
                 output.contains("parco del corrispondente da verificare: 1 QSO"), output.constData());
        QVERIFY2(output.contains("PROBE potaReview call=K1ABC total=1"), output.constData());
        QVERIFY2(output.contains("reason=multipleReferences") && (output.contains("multiple correspondent parks") || output.contains("più parchi del corrispondente")), output.constData());
        QVERIFY2(output.contains("PROBE potaReview stale=false"), output.constData());
        QVERIFY2(output.contains("PROBE potaReview open QSO 1 visible=true"), output.constData());
        QVERIFY(QFileInfo::exists(dir.filePath("data/pota-catalogue.sqlite")));
    }
    void potaReviewThirdPageInDesktop()
    {
        using namespace decolog::core;
        QTemporaryDir dir; QVERIFY(dir.isValid()); QFile fixture(dir.filePath("test.sqlite.pota.csv"));
        QVERIFY(fixture.open(QIODevice::WriteOnly)); fixture.write("reference,name,active,locationDesc\nUS-0015,Park,1,US-MD\n"); fixture.close();
        {
            LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QSqlQuery q(db.connection());
            QVERIFY(q.exec("INSERT INTO activation_run(uuid,started_at,ended_at,initial_json,last_json) VALUES('pota-fixture','2020-01-01T00:00:00Z','2020-01-01T23:59:00Z','{}','{}')"));
            for (int i=0;i<205;++i) {
                AdifRecord record{{"CALL",QStringLiteral("K%1").arg(i,4,10,QLatin1Char('0'))},{"QSO_DATE","20200101"},
                    {"TIME_ON","120000"},{"BAND","20m"},{"MODE","CW"},{"MY_POTA_REF","US-0015"},
                    {"POTA_REF","US-0003,US-0004"},{"APP_DECOLOG_ACTIVATION_UUID","pota-fixture"}};
                QVERIFY(db.insertQso(record,"fixture",{},true,0,false).id>0);
            }
        }
        QByteArray output; QVERIFY2(launch(dir,QStringLiteral("potacatalogueprobe"),output),output.constData());
        QVERIFY2(output.contains("PROBE potaReview call=K0000 total=205"),output.constData());
        QVERIFY2(output.contains("PROBE potaReview third=K0200 page=2"),output.constData());
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
    void qslOcrSelectedQsosCanBeReviewedAndRemoved()
    {
        decolog::core::TesseractOcr engine; if (engine.executable().isEmpty()) QSKIP("Tesseract is not installed");
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path=dir.filePath("card.png"); QVERIFY(QFile::copy(QFINDTESTDATA("fixtures/qsl-ocr-synthetic.png"),path));
        { decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
          for (const auto* time:{"143000","153000"}) QVERIFY(db.insertQso(decolog::core::AdifRecord{{"CALL","K1ABC"},{"QSO_DATE","20260923"},{"TIME_ON",time},{"BAND","20m"},{"MODE","FT4"}},"fixture",{},true,0,false).id>0); }
        QByteArray output; QVERIFY2(launch(dir,QStringLiteral("qslocrselection:")+path,output,5500),output.constData());
        QVERIFY2(output.contains("PROBE qslSelectionReview before=2") && output.contains("after=1") && output.contains("FT4") && !output.contains("TypeError"),output.constData());
        decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QCOMPARE(db.qsoCount(),qint64(2));
        QCOMPARE(db.qslAttachments(1).size()+db.qslAttachments(2).size(),1);
        QVERIFY(db.record(1)->value("QSL_RCVD").isEmpty()); QVERIFY(db.record(2)->value("QSL_RCVD").isEmpty());
    }

    void qslOcrCandidateShowsFt4()
    {
        decolog::core::TesseractOcr engine; if (engine.executable().isEmpty()) QSKIP("Tesseract is not installed");
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path=dir.filePath("card.png"); QVERIFY(QFile::copy(QFINDTESTDATA("fixtures/qsl-ocr-synthetic.png"),path));
        { decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
          QVERIFY(db.insertQso(decolog::core::AdifRecord{{"CALL","K1ABC"},{"QSO_DATE","20260923"},{"TIME_ON","143000"},{"BAND","20m"},{"MODE","FT4"}},"fixture").id>0);
          QSqlQuery q(db.connection()); QVERIFY(q.exec("UPDATE qso SET qso_datetime_on='2026-09-23T15:30:00+01:00'")); }
        QByteArray output; QVERIFY2(launch(dir,QStringLiteral("qslocrmatches:")+path,output,5000),output.constData());
        QByteArray line; for (const auto& entry:output.split('\n')) if (entry.contains("PROBE qslCandidate")) line=entry;
        QVERIFY2(line.contains("K1ABC") && line.contains("FT4") && !line.contains("MFSK") && line.contains("2026-09-23T14:30:00Z"),output.constData());
    }

    void qslOcrDialogShowsSimilarCallsOnRequest()
    {
        decolog::core::TesseractOcr engine; if (engine.executable().isEmpty()) QSKIP("Tesseract is not installed");
        QTemporaryDir dir; QVERIFY(dir.isValid()); const QString path=dir.filePath("card.png");
        QVERIFY(QFile::copy(QFINDTESTDATA("fixtures/qsl-ocr-synthetic.png"),path));
        { decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
          for (const auto* call : {"K1ABC","K1ABO","K1AB","K1XYZ"})
            QVERIFY(db.insertQso(decolog::core::AdifRecord{{"CALL",call},{"QSO_DATE","20260923"},{"TIME_ON","143000"},{"BAND","20m"},{"MODE","SSB"}},"fixture").id>0); }
        QByteArray output; QVERIFY2(launch(dir,QStringLiteral("qslocrsimilar:")+path,output,4500),output.constData());
        QVERIFY2(output.contains("PROBE qslMatches success=true count=3"),output.constData());
        decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QCOMPARE(db.qsoCount(),qint64(4)); QVERIFY(db.qslAttachments(1).isEmpty());
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
    void qslOcrDialogFiltersBySelectedUtcDateAndTime()
    {
        decolog::core::TesseractOcr engine; if (engine.executable().isEmpty()) QSKIP("Tesseract is not installed");
        QTemporaryDir dir; QVERIFY(dir.isValid()); const QString path=dir.filePath("card.png");
        QVERIFY(QFile::copy(QFINDTESTDATA("fixtures/qsl-ocr-synthetic.png"),path));
        { decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
          for (const QString& date : {QString("20260923"),QString("20260924")})
              QCOMPARE(db.insertQso(decolog::core::AdifRecord{{"CALL","K1ABC"},{"QSO_DATE",date},{"TIME_ON","143000"},{"BAND","20m"},{"MODE","SSB"}},"manual",{},true,0,false).status,decolog::core::InsertResult::Status::Inserted); }
        QByteArray output; QVERIFY2(launch(dir,QStringLiteral("qslocrfiltered:")+path,output,5000),output.constData());
        QVERIFY2(output.contains("PROBE qslFilters 2026-09-23 14:30") && output.contains("PROBE qslMatches success=true count=1 first=K1ABC"),output.constData());
        QVERIFY2(output.contains("PROBE qslFilterResult K1ABC · 2026-09-23 · 14:30 2026-09-23") && output.contains("PROBE qslFilterResult K1ABC · 2026-09-24 · 14:30 2026-09-24"),output.constData());
        QVERIFY2(!output.contains("TypeError"),output.constData());
        decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QCOMPARE(db.qsoCount(),qint64(2)); QVERIFY(db.qslAttachments(1).isEmpty());
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
    void qslOcrChangedOriginAppearsInActivity()
    {
        using namespace decolog::core; QTemporaryDir dir; QVERIFY(dir.isValid()); const QString path=dir.filePath("test.sqlite");
        const QString image=QFINDTESTDATA("fixtures/qsl-ocr-synthetic.png"); QFile f(image); QVERIFY(f.open(QIODevice::ReadOnly)); const auto bytes=f.readAll();
        const QString hash=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()); QString uuid;
        { LogDatabase db; QVERIFY(db.open(path)); const auto qso=db.insertQso(AdifRecord{{"CALL","K1ABC"},{"QSO_DATE","20260923"},{"TIME_ON","1430"},{"BAND","20m"},{"MODE","CW"}},"fixture",{},true,0,false); QVERIFY(qso.id>0);
          QSqlQuery q(db.connection()); QVERIFY(q.exec("SELECT uuid,revision FROM qso")); QVERIFY(q.next()); const QVariantList selected{QVariantMap{{"id",qso.id},{"uuid",q.value(0)},{"revision",q.value(1)}}}; q.finish(); std::atomic_bool cancelled{false};
          QVERIFY(QslOcrAcquisition::execute(path,image,hash,"card.png","ocr",selected,&cancelled).value("ok").toBool()); QVERIFY(q.exec("SELECT uuid FROM qsl_ocr_acquisition WHERE id=1")); QVERIFY(q.next()); uuid=q.value(0).toString(); }
        auto env=QProcessEnvironment::systemEnvironment(); env.insert("QT_QPA_PLATFORM","offscreen"); env.insert("QT_QUICK_BACKEND","software"); env.insert("DECODXLOG_DATA_ROOT",dir.filePath("data")); env.insert("DECODXLOG_GRAB_MS","6500");
        QProcess app; app.setProcessEnvironment(env); app.setProcessChannelMode(QProcess::MergedChannels); app.start(QStringLiteral(DECOLOG_APP),{"--software-renderer","--settings",dir.filePath("settings"),"--db",path,"--port","0","--show","ocralertprobe:"+uuid,"--grab",dir.filePath("screen.png")});
        QVERIFY(app.waitForStarted(10000)); QByteArray output; QElapsedTimer timer; timer.start(); while (!output.contains("PROBE ocrAlert started=true") && timer.elapsed()<10000 && app.state()!=QProcess::NotRunning) { app.waitForReadyRead(100); output+=app.readAll(); }
        QVERIFY2(output.contains("PROBE ocrAlert started=true"),output.constData());
        const QString connection="ocr-alert-observer"; { QSqlDatabase observer=QSqlDatabase::addDatabase("QSQLITE",connection); observer.setConnectOptions("QSQLITE_OPEN_READONLY"); observer.setDatabaseName(path); QVERIFY(observer.open()); bool committed=false; timer.restart();
          while (!committed && timer.elapsed()<2500) { QSqlQuery q(observer); QVERIFY(q.exec("SELECT count(*) FROM qsl_ocr_review")); QVERIFY(q.next()); committed=q.value(0).toInt()==1; if (!committed) QTest::qSleep(10); } QVERIFY(committed); observer.close(); }
        QSqlDatabase::removeDatabase(connection); if (!QFile::rename(path,dir.filePath("moved.sqlite"))) { app.kill(); app.waitForFinished(); QSKIP("Platform does not allow replacing an open SQLite file"); }
        QVERIFY(app.waitForFinished(15000)); output+=app.readAll(); QVERIFY2(app.exitCode()==0,output.constData());
        QVERIFY2(output.contains("PROBE ocrAlert done=false category=OCR message=") && output.contains("test.sqlite"),output.constData());
    }
    void qslOcrHistorySavesReviewedTextSeparately()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const QString image=QFINDTESTDATA("fixtures/qsl-ocr-synthetic.png"); QVERIFY(!image.isEmpty());
        QFile f(image); QVERIFY(f.open(QIODevice::ReadOnly)); const QByteArray bytes=f.readAll();
        const QString hash=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
        { decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
          const auto qso=db.insertQso(decolog::core::AdifRecord{{"CALL","K1ABC"},{"QSO_DATE","20260923"},{"TIME_ON","143000"},{"BAND","20m"},{"MODE","SSB"}},"manual",{},true,0,false); QVERIFY(qso.id>0);
          QSqlQuery q(db.connection()); QVERIFY(q.exec("SELECT uuid,revision FROM qso WHERE id=1")); QVERIFY(q.next());
          const QVariantList selected{QVariantMap{{"id",qso.id},{"uuid",q.value(0)},{"revision",q.value(1)}}}; std::atomic_bool cancelled{false};
          QVERIFY(decolog::core::QslOcrAcquisition::execute(db.path(),image,hash,"card.png","Original OCR",selected,&cancelled).value("ok").toBool()); }
        QByteArray output; QVERIFY2(launch(dir,"qslocrreview",output,5000),output.constData());
        QVERIFY2(output.contains("PROBE qslReview saved=true") && output.contains("PROBE qslReview restored Operator corrected K1ABC"),output.constData());
        QByteArray reopened; QVERIFY2(launch(dir,"qslocrreview",reopened,4000),reopened.constData());
        QVERIFY2(reopened.contains("PROBE qslReview restored Operator corrected K1ABC"),reopened.constData());
        decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QSqlQuery q(db.connection());
        QVERIFY(q.exec("SELECT a.ocr_text,r.reviewed_text FROM qsl_ocr_acquisition a JOIN qsl_ocr_review r ON r.acquisition_id=a.id")); QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(),QString("Original OCR")); QCOMPARE(q.value(1).toString(),QString("Operator corrected K1ABC")); QVERIFY(!q.next());
        QCOMPARE(db.qsoCount(),qint64(1)); QVERIFY(db.record(1)->value("QSL_RCVD").isEmpty()); QCOMPARE(db.qslAttachments(1).size(),1);
        QVERIFY(q.exec("SELECT a.id,a.uuid,r.id FROM qsl_ocr_acquisition a JOIN qsl_ocr_review r ON r.acquisition_id=a.id")); QVERIFY(q.next());
        const qint64 acquisitionId=q.value(0).toLongLong(), latest=q.value(2).toLongLong(); const QString uuid=q.value(1).toString(); q.finish();
        std::atomic_bool cancelled{false};
        QVERIFY(decolog::core::QslOcrAcquisition::saveReviewExecute(db.path(),acquisitionId,uuid,latest,"Second operator correction",&cancelled).value("ok").toBool());
        QByteArray revisions; QVERIFY2(launch(dir,"qslocrrevisions",revisions,5000),revisions.constData());
        QVERIFY2(revisions.contains("PROBE qslRevisions Second operator correction | Operator corrected K1ABC"),revisions.constData());
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
        const QString expectedTx=qEnvironmentVariable("DECOLOG_TEST_SATELLITE_TX",QStringLiteral("145.998596"));
        const QString expectedRx=qEnvironmentVariable("DECOLOG_TEST_SATELLITE_RX",QStringLiteral("435.004184"));
        QCOMPARE(qso.value("FREQ"), expectedTx);
        QCOMPARE(qso.value("FREQ_RX"), expectedRx);
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
        QCOMPARE(roundtrip.records.first().value("FREQ_RX"), expectedRx);
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

    void radioNetReminderReadFailureIsVisible()
    {
        using namespace decolog::core;
        QTemporaryDir dir; QVERIFY(dir.isValid()); seed(dir,"",true);
        { QSettings settings(iniPath(dir),QSettings::IniFormat); settings.setValue("ui/language","en"); settings.sync(); }
        { LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); RadioNet net(&db);
          QVERIFY(net.start("First")); QVERIFY(net.setReminder(60));
          QSqlQuery q(db.connection()); QVERIFY(q.exec("DROP TABLE radio_net_event")); }
        QByteArray output; QVERIFY2(launch(dir,"radioneterrorprobe",output,3500),output.constData());
        QVERIFY2(output.contains("PROBE net reminder error banner=true text=Radio net reminders unavailable") &&
                 output.contains("PROBE net reminder error dialog=true text=Radio net reminders unavailable:"),output.constData());
        QVERIFY2(!output.contains("TypeError"),output.constData());
    }

    void radioNetReminderVisibleWithDialogClosed()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); QByteArray output;
        QVERIFY2(launch(dir, QStringLiteral("radionetbackgroundprobe"), output, 4500), output.constData());
        QVERIFY2(output.contains("PROBE net background due=true dialog=false"), output.constData());
        QVERIFY2(output.contains("PROBE net background opened=true"), output.constData());
        QVERIFY2(output.contains("PROBE net background cleared=true"), output.constData());
        decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath(QStringLiteral("test.sqlite"))));
        QCOMPARE(db.qsoCount(), qint64(0));
    }

    void radioNetReminderWhileAnotherSessionIsOpen()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); QByteArray output;
        QVERIFY2(launch(dir, QStringLiteral("radionetotherreminderprobe"), output, 4500), output.constData());
        QVERIFY2(output.contains("PROBE net other due=true"), output.constData());
        QVERIFY2(output.contains("PROBE net other selected=true"), output.constData());
        decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath(QStringLiteral("test.sqlite"))));
        QCOMPARE(db.qsoCount(), qint64(0));
    }

    void radioNetReminderForUnselectedSession()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); QByteArray output;
        QVERIFY2(launch(dir, QStringLiteral("radionetmultireminderprobe"), output, 4500), output.constData());
        QVERIFY2(output.contains("PROBE net background due=true dialog=false"), output.constData());
        QVERIFY2(output.contains("PROBE net background opened=true correctSession=true"), output.constData());
        QVERIFY2(output.contains("PROBE net background cleared=true"), output.constData());
        decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath(QStringLiteral("test.sqlite"))));
        QCOMPARE(db.qsoCount(), qint64(0));
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

    void importUndoShowsProtectedLotwObservation()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        { decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite")));
          const auto imported=db.importAdif("<CALL:5>K1AAA<QSO_DATE:8>20260101<TIME_ON:4>0900<BAND:3>20m<MODE:2>CW<EOR><CALL:5>K1BBB<QSO_DATE:8>20260101<TIME_ON:4>1000<BAND:3>20m<MODE:2>CW<EOR>");
          QCOMPARE(imported.inserted,2); QString error; QVERIFY(db.recordLotwGridObservation(imported.insertedIds.last(),"FN42","",&error)); }
        QByteArray output; QVERIFY2(launch(dir,"undoimport",output,5000),output.constData());
        QVERIFY2(output.contains("\"eligible\":1") && output.contains("\"changed\":1") && output.contains("PROBE importUndoView visible=true") &&
                 (output.contains("LoTW grid observation added or linked after import") || output.contains("Osservazione locator LoTW aggiunta o collegata dopo l'importazione")),output.constData());
        decolog::core::LogDatabase db; QVERIFY(db.open(dir.filePath("test.sqlite"))); QVERIFY(db.meta(1)->deleted); QVERIFY(!db.meta(2)->deleted);
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
