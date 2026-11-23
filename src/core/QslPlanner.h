#pragma once
#include "core/Awards.h"
#include "core/LogDatabase.h"

namespace decolog::core::qslplanner {
struct RouteCost {
    QString amount;       // decimal string; not a currency conversion
    QString currency;     // three-letter code supplied by operator
    QString basis;        // estimate or sourced (source is user supplied)
    QString source;
    QString observedDate; // ISO date, optional for estimates
};
std::optional<RouteCost> routeCost(const LogDatabase& db, const QString& manager, const QString& channel);
bool saveRouteCost(LogDatabase& db, const QString& manager, const QString& channel,
                   const RouteCost& cost, QString* error = nullptr);
bool clearRouteCost(LogDatabase& db, const QString& manager, const QString& channel, QString* error = nullptr);
struct Benefit {
    QString award;
    QString key;
    QStringList acceptedServices;
    QSet<qint64> alternativeQsoIds;
    QSet<qint64> pendingAlternativeQsoIds;
};
struct Candidate {
    qint64 qsoId{0};
    QString call;
    QString manager;
    QString managerSource;
    QDateTime managerObservedAt;
    QString paperChannel; // B or D only when explicitly recorded
    QStringList pendingServices;
    QList<Benefit> benefits;
    QSet<qint64> alternativeQsoIds;
    int priorityRank{0}; // zero means an alternative after the greedy coverage pass
    QList<QPair<QString,QString>> marginalBenefits;
    int pendingMarginalCount{0};
};
struct PaperRoute {
    QString manager;
    QString channel;
    QList<qint64> qsoIds;
    int uniqueBenefits{0};
    std::optional<RouteCost> cost;
};
struct Plan {
    QList<Candidate> candidates;
    QList<PaperRoute> paperRoutes;
    QList<qint64> ungroupedPaperQsoIds;
    int uniqueBenefits{0};
    QString error;
};
// Recomputed on demand from a read snapshot. The result is a potential plan;
// it does not send requests, estimate success, or assert official eligibility.
Plan build(const LogDatabase& db, const AwardFilter& filter, const QSet<QString>& goals,
           const AwardCalculator& calculator = AwardCalculator{});
}
