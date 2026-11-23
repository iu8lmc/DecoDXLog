#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QList>

namespace decolog::core { class LogDatabase; }

namespace decolog::core::contestreport {

struct Entry {
    int lineNumber{0};       // one-based position in original text
    QString kind;            // not_in_log, incorrect_call, incorrect_exchange
    QString frequencyKhz;
    QString mode;
    QString date;
    QString time;
    QString ownCall;
    QString call;
    QString sentExchange;
    QString receivedExchange;
    QString correction;
    QString original;
};

struct Report {
    QString error;
    QString contest;
    QString ownCall;
    QStringList lines;           // all original lines, including final empty line
    QList<int> unrecognizedLines; // zero-based; only nonempty lines in supported sections
    QList<Entry> entries;
};

struct Candidate {
    qint64 id{0};
    QString uuid;
    QString utc;
    QString stationCall; // may be the current profile when QSO has no station call
    QString stationSource; // qso, profile or unknown
};
struct CandidateResult {
    QString error;
    QList<Candidate> candidates;
    bool truncated{false};
};

// Read-only possible matches by recorded call, band, mode family and UTC minute.
// Results are suggestions, never an assertion of identity or an edit.
CandidateResult findCandidates(const LogDatabase& db, const Entry& entry);

// CQ WW LCR text as published by the contest organiser. Unsupported sections and
// lines are retained, not interpreted as lost or corrected QSO.
Report parseCqWw(const QByteArray& bytes);

}
