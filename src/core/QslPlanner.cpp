#include "core/QslPlanner.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>
#include <queue>

namespace decolog::core::qslplanner {
namespace {
bool validGoalSelection(int profileId, const QStringList& goals)
{
    if (profileId < 0 || goals.size() > 128) return false;
    QSet<QString> seen;
    for (const auto& id : goals) {
        if (id.isEmpty() || id.size() > 256 || seen.contains(id)) return false;
        seen.insert(id);
    }
    return true;
}
QString goalSelectionKey(int profileId)
{
    return QStringLiteral("qslplanner.goals.v1.profile.%1").arg(profileId);
}
}
static std::optional<QStringList> loadGoalSelectionAtKey(const LogDatabase& db, int profileId, const QString& key, QString* error)
{
    if (error) error->clear();
    if (!db.isOpen() || !db.fileIdentityMatchesOpen() || profileId < 0) {
        if (error) *error = QStringLiteral("Log unavailable or profile invalid"); return {};
    }
    QSqlQuery q(db.connection()); q.prepare(QStringLiteral("SELECT value FROM app_setting WHERE key=?"));
    q.addBindValue(key);
    if (!q.exec()) { if (error) *error=q.lastError().text(); return {}; }
    if (!q.next()) { if (error && q.lastError().isValid()) *error=q.lastError().text(); return {}; }
    QJsonParseError parse; const auto doc=QJsonDocument::fromJson(q.value(0).toString().toUtf8(),&parse);
    if (parse.error!=QJsonParseError::NoError || !doc.isArray()) { if (error) *error=QStringLiteral("Invalid saved QSL goals"); return {}; }
    QStringList goals; for (const auto& value:doc.array()) {
        if (!value.isString()) { if (error) *error=QStringLiteral("Invalid saved QSL goals"); return {}; }
        goals << value.toString();
    }
    if (!validGoalSelection(profileId,goals)) { if (error) *error=QStringLiteral("Invalid saved QSL goals"); return {}; }
    return goals;
}
static bool saveGoalSelectionAtKey(LogDatabase& db, int profileId, const QString& key, const QStringList& goals, QString* error)
{
    if (error) error->clear();
    if (!db.isOpen() || !db.fileIdentityMatchesOpen() || !validGoalSelection(profileId,goals)) {
        if (error) *error=QStringLiteral("Log unavailable or QSL goals invalid"); return false;
    }
    const auto json=QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(goals)).toJson(QJsonDocument::Compact));
    QSqlQuery q(db.connection()); q.prepare(QStringLiteral("INSERT INTO app_setting(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value"));
    q.addBindValue(key); q.addBindValue(json);
    if (!q.exec()) { if (error) *error=q.lastError().text(); return false; }
    return true;
}
PendingDetail pendingSince(const QslState& state,QDate today)
{
    PendingDetail out; out.service=state.service; out.rawDate=state.sentDate.trimmed();
    if (out.rawDate.isEmpty()) { out.dateStatus=QStringLiteral("missing"); return out; }
    const QDate date=QDate::fromString(out.rawDate,QStringLiteral("yyyyMMdd"));
    if (!today.isValid() || !date.isValid() || date.toString(QStringLiteral("yyyyMMdd"))!=out.rawDate) { out.dateStatus=QStringLiteral("invalid"); return out; }
    out.date=date.toString(Qt::ISODate);
    if (date>today) { out.dateStatus=QStringLiteral("future"); return out; }
    out.dateStatus=QStringLiteral("valid"); out.days=int(date.daysTo(today)); return out;
}
std::optional<QStringList> loadGoalSelection(const LogDatabase& db, int profileId, QString* error)
{
    return loadGoalSelectionAtKey(db,profileId,goalSelectionKey(profileId),error);
}
bool saveGoalSelection(LogDatabase& db, int profileId, const QStringList& goals, QString* error)
{
    return saveGoalSelectionAtKey(db,profileId,goalSelectionKey(profileId),goals,error);
}
static QString accountGoalKey(int profileId, const QString& account)
{
    const auto digest=QCryptographicHash::hash(account.toUtf8(),QCryptographicHash::Sha256).toHex();
    return QStringLiteral("qslplanner.goals.v2.profile.%1.account.%2").arg(profileId).arg(QString::fromLatin1(digest));
}
std::optional<QStringList> loadGoalSelectionForAccount(const LogDatabase& db, int profileId, const QString& account, QString* error)
{
    if (account.isEmpty()) return loadGoalSelection(db,profileId,error);
    if (account.size()>256 || account!=account.trimmed()) { if (error) *error=QStringLiteral("Invalid award account"); return {}; }
    QString localError; auto result=loadGoalSelectionAtKey(db,profileId,accountGoalKey(profileId,account),&localError);
    if (!localError.isEmpty()) { if (error) *error=localError; return {}; }
    if (result) { if (error) error->clear(); return result; }
    return loadGoalSelection(db,profileId,error);
}
bool saveGoalSelectionForAccount(LogDatabase& db, int profileId, const QString& account, const QStringList& goals, QString* error)
{
    if (account.isEmpty()) return saveGoalSelection(db,profileId,goals,error);
    if (account.size()>256 || account!=account.trimmed()) { if (error) *error=QStringLiteral("Invalid award account"); return false; }
    return saveGoalSelectionAtKey(db,profileId,accountGoalKey(profileId,account),goals,error);
}
int Benefit::alternativeCount(qint64 self, bool pendingOnly) const
{
    if (!group) return 0;
    const auto& ids=pendingOnly ? group->pendingIds : group->ids;
    return int(ids.size()) - (std::binary_search(ids.cbegin(),ids.cend(),self) ? 1 : 0);
}
QList<qint64> Benefit::alternativePage(qint64 self, int offset, int limit, bool pendingOnly) const
{
    QList<qint64> out; if (!group || limit<=0) return out;
    const auto& ids=pendingOnly ? group->pendingIds : group->ids;
    offset=qMax(0,offset); limit=qMin(limit,100);
    const auto selfIt=std::lower_bound(ids.cbegin(),ids.cend(),self);
    const qsizetype selfIndex=selfIt!=ids.cend() && *selfIt==self ? selfIt-ids.cbegin() : -1;
    qsizetype index=offset;
    if (selfIndex>=0 && index>=selfIndex) ++index;
    while (index<ids.size() && out.size()<limit) {
        if (index!=selfIndex) out.append(ids.at(index));
        ++index;
    }
    return out;
}
namespace {
QString costKey(const QString& manager, const QString& channel)
{
    const QString name=manager.simplified().toUpper(), via=channel.trimmed().toUpper();
    if (name.isEmpty() || name.size()>200 || (via!=QLatin1String("B") && via!=QLatin1String("D"))) return {};
    const QByteArray route=QJsonDocument(QJsonArray{name,via}).toJson(QJsonDocument::Compact);
    return QStringLiteral("qslplanner.paper-cost.")+QString::fromLatin1(QCryptographicHash::hash(route,QCryptographicHash::Sha256).toHex());
}
bool validCost(const RouteCost& cost)
{
    static const QRegularExpression amount(QStringLiteral("^(?:0|[1-9][0-9]{0,7})(?:\\.[0-9]{1,4})?$"));
    static const QRegularExpression currency(QStringLiteral("^[A-Z]{3}$"));
    if (!amount.match(cost.amount).hasMatch() || !currency.match(cost.currency).hasMatch() || cost.source.size()>1000 ||
        (cost.basis!=QLatin1String("estimate") && cost.basis!=QLatin1String("sourced"))) return false;
    if (cost.basis==QLatin1String("sourced") && (cost.source.trimmed().isEmpty() || cost.observedDate.isEmpty())) return false;
    if (!cost.observedDate.isEmpty()) {
        const QDate date=QDate::fromString(cost.observedDate,Qt::ISODate);
        if (!date.isValid() || date.toString(Qt::ISODate)!=cost.observedDate || date>QDate::currentDate()) return false;
    }
    return true;
}
}
std::optional<RouteCost> routeCost(const LogDatabase& db, const QString& manager, const QString& channel)
{
    const QString key=costKey(manager,channel); if (key.isEmpty() || !db.isOpen()) return std::nullopt;
    QSqlQuery q(db.connection()); q.prepare(QStringLiteral("SELECT value FROM app_setting WHERE key=?")); q.addBindValue(key);
    if (!q.exec() || !q.next()) return std::nullopt;
    QJsonParseError error; const auto json=QJsonDocument::fromJson(q.value(0).toString().toUtf8(),&error);
    if (error.error!=QJsonParseError::NoError || !json.isObject()) return std::nullopt;
    const auto obj=json.object();
    if (obj.value(QStringLiteral("manager")).toString()!=manager.simplified().toUpper() ||
        obj.value(QStringLiteral("channel")).toString()!=channel.trimmed().toUpper()) return std::nullopt;
    RouteCost cost{obj.value(QStringLiteral("amount")).toString(),obj.value(QStringLiteral("currency")).toString(),
                   obj.value(QStringLiteral("basis")).toString(),obj.value(QStringLiteral("source")).toString(),
                   obj.value(QStringLiteral("observedDate")).toString()};
    return validCost(cost) ? std::optional<RouteCost>(cost) : std::nullopt;
}
bool saveRouteCost(LogDatabase& db, const QString& manager, const QString& channel, const RouteCost& cost, QString* error)
{
    const QString key=costKey(manager,channel);
    if (!db.isOpen() || key.isEmpty() || !validCost(cost)) { if (error) *error=QStringLiteral("Invalid route or cost"); return false; }
    const QJsonObject obj{{QStringLiteral("manager"),manager.simplified().toUpper()},{QStringLiteral("channel"),channel.trimmed().toUpper()},
        {QStringLiteral("amount"),cost.amount},{QStringLiteral("currency"),cost.currency},{QStringLiteral("basis"),cost.basis},
        {QStringLiteral("source"),cost.source},{QStringLiteral("observedDate"),cost.observedDate}};
    QSqlQuery q(db.connection()); q.prepare(QStringLiteral("INSERT INTO app_setting(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value"));
    q.addBindValue(key); q.addBindValue(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
    if (!q.exec()) { if (error) *error=q.lastError().text(); return false; }
    if (error) error->clear(); return true;
}
bool clearRouteCost(LogDatabase& db, const QString& manager, const QString& channel, QString* error)
{
    const QString key=costKey(manager,channel);
    if (!db.isOpen() || key.isEmpty()) { if (error) *error=QStringLiteral("Invalid route"); return false; }
    QSqlQuery q(db.connection()); q.prepare(QStringLiteral("DELETE FROM app_setting WHERE key=?")); q.addBindValue(key);
    if (!q.exec()) { if (error) *error=q.lastError().text(); return false; }
    if (error) error->clear(); return true;
}
RouteCostOrigin routeCostOrigin(const LogDatabase& db)
{
    if (!db.isOpen() || !db.fileIdentityMatchesOpen()) return {};
    return {db.path(),db.identity(),db.openFileIdentity(),db.openGeneration(),db.dataVersion()};
}
bool changeRouteCostGuarded(LogDatabase& db, const RouteCostOrigin& origin, const QString& manager, const QString& channel,
                            const std::optional<RouteCost>& cost, QString* error)
{
    const auto fail=[&](const QString& text) { if (error) *error=text; return false; };
    if (!db.isOpen() || origin.path.isEmpty() || origin.identity.isEmpty() || origin.fileIdentity.isEmpty() || origin.dataVersion<0)
        return fail(QStringLiteral("The reviewed log or plan changed; reopen the cost editor."));
    QSqlDatabase sql=db.connection(); QSqlQuery begin(sql);
    if (!begin.exec(QStringLiteral("BEGIN IMMEDIATE"))) return fail(begin.lastError().text());
    if (db.path()!=origin.path || db.identity()!=origin.identity || db.openGeneration()!=origin.generation ||
        db.openFileIdentity()!=origin.fileIdentity || !db.fileIdentityMatchesOpen() || db.dataVersion()!=origin.dataVersion) {
        sql.rollback(); return fail(QStringLiteral("The reviewed log or plan changed; reopen the cost editor."));
    }
    QString detail;
    if (!(cost ? saveRouteCost(db,manager,channel,*cost,&detail) : clearRouteCost(db,manager,channel,&detail))) {
        sql.rollback(); return fail(detail);
    }
    if (!sql.commit()) { const QString detail=sql.lastError().text(); sql.rollback(); return fail(detail); }
    if (error) error->clear(); return true;
}
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
    QHash<QPair<QString,QString>,QSharedPointer<AlternativeGroup>> alternativeGroups;
    for (const auto& award : results) {
        if (!goals.contains(award.id)) continue;
        for (const AwardItem& item : award.items) {
            if (!item.countsForAward || item.confirmed()) continue;
            const auto alternatives=contributions.qsoIds.value(award.id).value(item.key);
            if (alternatives.isEmpty()) continue;
            ++result.uniqueBenefits;
            const auto group=QSharedPointer<AlternativeGroup>::create();
            group->ids=alternatives.values(); std::sort(group->ids.begin(),group->ids.end());
            alternativeGroups.insert({award.id,item.key},group);
            for (qint64 id : alternatives) {
                Benefit benefit{award.id,item.key,awards::creditsFor(award.id,filter),group};
                Candidate& candidate=candidates[id]; candidate.qsoId=id; candidate.benefits.append(benefit);
            }
        }
    }
    const QDate today=QDateTime::currentDateTimeUtc().date();
    for (auto it=candidates.begin(); it!=candidates.end(); ++it) {
        Candidate& candidate=it.value();
        const auto record=db.record(candidate.qsoId);
        if (!record) { result.error=QStringLiteral("QSO unavailable while planning"); result.candidates.clear(); return result; }
        const auto meta=db.meta(candidate.qsoId);
        if (!meta || meta->deleted || meta->uuid.isEmpty()) { result.error=QStringLiteral("QSO identity unavailable while planning"); result.candidates.clear(); return result; }
        candidate.qsoUuid=meta->uuid; candidate.revision=meta->revision;
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
            if (state.sent.compare(QLatin1String("Y"),Qt::CaseInsensitive)==0 && state.rcvd.compare(QLatin1String("Y"),Qt::CaseInsensitive)!=0) {
                candidate.pendingServices.append(state.service);
                candidate.pendingDetails.append(pendingSince(state,today));
            }
            if (state.service==QLatin1String("card")) {
                const QString via=state.via.trimmed().toUpper();
                if (via==QLatin1String("B") || via==QLatin1String("D")) candidate.paperChannel=via;
            }
        }
        std::sort(candidate.benefits.begin(),candidate.benefits.end(),[](const Benefit& a,const Benefit& b) { return a.award==b.award ? a.key<b.key : a.award<b.award; });
    }
    for (auto it=alternativeGroups.begin(); it!=alternativeGroups.end(); ++it) {
        const QStringList accepted=awards::creditsFor(it.key().first,filter);
        for (qint64 id : it.value()->ids) {
            const auto candidate=candidates.constFind(id); if (candidate==candidates.cend()) continue;
            for (const QString& service : candidate->pendingServices) {
                if (accepted.contains(service)) { it.value()->pendingIds.append(id); break; }
            }
        }
    }
    for (auto it=candidates.cbegin(); it!=candidates.cend(); ++it) result.candidates.append(it.value());
    std::sort(result.candidates.begin(),result.candidates.end(),[](const Candidate& a,const Candidate& b) { return a.qsoId<b.qsoId; });
    // Greedy coverage: maximize still-uncovered award/items; on ties, prefer
    // already sent/unconfirmed accepted services, then the lower QSO ID.
    // This is explanatory prioritization, not a minimum-cost or success model.
    using Key=QPair<QString,QString>;
    struct Score { int remaining; int pending; qint64 id; int index; int generation; };
    struct Less { bool operator()(const Score& a,const Score& b) const {
        if (a.remaining!=b.remaining) return a.remaining<b.remaining;
        if (a.pending!=b.pending) return a.pending<b.pending;
        return a.id>b.id;
    }};
    struct Work { int remaining{0}; int pending{0}; int generation{0}; bool chosen{false}; };
    QList<Work> work(result.candidates.size());
    QHash<Key,QList<int>> owners;
    auto alreadySent=[](const Candidate& candidate,const Benefit& benefit) {
        for (const QString& service : candidate.pendingServices)
            if (benefit.acceptedServices.contains(service)) return true;
        return false;
    };
    std::priority_queue<Score,std::vector<Score>,Less> queue;
    for (int i=0; i<result.candidates.size(); ++i) {
        const Candidate& candidate=result.candidates.at(i);
        for (const Benefit& benefit : candidate.benefits) {
            owners[{benefit.award,benefit.key}].append(i); ++work[i].remaining;
            if (alreadySent(candidate,benefit)) ++work[i].pending;
        }
        queue.push({work[i].remaining,work[i].pending,candidate.qsoId,i,0});
    }
    QSet<Key> covered; int rank=0;
    while (!queue.empty()) {
        const Score score=queue.top(); queue.pop(); Work& current=work[score.index];
        if (current.chosen || score.generation!=current.generation || score.remaining<=0) continue;
        current.chosen=true; Candidate& chosen=result.candidates[score.index]; chosen.priorityRank=++rank;
        for (const Benefit& benefit : chosen.benefits) {
            const Key key{benefit.award,benefit.key}; if (covered.contains(key)) continue;
            covered.insert(key); chosen.marginalBenefits.append(key);
            if (alreadySent(chosen,benefit)) ++chosen.pendingMarginalCount;
            for (int index : owners.value(key)) {
                Work& other=work[index]; if (other.chosen) continue;
                const Candidate& candidate=result.candidates.at(index);
                const auto found=std::find_if(candidate.benefits.cbegin(),candidate.benefits.cend(),[&](const Benefit& b) { return b.award==key.first && b.key==key.second; });
                --other.remaining;
                if (found!=candidate.benefits.cend() && alreadySent(candidate,*found)) --other.pending;
                ++other.generation; queue.push({other.remaining,other.pending,candidate.qsoId,index,other.generation});
            }
        }
    }
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
        result.paperRoutes.append({it.key().first,it.key().second,it->ids,int(it->benefits.size()),routeCost(db,it.key().first,it.key().second)});
    return result;
}
}
