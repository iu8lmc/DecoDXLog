#pragma once
#include <QByteArray>
#include <QSqlDatabase>
#include <QString>
#include <QVariantList>
#include <optional>
class QIODevice;
namespace decolog::core { class LogDatabase; }

namespace decolog::core::activityreport {
struct Result { QVariantList rows; bool truncated{false}; QString error; };
// Starts a read transaction and pins it by verifying the expected log identity.
// The caller owns the transaction and must close it after running the report.
bool beginVerifiedSnapshot(QSqlDatabase& db, const QString& expectedIdentity, QString* error = nullptr);
struct Definition {
    QString id, name, kind, band, station, aggregate;
    QStringList dimensions;
    int year{0}, revision{0};
};
bool saveDefinition(LogDatabase& db, Definition& definition, QString* error = nullptr);
std::optional<Definition> loadDefinition(const LogDatabase& db, const QString& id, QString* error = nullptr);
QList<Definition> listDefinitions(const LogDatabase& db, QString* error = nullptr);
bool removeDefinition(LogDatabase& db, const Definition& definition, QString* error = nullptr);
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
