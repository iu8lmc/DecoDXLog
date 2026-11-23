// Invio QSL: codici d'uscita di TQSL, risposte di QRZ Logbook ed eQSL, coda e
// stato per servizio nel database.
#include "core/Adif.h"
#include "core/ClubLogAccessGuard.h"
#include "core/ClubLogBatchReconcile.h"
#include "core/ClubLogOqrsClient.h"
#include "core/ClubLogUploadJournal.h"
#include "core/ClubLogRestBatchProtocol.h"
#include "core/LogDatabase.h"
#include "core/QslUpload.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

using namespace decolog::core;

namespace {

bool hasBearerAuthorization(const QByteArray& request, const QByteArray& token)
{
    const QByteArray headers = request.left(request.indexOf("\r\n\r\n"));
    for (const QByteArray& line : headers.split('\n')) {
        const int colon = line.indexOf(':');
        if (colon > 0 && line.left(colon).trimmed().toLower() == "authorization")
            return line.mid(colon + 1).trimmed() == "Bearer " + token;
    }
    return false;
}

// Un CRX finto: risponde sempre con `answer`, e si tiene la richiesta.
class FakeCrx : public QTcpServer {
public:
    QByteArray request;
    QByteArray body;
    QByteArray answer;
    int status{200};

    FakeCrx()
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            QTcpSocket* socket = nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                request += socket->readAll();
                const int head = request.indexOf("\r\n\r\n");
                if (head < 0)
                    return;
                body = request.mid(head + 4);
                QByteArray length;
                for (const QByteArray& line : request.left(head).split('\n')) {
                    if (line.toLower().startsWith("content-length:"))
                        length = line.mid(15).trimmed();
                }
                if (body.size() < length.toInt())
                    return;
                socket->write("HTTP/1.1 " + QByteArray::number(status) + " X\r\nContent-Type: application/json\r\n"
                              "Content-Length: " + QByteArray::number(answer.size()) + "\r\n\r\n" + answer);
                socket->disconnectFromHost();
            });
        });
    }
    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/api/").arg(serverPort())); }
};

class FakeClubLogPoll : public QTcpServer {
public:
    int status {200};
    QList<QByteArray> answers;
    QList<QByteArray> requests;
    FakeClubLogPoll()
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            QTcpSocket* socket = nextPendingConnection();
            auto buffer = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer] {
                *buffer += socket->readAll();
                if (!buffer->contains("\r\n\r\n")) return;
                requests.append(*buffer);
                const QByteArray answer = answers.isEmpty() ? QByteArray("{}") : answers.takeFirst();
                socket->write("HTTP/1.1 " + QByteArray::number(status) + " X\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "
                              + QByteArray::number(answer.size()) + "\r\n\r\n" + answer);
                socket->disconnectFromHost();
            });
        });
    }
    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/api/").arg(serverPort())); }
};

} // namespace

class TestQsl : public QObject {
    Q_OBJECT
    QTemporaryDir m_settingsDir;
private slots:
    void unconfiguredUploadServicesDoNotConnect()
    {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        QSignalSpy connections(&server, &QTcpServer::newConnection);
        const QUrl url(QStringLiteral("http://127.0.0.1:%1/api/").arg(server.serverPort()));
        WebQslUploader web;
        web.setEndpoints(url, url);
        web.setHrdLogEndpoint(url);
        web.setCrxEndpoint(url);
        QSignalSpy done(&web, &WebQslUploader::finished);
        QSignalSpy crx(&web, &WebQslUploader::crxLogsListed);
        QSignalSpy wavelog(&web, &WebQslUploader::wavelogStationsListed);
        web.uploadQrz(QStringLiteral(" "), QStringLiteral("<EOR>"));
        web.uploadEqsl(QStringLiteral("K1ABC"), QStringLiteral(" "), QStringLiteral("<EOR>"));
        web.uploadEqsl(QStringLiteral(" "), QStringLiteral("secret"), QStringLiteral("<EOR>"));
        web.uploadHrdLog(QStringLiteral("K1ABC"), QStringLiteral(" "), QStringLiteral("<EOR>"));
        web.uploadWavelog(url.toString(), QStringLiteral(" "), QStringLiteral("1"), QStringLiteral("<EOR>"));
        web.uploadCrx(QStringLiteral(" "), {});
        web.deleteCrx(QStringLiteral(" "), 1);
        web.listCrxLogs(QStringLiteral(" "));
        web.listWavelogStations(url.toString(), QStringLiteral(" "));
        QCOMPARE(done.size(), 7);
        QCOMPARE(crx.size(), 1);
        QCOMPARE(wavelog.size(), 1);
        QTest::qWait(100);
        QCOMPARE(connections.size(), 0);
    }

    void initTestCase() {
        QVERIFY(m_settingsDir.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("DecoDXLogTests"));
        QCoreApplication::setApplicationName(QStringLiteral("QslCoreTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
    }
    void init() { QSettings().clear(); }

    void sentStateOverridesExplicitAdifNo_data()
    {
        QTest::addColumn<QString>("service");
        QTest::addColumn<QString>("sentField");
        QTest::addColumn<QString>("sentDateField");
        QTest::newRow("eqsl") << "eqsl" << "EQSL_QSL_SENT" << "EQSL_QSLSDATE";
        QTest::newRow("lotw") << "lotw" << "LOTW_QSL_SENT" << "LOTW_QSLSDATE";
        QTest::newRow("qrz") << "qrz" << "QRZCOM_QSO_UPLOAD_STATUS" << "QRZCOM_QSO_UPLOAD_DATE";
        QTest::newRow("clublog") << "clublog" << "CLUBLOG_QSO_UPLOAD_STATUS" << "CLUBLOG_QSO_UPLOAD_DATE";
        QTest::newRow("hrdlog") << "hrdlog" << "HRDLOG_QSO_UPLOAD_STATUS" << "HRDLOG_QSO_UPLOAD_DATE";
    }

    void sentStateOverridesExplicitAdifNo()
    {
        QFETCH(QString, service);
        QFETCH(QString, sentField);
        QFETCH(QString, sentDateField);
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        AdifRecord initial{{"CALL", "K1ABC"}, {"QSO_DATE", "20261118"}, {"TIME_ON", "120000"},
                           {"BAND", "20m"}, {"MODE", "FT8"}};
        initial.set(sentField, "N");
        initial.set(sentDateField, "20200101");
        const auto inserted = db.insertQso(initial, "test");
        QCOMPARE(inserted.status, InsertResult::Status::Inserted);
        QCOMPARE(db.record(inserted.id)->value(sentField), QString("N"));
        QCOMPARE(db.qsosToUpload(service), QList<qint64>{inserted.id});

        QslState sent;
        sent.service = service;
        sent.sent = "Y";
        sent.sentDate = "20261118";
        QVERIFY(db.setQslState(inserted.id, sent));
        QVERIFY(db.qsosToUpload(service).isEmpty());
        auto record = db.record(inserted.id);
        QVERIFY(record.has_value());
        QCOMPARE(record->value(sentField), QString("Y"));
        QCOMPARE(record->value(sentDateField), QString("20261118"));
        record->set("COMMENT", "edited");
        QCOMPARE(db.updateQso(inserted.id, *record).status, InsertResult::Status::Inserted);
        QCOMPARE(db.record(inserted.id)->value(sentField), QString("Y"));
        QVERIFY(db.qsosToUpload(service).isEmpty());
    }

    void cardStateOverridesStaleAdifSentAndReceived()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        const auto inserted = db.insertQso({{"CALL", "K1ABC"}, {"QSO_DATE", "20261118"},
                                            {"TIME_ON", "120000"}, {"BAND", "20m"}, {"MODE", "FT8"},
                                            {"QSL_SENT", "N"}, {"QSLSDATE", "20200101"},
                                            {"QSL_RCVD", "N"}, {"QSLRDATE", "20200102"},
                                            {"APP_EXAMPLE_NOTE", "kept"}}, "test");
        QCOMPARE(inserted.status, InsertResult::Status::Inserted);
        QslState card;
        card.service = "card";
        card.sent = "Y";
        card.sentDate = "20261118";
        card.rcvd = "Y";
        card.rcvdDate = "20261119";
        QVERIFY(db.setCardState(inserted.id, card));
        auto record = db.record(inserted.id);
        QVERIFY(record.has_value());
        QCOMPARE(record->value("QSL_SENT"), QString("Y"));
        QCOMPARE(record->value("QSLSDATE"), QString("20261118"));
        QCOMPARE(record->value("QSL_RCVD"), QString("Y"));
        QCOMPARE(record->value("QSLRDATE"), QString("20261119"));
        QCOMPARE(record->value("APP_EXAMPLE_NOTE"), QString("kept"));

        card.sent = "N";
        card.rcvd = "N";
        card.sentDate.clear();
        card.rcvdDate.clear();
        QVERIFY(db.setCardState(inserted.id, card));
        record = db.record(inserted.id);
        QCOMPARE(record->value("QSL_SENT"), QString("N"));
        QCOMPARE(record->value("QSL_RCVD"), QString("N"));
        QVERIFY(record->value("QSLSDATE").isEmpty());
        QVERIFY(record->value("QSLRDATE").isEmpty());

        // Legacy or interrupted writes may also leave contradictory Y values
        // in extras; the current N must not resurrect either confirmation.
        QSqlQuery extra(db.connection());
        extra.prepare("UPDATE qso SET adif_extra = ? WHERE id = ?");
        extra.addBindValue(QStringLiteral(R"({"QSL_SENT":"Y","QSLSDATE":"20200101","QSL_RCVD":"Y","QSLRDATE":"20200102","APP_EXAMPLE_NOTE":"kept"})"));
        extra.addBindValue(inserted.id);
        QVERIFY(extra.exec());
        record = db.record(inserted.id);
        QVERIFY(record->value("QSL_SENT").isEmpty());
        QVERIFY(record->value("QSL_RCVD").isEmpty());
        QVERIFY(record->value("QSLSDATE").isEmpty());
        QVERIFY(record->value("QSLRDATE").isEmpty());
        QCOMPARE(record->value("APP_EXAMPLE_NOTE"), QString("kept"));
    }

    void clubLogOqrsPaginatesDirectRequestsWithoutConfirmingQsos()
    {
        FakeClubLogPoll server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        server.answers = {R"({"matches":[{"dxcall":"K1AAA","band":"20","mode":"FT8","qsodate":"2026-01-01T10:00:00Z","note":"cl_fixture"}],"next_cursor":"page2"})",
                          R"({"matches":[{"dxcall":"K1BBB","band":"40","mode":"CW","qsodate":"2026-01-02T10:00:00Z"}],"next_cursor":null})"};
        ClubLogOqrsClient client;
        client.setBaseUrl(server.url());
        QSignalSpy done(&client, &ClubLogOqrsClient::finished);
        client.fetch(QStringLiteral("VP8/G7VJR"), QStringLiteral("cl_fixture"));
        QVERIFY(done.wait(5000));
        const QList<QVariant> result = done.takeFirst();
        QVERIFY(result.at(0).toBool());
        const QVariantList rows = result.at(1).toList();
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows.at(0).toMap().value("dxcall").toString(), QStringLiteral("K1AAA"));
        QCOMPARE(rows.at(0).toMap().value("note").toString(), QStringLiteral("[redacted]"));
        QVERIFY(!rows.at(0).toMap().contains("confirmed"));
        QCOMPARE(server.requests.size(), 2);
        QVERIFY(server.requests.at(0).startsWith("GET /api/logs/VP8/G7VJR/oqrs-matches "));
        QVERIFY(server.requests.at(1).startsWith("GET /api/logs/VP8/G7VJR/oqrs-matches?cursor=page2 "));
        for (const QByteArray& request : server.requests) {
            QVERIFY(hasBearerAuthorization(request, "cl_fixture"));
            QVERIFY(!request.left(request.indexOf("\r\n")).contains("cl_fixture"));
        }
    }

    void clubLogOqrsRejectsPartialOrRepeatedPages()
    {
        FakeClubLogPoll server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        server.answers = {R"({"matches":[{"dxcall":"K1AAA"}],"next_cursor":"same"})",
                          R"({"matches":[{"dxcall":"K1BBB"}],"next_cursor":"same"})"};
        ClubLogOqrsClient client;
        client.setBaseUrl(server.url());
        QSignalSpy done(&client, &ClubLogOqrsClient::finished);
        client.fetch(QStringLiteral("K1TEST"), QStringLiteral("cl_fixture"));
        QVERIFY(done.wait(5000));
        const QList<QVariant> result = done.takeFirst();
        QVERIFY(!result.at(0).toBool());
        QVERIFY(result.at(1).toList().isEmpty());
        QVERIFY(!result.at(2).toString().isEmpty());
        QCOMPARE(server.requests.size(), 2);
    }

    void clubLogOqrsRejectsCursorContainingToken()
    {
        FakeClubLogPoll server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        server.answers = {R"({"matches":[{"dxcall":"K1AAA"}],"next_cursor":"cl_fixture"})"};
        ClubLogOqrsClient client;
        client.setBaseUrl(server.url());
        QSignalSpy done(&client, &ClubLogOqrsClient::finished);
        client.fetch(QStringLiteral("K1TEST"), QStringLiteral("cl_fixture"));
        QVERIFY(done.wait(5000));
        QVERIFY(!done.first().at(0).toBool());
        QCOMPARE(server.requests.size(), 1);
    }
    void clubLogBatchResultsRequireUniqueKnownIdentities()
    {
        const QHash<qint64, QString> ids{{1, QStringLiteral("101")}, {2, QStringLiteral("102")}};
        const QVariantList rows{QVariantMap{{"qsoid", 102}, {"status", "duplicate"}},
                                QVariantMap{{"qsoid", 101}, {"status", "updated"}}};
        const auto matched = matchClubLogBatchResults(ids, rows);
        QCOMPARE(matched.size(), 2);
        QCOMPARE(matched.value(1).value("status").toString(), QStringLiteral("updated"));
        QCOMPARE(matched.value(2).value("status").toString(), QStringLiteral("duplicate"));
        QVERIFY(matchClubLogBatchResults({{1, "101"}, {2, "101"}}, rows).isEmpty());
        QVERIFY(matchClubLogBatchResults({{1, "101"}, {2, ""}}, rows).isEmpty());
        QVERIFY(matchClubLogBatchResults(ids, {rows.first(), rows.first()}).isEmpty());
        QVERIFY(matchClubLogBatchResults(ids, {rows.first()}).isEmpty());
        QVERIFY(matchClubLogBatchResults(ids, {rows.first(), QVariantMap{{"qsoid", 101}, {"status", "rejected"}}}).isEmpty());
        const QVariantMap rejected{{"qsoid", QVariant()}, {"status", "rejected"}, {"message", "invalid"}};
        const auto partial = matchClubLogBatchResults(ids, {rows.first(), rejected});
        QCOMPARE(partial.size(), 2);
        QCOMPARE(partial.value(1).value("status").toString(), QStringLiteral("rejected"));
        const QVariantMap another{{"qsoid", QVariant()}, {"status", "rejected"}, {"message", "bad band"}};
        const auto bothRejected = matchClubLogBatchResults(ids, {rejected, another});
        QCOMPARE(bothRejected.size(), 2);
        for (const QVariantMap& value : bothRejected) {
            QCOMPARE(value.value("status").toString(), QStringLiteral("rejected"));
            QVERIFY(value.value("message").toString().contains(QStringLiteral("individual reason is unknown")));
            QVERIFY(value.value("message").toString().contains(QStringLiteral("invalid")));
            QVERIFY(value.value("message").toString().contains(QStringLiteral("bad band")));
        }
        const QVariantMap withoutReason{{"qsoid", QVariant()}, {"status", "rejected"}};
        const auto missingReason = matchClubLogBatchResults(ids, {rejected, withoutReason});
        QCOMPARE(missingReason.size(), 2);
        for (const QVariantMap& value : missingReason) {
            QVERIFY(value.value("message").toString().contains(QStringLiteral("individual reason is unknown")));
            QVERIFY(value.value("message").toString().contains(QStringLiteral("invalid")));
        }
    }

    void clubLogIdentityQueryUsesFiltersAndReturnedIds()
    {
        const AdifRecord a{{"CALL", "K1AAA"}, {"QSO_DATE", "20260101"}, {"TIME_ON", "1000"},
                           {"BAND", "20m"}, {"MODE", "MFSK"}, {"SUBMODE", "FT4"}};
        const AdifRecord b{{"CALL", "K1BBB"}, {"QSO_DATE", "20260101"}, {"TIME_ON", "110012"},
                           {"BAND", "70cm"}, {"MODE", "CW"}};
        const QJsonObject first = clubLogIdentityFilter(a), second = clubLogIdentityFilter(b);
        QCOMPARE(first.value("mode").toString(), QStringLiteral("FT4"));
        QCOMPARE(first.value("band").toString(), QStringLiteral("20"));
        QCOMPARE(first.value("to").toString(), QStringLiteral("2026-01-01T10:00:59Z"));
        QCOMPARE(second.value("band").toString(), QStringLiteral("70cm"));
        QCOMPARE(second.value("to").toString(), QStringLiteral("2026-01-01T11:00:12Z"));
        FakeCrx server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        const QJsonArray responses{
            QJsonObject{{"filter", second}, {"qsos", QJsonArray{QJsonObject{{"qsoid", 102}}}}},
            QJsonObject{{"filter", first}, {"qsos", QJsonArray{QJsonObject{{"qsoid", 101}}}}}};
        server.answer = QJsonDocument(QJsonObject{{"results", responses}}).toJson(QJsonDocument::Compact);
        WebQslUploader web;
        web.setClubLogRestBatchBaseUrl(server.url());
        bool done = false;
        QList<QString> resolved;
        QString error;
        web.resolveClubLogBatchIdentities("K1TEST", "cl_test", {first, second}, {101, 102},
            [&](const QList<QString>& ids, const QString& message) { resolved = ids; error = message; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(resolved, (QList<QString>{"101", "102"}));
        QVERIFY(server.request.startsWith("POST /api/logs/K1TEST/qsos/query "));
        QVERIFY(hasBearerAuthorization(server.request, "cl_test"));
        QVERIFY(!server.request.left(server.request.indexOf("\r\n")).contains("cl_test"));
    }

    void clubLogIdentityQueryRejectsAmbiguousMatches()
    {
        const AdifRecord a{{"CALL", "K1AAA"}, {"QSO_DATE", "20260101"}, {"TIME_ON", "1000"},
                           {"BAND", "20m"}, {"MODE", "CW"}};
        const QJsonObject filter = clubLogIdentityFilter(a);
        FakeCrx server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        const QJsonArray qsos{QJsonObject{{"qsoid", 101}}, QJsonObject{{"qsoid", 102}}};
        server.answer = QJsonDocument(QJsonObject{{"results", QJsonArray{QJsonObject{{"filter", filter}, {"qsos", qsos}}}}}).toJson(QJsonDocument::Compact);
        WebQslUploader web;
        web.setClubLogRestBatchBaseUrl(server.url());
        bool done = false;
        QString error;
        // Two candidate IDs for one filter cannot arise when the complete
        // expected set contains only one ID; test duplicate local filters too.
        web.resolveClubLogBatchIdentities("K1TEST", "cl_test", {filter, filter}, {101, 102},
            [&](const QList<QString>& ids, const QString& message) { QVERIFY(ids.isEmpty()); error = message; done = true; });
        QVERIFY(done);
        QVERIFY(!error.isEmpty());
        QVERIFY(server.request.isEmpty());
        AdifRecord b = a;
        b.set(QStringLiteral("CALL"), QStringLiteral("K1BBB"));
        const QJsonObject second = clubLogIdentityFilter(b);
        server.answer = QJsonDocument(QJsonObject{{"results", QJsonArray{
            QJsonObject{{"filter", filter}, {"qsos", qsos}},
            QJsonObject{{"filter", second}, {"qsos", QJsonArray{QJsonObject{{"qsoid", 102}}}}}}}}).toJson(QJsonDocument::Compact);
        done = false; error.clear();
        web.resolveClubLogBatchIdentities("K1TEST", "cl_test", {filter, second}, {101, 102},
            [&](const QList<QString>& ids, const QString& message) { QVERIFY(ids.isEmpty()); error = message; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        QVERIFY(!error.isEmpty());
    }

    void clubLogIdentityQueryCanResolveMultipleRejectedContacts()
    {
        const QJsonObject a{{"dxcall", "K1AAA"}}, b{{"dxcall", "K1BBB"}}, c{{"dxcall", "K1CCC"}};
        const QJsonArray responses{
            QJsonObject{{"filter", c}, {"qsos", QJsonArray{}}},
            QJsonObject{{"filter", a}, {"qsos", QJsonArray{QJsonObject{{"qsoid", 101}}}}},
            QJsonObject{{"filter", b}, {"qsos", QJsonArray{}}}};
        FakeCrx server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        server.answer = QJsonDocument(QJsonObject{{"results", responses}}).toJson(QJsonDocument::Compact);
        WebQslUploader web;
        web.setClubLogRestBatchBaseUrl(server.url());
        bool done = false;
        QList<QString> ids;
        QString error;
        web.resolveClubLogBatchIdentities("K1TEST", "cl_test", {a, b, c}, {101},
            [&](const QList<QString>& result, const QString& message) { ids = result; error = message; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(ids, (QList<QString>{QStringLiteral("101"), QString(), QString()}));
    }

    void clubLogIdentityQueriesRespectHundredFilterLimit()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QList<int> chunkSizes;
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
                const QJsonArray filters = QJsonDocument::fromJson(request->mid(headerEnd + 4)).object().value("filters").toArray();
                chunkSizes.append(filters.size());
                QJsonArray results;
                for (const QJsonValue& filter : filters) {
                    const int id = filter.toObject().value("dxcall").toString().mid(1).toInt();
                    results.prepend(QJsonObject{{"filter", filter.toObject()}, {"qsos", QJsonArray{QJsonObject{{"qsoid", id}}}}});
                }
                const QByteArray body = QJsonDocument(QJsonObject{{"results", results}}).toJson(QJsonDocument::Compact);
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                              + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
        QList<QJsonObject> filters;
        QSet<qint64> ids;
        for (int i = 1; i <= 101; ++i) {
            filters.append(QJsonObject{{"dxcall", QStringLiteral("K%1").arg(i)}});
            ids.insert(i);
        }
        WebQslUploader web;
        web.setClubLogRestBatchBaseUrl(QUrl(QStringLiteral("http://127.0.0.1:%1/api/").arg(server.serverPort())));
        bool done = false;
        QList<QString> resolved;
        QString error;
        web.resolveClubLogBatchIdentities("K1TEST", "cl_test", filters, ids,
            [&](const QList<QString>& result, const QString& message) { resolved = result; error = message; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(chunkSizes, (QList<int>{100, 1}));
        QCOMPARE(resolved.size(), 101);
        QCOMPARE(resolved.first(), QStringLiteral("1"));
        QCOMPARE(resolved.last(), QStringLiteral("101"));
    }

    void automaticServicesAreQueuedSequentially()
    {
        qsl::AutomaticUploadQueue queue;
        queue.reset({QStringLiteral("lotw"), QStringLiteral("eqsl")});

        QCOMPARE(queue.takeNext(false), QStringLiteral("lotw"));
        // Mentre LoTW sta lavorando, eQSL non viene consumato ne' perso.
        QVERIFY(queue.takeNext(true).isEmpty());
        QCOMPARE(queue.takeNext(false), QStringLiteral("eqsl"));
        QVERIFY(queue.takeNext(false).isEmpty());

        // Una nuova raffica sostituisce il piano precedente, scarta voci
        // vuote e doppie e conserva l'ordine dei servizi abilitati.
        queue.reset({QStringLiteral("lotw"), QStringLiteral(""),
                     QStringLiteral("eqsl"), QStringLiteral("lotw")});
        QCOMPARE(queue.takeNext(false), QStringLiteral("lotw"));
        QCOMPARE(queue.takeNext(false), QStringLiteral("eqsl"));
        QVERIFY(queue.isEmpty());

        queue.reset({QStringLiteral("eqsl")});
        queue.append({QStringLiteral("clublog"), QStringLiteral("eqsl")});
        QCOMPARE(queue.takeNext(false), QStringLiteral("eqsl"));
        QCOMPARE(queue.takeNext(false), QStringLiteral("clublog"));
        QVERIFY(queue.isEmpty());
    }

    void tqslCrashAndCancellationNeedReconciliation()
    {
#ifdef Q_OS_WIN
        QSKIP("POSIX fake process fixture");
#endif
        QTemporaryDir dir; QVERIFY(dir.isValid()); const QString script=dir.filePath("fake-tqsl"); const QString ready=dir.filePath("ready");
        QFile f(script); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("#!/bin/sh\nkill -SEGV $$\n"); f.close(); QVERIFY(f.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner));
        TqslUploader uploader(nullptr, [] { return true; }); uploader.setProgram(script); QSignalSpy done(&uploader,&TqslUploader::finished);
        uploader.upload(dir.filePath("input.adi"),{},2); QTRY_VERIFY_WITH_TIMEOUT(done.size()>=1,5000); QTest::qWait(200);
        QCOMPARE(done.size(),1); const auto crashed=done.takeFirst().first().value<QslUploadResult>(); QVERIFY(!crashed.ok); QVERIFY(crashed.requiresReconciliation); QVERIFY(!crashed.retryLater);
        QVERIFY(f.open(QIODevice::WriteOnly|QIODevice::Truncate)); f.write("#!/bin/sh\ntouch "+QFile::encodeName(ready)+"\nexec sleep 30\n"); f.close();
        uploader.upload(dir.filePath("input.adi"),{},2); QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(ready),5000); uploader.cancel(); QTRY_VERIFY_WITH_TIMEOUT(done.size()>=1,5000); QTest::qWait(200);
        QCOMPARE(done.size(),1); const auto cancelled=done.takeFirst().first().value<QslUploadResult>(); QVERIFY(!cancelled.ok); QVERIFY(cancelled.requiresReconciliation); QVERIFY(!cancelled.retryLater);
        QVERIFY(f.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner)); uploader.upload(dir.filePath("input.adi"),{},2);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,5000); const auto notStarted=done.takeFirst().first().value<QslUploadResult>(); QVERIFY(!notStarted.ok); QVERIFY(!notStarted.requiresReconciliation);
    }

    void tqslExitCodes()
    {
        auto r = qsl::resultFromTqslExit(0, "Sending to LoTW", 12);
        QVERIFY(r.ok);
        QCOMPARE(r.accepted, 12);

        r = qsl::resultFromTqslExit(7, "cannot open output", 5);
        QVERIFY(!r.ok);
        QCOMPARE(r.duplicates, 0);
        QCOMPARE(r.accepted, 0);

        r = qsl::resultFromTqslExit(8, "No QSOs processed", 5);
        QVERIFY(!r.ok);
        QVERIFY(r.requiresReconciliation);
        QCOMPARE(r.accepted, 0);
        r = qsl::resultFromTqslExit(9, "Some QSOs processed", 5);
        QVERIFY(!r.ok);
        QVERIFY(r.requiresReconciliation);
        QCOMPARE(r.accepted, 0);
        QVERIFY(r.message.contains("individual QSO status is unknown"));

        r = qsl::resultFromTqslExit(2, "rejected", 3);
        QVERIFY(!r.ok);
        QCOMPARE(r.rejected, 3);
        QVERIFY(!r.retryLater);

        r = qsl::resultFromTqslExit(10, "syntax error", 3);
        QVERIFY(!r.ok);
        QVERIFY(!r.retryLater);
        r = qsl::resultFromTqslExit(11, "connection failed", 3);
        QVERIFY(!r.ok);
        QVERIFY(!r.retryLater);
        QVERIFY(r.requiresReconciliation);
        for (int code : {1, 3, 12, 99}) {
            r = qsl::resultFromTqslExit(code, "uncertain", 3);
            QVERIFY(!r.ok); QVERIFY(r.requiresReconciliation); QVERIFY(!r.retryLater);
        }

        r = qsl::resultFromTqslExit(5, "no station location", 3);
        QVERIFY(!r.ok);
        QVERIFY(r.message.contains("library error"));
    }

    void qrzInsertPreservesFormValues()
    {
        FakeCrx server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        server.answer = "RESULT=OK&LOGID=123";
        WebQslUploader web;
        web.setEndpoints(server.url(), {});
        QSignalSpy done(&web, &WebQslUploader::finished);
        const QString record = adif::writeRecord({
            {"CALL", "UN0GY"}, {"QSL_VIA", "QSL manager: M0OXO; LoTW, QRZ.com"},
            {"COMMENT", QStringLiteral("Caffè + portable & literal %20")}
        });
        web.uploadQrz("test-key", record);
        QVERIFY(done.wait(5000));
        QVERIFY(done.first().first().value<QslUploadResult>().ok);
        QVERIFY(server.request.toLower().contains("content-type: application/x-www-form-urlencoded"));
        // Servers that accept ';' as a form separator must still see one complete ADIF value.
        QMap<QByteArray, QByteArray> fields;
        for (const auto& part : QByteArray(server.body).replace(';', '&').split('&')) {
            const auto pos = part.indexOf('=');
            QVERIFY(pos >= 0);
            fields.insert(part.left(pos), QByteArray::fromPercentEncoding(part.mid(pos + 1).replace('+', ' ')));
        }
        QCOMPARE(fields.size(), 3);
        QCOMPARE(fields.value("KEY"), QByteArray("test-key"));
        QCOMPARE(fields.value("ACTION"), QByteArray("INSERT"));
        QCOMPARE(fields.value("ADIF"), record.toUtf8());
    }

    void qrzRejectedAdifMarker_data()
    {
        QTest::addColumn<QByteArray>("reason");
        QTest::newRow("literal HTML entities from QRZ") << QByteArray("missing &lt;eor&gt; in record");
        QTest::newRow("percent-encoded ADIF tag") << QByteArray("missing%20%3Ceor%3E%20in%20record");
        QTest::newRow("form-encoded HTML entities") << QByteArray("missing+%26lt%3Beor%26gt%3B+in+record");
    }

    void qrzRejectedAdifMarker()
    {
        QFETCH(QByteArray, reason);
        const auto result = qsl::parseQrzResponse("STATUS=FAIL&RESULT=FAIL&REASON=" + reason + "&EXTENDED=");
        QVERIFY(!result.ok);
        QCOMPARE(result.rejected, 1);
        QCOMPARE(result.message, QStringLiteral("QRZ Logbook: missing <eor> in record"));
    }

    void qrzAnswers()
    {
        auto r = qsl::parseQrzResponse("RESULT=OK&LOGID=123456&COUNT=1");
        QVERIFY(r.ok);
        QCOMPARE(r.accepted, 1);
        QCOMPARE(r.remoteId, QString("123456"));

        r = qsl::parseQrzResponse("RESULT=FAIL&REASON=Unable to add QSO to database: duplicate");
        QVERIFY(r.ok);
        QCOMPARE(r.duplicates, 1);

        r = qsl::parseQrzResponse("RESULT=AUTH&REASON=invalid api key");
        QVERIFY(!r.ok);
        QCOMPARE(r.rejected, 1);
        QVERIFY(r.message.contains("api key"));

        r = qsl::parseQrzResponse("<html>gateway timeout</html>");
        QVERIFY(!r.ok);
        QVERIFY(r.retryLater);
    }

    void hrdLogAnswers()
    {
        auto r = qsl::parseHrdLogResponse(R"(<?xml version="1.0"?><RobotRequest><insert>1</insert></RobotRequest>)");
        QVERIFY(r.ok);
        QCOMPARE(r.accepted, 1);
        r = qsl::parseHrdLogResponse("<RobotRequest><insert>0</insert><error>Duplicate QSO</error></RobotRequest>");
        QVERIFY(r.ok);
        QCOMPARE(r.duplicates, 1);
        r = qsl::parseHrdLogResponse("<RobotRequest><error>Invalid upload code</error></RobotRequest>");
        QVERIFY(!r.ok);
        QCOMPARE(r.rejected, 1);
        QVERIFY(r.message.contains("Invalid upload code"));
        r = qsl::parseHrdLogResponse("");
        QVERIFY(r.retryLater);
    }

    void eqslAnswers()
    {
        auto r = qsl::parseEqslResponse("<html><body>Result: 1 out of 1 records added</body></html>");
        QVERIFY(r.ok);
        QCOMPARE(r.accepted, 1);

        r = qsl::parseEqslResponse("<html>Result: 0 out of 1 records added<br>Warning: Duplicate</html>");
        QVERIFY(r.ok);
        QCOMPARE(r.duplicates, 1);

        r = qsl::parseEqslResponse("<html>Bad record: no callsign</html>");
        QVERIFY(!r.ok);
        QCOMPARE(r.rejected, 1);

        r = qsl::parseEqslResponse("");
        QVERIFY(!r.ok);
        QVERIFY(r.retryLater);
    }

    void clubLogAnswers()
    {
        auto r = qsl::parseClubLogResponse(200, "OK", 3);
        QVERIFY(r.ok);
        QCOMPARE(r.accepted, 3);

        r = qsl::parseClubLogResponse(200, "Duplicate QSO ignored", 1);
        QVERIFY(r.ok);
        QCOMPARE(r.duplicates, 1);
        QCOMPARE(r.accepted, 0);

        // Chiave o password sbagliate: e' inutile ritentare, e il motivo si legge.
        r = qsl::parseClubLogResponse(403, "Invalid API Key", 5);
        QVERIFY(!r.ok);
        QVERIFY(!r.retryLater);
        QCOMPARE(r.rejected, 5);
        QVERIFY(r.message.contains(QLatin1String("Invalid API Key")));

        r = qsl::parseClubLogResponse(500, "<html><body>Server error</body></html>", 2);
        QVERIFY(!r.ok);
        QVERIFY(r.retryLater);

        r = qsl::parseClubLogResponse(0, "", 1);
        QVERIFY(!r.ok);
        QVERIFY(r.retryLater);
    }

    void crxQsoFromTheLog()
    {
        // Il QSO come lo vuole CRX: campi logentry_*, frequenza in kHz, il modo
        // vero (FT8, non MFSK) e l'ora in secondi Unix, come in get_myqsos.
        AdifRecord r{{"CALL", "w1aw"}, {"QSO_DATE", "20241020"}, {"TIME_ON", "1248"}, {"BAND", "40M"},
                     {"FREQ", "7.025"}, {"MODE", "MFSK"}, {"SUBMODE", "FT4"}, {"RST_SENT", "-05"},
                     {"RST_RCVD", "-12"}, {"NAME", "John"}, {"COMMENT", "Good signal"}};
        const QJsonObject o = qsl::crxQsoData(r, 123, 0);
        QCOMPARE(o.value("qso_id").toInteger(), 0);
        QCOMPARE(o.value("f_log_id").toInteger(), 123);
        QCOMPARE(o.value("logentry_his_call").toString(), QString("W1AW"));
        QCOMPARE(o.value("logentry_band").toString(), QString("40m"));
        QCOMPARE(o.value("logentry_frequency").toString(), QString("7025"));
        QCOMPARE(o.value("logentry_mode").toString(), QString("FT4"));
        QCOMPARE(o.value("logentry_his_report").toString(), QString("-05"));
        QCOMPARE(o.value("logentry_my_report").toString(), QString("-12"));
        QCOMPARE(o.value("logentry_his_name").toString(), QString("John"));
        QCOMPARE(o.value("logentry_comment").toString(), QString("Good signal"));
        // 2024-10-20 12:48 UTC
        QCOMPARE(o.value("logentry_date").toInteger(), 1729428480);

        // Un QSO gia' mandato si corregge con il suo numero, e l'SSB resta SSB.
        AdifRecord ssb{{"CALL", "IK0ABC"}, {"QSO_DATE", "20260924"}, {"TIME_ON", "101530"}, {"BAND", "20m"},
                       {"FREQ", "14.2745"}, {"MODE", "SSB"}, {"SUBMODE", "USB"}};
        const QJsonObject again = qsl::crxQsoData(ssb, 123, 456);
        QCOMPARE(again.value("qso_id").toInteger(), 456);
        QCOMPARE(again.value("logentry_mode").toString(), QString("SSB"));
        QCOMPARE(again.value("logentry_frequency").toString(), QString("14274.5"));
        QVERIFY(!again.contains("logentry_his_name"));
        QVERIFY(!again.contains("logentry_custom_field38"));

        // Il numero del QSO qui va nel campo personalizzato 38.
        QCOMPARE(qsl::crxQsoData(ssb, 123, 0, 116).value("logentry_custom_field38").toString(), QString("116"));

        // La mappa: il numero CRX vale nel suo log; i vecchi, senza log, valgono.
        QCOMPARE(qsl::crxRemoteKey(123, "111354"), QString("123:111354"));
        QCOMPARE(qsl::crxRemoteQso("123:111354", 123), 111354);
        QCOMPARE(qsl::crxRemoteQso("123:111354", 7), 0);
        QCOMPARE(qsl::crxRemoteQso("456", 7), 456);
        QCOMPARE(qsl::crxRemoteQso("", 7), 0);
    }

    void crxAnswers()
    {
        auto r = qsl::parseCrxResponse(200, R"({"success": true, "message": "QSO created successfully", "qso_id": 457})");
        QVERIFY(r.ok);
        QCOMPARE(r.accepted, 1);
        QCOMPARE(r.remoteId, QString("457"));

        // Chiave sbagliata: vale per tutti, ci si ferma.
        r = qsl::parseCrxResponse(401, R"({"error": "Invalid API key"})");
        QVERIFY(!r.ok);
        QVERIFY(r.retryLater);

        // Un QSO rifiutato: il motivo resta scritto, gli altri partono.
        r = qsl::parseCrxResponse(400, R"({"error": "Missing logentry_his_call"})");
        QVERIFY(!r.ok);
        QVERIFY(!r.retryLater);
        QCOMPARE(r.rejected, 1);
        QVERIFY(r.message.contains(QLatin1String("Missing logentry_his_call")));

        // Com'e' oggi: la chiave rifiutata arriva con 500 e il motivo nel corpo.
        // Non e' "il servizio non risponde": e' la chiave, e va detto.
        r = qsl::parseCrxResponse(500, R"({"error":"Error : 20-api-key-auth-error"})");
        QVERIFY(!r.ok);
        QVERIFY(r.retryLater);
        QVERIFY(r.message.contains(QStringLiteral("API key")));
        QVERIFY(r.message.contains(QStringLiteral("20-api-key-auth-error")));
        QVERIFY(r.message.contains(QStringLiteral("my-api")));
        {
            QString error;
            QVERIFY(qsl::parseCrxLogs(R"({"error":"Error : 20-api-key-auth-error"})", &error).isEmpty());
            QVERIFY(error.contains(QStringLiteral("API key")));
        }
        r = qsl::parseCrxResponse(500, "<html>oops</html>");
        QVERIFY(r.retryLater);

        // Risposte vere di CRX (prova del 25/09/2026): il numero arriva come
        // testo, la modifica non lo ripete, la cancellazione di un QSO che non
        // c'e' piu' e' un 404 che non va ripetuto.
        r = qsl::parseCrxResponse(200, R"({"success":true,"message":"QSO created successfully","qso_id":"1"})");
        QCOMPARE(r.remoteId, QString("1"));
        r = qsl::parseCrxResponse(200, R"({"success":true,"message":"QSO updated successfully"})");
        QVERIFY(r.ok);
        QCOMPARE(r.remoteId, QString("0"));
        r = qsl::parseCrxResponse(404, R"({"error":"QSO not found or already deleted"})");
        QVERIFY(!r.ok);
        QVERIFY(!r.retryLater);

        QString error;
        const QVariantList logs = qsl::parseCrxLogs(
            R"({"logs": [{"log_id": 123, "log_name": "IOTA Contest 2023", "log_activation_call": "F/K1ABC", "log_desc": "EU-048"}]})",
            &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(logs.size(), 1);
        QCOMPARE(logs.first().toMap().value("id").toLongLong(), 123);
        QCOMPARE(logs.first().toMap().value("name").toString(), QString("IOTA Contest 2023"));
    }

    void crxOverTheWire()
    {
        // La busta vera: POST JSON {"req": {type, query, apikey, qsoData}}.
        FakeCrx crx;
        QVERIFY(crx.listen(QHostAddress::LocalHost));
        crx.answer = R"({"success": true, "qso_id": 457})";
        WebQslUploader web;
        web.setCrxEndpoint(crx.url());
        QSignalSpy done(&web, &WebQslUploader::finished);
        AdifRecord r{{"CALL", "W1AW"}, {"QSO_DATE", "20241020"}, {"TIME_ON", "1248"}, {"BAND", "40m"},
                     {"FREQ", "7.025"}, {"MODE", "CW"}};
        web.uploadCrx("HAM-XX-12345678-12345678", qsl::crxQsoData(r, 123, 0));
        QVERIFY(done.wait(5000));
        const auto result = done.first().first().value<QslUploadResult>();
        QVERIFY(result.ok);
        QCOMPARE(result.remoteId, QString("457"));

        QVERIFY(crx.request.startsWith("POST /api/ "));
        QVERIFY(crx.request.toLower().contains("content-type: application/json"));
        const QJsonObject req = QJsonDocument::fromJson(crx.body).object().value("req").toObject();
        QCOMPARE(req.value("type").toString(), QString("radio"));
        QCOMPARE(req.value("query").toString(), QString("edit_myqso"));
        QCOMPARE(req.value("apikey").toString(), QString("HAM-XX-12345678-12345678"));
        QCOMPARE(req.value("qsoData").toObject().value("logentry_his_call").toString(), QString("W1AW"));

        // La cancellazione: edit_myqso con qso_id e action "delete", come HAMPI.
        FakeCrx del;
        QVERIFY(del.listen(QHostAddress::LocalHost));
        del.answer = R"({"success": true})";
        web.setCrxEndpoint(del.url());
        QSignalSpy deleted(&web, &WebQslUploader::finished);
        web.deleteCrx("HAM-XX-12345678-12345678", 457);
        QVERIFY(deleted.wait(5000));
        QVERIFY(deleted.first().first().value<QslUploadResult>().ok);
        const QJsonObject delReq = QJsonDocument::fromJson(del.body).object().value("req").toObject();
        QCOMPARE(delReq.value("query").toString(), QString("edit_myqso"));
        QCOMPARE(delReq.value("action").toString(), QString("delete"));
        QCOMPARE(delReq.value("qso_id").toInteger(), 457);

        // L'elenco dei logbook.
        FakeCrx logs;
        QVERIFY(logs.listen(QHostAddress::LocalHost));
        logs.answer = R"({"logs": [{"log_id": 7, "log_name": "Station", "log_activation_call": "", "log_desc": ""}]})";
        web.setCrxEndpoint(logs.url());
        QSignalSpy listed(&web, &WebQslUploader::crxLogsListed);
        web.listCrxLogs("HAM-XX-12345678-12345678");
        QVERIFY(listed.wait(5000));
        QCOMPARE(listed.first().at(0).toList().size(), 1);
        QVERIFY(listed.first().at(1).toString().isEmpty());
        QCOMPARE(QJsonDocument::fromJson(logs.body).object().value("req").toObject().value("query").toString(),
                 QString("get_mylogs"));
    }

    void wavelogAddresses()
    {
        // Quello che l'operatore scrive, e l'indirizzo dell'API che ne viene.
        QCOMPARE(qsl::wavelogApiUrl("log.example.org", "qso"), QUrl("https://log.example.org/index.php/api/qso"));
        QCOMPARE(qsl::wavelogApiUrl("https://x.org/wavelog/", "qso"), QUrl("https://x.org/wavelog/index.php/api/qso"));
        QCOMPARE(qsl::wavelogApiUrl("http://192.168.1.5:8086/index.php/api/qso", "station_info/K"),
                 QUrl("http://192.168.1.5:8086/index.php/api/station_info/K"));
        QVERIFY(qsl::wavelogApiUrl("  ", "qso").isEmpty());
    }

    void wavelogAnswers()
    {
        QslUploadResult r = qsl::parseWavelogResponse(201, R"({"status":"created","type":"adif","adif_count":1,"adif_errors":0})");
        QVERIFY(r.ok);
        QCOMPARE(r.accepted, 1);
        r = qsl::parseWavelogResponse(201, R"({"status":"created","adif_errors":1,"messages":["Duplicate for W1AW 20m FT8"]})");
        QVERIFY(r.ok);
        QCOMPARE(r.duplicates, 1);
        r = qsl::parseWavelogResponse(201, R"({"status":"created","adif_errors":1,"messages":["Missing mode"]})");
        QCOMPARE(r.rejected, 1);
        QVERIFY(r.message.contains("Missing mode"));
        r = qsl::parseWavelogResponse(401, R"({"status":"failed","reason":"missing api key"})");
        QVERIFY(!r.ok);
        QVERIFY(r.message.contains("API key"));
        r = qsl::parseWavelogResponse(400, R"({"status":"failed","reason":"station id does not belong to the API key owner."})");
        QVERIFY(!r.ok);
        QCOMPARE(r.rejected, 1);

        QString error;
        const QVariantList stations = qsl::parseWavelogStations(
            R"([{"station_id":"1","station_profile_name":"Casa","station_gridsquare":"JN71DC","station_callsign":"IU8LMC","station_active":"1"},
                {"station_id":2,"station_profile_name":"Portatile","station_callsign":"IU8LMC/P","station_active":null}])",
            &error);
        QCOMPARE(stations.size(), 2);
        QCOMPARE(stations.at(0).toMap().value("id").toString(), QString("1"));
        QVERIFY(stations.at(0).toMap().value("active").toBool());
        QCOMPARE(stations.at(1).toMap().value("id").toString(), QString("2"));
        QVERIFY(!stations.at(1).toMap().value("active").toBool());
        QVERIFY(qsl::parseWavelogStations(R"({"status":"failed","reason":"missing api key"})", &error).isEmpty());
        QCOMPARE(error, QString("missing api key"));
    }

    void wavelogOverTheWire()
    {
        FakeCrx wavelog;
        QVERIFY(wavelog.listen(QHostAddress::LocalHost));
        wavelog.status = 201;
        wavelog.answer = R"({"status":"created","type":"adif","adif_count":1,"adif_errors":0})";
        WebQslUploader web;
        QSignalSpy done(&web, &WebQslUploader::finished);
        web.uploadWavelog(QStringLiteral("http://127.0.0.1:%1/").arg(wavelog.serverPort()), "wl64key", "3",
                          "<CALL:4>W1AW<EOR>");
        QVERIFY(done.wait(5000));
        QVERIFY(done.first().first().value<QslUploadResult>().ok);
        QVERIFY(wavelog.request.startsWith("POST /index.php/api/qso "));
        const QJsonObject body = QJsonDocument::fromJson(wavelog.body).object();
        QCOMPARE(body.value("key").toString(), QString("wl64key"));
        QCOMPARE(body.value("station_profile_id").toString(), QString("3"));
        QCOMPARE(body.value("type").toString(), QString("adif"));
        QCOMPARE(body.value("string").toString(), QString("<CALL:4>W1AW<EOR>"));

        // Un rifiuto con 400 dice il motivo, non "errore HTTP".
        FakeCrx refusing;
        QVERIFY(refusing.listen(QHostAddress::LocalHost));
        refusing.status = 400;
        refusing.answer = R"({"status":"failed","reason":"wrong station"})";
        QSignalSpy refused(&web, &WebQslUploader::finished);
        web.uploadWavelog(QStringLiteral("http://127.0.0.1:%1").arg(refusing.serverPort()), "wl64key", "3", "<CALL:4>W1AW<EOR>");
        QVERIFY(refused.wait(5000));
        const auto result = refused.first().first().value<QslUploadResult>();
        QVERIFY(!result.retryLater);
        QVERIFY2(result.message.contains("wrong station"), qPrintable(result.message));

        // L'elenco delle stazioni: la chiave nell'indirizzo.
        FakeCrx stations;
        QVERIFY(stations.listen(QHostAddress::LocalHost));
        stations.answer = R"([{"station_id":"3","station_profile_name":"Casa","station_callsign":"IU8LMC","station_active":"1"}])";
        QSignalSpy listed(&web, &WebQslUploader::wavelogStationsListed);
        web.listWavelogStations(QStringLiteral("127.0.0.1:%1").arg(stations.serverPort()).prepend("http://"), "wl64key");
        QVERIFY(listed.wait(5000));
        QCOMPARE(listed.first().at(0).toList().size(), 1);
        QVERIFY(stations.request.startsWith("GET /index.php/api/station_info/wl64key "));
    }

    void clubLogLegacySingleOverWire()
    {
        FakeCrx server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        server.answer = "OK";
        WebQslUploader web;
        web.setClubLogEndpoints(server.url(), server.url());
        QSignalSpy done(&web, &WebQslUploader::finished);
        ClubLogAuth auth;
        auth.callsign = QStringLiteral("9H1SR");
        auth.email = QStringLiteral("test@example.org");
        auth.password = QStringLiteral("legacy_password");
        auth.apiKey = QStringLiteral("legacy_key");
        web.uploadClubLog(auth, "<CALL:5>K1ABC <EOR>", 1);
        QVERIFY(done.wait(5000));
        QVERIFY(done.takeFirst().first().value<QslUploadResult>().ok);
        QVERIFY(server.body.contains("api=legacy_key"));
        QVERIFY(server.body.contains("password=legacy_password"));
        QVERIFY(!server.request.toLower().contains("authorization: bearer"));
    }

    void clubLogLegacy403BlocksFurtherRequestsAcrossInstances()
    {
        FakeCrx server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        server.status = 403; server.answer = "Forbidden";
        ClubLogAuth auth;
        auth.callsign = "K1TEST"; auth.email = "legacy-guard@example.org";
        auth.password = "legacy-guard-password"; auth.apiKey = "legacy-guard-key";
        WebQslUploader web;
        web.setClubLogEndpoints(server.url(), server.url());
        QSignalSpy done(&web, &WebQslUploader::finished);
        web.uploadClubLog(auth, "<CALL:5>K1ABC<EOR>", 1);
        QVERIFY(done.wait(5000));
        QVERIFY(!done.takeFirst().first().value<QslUploadResult>().ok);
        server.request.clear(); server.body.clear();
        WebQslUploader restarted;
        restarted.setClubLogEndpoints(server.url(), server.url());
        QSignalSpy blocked(&restarted, &WebQslUploader::finished);
        restarted.uploadClubLog(auth, "<CALL:5>K1ABC<EOR>", 1);
        QCOMPARE(blocked.size(), 1);
        QVERIFY(!blocked.takeFirst().first().value<QslUploadResult>().ok);
        QVERIFY(server.request.isEmpty());
        auth.password = "legacy-corrected-password";
        server.status = 200; server.answer = "OK";
        restarted.uploadClubLog(auth, "<CALL:5>K1ABC<EOR>", 1);
        QVERIFY(blocked.wait(5000));
        QVERIFY(blocked.takeFirst().first().value<QslUploadResult>().ok);
        QVERIFY(!server.request.isEmpty());
        QSettings settings; settings.sync();
        for (const QString& key : settings.allKeys()) {
            QVERIFY(!key.contains(auth.password));
            QVERIFY(!settings.value(key).toString().contains(auth.password));
        }
    }

    void clubLogLegacyBatchOverWire()
    {
        FakeCrx realtime, batch;
        QVERIFY(realtime.listen(QHostAddress::LocalHost));
        QVERIFY(batch.listen(QHostAddress::LocalHost));
        batch.answer = "OK";
        WebQslUploader web;
        web.setClubLogEndpoints(realtime.url(), batch.url());
        QSignalSpy done(&web, &WebQslUploader::finished);
        ClubLogAuth auth;
        auth.callsign = QStringLiteral("K1TEST");
        auth.email = QStringLiteral("test@example.org");
        auth.password = QStringLiteral("legacy_password");
        auth.apiKey = QStringLiteral("legacy_key");
        const QByteArray contacts = "<CALL:5>K1AAA<EOR><CALL:5>K1BBB<EOR>";
        web.uploadClubLog(auth, contacts, 2);
        QVERIFY(done.wait(5000));
        QVERIFY(done.takeFirst().first().value<QslUploadResult>().ok);
        QVERIFY(realtime.request.isEmpty());
        QVERIFY(batch.request.contains("multipart/form-data"));
        QVERIFY(batch.body.contains("name=\"email\""));
        QVERIFY(batch.body.contains("test@example.org"));
        QVERIFY(batch.body.contains("name=\"password\""));
        QVERIFY(batch.body.contains("legacy_password"));
        QVERIFY(batch.body.contains("name=\"api\""));
        QVERIFY(batch.body.contains("legacy_key"));
        QVERIFY(batch.body.contains("name=\"clear\"\r\n\r\n0\r\n"));
        QVERIFY(batch.body.contains(contacts));
        QVERIFY(!batch.request.toLower().contains("authorization: bearer"));
    }

    void clubLogRealtimeBatchSendsOneRequestAndPreservesResults()
    {
        FakeCrx server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        server.answer = R"({"results":[{"status":"created","qsoid":101},{"status":"duplicate","qsoid":102}]})";
        WebQslUploader web;
        web.setClubLogRestBatchBaseUrl(server.url());
        QSignalSpy done(&web, &WebQslUploader::finished);
        const QByteArray contacts = "<CALL:5>K1AAA<QSO_DATE:8>20260101<TIME_ON:4>1000<BAND:3>20m<MODE:2>CW<EOR>"
                                    "<CALL:5>K1BBB<QSO_DATE:8>20260101<TIME_ON:4>1100<BAND:3>20m<MODE:2>CW<EOR>";
        web.submitClubLogRealtimeBatch("K1TEST", "cl_fixture_batch", contacts, 2);
        QVERIFY(done.wait(5000));
        const auto result = done.takeFirst().first().value<QslUploadResult>();
        QVERIFY(result.requiresReconciliation);
        QCOMPARE(result.clubLogBatchRecords.size(), 2);
        QVERIFY(server.request.startsWith("POST /api/qso/batch "));
        QVERIFY(hasBearerAuthorization(server.request, "cl_fixture_batch"));
        const QJsonArray qsos = QJsonDocument::fromJson(server.body).object().value("qsos").toArray();
        QCOMPARE(qsos.size(), 2);
        QCOMPARE(qsos.at(0).toObject().value("call").toString(), QStringLiteral("K1TEST"));
        QVERIFY(qsos.at(0).toObject().value("adif").toString().contains("K1AAA"));
        QVERIFY(qsos.at(1).toObject().value("adif").toString().contains("K1BBB"));
        server.request.clear(); server.body.clear();
        server.status = 500; server.answer = R"({"error":{"message":"failure"}})";
        web.submitClubLogRealtimeBatch("K1TEST", "cl_fixture_batch", contacts, 2);
        QVERIFY(done.wait(5000));
        const auto uncertain = done.takeFirst().first().value<QslUploadResult>();
        QVERIFY(!uncertain.ok); QVERIFY(uncertain.requiresReconciliation);
        QCOMPARE(uncertain.httpStatus, 500);
        QVERIFY(uncertain.clubLogBatchRecords.isEmpty());
    }

    void clubLogRestSingleAndRejectedResponse()
    {
        FakeCrx server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        server.status = 201;
        server.answer = R"({"status":"created","qsoid":123456})";
        WebQslUploader web;
        web.setClubLogRestEndpoint(server.url());
        QSignalSpy done(&web, &WebQslUploader::finished);
        ClubLogAuth auth;
        auth.callsign = QStringLiteral("9H1SR");
        auth.password = QStringLiteral("cl_test_fixture");
        web.uploadClubLog(auth, "<CALL:5>K1ABC <QSO_DATE:8>20260101 <TIME_ON:4>1200 <BAND:3>20m <MODE:3>FT8 <EOR>", 1);
        QVERIFY(done.wait(5000));
        const auto result = done.takeFirst().first().value<QslUploadResult>();
        QVERIFY(result.ok);
        QCOMPARE(result.remoteId, QStringLiteral("123456"));
        QVERIFY(hasBearerAuthorization(server.request, "cl_test_fixture"));
        const QJsonObject body = QJsonDocument::fromJson(server.body).object();
        QCOMPARE(body.value(QStringLiteral("call")).toString(), QStringLiteral("9H1SR"));
        QVERIFY(body.value(QStringLiteral("adif")).toString().contains(QStringLiteral("K1ABC")));
        QVERIFY(!server.request.left(server.request.indexOf("\r\n")).contains("cl_test_fixture"));

        server.request.clear(); server.body.clear();
        server.status = 200;
        server.answer = R"({"status":"created","qsoid":null})";
        web.uploadClubLog(auth, "<CALL:5>K1ABC <EOR>", 1);
        QVERIFY(done.wait(5000));
        const auto unconfirmed = done.takeFirst().first().value<QslUploadResult>();
        QVERIFY(!unconfirmed.ok);
        QVERIFY(unconfirmed.requiresReconciliation);

        server.request.clear(); server.body.clear();
        server.status = 401;
        server.answer = R"({"code":"unauthorized","message":"Invalid token cl_test_fixture"})";
        web.uploadClubLog(auth, "<CALL:5>K1ABC <EOR>", 1);
        QVERIFY(done.wait(5000));
        const auto refused = done.takeFirst().first().value<QslUploadResult>();
        QVERIFY(!refused.ok);
        QVERIFY(!refused.retryLater);
        QCOMPARE(refused.message, QStringLiteral("Club Log REST: HTTP 401: Invalid token [redacted]"));

        server.request.clear(); server.body.clear();
        server.status = 500;
        server.answer = R"({"message":"temporary failure"})";
        auth.password = QStringLiteral("cl_different_test_fixture");
        web.uploadClubLog(auth, "<CALL:5>K1ABC <EOR>", 1);
        QVERIFY(done.wait(5000));
        const auto ambiguous = done.takeFirst().first().value<QslUploadResult>();
        QVERIFY(!ambiguous.ok);
        QVERIFY(!ambiguous.retryLater);
        QVERIFY(ambiguous.requiresReconciliation);
    }

    void clubLogRefusalsAndRateLimitPersistWithoutTheToken()
    {
        const QString token = QStringLiteral("cl_guard_fixture");
        QVERIFY(ClubLogAccessGuard::restriction(token).isEmpty());
        ClubLogAccessGuard::record(token, 429, "300");
        QVERIFY(ClubLogAccessGuard::restriction(token).contains(QStringLiteral("rate limit")));
        QVERIFY(ClubLogAccessGuard::restriction(QStringLiteral("cl_other_fixture")).isEmpty());
        QSettings saved;
        saved.sync();
        for (const QString& key : saved.allKeys()) {
            QVERIFY(!key.contains(token));
            QVERIFY(!saved.value(key).toString().contains(token));
        }
        ClubLogAccessGuard::record(token, 403, {});
        QVERIFY(ClubLogAccessGuard::restriction(token).contains(QStringLiteral("403")));
    }

    void clubLogRestPollsQueuedAndCollectsEveryPage()
    {
        FakeClubLogPoll server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        server.answers = {R"({"status":"queued","records":[]})",
                          R"({"status":"finished","records":[{"index":0,"status":"created","qsoid":101}],"next_cursor":"abc"})",
                          R"({"status":"finished","records":[{"index":1,"status":"duplicate","qsoid":102,"message":"cl_test_fixture","extra":{"value":"cl_test_fixture"}}],"next_cursor":null})"};
        WebQslUploader web;
        web.setClubLogRestBatchBaseUrl(server.url());
        web.setClubLogRestPollIntervalForTest(1);
        QSignalSpy done(&web, &WebQslUploader::finished);
        web.pollClubLogRestBatch(QStringLiteral("9H1SR"), QStringLiteral("cl_test_fixture"), QStringLiteral("123"), 2);
        QVERIFY(done.wait(5000));
        const auto result = done.takeFirst().first().value<QslUploadResult>();
        QVERIFY(!result.ok); // a controller must reconcile individual records first
        QVERIFY(result.requiresReconciliation);
        QCOMPARE(result.remoteId, QStringLiteral("123"));
        QCOMPARE(result.clubLogBatchRecords.size(), 2);
        const QVariantMap last = result.clubLogBatchRecords.last().toMap();
        QCOMPARE(last.value("message").toString(), QStringLiteral("[redacted]"));
        QCOMPARE(last.value("extra").toMap().value("value").toString(), QStringLiteral("[redacted]"));
        QVERIFY(result.message.contains(QStringLiteral("1 created or updated")));
        QVERIFY(result.message.contains(QStringLiteral("1 duplicates")));
        QCOMPARE(server.requests.size(), 3);
        QVERIFY(server.requests.at(0).startsWith("GET /api/logs/9H1SR/uploads/123 "));
        QVERIFY(server.requests.at(2).startsWith("GET /api/logs/9H1SR/uploads/123?cursor=abc "));
        for (const QByteArray& request : server.requests) {
            QVERIFY(hasBearerAuthorization(request, "cl_test_fixture"));
            QVERIFY(!request.left(request.indexOf("\r\n")).contains("cl_test_fixture"));
        }
    }

    void clubLogRestPollingRejectsMissingRecords()
    {
        FakeClubLogPoll server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        server.answers = {R"({"status":"finished","records":[{"status":"created","qsoid":101}]})"};
        WebQslUploader web;
        web.setClubLogRestBatchBaseUrl(server.url());
        QSignalSpy done(&web, &WebQslUploader::finished);
        web.pollClubLogRestBatch(QStringLiteral("9H1SR"), QStringLiteral("cl_test_fixture"), QStringLiteral("123"), 2);
        QVERIFY(done.wait(5000));
        const auto result = done.takeFirst().first().value<QslUploadResult>();
        QVERIFY(!result.ok);
        QVERIFY(result.requiresReconciliation);
        QVERIFY(result.clubLogBatchRecords.isEmpty());
        QCOMPARE(server.requests.size(), 1);
    }

    void clubLogRestStatusReportsNestedServerErrorWithoutToken()
    {
        FakeClubLogPoll server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        server.status = 500;
        server.answers = {R"({"error":{"code":"server_error","message":"Unexpected error cl_fixture"}})"};
        WebQslUploader web;
        web.setClubLogRestBatchBaseUrl(server.url());
        QSignalSpy done(&web, &WebQslUploader::finished);
        web.pollClubLogRestBatch(QStringLiteral("K1TEST"), QStringLiteral("cl_fixture"), QStringLiteral("123"), 1);
        QVERIFY(done.wait(5000));
        const auto result = done.takeFirst().first().value<QslUploadResult>();
        QVERIFY(result.requiresReconciliation);
        QVERIFY(result.message.contains(QStringLiteral("HTTP 500")));
        QVERIFY(result.message.contains(QStringLiteral("Unexpected error [redacted]")));
        QVERIFY(!result.message.contains(QStringLiteral("cl_fixture")));
        QCOMPARE(server.requests.size(), 1);
    }

    void clubLogRestPollingStopsOnRepeatedCursorAndRateLimit()
    {
        FakeClubLogPoll server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        server.answers = {R"({"status":"finished","records":[],"next_cursor":"same"})",
                          R"({"status":"finished","records":[],"next_cursor":"same"})"};
        WebQslUploader web;
        web.setClubLogRestBatchBaseUrl(server.url());
        QSignalSpy done(&web, &WebQslUploader::finished);
        web.pollClubLogRestBatch(QStringLiteral("9H1SR"), QStringLiteral("cl_test_fixture"), QStringLiteral("123"), 1);
        QVERIFY(done.wait(5000));
        auto result = done.takeFirst().first().value<QslUploadResult>();
        QVERIFY(!result.ok);
        QVERIFY(result.requiresReconciliation);
        QCOMPARE(server.requests.size(), 2);
        server.requests.clear();
        server.status = 429;
        server.answers = {R"({"message":"rate limit cl_test_fixture"})"};
        web.pollClubLogRestBatch(QStringLiteral("9H1SR"), QStringLiteral("cl_test_fixture"), QStringLiteral("123"), 1);
        QVERIFY(done.wait(5000));
        result = done.takeFirst().first().value<QslUploadResult>();
        QVERIFY(!result.ok);
        QVERIFY(result.requiresReconciliation);
        QVERIFY(!result.retryLater);
        QVERIFY(!result.message.contains(QStringLiteral("cl_test_fixture")));
        QCOMPARE(server.requests.size(), 1);
        WebQslUploader restarted;
        restarted.setClubLogRestBatchBaseUrl(server.url());
        QSignalSpy blocked(&restarted, &WebQslUploader::finished);
        restarted.pollClubLogRestBatch(QStringLiteral("9H1SR"), QStringLiteral("cl_test_fixture"), QStringLiteral("123"), 1);
        QCOMPARE(blocked.size(), 1);
        QVERIFY(blocked.takeFirst().first().value<QslUploadResult>().requiresReconciliation);
        QCOMPARE(server.requests.size(), 1);
    }

    void clubLogRestBatchQueuesWithoutClaimingSuccess()
    {
        FakeCrx server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        server.status = 202;
        server.answer = R"({"status":"queued","upload_id":12345})";
        WebQslUploader web;
        web.setClubLogRestBatchBaseUrl(server.url());
        QSignalSpy done(&web, &WebQslUploader::finished);
        QString savedId;
        web.submitClubLogRestBatch(QStringLiteral("VP8/G7VJR"), QStringLiteral("cl_test_fixture"),
                                  "<CALL:5>K1ABC <EOR>",
                                  [&savedId](const QString& id) { savedId = id; return true; });
        QVERIFY(done.wait(5000));
        const auto result = done.takeFirst().first().value<QslUploadResult>();
        QVERIFY(!result.ok);
        QVERIFY(result.requiresReconciliation);
        QCOMPARE(savedId, QStringLiteral("12345"));
        QCOMPARE(result.remoteId, savedId);
        QVERIFY(server.request.startsWith("POST /api/logs/VP8/G7VJR/uploads "));
        QVERIFY(hasBearerAuthorization(server.request, "cl_test_fixture"));
        QVERIFY(server.body.contains("name=\"file\""));
        QVERIFY(server.body.contains("filename=\"decodxlog.adi\""));
        QVERIFY(server.body.contains("<CALL:5>K1ABC"));
        QVERIFY(!server.body.contains("name=\"clear\""));
        QVERIFY(!server.request.left(server.request.indexOf("\r\n")).contains("cl_test_fixture"));

        server.request.clear(); server.body.clear();
        server.status = 409;
        server.answer = R"({"message":"identical file already processed"})";
        savedId.clear();
        web.submitClubLogRestBatch(QStringLiteral("9H1SR"), QStringLiteral("cl_test_fixture"),
                                  "<CALL:5>K1ABC <EOR>",
                                  [&savedId](const QString& id) { savedId = id; return true; });
        QVERIFY(done.wait(5000));
        const auto duplicateFile = done.takeFirst().first().value<QslUploadResult>();
        QVERIFY(!duplicateFile.ok);
        QVERIFY(duplicateFile.requiresReconciliation);
        QVERIFY(savedId.isEmpty());

        server.request.clear(); server.body.clear();
        server.status = 202;
        server.answer = R"({"status":"queued","upload_id":987})";
        web.submitClubLogRestBatch(QStringLiteral("9H1SR"), QStringLiteral("cl_test_fixture"),
                                  "<CALL:5>K1ABC <EOR>", [](const QString&) { return false; });
        QVERIFY(done.wait(5000));
        const auto checkpointFailed = done.takeFirst().first().value<QslUploadResult>();
        QVERIFY(!checkpointFailed.ok);
        QVERIFY(checkpointFailed.requiresReconciliation);
        QCOMPARE(checkpointFailed.remoteId, QStringLiteral("987"));
        QVERIFY(checkpointFailed.message.contains(QStringLiteral("could not be saved")));
    }

    void clubLogBatchEnvelopeNeverTreatsQueueAsCompletion()
    {
        using State = ClubLogBatchEnvelope::State;
        auto queued = parseClubLogBatchEnvelope(202, R"({"status":"queued","upload_id":123})", true);
        QCOMPARE(queued.state, State::Queued);
        QCOMPARE(queued.uploadId, QStringLiteral("123"));
        QCOMPARE(parseClubLogBatchEnvelope(202, R"({"status":"queued"})", true).state, State::Invalid);
        QCOMPARE(parseClubLogBatchEnvelope(200, R"({"status":"queued","records":[]})", false).state, State::Queued);
        QCOMPARE(parseClubLogBatchEnvelope(200, R"({"status":"finished"})", false).state, State::Finished);
        QCOMPARE(parseClubLogBatchEnvelope(200, "not-json", false).state, State::Invalid);
        QCOMPARE(parseClubLogBatchEnvelope(409, R"({"message":"already processed"})", true).state, State::Rejected);
        QCOMPARE(parseClubLogBatchEnvelope(429, R"({"message":"rate limit"})", true).state, State::RateLimited);
        const auto nested = parseClubLogBatchEnvelope(500, R"({"error":{"code":"server_error","message":"unexpected error"}})", false);
        QCOMPARE(nested.message, QStringLiteral("unexpected error"));
        QCOMPARE(parseClubLogBatchEnvelope(200, R"({"status":"finished"})", true).state, State::Invalid);
    }

    void clubLogUploadFingerprintIgnoresVolatileAdifHeader()
    {
        const QByteArray first = "<CREATED_TIMESTAMP:15>20261115 120000 <EOH><CALL:5>K1ABC<EOR>";
        const QByteArray later = "<CREATED_TIMESTAMP:15>20261116 140000 <EOH><CALL:5>K1ABC<EOR>";
        const QByteArray changed = "<CREATED_TIMESTAMP:15>20261116 140000 <EOH><CALL:5>K2ABC<EOR>";
        const QByteArray saved = "<CALL:5>K1ABC<CLUBLOG_QSO_UPLOAD_STATUS:1>Y<CLUBLOG_QSO_UPLOAD_DATE:8>20261116<EOR>";
        const QString a = ClubLogUploadJournal::recordsFingerprint(first, 1);
        QVERIFY(!a.isEmpty());
        QCOMPARE(a, ClubLogUploadJournal::recordsFingerprint(later, 1));
        QCOMPARE(a, ClubLogUploadJournal::recordsFingerprint(saved, 1));
        QVERIFY(a != ClubLogUploadJournal::recordsFingerprint(changed, 1));
        QVERIFY(ClubLogUploadJournal::recordsFingerprint(first, 2).isEmpty());
    }

    void clubLogJournalAllowsDifferentContactsWhileProtectingPendingOnes()
    {
        QTemporaryDir directory;
        QSettings settings(directory.filePath("journal.ini"), QSettings::IniFormat);
        ClubLogUploadJournal journal(settings);
        QVERIFY(journal.begin("log", "K1TEST", {"1", "2"}, "hash1"));
        const QString first = journal.nonce();
        QVERIFY(journal.queued("123", first));
        QVERIFY(!journal.begin("log", "K1TEST", {"2", "3"}, "hash2"));
        QVERIFY(journal.begin("log", "K1TEST", {"3"}, "hash2"));
        const QString second = journal.nonce();
        QVERIFY(second != first);
        QCOMPARE(journal.reservedIds("log"), QSet<QString>({"1", "2", "3"}));
        QVERIFY(!journal.begin("log", "K1TEST", {"1"}, "hash3"));
        QVERIFY(journal.clearAfterReconciliation(second));
        QCOMPARE(journal.nonce(), first);
        QCOMPARE(journal.uploadId(), QStringLiteral("123"));
        QCOMPARE(journal.reservedIds("log"), QSet<QString>({"1", "2"}));
        QVERIFY(journal.clearAfterReconciliation(first));
        QVERIFY(!journal.pending());
    }

    void clubLogUploadJournalSurvivesRestart()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("journal.ini"));
        {
            QSettings settings(path, QSettings::IniFormat);
            ClubLogUploadJournal journal(settings);
            QVERIFY(!journal.pending());
            QVERIFY(journal.begin(QStringLiteral("local-log-A"), QStringLiteral("9H1SR"),
                                  {QStringLiteral("11"), QStringLiteral("12")}, QStringLiteral("abc123")));
            QCOMPARE(journal.phase(), QStringLiteral("submitting"));
            QVERIFY(!journal.begin(QStringLiteral("local-log-A"), QStringLiteral("9H1SR"), {QStringLiteral("11")}, QStringLiteral("def")));
            QSettings secondSettings(path, QSettings::IniFormat);
            ClubLogUploadJournal secondInstance(secondSettings);
            QVERIFY(!secondInstance.begin(QStringLiteral("local-log-A"), QStringLiteral("9H1SR"), {QStringLiteral("12")}, QStringLiteral("xyz")));
        }
        {
            QSettings settings(path, QSettings::IniFormat);
            ClubLogUploadJournal journal(settings);
            QVERIFY(journal.pending());
            QCOMPARE(journal.phase(), QStringLiteral("submitting"));
            QCOMPARE(journal.localIds(), QStringList({QStringLiteral("11"), QStringLiteral("12")}));
            QVERIFY(journal.queued(QStringLiteral("123456"), journal.nonce()));
        }
        {
            QSettings settings(path, QSettings::IniFormat);
            ClubLogUploadJournal journal(settings);
            QCOMPARE(journal.phase(), QStringLiteral("queued"));
            QCOMPARE(journal.uploadId(), QStringLiteral("123456"));
            QCOMPARE(journal.station(), QStringLiteral("9H1SR"));
            QCOMPARE(journal.logIdentity(), QStringLiteral("local-log-A"));
            const QString previous = journal.nonce();
            QVERIFY(!journal.queued(QStringLiteral("another"), previous));
            QVERIFY(journal.clearAfterReconciliation(previous));
            QVERIFY(!journal.pending());
            QVERIFY(journal.begin(QStringLiteral("local-log-B"), QStringLiteral("9H1SR"), {QStringLiteral("13")}, QStringLiteral("def")));
            QVERIFY(journal.nonce() != previous);
            QVERIFY(!journal.queued(QStringLiteral("111"), previous));
            QVERIFY(!journal.clearAfterReconciliation(previous));
            bool staleWrite = false;
            QVERIFY(!journal.reconcileAndClear(previous, [&] { staleWrite = true; return true; }));
            QVERIFY(!staleWrite);
            QVERIFY(journal.pending());
            const QString current = journal.nonce();
            QVERIFY(journal.reconcileAndClear(current, [&] {
                QSettings otherSettings(path, QSettings::IniFormat);
                ClubLogUploadJournal other(otherSettings);
                return !other.begin(QStringLiteral("log-C"), QStringLiteral("9H1SR"),
                                    {QStringLiteral("14")}, QStringLiteral("ghi"));
            }));
        }
    }

    void clubLogOlderCheckpointCanStillBeReconciled()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QSettings settings(directory.filePath(QStringLiteral("legacy.ini")), QSettings::IniFormat);
        settings.setValue(QStringLiteral("clubLogRestUpload/phase"), QStringLiteral("queued"));
        settings.setValue(QStringLiteral("clubLogRestUpload/uploadId"), QStringLiteral("123"));
        settings.sync();
        ClubLogUploadJournal journal(settings);
        QVERIFY(journal.nonce().isEmpty());
        QVERIFY(journal.reconcileAndClear(QString(), [] { return true; }));
        QVERIFY(!journal.pending());
    }

    void clubLogNeedsEverything()
    {
        ClubLogAuth auth;
        QVERIFY(!auth.complete());
        auth.email = QStringLiteral("call@example.org");
        auth.password = QStringLiteral("secret");
        auth.callsign = QStringLiteral("IU8LMC");
        QVERIFY(!auth.complete());       // manca la chiave API
        auth.apiKey = QStringLiteral("abc");
        QVERIFY(auth.complete());

        // Senza credenziali complete non parte nessuna richiesta di rete.
        WebQslUploader uploader;
        QslUploadResult got;
        QObject::connect(&uploader, &WebQslUploader::finished, [&got](const QslUploadResult& r) { got = r; });
        uploader.uploadClubLog(ClubLogAuth{}, "<EOR>", 1);
        QVERIFY(!got.ok);
        QVERIFY(!got.retryLater);
        QVERIFY(!uploader.busy());
    }

    void queueAndState()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        const qint64 a = db.insertQso({{"CALL", "K1ABC"}, {"QSO_DATE", "20260101"}, {"TIME_ON", "1200"},
                                       {"BAND", "20m"}, {"MODE", "FT8"}}, "import").id;
        const qint64 b = db.insertQso({{"CALL", "JA1XX"}, {"QSO_DATE", "20260102"}, {"TIME_ON", "1200"},
                                       {"BAND", "40m"}, {"MODE", "FT8"}, {"LOTW_QSL_SENT", "Y"}}, "import").id;
        QVERIFY(a > 0 && b > 0);

        // b e' gia' inviato a LoTW, a no.
        QCOMPARE(db.uploadPendingCount("lotw"), 1);
        QCOMPARE(db.qsosToUpload("lotw"), QList<qint64>({a}));
        QCOMPARE(db.uploadPendingCount("qrz"), 2);

        QslState sent;
        sent.service = "qrz";
        sent.sent = "Y";
        sent.sentDate = "20260917";
        sent.remoteId = "999";
        QVERIFY(db.setQslState(a, sent));
        QCOMPARE(db.uploadPendingCount("qrz"), 1);
        QCOMPARE(db.qsosToUpload("qrz"), QList<qint64>({b}));
        // Lo stato QSL non crea una revisione nuova.
        QCOMPARE(db.meta(a)->revision, 1);
        QVERIFY(db.meta(a)->dirty);
        QCOMPARE(db.record(a)->value("QRZCOM_QSO_UPLOAD_STATUS"), QString("Y"));

        // Un rifiuto resta scritto e il QSO torna in coda.
        QslState failed;
        failed.service = "eqsl";
        failed.sent = "N";
        failed.lastError = "Bad record";
        QVERIFY(db.setQslState(a, failed));
        QCOMPARE(db.uploadPendingCount("eqsl"), 2);
        bool found = false;
        for (const QslState& st : db.qslStatus(a)) {
            if (st.service == "eqsl") {
                QCOMPARE(st.lastError, QString("Bad record"));
                found = true;
            }
        }
        QVERIFY(found);

        // La conferma ricevuta non si perde quando si riscrive l'invio.
        QslState confirmed;
        confirmed.service = "lotw";
        confirmed.sent = "Y";
        confirmed.rcvd = "Y";
        confirmed.rcvdDate = "20260918";
        QVERIFY(db.setQslState(a, confirmed));
        QslState again;
        again.service = "lotw";
        again.sent = "Y";
        again.rcvd = "Y";
        again.rcvdDate = "20260918";
        QVERIFY(db.setQslState(a, again));
        QCOMPARE(db.record(a)->value("LOTW_QSL_RCVD"), QString("Y"));
    }

    void ambiguousConfirmationIsNotAssignedArbitrarily()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        const AdifRecord firstQso{{"CALL", "W1AW"}, {"QSO_DATE", "20260101"}, {"TIME_ON", "1158"},
                                  {"BAND", "20m"}, {"MODE", "CW"}};
        const AdifRecord secondQso{{"CALL", "W1AW"}, {"QSO_DATE", "20260101"}, {"TIME_ON", "1202"},
                                   {"BAND", "20m"}, {"MODE", "CW"}};
        const qint64 first = db.insertQso(firstQso, "import").id;
        const qint64 second = db.insertQso(secondQso, "import").id;
        QVERIFY(first > 0 && second > 0 && first != second);
        const AdifRecord confirmation{{"CALL", "W1AW"}, {"QSO_DATE", "20260101"}, {"TIME_ON", "1200"},
                                      {"BAND", "20m"}, {"MODE", "CW"}};
        const ConfirmationResult result = db.applyConfirmation("lotw", confirmation, 1800);
        QCOMPARE(result.status, ConfirmationResult::Status::Ambiguous);
        QCOMPARE(result.candidates.size(), 2);
        QVERIFY(db.record(first)->value("LOTW_QSL_RCVD").isEmpty());
        QVERIFY(db.record(second)->value("LOTW_QSL_RCVD").isEmpty());
    }

    void uploadQueueSurvivesReopenAndRemainsIdempotent()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = QDir(dir.path()).filePath(QStringLiteral("queue.sqlite"));
        qint64 id = 0;
        {
            LogDatabase db;
            QVERIFY(db.open(path));
            id = db.insertQso({{"CALL", "K1ABC"}, {"QSO_DATE", "20260101"}, {"TIME_ON", "1200"},
                               {"BAND", "20m"}, {"MODE", "FT8"}}, "import").id;
            QVERIFY(id > 0);
            QslState failed;
            failed.service = QStringLiteral("qrz");
            failed.sent = QStringLiteral("N");
            failed.lastError = QStringLiteral("network unavailable");
            QVERIFY(db.setQslState(id, failed));
            db.close();
        }
        {
            LogDatabase db;
            QVERIFY(db.open(path));
            QCOMPARE(db.qsosToUpload(QStringLiteral("qrz")), QList<qint64>{id});
            QCOMPARE(db.qslStatus(id).first().lastError, QStringLiteral("network unavailable"));
            QslState sent;
            sent.service = QStringLiteral("qrz");
            sent.sent = QStringLiteral("Y");
            QVERIFY(db.setQslState(id, sent));
            QCOMPARE(db.qsosToUpload(QStringLiteral("qrz")), QList<qint64>{});
            db.close();
        }
    }

    void receivedQslAttachmentIsDeduplicatedAndLinkedToMultipleQsos()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        const qint64 a = db.insertQso({{"CALL", "W1AW"}, {"QSO_DATE", "20260101"}, {"TIME_ON", "1200"},
                                       {"BAND", "20m"}, {"MODE", "CW"}}, "import").id;
        const qint64 b = db.insertQso({{"CALL", "W1AW"}, {"QSO_DATE", "20260101"}, {"TIME_ON", "1300"},
                                       {"BAND", "20m"}, {"MODE", "CW"}}, "import").id;
        const QByteArray image("fake-qsl-image");
        const qint64 first = db.addQslAttachment(image, "card.jpg", "image/jpeg", {a, b});
        const qint64 second = db.addQslAttachment(image, "renamed.jpg", "image/jpeg", {a});
        QVERIFY(first > 0);
        QCOMPARE(second, first);
        QCOMPARE(db.qslAttachments(a).size(), 1);
        QCOMPARE(db.qslAttachments(b).size(), 1);
        QCOMPARE(db.qslAttachmentData(first), image);
        QCOMPARE(db.record(a)->value("LOTW_QSL_RCVD"), QString());
    }

    void theCertificateIsTheOneOfTheCallsign()
    {
        // TQSL su Windows tiene tutto in %APPDATA%\TrustedQSL: si finge quella
        // cartella e si guarda cosa ci trova DecoDXLog dentro.
        QTemporaryDir home;
        QVERIFY(home.isValid());
        const QByteArray appData = qgetenv("APPDATA");
        qputenv("APPDATA", QFile::encodeName(home.path()));

        const QString data = QDir(home.path()).filePath("TrustedQSL");
        QDir().mkpath(data + "/certs");

        // Appena installato TQSL ha solo le sue radici: non firma niente.
        QFile root(data + "/certs/root");
        QVERIFY(root.open(QIODevice::WriteOnly));
        root.write("radici");
        root.close();
        QFile authorities(data + "/certs/authorities");
        QVERIFY(authorities.open(QIODevice::WriteOnly));
        authorities.write("autorita");
        authorities.close();

#ifdef Q_OS_WIN
        QCOMPARE(qsl::tqslDataDirectory(), data);
        QVERIFY(!qsl::tqslHasCertificate());

        // Col certificato del nominativo (il .tq6 caricato in TQSL) si firma.
        QFile user(data + "/certs/user");
        QVERIFY(user.open(QIODevice::WriteOnly));
        user.write("-----BEGIN CERTIFICATE-----");
        user.close();
        QVERIFY(qsl::tqslHasCertificate());

        // E le station location si leggono da li'.
        QFile stations(data + "/station_data");
        QVERIFY(stations.open(QIODevice::WriteOnly));
        stations.write("<StationDataFile><StationData name=\"Casa\"><CALL>IU8LMC</CALL></StationData></StationDataFile>");
        stations.close();
        QCOMPARE(qsl::tqslStationLocations(), QStringList{QStringLiteral("Casa")});
#endif

        if (appData.isEmpty())
            qunsetenv("APPDATA");
        else
            qputenv("APPDATA", appData);
    }

    void tqslDiscovery()
    {
        // Su questa macchina TQSL c'e' o non c'e': in ogni caso non deve esplodere.
        const QString path = qsl::findTqsl();
        qInfo() << "tqsl:" << path << "certificato:" << qsl::tqslHasCertificate()
                << "station location:" << qsl::tqslStationLocations();
        if (!path.isEmpty())
            QVERIFY(path.contains("tqsl", Qt::CaseInsensitive));
    }
};

QTEST_GUILESS_MAIN(TestQsl)
#include "tst_qsl.moc"
