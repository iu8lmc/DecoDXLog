#pragma once
#include <QByteArray>
#include <QDateTime>
#include <QString>

namespace decolog::core::tle {
struct Inspection {
    bool ok{false};
    QString error;
    QString catalogue;
    QDateTime epoch;
};
// Verifies line length, catalogue agreement, checksum and epoch; does not
// authenticate the source or validate every orbital element.
Inspection inspect(const QByteArray& first, const QByteArray& second);
}
