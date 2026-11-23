// DecoDXLog — la sessione di attivazione o di contest.
//
// Finche' e' aperta: ogni QSO prende la referenza dell'attivatore, il locatore del
// posto, l'etichetta e il numero progressivo; un nominativo gia' lavorato sulla
// stessa banda e nello stesso modo dentro la sessione e' un duplicato, anche se il
// QSO di prima e' di tre ore fa; i conteggi dicono quanto manca perche'
// l'attivazione valga (dieci QSO per POTA, quattro per SOTA).
#pragma once

#include "core/Activation.h"
#include "core/CallHistory.h"
#include "core/ContestRules.h"

#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QSet>
#include <QVariantMap>
#include <functional>

namespace decolog::core { class LogDatabase; }

namespace decolog::app {

class ActivationController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QVariantMap state READ state NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(int qsoCount READ qsoCount NOTIFY changed)
    Q_PROPERTY(int uniqueCalls READ uniqueCalls NOTIFY changed)
    Q_PROPERTY(int requiredQsos READ requiredQsos NOTIFY changed)
    Q_PROPERTY(int nextSerial READ nextSerial NOTIFY changed)
    Q_PROPERTY(QVariantList perBand READ perBand NOTIFY changed)
    Q_PROPERTY(QString lastQso READ lastQso NOTIFY changed)
    Q_PROPERTY(QString elapsed READ elapsed NOTIFY changed)
    Q_PROPERTY(QStringList kinds READ kinds CONSTANT)

public:
    struct Context {
        core::LogDatabase* db{nullptr};
        std::function<QString()> stationCall;
        std::function<QString()> stationGrid;
        std::function<qint64()> activeProfileId;
        std::function<void(const QString& category, const QString& text, const QString& level)> activity;
        std::function<void()> logChanged;
        // Dove sta la propria stazione: paese, continente e zone. Serve a sapere
        // quanto vale un QSO, che nei contest dipende da dove sono i due.
        std::function<core::ContestStation()> station;
        // Il paese e le zone di un nominativo, secondo il cty.csv.
        std::function<core::ContestStation(const QString& call)> locate;
        // Il nome di un'entita' DXCC, per le finestre dei moltiplicatori.
        std::function<QString(int dxcc)> dxccName;
        // Il progressivo da mandare quando e' condiviso sulla rete.
        std::function<int(int localNext)> takeSerial;
        std::function<void(bool on)> shareSerials;
    };

    explicit ActivationController(Context context, QObject* parent = nullptr);

    // Da chiamare quando il log e' aperto: la sessione sopravvive a una chiusura
    // del programma (sta nel database, non nelle impostazioni: e' del log).
    void load();

    bool active() const { return m_session.active; }
    QVariantMap state() const;
    QString title() const { return m_session.title(); }
    int qsoCount() const { return m_qsoCount; }
    int uniqueCalls() const { return m_uniqueCalls; }
    int requiredQsos() const { return m_session.requiredQsos(); }
    int nextSerial() const { return m_session.nextSerial; }
    QVariantList perBand() const { return m_perBand; }
    QString lastQso() const { return m_lastQso; }
    QString elapsed() const;
    QStringList kinds() const;

    // {kind, reference, name, contestId, myGrid, tag, band, mode, serialEnabled, nextSerial}
    Q_INVOKABLE QString start(const QVariantMap& session);
    Q_INVOKABLE QString update(const QVariantMap& session);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void refresh();
    // Gli id dei QSO della sessione, dal primo.
    Q_INVOKABLE QVariantList qsoIds() const;
    // Read-only grouped view of QSO reporting the active session UUID.
    Q_INVOKABLE QVariantMap partitionPreview() const;
    // Nome del file suggerito per l'invio (POTA e compagnia).
    Q_INVOKABLE QString suggestedFileName() const;
    // I contest che il programma conosce (quelli dell'enumerazione ADIF piu' i
    // contest ARI), filtrati per testo: {id, name, label}. Serve a scegliere
    // invece di scrivere a memoria un CONTEST_ID.
    Q_INVOKABLE QVariantList contests(const QString& search = {}) const;
    // Il nome di un identificativo, vuoto se non e' uno di quelli conosciuti.
    Q_INVOKABLE QString contestName(const QString& id) const;
    // Punti, moltiplicatori e punteggio della sessione, secondo il regolamento
    // del contest: {valid, points, multipliers, score, exchangeLabel, source,
    // bands: [{band, qsos, points, multipliers}]}. `valid` falso vuol dire che
    // di quel contest non si sanno le regole, e allora non si inventa un numero.
    Q_INVOKABLE QVariantMap score() const;
    // Lo scambio ricevuto ha la forma che il contest vuole? Vuoto se va bene.
    Q_INVOKABLE QString checkExchange(const QString& exchange) const;
    // Quanto vale questo spot per il contest aperto: {points, newMultiplier,
    // duplicate, label}. Serve al cluster, per far vedere subito quali spot
    // portano un moltiplicatore che non si ha ancora.
    // I moltiplicatori della gara, banda per banda: {bands: [...], kinds: [{kind,
    // label, perBand, rows: [{value, name, worked: {banda: true}, any}]}]}. Per le
    // zone e le province ci sono anche quelle che mancano, per vederle.
    Q_INVOKABLE QVariantMap multiplierMatrix() const;
    // Per un nominativo: i suoi moltiplicatori, dove sono gia' lavorati e dove
    // mancano. [{label, worked: [bande], needed: [bande], newHere}].
    Q_INVOKABLE QVariantList multiplierCheck(const QString& call, const QString& band) const;
    Q_INVOKABLE QVariantMap spotValue(const QString& call, const QString& band,
                                      const QString& mode) const;
    // Vero quando il contest aperto si fa in telegrafia: la finestra CW serve
    // solo allora.
    Q_INVOKABLE bool isCwContest() const;
    // Lo scambio che questa stazione probabilmente mandera', da mettere nel
    // campo mentre si scrive il nominativo: {value, from}. `from` e' "log" se
    // viene da un QSO gia' fatto con lei, "cty" se dal paese (le zone), vuoto
    // se non si puo' sapere (il progressivo, una sezione mai lavorata).
    Q_INVOKABLE QVariantMap suggestExchange(const QString& call) const;
    // Il file "call history" (formato N1MM) della gara aperta: "" se e' andata.
    Q_INVOKABLE QString loadCallHistory(const QUrl& file);
    Q_INVOKABLE void clearCallHistory();
    // Le regole di questa edizione: la scheda piu' quello che si e' cambiato.
    Q_INVOKABLE QVariantMap editionRules() const;
    // Il log e' cambiato fuori dalla sessione (un QSO corretto, cancellato,
    // importato): il punteggio in memoria non vale piu'.
    void invalidateScore() { m_score.valid = false; }
    // Scrive l'ADIF della sessione. Restituisce un messaggio d'errore, o "".
    Q_INVOKABLE QString exportAdif(const QUrl& file);
    // Scrive il Cabrillo del contest. `info` sono le righe della testata:
    // {contest, callsign, categoryOperator, categoryPower, categoryMode,
    //  categoryBand, categoryAssisted, gridLocator, location, club, name,
    //  address, email, operators, claimedScore, soapbox}. Torna "" se e' andata.
    Q_INVOKABLE QString exportCabrillo(const QUrl& file, const QVariantMap& info);
    // La testata gia' riempita con quello che il programma sa.
    Q_INVOKABLE QVariantMap cabrilloDefaults() const;

    // ── Finestra contest ────────────────────────────────────────────────────
    // Gli ultimi QSO della sessione, dal piu' recente.
    Q_INVOKABLE QVariantList recentQsos(int limit = 12) const;
    // QSO all'ora sugli ultimi 10 e 60 minuti, moltiplicatori, ultimo scambio.
    Q_INVOKABLE QVariantMap rate() const;
    // Questo nominativo e' gia' stato lavorato in questa sessione, su questa
    // banda e in questo modo?
    Q_INVOKABLE bool wouldDuplicate(const QString& call, const QString& band, const QString& mode) const
    {
        return isDuplicate(call, band, mode);
    }

    // ── Usate dal controller principale ─────────────────────────────────────
    const core::Activation& session() const { return m_session; }
    // Il QSO sarebbe un duplicato di questa attivazione?
    bool isDuplicate(const QString& call, const QString& band, const QString& mode) const;
    // Aggiunge al record quello che la sessione impone.
    void applyTo(core::AdifRecord& record) const;
    // Un QSO e' stato scritto: numero progressivo avanti e conteggi aggiornati.
    void qsoLogged();

signals:
    void changed();

private:
    // Il punteggio e i moltiplicatori gia' presi, calcolati una volta per QSO
    // e non una volta per ogni riquadro che li mostra: in gara li leggono
    // quattro finestre e ogni riga del cluster, e ricalcolarli ogni volta
    // voleva dire secondi di programma fermo a ogni QSO.
    struct ScoreCache {
        bool valid{false};
        QVariantMap score;
        QSet<QString> multipliers;
    };
    mutable ScoreCache m_score;
    void buildScore() const;
    // I QSO della sessione come servono al conto, letti con una query sola.
    QList<core::ContestQso> sessionQsos() const;
    // Le chiavi dei moltiplicatori gia' in tasca, per sapere quali mancano.
    QSet<QString> workedMultipliers() const;
    void save();
    bool persistTransition(const core::Activation& next, const QString& event);
    void recount();
    // La scheda della gara aperta con le modifiche di questa edizione.
    core::ContestRules currentRules() const;
    // Lo scambio dal log o dal paese, senza il call history.
    QVariantMap suggestFromLog(const QString& call) const;
    void loadHistoryOfSession();
    core::CallHistory m_history;
    mutable int m_lastSerial{0};

    Context m_ctx;
    core::Activation m_session;
    int m_qsoCount{0};
    int m_uniqueCalls{0};
    QVariantList m_perBand;
    QString m_lastQso;
};

} // namespace decolog::app
