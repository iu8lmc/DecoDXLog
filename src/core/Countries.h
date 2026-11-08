// DecoDXLog — da nominativo a entità DXCC, con il file cty.csv di AD1C.
//
// cty.csv (country-files.com) è la versione di cty.dat che porta anche il
// numero di entità ADIF: è quello che serve per l'FT2 Award e per dire "nuovo
// DXCC" mentre si lavora. Ne esiste una copia fra le risorse; una più recente
// messa nella cartella dei dati la sostituisce.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVector>
#include <optional>

namespace decolog::core {

struct DxccEntity {
    int     dxcc{0};        // numero ADIF
    QString name;           // nome dell'entità DXCC (non quello WAE)
    QString prefix;         // prefisso principale, es. "EA8"
    QString continent;
    int     cqZone{0};
    int     ituZone{0};
    double  lat{0.0};       // gradi, nord positivo
    double  lon{0.0};       // gradi, est positivo (cty.csv li scrive al contrario)
    double  utcOffset{0.0};
};

class Countries {
public:
    // Legge cty.csv. Restituisce false se il contenuto non ha entità valide.
    bool load(const QByteArray& csv);
    bool isEmpty() const { return m_entities.isEmpty(); }
    int entityCount() const;
    // Versione del file (es. "VER20260915"), vuota se non dichiarata.
    QString version() const { return m_version; }

    // Eccezioni dell'utente, nel formato del cty.csv: "=NOMINATIVO 191" per un
    // nominativo esatto, "PREFISSO 191" per un prefisso, con il numero DXCC.
    // Vincono sul cty.csv. Le righe non valide (o con un DXCC sconosciuto) si
    // saltano; restituisce quante ne ha applicate.
    int addExceptions(const QByteArray& text);

    // L'entità del nominativo, o nullopt per /MM, /AM e nominativi sconosciuti.
    std::optional<DxccEntity> lookup(const QString& callsign) const;
    // Il nome dell'entità dal numero ADIF.
    QString nameFor(int dxcc) const { return m_names.value(dxcc); }
    // Una voce per entita' DXCC (senza le voci solo WAE), in ordine di numero.
    QList<DxccEntity> entities() const;

private:
    struct Match {
        int entityIndex{-1};
        int cqZone{0};
        int ituZone{0};
    };

    std::optional<Match> matchPrefix(const QString& text) const;
    DxccEntity resolve(const Match& m) const;

    QVector<DxccEntity> m_entities;
    QHash<QString, Match> m_exact;      // =CALL
    QHash<QString, Match> m_prefixes;   // prefisso
    QHash<int, QString> m_names;
    QVector<int> m_primary;             // indice in m_entities della prima voce di ogni entita'
    int m_longestPrefix{0};
    QString m_version;
};

} // namespace decolog::core
