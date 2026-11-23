#pragma once
#include <QDate>
#include <QSqlDatabase>
#include <QVariantList>
namespace decolog::core::contestcomparison {
struct Goal { int target{0}; int revision{0}; };
struct GoalPair { Goal a, b; int generation{0}; QString error; };
GoalPair loadGoals(const QSqlDatabase &db, const QString &contestId, QDate fromA, QDate toA, QDate fromB, QDate toB);
GoalPair saveGoals(const QSqlDatabase &db, const QString &expectedIdentity, const QString &contestId, QDate fromA, QDate toA, QDate fromB, QDate toB, int targetA, int targetB, int revisionA, int revisionB, int expectedGeneration);
GoalPair removeGoals(const QSqlDatabase &db, const QString &expectedIdentity, const QString &contestId, QDate fromA, QDate toA, QDate fromB, QDate toB, int revisionA, int revisionB, int expectedGeneration);
struct Result { int totalA{0}, totalB{0}, invalidTimestamp{0}; QVariantList rows; QString error; };
// Counts reported contest-ID records within inclusive UTC-date periods. No scoring or eligibility inference.
Result compare(const QSqlDatabase &db, const QString &contestId, QDate fromA, QDate toA, QDate fromB, QDate toB);
}
