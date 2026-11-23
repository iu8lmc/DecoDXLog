#pragma once
#include <QByteArray>
#include <QSqlDatabase>
#include <QString>
#include <QVariantList>
class QIODevice;

namespace decolog::core::activityreport {
struct Result { QVariantList rows; bool truncated{false}; QString error; };
// Counts recorded QSO by their own reported antenna, band and own grid.
// A single SELECT supplies one SQLite read snapshot. Missing values remain empty.
Result run(const QSqlDatabase& db, int utcYear, const QString& band);
Result runConfirmations(const QSqlDatabase& db, int utcYear, const QString& band);
Result runClub(const QSqlDatabase& db, int utcYear, const QString& band, const QString& station);
QString validateCustomSpec(const QStringList& dimensions, const QString& aggregate);
Result runCustom(const QSqlDatabase& db, int utcYear, const QString& band, const QStringList& dimensions, const QString& aggregate);
QByteArray csv(const Result& result);
QByteArray confirmationsCsv(const Result& result);
QByteArray clubCsv(const Result& result);
QByteArray customCsv(const Result& result, const QStringList& dimensions, const QString& aggregate);
bool writePdf(QIODevice* output, const QVariantMap& snapshot, QString* error = nullptr);
}
