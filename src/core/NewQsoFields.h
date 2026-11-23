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
#include <QVariantMap>
#include <QLocale>

namespace decolog::core::newqso {

inline constexpr int kMaxCustomFields = 12;

enum class CustomFieldError {
    None,
    InvalidName,
    Duplicate,
    StandardField,
    ProtectedField,
    TooMany,
    InvalidType,
    InvalidChoices,
    InvalidUnit,
    InvalidCondition,
    InvalidFormula,
};

struct CustomFieldsResult {
    QVariantList fields;
    CustomFieldError error{CustomFieldError::None};
    QString field;

    bool ok() const { return error == CustomFieldError::None; }
};
struct DerivedValuesResult { QVariantMap values; QString error; };

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
// Returns an explanatory error; the caller retains the original ADIF text.
QString validateCustomValue(const QVariantMap& definition, const QString& value, const QLocale& locale);
QString validateCustomValues(const QVariantList& definitions, const QVariantMap& values, const QLocale& locale);
// Derived outputs use canonical C-locale decimal text; original inputs are untouched.
DerivedValuesResult computeDerivedValues(const QVariantList& definitions, const QVariantMap& values, const QLocale& locale);
QString validateDerivedFormulas(const QVariantList& definitions, QString* invalidField = nullptr);

QString encodeCustomFields(const QVariantList& fields);
QVariantList decodeCustomFields(const QString& json, bool* legacyIota = nullptr);

} // namespace decolog::core::newqso
