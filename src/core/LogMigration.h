// DecoDXLog — venire da un altro programma di log.
//
// Quasi tutti esportano in ADIF, e l'ADIF entra cosi' com'e'. Restano due casi:
//
//  - il foglio di calcolo (CSV): le colonne si riconoscono dal nome, in inglese
//    o in italiano, o col nome ADIF ("Call", "Nominativo", "QSO_DATE"...);
//  - il database di N1MM Logger+ (.s3db): la tabella DXLOG, letta direttamente,
//    senza passare dall'esportazione di N1MM.
//
// E nell'ADIF di certi programmi le conferme stanno in campi loro: il logbook di
// QRZ scrive APP_QRZLOG_STATUS=C per un QSO confermato. Si traducono nei campi
// ADIF veri, cosi' la conferma non si perde.
#pragma once

#include "core/Adif.h"

#include <QByteArray>
#include <QList>
#include <QString>
namespace decolog::core { class LogDatabase; struct ImportResult; }

namespace decolog::core::migration {

// Il file come ADIF: un ADIF resta com'e', un CSV o un database N1MM si
// convertono. `kind` dice cos'era ("adif", "csv", "n1mm").
QByteArray asAdif(const QString& path, QString* error = nullptr, QString* kind = nullptr);
// Advisory dry run using the exact stored duplicate keys from a consistent
// source snapshot. The source log is never modified.
bool estimateImport(const LogDatabase& source, const QByteArray& adif, ImportResult* result, QString* error = nullptr);

QList<AdifRecord> readCsv(const QByteArray& data, QString* error = nullptr);
QList<AdifRecord> readN1mmDatabase(const QString& path, QString* error = nullptr);

// I campi dei programmi tradotti in quelli ADIF (le conferme di QRZ).
void mapProgramFields(AdifRecord& record);

} // namespace decolog::core::migration
