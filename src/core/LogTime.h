#pragma once

#include <QDateTime>
#include <QTimeZone>

namespace decolog::core::logtime {
// Applies only to timestamps read from the log: legacy unzoned values are UTC wall time.
inline QDateTime fromStoredQso(const QString& value)
{
    QDateTime time=QDateTime::fromString(value,Qt::ISODateWithMs);
    if (!time.isValid()) return {};
    if (time.timeSpec()==Qt::LocalTime) time.setTimeZone(QTimeZone::UTC);
    else time=time.toUTC();
    return time;
}
}
