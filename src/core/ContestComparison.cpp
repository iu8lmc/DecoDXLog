#include "core/ContestComparison.h"
#include <QDateTime>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <limits>
#include <QMap>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimeZone>

namespace decolog::core::contestcomparison {
namespace {
const QString goalPrefix=QStringLiteral("contest_goal.v1.");
bool validGoalKey(const QString &contest, QDate from, QDate to)
{
    return !contest.isEmpty() && contest.size()<=64 && from.isValid() && to.isValid() && from<=to && from.daysTo(to)<=366;
}
QString goalKey(const QString &contest, QDate from, QDate to)
{
    const QByteArray canonical=QJsonDocument(QJsonArray{contest,from.toString(Qt::ISODate),to.toString(Qt::ISODate)}).toJson(QJsonDocument::Compact);
    return goalPrefix+QString::fromLatin1(QCryptographicHash::hash(canonical,QCryptographicHash::Sha256).toHex());
}
QString encodeGoal(const QString &contest,QDate from,QDate to,const Goal &goal)
{
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"format",1},{"contest",contest},{"from",from.toString(Qt::ISODate)},
        {"to",to.toString(Qt::ISODate)},{"target",goal.target},{"revision",goal.revision}}).toJson(QJsonDocument::Compact));
}
bool readGoal(const QSqlDatabase &db,const QString &contest,QDate from,QDate to,Goal &goal,QString &error)
{
    QSqlQuery q(db); q.prepare(QStringLiteral("SELECT value FROM app_setting WHERE key=?")); q.addBindValue(goalKey(contest,from,to));
    if (!q.exec()) { error=q.lastError().text(); return false; }
    if (!q.next()) { goal={}; return true; }
    QJsonParseError parse; const auto doc=QJsonDocument::fromJson(q.value(0).toString().toUtf8(),&parse); const auto o=doc.object();
    const double target=o.value("target").toDouble(-1), revision=o.value("revision").toDouble(-1);
    if (parse.error!=QJsonParseError::NoError || !doc.isObject() || o.value("format").toInt()!=1 || o.value("contest").toString()!=contest ||
        o.value("from").toString()!=from.toString(Qt::ISODate) || o.value("to").toString()!=to.toString(Qt::ISODate) ||
        target<0 || target>100000 || target!=int(target) || revision<1 || revision>=std::numeric_limits<int>::max() || revision!=int(revision)) {
        error=QStringLiteral("Invalid saved contest goal"); return false;
    }
    goal={int(target),int(revision)}; return true;
}
bool writeGoal(const QSqlDatabase &db,const QString &contest,QDate from,QDate to,int target,int expected,Goal &out,QString &error)
{
    Goal current; if (!readGoal(db,contest,from,to,current,error)) return false;
    if (current.revision!=expected || expected>=std::numeric_limits<int>::max()-1) { error=QStringLiteral("Goal changed; reload before saving"); return false; }
    out={target,expected+1}; QSqlQuery q(db);
    if (expected==0) {
        q.prepare(QStringLiteral("INSERT INTO app_setting(key,value) SELECT ?,? WHERE (SELECT COUNT(*) FROM app_setting WHERE substr(key,1,?)=?)<1000"));
        q.addBindValue(goalKey(contest,from,to)); q.addBindValue(encodeGoal(contest,from,to,out)); q.addBindValue(goalPrefix.size()); q.addBindValue(goalPrefix);
    } else {
        q.prepare(QStringLiteral("UPDATE app_setting SET value=? WHERE key=?")); q.addBindValue(encodeGoal(contest,from,to,out)); q.addBindValue(goalKey(contest,from,to));
    }
    if (!q.exec() || q.numRowsAffected()!=1) { error=q.lastError().isValid() ? q.lastError().text() : QStringLiteral("Saved goal limit reached"); return false; }
    return true;
}
}
GoalPair loadGoals(const QSqlDatabase &db,const QString &contestId,QDate fromA,QDate toA,QDate fromB,QDate toB)
{
    GoalPair result; const QString contest=contestId.trimmed().toUpper();
    if (!db.isOpen() || !validGoalKey(contest,fromA,toA) || !validGoalKey(contest,fromB,toB)) { result.error=QStringLiteral("Invalid contest or goal period"); return result; }
    if (!readGoal(db,contest,fromA,toA,result.a,result.error)) return result;
    readGoal(db,contest,fromB,toB,result.b,result.error); return result;
}
GoalPair saveGoals(const QSqlDatabase &database,const QString &expectedIdentity,const QString &contestId,QDate fromA,QDate toA,QDate fromB,QDate toB,int targetA,int targetB,int revisionA,int revisionB)
{
    GoalPair result; const QString contest=contestId.trimmed().toUpper(); QSqlDatabase db=database;
    if (!db.isOpen() || expectedIdentity.isEmpty() || !validGoalKey(contest,fromA,toA) || !validGoalKey(contest,fromB,toB) ||
        targetA<0 || targetA>100000 || targetB<0 || targetB>100000 || revisionA<0 || revisionB<0 ||
        (fromA==fromB && toA==toB && (targetA!=targetB || revisionA!=revisionB))) {
        result.error=QStringLiteral("Invalid goals or duplicate periods with different goals"); return result;
    }
    QSqlQuery begin(db); if (!begin.exec(QStringLiteral("BEGIN IMMEDIATE"))) { result.error=begin.lastError().text(); return result; }
    QSqlQuery identity(db); if (!identity.exec(QStringLiteral("SELECT uuid FROM log_identity WHERE id=1")) || !identity.next() || identity.value(0).toString()!=expectedIdentity) {
        db.rollback(); result.error=QStringLiteral("Log identity changed"); return result;
    }
    if (!writeGoal(db,contest,fromA,toA,targetA,revisionA,result.a,result.error)) { db.rollback(); return result; }
    if (fromA==fromB && toA==toB) result.b=result.a;
    else if (!writeGoal(db,contest,fromB,toB,targetB,revisionB,result.b,result.error)) { db.rollback(); return result; }
    if (!db.commit()) { result.error=db.lastError().text(); db.rollback(); }
    return result;
}
Result compare(const QSqlDatabase &db, const QString &contestId, QDate fromA, QDate toA, QDate fromB, QDate toB)
{
    Result out; const QString contest=contestId.trimmed().toUpper();
    if (!db.isOpen() || contest.isEmpty() || contest.size()>64 || !fromA.isValid() || !toA.isValid() ||
        !fromB.isValid() || !toB.isValid() || fromA>toA || fromB>toB || !toA.addDays(1).isValid() || !toB.addDays(1).isValid() ||
        fromA.daysTo(toA)>366 || fromB.daysTo(toB)>366) {
        out.error=QStringLiteral("Select a contest ID and two valid UTC date ranges of at most 367 days."); return out;
    }
    const auto bound=[](QDate day) { return day.toString(Qt::ISODate)+QStringLiteral("T00:00:00"); };
    const QString a0=bound(fromA), a1=bound(toA.addDays(1)), b0=bound(fromB), b1=bound(toB.addDays(1));
    QSqlQuery q(db); q.setForwardOnly(true);
    q.prepare(QStringLiteral("SELECT qso_datetime_on,COALESCE(band,''),"
        "CASE WHEN json_valid(adif_extra) AND json_type(adif_extra,'$.OPERATOR')='text' THEN json_extract(adif_extra,'$.OPERATOR') ELSE '' END,"
        "CASE WHEN json_valid(adif_extra) AND json_type(adif_extra,'$.MY_RIG')='text' THEN json_extract(adif_extra,'$.MY_RIG') ELSE '' END "
        "FROM qso WHERE deleted=0 AND ((qso_datetime_on>=? AND qso_datetime_on<?) OR (qso_datetime_on>=? AND qso_datetime_on<?)) "
        "AND CASE WHEN json_valid(adif_extra) AND json_type(adif_extra,'$.CONTEST_ID')='text' THEN UPPER(TRIM(json_extract(adif_extra,'$.CONTEST_ID'))) ELSE '' END=? LIMIT 100001"));
    q.addBindValue(a0); q.addBindValue(a1); q.addBindValue(b0); q.addBindValue(b1); q.addBindValue(contest);
    if (!q.exec()) { out.error=q.lastError().text(); return out; }
    QMap<QPair<QString,QString>,QPair<int,int>> counts; int scanned=0;
    while (q.next()) {
        if (++scanned>100000) { out.error=QStringLiteral("More than 100,000 QSO match; narrow the periods."); out.rows.clear(); out.totalA=out.totalB=0; return out; }
        const QString raw=q.value(0).toString(); const auto timestamp=QDateTime::fromString(raw,Qt::ISODateWithMs);
        if (!raw.endsWith(QLatin1Char('Z'),Qt::CaseInsensitive) || !timestamp.isValid()) { ++out.invalidTimestamp; continue; }
        const QDateTime utc=timestamp.toUTC(); const QDate day=utc.date();
        const bool a=day>=fromA && day<=toA, b=day>=fromB && day<=toB;
        if (!a && !b) continue;
        if (a) ++out.totalA;
        if (b) ++out.totalB;
        const QList<QPair<QString,QString>> dims{{QStringLiteral("band"),q.value(1).toString().trimmed().toLower()},
            {QStringLiteral("operator"),q.value(2).toString().trimmed().toUpper()},
            {QStringLiteral("radio"),q.value(3).toString().trimmed()},
            {QStringLiteral("hour"),utc.toString(QStringLiteral("HH:00"))}};
        for (const auto &key:dims) {
            if (key.second.size()>256) { out.error=QStringLiteral("A recorded value exceeds the report limit."); out.rows.clear(); out.totalA=out.totalB=0; return out; }
            auto &count=counts[key]; if (a) ++count.first; if (b) ++count.second;
        }
        if (counts.size()>4000) { out.error=QStringLiteral("Too many report groups; narrow the periods."); out.rows.clear(); out.totalA=out.totalB=0; return out; }
    }
    if (q.lastError().isValid()) { out.error=q.lastError().text(); out.rows.clear(); out.totalA=out.totalB=0; return out; }
    for (auto it=counts.cbegin();it!=counts.cend();++it)
        out.rows << QVariantMap{{QStringLiteral("dimension"),it.key().first},{QStringLiteral("value"),it.key().second},
            {QStringLiteral("a"),it.value().first},{QStringLiteral("b"),it.value().second},{QStringLiteral("delta"),it.value().second-it.value().first}};
    return out;
}
}
