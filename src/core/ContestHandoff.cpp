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
const QString prefix=QStringLiteral("contest_handoff.in.");
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
Result receiveOffer(LogDatabase& db,const QString& scope,const QString& recipient,const QString& envelopeSender,const QJsonObject& body) {
    const auto offer=parse(body);
    if (!db.isOpen() || !offer || offer->scope!=scope || offer->recipient!=recipient || offer->sender!=envelopeSender)
        return {false,false,QStringLiteral("Invalid or misaddressed handoff offer")};
    const QString key=prefix+offer->id, value=QString::fromUtf8(QJsonDocument(offerFrame(*offer)).toJson(QJsonDocument::Compact));
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
QList<Offer> incomingOffers(const LogDatabase& db,const QString& scope,const QString& recipient,QString* error) {
    if (error) error->clear(); QList<Offer> out;
    if (!db.isOpen() || !identifier(scope) || !identifier(recipient)) { if (error) *error=QStringLiteral("Invalid handoff listing"); return {}; }
    QSqlQuery q(db.connection()); q.prepare(QStringLiteral("SELECT key,value FROM app_setting WHERE substr(key,1,?)=? ORDER BY key LIMIT 10001"));
    q.addBindValue(prefix.size()); q.addBindValue(prefix); if (!q.exec()) { if (error) *error=q.lastError().text(); return {}; }
    int count=0;
    while(q.next()) {
        if (++count>10000) { if (error) *error=QStringLiteral("Too many handoff offers"); return {}; }
        QJsonParseError parseError; const auto doc=QJsonDocument::fromJson(q.value(1).toString().toUtf8(),&parseError);
        const auto offer=doc.isObject()?parse(doc.object()):std::nullopt;
        if (parseError.error!=QJsonParseError::NoError || !offer || prefix+offer->id!=q.value(0).toString()) {
            if (error) *error=QStringLiteral("Invalid stored handoff offer"); return {};
        }
        if (offer->scope==scope && offer->recipient==recipient) out << *offer;
    }
    if (q.lastError().isValid()) { if (error) *error=q.lastError().text(); return {}; }
    return out;
}
}
