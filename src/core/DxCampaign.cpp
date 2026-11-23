#include "core/DxCampaign.h"
#include "core/LogDatabase.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <limits>

namespace decolog::core::dxcampaign {
namespace {
const QString prefix=QStringLiteral("dx_campaign.");
QString campaignMode(const QString& text)
{
    const QString mode=text.trimmed().toUpper();
    if (mode==QLatin1String("USB") || mode==QLatin1String("LSB")) return QStringLiteral("SSB");
    return mode;
}
bool fail(QString* error,const QString& message) { if(error) *error=message; return false; }
bool valid(const Campaign& c,QString* error)
{
    static const QRegularExpression call(QStringLiteral("^[A-Z0-9/]{3,24}$"));
    static const QRegularExpression uuid(QStringLiteral("^[0-9a-f-]{36}$"));
    if ((!c.id.isEmpty() && !uuid.match(c.id).hasMatch()) || c.title.trimmed().isEmpty() || c.title.size()>200 ||
        !c.start.isValid() || !c.end.isValid() || c.start.year()<1 || c.end>=QDate(9999,12,31) || c.end<c.start || !c.verifiedAt.isValid() || c.verifiedAt.timeSpec()==Qt::LocalTime ||
        c.calls.isEmpty() || c.calls.size()>32 || c.bands.isEmpty() || c.bands.size()>30 || c.modes.isEmpty() || c.modes.size()>30 ||
        c.source.trimmed().isEmpty() || c.source.size()>2048 || c.stationProfileId<0) return fail(error,QStringLiteral("Invalid campaign details"));
    QSet<QString> seen;
    for (const auto& item : c.calls) {
        if (!call.match(item).hasMatch() || seen.contains(item)) return fail(error,QStringLiteral("Invalid or duplicate callsign"));
        seen.insert(item);
    }
    for (const QStringList& values : {c.bands,c.modes}) {
        QSet<QString> unique;
        for (const auto& value : values) {
            if (value.isEmpty() || value.size()>24 || unique.contains(value)) return fail(error,QStringLiteral("Invalid or duplicate matrix value"));
            unique.insert(value);
        }
    }
    for (const QString& band : c.bands) if (band!=band.trimmed().toLower()) return fail(error,QStringLiteral("Bands must be lowercase"));
    for (const QString& mode : c.modes) if (mode!=mode.trimmed().toUpper()) return fail(error,QStringLiteral("Modes must be uppercase"));
    return true;
}
QJsonObject encode(const Campaign& c)
{
    return {{"format",1},{"id",c.id},{"title",c.title},{"calls",QJsonArray::fromStringList(c.calls)},
        {"start",c.start.toString(Qt::ISODate)},{"end",c.end.toString(Qt::ISODate)},{"source",c.source},
        {"verifiedAt",c.verifiedAt.toUTC().toString(Qt::ISODateWithMs)},{"bands",QJsonArray::fromStringList(c.bands)},
        {"modes",QJsonArray::fromStringList(c.modes)},{"stationProfileId",QString::number(c.stationProfileId)},
        {"archived",c.archived},{"revision",c.revision}};
}
QStringList strings(const QJsonValue& v)
{
    QStringList out; if (!v.isArray()) return out;
    for (const auto& item : v.toArray()) { if (!item.isString()) return {}; out << item.toString(); } return out;
}
std::optional<Campaign> decode(const QString& raw)
{
    QJsonParseError e; const auto doc=QJsonDocument::fromJson(raw.toUtf8(),&e); if(e.error!=QJsonParseError::NoError || !doc.isObject()) return {};
    const auto o=doc.object(); if(o.value("format").toInt()!=1) return {};
    Campaign c; c.id=o.value("id").toString(); c.title=o.value("title").toString(); c.calls=strings(o.value("calls"));
    c.start=QDate::fromString(o.value("start").toString(),Qt::ISODate); c.end=QDate::fromString(o.value("end").toString(),Qt::ISODate);
    c.source=o.value("source").toString(); c.verifiedAt=QDateTime::fromString(o.value("verifiedAt").toString(),Qt::ISODate);
    c.bands=strings(o.value("bands")); c.modes=strings(o.value("modes"));
    bool ok=false; c.stationProfileId=o.value("stationProfileId").toString().toLongLong(&ok);
    c.archived=o.value("archived").toBool(); c.revision=o.value("revision").toInt();
    if(!ok || c.id.isEmpty() || c.revision<1 || !valid(c,nullptr)) return {}; return c;
}
}
bool save(LogDatabase& db,Campaign& c,QString* error)
{
    if(!db.isOpen()) return fail(error,QStringLiteral("Log is not open"));
    if(!valid(c,error)) return false;
    const bool creating=c.id.isEmpty(); Campaign next=c;
    if(creating) { next.id=QUuid::createUuid().toString(QUuid::WithoutBraces); next.revision=1; }
    else if(c.revision<1 || c.revision==std::numeric_limits<int>::max()) return fail(error,QStringLiteral("Campaign revision invalid"));
    QSqlQuery query(db.connection());
    if(creating) {
        query.prepare(QStringLiteral("INSERT INTO app_setting(key,value) VALUES(?,?)"));
        query.addBindValue(prefix+next.id); query.addBindValue(QString::fromUtf8(QJsonDocument(encode(next)).toJson(QJsonDocument::Compact)));
    } else {
        QSqlQuery get(db.connection()); get.prepare(QStringLiteral("SELECT value FROM app_setting WHERE key=?")); get.addBindValue(prefix+c.id);
        if(!get.exec() || !get.next()) return fail(error,QStringLiteral("Campaign unavailable"));
        const QString old=get.value(0).toString(); const auto existing=decode(old);
        if(!existing || existing->revision!=c.revision) return fail(error,QStringLiteral("Campaign changed; reload before saving"));
        ++next.revision;
        query.prepare(QStringLiteral("UPDATE app_setting SET value=? WHERE key=? AND value=?"));
        query.addBindValue(QString::fromUtf8(QJsonDocument(encode(next)).toJson(QJsonDocument::Compact))); query.addBindValue(prefix+c.id); query.addBindValue(old);
    }
    if(!query.exec()) return fail(error,query.lastError().text());
    if(query.numRowsAffected()!=1) return fail(error,QStringLiteral("Campaign changed; reload before saving"));
    c=next; if(error) error->clear(); return true;
}
std::optional<Campaign> load(const LogDatabase& db,const QString& id,QString* error)
{
    if(!db.isOpen()) { fail(error,QStringLiteral("Log is not open")); return {}; }
    QSqlQuery q(db.connection()); q.prepare(QStringLiteral("SELECT value FROM app_setting WHERE key=?")); q.addBindValue(prefix+id);
    if(!q.exec()) { fail(error,q.lastError().text()); return {}; }
    if(!q.next()) { fail(error,QStringLiteral("Campaign unavailable")); return {}; }
    auto c=decode(q.value(0).toString()); if(!c || c->id!=id) { fail(error,QStringLiteral("Invalid campaign data")); return {}; }
    if(error) error->clear(); return c;
}
QList<Campaign> list(const LogDatabase& db,QString* error)
{
    QList<Campaign> out; if(!db.isOpen()) { fail(error,QStringLiteral("Log is not open")); return out; }
    QSqlQuery q(db.connection()); q.prepare(QStringLiteral("SELECT key,value FROM app_setting WHERE substr(key,1,?)=? ORDER BY key"));
    q.addBindValue(prefix.size()); q.addBindValue(prefix); if(!q.exec()) { fail(error,q.lastError().text()); return out; }
    while(q.next()) { auto c=decode(q.value(1).toString()); if(!c || prefix+c->id!=q.value(0).toString()) { fail(error,QStringLiteral("Invalid campaign data")); return {}; } out << *c; }
    if(q.lastError().isValid()) { fail(error,q.lastError().text()); return {}; } if(error) error->clear(); return out;
}
QList<Cell> matrix(const LogDatabase& db,const Campaign& c,QString* error)
{
    QList<Cell> out; if(!db.isOpen()) { fail(error,QStringLiteral("Log is not open")); return out; }
    if(!valid(c,error)) return out;
    for(const QString& band:c.bands) for(const QString& mode:c.modes) out << Cell{band,mode,0,0};
    QStringList placeholders; for(int i=0;i<c.calls.size();++i) placeholders << QStringLiteral("?");
    const QString sql=QStringLiteral("SELECT band,mode,IFNULL(submode,''),qso_datetime_on,EXISTS(SELECT 1 FROM qsl_status s WHERE s.qso_id=qso.id AND s.service IN ('lotw','card') AND s.rcvd='Y') FROM qso WHERE deleted=0 AND call IN (%1) AND qso_datetime_on>=? AND qso_datetime_on<? AND (?=0 OR station_profile_id=?)").arg(placeholders.join(','));
    QSqlQuery q(db.connection()); q.prepare(sql); for(const auto& call:c.calls) q.addBindValue(call);
    q.addBindValue(c.start.toString(Qt::ISODate)+QStringLiteral("T00:00:00")); q.addBindValue(c.end.addDays(1).toString(Qt::ISODate)+QStringLiteral("T00:00:00"));
    q.addBindValue(c.stationProfileId); q.addBindValue(c.stationProfileId);
    if(!q.exec()) { fail(error,q.lastError().text()); return {}; }
    while(q.next()) {
        const QString band=q.value(0).toString().toLower();
        const QString rawMode=(q.value(2).toString().isEmpty()?q.value(1).toString():q.value(2).toString()).trimmed().toUpper();
        const QString mode=campaignMode(rawMode);
        const QDateTime time=QDateTime::fromString(q.value(3).toString(),Qt::ISODate); if(!time.isValid() || time.timeSpec()==Qt::LocalTime) continue;
        const QDate day=time.toUTC().date(); if(day<c.start || day>c.end) continue;
        for(Cell& cell:out) if(!mode.isEmpty() && cell.band==band &&
            (c.modes.contains(rawMode) ? cell.mode==rawMode : campaignMode(cell.mode)==mode)) { ++cell.worked; if(q.value(4).toBool()) ++cell.confirmed; break; }
    }
    if(q.lastError().isValid()) { fail(error,q.lastError().text()); return {}; } if(error) error->clear(); return out;
}
QList<EnrichedSpot> freshSpots(const Campaign& c,const QList<EnrichedSpot>& visible,
                               const QDateTime& now,int maxAgeMinutes)
{
    QList<EnrichedSpot> out;
    if (c.archived || !valid(c,nullptr) || !now.isValid() || now.timeSpec()==Qt::LocalTime || maxAgeMinutes<1 || maxAgeMinutes>60) return out;
    const QDateTime current=now.toUTC();
    for (const EnrichedSpot& e : visible) {
        const Spot& spot=e.spot;
        if (!spot.time.isValid() || spot.time.timeSpec()==Qt::LocalTime) continue;
        const QDateTime at=spot.time.toUTC(); const qint64 age=at.msecsTo(current);
        if (age<0 || age>qint64(maxAgeMinutes)*60000 || at.date()<c.start || at.date()>c.end) continue;
        if (!c.calls.contains(spot.dxCall.trimmed(),Qt::CaseInsensitive) || !c.bands.contains(spot.band.trimmed(),Qt::CaseInsensitive) || spot.mode.trimmed().isEmpty()) continue;
        const QString key=campaignMode(spot.mode); if (key.isEmpty() || key==QLatin1String("DIGI") || key==QLatin1String("DATA")) continue; bool modeMatches=false;
        for (const QString& mode : c.modes) if (campaignMode(mode)==key) { modeMatches=true; break; }
        if (modeMatches) out << e;
    }
    return out;
}
}
