#include "core/ContestReport.h"
#include "core/LogDatabase.h"
#include "core/Bands.h"

#include <QDate>
#include <QDateTime>
#include <QTimeZone>
#include <QSqlQuery>
#include <QSqlError>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QTime>

namespace decolog::core::contestreport {

CandidateResult findCandidates(const LogDatabase& db, const Entry& entry, int toleranceMinutes)
{
    CandidateResult result;
    if (toleranceMinutes < 0 || toleranceMinutes > 5) {
        result.error=QStringLiteral("Time tolerance must be between 0 and 5 minutes.");
        return result;
    }
    const QDate day=QDate::fromString(entry.date,Qt::ISODate);
    const QTime minute=QTime::fromString(entry.time,QStringLiteral("HHmm"));
    bool frequencyOk=false; const double khz=entry.frequencyKhz.toDouble(&frequencyOk);
    const QString band=frequencyOk ? bands::fromMhz(khz/1000.0) : QString();
    const QString mode=entry.mode.trimmed().toUpper();
    QString modeClause;
    if (mode == QLatin1String("CW")) modeClause=QStringLiteral("('CW','CW-R')");
    else if (mode == QLatin1String("PH")) modeClause=QStringLiteral("('SSB','USB','LSB','AM','DSB')");
    else if (mode == QLatin1String("RY")) modeClause=QStringLiteral("('RTTY')");
    if (!db.isOpen() || !day.isValid() || !minute.isValid() || band.isEmpty() || modeClause.isEmpty() || entry.call.trimmed().isEmpty()) {
        result.error=QStringLiteral("Cannot search this report row: invalid time, frequency, callsign or unsupported mode.");
        return result;
    }
    const QString reportedCall=entry.call.trimmed().toUpper();
    QString correctedCall=reportedCall;
    const QString correction=entry.correction.trimmed().toUpper();
    static const QRegularExpression safeCall(QStringLiteral("^[A-Z0-9/]{1,32}$"));
    if (entry.kind == QLatin1String("incorrect_call") && safeCall.match(correction).hasMatch())
        correctedCall=correction;
    const QDateTime start(day,minute,QTimeZone::UTC);
    QSqlQuery q(db.connection());
    q.prepare(QStringLiteral("SELECT q.id,q.uuid,q.qso_datetime_on,"
        "COALESCE(NULLIF(CASE WHEN json_valid(q.adif_extra) THEN json_extract(q.adif_extra,'$.STATION_CALLSIGN') END,''),p.station_callsign,''),"
        "CASE WHEN NULLIF(CASE WHEN json_valid(q.adif_extra) THEN json_extract(q.adif_extra,'$.STATION_CALLSIGN') END,'') IS NOT NULL THEN 'qso' "
        "WHEN NULLIF(p.station_callsign,'') IS NOT NULL THEN 'profile' ELSE 'unknown' END, "
        "q.revision,q.freq,"
        "CASE WHEN json_valid(q.adif_extra) THEN COALESCE(NULLIF(json_extract(q.adif_extra,'$.SRX_STRING'),''),json_extract(q.adif_extra,'$.SRX'),'') ELSE '' END,"
        "CASE WHEN json_valid(q.adif_extra) THEN COALESCE(json_extract(q.adif_extra,'$.CONTEST_ID'),'') ELSE '' END, q.call "
        "FROM qso q LEFT JOIN station_profile p ON p.id=q.station_profile_id "
        "WHERE q.deleted=0 AND q.call IN (?,?) AND q.band=? AND q.qso_datetime_on>=? AND q.qso_datetime_on<? AND q.mode IN ")
        + modeClause + QStringLiteral(" ORDER BY CASE WHEN q.qso_datetime_on>=? AND q.qso_datetime_on<? THEN 0 ELSE 1 END, "
          "COALESCE(CASE WHEN q.qso_datetime_on<? THEN julianday(?)-julianday(q.qso_datetime_on) "
          "WHEN q.qso_datetime_on>=? THEN julianday(q.qso_datetime_on)-julianday(?) ELSE 0 END, 999999), "
          "CASE WHEN q.call=? THEN 0 ELSE 1 END,q.qso_datetime_on,q.id LIMIT 6"));
    const QString minuteStart=start.toString(Qt::ISODate), minuteEnd=start.addSecs(60).toString(Qt::ISODate);
    q.addBindValue(reportedCall); q.addBindValue(correctedCall); q.addBindValue(band);
    q.addBindValue(start.addSecs(-60*toleranceMinutes).toString(Qt::ISODate));
    q.addBindValue(start.addSecs(60*(toleranceMinutes+1)).toString(Qt::ISODate));
    q.addBindValue(minuteStart); q.addBindValue(minuteEnd);
    q.addBindValue(minuteStart); q.addBindValue(minuteStart); q.addBindValue(minuteEnd); q.addBindValue(minuteEnd);
    q.addBindValue(reportedCall);
    if (!q.exec()) { result.error=q.lastError().text(); return result; }
    while (q.next()) {
        if (result.candidates.size()==5) { result.truncated=true; break; }
        const QString frequency=q.value(6).isNull() ? QString() : QString::number(q.value(6).toDouble()*1000.0,'g',12);
        result.candidates.append({q.value(0).toLongLong(),q.value(1).toString(),q.value(2).toString(),q.value(3).toString(),
                                  q.value(4).toString(),q.value(5).toInt(),frequency,q.value(7).toString(),q.value(8).toString(),
                                  q.value(9).toString(),q.value(9).toString()==reportedCall ? QStringLiteral("reported") : QStringLiteral("corrected")});
    }
    return result;
}

Report parseCqWw(const QByteArray& bytes)
{
    Report report;
    if (bytes.isEmpty() || bytes.size() > 2 * 1024 * 1024) {
        report.error = QStringLiteral("Report is empty or exceeds 2 MiB");
        return report;
    }
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString text = decoder.decode(bytes);
    if (decoder.hasError() || text.contains(QChar::Null)) {
        report.error = QStringLiteral("Report must be valid UTF-8 text");
        return report;
    }
    report.lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    const QRegularExpression contestRx(QStringLiteral(R"(^Contest:\s*(\d{4}\s+CQ\s+WORLD\s+WIDE\s+.*CONTEST)\s*$)"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression callRx(QStringLiteral(R"(^Call:\s*([A-Z0-9/]+)\s*$)"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression headingRx(QStringLiteral(R"(^\s*\*{3,}\s*(.*?)\s*\*{3,}\s*$)"));
    const QRegularExpression rowRx(QStringLiteral(R"(^\s*(\d{3,6}(?:\.\d+)?)\s+(\S+)\s+(\d{4}-\d{2}-\d{2})\s+(\d{4})\s+([A-Z0-9/]+)\s+(\S+)\s+([A-Z0-9/]+)\s+(\S+)(?:\s+(?:correct\s+(\S+)|(incorrect\s+call)))?\s*$)"), QRegularExpression::CaseInsensitiveOption);
    bool bodyStarted = false;
    for (const QString& line : report.lines) {
        const QString trimmed = line.trimmed();
        if (headingRx.match(trimmed).hasMatch()) bodyStarted = true;
        const auto contest = contestRx.match(trimmed);
        const auto call = callRx.match(trimmed);
        if (!bodyStarted && ((trimmed.startsWith(QLatin1String("Contest:"), Qt::CaseInsensitive) && !contest.hasMatch())
            || (trimmed.startsWith(QLatin1String("Call:"), Qt::CaseInsensitive) && !call.hasMatch()))) {
            report.error = QStringLiteral("Malformed CQ WW identity header");
            return report;
        }
        if (contest.hasMatch()) {
            const QString value = contest.captured(1).simplified();
            if (!report.contest.isEmpty() && report.contest.compare(value, Qt::CaseInsensitive) != 0) {
                report.error = QStringLiteral("Conflicting CQ WW contest headers");
                return report;
            }
            report.contest = value;
        }
        if (call.hasMatch()) {
            const QString value = call.captured(1).toUpper();
            if (!report.ownCall.isEmpty() && report.ownCall != value) {
                report.error = QStringLiteral("Conflicting CQ WW callsign headers");
                return report;
            }
            report.ownCall = value;
        }
    }
    if (report.contest.isEmpty() || report.ownCall.isEmpty()) {
        report.error = QStringLiteral("Not a supported CQ WW text report");
        return report;
    }
    QString section;
    bool foundSection = false;
    for (qsizetype i=0; i<report.lines.size(); ++i) {
        const QString& line=report.lines.at(i);
        const auto heading=headingRx.match(line.trimmed());
        if (heading.hasMatch()) {
            const QString name=heading.captured(1).trimmed();
            section = name.compare(QLatin1String("Not In Log"), Qt::CaseInsensitive)==0 ? QStringLiteral("not_in_log")
                    : name.compare(QLatin1String("Incorrect Call"), Qt::CaseInsensitive)==0 ? QStringLiteral("incorrect_call")
                    : name.compare(QLatin1String("Incorrect Exchange Information"), Qt::CaseInsensitive)==0 ? QStringLiteral("incorrect_exchange")
                    : QString();
            foundSection = foundSection || !section.isEmpty();
            continue;
        }
        if (section.isEmpty() || line.trimmed().isEmpty()) continue;
        const auto row=rowRx.match(line.trimmed());
        const bool uncorrectedCall = section == QLatin1String("incorrect_call") && !row.captured(10).isEmpty();
        const bool correctionRequired = section != QLatin1String("not_in_log") && !uncorrectedCall;
        if (!row.hasMatch() || row.captured(5).compare(report.ownCall,Qt::CaseInsensitive)!=0 ||
            !QDate::fromString(row.captured(3),Qt::ISODate).isValid() || !QTime::fromString(row.captured(4),QStringLiteral("HHmm")).isValid() ||
            (!row.captured(10).isEmpty() && !uncorrectedCall) || (correctionRequired != !row.captured(9).isEmpty())) {
            report.unrecognizedLines.append(int(i));
            continue;
        }
        Entry entry;
        entry.lineNumber=int(i)+1; entry.kind=section; entry.frequencyKhz=row.captured(1);
        entry.mode=row.captured(2).toUpper(); entry.date=row.captured(3); entry.time=row.captured(4);
        entry.ownCall=row.captured(5).toUpper(); entry.sentExchange=row.captured(6);
        entry.call=row.captured(7).toUpper(); entry.receivedExchange=row.captured(8);
        entry.correction=row.captured(9); entry.original=line;
        report.entries.append(entry);
    }
    if (!foundSection)
        report.error = QStringLiteral("No supported CQ WW review sections found");
    return report;
}

}
