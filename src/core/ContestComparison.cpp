#include "core/ContestComparison.h"
#include "core/ContestReport.h"
#include <QRegularExpression>
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
Findings compareFindings(const contestreport::Report &a, const contestreport::Report &b, const QString &contestId,
                         QDate fromA, QDate toA, QDate fromB, QDate toB)
{
    Findings out;
    const QString contest=contestId.trimmed().toUpper();
    const QRegularExpression header(QStringLiteral("^\\d{4}\\s+CQ\\s+WORLD\\s+WIDE\\s+(CW|SSB|RTTY)\\s+DX\\s+CONTEST$"), QRegularExpression::CaseInsensitiveOption);
    auto valid=[&](const contestreport::Report &report) {
        const auto match=header.match(report.contest.trimmed());
        return report.error.isEmpty() && !report.ownCall.trimmed().isEmpty() && match.hasMatch()
            && QStringLiteral("CQ-WW-")+match.captured(1).toUpper()==contest;
    };
    if (!fromA.isValid() || !toA.isValid() || !fromB.isValid() || !toB.isValid() || fromA>toA || fromB>toB ||
        fromA.daysTo(toA)>366 || fromB.daysTo(toB)>366 || !valid(a) || !valid(b)) {
        out.error=QStringLiteral("Select valid periods and two CQ WW reports matching the contest ID."); return out;
    }
    out.ownCallA=a.ownCall; out.ownCallB=b.ownCall; out.contestA=a.contest; out.contestB=b.contest;
    out.unknownA=a.unrecognizedLines.size(); out.unknownB=b.unrecognizedLines.size();
    auto count=[](const contestreport::Report &report,QDate from,QDate to,int &nil,int &call,int &exchange,int &outside) {
        for (const auto &entry:report.entries) {
            const QDate day=QDate::fromString(entry.date,Qt::ISODate);
            if (!day.isValid() || day<from || day>to) { ++outside; continue; }
            if (entry.kind==QStringLiteral("not_in_log")) ++nil;
            else if (entry.kind==QStringLiteral("incorrect_call")) ++call;
            else if (entry.kind==QStringLiteral("incorrect_exchange")) ++exchange;
        }
    };
    count(a,fromA,toA,out.nilA,out.callA,out.exchangeA,out.outsideA);
    count(b,fromB,toB,out.nilB,out.callB,out.exchangeB,out.outsideB);
    return out;
}
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
const QString goalClock=QStringLiteral("contest_goal.clock");
bool readClock(const QSqlDatabase &db,int &clock,QString &error)
{
    QSqlQuery q(db); q.prepare(QStringLiteral("SELECT value FROM app_setting WHERE key=?")); q.addBindValue(goalClock);
    if (!q.exec()) { error=q.lastError().text(); return false; }
    if (!q.next()) { clock=0; return true; }
    bool ok=false; const int value=q.value(0).toString().toInt(&ok);
    if (!ok || value<0 || value>=std::numeric_limits<int>::max()) { error=QStringLiteral("Invalid contest goal generation"); return false; }
    clock=value; return true;
}
bool advanceClock(const QSqlDatabase &db,int expected,int &next,QString &error)
{
    int current=0; if (!readClock(db,current,error)) return false;
    if (current!=expected || current>=std::numeric_limits<int>::max()-1) { error=QStringLiteral("Goals changed; reload before saving"); return false; }
    next=current+1; QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT INTO app_setting(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value"));
    q.addBindValue(goalClock); q.addBindValue(QString::number(next));
    if (!q.exec()) { error=q.lastError().text(); return false; } return true;
}
bool eraseGoal(const QSqlDatabase &db,const QString &contest,QDate from,QDate to,int revision,QString &error)
{
    Goal current; if (!readGoal(db,contest,from,to,current,error)) return false;
    if (current.revision!=revision) { error=QStringLiteral("Goal changed; reload before removing"); return false; }
    if (!revision) return true;
    QSqlQuery q(db); q.prepare(QStringLiteral("DELETE FROM app_setting WHERE key=?")); q.addBindValue(goalKey(contest,from,to));
    if (!q.exec() || q.numRowsAffected()!=1) { error=QStringLiteral("Cannot remove saved goal"); return false; } return true;
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
    QSqlDatabase snapshot=db;
    if (!snapshot.transaction()) { result.error=snapshot.lastError().text(); return result; }
    if (!readClock(snapshot,result.generation,result.error) || !readGoal(snapshot,contest,fromA,toA,result.a,result.error) ||
        !readGoal(snapshot,contest,fromB,toB,result.b,result.error)) { snapshot.rollback(); return result; }
    if (!snapshot.commit()) { result.error=snapshot.lastError().text(); snapshot.rollback(); }
    return result;
}
GoalPair saveGoals(const QSqlDatabase &database,const QString &expectedIdentity,const QString &contestId,QDate fromA,QDate toA,QDate fromB,QDate toB,int targetA,int targetB,int revisionA,int revisionB,int expectedGeneration)
{
    GoalPair result; const QString contest=contestId.trimmed().toUpper(); QSqlDatabase db=database;
    if (!db.isOpen() || expectedIdentity.isEmpty() || !validGoalKey(contest,fromA,toA) || !validGoalKey(contest,fromB,toB) ||
        targetA<0 || targetA>100000 || targetB<0 || targetB>100000 || revisionA<0 || revisionB<0 || expectedGeneration<0 ||
        (fromA==fromB && toA==toB && (targetA!=targetB || revisionA!=revisionB))) {
        result.error=QStringLiteral("Invalid goals or duplicate periods with different goals"); return result;
    }
    QSqlQuery begin(db); if (!begin.exec(QStringLiteral("BEGIN IMMEDIATE"))) { result.error=begin.lastError().text(); return result; }
    QSqlQuery identity(db); if (!identity.exec(QStringLiteral("SELECT uuid FROM log_identity WHERE id=1")) || !identity.next() || identity.value(0).toString()!=expectedIdentity) {
        db.rollback(); result.error=QStringLiteral("Log identity changed"); return result;
    }
    if (!advanceClock(db,expectedGeneration,result.generation,result.error)) { db.rollback(); return result; }
    if (!writeGoal(db,contest,fromA,toA,targetA,revisionA,result.a,result.error)) { db.rollback(); return result; }
    if (fromA==fromB && toA==toB) result.b=result.a;
    else if (!writeGoal(db,contest,fromB,toB,targetB,revisionB,result.b,result.error)) { db.rollback(); return result; }
    if (!db.commit()) { result.error=db.lastError().text(); db.rollback(); }
    return result;
}
GoalPair removeGoals(const QSqlDatabase &database,const QString &expectedIdentity,const QString &contestId,QDate fromA,QDate toA,QDate fromB,QDate toB,int revisionA,int revisionB,int expectedGeneration)
{
    GoalPair result; const QString contest=contestId.trimmed().toUpper(); QSqlDatabase db=database;
    if (!db.isOpen() || expectedIdentity.isEmpty() || !validGoalKey(contest,fromA,toA) || !validGoalKey(contest,fromB,toB) ||
        revisionA<0 || revisionB<0 || expectedGeneration<0 || (fromA==fromB && toA==toB && revisionA!=revisionB)) {
        result.error=QStringLiteral("Invalid goal removal request"); return result;
    }
    QSqlQuery begin(db); if (!begin.exec(QStringLiteral("BEGIN IMMEDIATE"))) { result.error=begin.lastError().text(); return result; }
    QSqlQuery identity(db);
    if (!identity.exec(QStringLiteral("SELECT uuid FROM log_identity WHERE id=1")) || !identity.next() || identity.value(0).toString()!=expectedIdentity) {
        db.rollback(); result.error=QStringLiteral("Log identity changed"); return result;
    }
    if (!advanceClock(db,expectedGeneration,result.generation,result.error) || !eraseGoal(db,contest,fromA,toA,revisionA,result.error) ||
        ((fromA!=fromB || toA!=toB) && !eraseGoal(db,contest,fromB,toB,revisionB,result.error))) { db.rollback(); return result; }
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
