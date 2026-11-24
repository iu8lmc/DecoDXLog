#pragma once

#include <QJsonDocument>
#include <QJsonArray>
#include <QList>
#include <QString>
#include "core/LogDatabase.h"

namespace decolog::core::vucc {

struct Entry {
    qint64 qsoId{0};
    QString grid;
    QString band;
    QString account;
    QString site;
    bool confirmed{false};
    QString evidence;
    QString grids;
    QString myGrids;
    QString myGrid;
    QString otherEvidence; // recorded confirmations not accepted by this candidate filter
    QString propMode, satName, rxBand, classification;
    QString qsoUtc;
    QJsonArray lotwGridObservations;
    int lotwGridObservationCount{0};
};

struct Rules {
    QString account;
    QString site;
    QString band;
    int target{100};
    QString scope{QStringLiteral("terrestrial")};
};

struct Result {
    Rules rules;
    QList<Entry> eligible;
    QStringList exclusions;
    bool officialRulesVerified{false};
    bool isValid(QString* error = nullptr) const;
    QJsonDocument toJson() const;
};

Result select(const QList<Entry>& entries, const Rules& rules);
QList<Entry> loadEntries(const LogDatabase& db, QString* error = nullptr);
bool validGrid4(const QString& grid);
// Initial category threshold from the January 2026 VUCC rules; zero if unsupported.
int initialThreshold(const QString& scope, const QString& band);

} // namespace decolog::core::vucc
