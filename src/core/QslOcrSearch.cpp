#include "core/QslOcrSearch.h"
#include "core/LogDatabase.h"
#include "core/FileIdentity.h"
#include <QPointer>
#include <QDateTime>
#include <QRegularExpression>
#include <QSet>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace decolog::core {
QslOcrSearch::QslOcrSearch(LogDatabase *db, QObject *parent) : QObject(parent), m_db(db) {}
QslOcrSearch::~QslOcrSearch() { if (m_cancel) m_cancel->store(true); m_pool.waitForDone(); }
bool QslOcrSearch::start(const QString &rawCall)
{
    return startFiltered(rawCall,{},{});
}
bool QslOcrSearch::startFiltered(const QString &rawCall, const QString &dateUtc, const QString &timeUtc)
{
    return startSearch(rawCall, dateUtc, timeUtc, false);
}
bool QslOcrSearch::startSimilar(const QString &rawCall, const QString &dateUtc, const QString &timeUtc)
{
    return startSearch(rawCall, dateUtc, timeUtc, true);
}
bool QslOcrSearch::startSearch(const QString &rawCall, const QString &dateUtc, const QString &timeUtc, bool similar)
{
    if (m_busy) return false;
    m_matches.clear(); m_error.clear(); m_truncated = false;
    const QString call = rawCall.trimmed().toUpper();
    const QDate date=QDate::fromString(dateUtc,Qt::ISODate);
    const QTime time=QTime::fromString(timeUtc,QStringLiteral("HH:mm"));
    if ((!dateUtc.isEmpty() && (!date.isValid() || date.toString(Qt::ISODate)!=dateUtc)) ||
        (!timeUtc.isEmpty() && (!time.isValid() || time.toString(QStringLiteral("HH:mm"))!=timeUtc))) {
        m_error=tr("Choose a valid UTC date and time"); emit changed(); return false;
    }
    static const QRegularExpression valid(QStringLiteral("^(?=.{3,15}$)(?=.*[A-Z])(?=.*[0-9])[A-Z0-9]+(?:/[A-Z0-9]+)*$"));
    if (!m_db || !m_db->isOpen() || m_db->path().isEmpty() || m_db->path() == QLatin1String(":memory:") || !valid.match(call).hasMatch()) {
        m_error = tr("Choose a callsign and an open file-backed log"); emit changed(); return false;
    }
    QSet<QString> variants{call};
    if (similar) {
        const QString alphabet = QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
        for (qsizetype i=0; i<call.size(); ++i) {
            if (call.at(i)==QLatin1Char('/')) continue;
            for (const QChar ch : alphabet) { QString v=call; v[i]=ch; if (valid.match(v).hasMatch()) variants.insert(v); }
            if (i+1<call.size() && call.at(i+1)!=QLatin1Char('/')) {
                QString v=call; v[i]=call.at(i+1); v[i+1]=call.at(i);
                if (valid.match(v).hasMatch()) variants.insert(v);
            }
            QString shorter=call; shorter.remove(i,1);
            if (valid.match(shorter).hasMatch()) variants.insert(shorter);
        }
        if (call.size() < 15) {
            for (qsizetype i=0; i<=call.size(); ++i) {
                for (const QChar ch : alphabet) {
                    QString longer=call; longer.insert(i,ch);
                    if (valid.match(longer).hasMatch()) variants.insert(longer);
                }
            }
        }
    }
    const QStringList calls=variants.values();
    const QString path = m_db->path(), identity = m_db->identity();
    const QByteArray fileToken = m_db->openFileIdentity();
    const quint64 openGeneration = m_db->openGeneration();
    if (identity.isEmpty() || fileToken.isEmpty() || !m_db->fileIdentityMatchesOpen()) {
        m_error = tr("The open log changed"); emit changed(); return false;
    }
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const auto cancelled = m_cancel;
    m_busy = true; emit changed();
    QPointer<QslOcrSearch> self(this);
    m_pool.start([self, path, identity, fileToken, openGeneration, calls, call, similar, dateUtc, timeUtc, cancelled] {
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
                QSqlQuery identityQuery(sql);
                if (!sql.transaction() || fileidentity::read(path) != fileToken ||
                    !identityQuery.exec(QStringLiteral("SELECT uuid FROM log_identity WHERE id=1")) ||
                    !identityQuery.next() || identityQuery.value(0).toString() != identity) {
                    error = tr("The open log changed");
                } else {
                    QSqlQuery q(sql);
                    QString query=QStringLiteral("SELECT id,call,qso_datetime_on,band,mode,source,uuid,revision FROM qso WHERE call IN (SELECT value FROM json_each(?)) AND deleted=0");
                    if (!dateUtc.isEmpty()) query+=QStringLiteral(" AND strftime('%Y-%m-%d',qso_datetime_on)=?");
                    if (!timeUtc.isEmpty()) query+=QStringLiteral(" AND strftime('%H:%M',qso_datetime_on)=?");
                    query+=similar ? QStringLiteral(" ORDER BY CASE WHEN call=? THEN 0 ELSE 1 END,id DESC LIMIT 101")
                                   : QStringLiteral(" ORDER BY id DESC LIMIT 101");
                    q.prepare(query);
                    q.addBindValue(QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(calls)).toJson(QJsonDocument::Compact)));
                    if (!dateUtc.isEmpty()) q.addBindValue(dateUtc);
                    if (!timeUtc.isEmpty()) q.addBindValue(timeUtc);
                    if (similar) q.addBindValue(call);
                    if (!q.exec()) {
                        error = tr("Cannot search the log");
                    } else {
                        while (!cancelled->load() && q.next()) {
                            if (matches.size() == 100) { truncated = true; break; }
                            matches << QVariantMap{{QStringLiteral("id"), q.value(0)}, {QStringLiteral("call"), q.value(1)},
                                                   {QStringLiteral("similar"), q.value(1).toString() != call},
                                                   {QStringLiteral("at"), q.value(2)}, {QStringLiteral("band"), q.value(3)},
                                                   {QStringLiteral("mode"), q.value(4)}, {QStringLiteral("source"), q.value(5)},
                                                   {QStringLiteral("uuid"), q.value(6)}, {QStringLiteral("revision"), q.value(7)}};
                        }
                        if (q.lastError().isValid()) error = tr("Cannot search the log");
                    }
                }
                sql.rollback();
            }
            sql.close();
        }
        QSqlDatabase::removeDatabase(connection);
        QMetaObject::invokeMethod(self.data(), [self, path, identity, fileToken, openGeneration, matches, error, truncated, cancelled] {
            if (!self) return;
            self->m_busy = false;
            self->m_cancel.reset();
            const bool sameLog = self->m_db && self->m_db->isOpen() && self->m_db->path() == path &&
                                 self->m_db->openGeneration() == openGeneration && self->m_db->identity() == identity &&
                                 self->m_db->openFileIdentity() == fileToken && self->m_db->fileIdentityMatchesOpen();
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
