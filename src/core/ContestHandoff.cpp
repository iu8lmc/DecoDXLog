#include "core/ContestHandoff.h"
#include "core/LogDatabase.h"
#include <QDateTime>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <cmath>

namespace decolog::core::contesthandoff {
namespace {
const QString incomingPrefix=QStringLiteral("contest_handoff.in."), outgoingPrefix=QStringLiteral("contest_handoff.out.");
const QString statePrefix=QStringLiteral("contest_handoff.state.");
const QString remoteStatePrefix=QStringLiteral("contest_handoff.remote.");
const QString reminderPrefix=QStringLiteral("contest_handoff.reminder.");
const QString historyPrefix=QStringLiteral("contest_handoff.history.");
struct Savepoint {
    QSqlDatabase db; bool active{false}; QString error;
    explicit Savepoint(QSqlDatabase database):db(database) { QSqlQuery q(db); active=q.exec(QStringLiteral("SAVEPOINT handoff_history")); if (!active) error=q.lastError().text(); }
    ~Savepoint() { if (active) { QSqlQuery q(db); q.exec(QStringLiteral("ROLLBACK TO SAVEPOINT handoff_history")); q.exec(QStringLiteral("RELEASE SAVEPOINT handoff_history")); } }
    bool commit() { QSqlQuery q(db); if (!q.exec(QStringLiteral("RELEASE SAVEPOINT handoff_history"))) { error=q.lastError().text(); return false; } active=false; return true; }
};
int stateOrder(const QString& state) {
    if (state==QLatin1String("offered")) return 0;
    if (state==QLatin1String("pending")) return 1;
    if (state==QLatin1String("accepted") || state==QLatin1String("declined")) return 2;
    if (state==QLatin1String("completed")) return 3;
    return -1;
}
bool appendHistory(QSqlDatabase db,const QString& id,bool outgoing,const QString& previous,const QString& state,QString* error) {
    const auto key=historyPrefix+(outgoing?QStringLiteral("out."):QStringLiteral("in."))+id+QStringLiteral(".")+QString::number(stateOrder(state))+QStringLiteral(".")+state;
    const QJsonObject event{{QStringLiteral("previous"),previous},{QStringLiteral("state"),state},{QStringLiteral("atUtc"),QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
    QSqlQuery q(db); q.prepare(QStringLiteral("INSERT INTO app_setting(key,value) VALUES(?,?)")); q.addBindValue(key);
    q.addBindValue(QString::fromUtf8(QJsonDocument(event).toJson(QJsonDocument::Compact)));
    if (!q.exec()) { if (error) *error=q.lastError().text(); return false; }
    return true;
}
bool identifier(const QString& value) {
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_.:-]{1,128}$")); return re.match(value).hasMatch();
}
bool valid(const Offer& o) {
    static const QRegularExpression uuid(QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"));
    static const QRegularExpression call(QStringLiteral("^(?=.{3,24}$)(?=.*[A-Z])(?=.*[0-9])[A-Z0-9]+(?:/[A-Z0-9]+)*$"));
    const auto when=QDateTime::fromString(o.createdUtc,Qt::ISODate);
    const auto scheduled=QDateTime::fromString(o.scheduledUtc,Qt::ISODate);
    return uuid.match(o.id).hasMatch() && identifier(o.scope) && identifier(o.sender) && identifier(o.recipient) && o.sender!=o.recipient
        && call.match(o.call).hasMatch() && o.frequencyHz>=1000 && o.frequencyHz<=1000000000000LL
        && when.isValid() && when.timeSpec()==Qt::UTC && when.toUTC().toString(Qt::ISODate)==o.createdUtc
        && (o.scheduledUtc.isEmpty() || (scheduled.isValid() && scheduled.timeSpec()==Qt::UTC && scheduled.toUTC().toString(Qt::ISODate)==o.scheduledUtc));
}
std::optional<Offer> parse(const QJsonObject& o) {
    const bool sked=o.value("version")==QJsonValue(2);
    if ((!sked && (o.size()!=8 || o.value("version")!=QJsonValue(1))) || (sked && (o.size()!=9 || !o.value("scheduledUtc").isString() || o.value("scheduledUtc").toString().isEmpty())) || !o.value("frequencyHz").isDouble()) return {};
    const double f=o.value("frequencyHz").toDouble(); if (!std::isfinite(f) || f!=std::floor(f) || f<1000 || f>1000000000000.0) return {};
    Offer a{o.value("id").toString(),o.value("scope").toString(),o.value("sender").toString(),o.value("recipient").toString(),
            o.value("call").toString(),qint64(f),o.value("createdUtc").toString()};
    if (sked) a.scheduledUtc=o.value("scheduledUtc").toString();
    return valid(a) ? std::optional<Offer>(a) : std::nullopt;
}
}
QJsonObject offerFrame(const Offer& o) {
    if (!valid(o)) return {};
    QJsonObject frame{{"version",o.scheduledUtc.isEmpty()?1:2},{"id",o.id},{"scope",o.scope},{"sender",o.sender},{"recipient",o.recipient},
            {"call",o.call},{"frequencyHz",double(o.frequencyHz)},{"createdUtc",o.createdUtc}};
    if (!o.scheduledUtc.isEmpty()) frame.insert("scheduledUtc",o.scheduledUtc);
    return frame;
}
static Result store(LogDatabase& db,const QString& prefix,const Offer& offer) {
    if (!db.isOpen() || !valid(offer)) return {false,false,QStringLiteral("Invalid handoff offer")};
    Savepoint save(db.connection()); if (!save.active) return {false,false,save.error};
    const QString key=prefix+offer.id, value=QString::fromUtf8(QJsonDocument(offerFrame(offer)).toJson(QJsonDocument::Compact));
    QSqlQuery q(db.connection()); q.prepare(QStringLiteral("INSERT OR IGNORE INTO app_setting(key,value) SELECT ?,? WHERE (SELECT COUNT(*) FROM app_setting WHERE substr(key,1,?)=?)<10000"));
    q.addBindValue(key); q.addBindValue(value); q.addBindValue(prefix.size()); q.addBindValue(prefix);
    if (!q.exec()) return {false,false,q.lastError().text()};
    if (q.numRowsAffected()==1) {
        QString error; const bool outgoing=prefix==outgoingPrefix;
        if (!appendHistory(db.connection(),offer.id,outgoing,QStringLiteral("none"),outgoing?QStringLiteral("offered"):QStringLiteral("pending"),&error)) return {false,false,error};
        if (!save.commit()) return {false,false,save.error};
        return {true,false,{}};
    }
    QSqlQuery read(db.connection()); read.prepare(QStringLiteral("SELECT value FROM app_setting WHERE key=?")); read.addBindValue(key);
    if (!read.exec()) return {false,false,read.lastError().text()};
    if (!read.next()) return {false,false,QStringLiteral("Handoff storage limit reached")};
    if (read.value(0).toString()!=value) return {false,false,QStringLiteral("Handoff identity conflicts with an existing offer")};
    return {true,true,{}};
}
Result receiveOffer(LogDatabase& db,const QString& scope,const QString& recipient,const QString& envelopeSender,const QJsonObject& body) {
    const auto offer=parse(body);
    if (!db.isOpen() || !offer || offer->scope!=scope || offer->recipient!=recipient || offer->sender!=envelopeSender)
        return {false,false,QStringLiteral("Invalid or misaddressed handoff offer")};
    return store(db,incomingPrefix,*offer);
}
Result storeOutgoing(LogDatabase& db,const Offer& offer) { return store(db,outgoingPrefix,offer); }
static QList<Offer> list(const LogDatabase& db,const QString& prefix,const QString& scope,const QString& node,bool outgoing,QString* error) {
    if (error) error->clear(); QList<Offer> out;
    if (!db.isOpen() || !identifier(scope) || !identifier(node)) { if (error) *error=QStringLiteral("Invalid handoff listing"); return {}; }
    QSqlQuery q(db.connection()); q.prepare(QStringLiteral("SELECT a.key,a.value,s.value FROM app_setting a LEFT JOIN app_setting s ON s.key=? || substr(a.key,?) WHERE substr(a.key,1,?)=? ORDER BY a.key LIMIT 10001"));
    q.addBindValue(outgoing ? remoteStatePrefix : statePrefix); q.addBindValue(prefix.size()+1);
    q.addBindValue(prefix.size()); q.addBindValue(prefix); if (!q.exec()) { if (error) *error=q.lastError().text(); return {}; }
    int count=0;
    while(q.next()) {
        if (++count>10000) { if (error) *error=QStringLiteral("Too many handoff offers"); return {}; }
        QJsonParseError parseError; const auto doc=QJsonDocument::fromJson(q.value(1).toString().toUtf8(),&parseError);
        auto offer=doc.isObject()?parse(doc.object()):std::nullopt;
        if (parseError.error!=QJsonParseError::NoError || !offer || prefix+offer->id!=q.value(0).toString()) {
            if (error) *error=QStringLiteral("Invalid stored handoff offer"); return {};
        }
        if (!outgoing) {
            offer->state=q.value(2).isNull()?QStringLiteral("pending"):q.value(2).toString();
            if (offer->state!=QLatin1String("pending") && offer->state!=QLatin1String("accepted") && offer->state!=QLatin1String("declined") && offer->state!=QLatin1String("completed")) {
                if (error) *error=QStringLiteral("Invalid stored handoff state"); return {};
            }
        } else if (!q.value(2).isNull()) {
            offer->state=q.value(2).toString();
            if (offer->state!=QLatin1String("pending") && offer->state!=QLatin1String("accepted") && offer->state!=QLatin1String("declined") && offer->state!=QLatin1String("completed")) {
                if (error) *error=QStringLiteral("Invalid stored remote handoff state"); return {};
            }
        }
        if (offer->scope==scope && (outgoing ? offer->sender : offer->recipient)==node) out << *offer;
    }
    if (q.lastError().isValid()) { if (error) *error=q.lastError().text(); return {}; }
    return out;
}
QList<Offer> incomingOffers(const LogDatabase& db,const QString& scope,const QString& recipient,QString* error) { return list(db,incomingPrefix,scope,recipient,false,error); }
QList<Offer> outgoingOffers(const LogDatabase& db,const QString& scope,const QString& sender,QString* error) { return list(db,outgoingPrefix,scope,sender,true,error); }
Result setIncomingState(LogDatabase& db,const QString& scope,const QString& recipient,const QString& id,const QString& state) {
    QString error; const auto offers=incomingOffers(db,scope,recipient,&error);
    if (!error.isEmpty()) return {false,false,error};
    for (const auto& offer : offers) if (offer.id==id) {
        const auto old=offer.state;
        if (old==state) return {true,true,{}};
        if (!((old==QLatin1String("pending") && (state==QLatin1String("accepted") || state==QLatin1String("declined"))) ||
              (old==QLatin1String("accepted") && state==QLatin1String("completed"))))
            return {false,false,QStringLiteral("Invalid handoff state transition")};
        Savepoint save(db.connection()); if (!save.active) return {false,false,save.error};
        const auto value=QString::fromUtf8(QJsonDocument(offerFrame(offer)).toJson(QJsonDocument::Compact));
        QSqlQuery q(db.connection());
        q.prepare(QStringLiteral("INSERT INTO app_setting(key,value) SELECT ?,? WHERE EXISTS(SELECT 1 FROM app_setting WHERE key=? AND value=?) AND (?='pending' OR EXISTS(SELECT 1 FROM app_setting WHERE key=? AND value=?)) ON CONFLICT(key) DO UPDATE SET value=excluded.value WHERE app_setting.value=?"));
        const auto key=statePrefix+id; q.addBindValue(key); q.addBindValue(state); q.addBindValue(incomingPrefix+id); q.addBindValue(value);
        q.addBindValue(old); q.addBindValue(key); q.addBindValue(old); q.addBindValue(old);
        if (!q.exec()) return {false,false,q.lastError().text()};
        if (q.numRowsAffected()!=1) return {false,false,QStringLiteral("Handoff changed; refresh and retry")};
        if (!appendHistory(db.connection(),id,false,old,state,&error)) return {false,false,error};
        if (!save.commit()) return {false,false,save.error};
        return {true,false,{}};
    }
    return {false,false,QStringLiteral("Handoff offer not found")};
}
Result receiveRemoteState(LogDatabase& db,const QString& scope,const QString& sender,const QString& envelopeSender,const QJsonObject& frame,const QString& state) {
    const auto remote=parse(frame);
    if (!remote || remote->scope!=scope || remote->sender!=sender || remote->recipient!=envelopeSender ||
        (state!=QLatin1String("pending") && state!=QLatin1String("accepted") && state!=QLatin1String("declined") && state!=QLatin1String("completed")))
        return {false,false,QStringLiteral("Invalid or misaddressed handoff status")};
    QString error; const auto offers=outgoingOffers(db,scope,sender,&error);
    if (!error.isEmpty()) return {false,false,error};
    for (const auto& offer : offers) if (offer.id==remote->id) {
        if (offerFrame(offer)!=frame) return {false,false,QStringLiteral("Handoff offer does not match")};
        const auto old=offer.state.isEmpty()?QStringLiteral(""):offer.state;
        if (old==state || (!old.isEmpty() && state==QLatin1String("pending")) || (old==QLatin1String("completed") && state==QLatin1String("accepted"))) return {true,true,{}};
        if (!old.isEmpty() && !(old==QLatin1String("pending") || (old==QLatin1String("accepted") && state==QLatin1String("completed"))))
            return {false,false,QStringLiteral("Conflicting handoff status")};
        Savepoint save(db.connection()); if (!save.active) return {false,false,save.error};
        const auto key=remoteStatePrefix+offer.id;
        const auto value=QString::fromUtf8(QJsonDocument(frame).toJson(QJsonDocument::Compact));
        QSqlQuery q(db.connection()); q.prepare(QStringLiteral("INSERT INTO app_setting(key,value) SELECT ?,? WHERE EXISTS(SELECT 1 FROM app_setting WHERE key=? AND value=?) AND (?='' OR EXISTS(SELECT 1 FROM app_setting WHERE key=? AND value=?)) ON CONFLICT(key) DO UPDATE SET value=excluded.value WHERE app_setting.value=?"));
        q.addBindValue(key); q.addBindValue(state); q.addBindValue(outgoingPrefix+offer.id); q.addBindValue(value);
        q.addBindValue(old); q.addBindValue(key); q.addBindValue(old); q.addBindValue(old);
        if (!q.exec()) return {false,false,q.lastError().text()};
        if (q.numRowsAffected()!=1) return {false,false,QStringLiteral("Handoff changed; retry status")};
        if (!appendHistory(db.connection(),offer.id,true,old.isEmpty()?QStringLiteral("offered"):old,state,&error)) return {false,false,error};
        if (!save.commit()) return {false,false,save.error};
        return {true,false,{}};
    }
    return {false,false,QStringLiteral("Handoff offer not found")};
}
QList<HistoryEntry> history(const LogDatabase& db,const QString& scope,const QString& node,const QString& id,bool outgoing,QString* error) {
    QString localError; const auto offers=outgoing?outgoingOffers(db,scope,node,&localError):incomingOffers(db,scope,node,&localError);
    if (error) error->clear();
    if (!localError.isEmpty()) { if (error) *error=localError; return {}; }
    bool found=false; for (const auto& offer:offers) if (offer.id==id) { found=true; break; }
    if (!found) { if (error) *error=QStringLiteral("Handoff offer not found"); return {}; }
    const auto prefix=historyPrefix+(outgoing?QStringLiteral("out."):QStringLiteral("in."))+id+QStringLiteral(".");
    QSqlQuery q(db.connection()); q.prepare(QStringLiteral("SELECT key,value FROM app_setting WHERE substr(key,1,?)=? ORDER BY key LIMIT 5"));
    q.addBindValue(prefix.size()); q.addBindValue(prefix); if (!q.exec()) { if (error) *error=q.lastError().text(); return {}; }
    QList<HistoryEntry> entries;
    while (q.next()) {
        QJsonParseError pe; const auto doc=QJsonDocument::fromJson(q.value(1).toString().toUtf8(),&pe); const auto o=doc.object();
        const auto previous=o.value("previous").toString(), state=o.value("state").toString(), at=o.value("atUtc").toString();
        const auto dt=QDateTime::fromString(at,Qt::ISODateWithMs);
        if (entries.size()>=4 || pe.error!=QJsonParseError::NoError || !doc.isObject() || o.size()!=3 || previous.isEmpty() || stateOrder(state)<0 ||
            !dt.isValid() || dt.timeSpec()!=Qt::UTC || dt.toString(Qt::ISODateWithMs)!=at || q.value(0).toString()!=prefix+QString::number(stateOrder(state))+QStringLiteral(".")+state) {
            if (error) *error=QStringLiteral("Invalid stored handoff history"); return {};
        }
        entries << HistoryEntry{previous,state,at};
    }
    if (q.lastError().isValid()) { if (error) *error=q.lastError().text(); return {}; }
    return entries;
}
Result claimSkedReminderSnapshot(LogDatabase& db,const QString& scope,const QString& recipient,const Offer& offer,const QDateTime& nowUtc) {
    if (!nowUtc.isValid() || nowUtc.timeSpec()!=Qt::UTC) return {false,false,QStringLiteral("Invalid reminder time")};
    if (!db.isOpen() || !identifier(scope) || !identifier(recipient) || offer.scope!=scope || offer.recipient!=recipient || !valid(offer))
        return {false,false,QStringLiteral("Invalid or misaddressed handoff offer")};
    const auto scheduled=QDateTime::fromString(offer.scheduledUtc,Qt::ISODate);
    const auto seconds=scheduled.secsTo(nowUtc);
    if (offer.state!=QLatin1String("accepted") || !scheduled.isValid() || seconds < -300 || seconds > 600)
        return {false,false,QStringLiteral("Sked is not eligible for a reminder")};
    const auto value=QString::fromUtf8(QJsonDocument(offerFrame(offer)).toJson(QJsonDocument::Compact));
    QSqlQuery q(db.connection()); q.prepare(QStringLiteral("INSERT OR IGNORE INTO app_setting(key,value) SELECT ?,'1' WHERE EXISTS(SELECT 1 FROM app_setting WHERE key=? AND value=?) AND EXISTS(SELECT 1 FROM app_setting WHERE key=? AND value='accepted')"));
    q.addBindValue(reminderPrefix+offer.id); q.addBindValue(incomingPrefix+offer.id); q.addBindValue(value); q.addBindValue(statePrefix+offer.id);
    if (!q.exec()) return {false,false,q.lastError().text()};
    if (q.numRowsAffected()==1) return {true,false,{}};
    QSqlQuery read(db.connection()); read.prepare(QStringLiteral("SELECT value FROM app_setting WHERE key=? AND EXISTS(SELECT 1 FROM app_setting WHERE key=? AND value=?) AND EXISTS(SELECT 1 FROM app_setting WHERE key=? AND value='accepted')"));
    read.addBindValue(reminderPrefix+offer.id); read.addBindValue(incomingPrefix+offer.id); read.addBindValue(value); read.addBindValue(statePrefix+offer.id);
    if (!read.exec()) return {false,false,read.lastError().text()};
    if (read.next() && read.value(0).toString()==QLatin1String("1")) return {true,true,{}};
    return {false,false,QStringLiteral("Sked changed; reminder not recorded")};
}
Result claimSkedReminder(LogDatabase& db,const QString& scope,const QString& recipient,const QString& id,const QDateTime& nowUtc) {
    if (!nowUtc.isValid() || nowUtc.timeSpec()!=Qt::UTC) return {false,false,QStringLiteral("Invalid reminder time")};
    QString error; const auto offers=incomingOffers(db,scope,recipient,&error);
    if (!error.isEmpty()) return {false,false,error};
    for (const auto& offer:offers) if (offer.id==id) return claimSkedReminderSnapshot(db,scope,recipient,offer,nowUtc);
    return {false,false,QStringLiteral("Handoff offer not found")};
}
}
