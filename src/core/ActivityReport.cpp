#include "core/ActivityReport.h"
#include "core/NewQsoFields.h"
#include "core/LogDatabase.h"
#include <QSqlError>
#include <QSqlQuery>
#include <QVariantMap>
#include <QDateTime>
#include <QMap>
#include <QPdfWriter>
#include <QPainter>
#include <QFontMetrics>
#include <QPageLayout>
#include <QPageSize>
#include <QIODevice>
#include <QSet>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUuid>
#include <limits>
#include <algorithm>
#include <cmath>

namespace decolog::core::activityreport {
bool beginVerifiedSnapshot(QSqlDatabase& db,const QString& expectedIdentity,QString* error)
{
    if (error) error->clear();
    if (!db.isOpen() || expectedIdentity.isEmpty()) { if (error) *error=QStringLiteral("Invalid log identity"); return false; }
    if (!db.transaction()) { if (error) *error=db.lastError().text(); return false; }
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT uuid FROM log_identity WHERE id=1")) || !q.next() || q.value(0).toString()!=expectedIdentity) {
        if (error) *error=QStringLiteral("The log identity changed; run the report again.");
        db.rollback(); return false;
    }
    return true;
}
namespace {
const QString reportPrefix=QStringLiteral("saved_report.");
bool definitionFailure(QString* error,const QString& message) { if (error) *error=message; return false; }
bool validDefinition(const Definition& d)
{
    static const QRegularExpression uuid(QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"));
    if ((!d.id.isEmpty() && !uuid.match(d.id).hasMatch()) || d.name.isEmpty() || d.name.size()>120 || d.name!=d.name.trimmed() ||
        d.band.size()>24 || d.band!=d.band.trimmed().toLower() || d.station.size()>24 || d.station!=d.station.trimmed().toUpper() || d.year<0 || d.year>9999) return false;
    if (d.kind==QLatin1String("custom")) return d.station.isEmpty() && validModeFilter(d.mode) && validateCustomSpec(d.dimensions,d.aggregate).isEmpty();
    if (d.kind!=QLatin1String("activity") && d.kind!=QLatin1String("confirmation") && d.kind!=QLatin1String("club")) return false;
    return d.mode.isEmpty() && d.dimensions.isEmpty() && d.aggregate.isEmpty() && (d.kind==QLatin1String("club") || d.station.isEmpty());
}
QString encodeDefinition(const Definition& d)
{
    QJsonObject o{{"format",d.mode.isEmpty()?1:2},{"id",d.id},{"name",d.name},{"kind",d.kind},{"year",d.year},
        {"band",d.band},{"station",d.station},{"dimensions",QJsonArray::fromStringList(d.dimensions)},{"aggregate",d.aggregate},{"revision",d.revision}};
    if (!d.mode.isEmpty()) o.insert("mode",d.mode); return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}
std::optional<Definition> decodeDefinition(const QString& text)
{
    QJsonParseError e; const auto doc=QJsonDocument::fromJson(text.toUtf8(),&e); if (e.error!=QJsonParseError::NoError || !doc.isObject()) return {};
    const auto o=doc.object(); const int format=o.value("format").toInt(); if ((format!=1 && format!=2) || (format==2 && !o.value("mode").isString()) || (format==1 && o.contains("mode")) || !o.value("dimensions").isArray() || !o.value("year").isDouble() || !o.value("revision").isDouble()) return {};
    Definition d; d.id=o.value("id").toString(); d.name=o.value("name").toString(); d.kind=o.value("kind").toString(); d.band=o.value("band").toString(); d.station=o.value("station").toString();
    d.aggregate=o.value("aggregate").toString(); d.year=o.value("year").toInt(-1); d.revision=o.value("revision").toInt(-1);
    if (format==2) d.mode=o.value("mode").toString();
    for (const auto& value : o.value("dimensions").toArray()) { if (!value.isString()) return {}; d.dimensions << value.toString(); }
    if (d.id.isEmpty() || d.revision<1 || !validDefinition(d)) return {}; return d;
}
}
bool saveDefinition(LogDatabase& db,Definition& d,QString* error)
{
    if (!db.isOpen()) return definitionFailure(error,QStringLiteral("Log is not open"));
    if (!validDefinition(d)) return definitionFailure(error,QStringLiteral("Invalid report definition"));
    const bool creating=d.id.isEmpty(); Definition next=d; QSqlQuery q(db.connection());
    if (creating) {
        next.id=QUuid::createUuid().toString(QUuid::WithoutBraces); next.revision=1;
        q.prepare(QStringLiteral("INSERT INTO app_setting(key,value) SELECT ?,? WHERE (SELECT COUNT(*) FROM app_setting WHERE substr(key,1,?)=?)<1000"));
        q.addBindValue(reportPrefix+next.id); q.addBindValue(encodeDefinition(next)); q.addBindValue(reportPrefix.size()); q.addBindValue(reportPrefix);
    }
    else {
        if (d.revision<1 || d.revision==std::numeric_limits<int>::max()) return definitionFailure(error,QStringLiteral("Invalid report revision"));
        q.prepare(QStringLiteral("SELECT value FROM app_setting WHERE key=?")); q.addBindValue(reportPrefix+d.id);
        if (!q.exec() || !q.next()) return definitionFailure(error,QStringLiteral("Report unavailable"));
        const QString old=q.value(0).toString(); const auto current=decodeDefinition(old);
        if (!current || current->id!=d.id || current->revision!=d.revision) return definitionFailure(error,QStringLiteral("Report changed; reload before saving"));
        ++next.revision; q.prepare(QStringLiteral("UPDATE app_setting SET value=? WHERE key=? AND value=?"));
        q.addBindValue(encodeDefinition(next)); q.addBindValue(reportPrefix+d.id); q.addBindValue(old);
    }
    if (!q.exec()) return definitionFailure(error,QStringLiteral("Cannot save report"));
    if (q.numRowsAffected()!=1) return definitionFailure(error,creating ? QStringLiteral("Saved report limit of 1000 reached") : QStringLiteral("Report changed; reload before saving"));
    d=next; if (error) error->clear(); return true;
}
std::optional<Definition> loadDefinition(const LogDatabase& db,const QString& id,QString* error)
{
    if (!db.isOpen()) { definitionFailure(error,QStringLiteral("Log is not open")); return {}; }
    QSqlQuery q(db.connection()); q.prepare(QStringLiteral("SELECT value FROM app_setting WHERE key=?")); q.addBindValue(reportPrefix+id);
    if (!q.exec() || !q.next()) { definitionFailure(error,QStringLiteral("Report unavailable")); return {}; }
    auto d=decodeDefinition(q.value(0).toString()); if (!d || d->id!=id) { definitionFailure(error,QStringLiteral("Invalid saved report")); return {}; }
    if (error) error->clear(); return d;
}
QList<Definition> listDefinitions(const LogDatabase& db,QString* error)
{
    QList<Definition> out; if (!db.isOpen()) { definitionFailure(error,QStringLiteral("Log is not open")); return out; }
    QSqlQuery q(db.connection()); q.prepare(QStringLiteral("SELECT key,value FROM app_setting WHERE substr(key,1,?)=? ORDER BY key LIMIT 1001")); q.addBindValue(reportPrefix.size()); q.addBindValue(reportPrefix);
    if (!q.exec()) { definitionFailure(error,QStringLiteral("Cannot read saved reports")); return {}; }
    while (q.next()) { if (out.size()>=1000) { definitionFailure(error,QStringLiteral("Too many saved reports")); return {}; }
        auto d=decodeDefinition(q.value(1).toString()); if (!d || reportPrefix+d->id!=q.value(0).toString()) { definitionFailure(error,QStringLiteral("Invalid saved report")); return {}; } out << *d; }
    if (q.lastError().isValid()) { definitionFailure(error,QStringLiteral("Cannot read saved reports")); return {}; }
    std::sort(out.begin(),out.end(),[](const Definition& a,const Definition& b) { const int c=QString::compare(a.name,b.name,Qt::CaseInsensitive); return c==0 ? a.id<b.id : c<0; });
    if (error) error->clear(); return out;
}
bool removeDefinition(LogDatabase& db,const Definition& d,QString* error)
{
    if (!db.isOpen() || d.id.isEmpty() || d.revision<1) return definitionFailure(error,QStringLiteral("Invalid saved report"));
    QSqlQuery q(db.connection()); q.prepare(QStringLiteral("SELECT value FROM app_setting WHERE key=?")); q.addBindValue(reportPrefix+d.id);
    if (!q.exec() || !q.next()) return definitionFailure(error,QStringLiteral("Report unavailable"));
    const QString old=q.value(0).toString(); const auto current=decodeDefinition(old);
    if (!current || current->id!=d.id || current->revision!=d.revision) return definitionFailure(error,QStringLiteral("Report changed; reload before removing"));
    q.prepare(QStringLiteral("DELETE FROM app_setting WHERE key=? AND value=?")); q.addBindValue(reportPrefix+d.id); q.addBindValue(old);
    if (!q.exec() || q.numRowsAffected()!=1) return definitionFailure(error,QStringLiteral("Cannot remove report or report changed"));
    if (error) error->clear(); return true;
}
Result run(const QSqlDatabase& db,int utcYear,const QString& band)
{
    Result out;
    if (!db.isOpen() || utcYear<0 || utcYear>9999 || band.size()>24) { out.error=QStringLiteral("Invalid report request"); return out; }
    QSqlQuery q(db);
    QString sql=QStringLiteral("SELECT CASE WHEN json_valid(adif_extra) AND json_type(adif_extra,'$.MY_ANTENNA')='text' THEN json_extract(adif_extra,'$.MY_ANTENNA') ELSE '' END AS antenna, "
        "COALESCE(band,''), CASE WHEN json_valid(adif_extra) AND json_type(adif_extra,'$.MY_GRIDSQUARE')='text' THEN json_extract(adif_extra,'$.MY_GRIDSQUARE') ELSE '' END AS own_grid, COUNT(*) "
        "FROM qso WHERE deleted=0");
    if (utcYear) sql+=QStringLiteral(" AND strftime('%Y',qso_datetime_on)=?");
    if (!band.trimmed().isEmpty()) sql+=QStringLiteral(" AND band=?");
    sql+=QStringLiteral(" GROUP BY antenna,band,own_grid ORDER BY antenna,band,own_grid LIMIT 10001");
    q.prepare(sql);
    if (utcYear) q.addBindValue(QStringLiteral("%1").arg(utcYear,4,10,QLatin1Char('0')));
    if (!band.trimmed().isEmpty()) q.addBindValue(band.trimmed().toLower());
    if (!q.exec()) { out.error=q.lastError().text(); return out; }
    while (q.next()) {
        if (out.rows.size()==10000) { out.truncated=true; break; }
        out.rows << QVariantMap{{QStringLiteral("antenna"),q.value(0)},{QStringLiteral("band"),q.value(1)},
                               {QStringLiteral("grid"),q.value(2)},{QStringLiteral("count"),q.value(3)}};
    }
    if (q.lastError().isValid()) { out.rows.clear(); out.error=q.lastError().text(); }
    return out;
}
QByteArray csv(const Result& result)
{
    if (!result.error.isEmpty() || result.truncated) return {};
    auto escape=[](QString text) { text.replace(QLatin1Char('"'),QStringLiteral("\"\"")); return (QLatin1Char('"')+text+QLatin1Char('"')).toUtf8(); };
    QByteArray out="antenna,band,own_grid,qso_count\r\n";
    for (const QVariant& item : result.rows) {
        const auto row=item.toMap(); out+=escape(row.value(QStringLiteral("antenna")).toString())+','+escape(row.value(QStringLiteral("band")).toString())+','+
            escape(row.value(QStringLiteral("grid")).toString())+','+QByteArray::number(row.value(QStringLiteral("count")).toLongLong())+"\r\n";
    }
    return out;
}
Result runConfirmations(const QSqlDatabase& db,int utcYear,const QString& band)
{
    Result out; if (!db.isOpen() || utcYear<0 || utcYear>9999 || band.size()>24) { out.error=QStringLiteral("Invalid report request"); return out; }
    QString sql=QStringLiteral("SELECT s.service,q.qso_datetime_on,s.rcvd_date FROM qso q JOIN qsl_status s ON s.qso_id=q.id WHERE q.deleted=0 AND s.rcvd='Y'");
    if (utcYear) sql+=QStringLiteral(" AND strftime('%Y',q.qso_datetime_on)=?");
    if (!band.trimmed().isEmpty()) sql+=QStringLiteral(" AND q.band=?");
    sql+=QStringLiteral(" ORDER BY s.service,q.id");
    QSqlQuery q(db); q.prepare(sql);
    if (utcYear) q.addBindValue(QStringLiteral("%1").arg(utcYear,4,10,QLatin1Char('0')));
    if (!band.trimmed().isEmpty()) q.addBindValue(band.trimmed().toLower());
    if (!q.exec()) { out.error=q.lastError().text(); return out; }
    struct Counts { qint64 confirmed=0,dated=0,missing=0,invalid=0,earlier=0,days=0; };
    QMap<QString,Counts> groups;
    while (q.next()) {
        const QString service=q.value(0).toString();
        if (!groups.contains(service) && groups.size()>=10000) { out.truncated=true; break; }
        Counts& c=groups[service]; ++c.confirmed;
        const QString received=q.value(2).toString().trimmed();
        if (received.isEmpty()) { ++c.missing; continue; }
        const QDate receivedDate=QDate::fromString(received,QStringLiteral("yyyyMMdd"));
        const QDateTime when=QDateTime::fromString(q.value(1).toString(),Qt::ISODate);
        if (!receivedDate.isValid() || receivedDate.toString(QStringLiteral("yyyyMMdd"))!=received || !when.isValid() || when.timeSpec()==Qt::LocalTime) { ++c.invalid; continue; }
        const qint64 days=when.toUTC().date().daysTo(receivedDate);
        if (days<0) { ++c.earlier; continue; }
        ++c.dated; c.days+=days;
    }
    if (q.lastError().isValid()) { out.error=q.lastError().text(); return out; }
    for (auto it=groups.cbegin();it!=groups.cend();++it) {
        const Counts& c=it.value(); QVariantMap row{{QStringLiteral("service"),it.key()},{QStringLiteral("confirmed"),c.confirmed},
            {QStringLiteral("dated"),c.dated},{QStringLiteral("missing"),c.missing},{QStringLiteral("invalid"),c.invalid},{QStringLiteral("earlier"),c.earlier}};
        if (c.dated) row.insert(QStringLiteral("averageDays"),double(c.days)/double(c.dated));
        out.rows << row;
    }
    return out;
}
QByteArray confirmationsCsv(const Result& result)
{
    if (!result.error.isEmpty() || result.truncated) return {};
    QByteArray out="service,confirmed,dated,missing_date,invalid_date,earlier_date,average_days\r\n";
    for (const QVariant& item : result.rows) {
        const auto row=item.toMap(); QString service=row.value(QStringLiteral("service")).toString(); service.replace(QLatin1Char('"'),QStringLiteral("\"\""));
        out+='"'+service.toUtf8()+'"';
        for (const auto& key : {"confirmed","dated","missing","invalid","earlier"}) out+=','+QByteArray::number(row.value(QLatin1String(key)).toLongLong());
        out+=','; if (row.contains(QStringLiteral("averageDays"))) out+=QByteArray::number(row.value(QStringLiteral("averageDays")).toDouble(),'f',2);
        out+="\r\n";
    }
    return out;
}
Result runClub(const QSqlDatabase& db,int utcYear,const QString& band,const QString& station)
{
    Result out; const QString selectedStation=station.trimmed().toUpper();
    if (!db.isOpen() || utcYear<0 || utcYear>9999 || band.size()>24 || selectedStation.size()>24) { out.error=QStringLiteral("Invalid report request"); return out; }
    QString sql=QStringLiteral("WITH reported AS (SELECT COALESCE(strftime('%Y',qso_datetime_on),'') AS report_year, COALESCE(band,'') AS report_band, "
        "CASE WHEN json_valid(adif_extra) AND json_type(adif_extra,'$.STATION_CALLSIGN')='text' THEN json_extract(adif_extra,'$.STATION_CALLSIGN') ELSE '' END AS station, "
        "CASE WHEN json_valid(adif_extra) AND json_type(adif_extra,'$.OPERATOR')='text' THEN json_extract(adif_extra,'$.OPERATOR') ELSE '' END AS operator "
        "FROM qso WHERE deleted=0) SELECT report_year,station,operator,COUNT(*) FROM reported WHERE 1=1");
    if (utcYear) sql+=QStringLiteral(" AND report_year=?");
    if (!band.trimmed().isEmpty()) sql+=QStringLiteral(" AND report_band=?");
    if (!selectedStation.isEmpty()) sql+=QStringLiteral(" AND UPPER(TRIM(station))=?");
    sql+=QStringLiteral(" GROUP BY report_year,station,operator ORDER BY report_year,station,operator LIMIT 10001");
    QSqlQuery q(db); q.prepare(sql);
    if (utcYear) q.addBindValue(QStringLiteral("%1").arg(utcYear,4,10,QLatin1Char('0')));
    if (!band.trimmed().isEmpty()) q.addBindValue(band.trimmed().toLower());
    if (!selectedStation.isEmpty()) q.addBindValue(selectedStation);
    if (!q.exec()) { out.error=q.lastError().text(); return out; }
    while (q.next()) {
        if (out.rows.size()==10000) { out.truncated=true; break; }
        out.rows << QVariantMap{{QStringLiteral("year"),q.value(0)},{QStringLiteral("station"),q.value(1)},
                               {QStringLiteral("operator"),q.value(2)},{QStringLiteral("count"),q.value(3)}};
    }
    if (q.lastError().isValid()) { out.rows.clear(); out.error=q.lastError().text(); }
    return out;
}
QByteArray clubCsv(const Result& result)
{
    if (!result.error.isEmpty() || result.truncated) return {};
    auto escape=[](QString text) { text.replace(QLatin1Char('"'),QStringLiteral("\"\"")); return (QLatin1Char('"')+text+QLatin1Char('"')).toUtf8(); };
    QByteArray out="utc_year,station_callsign,operator,qso_count\r\n";
    for (const QVariant& item : result.rows) {
        const auto row=item.toMap(); out+=escape(row.value(QStringLiteral("year")).toString())+','+escape(row.value(QStringLiteral("station")).toString())+','+
            escape(row.value(QStringLiteral("operator")).toString())+','+QByteArray::number(row.value(QStringLiteral("count")).toLongLong())+"\r\n";
    }
    return out;
}
QString validateCustomSpec(const QStringList& dimensions,const QString& aggregate)
{
    const QSet<QString> supported{QStringLiteral("year"),QStringLiteral("band"),QStringLiteral("mode"),QStringLiteral("station"),QStringLiteral("operator"),QStringLiteral("grid"),QStringLiteral("antenna"),QStringLiteral("dxcc"),QStringLiteral("day"),QStringLiteral("hour")};
    if (dimensions.isEmpty() || dimensions.size()>3 || (aggregate!=QLatin1String("qsos") && aggregate!=QLatin1String("calls") && aggregate!=QLatin1String("days") && aggregate!=QLatin1String("mean_power") && aggregate!=QLatin1String("entities"))) return QStringLiteral("Choose one to three dimensions and a supported aggregate");
    QSet<QString> seen;
    for (const QString& dimension : dimensions) {
        if (seen.contains(dimension)) return QStringLiteral("Dimensions must be distinct"); seen.insert(dimension);
        if (supported.contains(dimension)) continue;
        if (!dimension.startsWith(QLatin1String("custom:"))) return QStringLiteral("Unsupported dimension");
        const QString field=dimension.mid(7);
        if (field!=newqso::normalizeAdifName(field) || !newqso::isValidAdifName(field)) return QStringLiteral("Invalid custom ADIF dimension");
    }
    return {};
}
bool validModeFilter(const QString& mode) { static const QRegularExpression re(QStringLiteral("^[A-Z0-9-]{0,24}$")); return re.match(mode).hasMatch(); }
Result runCustom(const QSqlDatabase& db,int utcYear,const QString& band,const QStringList& dimensions,const QString& aggregate,const QString& mode)
{
    Result out; out.error=validateCustomSpec(dimensions,aggregate); if (!out.error.isEmpty()) return out;
    if (!db.isOpen() || utcYear<0 || utcYear>9999 || band.size()>24 || !validModeFilter(mode)) { out.error=QStringLiteral("Invalid report request"); return out; }
    QStringList selections,positions,parameters;
    auto extra=[](const QString& path) { return QStringLiteral("CASE WHEN json_valid(adif_extra) THEN COALESCE(CAST(json_extract(adif_extra,%1) AS TEXT),'') ELSE '' END").arg(path); };
    for (int i=0;i<dimensions.size();++i) {
        const QString& d=dimensions[i]; QString expression;
        if (d==QLatin1String("year")) expression=QStringLiteral("COALESCE(strftime('%Y',qso_datetime_on),'')");
        else if (d==QLatin1String("day")) expression=QStringLiteral("COALESCE(strftime('%Y-%m-%d',qso_datetime_on),'')");
        else if (d==QLatin1String("hour")) expression=QStringLiteral("COALESCE(strftime('%H',qso_datetime_on),'')");
        else if (d==QLatin1String("band")) expression=QStringLiteral("COALESCE(band,'')");
        else if (d==QLatin1String("mode")) expression=QStringLiteral("COALESCE(NULLIF(submode,''),mode,'')");
        else if (d==QLatin1String("dxcc")) expression=QStringLiteral("CASE WHEN typeof(dxcc)='integer' AND dxcc>0 THEN CAST(dxcc AS TEXT) ELSE '' END");
        else if (d==QLatin1String("station")) expression=extra(QStringLiteral("'$.STATION_CALLSIGN'"));
        else if (d==QLatin1String("operator")) expression=extra(QStringLiteral("'$.OPERATOR'"));
        else if (d==QLatin1String("grid")) expression=extra(QStringLiteral("'$.MY_GRIDSQUARE'"));
        else if (d==QLatin1String("antenna")) expression=extra(QStringLiteral("'$.MY_ANTENNA'"));
        else { expression=extra(QStringLiteral("?")); parameters << QStringLiteral("$.")+d.mid(7); }
        selections << expression+QStringLiteral(" AS d%1").arg(i); positions << QString::number(i+1);
    }
    const QString validPower=QStringLiteral("CASE WHEN typeof(tx_pwr) IN ('integer','real') AND tx_pwr>=0 AND tx_pwr<=1.7976931348623157e308 THEN tx_pwr END");
    selections << (aggregate==QLatin1String("entities") ? QStringLiteral("COUNT(DISTINCT CASE WHEN typeof(dxcc)='integer' AND dxcc>0 THEN dxcc END)") : aggregate==QLatin1String("mean_power") ? QStringLiteral("AVG(%1)").arg(validPower) : aggregate==QLatin1String("calls") ? QStringLiteral("COUNT(DISTINCT NULLIF(TRIM(call),''))")
                   : aggregate==QLatin1String("days") ? QStringLiteral("COUNT(DISTINCT strftime('%Y-%m-%d',qso_datetime_on))") : QStringLiteral("COUNT(*)"));
    if (aggregate==QLatin1String("mean_power")) selections << QStringLiteral("COUNT(%1)").arg(validPower) << QStringLiteral("COUNT(*)-COUNT(%1)").arg(validPower);
    QString sql=QStringLiteral("SELECT %1 FROM qso WHERE deleted=0").arg(selections.join(','));
    if (utcYear) sql+=QStringLiteral(" AND strftime('%Y',qso_datetime_on)=?");
    if (!band.trimmed().isEmpty()) sql+=QStringLiteral(" AND band=?");
    if (!mode.isEmpty()) sql+=QStringLiteral(" AND UPPER(COALESCE(NULLIF(submode,''),mode,''))=?");
    sql+=QStringLiteral(" GROUP BY %1 ORDER BY %1 LIMIT 10001").arg(positions.join(','));
    QSqlQuery q(db); q.prepare(sql); for (const auto& parameter : parameters) q.addBindValue(parameter);
    if (utcYear) q.addBindValue(QStringLiteral("%1").arg(utcYear,4,10,QLatin1Char('0')));
    if (!band.trimmed().isEmpty()) q.addBindValue(band.trimmed().toLower());
    if (!mode.isEmpty()) q.addBindValue(mode);
    if (!q.exec()) { out.error=q.lastError().text(); return out; }
    while (q.next()) {
        if (out.rows.size()==10000) { out.truncated=true; break; }
        QVariantMap row; for (int i=0;i<dimensions.size();++i) row.insert(QStringLiteral("d%1").arg(i),q.value(i));
        const QVariant value=q.value(dimensions.size());
        if (aggregate==QLatin1String("mean_power")) {
            if (!value.isNull() && !std::isfinite(value.toDouble())) { out.rows.clear(); out.error=QStringLiteral("Non-finite mean power"); return out; }
            row.insert(QStringLiteral("samples"),q.value(dimensions.size()+1)); row.insert(QStringLiteral("excluded"),q.value(dimensions.size()+2));
        }
        row.insert(QStringLiteral("value"),value); out.rows << row;
    }
    if (q.lastError().isValid()) { out.rows.clear(); out.error=q.lastError().text(); }
    return out;
}
QByteArray customCsv(const Result& result,const QStringList& dimensions,const QString& aggregate,const QString& mode)
{
    if (!result.error.isEmpty() || result.truncated || !validModeFilter(mode) || !validateCustomSpec(dimensions,aggregate).isEmpty()) return {};
    auto escape=[](QString text) { text.replace(QLatin1Char('"'),QStringLiteral("\"\"")); return (QLatin1Char('"')+text+QLatin1Char('"')).toUtf8(); };
    QByteArray out; for (const QString& dimension : dimensions) out+=escape(dimension)+',';
    out+=aggregate==QLatin1String("entities") ? "distinct_recorded_dxcc" : aggregate==QLatin1String("mean_power") ? "mean_tx_power_w,samples,excluded" : aggregate==QLatin1String("calls") ? "distinct_calls" : aggregate==QLatin1String("days") ? "distinct_utc_days" : "qso_count";
    if (!mode.isEmpty()) out+=",filter_recorded_mode"; out+="\r\n";
    for (const QVariant& item : result.rows) { const auto row=item.toMap(); for (int i=0;i<dimensions.size();++i) out+=escape(row.value(QStringLiteral("d%1").arg(i)).toString())+','; const auto value=row.value(QStringLiteral("value")); if (aggregate==QLatin1String("mean_power")) { if (!value.isNull()) out+=QByteArray::number(value.toDouble(),'g',15); out+=','+QByteArray::number(row.value(QStringLiteral("samples")).toLongLong())+','+QByteArray::number(row.value(QStringLiteral("excluded")).toLongLong()); } else out+=QByteArray::number(value.toLongLong()); if (!mode.isEmpty()) out+=','+escape(mode); out+="\r\n"; }
    return out;
}
bool writePdf(QIODevice* output,const QVariantMap& snapshot,QString* error)
{
    auto fail=[error](const QString& message) { if (error) *error=message; return false; };
    const QString kind=snapshot.value(QStringLiteral("kind")).toString();
    if (!output || !output->isWritable() || snapshot.value(QStringLiteral("truncated")).toBool() ||
        !snapshot.value(QStringLiteral("error")).toString().isEmpty() ||
        (kind!=QLatin1String("activity") && kind!=QLatin1String("confirmation") && kind!=QLatin1String("club") && kind!=QLatin1String("custom"))) return fail(QStringLiteral("Invalid or incomplete report"));
    const QStringList customDimensions=snapshot.value(QStringLiteral("dimensions")).toStringList();
    const QString aggregate=snapshot.value(QStringLiteral("aggregate")).toString();
    if (kind==QLatin1String("custom") && !validateCustomSpec(customDimensions,aggregate).isEmpty()) return fail(QStringLiteral("Invalid custom report"));
    QPdfWriter writer(output); writer.setResolution(72); writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageOrientation(QPageLayout::Landscape); writer.setPageMargins(QMarginsF(32,32,32,32),QPageLayout::Point);
    writer.setTitle(QStringLiteral("DecoDXLog report")); writer.setCreator(QStringLiteral("DecoDXLog"));
    QPainter painter; if (!painter.begin(&writer)) return fail(QStringLiteral("Cannot start PDF painter"));
    painter.setRenderHint(QPainter::Antialiasing,true);
    const QString title=kind==QLatin1String("custom") ? QStringLiteral("Custom QSO report") : kind==QLatin1String("club") ? QStringLiteral("Annual station activity") : kind==QLatin1String("confirmation") ? QStringLiteral("Observed confirmation times") : QStringLiteral("Recorded station activity");
    QStringList keys=kind==QLatin1String("club") ? QStringList{"year","station","operator","count"} : kind==QLatin1String("confirmation") ? QStringList{"service","confirmed","dated","missing","invalid","earlier","averageDays"} : QStringList{"antenna","band","grid","count"};
    QStringList headings=kind==QLatin1String("club") ? QStringList{"UTC year","Station","Operator","QSO"} : kind==QLatin1String("confirmation") ? QStringList{"Service","Confirmed","Dated","Missing","Invalid","Earlier","Mean days"} : QStringList{"Antenna","Band","Own grid","QSO"};
    QList<qreal> fractions=kind==QLatin1String("club") ? QList<qreal>{0.13,0.30,0.42,0.15} : kind==QLatin1String("confirmation") ? QList<qreal>{0.20,0.14,0.12,0.12,0.12,0.12,0.18} : QList<qreal>{0.39,0.14,0.32,0.15};
    if (kind==QLatin1String("custom")) {
        keys.clear(); headings=customDimensions; fractions.clear();
        const bool power=aggregate==QLatin1String("mean_power");
        for (int i=0;i<customDimensions.size();++i) { keys << QStringLiteral("d%1").arg(i); fractions << (power ? 0.58 : 0.8)/customDimensions.size(); }
        keys << QStringLiteral("value"); headings << (power ? QStringLiteral("Mean W") : aggregate==QLatin1String("entities") ? QStringLiteral("Recorded DXCC") : aggregate==QLatin1String("calls") ? QStringLiteral("Distinct calls") : aggregate==QLatin1String("days") ? QStringLiteral("UTC days") : QStringLiteral("QSO")); fractions << (power ? 0.18 : 0.2);
        if (power) { keys << QStringLiteral("samples") << QStringLiteral("excluded"); headings << QStringLiteral("Samples") << QStringLiteral("Excluded"); fractions << 0.12 << 0.12; }
    }
    const qreal width=writer.width(), height=writer.height(); const qreal footerY=height-12; qreal y=0; int page=0;
    QFont titleFont=painter.font(); titleFont.setPointSize(16); titleFont.setBold(true);
    QFont bodyFont=painter.font(); bodyFont.setPointSize(9);
    QFont headFont=bodyFont; headFont.setBold(true);
    const QString year=snapshot.value(QStringLiteral("year")).toInt() ? snapshot.value(QStringLiteral("year")).toString() : QStringLiteral("all");
    const QString band=snapshot.value(QStringLiteral("band")).toString().isEmpty() ? QStringLiteral("all") : snapshot.value(QStringLiteral("band")).toString();
    const QString station=snapshot.value(QStringLiteral("station")).toString();
    const QString mode=snapshot.value(QStringLiteral("mode")).toString();
    const QString metadata=QStringLiteral("Snapshot %1 | UTC year: %2 | Band: %3%4").arg(snapshot.value(QStringLiteral("generatedAt")).toString(),year,band,kind==QLatin1String("club") ? QStringLiteral(" | Station: ")+(station.isEmpty()?QStringLiteral("all"):station) : kind==QLatin1String("custom") ? QStringLiteral(" | Mode: ")+(mode.isEmpty()?QStringLiteral("all"):mode) : QString());
    const QString note=kind==QLatin1String("custom") ? (aggregate==QLatin1String("mean_power") ? QStringLiteral("Recorded TX power only; missing, invalid and negative values excluded. Not measured power.") : QStringLiteral("Groups use recorded QSO values; missing values are not inferred. Counts do not establish award credit.")) : kind==QLatin1String("confirmation") ? QStringLiteral("Local recorded states only; missing, invalid and earlier dates are excluded from the mean. Not award credit.") : kind==QLatin1String("club") ? QStringLiteral("Recorded station and operator only; missing values are not inferred. Membership is not verified.") : QStringLiteral("Only recorded per-QSO antenna and own grid are used; missing values are not inferred.");
    auto footer=[&] { painter.setFont(bodyFont); painter.setPen(QColor("#555555")); painter.drawText(QRectF(0,footerY,width,12),Qt::AlignRight|Qt::AlignVCenter,QStringLiteral("Page %1").arg(page)); };
    auto header=[&] {
        ++page; y=0; painter.setPen(QColor("#183344")); painter.setFont(titleFont); painter.drawText(QRectF(0,y,width,24),Qt::AlignLeft|Qt::AlignVCenter,title); y+=27;
        painter.setFont(bodyFont); painter.setPen(Qt::black); painter.drawText(QRectF(0,y,width,14),Qt::AlignLeft|Qt::AlignVCenter,metadata); y+=17;
        painter.drawText(QRectF(0,y,width,14),Qt::AlignLeft|Qt::AlignVCenter,note); y+=15;
        painter.drawText(QRectF(0,y,width,14),Qt::AlignLeft|Qt::AlignVCenter,QStringLiteral("Long cells are abbreviated and line breaks flattened; CSV retains full values.")); y+=19;
        painter.fillRect(QRectF(0,y,width,20),QColor("#e6eff2")); painter.setFont(headFont); qreal x=0;
        for (int i=0;i<headings.size();++i) { const qreal w=width*fractions[i]; painter.drawText(QRectF(x+4,y,w-8,20),Qt::AlignLeft|Qt::AlignVCenter,headings[i]); x+=w; }
        y+=20; painter.setFont(bodyFont);
    };
    header(); const auto rows=snapshot.value(QStringLiteral("rows")).toList();
    if (rows.isEmpty()) { painter.drawText(QRectF(4,y,width-8,20),Qt::AlignLeft|Qt::AlignVCenter,QStringLiteral("No matching records.")); y+=20; }
    for (int rowIndex=0;rowIndex<rows.size();++rowIndex) {
        const QVariantMap row=rows[rowIndex].toMap(); QStringList values; qreal rowHeight=18; const QFontMetrics fm(bodyFont);
        for (int i=0;i<keys.size();++i) {
            QString value;
            if (keys[i]==QLatin1String("averageDays")) value=row.contains(keys[i]) ? QString::number(row.value(keys[i]).toDouble(),'f',2) : QStringLiteral("-");
            else value=row.value(keys[i]).toString();
            if (value.isEmpty()) value=QStringLiteral("(not recorded)");
            value.replace(QLatin1Char('\n'),QLatin1Char(' ')); value.replace(QLatin1Char('\r'),QLatin1Char(' ')); value.replace(QLatin1Char('\t'),QLatin1Char(' '));
            if (value.size()>160) value=value.left(157)+QStringLiteral("...");
            values << value;
            const QRect bounds=fm.boundingRect(QRect(0,0,int(width*fractions[i]-8),1000),Qt::TextWordWrap|Qt::TextWrapAnywhere,value);
            rowHeight=qMax(rowHeight,qreal(bounds.height()+8));
        }
        if (y+rowHeight>footerY-8) { footer(); if (!writer.newPage()) { painter.end(); return fail(QStringLiteral("Cannot add PDF page")); } header(); }
        if (rowIndex%2) painter.fillRect(QRectF(0,y,width,rowHeight),QColor("#f6f8f9"));
        painter.setPen(QColor("#24343c")); qreal x=0;
        for (int i=0;i<values.size();++i) { const qreal w=width*fractions[i]; painter.drawText(QRectF(x+4,y+4,w-8,rowHeight-8),Qt::AlignLeft|Qt::AlignTop|Qt::TextWordWrap|Qt::TextWrapAnywhere,values[i]); x+=w; }
        y+=rowHeight; painter.setPen(QColor("#d5dfe3")); painter.drawLine(QPointF(0,y),QPointF(width,y));
    }
    footer(); if (!painter.end()) return fail(QStringLiteral("Cannot finish PDF")); if (error) error->clear(); return true;
}
}
