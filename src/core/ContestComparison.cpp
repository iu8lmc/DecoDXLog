#include "core/ContestComparison.h"
#include <QDateTime>
#include <QMap>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimeZone>

namespace decolog::core::contestcomparison {
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
