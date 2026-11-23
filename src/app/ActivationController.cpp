#include "app/ActivationController.h"
#include "../StartupTrace.h"

#include "core/Awards.h"
#include "core/Cabrillo.h"
#include "core/ContestRules.h"
#include "core/Contests.h"
#include "core/LogDatabase.h"
#include "core/Spots.h"
#include "core/SotaCatalogue.h"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QSaveFile>
#include <filesystem>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimeZone>
#include <QUuid>

namespace decolog::app {

namespace {

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
    m_sotaCatalogue = new core::SotaCatalogue(QDir(base).filePath(QStringLiteral("sota-catalogue.sqlite")), this);
}

QObject* ActivationController::sotaCatalogue() const { return m_sotaCatalogue; }

ActivationController::~ActivationController()
{
    if (m_previewCancel) m_previewCancel->store(true);
    if (m_partitionExportCancel) m_partitionExportCancel->store(true);
    m_previewPool.waitForDone();
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
    m_uniqueCalls = 0;
    m_perBand.clear();
    m_lastQso.clear();
    if (!m_session.active || !m_ctx.db || !m_ctx.db->isOpen() || !m_session.startedAt.isValid())
        return;

    QSqlQuery q(m_ctx.db->connection());
    q.prepare(QStringLiteral(
        "SELECT COUNT(*), COUNT(DISTINCT call), MAX(qso_datetime_on) FROM qso "
        "WHERE deleted = 0 AND qso_datetime_on >= ?"));
    q.addBindValue(m_session.startedAt.toString(Qt::ISODate));
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
        "FROM qso WHERE deleted = 0 AND qso_datetime_on >= ? GROUP BY band, m ORDER BY COUNT(*) DESC"));
    bands.addBindValue(m_session.startedAt.toString(Qt::ISODate));
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
        "SELECT COUNT(*) FROM qso WHERE deleted = 0 AND call = ? AND band = ? AND qso_datetime_on >= ? "
        "AND (CASE WHEN IFNULL(submode, '') = '' OR mode = 'SSB' THEN mode ELSE submode END) = ?"));
    q.addBindValue(call.trimmed().toUpper());
    q.addBindValue(band.trimmed().toLower());
    q.addBindValue(m_session.startedAt.toString(Qt::ISODate));
    q.addBindValue(mode.trimmed().toUpper());
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

void ActivationController::applyTo(AdifRecord& record) const
{
    int serial = m_session.nextSerial;
    // In rete il numero lo da' chi distribuisce: una sequenza sola per tutti.
    if (m_session.serialEnabled && m_session.sharedSerial && m_ctx.takeSerial)
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
        "SELECT id FROM qso WHERE deleted = 0 AND qso_datetime_on >= ? ORDER BY qso_datetime_on, id"));
    q.addBindValue(m_session.startedAt.toString(Qt::ISODate));
    if (q.exec()) {
        while (q.next())
            ids << q.value(0).toLongLong();
    }
    return ids;
}

namespace {
QVariantMap scanPartitionPreview(QSqlDatabase db, const QString& uuid, const std::shared_ptr<std::atomic_bool>& cancelled,
                                 bool ownsTransaction = true, const QString& selectedKey = {}, QList<qint64>* selectedIds = nullptr)
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
    q.prepare(QStringLiteral("SELECT qso_datetime_on,adif_extra,id,uuid,revision,call FROM qso WHERE deleted=0 AND (CASE WHEN json_valid(adif_extra) THEN json_extract(adif_extra,'$.APP_DECOLOG_ACTIVATION_UUID') END)=? ORDER BY id"));
    q.addBindValue(uuid);
    if (!q.exec()) return fail(tr("Cannot read the session QSO."));
    QMap<QString, QVariantMap> groups;
    QMap<QString, QByteArray> hashes;
    QVariantList excluded;
    const auto exclude = [&](const QString& reason) {
        if (!selectedIds) excluded << QVariantMap{{QStringLiteral("id"), q.value(2)}, {QStringLiteral("call"), q.value(5)},
            {QStringLiteral("at"), q.value(0)}, {QStringLiteral("reason"), reason}};
    };
    int count = 0, missingDate = 0, missingReference = 0;
    const QList<QPair<QString, QString>> fields{{QStringLiteral("POTA"), QStringLiteral("MY_POTA_REF")},
        {QStringLiteral("SOTA"), QStringLiteral("MY_SOTA_REF")}, {QStringLiteral("WWFF"), QStringLiteral("MY_WWFF_REF")},
        {QStringLiteral("IOTA"), QStringLiteral("MY_IOTA")}};
    while (q.next()) {
        if (cancelled && cancelled->load()) return fail(tr("Preview cancelled."));
        ++count;
        const auto extra = QJsonDocument::fromJson(q.value(1).toString().toUtf8()).object();
        QDateTime timestamp = QDateTime::fromString(q.value(0).toString(), Qt::ISODate);
        if (timestamp.isValid()) timestamp.setTimeZone(QTimeZone::UTC);
        QDate date = timestamp.date();
        if (extra.contains(QStringLiteral("QSO_DATE"))) date = QDate::fromString(extra.value(QStringLiteral("QSO_DATE")).toString(), QStringLiteral("yyyyMMdd"));
        if (!date.isValid()) { ++missingDate; exclude(QStringLiteral("missingDate")); continue; }
        const QString day = date.toString(Qt::ISODate);
        const QString op = extra.value(QStringLiteral("OPERATOR")).toString().trimmed().toUpper();
        const QString stationCall = extra.value(QStringLiteral("STATION_CALLSIGN")).toString().trimmed();
        const QString grid = extra.value(QStringLiteral("MY_GRIDSQUARE")).toString().trimmed().toUpper();
        bool found = false;
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
                if (stationCall.isEmpty()) groups[key][QStringLiteral("missingStationCall")] = groups[key].value(QStringLiteral("missingStationCall")).toInt() + 1;
                if (op.isEmpty()) groups[key][QStringLiteral("missingOperator")] = groups[key].value(QStringLiteral("missingOperator")).toInt() + 1;
                if (stationCall.isEmpty() && op.isEmpty()) groups[key][QStringLiteral("missingCallIdentity")] = groups[key].value(QStringLiteral("missingCallIdentity")).toInt() + 1;
                const QByteArray identity = QJsonDocument(QJsonArray{q.value(3).toString(), QString::number(q.value(4).toLongLong())}).toJson(QJsonDocument::Compact);
                hashes[key] = QCryptographicHash::hash(hashes.value(key) + identity, QCryptographicHash::Sha256);
                if (selectedIds && key == selectedKey) selectedIds->append(q.value(2).toLongLong());
            }
        }
        if (!found) { ++missingReference; exclude(QStringLiteral("missingReference")); }
    }
    if (q.lastError().isValid()) return fail(tr("Cannot read the session QSO."));
    if (cancelled && cancelled->load()) return fail(tr("Preview cancelled."));
    QVariantList rows;
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        it.value()[QStringLiteral("fingerprint")] = QString::fromLatin1(hashes.value(it.key()).toHex());
        rows << it.value();
    }
    result[QStringLiteral("groups")] = rows; result[QStringLiteral("reportedCount")] = count;
    result[QStringLiteral("missingDate")] = missingDate; result[QStringLiteral("missingReference")] = missingReference;
    result[QStringLiteral("excluded")] = excluded;
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
    return scanPartitionPreview(m_ctx.db->connection(), m_session.sessionUuid, {});
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
            {QStringLiteral("title"), state.value(QStringLiteral("title"))}};
    }
    return rows;
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
    m_previewSessionUuid = uuid;
    const quint64 generation = m_previewGeneration;
    m_previewCancel = std::make_shared<std::atomic_bool>(false);
    const auto cancelled = m_previewCancel;
    m_previewBusy = true; emit previewChanged();
    QPointer<ActivationController> self(this);
    m_previewPool.start([self, path, connectionName, uuid, generation, cancelled] {
        QVariantMap result;
        const QString connection = QStringLiteral("activation-preview-") + QUuid::createUuid().toString();
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
            db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=100"));
            db.setDatabaseName(path);
            if (!db.open()) result[QStringLiteral("error")] = tr("Cannot open the log for preview.");
            else result = scanPartitionPreview(db, uuid, cancelled);
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
    if (output.isEmpty()) return false;
    const QFileInfo outInfo(output);
    for (const QString& protectedPath : {path, path + QStringLiteral("-wal"), path + QStringLiteral("-shm"), path + QStringLiteral("-journal")}) {
        const QFileInfo protectedInfo(protectedPath);
        std::error_code ec;
        const bool sameExistingFile = std::filesystem::equivalent(std::filesystem::path(output.toStdString()), std::filesystem::path(protectedPath.toStdString()), ec);
        if (outInfo.absoluteFilePath() == protectedInfo.absoluteFilePath() || (!ec && sameExistingFile)) return false;
    }
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
                    } else {
                        record.set(program == QLatin1String("SOTA") ? QStringLiteral("MY_SOTA_REF") : QStringLiteral("MY_IOTA"), ref);
                    }
                }
            }
            if (!db.connection().commit() && error.isEmpty()) error = tr("Cannot complete the export snapshot.");
            if (cancelled->load() && error.isEmpty()) error = tr("Export cancelled.");
            if (error.isEmpty()) {
                QSaveFile file(output);
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
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return tr("Cannot write %1").arg(path);
    file.write(text_out);
    file.close();
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
        "WHERE deleted = 0 AND qso_datetime_on >= ? ORDER BY qso_datetime_on DESC, id DESC LIMIT ?"));
    q.addBindValue(m_session.startedAt.toString(Qt::ISODate));
    q.addBindValue(qMax(1, limit));
    if (!q.exec())
        return out;
    while (q.next()) {
        const QDateTime when = QDateTime::fromString(q.value(2).toString(), Qt::ISODate);
        out << QVariantMap{{QStringLiteral("id"), q.value(0).toLongLong()},
                           {QStringLiteral("call"), q.value(1).toString()},
                           {QStringLiteral("time"), when.toString(QStringLiteral("hh:mm"))},
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
            "SELECT COUNT(*) FROM qso WHERE deleted = 0 AND qso_datetime_on >= ? AND qso_datetime_on >= ?"));
        q.addBindValue(start);
        q.addBindValue(from.toString(Qt::ISODate));
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
        "WHERE deleted = 0 AND qso_datetime_on >= ?"));
    mult.addBindValue(start);
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
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return file.errorString();
    file.write(m_ctx.db->exportAdif(list, QCoreApplication::applicationVersion()));
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
        "FROM qso WHERE deleted = 0 AND qso_datetime_on >= ? ORDER BY qso_datetime_on, id"));
    q.addBindValue(m_session.startedAt.toString(Qt::ISODate));
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
