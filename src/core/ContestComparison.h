#pragma once
#include <QDate>
#include <QSqlDatabase>
#include <QVariantList>
namespace decolog::core::contestcomparison {
struct Result { int totalA{0}, totalB{0}, invalidTimestamp{0}; QVariantList rows; QString error; };
// Counts reported contest-ID records within inclusive UTC-date periods. No scoring or eligibility inference.
Result compare(const QSqlDatabase &db, const QString &contestId, QDate fromA, QDate toA, QDate fromB, QDate toB);
}
