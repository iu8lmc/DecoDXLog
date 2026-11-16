#include "app/QslController.h"
#include "core/ClubLogUploadJournal.h"
#include "core/ClubLogBatchReconcile.h"
#include "core/CredentialStore.h"
#include "core/LogDatabase.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QDir>
#include <QSettings>
#include <QSqlQuery>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <cstdio>
#include <algorithm>

namespace decolog::app {
using namespace decolog::core;

class QslControllerTest : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;
private slots:
    void clubLogBatchLiveOptIn() {
        const QString fixturePath = qEnvironmentVariable("DECODXLOG_CLUBLOG_LIVE_FIXTURE");
        const QString auditDir = qEnvironmentVariable("DECODXLOG_CLUBLOG_LIVE_AUDIT");
        if (fixturePath.isEmpty() || auditDir.isEmpty()) QSKIP("Live Club Log test not requested");
        QFile fixtureFile(fixturePath); QVERIFY(fixtureFile.open(QIODevice::ReadOnly));
        const auto fixture = QJsonDocument::fromJson(fixtureFile.readAll()).object();
        const QString station = fixture.value("station").toString();
        const auto records = fixture.value("records").toArray();
        QVERIFY(!station.isEmpty()); QCOMPARE(records.size(), 2);
        QVERIFY(QDir().mkpath(auditDir));
        QFile marker(QDir(auditDir).filePath("attempted"));
        if (!marker.open(QIODevice::WriteOnly | QIODevice::NewOnly)) QSKIP("Live test already attempted; no repeat will be made");
        marker.write("One permitted attempt; retain if outcome is uncertain.\n"); marker.close();
        char secretBuffer[256] = {};
        QVERIFY(std::fgets(secretBuffer, sizeof(secretBuffer), stdin));
        const QString token = QString::fromUtf8(secretBuffer).trimmed();
        std::fill(std::begin(secretBuffer), std::end(secretBuffer), char(0));
        QVERIFY(token.startsWith(QLatin1String("cl_")));
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, auditDir);
        QSettings settings; settings.setValue("credentials/clublog/stored", true); settings.sync();
        LogDatabase db;
        QVERIFY(db.open(QDir(auditDir).filePath("test.sqlite")));
        QByteArray adif;
        for (const auto& record : records) adif += record.toString().toUtf8() + '\n';
        QCOMPARE(db.importAdif(adif).inserted, 2);
        CredentialStore credentials("unused-live-test");
        credentials.m_secretCache.insert("clublog", token);
        QslController::Context ctx; ctx.db = &db; ctx.credentials = &credentials;
        ctx.stationCallsign = [station] { return station; };
        QslController controller(ctx);
        controller.uploadPending("clublog", 0);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 45000);
        QSettings after;
        QVERIFY2(!ClubLogUploadJournal(after).pending(), "Live batch outcome is not fully reconciled; do not resend");
        const auto sent = db.qsosToUpload("clublog", 0);
        QVERIFY2(sent.isEmpty(), "Some live contacts remain unconfirmed; do not resend blindly");
        QSqlQuery verified(db.connection());
        QVERIFY(verified.exec("SELECT COUNT(*) FROM qsl_status WHERE service='clublog' AND sent='Y' AND CAST(remote_id AS INTEGER)>0"));
        QVERIFY(verified.next()); QCOMPARE(verified.value(0).toInt(), 2);
    }
    void initTestCase() {
        QVERIFY(m_dir.isValid());
        QCoreApplication::setOrganizationName("DecoDXLogTests");
        QCoreApplication::setApplicationName("QslControllerBatch");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir.path());
    }
    void init() { QSettings().clear(); }

    void misplacedPersonalTokenIsRemovedFromLegacySettings()
    {
        QSettings settings;
        settings.setValue(QStringLiteral("qsl/clubLogApiKey"), QStringLiteral("cl_synthetic_fixture"));
        settings.sync();
        QslController controller({nullptr});
        QVERIFY(controller.clubLogApiKey().isEmpty());
        QSettings after;
        QVERIFY(!after.contains(QStringLiteral("qsl/clubLogApiKey")));
        QFile file(after.fileName());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(!file.readAll().contains("cl_synthetic_fixture"));
    }

    void migrationDoesNotReplaceAnExistingSecureCredential()
    {
        QSettings settings;
        settings.setValue(QStringLiteral("qsl/clubLogApiKey"), QStringLiteral("cl_stale_fixture"));
        settings.setValue(QStringLiteral("credentials/clublog/stored"), true);
        settings.sync();
        CredentialStore credentials(QStringLiteral("DecoDXLog-test-unused"));
        QslController::Context context;
        context.credentials = &credentials;
        QslController controller(context);
        QVERIFY(controller.clubLogApiKey().isEmpty());
        QVERIFY(credentials.hasSecret(QStringLiteral("clublog")));
        QSettings after;
        QVERIFY(!after.contains(QStringLiteral("qsl/clubLogApiKey")));
        QVERIFY(!after.value(QStringLiteral("credentials/clublog/replacementPending")).toBool());
    }

    void directOqrsRequestsNeverChangeQslConfirmation()
    {
        LogDatabase db;
        QVERIFY(db.open(m_dir.filePath(QStringLiteral("oqrs-display.db"))));
        QCOMPARE(db.importAdif("<CALL:5>K1AAA<QSO_DATE:8>20260101<TIME_ON:4>1000<BAND:3>20m<MODE:2>CW<EOR>").inserted, 1);
        const qint64 id = db.latestIds(QStringLiteral("K1AAA"), 1).first();
        QString station = QStringLiteral("K1TEST");
        QslController::Context context;
        context.db = &db;
        context.stationCallsign = [&station] { return station; };
        QslController controller(context);
        controller.m_oqrsCall = station;
        controller.m_oqrsRequestGeneration = controller.m_oqrsGeneration;
        const QVariantList rows{QVariantMap{{"dxcall", "K1AAA"}, {"band", "20"}, {"mode", "CW"}}};
        controller.m_oqrs.finished(true, rows, {});
        QCOMPARE(controller.clubLogOqrsRequests().size(), 1);
        QVERIFY(controller.clubLogOqrsStatus().contains(QStringLiteral("not QSL confirmations")));
        QVERIFY(db.qslStatus(id).isEmpty());
        controller.m_oqrs.finished(false, {}, QStringLiteral("network error"));
        QCOMPARE(controller.clubLogOqrsRequests().size(), 1);
        QCOMPARE(controller.clubLogOqrsStatus(), QStringLiteral("network error"));
        station = QStringLiteral("K1OTHER");
        QVERIFY(controller.clubLogOqrsRequests().isEmpty());
        QVERIFY(controller.clubLogOqrsStatus().contains(QStringLiteral("Station changed")));
        QVERIFY(db.qslStatus(id).isEmpty());
    }

    void finishedResultsUpdateOnlyTheirOwnContacts_data() {
        QTest::addColumn<int>("scenario");
        QTest::newRow("identified, reverse order") << 0;
        QTest::newRow("duplicate remote identity") << 1;
        QTest::newRow("database write fails after first record") << 2;
        QTest::newRow("one rejected contact identified by elimination") << 3;
        QTest::newRow("two rejections with unmatched reasons") << 4;
    }
    void finishedResultsUpdateOnlyTheirOwnContacts() {
        QFETCH(int, scenario);
        const bool ambiguous = scenario == 1;
        LogDatabase db;
        QVERIFY(db.open(m_dir.filePath(QStringLiteral("scenario-%1.db").arg(scenario))));
        const QByteArray adi = "<CALL:5>K1AAA<QSO_DATE:8>20260101<TIME_ON:4>1000<BAND:3>20m<MODE:2>CW<APP_CLUBLOG_QSOID:3>101<EOR>"
                               "<CALL:5>K1BBB<QSO_DATE:8>20260101<TIME_ON:4>1100<BAND:3>20m<MODE:2>CW<APP_CLUBLOG_QSOID:3>102<EOR>";
        QCOMPARE(db.importAdif(adi).inserted, 2);
        const auto a = db.latestIds("K1AAA", 1).first();
        const auto b = db.latestIds("K1BBB", 1).first();
        if (scenario == 2) {
            QSqlQuery trigger(db.connection());
            QVERIFY(trigger.exec(QStringLiteral("CREATE TRIGGER fail_second BEFORE INSERT ON qsl_status "
                "WHEN NEW.qso_id = %1 BEGIN SELECT RAISE(ABORT, 'test failure'); END").arg(b)));
        }
        QSettings settings;
        ClubLogUploadJournal journal(settings);
        QVERIFY(journal.begin(db.path(), "K1TEST", {QString::number(a), QString::number(b)},
                              ClubLogUploadJournal::recordsFingerprint(db.exportAdif(QList<qint64>{a, b}), 2)));
        QVERIFY(journal.queued("123", journal.nonce()));
        QslController controller({&db});
        controller.m_batch = {a, b};
        controller.m_batchMode = true;
        controller.m_busyService = "clublog";
        controller.m_clubLogRestStage = "polling";
        controller.m_clubLogRestJournalThisRun = true;
        controller.m_clubLogJournalNonce = journal.nonce();
        QCOMPARE(controller.clubLogPendingInfo().value(QStringLiteral("uploadId")).toString(), QStringLiteral("123"));
        QCOMPARE(controller.clubLogPendingInfo().value(QStringLiteral("count")).toInt(), 2);
        QVERIFY(controller.clubLogPendingInfo().value(QStringLiteral("currentLog")).toBool());
        QslUploadResult result;
        result.requiresReconciliation = true;
        result.clubLogBatchRecords = {QVariantMap{{"qsoid", 102}, {"status", "duplicate"}},
                                      QVariantMap{{"qsoid", ambiguous ? 102 : 101}, {"status", "created"}}};
        if (scenario == 3)
            result.clubLogBatchRecords[1] = QVariantMap{{"qsoid", QVariant()}, {"status", "rejected"}, {"message", "invalid contact"}};
        if (scenario == 4)
            result.clubLogBatchRecords = {QVariantMap{{"qsoid", QVariant()}, {"status", "rejected"}, {"message", "invalid contact"}},
                                         QVariantMap{{"qsoid", QVariant()}, {"status", "rejected"}, {"message", "invalid band"}}};
        controller.m_web.finished(result);
        QSettings after;
        ClubLogUploadJournal remaining(after);
        QCOMPARE(remaining.pending(), scenario == 1 || scenario == 2);
        const auto statesA = db.qslStatus(a);
        const auto statesB = db.qslStatus(b);
        if (scenario == 1 || scenario == 2) {
            QVERIFY(statesA.isEmpty());
            QVERIFY(statesB.isEmpty());
        } else {
            QCOMPARE(statesA.size(), 1);
            QCOMPARE(statesB.size(), 1);
            QCOMPARE(statesA.first().remoteId, scenario >= 3 ? QString() : QStringLiteral("101"));
            QCOMPARE(statesB.first().remoteId, scenario == 4 ? QString() : QStringLiteral("102"));
            QCOMPARE(statesA.first().sent, scenario >= 3 ? QStringLiteral("N") : QStringLiteral("Y"));
            QCOMPARE(statesB.first().sent, scenario == 4 ? QStringLiteral("N") : QStringLiteral("Y"));
            if (scenario == 3) QCOMPARE(statesA.first().lastError, QStringLiteral("invalid contact"));
            if (scenario == 4) {
                QVERIFY(statesA.first().lastError.contains(QStringLiteral("individual reason is unknown")));
                QCOMPARE(statesA.first().lastError, statesB.first().lastError);
                QVERIFY(statesA.first().lastError.contains(QStringLiteral("invalid contact")));
                QVERIFY(statesA.first().lastError.contains(QStringLiteral("invalid band")));
            }
        }
    }

    void newQsoCanSendWhileOlderUploadIsPending() {
        LogDatabase db;
        QVERIFY(db.open(m_dir.filePath(QStringLiteral("parallel-pending.db"))));
        QCOMPARE(db.importAdif("<CALL:5>K1AAA<QSO_DATE:8>20260101<TIME_ON:4>1000<BAND:3>20m<MODE:2>CW<EOR>"
                               "<CALL:5>K1BBB<QSO_DATE:8>20260101<TIME_ON:4>1100<BAND:3>20m<MODE:2>CW<EOR>").inserted, 2);
        const auto old = db.latestIds("K1AAA", 1).first();
        const auto fresh = db.latestIds("K1BBB", 1).first();
        QSettings settings;
        ClubLogUploadJournal journal(settings);
        QVERIFY(journal.begin(db.path(), "K1TEST", {QString::number(old)},
                              ClubLogUploadJournal::recordsFingerprint(db.exportAdif(QList<qint64>{old}), 1)));
        const QString oldNonce = journal.nonce();
        QVERIFY(journal.queued("123", oldNonce));
        settings.setValue("credentials/clublog/stored", true);
        settings.sync();
        CredentialStore credentials("unused-test");
        credentials.m_secretCache.insert("clublog", "cl_new_upload_fixture");
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QByteArray observed;
        connect(&server, &QTcpServer::newConnection, this, [&] {
            QTcpSocket* socket = server.nextPendingConnection();
            auto request = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, this, [&, socket, request] {
                *request += socket->readAll();
                const int head = request->indexOf("\r\n\r\n");
                if (head < 0) return;
                int length = 0;
                for (const QByteArray& line : request->left(head).split('\n'))
                    if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toInt();
                if (request->size() < head + 4 + length) return;
                observed = *request;
                const QByteArray body = R"({"status":"created","qsoid":456})";
                socket->write("HTTP/1.1 201 Created\r\nContent-Type: application/json\r\nContent-Length: "
                              + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
        QslController::Context ctx; ctx.db = &db; ctx.credentials = &credentials;
        ctx.stationCallsign = [] { return QStringLiteral("K1TEST"); };
        QslController controller(ctx);
        controller.m_web.setClubLogRestEndpoint(QUrl(QStringLiteral("http://127.0.0.1:%1/api/qso").arg(server.serverPort())));
        controller.uploadPending("clublog", 0);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QVERIFY(observed.contains("K1BBB")); QVERIFY(!observed.contains("K1AAA"));
        QCOMPARE(db.qslStatus(fresh).size(), 1);
        QCOMPARE(db.qslStatus(fresh).first().sent, QStringLiteral("Y"));
        QVERIFY(db.qslStatus(old).isEmpty());
        QSettings after;
        QCOMPARE(ClubLogUploadJournal(after).nonce(), oldNonce);
        QCOMPARE(ClubLogUploadJournal(after).localIds(), QStringList({QString::number(old)}));
    }

    void clubLogNeverSendsAnotherStationUnderActiveCall() {
        LogDatabase db;
        QVERIFY(db.open(m_dir.filePath("different-station.db")));
        QCOMPARE(db.importAdif("<CALL:5>K1AAA<QSO_DATE:8>20260101<TIME_ON:4>1000<BAND:3>20m<MODE:2>CW<STATION_CALLSIGN:6>K1TEST<EOR>"
                               "<CALL:5>K1BBB<QSO_DATE:8>20260101<TIME_ON:4>1100<BAND:3>20m<MODE:2>CW<STATION_CALLSIGN:7>K1OTHER<EOR>").inserted, 2);
        const auto a = db.latestIds("K1AAA", 1).first();
        const auto b = db.latestIds("K1BBB", 1).first();
        QSettings().setValue("credentials/clublog/stored", true);
        CredentialStore credentials("unused-test");
        credentials.m_secretCache.insert("clublog", "cl_station_check_fixture");
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        QByteArray observed;
        connect(&server, &QTcpServer::newConnection, this, [&] {
            QTcpSocket* socket = server.nextPendingConnection();
            auto request = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, this, [&, socket, request] {
                *request += socket->readAll();
                const int head = request->indexOf("\r\n\r\n"); if (head < 0) return;
                int length = 0;
                for (const QByteArray& line : request->left(head).split('\n'))
                    if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toInt();
                if (request->size() < head + 4 + length) return;
                observed = *request;
                const QByteArray body = R"({"status":"created","qsoid":101})";
                socket->write("HTTP/1.1 201 Created\r\nContent-Type: application/json\r\nContent-Length: "
                              + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
        QslController::Context ctx; ctx.db = &db; ctx.credentials = &credentials;
        ctx.stationCallsign = [] { return QStringLiteral("K1TEST"); };
        QslController controller(ctx);
        controller.m_web.setClubLogRestEndpoint(QUrl(QStringLiteral("http://127.0.0.1:%1/api/qso").arg(server.serverPort())));
        controller.uploadPending("clublog", 0);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QVERIFY(observed.contains("K1AAA")); QVERIFY(!observed.contains("K1BBB"));
        QCOMPARE(db.qslStatus(a).size(), 1); QVERIFY(db.qslStatus(b).isEmpty());
    }

    void twoNewQsosUseRealtimeBatchAndReconcile() {
        LogDatabase db;
        QVERIFY(db.open(m_dir.filePath("realtime-batch.db")));
        QCOMPARE(db.importAdif("<CALL:5>K1AAA<QSO_DATE:8>20260101<TIME_ON:4>1000<BAND:3>20m<MODE:2>CW<EOR>"
                               "<CALL:5>K1BBB<QSO_DATE:8>20260101<TIME_ON:4>1100<BAND:3>20m<MODE:2>CW<EOR>").inserted, 2);
        const auto a = db.latestIds("K1AAA", 1).first();
        const auto b = db.latestIds("K1BBB", 1).first();
        QSettings().setValue("credentials/clublog/stored", true);
        CredentialStore credentials("unused-test");
        credentials.m_secretCache.insert("clublog", "cl_realtime_batch_test");
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        int batches = 0, queries = 0;
        connect(&server, &QTcpServer::newConnection, this, [&] {
            QTcpSocket* socket = server.nextPendingConnection();
            auto request = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, this, [&, socket, request] {
                *request += socket->readAll();
                const int head = request->indexOf("\r\n\r\n");
                if (head < 0) return;
                int length = 0;
                for (const QByteArray& line : request->left(head).split('\n'))
                    if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toInt();
                if (request->size() < head + 4 + length) return;
                QByteArray body;
                if (request->startsWith("POST /api/qso/batch ")) {
                    ++batches;
                    const auto sent = QJsonDocument::fromJson(request->mid(head + 4)).object().value("qsos").toArray();
                    QCOMPARE(sent.size(), 2);
                    body = R"({"results":[{"status":"updated","qsoid":101},{"status":"updated","qsoid":102}]})";
                } else {
                    QVERIFY(request->startsWith("POST /api/logs/K1TEST/qsos/query "));
                    ++queries;
                    const auto filters = QJsonDocument::fromJson(request->mid(head + 4)).object().value("filters").toArray();
                    QJsonArray results;
                    for (const auto& item : filters) {
                        const auto filter = item.toObject();
                        const int id = filter.value("dxcall").toString() == QLatin1String("K1AAA") ? 101 : 102;
                        results.append(QJsonObject{{"filter", filter}, {"qsos", QJsonArray{QJsonObject{{"qsoid", id}}}}});
                    }
                    body = QJsonDocument(QJsonObject{{"results", results}}).toJson(QJsonDocument::Compact);
                }
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                              + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
        QslController::Context ctx; ctx.db = &db; ctx.credentials = &credentials;
        ctx.stationCallsign = [] { return QStringLiteral("K1TEST"); };
        QslController controller(ctx);
        controller.m_web.setClubLogRestBatchBaseUrl(QUrl(QStringLiteral("http://127.0.0.1:%1/api/").arg(server.serverPort())));
        controller.uploadPending("clublog", 0);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QCOMPARE(batches, 1); QCOMPARE(queries, 1);
        QCOMPARE(db.qslStatus(a).first().remoteId, QStringLiteral("101"));
        QCOMPARE(db.qslStatus(b).first().remoteId, QStringLiteral("102"));
        QSettings after; QVERIFY(!ClubLogUploadJournal(after).pending());
    }

    void statusServerErrorFallsBackToPresence_data() {
        QTest::addColumn<int>("scenario");
        QTest::newRow("all present") << 0;
        QTest::newRow("one absent") << 1;
        QTest::newRow("ambiguous match") << 2;
        QTest::newRow("unauthorised does not query") << 3;
    }
    void statusServerErrorFallsBackToPresence() {
        QFETCH(int, scenario);
        LogDatabase db;
        QVERIFY(db.open(m_dir.filePath(QStringLiteral("presence-%1.db").arg(scenario))));
        QCOMPARE(db.importAdif("<CALL:5>K1AAA<QSO_DATE:8>20260101<TIME_ON:4>1000<BAND:3>20m<MODE:2>CW<EOR>"
                               "<CALL:5>K1BBB<QSO_DATE:8>20260101<TIME_ON:4>1100<BAND:3>20m<MODE:2>CW<EOR>").inserted, 2);
        const auto a = db.latestIds("K1AAA", 1).first();
        const auto b = db.latestIds("K1BBB", 1).first();
        QSettings settings;
        ClubLogUploadJournal journal(settings);
        QVERIFY(journal.begin(db.path(), "K1TEST", {QString::number(a), QString::number(b)},
                              ClubLogUploadJournal::recordsFingerprint(db.exportAdif(QList<qint64>{a, b}), 2)));
        QVERIFY(journal.queued("123", journal.nonce()));
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        int statusRequests = 0, queryRequests = 0;
        connect(&server, &QTcpServer::newConnection, this, [&] {
            QTcpSocket* socket = server.nextPendingConnection();
            auto request = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, this, [&, socket, request] {
                *request += socket->readAll();
                const int head = request->indexOf("\r\n\r\n");
                if (head < 0) return;
                if (request->startsWith("GET ")) {
                    ++statusRequests;
                    const QByteArray body = R"({"error":{"code":"server_error","message":"failure"}})";
                    socket->write(QByteArray(scenario == 3 ? "HTTP/1.1 401 Error\r\n" : "HTTP/1.1 500 Error\r\n") + "Content-Type: application/json\r\nContent-Length: "
                                  + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                    return;
                }
                int length = 0;
                for (const QByteArray& line : request->left(head).split('\n'))
                    if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toInt();
                if (request->size() < head + 4 + length) return;
                QVERIFY(request->startsWith("POST /api/logs/K1TEST/qsos/query "));
                ++queryRequests;
                const auto filters = QJsonDocument::fromJson(request->mid(head + 4)).object().value("filters").toArray();
                QJsonArray results;
                for (const QJsonValue& value : filters) {
                    const auto filter = value.toObject();
                    const bool second = filter.value("dxcall").toString() == QLatin1String("K1BBB");
                    QJsonArray qsos;
                    if (!(second && scenario == 1)) qsos.append(QJsonObject{{"qsoid", second ? 102 : 101}});
                    if (second && scenario == 2) qsos.append(QJsonObject{{"qsoid", 103}});
                    results.append(QJsonObject{{"filter", filter}, {"qsos", qsos}});
                }
                const auto body = QJsonDocument(QJsonObject{{"results", results}}).toJson(QJsonDocument::Compact);
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                              + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
        QslController controller({&db});
        controller.m_web.setClubLogRestBatchBaseUrl(QUrl(QStringLiteral("http://127.0.0.1:%1/api/").arg(server.serverPort())));
        controller.m_secret = "cl_presence_fixture";
        controller.m_clubLogRestCall = "K1TEST";
        controller.m_batch = {a, b}; controller.m_batchMode = true; controller.m_busyService = "clublog";
        controller.m_clubLogRestStage = "polling"; controller.m_clubLogRestJournalThisRun = true;
        controller.m_clubLogJournalNonce = journal.nonce();
        controller.m_web.pollClubLogRestBatch("K1TEST", "cl_presence_fixture", "123", 2);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QCOMPARE(statusRequests, 1); QCOMPARE(queryRequests, scenario == 3 ? 0 : 1);
        QSettings after;
        QCOMPARE(ClubLogUploadJournal(after).pending(), scenario != 0);
        const auto sa = db.qslStatus(a), sb = db.qslStatus(b);
        if (scenario == 0) {
            QCOMPARE(sa.size(), 1); QCOMPARE(sb.size(), 1);
            QCOMPARE(sa.first().sent, QStringLiteral("Y")); QCOMPARE(sb.first().sent, QStringLiteral("Y"));
            QCOMPARE(sa.first().remoteId, QStringLiteral("101")); QCOMPARE(sb.first().remoteId, QStringLiteral("102"));
        } else if (scenario == 1 || scenario == 2) {
            QCOMPARE(sa.size(), 1); QCOMPARE(sa.first().sent, QStringLiteral("Y"));
            QVERIFY(sb.isEmpty());
            QCOMPARE(ClubLogUploadJournal(after).localIds(), QStringList({QString::number(b)}));
        } else {
            QVERIFY(sa.isEmpty()); QVERIFY(sb.isEmpty());
        }
    }

    void newContactsAreResolvedThroughReadOnlyQuery_data() {
        QTest::addColumn<bool>("rejected");
        QTest::newRow("both accepted") << false;
        QTest::newRow("one rejected") << true;
    }
    void newContactsAreResolvedThroughReadOnlyQuery() {
        QFETCH(bool, rejected);
        LogDatabase db;
        QVERIFY(db.open(m_dir.filePath(rejected ? "new-rejected.db" : "new-contacts.db")));
        const QByteArray adi = "<CALL:5>K1AAA<QSO_DATE:8>20260101<TIME_ON:4>1000<BAND:3>20m<MODE:2>CW<EOR>"
                               "<CALL:5>K1BBB<QSO_DATE:8>20260101<TIME_ON:4>1100<BAND:3>20m<MODE:2>CW<EOR>";
        QCOMPARE(db.importAdif(adi).inserted, 2);
        const auto a = db.latestIds("K1AAA", 1).first();
        const auto b = db.latestIds("K1BBB", 1).first();
        QSettings settings;
        ClubLogUploadJournal journal(settings);
        QVERIFY(journal.begin(db.path(), "K1TEST", {QString::number(a), QString::number(b)},
                              ClubLogUploadJournal::recordsFingerprint(db.exportAdif(QList<qint64>{a, b}), 2)));
        QVERIFY(journal.queued("123", journal.nonce()));
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QByteArray observed;
        connect(&server, &QTcpServer::newConnection, this, [&] {
            QTcpSocket* socket = server.nextPendingConnection();
            auto request = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, this, [&, socket, request] {
                *request += socket->readAll();
                const int headerEnd = request->indexOf("\r\n\r\n");
                if (headerEnd < 0) return;
                int length = 0;
                for (const QByteArray& line : request->left(headerEnd).split('\n'))
                    if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toInt();
                if (request->size() < headerEnd + 4 + length) return;
                observed = *request;
                const auto filters = QJsonDocument::fromJson(request->mid(headerEnd + 4)).object().value("filters").toArray();
                QJsonArray results;
                for (const QJsonValue& value : filters) {
                    const QJsonObject filter = value.toObject();
                    const int remote = filter.value("dxcall").toString() == QLatin1String("K1AAA") ? 101 : 102;
                    const QJsonArray qsos = rejected && remote == 102 ? QJsonArray{}
                        : QJsonArray{QJsonObject{{"qsoid", remote}}};
                    results.prepend(QJsonObject{{"filter", filter}, {"qsos", qsos}});
                }
                const QByteArray body = QJsonDocument(QJsonObject{{"results", results}}).toJson(QJsonDocument::Compact);
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                              + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
        QslController controller({&db});
        controller.m_web.setClubLogRestBatchBaseUrl(QUrl(QStringLiteral("http://127.0.0.1:%1/api/").arg(server.serverPort())));
        controller.m_secret = "cl_fixture";
        controller.m_clubLogRestCall = "K1TEST";
        controller.m_batch = {a, b};
        controller.m_batchMode = true;
        controller.m_busyService = "clublog";
        controller.m_clubLogRestStage = "polling";
        controller.m_clubLogRestJournalThisRun = true;
        controller.m_clubLogJournalNonce = journal.nonce();
        QslUploadResult final;
        final.requiresReconciliation = true;
        final.clubLogBatchRecords = {QVariantMap{{"qsoid", 102}, {"status", "duplicate"}},
                                    QVariantMap{{"qsoid", 101}, {"status", "created"}}};
        if (rejected)
            final.clubLogBatchRecords[0] = QVariantMap{{"qsoid", QVariant()}, {"status", "rejected"}, {"message", "invalid contact"}};
        controller.m_web.finished(final);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
        QVERIFY(observed.startsWith("POST /api/logs/K1TEST/qsos/query "));
        QCOMPARE(db.qslStatus(a).first().remoteId, QStringLiteral("101"));
        QCOMPARE(db.qslStatus(b).first().remoteId, rejected ? QString() : QStringLiteral("102"));
        QCOMPARE(db.qslStatus(b).first().sent, rejected ? QStringLiteral("N") : QStringLiteral("Y"));
        if (rejected) QCOMPARE(db.qslStatus(b).first().lastError, QStringLiteral("invalid contact"));
        QSettings after;
        QVERIFY(!ClubLogUploadJournal(after).pending());
    }
};
}

QTEST_GUILESS_MAIN(decolog::app::QslControllerTest)
#include "tst_qslcontroller.moc"
