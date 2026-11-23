// DecoDXLog — award calcolati dal log: DXCC, WAZ, WAS, WPX, locatori, IOTA, POTA,
// SOTA, WWFF, WAIP, DCI e FT2.
//
// Nessuna tabella in piu' nel database: gli award si ricalcolano dai QSO, cosi'
// una correzione a un QSO si vede subito ovunque. Per ogni elemento (un'entita',
// una zona, uno stato...) si tiene su quali bande e' stato lavorato e su quali
// confermato, secondo le conferme che l'operatore accetta (LoTW e cartolina per
// default, come ARRL per il DXCC).
#pragma once

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>
#include <QJsonObject>

namespace decolog::core {

class LogDatabase;

namespace awards {

struct AwardClaim {
    enum class State { Draft, Submitted, Verified, Credited, Rejected };

    QString id;
    QString awardId;
    QString accountId; // diploma account/profile aggregation identity
    QString category;  // external provider category, if applicable
    QString ruleVersion;
    QList<qint64> qsoIds;
    State state{State::Draft};
    QDateTime createdAt;
    QDateTime submittedAt;
    QDateTime decidedAt;
    QString outcome;
    QString evidence;
    QString externalProvider;
    QList<qint64> creditedQsoIds;
    QList<qint64> rejectedQsoIds;

    static QString stateName(State state);
    static std::optional<State> stateFromName(const QString& name);
    bool transitionTo(State next, const QDateTime& when, const QString& note = {}, QString* error = nullptr);
    QJsonObject toJson() const;
    static std::optional<AwardClaim> fromJson(const QJsonObject& object, QString* error = nullptr);
};

class AwardClaimStore {
public:
    static QList<AwardClaim> load(const LogDatabase& db, QString* error = nullptr);
    static bool save(LogDatabase& db, const QList<AwardClaim>& claims, QString* error = nullptr);
    // Importa un esito JSON dichiarativo; non considera mai una conferma QSL
    // come credito esterno e conserva gli esiti parziali per singolo QSO.
    static bool reconcileExternalResult(LogDatabase& db, const QByteArray& json,
                                        QString* error = nullptr);
};

struct AwardRulePackage {
    struct Condition {
        QString field;
        QStringList values;
    };
    QString id;
    QString title;
    QString source;
    QString version;
    QDate validFrom;
    QDate validTo;
    QString keyField; // DXCC, STATE, CQZ, GRID4, BAND, CALL_PREFIX
    QList<Condition> conditions; // all conditions must match; data-only, no code
    QString fingerprint;
    int target{0};
    int total{0};

    static std::optional<AwardRulePackage> fromJson(const QByteArray& json, QString* error = nullptr);
    bool isValid(QString* error = nullptr) const;
};

class AwardRuleCatalog {
public:
    bool add(const AwardRulePackage& package, QString* error = nullptr);
    static AwardRuleCatalog loadDirectory(const QString& directory, QStringList* errors = nullptr);
    QList<AwardRulePackage> packages() const { return m_packages; }

private:
    QList<AwardRulePackage> m_packages;
};

// Il prefisso WPX di un nominativo secondo le regole CQ WPX:
// N8BJQ → N8, EA8/OH2XX → EA8, W1AW/4 → W4, LX/DL1ABC → LX0.
QString wpxPrefix(const QString& callsign);

// I 50 stati USA con il nome, per WAS.
const QMap<QString, QString>& usStates();

// Le 110 province italiane del WAIP, con il nome. La chiave e' la sigla
// automobilistica, che e' anche quella che ADIF usa per l'Italia. Ci sono anche
// le sarde soppresse nel 2016 (OG, OT, VS), perche' i QSO di allora sono ancora
// nei log e il WAIP continua a contarle.
const QMap<QString, QString>& italianProvinces();

// La provincia da come la scrivono i log: "NA", "na", "I-NA", "NA Napoli".
// Vuota se non e' una sigla che esiste.
QString italianProvince(const QString& state);

// La regione di una provincia, per i diplomi che contano le regioni: il DCI
// vuole castelli in cinque regioni diverse, e gli endorsement tre castelli per
// ogni regione.
const QMap<QString, QString>& italianRegions();

// Il riferimento DCI (Castelli d'Italia): due lettere di provincia e tre cifre,
// come vuole il regolamento — "PR001". Si accetta come lo scrivono i log —
// "DCI PR-001", "dci pr001" — e si guarda anche nel commento e nelle note,
// perche' li' e' dove finisce quasi sempre. Vuoto se non c'e' niente che
// somigli a un riferimento.
QString dciReference(const QString& sig, const QString& sigInfo,
                     const QString& comment, const QString& notes);

// Il riferimento di un diploma a "special interest group" (IIA, DIFI, DAI...):
// in SIG/SIG_INFO quando il log e' fatto bene ("SIG=IIA, SIG_INFO=LI-001"), o
// nel testo con il nome del diploma davanti ("IIA LI-001"). Torna "LI-001", o
// vuoto se non c'e'.
QString sigReference(const QString& award, const QString& sig, const QString& sigInfo,
                     const QString& comment, const QString& notes);

// Le 20 regioni italiane, per il WAIR.
QStringList italianRegionNames();

// I continenti dell'IARU con il nome, per il WAC. L'Antartide c'e' — chi l'ha
// lavorata vuole vederla — ma il traguardo del diploma resta sei.
const QMap<QString, QString>& continents();

// Le 47 prefetture giapponesi con il nome, per il WAJA. La chiave e' il numero
// a due cifre come lo scrive ADIF ("01" Hokkaido … "47" Okinawa).
const QMap<QString, QString>& japanPrefectures();

// La prefettura da come la scrivono i log: "12", "JA12", "12 Chiba".
QString japanPrefecture(const QString& state);

// Il distretto giapponese (AJD) dalla cifra del nominativo: JA1 → 1, JA0 → 0.
// Vuoto se il nominativo non e' giapponese o non si capisce.
QString japanDistrict(const QString& callsign);

// Il numero JARL che sta nel campo CNTY: le citta' (JCC) hanno quattro cifre,
// sei se sono quartieri di una citta' designata; i distretti (JCG) ne hanno
// cinque. Le prime due cifre sono sempre la prefettura, da 01 a 47. Si accetta
// come lo scrivono i log — "1001", "10-01", "JCC 1001" — e torna vuoto se il
// numero non sta in piedi.
QString japanJarlCode(const QString& county);

// Vero se quel numero e' di un distretto (JCG), cioe' di cinque cifre.
bool isJapanGun(const QString& jarlCode);

} // namespace awards

struct AwardFilter {
    QString band;               // vuoto = tutte
    QString modeGroup;          // "", "FT2", "FT8", "DIGITAL", "CW", "PHONE"
    // Le conferme che valgono per i diplomi che non hanno regole proprie.
    bool confirmLotw{true};
    bool confirmCard{true};
    bool confirmEqsl{false};
    qint64 stationProfileId{0}; // 0 = tutti i profili
    QString awardAccountUuid;    // vuoto = tutti; account storico del profilo
    QString tag;                // etichetta dei QSO che contano, vuota = tutti
    // I 60 metri nel DXCC, nel DXCC Challenge e nel WAS. Per l'ARRL non
    // valgono: il profilo predefinito e' quindi ufficiale; true e' l'override
    // personale scelto dall'operatore.
    bool count60m{false};
    // Le conferme scelte dall'operatore per un diploma ("dxcc" → lotw, card):
    // vincono sulle regole del diploma e sulle scelte generali.
    QHash<QString, QStringList> credits;
};

namespace awards {

// I servizi che confermano: lotw, card, eqsl, qrz.
QStringList creditServices();
// Le conferme che il regolamento del diploma accetta, se ne ha di sue: il
// DXCC e il WAS dell'ARRL solo LoTW e cartolina. Vuoto = quelle generali.
QStringList officialCredits(const QString& awardId);
// Quelle che valgono davvero per un diploma con quel filtro.
QStringList creditsFor(const QString& awardId, const AwardFilter& filter);

} // namespace awards

struct BandTotal {
    QString band;
    int worked{0};              // elementi lavorati su questa banda
    int confirmed{0};
};

struct AwardItem {
    QString key;                // "291", "14", "CA", "IU8", "JN71", "EU-025"...
    QString name;               // nome leggibile, se c'e'
    QSet<QString> bandsWorked;
    QSet<QString> bandsConfirmed;
    int qsoCount{0};
    QDateTime first;
    QDateTime last;
    qint64 firstQsoId{0};
    QString firstCall;
    bool countsForAward{true}; // visibile nel dettaglio, eventualmente fuori dal conteggio ufficiale
    bool confirmed() const { return !bandsConfirmed.isEmpty(); }
};

struct AwardResult {
    QString id;                 // "dxcc", "waz"...
    QString title;
    // False means the view is an operational/statistical estimate and must not
    // be presented as proof of eligibility for an external award.
    bool officialRules{false};
    QString rulesSource;
    QString verifier;
    // Quello che il regolamento chiede oltre al numero, gia' tradotto e gia'
    // confrontato con il log: "30 castelli in 5 regioni, uno della provincia di
    // Cuneo - ne hai 12 in 4 regioni, Cuneo manca". Vuoto se non c'e' altro.
    QString requirement;
    int target{0};              // traguardo del diploma base (0 = nessuno)
    int total{0};               // elementi esistenti, se il numero e' fisso (40 zone, 50 stati)
    QList<AwardItem> items;     // ordinati per chiave
    int worked(bool officialOnly = false) const;
    int confirmed(bool officialOnly = false) const;
    // Per banda, nell'ordine dato: quanti elementi lavorati e confermati. La somma
    // dei confermati e' il conteggio dei "band slot" (DXCC Challenge).
    QList<BandTotal> bandTotals(const QStringList& bands, bool officialOnly = false) const;
};

// Optional per-QSO provenance for selected awards. This is a potential
// contribution under the same filters as the calculator, not award credit.
struct AwardContributions {
    QSet<QString> awards;
    QHash<QString, QHash<QString, QSet<qint64>>> qsoIds;
};

class AwardCalculator {
public:
    // Nome di un'entita' DXCC dal numero (lo fornisce il cty.csv).
    using DxccName = std::function<QString(int dxcc)>;

    explicit AwardCalculator(DxccName dxccName = {});

    void setRulePackages(const QList<awards::AwardRulePackage>& packages) { m_rulePackages = packages; }

    // Con il cty.xml di Club Log il DXCC sa di piu': le operazioni che l'ARRL
    // non ha accettato non contano, e le entita' cancellate non fanno numero
    // per il diploma di adesso.
    struct DxccRules {
        std::function<bool(const QString& call, const QDateTime& when)> invalid;
        std::function<bool(int dxcc)> deleted;
    };
    void setDxccRules(DxccRules rules) { m_rules = std::move(rules); }

    static QStringList awardIds();
    // Tutti gli award in una passata sul log.
    QList<AwardResult> compute(const LogDatabase& db, const AwardFilter& filter,
                               AwardContributions* contributions = nullptr) const;

private:
    DxccName m_dxccName;
    DxccRules m_rules;
    QList<awards::AwardRulePackage> m_rulePackages;
};

} // namespace decolog::core
