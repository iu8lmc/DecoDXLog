#pragma once

#include "core/Adif.h"

namespace decolog::core {
// A successful export is an owned Club Log log. Preserve its remote IDs and
// prevent newly imported contacts from being sent straight back to Club Log.
inline QByteArray prepareClubLogDownload(const QByteArray& input)
{
    AdifDocument doc = adif::parse(input);
    if (doc.records.isEmpty()) return {};
    for (AdifRecord& record : doc.records) {
        bool ok = false;
        const qint64 id = record.value(QStringLiteral("APP_CLUBLOG_QSOID")).toLongLong(&ok);
        if (!ok || id <= 0 || record.value(QStringLiteral("STATION_CALLSIGN")).trimmed().isEmpty()) return {};
        record.set(QStringLiteral("CLUBLOG_QSO_UPLOAD_STATUS"), QStringLiteral("Y"));
    }
    return adif::writeDocument(doc);
}
}
