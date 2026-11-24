#include "core/RoverSession.h"
#include "core/LogDatabase.h"
#include "core/Maidenhead.h"
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
namespace decolog::core {
namespace {
QString minuteUtc(const QDateTime &when) { return when.toUTC().toString(QStringLiteral("yyyy-MM-dd'T'HH:mm'Z'")); }
QString nowUtc() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs); }
bool validGrid(const QString &grid) { return grid.size() >= 4 && maidenhead::toLatLon(grid).has_value(); }
} // namespace
RoverSession::RoverSession(LogDatabase *db, QObject *parent) : QObject(parent), m_db(db)
{
    if (m_db && m_db->isOpen()) {
        QSqlQuery q(m_db->connection());
        if (q.exec(QStringLiteral("SELECT id FROM rover_session ORDER BY (ended_at IS NULL) DESC,id DESC LIMIT 1")) &&
            q.next())
            m_selectedId = q.value(0).toLongLong();
    }
}
bool RoverSession::fail(const QString &message)
{
    m_error = message;
    emit changed();
    return false;
}
QVariantMap RoverSession::session() const
{
    QVariantMap result; readSession(&result); return result;
}
bool RoverSession::readSession(QVariantMap *result, QString *error) const
{
    if (error) error->clear();
    if (!result) { if (error) *error=tr("Cannot read rover session"); return false; }
    result->clear();
    if (!m_db || !m_db->isOpen()) { if (error) *error=tr("Log is not open"); return false; }
    QSqlQuery q(m_db->connection());
    if (!q.exec(QStringLiteral(
            "SELECT s.id,s.title,s.station_profile_id,s.started_at, "
            "(SELECT gridsquare FROM rover_position p WHERE p.session_id=s.id ORDER BY p.id DESC LIMIT 1) "
            "FROM rover_session s WHERE s.ended_at IS NULL LIMIT 1"))) {
        if (error) *error=q.lastError().text(); return false;
    }
    if (!q.next()) {
        if (q.lastError().isValid()) { if (error) *error=q.lastError().text(); return false; }
        return true;
    }
    *result = {{QStringLiteral("id"), q.value(0)},
            {QStringLiteral("title"), q.value(1)},
            {QStringLiteral("profileId"), q.value(2)},
            {QStringLiteral("startedAt"), q.value(3)},
            {QStringLiteral("grid"), q.value(4)}};
    return true;
}
QVariantList RoverSession::positions() const
{
    QVariantList out;
    const qint64 id = m_selectedId > 0 ? m_selectedId : session().value(QStringLiteral("id")).toLongLong();
    if (!id)
        return out;
    QSqlQuery q(m_db->connection());
    q.prepare(QStringLiteral(
        "SELECT id,gridsquare,effective_at,recorded_at FROM rover_position WHERE session_id=? ORDER BY id DESC"));
    q.addBindValue(id);
    if (!q.exec())
        return out;
    while (q.next())
        out << QVariantMap{{QStringLiteral("id"), q.value(0)},
                           {QStringLiteral("grid"), q.value(1)},
                           {QStringLiteral("effectiveAt"), q.value(2)},
                           {QStringLiteral("recordedAt"), q.value(3)}};
    return out;
}
QVariantList RoverSession::sessions() const
{
    QVariantList out;
    if (!m_db || !m_db->isOpen())
        return out;
    QSqlQuery q(m_db->connection());
    if (!q.exec(QStringLiteral("SELECT id,title,started_at,ended_at FROM rover_session ORDER BY id DESC")))
        return out;
    while (q.next())
        out << QVariantMap{{QStringLiteral("id"), q.value(0)},
                           {QStringLiteral("title"), q.value(1)},
                           {QStringLiteral("startedAt"), q.value(2)},
                           {QStringLiteral("endedAt"), q.value(3)},
                           {QStringLiteral("active"), q.value(3).isNull()}};
    return out;
}
bool RoverSession::selectHistory(qint64 id)
{
    if (!m_db || !m_db->isOpen())
        return fail(tr("Log is not open"));
    QSqlQuery q(m_db->connection());
    q.prepare(QStringLiteral("SELECT 1 FROM rover_session WHERE id=?"));
    q.addBindValue(id);
    if (!q.exec() || !q.next())
        return fail(tr("Rover session not found"));
    m_selectedId = id;
    m_error.clear();
    emit changed();
    return true;
}
bool RoverSession::start(const QString &title, const QString &grid, qint64 profileId)
{
    const QString name = title.trimmed(), locator = grid.trimmed().toUpper();
    if (!m_db || !m_db->isOpen())
        return fail(tr("Log is not open"));
    if (name.isEmpty() || name.size() > 160 || !validGrid(locator))
        return fail(tr("Enter a session name and a valid locator"));
    if (profileId > 0) {
        const auto p = m_db->stationProfile(profileId);
        if (!p || p->deleted)
            return fail(tr("The selected station profile is not available"));
    }
    auto db = m_db->connection();
    if (!db.transaction())
        return fail(db.lastError().text());
    const QString at = minuteUtc(QDateTime::currentDateTimeUtc());
    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT INTO rover_session(uuid,title,station_profile_id,started_at) VALUES(?,?,?,?)"));
    q.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
    q.addBindValue(name);
    q.addBindValue(profileId > 0 ? QVariant(profileId) : QVariant());
    q.addBindValue(at);
    if (!q.exec()) {
        const QString err = q.lastError().text();
        db.rollback();
        return fail(err);
    }
    const qint64 id = q.lastInsertId().toLongLong();
    q.prepare(
        QStringLiteral("INSERT INTO rover_position(session_id,gridsquare,effective_at,recorded_at) VALUES(?,?,?,?)"));
    q.addBindValue(id);
    q.addBindValue(locator);
    q.addBindValue(at);
    q.addBindValue(nowUtc());
    if (!q.exec() || !db.commit()) {
        const QString err = q.lastError().text();
        db.rollback();
        return fail(err.isEmpty() ? tr("Cannot save rover session") : err);
    }
    m_selectedId = id;
    m_error.clear();
    emit changed();
    return true;
}
bool RoverSession::setPosition(const QString &grid)
{
    const QString locator = grid.trimmed().toUpper();
    if (!m_db || !m_db->isOpen() || !validGrid(locator))
        return fail(tr("Enter a valid locator"));
    auto db = m_db->connection();
    if (!db.transaction())
        return fail(db.lastError().text());
    QVariantMap current; QString readError;
    if (!readSession(&current,&readError)) { db.rollback(); return fail(readError); }
    const qint64 id = current.value(QStringLiteral("id")).toLongLong();
    if (!id) {
        db.rollback();
        return fail(tr("Start a rover session first"));
    }
    QSqlQuery latest(db);
    latest.prepare(QStringLiteral("SELECT effective_at FROM rover_position WHERE session_id=? ORDER BY id DESC LIMIT 1"));
    latest.addBindValue(id);
    if (!latest.exec() || !latest.next()) {
        db.rollback();
        return fail(tr("Cannot read rover position history"));
    }
    const QString effectiveAt = minuteUtc(QDateTime::currentDateTimeUtc());
    if (effectiveAt < latest.value(0).toString()) {
        db.rollback();
        return fail(tr("System clock moved backwards; cannot record rover position"));
    }
    if (current.value(QStringLiteral("grid")).toString() == locator) {
        db.rollback();
        m_error.clear();
        emit changed();
        return true;
    }
    QSqlQuery q(db);
    q.prepare(
        QStringLiteral("INSERT INTO rover_position(session_id,gridsquare,effective_at,recorded_at) VALUES(?,?,?,?)"));
    q.addBindValue(id);
    q.addBindValue(locator);
    q.addBindValue(effectiveAt);
    q.addBindValue(nowUtc());
    if (!q.exec() || !db.commit()) {
        const QString err = q.lastError().text();
        db.rollback();
        return fail(err.isEmpty() ? tr("Cannot save rover position") : err);
    }
    m_error.clear();
    emit changed();
    return true;
}
bool RoverSession::close()
{
    if (!m_db || !m_db->isOpen())
        return fail(tr("Log is not open"));
    QSqlQuery q(m_db->connection());
    q.prepare(QStringLiteral("UPDATE rover_session SET ended_at=? WHERE ended_at IS NULL"));
    q.addBindValue(nowUtc());
    if (!q.exec() || q.numRowsAffected() != 1)
        return fail(tr("No active rover session"));
    m_error.clear();
    emit changed();
    return true;
}
QString RoverSession::gridAt(const QDateTime &when, QString *error) const
{
    if (error) error->clear();
    QVariantMap current;
    if (!readSession(&current,error)) return {};
    const qint64 id = current.value(QStringLiteral("id")).toLongLong();
    auto reject = [error](const QString &message) {
        if (error)
            *error = message;
        return QString();
    };
    if (!id)
        return {};
    if (!when.isValid() || minuteUtc(when) > minuteUtc(QDateTime::currentDateTimeUtc()))
        return reject(tr("The QSO time is outside the rover position history"));
    QSqlQuery q(m_db->connection());
    q.prepare(QStringLiteral(
        "SELECT effective_at,MIN(gridsquare),COUNT(DISTINCT gridsquare) FROM rover_position "
        "WHERE session_id=? AND effective_at<=? GROUP BY effective_at ORDER BY effective_at DESC LIMIT 1"));
    q.addBindValue(id);
    q.addBindValue(minuteUtc(when));
    if (!q.exec() || !q.next())
        return reject(tr("The QSO time is outside the rover position history"));
    if (q.value(2).toInt() != 1)
        return reject(tr("Several rover positions were recorded in this minute; resolve the QSO locator manually"));
    return q.value(1).toString();
}
} // namespace decolog::core
