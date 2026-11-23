#pragma once
#include "core/Awards.h"
#include "core/LogDatabase.h"

namespace decolog::core::qslplanner {
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
    QStringList pendingServices;
    QList<Benefit> benefits;
    QSet<qint64> alternativeQsoIds;
};
struct Plan {
    QList<Candidate> candidates;
    int uniqueBenefits{0};
    QString error;
};
// Recomputed on demand from a read snapshot. The result is a potential plan;
// it does not send requests, estimate success, or assert official eligibility.
Plan build(const LogDatabase& db, const AwardFilter& filter, const QSet<QString>& goals,
           const AwardCalculator& calculator = AwardCalculator{});
}
