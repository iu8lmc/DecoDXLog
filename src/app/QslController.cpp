#include "../StartupTrace.h"
#include "app/QslController.h"

#include "core/CredentialStore.h"
#include "core/LogDatabase.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>
#include <algorithm>
#include <array>

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

bool isBatchService(const QString& service)
{
    return service == QLatin1String("lotw") || service == QLatin1String("clublog");
}

QString tempAdifPath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
        .filePath(QStringLiteral("decolog-lotw-upload.adi"));
}

} // namespace

QslController::QslController(Context context, QObject* parent)
    : QObject(parent)
    , m_ctx(std::move(context))
{
    QSettings s;
    m_tqslPath = s.value(QStringLiteral("qsl/tqslPath")).toString();
    if (m_tqslPath.isEmpty())
        m_tqslPath = qsl::findTqsl();
    m_tqslLocation = s.value(QStringLiteral("qsl/tqslLocation")).toString();
    m_clubLogApiKey = s.value(QStringLiteral("qsl/clubLogApiKey")).toString();
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
            m_ctx.db->forgetRemote(id, m_busyService);
            ++m_removed;
            uploadNextWeb();
            return;
        }
        if (m_batch.isEmpty())
            return;
        if (m_batchMode) {
            // Club Log ha risposto per tutto il blocco in una volta.
            finishBatch(result);
            return;
        }
        const qint64 id = m_batch.takeFirst();
        markSent(id, m_busyService, result);
        if (result.retryLater) {
            // Rete o servizio giu': si ferma qui, il resto resta in coda per dopo.
            note(tr("%1: stopped, %2").arg(m_busyService, result.message), QStringLiteral("warning"));
            finishBatch(result);
            return;
        }
        uploadNextWeb();
    });

    // I QSO che arrivano a raffica si mandano insieme poco dopo, non uno a uno.
    m_autoDelay.setSingleShot(true);
    m_autoDelay.setInterval(20'000);
    connect(&m_autoDelay, &QTimer::timeout, this, [this] {
        m_autoQueue.clear();
        QStringList services;
        for (const auto& info : kServices) {
            const QString id = QLatin1String(info.id);
            if (m_auto.value(id))
                services.append(id);
        }
        m_automaticUploads.reset(services);
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
            ready = hasSecret && !m_clubLogApiKey.isEmpty();
            hint = !hasSecret      ? tr("no credentials: Setup \u2192 QSL services")
                 : m_clubLogApiKey.isEmpty() ? tr("no API key: Setup \u2192 QSL services")
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

void QslController::setClubLogApiKey(const QString& key)
{
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
    if (m_auto.value(service) == automatic)
        return;
    m_auto.insert(service, automatic);
    QSettings().setValue(QStringLiteral("qsl/auto/") + service, automatic);
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
    m_ctx.credentials->readSecret(service, std::move(done));
}

// ── Invio ─────────────────────────────────────────────────────────────────────

void QslController::qsoLogged(qint64 id)
{
    const bool anyAuto = std::any_of(kServices.cbegin(), kServices.cend(),
                                     [this](const ServiceInfo& i) { return m_auto.value(QLatin1String(i.id)); });
    if (!anyAuto)
        return;
    m_autoQueue << id;
    m_autoDelay.start();
}

void QslController::uploadQso(qint64 id, const QString& service)
{
    if (busy() || !m_ctx.db)
        return;
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
    QList<qint64> batch;
    for (const QVariant& value : ids) {
        const qint64 id = value.toLongLong();
        if (waiting.contains(id) && !batch.contains(id))
            batch << id;
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
    if (busy() || !m_ctx.db || !m_ctx.db->isOpen())
        return;
    int cap = limit;
    if (service == QLatin1String("lotw"))
        cap = limit > 0 ? qMin(limit, kLotwBatch) : kLotwBatch;
    else if (service == QLatin1String("clublog"))
        cap = limit > 0 ? qMin(limit, kClubLogBatch) : kClubLogBatch;
    m_batch = m_ctx.db->qsosToUpload(service, cap, sinceFor(service));
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
    m_accepted = m_duplicates = m_rejected = m_removed = 0;
    m_batchMode = isBatchService(m_busyService);
    emit changed();

    if (m_busyService == QLatin1String("lotw")) {
        const QString path = tempAdifPath();
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QslUploadResult failed;
            failed.message = tr("Cannot write %1").arg(path);
            finishBatch(failed);
            return;
        }
        file.write(m_ctx.db->exportAdif(m_batch, QCoreApplication::applicationVersion()));
        file.close();
        m_adifFile = path;
        const QString location = m_tqslLocation.isEmpty() && m_ctx.stationLocation ? m_ctx.stationLocation()
                                                                                   : m_tqslLocation;
        note(tr("LoTW: sending %n QSO with TQSL…", nullptr, static_cast<int>(m_batch.size())), QStringLiteral("info"));
        m_tqsl.upload(path, location, static_cast<int>(m_batch.size()));
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
            note(tr("Club Log: sending %n QSO\u2026", nullptr, static_cast<int>(m_batch.size())), QStringLiteral("info"));
            m_web.uploadClubLog(auth, document, static_cast<int>(m_batch.size()));
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
            m_ctx.db->forgetRemote(id, m_busyService);
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

void QslController::markSent(qint64 id, const QString& service, const QslUploadResult& result)
{
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
    // La conferma ricevuta non si tocca: la scrive il download delle conferme.
    for (const QslState& existing : m_ctx.db->qslStatus(id)) {
        if (existing.service == service) {
            state.rcvd = existing.rcvd;
            state.rcvdDate = existing.rcvdDate;
        }
    }
    m_ctx.db->setQslState(id, state);
    m_accepted += result.accepted;
    m_duplicates += result.duplicates;
    m_rejected += result.rejected;
}

void QslController::finishBatch(const QslUploadResult& result)
{
    countsDirty();
    const QString service = m_busyService;
    if (m_batchMode) {
        if (result.ok) {
            for (qint64 id : m_batch)
                markSent(id, service, result.duplicates > 0 && result.accepted == 0
                                          ? [] { QslUploadResult r; r.ok = true; r.duplicates = 1; return r; }()
                                          : [] { QslUploadResult r; r.ok = true; r.accepted = 1; return r; }());
        } else {
            for (qint64 id : m_batch) {
                QslUploadResult failed = result;
                failed.accepted = failed.duplicates = 0;
                markSent(id, service, failed);
            }
        }
        if (!m_adifFile.isEmpty()) {
            QFile::remove(m_adifFile);
            m_adifFile.clear();
        }
    }

    m_secret.clear();
    m_credential.clear();
    m_batch.clear();
    m_busyService.clear();
    m_batchMode = false;

    QString summary = result.ok
        ? tr("%1: %2 sent, %3 already there, %4 rejected").arg(service).arg(m_accepted).arg(m_duplicates).arg(m_rejected)
        : result.message;
    if (result.ok && m_removed > 0)
        summary += tr(", %n deleted", nullptr, m_removed);
    m_lastResult.insert(service, summary);
    note(summary, result.ok ? QStringLiteral("success") : QStringLiteral("error"));
    if (!result.ok && !result.message.isEmpty() && result.message != summary)
        note(QStringLiteral("  ") + result.message, QStringLiteral("warning"));
    if (m_ctx.logChanged)
        m_ctx.logChanged();
    emit changed();
    // Uscire prima completamente dal callback del servizio appena concluso;
    // poi avviare il prossimo automatico. Evita di riusare nello stesso stack
    // TQSL o il client HTTP che ha appena emesso finished().
    QTimer::singleShot(0, this, &QslController::startNextAutomaticUpload);
}

void QslController::cancel()
{
    m_tqsl.cancel();
    m_batch.clear();
    m_deletions.clear();
    m_deletingId = 0;
    m_busyService.clear();
    m_batchMode = false;
    m_secret.clear();
    m_automaticUploads.clear();
    emit changed();
}

} // namespace decolog::app
