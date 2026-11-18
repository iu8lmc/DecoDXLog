#pragma once

#include "core/Adif.h"

namespace decolog::core {
// A successful export is an owned Club Log log. Preserve its remote IDs and
// prevent newly imported contacts from being sent straight back to Club Log.
struct ClubLogDownloadResult {
    QByteArray adif;
    enum class Error { None, Empty, Invalid, StationMismatch, InvalidRemoteId } error = Error::None;
    int missingRemoteIds = 0;
    int filledStations = 0;
};

inline ClubLogDownloadResult prepareClubLogDownload(const QByteArray& input, const QString& requestedCall)
{
    ClubLogDownloadResult result;
    const QString station = requestedCall.trimmed().toUpper();
    const int endings = input.toUpper().count("<EOR>");
    if (station.isEmpty()) {
        result.error = ClubLogDownloadResult::Error::Invalid;
        return result;
    }
    AdifDocument doc = adif::parse(input);
    if (doc.records.isEmpty() && endings == 0) { result.error = ClubLogDownloadResult::Error::Empty; return result; }
    if (endings == 0 || doc.records.size() != endings) { result.error = ClubLogDownloadResult::Error::Invalid; return result; }
    for (AdifRecord& record : doc.records) {
        if (record.value(QStringLiteral("CALL")).trimmed().isEmpty()) {
            result.error = ClubLogDownloadResult::Error::Invalid; return result;
        }
        const QString remote = record.value(QStringLiteral("APP_CLUBLOG_QSOID")).trimmed();
        if (remote.isEmpty()) ++result.missingRemoteIds;
        else {
            bool ok = false;
            const qint64 id = remote.toLongLong(&ok);
            if (!ok || id <= 0) { result.error = ClubLogDownloadResult::Error::InvalidRemoteId; return result; }
        }
        const QString recordedStation = record.value(QStringLiteral("STATION_CALLSIGN")).trimmed().toUpper();
        if (recordedStation.isEmpty()) {
            record.set(QStringLiteral("STATION_CALLSIGN"), station);
            ++result.filledStations;
        } else if (recordedStation != station) {
            result.error = ClubLogDownloadResult::Error::StationMismatch; return result;
        }
        record.set(QStringLiteral("CLUBLOG_QSO_UPLOAD_STATUS"), QStringLiteral("Y"));
    }
    result.adif = adif::writeDocument(doc);
    return result;
}
}
