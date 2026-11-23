#pragma once

#include "core/ContestNetInventory.h"

namespace decolog::core {

struct ContestNetDifference {
    enum class Kind { MatchingFingerprint, LocalOnly, RemoteOnly, RevisionDifference, ContentConflict, DeletionConflict };
    struct Side { bool present{false}; int revision{0}; bool deleted{false}; QByteArray contentHash; };
    QString uuid;
    Kind kind{Kind::MatchingFingerprint};
    Side local;
    Side remote;
};

struct ContestNetDifferencePage {
    bool ok{false};
    bool hasMore{false};
    QList<ContestNetDifference> rows;
    QString error;
};

// Read-only comparison of two independently pinned snapshots. This does not
// apply changes or infer that a higher revision is authoritative.
class ContestNetReconcile {
public:
    bool begin(const QString& localPath, const QString& remotePath, qint64 sourcePayloadBudget = 1024 * 1024);
    ContestNetDifferencePage next(int limit);
    void close();
    QString error() const { return m_error; }
    qint64 localWatermark() const { return m_local.inventory.journalWatermark(); }
    qint64 remoteWatermark() const { return m_remote.inventory.journalWatermark(); }
    QString localIdentity() const { return m_local.inventory.logIdentity(); }
    QString remoteIdentity() const { return m_remote.inventory.logIdentity(); }

private:
    struct Source { ContestNetInventory inventory; QList<ContestNetInventoryRow> rows; qsizetype index{0}; bool exhausted{false}; };
    bool fill(Source& source);
    Source m_local, m_remote;
    QString m_error;
    qint64 m_sourcePayloadBudget{1024 * 1024};
    bool m_open{false};
};

} // namespace decolog::core
