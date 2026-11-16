#pragma once

#include "core/Adif.h"
#include <QDateTime>
#include <QCoreApplication>
#include <QTimeZone>
#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <utility>

namespace decolog::core {

// An exact contact time where available, otherwise the recorded UTC minute.
// The API supports both metre bands and centimetre-band names.
inline QJsonObject clubLogIdentityFilter(const AdifRecord& record)
{
    const QString call = record.value(QStringLiteral("CALL")).trimmed().toUpper();
    const QString date = record.value(QStringLiteral("QSO_DATE")).trimmed();
    const QString time = record.value(QStringLiteral("TIME_ON")).trimmed();
    QString band = record.value(QStringLiteral("BAND")).trimmed().toLower();
    QString mode = record.value(QStringLiteral("MODE")).trimmed().toUpper();
    const QString submode = record.value(QStringLiteral("SUBMODE")).trimmed().toUpper();
    if (mode == QLatin1String("MFSK") && !submode.isEmpty()) mode = submode;
    if (band.endsWith(QLatin1String("m")) && !band.endsWith(QLatin1String("cm"))) band.chop(1);
    const QDate day = QDate::fromString(date, QStringLiteral("yyyyMMdd"));
    const QTime clock = QTime::fromString(time, time.size() == 4 ? QStringLiteral("HHmm") : QStringLiteral("HHmmss"));
    if (call.isEmpty() || band.isEmpty() || mode.isEmpty() || !day.isValid() || !clock.isValid()
        || (time.size() != 4 && time.size() != 6)) return {};
    const QDateTime from(day, clock, QTimeZone::UTC);
    const QDateTime to = from.addSecs(time.size() == 4 ? 59 : 0);
    return {{QStringLiteral("dxcall"), call}, {QStringLiteral("band"), band},
            {QStringLiteral("mode"), mode}, {QStringLiteral("from"), from.toString(Qt::ISODate)},
            {QStringLiteral("to"), to.toString(Qt::ISODate)}};
}

// Accepted rows require an independently known Club Log ID. After every
// accepted row is matched, the remaining contacts are exactly the rejected
// rows. Their individual messages must not be paired by response order.
inline QHash<qint64, QVariantMap> matchClubLogBatchResults(
    const QHash<qint64, QString>& localToRemote, const QVariantList& rows)
{
    QHash<QString, qint64> remoteToLocal;
    QSet<qint64> localIds;
    for (auto it = localToRemote.cbegin(); it != localToRemote.cend(); ++it) {
        if (it.key() <= 0) return {};
        localIds.insert(it.key());
        if (it.value().isEmpty()) continue;
        bool ok = false;
        const qint64 remote = it.value().toLongLong(&ok);
        const QString id = QString::number(remote);
        if (!ok || remote <= 0 || remoteToLocal.contains(id)) return {};
        remoteToLocal.insert(id, it.key());
    }
    if (localIds.isEmpty() || rows.size() != localIds.size()) return {};
    QHash<qint64, QVariantMap> result;
    QVariantList rejected;
    for (const QVariant& item : rows) {
        const QVariantMap row = item.toMap();
        const QString status = row.value(QStringLiteral("status")).toString().toLower();
        if (status == QLatin1String("rejected")) {
            if (!row.contains(QStringLiteral("qsoid")) || !row.value(QStringLiteral("qsoid")).isNull()) return {};
            rejected.append(row); continue;
        }
        bool ok = false;
        const qint64 remote = row.value(QStringLiteral("qsoid")).toLongLong(&ok);
        if (!ok || remote <= 0 || (status != QLatin1String("created")
            && status != QLatin1String("updated") && status != QLatin1String("duplicate"))) return {};
        const qint64 local = remoteToLocal.value(QString::number(remote), 0);
        if (!local || result.contains(local)) return {};
        result.insert(local, row);
    }
    if (!rejected.isEmpty()) {
        for (auto it = result.cbegin(); it != result.cend(); ++it) localIds.remove(it.key());
        if (localIds.size() != rejected.size()) return {};
        if (rejected.size() == 1) {
            result.insert(*localIds.cbegin(), rejected.first().toMap());
        } else {
            QStringList reasons;
            bool everyReasonPresent = true;
            for (const QVariant& value : rejected) {
                const QString reason = value.toMap().value(QStringLiteral("message")).toString().trimmed();
                if (reason.isEmpty()) everyReasonPresent = false;
                if (!reason.isEmpty() && !reasons.contains(reason)) reasons.append(reason);
            }
            QString message;
            if (everyReasonPresent && reasons.size() == 1)
                message = reasons.first();
            else if (!reasons.isEmpty())
                message = QCoreApplication::translate("ClubLogBatch",
                    "Club Log rejected this contact; the individual reason is unknown. Batch reasons: %1")
                              .arg(reasons.join(QStringLiteral(" | ")));
            else
                message = QCoreApplication::translate("ClubLogBatch", "Club Log rejected this contact");
            const QVariantMap sameOutcome{{QStringLiteral("status"), QStringLiteral("rejected")},
                                          {QStringLiteral("qsoid"), QVariant()},
                                          {QStringLiteral("message"), message}};
            for (qint64 id : std::as_const(localIds)) result.insert(id, sameOutcome);
        }
    }
    return result.size() == localToRemote.size() ? result : QHash<qint64, QVariantMap>();
}
}
