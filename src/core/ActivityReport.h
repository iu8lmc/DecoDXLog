#pragma once
#include <QByteArray>
#include <QSqlDatabase>
#include <QString>
#include <QVariantList>

namespace decolog::core::activityreport {
struct Result { QVariantList rows; bool truncated{false}; QString error; };
// Counts recorded QSO by their own reported antenna, band and own grid.
// A single SELECT supplies one SQLite read snapshot. Missing values remain empty.
Result run(const QSqlDatabase& db, int utcYear, const QString& band);
Result runConfirmations(const QSqlDatabase& db, int utcYear, const QString& band);
Result runClub(const QSqlDatabase& db, int utcYear, const QString& band, const QString& station);
QByteArray csv(const Result& result);
QByteArray confirmationsCsv(const Result& result);
QByteArray clubCsv(const Result& result);
}
