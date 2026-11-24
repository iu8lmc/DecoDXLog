// Le conferme di eQSL e di QRZ Logbook: le risposte dei due servizi lette
// bene, i QSO confermati segnati nel log, e lo scarico vero e proprio contro
// server finti (le pagine di QRZ una dopo l'altra, la password mai negli errori).
#include "core/LogDatabase.h"
#include "core/QslDownload.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUrlQuery>

using namespace decolog::core;

namespace {

// La pagina che eQSL risponde quando il file e' pronto: il link e' relativo.
const QByteArray kEqslPage = "<HTML><HEAD><TITLE>eQSL.cc DownloadInBox</TITLE></HEAD><BODY>\n"
                             "<H2>Your ADIF log file has been built</H2>\n"
                             "<LI><A HREF=\"../downloadedfiles/iu8lmc7f3a.adi\">.ADI file</A>\n"
                             "</BODY></HTML>";

const QByteArray kEqslAdif = "eQSL.cc DownloadInBox\n"
                             "<PROGRAMID:21>eQSL.cc DownloadInBox\n<ADIF_Ver:1>1\n<EOH>\n"
                             "<CALL:5>K1ABC<QSO_DATE:8>20260910<TIME_ON:4>1205<BAND:3>20M<MODE:4>MFSK"
                             "<SUBMODE:3>FT4<RST_SENT:3>-05<QSL_SENT:1>Y<QSL_SENT_VIA:1>E"
                             "<GRIDSQUARE:6>FN42AB<EQSL_QSLRDATE:8>20260915<EOR>\n"
                             // Una segnalazione d'ascolto: non e' un QSO.
                             "<CALL:5>K1ABC<QSO_DATE:8>20260910<TIME_ON:4>1205<BAND:3>20M<MODE:3>FT8"
                             "<APP_EQSL_SWL:1>Y<EOR>\n"
                             "<CALL:5>JA1XX<QSO_DATE:8>20260911<TIME_ON:4>0715<BAND:3>40M<MODE:3>FT8<EOR>\n"
                             // Non mandata: non e' una conferma.
                             "<CALL:5>W1AW<QSO_DATE:8>20260911<TIME_ON:4>0800<BAND:3>40M<MODE:3>FT8"
                             "<QSL_SENT:1>N<EOR>\n";

// Un QSO del logbook di QRZ, come lo manda l'API: l'ADIF scritto con &lt; e &gt;.
QByteArray qrzRecord(qint64 logId, const QByteArray& call, const QByteArray& status)
{
    auto field = [](const QByteArray& name, const QByteArray& value) {
        return "&lt;" + name + ":" + QByteArray::number(value.size()) + "&gt;" + value;
    };
    return field("app_qrzlog_logid", QByteArray::number(logId)) + field("call", call) + field("band", "20m")
           + field("mode", "FT8") + field("qso_date", "20260910") + field("time_on", "1203")
           + field("app_qrzlog_status", status) + field("app_qrzlog_qsldate", "20260920")
           + field("gridsquare", "FN42") + "&lt;eor&gt;\n";
}

// Un server HTTP finto: risponde secondo il percorso, e tiene le richieste
// (con il corpo delle POST, che puo' arrivare in un secondo pezzo).
class FakeServer : public QTcpServer {
public:
    struct Request {
        QByteArray method;
        QByteArray path;
        QUrlQuery query;
        QUrlQuery form;
    };
    QList<Request> requests;
    std::function<QByteArray(const Request&)> answer;
    int status{200};

    FakeServer()
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* s = nextPendingConnection()) {
                auto buffer = std::make_shared<QByteArray>();
                connect(s, &QTcpSocket::readyRead, s, [this, s, buffer] {
                    *buffer += s->readAll();
                    const qsizetype end = buffer->indexOf("\r\n\r\n");
                    if (end < 0)
                        return;
                    const QByteArray head = buffer->left(end);
                    qsizetype length = 0;
                    for (const QByteArray& line : head.split('\n')) {
                        if (line.toLower().startsWith("content-length:"))
                            length = line.mid(15).trimmed().toLongLong();
                    }
                    if (buffer->size() < end + 4 + length)
                        return;
                    const QList<QByteArray> first = head.left(head.indexOf("\r\n")).split(' ');
                    const QUrl url(QString::fromLatin1(first.value(1)));
                    Request r{first.value(0), url.path().toLatin1(), QUrlQuery(url),
                              QUrlQuery(QString::fromUtf8(buffer->mid(end + 4, length)))};
                    requests << r;
                    const QByteArray body = answer ? answer(r) : QByteArray();
                    s->write("HTTP/1.1 " + QByteArray::number(status) + (status == 200 ? " OK" : " Error")
                             + "\r\nContent-Type: text/html\r\nConnection: close\r\nContent-Length: "
                             + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                    s->disconnectFromHost();
                });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
        listen(QHostAddress::LocalHost);
    }
    QUrl url(const char* path) const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(serverPort()).arg(QLatin1String(path)));
    }
};

AdifRecord qso(const char* call, const char* band, const char* mode, const char* submode, const char* date,
               const char* time)
{
    return AdifRecord{{"CALL", call}, {"BAND", band}, {"MODE", mode}, {"SUBMODE", submode},
                      {"QSO_DATE", date}, {"TIME_ON", time}};
}

} // namespace

class TestQslDownload : public QObject {
    Q_OBJECT

private slots:
    void cursorRequiresCompleteDatabaseSuccess()
    {
        QVERIFY(confirmations::mayAdvanceCursor(false,0));
        QVERIFY(!confirmations::mayAdvanceCursor(true,0));
        QVERIFY(!confirmations::mayAdvanceCursor(false,1));
        QVERIFY(!confirmations::mayAdvanceCursor(true,3));
    }
    void missingCredentialsNeverOpenConnection()
    {
        FakeServer server;
        ConfirmationDownloader downloader;
        downloader.setEndpoints(server.url("/eqsl"), server.url("/qrz"));
        QSignalSpy done(&downloader, &ConfirmationDownloader::finished);
        downloader.downloadQrz(QStringLiteral("  "));
        downloader.downloadEqsl(QStringLiteral("K1ABC"), QStringLiteral(" "));
        downloader.downloadEqsl(QStringLiteral(" "), QStringLiteral("secret"));
        QCOMPARE(done.size(), 3);
        QTest::qWait(100);
        QVERIFY(server.requests.isEmpty());
        QVERIFY(!downloader.busy());
    }

    void qrzAutomaticOptInAB()
    {
        FakeServer server;
        server.status = 403;
        ConfirmationDownloader downloader;
        downloader.setEndpoints(server.url("/eqsl"), server.url("/qrz"));
        QSignalSpy done(&downloader, &ConfirmationDownloader::finished);
        const QDateTime now = QDateTime::currentDateTimeUtc();
        const QString fixtureKey = QStringLiteral("ABCD-1234-EF01-5678");
        auto scheduledCheck = [&](bool qrzEnabled) {
            if (confirmations::autoDownloadDue({}, {}, now, 6, qrzEnabled))
                downloader.downloadQrz(fixtureKey);
        };

        scheduledCheck(true); // Previous schedule included QRZ whenever a key was present.
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 5000);
        QCOMPARE(server.requests.size(), 1);
        QCOMPARE(server.requests.first().form.queryItemValue("ACTION"), QStringLiteral("FETCH"));
        const auto report = done.first().first().value<confirmations::Report>();
        QVERIFY(!report.ok);
        QVERIFY(report.error.contains(QStringLiteral("HTTP 403")));
        QVERIFY(!report.error.contains(fixtureKey));

        scheduledCheck(false); // Identical local key, but QRZ not selected for automatic use.
        QTest::qWait(100);
        QCOMPARE(done.size(), 1);
        QCOMPARE(server.requests.size(), 1);
        QVERIFY(confirmations::autoDownloadDue({}, {}, now, 6)); // eQSL keeps its existing schedule.

        server.status = 200;
        server.answer = [](const FakeServer::Request&) { return QByteArray("RESULT=OK&COUNT=0&ADIF="); };
        downloader.downloadQrz(fixtureKey); // Manual FETCH does not depend on the schedule.
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 2, 5000);
        QCOMPARE(server.requests.size(), 2);
        QVERIFY(done.last().first().value<confirmations::Report>().ok);
    }

    void eqslLinkFromThePage()
    {
        const QUrl page("https://www.eqsl.cc/qslcard/DownloadInBox.cfm?UserName=X");
        QString error;
        QCOMPARE(confirmations::eqslFileLink(kEqslPage, page, &error),
                 QUrl("https://www.eqsl.cc/downloadedfiles/iu8lmc7f3a.adi"));

        const QUrl none = confirmations::eqslFileLink(
            "<html><body><b>Error: No such Username/Password found</b></body></html>", page, &error);
        QVERIFY(none.isEmpty());
        QVERIFY2(error.contains("Username/Password"), qPrintable(error));
    }

    void eqslConfirmationsSkipSwl()
    {
        const QList<AdifRecord> list = confirmations::eqslConfirmations(kEqslAdif);
        QCOMPARE(list.size(), 2);
        QCOMPARE(list.at(0).value("CALL"), QString("K1ABC"));
        QCOMPARE(list.at(0).value("QSLRDATE"), QString("20260915"));
        QCOMPARE(list.at(1).value("CALL"), QString("JA1XX"));
    }

    void qrzPage()
    {
        const QByteArray body = "RESULT=OK&COUNT=2&LOGIDS=101,102&ADIF=" + qrzRecord(101, "K1ABC", "C")
                                + qrzRecord(102, "W1AW", "N");
        const confirmations::QrzPage page = confirmations::parseQrzFetch(body);
        QVERIFY2(page.ok, qPrintable(page.error));
        QCOMPARE(page.count, 2);
        QCOMPARE(page.records.size(), 2);
        QCOMPARE(page.lastLogId, 102);
        const QList<AdifRecord> confirmed = confirmations::qrzConfirmations(page.records);
        QCOMPARE(confirmed.size(), 1);
        QCOMPARE(confirmed.at(0).value("CALL"), QString("K1ABC"));
        QCOMPARE(confirmed.at(0).value("QSLRDATE"), QString("20260920"));
    }

    void qrzEmptyAndErrors()
    {
        // Niente da scaricare non e' un errore.
        confirmations::QrzPage page =
            confirmations::parseQrzFetch("RESULT=FAIL&REASON=no log entries found&COUNT=0");
        QVERIFY(page.ok);
        QVERIFY(page.records.isEmpty());

        page = confirmations::parseQrzFetch("RESULT=AUTH&REASON=invalid api key 1234-ABCD&EXTENDED=");
        QVERIFY(!page.ok);
        QVERIFY2(page.error.contains("API key"), qPrintable(page.error));

        page = confirmations::parseQrzFetch("<html>502 Bad Gateway</html>");
        QVERIFY(!page.ok);
    }

    void whenToDownload()
    {
        const QDateTime now(QDate(2026, 9, 28), QTime(15, 0), QTimeZone::UTC);
        const QDateTime success = now.addSecs(-5 * 3600);

        // Da quando: un giorno prima dell'ultimo scarico riuscito, o tutto.
        QCOMPARE(confirmations::downloadSince(success, false), success.addDays(-1));
        QVERIFY(!confirmations::downloadSince(success, true).isValid());
        QVERIFY(!confirmations::downloadSince({}, false).isValid());

        // Mai provato: subito. Spento: mai.
        QVERIFY(confirmations::autoDownloadDue({}, {}, now, 12));
        QVERIFY(!confirmations::autoDownloadDue({}, {}, now, 0));
        // Cinque ore fa: con 6 non ancora, con 12 no, con 4 si'.
        QVERIFY(!confirmations::autoDownloadDue(success, {}, now, 6));
        QVERIFY(confirmations::autoDownloadDue(success, {}, now, 4));
        // Un tentativo fallito un'ora fa: si aspetta il giro, anche se
        // l'ultimo riuscito e' di ieri.
        QVERIFY(!confirmations::autoDownloadDue(now.addDays(-1), now.addSecs(-3600), now, 6));
        QVERIFY(confirmations::autoDownloadDue(now.addDays(-1), now.addSecs(-7 * 3600), now, 6));
    }

    void confirmationsMarkTheLog()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        const qint64 k1 = db.insertQso(qso("K1ABC", "20m", "MFSK", "FT4", "20260910", "120300"), "import").id;
        QVERIFY(k1 > 0);

        const QList<AdifRecord> eqsl = confirmations::eqslConfirmations(kEqslAdif);
        QCOMPARE(db.applyConfirmation("eqsl", eqsl.at(0)).status, ConfirmationResult::Status::Confirmed);
        QCOMPARE(db.applyConfirmation("eqsl", eqsl.at(1)).status, ConfirmationResult::Status::NotFound);
        auto rec = db.record(k1);
        QCOMPARE(rec->value("EQSL_QSL_RCVD"), QString("Y"));
        QCOMPARE(rec->value("EQSL_QSLRDATE"), QString("20260915"));
        QCOMPARE(rec->value("GRIDSQUARE"), QString("FN42AB"));
        QCOMPARE(db.applyConfirmation("eqsl", eqsl.at(0)).status, ConfirmationResult::Status::AlreadyConfirmed);

        const auto qrz = confirmations::qrzConfirmations(
            confirmations::parseQrzFetch("RESULT=OK&COUNT=1&ADIF=" + qrzRecord(7, "K1ABC", "C")).records);
        QCOMPARE(db.applyConfirmation("qrz", qrz.at(0)).status, ConfirmationResult::Status::Confirmed);
        rec = db.record(k1);
        QCOMPARE(rec->value("QRZCOM_QSO_DOWNLOAD_STATUS"), QString("Y"));
        QCOMPARE(rec->value("QRZCOM_QSO_DOWNLOAD_DATE"), QString("20260920"));
        // La cartolina resta com'era: queste non sono cartoline.
        QVERIFY(rec->value("QSL_RCVD").isEmpty());
    }

    void anAccountConfirmsOnlyItsProfiles()
    {
        // Due profili: IU8LMC (1) e un nominativo speciale (2) con il suo
        // account. La conferma dell'account speciale tocca solo i QSO del 2.
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        StationProfile home;
        home.name = "Casa";
        home.stationCallsign = "IU8LMC";
        const qint64 p1 = db.saveStationProfile(home);
        StationProfile special;
        special.name = "Speciale";
        special.stationCallsign = "II8XYZ";
        const qint64 p2 = db.saveStationProfile(special);
        QVERIFY(p1 > 0 && p2 > 0);
        const qint64 fromHome = db.insertQso(qso("K1ABC", "20m", "FT8", "", "20260910", "120300"), "import", {}, false, p1).id;

        const auto eqsl = confirmations::eqslConfirmations(kEqslAdif);
        // Solo il profilo 2: il QSO di casa non si tocca.
        QCOMPARE(db.applyConfirmation("eqsl", eqsl.at(0), 1800, {p2}).status, ConfirmationResult::Status::NotFound);
        // L'account generale, esclusi i profili con un account loro: si'.
        QCOMPARE(db.applyConfirmation("eqsl", eqsl.at(0), 1800, {}, {p2}).status, ConfirmationResult::Status::Confirmed);
        QCOMPARE(db.record(fromHome)->value("EQSL_QSL_RCVD"), QString("Y"));
    }

    // La cartolina di una eQSL: GeteQSL.cfm dice dove sta l'immagine, la si
    // scarica e la si tiene; chiesta di nuovo, si riprende dalla cartella
    // senza disturbare eQSL. Un errore della pagina si dice, senza password.
    void eqslCardImage()
    {
        QString error;
        QCOMPARE(confirmations::eqslCardImage(
                     "<HTML><BODY><IMG SRC=\"/CFFileServlet/_cf_image/_cfimg123.JPG\" ALT=\"eQSL\"></BODY></HTML>",
                     QUrl("https://www.eqsl.cc/qslcard/GeteQSL.cfm"), &error),
                 QUrl("https://www.eqsl.cc/CFFileServlet/_cf_image/_cfimg123.JPG"));
        // Un <img> senza un file vero dietro ("and") non e' la cartolina: si va
        // a quello dopo, e se non ce n'e' si dice cosa c'era invece di un 404.
        QCOMPARE(confirmations::eqslCardImage(
                     "<HTML><IMG SRC=and><IMG BORDER=0 SRC=\"/CFFileServlet/_cf_image/_cfimg9.PNG\"></HTML>",
                     QUrl("https://www.eqsl.cc/qslcard/GeteQSL.cfm"), &error),
                 QUrl("https://www.eqsl.cc/CFFileServlet/_cf_image/_cfimg9.PNG"));
        QVERIFY(confirmations::eqslCardImage("<HTML><IMG SRC=and></HTML>",
                                             QUrl("https://www.eqsl.cc/qslcard/GeteQSL.cfm"), &error).isEmpty());
        QVERIFY2(error.contains(QLatin1String("and")), qPrintable(error));
        QVERIFY(confirmations::eqslCardImage("<HTML><BODY>Error: I cannot find that log entry</BODY></HTML>",
                                             QUrl("https://www.eqsl.cc/"), &error).isEmpty());
        QCOMPARE(error, QString("Error: I cannot find that log entry"));

        FakeServer server;
        server.answer = [](const FakeServer::Request& r) -> QByteArray {
            if (r.path.endsWith(".JPG"))
                return QByteArray("\xFF\xD8\xFF\xE0 fake jpeg", 14);
            if (r.query.queryItemValue("CallsignFrom") == QLatin1String("JA1XX"))
                return "<HTML>Error: No match on Username/Password for that QSO Date/Time</HTML>";
            return "<HTML><IMG SRC=\"/CFFileServlet/_cf_image/k1abc.JPG\"></HTML>";
        };
        QTemporaryDir dir;
        EqslCardFetcher f;
        f.setEndpoint(server.url("/qslcard/GeteQSL.cfm"));
        f.setCacheDir(dir.path());
        f.setMinimumGapMs(0);
        QSignalSpy ready(&f, &EqslCardFetcher::ready);
        EqslCardFetcher::Request r{"IU8LMC", "segreta", "K1ABC",
                                   QDateTime(QDate(2026, 9, 10), QTime(12, 5), QTimeZone::UTC), "20m", "FT8"};
        f.fetch(r);
        QVERIFY(ready.wait(5000));
        QCOMPARE(ready.at(0).at(0).toString(), EqslCardFetcher::keyOf(r));
        const QString file = ready.at(0).at(1).toString();
        QVERIFY2(QFileInfo::exists(file), qPrintable(ready.at(0).at(2).toString()));
        const QUrlQuery q = server.requests.at(0).query;
        QCOMPARE(q.queryItemValue("Username"), QString("IU8LMC"));
        QCOMPARE(q.queryItemValue("QSOYear"), QString("2026"));
        QCOMPARE(q.queryItemValue("QSOHour"), QString("12"));
        QCOMPARE(q.queryItemValue("QSOMinute"), QString("05"));
        QCOMPARE(q.queryItemValue("QSOBand"), QString("20m"));
        QCOMPARE(q.queryItemValue("QSOMode"), QString("FT8"));

        // Di nuovo: dalla cartella, nessuna richiesta in piu'.
        const int asked = server.requests.size();
        f.fetch(r);
        QVERIFY(ready.wait(5000));
        QCOMPARE(ready.at(1).at(1).toString(), file);
        QCOMPARE(server.requests.size(), asked);

        // Un errore di eQSL si dice, e la password non c'e'.
        r.call = "JA1XX";
        f.fetch(r);
        QVERIFY(ready.wait(5000));
        QVERIFY(ready.at(2).at(1).toString().isEmpty());
        QVERIFY(ready.at(2).at(2).toString().contains(QStringLiteral("No match")));
        QVERIFY(!ready.at(2).at(2).toString().contains(QStringLiteral("segreta")));
    }

    void downloadEqsl()
    {
        FakeServer server;
        server.answer = [](const FakeServer::Request& r) {
            return r.path.endsWith(".adi") ? kEqslAdif : kEqslPage;
        };
        ConfirmationDownloader d;
        d.setEndpoints(server.url("/qslcard/DownloadInBox.cfm"), server.url("/api"));
        QSignalSpy done(&d, &ConfirmationDownloader::finished);
        d.downloadEqsl("IU8LMC", "pw&x=1", QDateTime(QDate(2026, 9, 1), QTime(6, 30), QTimeZone::UTC));
        QVERIFY(d.busy());
        QVERIFY(done.wait(5000));
        QVERIFY(!d.busy());
        const auto report = done.at(0).at(0).value<confirmations::Report>();
        QVERIFY2(report.ok, qPrintable(report.error));
        QCOMPARE(report.service, QString("eqsl"));
        QCOMPARE(report.confirmations.size(), 2);
        QCOMPARE(server.requests.size(), 2);
        const QUrlQuery q = server.requests.at(0).query;
        QCOMPARE(q.queryItemValue("UserName"), QString("IU8LMC"));
        QCOMPARE(q.queryItemValue("Password", QUrl::FullyDecoded), QString("pw&x=1"));
        QCOMPARE(q.queryItemValue("RcvdSince"), QString("202609010630"));
        QCOMPARE(server.requests.at(1).path, QByteArray("/downloadedfiles/iu8lmc7f3a.adi"));
    }

    // Un periodo di QSO, come per LoTW: gli estremi compresi, una data si puo' lasciare vuota.
    void periodFiltersTheQsoDate()
    {
        const QList<AdifRecord> all{qso("A1AA", "20m", "FT8", "", "20260831", "2359"),
                                    qso("B1BB", "20m", "FT8", "", "20260901", "0000"),
                                    qso("C1CC", "20m", "FT8", "", "20260915", "1200"),
                                    qso("D1DD", "20m", "FT8", "", "20260930", "2359"),
                                    qso("E1EE", "20m", "FT8", "", "20261001", "0001"),
                                    qso("F1FF", "20m", "FT8", "", "", "1200")};
        auto calls = [](const QList<AdifRecord>& list) {
            QStringList out;
            for (const AdifRecord& r : list)
                out << r.value("CALL");
            return out;
        };
        const QDate sep1(2026, 9, 1), sep30(2026, 9, 30);
        QCOMPARE(calls(confirmations::inPeriod(all, sep1, sep30)), QStringList({"B1BB", "C1CC", "D1DD"}));
        QCOMPARE(calls(confirmations::inPeriod(all, sep30, QDate())), QStringList({"D1DD", "E1EE"}));
        QCOMPARE(calls(confirmations::inPeriod(all, QDate(), sep1)), QStringList({"A1AA", "B1BB"}));
        // Nessun periodo: tutto, anche la riga senza data.
        QCOMPARE(confirmations::inPeriod(all, QDate(), QDate()).size(), all.size());
    }

    void downloadEqslForAPeriod()
    {
        FakeServer server;
        server.answer = [](const FakeServer::Request& r) {
            return r.path.endsWith(".adi") ? kEqslAdif : kEqslPage;
        };
        ConfirmationDownloader d;
        d.setEndpoints(server.url("/qslcard/DownloadInBox.cfm"), server.url("/api"));
        QSignalSpy done(&d, &ConfirmationDownloader::finished);
        d.downloadEqsl("IU8LMC", "pw", QDateTime(), QDate(2026, 9, 1), QDate(2026, 9, 30));
        QVERIFY(done.wait(5000));
        const auto report = done.at(0).at(0).value<confirmations::Report>();
        QVERIFY2(report.ok, qPrintable(report.error));
        // eQSL filtra per data del QSO (MM/GG/AAAA), non per data di ricezione.
        const QUrlQuery q = server.requests.at(0).query;
        QCOMPARE(q.queryItemValue("LimitDateLo", QUrl::FullyDecoded), QString("09/01/2026"));
        QCOMPARE(q.queryItemValue("LimitDateHi", QUrl::FullyDecoded), QString("09/30/2026"));
        QVERIFY(!q.hasQueryItem("RcvdSince"));
    }

    void downloadQrzForAPeriod()
    {
        // QRZ risponde con tutto; il periodo si tiene qui (le righe di prova sono del 10/09).
        FakeServer server;
        server.answer = [](const FakeServer::Request&) {
            return "RESULT=OK&COUNT=2&ADIF=" + qrzRecord(1001, "K1AAA", "C") + qrzRecord(1002, "K2BBB", "C");
        };
        ConfirmationDownloader d;
        d.setEndpoints(server.url("/qslcard/DownloadInBox.cfm"), server.url("/api"));
        QSignalSpy done(&d, &ConfirmationDownloader::finished);
        d.downloadQrz("ABCD-1234", QDate(), QDate(2026, 9, 1), QDate(2026, 9, 30));
        QVERIFY(done.wait(5000));
        QCOMPARE(done.at(0).at(0).value<confirmations::Report>().confirmations.size(), 2);

        QSignalSpy other(&d, &ConfirmationDownloader::finished);
        d.downloadQrz("ABCD-1234", QDate(), QDate(2026, 10, 1), QDate());
        QVERIFY(other.wait(5000));
        QVERIFY(other.at(0).at(0).value<confirmations::Report>().confirmations.isEmpty());
        // Il periodo non mette MODSINCE: si chiede per intero.
        QVERIFY(!server.requests.at(0).form.queryItemValue("OPTION", QUrl::FullyDecoded).contains("MODSINCE"));
    }

    void eqslNothingNewAndWrongPassword()
    {
        FakeServer server;
        QByteArray page = "<html><body>You have no log entries</body></html>";
        server.answer = [&page](const FakeServer::Request&) { return page; };
        ConfirmationDownloader d;
        d.setEndpoints(server.url("/qslcard/DownloadInBox.cfm"), server.url("/api"));
        QSignalSpy done(&d, &ConfirmationDownloader::finished);
        d.downloadEqsl("IU8LMC", "secretpw");
        QVERIFY(done.wait(5000));
        auto report = done.at(0).at(0).value<confirmations::Report>();
        QVERIFY2(report.ok, qPrintable(report.error));
        QVERIFY(report.confirmations.isEmpty());
        QVERIFY(!server.requests.at(0).query.hasQueryItem("RcvdSince"));

        // Una pagina d'errore che ripete la password: nel messaggio non c'e'.
        page = "<html><body>Error: No such Username/Password found (secretpw)</body></html>";
        d.downloadEqsl("IU8LMC", "secretpw");
        QVERIFY(done.wait(5000));
        report = done.at(1).at(0).value<confirmations::Report>();
        QVERIFY(!report.ok);
        QVERIFY2(!report.error.contains("secretpw"), qPrintable(report.error));

        // Un errore HTTP: l'URL con la password non arriva nel messaggio.
        server.status = 503;
        d.downloadEqsl("IU8LMC", "secretpw");
        QVERIFY(done.wait(5000));
        report = done.at(2).at(0).value<confirmations::Report>();
        QVERIFY(!report.ok);
        QVERIFY(!report.error.isEmpty());
        QVERIFY2(!report.error.contains("secretpw"), qPrintable(report.error));
    }

    void downloadQrzPageByPage()
    {
        // Una pagina piena (250), poi una da tre: due richieste, la seconda
        // dal logid successivo all'ultimo della prima.
        FakeServer server;
        server.answer = [](const FakeServer::Request& r) {
            const QString option = r.form.queryItemValue("OPTION", QUrl::FullyDecoded);
            const bool second = option.contains("AFTERLOGID:");
            const int n = second ? 3 : ConfirmationDownloader::kQrzPage;
            const qint64 start = second ? 1000 + ConfirmationDownloader::kQrzPage : 1000;
            QByteArray body = "RESULT=OK&COUNT=" + QByteArray::number(n) + "&ADIF=";
            for (int i = 0; i < n; ++i)
                body += qrzRecord(start + i, "K" + QByteArray::number(i) + "AA", i % 2 ? "N" : "C");
            return body;
        };
        ConfirmationDownloader d;
        d.setEndpoints(server.url("/qslcard/DownloadInBox.cfm"), server.url("/api"));
        QSignalSpy done(&d, &ConfirmationDownloader::finished);
        d.downloadQrz("ABCD-1234", QDate(2026, 9, 1));
        QVERIFY(done.wait(5000));
        const auto report = done.at(0).at(0).value<confirmations::Report>();
        QVERIFY2(report.ok, qPrintable(report.error));
        QCOMPARE(report.service, QString("qrz"));
        QCOMPARE(server.requests.size(), 2);
        QCOMPARE(server.requests.at(0).method, QByteArray("POST"));
        QCOMPARE(server.requests.at(0).form.queryItemValue("KEY"), QString("ABCD-1234"));
        QCOMPARE(server.requests.at(0).form.queryItemValue("ACTION"), QString("FETCH"));
        const QString first = server.requests.at(0).form.queryItemValue("OPTION", QUrl::FullyDecoded);
        QVERIFY2(first.contains("STATUS:CONFIRMED"), qPrintable(first));
        QVERIFY2(first.contains("MODSINCE:2026-09-01"), qPrintable(first));
        QVERIFY2(!first.contains("AFTERLOGID"), qPrintable(first));
        const QString second = server.requests.at(1).form.queryItemValue("OPTION", QUrl::FullyDecoded);
        QVERIFY2(second.contains(QStringLiteral("AFTERLOGID:%1").arg(1000 + ConfirmationDownloader::kQrzPage)),
                 qPrintable(second));
        // Solo quelle con stato C: meta' della prima pagina, due della seconda.
        QCOMPARE(report.confirmations.size(), ConfirmationDownloader::kQrzPage / 2 + 2);
    }

    void qrzWrongKey()
    {
        FakeServer server;
        // Come risponde logbook.qrz.com a una chiave sbagliata.
        server.answer = [](const FakeServer::Request&) {
            return QByteArray("STATUS=AUTH&RESULT=AUTH&REASON=invalid api key zzzz9999\n&EXTENDED=");
        };
        ConfirmationDownloader d;
        d.setEndpoints(server.url("/qslcard/DownloadInBox.cfm"), server.url("/api"));
        QSignalSpy done(&d, &ConfirmationDownloader::finished);
        d.downloadQrz("ZZZZ-9999");
        QVERIFY(done.wait(5000));
        const auto report = done.at(0).at(0).value<confirmations::Report>();
        QVERIFY(!report.ok);
        QVERIFY2(report.error.contains("API key"), qPrintable(report.error));
        QVERIFY2(!report.error.contains("ZZZZ-9999"), qPrintable(report.error));
        // QRZ vero la ripete in minuscolo e senza trattini.
        QVERIFY2(!report.error.contains("zzzz9999", Qt::CaseInsensitive), qPrintable(report.error));
        QVERIFY(!server.requests.at(0).form.queryItemValue("OPTION", QUrl::FullyDecoded).contains("MODSINCE"));
    }
};

QTEST_GUILESS_MAIN(TestQslDownload)
#include "tst_qsldownload.moc"
