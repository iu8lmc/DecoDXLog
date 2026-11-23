#pragma once

#include "core/LogDatabase.h"
#include <limits>

namespace decolog::core {

struct ContestNetInventoryRow {
    qint64 id{0};
    QString uuid;
    int revision{0};
    bool deleted{false};
    QByteArray contentHash;
    QByteArray adifPayload;
};

struct ContestNetInventoryPage {
    bool ok{false};
    bool hasMore{false};
    QList<ContestNetInventoryRow> rows;
    QString error;
};

// A single connection and read transaction, used from its creating thread.
// Close promptly: a long SQLite snapshot can retain WAL pages or block writers.
class ContestNetInventory {
public:
    enum class Order { RowId, Uuid };
    ~ContestNetInventory();
    bool begin(const QString& path, Order order = Order::RowId);
    // Payload budget applies to returned UTF-8 ADIF bytes, not temporary
    // SQLite/QString allocations. A too-large first record is never skipped.
    ContestNetInventoryPage next(int limit, qint64 maxPayloadBytes = 1024 * 1024);
    // Read one UUID from the same pinned snapshot without advancing pagination.
    ContestNetInventoryPage findUuid(const QString& uuid, qint64 maxPayloadBytes = 1024 * 1024);
    void close();
    QString error() const { return m_error; }
    QString logIdentity() const { return m_identity; }
    QByteArray fileIdentity() const { return m_fileIdentity; }
    bool fileIdentityCurrent() const { return m_transaction && m_db.fileIdentityMatchesOpen(); }
    qint64 journalWatermark() const { return m_watermark; }
    // SHA-256 of versioned, sorted ADIF names and exact values; excludes UUID,
    // revision, attachments and metadata that recordChecked() does not export.
    static QByteArray fingerprint(const AdifRecord& record);

private:
    bool populate(ContestNetInventoryRow& row, QString& error);
    LogDatabase m_db;
    QString m_error;
    QString m_identity;
    QByteArray m_fileIdentity;
    qint64 m_watermark{0};
    qint64 m_afterId{std::numeric_limits<qint64>::min()};
    QString m_afterUuid;
    Order m_order{Order::RowId};
    bool m_transaction{false};
    bool m_done{false};
    bool m_started{false};
};

} // namespace decolog::core
