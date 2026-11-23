// DecoDXLog — attivazioni e contest.
//
// Un'attivazione (POTA, SOTA, WWFF, un'isola IOTA) o un contest cambia il modo di
// stare al log: la referenza e' sempre la stessa, il locatore e' quello del posto,
// il numero progressivo sale a ogni QSO, e "duplicato" non vuol dire "gia' fatto
// due minuti fa" ma "gia' fatto in questa attivazione, su questa banda, in questo
// modo". Qui c'e' la sessione: quello che si applica a ogni QSO finche' e' aperta.
//
// I riferimenti dell'attivatore in ADIF sono i campi MY_*: MY_SIG/MY_SIG_INFO per
// POTA e WWFF, MY_SOTA_REF per SOTA. Quelli senza prefisso (POTA_REF, SOTA_REF)
// sono di chi sta dall'altra parte, il cacciatore.
#pragma once

#include "core/Adif.h"

#include <QDateTime>
#include <QString>
#include <QVariantMap>

namespace decolog::core {

struct Activation {
    enum class Kind { None, Pota, Sota, Wwff, Iota, Contest, Free };

    bool      active{false};
    Kind      kind{Kind::None};
    QString   reference;        // "IT-1234", "I/LM-001", "IFF-0123", "EU-025"
    QVariantMap programs;      // Additional operator references; never the contacted station's fields.
    QString   name;             // nome del parco o della cima, o del contest
    QString   contestId;        // CONTEST_ID ADIF, per i contest
    QString   myGrid;           // locatore del posto, se diverso da quello del profilo
    qint64    stationProfileId{0};
    QString   tag;              // etichetta su ogni QSO della sessione
    QString   band;             // banda fissa, vuota = libera
    QString   mode;             // modo fisso (FT2, CW...), vuoto = libero
    int       nextSerial{1};    // numero progressivo da mandare (contest)
    bool      serialEnabled{false};
    // Multi-operatore: i progressivi sono una sequenza sola per tutti i PC
    // della rete, e li distribuisce uno di loro.
    bool      sharedSerial{false};
    // Quello che per questa edizione della gara e' cambiato rispetto alla
    // scheda (vedi contestrules::withOverrides).
    QVariantMap rules;
    // Il file "call history" per suggerire lo scambio.
    QString   callHistory;
    QDateTime startedAt;
    QString sessionUuid; // Empty for sessions created before session tracking.

    static QString kindId(Kind kind);
    static Kind kindFromId(const QString& id);
    static bool normalizePrograms(const QVariantMap& input, QVariantMap* normalized, QString* error = nullptr);
    // Quanti QSO servono perche' l'attivazione valga: POTA 10, SOTA 4.
    int requiredQsos() const;
    // Etichetta di default per il tipo ("pota", "sota"...).
    QString defaultTag() const;
    QString title() const;

    // Aggiunge al QSO quello che la sessione impone: referenze dell'attivatore,
    // locatore, contest, numero progressivo, etichetta. Non tocca quello che il
    // QSO ha gia'.
    void applyTo(AdifRecord& record, int serial) const;
    // Nome del file per l'invio: IU8LMC@IT-1234-20260917.adi (POTA), oppure
    // IU8LMC-20260917.adi.
    QString exportFileName(const QString& stationCall, const QDate& day) const;

    QVariantMap toMap() const;
    static Activation fromMap(const QVariantMap& map);
};

} // namespace decolog::core
