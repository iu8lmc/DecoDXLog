#include "core/CqMarathon.h"
#include "core/CqMarathonEntityCodes.h"
#include "core/Maidenhead.h"
#include "core/Modes.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <algorithm>
#include <cmath>
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
const QSet<QString>& supportedBands()
{
    static const QSet<QString> bands{QStringLiteral("160m"),QStringLiteral("80m"),QStringLiteral("60m"),QStringLiteral("40m"),
        QStringLiteral("30m"),QStringLiteral("20m"),QStringLiteral("17m"),QStringLiteral("15m"),QStringLiteral("12m"),QStringLiteral("10m"),QStringLiteral("6m")};
    return bands;
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
    if (rules.year==2026 && rules.maxPerEntity!=1) {
        if (error) *error=QStringLiteral("2026 CQ DX Marathon requires one candidate per entity");
        return false;
    }
    if (!rules.band.trimmed().isEmpty() && !supportedBands().contains(rules.band.trimmed().toLower())) {
        if (error) *error=QStringLiteral("unsupported CQ DX Marathon band filter");
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
        if (!rules.band.trimmed().isEmpty() && entry.band.trimmed().compare(rules.band.trimmed(),Qt::CaseInsensitive)!=0) {
            result.exclusions << QStringLiteral("QSO %1: band mismatch").arg(entry.qsoId); continue;
        }
        if (rules.year==2026) {
            if (!supportedBands().contains(entry.band.trimmed().toLower())) {
                result.exclusions << QStringLiteral("QSO %1: outside supported 2026 bands").arg(entry.qsoId); continue;
            }
            const QString prop=entry.propMode.trimmed().toUpper();
            static const QSet<QString> assisted{QStringLiteral("RPT"),QStringLiteral("ECH"),QStringLiteral("IRL"),QStringLiteral("INTERNET"),QStringLiteral("SAT")};
            if (assisted.contains(prop)) {
                result.exclusions << QStringLiteral("QSO %1: recorded propagation %2; relay or assisted path requires review").arg(entry.qsoId).arg(prop);
                continue;
            }
            const QString call=entry.call.trimmed().toUpper();
            if (call.endsWith(QLatin1String("/AM"))) {
                result.exclusions << QStringLiteral("QSO %1: aeronautical mobile designation").arg(entry.qsoId); continue;
            }
            if (call.endsWith(QLatin1String("/MM"))) {
                result.exclusions << QStringLiteral("QSO %1: maritime mobile designation; location requires review").arg(entry.qsoId); continue;
            }
        }
        const QString ownGrid=entry.myGrid.trimmed().toUpper();
        if (entry.invalidExtra) ++result.locationInvalid;
        else if (ownGrid.isEmpty()) ++result.locationMissing;
        else if (ownGrid.size()<4 || !maidenhead::toLatLon(ownGrid)) ++result.locationInvalid;
        else if (!result.locationGrids.contains(ownGrid.left(4))) result.locationGrids << ownGrid.left(4);
        if (entry.cqZone >= 1 && entry.cqZone <= 40) result.zoneAlternatives << entry;
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
            if (entry.cqZone >= 1 && entry.cqZone <= 40) {
                const auto it = std::lower_bound(detail::publishedEntityCodes.begin(), detail::publishedEntityCodes.end(), candidate.entityCode);
                const auto index = static_cast<std::size_t>(it - detail::publishedEntityCodes.begin());
                if ((detail::publishedZoneMasks[index] & (std::uint64_t{1} << (entry.cqZone - 1))) == 0)
                    result.warnings << QStringLiteral("QSO %1: CQ zone %2 is not listed for entity %3 in v1.1; review recorded data")
                                           .arg(entry.qsoId).arg(entry.cqZone).arg(candidate.entityCode);
            }
        }
        result.entityAlternatives << candidate;
        if (entityCount.value(candidate.entityCode) >= rules.maxPerEntity) {
            if (!newZone)
                result.exclusions << QStringLiteral("QSO %1: entity limit reached and no new valid zone").arg(entry.qsoId);
            continue;
        }
        result.selected << candidate;
        ++entityCount[candidate.entityCode];
    }
    result.locationGrids.sort();
    if (result.locationGrids.size()>1)
        result.warnings << QStringLiteral("Multiple recorded own grid squares (%1); verify single operating location and grid boundaries").arg(result.locationGrids.join(QStringLiteral(", ")));
    return result;
}

bool applyReviewChoices(Result& result, const QList<qint64>& entityIds, const QList<qint64>& zoneIds, QString* error)
{
    const auto resolve = [](const QList<Entry>& original, const QList<Entry>& alternatives,
                            const QList<qint64>& ids, bool entity, QList<Entry>& chosen) {
        if (ids.size()!=original.size()) return false;
        QHash<qint64,Entry> byId;
        for (const auto& entry : alternatives) byId.insert(entry.qsoId,entry);
        QHash<int,int> expected, actual;
        for (const auto& entry : original) ++expected[entity ? entry.entityCode : entry.cqZone];
        QSet<qint64> seen;
        for (qint64 id : ids) {
            if (id<=0 || seen.contains(id) || !byId.contains(id)) return false;
            seen.insert(id);
            const Entry entry=byId.value(id);
            ++actual[entity ? entry.entityCode : entry.cqZone];
            chosen << entry;
        }
        return expected==actual;
    };
    QList<Entry> entities,zones;
    if (!resolve(result.selected,result.entityAlternatives,entityIds,true,entities) ||
        !resolve(result.zoneCandidates,result.zoneAlternatives,zoneIds,false,zones)) {
        if (error) *error=QStringLiteral("Reviewed choices are no longer eligible or do not preserve entity and zone coverage");
        return false;
    }
    result.selected=entities; result.zoneCandidates=zones; result.reviewed=true;
    return true;
}

bool restoreReviewChoices(Result& result, const QJsonObject& file, const QString& expectedSnapshot, QString* error)
{
    const auto fail=[&]() { if (error) *error=QStringLiteral("Review file does not match the current log and preview"); return false; };
    if (expectedSnapshot.isEmpty() || file.value(QStringLiteral("previewSnapshot")).toString()!=expectedSnapshot ||
        file.value(QStringLiteral("format")).toString()!=QLatin1String("decodxlog-cq-marathon-1") ||
        file.value(QStringLiteral("selectionKind")).toString()!=QLatin1String("reviewed") ||
        file.value(QStringLiteral("edition")).toInt(-1)!=result.rules.year ||
        file.value(QStringLiteral("category")).toString()!=normalizedCategory(result.rules.category) ||
        file.value(QStringLiteral("band")).toString()!=result.rules.band.trimmed().toLower() ||
        file.value(QStringLiteral("maxPerEntity")).toInt(-1)!=result.rules.maxPerEntity ||
        file.value(QStringLiteral("stationProfileId")).toDouble(-1)!=double(result.rules.stationProfileId)) return fail();
    const auto ids=[](const QJsonValue& value,QList<qint64>& out) {
        if (!value.isArray()) return false;
        for (const auto& item : value.toArray()) {
            if (!item.isObject()) return false;
            const auto raw=item.toObject().value(QStringLiteral("qsoId"));
            if (!raw.isDouble()) return false;
            const double number=raw.toDouble();
            if (!std::isfinite(number) || number<1 || number>9007199254740991.0 || std::floor(number)!=number) return false;
            out << static_cast<qint64>(number);
        }
        return true;
    };
    QList<qint64> entities,zones;
    if (!ids(file.value(QStringLiteral("selected")),entities) || !ids(file.value(QStringLiteral("zoneCandidates")),zones)) return fail();
    return applyReviewChoices(result,entities,zones,error);
}

QList<Entry> loadEntries(const LogDatabase& db, QString* error)
{
    QList<Entry> entries;
    QSqlQuery query(db.connection());
    query.setForwardOnly(true);
    if (!query.exec(QStringLiteral(
            "SELECT id, call, dxcc, cqz, band, mode, qso_datetime_on, station_profile_id, adif_extra, prop_mode, sat_name "
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
        entry.propMode=query.value(9).toString(); entry.satName=query.value(10).toString();
        const QByteArray extra=query.value(8).toByteArray();
        if (!extra.isEmpty()) {
            QJsonParseError parseError; const auto doc=QJsonDocument::fromJson(extra,&parseError);
            if (parseError.error!=QJsonParseError::NoError || !doc.isObject()) entry.invalidExtra=true;
            else {
                const auto object=doc.object(); const auto grid=object.value(QStringLiteral("MY_GRIDSQUARE"));
                entry.region=object.value(QStringLiteral("REGION")).toString();
                if (!grid.isUndefined() && !grid.isString()) entry.invalidExtra=true;
                else entry.myGrid=grid.toString();
            }
        }
        entries << entry;
    }
    return entries;
}

QJsonDocument Result::toJson() const
{
    QJsonArray qsos;
    const auto row = [](const Entry& entry) {
        QJsonObject object{{QStringLiteral("qsoId"), static_cast<double>(entry.qsoId)},
                                {QStringLiteral("call"), entry.call},
                                {QStringLiteral("dxcc"), entry.dxcc},
                                {QStringLiteral("region"), entry.region},
                                {QStringLiteral("myGrid"), entry.myGrid},
                                {QStringLiteral("entityCode"), entry.entityCode},
                                {QStringLiteral("entityBasis"), entry.entityBasis},
                                {QStringLiteral("cqZone"), entry.cqZone},
                                {QStringLiteral("band"), entry.band},
                                {QStringLiteral("mode"), entry.mode},
                                {QStringLiteral("timestamp"), entry.timestamp.toUTC().toString(Qt::ISODateWithMs)}};
        if (!entry.propMode.isEmpty()) object.insert(QStringLiteral("propMode"),entry.propMode);
        if (!entry.satName.isEmpty()) object.insert(QStringLiteral("satName"),entry.satName);
        return object;
    };
    for (const Entry& entry : selected) qsos.append(row(entry));
    QJsonArray zones;
    for (const Entry& entry : zoneCandidates) zones.append(row(entry));
    QJsonArray entityAlternativesJson, zoneAlternativesJson;
    for (const Entry& entry : entityAlternatives) entityAlternativesJson.append(row(entry));
    for (const Entry& entry : zoneAlternatives) zoneAlternativesJson.append(row(entry));
    QJsonArray exclusions;
    for (const QString& value : this->exclusions) exclusions.append(value);
    QJsonArray warnings;
    for (const QString& value : this->warnings) warnings.append(value);
    QJsonObject object{{QStringLiteral("format"), QStringLiteral("decodxlog-cq-marathon-1")},
                                     {QStringLiteral("edition"), rules.year},
                                     {QStringLiteral("selectionKind"), reviewed ? QStringLiteral("reviewed") : QStringLiteral("automatic")},
                                     {QStringLiteral("exclusionsScope"), QStringLiteral("automatic selection")},
                                     {QStringLiteral("category"), normalizedCategory(rules.category)},
                                     {QStringLiteral("maxPerEntity"), rules.maxPerEntity},
                                     {QStringLiteral("stationProfileId"), static_cast<double>(rules.stationProfileId)},
                                     {QStringLiteral("selected"), qsos},
                                     {QStringLiteral("zoneCandidates"), zones},
                                     {QStringLiteral("entityAlternatives"), entityAlternativesJson},
                                     {QStringLiteral("zoneAlternatives"), zoneAlternativesJson},
                                     {QStringLiteral("locationGrids"), QJsonArray::fromStringList(locationGrids)},
                                     {QStringLiteral("locationMissing"), locationMissing},
                                     {QStringLiteral("locationInvalid"), locationInvalid},
                                     {QStringLiteral("entityListVersion"), rules.year == 2026 ? QStringLiteral("DX Marathon v1.1; list as of 2023") : QString()},
                                     {QStringLiteral("officialRulesVerified"), false},
                                     {QStringLiteral("warnings"), warnings},
                                     {QStringLiteral("exclusions"), exclusions}};
    if (!rules.band.trimmed().isEmpty()) object.insert(QStringLiteral("band"),rules.band.trimmed().toLower());
    return QJsonDocument(object);
}

} // namespace decolog::core::cqmarathon
