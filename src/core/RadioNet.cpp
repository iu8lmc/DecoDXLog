#include "core/RadioNet.h"
#include "core/LogDatabase.h"
#include "core/FileIdentity.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>
#include <limits>

namespace decolog::core {
namespace {
QString nowUtc() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs); }
bool beginNetWrite(QSqlDatabase db,QString *error)
{
    // Acquire the SQLite write reservation before reading session state. A
    // deferred read transaction can otherwise fail to upgrade after a
    // concurrent commit, even when busy_timeout is configured.
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("BEGIN IMMEDIATE"))) { if (error) *error=query.lastError().text(); return false; }
    return true;
}
bool safeNetExportDestination(const QString &destination, const QString &source)
{
    if (destination.isEmpty()) return false;
    const auto resolved = [](const QString &path) {
        const QFileInfo info(path);
        if (info.exists()) return info.canonicalFilePath();
        const QString parent = QFileInfo(info.absolutePath()).canonicalFilePath();
        return QDir(parent.isEmpty() ? info.absolutePath() : parent).filePath(info.fileName());
    };
    const QString target = resolved(destination);
    const QByteArray targetIdentity = fileidentity::read(destination);
    for (const QString &suffix : {QString(), QStringLiteral("-wal"), QStringLiteral("-shm"), QStringLiteral("-journal")}) {
        const QString protectedPath = source + suffix;
        const QByteArray protectedIdentity = fileidentity::read(protectedPath);
        if (target == resolved(protectedPath) || (!targetIdentity.isEmpty() && targetIdentity == protectedIdentity)) return false;
    }
    return true;
}
} // namespace
RadioNet::RadioNet(LogDatabase *db, QObject *parent) : QObject(parent), m_db(db) {}
RadioNet::~RadioNet() { m_exportPool.waitForDone(); }

bool RadioNet::fail(const QString &message)
{
    m_error = message;
    emit changed();
    return false;
}
void RadioNet::bindSession(qint64 id,const QString &uuid)
{
    m_sessionId=id; m_sessionUuid=uuid;
    m_sessionGeneration=m_db->openGeneration(); m_sessionLogPath=m_db->path(); m_sessionLogIdentity=m_db->identity();
    m_sessionFileIdentity=m_db->openFileIdentity();
}
bool RadioNet::selectedSessionCurrent() const
{
    if (!m_db || !m_db->isOpen() || m_sessionId<=0 || m_sessionUuid.isEmpty() || m_sessionLogIdentity.isEmpty() ||
        m_db->openGeneration()!=m_sessionGeneration || m_db->path()!=m_sessionLogPath || m_db->identity()!=m_sessionLogIdentity ||
        m_db->openFileIdentity()!=m_sessionFileIdentity || !m_db->fileIdentityMatchesOpen()) return false;
    QSqlQuery q(m_db->connection()); q.prepare(QStringLiteral("SELECT 1 FROM radio_net_session WHERE id=? AND uuid=?"));
    q.addBindValue(m_sessionId); q.addBindValue(m_sessionUuid); return q.exec() && q.next();
}
void RadioNet::load()
{
    m_sessionId = 0;
    m_sessionUuid.clear();
    if (m_db && m_db->isOpen() && m_db->fileIdentityMatchesOpen()) {
        QSqlQuery q(m_db->connection());
        if (q.exec(
                QStringLiteral("SELECT id,uuid FROM radio_net_session ORDER BY (ended_at IS NULL) DESC,id DESC LIMIT 1")) &&
            q.next())
            bindSession(q.value(0).toLongLong(),q.value(1).toString());
    }
    m_error.clear();
    emit changed();
}
QVariantList RadioNet::sessions() const
{
    QVariantList out;
    if (!m_db || !m_db->isOpen() || !m_db->fileIdentityMatchesOpen())
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
    if (!selectedSessionCurrent())
        return {};
    QSqlQuery q(m_db->connection());
    q.prepare(QStringLiteral(
        "SELECT id,title,started_at,ended_at,current_member_id,station_profile_id,uuid FROM radio_net_session WHERE id=?"));
    q.addBindValue(m_sessionId);
    if (!q.exec() || !q.next())
        return {};
    return {{QStringLiteral("id"), q.value(0)},
            {QStringLiteral("title"), q.value(1)},
            {QStringLiteral("startedAt"), q.value(2)},
            {QStringLiteral("endedAt"), q.value(3)},
            {QStringLiteral("currentMemberId"), q.value(4)},
            {QStringLiteral("stationProfileId"), q.value(5)},
            {QStringLiteral("uuid"), q.value(6)},
            {QStringLiteral("active"), q.value(3).isNull()}};
}
bool RadioNet::active() const { return session().value(QStringLiteral("active")).toBool(); }
QVariantMap RadioNet::reminder() const
{
    if (!selectedSessionCurrent())
        return {};
    QSqlQuery q(m_db->connection());
    q.prepare(QStringLiteral("SELECT event,detail FROM radio_net_event WHERE session_id=? AND "
                             "event IN ('reminder_set','reminder_clear') ORDER BY id DESC LIMIT 1"));
    q.addBindValue(m_sessionId);
    if (!q.exec() || !q.next() || q.value(0).toString() != QLatin1String("reminder_set"))
        return {{QStringLiteral("scheduled"), false}};
    const QString deadline = q.value(1).toString();
    const QDateTime due = QDateTime::fromString(deadline, Qt::ISODateWithMs);
    if (!due.isValid())
        return {{QStringLiteral("scheduled"), false}};
    return {{QStringLiteral("scheduled"), true},
            {QStringLiteral("deadline"), deadline},
            {QStringLiteral("due"), due <= QDateTime::currentDateTimeUtc()}};
}

QVariantList RadioNet::activeReminders() const
{
    return reminderOverview().value(QStringLiteral("reminders")).toList();
}

QVariantMap RadioNet::reminderOverview() const
{
    QVariantList result;
    const auto overview = [](const QVariantList &rows, const QString &error) {
        return QVariantMap{{QStringLiteral("reminders"), rows}, {QStringLiteral("error"), error}};
    };
    if (!m_db || !m_db->isOpen()) return overview(result, {});
    if (!m_db->fileIdentityMatchesOpen()) return overview({},tr("The log file changed; reopen it"));
    QSqlQuery q(m_db->connection());
    if (!q.exec(QStringLiteral(
            "SELECT s.id,s.title,e.detail FROM radio_net_session s JOIN radio_net_event e ON e.id=("
            "SELECT id FROM radio_net_event WHERE session_id=s.id AND event IN ('reminder_set','reminder_clear') "
            "ORDER BY id DESC LIMIT 1) WHERE s.ended_at IS NULL AND e.event='reminder_set'")))
        return overview({}, q.lastError().text());
    while (q.next()) {
        const QString deadline = q.value(2).toString();
        if (!QDateTime::fromString(deadline, Qt::ISODateWithMs).isValid())
            return overview({}, tr("Invalid radio net reminder deadline"));
        result << QVariantMap{{QStringLiteral("sessionId"), q.value(0)},
                              {QStringLiteral("title"), q.value(1)},
                              {QStringLiteral("deadline"), deadline}};
    }
    if (q.lastError().isValid()) return overview({}, q.lastError().text());
    return overview(result, {});
}

bool RadioNet::setReminder(int seconds)
{
    if (!m_db || !m_db->isOpen() || seconds < 1 || seconds > 86400)
        return fail(tr("Enter a reminder duration from 1 to 86400 seconds"));
    QSqlDatabase db = m_db->connection();
    QString transactionError;
    if (!beginNetWrite(db,&transactionError))
        return fail(transactionError);
    if (!active()) {
        db.rollback();
        return fail(tr("Open an active radio net first"));
    }
    const QString deadline = QDateTime::currentDateTimeUtc().addSecs(seconds).toString(Qt::ISODateWithMs);
    if (!appendEvent(0, QStringLiteral("reminder_set"), deadline) || !db.commit()) {
        const QString error = db.lastError().text();
        db.rollback();
        return fail(error.isEmpty() ? tr("Cannot save session") : error);
    }
    m_error.clear();
    emit changed();
    return true;
}

bool RadioNet::clearReminder()
{
    if (!selectedSessionCurrent())
        return fail(tr("Radio net session not found"));
    QSqlDatabase db = m_db->connection();
    QString transactionError;
    if (!beginNetWrite(db,&transactionError))
        return fail(transactionError);
    if (!selectedSessionCurrent()) { db.rollback(); return fail(tr("Radio net session changed; select it again")); }
    if (!appendEvent(0, QStringLiteral("reminder_clear")) || !db.commit()) {
        const QString error = db.lastError().text();
        db.rollback();
        return fail(error.isEmpty() ? tr("Cannot save session") : error);
    }
    m_error.clear();
    emit changed();
    return true;
}
QVariantMap RadioNet::summary() const
{
    if (!selectedSessionCurrent())
        return {};
    QSqlQuery q(m_db->connection());
    q.prepare(QStringLiteral("SELECT COUNT(*),COALESCE(SUM(status='present'),0),COALESCE(SUM(status='waiting'),0),"
                             "COALESCE(SUM(status='absent'),0),COALESCE(SUM(status='departed'),0),"
                             "(SELECT COUNT(*) FROM radio_net_event WHERE session_id=? AND event='check_in') "
                             "FROM radio_net_member WHERE session_id=?"));
    q.addBindValue(m_sessionId);
    q.addBindValue(m_sessionId);
    if (!q.exec() || !q.next())
        return {};
    return {{QStringLiteral("participants"), q.value(0)}, {QStringLiteral("present"), q.value(1)},
            {QStringLiteral("waiting"), q.value(2)},      {QStringLiteral("absent"), q.value(3)},
            {QStringLiteral("departed"), q.value(4)},     {QStringLiteral("checkIns"), q.value(5)}};
}
QVariantList RadioNet::members() const
{
    QVariantList out;
    if (!selectedSessionCurrent())
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
    QVariantList result = historyPage(0);
    if (result.size() > 200)
        result.removeLast();
    return result;
}
QVariantList RadioNet::historyPage(qint64 beforeId) const
{
    QVariantList out;
    if (!selectedSessionCurrent())
        return out;
    QSqlQuery q(m_db->connection());
    q.prepare(QStringLiteral(
        "SELECT e.id,e.event,e.recorded_at,e.detail,IFNULL(m.call,'') FROM radio_net_event e LEFT JOIN "
        "radio_net_member m ON m.id=e.member_id WHERE e.session_id=? AND e.id<? ORDER BY e.id DESC LIMIT 201"));
    q.addBindValue(m_sessionId);
    q.addBindValue(beforeId > 0 ? beforeId : std::numeric_limits<qint64>::max());
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
QVariantList RadioNet::reportedQsoPage(qint64 beforeId) const
{
    QVariantList out;
    if (!selectedSessionCurrent())
        return out;
    QSqlQuery q(m_db->connection());
    q.prepare(QStringLiteral("SELECT q.id,q.uuid,q.call,q.qso_datetime_on,q.band,q.mode,q.source,"
                             "CASE WHEN json_valid(q.adif_extra) THEN json_extract(q.adif_extra,'$.APP_DECOLOG_RADIO_NET_CALL') END "
                             "FROM qso q WHERE q.deleted=0 AND "
                             "(CASE WHEN json_valid(q.adif_extra) THEN json_extract(q.adif_extra,'$.APP_DECOLOG_RADIO_NET_SESSION') END) IS NOT NULL AND "
                             "(CASE WHEN json_valid(q.adif_extra) THEN json_extract(q.adif_extra,'$.APP_DECOLOG_RADIO_NET_SESSION') END)="
                             "(SELECT uuid FROM radio_net_session WHERE id=?) AND q.id<? ORDER BY q.id DESC LIMIT 201"));
    q.addBindValue(m_sessionId);
    q.addBindValue(beforeId > 0 ? beforeId : std::numeric_limits<qint64>::max());
    if (!q.exec())
        return out;
    while (q.next())
        out << QVariantMap{{QStringLiteral("id"), q.value(0)}, {QStringLiteral("uuid"), q.value(1)},
                           {QStringLiteral("call"), q.value(2)}, {QStringLiteral("at"), q.value(3)},
                           {QStringLiteral("band"), q.value(4)}, {QStringLiteral("mode"), q.value(5)},
                           {QStringLiteral("source"), q.value(6)}, {QStringLiteral("reportedCall"), q.value(7)}};
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
    if (!m_db || !m_db->isOpen() || !m_db->fileIdentityMatchesOpen())
        return fail(tr("Log is not open"));
    QSqlQuery q(m_db->connection());
    q.prepare(QStringLiteral("SELECT uuid FROM radio_net_session WHERE id=?"));
    q.addBindValue(id);
    if (!q.exec() || !q.next())
        return fail(tr("Radio net session not found"));
    const QString uuid=q.value(0).toString();
    if (uuid.isEmpty()) return fail(tr("Radio net session not found"));
    bindSession(id,uuid);
    m_error.clear();
    emit changed();
    return true;
}
bool RadioNet::start(const QString &title, qint64 stationProfileId)
{
    if (!m_db || !m_db->isOpen() || !m_db->fileIdentityMatchesOpen())
        return fail(tr("Log is not open"));
    const QString label = title.trimmed();
    if (label.isEmpty() || label.size() > 160)
        return fail(tr("Enter a session name (up to 160 characters)"));
    QSqlDatabase db = m_db->connection();
    QString transactionError;
    if (!beginNetWrite(db,&transactionError))
        return fail(transactionError);
    QSqlQuery q(db);
    q.prepare(
        QStringLiteral("INSERT INTO radio_net_session(uuid,title,station_profile_id,started_at) VALUES(?,?,?,?)"));
    const QString uuid=QUuid::createUuid().toString(QUuid::WithoutBraces);
    q.addBindValue(uuid);
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
    bindSession(m_sessionId,uuid);
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
    QString transactionError;
    if (!beginNetWrite(db,&transactionError))
        return fail(transactionError);
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
bool RadioNet::editParticipant(qint64 memberId, const QString &sessionUuid, const QString &expectedName,
                               const QString &expectedNotes, const QString &name, const QString &notes)
{
    if (memberId<=0 || sessionUuid.isEmpty() || name.size()>160 || notes.size()>4000)
        return fail(tr("Name or notes are too long"));
    if (!active() || m_sessionUuid!=sessionUuid) return fail(tr("Radio net session changed; select it again"));
    QSqlDatabase db=m_db->connection(); QString transactionError;
    if (!beginNetWrite(db,&transactionError)) return fail(transactionError);
    if (!active() || m_sessionUuid!=sessionUuid) { db.rollback(); return fail(tr("Radio net session changed; select it again")); }
    const QString newName=name.trimmed().isEmpty() ? QStringLiteral("") : name.trimmed();
    const QString newNotes=notes.trimmed().isEmpty() ? QStringLiteral("") : notes.trimmed();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE radio_net_member SET name=?,notes=? WHERE id=? AND session_id=? AND name=? AND notes=?"));
    q.addBindValue(newName); q.addBindValue(newNotes); q.addBindValue(memberId); q.addBindValue(m_sessionId);
    q.addBindValue(expectedName); q.addBindValue(expectedNotes);
    if (!q.exec() || q.numRowsAffected()!=1) { db.rollback(); return fail(tr("Participant changed; reopen the editor")); }
    const QString detail=QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("oldName"),expectedName},
        {QStringLiteral("oldNotes"),expectedNotes},{QStringLiteral("name"),newName},{QStringLiteral("notes"),newNotes}}).toJson(QJsonDocument::Compact));
    if (!appendEvent(memberId,QStringLiteral("participant_details"),detail) || !db.commit()) {
        const QString error=db.lastError().text(); db.rollback(); return fail(error.isEmpty() ? tr("Cannot save participant") : error);
    }
    m_error.clear(); emit changed(); return true;
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
    QString transactionError;
    if (!beginNetWrite(db,&transactionError))
        return fail(transactionError);
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
    QString transactionError;
    if (!beginNetWrite(db,&transactionError))
        return fail(transactionError);
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
    QString transactionError;
    if (!beginNetWrite(db,&transactionError))
        return fail(transactionError);
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
    QString transactionError;
    if (!beginNetWrite(db,&transactionError))
        return fail(transactionError);
    if (!active()) { db.rollback(); return fail(tr("Open an active radio net first")); }
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
    if (!selectedSessionCurrent() || active())
        return fail(tr("Select a closed session"));
    QSqlDatabase db = m_db->connection();
    QString transactionError;
    if (!beginNetWrite(db,&transactionError))
        return fail(transactionError);
    if (!selectedSessionCurrent() || active()) { db.rollback(); return fail(tr("Select a closed session")); }
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

bool RadioNet::exportSession(const QUrl &file)
{
    if (m_exportBusy || !selectedSessionCurrent() || !file.isLocalFile())
        return fail(tr("Select a session and a local export file"));
    const QString source = m_db->path();
    if (source.isEmpty() || source == QLatin1String(":memory:"))
        return fail(tr("A file-backed log is required for export"));
    const QByteArray sourceFile = m_db->openFileIdentity();
    const QString sourceIdentity = m_db->identity();
    const quint64 sourceGeneration = m_db->openGeneration();
    if (sourceFile.isEmpty() || sourceIdentity.isEmpty() || !m_db->fileIdentityMatchesOpen())
        return fail(tr("The log file changed; reopen it before exporting"));
    QSqlQuery selected(m_db->connection());
    selected.prepare(QStringLiteral("SELECT uuid FROM radio_net_session WHERE id=?"));
    selected.addBindValue(m_sessionId);
    if (!selected.exec() || !selected.next() || selected.value(0).toString().isEmpty())
        return fail(tr("Radio net session not found"));
    const QString sessionUuid = selected.value(0).toString();
    selected.finish();
    const QString destination = file.toLocalFile();
    if (!safeNetExportDestination(destination, source))
        return fail(tr("Choose a file outside the open log database"));
    const qint64 sessionId = m_sessionId;
    m_exportCancel = std::make_shared<std::atomic_bool>(false);
    const auto cancel = m_exportCancel;
    m_exportBusy = true;
    m_error.clear();
    emit changed();
    emit exportChanged();
    QPointer<RadioNet> self(this);
    m_exportPool.start([self, source, sourceFile, sourceIdentity, sourceGeneration, sessionUuid, destination, sessionId, cancel] {
        QString error;
        bool success = false;
        {
            LogDatabase db;
            if (cancel->load()) {
                error = tr("Export cancelled");
            } else if (!db.openReadOnly(source)) {
                error = db.lastError();
            } else if (!db.fileIdentityMatchesOpen() || db.openFileIdentity() != sourceFile || db.identity() != sourceIdentity) {
                error = tr("The log file changed; reopen it before exporting");
            } else {
                QSqlDatabase sql = db.connection();
                if (!sql.transaction()) {
                    error = sql.lastError().text();
                } else {
                    QSaveFile output(destination);
                    output.setDirectWriteFallback(false);
                    if (!safeNetExportDestination(destination, source)) {
                        error = tr("Choose a file outside the open log database");
                    } else if (!output.open(QIODevice::WriteOnly)) {
                        error = output.errorString();
                    } else {
                        const auto write = [&output, &error](const QByteArray &bytes) {
                            if (output.write(bytes) == bytes.size())
                                return true;
                            error = output.errorString();
                            return false;
                        };
                        const auto object = [](const QSqlQuery &query) {
                            QJsonObject row;
                            const QSqlRecord record = query.record();
                            for (int i = 0; i < record.count(); ++i)
                                row.insert(record.fieldName(i), QJsonValue::fromVariant(query.value(i)));
                            return QJsonDocument(row).toJson(QJsonDocument::Compact);
                        };
                        QSqlQuery q(sql);
                        q.prepare(QStringLiteral(
                            "SELECT id,uuid,title,station_profile_id,started_at,ended_at,current_member_id FROM "
                            "radio_net_session WHERE id=? AND uuid=?"));
                        q.addBindValue(sessionId);
                        q.addBindValue(sessionUuid);
                        if (!q.exec() || !q.next()) {
                            error = q.lastError().text();
                            if (error.isEmpty())
                                error = tr("Radio net session not found");
                        } else {
                            const QByteArray sessionJson = object(q);
                            q.finish();
                            bool ok = write(QByteArrayLiteral("{\"format\":\"decodxlog-radio-net-1\",\"session\":")) &&
                                      write(sessionJson);
                            const auto rows = [&](const QByteArray &key, const QString &statement) {
                                if (!write(QByteArrayLiteral(",\"") + key + QByteArrayLiteral("\":[")))
                                    return false;
                                QSqlQuery rowsQuery(sql);
                                rowsQuery.setForwardOnly(true);
                                rowsQuery.prepare(statement);
                                rowsQuery.addBindValue(sessionId);
                                if (!rowsQuery.exec()) {
                                    error = rowsQuery.lastError().text();
                                    return false;
                                }
                                bool first = true;
                                while (rowsQuery.next()) {
                                    if (cancel->load()) {
                                        error = tr("Export cancelled");
                                        return false;
                                    }
                                    if (!first && !write(QByteArrayLiteral(",")))
                                        return false;
                                    if (!write(object(rowsQuery)))
                                        return false;
                                    first = false;
                                }
                                if (rowsQuery.lastError().isValid()) {
                                    error = rowsQuery.lastError().text();
                                    return false;
                                }
                                return write(QByteArrayLiteral("]"));
                            };
                            if (ok)
                                ok = rows(
                                    QByteArrayLiteral("participants"),
                                    QStringLiteral("SELECT id,call,name,notes,status,sort_order,first_seen,last_seen "
                                                   "FROM radio_net_member WHERE session_id=? ORDER BY sort_order,id"));
                            if (ok)
                                ok =
                                    rows(QByteArrayLiteral("history"),
                                         QStringLiteral("SELECT e.id,e.member_id,e.event,e.recorded_at,e.detail,m.call "
                                                        "FROM radio_net_event e LEFT JOIN radio_net_member m ON "
                                                        "m.id=e.member_id WHERE e.session_id=? ORDER BY e.id"));
                            if (ok)
                                ok = rows(QByteArrayLiteral("reported_qsos"),
                                          QStringLiteral("SELECT q.id,q.uuid,q.call,q.qso_datetime_on,q.band,q.mode,q.source,"
                                                         "CASE WHEN json_valid(q.adif_extra) THEN json_extract(q.adif_extra,'$.APP_DECOLOG_RADIO_NET_CALL') END AS reported_member_call "
                                                         "FROM qso q WHERE q.deleted=0 AND "
                             "(CASE WHEN json_valid(q.adif_extra) THEN json_extract(q.adif_extra,'$.APP_DECOLOG_RADIO_NET_SESSION') END) IS NOT NULL AND "
                                                         "(CASE WHEN json_valid(q.adif_extra) THEN json_extract(q.adif_extra,'$.APP_DECOLOG_RADIO_NET_SESSION') END)="
                                                         "(SELECT uuid FROM radio_net_session WHERE id=?) ORDER BY q.id"));
                            if (ok && cancel->load()) {
                                error = tr("Export cancelled");
                                ok = false;
                            }
                            if (ok)
                                ok = write(QByteArrayLiteral("}\n"));
                            if (ok && !sql.commit()) {
                                error = sql.lastError().text();
                                ok = false;
                            }
                            if (ok && (!db.fileIdentityMatchesOpen() || db.openFileIdentity() != sourceFile)) {
                                error = tr("The log file changed; reopen it before exporting");
                                ok = false;
                            }
                            if (ok && !safeNetExportDestination(destination, source)) {
                                error = tr("Choose a file outside the open log database");
                                ok = false;
                            }
                            if (ok && !output.commit()) {
                                error = output.errorString();
                                ok = false;
                            }
                            success = ok;
                        }
                    }
                    if (!success)
                        sql.rollback();
                }
            }
        }
        QMetaObject::invokeMethod(
            self.data(),
            [self, success, error, source, sourceFile, sourceGeneration, sessionId] {
                if (!self)
                    return;
                self->m_exportBusy = false;
                self->m_exportCancel.reset();
                emit self->exportChanged();
                const bool changed = !self->m_db || !self->m_db->isOpen() || self->m_db->path() != source
                    || self->m_db->openGeneration() != sourceGeneration || self->m_db->openFileIdentity() != sourceFile
                    || self->m_sessionId != sessionId;
                const QString message = changed && success
                    ? tr("Export saved from the original log %1; the current log or session changed.").arg(source)
                    : changed ? tr("Export from the original log %1 failed: %2").arg(source, error) : error;
                emit self->exportFinished(success, message);
            },
            Qt::QueuedConnection);
    });
    return true;
}

void RadioNet::cancelExport()
{
    if (m_exportCancel)
        m_exportCancel->store(true);
}
} // namespace decolog::core
