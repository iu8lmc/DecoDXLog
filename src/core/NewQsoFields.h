// DecoDXLog — configurazione dei campi aggiuntivi nel pannello Nuovo QSO.
//
// La GUI puo' mostrare o nascondere i campi opzionali e aggiungere campi ADIF
// liberi. Qui restano normalizzazione e validazione, cosi' un nome scritto nella
// configurazione non puo' sostituire per errore nominativo, data, banda o altri
// dati fondamentali del QSO.
#pragma once

#include <QString>
#include <QStringList>
#include <QVariantList>

namespace decolog::core::newqso {

inline constexpr int kMaxCustomFields = 12;

enum class CustomFieldError {
    None,
    InvalidName,
    Duplicate,
    StandardField,
    ProtectedField,
    TooMany,
};

struct CustomFieldsResult {
    QVariantList fields;
    CustomFieldError error{CustomFieldError::None};
    QString field;

    bool ok() const { return error == CustomFieldError::None; }
};

// Campi opzionali gia' gestiti dal pannello e quelli visibili per impostazione
// predefinita. STATE, IOTA e TX_PWR sono disponibili ma inizialmente spenti, per non
// cambiare la disposizione degli utenti esistenti.
QStringList standardFields();
QStringList defaultVisibleFields();
QStringList normalizeVisibleFields(const QStringList& fields);

QString normalizeAdifName(const QString& name);
bool isValidAdifName(const QString& name);
bool isProtectedAdifField(const QString& name);

// Ogni elemento e' {name, label}. Il risultato e' normalizzato e privo di
// righe vuote. I nomi dei campi standard vanno abilitati, non duplicati come
// campi liberi.
CustomFieldsResult validateCustomFields(const QVariantList& fields);

QString encodeCustomFields(const QVariantList& fields);
QVariantList decodeCustomFields(const QString& json, bool* legacyIota = nullptr);

} // namespace decolog::core::newqso
