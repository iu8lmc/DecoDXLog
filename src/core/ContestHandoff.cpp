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
bool identifier(const QString& value) {
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_.:-]{1,128}$")); return re.match(value).hasMatch();
}
bool valid(const Offer& o) {
    static const QRegularExpression uuid(QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"));
    static const QRegularExpression call(QStringLiteral("^(?=.{3,24}$)(?=.*[A-Z])(?=.*[0-9])[A-Z0-9]+(?:/[A-Z0-9]+)*$"));
    const auto when=QDateTime::fromString(o.createdUtc,Qt::ISODate);
    return uuid.match(o.id).hasMatch() && identifier(o.scope) && identifier(o.sender) && identifier(o.recipient) && o.sender!=o.recipient
        && call.match(o.call).hasMatch() && o.frequencyHz>=1000 && o.frequencyHz<=1000000000000LL
        && when.isValid() && when.timeSpec()==Qt::UTC && when.toUTC().toString(Qt::ISODate)==o.createdUtc;
}
std::optional<Offer> parse(const QJsonObject& o) {
    if (o.size()!=8 || o.value("version")!=QJsonValue(1) || !o.value("frequencyHz").isDouble()) return {};
    const double f=o.value("frequencyHz").toDouble(); if (!std::isfinite(f) || f!=std::floor(f) || f<1000 || f>1000000000000.0) return {};
    Offer a{o.value("id").toString(),o.value("scope").toString(),o.value("sender").toString(),o.value("recipient").toString(),
            o.value("call").toString(),qint64(f),o.value("createdUtc").toString()};
    return valid(a) ? std::optional<Offer>(a) : std::nullopt;
}
}
QJsonObject offerFrame(const Offer& o) {
    if (!valid(o)) return {};
    return {{"version",1},{"id",o.id},{"scope",o.scope},{"sender",o.sender},{"recipient",o.recipient},
            {"call",o.call},{"frequencyHz",double(o.frequencyHz)},{"createdUtc",o.createdUtc}};
}
static Result store(LogDatabase& db,const QString& prefix,const Offer& offer) {
    if (!db.isOpen() || !valid(offer)) return {false,false,QStringLiteral("Invalid handoff offer")};
    const QString key=prefix+offer.id, value=QString::fromUtf8(QJsonDocument(offerFrame(offer)).toJson(QJsonDocument::Compact));
    QSqlQuery q(db.connection()); q.prepare(QStringLiteral("INSERT OR IGNORE INTO app_setting(key,value) SELECT ?,? WHERE (SELECT COUNT(*) FROM app_setting WHERE substr(key,1,?)=?)<10000"));
    q.addBindValue(key); q.addBindValue(value); q.addBindValue(prefix.size()); q.addBindValue(prefix);
    if (!q.exec()) return {false,false,q.lastError().text()};
    if (q.numRowsAffected()==1) return {true,false,{}};
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
        const auto value=QString::fromUtf8(QJsonDocument(offerFrame(offer)).toJson(QJsonDocument::Compact));
        QSqlQuery q(db.connection());
        q.prepare(QStringLiteral("INSERT INTO app_setting(key,value) SELECT ?,? WHERE EXISTS(SELECT 1 FROM app_setting WHERE key=? AND value=?) AND (?='pending' OR EXISTS(SELECT 1 FROM app_setting WHERE key=? AND value=?)) ON CONFLICT(key) DO UPDATE SET value=excluded.value WHERE app_setting.value=?"));
        const auto key=statePrefix+id; q.addBindValue(key); q.addBindValue(state); q.addBindValue(incomingPrefix+id); q.addBindValue(value);
        q.addBindValue(old); q.addBindValue(key); q.addBindValue(old); q.addBindValue(old);
        if (!q.exec()) return {false,false,q.lastError().text()};
        if (q.numRowsAffected()!=1) return {false,false,QStringLiteral("Handoff changed; refresh and retry")};
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
        const auto key=remoteStatePrefix+offer.id;
        const auto value=QString::fromUtf8(QJsonDocument(frame).toJson(QJsonDocument::Compact));
        QSqlQuery q(db.connection()); q.prepare(QStringLiteral("INSERT INTO app_setting(key,value) SELECT ?,? WHERE EXISTS(SELECT 1 FROM app_setting WHERE key=? AND value=?) AND (?='' OR EXISTS(SELECT 1 FROM app_setting WHERE key=? AND value=?)) ON CONFLICT(key) DO UPDATE SET value=excluded.value WHERE app_setting.value=?"));
        q.addBindValue(key); q.addBindValue(state); q.addBindValue(outgoingPrefix+offer.id); q.addBindValue(value);
        q.addBindValue(old); q.addBindValue(key); q.addBindValue(old); q.addBindValue(old);
        if (!q.exec()) return {false,false,q.lastError().text()};
        if (q.numRowsAffected()!=1) return {false,false,QStringLiteral("Handoff changed; retry status")};
        return {true,false,{}};
    }
    return {false,false,QStringLiteral("Handoff offer not found")};
}
}
