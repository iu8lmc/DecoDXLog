#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QList>

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

// CQ WW LCR text as published by the contest organiser. Unsupported sections and
// lines are retained, not interpreted as lost or corrected QSO.
Report parseCqWw(const QByteArray& bytes);

}
