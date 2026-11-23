#pragma once
#include "core/Awards.h"
#include "core/LogDatabase.h"
#include <QSharedPointer>

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
struct AlternativeGroup {
    QList<qint64> ids;
    QList<qint64> pendingIds;
};
struct Benefit {
    QString award;
    QString key;
    QStringList acceptedServices;
    QSharedPointer<AlternativeGroup> group;
    QList<qint64> alternativePage(qint64 self, int offset, int limit, bool pendingOnly = false) const;
    int alternativeCount(qint64 self, bool pendingOnly = false) const;
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
