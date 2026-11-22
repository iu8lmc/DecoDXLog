#include "core/CqMarathon.h"

#include <QJsonArray>
#include <QJsonObject>
#include <algorithm>
#include <QSqlQuery>
#include <QSqlError>

namespace decolog::core::cqmarathon {

namespace {
QString normalizedCategory(const QString& value)
{
    return value.trimmed().toLower();
}

bool categoryMatches(const Entry& entry, const QString& category)
{
    const QString c = normalizedCategory(category);
    if (c.isEmpty() || c == QLatin1String("mixed")) return true;
    if (c == QLatin1String("cw")) return entry.mode.compare(QStringLiteral("CW"), Qt::CaseInsensitive) == 0;
    if (c == QLatin1String("phone")) return entry.mode.compare(QStringLiteral("SSB"), Qt::CaseInsensitive) == 0
                                      || entry.mode.compare(QStringLiteral("FM"), Qt::CaseInsensitive) == 0;
    if (c == QLatin1String("digital")) return entry.mode.compare(QStringLiteral("CW"), Qt::CaseInsensitive) != 0
                                           && entry.mode.compare(QStringLiteral("SSB"), Qt::CaseInsensitive) != 0
                                           && entry.mode.compare(QStringLiteral("FM"), Qt::CaseInsensitive) != 0;
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
    for (const Entry& entry : sorted) {
        if (!entry.timestamp.isValid() || entry.timestamp.toUTC().date().year() != rules.year) {
            result.exclusions << QStringLiteral("QSO %1: outside edition year").arg(entry.qsoId);
            continue;
        }
        if (entry.qsoId <= 0 || entry.call.trimmed().isEmpty() || entry.dxcc <= 0) {
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
        if (entityCount.value(entry.dxcc) >= rules.maxPerEntity) {
            result.exclusions << QStringLiteral("QSO %1: entity limit reached").arg(entry.qsoId);
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
    for (const Entry& entry : selected) {
        qsos.append(QJsonObject{{QStringLiteral("qsoId"), static_cast<double>(entry.qsoId)},
                                {QStringLiteral("call"), entry.call},
                                {QStringLiteral("dxcc"), entry.dxcc},
                                {QStringLiteral("cqZone"), entry.cqZone},
                                {QStringLiteral("band"), entry.band},
                                {QStringLiteral("mode"), entry.mode},
                                {QStringLiteral("timestamp"), entry.timestamp.toUTC().toString(Qt::ISODateWithMs)}});
    }
    QJsonArray exclusions;
    for (const QString& value : this->exclusions) exclusions.append(value);
    return QJsonDocument(QJsonObject{{QStringLiteral("format"), QStringLiteral("decodxlog-cq-marathon-1")},
                                     {QStringLiteral("edition"), rules.year},
                                     {QStringLiteral("category"), normalizedCategory(rules.category)},
                                     {QStringLiteral("maxPerEntity"), rules.maxPerEntity},
                                     {QStringLiteral("stationProfileId"), static_cast<double>(rules.stationProfileId)},
                                     {QStringLiteral("selected"), qsos},
                                     {QStringLiteral("exclusions"), exclusions}});
}

} // namespace decolog::core::cqmarathon
