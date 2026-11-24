#include "app/ActivationController.h"
#include "../StartupTrace.h"

#include "core/Awards.h"
#include "core/Cabrillo.h"
#include "core/ContestRules.h"
#include "core/ContestReport.h"
#include "core/ContestComparison.h"
#include "core/Contests.h"
#include "core/LogDatabase.h"
#include "core/Spots.h"
#include "core/SotaCatalogue.h"
#include "core/PotaCatalogue.h"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QDataStream>
#include <QSqlRecord>
#include <QSaveFile>
#include <filesystem>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimeZone>
#include <QUuid>

namespace decolog::app {

namespace {

// Match the indexed JSON expression for tracked sessions. Legacy sessions have
// no recorded UUID and keep their historical start-time selection.
QString activeSessionStartPredicate(const QString& uuid)
{
    // Tracked sessions use their explicit association; legacy sessions have
    // only an approximate start time. Keep one bound parameter in both paths.
    return uuid.isEmpty() ? QStringLiteral("qso_datetime_on >= ?") : QStringLiteral("? IS NOT NULL");
}

QString activeSessionPredicate(const QString& uuid)
{
    if (uuid.isEmpty()) return {};
    return QStringLiteral(" AND (CASE WHEN json_valid(adif_extra) THEN json_extract(adif_extra,'$.APP_DECOLOG_ACTIVATION_UUID') END) = ?"
                          " AND COALESCE(CASE WHEN json_valid(adif_extra) THEN json_extract(adif_extra,'$.APP_DECOLOG_ACTIVATION_REVIEW') END, '') <> 'PERIOD_UNCERTAIN'");
}

QString normalizeReferenceList(QString value, QString* error)
{
    value = value.trimmed().toUpper();
    if (value.isEmpty())
        return {};

    QStringList normalized;
    QSet<QString> seen;
    for (const QString& raw : value.split(QLatin1Char(','))) {
        const QString ref = raw.trimmed();
        if (ref.isEmpty()) {
            if (error)
                *error = QCoreApplication::translate("ActivationController",
                                                      "Activation references cannot contain empty items");
            return {};
        }
        if (seen.contains(ref)) {
            if (error)
                *error = QCoreApplication::translate("ActivationController",
                                                      "Activation references cannot contain duplicates: %1").arg(ref);
            return {};
        }
        seen.insert(ref);
        normalized << ref;
    }
    return normalized.join(QLatin1Char(','));
}

QString normalizeAdditionalPrograms(core::Activation& session)
{
    QVariantMap normalized;
    QString error;
    if (!core::Activation::normalizePrograms(session.programs, &normalized, &error)) return error;
    const QString primary = core::Activation::kindId(session.kind);
    if (normalized.contains(primary) && normalized.value(primary).toString() != session.reference)
        return QCoreApplication::translate("ActivationController", "The additional %1 reference conflicts with the primary reference.").arg(primary.toUpper());
    session.programs = normalized;
    return {};
}

} // namespace

using namespace decolog::core;

namespace {

constexpr const char* kSetting = "activation.session";

} // namespace

ActivationController::ActivationController(Context context, QObject* parent)
    : QObject(parent)
    , m_ctx(std::move(context))
{
    connect(this, &ActivationController::changed, this, &ActivationController::invalidatePreview);
    const QString testRoot = qEnvironmentVariable("DECODXLOG_DATA_ROOT");
    const QString base = testRoot.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) : testRoot;
    const QString cataloguePath = m_ctx.sotaCataloguePath.isEmpty() ? QDir(base).filePath(QStringLiteral("sota-catalogue.sqlite")) : m_ctx.sotaCataloguePath;
    m_sotaCatalogue = new core::SotaCatalogue(cataloguePath, this);
    connect(m_sotaCatalogue, &core::SotaCatalogue::changed, this, &ActivationController::invalidatePreview);
    const QString potaPath = m_ctx.potaCataloguePath.isEmpty() ? QDir(base).filePath(QStringLiteral("pota-catalogue.sqlite")) : m_ctx.potaCataloguePath;
    m_potaCatalogue = new core::PotaCatalogue(potaPath, this);
    connect(m_potaCatalogue, &core::PotaCatalogue::changed, this, &ActivationController::invalidatePreview);
}

QObject* ActivationController::sotaCatalogue() const { return m_sotaCatalogue; }
QObject* ActivationController::potaCatalogue() const { return m_potaCatalogue; }

ActivationController::~ActivationController()
{
    if (m_previewCancel) m_previewCancel->store(true);
    if (m_partitionExportCancel) m_partitionExportCancel->store(true);
    m_previewPool.waitForDone();
    m_contestReportPool.waitForDone();
    m_partitionExportPool.waitForDone();
}

void ActivationController::invalidatePreview()
{
    ++m_previewGeneration;
    m_previewSessionUuid.clear();
    if (m_previewCancel) m_previewCancel->store(true);
    if (m_partitionExportCancel) m_partitionExportCancel->store(true);
    if (!m_previewResult.isEmpty()) { m_previewResult.clear(); emit previewChanged(); }
}

void ActivationController::invalidateScore()
{
    m_score.valid = false;
    invalidatePreview();
}

void ActivationController::cancelPartitionPreview()
{
    if (m_previewCancel) m_previewCancel->store(true);
}


void ActivationController::load()
{
    if (!m_ctx.db || !m_ctx.db->isOpen())
        return;
    const QString json = m_ctx.db->setting(QLatin1String(kSetting));
    if (!json.isEmpty())
        m_session = Activation::fromMap(QJsonDocument::fromJson(json.toUtf8()).object().toVariantMap());
    loadHistoryOfSession();
    if (m_ctx.shareSerials)
        m_ctx.shareSerials(m_session.active && m_session.serialEnabled && m_session.sharedSerial);
    recount();
    emit changed();
}

void ActivationController::save()
{
    if (!m_ctx.db || !m_ctx.db->isOpen())
        return;
    m_ctx.db->setSetting(QLatin1String(kSetting),
                         QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(m_session.toMap()))
                                               .toJson(QJsonDocument::Compact)));
}

bool ActivationController::persistTransition(const Activation& next, const QString& event)
{
    if (!m_ctx.db || !m_ctx.db->isOpen()) return false;
    auto db = m_ctx.db->connection();
    if (!db.transaction()) return false;
    const auto json = [](const Activation& a) {
        return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(a.toMap())).toJson(QJsonDocument::Compact));
    };
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    const auto record = [&](const Activation& a, const QString& action) {
        if (a.sessionUuid.isEmpty()) return true;
        QSqlQuery q(db);
        if (action == QLatin1String("start")) {
            q.prepare(QStringLiteral("INSERT INTO activation_run(uuid,started_at,initial_json,last_json) VALUES(?,?,?,?)"));
            q.addBindValue(a.sessionUuid); q.addBindValue(a.startedAt.toString(Qt::ISODateWithMs));
            q.addBindValue(json(a)); q.addBindValue(json(a));
        } else {
            q.prepare(action == QLatin1String("stop")
                ? QStringLiteral("UPDATE activation_run SET ended_at=?,last_json=? WHERE uuid=? AND ended_at IS NULL")
                : QStringLiteral("UPDATE activation_run SET last_json=? WHERE uuid=? AND ended_at IS NULL"));
            if (action == QLatin1String("stop")) q.addBindValue(now);
            q.addBindValue(json(a)); q.addBindValue(a.sessionUuid);
        }
        if (!q.exec() || q.numRowsAffected() != 1) return false;
        QSqlQuery change(db);
        change.prepare(QStringLiteral("INSERT INTO activation_run_change(session_uuid,changed_at,event,state_json) VALUES(?,?,?,?)"));
        change.addBindValue(a.sessionUuid); change.addBindValue(now); change.addBindValue(action); change.addBindValue(json(a));
        return change.exec();
    };
    bool ok = true;
    if ((event == QLatin1String("start") && m_session.active) || event == QLatin1String("stop"))
        ok = record(m_session, QStringLiteral("stop"));
    if (ok && event != QLatin1String("stop")) ok = record(next, event);
    QSqlQuery setting(db);
    if (ok) {
        setting.prepare(QStringLiteral("INSERT INTO app_setting(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value"));
        setting.addBindValue(QLatin1String(kSetting)); setting.addBindValue(json(next));
        ok = setting.exec();
    }
    if (!ok || !db.commit()) { db.rollback(); return false; }
    return true;
}

ActivationController::RecoveryAssignment ActivationController::recoveryAssignment(const AdifRecord& record) const
{
    using Status = RecoveryAssignment::Status;
    const auto uncertain = [] { return RecoveryAssignment{Status::Uncertain, {}}; };
    if (!m_ctx.db || !m_ctx.db->isOpen()) return uncertain();
    QSqlQuery savepoint(m_ctx.db->connection());
    if (!savepoint.exec(QStringLiteral("SAVEPOINT activation_recovery_lookup"))) return uncertain();
    struct ReadScope {
        QSqlDatabase db;
        ~ReadScope() { QSqlQuery release(db); release.exec(QStringLiteral("RELEASE SAVEPOINT activation_recovery_lookup")); }
    } readScope{m_ctx.db->connection()};
    const auto parseAdif = [](const QString& rawDate, const QString& rawTime, QDateTime* at, int* precision) {
        const QString dateText = rawDate.trimmed(), timeText = rawTime.trimmed();
        static const QRegularExpression digits(QStringLiteral("^[0-9]{4}(?:[0-9]{2})?$"));
        if (dateText.size() != 8 || !digits.match(timeText).hasMatch()) return false;
        const QDate date = QDate::fromString(dateText, QStringLiteral("yyyyMMdd"));
        const QTime time = QTime::fromString(timeText, timeText.size() == 4 ? QStringLiteral("HHmm") : QStringLiteral("HHmmss"));
        if (!date.isValid() || !time.isValid()) return false;
        *at = QDateTime(date, time, QTimeZone::UTC); *precision = timeText.size() == 4 ? 60 : 1; return true;
    };
    QDateTime begin, off; int onPrecision = 0, offPrecision = 0;
    if (!parseAdif(record.value(QStringLiteral("QSO_DATE")), record.value(QStringLiteral("TIME_ON")), &begin, &onPrecision)) {
        QSqlQuery anyRun(m_ctx.db->connection());
        if (!anyRun.exec(QStringLiteral("SELECT 1 FROM activation_run LIMIT 1"))) return uncertain();
        return m_session.active || anyRun.next() ? uncertain() : RecoveryAssignment{};
    }
    const QString rawOff = record.value(QStringLiteral("TIME_OFF")).trimmed();
    const QString rawOffDate = record.value(QStringLiteral("QSO_DATE_OFF")).trimmed();
    const bool complete = !rawOff.isEmpty() && parseAdif(rawOffDate.isEmpty() ? record.value(QStringLiteral("QSO_DATE")) : rawOffDate,
                                                        rawOff, &off, &offPrecision);
    QDateTime end = complete ? off.addSecs(offPrecision) : begin.addSecs(onPrecision);
    if (complete && rawOffDate.isEmpty() && end <= begin) end = end.addDays(1);
    if (end <= begin || begin > QDateTime::currentDateTimeUtc()) return uncertain();
    const auto utcTime = [](const QString& text) {
        if (!text.endsWith(QLatin1Char('Z'), Qt::CaseInsensitive)) return QDateTime{};
        return QDateTime::fromString(text, Qt::ISODate).toUTC();
    };
    QSqlQuery runs(m_ctx.db->connection());
    runs.prepare(QStringLiteral("SELECT uuid,started_at,ended_at FROM activation_run "
                                "WHERE substr(started_at,1,10)<=? AND (ended_at IS NULL OR substr(ended_at,1,10)>=?) ORDER BY rowid"));
    runs.addBindValue(end.date().toString(Qt::ISODate)); runs.addBindValue(begin.date().toString(Qt::ISODate));
    if (!runs.exec()) return uncertain();
    struct Run { QString uuid; QDateTime start; QDateTime end; bool closed{false}; };
    QList<Run> overlapping;
    while (runs.next()) {
        Run run{runs.value(0).toString(), utcTime(runs.value(1).toString()), utcTime(runs.value(2).toString()), !runs.value(2).isNull()};
        if (run.uuid.isEmpty() || !run.start.isValid() || (run.closed && (!run.end.isValid() || run.end <= run.start))) return uncertain();
        if (run.start < end && (!run.closed || run.end > begin)) overlapping << run;
    }
    if (runs.lastError().isValid()) return uncertain();
    if (m_session.active && m_session.sessionUuid.isEmpty() && m_session.startedAt.isValid() && m_session.startedAt < end)
        return uncertain();
    if (overlapping.isEmpty()) return {};
    if (overlapping.size() != 1 || !complete) return uncertain();
    const Run run = overlapping.first();
    const QString reported = record.value(QStringLiteral("APP_DECOLOG_ACTIVATION_UUID")).trimmed();
    if (!reported.isEmpty() && reported != run.uuid) return uncertain();
    QSqlQuery changes(m_ctx.db->connection());
    changes.prepare(QStringLiteral("SELECT event,changed_at,state_json FROM activation_run_change WHERE session_uuid=? ORDER BY id"));
    changes.addBindValue(run.uuid);
    if (!changes.exec()) return uncertain();
    struct Event { QString kind; QDateTime at; Activation state; };
    QList<Event> events;
    while (changes.next()) {
        const QString kind = changes.value(0).toString(); const QDateTime at = utcTime(changes.value(1).toString());
        QJsonParseError error; const auto doc = QJsonDocument::fromJson(changes.value(2).toByteArray(), &error);
        if (!at.isValid() || error.error != QJsonParseError::NoError || !doc.isObject()) return uncertain();
        const Activation state = Activation::fromMap(doc.object().toVariantMap());
        if (!state.active || state.sessionUuid != run.uuid || state.kind == Activation::Kind::None ||
            (!events.isEmpty() && at <= events.last().at)) return uncertain();
        if ((events.isEmpty() && kind != QLatin1String("start")) ||
            (!events.isEmpty() && kind != QLatin1String("update") && kind != QLatin1String("stop")) ||
            (!events.isEmpty() && events.last().kind == QLatin1String("stop"))) return uncertain();
        events << Event{kind, at, state};
    }
    if (changes.lastError().isValid() || events.isEmpty() || events.first().at < run.start ||
        (run.closed && (events.last().kind != QLatin1String("stop") || events.last().at != run.end)) ||
        (!run.closed && events.last().kind == QLatin1String("stop"))) return uncertain();
    for (qsizetype i = 0; i < events.size(); ++i) {
        if (events.at(i).kind == QLatin1String("stop")) break;
        const QDateTime until = i + 1 < events.size() ? events.at(i + 1).at : QDateTime::currentDateTimeUtc();
        if (begin >= events.at(i).at && end <= until) return {Status::Resolved, events.at(i).state};
    }
    return uncertain();
}

QStringList ActivationController::kinds() const
{
    return {QStringLiteral("pota"), QStringLiteral("sota"), QStringLiteral("wwff"),
            QStringLiteral("iota"), QStringLiteral("contest"), QStringLiteral("free")};
}

QVariantMap ActivationController::state() const
{
    QVariantMap map = m_session.toMap();
    map.insert(QStringLiteral("qsoCount"), m_qsoCount);
    map.insert(QStringLiteral("uniqueCalls"), m_uniqueCalls);
    map.insert(QStringLiteral("perBand"), m_perBand);
    map.insert(QStringLiteral("lastQso"), m_lastQso);
    map.insert(QStringLiteral("elapsed"), elapsed());
    map.insert(QStringLiteral("fileName"), suggestedFileName());
    map.insert(QStringLiteral("callHistoryName"), QFileInfo(m_session.callHistory).fileName());
    map.insert(QStringLiteral("callHistoryCount"), m_history.size());
    return map;
}

QString ActivationController::elapsed() const
{
    if (!m_session.active || !m_session.startedAt.isValid())
        return {};
    const qint64 minutes = m_session.startedAt.secsTo(QDateTime::currentDateTimeUtc()) / 60;
    return QStringLiteral("%1:%2").arg(minutes / 60).arg(minutes % 60, 2, 10, QLatin1Char('0'));
}

QString ActivationController::start(const QVariantMap& map)
{
    if (map.contains(QStringLiteral("programs")) && map.value(QStringLiteral("programs")).metaType().id() != QMetaType::QVariantMap)
        return tr("Additional programs must be a map of references.");
    Activation session = Activation::fromMap(map);
    session.active = true;
    if (session.kind == Activation::Kind::None)
        return tr("Choose what kind of session it is");
    const bool needsReference = session.kind == Activation::Kind::Pota || session.kind == Activation::Kind::Sota
        || session.kind == Activation::Kind::Wwff || session.kind == Activation::Kind::Iota;
    if (needsReference && session.reference.isEmpty())
        return tr("A %1 activation needs its reference").arg(Activation::kindId(session.kind).toUpper());
    if (!session.reference.isEmpty()) {
        QString referenceError;
        const QString normalized = normalizeReferenceList(session.reference, &referenceError);
        if (!referenceError.isEmpty())
            return referenceError;
        session.reference = normalized;
    }
    const QString programError = normalizeAdditionalPrograms(session);
    if (!programError.isEmpty()) return programError;
    if (session.kind == Activation::Kind::Contest && session.contestId.isEmpty())
        return tr("A contest needs its name (CONTEST_ID)");

    if (session.myGrid.isEmpty() && m_ctx.stationGrid)
        session.myGrid = m_ctx.stationGrid();
    if (session.stationProfileId <= 0 && m_ctx.activeProfileId)
        session.stationProfileId = m_ctx.activeProfileId();
    if (session.tag.isEmpty())
        session.tag = session.defaultTag();
    session.startedAt = QDateTime::currentDateTimeUtc();
    session.sessionUuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!persistTransition(session, QStringLiteral("start"))) return tr("Cannot save activation session");
    m_session = session;
    loadHistoryOfSession();
    if (m_ctx.shareSerials)
        m_ctx.shareSerials(m_session.serialEnabled && m_session.sharedSerial);
    recount();
    if (m_ctx.activity)
        m_ctx.activity(QStringLiteral("ACT"), tr("Session open: %1 · grid %2").arg(m_session.title(), m_session.myGrid),
                       QStringLiteral("success"));
    emit changed();
    return {};
}

QString ActivationController::update(const QVariantMap& map)
{
    if (!m_session.active)
        return tr("Open a session first");
    if (map.contains(QStringLiteral("programs")) && map.value(QStringLiteral("programs")).metaType().id() != QMetaType::QVariantMap)
        return tr("Additional programs must be a map of references.");
    const QDateTime started = m_session.startedAt;
    Activation session = Activation::fromMap(map);
    if (!map.contains(QStringLiteral("programs"))) session.programs = m_session.programs;
    session.active = true;
    if (session.kind == Activation::Kind::None)
        return tr("Choose what kind of session it is");
    const bool needsReference = session.kind == Activation::Kind::Pota || session.kind == Activation::Kind::Sota
        || session.kind == Activation::Kind::Wwff || session.kind == Activation::Kind::Iota;
    if (needsReference && session.reference.isEmpty())
        return tr("A %1 activation needs its reference").arg(Activation::kindId(session.kind).toUpper());
    if (session.kind == Activation::Kind::Contest && session.contestId.isEmpty())
        return tr("A contest needs its name (CONTEST_ID)");
    if (!session.reference.isEmpty()) {
        QString referenceError;
        const QString normalized = normalizeReferenceList(session.reference, &referenceError);
        if (!referenceError.isEmpty()) {
            if (m_ctx.activity)
                m_ctx.activity(QStringLiteral("ACT"), referenceError, QStringLiteral("error"));
            return referenceError;
        }
        session.reference = normalized;
    }
    const QString programError = normalizeAdditionalPrograms(session);
    if (!programError.isEmpty()) return programError;
    session.startedAt = started.isValid() ? started : QDateTime::currentDateTimeUtc();
    session.sessionUuid = m_session.sessionUuid;
    if (!persistTransition(session, QStringLiteral("update"))) return tr("Cannot save activation session");
    m_session = session;
    loadHistoryOfSession();
    if (m_ctx.shareSerials)
        m_ctx.shareSerials(m_session.serialEnabled && m_session.sharedSerial);
    m_score.valid = false;
    recount();
    emit changed();
    return {};
}

void ActivationController::stop()
{
    if (!m_session.active)
        return;
    if (!persistTransition(Activation{}, QStringLiteral("stop"))) {
        if (m_ctx.activity) m_ctx.activity(QStringLiteral("ACT"), tr("Cannot save activation session"), QStringLiteral("error"));
        return;
    }
    if (m_ctx.activity) {
        m_ctx.activity(QStringLiteral("ACT"),
                       tr("Session closed: %1 · %n QSO", nullptr, m_qsoCount).arg(m_session.title()),
                       m_qsoCount >= m_session.requiredQsos() ? QStringLiteral("success") : QStringLiteral("warning"));
    }
    m_session = Activation{};
    m_history.clear();
    if (m_ctx.shareSerials)
        m_ctx.shareSerials(false);
    recount();
    emit changed();
}

void ActivationController::refresh()
{
    recount();
    emit changed();
}

void ActivationController::recount()
{
    m_score.valid = false;
    m_qsoCount = 0;
    m_uncertainCount = 0;
    m_uniqueCalls = 0;
    m_perBand.clear();
    m_lastQso.clear();
    if (!m_session.active || !m_ctx.db || !m_ctx.db->isOpen() || !m_session.startedAt.isValid())
        return;

    if (!m_session.sessionUuid.isEmpty()) {
        QSqlQuery uncertain(m_ctx.db->connection());
        uncertain.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM qso WHERE deleted=0 AND "
            "(CASE WHEN json_valid(adif_extra) THEN json_extract(adif_extra,'$.APP_DECOLOG_ACTIVATION_UUID') END)=? "
            "AND (CASE WHEN json_valid(adif_extra) THEN json_extract(adif_extra,'$.APP_DECOLOG_ACTIVATION_REVIEW') END)='PERIOD_UNCERTAIN'"));
        uncertain.addBindValue(m_session.sessionUuid);
        m_uncertainCount = uncertain.exec() && uncertain.next() ? uncertain.value(0).toInt() : -1;
    }

    QSqlQuery q(m_ctx.db->connection());
    q.prepare(QStringLiteral(
        "SELECT COUNT(*), COUNT(DISTINCT call), MAX(qso_datetime_on) FROM qso "
        "WHERE deleted = 0 AND %2%1").arg(activeSessionPredicate(m_session.sessionUuid), activeSessionStartPredicate(m_session.sessionUuid)));
    q.addBindValue(m_session.startedAt.toString(Qt::ISODate));
    if (!m_session.sessionUuid.isEmpty()) q.addBindValue(m_session.sessionUuid);
    if (q.exec() && q.next()) {
        m_qsoCount = q.value(0).toInt();
        m_uniqueCalls = q.value(1).toInt();
        const QDateTime last = QDateTime::fromString(q.value(2).toString(), Qt::ISODate);
        if (last.isValid())
            m_lastQso = last.toString(QStringLiteral("HH:mm"));
    }

    QSqlQuery bands(m_ctx.db->connection());
    bands.prepare(QStringLiteral(
        "SELECT band, CASE WHEN IFNULL(submode, '') = '' OR mode = 'SSB' THEN mode ELSE submode END AS m, COUNT(*) "
        "FROM qso WHERE deleted = 0 AND %2%1 GROUP BY band, m ORDER BY COUNT(*) DESC").arg(activeSessionPredicate(m_session.sessionUuid), activeSessionStartPredicate(m_session.sessionUuid)));
    bands.addBindValue(m_session.startedAt.toString(Qt::ISODate));
    if (!m_session.sessionUuid.isEmpty()) bands.addBindValue(m_session.sessionUuid);
    if (bands.exec()) {
        while (bands.next()) {
            m_perBand << QVariantMap{{QStringLiteral("band"), bands.value(0).toString()},
                                     {QStringLiteral("mode"), bands.value(1).toString()},
                                     {QStringLiteral("count"), bands.value(2).toInt()}};
        }
    }
}

bool ActivationController::isDuplicate(const QString& call, const QString& band, const QString& mode) const
{
    if (!m_session.active || !m_ctx.db || !m_ctx.db->isOpen() || !m_session.startedAt.isValid())
        return false;
    QSqlQuery q(m_ctx.db->connection());
    q.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM qso WHERE deleted = 0 AND call = ? AND band = ? AND %2 "
        "AND (CASE WHEN IFNULL(submode, '') = '' OR mode = 'SSB' THEN mode ELSE submode END) = ?%1").arg(activeSessionPredicate(m_session.sessionUuid), activeSessionStartPredicate(m_session.sessionUuid)));
    q.addBindValue(call.trimmed().toUpper());
    q.addBindValue(band.trimmed().toLower());
    q.addBindValue(m_session.startedAt.toString(Qt::ISODate));
    q.addBindValue(mode.trimmed().toUpper());
    if (!m_session.sessionUuid.isEmpty()) q.addBindValue(m_session.sessionUuid);
    return q.exec() && q.next() && q.value(0).toInt() > 0;
}

core::ContestRules ActivationController::currentRules() const
{
    return core::contestrules::withOverrides(core::contestrules::forId(m_session.contestId), m_session.rules);
}

QVariantMap ActivationController::editionRules() const
{
    const core::ContestRules r = currentRules();
    const core::ContestRules base = core::contestrules::forId(m_session.contestId);
    return QVariantMap{
        {QStringLiteral("valid"), r.valid},
        {QStringLiteral("bands"), r.bands},
        {QStringLiteral("submitHours"), r.submitHours},
        {QStringLiteral("submitDays"), r.submitDays},
        {QStringLiteral("maxOperatingHours"), r.maxOperatingHours},
        {QStringLiteral("minOffMinutes"), r.minOffMinutes},
        {QStringLiteral("changed"), !m_session.rules.isEmpty()},
        {QStringLiteral("baseBands"), base.bands},
    };
}

void ActivationController::loadHistoryOfSession()
{
    m_history.clear();
    if (!m_session.active || m_session.callHistory.isEmpty())
        return;
    QString error;
    if (!m_history.load(m_session.callHistory, &error) && m_ctx.activity)
        m_ctx.activity(QStringLiteral("ACT"), tr("Call history not readable: %1").arg(error), QStringLiteral("warning"));
}

QString ActivationController::loadCallHistory(const QUrl& file)
{
    if (!m_session.active)
        return tr("Open the contest first");
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    core::CallHistory history;
    QString error;
    if (!history.load(path, &error))
        return tr("Call history not readable: %1").arg(error);
    m_history = history;
    m_session.callHistory = path;
    save();
    if (m_ctx.activity)
        m_ctx.activity(QStringLiteral("ACT"), tr("Call history: %n call(s) from %1", nullptr, history.size())
                                                  .arg(QFileInfo(path).fileName()),
                       QStringLiteral("success"));
    emit changed();
    return {};
}

void ActivationController::clearCallHistory()
{
    m_history.clear();
    m_session.callHistory.clear();
    save();
    emit changed();
}

QVariantMap ActivationController::suggestExchange(const QString& rawCall) const
{
    // Prima il log (quello che la stazione ha mandato a noi), poi il call
    // history (quello che ha mandato a tutti l'anno scorso), poi il paese.
    const QVariantMap fromLog = suggestFromLog(rawCall);
    if (fromLog.value(QStringLiteral("from")).toString() == QLatin1String("log") || m_history.size() == 0)
        return fromLog;
    const QString call = rawCall.trimmed().toUpper();
    const core::ContestRules rules = currentRules();
    QString value;
    switch (rules.exchange) {
    case core::ContestRules::Exchange::CqZone:
        value = m_history.value(call, {QStringLiteral("CQZONE"), QStringLiteral("CQZ"), QStringLiteral("EXCH1")});
        break;
    case core::ContestRules::Exchange::CqZoneQth: {
        const QString zone = m_history.value(call, {QStringLiteral("CQZONE"), QStringLiteral("CQZ")});
        const QString state = m_history.value(call, {QStringLiteral("STATE"), QStringLiteral("SECT")});
        value = zone.isEmpty() ? QString() : state.isEmpty() ? zone : zone + QLatin1Char(' ') + state;
        break;
    }
    case core::ContestRules::Exchange::ItuZone:
        value = m_history.value(call, {QStringLiteral("ITUZONE"), QStringLiteral("ITUZ"), QStringLiteral("EXCH1")});
        break;
    case core::ContestRules::Exchange::Province:
    case core::ContestRules::Exchange::AriSection:
        value = m_history.value(call, {QStringLiteral("EXCH1"), QStringLiteral("STATE")});
        break;
    case core::ContestRules::Exchange::Grid:
        value = m_history.value(call, {QStringLiteral("LOC1"), QStringLiteral("GRID"), QStringLiteral("EXCH1")});
        break;
    case core::ContestRules::Exchange::Serial:
    case core::ContestRules::Exchange::None:
        break;
    }
    if (value.isEmpty())
        return fromLog;
    return QVariantMap{{QStringLiteral("value"), value.toUpper()}, {QStringLiteral("from"), QStringLiteral("history")}};
}

QVariantMap ActivationController::suggestFromLog(const QString& rawCall) const
{
    QVariantMap out{{QStringLiteral("value"), QString()}, {QStringLiteral("from"), QString()}};
    const QString call = rawCall.trimmed().toUpper();
    const core::ContestRules rules = currentRules();
    if (!rules.valid || !m_session.active || call.size() < 3)
        return out;
    auto answer = [&out](const QString& value, const char* from) {
        out[QStringLiteral("value")] = value;
        out[QStringLiteral("from")] = QLatin1String(from);
        return out;
    };

    // Quello che la stazione ha mandato l'ultima volta, e cosa di lei sa il
    // log: una stazione non cambia zona ne' provincia da un anno all'altro.
    struct Past {
        int cqz{0};
        int ituz{0};
        QString state;
        QString grid;
        QString contestId;
        QString exchange;
    };
    QList<Past> past;
    if (m_ctx.db && m_ctx.db->isOpen()) {
        QSqlQuery q(m_ctx.db->connection());
        q.setForwardOnly(true);
        q.prepare(QStringLiteral(
            "SELECT IFNULL(cqz, 0), IFNULL(ituz, 0), IFNULL(state, ''), IFNULL(gridsquare, ''), "
            "IFNULL(adif_extra, '') FROM qso WHERE deleted = 0 AND call = ? "
            "ORDER BY qso_datetime_on DESC LIMIT 30"));
        q.addBindValue(call);
        if (q.exec()) {
            while (q.next()) {
                Past p;
                p.cqz = q.value(0).toInt();
                p.ituz = q.value(1).toInt();
                p.state = q.value(2).toString();
                p.grid = q.value(3).toString();
                const QString extra = q.value(4).toString();
                if (!extra.isEmpty()) {
                    const QJsonObject fields = QJsonDocument::fromJson(extra.toUtf8()).object();
                    p.contestId = fields.value(QStringLiteral("CONTEST_ID")).toString().toUpper();
                    p.exchange = fields.value(QStringLiteral("SRX_STRING")).toString().trimmed().toUpper();
                    if (p.exchange.isEmpty())
                        p.exchange = fields.value(QStringLiteral("SRX")).toString().trimmed().toUpper();
                }
                past << p;
            }
        }
    }
    const core::ContestStation where = m_ctx.locate ? m_ctx.locate(call) : core::ContestStation{};
    // Lo scambio di un QSO fatto in un contest dello stesso tipo: il CQ WW in
    // CW e quello in SSB si scambiano la stessa cosa.
    auto sameKind = [&rules](const QString& id) {
        if (id == rules.id)
            return true;
        const QString family = rules.id.section(QLatin1Char('-'), 0, 1);
        return !family.isEmpty() && id.startsWith(family + QLatin1Char('-'));
    };

    switch (rules.exchange) {
    case core::ContestRules::Exchange::CqZone:
    case core::ContestRules::Exchange::CqZoneQth: {
        // La zona: "05 MA" di un CQ WW RTTY vale 5 anche per il CQ WW in CW.
        int zone = 0;
        const char* from = "log";
        for (const Past& p : past) {
            if (sameKind(p.contestId) && (zone = core::contestrules::exchangeZone(p.exchange)) > 0)
                break;
        }
        for (const Past& p : past) {
            if (zone > 0)
                break;
            zone = p.cqz;
        }
        if (zone <= 0 && where.cqZone > 0) {
            zone = where.cqZone;
            from = "cty";
        }
        if (zone <= 0)
            return out;
        QString value = QString::number(zone);
        // In RTTY chi sta negli USA o in Canada manda anche lo stato o l'area:
        // da una gara uguale gia' fatta, o dallo STATE del log.
        if (rules.exchange == core::ContestRules::Exchange::CqZoneQth) {
            for (const Past& p : past) {
                const QString qth = core::contestrules::wveQth(p.contestId == rules.id ? p.exchange : QString(),
                                                               p.state, where.dxcc);
                if (!qth.isEmpty()) {
                    value += QLatin1Char(' ') + qth;
                    break;
                }
            }
        }
        return answer(value, from);
    }
    case core::ContestRules::Exchange::ItuZone:
        // Le stazioni HQ mandano la sigla della societa' al posto della zona:
        // si sa solo da una gara gia' fatta con loro.
        for (const Past& p : past) {
            if (sameKind(p.contestId) && !p.exchange.isEmpty())
                return answer(p.exchange, "log");
        }
        for (const Past& p : past) {
            if (p.ituz > 0)
                return answer(QString::number(p.ituz), "log");
        }
        if (where.ituZone > 0)
            return answer(QString::number(where.ituZone), "cty");
        return out;
    case core::ContestRules::Exchange::Province: {
        // Le italiane mandano la provincia; le altre il progressivo, che non
        // si puo' sapere prima.
        const bool italian = where.dxcc == 248 || where.dxcc == 225;
        if (!italian)
            return out;
        for (const Past& p : past) {
            if (sameKind(p.contestId)) {
                const QString code = core::awards::italianProvince(p.exchange);
                if (!code.isEmpty())
                    return answer(code, "log");
            }
        }
        for (const Past& p : past) {
            const QString code = core::awards::italianProvince(p.state);
            if (!code.isEmpty())
                return answer(code, "log");
        }
        return out;
    }
    case core::ContestRules::Exchange::AriSection:
        for (const Past& p : past) {
            if (sameKind(p.contestId) && !p.exchange.isEmpty())
                return answer(p.exchange, "log");
        }
        return out;
    case core::ContestRules::Exchange::Grid:
        for (const Past& p : past) {
            if (p.grid.size() >= 4)
                return answer(p.grid.left(4).toUpper(), "log");
        }
        return out;
    case core::ContestRules::Exchange::Serial:
    case core::ContestRules::Exchange::None:
        return out;
    }
    return out;
}

void ActivationController::applyTo(AdifRecord& record, int serialOverride) const
{
    int serial = serialOverride > 0 ? serialOverride : m_session.nextSerial;
    // In rete il numero lo da' chi distribuisce: una sequenza sola per tutti.
    if (serialOverride <= 0 && m_session.serialEnabled && m_session.sharedSerial && m_ctx.takeSerial)
        serial = m_ctx.takeSerial(m_session.nextSerial);
    m_lastSerial = serial;
    m_session.applyTo(record, serial);
}

void ActivationController::qsoLogged()
{
    if (!m_session.active)
        return;
    if (m_session.serialEnabled) {
        m_session.nextSerial = qMax(m_session.nextSerial, m_lastSerial) + 1;
        save();
    }
    recount();
    emit changed();
}

QVariantList ActivationController::qsoIds() const
{
    QVariantList ids;
    if (!m_session.active || !m_ctx.db || !m_ctx.db->isOpen() || !m_session.startedAt.isValid())
        return ids;
    QSqlQuery q(m_ctx.db->connection());
    q.prepare(QStringLiteral(
        "SELECT id FROM qso WHERE deleted = 0 AND %2%1 ORDER BY qso_datetime_on, id").arg(activeSessionPredicate(m_session.sessionUuid), activeSessionStartPredicate(m_session.sessionUuid)));
    q.addBindValue(m_session.startedAt.toString(Qt::ISODate));
    if (!m_session.sessionUuid.isEmpty()) q.addBindValue(m_session.sessionUuid);
    if (q.exec()) {
        while (q.next())
            ids << q.value(0).toLongLong();
    }
    return ids;
}

namespace {
QVariantMap scanPartitionPreview(QSqlDatabase db, const QString& uuid, const std::shared_ptr<std::atomic_bool>& cancelled,
                                 bool ownsTransaction = true, const QString& selectedKey = {}, QList<qint64>* selectedIds = nullptr,
                                 const QString& cataloguePath = {}, const QString& potaCataloguePath = {})
{
    QVariantMap result{{QStringLiteral("groups"), QVariantList{}}, {QStringLiteral("reportedCount"), 0}};
    const auto fail = [&](const QString& error) { result[QStringLiteral("error")] = error; return result; };
    const auto tr = [](const char* text) { return QCoreApplication::translate("ActivationController", text); };
    if (ownsTransaction && !db.transaction()) return fail(tr("Cannot open a consistent preview of the log."));
    struct ReadTransaction { QSqlDatabase db; bool committed{false}; ~ReadTransaction() { if (!committed) db.rollback(); } } transaction{db, !ownsTransaction};
    QSqlQuery exists(db);
    exists.prepare(QStringLiteral("SELECT 1 FROM activation_run WHERE uuid=?")); exists.addBindValue(uuid);
    if (!exists.exec() || !exists.next()) return fail(tr("The session journal is unavailable or inconsistent."));
    QSqlQuery q(db); q.setForwardOnly(true);
    q.prepare(QStringLiteral("SELECT qso_datetime_on,adif_extra,id,uuid,revision,call,qso.*, "
        "(SELECT json_group_array(json_array(service,sent,sent_date,rcvd,rcvd_date,remote_id,via,last_error)) "
        "FROM (SELECT service,sent,sent_date,rcvd,rcvd_date,remote_id,via,last_error FROM qsl_status "
        "WHERE qso_id=qso.id ORDER BY service)) FROM qso WHERE deleted=0 AND "
        "(CASE WHEN json_valid(adif_extra) THEN json_extract(adif_extra,'$.APP_DECOLOG_ACTIVATION_UUID') END)=? ORDER BY id"));
    q.addBindValue(uuid);
    if (!q.exec()) return fail(tr("Cannot read the session QSO."));
    QMap<QString, QVariantMap> groups;
    QMap<QString, QByteArray> hashes;
    QVariantList excluded, potaCorrespondentRows;
    const auto exclude = [&](const QString& reason) {
        if (!selectedIds) excluded << QVariantMap{{QStringLiteral("id"), q.value(2)}, {QStringLiteral("call"), q.value(5)},
            {QStringLiteral("uuid"), q.value(3)}, {QStringLiteral("revision"), q.value(4)},
            {QStringLiteral("at"), q.value(0)}, {QStringLiteral("reason"), reason}};
    };
    int count = 0, missingDate = 0, missingReference = 0, uncertainPeriod = 0;
    const QList<QPair<QString, QString>> fields{{QStringLiteral("POTA"), QStringLiteral("MY_POTA_REF")},
        {QStringLiteral("SOTA"), QStringLiteral("MY_SOTA_REF")}, {QStringLiteral("WWFF"), QStringLiteral("MY_WWFF_REF")},
        {QStringLiteral("IOTA"), QStringLiteral("MY_IOTA")}};
    while (q.next()) {
        if (cancelled && cancelled->load()) return fail(tr("Preview cancelled."));
        ++count;
        const auto extra = QJsonDocument::fromJson(q.value(1).toString().toUtf8()).object();
        if (extra.value(QStringLiteral("APP_DECOLOG_ACTIVATION_REVIEW")).toString() == QLatin1String("PERIOD_UNCERTAIN")) {
            ++uncertainPeriod; exclude(QStringLiteral("uncertainPeriod")); continue;
        }
        QDateTime timestamp = QDateTime::fromString(q.value(0).toString(), Qt::ISODate);
        if (timestamp.isValid()) {
            // Legacy timestamps without a zone are stored as UTC wall time.
            if (timestamp.timeSpec() == Qt::LocalTime) timestamp.setTimeZone(QTimeZone::UTC);
            else timestamp = timestamp.toUTC();
        }
        QDate date = timestamp.date();
        if (extra.contains(QStringLiteral("QSO_DATE"))) date = QDate::fromString(extra.value(QStringLiteral("QSO_DATE")).toString(), QStringLiteral("yyyyMMdd"));
        if (!date.isValid()) { ++missingDate; exclude(QStringLiteral("missingDate")); continue; }
        const QString day = date.toString(Qt::ISODate);
        const QString op = extra.value(QStringLiteral("OPERATOR")).toString().trimmed().toUpper();
        const QString stationCall = extra.value(QStringLiteral("STATION_CALLSIGN")).toString().trimmed();
        const QString grid = extra.value(QStringLiteral("MY_GRIDSQUARE")).toString().trimmed().toUpper();
        QByteArray recordBytes;
        QDataStream stream(&recordBytes, QIODevice::WriteOnly);
        stream.setVersion(QDataStream::Qt_6_0);
        const int columnCount=q.record().count();
        for (int column=6; column<columnCount; ++column) stream << q.value(column);
        if (stream.status()!=QDataStream::Ok)
            return fail(tr("Cannot fingerprint the session QSO."));
        const QByteArray identity=QCryptographicHash::hash(recordBytes,QCryptographicHash::Sha256);
        bool found = false, potaReviewRecorded = false;
        for (const auto& pair : fields) {
            QSet<QString> seen;
            for (const QString& item : extra.value(pair.second).toString().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                const QString reference = item.trimmed().toUpper();
                if (reference.isEmpty() || seen.contains(reference)) continue;
                seen.insert(reference); found = true;
                const QString key = QString::fromUtf8(QJsonDocument(QJsonArray{pair.first, reference, day, op, grid}).toJson(QJsonDocument::Compact));
                if (!groups.contains(key)) {
                    QVariantMap checked;
                    const bool syntaxMatches = Activation::normalizePrograms({{pair.first.toLower(), reference}}, &checked);
                    groups.insert(key, {{QStringLiteral("program"), pair.first}, {QStringLiteral("reference"), reference},
                        {QStringLiteral("day"), day}, {QStringLiteral("operator"), op}, {QStringLiteral("grid"), grid},
                        {QStringLiteral("count"), 0}, {QStringLiteral("referenceSyntaxWarning"), !syntaxMatches}});
                }
                groups[key][QStringLiteral("count")] = groups[key].value(QStringLiteral("count")).toInt() + 1;
                if (pair.first == QLatin1String("POTA")) {
                    const auto exportedValue = [&extra, &q](const QString& adif, const QString& column) {
                        return (extra.contains(adif) ? extra.value(adif).toString() : q.value(column).toString()).trimmed().toUpper();
                    };
                    const QString other = exportedValue(QStringLiteral("POTA_REF"), QStringLiteral("pota_ref"));
                    const QString sig = exportedValue(QStringLiteral("SIG"), QStringLiteral("sig"));
                    const QString sigInfo = exportedValue(QStringLiteral("SIG_INFO"), QStringLiteral("sig_info"));
                    QVariantMap normalized;
                    const bool singleValid = !other.isEmpty() && !other.contains(QLatin1Char(','))
                        && Activation::normalizePrograms({{QStringLiteral("pota"), other}}, &normalized);
                    const bool explicitMatch = sig == QLatin1String("POTA")
                        && sigInfo == other.section(QLatin1Char('@'), 0, 0);
                    const bool incompletePotaSig=other.isEmpty() && sig==QLatin1String("POTA") && sigInfo.isEmpty();
                    const bool invalidPotaSig=other.isEmpty() && sig==QLatin1String("POTA") && !sigInfo.isEmpty()
                        && !Activation::normalizePrograms({{QStringLiteral("pota"),sigInfo}}, &normalized);
                    if (incompletePotaSig || invalidPotaSig || (!other.isEmpty() && (!singleValid || ((!sig.isEmpty() || !sigInfo.isEmpty()) && !explicitMatch)))) {
                        groups[key][QStringLiteral("potaCorrespondentReview")] = groups[key].value(QStringLiteral("potaCorrespondentReview")).toInt() + 1;
                        if (!selectedIds && !potaReviewRecorded) {
                            potaReviewRecorded = true;
                            const QString reason = other.contains(QLatin1Char(',')) ? QStringLiteral("multipleReferences")
                                : invalidPotaSig || (!other.isEmpty() && !singleValid) ? QStringLiteral("invalidReference") : QStringLiteral("conflictingSig");
                            potaCorrespondentRows << QVariantMap{{QStringLiteral("id"), q.value(2)}, {QStringLiteral("uuid"), q.value(3)},
                                {QStringLiteral("revision"), q.value(4)}, {QStringLiteral("call"), q.value(5)}, {QStringLiteral("at"), q.value(0)},
                                {QStringLiteral("reason"), reason}};
                        }
                    }
                }
                if (stationCall.isEmpty()) groups[key][QStringLiteral("missingStationCall")] = groups[key].value(QStringLiteral("missingStationCall")).toInt() + 1;
                if (op.isEmpty()) groups[key][QStringLiteral("missingOperator")] = groups[key].value(QStringLiteral("missingOperator")).toInt() + 1;
                if (stationCall.isEmpty() && op.isEmpty()) groups[key][QStringLiteral("missingCallIdentity")] = groups[key].value(QStringLiteral("missingCallIdentity")).toInt() + 1;
                hashes[key] = QCryptographicHash::hash(hashes.value(key) + identity, QCryptographicHash::Sha256);
                if (selectedIds && key == selectedKey) selectedIds->append(q.value(2).toLongLong());
            }
        }
        if (!found) { ++missingReference; exclude(QStringLiteral("missingReference")); }
    }
    if (q.lastError().isValid()) return fail(tr("Cannot read the session QSO."));
    if (cancelled && cancelled->load()) return fail(tr("Preview cancelled."));
    if (!cataloguePath.isEmpty() && !selectedIds) {
        QVariantList requests; QStringList keys;
        for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
            if (it.value().value(QStringLiteral("program")) == QLatin1String("SOTA")) {
                requests << QVariantMap{{QStringLiteral("reference"), it.value().value(QStringLiteral("reference"))},
                                        {QStringLiteral("day"), it.value().value(QStringLiteral("day"))}};
                keys << it.key();
            }
        }
        if (!requests.isEmpty()) {
            QVariantMap catalogue = SotaCatalogue::lookupBatch(cataloguePath, requests, cancelled);
            if (cancelled && cancelled->load()) return fail(tr("Preview cancelled."));
            const auto matches = catalogue.take(QStringLiteral("rows")).toList();
            result[QStringLiteral("sotaCatalogue")] = catalogue;
            for (qsizetype i = 0; i < keys.size(); ++i) {
                const QVariantMap match = i < matches.size() ? matches.at(i).toMap() : QVariantMap{};
                groups[keys.at(i)][QStringLiteral("catalogueStatus")] = match.value(QStringLiteral("status"), QStringLiteral("unavailable"));
                groups[keys.at(i)][QStringLiteral("catalogueName")] = match.value(QStringLiteral("name"));
                groups[keys.at(i)][QStringLiteral("catalogueValidFrom")] = match.value(QStringLiteral("validFrom"));
                groups[keys.at(i)][QStringLiteral("catalogueValidTo")] = match.value(QStringLiteral("validTo"));
            }
        }
    }
    if (!potaCataloguePath.isEmpty() && !selectedIds) {
        QVariantList references; QStringList keys;
        for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
            if (it.value().value(QStringLiteral("program")) == QLatin1String("POTA")) {
                references << it.value().value(QStringLiteral("reference")); keys << it.key();
            }
        }
        if (!references.isEmpty()) {
            QVariantMap catalogue = PotaCatalogue::lookupBatch(potaCataloguePath, references, cancelled);
            if (cancelled && cancelled->load()) return fail(tr("Preview cancelled."));
            const auto matches = catalogue.take(QStringLiteral("rows")).toList();
            result[QStringLiteral("potaCatalogue")] = catalogue;
            for (qsizetype i = 0; i < keys.size(); ++i) {
                const QVariantMap match = i < matches.size() ? matches.at(i).toMap() : QVariantMap{};
                groups[keys.at(i)][QStringLiteral("potaSnapshotStatus")] = match.value(QStringLiteral("status"), QStringLiteral("unavailable"));
                groups[keys.at(i)][QStringLiteral("potaLocationStatus")] = match.value(QStringLiteral("locationStatus"));
                groups[keys.at(i)][QStringLiteral("potaName")] = match.value(QStringLiteral("name"));
                groups[keys.at(i)][QStringLiteral("potaLocations")] = match.value(QStringLiteral("locations"));
            }
        }
    }
    QVariantList rows;
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        it.value()[QStringLiteral("fingerprint")] = QString::fromLatin1(hashes.value(it.key()).toHex());
        rows << it.value();
    }
    result[QStringLiteral("groups")] = rows; result[QStringLiteral("reportedCount")] = count;
    result[QStringLiteral("missingDate")] = missingDate; result[QStringLiteral("missingReference")] = missingReference;
    result[QStringLiteral("uncertainPeriod")] = uncertainPeriod;
    result[QStringLiteral("excluded")] = excluded;
    result[QStringLiteral("potaCorrespondentRows")] = potaCorrespondentRows;
    if (ownsTransaction && !db.commit()) return fail(tr("Cannot complete the log preview."));
    transaction.committed = true;
    return result;
}
} // namespace

QVariantMap ActivationController::partitionPreview() const
{
    if (!m_session.active || m_session.sessionUuid.isEmpty())
        return {{QStringLiteral("error"), tr("This session has no tracked identity; its QSO cannot be attributed automatically.")}};
    if (!m_ctx.db || !m_ctx.db->isOpen()) return {{QStringLiteral("error"), tr("The log is not open.")}};
    return scanPartitionPreview(m_ctx.db->connection(), m_session.sessionUuid, {}, true, {}, nullptr, m_sotaCatalogue->path(), m_potaCatalogue->path());
}

QString ActivationController::sessionReviewToken(const QString& uuid) const
{
    return m_ctx.db && m_ctx.db->isOpen() ? m_ctx.db->activationRunToken(uuid) : QString();
}
QString ActivationController::sessionReviewSummary(const QString& uuid) const
{
    return m_ctx.db && m_ctx.db->isOpen() ? m_ctx.db->activationRunToken(uuid, false) : QString();
}
QVariantMap ActivationController::sessionReviewTokens(const QString& uuid) const
{
    if (!m_ctx.db || !m_ctx.db->isOpen()) return {};
    QSqlDatabase db=m_ctx.db->connection(); QSqlQuery scope(db);
    if (!scope.exec(QStringLiteral("SAVEPOINT activation_review_tokens"))) return {};
    const QString summary=m_ctx.db->activationRunToken(uuid,false);
    const QString token=m_ctx.db->activationRunToken(uuid);
    QSqlQuery release(db);
    if (!release.exec(QStringLiteral("RELEASE SAVEPOINT activation_review_tokens")) || summary.isEmpty() || token.isEmpty()) return {};
    return {{QStringLiteral("summary"),summary},{QStringLiteral("token"),token}};
}

QVariantMap ActivationController::sessionReviewHistory(const QString& uuid, const QString& token, qint64 afterId) const
{
    const auto fail = [this](const QString& text) { return QVariantMap{{QStringLiteral("error"),text},{QStringLiteral("rows"),QVariantList{}}}; };
    if (!m_ctx.db || !m_ctx.db->isOpen() || uuid.isEmpty() || token.isEmpty() || afterId<0)
        return fail(tr("The activation history cannot be checked."));
    QSqlDatabase db=m_ctx.db->connection(); QSqlQuery scope(db);
    if (!scope.exec(QStringLiteral("SAVEPOINT activation_review_history"))) return fail(tr("The activation history cannot be checked."));
    struct Release { QSqlDatabase db; ~Release() { QSqlQuery q(db); q.exec(QStringLiteral("RELEASE SAVEPOINT activation_review_history")); } } release{db};
    if (m_ctx.db->activationRunToken(uuid)!=token) return fail(tr("The activation session changed; select it again."));
    QSqlQuery q(db); q.prepare(QStringLiteral("SELECT id,changed_at,event,substr(state_json,1,65537) FROM activation_run_change WHERE session_uuid=? AND id>? ORDER BY id LIMIT 51"));
    q.addBindValue(uuid); q.addBindValue(afterId);
    if (!q.exec()) return fail(tr("The activation history cannot be checked."));
    QVariantList rows;
    while (q.next()) {
        if (rows.size() == 50) return {{QStringLiteral("rows"),rows},{QStringLiteral("hasMore"),true}};
        const QString json=q.value(3).toString(); QJsonParseError parse; const auto doc=QJsonDocument::fromJson(json.toUtf8(),&parse);
        const bool valid=json.size()<=65536 && parse.error==QJsonParseError::NoError && doc.isObject();
        const auto state=valid ? doc.object() : QJsonObject{};
        rows << QVariantMap{{QStringLiteral("id"),q.value(0)}, {QStringLiteral("at"),q.value(1).toString().left(64)}, {QStringLiteral("event"),q.value(2).toString().left(32)},
            {QStringLiteral("valid"),valid}, {QStringLiteral("kind"),state.value(QStringLiteral("kind")).toString().left(64)},
            {QStringLiteral("reference"),state.value(QStringLiteral("reference")).toString().left(512)},
            {QStringLiteral("grid"),state.value(QStringLiteral("myGrid")).toString().left(64)},
            {QStringLiteral("profileId"),state.value(QStringLiteral("stationProfileId")).toVariant()},
            {QStringLiteral("programs"),QString::fromUtf8(QJsonDocument(state.value(QStringLiteral("programs")).toObject()).toJson(QJsonDocument::Compact)).left(1024)}};
    }
    if (q.lastError().isValid()) return fail(tr("The activation history cannot be checked."));
    return {{QStringLiteral("rows"),rows},{QStringLiteral("hasMore"),false}};
}

QVariantList ActivationController::closedRuns(qint64 beforeRowId) const
{
    QVariantList rows;
    if (!m_ctx.db || !m_ctx.db->isOpen()) return rows;
    QSqlQuery q(m_ctx.db->connection());
    q.prepare(beforeRowId > 0
        ? QStringLiteral("SELECT rowid,uuid,started_at,ended_at,last_json FROM activation_run WHERE ended_at IS NOT NULL AND rowid<? ORDER BY rowid DESC LIMIT 101")
        : QStringLiteral("SELECT rowid,uuid,started_at,ended_at,last_json FROM activation_run WHERE ended_at IS NOT NULL ORDER BY rowid DESC LIMIT 101"));
    if (beforeRowId > 0) q.addBindValue(beforeRowId);
    if (!q.exec()) return rows;
    while (q.next()) {
        const QVariantMap state = QJsonDocument::fromJson(q.value(4).toByteArray()).object().toVariantMap();
        rows << QVariantMap{{QStringLiteral("rowid"), q.value(0)}, {QStringLiteral("uuid"), q.value(1)},
            {QStringLiteral("startedAt"), q.value(2)}, {QStringLiteral("endedAt"), q.value(3)},
            {QStringLiteral("title"), state.value(QStringLiteral("title"))},
            {QStringLiteral("reviewSummary"), m_ctx.db->activationRunToken(q.value(1).toString(), false)}};
    }
    return rows;
}

static QVariantMap contestReportResult(const core::contestreport::Report& report)
{
    QVariantMap result;
    result.insert(QStringLiteral("error"), report.error);
    result.insert(QStringLiteral("contest"), report.contest);
    result.insert(QStringLiteral("ownCall"), report.ownCall);
    result.insert(QStringLiteral("total"), report.entries.size());
    result.insert(QStringLiteral("unknownTotal"), report.unrecognizedLines.size());
    QVariantList entries, unknown;
    int nil=0, calls=0, exchanges=0;
    for (const auto& entry : report.entries) {
        if (entry.kind == QLatin1String("not_in_log")) ++nil;
        else if (entry.kind == QLatin1String("incorrect_call")) ++calls;
        else if (entry.kind == QLatin1String("incorrect_exchange")) ++exchanges;
        if (entries.size() < 500)
            entries << QVariantMap{{QStringLiteral("line"), entry.lineNumber}, {QStringLiteral("kind"), entry.kind},
        {QStringLiteral("date"), entry.date}, {QStringLiteral("time"), entry.time}, {QStringLiteral("mode"), entry.mode},
        {QStringLiteral("frequency"), entry.frequencyKhz}, {QStringLiteral("call"), entry.call},
        {QStringLiteral("receivedExchange"), entry.receivedExchange},
        {QStringLiteral("correction"), entry.correction}, {QStringLiteral("original"), entry.original.left(1024)}};
    }
    for (int index : report.unrecognizedLines) {
        if (unknown.size() >= 200) break;
        unknown << QVariantMap{{QStringLiteral("line"), index+1}, {QStringLiteral("text"), report.lines.at(index).left(1024)}};
    }
    result.insert(QStringLiteral("entries"), entries); result.insert(QStringLiteral("unknown"), unknown);
    result.insert(QStringLiteral("nil"), nil); result.insert(QStringLiteral("incorrectCall"), calls);
    result.insert(QStringLiteral("incorrectExchange"), exchanges);
    return result;
}

bool ActivationController::requestContestGoals(const QString &contest, const QString &fromA, const QString &toA, const QString &fromB, const QString &toB,
                                               bool save, int targetA, int targetB, int revisionA, int revisionB, bool remove)
{
    if ((save && remove) || m_contestReportBusy || !m_ctx.db || !m_ctx.db->isOpen() || m_ctx.db->path().isEmpty() ||
        m_ctx.db->path()==QLatin1String(":memory:") || contest.size()>64 || fromA.size()>10 || toA.size()>10 || fromB.size()>10 || toB.size()>10) return false;
    const QString path=m_ctx.db->path(), identity=m_ctx.db->identity(); const quint64 generation=m_ctx.db->openGeneration();
    if (identity.isEmpty()) return false;
    const QStringList selection{contest.trimmed().toUpper(),fromA,toA,fromB,toB};
    const bool mutate=save || remove;
    if (mutate && (m_goalSelection!=selection || m_goalLogPath!=path || m_goalLogIdentity!=identity || m_goalLogGeneration!=generation ||
                 m_goalRevisionA!=revisionA || m_goalRevisionB!=revisionB || m_goalGeneration<0)) return false;
    const int goalGeneration=m_goalGeneration;
    if (!mutate) { m_goalSelection.clear(); m_goalRevisionA=m_goalRevisionB=m_goalGeneration=-1; }
    m_contestReportBusy=true; emit contestReportBusyChanged(); QPointer<ActivationController> self(this);
    m_contestReportPool.start([self,path,identity,generation,contest,fromA,toA,fromB,toB,save,remove,mutate,targetA,targetB,revisionA,revisionB,selection,goalGeneration] {
        QVariantMap result{{QStringLiteral("contest"),contest},{QStringLiteral("fromA"),fromA},{QStringLiteral("toA"),toA},
            {QStringLiteral("fromB"),fromB},{QStringLiteral("toB"),toB},{QStringLiteral("saved"),save},{QStringLiteral("removed"),remove}};
        LogDatabase db;
        if (!(mutate ? db.openExistingReadWrite(path) : db.openReadOnly(path))) result.insert(QStringLiteral("error"),db.lastError());
        else if (db.identity()!=identity) result.insert(QStringLiteral("error"),tr("The log identity changed."));
        else {
            const QDate a=QDate::fromString(fromA,Qt::ISODate), ae=QDate::fromString(toA,Qt::ISODate),
                        b=QDate::fromString(fromB,Qt::ISODate), be=QDate::fromString(toB,Qt::ISODate);
            const auto goals=remove ? core::contestcomparison::removeGoals(db.connection(),identity,contest,a,ae,b,be,revisionA,revisionB,goalGeneration)
                : save ? core::contestcomparison::saveGoals(db.connection(),identity,contest,a,ae,b,be,targetA,targetB,revisionA,revisionB,goalGeneration)
                       : core::contestcomparison::loadGoals(db.connection(),contest,a,ae,b,be);
            result.insert(QStringLiteral("error"),goals.error); result.insert(QStringLiteral("targetA"),goals.a.target);
            result.insert(QStringLiteral("targetB"),goals.b.target); result.insert(QStringLiteral("revisionA"),goals.a.revision);
            result.insert(QStringLiteral("revisionB"),goals.b.revision); result.insert(QStringLiteral("goalGeneration"),goals.generation);
        }
        if (!self) return;
        QMetaObject::invokeMethod(self.data(),[self,path,identity,generation,result,selection,mutate] {
            if (!self) return;
            QVariantMap delivered=result;
            if (!self->m_ctx.db || !self->m_ctx.db->isOpen() || self->m_ctx.db->path()!=path ||
                self->m_ctx.db->openGeneration()!=generation || self->m_ctx.db->identity()!=identity)
                delivered={{QStringLiteral("error"),mutate ? tr("The log changed; the change may have completed in the previous log.") : tr("The log changed; load goals again.")}};
            if (delivered.value(QStringLiteral("error")).toString().isEmpty()) {
                self->m_goalSelection=selection; self->m_goalLogPath=path; self->m_goalLogIdentity=identity; self->m_goalLogGeneration=generation;
                self->m_goalRevisionA=delivered.value(QStringLiteral("revisionA")).toInt(); self->m_goalRevisionB=delivered.value(QStringLiteral("revisionB")).toInt(); self->m_goalGeneration=delivered.value(QStringLiteral("goalGeneration")).toInt();
            } else { self->m_goalSelection.clear(); self->m_goalRevisionA=self->m_goalRevisionB=self->m_goalGeneration=-1; }
            self->m_contestReportBusy=false; emit self->contestReportBusyChanged(); emit self->contestGoalsFinished(delivered);
        },Qt::QueuedConnection);
    });
    return true;
}

bool ActivationController::startContestFindings(const QUrl &a, const QUrl &b, const QString &contest, const QString &fromA, const QString &toA, const QString &fromB, const QString &toB)
{
    if (m_contestReportBusy || !a.isLocalFile() || !b.isLocalFile() || a.toLocalFile().isEmpty() || b.toLocalFile().isEmpty() ||
        !m_ctx.db || !m_ctx.db->isOpen() || m_ctx.db->path().isEmpty() || m_ctx.db->path()==QLatin1String(":memory:") ||
        contest.size()>64 || fromA.size()>10 || toA.size()>10 || fromB.size()>10 || toB.size()>10 || m_ctx.db->identity().isEmpty()) return false;
    const QString path=m_ctx.db->path(), identity=m_ctx.db->identity(); const quint64 generation=m_ctx.db->openGeneration();
    m_contestReportBusy=true; emit contestReportBusyChanged(); QPointer<ActivationController> self(this);
    m_contestReportPool.start([self,path,identity,generation,a,b,contest,fromA,toA,fromB,toB] {
        QVariantMap result{{"contest",contest},{"fromA",fromA},{"toA",toA},{"fromB",fromB},{"toB",toB},
                           {"fileA",a.toString()},{"fileB",b.toString()},{"nameA",QFileInfo(a.toLocalFile()).fileName()},{"nameB",QFileInfo(b.toLocalFile()).fileName()}};
        auto read=[](const QUrl &url) {
            core::contestreport::Report report; QFile f(url.toLocalFile());
            if (!QFileInfo(f).isFile() || !f.open(QIODevice::ReadOnly)) { report.error=tr("Cannot open the selected report file."); return report; }
            const auto bytes=f.read(2*1024*1024+1);
            if (f.error()!=QFileDevice::NoError) { report.error=tr("Could not read the complete report file."); return report; }
            return core::contestreport::parseCqWw(bytes);
        };
        const auto ra=read(a), rb=read(b);
        const auto c=core::contestcomparison::compareFindings(ra,rb,contest,QDate::fromString(fromA,Qt::ISODate),QDate::fromString(toA,Qt::ISODate),QDate::fromString(fromB,Qt::ISODate),QDate::fromString(toB,Qt::ISODate));
        result.insert("error",!ra.error.isEmpty()?ra.error:!rb.error.isEmpty()?rb.error:c.error);
        result.insert("nilA",c.nilA); result.insert("nilB",c.nilB); result.insert("callA",c.callA); result.insert("callB",c.callB);
        result.insert("exchangeA",c.exchangeA); result.insert("exchangeB",c.exchangeB); result.insert("unknownA",c.unknownA); result.insert("unknownB",c.unknownB);
        result.insert("outsideA",c.outsideA); result.insert("outsideB",c.outsideB); result.insert("ownCallA",c.ownCallA); result.insert("ownCallB",c.ownCallB);
        result.insert("contestA",c.contestA); result.insert("contestB",c.contestB);
        if (!self) return;
        QMetaObject::invokeMethod(self.data(),[self,path,identity,generation,result] {
            if (!self) return; QVariantMap delivered=result;
            if (!self->m_ctx.db || !self->m_ctx.db->isOpen() || self->m_ctx.db->path()!=path || self->m_ctx.db->openGeneration()!=generation || self->m_ctx.db->identity()!=identity)
                delivered={{"error",tr("The log changed; compare again.")}};
            self->m_contestReportBusy=false; emit self->contestReportBusyChanged(); emit self->contestFindingsFinished(delivered);
        },Qt::QueuedConnection);
    });
    return true;
}

bool ActivationController::startContestComparison(const QString &contest, const QString &fromA, const QString &toA, const QString &fromB, const QString &toB)
{
    if (m_contestReportBusy || !m_ctx.db || !m_ctx.db->isOpen() || m_ctx.db->path().isEmpty() ||
        m_ctx.db->path()==QLatin1String(":memory:") || contest.size()>64 || fromA.size()>10 || toA.size()>10 || fromB.size()>10 || toB.size()>10) return false;
    const QString path=m_ctx.db->path(), identity=m_ctx.db->identity(); const quint64 generation=m_ctx.db->openGeneration();
    if (identity.isEmpty()) return false;
    m_contestReportBusy=true; emit contestReportBusyChanged(); QPointer<ActivationController> self(this);
    m_contestReportPool.start([self,path,identity,generation,contest,fromA,toA,fromB,toB] {
        QVariantMap result{{QStringLiteral("contest"),contest},{QStringLiteral("fromA"),fromA},{QStringLiteral("toA"),toA},
                           {QStringLiteral("fromB"),fromB},{QStringLiteral("toB"),toB}};
        LogDatabase db;
        if (!db.openReadOnly(path)) result.insert(QStringLiteral("error"),db.lastError());
        else if (db.identity()!=identity) result.insert(QStringLiteral("error"),tr("The reviewed log identity changed."));
        else {
            const auto compared=core::contestcomparison::compare(db.connection(),contest,QDate::fromString(fromA,Qt::ISODate),QDate::fromString(toA,Qt::ISODate),
                                                                 QDate::fromString(fromB,Qt::ISODate),QDate::fromString(toB,Qt::ISODate));
            result.insert(QStringLiteral("error"),compared.error); result.insert(QStringLiteral("totalA"),compared.totalA);
            result.insert(QStringLiteral("totalB"),compared.totalB); result.insert(QStringLiteral("invalidTimestamp"),compared.invalidTimestamp);
            result.insert(QStringLiteral("rows"),compared.rows);
        }
        if (!self) return;
        QMetaObject::invokeMethod(self.data(),[self,path,identity,generation,result] {
            if (!self) return;
            QVariantMap delivered=result;
            if (!self->m_ctx.db || !self->m_ctx.db->isOpen() || self->m_ctx.db->path()!=path ||
                self->m_ctx.db->openGeneration()!=generation || self->m_ctx.db->identity()!=identity)
                delivered={{QStringLiteral("error"),tr("The log changed; compare again.")}};
            self->m_contestReportBusy=false; emit self->contestReportBusyChanged(); emit self->contestComparisonFinished(delivered);
        },Qt::QueuedConnection);
    });
    return true;
}

bool ActivationController::startContestReportCandidates(const QVariantMap& row, int toleranceMinutes)
{
    if (toleranceMinutes < 0 || toleranceMinutes > 5 || m_contestReportBusy || !m_ctx.db || !m_ctx.db->isOpen() || m_ctx.db->path().isEmpty() ||
        m_ctx.db->path()==QLatin1String(":memory:")) return false;
    const QString path=m_ctx.db->path(), identity=m_ctx.db->identity();
    const quint64 generation=m_ctx.db->openGeneration();
    m_contestCandidates.clear();
    if (identity.isEmpty()) return false;
    core::contestreport::Entry entry;
    entry.call=row.value(QStringLiteral("call")).toString(); entry.date=row.value(QStringLiteral("date")).toString();
    entry.time=row.value(QStringLiteral("time")).toString(); entry.frequencyKhz=row.value(QStringLiteral("frequency")).toString();
    entry.mode=row.value(QStringLiteral("mode")).toString(); entry.lineNumber=row.value(QStringLiteral("line")).toInt();
    entry.receivedExchange=row.value(QStringLiteral("receivedExchange")).toString();
    entry.kind=row.value(QStringLiteral("kind")).toString(); entry.correction=row.value(QStringLiteral("correction")).toString();
    m_contestReportBusy=true; emit contestReportBusyChanged(); QPointer<ActivationController> self(this);
    m_contestReportPool.start([self,path,identity,generation,entry,toleranceMinutes] {
        QVariantMap result{{QStringLiteral("line"),entry.lineNumber},{QStringLiteral("call"),entry.call},
                           {QStringLiteral("reportFrequency"),entry.frequencyKhz},{QStringLiteral("reportExchange"),entry.receivedExchange},
                           {QStringLiteral("toleranceMinutes"),toleranceMinutes}};
        LogDatabase db;
        if (!db.openReadOnly(path)) result.insert(QStringLiteral("error"),db.lastError());
        else if (db.identity()!=identity) result.insert(QStringLiteral("error"),tr("The reviewed log identity changed."));
        else {
            const auto found=core::contestreport::findCandidates(db,entry,toleranceMinutes); QVariantList candidates;
            for (const auto& item : found.candidates)
                candidates << QVariantMap{{QStringLiteral("id"),item.id},{QStringLiteral("uuid"),item.uuid},
                    {QStringLiteral("utc"),item.utc},{QStringLiteral("stationCall"),item.stationCall},
                    {QStringLiteral("stationSource"),item.stationSource},{QStringLiteral("revision"),item.revision},
                    {QStringLiteral("frequency"),item.frequencyKhz},{QStringLiteral("receivedExchange"),item.receivedExchange},
                    {QStringLiteral("contestId"),item.contestId},{QStringLiteral("call"),item.call},{QStringLiteral("matchBasis"),item.matchBasis}};
            result.insert(QStringLiteral("error"),found.error); result.insert(QStringLiteral("candidates"),candidates);
            result.insert(QStringLiteral("truncated"),found.truncated);
        }
        if (!self) return;
        QMetaObject::invokeMethod(self.data(),[self,path,identity,generation,result] {
            if (!self) return;
            QVariantMap delivered=result;
            if (!self->m_ctx.db || !self->m_ctx.db->isOpen() || self->m_ctx.db->path()!=path ||
                self->m_ctx.db->openGeneration()!=generation || self->m_ctx.db->identity()!=identity)
                delivered={{QStringLiteral("error"),tr("The log changed; search again.")}};
            if (delivered.value(QStringLiteral("error")).toString().isEmpty()) {
                self->m_contestCandidatesLogPath=path; self->m_contestCandidatesLogIdentity=identity;
                self->m_contestCandidatesLogGeneration=generation;
                for (const auto& value : delivered.value(QStringLiteral("candidates")).toList()) {
                    const auto candidate=value.toMap();
                    self->m_contestCandidates.insert(candidate.value(QStringLiteral("id")).toLongLong(),
                        {candidate.value(QStringLiteral("uuid")).toString(),candidate.value(QStringLiteral("revision")).toInt()});
                }
            }
            self->m_contestReportBusy=false; emit self->contestReportBusyChanged();
            emit self->contestReportCandidatesFinished(delivered);
        },Qt::QueuedConnection);
    });
    return true;
}

bool ActivationController::isContestReportCandidateCurrent(qint64 id, const QString& uuid, int revision) const
{
    if (!m_ctx.db || !m_ctx.db->isOpen() || m_ctx.db->path()!=m_contestCandidatesLogPath ||
        m_ctx.db->openGeneration()!=m_contestCandidatesLogGeneration || m_ctx.db->identity()!=m_contestCandidatesLogIdentity ||
        !m_contestCandidates.contains(id) || m_contestCandidates.value(id)!=qMakePair(uuid,revision)) return false;
    const auto current=m_ctx.db->meta(id);
    return current && !current->deleted && current->uuid==uuid && current->revision==revision;
}

bool ActivationController::startContestReportPreview(const QUrl& file)
{
    if (m_contestReportBusy || !file.isLocalFile() || file.toLocalFile().isEmpty()) return false;
    const QString path = file.toLocalFile();
    m_contestReportSnapshot.clear();
    m_contestCandidates.clear();
    const QString logPath = m_ctx.db && m_ctx.db->isOpen() ? m_ctx.db->path() : QString();
    const quint64 logGeneration = m_ctx.db ? m_ctx.db->openGeneration() : 0;
    const QString logIdentity = m_ctx.db && m_ctx.db->isOpen() ? m_ctx.db->identity() : QString();
    m_contestReportBusy = true; emit contestReportBusyChanged();
    QPointer<ActivationController> self(this);
    m_contestReportPool.start([self, path, logPath, logGeneration, logIdentity] {
        QVariantMap result;
        QByteArray snapshot;
        QFile input(path);
        if (!QFileInfo(path).isFile() || !input.open(QIODevice::ReadOnly)) {
            result.insert(QStringLiteral("error"), tr("Cannot open the selected report file."));
        } else {
            const QByteArray bytes = input.read(2 * 1024 * 1024 + 1);
            auto report = core::contestreport::parseCqWw(bytes);
            if (input.error() != QFileDevice::NoError) {
                report = {};
                report.error = tr("Could not read the complete report file.");
            }
            if (report.error.isEmpty()) snapshot = bytes;
            result = contestReportResult(report);
        }
        if (!self) return;
        QMetaObject::invokeMethod(self.data(), [self, result, snapshot, path, logPath, logGeneration, logIdentity] {
            if (!self) return;
            const bool sameLog = self->m_ctx.db && self->m_ctx.db->isOpen() &&
                self->m_ctx.db->path() == logPath && self->m_ctx.db->openGeneration() == logGeneration &&
                !logPath.isEmpty() && logPath != QLatin1String(":memory:") && !logIdentity.isEmpty() &&
                self->m_ctx.db->identity() == logIdentity;
            QVariantMap delivered = result;
            delivered.insert(QStringLiteral("archiveReady"), sameLog && !snapshot.isEmpty());
            if (sameLog && !snapshot.isEmpty()) {
                self->m_contestReportSnapshot = snapshot;
                self->m_contestReportLogPath = logPath;
                self->m_contestReportLogIdentity = logIdentity;
                self->m_contestReportLogGeneration = logGeneration;
                self->m_contestReportSourceName = QFileInfo(path).fileName();
            }
            self->m_contestReportBusy = false; emit self->contestReportBusyChanged();
            emit self->contestReportFinished(delivered);
        }, Qt::QueuedConnection);
    });
    return true;
}

bool ActivationController::startContestReportHistory(qint64 beforeId)
{
    if (m_contestReportBusy || beforeId < 0 || !m_ctx.db || !m_ctx.db->isOpen() ||
        m_ctx.db->path().isEmpty() || m_ctx.db->path() == QLatin1String(":memory:")) return false;
    const QString path=m_ctx.db->path(); const quint64 generation=m_ctx.db->openGeneration();
    m_contestReportHistoryIds.clear();
    m_contestReportBusy=true; emit contestReportBusyChanged();
    QPointer<ActivationController> self(this);
    m_contestReportPool.start([self,path,generation,beforeId] {
        QVariantMap result; QVariantList rows; QHash<qint64,QString> identities;
        LogDatabase db;
        if (!db.openReadOnly(path)) result.insert(QStringLiteral("error"),db.lastError());
        else {
            QSqlQuery q(db.connection());
            q.prepare(QStringLiteral("SELECT id,sha256,filename,contest,own_call,imported_at FROM contest_report_archive "
                                     "WHERE (?=0 OR id<?) ORDER BY id DESC LIMIT 101"));
            q.addBindValue(beforeId); q.addBindValue(beforeId);
            if (!q.exec()) result.insert(QStringLiteral("error"),q.lastError().text());
            else {
                while (q.next()) {
                    if (rows.size()==100) { result.insert(QStringLiteral("hasMore"),true); break; }
                    const qint64 id=q.value(0).toLongLong(); const QString sha=q.value(1).toString();
                    identities.insert(id,sha);
                    rows << QVariantMap{{QStringLiteral("id"),id},{QStringLiteral("sha256"),sha},
                        {QStringLiteral("filename"),q.value(2)},{QStringLiteral("contest"),q.value(3)},
                        {QStringLiteral("ownCall"),q.value(4)},{QStringLiteral("importedAt"),q.value(5)}};
                }
                result.insert(QStringLiteral("reports"),rows);
            }
        }
        if (!self) return;
        QMetaObject::invokeMethod(self.data(), [self,path,generation,result,identities] {
            if (!self) return;
            QVariantMap delivered=result;
            if (!self->m_ctx.db || !self->m_ctx.db->isOpen() || self->m_ctx.db->path()!=path ||
                self->m_ctx.db->openGeneration()!=generation) {
                delivered={{QStringLiteral("error"),tr("The log changed; reload report history.")}};
            } else if (result.value(QStringLiteral("error")).toString().isEmpty()) {
                self->m_contestReportHistoryIds=identities;
                self->m_contestReportHistoryPath=path;
                self->m_contestReportHistoryGeneration=generation;
            }
            self->m_contestReportBusy=false; emit self->contestReportBusyChanged();
            emit self->contestReportHistoryFinished(delivered);
        },Qt::QueuedConnection);
    });
    return true;
}

bool ActivationController::startArchivedContestReportPreview(qint64 id, const QString& sha256)
{
    if (m_contestReportBusy || !m_ctx.db || !m_ctx.db->isOpen() || id<=0 ||
        m_ctx.db->path()!=m_contestReportHistoryPath || m_ctx.db->openGeneration()!=m_contestReportHistoryGeneration ||
        !m_contestReportHistoryIds.contains(id) || m_contestReportHistoryIds.value(id)!=sha256) return false;
    const QString path=m_contestReportHistoryPath; const quint64 generation=m_contestReportHistoryGeneration;
    m_contestReportSnapshot.clear(); m_contestCandidates.clear(); m_contestReportBusy=true; emit contestReportBusyChanged();
    QPointer<ActivationController> self(this);
    m_contestReportPool.start([self,path,generation,id,sha256] {
        QVariantMap result; LogDatabase db;
        if (!db.openReadOnly(path)) result.insert(QStringLiteral("error"),db.lastError());
        else {
            QSqlQuery q(db.connection()); q.prepare(QStringLiteral("SELECT sha256,length(original),CASE WHEN length(original)<=2097152 THEN original END FROM contest_report_archive WHERE id=?")); q.addBindValue(id);
            if (!q.exec()) result.insert(QStringLiteral("error"),q.lastError().text());
            else if (!q.next()) result.insert(QStringLiteral("error"),tr("The archived report is no longer available."));
            else {
                const QByteArray bytes=q.value(2).toByteArray();
                const QString actual=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
                if (q.value(1).toLongLong()>2*1024*1024 || q.value(0).toString()!=sha256 || actual!=sha256)
                    result.insert(QStringLiteral("error"),tr("The archived report changed or is damaged."));
                else result=contestReportResult(core::contestreport::parseCqWw(bytes));
            }
        }
        result.insert(QStringLiteral("archived"),true);
        if (!self) return;
        QMetaObject::invokeMethod(self.data(), [self,path,generation,result] {
            if (!self) return;
            QVariantMap delivered=result;
            if (!self->m_ctx.db || !self->m_ctx.db->isOpen() || self->m_ctx.db->path()!=path ||
                self->m_ctx.db->openGeneration()!=generation)
                delivered={{QStringLiteral("error"),tr("The log changed; reload report history.")}};
            self->m_contestReportBusy=false; emit self->contestReportBusyChanged();
            emit self->contestReportFinished(delivered);
        },Qt::QueuedConnection);
    });
    return true;
}

static bool safeContestReportDestination(const QString& output, const QString& logPath)
{
    if (output.isEmpty() || logPath.isEmpty()) return false;
    const QFileInfo outInfo(output);
    const auto resolvedTarget=[](const QFileInfo& info) {
        const QString parent=info.dir().canonicalPath();
        return parent.isEmpty() ? info.absoluteFilePath() : QDir(parent).absoluteFilePath(info.fileName());
    };
    for (const QString& protectedPath : {logPath, logPath + QStringLiteral("-wal"),
                                          logPath + QStringLiteral("-shm"), logPath + QStringLiteral("-journal")}) {
        const QFileInfo protectedInfo(protectedPath);
        std::error_code ec;
        const bool same = std::filesystem::equivalent(std::filesystem::path(output.toStdString()),
                                                       std::filesystem::path(protectedPath.toStdString()), ec);
        if (outInfo.absoluteFilePath()==protectedInfo.absoluteFilePath() ||
            resolvedTarget(outInfo)==resolvedTarget(protectedInfo) || (!ec && same)) return false;
    }
    return true;
}

bool ActivationController::startContestReportExport(qint64 id, const QString& sha256, const QUrl& destination)
{
    if (m_contestReportBusy || !destination.isLocalFile() || !m_ctx.db || !m_ctx.db->isOpen() || id<=0 ||
        m_ctx.db->path()!=m_contestReportHistoryPath || m_ctx.db->openGeneration()!=m_contestReportHistoryGeneration ||
        !m_contestReportHistoryIds.contains(id) || m_contestReportHistoryIds.value(id)!=sha256 ||
        !safeContestReportDestination(destination.toLocalFile(),m_contestReportHistoryPath)) return false;
    const QString path=m_contestReportHistoryPath, output=destination.toLocalFile();
    m_contestReportBusy=true; emit contestReportBusyChanged(); QPointer<ActivationController> self(this);
    m_contestReportPool.start([self,path,output,id,sha256] {
        QString error; QByteArray bytes; LogDatabase db;
        if (!db.openReadOnly(path)) error=db.lastError();
        else {
            QSqlQuery q(db.connection()); q.prepare(QStringLiteral("SELECT sha256,length(original),CASE WHEN length(original)<=2097152 THEN original END FROM contest_report_archive WHERE id=?")); q.addBindValue(id);
            if (!q.exec()) error=q.lastError().text();
            else if (!q.next()) error=tr("The archived report is no longer available.");
            else {
                bytes=q.value(2).toByteArray();
                const QString actual=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
                if (q.value(1).toLongLong()>2*1024*1024 || q.value(0).toString()!=sha256 || actual!=sha256)
                    error=tr("The archived report changed or is damaged.");
            }
        }
        if (error.isEmpty() && !safeContestReportDestination(output,path)) error=tr("Choose a file other than the log database and its SQLite files.");
        if (error.isEmpty()) {
            QSaveFile file(output); file.setDirectWriteFallback(false);
            if (!file.open(QIODevice::WriteOnly)) error=file.errorString();
            else if (file.write(bytes)!=bytes.size()) { error=file.errorString(); file.cancelWriting(); }
            else if (!file.commit()) error=file.errorString();
        }
        if (!self) return;
        QMetaObject::invokeMethod(self.data(), [self,error,output] {
            if (!self) return;
            self->m_contestReportBusy=false; emit self->contestReportBusyChanged();
            emit self->contestReportExportFinished(error.isEmpty(),error.isEmpty() ? tr("Exported to %1.").arg(output) : error);
        },Qt::QueuedConnection);
    });
    return true;
}

bool ActivationController::archiveContestReportPreview()
{
    if (m_contestReportBusy || m_contestReportSnapshot.isEmpty() || !m_ctx.db || !m_ctx.db->isOpen() ||
        m_ctx.db->path() != m_contestReportLogPath || m_ctx.db->openGeneration() != m_contestReportLogGeneration ||
        m_contestReportLogIdentity.isEmpty() || m_ctx.db->identity()!=m_contestReportLogIdentity)
        return false;
    const QString path=m_contestReportLogPath, filename=m_contestReportSourceName, identity=m_contestReportLogIdentity;
    const QByteArray snapshot=m_contestReportSnapshot;
    m_contestReportBusy=true; emit contestReportBusyChanged();
    QPointer<ActivationController> self(this);
    m_contestReportPool.start([self, path, filename, snapshot, identity] {
        QString error; qint64 id=0;
        LogDatabase db;
        if (!db.openExistingReadWrite(path)) error=db.lastError();
        else id=db.archiveContestReport(snapshot,filename,&error,identity);
        if (!self) return;
        QMetaObject::invokeMethod(self.data(), [self, id, error, path] {
            if (!self) return;
            self->m_contestReportBusy=false; emit self->contestReportBusyChanged();
            emit self->contestReportArchiveFinished(id>0, id>0 ? tr("Saved in %1 (report %2).").arg(QFileInfo(path).fileName()).arg(id)
                                                               : (error.isEmpty() ? tr("Could not archive the report.") : error));
        }, Qt::QueuedConnection);
    });
    return true;
}

bool ActivationController::startPartitionPreview()
{
    return startRunPartitionPreview(m_session.active ? m_session.sessionUuid : QString());
}

bool ActivationController::startRunPartitionPreview(const QString& requestedUuid)
{
    if (m_previewBusy) return false;
    m_previewResult.clear(); m_previewSessionUuid.clear();
    if (requestedUuid.isEmpty() || !m_ctx.db || !m_ctx.db->isOpen() ||
        m_ctx.db->path().isEmpty() || m_ctx.db->path() == QLatin1String(":memory:")) {
        m_previewResult = {{QStringLiteral("error"), tr("A tracked session and an open file-backed log are required.")}};
        emit previewChanged(); return false;
    }
    const QString path = m_ctx.db->path();
    const QString connectionName = m_ctx.db->connection().connectionName();
    const QString uuid = requestedUuid;
    const QString cataloguePath = m_sotaCatalogue->path();
    const QString potaCataloguePath = m_potaCatalogue->path();
    m_previewSessionUuid = uuid;
    const quint64 generation = m_previewGeneration;
    m_previewCancel = std::make_shared<std::atomic_bool>(false);
    const auto cancelled = m_previewCancel;
    m_previewBusy = true; emit previewChanged();
    QPointer<ActivationController> self(this);
    m_previewPool.start([self, path, connectionName, uuid, cataloguePath, potaCataloguePath, generation, cancelled] {
        QVariantMap result;
        const QString connection = QStringLiteral("activation-preview-") + QUuid::createUuid().toString();
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
            db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=100"));
            db.setDatabaseName(path);
            if (!db.open()) result[QStringLiteral("error")] = tr("Cannot open the log for preview.");
            else result = scanPartitionPreview(db, uuid, cancelled, true, {}, nullptr, cataloguePath, potaCataloguePath);
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
        if (!self) return;
        QMetaObject::invokeMethod(self.data(), [self, path, connectionName, uuid, generation, cancelled, result] {
            if (!self) return;
            self->m_previewBusy = false; self->m_previewCancel.reset();
            const bool same = self->m_ctx.db && self->m_ctx.db->isOpen() && self->m_ctx.db->path() == path &&
                self->m_ctx.db->connection().connectionName() == connectionName &&
                self->m_previewSessionUuid == uuid && self->m_previewGeneration == generation;
            self->m_previewResult = !same ? QVariantMap{{QStringLiteral("error"), tr("The log or session changed; recalculate the preview.")}}
                : cancelled->load() ? QVariantMap{{QStringLiteral("error"), tr("Preview cancelled.")}} : result;
            const bool success = self->m_previewResult.value(QStringLiteral("error")).toString().isEmpty();
            emit self->previewChanged(); emit self->previewFinished(success);
        }, Qt::QueuedConnection);
    });
    return true;
}

void ActivationController::cancelPartitionExport()
{
    if (m_partitionExportCancel) m_partitionExportCancel->store(true);
}

bool ActivationController::startPartitionExport(const QVariantMap& group, const QUrl& destination)
{
    if (m_partitionExportBusy || m_previewBusy || !destination.isLocalFile() || m_previewSessionUuid.isEmpty() ||
        !m_ctx.db || !m_ctx.db->isOpen() || m_ctx.db->path() == QLatin1String(":memory:")) return false;
    const QString program = group.value(QStringLiteral("program")).toString();
    if (program != QLatin1String("POTA") && program != QLatin1String("WWFF") &&
        program != QLatin1String("SOTA") && program != QLatin1String("IOTA")) return false;
    bool found = false;
    for (const auto& item : m_previewResult.value(QStringLiteral("groups")).toList())
        if (item.toMap() == group) { found = true; break; }
    if (!found) return false;
    const QString path = m_ctx.db->path();
    const QString output = destination.toLocalFile();
    if (!safeContestReportDestination(output, path)) return false;
    const QString uuid = m_previewSessionUuid;
    const QString key = QString::fromUtf8(QJsonDocument(QJsonArray{program, group.value(QStringLiteral("reference")).toString(),
        group.value(QStringLiteral("day")).toString(), group.value(QStringLiteral("operator")).toString(), group.value(QStringLiteral("grid")).toString()}).toJson(QJsonDocument::Compact));
    const QString expected = group.value(QStringLiteral("fingerprint")).toString();
    if (expected.isEmpty()) return false;
    m_partitionExportCancel = std::make_shared<std::atomic_bool>(false);
    const auto cancelled = m_partitionExportCancel;
    m_partitionExportBusy = true; emit partitionExportChanged();
    QPointer<ActivationController> self(this);
    m_partitionExportPool.start([self, path, output, uuid, key, group, expected, program, cancelled] {
        QString error;
        LogDatabase db;
        if (!db.openReadOnly(path)) error = tr("Cannot open the log for export.");
        else if (!db.connection().transaction()) error = tr("Cannot open a consistent export snapshot.");
        else {
            QList<qint64> ids;
            const QVariantMap preview = scanPartitionPreview(db.connection(), uuid, cancelled, false, key, &ids);
            error = preview.value(QStringLiteral("error")).toString();
            bool same = false;
            for (const auto& value : preview.value(QStringLiteral("groups")).toList()) {
                const QVariantMap current = value.toMap();
                if (current.value(QStringLiteral("fingerprint")).toString() == expected &&
                    current.value(QStringLiteral("program")) == group.value(QStringLiteral("program")) &&
                    current.value(QStringLiteral("reference")) == group.value(QStringLiteral("reference")) &&
                    current.value(QStringLiteral("day")) == group.value(QStringLiteral("day")) &&
                    current.value(QStringLiteral("operator")) == group.value(QStringLiteral("operator")) &&
                    current.value(QStringLiteral("grid")) == group.value(QStringLiteral("grid"))) { same = true; break; }
            }
            if (error.isEmpty() && (!same || ids.isEmpty() || ids.size() != group.value(QStringLiteral("count")).toInt()))
                error = tr("The selected group changed; calculate the preview again.");
            AdifDocument doc;
            if (error.isEmpty()) {
                doc = adif::parse(db.exportAdif(ids, QCoreApplication::applicationVersion()));
                if (doc.records.size() != ids.size()) error = tr("Cannot read every QSO in the selected group.");
            }
            if (error.isEmpty()) {
                const QString ref = group.value(QStringLiteral("reference")).toString();
                for (AdifRecord& record : doc.records) {
                    if (program == QLatin1String("POTA") || program == QLatin1String("WWFF")) {
                        record.set(program == QLatin1String("POTA") ? QStringLiteral("MY_POTA_REF") : QStringLiteral("MY_WWFF_REF"), ref);
                        record.set(QStringLiteral("MY_SIG"), program);
                        record.set(QStringLiteral("MY_SIG_INFO"), program == QLatin1String("POTA") ? ref.section(QLatin1Char('@'), 0, 0) : ref);
                        if (program == QLatin1String("POTA") && record.value(QStringLiteral("SIG")).trimmed().isEmpty()
                            && record.value(QStringLiteral("SIG_INFO")).trimmed().isEmpty()) {
                            const QString other = record.value(QStringLiteral("POTA_REF")).trimmed().toUpper();
                            QVariantMap normalized;
                            if (!other.contains(QLatin1Char(',')) && !other.isEmpty()
                                && Activation::normalizePrograms({{QStringLiteral("pota"), other}}, &normalized)) {
                                record.set(QStringLiteral("SIG"), QStringLiteral("POTA"));
                                record.set(QStringLiteral("SIG_INFO"), other.section(QLatin1Char('@'), 0, 0));
                            }
                        }
                    } else {
                        record.set(program == QLatin1String("SOTA") ? QStringLiteral("MY_SOTA_REF") : QStringLiteral("MY_IOTA"), ref);
                    }
                }
            }
            if (!db.connection().commit() && error.isEmpty()) error = tr("Cannot complete the export snapshot.");
            if (cancelled->load() && error.isEmpty()) error = tr("Export cancelled.");
            if (error.isEmpty() && !safeContestReportDestination(output, path))
                error = tr("Choose a file other than the log database and its SQLite files.");
            if (error.isEmpty()) {
                QSaveFile file(output);
                file.setDirectWriteFallback(false);
                if (!file.open(QIODevice::WriteOnly)) error = file.errorString();
                else {
                    const QByteArray data = adif::writeDocument(doc);
                    if (file.write(data) != data.size() || cancelled->load()) { file.cancelWriting(); error = tr("Cannot write the export or export cancelled."); }
                    else if (!file.commit()) error = file.errorString();
                }
            }
        }
        if (!self) return;
        QMetaObject::invokeMethod(self.data(), [self, error] {
            if (!self) return;
            self->m_partitionExportBusy = false; self->m_partitionExportCancel.reset(); emit self->partitionExportChanged();
            emit self->partitionExportFinished(error.isEmpty(), error);
        }, Qt::QueuedConnection);
    });
    return true;
}

QString ActivationController::suggestedFileName() const
{
    if (!m_session.active)
        return {};
    const QString call = m_ctx.stationCall ? m_ctx.stationCall() : QString();
    const QDate day = m_session.startedAt.isValid() ? m_session.startedAt.date() : QDate::currentDate();
    return m_session.exportFileName(call.isEmpty() ? QStringLiteral("DECODXLOG") : call, day);
}

QVariantMap ActivationController::cabrilloDefaults() const
{
    const QString call = m_ctx.stationCall ? m_ctx.stationCall() : QString();
    return QVariantMap{
        {QStringLiteral("contest"), m_session.contestId.isEmpty() ? m_session.name : m_session.contestId},
        {QStringLiteral("callsign"), call},
        {QStringLiteral("gridLocator"), m_session.myGrid.isEmpty() && m_ctx.stationGrid ? m_ctx.stationGrid()
                                                                                        : m_session.myGrid},
        {QStringLiteral("operators"), call},
        {QStringLiteral("categoryOperator"), QStringLiteral("SINGLE-OP")},
        {QStringLiteral("categoryAssisted"), QStringLiteral("NON-ASSISTED")},
        {QStringLiteral("categoryBand"), m_session.band.isEmpty() ? QStringLiteral("ALL")
                                                                  : m_session.band.toUpper()},
        {QStringLiteral("categoryMode"), QStringLiteral("MIXED")},
        {QStringLiteral("categoryPower"), QStringLiteral("LOW")},
        {QStringLiteral("categoryTransmitter"), QStringLiteral("ONE")},
    };
}

QString ActivationController::exportCabrillo(const QUrl& url, const QVariantMap& info)
{
    const QVariantList ids = qsoIds();
    if (ids.isEmpty())
        return tr("No QSO in this session yet");

    auto text = [&info](const char* key) { return info.value(QLatin1String(key)).toString().trimmed(); };
    const QString contestId = text("contest").isEmpty() ? m_session.contestId : text("contest");
    if (m_session.kind == Activation::Kind::Contest && !contestrules::forId(contestId).valid)
        return tr("Cabrillo export is unavailable: this contest has no verified rule sheet");
    cabrillo::Info header;
    header.contest = contestId;
    header.callsign = text("callsign");
    if (header.callsign.isEmpty() && m_ctx.stationCall)
        header.callsign = m_ctx.stationCall();
    if (!text("categoryOperator").isEmpty())
        header.categoryOperator = text("categoryOperator");
    if (!text("categoryAssisted").isEmpty())
        header.categoryAssisted = text("categoryAssisted");
    if (!text("categoryBand").isEmpty())
        header.categoryBand = text("categoryBand");
    if (!text("categoryMode").isEmpty())
        header.categoryMode = text("categoryMode");
    if (!text("categoryPower").isEmpty())
        header.categoryPower = text("categoryPower");
    if (!text("categoryTransmitter").isEmpty())
        header.categoryTransmitter = text("categoryTransmitter");
    header.categoryOverlay = text("categoryOverlay");
    header.gridLocator = text("gridLocator");
    header.location = text("location");
    header.club = text("club");
    header.name = text("name");
    header.email = text("email");
    header.operators = text("operators");
    header.claimedScore = info.value(QStringLiteral("claimedScore")).toLongLong();
    const QString address = info.value(QStringLiteral("address")).toString();
    if (!address.trimmed().isEmpty())
        header.address = address.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    const QString soapbox = info.value(QStringLiteral("soapbox")).toString();
    if (!soapbox.trimmed().isEmpty())
        header.soapbox = soapbox.split(QLatin1Char('\n'), Qt::SkipEmptyParts);

    QList<AdifRecord> records;
    for (const QVariant& v : ids) {
        if (const auto record = m_ctx.db->record(v.toLongLong()))
            records << *record;
    }

    QString error;
    const QByteArray text_out = cabrillo::write(header, records, &error);
    if (text_out.isEmpty())
        return error.isEmpty() ? tr("Nothing to write") : error;

    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (!safeContestReportDestination(path, m_ctx.db->path()))
        return tr("Choose a file other than the log database and its SQLite files.");
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        return tr("Cannot write %1").arg(path);
    if (file.write(text_out) != text_out.size()) { file.cancelWriting(); return file.errorString(); }
    if (!file.commit()) return file.errorString();
    if (m_ctx.activity) {
        m_ctx.activity(QStringLiteral("ACT"),
                       tr("Cabrillo written: %1 (%n QSO)", nullptr, static_cast<int>(records.size())).arg(path),
                       QStringLiteral("success"));
    }
    return {};
}

QVariantList ActivationController::recentQsos(int limit) const
{
    QVariantList out;
    if (!m_session.active || !m_ctx.db || !m_ctx.db->isOpen() || !m_session.startedAt.isValid())
        return out;
    QSqlQuery q(m_ctx.db->connection());
    q.setForwardOnly(true);
    q.prepare(QStringLiteral(
        "SELECT id, call, qso_datetime_on, band, "
        "CASE WHEN IFNULL(submode, '') = '' OR mode = 'SSB' THEN mode ELSE submode END, "
        "rst_sent, rst_rcvd, country FROM qso "
        "WHERE deleted = 0 AND %2%1 ORDER BY qso_datetime_on DESC, id DESC LIMIT ?").arg(activeSessionPredicate(m_session.sessionUuid), activeSessionStartPredicate(m_session.sessionUuid)));
    q.addBindValue(m_session.startedAt.toString(Qt::ISODate));
    if (!m_session.sessionUuid.isEmpty()) q.addBindValue(m_session.sessionUuid);
    q.addBindValue(qMax(1, limit));
    if (!q.exec())
        return out;
    while (q.next()) {
        const QDateTime when = QDateTime::fromString(q.value(2).toString(), Qt::ISODate);
        out << QVariantMap{{QStringLiteral("id"), q.value(0).toLongLong()},
                           {QStringLiteral("call"), q.value(1).toString()},
                           {QStringLiteral("time"), when.toString(QStringLiteral("hh:mm"))},
                           {QStringLiteral("utc"), q.value(2).toString()},
                           {QStringLiteral("band"), q.value(3).toString()},
                           {QStringLiteral("mode"), q.value(4).toString()},
                           {QStringLiteral("rstSent"), q.value(5).toString()},
                           {QStringLiteral("rstRcvd"), q.value(6).toString()},
                           {QStringLiteral("country"), q.value(7).toString()}};
    }
    return out;
}

QVariantMap ActivationController::rate() const
{
    QVariantMap out{{QStringLiteral("last10"), 0}, {QStringLiteral("last60"), 0},
                    {QStringLiteral("perHour10"), 0}, {QStringLiteral("perHour60"), 0},
                    {QStringLiteral("dxcc"), 0}, {QStringLiteral("grids"), 0}};
    if (!m_session.active || !m_ctx.db || !m_ctx.db->isOpen() || !m_session.startedAt.isValid())
        return out;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QString start = m_session.startedAt.toString(Qt::ISODate);

    auto since = [this, &start](const QDateTime& from) {
        QSqlQuery q(m_ctx.db->connection());
        q.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM qso WHERE deleted = 0 AND %2 AND qso_datetime_on >= ?%1").arg(activeSessionPredicate(m_session.sessionUuid), activeSessionStartPredicate(m_session.sessionUuid)));
        q.addBindValue(start);
        q.addBindValue(from.toString(Qt::ISODate));
        if (!m_session.sessionUuid.isEmpty()) q.addBindValue(m_session.sessionUuid);
        return q.exec() && q.next() ? q.value(0).toInt() : 0;
    };
    const int last10 = since(now.addSecs(-600));
    const int last60 = since(now.addSecs(-3600));
    out[QStringLiteral("last10")] = last10;
    out[QStringLiteral("last60")] = last60;
    // Il ritmo: quello che verrebbe fuori se si andasse avanti cosi' per un'ora.
    out[QStringLiteral("perHour10")] = last10 * 6;
    out[QStringLiteral("perHour60")] = last60;

    QSqlQuery mult(m_ctx.db->connection());
    mult.prepare(QStringLiteral(
        "SELECT COUNT(DISTINCT dxcc), COUNT(DISTINCT substr(gridsquare, 1, 4)) FROM qso "
        "WHERE deleted = 0 AND %2%1").arg(activeSessionPredicate(m_session.sessionUuid), activeSessionStartPredicate(m_session.sessionUuid)));
    mult.addBindValue(start);
    if (!m_session.sessionUuid.isEmpty()) mult.addBindValue(m_session.sessionUuid);
    if (mult.exec() && mult.next()) {
        out[QStringLiteral("dxcc")] = mult.value(0).toInt();
        out[QStringLiteral("grids")] = mult.value(1).toInt();
    }
    return out;
}

QString ActivationController::exportAdif(const QUrl& url)
{
    const QVariantList ids = qsoIds();
    if (ids.isEmpty())
        return tr("No QSO in this session yet");
    QList<qint64> list;
    for (const QVariant& v : ids)
        list << v.toLongLong();
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (!safeContestReportDestination(path, m_ctx.db->path()))
        return tr("Choose a file other than the log database and its SQLite files.");
    const QByteArray data = m_ctx.db->exportAdif(list, QCoreApplication::applicationVersion());
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        return file.errorString();
    if (file.write(data) != data.size()) { file.cancelWriting(); return file.errorString(); }
    if (!file.commit()) return file.errorString();
    if (m_ctx.activity)
        m_ctx.activity(QStringLiteral("ACT"), tr("%n QSO of the session → %1", nullptr, static_cast<int>(list.size())).arg(path),
                       QStringLiteral("success"));
    return {};
}

QVariantList ActivationController::contests(const QString& search) const
{
    QVariantList out;
    for (const core::Contest& c : core::contests::search(search)) {
        out << QVariantMap{
            {QStringLiteral("id"), c.id},
            {QStringLiteral("name"), c.name},
            // Quello che si legge nell'elenco: il nome, e l'id fra parentesi
            // perche' e' quello che finisce nel log.
            {QStringLiteral("label"), QStringLiteral("%1  (%2)").arg(c.name, c.id)},
        };
    }
    return out;
}

QString ActivationController::contestName(const QString& id) const
{
    return core::contests::nameFor(id);
}

QVariantMap ActivationController::score() const
{
    decolog::StartupSpan trace("ActivationController::score");
    if (!m_score.valid)
        buildScore();
    return m_score.score;
}

void ActivationController::buildScore() const
{
    const core::ContestRules rules = currentRules();
    QVariantMap out{
        {QStringLiteral("valid"), rules.valid},
        {QStringLiteral("contestId"), m_session.contestId},
        {QStringLiteral("exchangeLabel"), rules.valid ? rules.exchangeLabel : tr("Exchange")},
        {QStringLiteral("source"), rules.source},
        {QStringLiteral("submitUrl"), rules.submitUrl},
        {QStringLiteral("submitDays"), rules.submitDays},
        {QStringLiteral("submitHours"), rules.submitHours},
        {QStringLiteral("points"), 0},
        {QStringLiteral("multipliers"), 0},
        {QStringLiteral("score"), 0},
        {QStringLiteral("bands"), QVariantList{}},
    };
    m_score.valid = true;
    m_score.multipliers.clear();
    m_score.score = out;
    if (!rules.valid || !m_session.active || !m_ctx.db || !m_ctx.db->isOpen())
        return;

    const core::ContestStation me = m_ctx.station ? m_ctx.station() : core::ContestStation{};
    int points = 0;
    QSet<QString> mults;
    // Per banda: quanti QSO, quanti punti, quanti moltiplicatori nuovi.
    QMap<QString, QVariantMap> perBand;

    for (const core::ContestQso& qso : sessionQsos()) {
        const int value = core::contestrules::points(rules, qso, me);
        points += value;

        QVariantMap& band = perBand[qso.band];
        band[QStringLiteral("band")] = qso.band;
        band[QStringLiteral("qsos")] = band.value(QStringLiteral("qsos")).toInt() + 1;
        band[QStringLiteral("points")] = band.value(QStringLiteral("points")).toInt() + value;

        for (const QString& key : core::contestrules::multipliers(rules, qso, me)) {
            if (mults.contains(key))
                continue;
            mults.insert(key);
            band[QStringLiteral("multipliers")] = band.value(QStringLiteral("multipliers")).toInt() + 1;
        }
    }

    QVariantList bands;
    for (const QVariantMap& band : std::as_const(perBand))
        bands << band;
    // Il tempo in aria: il WPX da singolo operatore ne concede 36 ore su 48.
    QList<QDateTime> times;
    for (const core::ContestQso& qso : sessionQsos()) {
        if (qso.when.isValid())
            times << qso.when;
    }
    const core::OperatingTime onAir = core::contestrules::operatingTime(rules, times);
    out[QStringLiteral("onMinutes")] = onAir.onMinutes;
    out[QStringLiteral("offMinutes")] = onAir.offMinutes;
    out[QStringLiteral("breaks")] = onAir.breaks;
    out[QStringLiteral("maxMinutes")] = onAir.maxMinutes;
    out[QStringLiteral("minOffMinutes")] = rules.minOffMinutes;
    out[QStringLiteral("overTime")] = onAir.over();
    out[QStringLiteral("points")] = points;
    out[QStringLiteral("multipliers")] = mults.size();
    out[QStringLiteral("score")] = static_cast<qint64>(points) * mults.size();
    out[QStringLiteral("bands")] = bands;
    m_score.score = out;
    m_score.multipliers = mults;
}

QList<core::ContestQso> ActivationController::sessionQsos() const
{
    QList<core::ContestQso> out;
    if (!m_session.active || !m_ctx.db || !m_ctx.db->isOpen() || !m_session.startedAt.isValid())
        return out;
    // Una query sola: leggere i QSO uno per uno, con tutto il loro ADIF, costava
    // decine di millisecondi per ogni cento QSO della gara.
    QSqlQuery q(m_ctx.db->connection());
    q.setForwardOnly(true);
    q.prepare(QStringLiteral(
        "SELECT call, band, mode, IFNULL(submode, ''), IFNULL(dxcc, 0), IFNULL(cont, ''), "
        "IFNULL(cqz, 0), IFNULL(ituz, 0), IFNULL(adif_extra, ''), IFNULL(state, ''), qso_datetime_on "
        "FROM qso WHERE deleted = 0 AND %2%1 ORDER BY qso_datetime_on, id").arg(activeSessionPredicate(m_session.sessionUuid), activeSessionStartPredicate(m_session.sessionUuid)));
    q.addBindValue(m_session.startedAt.toString(Qt::ISODate));
    if (!m_session.sessionUuid.isEmpty()) q.addBindValue(m_session.sessionUuid);
    if (!q.exec())
        return out;
    while (q.next()) {
        core::ContestQso qso;
        qso.call = q.value(0).toString();
        qso.band = q.value(1).toString().toLower();
        qso.mode = q.value(2).toString().toUpper();
        const QString submode = q.value(3).toString().toUpper();
        if (qso.mode == QLatin1String("MFSK") && !submode.isEmpty())
            qso.mode = submode;
        qso.dxcc = q.value(4).toInt();
        qso.continent = q.value(5).toString().toUpper();
        qso.cqZone = q.value(6).toInt();
        qso.ituZone = q.value(7).toInt();
        qso.state = q.value(9).toString();
        qso.when = QDateTime::fromString(q.value(10).toString(), Qt::ISODate);
        // Lo scambio ricevuto non ha una colonna: sta fra i campi ADIF in piu'.
        const QString extra = q.value(8).toString();
        if (!extra.isEmpty()) {
            const QJsonObject fields = QJsonDocument::fromJson(extra.toUtf8()).object();
            qso.exchange = fields.value(QStringLiteral("SRX_STRING")).toString();
            if (qso.exchange.isEmpty())
                qso.exchange = fields.value(QStringLiteral("SRX")).toString();
        }
        // Un QSO scritto in fretta non ha il paese: lo si chiede al cty.csv,
        // invece di contare zero punti per un dato che si puo' sapere.
        if ((qso.dxcc == 0 || qso.continent.isEmpty()) && m_ctx.locate) {
            const core::ContestStation found = m_ctx.locate(qso.call);
            if (qso.dxcc == 0)
                qso.dxcc = found.dxcc;
            if (qso.continent.isEmpty())
                qso.continent = found.continent;
            if (qso.cqZone == 0)
                qso.cqZone = found.cqZone;
            if (qso.ituZone == 0)
                qso.ituZone = found.ituZone;
        }
        out << qso;
    }
    return out;
}


QString ActivationController::checkExchange(const QString& exchange) const
{
    return core::contestrules::checkExchange(currentRules(), exchange);
}

namespace {
// Il moltiplicatore come si legge in una colonna stretta del cluster: "Z5"
// per una zona, "DXCC" per un paese nuovo, la sigla per il resto. Le chiavi
// interne ("zona 5", "paese 291") non dicevano niente a chi le leggeva.
QString shortMultiplier(const QString& key)
{
    const QString kind = key.section(QLatin1Char(' '), 0, 0);
    const QString value = key.section(QLatin1Char(' '), 1);
    if (kind == QLatin1String("zona"))
        return QStringLiteral("Z") + value;
    if (kind == QLatin1String("paese"))
        return QStringLiteral("DXCC");
    if (kind == QLatin1String("hq") || kind == QLatin1String("prov") || kind == QLatin1String("sez")
        || kind == QLatin1String("qth"))
        return value;
    return key;
}
} // namespace

namespace {

// Le bande della gara, nell'ordine dell'operatore: quelle della scheda, o le
// sei dei contest HF quando la scheda non le dice.
QStringList contestBands(const core::ContestRules& rules)
{
    if (!rules.bands.isEmpty())
        return rules.bands;
    return {QStringLiteral("160m"), QStringLiteral("80m"), QStringLiteral("40m"),
            QStringLiteral("20m"), QStringLiteral("15m"), QStringLiteral("10m")};
}

QString kindLabel(const QString& kind)
{
    if (kind == QLatin1String("zona")) return QCoreApplication::translate("ActivationController", "Zones");
    if (kind == QLatin1String("paese")) return QCoreApplication::translate("ActivationController", "Countries");
    if (kind == QLatin1String("prefisso")) return QCoreApplication::translate("ActivationController", "Prefixes");
    if (kind == QLatin1String("hq")) return QCoreApplication::translate("ActivationController", "HQ stations");
    if (kind == QLatin1String("prov")) return QCoreApplication::translate("ActivationController", "Provinces");
    if (kind == QLatin1String("sez")) return QCoreApplication::translate("ActivationController", "Sections");
    if (kind == QLatin1String("qth")) return QCoreApplication::translate("ActivationController", "W/VE QTH");
    return kind;
}

} // namespace

QVariantMap ActivationController::multiplierMatrix() const
{
    if (!m_score.valid)
        buildScore();
    const core::ContestRules rules = currentRules();
    QStringList bands = contestBands(rules);
    // kind → value → bande (vuota la stringa per i moltiplicatori una volta sola)
    QMap<QString, QMap<QString, QSet<QString>>> worked;
    QSet<QString> perBandKinds;
    for (const QString& key : std::as_const(m_score.multipliers)) {
        const QString head = key.section(QLatin1Char('|'), 0, 0);
        const QString band = key.section(QLatin1Char('|'), 1, 1);
        const QString kind = head.section(QLatin1Char(' '), 0, 0);
        const QString value = head.section(QLatin1Char(' '), 1);
        worked[kind][value].insert(band);
        if (!band.isEmpty()) {
            perBandKinds.insert(kind);
            if (!bands.contains(band))
                bands << band;
        }
    }
    // Le zone e le province si mostrano tutte, anche quelle che mancano.
    if (rules.valid && m_session.active) {
        if (rules.id.startsWith(QLatin1String("CQ-WW-"))) {
            for (int z = 1; z <= 40; ++z)
                worked[QStringLiteral("zona")][QString::number(z)];
            perBandKinds << QStringLiteral("zona") << QStringLiteral("paese");
            if (rules.exchange == core::ContestRules::Exchange::CqZoneQth) {
                for (const QString& qth : core::contestrules::wveQths().keys())
                    worked[QStringLiteral("qth")][qth];
                perBandKinds << QStringLiteral("qth");
            }
        } else if (rules.id == QLatin1String("IARU-HF")) {
            for (int z = 1; z <= 90; ++z)
                worked[QStringLiteral("zona")][QString::number(z)];
            perBandKinds << QStringLiteral("zona");
        } else if (rules.id == QLatin1String("ARI-DX")) {
            for (const QString& p : core::awards::italianProvinces().keys())
                worked[QStringLiteral("prov")][p];
            perBandKinds << QStringLiteral("prov");
        }
    }
    QVariantList kinds;
    for (auto k = worked.cbegin(); k != worked.cend(); ++k) {
        const bool perBand = perBandKinds.contains(k.key());
        QList<QPair<QString, QSet<QString>>> rows;
        for (auto v = k.value().cbegin(); v != k.value().cend(); ++v)
            rows << qMakePair(v.key(), v.value());
        // Numeri come numeri.
        std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
            bool okA = false, okB = false;
            const int na = a.first.toInt(&okA), nb = b.first.toInt(&okB);
            return okA && okB ? na < nb : a.first < b.first;
        });
        QVariantList out;
        int count = 0;
        for (const auto& r : rows) {
            QVariantMap bandsWorked;
            for (const QString& b : r.second)
                if (!b.isEmpty())
                    bandsWorked.insert(b, true);
            const bool any = !r.second.isEmpty();
            if (any)
                ++count;
            QString name;
            if (k.key() == QLatin1String("paese") && m_ctx.dxccName)
                name = m_ctx.dxccName(r.first.toInt());
            else if (k.key() == QLatin1String("prov"))
                name = core::awards::italianProvinces().value(r.first);
            else if (k.key() == QLatin1String("qth"))
                name = core::contestrules::wveQths().value(r.first);
            out << QVariantMap{{QStringLiteral("value"), r.first},
                               {QStringLiteral("name"), name},
                               {QStringLiteral("worked"), bandsWorked},
                               {QStringLiteral("any"), any}};
        }
        kinds << QVariantMap{{QStringLiteral("kind"), k.key()},
                             {QStringLiteral("label"), kindLabel(k.key())},
                             {QStringLiteral("perBand"), perBand},
                             {QStringLiteral("count"), count},
                             {QStringLiteral("rows"), out}};
    }
    return QVariantMap{{QStringLiteral("bands"), bands}, {QStringLiteral("kinds"), kinds}};
}

QVariantList ActivationController::multiplierCheck(const QString& call, const QString& band) const
{
    QVariantList out;
    const core::ContestRules rules = currentRules();
    if (!rules.valid || !m_session.active || call.trimmed().size() < 3)
        return out;
    if (!m_score.valid)
        buildScore();
    const core::ContestStation me = m_ctx.station ? m_ctx.station() : core::ContestStation{};
    core::ContestQso qso;
    qso.call = call.trimmed().toUpper();
    if (m_ctx.locate) {
        const core::ContestStation where = m_ctx.locate(qso.call);
        qso.dxcc = where.dxcc;
        qso.continent = where.continent;
        qso.cqZone = where.cqZone;
        qso.ituZone = where.ituZone;
    }
    struct Row {
        QString key;
        QString label;
        bool perBand{false};
        QStringList worked;
        QStringList needed;
    };
    QList<Row> rows;
    for (const QString& b : contestBands(rules)) {
        qso.band = b;
        for (const QString& key : core::contestrules::multipliers(rules, qso, me)) {
            const QString head = key.section(QLatin1Char('|'), 0, 0);
            const bool perBand = key.contains(QLatin1Char('|'));
            auto it = std::find_if(rows.begin(), rows.end(), [&head](const Row& r) { return r.key == head; });
            if (it == rows.end()) {
                Row r;
                r.key = head;
                r.perBand = perBand;
                r.label = shortMultiplier(head);
                if (head.startsWith(QLatin1String("paese")) && m_ctx.dxccName)
                    r.label = m_ctx.dxccName(head.section(QLatin1Char(' '), 1).toInt());
                rows << r;
                it = rows.end() - 1;
            }
            QStringList& list = m_score.multipliers.contains(key) ? it->worked : it->needed;
            const QString where = perBand ? b : QString();
            if (!list.contains(where))
                list << where;
        }
    }
    for (const Row& r : std::as_const(rows)) {
        out << QVariantMap{{QStringLiteral("label"), r.label},
                           {QStringLiteral("perBand"), r.perBand},
                           {QStringLiteral("worked"), r.perBand ? r.worked : QStringList()},
                           {QStringLiteral("needed"), r.perBand ? r.needed : QStringList()},
                           {QStringLiteral("newHere"), r.perBand ? r.needed.contains(band.toLower()) : r.worked.isEmpty()}};
    }
    return out;
}

QVariantMap ActivationController::spotValue(const QString& call, const QString& band,
                                            const QString& mode) const
{
    QVariantMap out{{QStringLiteral("points"), 0},
                    {QStringLiteral("newMultiplier"), false},
                    {QStringLiteral("duplicate"), false},
                    {QStringLiteral("label"), QString()}};
    const core::ContestRules rules = currentRules();
    if (!rules.valid || !m_session.active || call.trimmed().isEmpty())
        return out;

    const core::ContestStation me = m_ctx.station ? m_ctx.station() : core::ContestStation{};
    core::ContestQso qso;
    qso.call = call.trimmed().toUpper();
    qso.band = band.toLower();
    qso.mode = mode.toUpper();
    if (m_ctx.locate) {
        const core::ContestStation where = m_ctx.locate(qso.call);
        qso.dxcc = where.dxcc;
        qso.continent = where.continent;
        qso.cqZone = where.cqZone;
        qso.ituZone = where.ituZone;
    }
    // Lo scambio non si sa prima di averlo lavorato: per i contest che contano
    // zone o paesi basta il cty.csv, per gli altri (province, sezioni) il
    // moltiplicatore si sapra' solo a QSO fatto.
    out[QStringLiteral("points")] = core::contestrules::points(rules, qso, me);
    out[QStringLiteral("duplicate")] = isDuplicate(qso.call, qso.band, qso.mode);

    const QStringList keys = core::contestrules::multipliers(rules, qso, me);
    if (keys.isEmpty())
        return out;
    const QSet<QString> already = workedMultipliers();
    QStringList missing;
    for (const QString& key : keys) {
        if (!already.contains(key))
            missing << shortMultiplier(key.section(QLatin1Char('|'), 0, 0));
    }
    out[QStringLiteral("newMultiplier")] = !missing.isEmpty();
    out[QStringLiteral("label")] = missing.join(QStringLiteral(" · "));
    return out;
}

bool ActivationController::isCwContest() const
{
    if (!m_session.active)
        return false;
    if (m_session.mode.compare(QLatin1String("CW"), Qt::CaseInsensitive) == 0)
        return true;
    // Un contest che si fa solo in telegrafia lo dice nel suo identificativo.
    return m_session.contestId.endsWith(QLatin1String("-CW"), Qt::CaseInsensitive)
           || m_session.contestId.contains(QLatin1String("-CW-"), Qt::CaseInsensitive);
}

QSet<QString> ActivationController::workedMultipliers() const
{
    if (!m_score.valid)
        buildScore();
    return m_score.multipliers;
}

} // namespace decolog::app
