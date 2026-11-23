#include "core/CqMarathon.h"
#include "core/Modes.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <algorithm>
#include <QSqlQuery>
#include <QSqlError>

namespace decolog::core::cqmarathon {

ReviewAdif reviewAdif(const LogDatabase& database,int year,qint64 stationProfileId,const QString& expectedIdentity)
{
    ReviewAdif out;
    if (!database.isOpen() || year<1900 || year>2200 || stationProfileId<=0) { out.error=QStringLiteral("Choose a valid year and station profile"); return out; }
    QSqlDatabase db=database.connection();
    if (!db.transaction()) { out.error=db.lastError().text(); return out; }
    if (!expectedIdentity.isEmpty() && database.identity()!=expectedIdentity) { db.rollback(); out.error=QStringLiteral("Log identity changed"); return out; }
    if (!database.stationProfile(stationProfileId)) { db.rollback(); out.error=QStringLiteral("Station profile unavailable"); return out; }
    QSqlQuery q(db); q.setForwardOnly(true);
    q.prepare(QStringLiteral("SELECT id,qso_datetime_on,qso_datetime_off FROM qso WHERE deleted=0 AND station_profile_id=? ORDER BY id")); q.addBindValue(stationProfileId);
    if (!q.exec()) { out.error=q.lastError().text(); db.rollback(); return out; }
    QList<qint64> ids; QList<QDateTime> starts,ends;
    while (q.next()) {
        const auto timestamp=QDateTime::fromString(q.value(1).toString(),Qt::ISODateWithMs);
        if (!timestamp.isValid() || timestamp.timeSpec()==Qt::LocalTime) { ++out.invalidTimestamp; continue; }
        if (timestamp.toUTC().date().year()==year) {
            const QString rawEnd=q.value(2).toString(); QDateTime end;
            if (!rawEnd.isEmpty()) {
                end=QDateTime::fromString(rawEnd,Qt::ISODateWithMs);
                if (!end.isValid() || end.timeSpec()==Qt::LocalTime) { q.finish(); db.rollback(); out.error=QStringLiteral("A selected QSO has an invalid end timestamp"); return out; }
            }
            ids.append(q.value(0).toLongLong()); starts.append(timestamp.toUTC()); ends.append(end.isValid()?end.toUTC():QDateTime());
        }
    }
    if (q.lastError().isValid()) { out.error=q.lastError().text(); q.finish(); db.rollback(); return out; }
    q.finish();
    if (ids.isEmpty()) { db.rollback(); out.error=QStringLiteral("No QSO found for this profile and year"); return out; }
    const QByteArray bytes=database.exportAdif(ids);
    auto parsed=adif::parse(bytes);
    if (parsed.records.size()!=ids.size()) { db.rollback(); out.error=QStringLiteral("Could not export all selected QSO"); return out; }
    for (qsizetype i=0;i<parsed.records.size();++i) {
        auto& record=parsed.records[i];
        record.set(QStringLiteral("QSO_DATE"),starts[i].toString(QStringLiteral("yyyyMMdd"))); record.set(QStringLiteral("TIME_ON"),starts[i].toString(QStringLiteral("HHmmss")));
        if (ends[i].isValid()) { record.set(QStringLiteral("QSO_DATE_OFF"),ends[i].toString(QStringLiteral("yyyyMMdd"))); record.set(QStringLiteral("TIME_OFF"),ends[i].toString(QStringLiteral("HHmmss"))); }
        for (const char* field:{"QSO_DATE","TIME_ON","CALL","BAND","MODE"})
            if (record.value(QLatin1String(field)).trimmed().isEmpty()) { db.rollback(); out.error=QStringLiteral("A selected QSO is missing an ADIF field required by the submission tool"); return out; }
    }
    if (!db.rollback()) { out.error=db.lastError().text(); return out; }
    out.bytes=adif::writeDocument(parsed); out.count=ids.size(); return out;
}

namespace {
QString normalizedCategory(const QString& value)
{
    return value.trimmed().toLower();
}

bool categoryMatches(const Entry& entry, const QString& category)
{
    const QString c = normalizedCategory(category);
    if (c.isEmpty() || c == QLatin1String("mixed")) return true;
    const QString mode = entry.mode.trimmed();
    if (mode.isEmpty()) return false;
    const QString group = mode.compare(QStringLiteral("DSB"), Qt::CaseInsensitive) == 0
        ? QStringLiteral("PHONE") : modes::groupFor(mode);
    if (c == QLatin1String("cw")) return group == QLatin1String("CW");
    if (c == QLatin1String("phone")) return group == QLatin1String("PHONE");
    if (c == QLatin1String("digital")) return group == QLatin1String("DATA");
    return false;
}
}

bool Result::isValid(QString* error) const
{
    if (rules.year < 1900 || rules.year > 2200) {
        if (error) *error = QStringLiteral("invalid CQ DX Marathon edition year");
        return false;
    }
    if (!QStringList{QStringLiteral("mixed"), QStringLiteral("cw"), QStringLiteral("phone"), QStringLiteral("digital")}
            .contains(normalizedCategory(rules.category))) {
        if (error) *error = QStringLiteral("unsupported CQ DX Marathon category");
        return false;
    }
    if (rules.maxPerEntity < 1 || rules.maxPerEntity > 10) {
        if (error) *error = QStringLiteral("invalid CQ DX Marathon entity limit");
        return false;
    }
    return true;
}

Result select(const QList<Entry>& entries, const Rules& rules)
{
    Result result;
    result.rules = rules;
    QString error;
    if (!result.isValid(&error)) {
        result.exclusions << error;
        return result;
    }
    QList<Entry> sorted = entries;
    std::stable_sort(sorted.begin(), sorted.end(), [](const Entry& a, const Entry& b) {
        if (a.timestamp != b.timestamp) return a.timestamp < b.timestamp;
        return a.qsoId < b.qsoId;
    });
    QHash<int, int> entityCount;
    QSet<int> zones;
    for (const Entry& entry : sorted) {
        if (!entry.timestamp.isValid() || entry.timestamp.toUTC().date().year() != rules.year) {
            result.exclusions << QStringLiteral("QSO %1: outside edition year").arg(entry.qsoId);
            continue;
        }
        if (entry.qsoId <= 0 || entry.call.trimmed().isEmpty()) {
            result.exclusions << QStringLiteral("QSO %1: missing required identity").arg(entry.qsoId);
            continue;
        }
        if (rules.stationProfileId > 0 && entry.stationProfileId != rules.stationProfileId) {
            result.exclusions << QStringLiteral("QSO %1: outside selected station profile").arg(entry.qsoId);
            continue;
        }
        if (!categoryMatches(entry, rules.category)) {
            result.exclusions << QStringLiteral("QSO %1: category mismatch").arg(entry.qsoId);
            continue;
        }
        const bool newZone = entry.cqZone >= 1 && entry.cqZone <= 40 && !zones.contains(entry.cqZone);
        if (newZone) {
            result.zoneCandidates << entry;
            zones.insert(entry.cqZone);
        }
        if (entry.dxcc <= 0) {
            if (!newZone) result.exclusions << QStringLiteral("QSO %1: no recorded entity or new valid zone").arg(entry.qsoId);
            continue;
        }
        if (entityCount.value(entry.dxcc) >= rules.maxPerEntity) {
            if (!newZone)
                result.exclusions << QStringLiteral("QSO %1: entity limit reached and no new valid zone").arg(entry.qsoId);
            continue;
        }
        result.selected << entry;
        ++entityCount[entry.dxcc];
    }
    return result;
}

QList<Entry> loadEntries(const LogDatabase& db, QString* error)
{
    QList<Entry> entries;
    QSqlQuery query(db.connection());
    query.setForwardOnly(true);
    if (!query.exec(QStringLiteral(
            "SELECT id, call, dxcc, cqz, band, mode, qso_datetime_on, station_profile_id "
            "FROM qso WHERE deleted = 0 ORDER BY qso_datetime_on, id"))) {
        if (error) *error = query.lastError().text();
        return {};
    }
    while (query.next()) {
        Entry entry;
        entry.qsoId = query.value(0).toLongLong();
        entry.call = query.value(1).toString();
        entry.dxcc = query.value(2).toInt();
        entry.cqZone = query.value(3).toInt();
        entry.band = query.value(4).toString();
        entry.mode = query.value(5).toString();
        entry.timestamp = QDateTime::fromString(query.value(6).toString(), Qt::ISODate).toUTC();
        entry.stationProfileId = query.value(7).toLongLong();
        entries << entry;
    }
    return entries;
}

QJsonDocument Result::toJson() const
{
    QJsonArray qsos;
    const auto row = [](const Entry& entry) {
        return QJsonObject{{QStringLiteral("qsoId"), static_cast<double>(entry.qsoId)},
                                {QStringLiteral("call"), entry.call},
                                {QStringLiteral("dxcc"), entry.dxcc},
                                {QStringLiteral("cqZone"), entry.cqZone},
                                {QStringLiteral("band"), entry.band},
                                {QStringLiteral("mode"), entry.mode},
                                {QStringLiteral("timestamp"), entry.timestamp.toUTC().toString(Qt::ISODateWithMs)}};
    };
    for (const Entry& entry : selected) qsos.append(row(entry));
    QJsonArray zones;
    for (const Entry& entry : zoneCandidates) zones.append(row(entry));
    QJsonArray exclusions;
    for (const QString& value : this->exclusions) exclusions.append(value);
    return QJsonDocument(QJsonObject{{QStringLiteral("format"), QStringLiteral("decodxlog-cq-marathon-1")},
                                     {QStringLiteral("edition"), rules.year},
                                     {QStringLiteral("category"), normalizedCategory(rules.category)},
                                     {QStringLiteral("maxPerEntity"), rules.maxPerEntity},
                                     {QStringLiteral("stationProfileId"), static_cast<double>(rules.stationProfileId)},
                                     {QStringLiteral("selected"), qsos},
                                     {QStringLiteral("zoneCandidates"), zones},
                                     {QStringLiteral("officialRulesVerified"), false},
                                     {QStringLiteral("exclusions"), exclusions}});
}

} // namespace decolog::core::cqmarathon
