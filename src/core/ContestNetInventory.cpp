#include "core/ContestNetInventory.h"

#include <QSqlError>
#include <QSqlQuery>

namespace decolog::core {

ContestNetInventory::~ContestNetInventory() { close(); }

bool ContestNetInventory::begin(const QString& path)
{
    close(); m_error.clear();
    if (path.isEmpty() || path==QLatin1String(":memory:")) { m_error=QStringLiteral("A file-backed log is required"); return false; }
    if (!m_db.openReadOnly(path)) { m_error=m_db.lastError(); close(); return false; }
    if (m_db.openFileIdentity().isEmpty() || !m_db.fileIdentityMatchesOpen()) { m_error=QStringLiteral("Log file identity unavailable"); close(); return false; }
    if (m_db.schemaVersion()<28) { m_error=QStringLiteral("Log requires migration before inventory"); close(); return false; }
    {
        QSqlQuery q(m_db.connection());
        if (!q.exec(QStringLiteral("BEGIN"))) { m_error=q.lastError().text(); }
        else {
            m_transaction=true;
            m_identity=m_db.identity();
            if (m_identity.isEmpty()) m_error=QStringLiteral("Log identity unavailable");
            else if (!q.exec(QStringLiteral("SELECT COALESCE(MAX(sequence),0) FROM contest_net_change"))) m_error=q.lastError().text();
            else if (!q.next()) m_error=QStringLiteral("Cannot read the journal watermark");
            else m_watermark=q.value(0).toLongLong();
        }
    }
    if (!m_error.isEmpty() || !m_transaction || !m_db.fileIdentityMatchesOpen()) {
        if (m_error.isEmpty()) m_error=QStringLiteral("Log file changed during inventory setup");
        close(); return false;
    }
    m_fileIdentity=m_db.openFileIdentity();
    return true;
}

ContestNetInventoryPage ContestNetInventory::next(int limit)
{
    ContestNetInventoryPage page;
    if (!m_transaction) { page.error=QStringLiteral("Inventory is not open"); return page; }
    if (limit<1 || limit>500) { page.error=QStringLiteral("Invalid inventory page size"); return page; }
    if (!m_db.fileIdentityMatchesOpen()) { page.error=QStringLiteral("Log file changed during inventory"); close(); return page; }
    if (m_done) { page.ok=true; return page; }
    {
        QSqlQuery q(m_db.connection());
        q.prepare(m_started ? QStringLiteral("SELECT id,uuid,revision,deleted FROM qso WHERE id>? ORDER BY id LIMIT ?")
                            : QStringLiteral("SELECT id,uuid,revision,deleted FROM qso ORDER BY id LIMIT ?"));
        if (m_started) q.addBindValue(m_afterId);
        q.addBindValue(limit+1);
        if (!q.exec()) page.error=q.lastError().text();
        else {
            while (q.next()) {
                if (page.rows.size()==limit) { page.hasMore=true; break; }
                page.rows.append({q.value(0).toLongLong(),q.value(1).toString(),q.value(2).toInt(),q.value(3).toBool()});
            }
            if (q.lastError().isValid()) page.error=q.lastError().text();
        }
    }
    if (!page.error.isEmpty()) { page.rows.clear(); close(); return page; }
    if (!m_db.fileIdentityMatchesOpen()) { page.rows.clear(); page.error=QStringLiteral("Log file changed during inventory"); close(); return page; }
    if (!page.rows.isEmpty()) m_afterId=page.rows.last().id;
    m_started=true; m_done=!page.hasMore; page.ok=true; return page;
}

void ContestNetInventory::close()
{
    if (m_transaction && m_db.isOpen()) { QSqlQuery q(m_db.connection()); q.exec(QStringLiteral("ROLLBACK")); }
    m_transaction=false; m_db.close(); m_identity.clear(); m_fileIdentity.clear();
    m_watermark=0; m_afterId=std::numeric_limits<qint64>::min(); m_done=false; m_started=false;
}

} // namespace decolog::core
