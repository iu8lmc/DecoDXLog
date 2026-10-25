// Invio QSL: codici d'uscita di TQSL, risposte di QRZ Logbook ed eQSL, coda e
// stato per servizio nel database.
#include "core/Adif.h"
#include "core/LogDatabase.h"
#include "core/QslUpload.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

using namespace decolog::core;

namespace {

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

} // namespace

class TestQsl : public QObject {
    Q_OBJECT

private slots:
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
    }

    void tqslExitCodes()
    {
        auto r = qsl::resultFromTqslExit(0, "Sending to LoTW", 12);
        QVERIFY(r.ok);
        QCOMPARE(r.accepted, 12);

        r = qsl::resultFromTqslExit(7, "All QSOs were duplicates", 5);
        QVERIFY(r.ok);
        QCOMPARE(r.duplicates, 5);
        QCOMPARE(r.accepted, 0);

        r = qsl::resultFromTqslExit(8, "Some QSOs were duplicates", 5);
        QVERIFY(r.ok);
        QCOMPARE(r.accepted, 5);

        r = qsl::resultFromTqslExit(2, "rejected", 3);
        QVERIFY(!r.ok);
        QCOMPARE(r.rejected, 3);
        QVERIFY(!r.retryLater);

        r = qsl::resultFromTqslExit(10, "connection failed", 3);
        QVERIFY(!r.ok);
        QVERIFY(r.retryLater);

        r = qsl::resultFromTqslExit(5, "no station location", 3);
        QVERIFY(!r.ok);
        QVERIFY(r.message.contains("station location"));
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
