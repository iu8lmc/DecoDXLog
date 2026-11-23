#include "core/QslPlanner.h"
#include <QSqlQuery>
#include <QSqlError>
#include <algorithm>

namespace decolog::core::qslplanner {
Plan build(const LogDatabase& db, const AwardFilter& filter, const QSet<QString>& goals,
           const AwardCalculator& calculator)
{
    Plan result;
    if (!db.isOpen()) { result.error=QStringLiteral("Log is not open"); return result; }
    QSqlQuery savepoint(db.connection());
    if (!savepoint.exec(QStringLiteral("SAVEPOINT qsl_planner_read"))) { result.error=savepoint.lastError().text(); return result; }
    struct Scope { QSqlDatabase db; ~Scope() { QSqlQuery q(db); q.exec(QStringLiteral("RELEASE SAVEPOINT qsl_planner_read")); } } scope{db.connection()};
    AwardContributions contributions; contributions.awards=goals;
    const auto results=calculator.compute(db,filter,&contributions);
    QSet<QString> available;
    for (const auto& award : results) available.insert(award.id);
    for (const QString& goal : goals) if (!available.contains(goal)) { result.error=QStringLiteral("Unknown award: ")+goal; return result; }
    QHash<qint64,Candidate> candidates;
    for (const auto& award : results) {
        if (!goals.contains(award.id)) continue;
        for (const AwardItem& item : award.items) {
            if (!item.countsForAward || item.confirmed()) continue;
            const auto alternatives=contributions.qsoIds.value(award.id).value(item.key);
            if (alternatives.isEmpty()) continue;
            ++result.uniqueBenefits;
            for (qint64 id : alternatives) {
                Benefit benefit{award.id,item.key,awards::creditsFor(award.id,filter),alternatives,{}};
                benefit.alternativeQsoIds.remove(id);
                Candidate& candidate=candidates[id]; candidate.qsoId=id; candidate.benefits.append(benefit);
                candidate.alternativeQsoIds.unite(alternatives);
            }
        }
    }
    for (auto it=candidates.begin(); it!=candidates.end(); ++it) {
        Candidate& candidate=it.value(); candidate.alternativeQsoIds.remove(candidate.qsoId);
        const auto record=db.record(candidate.qsoId);
        if (!record) { result.error=QStringLiteral("QSO unavailable while planning"); result.candidates.clear(); return result; }
        candidate.call=record->value(QStringLiteral("CALL"));
        candidate.manager=record->value(QStringLiteral("QSL_VIA")).trimmed();
        if (!candidate.manager.isEmpty()) candidate.managerSource=QStringLiteral("QSO");
        for (const EnrichmentEntry& entry : db.enrichments(candidate.qsoId)) {
            if (entry.field==QLatin1String("QSL_VIA") && entry.value.trimmed().compare(candidate.manager,Qt::CaseInsensitive)==0) {
                candidate.managerSource=entry.source;
                candidate.managerObservedAt=QDateTime::fromString(entry.acquiredAt,Qt::ISODate);
            }
        }
        for (const QslState& state : db.qslStatus(candidate.qsoId)) {
            if (state.sent.compare(QLatin1String("Y"),Qt::CaseInsensitive)==0 && state.rcvd.compare(QLatin1String("Y"),Qt::CaseInsensitive)!=0)
                candidate.pendingServices.append(state.service);
            if (state.service==QLatin1String("card")) {
                const QString via=state.via.trimmed().toUpper();
                if (via==QLatin1String("B") || via==QLatin1String("D")) candidate.paperChannel=via;
            }
        }
        std::sort(candidate.benefits.begin(),candidate.benefits.end(),[](const Benefit& a,const Benefit& b) { return a.award==b.award ? a.key<b.key : a.award<b.award; });
    }
    for (auto it=candidates.begin(); it!=candidates.end(); ++it) {
        Candidate& candidate=it.value();
        for (Benefit& benefit : candidate.benefits) {
            for (qint64 alternate : benefit.alternativeQsoIds) {
                const auto other=candidates.constFind(alternate);
                if (other==candidates.cend()) continue;
                for (const QString& service : other->pendingServices) {
                    if (benefit.acceptedServices.contains(service)) {
                        benefit.pendingAlternativeQsoIds.insert(alternate);
                        break;
                    }
                }
            }
        }
        result.candidates.append(candidate);
    }
    std::sort(result.candidates.begin(),result.candidates.end(),[](const Candidate& a,const Candidate& b) { return a.qsoId<b.qsoId; });
    struct Group { QList<qint64> ids; QSet<QPair<QString,QString>> benefits; };
    QMap<QPair<QString,QString>,Group> routes;
    for (const Candidate& candidate : result.candidates) {
        QSet<QPair<QString,QString>> paperBenefits;
        for (const Benefit& benefit : candidate.benefits)
            if (benefit.acceptedServices.contains(QStringLiteral("card"))) paperBenefits.insert({benefit.award,benefit.key});
        if (paperBenefits.isEmpty()) continue;
        const QString manager=candidate.manager.simplified().toUpper();
        if (manager.isEmpty() || candidate.paperChannel.isEmpty()) {
            result.ungroupedPaperQsoIds.append(candidate.qsoId); continue;
        }
        Group& group=routes[{manager,candidate.paperChannel}];
        group.ids.append(candidate.qsoId); group.benefits.unite(paperBenefits);
    }
    for (auto it=routes.cbegin(); it!=routes.cend(); ++it)
        result.paperRoutes.append({it.key().first,it.key().second,it->ids,int(it->benefits.size())});
    return result;
}
}
