#include "core/QslOcrSearch.h"
#include "core/LogDatabase.h"
#include <QPointer>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace decolog::core {
QslOcrSearch::QslOcrSearch(LogDatabase *db, QObject *parent) : QObject(parent), m_db(db) {}
QslOcrSearch::~QslOcrSearch() { if (m_cancel) m_cancel->store(true); m_pool.waitForDone(); }
bool QslOcrSearch::start(const QString &rawCall)
{
    if (m_busy) return false;
    m_matches.clear(); m_error.clear(); m_truncated = false;
    const QString call = rawCall.trimmed().toUpper();
    static const QRegularExpression valid(QStringLiteral("^(?=.{3,15}$)(?=.*[A-Z])(?=.*[0-9])[A-Z0-9]+(?:/[A-Z0-9]+)*$"));
    if (!m_db || !m_db->isOpen() || m_db->path().isEmpty() || m_db->path() == QLatin1String(":memory:") || !valid.match(call).hasMatch()) {
        m_error = tr("Choose a callsign and an open file-backed log"); emit changed(); return false;
    }
    const QString path = m_db->path();
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const auto cancelled = m_cancel;
    m_busy = true; emit changed();
    QPointer<QslOcrSearch> self(this);
    m_pool.start([self, path, call, cancelled] {
        QVariantList matches;
        QString error;
        bool truncated = false;
        const QString connection = QStringLiteral("qsl-ocr-search-") + QUuid::createUuid().toString();
        {
            QSqlDatabase sql = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
            sql.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=100"));
            sql.setDatabaseName(path);
            if (!sql.open()) {
                error = tr("Cannot open the log for searching");
            } else if (!cancelled->load()) {
                QSqlQuery q(sql);
                q.prepare(QStringLiteral("SELECT id,call,qso_datetime_on,band,mode,source FROM qso "
                                         "WHERE call=? AND deleted=0 ORDER BY id DESC LIMIT 101"));
                q.addBindValue(call);
                if (!q.exec()) {
                    error = tr("Cannot search the log");
                } else {
                    while (!cancelled->load() && q.next()) {
                        if (matches.size() == 100) { truncated = true; break; }
                        matches << QVariantMap{{QStringLiteral("id"), q.value(0)}, {QStringLiteral("call"), q.value(1)},
                                               {QStringLiteral("at"), q.value(2)}, {QStringLiteral("band"), q.value(3)},
                                               {QStringLiteral("mode"), q.value(4)}, {QStringLiteral("source"), q.value(5)}};
                    }
                    if (q.lastError().isValid()) error = tr("Cannot search the log");
                }
            }
            sql.close();
        }
        QSqlDatabase::removeDatabase(connection);
        QMetaObject::invokeMethod(self.data(), [self, path, matches, error, truncated, cancelled] {
            if (!self) return;
            self->m_busy = false;
            self->m_cancel.reset();
            const bool sameLog = self->m_db && self->m_db->isOpen() && self->m_db->path() == path;
            const bool success = !cancelled->load() && error.isEmpty() && sameLog;
            self->m_matches = success ? matches : QVariantList{};
            self->m_truncated = success && truncated;
            self->m_error = cancelled->load() ? tr("Search cancelled") : !sameLog ? tr("The open log changed") : error;
            emit self->changed();
            emit self->finished(success);
        }, Qt::QueuedConnection);
    });
    return true;
}
void QslOcrSearch::cancel() { if (m_cancel) m_cancel->store(true); }
} // namespace decolog::core
