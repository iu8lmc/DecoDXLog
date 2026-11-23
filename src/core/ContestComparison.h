#pragma once
#include <QDate>
#include <QSqlDatabase>
#include <QVariantList>
namespace decolog::core::contestreport { struct Report; }
namespace decolog::core::contestcomparison {
struct Findings {
    int nilA{0}, nilB{0}, callA{0}, callB{0}, exchangeA{0}, exchangeB{0};
    int unknownA{0}, unknownB{0}, outsideA{0}, outsideB{0};
    QString ownCallA, ownCallB, contestA, contestB, error;
};
Findings compareFindings(const contestreport::Report &a, const contestreport::Report &b, const QString &contestId,
                         QDate fromA, QDate toA, QDate fromB, QDate toB);
struct Goal { int target{0}; int revision{0}; };
struct GoalPair { Goal a, b; int generation{0}; QString error; };
GoalPair loadGoals(const QSqlDatabase &db, const QString &contestId, QDate fromA, QDate toA, QDate fromB, QDate toB);
GoalPair saveGoals(const QSqlDatabase &db, const QString &expectedIdentity, const QString &contestId, QDate fromA, QDate toA, QDate fromB, QDate toB, int targetA, int targetB, int revisionA, int revisionB, int expectedGeneration);
GoalPair removeGoals(const QSqlDatabase &db, const QString &expectedIdentity, const QString &contestId, QDate fromA, QDate toA, QDate fromB, QDate toB, int revisionA, int revisionB, int expectedGeneration);
struct Result { int totalA{0}, totalB{0}, invalidTimestamp{0}; QVariantList rows; QString error; };
// Counts reported contest-ID records within inclusive UTC-date periods. No scoring or eligibility inference.
Result compare(const QSqlDatabase &db, const QString &contestId, QDate fromA, QDate toA, QDate fromB, QDate toB);
}
