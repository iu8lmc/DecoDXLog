#pragma once

#include <QDateTime>
#include <QJsonDocument>
#include <QList>
#include <QString>
#include "core/LogDatabase.h"

namespace decolog::core::cqmarathon {

struct Entry {
    qint64 qsoId{0};
    QString call;
    int dxcc{0};
    int cqZone{0};
    QString band;
    QString mode;
    QDateTime timestamp;
    qint64 stationProfileId{0};
    QString region; // recorded ADIF REGION, never inferred from the callsign
    int entityCode{0}; // preliminary Marathon entity; original DXCC remains unchanged
    QString entityBasis;
};

struct Rules {
    int year{0};
    QString category; // e.g. "mixed", "cw", "phone", "digital"
    int maxPerEntity{1};
    qint64 stationProfileId{0}; // mandatory scope when non-zero
};

struct Result {
    Rules rules;
    QList<Entry> selected;
    QList<Entry> zoneCandidates; // preliminary, independent of entity selection
    QStringList exclusions;
    QStringList warnings; // potential data conflicts; candidate eligibility is unchanged
    bool isValid(QString* error = nullptr) const;
    QJsonDocument toJson() const;
};
struct ReviewAdif { QByteArray bytes; int count{0}; int invalidTimestamp{0}; QString error; };
// Exports all recorded alternatives for one explicit station profile and UTC
// year from a consistent read transaction; this is not an official submission.
ReviewAdif reviewAdif(const LogDatabase& db, int year, qint64 stationProfileId, const QString& expectedIdentity = {});

Result select(const QList<Entry>& entries, const Rules& rules);
QList<Entry> loadEntries(const LogDatabase& db, QString* error = nullptr);

} // namespace decolog::core::cqmarathon
