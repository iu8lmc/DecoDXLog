#include "core/RadioNet.h"
#include "core/LogDatabase.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace decolog::core {
namespace {
QString nowUtc() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs); }
} // namespace
RadioNet::RadioNet(LogDatabase *db, QObject *parent) : QObject(parent), m_db(db) {}

bool RadioNet::fail(const QString &message)
{
    m_error = message;
    emit changed();
    return false;
}
void RadioNet::load()
{
    m_sessionId = 0;
    if (m_db && m_db->isOpen()) {
        QSqlQuery q(m_db->connection());
        if (q.exec(
                QStringLiteral("SELECT id FROM radio_net_session ORDER BY (ended_at IS NULL) DESC,id DESC LIMIT 1")) &&
            q.next())
            m_sessionId = q.value(0).toLongLong();
    }
    m_error.clear();
    emit changed();
}
QVariantList RadioNet::sessions() const
{
    QVariantList out;
    if (!m_db || !m_db->isOpen())
        return out;
    QSqlQuery q(m_db->connection());
    if (!q.exec(QStringLiteral("SELECT id,title,started_at,ended_at FROM radio_net_session ORDER BY id DESC")))
        return out;
    while (q.next())
        out << QVariantMap{{QStringLiteral("id"), q.value(0)},
                           {QStringLiteral("title"), q.value(1)},
                           {QStringLiteral("startedAt"), q.value(2)},
                           {QStringLiteral("endedAt"), q.value(3)},
                           {QStringLiteral("active"), q.value(3).isNull()}};
    return out;
}
QVariantMap RadioNet::session() const
{
    if (!m_db || !m_db->isOpen() || m_sessionId <= 0)
        return {};
    QSqlQuery q(m_db->connection());
    q.prepare(QStringLiteral(
        "SELECT id,title,started_at,ended_at,current_member_id,station_profile_id FROM radio_net_session WHERE id=?"));
    q.addBindValue(m_sessionId);
    if (!q.exec() || !q.next())
        return {};
    return {{QStringLiteral("id"), q.value(0)},
            {QStringLiteral("title"), q.value(1)},
            {QStringLiteral("startedAt"), q.value(2)},
            {QStringLiteral("endedAt"), q.value(3)},
            {QStringLiteral("currentMemberId"), q.value(4)},
            {QStringLiteral("stationProfileId"), q.value(5)},
            {QStringLiteral("active"), q.value(3).isNull()}};
}
bool RadioNet::active() const { return session().value(QStringLiteral("active")).toBool(); }
QVariantList RadioNet::members() const
{
    QVariantList out;
    if (!m_db || !m_db->isOpen() || m_sessionId <= 0)
        return out;
    QSqlQuery q(m_db->connection());
    q.prepare(QStringLiteral("SELECT id,call,name,notes,status,sort_order,first_seen,last_seen FROM radio_net_member "
                             "WHERE session_id=? ORDER BY sort_order,id"));
    q.addBindValue(m_sessionId);
    if (!q.exec())
        return out;
    while (q.next())
        out << QVariantMap{{QStringLiteral("id"), q.value(0)},        {QStringLiteral("call"), q.value(1)},
                           {QStringLiteral("name"), q.value(2)},      {QStringLiteral("notes"), q.value(3)},
                           {QStringLiteral("status"), q.value(4)},    {QStringLiteral("order"), q.value(5)},
                           {QStringLiteral("firstSeen"), q.value(6)}, {QStringLiteral("lastSeen"), q.value(7)}};
    return out;
}
QVariantList RadioNet::events() const
{
    QVariantList out;
    if (!m_db || !m_db->isOpen() || m_sessionId <= 0)
        return out;
    QSqlQuery q(m_db->connection());
    q.prepare(
        QStringLiteral("SELECT e.id,e.event,e.recorded_at,e.detail,IFNULL(m.call,'') FROM radio_net_event e LEFT JOIN "
                       "radio_net_member m ON m.id=e.member_id WHERE e.session_id=? ORDER BY e.id DESC LIMIT 200"));
    q.addBindValue(m_sessionId);
    if (!q.exec())
        return out;
    while (q.next())
        out << QVariantMap{{QStringLiteral("id"), q.value(0)},
                           {QStringLiteral("event"), q.value(1)},
                           {QStringLiteral("at"), q.value(2)},
                           {QStringLiteral("detail"), q.value(3)},
                           {QStringLiteral("call"), q.value(4)}};
    return out;
}
bool RadioNet::appendEvent(qint64 memberId, const QString &kind, const QString &detail)
{
    QSqlQuery q(m_db->connection());
    q.prepare(
        QStringLiteral("INSERT INTO radio_net_event(session_id,member_id,event,recorded_at,detail) VALUES(?,?,?,?,?)"));
    q.addBindValue(m_sessionId);
    q.addBindValue(memberId > 0 ? QVariant(memberId) : QVariant());
    q.addBindValue(kind);
    q.addBindValue(nowUtc());
    q.addBindValue(detail.isNull() ? QStringLiteral("") : detail);
    return q.exec();
}
bool RadioNet::selectSession(qint64 id)
{
    if (!m_db || !m_db->isOpen())
        return fail(tr("Log is not open"));
    QSqlQuery q(m_db->connection());
    q.prepare(QStringLiteral("SELECT 1 FROM radio_net_session WHERE id=?"));
    q.addBindValue(id);
    if (!q.exec() || !q.next())
        return fail(tr("Radio net session not found"));
    m_sessionId = id;
    m_error.clear();
    emit changed();
    return true;
}
bool RadioNet::start(const QString &title, qint64 stationProfileId)
{
    if (!m_db || !m_db->isOpen())
        return fail(tr("Log is not open"));
    const QString label = title.trimmed();
    if (label.isEmpty() || label.size() > 160)
        return fail(tr("Enter a session name (up to 160 characters)"));
    QSqlDatabase db = m_db->connection();
    if (!db.transaction())
        return fail(db.lastError().text());
    QSqlQuery q(db);
    q.prepare(
        QStringLiteral("INSERT INTO radio_net_session(uuid,title,station_profile_id,started_at) VALUES(?,?,?,?)"));
    q.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
    q.addBindValue(label);
    q.addBindValue(stationProfileId > 0 ? QVariant(stationProfileId) : QVariant());
    q.addBindValue(nowUtc());
    if (!q.exec()) {
        const QString err = q.lastError().text();
        db.rollback();
        return fail(err);
    }
    const qint64 previous = m_sessionId;
    m_sessionId = q.lastInsertId().toLongLong();
    if (!appendEvent(0, QStringLiteral("start")) || !db.commit()) {
        const QString err = db.lastError().text();
        db.rollback();
        m_sessionId = previous;
        return fail(err.isEmpty() ? tr("Cannot save session") : err);
    }
    m_error.clear();
    emit changed();
    return true;
}
bool RadioNet::checkIn(const QString &rawCall, const QString &name, const QString &notes)
{
    if (!active())
        return fail(tr("Open an active radio net first"));
    const QString call = rawCall.trimmed().toUpper();
    static const QRegularExpression valid(QStringLiteral("^[A-Z0-9/]{2,32}$"));
    if (!valid.match(call).hasMatch())
        return fail(tr("Use 2–32 letters, digits or slashes for the callsign"));
    if (name.size() > 160 || notes.size() > 4000)
        return fail(tr("Name or notes are too long"));
    QSqlDatabase db = m_db->connection();
    if (!db.transaction())
        return fail(db.lastError().text());
    if (!active()) {
        db.rollback();
        return fail(tr("Open an active radio net first"));
    }
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO radio_net_member(session_id,call,name,notes,status,sort_order,first_seen,last_seen) "
        "VALUES(?,?,?,?,'present',(SELECT COALESCE(MAX(sort_order),0)+1 FROM radio_net_member WHERE session_id=?),?,?) "
        "ON CONFLICT(session_id,call) DO UPDATE SET status='present',name=CASE WHEN excluded.name='' THEN name ELSE "
        "excluded.name END,notes=CASE WHEN excluded.notes='' THEN notes ELSE excluded.notes "
        "END,last_seen=excluded.last_seen"));
    q.addBindValue(m_sessionId);
    q.addBindValue(call);
    q.addBindValue(name.trimmed().isEmpty() ? QStringLiteral("") : name.trimmed());
    q.addBindValue(notes.trimmed().isEmpty() ? QStringLiteral("") : notes.trimmed());
    q.addBindValue(m_sessionId);
    const QString timestamp = nowUtc();
    q.addBindValue(timestamp);
    q.addBindValue(timestamp);
    if (!q.exec()) {
        const QString err = q.lastError().text();
        db.rollback();
        return fail(err);
    }
    q.prepare(QStringLiteral("SELECT id,name,notes FROM radio_net_member WHERE session_id=? AND call=?"));
    q.addBindValue(m_sessionId);
    q.addBindValue(call);
    if (!q.exec() || !q.next()) {
        const QString err = q.lastError().text();
        db.rollback();
        return fail(err);
    }
    const qint64 id = q.value(0).toLongLong();
    const QString detail =
        QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("name"), q.value(1).toString()},
                                                    {QStringLiteral("notes"), q.value(2).toString()}})
                              .toJson(QJsonDocument::Compact));
    q.finish();
    if (!appendEvent(id, QStringLiteral("check_in"), detail) || !db.commit()) {
        const QString err = db.lastError().text();
        db.rollback();
        return fail(err.isEmpty() ? tr("Cannot save check-in") : err);
    }
    m_error.clear();
    emit changed();
    return true;
}
bool RadioNet::setStatus(qint64 memberId, const QString &status)
{
    if (!active())
        return fail(tr("Open an active radio net first"));
    if (!QStringList{QStringLiteral("present"), QStringLiteral("waiting"), QStringLiteral("absent"),
                     QStringLiteral("departed")}
             .contains(status))
        return fail(tr("Invalid participant status"));
    QSqlDatabase db = m_db->connection();
    if (!db.transaction())
        return fail(db.lastError().text());
    if (!active()) {
        db.rollback();
        return fail(tr("Open an active radio net first"));
    }
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE radio_net_member SET status=?,last_seen=? WHERE id=? AND session_id=?"));
    q.addBindValue(status);
    q.addBindValue(nowUtc());
    q.addBindValue(memberId);
    q.addBindValue(m_sessionId);
    if (!q.exec() || q.numRowsAffected() != 1) {
        const QString err = q.lastError().text();
        db.rollback();
        return fail(err.isEmpty() ? tr("Participant not found") : err);
    }
    if (!appendEvent(memberId, QStringLiteral("status"), status) || !db.commit()) {
        const QString err = db.lastError().text();
        db.rollback();
        return fail(err.isEmpty() ? tr("Cannot save status") : err);
    }
    m_error.clear();
    emit changed();
    return true;
}
bool RadioNet::nextTurn()
{
    if (!active())
        return fail(tr("Open an active radio net first"));
    QSqlDatabase db = m_db->connection();
    if (!db.transaction())
        return fail(db.lastError().text());
    if (!active()) {
        db.rollback();
        return fail(tr("Open an active radio net first"));
    }
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT id FROM radio_net_member WHERE session_id=? AND status IN ('present','waiting') ORDER BY CASE WHEN "
        "sort_order > COALESCE((SELECT sort_order FROM radio_net_member WHERE id=(SELECT current_member_id FROM "
        "radio_net_session WHERE id=?)),-1) THEN 0 ELSE 1 END,sort_order,id LIMIT 1"));
    q.addBindValue(m_sessionId);
    q.addBindValue(m_sessionId);
    if (!q.exec() || !q.next()) {
        db.rollback();
        return fail(tr("No available participants"));
    }
    const qint64 id = q.value(0).toLongLong();
    q.finish();
    q.prepare(QStringLiteral("UPDATE radio_net_session SET current_member_id=? WHERE id=?"));
    q.addBindValue(id);
    q.addBindValue(m_sessionId);
    if (!q.exec() || !appendEvent(id, QStringLiteral("turn")) || !db.commit()) {
        const QString err = db.lastError().text();
        db.rollback();
        return fail(err.isEmpty() ? tr("Cannot save turn") : err);
    }
    m_error.clear();
    emit changed();
    return true;
}
bool RadioNet::moveMember(qint64 memberId, int direction)
{
    if (!active())
        return fail(tr("Open an active radio net first"));
    if (direction != -1 && direction != 1)
        return fail(tr("Invalid movement"));
    QSqlDatabase db = m_db->connection();
    if (!db.transaction())
        return fail(db.lastError().text());
    if (!active()) {
        db.rollback();
        return fail(tr("Open an active radio net first"));
    }
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT sort_order FROM radio_net_member WHERE id=? AND session_id=?"));
    q.addBindValue(memberId);
    q.addBindValue(m_sessionId);
    if (!q.exec() || !q.next()) {
        db.rollback();
        return fail(tr("Participant not found"));
    }
    const int order = q.value(0).toInt();
    q.finish();
    const QString comparator = direction < 0 ? QStringLiteral("<") : QStringLiteral(">");
    const QString sorting = direction < 0 ? QStringLiteral("DESC") : QStringLiteral("ASC");
    q.prepare(QStringLiteral("SELECT id,sort_order FROM radio_net_member WHERE session_id=? AND sort_order %1 ? ORDER "
                             "BY sort_order %2,id %2 LIMIT 1")
                  .arg(comparator, sorting));
    q.addBindValue(m_sessionId);
    q.addBindValue(order);
    if (!q.exec() || !q.next()) {
        db.rollback();
        return fail(tr("Participant is already at the edge"));
    }
    const qint64 otherId = q.value(0).toLongLong();
    const int otherOrder = q.value(1).toInt();
    q.finish();
    q.prepare(QStringLiteral("UPDATE radio_net_member SET sort_order=CASE WHEN id=? THEN ? WHEN id=? THEN ? END WHERE "
                             "session_id=? AND id IN (?,?)"));
    q.addBindValue(memberId);
    q.addBindValue(otherOrder);
    q.addBindValue(otherId);
    q.addBindValue(order);
    q.addBindValue(m_sessionId);
    q.addBindValue(memberId);
    q.addBindValue(otherId);
    if (!q.exec() || q.numRowsAffected() != 2 ||
        !appendEvent(memberId, QStringLiteral("reorder"), QString::number(direction)) || !db.commit()) {
        const QString err = db.lastError().text();
        db.rollback();
        return fail(err.isEmpty() ? tr("Cannot reorder participant") : err);
    }
    m_error.clear();
    emit changed();
    return true;
}
bool RadioNet::closeSession()
{
    if (!active())
        return fail(tr("Open an active radio net first"));
    QSqlDatabase db = m_db->connection();
    if (!db.transaction())
        return fail(db.lastError().text());
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE radio_net_session SET ended_at=? WHERE id=? AND ended_at IS NULL"));
    q.addBindValue(nowUtc());
    q.addBindValue(m_sessionId);
    if (!q.exec() || q.numRowsAffected() != 1 || !appendEvent(0, QStringLiteral("close")) || !db.commit()) {
        const QString err = db.lastError().text();
        db.rollback();
        return fail(err.isEmpty() ? tr("Cannot close session") : err);
    }
    m_error.clear();
    emit changed();
    return true;
}
bool RadioNet::reopenSession()
{
    if (!m_db || !m_db->isOpen() || m_sessionId <= 0 || active())
        return fail(tr("Select a closed session"));
    QSqlDatabase db = m_db->connection();
    if (!db.transaction())
        return fail(db.lastError().text());
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE radio_net_session SET ended_at=NULL WHERE id=? AND ended_at IS NOT NULL"));
    q.addBindValue(m_sessionId);
    if (!q.exec() || q.numRowsAffected() != 1 || !appendEvent(0, QStringLiteral("reopen")) || !db.commit()) {
        const QString err = db.lastError().text();
        db.rollback();
        return fail(err.isEmpty() ? tr("Cannot reopen session") : err);
    }
    m_error.clear();
    emit changed();
    return true;
}
} // namespace decolog::core
