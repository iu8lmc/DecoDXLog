#include "core/CallList.h"

#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>
#include <algorithm>

namespace decolog::core {
namespace {
QString canonical(QString text)
{
    return text.trimmed().toUpper().remove(QLatin1Char(' ')).remove(QLatin1Char('_'));
}

// A small strict CSV reader: escaped quotes, embedded separators/newlines,
// CRLF and whole-line comments. Malformed input must not replace a good cache.
bool readRows(const QString& text, QChar separator, QList<QStringList>& rows, QString& error)
{
    QStringList row;
    QString field;
    bool quoted = false, closed = false, atStart = true;
    auto finishField = [&] { row.append(field.trimmed()); field.clear(); closed = false; atStart = true; };
    for (qsizetype i = 0; i <= text.size(); ++i) {
        const QChar c = i == text.size() ? QChar('\n') : text.at(i);
        if (!quoted && row.isEmpty() && field.trimmed().isEmpty() && c == QLatin1Char('#')) {
            while (i < text.size() && text.at(i) != QLatin1Char('\n')) ++i;
            field.clear(); atStart = true;
            continue;
        }
        if (quoted) {
            if (i == text.size()) { error = QStringLiteral("Unclosed quoted field."); return false; }
            if (c == QLatin1Char('"')) {
                if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('"')) { field += c; ++i; }
                else { quoted = false; closed = true; }
            } else field += c;
        } else if (c == separator) {
            finishField();
            if (row.size() > 64) { error = QStringLiteral("More than 64 columns."); return false; }
        } else if (c == QLatin1Char('\r') || c == QLatin1Char('\n')) {
            if (c == QLatin1Char('\r') && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('\n')) ++i;
            finishField();
            if (row.size() > 64) { error = QStringLiteral("More than 64 columns."); return false; }
            if (row.size() > 1 || !row.first().isEmpty()) rows.append(row);
            row.clear();
            if (rows.size() > 500000) { error = QStringLiteral("More than 500000 rows."); return false; }
        } else if (c == QLatin1Char('"')) {
            if (!atStart || closed) { error = QStringLiteral("Unexpected quote in an unquoted field."); return false; }
            field.clear(); quoted = true; atStart = false;
        } else {
            if (closed && !c.isSpace()) { error = QStringLiteral("Unexpected text after a quoted field."); return false; }
            if (!closed) field += c;
            if (!c.isSpace()) atStart = false;
        }
        if (field.size() > 4096) { error = QStringLiteral("A field exceeds 4096 characters."); return false; }
    }
    return true;
}

int column(const QStringList& columns, const QStringList& aliases)
{
    for (qsizetype i = 0; i < columns.size(); ++i)
        if (aliases.contains(canonical(columns.at(i)))) return int(i);
    return -1;
}
}

QString CallList::normalizedCall(const QString& value)
{
    const QString call = value.trimmed().toUpper();
    static const QRegularExpression syntax(QStringLiteral("^[A-Z0-9]+(?:/[A-Z0-9]+)*$"));
    static const QRegularExpression letter(QStringLiteral("[A-Z]")), digit(QStringLiteral("[0-9]"));
    if (call.size() < 3 || call.size() > 24 || !syntax.match(call).hasMatch()
        || !letter.match(call).hasMatch() || !digit.match(call).hasMatch()) return {};
    return call;
}

CallListData CallList::parse(const QByteArray& bytes, const QVariantMap& options, bool previewOnly)
{
    CallListData result;
    auto fail = [&](const QString& message) { result.error = message; result.calls.clear(); result.records = 0; return result; };
    if (bytes.isEmpty() || bytes.size() > maxBytes) return fail(QStringLiteral("Empty file or file larger than 32 MiB."));
    if (bytes.contains('\0')) return fail(QStringLiteral("Binary/UTF-16 input is not supported. Export UTF-8 text."));
    const QString encoding = options.value(QStringLiteral("encoding"), QStringLiteral("auto")).toString();
    QStringDecoder decoder(QStringDecoder::Utf8);
    QString text = decoder(bytes);
    QString actualEncoding = QStringLiteral("UTF-8");
    if (encoding == QLatin1String("latin1") || (decoder.hasError() && encoding == QLatin1String("auto"))) {
        text = QString::fromLatin1(bytes); actualEncoding = QStringLiteral("Latin-1");
    } else if (decoder.hasError()) return fail(QStringLiteral("Invalid UTF-8. Select the correct encoding."));
    if (text.startsWith(QChar(0xfeff))) text.remove(0, 1);

    QString delim = options.value(QStringLiteral("delimiter"), QStringLiteral("auto")).toString();
    if (delim == QLatin1String("auto") || delim.isEmpty()) {
        delim = QStringLiteral(",");
        for (const QString& line : text.split(QLatin1Char('\n'))) {
            if (line.trimmed().isEmpty() || line.trimmed().startsWith(QLatin1Char('#'))) continue;
            int best = 0;
            for (QChar candidate : {QChar(','), QChar(';'), QChar('\t')}) {
                bool inQuote = false; int count = 0;
                for (QChar c : line) { if (c == QLatin1Char('"')) inQuote = !inQuote; else if (!inQuote && c == candidate) ++count; }
                if (count > best) { best = count; delim = QString(candidate); }
            }
            break;
        }
    }
    if (delim.size() != 1 || !QStringLiteral(",;\t").contains(delim)) return fail(QStringLiteral("Unsupported separator."));
    QList<QStringList> rows;
    if (!readRows(text, delim.front(), rows, result.error)) return result;
    if (rows.isEmpty()) return fail(QStringLiteral("No data rows."));
    const bool n1mm = std::any_of(rows.cbegin(), rows.cend(), [](const QStringList& row) {
        return row.first().compare(QLatin1String("!!Order!!"), Qt::CaseInsensitive) == 0;
    });
    QStringList columns;
    const bool header = options.value(QStringLiteral("header"), true).toBool();
    if (!n1mm) {
        if (header) columns = rows.takeFirst();
        else for (int i = 0; i < rows.first().size(); ++i) columns.append(QStringLiteral("Column %1").arg(i + 1));
    }
    int callIndex = options.value(QStringLiteral("callColumn"), header ? column(columns, {"CALL", "CALLSIGN", "NOMINATIVO"}) : 0).toInt();
    QVariantList samples;
    QSet<QByteArray> seen;
    for (const auto& row : rows) {
        if (row.first().startsWith(QLatin1String("!!"))) {
            if (row.first().compare(QLatin1String("!!Order!!"), Qt::CaseInsensitive) == 0) {
                columns = row.mid(1);
                callIndex = column(columns, {"CALL"});
            }
            continue;
        }
        if (columns.isEmpty()) { ++result.skipped; continue; }
        const QString candidate = callIndex >= 0 && callIndex < row.size() ? normalizedCall(row.at(callIndex)) : QString();
        // The combined N1MM files contain LATEST/RECORDS/CLUBS/FILE rows.
        // They are metadata, not examples of membership records either.
        if (n1mm && candidate.isEmpty()) { ++result.skipped; continue; }
        if (samples.size() < 8) samples.append(QVariant(row));
        if (!result.preview.contains(QStringLiteral("columns"))) {
            result.preview.insert(QStringLiteral("columns"), columns);
            result.preview.insert(QStringLiteral("callColumn"), callIndex);
        }
        if (callIndex < 0 || callIndex >= row.size()) { ++result.skipped; continue; }
        const QString call = candidate;
        if (call.isEmpty()) { ++result.skipped; continue; }
        if (row.size() > columns.size()) {
            // N1MM exports often have empty trailing placeholders.
            bool extra = false;
            for (qsizetype i = columns.size(); i < row.size(); ++i) extra |= !row.at(i).isEmpty();
            if (extra) return fail(QStringLiteral("A data row has more fields than the column definition."));
        }
        QVariantList fields;
        QVariantMap identityFields;
        QString club;
        for (int i = 0; i < columns.size() && i < row.size(); ++i) {
            if (i == callIndex || row.at(i).isEmpty() || columns.at(i).isEmpty()) continue;
            QString label = columns.at(i);
            if (!n1mm) {
                for (const auto& role : {QStringLiteral("name"), QStringLiteral("club"), QStringLiteral("member"), QStringLiteral("qth")}) {
                    if (options.value(role + QStringLiteral("Column"), -1).toInt() == i) label = role.toUpper();
                }
            }
            if (canonical(label) == QLatin1String("CLUB")) club = row.at(i);
            fields.append(QVariantMap{{"label", label}, {"value", row.at(i)}});
            identityFields.insert(canonical(label), row.at(i));
        }
        const QVariantMap entry{{"call", call}, {"club", club}, {"fields", fields}};
        const QByteArray key = call.toUtf8() + '\n' + QJsonDocument::fromVariant(identityFields).toJson(QJsonDocument::Compact);
        if (seen.contains(key)) { ++result.skipped; continue; }
        seen.insert(key);
        result.calls[call].append(entry);
        ++result.records;
    }
    result.preview.insert(QStringLiteral("samples"), samples);
    result.preview.insert(QStringLiteral("format"), n1mm ? QStringLiteral("N1MM Call History") : QStringLiteral("CSV/TXT"));
    result.preview.insert(QStringLiteral("n1mm"), n1mm);
    result.preview.insert(QStringLiteral("encoding"), actualEncoding);
    result.preview.insert(QStringLiteral("delimiter"), delim);
    result.preview.insert(QStringLiteral("records"), result.records);
    result.preview.insert(QStringLiteral("skipped"), result.skipped);
    if (!previewOnly && result.records == 0) return fail(QStringLiteral("No valid callsigns. Check the callsign column and header setting."));
    return result;
}
} // namespace decolog::core
