#include "../StartupTrace.h"
#include "app/QslController.h"

#include "core/CredentialStore.h"
#include "core/ClubLogBatchReconcile.h"
#include "core/ClubLogAccessGuard.h"
#include "core/ClubLogUploadJournal.h"
#include "core/LogDatabase.h"
#include "core/Adif.h"
#include "core/LotwUploadJournal.h"
#include <QJsonArray>
#include <QDateTime>
#include <QUuid>

#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QTemporaryFile>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QPointer>
#include <QSettings>
#include <utility>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QStandardPaths>
#include <algorithm>
#include <memory>
#include <array>
#include <optional>

namespace decolog::app {

using namespace decolog::core;

namespace {

struct ServiceInfo {
    const char* id;
    const char* label;
    const char* credential;     // il servizio nel portachiavi
};

constexpr std::array<ServiceInfo, 7> kServices{{
    {"lotw", "LoTW", "lotw"},
    {"qrz", "QRZ Logbook", "qrzlogbook"},
    {"clublog", "Club Log", "clublog"},
    {"eqsl", "eQSL", "eqsl"},
    {"crx", "CRX Logbook", "crx"},
    {"hrdlog", "HRDLog.net", "hrdlog"},
    {"wavelog", "Wavelog", "wavelog"},
}};

// LoTW accetta file grandi, ma un invio troppo lungo blocca tutto il resto.
constexpr int kLotwBatch = 500;
// Club Log preferisce blocchi ragionevoli a un log intero per volta.
constexpr int kClubLogBatch = 1000;

// QSL state changes do not increment the QSO revision. Include row presence so
// that a newly inserted, otherwise empty state also invalidates the snapshot.
std::optional<QVariantList> lotwQslSnapshot(QSqlDatabase db, qint64 id)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT sent, sent_date, rcvd, rcvd_date, remote_id, last_error, via FROM qsl_status WHERE qso_id = ? AND service = 'lotw'"));
    q.addBindValue(id);
    if (!q.exec()) return std::nullopt;
    QVariantList values;
    if (q.next()) {
        values.append(true);
        for (int i=0; i<7; ++i) values.append(q.value(i));
    } else {
        if (q.lastError().isValid()) return std::nullopt;
        values.append(false);
    }
    return values;
}

bool isBatchService(const QString& service)
{
    return service == QLatin1String("lotw") || service == QLatin1String("clublog");
}

} // namespace

QslController::QslController(Context context, QObject* parent)
    : QObject(parent)
    , m_ctx(std::move(context))
    , m_tqsl(nullptr, m_ctx.tqslCertificateCheck)
{
    m_downloadNet = new QNetworkAccessManager(this);
    if (m_ctx.credentials) {
        connect(m_ctx.credentials, &CredentialStore::changed, this, [this] {
            ++m_oqrsGeneration;
            if (!m_oqrsResultCall.isEmpty())
                m_oqrsStatus = tr("Credentials changed; refresh the OQRS requests");
            m_oqrsRequests.clear();
            m_oqrsResultCall.clear();
            emit changed();
        });
    }
    connect(&m_oqrs, &ClubLogOqrsClient::finished, this,
            [this](bool ok, const QVariantList& rows, const QString& error) {
        m_oqrsBusy = false;
        const QString current = m_ctx.stationCallsign ? m_ctx.stationCallsign().trimmed().toUpper() : QString();
        if (current != m_oqrsCall || m_oqrsGeneration != m_oqrsRequestGeneration) {
            m_oqrsStatus = tr("Club Log account or station changed; refresh the OQRS requests");
        } else if (!ok) {
            m_oqrsStatus = error;
        } else {
            m_oqrsRequests = rows;
            m_oqrsResultCall = current;
            m_oqrsStatus = tr("%n direct OQRS request(s); these are not QSL confirmations", nullptr, int(rows.size()));
        }
        emit changed();
    });
    QSettings s;
    m_tqslPath = s.value(QStringLiteral("qsl/tqslPath")).toString();
    if (m_tqslPath.isEmpty())
        m_tqslPath = qsl::findTqsl();
    m_tqslLocation = s.value(QStringLiteral("qsl/tqslLocation")).toString();
    m_clubLogApiKey = s.value(QStringLiteral("qsl/clubLogApiKey")).toString();
    if (m_clubLogApiKey.trimmed().startsWith(QStringLiteral("cl_"))) {
        // Older configurations may contain a personal token in the legacy
        // key field. Remove the plaintext first and migrate only if doing so
        // will not overwrite an already configured secure credential.
        const QString misplacedToken = m_clubLogApiKey.trimmed();
        m_clubLogApiKey.clear();
        s.remove(QStringLiteral("qsl/clubLogApiKey"));
        s.sync();
        if (s.status() != QSettings::NoError)
            note(tr("Club Log: could not remove the personal token from the old settings file; check its permissions"),
                 QStringLiteral("error"));
        if (m_ctx.credentials && !m_ctx.credentials->hasSecret(QStringLiteral("clublog"))) {
            m_ctx.credentials->save(QStringLiteral("clublog"),
                                    m_ctx.credentials->account(QStringLiteral("clublog")), misplacedToken);
            note(tr("Club Log personal token was removed from the legacy key field and is being moved to secure storage"),
                 QStringLiteral("warning"));
        } else {
            note(tr("Club Log personal token was removed from the legacy key field; check the secure credential"),
                 QStringLiteral("warning"));
        }
    }
    m_crxLogId = s.value(QStringLiteral("qsl/crxLogId"), 0).toLongLong();
    m_crxLogName = s.value(QStringLiteral("qsl/crxLogName")).toString();
    m_crxSince = QDate::fromString(s.value(QStringLiteral("qsl/crxSince")).toString(), Qt::ISODate);
    m_wavelogStationId = s.value(QStringLiteral("qsl/wavelogStationId")).toString();
    connect(&m_web, &WebQslUploader::wavelogStationsListed, this, [this](const QVariantList& stations, const QString& error) {
        m_wavelogStations = stations;
        m_wavelogStatus = !error.isEmpty()     ? tr("Wavelog: %1").arg(error)
                        : stations.isEmpty()   ? tr("Wavelog: no station on the site yet")
                                               : tr("Wavelog: %n station(s) on the site", nullptr, static_cast<int>(stations.size()));
        // Una sola, o quella attiva: si sceglie da se'.
        if (m_wavelogStationId.isEmpty()) {
            for (const QVariant& v : stations) {
                if (stations.size() == 1 || v.toMap().value(QStringLiteral("active")).toBool()) {
                    setWavelogStationId(v.toMap().value(QStringLiteral("id")).toString());
                    break;
                }
            }
        }
        emit changed();
    });
    connect(&m_web, &WebQslUploader::crxLogsListed, this, [this](const QVariantList& logs, const QString& error) {
        m_crxLogs = logs;
        m_crxStatus = !error.isEmpty() ? tr("CRX: %1").arg(error)
                    : logs.isEmpty()   ? tr("CRX: this account has no logbook yet — create one on crx.cloud")
                                       : tr("CRX: %n logbook(s) in the account", nullptr, static_cast<int>(logs.size()));
        // Uno solo: si sceglie da se'.
        if (m_crxLogId == 0 && logs.size() == 1)
            setCrxLogId(logs.first().toMap().value(QStringLiteral("id")).toLongLong());
        // Il nome aggiornato di quello gia' scelto.
        for (const QVariant& v : logs) {
            const QVariantMap log = v.toMap();
            if (log.value(QStringLiteral("id")).toLongLong() == m_crxLogId && m_crxLogName != log.value(QStringLiteral("name")).toString()) {
                m_crxLogName = log.value(QStringLiteral("name")).toString();
                QSettings().setValue(QStringLiteral("qsl/crxLogName"), m_crxLogName);
            }
        }
        emit changed();
    });
    for (const auto& info : kServices)
        m_auto.insert(QLatin1String(info.id), s.value(QStringLiteral("qsl/auto/") + QLatin1String(info.id), false).toBool());
    if (LotwUploadJournal(s).pending() && m_auto.value(QStringLiteral("lotw"))) {
        m_auto.insert(QStringLiteral("lotw"),false);
        s.setValue(QStringLiteral("qsl/auto/lotw"),false); s.sync();
        note(tr("LoTW automatic upload paused: review the previous batch before retrying"),QStringLiteral("warning"));
    }
    m_tqsl.setProgram(m_tqslPath);

    connect(&m_tqsl, &TqslUploader::finished, this, &QslController::finishBatch);
    connect(&m_web, &WebQslUploader::finished, this, [this](const QslUploadResult& result) {
        if (m_deletingId > 0) {
            // Una cancellazione su CRX.
            const qint64 id = m_deletingId;
            m_deletingId = 0;
            if (result.retryLater) {
                note(tr("%1: stopped, %2").arg(m_busyService, result.message), QStringLiteral("warning"));
                finishBatch(result);
                return;
            }
            // Tolto, o CRX non l'aveva piu': in tutti e due i casi li' non c'e'.
            if (!result.ok)
                note(tr("CRX Logbook: deleting QSO %1: %2").arg(id).arg(result.message), QStringLiteral("warning"));
            if (m_ctx.db->forgetRemote(id, m_busyService))
                m_uploadChanges.insert(id);
            ++m_removed;
            uploadNextWeb();
            return;
        }
        if (m_batch.isEmpty())
            return;
        if (m_batchMode) {
            if (m_busyService == QLatin1String("clublog") && m_clubLogRestStage == QLatin1String("submitting")) {
                QSettings settings;
                ClubLogUploadJournal journal(settings);
                if (result.requiresReconciliation && !result.remoteId.isEmpty()
                    && journal.phase() == QLatin1String("queued") && journal.uploadId() == result.remoteId
                    && journal.nonce() == m_clubLogJournalNonce) {
                    m_clubLogRestStage = QStringLiteral("polling");
                    const QString id = result.remoteId;
                    QTimer::singleShot(0, this, [this, id] {
                        if (m_clubLogRestStage == QLatin1String("polling"))
                            m_web.pollClubLogRestBatch(m_clubLogRestCall, m_secret, id, static_cast<int>(m_batch.size()));
                    });
                    return;
                }
            }
            if (m_busyService == QLatin1String("clublog") && m_clubLogRestStage == QLatin1String("polling")
                && !result.clubLogBatchRecords.isEmpty()) {
                QHash<qint64, QString> identities;
                for (qint64 id : std::as_const(m_batch)) {
                    QList<QslState> states;
                    if (!m_ctx.db->readQslStatus(id,&states)) {
                        QslUploadResult unresolved=result; unresolved.ok=false; unresolved.requiresReconciliation=true;
                        unresolved.message=tr("Club Log: cannot read local QSL status; the batch remains awaiting review"); finishBatch(unresolved); return;
                    }
                    const auto record = m_ctx.db->record(id);
                    if (!record) { identities.clear(); break; }
                    QString remote = record->value(QStringLiteral("APP_CLUBLOG_QSOID")).trimmed();
                    for (const QslState& state : std::as_const(states)) {
                        if (state.service != QLatin1String("clublog") || state.remoteId.isEmpty()) continue;
                        if (!remote.isEmpty() && remote != state.remoteId) { remote.clear(); break; }
                        remote = state.remoteId;
                    }
                    identities.insert(id, remote);
                }
                const auto matched = matchClubLogBatchResults(identities, result.clubLogBatchRecords);
                if (matched.size() == m_batch.size()) {
                    if (reconcileClubLogBatch(identities, result.clubLogBatchRecords)) {
                        QslUploadResult reconciled; reconciled.ok = true;
                        m_batchMode = false;
                        finishBatch(reconciled);
                        return;
                    }
                    finishBatch(result);
                    return;
                }
                bool missing = false;
                for (const QString& id : std::as_const(identities)) missing |= id.isEmpty();
                if (missing && identities.size() == m_batch.size()) {
                    QList<QJsonObject> filters;
                    QSet<qint64> remoteIds;
                    int rejectedCount = 0;
                    for (qint64 id : std::as_const(m_batch)) {
                        const auto row = m_ctx.db->record(id);
                        filters.append(row ? clubLogIdentityFilter(*row) : QJsonObject());
                    }
                    for (const QVariant& value : result.clubLogBatchRecords) {
                        const QVariantMap row = value.toMap();
                        const QString status = row.value(QStringLiteral("status")).toString().toLower();
                        if (status == QLatin1String("rejected") && row.contains(QStringLiteral("qsoid"))
                            && row.value(QStringLiteral("qsoid")).isNull()) {
                            ++rejectedCount;
                            continue;
                        }
                        bool ok = false;
                        const qint64 id = row.value(QStringLiteral("qsoid")).toLongLong(&ok);
                        if (!ok || id <= 0 || remoteIds.contains(id) ||
                            (status != QLatin1String("created") && status != QLatin1String("updated")
                             && status != QLatin1String("duplicate"))) { remoteIds.clear(); break; }
                        remoteIds.insert(id);
                    }
                    if (remoteIds.size() + rejectedCount == m_batch.size()) {
                        m_clubLogRestStage = QStringLiteral("resolving");
                        m_web.resolveClubLogBatchIdentities(m_clubLogRestCall, m_secret, filters, remoteIds,
                            [this, identities, result](const QList<QString>& ids, const QString& error) {
                                if (m_clubLogRestStage != QLatin1String("resolving")) return;
                                QHash<qint64, QString> resolved = identities;
                                if (ids.size() == m_batch.size()) {
                                    bool consistent = true;
                                    for (qsizetype i = 0; i < m_batch.size(); ++i) {
                                        const QString known = resolved.value(m_batch.at(i));
                                        if (!known.isEmpty() && !ids.at(i).isEmpty() && known != ids.at(i)) { consistent = false; break; }
                                        if (!ids.at(i).isEmpty()) resolved[m_batch.at(i)] = ids.at(i);
                                    }
                                    if (consistent && reconcileClubLogBatch(resolved, result.clubLogBatchRecords)) {
                                        QslUploadResult success; success.ok = true;
                                        m_batchMode = false;
                                        finishBatch(success);
                                        return;
                                    }
                                }
                                QslUploadResult pending = result;
                                if (!error.isEmpty()) pending.message += QStringLiteral("; ") + error;
                                finishBatch(pending);
                            });
                        return;
                    }
                }
            }
            if (m_busyService == QLatin1String("clublog") && m_clubLogRestStage == QLatin1String("polling")
                && (result.httpStatus == 200 || (result.httpStatus >= 500 && result.httpStatus <= 599))) {
                reconcileClubLogPresence(result);
                return;
            }
            // Final records must be reconciled individually; never treat a
            // queued or unrecognised response as acceptance of the batch.
            finishBatch(result);
            return;
        }
        const qint64 id = m_batch.takeFirst();
        markSent(id, m_busyService, result);
        if (!result.ok && !result.message.isEmpty())
            note(tr("QSO %1: %2").arg(id).arg(result.message), QStringLiteral("warning"));
        if (result.retryLater) {
            // Rete o servizio giu': si ferma qui, il resto resta in coda per dopo.
            note(tr("%1: stopped, %2").arg(m_busyService, result.message), QStringLiteral("warning"));
            finishBatch(result);
            return;
        }
        uploadNextWeb();
    });

    // I QSO che arrivano a raffica si mandano insieme poco dopo, non uno a uno.
    m_clubLogRecheck.setSingleShot(true);
    m_clubLogRecheck.setInterval(5 * 60 * 1000);
    connect(&m_clubLogRecheck, &QTimer::timeout, this, [this] {
        if (!autoUpload(QStringLiteral("clublog")) || !m_ctx.credentials
            || !m_ctx.credentials->hasSecret(QStringLiteral("clublog"))) return;
        if (busy()) { m_clubLogRecheck.start(); return; }
        QSettings settings;
        ClubLogUploadJournal journal(settings);
        if (!journal.pending()) return;
        if (!m_ctx.db || journal.logIdentity() != m_ctx.db->path()) return;
        resumeClubLogPendingUpload();
    });
    {
        QSettings settings;
        if (autoUpload(QStringLiteral("clublog")) && m_ctx.credentials
            && m_ctx.credentials->hasSecret(QStringLiteral("clublog"))
            && ClubLogUploadJournal(settings).pending()) m_clubLogRecheck.start();
    }
    m_autoDelay.setSingleShot(true);
    m_autoDelay.setInterval(20'000);
    connect(&m_autoDelay, &QTimer::timeout, this, [this] {
        QStringList services;
        for (const auto& info : kServices) {
            const QString id = QLatin1String(info.id);
            if (m_autoServices.contains(id) && m_auto.value(id))
                services.append(id);
        }
        m_autoServices.clear();
        m_automaticUploads.append(services);
        startNextAutomaticUpload();
    });
}

// ── Stato ─────────────────────────────────────────────────────────────────────

bool QslController::tqslReady() const
{
    return !m_tqslPath.isEmpty() && QFile::exists(m_tqslPath) && qsl::tqslHasCertificate();
}

QString QslController::tqslStatus() const
{
    if (m_tqslPath.isEmpty() || !QFile::exists(m_tqslPath))
        return tr("TQSL not found: install Trusted QSL or set its path here");
    if (qsl::tqslDataDirectory().isEmpty()) {
        return tr("TQSL is installed but has not been set up yet: open it once, "
                  "so it creates its own folder with certificates and station locations");
    }
    if (!qsl::tqslHasCertificate()) {
        return tr("TQSL has no callsign certificate yet: load the .tq6 file ARRL sends you "
                  "(the .tq5 request alone does not sign anything) — TQSL's folder is %1")
            .arg(QDir::toNativeSeparators(qsl::tqslDataDirectory()));
    }
    const QStringList locations = tqslLocations();
    if (locations.isEmpty())
        return tr("TQSL has no station location: create one in TQSL (Station → Add location)");
    return tr("TQSL ready · %n station location(s)", nullptr, static_cast<int>(locations.size()));
}

bool QslController::backgroundReady() const
{
    return m_ctx.db && m_ctx.db->isOpen() && !m_ctx.db->path().isEmpty()
           && m_ctx.db->path() != QLatin1String(":memory:");
}

void QslController::countsDirty()
{
    if (!backgroundReady() || m_countsScheduled)
        return;
    m_countsScheduled = true;
    QMetaObject::invokeMethod(this, [this] { refreshCounts(); }, Qt::QueuedConnection);
}

void QslController::refreshCounts()
{
    m_countsScheduled = false;
    if (!backgroundReady())
        return;
    const QString path = m_ctx.db->path();
    QHash<QString, QDate> since;
    for (const auto& info : kServices)
        since.insert(QLatin1String(info.id), sinceFor(QLatin1String(info.id)));
    const int generation = ++m_countGeneration;
    QPointer<QslController> self(this);
    m_countPool.start([self, path, since, generation] {
        QHash<QString, int> pending;
        QList<QVariantMap> summary;
        {
            LogDatabase db;
            if (db.open(path)) {
                summary = db.qslSummary();
                for (auto it = since.cbegin(); it != since.cend(); ++it)
                    pending.insert(it.key(), db.uploadPendingCount(it.key(), it.value()));
            }
        }
        QMetaObject::invokeMethod(
            self.data(), [self, pending, summary, generation] {
                if (!self || generation != self->m_countGeneration)
                    return;
                self->m_pendingCounts = pending;
                self->m_summaryRows = summary;
                self->m_countsValid = true;
                emit self->changed();
            },
            Qt::QueuedConnection);
    });
}

QVariantList QslController::services() const
{
    decolog::StartupSpan trace("QslController::services");
    QVariantList out;
    if (!m_ctx.db || !m_ctx.db->isOpen())
        return out;
    // Su un log in un file i conti arrivano da un altro filo (refreshCounts):
    // qui si leggono e basta. In memoria (le prove) si contano qui.
    const bool cached = backgroundReady();
    if (cached && !m_countsValid)
        const_cast<QslController*>(this)->countsDirty();
    const QList<QVariantMap> summary = cached ? m_summaryRows : m_ctx.db->qslSummary();
    for (const auto& info : kServices) {
        const QString id = QLatin1String(info.id);
        QVariantMap row{{QStringLiteral("id"), id},
                        {QStringLiteral("label"), QLatin1String(info.label)},
                        {QStringLiteral("pending"), cached ? m_pendingCounts.value(id, 0)
                                                           : m_ctx.db->uploadPendingCount(id, sinceFor(id))},
                        {QStringLiteral("auto"), m_auto.value(id)},
                        {QStringLiteral("busy"), m_busyService == id},
                        {QStringLiteral("lastResult"), m_lastResult.value(id)}};
        for (const QVariantMap& s : summary) {
            if (s.value(QStringLiteral("service")).toString() == id) {
                row.insert(QStringLiteral("sent"), s.value(QStringLiteral("sent")));
                row.insert(QStringLiteral("confirmed"), s.value(QStringLiteral("confirmed")));
                row.insert(QStringLiteral("errors"), s.value(QStringLiteral("errors")));
            }
        }
        const QString credential = QLatin1String(info.credential);
        const QString account = m_ctx.credentials ? m_ctx.credentials->account(credential) : QString();
        const QString profile = m_ctx.stationProfileName ? m_ctx.stationProfileName() : QString();
        row.insert(QStringLiteral("account"), account.isEmpty() ? tr("not configured") : account);
        row.insert(QStringLiteral("profile"), profile.isEmpty() ? tr("active station") : profile);
        bool hasSecret = m_ctx.credentials && m_ctx.credentials->hasSecret(credential);
        // QRZ ed eQSL: basta anche l'account di un solo profilo.
        if (!hasSecret && m_ctx.credentials && m_ctx.db && m_ctx.db->isOpen()
            && CredentialStore::profileServiceBases().contains(credential)) {
            for (const StationProfile& p : m_ctx.db->stationProfiles(false)) {
                if (m_ctx.credentials->hasSecret(CredentialStore::profileService(credential, p.id)))
                    hasSecret = true;
            }
        }
        bool ready = hasSecret;
        QString hint;
        if (id == QLatin1String("lotw")) {
            ready = tqslReady();
            hint = tqslStatus();
        } else if (id == QLatin1String("clublog")) {
            ready = hasSecret;
            hint = !hasSecret      ? tr("no credentials: Setup \u2192 QSL services")
                 : QString();
        } else if (id == QLatin1String("wavelog")) {
            const bool hasServer = m_ctx.credentials && !m_ctx.credentials->account(credential).isEmpty();
            ready = hasSecret && hasServer && !m_wavelogStationId.isEmpty();
            hint = !hasSecret || !hasServer    ? tr("no address or API key: Setup \u2192 QSL services")
                 : m_wavelogStationId.isEmpty() ? tr("choose the Wavelog station: Setup \u2192 QSL services")
                                                : QString();
        } else if (id == QLatin1String("crx")) {
            ready = hasSecret && m_crxLogId > 0;
            hint = !hasSecret        ? tr("no API key: Setup \u2192 QSL services")
                 : m_crxLogId == 0   ? tr("choose the CRX logbook: Setup \u2192 QSL services")
                                     : QString();
        } else if (!hasSecret) {
            hint = tr("no credentials: Setup \u2192 QSL services");
        }
        row.insert(QStringLiteral("ready"), ready);
        row.insert(QStringLiteral("hint"), hint);
        row.insert(QStringLiteral("credential"), credential);
        out << row;
    }
    return out;
}

void QslController::setTqslPath(const QString& path)
{
    if (path == m_tqslPath)
        return;
    m_tqslPath = path.trimmed();
    m_tqsl.setProgram(m_tqslPath);
    QSettings().setValue(QStringLiteral("qsl/tqslPath"), m_tqslPath);
    emit changed();
}

QString QslController::clubLogPendingState() const
{
    QSettings settings;
    ClubLogUploadJournal journal(settings);
    return journal.phase();
}

QVariantMap QslController::clubLogPendingInfo() const
{
    QSettings settings;
    ClubLogUploadJournal journal(settings);
    if (!journal.pending()) return {};
    return {{QStringLiteral("phase"), journal.phase()}, {QStringLiteral("station"), journal.station()},
            {QStringLiteral("uploadId"), journal.uploadId()}, {QStringLiteral("count"), journal.localIds().size()},
            {QStringLiteral("currentLog"), m_ctx.db && journal.logIdentity() == m_ctx.db->path()}};
}

QVariantList QslController::clubLogOqrsRequests() const
{
    const QString current = m_ctx.stationCallsign ? m_ctx.stationCallsign().trimmed().toUpper() : QString();
    return current == m_oqrsResultCall ? m_oqrsRequests : QVariantList{};
}

QString QslController::clubLogOqrsStatus() const
{
    const QString current = m_ctx.stationCallsign ? m_ctx.stationCallsign().trimmed().toUpper() : QString();
    if (!m_oqrsBusy && !m_oqrsResultCall.isEmpty() && current != m_oqrsResultCall)
        return tr("Station changed; refresh the OQRS requests");
    return m_oqrsStatus;
}

void QslController::fetchClubLogOqrs()
{
    if (m_oqrsBusy) return;
    m_oqrsCall = m_ctx.stationCallsign ? m_ctx.stationCallsign().trimmed().toUpper() : QString();
    m_oqrsRequestGeneration = m_oqrsGeneration;
    m_oqrsBusy = true;
    m_oqrsStatus = tr("Reading direct OQRS requests from Club Log…");
    emit changed();
    readSecret(QStringLiteral("clublog"), [this, generation = m_oqrsRequestGeneration](const QString& token, const QString& error) {
        if (generation != m_oqrsGeneration) {
            m_oqrsBusy = false; m_oqrsStatus = tr("Club Log credential changed; try again"); emit changed(); return;
        }
        if (!token.startsWith(QLatin1String("cl_"))) {
            m_oqrsBusy = false;
            m_oqrsStatus = error.isEmpty() ? tr("A Club Log personal token is required for OQRS requests") : error;
            emit changed(); return;
        }
        m_oqrs.fetch(m_oqrsCall, token);
    });
}

void QslController::downloadClubLog()
{
    if (m_downloadBusy) return;
    const QString call = m_ctx.stationCallsign ? m_ctx.stationCallsign().trimmed().toUpper() : QString();
    if (!QRegularExpression(QStringLiteral("^[A-Z0-9]+(?:/[A-Z0-9]+)*$")).match(call).hasMatch()) {
        m_downloadStatus = tr("Select a station callsign before downloading Club Log"); emit changed(); return;
    }
    const quint64 generation = m_oqrsGeneration;
    const QString logPath = m_ctx.db ? m_ctx.db->path() : QString();
    m_downloadBusy = true;
    m_downloadStatus = tr("Downloading the Club Log logbook…"); emit changed();
    readSecret(QStringLiteral("clublog"), [this, call, generation, logPath](const QString& token, const QString&) {
        auto fail = [this](const QString& error) { m_downloadBusy = false; m_downloadStatus = error; emit changed(); };
        if (generation != m_oqrsGeneration || !token.startsWith(QLatin1String("cl_"))) {
            fail(tr("A valid Club Log personal token is required")); return;
        }
        const QString restriction = ClubLogAccessGuard::restriction(token);
        if (!restriction.isEmpty()) { fail(restriction); return; }
        m_downloadFile = new QTemporaryFile(QDir::tempPath() + QStringLiteral("/decodxlog-clublog-XXXXXX.adi"), this);
        if (!m_downloadFile->open()) { delete m_downloadFile; m_downloadFile = nullptr; fail(tr("Cannot create a temporary download file")); return; }
        const QUrl url = m_downloadBase.resolved(QUrl(QStringLiteral("logs/") + QString::fromLatin1(QUrl::toPercentEncoding(call, "/")) + QStringLiteral("/adif-export")));
        QNetworkRequest request(url);
        request.setRawHeader("Authorization", QByteArray("Bearer ") + token.toUtf8());
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        request.setTransferTimeout(30000);
        QNetworkReply* reply = m_downloadNet->post(request, QByteArray("{}"));
        auto failedWrite = std::make_shared<bool>(false);
        connect(reply, &QNetworkReply::readyRead, this, [this, reply, failedWrite] {
            const QByteArray bytes = reply->readAll();
            if (!m_downloadFile || m_downloadFile->size() + bytes.size() > 256LL * 1024 * 1024 || m_downloadFile->write(bytes) != bytes.size()) {
                *failedWrite = true; reply->abort();
            }
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, token, call, generation, logPath, failedWrite] {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            ClubLogAccessGuard::record(token, status, reply->rawHeader("Retry-After"));
            const bool success = status == 200 && reply->error() == QNetworkReply::NoError && !*failedWrite
                && generation == m_oqrsGeneration && m_ctx.db && m_ctx.db->path() == logPath
                && m_ctx.stationCallsign && m_ctx.stationCallsign().trimmed().toUpper() == call
                && m_downloadFile && m_downloadFile->flush() && m_downloadFile->size() > 0;
            reply->deleteLater(); m_downloadBusy = false;
            if (!success) {
                delete m_downloadFile; m_downloadFile = nullptr;
                m_downloadStatus = tr("Club Log download failed (HTTP %1); no contacts were imported").arg(status); emit changed(); return;
            }
            const QUrl file = QUrl::fromLocalFile(m_downloadFile->fileName());
            m_downloadFile->close();
            m_downloadFile = nullptr; // The parent keeps the temporary file until shutdown.
            m_downloadStatus = tr("Download received; see the import preview"); emit changed();
            emit clubLogDownloadReady(file, call);
        });
    });
}

void QslController::resumeClubLogPendingUpload()
{
    if (busy() || !m_ctx.db || !m_ctx.db->isOpen()) return;
    QSettings settings;
    ClubLogUploadJournal journal(settings);
    if ((journal.phase() != QLatin1String("queued") && journal.phase() != QLatin1String("submitting")) || journal.logIdentity() != m_ctx.db->path()) {
        note(tr("Club Log: no resumable upload for this logbook; review the pending checkpoint"), QStringLiteral("warning"));
        emit changed();
        return;
    }
    QList<qint64> ids;
    QSet<qint64> unique;
    for (const QString& text : journal.localIds()) {
        bool ok = false;
        const qint64 id = text.toLongLong(&ok);
        if (!ok || id <= 0 || unique.contains(id)) {
            note(tr("Club Log: invalid saved QSO list; reconciliation required"), QStringLiteral("warning"));
            return;
        }
        unique.insert(id); ids.append(id);
    }
    const QByteArray document = m_ctx.db->exportAdif(ids, QCoreApplication::applicationVersion());
    const QString hash = ClubLogUploadJournal::recordsFingerprint(document, static_cast<int>(ids.size()));
    if (ids.isEmpty() || hash != journal.payloadSha256()) {
        note(tr("Club Log: local QSO data changed; review the pending upload before resuming"), QStringLiteral("warning"));
        return;
    }
    const QString uploadId = journal.uploadId();
    const bool noUploadId = journal.phase() == QLatin1String("submitting");
    m_clubLogJournalNonce = journal.nonce();
    m_clubLogRestCall = journal.station();
    m_batch = ids;
    m_batchMode = true;
    m_busyService = QStringLiteral("clublog");
    m_clubLogRestJournalThisRun = true;
    m_clubLogRestStage = QStringLiteral("polling");
    readSecret(QStringLiteral("clublog"), [this, uploadId, noUploadId](const QString& secret, const QString&) {
        if (m_clubLogRestStage != QLatin1String("polling")) return;
        if (!secret.startsWith(QStringLiteral("cl_"))) {
            QslUploadResult failed;
            failed.requiresReconciliation = true;
            failed.message = tr("Club Log: personal token required to resume the pending upload");
            finishBatch(failed);
            return;
        }
        m_secret = secret;
        if (noUploadId) {
            QslUploadResult pending;
            pending.requiresReconciliation = true;
            pending.message = tr("Club Log: upload outcome remains unknown");
            reconcileClubLogPresence(pending);
            return;
        }
        m_web.pollClubLogRestBatch(m_clubLogRestCall, m_secret, uploadId, static_cast<int>(m_batch.size()));
    });
    emit changed();
}

void QslController::setClubLogApiKey(const QString& key)
{
    if (key.trimmed().startsWith(QStringLiteral("cl_"))) {
        note(tr("Club Log personal tokens must be saved in the secure Club Log credential field, not the legacy API-key field"), QStringLiteral("warning"));
        emit changed();
        return;
    }
    if (key.trimmed() == m_clubLogApiKey)
        return;
    m_clubLogApiKey = key.trimmed();
    QSettings().setValue(QStringLiteral("qsl/clubLogApiKey"), m_clubLogApiKey);
    emit changed();
}

void QslController::setCrxLogId(qint64 id)
{
    if (id == m_crxLogId)
        return;
    m_crxLogId = id;
    m_crxLogName.clear();
    for (const QVariant& v : m_crxLogs) {
        const QVariantMap log = v.toMap();
        if (log.value(QStringLiteral("id")).toLongLong() == id)
            m_crxLogName = log.value(QStringLiteral("name")).toString();
    }
    QSettings s;
    s.setValue(QStringLiteral("qsl/crxLogId"), m_crxLogId);
    s.setValue(QStringLiteral("qsl/crxLogName"), m_crxLogName);
    // Scegliendo il logbook per la prima volta si parte da oggi: il log di
    // prima si manda solo se lo si chiede, cambiando la data.
    if (!m_crxSince.isValid() && id > 0)
        setCrxSince(QDate::currentDate().toString(Qt::ISODate));
    emit changed();
}

void QslController::setCrxSince(const QString& date)
{
    countsDirty();
    const QDate d = QDate::fromString(date.trimmed(), Qt::ISODate);
    if (d == m_crxSince)
        return;
    m_crxSince = d;
    QSettings().setValue(QStringLiteral("qsl/crxSince"), d.isValid() ? d.toString(Qt::ISODate) : QString());
    emit changed();
}

QDate QslController::sinceFor(const QString& service) const
{
    return service == QLatin1String("crx") ? m_crxSince : QDate();
}

void QslController::setWavelogStationId(const QString& id)
{
    if (id == m_wavelogStationId)
        return;
    m_wavelogStationId = id;
    QSettings().setValue(QStringLiteral("qsl/wavelogStationId"), id);
    emit changed();
}

void QslController::fetchWavelogStations()
{
    m_wavelogStatus = tr("Wavelog: asking for the stations…");
    emit changed();
    const QString server = m_ctx.credentials ? m_ctx.credentials->account(QStringLiteral("wavelog")) : QString();
    readSecret(QStringLiteral("wavelog"), [this, server](const QString& secret, const QString& error) {
        if (secret.isEmpty() || server.isEmpty()) {
            m_wavelogStatus = tr("Wavelog: address and API key are needed (%1)")
                                  .arg(error.isEmpty() ? tr("add them below") : error);
            emit changed();
            return;
        }
        m_web.listWavelogStations(server, secret);
    });
}

void QslController::fetchCrxLogs()
{
    m_crxStatus = tr("CRX: asking for the logbooks…");
    emit changed();
    readSecret(QStringLiteral("crx"), [this](const QString& secret, const QString& error) {
        if (secret.isEmpty()) {
            m_crxStatus = tr("CRX: no API key (%1)").arg(error.isEmpty() ? tr("add it below") : error);
            emit changed();
            return;
        }
        m_web.listCrxLogs(secret);
    });
}

void QslController::setTqslLocation(const QString& location)
{
    if (location == m_tqslLocation)
        return;
    m_tqslLocation = location;
    QSettings().setValue(QStringLiteral("qsl/tqslLocation"), location);
    emit changed();
}

void QslController::setAutoUpload(const QString& service, bool automatic)
{
    if (service == QLatin1String("lotw") && automatic) {
        QSettings settings; LotwUploadJournal journal(settings);
        if (journal.pending() || settings.status()!=QSettings::NoError) {
            note(tr("LoTW: an earlier batch needs review before another upload"), QStringLiteral("warning"));
            return;
        }
    }
    if (m_auto.value(service) == automatic)
        return;
    m_auto.insert(service, automatic);
    QSettings().setValue(QStringLiteral("qsl/auto/") + service, automatic);
    if (service == QLatin1String("clublog")) {
        if (!automatic) m_clubLogRecheck.stop();
        else if (m_ctx.credentials && m_ctx.credentials->hasSecret(service)) {
            QSettings settings;
            if (ClubLogUploadJournal(settings).pending()) m_clubLogRecheck.start();
        }
    }
    emit changed();
}

bool QslController::autoUpload(const QString& service) const
{
    return m_auto.value(service);
}

void QslController::note(const QString& text, const QString& level)
{
    if (m_ctx.activity)
        m_ctx.activity(QStringLiteral("QSL"), text, level);
}

void QslController::readSecret(const QString& service, std::function<void(const QString&, const QString&)> done)
{
    if (!m_ctx.credentials) {
        done({}, tr("no keystore"));
        return;
    }
    if (!m_ctx.credentials->hasSecret(service)) {
        done({}, tr("no credentials configured"));
        return;
    }
    QPointer<QslController> self(this);
    m_ctx.credentials->readSecret(service, [self, service, done = std::move(done)](const QString& secret, const QString& error) {
        if (!self) return;
        if (!self->m_ctx.credentials || !self->m_ctx.credentials->hasSecret(service) || secret.trimmed().isEmpty()) {
            done({}, error.isEmpty() ? self->tr("no credentials configured") : error);
            return;
        }
        done(secret, error);
    });
}

// ── Invio ─────────────────────────────────────────────────────────────────────

void QslController::qsoLogged(qint64 id)
{
    Q_UNUSED(id);
    scheduleAutomaticServices(enabledAutomaticServices());
}

QStringList QslController::enabledAutomaticServices() const
{
    QStringList services;
    for (const auto& info : kServices) {
        const QString service = QLatin1String(info.id);
        if (m_auto.value(service))
            services.append(service);
    }
    return services;
}

void QslController::scheduleAutomaticServices(const QStringList& services)
{
    bool scheduled = false;
    for (const QString& service : services) {
        if (m_auto.value(service)) {
            m_autoServices.insert(service);
            scheduled = true;
        }
    }
    if (scheduled)
        m_autoDelay.start();
}

void QslController::qsoEdited(qint64 id)
{
    if (!m_ctx.db || !m_ctx.db->isOpen())
        return;
    QHash<QString, QString> sent;
    for (const QslState& state : m_ctx.db->qslStatus(id))
        sent.insert(state.service, state.sent);
    bool scheduled = false;
    for (const auto& info : kServices) {
        const QString service = QLatin1String(info.id);
        const QString status = sent.value(service);
        if (m_auto.value(service) && (status.isEmpty() || status == QLatin1String("N")
            || status == QLatin1String("R") || status == QLatin1String("Q") || status == QLatin1String("D"))) {
            m_autoServices.insert(service);
            scheduled = true;
        }
    }
    if (scheduled)
        m_autoDelay.start();
}

QVariantMap QslController::lotwPendingInfo() const
{
    QSettings settings; LotwUploadJournal journal(settings);
    if (!journal.pending()) return {};
    const QJsonObject obj=journal.info();
    return {{QStringLiteral("pending"),true},{QStringLiteral("nonce"),obj.value(QStringLiteral("nonce")).toString()},
            {QStringLiteral("logIdentity"),obj.value(QStringLiteral("logIdentity")).toString()},
            {QStringLiteral("logPath"),obj.value(QStringLiteral("logPath")).toString()},
            {QStringLiteral("location"),obj.value(QStringLiteral("location")).toString()},
            {QStringLiteral("createdAt"),obj.value(QStringLiteral("createdAt")).toString()},
            {QStringLiteral("sha256"),obj.value(QStringLiteral("sha256")).toString()},
            {QStringLiteral("contacts"),obj.value(QStringLiteral("contacts")).toArray().toVariantList()},
            {QStringLiteral("count"),obj.value(QStringLiteral("contacts")).toArray().size()}};
}
bool QslController::acknowledgeLotwPending(const QString& nonce)
{
    QSettings settings; LotwUploadJournal journal(settings); const QJsonObject obj=journal.info();
    QLockFile flight(settings.fileName()+QStringLiteral(".lotw-flight.lock"));
    if (!flight.tryLock(0)) { note(tr("LoTW: an upload is still running; review it after it stops"),QStringLiteral("warning")); return false; }
    if (busy() || !m_ctx.db || !m_ctx.db->isOpen() || !m_ctx.db->fileIdentityMatchesOpen() ||
        obj.value(QStringLiteral("logIdentity")).toString().isEmpty() || obj.value(QStringLiteral("logIdentity")).toString()!=m_ctx.db->identity() ||
        obj.value(QStringLiteral("logPath")).toString()!=m_ctx.db->path() || !journal.clear(nonce)) {
        note(tr("LoTW: cannot clear the checkpoint; open its original log and review the batch"), QStringLiteral("warning")); return false;
    }
    note(tr("LoTW: checkpoint cleared after operator review; QSO statuses were not changed"), QStringLiteral("warning"));
    emit changed(); return true;
}

void QslController::uploadQso(qint64 id, const QString& service)
{
    if (busy() || !m_ctx.db)
        return;
    if (service == QLatin1String("clublog")) {
        QSettings settings;
        if (ClubLogUploadJournal(settings).reservedIds(m_ctx.db->path()).contains(QString::number(id))) {
            note(tr("Club Log: this contact is already awaiting verification"), QStringLiteral("warning"));
            return;
        }
    }
    m_busyService = service;
    m_batch = {id};
    startNext();
}

int QslController::uploadQsos(const QVariantList& ids, const QString& service)
{
    if (busy() || !m_ctx.db || !m_ctx.db->isOpen() || ids.isEmpty())
        return 0;
    // Solo quelli che a quel servizio devono ancora andare: rimandare un QSO
    // gia' confermato non serve a nessuno e fa arrabbiare il servizio.
    const QList<qint64> pending = m_ctx.db->qsosToUpload(service, 0);
    const QSet<qint64> waiting(pending.cbegin(), pending.cend());
    QSettings checkpointSettings;
    const QSet<QString> reserved = service == QLatin1String("clublog")
        ? ClubLogUploadJournal(checkpointSettings).reservedIds(m_ctx.db->path()) : QSet<QString>();
    QList<qint64> batch;
    QSet<qint64> added;
    for (const QVariant& value : ids) {
        const qint64 id = value.toLongLong();
        if (waiting.contains(id) && !added.contains(id) && !reserved.contains(QString::number(id))) {
            batch << id;
            added.insert(id);
        }
    }
    if (batch.isEmpty()) {
        m_lastResult.insert(service, tr("nothing to send: they have already gone"));
        emit changed();
        return 0;
    }
    m_busyService = service;
    m_batch = batch;
    startNext();
    return static_cast<int>(batch.size());
}

void QslController::uploadPending(const QString& service, int limit)
{
    decolog::StartupSpan trace("QSL prepare pending uploads");
    if (busy() || !m_ctx.db || !m_ctx.db->isOpen())
        return;
    int cap = limit;
    if (service == QLatin1String("lotw"))
        cap = limit > 0 ? qMin(limit, kLotwBatch) : kLotwBatch;
    else if (service == QLatin1String("clublog"))
        cap = limit > 0 ? qMin(limit, kClubLogBatch) : kClubLogBatch;
    m_batch = m_ctx.db->qsosToUpload(service, service == QLatin1String("clublog") ? 0 : cap, sinceFor(service));
    if (service == QLatin1String("clublog")) {
        QSettings settings;
        const QSet<QString> reserved = ClubLogUploadJournal(settings).reservedIds(m_ctx.db->path());
        m_batch.erase(std::remove_if(m_batch.begin(), m_batch.end(), [&reserved](qint64 id) {
            return reserved.contains(QString::number(id));
        }), m_batch.end());
        if (cap > 0 && m_batch.size() > cap) m_batch = m_batch.mid(0, cap);
    }
    m_deletions.clear();
    if (LogDatabase::editableServices().contains(service))
        m_deletions = m_ctx.db->remoteDeletions(service);
    if (m_batch.isEmpty() && m_deletions.isEmpty()) {
        m_lastResult.insert(service, tr("nothing to send"));
        emit changed();
        return;
    }
    m_busyService = service;
    startNext();
}

void QslController::startNextAutomaticUpload()
{
    // Un solo uploader alla volta. Il servizio successivo resta nella coda e
    // viene ripreso da finishBatch(): in questo modo LoTW ed eQSL automatici
    // vengono entrambi eseguiti, in sequenza, anche se il primo fallisce.
    while (true) {
        const QString service = m_automaticUploads.takeNext(busy());
        if (service.isEmpty())
            return;
        // L'operatore puo' disattivare un servizio mentre il precedente e' in
        // corso: non avviarlo usando una configurazione ormai superata.
        if (!m_auto.value(service))
            continue;
        uploadPending(service, 0);
    }
}

void QslController::startNext()
{
    decolog::StartupSpan trace("QSL start upload");
    m_accepted = m_duplicates = m_rejected = m_removed = 0;
    m_batchMode = isBatchService(m_busyService);
    emit changed();

    if (m_busyService == QLatin1String("lotw")) {
        QSettings settings; LotwUploadJournal journal(settings);
        if (journal.pending() || settings.status()!=QSettings::NoError) {
            QslUploadResult failed; failed.requiresReconciliation=true;
            failed.message=tr("LoTW: an earlier batch needs review before another upload"); finishBatch(failed); return;
        }
        if (settings.fileName().isEmpty() || !QDir().mkpath(QFileInfo(settings.fileName()).absolutePath())) {
            QslUploadResult failed; failed.requiresReconciliation=true; failed.message=tr("LoTW: cannot lock the upload checkpoint; no upload started"); finishBatch(failed); return;
        }
        m_lotwFlightLock=std::make_unique<QLockFile>(settings.fileName()+QStringLiteral(".lotw-flight.lock"));
        if (!m_lotwFlightLock->tryLock(0)) {
            m_lotwFlightLock.reset(); QslUploadResult failed; failed.requiresReconciliation=true;
            failed.message=tr("LoTW: another upload or review is in progress"); finishBatch(failed); return;
        }
        settings.sync();
        if (journal.pending() || settings.status()!=QSettings::NoError) {
            QslUploadResult failed; failed.requiresReconciliation=true; failed.message=tr("LoTW: an earlier batch needs review before another upload"); finishBatch(failed); return;
        }
        if (!m_ctx.db->fileIdentityMatchesOpen() || m_ctx.db->identity().isEmpty()) {
            QslUploadResult failed; failed.message=tr("LoTW: cannot verify the open log"); finishBatch(failed); return;
        }
        QSqlDatabase snapshotDb=m_ctx.db->connection();
        if (!snapshotDb.transaction()) {
            QslUploadResult failed; failed.message=tr("LoTW: cannot prepare the batch"); finishBatch(failed); return;
        }
        const QByteArray adif=m_ctx.db->exportAdif(m_batch,QCoreApplication::applicationVersion());
        const AdifDocument document=adif::parse(adif);
        QJsonArray contacts; QHash<qint64,QPair<QString,int>> versions; QHash<qint64,QVariantList> qslSnapshots;
        bool snapshotOk=!adif.isEmpty() && !m_batch.isEmpty() && document.records.size()==m_batch.size();
        for (qsizetype i=0; snapshotOk && i<m_batch.size(); ++i) {
            const qint64 id=m_batch.at(i); const auto meta=m_ctx.db->meta(id);
            if (!meta || meta->deleted || meta->uuid.isEmpty()) { snapshotOk=false; break; }
            const auto qsl=lotwQslSnapshot(snapshotDb,id);
            if (!qsl) { snapshotOk=false; break; }
            qslSnapshots.insert(id,*qsl);
            const auto& record=document.records.at(i);
            versions.insert(id,qMakePair(meta->uuid,meta->revision));
            contacts.append(QJsonObject{{QStringLiteral("id"),QString::number(id)},{QStringLiteral("uuid"),meta->uuid},{QStringLiteral("revision"),meta->revision},
                {QStringLiteral("call"),record.value(QStringLiteral("CALL"))},{QStringLiteral("date"),record.value(QStringLiteral("QSO_DATE"))},
                {QStringLiteral("time"),record.value(QStringLiteral("TIME_ON"))},{QStringLiteral("band"),record.value(QStringLiteral("BAND"))},
                {QStringLiteral("mode"),record.value(QStringLiteral("MODE"))},{QStringLiteral("submode"),record.value(QStringLiteral("SUBMODE"))}});
        }
        if (!snapshotOk || contacts.isEmpty()) {
            snapshotDb.rollback(); QslUploadResult failed; failed.message=tr("LoTW: cannot prepare the batch"); finishBatch(failed); return;
        }
        if (!snapshotDb.commit()) {
            snapshotDb.rollback(); QslUploadResult failed; failed.message=tr("LoTW: cannot prepare the batch"); finishBatch(failed); return;
        }
        const QString location=m_tqslLocation.isEmpty() && m_ctx.stationLocation ? m_ctx.stationLocation() : m_tqslLocation;
        const QString nonce=QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QJsonObject checkpoint{{QStringLiteral("version"),1},{QStringLiteral("nonce"),nonce},
            {QStringLiteral("logIdentity"),m_ctx.db->identity()},{QStringLiteral("logPath"),m_ctx.db->path()},
            {QStringLiteral("location"),location},{QStringLiteral("createdAt"),QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {QStringLiteral("sha256"),QString::fromLatin1(QCryptographicHash::hash(adif,QCryptographicHash::Sha256).toHex())},
            {QStringLiteral("contacts"),contacts}};
        if (!journal.begin(checkpoint)) {
            QslUploadResult failed; failed.requiresReconciliation=true; failed.message=tr("LoTW: cannot save the batch checkpoint; no upload started"); finishBatch(failed); return;
        }
        m_lotwCheckpointNonce=nonce; m_lotwLogIdentity=m_ctx.db->identity(); m_lotwLogPath=m_ctx.db->path();
        m_lotwFileIdentity=m_ctx.db->openFileIdentity(); m_lotwOpenGeneration=m_ctx.db->openGeneration(); m_lotwQsoVersions=versions; m_lotwQslSnapshots=qslSnapshots;
        emit changed();
        QTemporaryFile file(QDir::tempPath()+QStringLiteral("/decolog-lotw-XXXXXX.adi")); file.setAutoRemove(false);
        if (!file.open() || file.write(adif)!=adif.size() || !file.flush()) {
            const QString path=file.fileName(); file.close(); if (!path.isEmpty()) QFile::remove(path);
            QslUploadResult failed; failed.message=tr("LoTW: cannot write the temporary ADIF"); finishBatch(failed); return;
        }
        m_adifFile=file.fileName(); file.close();
        note(tr("LoTW: sending %n QSO with TQSL…", nullptr, static_cast<int>(m_batch.size())), QStringLiteral("info"));
        m_tqsl.upload(m_adifFile,location,static_cast<int>(m_batch.size()));
        return;
    }

    if (m_busyService == QLatin1String("clublog")) {
        const QString call = m_ctx.stationCallsign ? m_ctx.stationCallsign() : QString();
        if (call.isEmpty()) {
            QslUploadResult failed;
            failed.message = tr("Club Log: the station profile has no callsign");
            finishBatch(failed);
            return;
        }
        const QString activeCall = call.trimmed().toUpper();
        int differentStation = 0;
        m_batch.erase(std::remove_if(m_batch.begin(), m_batch.end(), [this, &activeCall, &differentStation](qint64 id) {
            const auto record = m_ctx.db->record(id);
            if (!record) { ++differentStation; return true; }
            const QString recordCall = record->value(QStringLiteral("STATION_CALLSIGN")).trimmed().toUpper();
            const auto meta = m_ctx.db->meta(id);
            QString profileCall;
            if (meta && meta->stationProfileId > 0) {
                const auto profile = m_ctx.db->stationProfile(meta->stationProfileId);
                if (!profile) { ++differentStation; return true; }
                profileCall = profile->stationCallsign.trimmed().toUpper();
            }
            const bool mismatch = (!recordCall.isEmpty() && recordCall != activeCall)
                || (!profileCall.isEmpty() && profileCall != activeCall);
            if (mismatch) ++differentStation;
            return mismatch;
        }), m_batch.end());
        if (differentStation > 0)
            note(tr("Club Log: %n QSO belong to another station and remain pending", nullptr, differentStation), QStringLiteral("warning"));
        if (m_batch.isEmpty()) {
            QslUploadResult failed;
            failed.requiresReconciliation = true;
            failed.message = tr("Club Log: no pending contacts belong to the active station");
            finishBatch(failed);
            return;
        }
        m_account = m_ctx.credentials ? m_ctx.credentials->account(QStringLiteral("clublog")) : QString();
        readSecret(QStringLiteral("clublog"), [this, call](const QString& secret, const QString& error) {
            if (secret.isEmpty()) {
                QslUploadResult failed;
                failed.message = tr("%1: no credentials (%2)").arg(m_busyService, error);
                finishBatch(failed);
                return;
            }
            ClubLogAuth auth;
            auth.email = m_account;
            auth.password = secret;
            auth.callsign = call;
            auth.apiKey = m_clubLogApiKey;
            const QByteArray document = m_ctx.db->exportAdif(m_batch, QCoreApplication::applicationVersion());
            if (secret.startsWith(QStringLiteral("cl_"))) {
                QSettings settings;
                ClubLogUploadJournal journal(settings);
                QStringList ids;
                for (qint64 id : std::as_const(m_batch)) ids.append(QString::number(id));
                const QString hash = ClubLogUploadJournal::recordsFingerprint(document, static_cast<int>(ids.size()));
                if (!journal.begin(m_ctx.db->path(), call, ids, hash)) {
                    QslUploadResult failed;
                    failed.requiresReconciliation = true;
                    failed.message = tr("Club Log: an upload is unresolved or its checkpoint could not be saved; review it before sending");
                    finishBatch(failed);
                    return;
                }
                m_clubLogRestJournalThisRun = true;
                m_clubLogJournalNonce = journal.nonce();
            }
            note(tr("Club Log: sending %n QSO\u2026", nullptr, static_cast<int>(m_batch.size())), QStringLiteral("info"));
            if (secret.startsWith(QStringLiteral("cl_")) && m_batch.size() > 1) {
                m_secret = secret;
                m_clubLogRestCall = call;
                if (m_batch.size() <= 20) {
                    m_clubLogRestStage = QStringLiteral("polling");
                    m_web.submitClubLogRealtimeBatch(call, secret, document, int(m_batch.size()));
                    return;
                }
                m_clubLogRestStage = QStringLiteral("submitting");
                m_web.submitClubLogRestBatch(call, secret, document, [nonce = m_clubLogJournalNonce](const QString& uploadId) {
                    QSettings settings;
                    ClubLogUploadJournal journal(settings);
                    return journal.queued(uploadId, nonce);
                });
            } else {
                m_web.uploadClubLog(auth, document, static_cast<int>(m_batch.size()));
            }
        });
        return;
    }

    if (m_busyService == QLatin1String("crx") && m_crxLogId <= 0) {
        QslUploadResult failed;
        failed.message = tr("CRX Logbook: choose the logbook first (Setup \u2192 QSL services)");
        finishBatch(failed);
        return;
    }

    // QRZ ed eQSL: l'account dipende dal profilo del QSO, e si legge QSO per QSO.
    if (m_busyService == QLatin1String("qrz") || m_busyService == QLatin1String("eqsl")) {
        m_credential.clear();
        m_secret.clear();
        note(tr("%1: sending %n QSO…", nullptr, static_cast<int>(m_batch.size())).arg(m_busyService), QStringLiteral("info"));
        uploadNextWeb();
        return;
    }

    // CRX e HRDLog: prima la credenziale, poi un QSO alla volta.
    const QString credential = m_busyService == QLatin1String("qrz")     ? QStringLiteral("qrzlogbook")
                             : m_busyService == QLatin1String("crx")     ? QStringLiteral("crx")
                             : m_busyService == QLatin1String("hrdlog")  ? QStringLiteral("hrdlog")
                             : m_busyService == QLatin1String("wavelog") ? QStringLiteral("wavelog")
                                                                         : QStringLiteral("eqsl");
    m_account = m_ctx.credentials ? m_ctx.credentials->account(credential) : QString();
    readSecret(credential, [this](const QString& secret, const QString& error) {
        if (secret.isEmpty()) {
            QslUploadResult failed;
            failed.message = tr("%1: no credentials (%2)").arg(m_busyService, error);
            finishBatch(failed);
            return;
        }
        m_secret = secret;
        note(tr("%1: sending %n QSO…", nullptr, static_cast<int>(m_batch.size())).arg(m_busyService), QStringLiteral("info"));
        uploadNextWeb();
    });
}

void QslController::uploadNextWeb()
{
    if (m_busyService == QLatin1String("crx") && !m_deletions.isEmpty()) {
        // Prima le cancellazioni: il numero CRX vale in qualunque log.
        const auto [id, key] = m_deletions.takeFirst();
        const qint64 remote = key.section(QLatin1Char(':'), -1).toLongLong();
        if (remote <= 0) {
            if (m_ctx.db->forgetRemote(id, m_busyService))
                m_uploadChanges.insert(id);
            uploadNextWeb();
            return;
        }
        m_deletingId = id;
        m_web.deleteCrx(m_secret, remote);
        return;
    }
    if (m_batch.isEmpty()) {
        QslUploadResult done;
        done.ok = true;
        finishBatch(done);
        return;
    }
    const auto record = m_ctx.db->record(m_batch.first());
    if (!record) {
        m_batch.removeFirst();
        uploadNextWeb();
        return;
    }
    if (m_busyService == QLatin1String("crx")) {
        // Un QSO gia' mandato e poi corretto si aggiorna con il suo numero CRX,
        // invece di crearne un secondo.
        // Il numero vale solo nel log dove e' stato dato: in un altro log
        // scelto dopo, il QSO e' nuovo.
        qint64 remote = 0;
        for (const QslState& existing : m_ctx.db->qslStatus(m_batch.first())) {
            if (existing.service == QLatin1String("crx"))
                remote = qsl::crxRemoteQso(existing.remoteId, m_crxLogId);
        }
        m_web.uploadCrx(m_secret, qsl::crxQsoData(*record, m_crxLogId, remote, m_batch.first()));
        return;
    }
    if (m_busyService == QLatin1String("qrz") || m_busyService == QLatin1String("eqsl")) {
        const QString base = m_busyService == QLatin1String("qrz") ? QStringLiteral("qrzlogbook") : QStringLiteral("eqsl");
        const auto meta = m_ctx.db->meta(m_batch.first());
        const QString credential = m_ctx.credentials
                                       ? m_ctx.credentials->serviceFor(base, meta ? meta->stationProfileId : 0)
                                       : base;
        if (credential != m_credential || m_secret.isEmpty()) {
            m_credential = credential;
            m_account = m_ctx.credentials ? m_ctx.credentials->account(credential) : QString();
            m_secret.clear();
            readSecret(credential, [this](const QString& secret, const QString& error) {
                if (m_batch.isEmpty())
                    return;
                if (secret.isEmpty()) {
                    // Questo QSO resta da mandare, con il motivo; si va avanti.
                    QslUploadResult failed;
                    failed.message = tr("%1: no credentials (%2)").arg(m_busyService, error);
                    markSent(m_batch.first(), m_busyService, failed);
                    m_batch.removeFirst();
                    m_credential.clear();
                    uploadNextWeb();
                    return;
                }
                m_secret = secret;
                uploadNextWeb();
            });
            return;
        }
    }
    const QString adif = adif::writeRecord(*record);
    if (m_busyService == QLatin1String("qrz"))
        m_web.uploadQrz(m_secret, adif);
    else if (m_busyService == QLatin1String("wavelog"))
        m_web.uploadWavelog(m_account, m_secret, m_wavelogStationId, adif);
    else if (m_busyService == QLatin1String("hrdlog"))
        m_web.uploadHrdLog(m_account.isEmpty() && m_ctx.stationCallsign ? m_ctx.stationCallsign() : m_account,
                           m_secret, adif);
    else
        m_web.uploadEqsl(m_account, m_secret, adif);
}

bool QslController::clubLogCheckpointMatches() const
{
    if (!m_ctx.db || !m_ctx.db->isOpen()) return false;
    QSettings settings;
    ClubLogUploadJournal journal(settings);
    QStringList ids;
    for (qint64 id : m_batch) ids.append(QString::number(id));
    return (journal.phase() == QLatin1String("queued") || journal.phase() == QLatin1String("submitting")) && journal.nonce() == m_clubLogJournalNonce
        && journal.logIdentity() == m_ctx.db->path() && journal.station() == m_clubLogRestCall
        && journal.localIds() == ids && !ids.isEmpty()
        && journal.payloadSha256() == ClubLogUploadJournal::recordsFingerprint(
            m_ctx.db->exportAdif(m_batch, QCoreApplication::applicationVersion()), int(ids.size()));
}

void QslController::reconcileClubLogPresence(const QslUploadResult& pending)
{
    if (!clubLogCheckpointMatches()) {
        QslUploadResult unresolved = pending;
        unresolved.message = tr("Club Log: local QSO data changed; review the pending upload before resuming");
        finishBatch(unresolved);
        return;
    }
    QList<QJsonObject> filters;
    for (qint64 id : std::as_const(m_batch)) {
        const auto row = m_ctx.db->record(id);
        filters.append(row ? clubLogIdentityFilter(*row) : QJsonObject());
    }
    m_clubLogRestStage = QStringLiteral("presence");
    m_web.resolveClubLogBatchIdentities(m_clubLogRestCall, m_secret, filters, {},
        [this, pending](const QList<QString>& ids, const QString& error) {
            if (m_clubLogRestStage != QLatin1String("presence")) return;
            if (ids.size() == m_batch.size() && clubLogCheckpointMatches()) {
                QHash<qint64, QString> identities;
                QVariantList rows;
                QList<qint64> remaining;
                bool statusUnreadable=false;
                for (qsizetype i = 0; i < ids.size(); ++i) {
                    if (ids.at(i).isEmpty()) { remaining.append(m_batch.at(i)); continue; }
                    QList<QslState> states;
                    if (!m_ctx.db->readQslStatus(m_batch.at(i),&states)) { identities.clear(); statusUnreadable=true; break; }
                    const auto record = m_ctx.db->record(m_batch.at(i));
                    if (!record) { identities.clear(); break; }
                    QString known = record->value(QStringLiteral("APP_CLUBLOG_QSOID")).trimmed();
                    for (const QslState& state : std::as_const(states)) {
                        if (state.service != QLatin1String("clublog") || state.remoteId.isEmpty()) continue;
                        if (!known.isEmpty() && known.toLongLong() != state.remoteId.toLongLong()) { known = QStringLiteral("invalid"); break; }
                        known = state.remoteId;
                    }
                    if (!known.isEmpty() && known.toLongLong() != ids.at(i).toLongLong()) { identities.clear(); break; }
                    identities.insert(m_batch.at(i), ids.at(i));
                    rows.append(QVariantMap{{QStringLiteral("status"), QStringLiteral("duplicate")},
                                            {QStringLiteral("qsoid"), ids.at(i)}});
                }
                if (statusUnreadable) {
                    QslUploadResult unresolved=pending; unresolved.ok=false; unresolved.requiresReconciliation=true;
                    unresolved.message=tr("Club Log: cannot read local QSL status; the batch remains awaiting review"); finishBatch(unresolved); return;
                }
                if (identities.size() == m_batch.size() && reconcileClubLogBatch(identities, rows)) {
                    note(tr("Club Log: every contact was found in the log; upload processing details are unavailable"), QStringLiteral("info"));
                    QslUploadResult success; success.ok = true;
                    m_batchMode = false;
                    finishBatch(success);
                    return;
                }
                if (!identities.isEmpty() && identities.size() + remaining.size() == m_batch.size() && !remaining.isEmpty()) {
                    QStringList remainingIds;
                    for (qint64 id : remaining) remainingIds.append(QString::number(id));
                    const QString hash = ClubLogUploadJournal::recordsFingerprint(
                        m_ctx.db->exportAdif(remaining, QCoreApplication::applicationVersion()), int(remaining.size()));
                    QSettings settings;
                    ClubLogUploadJournal journal(settings);
                    const int oldDuplicates = m_duplicates;
                    const bool saved = journal.reconcilePartial(m_clubLogJournalNonce, remainingIds, hash, [this, &identities] {
                        QSqlDatabase db = m_ctx.db->connection();
                        if (!db.transaction()) return false;
                        bool ok = true;
                        for (auto it = identities.cbegin(); it != identities.cend(); ++it) {
                            QList<QslState> current;
                            if (!m_ctx.db->readQslStatus(it.key(),&current)) { ok=false; break; }
                            for (const QslState& state : std::as_const(current)) {
                                if (state.service==QLatin1String("clublog") && !state.remoteId.isEmpty() && state.remoteId!=it.value()) { ok=false; break; }
                            }
                            if (!ok) break;
                            QslUploadResult found; found.ok = true; found.duplicates = 1; found.remoteId = it.value();
                            if (!markSent(it.key(), QStringLiteral("clublog"), found)) { ok = false; break; }
                        }
                        if (ok) ok = db.commit();
                        if (!ok) {
                            db.rollback();
                            for (auto it = identities.cbegin(); it != identities.cend(); ++it)
                                m_uploadChanges.remove(it.key());
                        }
                        return ok;
                    });
                    if (saved) {
                        QslUploadResult unresolved = pending;
                        unresolved.message = tr("Club Log: %1 contacts found; %2 still awaiting verification")
                                                 .arg(identities.size()).arg(remaining.size());
                        finishBatch(unresolved);
                        return;
                    }
                    m_duplicates = oldDuplicates;
                }
            }
            QslUploadResult unresolved = pending;
            if (!error.isEmpty()) unresolved.message += QStringLiteral("; ") + error;
            finishBatch(unresolved);
        }, true);
}

bool QslController::reconcileClubLogBatch(const QHash<qint64, QString>& identities, const QVariantList& rows)
{
    const auto matched = matchClubLogBatchResults(identities, rows);
    if (matched.size() != m_batch.size()) return false;
    const int oldAccepted = m_accepted, oldDuplicates = m_duplicates, oldRejected = m_rejected;
    QSettings settings;
    ClubLogUploadJournal journal(settings);
    const bool saved = journal.reconcileAndClear(m_clubLogJournalNonce, [this, &matched, &identities] {
        QSqlDatabase db = m_ctx.db->connection();
        if (!db.transaction()) return false;
        bool committed = true;
        for (qint64 id : std::as_const(m_batch)) {
            QList<QslState> current;
            if (!m_ctx.db->readQslStatus(id,&current)) { committed=false; break; }
            for (const QslState& state : std::as_const(current)) {
                if (state.service==QLatin1String("clublog") && !state.remoteId.isEmpty() && state.remoteId!=identities.value(id)) { committed=false; break; }
            }
            if (!committed) break;
            const QVariantMap row = matched.value(id);
            QslUploadResult individual;
            individual.remoteId = row.value(QStringLiteral("qsoid")).toString();
            const QString status = row.value(QStringLiteral("status")).toString().toLower();
            if (status == QLatin1String("rejected")) {
                individual.rejected = 1;
                individual.message = row.value(QStringLiteral("message")).toString();
                if (individual.message.isEmpty()) individual.message = tr("Club Log rejected this QSO");
            } else if (status == QLatin1String("duplicate")) {
                individual.ok = true;
                individual.duplicates = 1;
            } else {
                individual.ok = true;
                individual.accepted = 1;
            }
            if (!markSent(id, QStringLiteral("clublog"), individual)) { committed = false; break; }
        }
        if (committed) committed = db.commit();
        if (!committed) {
            db.rollback();
            for (qint64 id : std::as_const(m_batch))
                m_uploadChanges.remove(id);
        }
        return committed;
    });
    if (!saved) {
        m_accepted = oldAccepted; m_duplicates = oldDuplicates; m_rejected = oldRejected;
    } else {
        m_clubLogRestJournalThisRun = false;
    }
    return saved;
}

bool QslController::markSent(qint64 id, const QString& service, const QslUploadResult& result)
{
    decolog::StartupSpan trace("QSL save upload status");
    countsDirty();
    QslState state;
    state.service = service;
    // Anche il duplicato e' "inviato": il servizio ce l'ha.
    state.sent = result.accepted > 0 || result.duplicates > 0 ? QStringLiteral("Y") : QStringLiteral("N");
    if (state.sent == QLatin1String("Y"))
        state.sentDate = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd"));
    // Il numero che il servizio ha dato al QSO; se non lo ripete (una
    // correzione, un errore) resta quello di prima. Per CRX con il suo log.
    const bool newId = !result.remoteId.isEmpty() && result.remoteId != QLatin1String("0");
    state.remoteId = !newId ? QString()
                   : service == QLatin1String("crx") ? qsl::crxRemoteKey(m_crxLogId, result.remoteId)
                                                     : result.remoteId;
    state.lastError = result.ok ? QString() : result.message;
    // setQslState preserves the received fields in the existing row.
    if (!m_ctx.db->setQslState(id, state)) return false;
    m_uploadChanges.insert(id);
    m_accepted += result.accepted;
    m_duplicates += result.duplicates;
    m_rejected += result.rejected;
    return true;
}

void QslController::publishUploadChanges()
{
    const auto ids = std::exchange(m_uploadChanges, {});
    if (!ids.isEmpty() && m_ctx.uploadStateChanged)
        m_ctx.uploadStateChanged(ids);
}

bool QslController::saveBatchStates(const QString& service, const QslUploadResult& state)
{
    decolog::StartupSpan trace("QSL save batch upload statuses");
    if (m_batch.isEmpty()) return true;
    const bool guardedLotw=service==QLatin1String("lotw") && !m_lotwCheckpointNonce.isEmpty();
    if (m_batch.size() == 1 && !guardedLotw) return markSent(m_batch.first(), service, state);

    // Group local status writes after a single remote batch response, avoiding
    // a separate durable commit for every contact in a LoTW or Club Log batch.
    QSqlDatabase db = m_ctx.db->connection();
    if (!db.transaction()) return false;
    const int accepted = m_accepted, duplicates = m_duplicates, rejected = m_rejected;
    const auto previousChanges = m_uploadChanges;
    bool saved = true;
    if (guardedLotw) {
        if (m_lotwQsoVersions.size()!=m_batch.size() || m_lotwQslSnapshots.size()!=m_batch.size()) saved=false;
        for (qint64 id : std::as_const(m_batch)) {
            const auto meta=m_ctx.db->meta(id); const auto expected=m_lotwQsoVersions.value(id);
            const auto qsl=lotwQslSnapshot(db,id);
            if (!meta || meta->deleted || meta->uuid!=expected.first || meta->revision!=expected.second || !qsl || *qsl!=m_lotwQslSnapshots.value(id)) { saved=false; break; }
        }
    }
    for (qint64 id : std::as_const(m_batch)) {
        if (!saved || !markSent(id, service, state)) {
            saved = false;
            break;
        }
    }
    if (saved && db.commit()) return true;
    db.rollback();
    m_accepted = accepted;
    m_duplicates = duplicates;
    m_rejected = rejected;
    m_uploadChanges = previousChanges;
    return false;
}

void QslController::finishBatch(const QslUploadResult& result)
{
    countsDirty();
    const QString service = m_busyService;
    bool clubLogStatePersisted = true;
    bool batchStateSaved = true;
    if (service == QLatin1String("clublog") && !result.ok && m_auto.value(service)
        && (!result.requiresReconciliation || result.httpStatus == 401 || result.httpStatus == 403 || result.httpStatus == 429)) {
        setAutoUpload(service, false);
        note(tr("Club Log automatic upload paused: review the result before retrying"), QStringLiteral("warning"));
    }
    if (service == QLatin1String("clublog") && m_rejected > 0 && m_auto.value(service)) {
        setAutoUpload(service, false);
        note(tr("Club Log automatic upload paused: correct the rejected contacts before retrying"), QStringLiteral("warning"));
    }
    if (service == QLatin1String("lotw") && result.requiresReconciliation && m_auto.value(service)) {
        setAutoUpload(service, false);
        note(tr("LoTW automatic upload paused: check the batch in LoTW before retrying"), QStringLiteral("warning"));
    }
    if (m_batchMode) {
        if (result.ok && service == QLatin1String("clublog") && m_batch.size() == 1) {
            if (m_clubLogRestJournalThisRun) {
                QSettings settings;
                ClubLogUploadJournal journal(settings);
                clubLogStatePersisted = journal.reconcileAndClear(m_clubLogJournalNonce,
                    [this, &service, &result] { return markSent(m_batch.first(), service, result); });
                if (clubLogStatePersisted) m_clubLogRestJournalThisRun = false;
            } else {
                clubLogStatePersisted = markSent(m_batch.first(), service, result);
                batchStateSaved = clubLogStatePersisted;
            }
        } else if (result.ok && service == QLatin1String("lotw") && !m_lotwCheckpointNonce.isEmpty() &&
                   (!m_ctx.db || !m_ctx.db->isOpen() || !m_ctx.db->fileIdentityMatchesOpen() ||
                    m_ctx.db->path()!=m_lotwLogPath || m_ctx.db->identity()!=m_lotwLogIdentity ||
                    m_ctx.db->openFileIdentity()!=m_lotwFileIdentity || m_ctx.db->openGeneration()!=m_lotwOpenGeneration)) {
            batchStateSaved=false;
        } else if (result.ok) {
            QslUploadResult state;
            state.ok = true;
            if (result.duplicates > 0 && result.accepted == 0)
                state.duplicates = 1;
            else
                state.accepted = 1;
            batchStateSaved = saveBatchStates(service, state);
        } else if (service != QLatin1String("lotw") && !result.requiresReconciliation) {
            QslUploadResult failed = result;
            failed.accepted = failed.duplicates = 0;
            batchStateSaved = saveBatchStates(service, failed);
        }
        if (!m_adifFile.isEmpty()) {
            QFile::remove(m_adifFile);
            m_adifFile.clear();
        }
    }

    if (!batchStateSaved && m_auto.value(service))
        setAutoUpload(service, false);

    if (service == QLatin1String("lotw") && !m_lotwCheckpointNonce.isEmpty()) {
        if ((!result.ok && !result.requiresReconciliation) || (result.ok && batchStateSaved)) {
            QSettings settings; LotwUploadJournal journal(settings);
            if (!journal.clear(m_lotwCheckpointNonce)) {
                batchStateSaved=false;
                if (m_auto.value(service)) setAutoUpload(service,false);
                note(tr("LoTW: checkpoint could not be cleared; review the batch before retrying"), QStringLiteral("warning"));
            }
        }
        m_lotwCheckpointNonce.clear(); m_lotwLogIdentity.clear(); m_lotwLogPath.clear(); m_lotwFileIdentity.clear(); m_lotwQsoVersions.clear(); m_lotwQslSnapshots.clear();
    }

    if (m_clubLogRestJournalThisRun) {
        if (!result.requiresReconciliation && (!result.ok || clubLogStatePersisted)) {
            QSettings settings;
            ClubLogUploadJournal journal(settings);
            if (!journal.clearAfterReconciliation(m_clubLogJournalNonce))
                note(tr("Club Log: could not clear upload checkpoint; check before sending again"), QStringLiteral("warning"));
        }
        if (result.ok && !clubLogStatePersisted)
            note(tr("Club Log: remote result received but local save failed; checkpoint retained for reconciliation"), QStringLiteral("warning"));
        m_clubLogRestJournalThisRun = false;
    }
    if (service == QLatin1String("lotw")) m_lotwFlightLock.reset();
    m_clubLogRestStage.clear();
    m_clubLogRestCall.clear();
    m_clubLogJournalNonce.clear();
    m_secret.clear();
    m_credential.clear();
    m_batch.clear();
    m_busyService.clear();
    m_batchMode = false;
    if (service == QLatin1String("clublog")) {
        QSettings settings;
        if (autoUpload(QStringLiteral("clublog")) && m_ctx.credentials
            && m_ctx.credentials->hasSecret(QStringLiteral("clublog"))
            && ClubLogUploadJournal(settings).pending()
            && result.httpStatus != 401 && result.httpStatus != 403 && result.httpStatus != 429
            && result.httpStatus < 500)
            m_clubLogRecheck.start();
    }

    QString summary = result.ok
        ? (service == QLatin1String("lotw")
               ? tr("LoTW: TQSL uploaded %1 QSO; check processing in LoTW Your Activity").arg(m_accepted)
               : tr("%1: %2 sent, %3 already there, %4 rejected").arg(service).arg(m_accepted).arg(m_duplicates).arg(m_rejected))
        : result.message;
    if (result.ok && m_removed > 0)
        summary += tr(", %n deleted", nullptr, m_removed);
    if (!batchStateSaved)
        summary = tr("%1: could not save the upload status locally. Check the remote log before retrying.").arg(service);
    m_lastResult.insert(service, summary);
    note(summary, result.ok && batchStateSaved ? QStringLiteral("success") : QStringLiteral("error"));
    if (!result.ok && !result.message.isEmpty() && result.message != summary)
        note(QStringLiteral("  ") + result.message, QStringLiteral("warning"));
    publishUploadChanges();
    emit changed();
    // Uscire prima completamente dal callback del servizio appena concluso;
    // poi avviare il prossimo automatico. Evita di riusare nello stesso stack
    // TQSL o il client HTTP che ha appena emesso finished().
    QTimer::singleShot(0, this, &QslController::startNextAutomaticUpload);
}

void QslController::cancel()
{
    if (m_busyService == QLatin1String("lotw") && m_tqsl.busy()) {
        // Keep the batch and temporary file until TQSL exits: it may already
        // have submitted data, and finishBatch must report the uncertain result.
        m_automaticUploads.clear(); m_autoDelay.stop(); m_autoServices.clear();
        if (m_auto.value(QStringLiteral("lotw"))) setAutoUpload(QStringLiteral("lotw"), false);
        note(tr("LoTW upload cancelled after launch; check the original batch in LoTW before retrying"), QStringLiteral("warning"));
        m_tqsl.cancel();
        emit changed();
        return;
    }
    // Do not clear a checkpoint: a submitted network request may have reached
    // Club Log even if the user cancels the local operation.
    m_clubLogRestJournalThisRun = false;
    m_clubLogRestStage.clear();
    m_clubLogRestCall.clear();
    m_tqsl.cancel();
    m_batch.clear();
    m_deletions.clear();
    m_deletingId = 0;
    m_busyService.clear();
    m_batchMode = false;
    m_secret.clear();
    m_automaticUploads.clear();
    m_autoDelay.stop();
    m_autoServices.clear();
    publishUploadChanges();
    emit changed();
}

} // namespace decolog::app
