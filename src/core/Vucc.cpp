#include "core/Vucc.h"
#include "core/Maidenhead.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QDateTime>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <algorithm>

namespace decolog::core::vucc {

namespace {
QString norm(const QString& value) { return value.trimmed().toUpper(); }

bool validLocator(const QString& grid)
{
    const QString value = norm(grid);
    return value.size() >= 4 && maidenhead::toLatLon(value).has_value();
}

QStringList boundaryGrids(const QString& text, const QString& primary)
{
    const QStringList values = norm(text).split(QLatin1Char(','), Qt::KeepEmptyParts);
    if (values.size() != 2 && values.size() != 4)
        return {};
    constexpr int longitudeCells = 180 * 24;
    struct Rectangle { int x, y, size; };
    QSet<QString> unique;
    QList<Rectangle> rectangles;
    QStringList result;
    const QString normalizedPrimary = norm(primary);
    bool primaryMatches = normalizedPrimary.isEmpty();
    if (!primaryMatches && !validLocator(normalizedPrimary))
        return {};
    for (const QString& part : values) {
        const QString grid = part.trimmed();
        if ((grid.size() != 4 && grid.size() != 6) || !validLocator(grid) || unique.contains(grid))
            return {};
        unique.insert(grid);
        int x = ((grid[0].unicode() - 'A') * 10 + grid[2].digitValue()) * 24;
        int y = ((grid[1].unicode() - 'A') * 10 + grid[3].digitValue()) * 24;
        const int size = grid.size() == 6 ? 1 : 24;
        if (grid.size() == 6) {
            x += grid[4].unicode() - 'A';
            y += grid[5].unicode() - 'A';
        }
        if (!rectangles.isEmpty()) {
            const int origin = rectangles.first().x;
            if (x - origin > longitudeCells / 2) x -= longitudeCells;
            if (x - origin < -longitudeCells / 2) x += longitudeCells;
        }
        rectangles.append({x, y, size});
        result.append(grid.left(4));
        const int common = qMin(int(normalizedPrimary.size()), int(grid.size()));
        if (common >= 4 && normalizedPrimary.left(common) == grid.left(common))
            primaryMatches = true;
    }
    if (!primaryMatches)
        return {};
    if (rectangles.size() == 2) {
        const auto a = rectangles[0], b = rectangles[1];
        const bool verticalEdge = (a.x + a.size == b.x || b.x + b.size == a.x)
                                  && qMax(a.y, b.y) < qMin(a.y + a.size, b.y + b.size);
        const bool horizontalEdge = (a.y + a.size == b.y || b.y + b.size == a.y)
                                    && qMax(a.x, b.x) < qMin(a.x + a.size, b.x + b.size);
        if (!verticalEdge && !horizontalEdge)
            return {};
    } else {
        int minUpperX = rectangles.first().x + rectangles.first().size;
        int minUpperY = rectangles.first().y + rectangles.first().size;
        int maxLowerX = rectangles.first().x, maxLowerY = rectangles.first().y;
        for (const auto& r : rectangles) {
            maxLowerX = qMax(maxLowerX, r.x); maxLowerY = qMax(maxLowerY, r.y);
            minUpperX = qMin(minUpperX, r.x + r.size); minUpperY = qMin(minUpperY, r.y + r.size);
        }
        if (maxLowerX != minUpperX || maxLowerY != minUpperY)
            return {};
        QSet<int> quadrants;
        for (const auto& r : rectangles) {
            const int xSide = r.x == maxLowerX ? 1 : r.x + r.size == maxLowerX ? 0 : -1;
            const int ySide = r.y == maxLowerY ? 1 : r.y + r.size == maxLowerY ? 0 : -1;
            if (xSide < 0 || ySide < 0)
                return {};
            quadrants.insert(xSide + 2 * ySide);
        }
        if (quadrants.size() != 4)
            return {};
    }
    result.removeDuplicates();
    return result;
}
}

bool validGrid4(const QString& grid)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-R]{2}[0-9]{2}$"));
    return pattern.match(norm(grid)).hasMatch();
}

int initialThreshold(const QString& scope,const QString& band)
{
    if (scope==QLatin1String("satellite")) return 100;
    if (scope!=QLatin1String("terrestrial")) return 0;
    const QString b=norm(band);
    if (b==QLatin1String("6M") || b==QLatin1String("2M")) return 100;
    if (b==QLatin1String("1.25M") || b==QLatin1String("70CM")) return 50;
    if (b==QLatin1String("33CM") || b==QLatin1String("23CM")) return 25;
    if (b==QLatin1String("13CM")) return 10;
    static const QSet<QString> higher{QStringLiteral("9CM"),QStringLiteral("6CM"),QStringLiteral("3CM"),QStringLiteral("1.25CM"),
        QStringLiteral("6MM"),QStringLiteral("4MM"),QStringLiteral("2.5MM"),QStringLiteral("2MM"),QStringLiteral("1MM")};
    return higher.contains(b) ? 5 : 0;
}

bool Result::isValid(QString* error) const
{
    if (rules.target < 1 || rules.target > 10000) {
        if (error) *error = QStringLiteral("invalid VUCC target");
        return false;
    }
    if (rules.scope != QLatin1String("terrestrial") && rules.scope != QLatin1String("satellite")) {
        if (error) *error = QStringLiteral("invalid VUCC scope"); return false;
    }
    if (rules.scope == QLatin1String("terrestrial") && rules.band.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("VUCC band is required");
        return false;
    }
    if (initialThreshold(rules.scope,rules.band)==0 && !(rules.scope==QLatin1String("terrestrial") && norm(rules.band)==QLatin1String("SUBMM"))) {
        if (error) *error=QStringLiteral("Band is not in the supported VUCC band list"); return false;
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
    QSet<QString> grids;
    QList<Entry> sorted = entries;
    std::stable_sort(sorted.begin(), sorted.end(), [](const Entry& a, const Entry& b) {
        return a.qsoId < b.qsoId;
    });
    for (const Entry& entry : sorted) {
        const QStringList candidates = entry.grids.trimmed().isEmpty()
            ? (validLocator(entry.grid) ? QStringList{norm(entry.grid).left(4)} : QStringList{})
            : boundaryGrids(entry.grids, entry.grid);
        if (entry.qsoId <= 0 || candidates.isEmpty()) {
            result.exclusions << QStringLiteral("QSO %1: invalid or inconsistent grid data").arg(entry.qsoId);
            continue;
        }
        const auto timestamp=QDateTime::fromString(entry.qsoUtc,Qt::ISODateWithMs);
        if (!timestamp.isValid() || timestamp.timeSpec()==Qt::LocalTime) {
            result.exclusions << QStringLiteral("QSO %1: valid UTC timestamp required").arg(entry.qsoId); continue;
        }
        if (timestamp.toUTC().date()<QDate(1983,1,1)) {
            result.exclusions << QStringLiteral("QSO %1: before 1983-01-01 UTC").arg(entry.qsoId); continue;
        }
        const QString propagation=norm(entry.propMode), satelliteName=norm(entry.satName);
        if (!satelliteName.isEmpty() && propagation!=QLatin1String("SAT")) {
            result.exclusions << QStringLiteral("QSO %1: inconsistent satellite metadata; review propagation mode").arg(entry.qsoId); continue;
        }
        const bool viaSatellite=propagation==QLatin1String("SAT");
        if ((rules.scope==QLatin1String("satellite")) != viaSatellite) {
            result.exclusions << QStringLiteral("QSO %1: propagation scope mismatch").arg(entry.qsoId); continue;
        }
        if (!viaSatellite && !entry.rxBand.trimmed().isEmpty() && norm(entry.band)!=norm(entry.rxBand)) {
            result.exclusions << QStringLiteral("QSO %1: recorded crossband contact needs review").arg(entry.qsoId); continue;
        }
        if (!viaSatellite && norm(entry.band) != norm(rules.band)) {
            result.exclusions << QStringLiteral("QSO %1: band mismatch").arg(entry.qsoId);
            continue;
        }
        if (!rules.account.trimmed().isEmpty() && norm(entry.account) != norm(rules.account)) {
            result.exclusions << QStringLiteral("QSO %1: account mismatch").arg(entry.qsoId);
            continue;
        }
        if (!rules.site.trimmed().isEmpty() && norm(entry.site) != norm(rules.site)) {
            result.exclusions << QStringLiteral("QSO %1: operating site mismatch").arg(entry.qsoId);
            continue;
        }
        if (!entry.confirmed || entry.evidence.trimmed().isEmpty()) {
            result.exclusions << QStringLiteral("QSO %1: confirmation evidence required").arg(entry.qsoId);
            continue;
        }
        QStringList accepted, other;
        for (const auto& item : entry.evidence.split(QLatin1Char(','),Qt::SkipEmptyParts)) {
            const QString service=item.trimmed().toLower();
            if (service.isEmpty()) continue;
            QStringList& target=(service==QLatin1String("lotw") || service==QLatin1String("card")) ? accepted : other;
            if (!target.contains(service)) target.append(service);
        }
        accepted.sort(); other.sort();
        if (accepted.isEmpty()) {
            result.exclusions << QStringLiteral("QSO %1: unsupported confirmation sources: %2").arg(entry.qsoId).arg(other.join(QStringLiteral(", ")));
            continue;
        }
        for (const QString& grid : candidates) {
            if (grids.contains(grid)) {
                result.exclusions << QStringLiteral("QSO %1: GRID4 %2 already selected").arg(entry.qsoId).arg(grid);
                continue;
            }
            Entry credited = entry;
            credited.grid = grid;
            credited.evidence=accepted.join(QLatin1Char(',')); credited.otherEvidence=other.join(QLatin1Char(','));
            credited.classification=viaSatellite ? (satelliteName.isEmpty() ? QStringLiteral("satellite-name-missing") : QStringLiteral("satellite"))
                : propagation.isEmpty() ? QStringLiteral("terrestrial-unreported") : QStringLiteral("terrestrial");
            result.eligible << credited;
            grids.insert(grid);
        }
    }
    return result;
}

QList<Entry> loadEntries(const LogDatabase& db, QString* error)
{
    QList<Entry> entries;
    QSqlQuery query(db.connection());
    query.setForwardOnly(true);
    if (!query.exec(QStringLiteral(
            "SELECT q.id, q.gridsquare, q.band, IFNULL(p.award_account, ''), "
            "IFNULL(p.operating_location, ''), "
            "EXISTS(SELECT 1 FROM qsl_status s WHERE s.qso_id = q.id AND s.rcvd = 'Y'), "
            "(SELECT group_concat(s.service, ',') FROM qsl_status s WHERE s.qso_id = q.id AND s.rcvd = 'Y'), "
            "IFNULL(q.adif_extra, ''), IFNULL(q.prop_mode,''), IFNULL(q.sat_name,''), IFNULL(q.band_rx,''), q.qso_datetime_on "
            "FROM qso q LEFT JOIN station_profile p ON p.id = q.station_profile_id "
            "WHERE q.deleted = 0 AND (IFNULL(q.gridsquare, '') <> '' OR q.adif_extra LIKE '%\"VUCC_GRIDS\"%') ORDER BY q.id"))) {
        if (error) *error = query.lastError().text();
        return {};
    }
    while (query.next()) {
        Entry entry;
        entry.qsoId = query.value(0).toLongLong();
        entry.grid = query.value(1).toString();
        entry.band = query.value(2).toString();
        entry.account = query.value(3).toString();
        entry.site = query.value(4).toString();
        entry.confirmed = query.value(5).toBool();
        entry.evidence = query.value(6).toString();
        entry.propMode=query.value(8).toString(); entry.satName=query.value(9).toString(); entry.rxBand=query.value(10).toString();
        entry.qsoUtc=query.value(11).toString();
        const QByteArray extra = query.value(7).toByteArray();
        if (!extra.isEmpty()) {
            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(extra, &parseError);
            if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
                if (error) *error = QStringLiteral("QSO %1: invalid ADIF extra data").arg(entry.qsoId);
                return {};
            }
            const QJsonObject object = doc.object();
            const QJsonValue grids = object.value(QStringLiteral("VUCC_GRIDS"));
            const QJsonValue myGrids = object.value(QStringLiteral("MY_VUCC_GRIDS"));
            const QJsonValue myGrid = object.value(QStringLiteral("MY_GRIDSQUARE"));
            if ((!grids.isUndefined() && !grids.isString()) || (!myGrids.isUndefined() && !myGrids.isString()) ||
                (!myGrid.isUndefined() && !myGrid.isString())) {
                if (error) *error = QStringLiteral("QSO %1: invalid VUCC grid field type").arg(entry.qsoId);
                return {};
            }
            entry.grids = grids.toString();
            entry.myGrids = myGrids.toString();
            entry.myGrid = myGrid.toString();
        }
        if (entry.grid.trimmed().isEmpty() && entry.grids.trimmed().isEmpty())
            continue;
        entries << entry;
    }
    return entries;
}

QJsonDocument Result::toJson() const
{
    QJsonArray rows;
    for (const Entry& entry : eligible) {
        rows.append(QJsonObject{{QStringLiteral("qsoId"), static_cast<double>(entry.qsoId)},
                                {QStringLiteral("grid4"), entry.grid},
                                {QStringLiteral("band"), entry.band},
                                {QStringLiteral("account"), entry.account},
                                {QStringLiteral("site"), entry.site},
                                {QStringLiteral("reportedVuccGrids"), entry.grids},
                                {QStringLiteral("reportedMyVuccGrids"), entry.myGrids},
                                {QStringLiteral("reportedMyGrid"), entry.myGrid},
                                {QStringLiteral("evidence"), entry.evidence},
                                {QStringLiteral("otherEvidence"), entry.otherEvidence},
                                {QStringLiteral("propMode"),entry.propMode},{QStringLiteral("satName"),entry.satName},
                                {QStringLiteral("rxBand"),entry.rxBand},{QStringLiteral("classification"),entry.classification},{QStringLiteral("qsoUtc"),entry.qsoUtc}});
    }
    QJsonArray exclusions;
    for (const auto& value : this->exclusions) exclusions.append(value);
    return QJsonDocument(QJsonObject{{QStringLiteral("format"), QStringLiteral("decodxlog-vucc-1")},
                                     {QStringLiteral("officialRulesVerified"), officialRulesVerified},
                                     {QStringLiteral("account"), rules.account},
                                     {QStringLiteral("site"), rules.site},
                                     {QStringLiteral("band"), rules.band},
                                     {QStringLiteral("scope"), rules.scope},
                                     {QStringLiteral("initialThreshold"), initialThreshold(rules.scope,rules.band)},
                                     {QStringLiteral("target"), rules.target},
                                     {QStringLiteral("eligible"), rows},
                                     {QStringLiteral("exclusions"), exclusions}});
}

} // namespace decolog::core::vucc
