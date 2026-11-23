#include "core/CqMarathon.h"
#include "core/CqMarathonEntityCodes.h"
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
        if (!entry.timestamp.isValid() || entry.timestamp.timeSpec()==Qt::LocalTime) {
            result.exclusions << QStringLiteral("QSO %1: valid UTC timestamp required").arg(entry.qsoId); continue;
        }
        if (entry.timestamp.toUTC().date().year() != rules.year) {
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
        if (rules.year==2026) {
            static const QSet<QString> bands{QStringLiteral("160M"),QStringLiteral("80M"),QStringLiteral("60M"),QStringLiteral("40M"),
                QStringLiteral("30M"),QStringLiteral("20M"),QStringLiteral("17M"),QStringLiteral("15M"),QStringLiteral("12M"),QStringLiteral("10M"),QStringLiteral("6M")};
            if (!bands.contains(entry.band.trimmed().toUpper())) {
                result.exclusions << QStringLiteral("QSO %1: outside supported 2026 bands").arg(entry.qsoId); continue;
            }
            const QString call=entry.call.trimmed().toUpper();
            if (call.endsWith(QLatin1String("/AM"))) {
                result.exclusions << QStringLiteral("QSO %1: aeronautical mobile designation").arg(entry.qsoId); continue;
            }
            if (call.endsWith(QLatin1String("/MM"))) {
                result.exclusions << QStringLiteral("QSO %1: maritime mobile designation; location requires review").arg(entry.qsoId); continue;
            }
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
        Entry candidate = entry;
        candidate.entityCode = entry.dxcc;
        candidate.entityBasis = QStringLiteral("recorded DXCC; special regions may require review");
        if (rules.year == 2026) {
            // CQ DX Marathon entity list v1.1, published 2025-11-18. REGION
            // values are meaningful only in combination with their parent DXCC.
            struct Special { int dxcc; const char* region; int code; };
            static constexpr Special special[] = {{206,"IV",901},{279,"SI",902},{248,"AI",903},
                                                   {248,"SY",904},{259,"BI",905},{390,"ET",906}};
            const QString region = entry.region.trimmed().toUpper();
            for (const auto& item : special) {
                if (entry.dxcc == item.dxcc && region == QLatin1String(item.region)) {
                    candidate.entityCode = item.code;
                    candidate.entityBasis = QStringLiteral("recorded DXCC and REGION %1").arg(region);
                    break;
                }
            }
            if (entry.dxcc >= 901 && entry.dxcc <= 906)
                candidate.entityBasis = QStringLiteral("recorded Marathon special code; verify source");
            if (!std::binary_search(detail::publishedEntityCodes.begin(), detail::publishedEntityCodes.end(), candidate.entityCode)) {
                result.exclusions << QStringLiteral("QSO %1: entity code %2 not in published list v1.1; zone considered independently")
                                         .arg(entry.qsoId).arg(candidate.entityCode);
                continue;
            }
        }
        if (entityCount.value(candidate.entityCode) >= rules.maxPerEntity) {
            if (!newZone)
                result.exclusions << QStringLiteral("QSO %1: entity limit reached and no new valid zone").arg(entry.qsoId);
            continue;
        }
        result.selected << candidate;
        ++entityCount[candidate.entityCode];
    }
    return result;
}

QList<Entry> loadEntries(const LogDatabase& db, QString* error)
{
    QList<Entry> entries;
    QSqlQuery query(db.connection());
    query.setForwardOnly(true);
    if (!query.exec(QStringLiteral(
            "SELECT id, call, dxcc, cqz, band, mode, qso_datetime_on, station_profile_id, adif_extra "
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
        const auto timestamp=QDateTime::fromString(query.value(6).toString(),Qt::ISODateWithMs);
        if (timestamp.isValid() && timestamp.timeSpec()!=Qt::LocalTime) entry.timestamp=timestamp.toUTC();
        entry.stationProfileId = query.value(7).toLongLong();
        entry.region = QJsonDocument::fromJson(query.value(8).toByteArray()).object().value(QStringLiteral("REGION")).toString();
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
                                {QStringLiteral("region"), entry.region},
                                {QStringLiteral("entityCode"), entry.entityCode},
                                {QStringLiteral("entityBasis"), entry.entityBasis},
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
                                     {QStringLiteral("entityListVersion"), rules.year == 2026 ? QStringLiteral("DX Marathon v1.1; list as of 2023") : QString()},
                                     {QStringLiteral("officialRulesVerified"), false},
                                     {QStringLiteral("exclusions"), exclusions}});
}

} // namespace decolog::core::cqmarathon
