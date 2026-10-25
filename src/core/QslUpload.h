// DecoDXLog — invio dei QSO ai servizi QSL.
//
// LoTW non si carica con una password: i QSO vanno firmati con il certificato
// dell'operatore, e l'unico programma che lo sa fare e' TQSL. DecoDXLog gli passa un
// file ADIF e legge il codice d'uscita, cosi' il certificato resta dove l'ARRL lo
// vuole e non passa mai da qui. QRZ Logbook ed eQSL invece hanno una API e si
// caricano un QSO alla volta.
//
// Ogni invio dice quanti QSO sono stati accettati, quanti erano gia' li' e quanti
// il servizio ha rifiutato: un duplicato non e' un errore, e non va ritentato
// all'infinito.
#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QVariantList>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QProcess;

namespace decolog::core {

struct QslUploadResult {
    bool    ok{false};
    int     accepted{0};
    int     duplicates{0};
    int     rejected{0};
    QString message;
    QString remoteId;       // QRZ: il numero del QSO nel logbook
    bool    retryLater{false};   // problema di rete o servizio giu': si riprova
};

class AdifRecord;

namespace qsl {

// I servizi di invio automatico condividono un solo controller. La coda
// conserva l'ordine configurato e consegna il servizio successivo soltanto
// quando quello precedente non e' piu' occupato: LoTW non deve far saltare
// eQSL (o gli altri servizi) solo perche' parte per primo.
class AutomaticUploadQueue {
public:
    void reset(const QStringList& services);
    QString takeNext(bool busy);
    void clear() { m_services.clear(); }
    bool isEmpty() const { return m_services.isEmpty(); }

private:
    QStringList m_services;
};

// Il TQSL installato: dal registro di Windows, dalle cartelle solite o dal PATH.
QString findTqsl();
// Il rigctld di Hamlib, cercato allo stesso modo: e' lui che parla alle radio.
QString findRigctld();
// I nomi delle "station location" configurate in TQSL (station_data).
QStringList tqslStationLocations();
// Dove TQSL tiene i suoi dati; vuota se non c'e'.
QString tqslDataDirectory();
// TQSL ha un certificato installato?
bool tqslHasCertificate();

// Il codice d'uscita di TQSL tradotto in risultato.
QslUploadResult resultFromTqslExit(int exitCode, const QString& output, int qsoCount);
// La risposta di logbook.qrz.com (RESULT=OK&LOGID=...).
QslUploadResult parseQrzResponse(const QByteArray& body);
// La pagina di eQSL dopo importADIF.cfm.
QslUploadResult parseEqslResponse(const QByteArray& body);
// Wavelog (e Cloudlog, da cui viene): l'indirizzo dell'API a partire da quello
// del sito scritto dall'operatore ("log.example.org", "https://x.org/wavelog/").
QUrl wavelogApiUrl(const QString& server, const QString& endpoint);
// Un testo senza la chiave (Wavelog la vuole anche nell'indirizzo).
QString withoutKey(QString text, const QString& key);
// La risposta di api/qso: JSON {"status":"created"...} o {"status":"failed","reason":...}.
QslUploadResult parseWavelogResponse(int status, const QByteArray& body);
// L'elenco delle stazioni di api/station_info: id, nome, nominativo, attiva.
QVariantList parseWavelogStations(const QByteArray& body, QString* error);
// La risposta di HRDLog.net (NewEntry.aspx): XML con <insert> o <error>.
QslUploadResult parseHrdLogResponse(const QByteArray& body);
// La risposta di Club Log: `status` e' il codice HTTP, 0 se non e' arrivato.
QslUploadResult parseClubLogResponse(int status, const QByteArray& body, int qsoCount);

// CRX Logbook (crx.cloud): un QSO si manda come JSON, dentro {"req": {...}}.
// `remoteId` e' il numero che CRX ha dato al QSO la prima volta: con quello si
// corregge invece di crearne un doppione; 0 per un QSO nuovo. `localId` e' il
// numero del QSO qui: va nel campo personalizzato 38 di CRX, come suggerito da
// CRX stesso, cosi' dal sito si risale al QSO di DecoDXLog.
QJsonObject crxQsoData(const AdifRecord& record, qint64 logId, qint64 remoteId, qint64 localId = 0);
// Dove sta un QSO su CRX, come lo tiene qsl_status: "log:qso" ("123:111354").
QString crxRemoteKey(qint64 logId, const QString& qsoId);
// Il numero CRX del QSO se sta in quel log, 0 altrimenti (in un altro log e'
// un QSO nuovo). I numeri vecchi, senza log, erano del log scelto allora.
qint64 crxRemoteQso(const QString& remoteKey, qint64 logId);
// La risposta a edit_myqso: {"success": true, "qso_id": ...} o {"error": ...}.
QslUploadResult parseCrxResponse(int status, const QByteArray& body);
// Cosa dire quando CRX rifiuta la chiave, e dove si rifa'.
QString crxKeyRefusedMessage(const QString& error);
// I logbook dell'account: {id, name, call, description}.
QVariantList parseCrxLogs(const QByteArray& body, QString* error);

} // namespace qsl

// Quello che Club Log vuole sapere a ogni invio. La chiave API e' della
// applicazione, la password e' dell'operatore: due cose diverse.
struct ClubLogAuth {
    QString email;
    QString password;
    QString callsign;
    QString apiKey;

    bool complete() const
    {
        return !email.isEmpty() && !password.isEmpty() && !callsign.isEmpty() && !apiKey.isEmpty();
    }
};

// Manda un file ADIF a LoTW facendolo firmare a TQSL.
class TqslUploader : public QObject {
    Q_OBJECT

public:
    explicit TqslUploader(QObject* parent = nullptr);

    void setProgram(const QString& path) { m_program = path; }
    QString program() const { return m_program; }
    bool busy() const { return m_busy; }

    // `adifPath` e' un file temporaneo con i QSO da caricare; `location` e' la
    // station location di TQSL (vuota: quella predefinita, se TQSL ne ha una sola).
    void upload(const QString& adifPath, const QString& location, int qsoCount);
    void cancel();

signals:
    void finished(const decolog::core::QslUploadResult& result);

private:
    QString m_program;
    bool    m_busy{false};
    QProcess* m_process{nullptr};
};

// QRZ Logbook ed eQSL: un QSO per volta, con la chiave o la password del servizio.
class WebQslUploader : public QObject {
    Q_OBJECT

public:
    enum class Service { QrzLogbook, Eqsl, ClubLog, Crx, HrdLog, Wavelog };

    explicit WebQslUploader(QObject* parent = nullptr);

    // Per i test: server finti al posto di logbook.qrz.com e www.eqsl.cc.
    void setEndpoints(const QUrl& qrz, const QUrl& eqsl);
    void setClubLogEndpoints(const QUrl& realtime, const QUrl& batch);
    bool busy() const { return m_busy; }

    // `credentials`: per QRZ la chiave API; per eQSL utente e password.
    void uploadQrz(const QString& apiKey, const QString& adifRecord);
    void uploadEqsl(const QString& user, const QString& password, const QString& adifRecord);
    // HRDLog.net: il nominativo, il codice di upload del profilo e un QSO.
    void uploadHrdLog(const QString& callsign, const QString& uploadCode, const QString& adifRecord);
    void setHrdLogEndpoint(const QUrl& url) { m_hrdLogUrl = url; }
    // Club Log prende tutto il blocco in una volta: un QSO solo passa da
    // realtime.php, il resto dal caricamento normale di un file ADIF.
    void uploadClubLog(const ClubLogAuth& auth, const QByteArray& adifDocument, int qsoCount);
    // CRX Logbook: la chiave API dell'operatore e il QSO gia' tradotto.
    void uploadCrx(const QString& apiKey, const QJsonObject& qsoData);
    // Toglie un QSO da CRX: edit_myqso con action "delete", come fa HAMPI.
    void deleteCrx(const QString& apiKey, qint64 remoteQsoId);
    // L'elenco dei logbook dell'account CRX, per scegliere dove scrivere.
    void listCrxLogs(const QString& apiKey);
    void setCrxEndpoint(const QUrl& url) { m_crxUrl = url; }
    // Wavelog: il sito dell'operatore, la chiave API, la stazione e un QSO.
    void uploadWavelog(const QString& server, const QString& apiKey, const QString& stationId,
                       const QString& adifRecord);
    void listWavelogStations(const QString& server, const QString& apiKey);

signals:
    void finished(const decolog::core::QslUploadResult& result);
    void crxLogsListed(const QVariantList& logs, const QString& error);
    void wavelogStationsListed(const QVariantList& stations, const QString& error);

private:
    void send(Service service, const QUrl& url, const QByteArray& body);
    void watch(QNetworkReply* reply, Service service, int qsoCount);

    QNetworkAccessManager* m_net;
    QUrl m_qrzUrl{QStringLiteral("https://logbook.qrz.com/api")};
    QUrl m_eqslUrl{QStringLiteral("https://www.eqsl.cc/qslcard/importADIF.cfm")};
    QUrl m_clubLogRealtimeUrl{QStringLiteral("https://clublog.org/realtime.php")};
    QUrl m_clubLogBatchUrl{QStringLiteral("https://clublog.org/putlogs.php")};
    QUrl m_crxUrl{QStringLiteral("https://s.crx.cloud/api/")};
    QUrl m_hrdLogUrl{QStringLiteral("https://robot.hrdlog.net/NewEntry.aspx")};
    bool m_busy{false};
};

} // namespace decolog::core
