#pragma once
#include <QString>
#include <QStringList>

namespace decolog::core::referencecsv {
// One CSV record per line; reject malformed quoting rather than infer data.
inline bool parseLine(const QString& line, QStringList* values)
{
    values->clear(); QString cell; bool quoted = false, afterQuote = false;
    for (qsizetype i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (quoted) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"')) { cell += c; ++i; }
                else { quoted = false; afterQuote = true; }
            } else cell += c;
        } else if (c == QLatin1Char(',')) { values->append(cell.trimmed()); cell.clear(); afterQuote = false; }
        else if (c == QLatin1Char('"')) { if (cell.isEmpty() && !afterQuote) quoted = true; else return false; }
        else if (afterQuote && !c.isSpace()) return false;
        else if (!afterQuote) cell += c;
    }
    if (quoted) return false;
    values->append(cell.trimmed()); return true;
}
}
