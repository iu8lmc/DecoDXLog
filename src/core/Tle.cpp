#include "core/Tle.h"
#include <QDate>
#include <QRegularExpression>
#include <QTimeZone>
#include <cmath>

namespace decolog::core::tle {
Inspection inspect(const QByteArray& first, const QByteArray& second)
{
    Inspection result;
    auto fail=[&](const QString& message) { result.error=message; return result; };
    if (first.size()!=69 || second.size()!=69 || !first.startsWith("1 ") || !second.startsWith("2 "))
        return fail(QStringLiteral("TLE lines must use the 69-column format"));
    const auto validChecksum=[](const QByteArray& line) {
        int sum=0;
        for (int i=0;i<68;++i) {
            const char c=line.at(i);
            if (c>='0' && c<='9') sum+=c-'0';
            else if (c=='-') ++sum;
            else if (!(c==' ' || c=='.' || c=='+' || (c>='A' && c<='Z'))) return false;
        }
        return line.at(68)>='0' && line.at(68)<='9' && sum%10==line.at(68)-'0';
    };
    if (!validChecksum(first) || !validChecksum(second))
        return fail(QStringLiteral("TLE checksum or characters are invalid"));
    const QByteArray firstId=first.mid(2,5).trimmed(), secondId=second.mid(2,5).trimmed();
    static const QRegularExpression catalogue(QStringLiteral("^[A-Z0-9]{1,5}$"));
    if (firstId!=secondId || !catalogue.match(QString::fromLatin1(firstId)).hasMatch())
        return fail(QStringLiteral("TLE satellite numbers do not match"));
    static const QRegularExpression epochPattern(QStringLiteral("^[0-9]{2}[ 0-9]{3}\\.[0-9]{8}$"));
    const QString epochText=QString::fromLatin1(first.mid(18,14));
    if (!epochPattern.match(epochText).hasMatch()) return fail(QStringLiteral("TLE epoch format is invalid"));
    bool yearOk=false, dayOk=false;
    const int yy=epochText.left(2).toInt(&yearOk);
    const double day=epochText.mid(2).toDouble(&dayOk);
    const int year=yy>=57 ? 1900+yy : 2000+yy;
    if (!yearOk || !dayOk || !std::isfinite(day) || day<1 || day >= (QDate::isLeapYear(year) ? 367 : 366))
        return fail(QStringLiteral("TLE epoch is invalid"));
    result.epoch=QDateTime(QDate(year,1,1),QTime(0,0),QTimeZone::UTC).addMSecs(qRound64((day-1.0)*86400000.0));
    result.catalogue=QString::fromLatin1(firstId);
    result.ok=true;
    return result;
}
}
