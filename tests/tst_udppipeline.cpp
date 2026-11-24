// Dal pacchetto UDP al QSO nel log, con il programma vero.
//
// tst_protocol prova il protocollo pezzo per pezzo. Qui si avvia DecoDXLog
// com'e' — senza finestra (offscreen), su un log e impostazioni di prova — gli
// si mandano i datagrammi che mandano Decodium, WSJT-X e JTDX, e si guarda nel
// database cosa e' finito nel log: una volta sola, con la fonte giusta, con il
// DXCC aggiunto dal cty.csv. Il programma racconta cosa fa sul registro
// attivita', che con DECODXLOG_TRACE_ACTIVITY=1 esce anche su stderr.
#include "core/WsjtxProtocol.h"
#include "core/LogDatabase.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QLockFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QSettings>
#include <QTimeZone>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QUdpSocket>

#include <functional>

using namespace decolog::core;

namespace {

wsjtx::QsoLogged qsoFor(const QString& call, const QString& grid, const QString& mode, const QTime& at)
{
    wsjtx::QsoLogged q;
    q.timeOn = QDateTime(QDate(2026, 9, 17), at, QTimeZone::UTC);
    q.timeOff = q.timeOn.addSecs(45);
    q.dxCall = call;
    q.dxGrid = grid;
    q.txFrequencyHz = 14084000;
    q.mode = mode;
    q.reportSent = QStringLiteral("-10");
    q.reportReceived = QStringLiteral("-05");
    q.myCall = QStringLiteral("IU8LMC");
    q.myGrid = QStringLiteral("JN70");
    return q;
}

// Un QSO come lo scrive Decodium nel suo decodium_log.adi, finito a `off`.
QByteArray decodiumRecord(const QString& call, const QDateTime& off)
{
    const QDateTime on = off.addSecs(-30);
    const auto field = [](const char* name, const QString& value) {
        return QStringLiteral("<%1:%2>%3 ").arg(QLatin1String(name)).arg(value.size()).arg(value);
    };
    return (field("CALL", call) + field("GRIDSQUARE", QStringLiteral("JN11")) + field("MODE", QStringLiteral("MFSK"))
            + field("SUBMODE", QStringLiteral("FT2")) + field("RST_SENT", QStringLiteral("+00"))
            + field("RST_RCVD", QStringLiteral("-12")) + field("QSO_DATE", on.toString(QStringLiteral("yyyyMMdd")))
            + field("TIME_ON", on.toString(QStringLiteral("HHmmss")))
            + field("QSO_DATE_OFF", off.toString(QStringLiteral("yyyyMMdd")))
            + field("TIME_OFF", off.toString(QStringLiteral("HHmmss"))) + field("BAND", QStringLiteral("20M"))
            + field("FREQ", QStringLiteral("14.084000")) + field("STATION_CALLSIGN", QStringLiteral("IU8LMC"))
            + QStringLiteral("<EOR>\n"))
        .toUtf8();
}

QByteArray adifFor(const QString& call, const QString& grid, const QString& time, const char* program,
                   const QString& stationCall = QStringLiteral("IU8LMC"))
{
    const auto field = [](const char* name, const QString& value) {
        return QStringLiteral("<%1:%2>%3 ").arg(QLatin1String(name)).arg(value.size()).arg(value);
    };
    QString record = field("call", call) + field("gridsquare", grid) + field("mode", QStringLiteral("MFSK"))
                     + field("submode", QStringLiteral("FT2")) + field("rst_sent", QStringLiteral("-10"))
                     + field("rst_rcvd", QStringLiteral("-05")) + field("qso_date", QStringLiteral("20260917"))
                     + field("time_on", time) + field("band", QStringLiteral("20m"))
                     + field("freq", QStringLiteral("14.084000")) + field("station_callsign", stationCall)
                     + QStringLiteral("<EOR>");
    const QString header = QStringLiteral("\n<adif_ver:5>3.1.0\n<programid:%1>%2\n<EOH>\n")
                               .arg(qstrlen(program))
                               .arg(QLatin1String(program));
    return (header + record).toUtf8();
}

} // namespace

class TestUdpPipeline : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    quint16 m_apiPort{0};
    QProcess m_app;
    QByteArray m_output;
    quint16 m_port{0};
    QString m_db;

    void send(const QByteArray& datagram)
    {
        QUdpSocket s;
        QCOMPARE(s.writeDatagram(datagram, QHostAddress::LocalHost, m_port), qint64(datagram.size()));
    }

    // Una connessione per ogni domanda, in sola lettura: il log e' del programma.
    QVariant scalar(const QString& sql)
    {
        QVariant out;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("probe"));
            db.setDatabaseName(m_db);
            db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=2000"));
            if (db.open()) {
                QSqlQuery q(db);
                if (q.exec(sql) && q.next())
                    out = q.value(0);
            }
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("probe"));
        return out;
    }

    int countCall(const QString& call)
    {
        return scalar(QStringLiteral("SELECT COUNT(*) FROM qso WHERE deleted = 0 AND call = '%1'").arg(call)).toInt();
    }

    QString field(const QString& call, const char* column)
    {
        return scalar(QStringLiteral("SELECT IFNULL(%1, '') FROM qso WHERE deleted = 0 AND call = '%2'")
                          .arg(QLatin1String(column), call))
            .toString();
    }

    qint64 profileForCall(const QString& call)
    {
        return scalar(QStringLiteral("SELECT IFNULL(station_profile_id, 0) FROM qso WHERE deleted = 0 AND call = '%1'")
                          .arg(call))
            .toLongLong();
    }

    bool waitFor(const std::function<bool()>& condition, int ms)
    {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            if (condition())
                return true;
            QTest::qWait(100);
        }
        return condition();
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_db = m_dir.filePath(QStringLiteral("log.sqlite"));

        // Due profili reali di prova: i QSO che arriveranno via UDP devono
        // essere verificati anche nel database dell'app, non solo nel resolver.
        {
            LogDatabase seed;
            QVERIFY(seed.open(m_db));
            StationProfile gbc;
            gbc.name = QStringLiteral("YL3GBC");
            gbc.stationCallsign = QStringLiteral("YL3GBC");
            gbc.operatorCall = QStringLiteral("YL3GBC");
            gbc.isDefault = true;
            QVERIFY(seed.saveStationProfile(gbc) > 0);
            StationProfile aug;
            aug.name = QStringLiteral("YL3AUG");
            aug.stationCallsign = QStringLiteral("YL3AUG");
            aug.operatorCall = QStringLiteral("YL3AUG");
            QVERIFY(seed.saveStationProfile(aug) > 0);
            StationProfile legacy;
            legacy.name = QStringLiteral("Legacy");
            legacy.stationCallsign = QStringLiteral("IU8LMC");
            legacy.operatorCall = QStringLiteral("IU8LMC");
            QVERIFY(seed.saveStationProfile(legacy) > 0);
            if (qEnvironmentVariableIntValue("DECODXLOG_TEST_UDP_LARGE_LOG") != 0) {
                QSqlQuery fixture(seed.connection());
                QVERIFY(fixture.exec(QStringLiteral(
                    "WITH RECURSIVE n(i) AS (VALUES(1) UNION ALL SELECT i+1 FROM n WHERE i<56387) "
                    "INSERT INTO qso (uuid, call, band, mode, qso_datetime_on, created_at, updated_at, source) "
                    "SELECT 'synthetic-'||i, 'K'||i||'ABC', '20m', 'FT8', '2026-11-20T19:00:00Z', "
                    "'2026-11-20T19:00:00Z', '2026-11-20T19:00:00Z', 'test' FROM n")));
                QCOMPARE(seed.qsoCount(), 56387);
            }
            seed.close();
        }

        // Una porta libera, presa e lasciata subito.
        {
            QUdpSocket probe;
            QVERIFY(probe.bind(QHostAddress::LocalHost, 0));
            m_port = probe.localPort();
        }
        // Impostazioni di prova: niente backup nella cartella vera.
        QVERIFY(QDir().mkpath(m_dir.filePath(QStringLiteral("Decodium"))));
        QFile ini(m_dir.filePath(QStringLiteral("Decodium/DecoDXLog.ini")));
        QVERIFY(ini.open(QIODevice::WriteOnly));
        ini.write("[backup]\nenabled=false\ndir=" + m_dir.filePath(QStringLiteral("backup")).toUtf8() + "\n");
        ini.close();

        // Il log di Decodium, dove Decodium dice che sta: un QSO fatto mezz'ora
        // fa e mai arrivato via UDP, uno di un minuto fa (sta ancora arrivando)
        // e uno di otto giorni fa (troppo vecchio per il primo controllo).
        const QString decodiumLog = m_dir.filePath(QStringLiteral("decodium_log.adi"));
        QFile adi(decodiumLog);
        QVERIFY(adi.open(QIODevice::WriteOnly));
        const QDateTime now = QDateTime::currentDateTimeUtc();
        adi.write("Decodium3 ADIF Log\n<EOH>\n");
        adi.write(decodiumRecord(QStringLiteral("SP9OLD"), now.addDays(-8)));
        adi.write(decodiumRecord(QStringLiteral("EA3RR"), now.addSecs(-1800)));
        adi.write(decodiumRecord(QStringLiteral("OK1TOO"), now.addSecs(-60)));
        adi.close();
        QFile decodiumIni(m_dir.filePath(QStringLiteral("Decodium/Decodium3.ini")));
        QVERIFY(decodiumIni.open(QIODevice::WriteOnly));
        decodiumIni.write("[Logbooks]\nActivePath=" + QDir::fromNativeSeparators(decodiumLog).toUtf8() + "\n");
        decodiumIni.close();

        // Un altro log ADIF tenuto d'occhio, come quello di fldigi: un QSO di
        // cinque minuti fa che DecoDXLog non ha.
        const QString fldigiLog = m_dir.filePath(QStringLiteral("logbook.adif"));
        QFile fldigi(fldigiLog);
        QVERIFY(fldigi.open(QIODevice::WriteOnly));
        fldigi.write("fldigi ADIF\n<EOH>\n");
        fldigi.write(decodiumRecord(QStringLiteral("HB9FLD"), now.addSecs(-300)));
        fldigi.close();
        {
            QSettings ini(m_dir.filePath(QStringLiteral("Decodium/DecoDXLog.ini")), QSettings::IniFormat);
            // L'interfaccia HTTP locale, con la sua chiave.
            QTcpServer probe;
            probe.listen(QHostAddress::LocalHost, 0);
            m_apiPort = probe.serverPort();
            probe.close();
            ini.setValue(QStringLiteral("api/port"), m_apiPort);
            ini.setValue(QStringLiteral("api/token"), QStringLiteral("0123456789abcdef0123456789abcdef"));
            ini.setValue(QStringLiteral("api/readOnlyToken"), QStringLiteral("fedcba9876543210fedcba9876543210"));
            ini.setValue(QStringLiteral("api/extensionCredentials"), QStringLiteral(R"([{"id":"test.report","token":"11111111111111111111111111111111","permissions":["qso.read"]}])"));
            ini.setValue(QStringLiteral("watch/files"),
                         QVariantList{QVariantMap{{QStringLiteral("path"), QDir::fromNativeSeparators(fldigiLog)},
                                                  {QStringLiteral("label"), QStringLiteral("fldigi")},
                                                  {QStringLiteral("enabled"), true}}});
        }

        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        env.insert(QStringLiteral("DECODXLOG_DATA_ROOT"), m_dir.filePath(QStringLiteral("app-data")));
        env.insert(QStringLiteral("DECODXLOG_TRACE_ACTIVITY"), QStringLiteral("1"));
        // --grab chiude il programma da solo anche se la prova si pianta.
        env.insert(QStringLiteral("DECODXLOG_GRAB_MS"), QStringLiteral("240000"));
        m_app.setProcessEnvironment(env);
        m_app.setProcessChannelMode(QProcess::MergedChannels);
        connect(&m_app, &QProcess::readyRead, this, [this] { m_output += m_app.readAll(); });
        m_app.start(QStringLiteral(DECODXLOG_APP),
                    {QStringLiteral("--settings"), m_dir.path(), QStringLiteral("--db"), m_db,
                     QStringLiteral("--port"), QString::number(m_port), QStringLiteral("--grab"),
                     m_dir.filePath(QStringLiteral("shot.png"))});
        QVERIFY2(m_app.waitForStarted(20000), qPrintable(m_app.errorString()));

        // Pronto quando dice di ascoltare su quella porta.
        const QRegularExpression listening(QStringLiteral("\\[activity\\] UDP\\|info\\|[^\\n]*\\b%1\\b").arg(m_port));
        const bool ready = waitFor([&] { return listening.match(QString::fromUtf8(m_output)).hasMatch(); }, 90000);
        QVERIFY2(ready, m_output.right(2000).constData());
    }

    void cleanupTestCase()
    {
        if (m_app.state() != QProcess::NotRunning) {
            m_app.kill();
            m_app.waitForFinished(10000);
        }
    }

    void decodiumSendsTwoMessagesAndOneQsoIsLogged()
    {
        // Decodium: prima QSOLogged, poi LoggedADIF dello stesso collegamento.
        send(wsjtx::buildQsoLogged(QStringLiteral("Decodium"), qsoFor("DL1AB", "JO62", "FT2", QTime(10, 15))));
        send(wsjtx::buildLoggedAdif(QStringLiteral("Decodium"),
                                    adifFor(QStringLiteral("DL1AB"), QStringLiteral("JO62"), QStringLiteral("101500"),
                                            "Decodium FT2 1.0.637")));
        QVERIFY(waitFor([&] { return countCall(QStringLiteral("DL1AB")) > 0; }, 15000));
        // Passata la finestra dell'abbinamento, e' ancora uno solo.
        QTest::qWait(2500);
        QCOMPARE(countCall(QStringLiteral("DL1AB")), 1);
        QCOMPARE(field(QStringLiteral("DL1AB"), "source"), QStringLiteral("udp_decodium"));
        QCOMPARE(field(QStringLiteral("DL1AB"), "submode"), QStringLiteral("FT2"));
        QCOMPARE(field(QStringLiteral("DL1AB"), "gridsquare"), QStringLiteral("JO62"));
        QCOMPARE(field(QStringLiteral("DL1AB"), "band"), QStringLiteral("20m"));
        // Decodium non manda il DXCC: ce lo mette il cty.csv (Germania, 230).
        QCOMPARE(field(QStringLiteral("DL1AB"), "dxcc"), QStringLiteral("230"));
        QVERIFY(field(QStringLiteral("DL1AB"), "source_app").contains(QStringLiteral("Decodium")));
        // E il programma lo racconta come salvato: e' passato da tutta la catena.
        QVERIFY(waitFor([&] { return m_output.contains("[activity] UDP|") && m_output.contains("DL1AB"); }, 5000));
    }

    void jtdxSendsOnlyQsoLogged()
    {
        // JTDX manda solo QSOLogged: si usa dopo la finestra dell'abbinamento.
        send(wsjtx::buildQsoLogged(QStringLiteral("JTDX"), qsoFor("OH2XX", "KP20", "FT8", QTime(10, 20))));
        QVERIFY(waitFor([&] { return countCall(QStringLiteral("OH2XX")) == 1; }, 15000));
        QCOMPARE(field(QStringLiteral("OH2XX"), "source"), QStringLiteral("udp_wsjtx"));
        QCOMPARE(field(QStringLiteral("OH2XX"), "mode"), QStringLiteral("FT8"));
        QCOMPARE(field(QStringLiteral("OH2XX"), "dxcc"), QStringLiteral("224"));
    }

    void wsjtxSendsTheAdif()
    {
        send(wsjtx::buildLoggedAdif(QStringLiteral("WSJT-X"),
                                    adifFor(QStringLiteral("K1ABC"), QStringLiteral("FN42"), QStringLiteral("102500"),
                                            "WSJT-X")));
        QVERIFY(waitFor([&] { return countCall(QStringLiteral("K1ABC")) == 1; }, 15000));
        QCOMPARE(field(QStringLiteral("K1ABC"), "source"), QStringLiteral("udp_wsjtx"));
        QCOMPARE(field(QStringLiteral("K1ABC"), "dxcc"), QStringLiteral("291"));
    }

    void wsjtxResendingAnFt8QsoIsOneQso()
    {
        // FT8 non ha sottomodo: fino alla 1.16.30 il doppione di un QSO senza
        // sottomodo non si riconosceva, e WSJT-X che rimanda lo scriveva due volte.
        QByteArray ft8 = adifFor(QStringLiteral("W2FT8"), QStringLiteral("FN20"), QStringLiteral("104000"), "WSJT-X");
        ft8.replace("<mode:4>MFSK <submode:3>FT2 ", "<mode:3>FT8 ");
        send(wsjtx::buildLoggedAdif(QStringLiteral("WSJT-X"), ft8));
        QVERIFY(waitFor([&] { return countCall(QStringLiteral("W2FT8")) == 1; }, 15000));
        QCOMPARE(field(QStringLiteral("W2FT8"), "mode"), QStringLiteral("FT8"));
        send(wsjtx::buildLoggedAdif(QStringLiteral("WSJT-X"), ft8));
        QTest::qWait(2500);
        QCOMPARE(countCall(QStringLiteral("W2FT8")), 1);
    }

    void theSameQsoSentTwiceIsOneQso()
    {
        // Un client che rimanda lo stesso ADIF (un clic doppio, una rete che
        // ripete): nel log resta uno.
        const QByteArray again = wsjtx::buildLoggedAdif(
            QStringLiteral("Decodium"),
            adifFor(QStringLiteral("DL1AB"), QStringLiteral("JO62"), QStringLiteral("101500"), "Decodium FT2 1.0.637"));
        send(again);
        send(again);
        QTest::qWait(2500);
        QCOMPARE(countCall(QStringLiteral("DL1AB")), 1);
    }

    void twoClientsRouteQsosToSeparateProfiles()
    {
        // A/B reale sul processo: due client UDP diversi, due identita' di
        // stazione, due righe con station_profile_id differenti.
        send(wsjtx::buildLoggedAdif(QStringLiteral("Decodium-YL3GBC"),
                                    adifFor(QStringLiteral("K1GBC"), QStringLiteral("FN31"), QStringLiteral("110000"),
                                            "Decodium", QStringLiteral("YL3GBC"))));
        send(wsjtx::buildLoggedAdif(QStringLiteral("Decodium-YL3AUG"),
                                    adifFor(QStringLiteral("K1AUG"), QStringLiteral("FN32"), QStringLiteral("110100"),
                                            "Decodium", QStringLiteral("YL3AUG"))));
        QVERIFY(waitFor([&] { return countCall(QStringLiteral("K1GBC")) == 1
                                   && countCall(QStringLiteral("K1AUG")) == 1; }, 15000));
        const qint64 gbcId = scalar(QStringLiteral("SELECT id FROM station_profile WHERE station_callsign = 'YL3GBC'")).toLongLong();
        const qint64 augId = scalar(QStringLiteral("SELECT id FROM station_profile WHERE station_callsign = 'YL3AUG'")).toLongLong();
        QVERIFY(gbcId > 0 && augId > 0 && gbcId != augId);
        QCOMPARE(profileForCall(QStringLiteral("K1GBC")), gbcId);
        QCOMPARE(profileForCall(QStringLiteral("K1AUG")), augId);
    }

    void heartbeatAndStatusDoNotLog()
    {
        const int before = scalar(QStringLiteral("SELECT COUNT(*) FROM qso")).toInt();
        wsjtx::Heartbeat hb;
        hb.version = QStringLiteral("4.0");
        send(wsjtx::buildHeartbeat(QStringLiteral("Decodium"), hb));
        wsjtx::Status st;
        st.dialFrequencyHz = 14074000;
        st.mode = QStringLiteral("FT8");
        st.dxCall = QStringLiteral("JA1ZZZ");
        send(wsjtx::buildStatus(QStringLiteral("Decodium"), st));
        QTest::qWait(1500);
        QCOMPARE(scalar(QStringLiteral("SELECT COUNT(*) FROM qso")).toInt(), before);
    }

    void rubbishDoesNotStopTheLog()
    {
        // Byte a caso, un datagramma troncato, uno con la firma sbagliata.
        send(QByteArray("hello, this is not WSJT-X"));
        QByteArray valid = wsjtx::buildLoggedAdif(
            QStringLiteral("Decodium"),
            adifFor(QStringLiteral("VK2ABC"), QStringLiteral("QF56"), QStringLiteral("103000"), "Decodium"));
        send(valid.left(valid.size() / 2));
        QByteArray wrongMagic = valid;
        wrongMagic[0] = char(0x00);
        send(wrongMagic);
        QTest::qWait(500);
        QCOMPARE(m_app.state(), QProcess::Running);
        QCOMPARE(countCall(QStringLiteral("VK2ABC")), 0);
        // E il QSO buono, dopo, entra.
        send(valid);
        QVERIFY(waitFor([&] { return countCall(QStringLiteral("VK2ABC")) == 1; }, 15000));
    }

    void aQsoLeftInTheDecodiumLogIsRecovered()
    {
        // Il primo controllo e' venti secondi dopo l'avvio.
        QVERIFY2(waitFor([&] { return countCall(QStringLiteral("EA3RR")) > 0; }, 60000),
                 m_output.right(2000).constData());
        QCOMPARE(countCall(QStringLiteral("EA3RR")), 1);
        QCOMPARE(field(QStringLiteral("EA3RR"), "source"), QStringLiteral("decodium_adif"));
        QCOMPARE(field(QStringLiteral("EA3RR"), "submode"), QStringLiteral("FT2"));
        // Spagna dal cty.csv, come per quelli arrivati via UDP.
        QCOMPARE(field(QStringLiteral("EA3RR"), "dxcc"), QStringLiteral("281"));
        QVERIFY(waitFor([&] { return m_output.contains("[activity] DECODIUM|") && m_output.contains("EA3RR"); }, 5000));
        QCOMPARE(countCall(QStringLiteral("OK1TOO")), 0);
        QCOMPARE(countCall(QStringLiteral("SP9OLD")), 0);

        // E dal log di fldigi, con il suo nome.
        QVERIFY2(waitFor([&] { return countCall(QStringLiteral("HB9FLD")) > 0; }, 30000),
                 m_output.right(2000).constData());
        QCOMPARE(field(QStringLiteral("HB9FLD"), "source"), QStringLiteral("adif_watch"));
        QCOMPARE(field(QStringLiteral("HB9FLD"), "source_app"), QStringLiteral("fldigi"));
    }

    void theLocalInterfaceLogsAndAnswers()
    {
        QNetworkAccessManager net;
        auto request = [&](const QByteArray& method, const QString& path, const QByteArray& body = {},
                           const QByteArray& idempotencyKey = QByteArray(),
                           const QByteArray& contentType = "text/plain", const QByteArray& app = "tester") {
            QNetworkRequest r(QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(m_apiPort).arg(path)));
            r.setRawHeader("X-DecoDXLog-Token", "0123456789abcdef0123456789abcdef");
            r.setRawHeader("X-App", app);
            if (!idempotencyKey.isEmpty())
                r.setRawHeader("Idempotency-Key", idempotencyKey);
            r.setRawHeader("Content-Type", contentType);
            QNetworkReply* reply = net.sendCustomRequest(r, method, body);
            QSignalSpy done(reply, &QNetworkReply::finished);
            done.wait(5000);
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();
            reply->deleteLater();
            return std::pair{status, json};
        };
        // Un QSO scritto da un altro programma.
        const auto [posted, result] = request("POST", QStringLiteral("/api/v1/qso"),
                                              "<CALL:5>VK2AB<QSO_DATE:8>20260917<TIME_ON:4>0900<BAND:3>20m"
                                              "<MODE:3>FT8<RST_SENT:3>-10<RST_RCVD:3>-12<EOR>");
        QCOMPARE(posted, 201);
        QCOMPARE(result.value("results").toArray().at(0).toObject().value("status").toString(), QString("logged"));
        QCOMPARE(countCall(QStringLiteral("VK2AB")), 1);
        QCOMPARE(field(QStringLiteral("VK2AB"), "source"), QStringLiteral("api"));
        QCOMPARE(field(QStringLiteral("VK2AB"), "source_app"), QStringLiteral("tester"));
        // Lo stesso di nuovo: doppione.
        QCOMPARE(request("POST", QStringLiteral("/api/v1/qso"),
                         "<CALL:5>VK2AB<QSO_DATE:8>20260917<TIME_ON:4>0900<BAND:3>20m<MODE:3>FT8<EOR>").first, 409);
        // Un retry con chiave idempotente restituisce la stessa risposta senza
        // creare un secondo QSO; riusare la chiave con un corpo diverso e'
        // invece un conflitto esplicito.
        const QByteArray retryBody =
            "<CALL:5>W8IDM<QSO_DATE:8>20260917<TIME_ON:4>0910<BAND:3>20m<MODE:3>FT8<EOR>";
        const auto firstRetry = request("POST", QStringLiteral("/api/v1/qso"), retryBody, "retry-1");
        // Il registro idempotente deve sopravvivere alla chiusura del processo,
        // non soltanto restare nella memoria del controller.
        m_app.terminate();
        QVERIFY(m_app.waitForFinished(10000));
        m_output.clear();
        m_app.start(QStringLiteral(DECODXLOG_APP),
                    {QStringLiteral("--settings"), m_dir.path(), QStringLiteral("--db"), m_db,
                     QStringLiteral("--port"), QString::number(m_port), QStringLiteral("--grab"),
                     m_dir.filePath(QStringLiteral("shot-restart.png"))});
        QVERIFY2(m_app.waitForStarted(20000), qPrintable(m_app.errorString()));
        const QRegularExpression restartedListening(
            QStringLiteral("\\[activity\\] UDP\\|info\\|[^\\n]*\\b%1\\b").arg(m_port));
        QVERIFY2(waitFor([&] { return restartedListening.match(QString::fromUtf8(m_output)).hasMatch(); }, 90000),
                 m_output.right(2000).constData());
        const auto secondRetry = request("POST", QStringLiteral("/api/v1/qso"), retryBody, "retry-1");
        QCOMPARE(request("POST", QStringLiteral("/api/v1/qso"), retryBody, "retry-1", "application/json").first, 409);
        QCOMPARE(request("POST", QStringLiteral("/api/v1/qso"), retryBody, "retry-1", "text/plain", "other-app").first, 409);
        QCOMPARE(firstRetry.first, 201);
        QCOMPARE(secondRetry.first, 201);
        QCOMPARE(secondRetry.second, firstRetry.second);
        QCOMPARE(countCall(QStringLiteral("W8IDM")), 1);
        const QByteArray lockedKey="locked-1";
        const QString lockPath=QFileInfo(m_db).canonicalFilePath()+QStringLiteral(".api-idempotency-")
            +QString::fromLatin1(QCryptographicHash::hash(lockedKey,QCryptographicHash::Sha256).toHex())+QStringLiteral(".lock");
        QLockFile held(lockPath); QVERIFY(held.tryLock(0));
        const QByteArray lockedBody="<CALL:6>W8LOCK<QSO_DATE:8>20260917<TIME_ON:4>0915<BAND:3>20m<MODE:3>FT8<EOR>";
        const auto heldResult=request("POST",QStringLiteral("/api/v1/qso"),lockedBody,lockedKey);
        QCOMPARE(heldResult.first,503); QCOMPARE(heldResult.second.value("retrySafe").toBool(true),false);
        QCOMPARE(countCall(QStringLiteral("W8LOCK")),0);
        held.unlock(); QCOMPARE(request("POST",QStringLiteral("/api/v1/qso"),lockedBody,lockedKey).first,201);
        QCOMPARE(countCall(QStringLiteral("W8LOCK")),1);
        const auto inspected = request("GET", QStringLiteral("/api/v1/idempotency"), {}, "retry-1");
        QCOMPARE(inspected.first,200);
        QCOMPARE(inspected.second.value("state").toString(),QStringLiteral("completed"));
        QCOMPARE(inspected.second.value("responseStatus").toInt(),201);
        QVERIFY(!inspected.second.contains("requestBody")); QVERIFY(!inspected.second.contains("responseBody"));
        QCOMPARE(request("GET",QStringLiteral("/api/v1/idempotency"),{},"not-found").first,404);
        QCOMPARE(request("POST", QStringLiteral("/api/v1/qso"),
                         "<CALL:5>W8DIFF<QSO_DATE:8>20260917<TIME_ON:4>0911<BAND:3>20m<MODE:3>FT8<EOR>",
                         "retry-1").first, 409);
        // Simulate a journal write failure after a successful QSO insert.
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("fault"));
            db.setDatabaseName(m_db);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral("CREATE TRIGGER api_fault BEFORE UPDATE ON api_idempotency BEGIN SELECT RAISE(FAIL, 'injected fault'); END")));
        }
        QSqlDatabase::removeDatabase(QStringLiteral("fault"));
        const QByteArray faultBody = "<CALL:5>W8ERR<QSO_DATE:8>20260917<TIME_ON:4>0920<BAND:3>20m<MODE:3>FT8<EOR>";
        const auto failedJournal = request("POST", QStringLiteral("/api/v1/qso"), faultBody, "fault-1");
        QCOMPARE(failedJournal.first, 503);
        QVERIFY(failedJournal.second.value(QStringLiteral("error")).toString().contains(QStringLiteral("may have been saved")));
        QCOMPARE(failedJournal.second.value(QStringLiteral("results")).toArray().at(0).toObject().value(QStringLiteral("status")).toString(), QStringLiteral("logged"));
        QCOMPARE(countCall(QStringLiteral("W8ERR")), 1);
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("fault"));
            db.setDatabaseName(m_db); QVERIFY(db.open()); QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral("DROP TRIGGER api_fault")));
        }
        QSqlDatabase::removeDatabase(QStringLiteral("fault"));
        m_app.terminate(); QVERIFY(m_app.waitForFinished(10000)); m_output.clear();
        m_app.start(QStringLiteral(DECODXLOG_APP),
                    {QStringLiteral("--settings"), m_dir.path(), QStringLiteral("--db"), m_db,
                     QStringLiteral("--port"), QString::number(m_port), QStringLiteral("--grab"),
                     m_dir.filePath(QStringLiteral("shot-pending-restart.png"))});
        QVERIFY2(m_app.waitForStarted(20000), qPrintable(m_app.errorString()));
        QVERIFY2(waitFor([&] { return restartedListening.match(QString::fromUtf8(m_output)).hasMatch(); }, 90000),
                 m_output.right(2000).constData());
        const auto retryAfterFault = request("POST", QStringLiteral("/api/v1/qso"), faultBody, "fault-1");
        QCOMPARE(retryAfterFault.first, 503);
        QCOMPARE(retryAfterFault.second.value(QStringLiteral("retrySafe")).toBool(true), false);
        const auto unresolved=request("GET",QStringLiteral("/api/v1/idempotency"),{},"fault-1");
        QCOMPARE(unresolved.first,200); QCOMPARE(unresolved.second.value("state").toString(),QStringLiteral("unresolved"));
        QCOMPARE(unresolved.second.value("retrySafe").toBool(true),false);
        QVERIFY(!unresolved.second.contains("responseBody"));
        QCOMPARE(countCall(QStringLiteral("W8ERR")), 1);
        QCOMPARE(request("POST",QStringLiteral("/api/v1/qso"),faultBody,"fault-1","text/plain","other-app").first,409);
        {
            LogDatabase reviewDb; QVERIFY(reviewDb.open(m_db));
            const auto rows=reviewDb.unresolvedApiRequests().value("rows").toList(); QCOMPARE(rows.size(),1);
            const auto row=rows.first().toMap(); QString reviewError;
            QVERIFY2(reviewDb.markApiRequestReviewed(row.value("rowId").toString(),row.value("reviewToken").toString(),&reviewError),qPrintable(reviewError));
            QSqlQuery age(reviewDb.connection());
            QVERIFY(age.exec(QStringLiteral("UPDATE api_idempotency SET created_at='2020-01-01T00:00:00Z' WHERE key='fault-1'")));
        }
        const auto reviewed=request("GET",QStringLiteral("/api/v1/idempotency"),{},"fault-1");
        QCOMPARE(reviewed.first,200); QCOMPARE(reviewed.second.value("state").toString(),QStringLiteral("reviewed-unknown"));
        QCOMPARE(reviewed.second.value("retrySafe").toBool(true),false);
        const auto afterReview=request("POST",QStringLiteral("/api/v1/qso"),faultBody,"fault-1");
        QCOMPARE(afterReview.first,409); QCOMPARE(afterReview.second.value("retrySafe").toBool(true),false);
        const auto changedAfterReview=request("POST",QStringLiteral("/api/v1/qso"),faultBody+" ","fault-1");
        QCOMPARE(changedAfterReview.first,409);
        QVERIFY(changedAfterReview.second.value("error").toString().contains("different request"));
        QCOMPARE(countCall(QStringLiteral("W8ERR")),1);
        // A reservation failure must occur before any QSO is processed.
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("fault"));
            db.setDatabaseName(m_db); QVERIFY(db.open()); QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral("CREATE TRIGGER api_claim_fault BEFORE INSERT ON api_idempotency BEGIN SELECT RAISE(FAIL, 'injected fault'); END")));
        }
        QSqlDatabase::removeDatabase(QStringLiteral("fault"));
        const QByteArray claimBody = "<CALL:7>W8CLAIM<QSO_DATE:8>20260917<TIME_ON:4>0925<BAND:3>20m<MODE:3>FT8<EOR>";
        QCOMPARE(request("POST",QStringLiteral("/api/v1/qso"),claimBody,"claim-fault").first,503);
        QCOMPARE(countCall(QStringLiteral("W8CLAIM")),0);
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("fault"));
            db.setDatabaseName(m_db); QVERIFY(db.open()); QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral("DROP TRIGGER api_claim_fault")));
        }
        QSqlDatabase::removeDatabase(QStringLiteral("fault"));
        QCOMPARE(request("POST",QStringLiteral("/api/v1/qso"),claimBody,"claim-fault").first,201);
        QCOMPARE(countCall(QStringLiteral("W8CLAIM")),1);
        // A failed lookup must not be treated as an unused key.
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("fault"));
            db.setDatabaseName(m_db); QVERIFY(db.open()); QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral("ALTER TABLE api_idempotency RENAME TO api_idempotency_unavailable")));
        }
        QSqlDatabase::removeDatabase(QStringLiteral("fault"));
        const QByteArray lookupBody = "<CALL:6>W8LOOK<QSO_DATE:8>20260917<TIME_ON:4>0930<BAND:3>20m<MODE:3>FT8<EOR>";
        const auto lookupFailed = request("POST", QStringLiteral("/api/v1/qso"), lookupBody, "lookup-1");
        const int countAfterFailedLookup = countCall(QStringLiteral("W8LOOK"));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("fault"));
            db.setDatabaseName(m_db); QVERIFY(db.open()); QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral("ALTER TABLE api_idempotency_unavailable RENAME TO api_idempotency")));
        }
        QSqlDatabase::removeDatabase(QStringLiteral("fault"));
        QCOMPARE(lookupFailed.first, 503);
        QCOMPARE(countAfterFailedLookup, 0);
        const auto afterLookupRecovery = request("POST", QStringLiteral("/api/v1/qso"), lookupBody, "lookup-1");
        QCOMPARE(afterLookupRecovery.first, 201);
        QCOMPARE(countCall(QStringLiteral("W8LOOK")), 1);
        const auto diagnostic = request("POST", QStringLiteral("/api/v1/qso"),
                                       "<CALL:9>DIAG-TEST<QSO_DATE:8>20000101<TIME_ON:6>000000<BAND:2>2m<MODE:4>TEST"
                                       "<APP_DECOLOG_DIAGNOSTIC:1>1<EOR>");
        QCOMPARE(diagnostic.first, 409);
        QCOMPARE(countCall(QStringLiteral("DIAG-TEST")), 0);
        // Gia' lavorato? Si', in 20 m, non in 40 m.
        const auto [status, worked] = request("GET", QStringLiteral("/api/v1/worked?call=vk2ab&band=20m&mode=FT8"));
        QCOMPARE(status, 200);
        QCOMPARE(worked.value("count").toInt(), 1);
        QCOMPARE(worked.value("workedBand").toBool(), true);
        QCOMPARE(request("GET", QStringLiteral("/api/v1/worked?call=VK2AB&band=40m")).second.value("workedBand").toBool(), false);
        // Gli ultimi QSO e lo stato.
        const auto qsos = request("GET", QStringLiteral("/api/v1/qsos?call=VK2AB")).second.value("qsos").toArray();
        QCOMPARE(qsos.size(), 1);
        QCOMPARE(qsos.at(0).toObject().value("call").toString(), QString("VK2AB"));
        QVERIFY(request("GET", QStringLiteral("/api/v1/status")).second.value("qsos").toInt() > 0);
        const auto capabilities = request("GET", QStringLiteral("/api/v1/capabilities"));
        QCOMPARE(capabilities.first, 200);
        QCOMPARE(capabilities.second.value("apiVersion").toInt(), 1);
        QCOMPARE(capabilities.second.value("permissionModel").toString(), QString("shared-token"));
        QVERIFY(!capabilities.second.value("txControl").toBool());
        QVERIFY(!capabilities.second.value("events").toBool());
        QVERIFY(capabilities.second.value("operations").toArray().contains("qso.read"));
        QVERIFY(capabilities.second.value("operations").toArray().contains("qso.write"));
        QVERIFY(capabilities.second.value("operations").toArray().contains("idempotency.inspect"));
        QCOMPARE(request("GET",QStringLiteral("/api/v1/idempotency")).first,400);
        QCOMPARE(request("POST", QStringLiteral("/api/v1/capabilities")).first,405);
        QCOMPARE(request("GET", QStringLiteral("/api/v1/capabilities?apiVersion=1")).first,200);
        QCOMPARE(request("GET", QStringLiteral("/api/v1/capabilities?apiVersion=2")).first,400);
        QVERIFY(!capabilities.second.value("perExtensionPermissions").toBool());
        QVERIFY(!capabilities.second.value("durableAtomicIdempotency").toBool());

        auto readRequest = [&](const QByteArray& method, const QString& path) {
            QNetworkRequest r(QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(m_apiPort).arg(path)));
            r.setRawHeader("X-DecoDXLog-Token", "fedcba9876543210fedcba9876543210");
            QNetworkReply* reply=net.sendCustomRequest(r,method,"<CALL:5>W1BAD<EOR>");
            QSignalSpy done(reply,&QNetworkReply::finished); done.wait(5000);
            const auto status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const auto json=QJsonDocument::fromJson(reply->readAll()).object(); reply->deleteLater();
            return std::pair{status,json};
        };
        const auto readCaps=readRequest("GET",QStringLiteral("/api/v1/capabilities"));
        QCOMPARE(readCaps.first,200); QCOMPARE(readCaps.second.value("permissionModel").toString(),QString("read-only-token"));
        QVERIFY(!readCaps.second.value("operations").toArray().contains("qso.write"));
        QVERIFY(!readCaps.second.value("operations").toArray().contains("idempotency.inspect"));
        QVERIFY(readCaps.second.value("operations").toArray().contains("tracker.read"));
        const auto tracker=readRequest("GET",QStringLiteral("/api/v1/tracker"));
        QCOMPARE(tracker.first,200); QVERIFY(!tracker.second.value("available").toBool());
        QCOMPARE(readRequest("POST",QStringLiteral("/api/v1/tracker")).first,403);
        QCOMPARE(readRequest("GET",QStringLiteral("/api/v1/qsos")).first,200);
        QCOMPARE(readRequest("POST",QStringLiteral("/api/v1/qso")).first,403);
        QCOMPARE(readRequest("GET",QStringLiteral("/api/v1/idempotency")).first,403);
        QCOMPARE(countCall(QStringLiteral("W1BAD")),0);
        const QString python=QStandardPaths::findExecutable(QStringLiteral("python3"));
        if (!python.isEmpty()) {
        const auto qsoCountBefore=scalar(QStringLiteral("SELECT COUNT(*) FROM qso"));
        QProcess report;
        QProcessEnvironment reportEnv=QProcessEnvironment::systemEnvironment();
        reportEnv.insert("DECODXLOG_READ_TOKEN","fedcba9876543210fedcba9876543210");
        report.setProcessEnvironment(reportEnv);
        report.start(python,{QStringLiteral(DECODXLOG_SOURCE_DIR) + "/examples/extensions/recent_activity.py",
                            "--port",QString::number(m_apiPort),"--limit","20"});
        QVERIFY(report.waitForStarted(5000)); QVERIFY(report.waitForFinished(15000));
        QCOMPARE(report.exitCode(),0);
        const auto summary=QJsonDocument::fromJson(report.readAllStandardOutput()).object();
        QCOMPARE(summary.value("scope").toString(),QString("recent_sample"));
        QVERIFY(summary.value("sampleSize").toInt()>0);
        QVERIFY(summary.value("byBand").toObject().value("20m").toInt()>0);
        QCOMPARE(countCall(QStringLiteral("W1BAD")),0);
        QProcess harness;
        harness.setProcessEnvironment(reportEnv);
        harness.start(python,{QStringLiteral(DECODXLOG_SOURCE_DIR) + "/tools/extension_harness.py",
                              QStringLiteral(DECODXLOG_SOURCE_DIR) + "/examples/extensions/recent_activity.manifest.json",
                              "--port",QString::number(m_apiPort)});
        QVERIFY(harness.waitForStarted(5000)); QVERIFY(harness.waitForFinished(15000));
        QCOMPARE(harness.exitCode(),0);
        const auto harnessOutput=QJsonDocument::fromJson(harness.readAllStandardOutput()).object();
        QCOMPARE(harnessOutput.value("scope").toString(),QString("recent_sample"));
        QVERIFY(harnessOutput.value("sampleSize").toInt()>0);
        QProcess privilegedReport;
        reportEnv.insert("DECODXLOG_READ_TOKEN","0123456789abcdef0123456789abcdef");
        privilegedReport.setProcessEnvironment(reportEnv);
        privilegedReport.start(python,{QStringLiteral(DECODXLOG_SOURCE_DIR) + "/examples/extensions/recent_activity.py",
                                      "--port",QString::number(m_apiPort),"--limit","20"});
        QVERIFY(privilegedReport.waitForStarted(5000)); QVERIFY(privilegedReport.waitForFinished(15000));
        QCOMPARE(privilegedReport.exitCode(),1);
        QVERIFY(!privilegedReport.readAllStandardError().contains("0123456789abcdef"));
        QCOMPARE(scalar(QStringLiteral("SELECT COUNT(*) FROM qso")),qsoCountBefore);
        const QString extensionDir=m_dir.filePath(QStringLiteral("hung-extension"));
        QVERIFY(QDir().mkpath(extensionDir));
        QFile stuck(extensionDir + "/stuck.py"); QVERIFY(stuck.open(QIODevice::WriteOnly));
        stuck.write("import time\ntime.sleep(30)\n"); stuck.close();
        QFile manifest(extensionDir + "/manifest.json"); QVERIFY(manifest.open(QIODevice::WriteOnly));
        manifest.write(R"({"schemaVersion":1,"apiVersion":1,"id":"org.test.stuck","version":"1.0.0","entrypoint":"stuck.py","permissions":["qso.read"]})"); manifest.close();
        QProcess stuckCheck; reportEnv.insert("DECODXLOG_READ_TOKEN","fedcba9876543210fedcba9876543210");
        stuckCheck.setProcessEnvironment(reportEnv);
        stuckCheck.start(python,{QStringLiteral(DECODXLOG_SOURCE_DIR) + "/tools/extension_harness.py",
                                 manifest.fileName(),"--port",QString::number(m_apiPort),"--timeout","2"});
        QVERIFY(stuckCheck.waitForStarted(5000)); QTest::qWait(300); QCOMPARE(stuckCheck.state(),QProcess::Running);
        const auto whileHung=request("POST",QStringLiteral("/api/v1/qso"),
                    "<CALL:5>K1HAR<QSO_DATE:8>20260917<TIME_ON:4>0912<BAND:3>20m<MODE:2>CW<EOR>");
        QCOMPARE(whileHung.first,201); QCOMPARE(countCall(QStringLiteral("K1HAR")),1);
        QVERIFY(stuckCheck.waitForFinished(5000)); QCOMPARE(stuckCheck.exitCode(),1);
        QCOMPARE(countCall(QStringLiteral("K1HAR")),1);
        } else {
            qWarning("Python unavailable; real-app extension subprocess was not exercised");
        }
        // Senza chiave, niente.
        QNetworkReply* anonymous = net.get(QNetworkRequest(QUrl(QStringLiteral("http://127.0.0.1:%1/api/v1/status").arg(m_apiPort))));
        QSignalSpy done(anonymous, &QNetworkReply::finished);
        done.wait(5000);
        QCOMPARE(anonymous->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 401);
        anonymous->deleteLater();
    }

    void scopedExtensionReadsThroughApplication()
    {
        QNetworkAccessManager net;
        auto request = [&](const QByteArray& method, const QString& path) {
            QNetworkRequest r(QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(m_apiPort).arg(path)));
            r.setRawHeader("X-DecoDXLog-Token", "11111111111111111111111111111111");
            QNetworkReply* reply = net.sendCustomRequest(r, method, "");
            QSignalSpy done(reply, &QNetworkReply::finished); done.wait(5000);
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();
            reply->deleteLater(); return std::pair{status,json};
        };
        const auto caps = request("GET", QStringLiteral("/api/v1/capabilities"));
        QCOMPARE(caps.first,200);
        QCOMPARE(caps.second.value("permissionModel").toString(),QStringLiteral("extension-token"));
        QVERIFY(caps.second.value("perExtensionPermissions").toBool());
        QCOMPARE(caps.second.value("operations").toArray(),QJsonArray{QStringLiteral("qso.read")});
        QCOMPARE(request("GET",QStringLiteral("/api/v1/qsos")).first,200);
        QCOMPARE(request("GET",QStringLiteral("/api/v1/tracker")).first,403);
        QCOMPARE(request("POST",QStringLiteral("/api/v1/qso")).first,403);
        const QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
        QVERIFY2(!python.isEmpty(), "Python 3 required to exercise the sample extension");
        QProcess report;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("DECODXLOG_READ_TOKEN"), QStringLiteral("11111111111111111111111111111111"));
        report.setProcessEnvironment(environment);
        report.start(python, {QStringLiteral(DECODXLOG_SOURCE_DIR "/examples/extensions/recent_activity.py"),
                              QStringLiteral("--port"), QString::number(m_apiPort)});
        QVERIFY(report.waitForFinished(10000));
        QCOMPARE(report.exitCode(), 0);
        const QByteArray output = report.readAllStandardOutput();
        QVERIFY(!output.contains("11111111111111111111111111111111"));
        QCOMPARE(QJsonDocument::fromJson(output).object().value("scope").toString(),QStringLiteral("recent_sample"));
    }

    void aBurstIsAllLogged()
    {
        QElapsedTimer elapsed;
        elapsed.start();
        // Venti QSO uno dietro l'altro, come a fine gara quando si svuota la coda.
        for (int i = 1; i <= 20; ++i) {
            const QString call = QStringLiteral("EA1%1").arg(QChar(QLatin1Char('A' + i)));
            send(wsjtx::buildLoggedAdif(
                QStringLiteral("Decodium"),
                adifFor(call + QStringLiteral("B"), QStringLiteral("IN53"),
                        QStringLiteral("11%1").arg(i, 2, 10, QLatin1Char('0')) + QStringLiteral("00"), "Decodium")));
        }
        QVERIFY(waitFor([&] {
            return scalar(QStringLiteral("SELECT COUNT(*) FROM qso WHERE deleted = 0 AND call LIKE 'EA1_B'")).toInt()
                   == 20;
        }, 20000));
        qInfo() << "Time until the app persisted 20 UDP QSOs:" << elapsed.elapsed() << "ms (100 ms polling)";
        QCOMPARE(m_app.state(), QProcess::Running);
    }
};

QTEST_GUILESS_MAIN(TestUdpPipeline)
#include "tst_udppipeline.moc"
