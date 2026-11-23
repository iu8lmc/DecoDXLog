#include "core/ActivityReport.h"
#include <QSqlError>
#include <QSqlQuery>
#include <QVariantMap>

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
}
