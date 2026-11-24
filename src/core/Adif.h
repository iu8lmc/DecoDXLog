// DecoDXLog — lettura e scrittura ADIF (.adi).
//
// Un record e' una lista ordinata di campi, non una mappa: l'ordine e il
// maiuscolo/minuscolo del nome non contano per ADIF, ma chi esporta quello che
// ha importato si aspetta di ritrovare il file che conosce. I nomi sono
// normalizzati in maiuscolo; i valori restano esattamente come arrivano.
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

namespace decolog::core {

struct AdifField {
    QString name;   // maiuscolo, es. "CALL"
    QString value;
};

class AdifRecord {
public:
    AdifRecord() = default;
    AdifRecord(std::initializer_list<AdifField> fields);

    QString value(const QString& name) const;
    bool    contains(const QString& name) const;
    // Sostituisce il valore se il campo c'e', altrimenti lo aggiunge in coda.
    // Un valore vuoto toglie il campo: ADIF non distingue vuoto da assente.
    void    set(const QString& name, const QString& value);
    void    remove(const QString& name);

    const QList<AdifField>& fields() const { return m_fields; }
    bool isEmpty() const { return m_fields.isEmpty(); }

private:
    QList<AdifField> m_fields;
};

struct AdifDocument {
    AdifRecord        header;
    QList<AdifRecord> records;
    bool              unterminatedRecord{false};
    bool              unterminatedTag{false};
};

namespace adif {

// Accetta file con o senza intestazione, <EOH>/<EOR> in qualunque maiuscolo, e
// lunghezze contate in caratteri (come fanno WSJT-X e Decodium) o in byte UTF-8
// (come fanno altri programmi). Il testo viene letto come UTF-8, o come Latin-1
// se non e' UTF-8 valido.
AdifDocument parse(const QByteArray& data);

// Rimette in piedi un valore rovinato da una vecchia lettura ADIF, quando la
// lunghezza contata in byte veniva presa per caratteri: il valore restava
// tagliato e si portava dietro il pezzo del tag seguente ("Vilnius<GRIDSQ").
// Se dentro c'e' il carattere di sostituzione — un carattere tagliato a meta',
// che non si puo' indovinare — il valore non vale piu' niente e torna vuoto,
// cosi' il callbook puo' riscriverlo giusto.
QString repairTruncated(const QString& value);

// Un record o un file intero. Le lunghezze sono in caratteri, come le scrive
// Decodium.
QString writeRecord(const AdifRecord& record);
QByteArray writeDocument(const AdifDocument& document);

// MODE/SUBMODE come li vuole ADIF 3.1.x: FT2, FT4, FST4 e Q65 sono sottomodi
// di MFSK. Un record con MODE=FT2 (log vecchi) diventa MODE=MFSK SUBMODE=FT2.
void normalizeMode(AdifRecord& record);

// Il gruppo di modi come lo usano LoTW e il DXCC: "CW", "PHONE", "IMAGE" o
// "DATA". LoTW conferma un QSO se il gruppo coincide, anche con modi diversi
// (FT8 da una parte, MFSK/FT4 dall'altra).
QString modeGroup(const QString& mode, const QString& submode = {});

} // namespace adif
} // namespace decolog::core
