#include "core/ContestNetReconcile.h"

namespace decolog::core {

bool ContestNetReconcile::begin(const QString& localPath, const QString& remotePath, qint64 sourcePayloadBudget)
{
    close(); m_error.clear();
    if (sourcePayloadBudget<1 || sourcePayloadBudget>16*1024*1024) { m_error=QStringLiteral("Invalid comparison payload budget"); return false; }
    m_sourcePayloadBudget=sourcePayloadBudget;
    if (!m_local.inventory.begin(localPath,ContestNetInventory::Order::Uuid)) { m_error=m_local.inventory.error(); close(); return false; }
    if (!m_remote.inventory.begin(remotePath,ContestNetInventory::Order::Uuid)) { m_error=m_remote.inventory.error(); close(); return false; }
    m_open=true; return true;
}

bool ContestNetReconcile::fill(Source& source)
{
    if (source.index<source.rows.size() || source.exhausted) return true;
    const auto page=source.inventory.next(64,m_sourcePayloadBudget);
    if (!page.ok) { m_error=page.error; return false; }
    source.rows=page.rows; source.index=0; source.exhausted=!page.hasMore;
    if (source.rows.isEmpty() && !source.exhausted) { m_error=QStringLiteral("Inventory returned an empty incomplete page"); return false; }
    return true;
}

ContestNetDifferencePage ContestNetReconcile::next(int limit)
{
    ContestNetDifferencePage page;
    if (!m_open) { page.error=QStringLiteral("Comparison is not open"); return page; }
    if (limit<1 || limit>500) { page.error=QStringLiteral("Invalid comparison page size"); return page; }
    if (!m_local.inventory.fileIdentityCurrent() || !m_remote.inventory.fileIdentityCurrent()) {
        page.error=QStringLiteral("A log file changed during comparison"); close(); return page;
    }
    auto fail=[&] { page.rows.clear(); page.error=m_error; close(); return page; };
    auto side=[](const ContestNetInventoryRow& row) { return ContestNetDifference::Side{true,row.revision,row.deleted,row.contentHash}; };
    while (page.rows.size()<limit) {
        if (!fill(m_local) || !fill(m_remote)) return fail();
        const bool left=m_local.index<m_local.rows.size(), right=m_remote.index<m_remote.rows.size();
        if (!left && !right) break;
        const int order=!left ? 1 : !right ? -1 : m_local.rows.at(m_local.index).uuid.toUtf8().compare(m_remote.rows.at(m_remote.index).uuid.toUtf8());
        ContestNetDifference row;
        if (order<0) { const auto& r=m_local.rows.at(m_local.index++); row.uuid=r.uuid; row.local=side(r); row.kind=ContestNetDifference::Kind::LocalOnly; }
        else if (order>0) { const auto& r=m_remote.rows.at(m_remote.index++); row.uuid=r.uuid; row.remote=side(r); row.kind=ContestNetDifference::Kind::RemoteOnly; }
        else {
            const auto& a=m_local.rows.at(m_local.index++); const auto& b=m_remote.rows.at(m_remote.index++);
            row.uuid=a.uuid; row.local=side(a); row.remote=side(b);
            if (a.deleted!=b.deleted) row.kind=ContestNetDifference::Kind::DeletionConflict;
            else if (a.contentHash!=b.contentHash) row.kind=ContestNetDifference::Kind::ContentConflict;
            else if (a.revision!=b.revision) row.kind=ContestNetDifference::Kind::RevisionDifference;
            else row.kind=ContestNetDifference::Kind::MatchingFingerprint;
        }
        page.rows.append(row);
    }
    if (!fill(m_local) || !fill(m_remote)) return fail();
    if (!m_local.inventory.fileIdentityCurrent() || !m_remote.inventory.fileIdentityCurrent()) {
        m_error=QStringLiteral("A log file changed during comparison"); return fail();
    }
    page.hasMore=m_local.index<m_local.rows.size() || m_remote.index<m_remote.rows.size();
    page.ok=true; return page;
}

void ContestNetReconcile::close()
{
    m_local.inventory.close(); m_remote.inventory.close();
    m_local.rows.clear(); m_remote.rows.clear(); m_local.index=m_remote.index=0;
    m_local.exhausted=m_remote.exhausted=false; m_open=false;
}

} // namespace decolog::core
