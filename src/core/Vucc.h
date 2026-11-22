#pragma once

#include <QJsonDocument>
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
};

struct Rules {
    QString account;
    QString site;
    QString band;
    int target{100};
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

} // namespace decolog::core::vucc
