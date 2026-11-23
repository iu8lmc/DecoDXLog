#include "core/ActivityReport.h"
#include <QSqlError>
#include <QSqlQuery>
#include <QVariantMap>
#include <QDateTime>
#include <QMap>

namespace decolog::core::activityreport {
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
}
