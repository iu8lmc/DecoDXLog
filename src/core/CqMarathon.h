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
    bool isValid(QString* error = nullptr) const;
    QJsonDocument toJson() const;
};

Result select(const QList<Entry>& entries, const Rules& rules);
QList<Entry> loadEntries(const LogDatabase& db, QString* error = nullptr);

} // namespace decolog::core::cqmarathon
