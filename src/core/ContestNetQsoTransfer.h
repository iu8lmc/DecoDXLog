#pragma once
#include "core/ContestNetInventory.h"
#include <QJsonObject>

namespace decolog::core::contestqso {
// Experimental framing only: sender identifiers are not authentication.
class Encoder {
public:
    Encoder(const QString& transfer, const QString& log, const ContestNetInventoryRow& row);
    bool ok() const { return m_error.isEmpty(); }
    QString error() const { return m_error; }
    int count() const;
    QJsonObject frame(int index) const;
    QList<QJsonObject> framesForRequest(const QJsonObject& request) const;
private:
    QString m_transfer, m_log, m_error;
    ContestNetInventoryRow m_row;
    QByteArray m_payloadHash;
};
class Receiver {
public:
    Receiver(const QString& sender, const QString& transfer) : m_sender(sender), m_transfer(transfer) {}
    bool accept(const QString& sender, const QJsonObject& frame);
    bool complete() const { return m_complete; }
    QJsonObject missingRequest(int fromIndex) const;
    ContestNetInventoryRow row() const { return m_complete ? m_row : ContestNetInventoryRow{}; }
    QString sourceLog() const { return m_complete ? m_log : QString{}; }
    QString error() const { return m_error; }
private:
    QString m_sender, m_transfer, m_log, m_error;
    ContestNetInventoryRow m_row;
    QByteArray m_payloadHash;
    QList<QByteArray> m_chunks;
    int m_total{0}, m_received{0};
    bool m_complete{false}, m_failed{false};
};
}
