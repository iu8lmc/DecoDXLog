// DecoDXLog — il log su SQLite.
//
// Le colonne hanno i nomi ADIF (db/schema.sql). Tutto quello che arriva in ADIF e
// non ha una colonna finisce in qso.adif_extra come JSON: un import seguito da
// un export restituisce gli stessi campi con gli stessi valori. Gli stati QSL
// (LoTW, eQSL, cartolina, Club Log, QRZ) vanno nella tabella qsl_status e
// tornano campi ADIF all'export.
#pragma once

#include "core/Adif.h"

#include <QDateTime>
#include <QByteArray>
#include <QJsonArray>
#include <QList>
#include <QSet>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <functional>
#include <optional>

namespace decolog::core {

struct InsertResult {
    enum class Status { Inserted, Duplicate, Invalid, Error };
    Status  status{Status::Error};
    qint64  id{0};          // la riga scritta, o quella gia' presente se Duplicate
    QString message;
};

struct ImportResult {
    int read{0};
    int inserted{0};
    int duplicates{0};
    int invalid{0};
    int missingUtc{0};
    int qslRecords{0};
    QStringList preservedExtraFields;
    QList<qint64> insertedIds;
    QStringList errors;     // i primi problemi, per mostrarli all'operatore
};

struct EnrichmentEntry {
    qint64 qsoId{0};
    QString field;
    QString previousValue;
    QString value;
    QString source;
    QString kind; // received | declared | inferred
    QString acquiredAt;
    QString validFrom;
    QString validTo;
};

struct BulkEditResult {
    int changed{0};
    int unchanged{0};       // avevano gia' quel valore, o il campo pieno con "solo dove e' vuoto"
    int failed{0};
    QStringList errors;     // i primi, con il nominativo
    QList<qint64> changedIds;
};

struct ApiIdempotencyRecord {
    QByteArray requestBody;
    int status{0};
    QByteArray contentType;
    QByteArray responseBody;
    QString createdAt;
};

struct ConfirmationResult {
    enum class Status { Confirmed, AlreadyConfirmed, Ambiguous, NotFound, Invalid, Error };
    Status  status{Status::NotFound};
    qint64  id{0};
    QList<qint64> candidates;
    QString message;
};

struct WorkedEntry {
    QDateTime on;
    QString   band;
    QString   mode;         // il sottomodo se c'e': FT2, non MFSK
    QString   lotwRcvd;
};

struct WorkedBefore {
    int         count{0};
    QStringList bands;
    QStringList modes;
    QDateTime   last;
    QString     lastBand;
    QString     lastMode;
    QString     name;
    QString     qth;
    QString     gridsquare;
    QString     country;
    QString     state;          // stato USA, prefettura giapponese, provincia
    int         dxcc{0};
    int         cqz{0};
    int         ituz{0};
    qint64      lastId{0};
    QList<WorkedEntry> recent;   // le ultime cinque
};

struct QslState {
    QString service;        // lotw | qrz | clublog | eqsl | card
    QString sent{QStringLiteral("N")};
    QString sentDate;       // yyyyMMdd come in ADIF
    QString rcvd{QStringLiteral("N")};
    QString rcvdDate;
    QString remoteId;
    QString lastError;
    QString via;            // solo le cartacee: B bureau, D diretta, E elettronica
};

struct QslAttachment {
    qint64 id{0};
    QString sha256;
    QString filename;
    QString mime;
    qint64 size{0};
    QString createdAt;
};

struct StationProfile {
    qint64  id{0};
    QString uuid;
    QString name;
    QString stationCallsign;
    QString operatorCall;
    QString myGridsquare;
    int     myCqZone{0};
    int     myItuZone{0};
    int     myDxcc{0};
    QString myRig;
    QString myAntenna;
    double  defaultTxPwr{0.0};
    QString lotwStationLocation;
    QString awardAccount;
    QString operatingLocation;
    QString validFrom;
    QString validTo;
    qint64  operatorProfileId{0};
    qint64  operatingLocationId{0};
    int     dxccEntityId{0};
    bool    isDefault{false};
    bool    deleted{false};
    bool    dirty{true};
    int     revision{1};
    int     qsoCount{0};
};

struct HistoryEntry {
    qint64    id{0};
    int       revision{0};
    QString   reason;
    QDateTime recordedAt;
    AdifRecord record;
};

struct QsoMeta {
    QString uuid;
    int     revision{0};
    QString source;
    QString sourceApp;
    QString createdAt;
    QString updatedAt;
    bool    dirty{false};
    bool    deleted{false};
    qint64  stationProfileId{0};
};

struct CountRow {
    QString key;
    int     count{0};
};

// Su cosa contare le statistiche: un modo (FT2, FT8, CW, SSB...), un anno, una
// banda. Vuoto o zero vuol dire "tutto".
struct StatsFilter {
    QString mode;
    QString band;
    int     year{0};
    // I 60 metri nel DXCC anno per anno: come per i diplomi (AwardFilter).
    bool    count60m{false};
};

struct Ft2Award {
    int qsos{0};
    int dxccWorked{0};
    int dxccConfirmed{0};   // confermati su LoTW
    int gridsWorked{0};
    int gridsConfirmed{0};
};

class LogDatabase {
public:
    LogDatabase();
    ~LogDatabase();
    LogDatabase(const LogDatabase&) = delete;
    LogDatabase& operator=(const LogDatabase&) = delete;

    // Crea il file e lo schema se mancano. ":memory:" per i test.
    bool open(const QString& path);
    void close();
    bool isOpen() const;
    QString path() const { return m_path; }
    QString lastError() const { return m_lastError; }
    int schemaVersion() const;

    QSqlDatabase connection() const;

    std::optional<ApiIdempotencyRecord> apiIdempotency(const QString& key) const;
    bool saveApiIdempotency(const QString& key, const ApiIdempotencyRecord& record);

    // Finestra dei duplicati, in secondi: per i QSO dai programmi digitali e per
    // quelli scritti a mano (l'ora a mano e' approssimativa).
    void setDedupWindows(int digitalSeconds, int manualSeconds);
    int dedupWindowSeconds(bool manual) const { return manual ? m_dedupManual : m_dedupDigital; }

    // ── QSO ──────────────────────────────────────────────────────────────────
    InsertResult insertQso(const AdifRecord& record,
                           const QString& source,
                           const QString& sourceApp = {},
                           bool manual = false,
                           qint64 stationProfileId = 0,
                           bool checkDuplicate = true);

    // Riscrive un QSO: la versione precedente va in qso_history, revision sale di
    // uno, dirty torna a 1. `stationProfileId` < 0 lascia il profilo com'e'.
    InsertResult updateQso(qint64 id, const AdifRecord& record,
                           qint64 stationProfileId = -1,
                           const QString& reason = QStringLiteral("edit"));

    // Cancellazione morbida: la riga resta, marcata deleted, e lo storico la
    // conserva.
    bool softDeleteQso(qint64 id);
    bool recordEnrichment(const QList<EnrichmentEntry>& entries);
    QList<EnrichmentEntry> enrichments(qint64 qsoId) const;
    bool undoEnrichment(qint64 qsoId, const QString& field);

    // Lo stesso valore in un campo di molti QSO, ognuno con la sua revisione
    // nello storico ("bulk"): una correzione in blocco si disfa QSO per QSO.
    // `onlyEmpty`: solo dove il campo e' vuoto. Un valore vuoto svuota il campo.
    // Uno stato QSL messo a Y prende la data di oggi, se non ne ha una. Il campo
    // "@profile" e' il profilo di stazione (il valore e' il suo id). A blocchi
    // di 500 QSO, ognuno nella sua transazione, e dopo ogni blocco `progress`.
    BulkEditResult bulkEdit(const QList<qint64>& ids, const QString& field, const QString& value, bool onlyEmpty,
                            const std::function<void(int done, int total)>& progress = {});

    // I doppioni: stesso nominativo, banda, gruppo di modo (CW, fonia, digitali)
    // e profilo di stazione, e ognuno entro `windowSeconds` dal QSO prima. Un
    // gruppo per lista, gli id in ordine di tempo; al massimo `limit` gruppi.
    QList<QList<qint64>> duplicateGroups(int windowSeconds, int limit = 5000) const;

    // Unisce dei doppioni in `keep`: prende dagli altri i campi che gli mancano,
    // le conferme (una Y vince), le etichette e il riferimento remoto dove lui
    // non ne ha; gli altri si cancellano (morbido, restano nello storico).
    InsertResult mergeQsos(qint64 keep, const QList<qint64>& others);

    // Il log conosce gia' questo QSO? Stessa banda, ora entro la finestra dei
    // doppioni digitali, e lo stesso nominativo: adesso o in una versione
    // precedente, anche se poi e' stato cancellato. Per chi recupera QSO da un
    // altro log: un QSO corretto o cancellato qui non deve tornare.
    bool knowsQso(const AdifRecord& record) const;

    // Gli ultimi QSO (non cancellati), del nominativo se c'e', dal piu' recente.
    QList<qint64> latestIds(const QString& call, int limit) const;

    // Il QSO (non cancellato) che ha `value` nel campo ADIF senza colonna
    // `field` (in adif_extra), cercato vicino a `near` (un giorno prima e dopo)
    // se l'ora e' valida: l'ID di N1MM, il numero di un altro log.
    std::optional<qint64> findByExtra(const QString& field, const QString& value, const QDateTime& near = {}) const;

    // Il record ADIF completo di un QSO: colonne, stati QSL e adif_extra.
    std::optional<AdifRecord> record(qint64 id) const;
    std::optional<QsoMeta> meta(qint64 id) const;
    QList<QslState> qslStatus(qint64 id) const;
    QList<HistoryEntry> history(qint64 id) const;
    // Rimette in vigore una versione dello storico (diventa una nuova revisione).
    InsertResult restoreRevision(qint64 id, qint64 historyId);

    // Una conferma arrivata da un servizio ("lotw", "qrz", "eqsl"): il record ha
    // CALL, BAND (o FREQ), MODE/SUBMODE, QSO_DATE, TIME_ON e QSLRDATE, piu' i
    // dettagli che il servizio conosce (GRIDSQUARE, CQZ, ITUZ, DXCC, STATE, CNTY,
    // IOTA). Il QSO si cerca per nominativo, banda, gruppo di modi e ora entro
    // `windowSeconds` (LoTW tollera mezz'ora); diventa confermato con una nuova
    // revisione, e i dettagli riempiono solo i campi vuoti.
    // `onlyProfiles` e `exceptProfiles` limitano i QSO a quelli di alcuni profili
    // di stazione (0 = senza profilo): un account di un profilo conferma solo i
    // suoi QSO, quello generale tutti gli altri.
    ConfirmationResult applyConfirmation(const QString& service, const AdifRecord& confirmation,
                                         int windowSeconds = 1800, const QList<qint64>& onlyProfiles = {},
                                         const QList<qint64>& exceptProfiles = {});

    ImportResult importAdif(const QByteArray& data, const QString& source = QStringLiteral("import"),
                            qint64 stationProfileId = 0);
    // Lo stesso, a blocchi di `batchSize` QSO, ognuno nella sua transazione, e
    // dopo ogni blocco `progress(fatti, totale)`. Fra un blocco e l'altro chi
    // scrive un QSO dalla radio passa: un'importazione da un milione di QSO
    // non tiene il log chiuso per minuti.
    using ImportProgress = std::function<void(int done, int total)>;
    ImportResult importAdif(const QByteArray& data, const QString& source, qint64 stationProfileId,
                            const ImportProgress& progress, int batchSize);
    QByteArray   exportAdif(const QString& programVersion = {}) const;
    QByteArray   exportAdif(const QList<qint64>& ids, const QString& programVersion = {}) const;

    int qsoCount() const;
    // I QSO fatti dal … al … (yyyy-MM-dd, in UTC, estremi compresi: «fino al»
    // comprende tutto quel giorno). Un estremo vuoto e' aperto: dal primo QSO,
    // o fino all'ultimo. In ordine di tempo, come li scrive l'export.
    QList<qint64> qsoIdsBetween(const QString& fromIso, const QString& toIso) const;
    int qsoCountBetween(const QString& fromIso, const QString& toIso) const;
    // Cambia ogni volta che il log cambia, da questa connessione o da un'altra
    // (un import o una modifica in blocco su un altro filo), e costa niente:
    // per sapere se vale la pena ricontare qualcosa.
    QString changeStamp() const;
    int dirtyCount() const;
    int conflictCount() const;
    WorkedBefore workedBefore(const QString& call) const;

    // Bande e modi (FT2, non MFSK) in cui un'entita' DXCC e' gia' stata lavorata.
    struct DxccWorked { int count{0}; QStringList bands; QStringList modes; };
    DxccWorked dxccWorked(int dxcc) const;

    // Una casella della griglia banda x modo: quanti QSO ci sono dentro e come
    // sono confermati. E' quello che serve per rispondere alla domanda vera
    // mentre si chiama: "questo, su questa banda, mi manca?".
    struct BandModeSlot {
        QString band;
        QString group;        // CW | PHONE | DATA
        int     count{0};
        bool    lotw{false};
        bool    eqsl{false};
        bool    card{false};
        bool    clublog{false};
        bool    qrz{false};
        bool confirmed() const { return lotw || eqsl || card || clublog || qrz; }
    };
    // Le caselle di un nominativo e quelle di tutta la sua entita' DXCC.
    QList<BandModeSlot> bandModeSlotsForCall(const QString& call) const;
    QList<BandModeSlot> bandModeSlotsForDxcc(int dxcc) const;
    // Tutti i QSO in forma compatta per DecoLink: [call, banda, modo, data yyyyMMdd,
    // locatore a 4, confermato 0/1], con le conferme accettate indicate.
    QList<QJsonArray> workedRows(bool confirmLotw, bool confirmCard, bool confirmEqsl) const;
    // Gli id dei QSO confermati da almeno uno dei servizi scelti, in una query:
    // chi scorre tutto il log li guarda qui invece di chiederli QSO per QSO.
    QSet<qint64> confirmedIds(bool lotw, bool card, bool eqsl) const;
    // Gli id confermati da un servizio solo ("lotw", "card", "eqsl"...).
    QSet<qint64> confirmedIds(const QString& service) const;
    static QJsonArray workedRow(const QString& call, const QString& band, const QString& mode,
                                const QString& submode, const QString& isoOn, const QString& grid, bool confirmed);
    // QSO senza numero DXCC, per completarli dal cty.csv.
    QList<qint64> idsWithoutDxcc() const;

    // QSO senza locatore: quelli che vale la pena chiedere al callbook. Il nome
    // manca a quasi tutti i QSO digitali e non e' un buon motivo per bussare al
    // servizio ventimila volte; il locatore invece serve, e arriva col nome.
    // In ordine dal piu' recente, che e' quello che interessa prima.
    QList<qint64> idsMissingCallbookData(int limit = 0) const;

    // QSO con un campo di testo rovinato da un vecchio import ADIF: la lunghezza
    // contata in byte anziche' in caratteri tagliava il valore a meta' e ci
    // lasciava dentro il pezzo del tag successivo ("Vilnius<GRIDSQ").
    QList<qint64> idsWithDamagedText() const;

    // E' il primo QSO FT2 con il suo DXCC, in ordine di tempo?
    bool isFirstFt2Dxcc(qint64 id) const;
    Ft2Award ft2Award() const;
    QList<CountRow> countByBand() const;
    QList<CountRow> countByMode() const;
    // Solo quali bande e modi ci sono, senza contarli: dall'indice delle bande
    // e dei modi, senza leggere il log. Per i menu dei filtri e le colonne dei
    // diplomi. Anche quelli dei QSO cancellati: in un menu non fanno danno.
    QStringList bandsInLog() const;
    QStringList modesInLog() const;
    // QSO per numero DXCC (chiave = numero), dal piu' lavorato.
    QList<CountRow> countByDxcc() const;
    // Statistiche nel tempo: chiave "2026", "2026-09", "00".."23", continente.
    QList<CountRow> countByYear(const StatsFilter& filter = {}) const;
    QList<CountRow> countByMonth(int months, const StatsFilter& filter = {}) const;
    QList<CountRow> countByHour(const StatsFilter& filter = {}) const;
    QList<CountRow> countByContinent(const StatsFilter& filter = {}) const;
    QList<CountRow> countByBand(const StatsFilter& filter) const;
    QList<CountRow> countByMode(const StatsFilter& filter) const;
    // Banda per ora UTC, per la mappa di calore: [{band, hour, count}].
    QList<QVariantMap> bandByHour(const StatsFilter& filter = {}) const;
    // Le entita' e i nominativi piu' lavorati.
    QList<CountRow> countByEntity(const StatsFilter& filter, int limit) const;
    QList<CountRow> countByCall(const StatsFilter& filter, int limit) const;
    // Banda per modo: [{band, mode, count}].
    QList<QVariantMap> bandByMode(const StatsFilter& filter = {}) const;
    // Come sono cresciuti i diplomi, anno per anno (cumulativo): [{year, dxcc,
    // dxccConfirmed, zones, grids, qsos}]. Confermato = LoTW o cartolina.
    QList<QVariantMap> awardProgress(const StatsFilter& filter = {}) const;
    // Totali: QSO, nominativi, entita', primo e ultimo QSO, giorno e ora migliori.
    QVariantMap statsSummary(const StatsFilter& filter = {}) const;
    // Gli anni presenti nel log, dal piu' recente.
    QStringList yearsInLog() const;
    // Per servizio: in coda (R/Q), inviati (Y), confermati (rcvd Y).
    QList<QVariantMap> qslSummary() const;
    // Locatori a quattro caratteri gia' lavorati, per la mappa.
    QStringList workedGrids(int limit = 4000) const;

    // ── Invio QSL ────────────────────────────────────────────────────────────
    // Lo stato di un servizio su un QSO, senza creare una revisione: l'invio di una
    // QSL non cambia il QSO, cambia quello che se ne e' fatto.
    bool setQslState(qint64 id, const QslState& state);
    bool writeQslState(qint64 id, const QslState& state, bool includeReceived);
    // I QSO ancora da mandare a un servizio (nessuna riga, o "N"/"R"/"Q"), dal piu'
    // vecchio. `limit` 0 = tutti.
    // `since`: solo i QSO da quel giorno in poi (UTC); non valida = tutti.
    QList<qint64> qsosToUpload(const QString& service, int limit = 0, const QDate& since = {}) const;
    // Conta anche le cancellazioni da mandare (CRX).
    int uploadPendingCount(const QString& service, const QDate& since = {}) const;
    // I servizi dove un QSO si corregge e si cancella, non solo si aggiunge:
    // oggi CRX Logbook.
    static QStringList editableServices();
    // Un QSO gia' su quei servizi e poi corretto qui torna in coda ("R"), e
    // partira' come modifica del QSO che hanno gia'.
    void queueRemoteEdit(qint64 id);
    // Il nominativo di un QSO e' stato corretto: quello che LoTW, QRZ, Club Log, HRDLog ed eQSL
    // hanno ricevuto, senza che nessuno lo confermasse, e' il QSO sbagliato, e il giusto non e'
    // mai partito. Quei servizi tornano «da mandare» (R); i confermati restano come sono — una
    // conferma vuol dire che dall'altra parte il nominativo era giusto. Torna i servizi toccati.
    QStringList markSentForResend(qint64 id);
    // I QSO cancellati qui che il servizio ha ancora ("D"): {id, remote_id}.
    QList<QPair<qint64, QString>> remoteDeletions(const QString& service) const;
    // Il servizio non ha piu' il QSO: si dimentica il suo numero.
    bool forgetRemote(qint64 id, const QString& service);

    // ── Sync con DecoDXLog Cloud ───────────────────────────────────────────────
    // I QSO ancora da mandare, dal piu' vecchio. `limit` 0 = tutti.
    QList<qint64> dirtyQsos(int limit = 0) const;
    // Rimette in coda per il Cloud tutto il log: serve dopo aver svuotato il
    // server, quando lassu' non c'e' piu' niente da confrontare.
    int markAllDirty();
    // Il QSO come lo vuole il Cloud: uuid, revisione, le chiavi del confronto e
    // tutti i campi ADIF dentro "fields".
    QVariantMap syncRecord(qint64 id) const;
    qint64 idForUuid(const QString& uuid) const;
    // Il server l'ha preso: esce dalla coda, con la revisione che dice lui
    // (che puo' essere piu' alta, se ha risolto un conflitto).
    bool markSynced(qint64 id, int revision);
    // Il server dice che quel collegamento da lui sta sotto un altro uuid: ci si
    // allinea, o si toglie di mezzo il doppione se quell'uuid c'e' gia'.
    bool adoptUuid(qint64 id, const QString& serverUuid);
    // Un QSO che arriva dal Cloud: si scrive senza rimetterlo in coda.
    enum class RemoteResult { Inserted, Updated, Deleted, Skipped, Failed };
    RemoteResult applyRemote(const QVariantMap& record);
    // Il log non e' solo i QSO: i profili stazione viaggiano come documenti,
    // con la stessa regola di revisione.
    QList<QVariantMap> dirtyProfiles() const;
    bool markProfileSynced(const QString& uuid, int revision);
    RemoteResult applyRemoteProfile(const QVariantMap& document);

    // Dove siamo arrivati col sync di questo account.
    QVariantMap syncState(const QString& account) const;
    void setSyncState(const QString& account, const QVariantMap& values);

    // ── QSL di carta ─────────────────────────────────────────────────────────
    // La coda delle cartacee: `state` e' "queue" (da mandare), "sent", "received"
    // o "all". Ogni riga ha quello che serve a un'etichetta e alla tabella.
    QList<QVariantMap> cardRows(const QString& state, int limit = 0) const;
    // Quante cartoline in coda, inviate, ricevute, e ricevute senza risposta:
    // contate da SQLite, senza leggere le righe.
    QVariantMap cardCounts() const;
    // Lo stato di una cartacea, conferma compresa (setQslState non tocca il
    // ricevuto, perche' per gli altri servizi lo scrive il download).
    bool setCardState(qint64 id, const QslState& state);
    // Allegati ricevuti: il contenuto resta nel database, viene deduplicato
    // per hash e puo' essere collegato a piu' QSO senza copie.
    qint64 addQslAttachment(const QByteArray& data, const QString& filename, const QString& mime,
                            const QList<qint64>& qsoIds);
    QList<QslAttachment> qslAttachments(qint64 qsoId) const;
    QByteArray qslAttachmentData(qint64 attachmentId) const;

    // ── Etichette ────────────────────────────────────────────────────────────
    // Le etichette di un QSO (attivazione, contest, evento, portatile) stanno in
    // APP_DECOLOG_TAGS, separate da virgola: tornano uguali in un export ADIF.
    static QStringList splitTags(const QString& tags);
    static QString joinTags(const QStringList& tags);
    // Tutte le etichette del log con quanti QSO le hanno, dalla piu' usata.
    QList<CountRow> tagCounts() const;
    // Aggiunge o toglie un'etichetta a piu' QSO; ognuno cambiato diventa una
    // nuova revisione. Restituisce quanti QSO sono cambiati.
    int setTag(const QList<qint64>& ids, const QString& tag, bool add);

    // ── Profili stazione ─────────────────────────────────────────────────────
    QList<StationProfile> stationProfiles(bool includeDeleted = true) const;
    std::optional<StationProfile> stationProfile(qint64 id) const;
    // Crea (id 0) o aggiorna. Restituisce l'id, 0 in caso d'errore.
    qint64 saveStationProfile(const StationProfile& profile);
    bool deleteStationProfile(qint64 id);
    // Il profilo che corrisponde a un nominativo di stazione (il predefinito, se piu' d'uno).
    qint64 profileForCallsign(const QString& stationCallsign) const;
    // Risolve l'identita' dichiarata da un QSO ricevuto da un programma esterno.
    // Prima usa STATION_CALLSIGN, poi OPERATOR se identifica un solo profilo.
    // In caso di ambiguita' torna 0: un QSO non deve finire in silenzio nel log
    // di un altro operatore.
    qint64 profileForIdentity(const QString& stationCallsign, const QString& operatorCall) const;

    // ── Impostazioni e manutenzione ──────────────────────────────────────────
    QString setting(const QString& key, const QString& fallback = {}) const;
    void    setSetting(const QString& key, const QString& value);
    // Copia coerente del database, anche mentre e' aperto (VACUUM INTO).
    bool backupTo(const QString& filePath);

    // Data e ora ADIF (QSO_DATE + TIME_ON) in ISO-8601 UTC, come nello schema.
    static QString isoFromAdif(const QString& date, const QString& time);

private:
    struct Prepared;
    std::optional<Prepared> prepare(const AdifRecord& input, InsertResult& error) const;
    bool writeQsl(qint64 qsoId, const QList<QslState>& states, bool keepRemote);
    QString snapshotJson(qint64 id) const;
    bool applySchema();
    bool migrate();
    std::optional<qint64> findDuplicate(const QString& call, const QString& band,
                                        const QString& mode, const QString& submode,
                                        const QDateTime& on, int windowSeconds) const;

    QString m_connectionName;
    QString m_path;
    int m_dedupDigital{120};
    int m_dedupManual{600};
    mutable QString m_lastError;
};

} // namespace decolog::core
