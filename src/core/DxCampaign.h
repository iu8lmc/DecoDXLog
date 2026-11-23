#pragma once
#include <QDate>
#include <QDateTime>
#include <QList>
#include <QStringList>
#include <optional>
#include "core/Spots.h"
namespace decolog::core { class LogDatabase;
namespace dxcampaign {
struct Campaign {
    QString id, title;
    QStringList calls;
    QDate start, end; // inclusive UTC dates, as reported by the calendar
    QString source;
    QDateTime verifiedAt;
    QStringList bands, modes;
    qint64 stationProfileId{0}; // zero = all profiles, not an award account
    bool archived{false};
    bool watched{false};
    int revision{0};
};
struct Cell { QString band, mode; int worked{0}, confirmed{0}; };
bool save(LogDatabase& db, Campaign& campaign, QString* error = nullptr);
std::optional<Campaign> load(const LogDatabase& db, const QString& id, QString* error = nullptr);
QList<Campaign> list(const LogDatabase& db, QString* error = nullptr);
QList<Cell> matrix(const LogDatabase& db, const Campaign& campaign, QString* error = nullptr);
// Input must already obey the operator's cluster filters. No callsign aliases
// or times are inferred; future, undated and out-of-period spots are excluded.
QList<EnrichedSpot> freshSpots(const Campaign& campaign, const QList<EnrichedSpot>& visible,
                               const QDateTime& now, int maxAgeMinutes);
bool watchMatches(const Campaign& campaign, const EnrichedSpot& spot, const QDateTime& now, qint64 activeProfileId);
}
}
