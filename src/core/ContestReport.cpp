#include "core/ContestReport.h"

#include <QDate>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QTime>

namespace decolog::core::contestreport {

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
    const QRegularExpression rowRx(QStringLiteral(R"(^\s*(\d{3,6}(?:\.\d+)?)\s+(\S+)\s+(\d{4}-\d{2}-\d{2})\s+(\d{4})\s+([A-Z0-9/]+)\s+(\S+)\s+([A-Z0-9/]+)\s+(\S+)(?:\s+correct\s+(\S+))?\s*$)"), QRegularExpression::CaseInsensitiveOption);
    for (const QString& line : report.lines) {
        const auto contest = contestRx.match(line.trimmed());
        const auto call = callRx.match(line.trimmed());
        if (contest.hasMatch() && report.contest.isEmpty()) report.contest = contest.captured(1);
        if (call.hasMatch() && report.ownCall.isEmpty()) report.ownCall = call.captured(1).toUpper();
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
        const bool correctionRequired = section != QLatin1String("not_in_log");
        if (!row.hasMatch() || row.captured(5).compare(report.ownCall,Qt::CaseInsensitive)!=0 ||
            !QDate::fromString(row.captured(3),Qt::ISODate).isValid() || !QTime::fromString(row.captured(4),QStringLiteral("HHmm")).isValid() ||
            (correctionRequired != !row.captured(9).isEmpty())) {
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
