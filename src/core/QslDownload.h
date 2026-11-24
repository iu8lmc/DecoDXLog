// DecoDXLog — le conferme che arrivano da eQSL e da QRZ.
//
// LoTW ha il suo scarico (Lotw.h). Qui gli altri due servizi che confermano:
//
//  - eQSL: DownloadInBox.cfm prepara un file ADIF con le eQSL ricevute e
//    risponde con una pagina HTML che contiene il link al file ("Your ADIF log
//    file has been built"). Si scarica il file, e ogni eQSL ricevuta e' una
//    conferma del QSO con quel nominativo, a quell'ora, su quella banda.
//  - QRZ Logbook: l'API (ACTION=FETCH, OPTION=STATUS:CONFIRMED) restituisce i
//    QSO del proprio logbook su QRZ che l'altra stazione ha confermato
//    (APP_QRZLOG_STATUS = C), a pagine, con l'ADIF scritto come &lt;call:4&gt;.
//
// In tutti e due i casi le conferme si segnano con LogDatabase::applyConfirmation,
// come quelle di LoTW: stesso nominativo, stessa banda, stesso gruppo di modi,
// entro mezz'ora.
#pragma once

#include "core/Adif.h"

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantMap>

class QNetworkAccessManager;
class QNetworkReply;

namespace decolog::core {

namespace confirmations {

// Bounded local identifiers for a past ambiguous download, not current QSO state.
QVariantMap ambiguousCandidateSummary(const QList<qint64>& candidates);

struct Report {
    bool ok{false};
    QString service;             // "eqsl" o "qrz"
    QString error;               // gia' tradotto
    QList<AdifRecord> confirmations;   // con QSLRDATE, pronte per applyConfirmation
};

// eQSL: dalla pagina di DownloadInBox il link al file .adi. Vuoto, con
// l'errore scritto dalla pagina, se il file non c'e'.
QUrl eqslFileLink(const QByteArray& html, const QUrl& page, QString* error);

// GeteQSL.cfm: la pagina con l'indirizzo dell'immagine della cartolina
// ("<IMG SRC=..."). Vuoto, con l'errore scritto dalla pagina, se non c'e'.
QUrl eqslCardImage(const QByteArray& html, const QUrl& page, QString* error);

// Le eQSL ricevute del file .adi, come conferme (QSLRDATE = EQSL_QSLRDATE).
// Le segnalazioni SWL non sono QSO e restano fuori.
QList<AdifRecord> eqslConfirmations(const QByteArray& adif);

// QRZ: una pagina di risposta di FETCH.
struct QrzPage {
    bool ok{false};
    QString error;
    int count{0};
    QList<AdifRecord> records;
    qint64 lastLogId{0};         // il piu' alto APP_QRZLOG_LOGID della pagina
};
QrzPage parseQrzFetch(const QByteArray& body);

// Un testo senza il segreto: QRZ ripete la chiave nel motivo dell'errore, in
// minuscolo e senza trattini ("invalid api key aaaabbbbccccdddd"). Si toglie in
// tutte e due le forme.
QString withoutSecret(QString text, const QString& secret);

// Solo i QSO confermati (APP_QRZLOG_STATUS = C), come conferme
// (QSLRDATE = APP_QRZLOG_QSLDATE).
QList<AdifRecord> qrzConfirmations(const QList<AdifRecord>& records);

// Le conferme dei soli QSO fatti dal `from` al `to`, estremi compresi (una
// delle due date puo' mancare: dal primo QSO, o fino a oggi). Un QSO senza
// data valida non puo' stare in un periodo e resta fuori.
QList<AdifRecord> inPeriod(const QList<AdifRecord>& records, const QDate& from, const QDate& to);

// Da quando chiedere le conferme: dall'ultimo scarico riuscito meno un giorno
// (quello che arrivava mentre si scaricava), tutto se non ce n'e' uno o se si
// chiede tutto.
QDateTime downloadSince(const QDateTime& lastSuccess, bool everything);
// A partial date range or a database failure must not advance the full-sync cursor.
bool mayAdvanceCursor(bool ranged, int databaseErrors);
bool mayAdvanceLotwCursor(bool ranged, int databaseErrors, int ambiguous, int invalid);
QString lotwCursorAfterSync(const QString& previous, const QString& lastQsl, bool full, bool ranged,
                            int databaseErrors, int ambiguous, int invalid);

// Lo scarico automatico ogni `hours` ore: tocca se non si e' mai provato, o
// se dall'ultimo tentativo (riuscito o no) sono passate le ore. Un tentativo
// fallito non si ripete a ogni controllo, ma al giro dopo.
bool autoDownloadDue(const QDateTime& lastSuccess, const QDateTime& lastAttempt, const QDateTime& now, int hours,
                     bool enabled = true);

} // namespace confirmations

class ConfirmationDownloader : public QObject {
    Q_OBJECT

public:
    explicit ConfirmationDownloader(QObject* parent = nullptr);

    // Per le prove: server finti al posto di www.eqsl.cc e logbook.qrz.com.
    void setEndpoints(const QUrl& eqslInbox, const QUrl& qrzApi);
    bool busy() const { return m_reply != nullptr; }

    // Le eQSL ricevute da `since` (vuoto = tutte). Con `from`/`to` solo quelle
    // dei QSO fatti in quel periodo (come LoTW): eQSL filtra per data del QSO,
    // e il filtro si rifa' qui, per non fidarsi.
    void downloadEqsl(const QString& user, const QString& password, const QDateTime& since = {},
                      const QDate& from = {}, const QDate& to = {});
    // Le conferme di QRZ dei QSO modificati da `since` (non valida = tutte).
    // Con `from`/`to` solo quelle dei QSO fatti in quel periodo: si scarica
    // tutto quello che QRZ ha confermato e si tiene il periodo.
    void downloadQrz(const QString& apiKey, const QDate& since = {}, const QDate& from = {}, const QDate& to = {});
    void cancel();

    static constexpr int kQrzPage = 250;

signals:
    void finished(const decolog::core::confirmations::Report& report);

private:
    void finish(confirmations::Report report);
    void fetchQrzPage();

    QNetworkAccessManager* m_net;
    QNetworkReply* m_reply{nullptr};
    QUrl m_eqslUrl{QStringLiteral("https://www.eqsl.cc/qslcard/DownloadInBox.cfm")};
    QUrl m_qrzUrl{QStringLiteral("https://logbook.qrz.com/api")};
    // Lo scarico di QRZ in corso, pagina dopo pagina.
    QString m_qrzKey;
    QDate m_qrzSince;
    QDate m_qrzFrom;
    QDate m_qrzTo;
    qint64 m_qrzAfter{0};
    int m_qrzPages{0};
    QList<AdifRecord> m_qrzConfirmations;
};

// La cartolina di una eQSL ricevuta, come immagine: GeteQSL.cfm dice dove sta,
// poi la si scarica e la si tiene in una cartella. eQSL chiede di non
// chiederne piu' di sei al minuto, una alla volta, e di non scaricare tutta la
// casella: qui si chiede solo quella che l'operatore guarda, una ogni dieci
// secondi, e quella gia' scaricata si riprende dalla cartella.
class EqslCardFetcher : public QObject {
    Q_OBJECT

public:
    struct Request {
        QString user;
        QString password;
        QString call;          // chi ha mandato l'eQSL
        QDateTime on;          // UTC
        QString band;          // "20m"
        QString mode;          // il modo come lo scrive eQSL
    };

    explicit EqslCardFetcher(QObject* parent = nullptr);
    void setEndpoint(const QUrl& url) { m_url = url; }
    void setCacheDir(const QString& dir) { m_dir = dir; }
    void setMinimumGapMs(int ms) { m_gapMs = ms; }

    // La chiave della cartolina (non dipende dalla password).
    static QString keyOf(const Request& r);
    // Il file gia' scaricato, o vuoto.
    QString cachedFile(const QString& key) const;
    // Chiede la cartolina: subito se c'e' gia', altrimenti al suo turno. Se
    // ne arriva un'altra prima, quella in attesa si lascia stare.
    void fetch(const Request& r);

signals:
    void ready(const QString& key, const QString& file, const QString& error);

private:
    void startNext();
    void done(const QString& key, const QString& file, const QString& error);

    QNetworkAccessManager* m_net;
    QUrl m_url{QStringLiteral("https://www.eqsl.cc/qslcard/GeteQSL.cfm")};
    QString m_dir;
    int m_gapMs{10000};
    bool m_busy{false};
    bool m_hasPending{false};
    Request m_pending;
    qint64 m_lastAsk{0};
};

} // namespace decolog::core
