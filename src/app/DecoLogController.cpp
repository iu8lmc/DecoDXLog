#include "app/DecoLogController.h"

#include "core/NetworkError.h"
#include "core/Diagnostics.h"
#include "core/Performance.h"
#include "core/CloudSettings.h"
#include "core/QslUpload.h"
#include "core/LogMigration.h"
#include "../StartupTrace.h"

#include "core/Bands.h"
#include "core/Dates.h"
#include "core/Maidenhead.h"
#include "core/Modes.h"
#include "core/NewQsoFields.h"
#include "core/Spots.h"
#include "ThemeManager.h"

#include <QMetaObject>
#include <QPointer>
#include <QProcess>
#include <QCoreApplication>
#include <QBuffer>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QWindow>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QSaveFile>
#include <QHostAddress>
#include <QLocale>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QRegularExpression>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUdpSocket>
#include <QXmlStreamWriter>
#include <QCryptographicHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace decolog::app {

using namespace decolog::core;

namespace {

// Un client che non si fa sentire da tre battiti (15 s l'uno) e' andato via.
constexpr qint64 kClientTimeoutMs = 45'000;
constexpr int kMaxActivity = 300;
constexpr int kMaxIncoming = 50;

QString nowUtcLabel()
{
    return QDateTime::currentDateTimeUtc().toString(QStringLiteral("HH:mm:ss"));
}

QString serviceLabel(const QString& service)
{
    if (service == QLatin1String("lotw"))    return QStringLiteral("LoTW");
    if (service == QLatin1String("qrz"))     return QStringLiteral("QRZ");
    if (service == QLatin1String("clublog")) return QStringLiteral("ClubLog");
    if (service == QLatin1String("eqsl"))    return QStringLiteral("eQSL");
    if (service == QLatin1String("card"))    return QStringLiteral("Card");
    return service;
}

QList<core::awards::AwardRulePackage> localAwardRulePackages()
{
    const QString directory = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                                  .filePath(QStringLiteral("awards"));
    return core::awards::AwardRuleCatalog::loadDirectory(directory).packages();
}

const QStringList kServices{QStringLiteral("lotw"), QStringLiteral("qrz"), QStringLiteral("clublog"),
                            QStringLiteral("eqsl"), QStringLiteral("card")};

QString n1mmBand(const QString& adifBand, double mhz)
{
    // N1MM non trasmette la frequenza del QSO nel campo band: usa il bordo
    // basso della banda (20m = 14, 80m = 3.5). HamConnect usa proprio questo
    // valore per riconoscere la banda dell'attivazione.
    double lowMhz = 0.0;
    if (bands::edges(adifBand, &lowMhz, nullptr)) {
        QString value = QString::number(lowMhz, 'f', lowMhz < 10.0 ? 1 : 3);
        value.remove(QRegularExpression(QStringLiteral("0+$")));
        value.remove(QRegularExpression(QStringLiteral("\\.$")));
        return value;
    }
    if (mhz > 0.0) {
        QString value = QString::number(mhz, 'f', mhz < 10.0 ? 1 : 3);
        value.remove(QRegularExpression(QStringLiteral("0+$")));
        value.remove(QRegularExpression(QStringLiteral("\\.$")));
        return value;
    }
    QString value = adifBand.trimmed().toLower();
    if (value.endsWith(QLatin1Char('m')))
        value.chop(1);
    return value;
}

QString n1mmMode(const QString& mode)
{
    const QString upper = mode.trimmed().toUpper();
    return upper == QLatin1String("SSB") ? QStringLiteral("USB") : upper;
}

// Le colonne della griglia banda x modo: quelle che un operatore si aspetta di
// vedere sempre, anche vuote. Le altre si aggiungono solo se il log le ha.
const QStringList kSlotBands{
    QStringLiteral("160m"), QStringLiteral("80m"), QStringLiteral("40m"), QStringLiteral("30m"),
    QStringLiteral("20m"), QStringLiteral("17m"), QStringLiteral("15m"), QStringLiteral("12m"),
    QStringLiteral("10m"), QStringLiteral("6m"), QStringLiteral("2m"), QStringLiteral("70cm")};

// La griglia gia' montata: le colonne, e per ogni riga (CW, digitale, fonia) una
// casella per colonna. Vuota dove non si e' lavorato: in QML resta un buco grigio.
QVariantMap slotGrid(const QList<LogDatabase::BandModeSlot>& worked)
{
    QStringList columns = kSlotBands;
    // Una banda fuori dall'elenco (630m, 23cm, un satellite) non si butta via:
    // va in fondo, nell'ordine delle bande.
    QStringList extra;
    for (const auto& s : worked) {
        if (!s.band.isEmpty() && !columns.contains(s.band) && !extra.contains(s.band))
            extra << s.band;
    }
    const QStringList order = bands::all();
    std::sort(extra.begin(), extra.end(), [&order](const QString& a, const QString& b) {
        return order.indexOf(a) < order.indexOf(b);
    });
    columns << extra;

    struct Row { const char* group; QString label; };
    const QVector<Row> rows{
        {"CW",    QStringLiteral("CW")},
        {"DATA",  DecoLogController::tr("Digital")},
        {"PHONE", DecoLogController::tr("Phone")},
    };

    QVariantList out;
    for (const Row& r : rows) {
        QVariantList cells;
        for (const QString& band : std::as_const(columns)) {
            QVariantMap cell{{QStringLiteral("band"), band}, {QStringLiteral("count"), 0}};
            for (const auto& s : worked) {
                if (s.band != band || s.group != QLatin1String(r.group))
                    continue;
                // Le lettere sono quelle dei diplomi: L LoTW, e eQSL, C Club Log,
                // Q QRZ, K la cartolina in mano.
                QString marks;
                if (s.lotw)    marks += QLatin1Char('L');
                if (s.eqsl)    marks += QLatin1Char('e');
                if (s.clublog) marks += QLatin1Char('C');
                if (s.qrz)     marks += QLatin1Char('Q');
                if (s.card)    marks += QLatin1Char('K');
                cell[QStringLiteral("count")] = s.count;
                cell[QStringLiteral("confirmed")] = s.confirmed();
                cell[QStringLiteral("marks")] = marks;
                break;
            }
            cells << cell;
        }
        out << QVariantMap{{QStringLiteral("group"), QString::fromLatin1(r.group)},
                           {QStringLiteral("label"), r.label},
                           {QStringLiteral("cells"), cells}};
    }
    return QVariantMap{{QStringLiteral("bands"), columns}, {QStringLiteral("rows"), out}};
}

// Campi che la scheda del QSO mostra nelle sue schede; tutto il resto e' "ADIF extra".
const QStringList kKnownFields{
    QStringLiteral("CALL"), QStringLiteral("QSO_DATE"), QStringLiteral("TIME_ON"), QStringLiteral("QSO_DATE_OFF"),
    QStringLiteral("TIME_OFF"), QStringLiteral("BAND"), QStringLiteral("BAND_RX"), QStringLiteral("FREQ"),
    QStringLiteral("FREQ_RX"), QStringLiteral("MODE"), QStringLiteral("SUBMODE"), QStringLiteral("RST_SENT"),
    QStringLiteral("RST_RCVD"), QStringLiteral("GRIDSQUARE"), QStringLiteral("NAME"), QStringLiteral("QTH"),
    QStringLiteral("COUNTRY"), QStringLiteral("DXCC"), QStringLiteral("CQZ"), QStringLiteral("ITUZ"),
    QStringLiteral("CONT"), QStringLiteral("STATE"), QStringLiteral("CNTY"), QStringLiteral("IOTA"),
    QStringLiteral("SOTA_REF"), QStringLiteral("POTA_REF"), QStringLiteral("WWFF_REF"),
    QStringLiteral("SIG"), QStringLiteral("SIG_INFO"), QStringLiteral("PROP_MODE"),
    QStringLiteral("SAT_NAME"), QStringLiteral("SAT_MODE"), QStringLiteral("TX_PWR"), QStringLiteral("COMMENT"), QStringLiteral("NOTES"),
    QStringLiteral("STATION_CALLSIGN"), QStringLiteral("OPERATOR"), QStringLiteral("MY_GRIDSQUARE"),
    QStringLiteral("LOTW_QSL_SENT"), QStringLiteral("LOTW_QSLSDATE"), QStringLiteral("LOTW_QSL_RCVD"),
    QStringLiteral("LOTW_QSLRDATE"), QStringLiteral("QRZCOM_QSO_UPLOAD_STATUS"), QStringLiteral("QRZCOM_QSO_UPLOAD_DATE"),
    QStringLiteral("QRZCOM_QSO_DOWNLOAD_STATUS"), QStringLiteral("QRZCOM_QSO_DOWNLOAD_DATE"),
    QStringLiteral("CLUBLOG_QSO_UPLOAD_STATUS"), QStringLiteral("CLUBLOG_QSO_UPLOAD_DATE"),
    QStringLiteral("EQSL_QSL_SENT"), QStringLiteral("EQSL_QSLSDATE"), QStringLiteral("EQSL_QSL_RCVD"),
    QStringLiteral("EQSL_QSLRDATE"), QStringLiteral("QSL_SENT"), QStringLiteral("QSLSDATE"), QStringLiteral("QSL_RCVD"),
    QStringLiteral("QSLRDATE"), QStringLiteral("APP_DECOLOG_TAGS")};

QVariantMap positionMap(const std::optional<maidenhead::LatLon>& p)
{
    if (!p)
        return {};
    return {{QStringLiteral("lat"), p->lat}, {QStringLiteral("lon"), p->lon}};
}

} // namespace

DecoLogController::DecoLogController(QObject* parent)
    : QObject(parent)
{
    m_activityModel = new ActivityModel(kMaxActivity, this);
    m_traffic = new TrafficMonitor(&m_udp, &m_decoLink, this);
    // I conteggi si rifanno un attimo dopo che il log e' cambiato, non mentre
    // si registra: una raffica di QSO costa un conteggio solo.
    m_countsTimer.setSingleShot(true);
    m_countsTimer.setInterval(400);
    connect(&m_countsTimer, &QTimer::timeout, this, [this] {
        // Si contano su un altro filo, con una connessione propria al log.
        if (!m_db.isOpen() || m_db.path().isEmpty() || m_db.path() == QLatin1String(":memory:")) {
            m_counts.valid = false;
            emit countsChanged();
            return;
        }
        const QString path = m_db.path();
        QPointer<DecoLogController> self(this);
        m_countsPool.start([self, path] {
            LogDatabase db;
            if (!db.open(path))
                return;
            Counts c;
            c.qsos = db.qsoCount();
            c.dirty = db.dirtyCount();
            c.conflicts = db.conflictCount();
            c.missingDxcc = static_cast<int>(db.idsWithoutDxcc().size());
            const QList<QVariantMap> summary = db.qslSummary();
            db.close();
            QMetaObject::invokeMethod(
                self.data(),
                [self, c, summary]() mutable {
                    if (!self)
                        return;
                    for (QVariantMap row : summary) {
                        row[QStringLiteral("label")] = serviceLabel(row.value(QStringLiteral("service")).toString());
                        c.qslSummary << row;
                    }
                    c.valid = true;
                    self->m_counts = c;
                    emit self->countsChanged();
                },
                Qt::QueuedConnection);
        });
    });
    m_countsPool.setMaxThreadCount(1);
    m_backupPool.setMaxThreadCount(1);
    connect(this, &DecoLogController::logChanged, this, [this] { m_countsTimer.start(); });
    // Register before QML observers: a notification must expose fresh data.
    connect(this, &DecoLogController::logChanged, this, [this] {
        m_gridPointsValid = false;
    });
    QSettings s;
    m_udpPort = s.value(QStringLiteral("udp/port"), 2237).toInt();
    m_multicast = s.value(QStringLiteral("udp/multicastGroup")).toString();
    m_udpForward = s.value(QStringLiteral("udp/forward")).toString();
    m_n1mmPort = s.value(QStringLiteral("n1mm/port"), 0).toInt();
    m_apiPort = s.value(QStringLiteral("api/port"), 0).toInt();
    m_apiToken = s.value(QStringLiteral("api/token")).toString();
    connect(&m_n1mm, &N1mmReceiver::contactReceived, this, [this](const AdifRecord& record, const QString&) {
        onQsoReceived(record, QStringLiteral("n1mm"), QStringLiteral("N1MM Logger+"));
    });
    connect(&m_n1mm, &N1mmReceiver::contactReplaced, this, &DecoLogController::onN1mmReplaced);
    connect(&m_n1mm, &N1mmReceiver::contactDeleted, this, &DecoLogController::onN1mmDeleted);
    m_udp.setForwardTargets(UdpReceiver::parseTargets(m_udpForward));
    m_udp.setPreferLoggedAdif(s.value(QStringLiteral("udp/preferLoggedAdif"), true).toBool());
    m_followDx = s.value(QStringLiteral("udp/followDxCall"), true).toBool();
    m_db.setDedupWindows(s.value(QStringLiteral("log/dedupDigitalMinutes"), 2).toInt() * 60,
                         s.value(QStringLiteral("log/dedupManualMinutes"), 10).toInt() * 60);

    m_backupEnabled = s.value(QStringLiteral("backup/enabled"), true).toBool();
    m_backupDir = s.value(QStringLiteral("backup/dir")).toString();
    m_backupTime = s.value(QStringLiteral("backup/time"), QStringLiteral("02:00")).toString();
    m_backupKeep = s.value(QStringLiteral("backup/keep"), 14).toInt();

    m_cloudServer = s.value(QStringLiteral("cloud/server")).toString();
    m_autoSync = s.value(QStringLiteral("cloud/autoSync"), QStringLiteral("qso+5min")).toString();
    m_conflictPolicy = s.value(QStringLiteral("cloud/conflictPolicy"), QStringLiteral("lastEdit")).toString();

    connect(&m_udp, &UdpReceiver::qsoReceived, this, &DecoLogController::onQsoReceived);
    connect(&m_udp, &UdpReceiver::listeningChanged, this, &DecoLogController::udpChanged);
    connect(&m_udp, &UdpReceiver::clientSeen, this, [this](const UdpClientInfo& c) {
        const bool wasConnected = clientConnected();
        const bool changed = c.id != m_clientName || (!c.version.isEmpty() && c.version != m_clientVersion);
        m_clientName = c.id;
        if (!c.version.isEmpty())
            m_clientVersion = c.version;
        m_clientLastSeen = c.lastSeen;
        if (!wasConnected)
            addActivity(QStringLiteral("UDP"), tr("%1 connected from %2").arg(c.id, c.address.toString()));
        if (changed || !wasConnected)
            emit clientChanged();
    });
    connect(&m_udp, &UdpReceiver::clientClosed, this, [this](const QString& id) {
        addActivity(QStringLiteral("UDP"), tr("%1 closed").arg(id));
        m_clientLastSeen = {};
        m_status = {};
        emit clientChanged();
    });
    connect(&m_udp, &UdpReceiver::statusReceived, this,
            [this](const QString&, const wsjtx::Status& st) {
                const bool dxChanged = st.dxCall != m_status.dxCall;
                const bool gridChanged = st.deGrid != m_status.deGrid;
                m_status = st;
                emit clientChanged();
                // Dove si e' adesso lo sa solo questo computer: il Cloud lo
                // riceve perche' lo si veda da lontano, con misura.
                reportPresenceToCloud();
                if (gridChanged)
                    emit stationChanged();
                maybeCreateProfileFromDecodium();
                // Il nominativo che Decodium sta lavorando e' quello che interessa
                // adesso: il pannello a destra lo segue da solo.
                if (m_followDx && dxChanged && !st.dxCall.isEmpty())
                    setLookupCall(st.dxCall);
            });

    m_clientWatch.setInterval(5000);
    connect(&m_clientWatch, &QTimer::timeout, this, [this] {
        if (m_clientLastSeen.isValid()
            && m_clientLastSeen.msecsTo(QDateTime::currentDateTimeUtc()) > kClientTimeoutMs) {
            addActivity(QStringLiteral("UDP"), tr("%1 not heard for 45 s").arg(m_clientName), QStringLiteral("warning"));
            m_clientLastSeen = {};
            emit clientChanged();
        }
    });
    m_clientWatch.start();

    loadCountries();

    m_awardFilter.band = s.value(QStringLiteral("awards/band")).toString();
    m_awardFilter.modeGroup = s.value(QStringLiteral("awards/modeGroup")).toString();
    m_awardFilter.confirmLotw = s.value(QStringLiteral("awards/confirmLotw"), true).toBool();
    m_awardFilter.confirmCard = s.value(QStringLiteral("awards/confirmCard"), true).toBool();
    m_awardFilter.confirmEqsl = s.value(QStringLiteral("awards/confirmEqsl"), false).toBool();
    // Il profilo ufficiale ARRL esclude i 60 metri; il valore true salvato e'
    // l'override personale dell'operatore.
    m_awardFilter.count60m = s.value(QStringLiteral("awards/count60m"), false).toBool();
    // Un account DXCC ufficiale non deve sommare automaticamente casa,
    // portatile e altri nominativi. Per una nuova installazione si parte dal
    // profilo attivo; una scelta gia' salvata (anche "All stations" = 0) e'
    // invece una decisione esplicita dell'operatore e viene rispettata.
    m_awardFilter.stationProfileId = s.contains(QStringLiteral("awards/profile"))
        ? s.value(QStringLiteral("awards/profile")).toLongLong()
        : (m_profiles ? m_profiles->activeProfileId() : 0);
    m_awardFilter.tag = s.value(QStringLiteral("awards/tag")).toString();
    // Le conferme scelte per i singoli diplomi.
    s.beginGroup(QStringLiteral("awards/credits"));
    for (const QString& award : s.childKeys())
        m_awardFilter.credits.insert(award, s.value(award).toString().split(QLatin1Char(','), Qt::SkipEmptyParts));
    s.endGroup();
    // Gli award si ricalcolano quando il log cambia, e solo quando qualcuno li guarda.
    // I diplomi e le statistiche di tutto il log si rifanno quando si smette di
    // scrivere, non a ogni QSO: rifarli subito voleva dire tenere il programma
    // fermo proprio mentre si registra, che in gara e' il momento peggiore.
    m_statsDebounce.setSingleShot(true);
    m_statsDebounce.setInterval(1200);
    m_statsPool.setMaxThreadCount(1);
    connect(&m_statsDebounce, &QTimer::timeout, this, [this] {
        // Diplomi, FT2 e conti si rifanno tutti in secondo piano; fino al
        // risultato vale quello di prima, senza fermare la finestra.
        refreshStatsInBackground();
    });
    connect(this, &DecoLogController::logChanged, this, [this] { m_statsDebounce.start(); });
    // DecoLink: il log verso Decodium. I dati li fornisce il controller.
    m_decoLinkEnabled = s.value(QStringLiteral("decolink/enabled"), true).toBool();
    m_decoLinkPort = s.value(QStringLiteral("decolink/port"), DecoLinkServer::kDefaultPort).toInt();
    m_hamHubLinkEnabled = s.value(QStringLiteral("hamhublink/enabled"), true).toBool();
    m_hamHubLinkPort = s.value(QStringLiteral("hamhublink/port"), decoportlog::kDefaultSessionPort).toInt();
    m_decoLink.workedRows = [this] {
        return m_db.workedRows(m_awardFilter.confirmLotw, m_awardFilter.confirmCard, m_awardFilter.confirmEqsl);
    };
    // L'elenco per Decodium si prepara su un altro filo, con una connessione
    // sua: su un log da un milione sono decine di secondi, e il programma non
    // deve fermarsi quando Decodium si collega.
    m_linkPool.setMaxThreadCount(1);
    m_importPool.setMaxThreadCount(1);
    m_statsViewPool.setMaxThreadCount(1);
    m_callInfoPool.setMaxThreadCount(1);
    // Ogni cambio del log fa vecchi i conti dell'entita' nella scheda.
    connect(this, &DecoLogController::logChanged, this, [this] { ++m_logVersion; });
    m_decoLink.buildSnapshot = [this](std::function<void(const QList<QByteArray>&)> done) {
        const bool lotw = m_awardFilter.confirmLotw;
        const bool card = m_awardFilter.confirmCard;
        const bool eqsl = m_awardFilter.confirmEqsl;
        const QString path = m_db.path();
        if (path.isEmpty() || path == QLatin1String(":memory:")) {
            done(DecoLinkServer::snapshotLines(m_db.workedRows(lotw, card, eqsl)));
            return;
        }
        QPointer<DecoLogController> self(this);
        m_linkPool.start([self, path, lotw, card, eqsl, done] {
            QList<QByteArray> lines;
            {
                LogDatabase db;
                if (db.open(path))
                    lines = DecoLinkServer::snapshotLines(db.workedRows(lotw, card, eqsl));
            }
            QMetaObject::invokeMethod(
                self.data(), [self, done, lines] {
                    if (self)
                        done(lines);
                },
                Qt::QueuedConnection);
        });
    };
    m_decoLink.awardState = [this] { return decoLinkAward(); };
    m_decoLink.resolveQuery = [this](const QJsonObject& q) { return decoLinkQuery(q); };
    connect(&m_decoLink, &DecoLinkServer::listeningChanged, this, &DecoLogController::decoLinkChanged);
    connect(&m_decoLink, &DecoLinkServer::listeningRecovered, this, [this] {
        addActivity(QStringLiteral("LINK"),
                    tr("DecoLink: the port is free again, listening on 127.0.0.1:%1").arg(m_decoLinkPort),
                    QStringLiteral("success"));
    });
    connect(&m_decoLink, &DecoLinkServer::clientsChanged, this, [this] {
        const int n = m_decoLink.clientCount();
        static int previous = 0;
        if (n != previous)
            addActivity(QStringLiteral("LINK"), n > previous ? tr("DecoLink: client connected (%1)").arg(n)
                                                             : tr("DecoLink: client disconnected (%1 left)").arg(n));
        previous = n;
        emit decoLinkChanged();
    });
    // HH su DecoPort: chi si collega, e se i QSO sono arrivati.
    connect(&m_hamHubLink, &DecoPortLogServer::clientsChanged, this, [this] {
        static int previous = 0;
        const int n = m_hamHubLink.clientCount();
        if (n != previous)
            addActivity(QStringLiteral("HAMHUB"), n > previous ? tr("HH connected over DecoPort")
                                                                : tr("HH disconnected"));
        previous = n;
        emit hamHubLinkChanged();
    });
    connect(&m_hamHubLink, &DecoPortLogServer::qsoAcked, this, [this] { emit hamHubLinkChanged(); });
    connect(&m_hamHubLink, &DecoPortLogServer::qsoGaveUp, this, [this](qint64 id) {
        addActivity(QStringLiteral("HAMHUB"), tr("HH did not confirm QSO #%1: it was not delivered over DecoPort").arg(id),
                    QStringLiteral("warning"));
        emit hamHubLinkChanged();
    });
    connect(&m_hamHubLink, &DecoPortLogServer::diagnostic, this,
            [this](const QString& line) { addActivity(QStringLiteral("HAMHUB"), line, QStringLiteral("warning")); });
    // Lo stato dell'award non a ogni QSO di un import: al piu' uno al secondo.
    m_decoLinkAwardDebounce.setSingleShot(true);
    m_decoLinkAwardDebounce.setInterval(1000);
    connect(&m_decoLinkAwardDebounce, &QTimer::timeout, this, [this] { m_decoLink.broadcastAward(); });
    // Il broadcast dei diplomi lo fa il conto in secondo piano, quando ha finito.

    // Un cty.csv nuovo cambia i nomi delle entita'.
    connect(this, &DecoLogController::countriesChanged, this, [this] {
        m_awardsDirty = m_globalAwardsDirty = true;
        emit awardsChanged();
    });

    m_credentials = new CredentialStore(QStringLiteral("DecoDXLog"), this);
    connect(m_credentials, &CredentialStore::finished, this,
            [this](const QString& service, bool ok, const QString& message) {
                // Mai il segreto: solo il servizio e l'esito.
                addActivity(QStringLiteral("KEYS"), QStringLiteral("%1: %2").arg(service, message),
                            ok ? QStringLiteral("info") : QStringLiteral("error"));
                // La password del collegamento con HH: la sessione si riapre con la nuova chiave.
                if (service == QLatin1String("hamhub"))
                    startHamHubLink();
                // Credenziali cambiate: la sessione del callbook va rifatta.
                if (service == QLatin1String("qrz") || service == QLatin1String("hamqth")) {
                    m_callbook.reset();
                    m_callbookErrors.clear();
                    m_callbookStatus.clear();
                    emit callbookChanged();
                    requestCallbook();
                }
            });

    m_callbook.setCredentialReaders(
        [this](const QString& service) { return m_credentials->account(service); },
        [this](const QString& service, std::function<void(const QString&, const QString&)> done) {
            m_credentials->readSecret(service, std::move(done));
        });
    m_callbook.setProvider(CallbookClient::providerFromId(
        s.value(QStringLiteral("callbook/provider"), QStringLiteral("off")).toString()));
    m_callbookAutofill = s.value(QStringLiteral("callbook/autofill"), true).toBool();
    // Completare i QSO appena scritti: chi ha un callbook lo vuole, e chi non
    // ce l'ha non se ne accorge.
    m_callbookComplete = s.value(QStringLiteral("callbook/completeLogged"), true).toBool();
    m_callbookFallback = s.value(QStringLiteral("callbook/fallback"), true).toBool();
    m_callbook.setFallbackEnabled(m_callbookFallback);
    connect(&m_callbook, &CallbookClient::found, this, [this](const QString& call, const CallbookRecord& record) {
        m_callbookResults.insert(call, record.toMap());
        m_callbookErrors.remove(call);
        if (call == m_callbookPending)
            m_callbookPending.clear();
        m_callbookStatus = tr("%1: %2 found").arg(record.source, call);
        emit callbookChanged();
        if (call == m_lookupCall)
            refreshCallInfo();
        // Chi aspettava l'email di questo nominativo — l'invio delle QSL —
        // la riceve adesso, una risposta per tutti.
        const QString email = record.email.trimmed();
        for (const auto& done : m_awaitingEmail.take(call)) {
            done(email, email.isEmpty()
                            ? tr("%1 is in the callbook but has no email there").arg(call)
                            : QString());
        }
        // I QSO che aspettavano questo nominativo si completano adesso.
        const QList<qint64> waiting = m_awaitingCallbook.take(call);
        for (qint64 id : waiting) {
            const QStringList filled = applyCallbookToQso(id, record.toMap());
            if (!filled.isEmpty()) {
                addActivity(QStringLiteral("CALLBOOK"),
                            tr("%1: %2 completed from %3 (%4)")
                                .arg(call, tr("QSO"), record.source, filled.join(QStringLiteral(", "))),
                            QStringLiteral("success"));
            }
        }
    });
    connect(&m_callbook, &CallbookClient::failed, this, [this](const QString& call, const QString& message) {
        m_callbookErrors.insert(call, message);
        if (call == m_callbookPending)
            m_callbookPending.clear();
        // Chi aspettava resta com'e': un QSO senza nome e' meglio di un QSO con
        // un nome inventato.
        m_awaitingCallbook.remove(call);
        for (const auto& done : m_awaitingEmail.take(call))
            done(QString(), message);
        // Credenziali sbagliate o rete assente: una riga nel registro, non una per nominativo.
        if (message != m_callbookStatus && !message.contains(QLatin1String("not found"), Qt::CaseInsensitive))
            addActivity(QStringLiteral("CALLBOOK"), message, QStringLiteral("warning"));
        m_callbookStatus = message;
        emit callbookChanged();
        if (call == m_lookupCall)
            refreshCallInfo();
    });
    // La coda dei lavori di gruppo: una ricerca ogni mezzo secondo, cosi' il
    // servizio non si arrabbia e il programma resta vivo.
    m_callbookQueueTimer.setInterval(500);
    connect(&m_callbookQueueTimer, &QTimer::timeout, this, &DecoLogController::serveCallbookQueue);

    // Si cerca quando si smette di scrivere, non a ogni lettera.
    m_callbookDebounce.setSingleShot(true);
    m_callbookDebounce.setInterval(600);
    connect(&m_callbookDebounce, &QTimer::timeout, this, &DecoLogController::requestCallbook);

    m_backupTimer.setInterval(60'000);
    connect(&m_backupTimer, &QTimer::timeout, this, &DecoLogController::checkBackupSchedule);

    m_lotwAutoHours = s.value(QStringLiteral("lotw/autoSyncHours"), 12).toInt();
    m_confirmAutoHours = s.value(QStringLiteral("confirmations/autoSyncHours"), 12).toInt();
    m_recoveryEnabled = s.value(QStringLiteral("decodium/recoverFromLog"), true).toBool();
    m_decodiumLogPath = s.value(QStringLiteral("decodium/logPath")).toString();
    m_recoveryPool.setMaxThreadCount(1);
    m_ctyPool.setMaxThreadCount(1);
    // Il cty.xml di Club Log, se c'e', si legge su un altro filo: sono alcuni MB.
    QTimer::singleShot(0, this, &DecoLogController::loadClubLogCty);
    m_recoveryTimer.setInterval(5 * 60'000);
    connect(&m_recoveryTimer, &QTimer::timeout, this, &DecoLogController::checkDecodiumRecovery);
    connect(&m_lotw, &LotwClient::finished, this, &DecoLogController::onLotwReport);
    connect(&m_confirmDownloader, &ConfirmationDownloader::finished, this, &DecoLogController::onConfirmationReport);
    connect(&m_eqslCards, &EqslCardFetcher::ready, this,
            [this](const QString& key, const QString& file, const QString& error) {
                emit eqslCardReady(key, file.isEmpty() ? QString() : QUrl::fromLocalFile(file).toString(), error);
            });
    connect(&m_lotw, &LotwClient::progress, this, [this](qint64 bytes) {
        m_lotwStatus = tr("LoTW: downloading… %1 kB").arg(bytes / 1024);
        emit lotwChanged();
    });
    // Il sync automatico si controlla ogni dieci minuti; il primo poco dopo
    // l'avvio, quando la finestra e' gia' su.
    m_lotwTimer.setInterval(10 * 60'000);
    connect(&m_lotwTimer, &QTimer::timeout, this, &DecoLogController::checkLotwSchedule);
    connect(&m_lotwTimer, &QTimer::timeout, this, &DecoLogController::checkConfirmSchedule);
}

void DecoLogController::testPointer(QObject* target, const QString& kind, qreal x, qreal y)
{
    auto* window = qobject_cast<QWindow*>(target);
    if (!window)
        return;
    const QPointF local(x, y);
    const QPointF global = window->mapToGlobal(local);
    QEvent::Type type = QEvent::MouseMove;
    Qt::MouseButton button = Qt::NoButton;
    Qt::MouseButtons buttons = Qt::LeftButton;
    if (kind == QLatin1String("press")) {
        type = QEvent::MouseButtonPress;
        button = Qt::LeftButton;
    } else if (kind == QLatin1String("hover")) {
        buttons = Qt::NoButton;
    } else if (kind == QLatin1String("release")) {
        type = QEvent::MouseButtonRelease;
        button = Qt::LeftButton;
        buttons = Qt::NoButton;
    }
    QMouseEvent event(type, local, local, global, button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &event);
}

DecoLogController::~DecoLogController()
{
    // Chiudendo il programma i membri si distruggono uno per uno, e alcuni
    // mandano ancora un segnale mentre se ne vanno: DecoLink, fermandosi,
    // dice che i client se ne sono andati, e quel segnale scriveva nel
    // registro attivita' — che a quel punto era gia' stato distrutto. Era la
    // caduta "in emplace<QVariant>" del registro di Windows, dalla 1.7 in poi.
    // Adesso DecoLink si ferma qui, con tutto ancora in piedi, e poi niente di
    // quello che resta puo' piu' chiamare questo oggetto.
    shutdown();
}

void DecoLogController::shutdown()
{
    if (m_shuttingDown)
        return;
    m_shuttingDown = true;

    // Fermare prima le sorgenti di eventi: durante la distruzione non devono
    // piu' arrivare datagrammi, scadenze o callback che riempiono l'attivita'.
    m_clientWatch.stop();
    m_recoveryTimer.stop();
    m_lotwTimer.stop();
    m_statsDebounce.stop();
    m_callbookQueueTimer.stop();
    m_callbookDebounce.stop();
    m_countsTimer.stop();
    m_backupTimer.stop();
    m_decoLinkAwardDebounce.stop();
    m_freezeBeat.stop();
    m_udp.stop();
    m_n1mm.stop();
    m_api.stop();
    m_decoLink.stop();
    m_hamHubLink.stop();

    if (m_dvk)
        m_dvk->stop();
    if (m_rig)
        m_rig->shutdown();
    if (m_cluster)
        m_cluster->stopVoice();
    if (m_chat)
        m_chat->disconnectChat();

    // Le richieste gia' in esecuzione terminano sulle loro copie e non hanno
    // piu' motivo di produrre un risultato; quelle in coda non devono allungare
    // l'uscita dell'applicazione.
    ++m_statsGeneration;
    ++m_logVersion;
    m_statsLatest->fetch_add(1);
    for (QThreadPool* pool : {&m_ctyPool, &m_recoveryPool, &m_statsPool,
                              &m_countsPool, &m_backupPool, &m_linkPool,
                              &m_importPool, &m_statsViewPool, &m_callInfoPool})
        pool->clear();
}

// Diplomi e statistiche di tutto il log, calcolati fuori dal thread della
// finestra: su un log di quindicimila QSO sono un quarto di secondo abbondante,
// e in gara un quarto di secondo fermo dopo ogni QSO si sente.
//
// Il thread lavora su copie (il percorso del log, il filtro, i nomi delle
// entita') e su una connessione sua: niente di quello che tocca e' condiviso
// con la finestra. Il risultato torna qui con un evento, e se nel frattempo e'
// partito un calcolo piu' nuovo, o e' cambiato il filtro, si butta.
void DecoLogController::refreshStatsInBackground()
{
    if (!m_db.isOpen())
        return;
    const QString path = m_db.path();
    const AwardFilter filter = m_awardFilter;
    QHash<int, QString> names;
    for (const auto& entity : m_countries.entities())
        names.insert(entity.dxcc, entity.name);
    const quint64 generation = ++m_statsGeneration;
    const qint64 startedAt = QDateTime::currentMSecsSinceEpoch();
    QPointer<DecoLogController> self(this);
    const AwardCalculator calc = awardCalculator(names);

    m_statsPool.start([self, path, filter, calc, generation, startedAt] {
        LogDatabase db;
        if (!db.open(path))
            return;
        const QList<AwardResult> awards = calc.compute(db, filter);
        // Quelli di tutto il log, per Decodium: prima si rifacevano sul filo
        // dell'interfaccia alla prima domanda di DecoLink dopo ogni QSO.
        AwardFilter all;
        all.confirmLotw = filter.confirmLotw;
        all.confirmCard = filter.confirmCard;
        all.confirmEqsl = filter.confirmEqsl;
        all.count60m = filter.count60m;
        all.credits = filter.credits;
        // Senza banda, modo, profilo o etichetta scelti sono gli stessi: un
        // conto solo, che su un log grande sono secondi risparmiati.
        const bool unfiltered = filter.band.isEmpty() && filter.modeGroup.isEmpty()
                                && filter.stationProfileId == 0 && filter.tag.isEmpty();
        const QList<AwardResult> global = unfiltered ? awards : calc.compute(db, all);
        const Ft2Award ft2 = db.ft2Award();
        const QList<CountRow> bands = db.countByBand();
        const QList<CountRow> modes = db.countByMode();
        db.close();

        QMetaObject::invokeMethod(
            self.data(),
            [self, filter, awards, global, ft2, bands, modes, generation, startedAt] {
                if (!self || generation != self->m_statsGeneration)
                    return;
                self->recordUiLatency(QStringLiteral("stats.awards"),
                                      QDateTime::currentMSecsSinceEpoch() - startedAt);
                self->m_globalAwardCache = global;
                self->m_globalAwardsDirty = false;
                const AwardFilter& now = self->m_awardFilter;
                const bool sameFilter = now.band == filter.band && now.modeGroup == filter.modeGroup
                                        && now.confirmLotw == filter.confirmLotw
                                        && now.confirmCard == filter.confirmCard
                                        && now.confirmEqsl == filter.confirmEqsl
                                        && now.count60m == filter.count60m
                                        && now.stationProfileId == filter.stationProfileId
                                        && now.tag == filter.tag;
                if (sameFilter) {
                    self->m_awardCache = awards;
                    self->m_awardsDirty = false;
                }
                QVariantList bandRows;
                for (const auto& row : bands)
                    bandRows << QVariantMap{{QStringLiteral("key"), row.key},
                                            {QStringLiteral("count"), row.count}};
                QVariantList modeRows;
                for (const auto& row : modes)
                    modeRows << QVariantMap{{QStringLiteral("key"), row.key},
                                            {QStringLiteral("count"), row.count}};
                self->m_statsCache.clear();
                self->m_statsCache.insert(QStringLiteral("ft2"), QVariantMap{
                    {QStringLiteral("qsos"), ft2.qsos},
                    {QStringLiteral("dxccWorked"), ft2.dxccWorked},
                    {QStringLiteral("dxccConfirmed"), ft2.dxccConfirmed},
                    {QStringLiteral("gridsWorked"), ft2.gridsWorked},
                    {QStringLiteral("gridsConfirmed"), ft2.gridsConfirmed},
                });
                self->m_statsCache.insert(QStringLiteral("bands"), bandRows);
                self->m_statsCache.insert(QStringLiteral("modes"), modeRows);
                // Se il filtro e' cambiato mentre si contava, i diplomi si
                // rifanno alla prima lettura, con il filtro giusto.
                if (!sameFilter)
                    self->m_awardsDirty = true;
                emit self->statsChanged();
                emit self->awardsChanged();
                // Decodium sente i diplomi nuovi adesso che sono pronti.
                if (self->m_decoLink.clientCount() > 0)
                    self->m_decoLink.broadcastAward();
            },
            Qt::QueuedConnection);
    });
}

bool DecoLogController::openDatabase(const QString& path)
{
    m_gridPointsValid = false;
    m_gridPointsCache.clear();
    const bool ok = m_db.open(path);
    // L'elenco dei log si tiene aggiornato da solo: quello che si apre entra
    // nell'elenco e diventa il piu' recente.
    if (!m_logs)
        m_logs = new LogLibrary(this);
    if (ok)
        m_logs->setCurrent(path);
    if (ok) {
        addActivity(QStringLiteral("LOG"), tr("Log opened: %1 (%n QSO)", nullptr, m_db.qsoCount()).arg(path));
    } else {
        addActivity(QStringLiteral("LOG"), tr("Cannot open log %1: %2").arg(path, m_db.lastError()),
                    QStringLiteral("error"));
    }
    if (m_backupDir.isEmpty())
        m_backupDir = QDir(QFileInfo(path).absolutePath()).filePath(QStringLiteral("backup"));
    if (ok) {
        // L'ultimo riepilogo delle conferme scaricate: si riapre anche domani.
        m_qslImport = QJsonDocument::fromJson(m_db.setting(QStringLiteral("qsl.import_summary")).toUtf8())
                          .object().toVariantMap();
    }

    // Giapponese e cinese senza i caratteri giusti: la finestra si riempie di
    // quadratini e sembra rotto il programma. Non lo e': mancano i caratteri.
    if (decodium::ui::ThemeManager::ideographsMissing()) {
        addActivity(QStringLiteral("LOG"),
                    tr("This computer has no font with ideographs: the writing shows up as "
                       "little boxes. On Windows they arrive with the language: Settings → "
                       "Time & language → Language → Add a language."),
                    QStringLiteral("warning"));
    }

    timed(tr("loading the log table"), [this] { m_model = new QsoTableModel(&m_db, this); });
    // Le righe prendono la prima categoria che ha un colore acceso: il modello
    // deve sapere quali sono, adesso e ogni volta che cambiano.
    m_model->setActiveCategories(logColors().keys());
    connect(this, &DecoLogController::logColorsChanged, m_model,
            [this] { m_model->setActiveCategories(logColors().keys()); });
    m_profiles = new StationProfileModel(&m_db, this);
    connect(m_profiles, &StationProfileModel::activeChanged, this, [this] {
        emit stationChanged();
        refreshCallInfo();
    });
    connect(m_profiles, &StationProfileModel::profilesChanged, this, &DecoLogController::stationChanged);

    ClusterController::Context ctx;
    ctx.db = &m_db;
    ctx.countries = &m_countries;
    ctx.credentials = m_credentials;
    ctx.decoLink = &m_decoLink;
    ctx.stationCall = [this] {
        const QString call = m_profiles->activeProfile().value(QStringLiteral("stationCallsign")).toString();
        return call.isEmpty() ? m_status.deCall : call;
    };
    ctx.stationGrid = [this] { return myGrid(); };
    ctx.spotSeen = [this](const EnrichedSpot& e) {
        if (!m_rotor || !e.hasPosition)
            return;
        if (auto* gw = m_rotor->gateway(); gw && gw->running())
            gw->noteCluster(e.spot.dxCall, e.lat, e.lon, e.entity, e.spot.mode,
                            static_cast<quint64>(e.spot.freqKhz * 1000.0), e.spot.comment);
    };
    ctx.propagationForecast = [this](const QString& grid, const QString& band,
                                     const QString& mode) {
        if (!m_solar || grid.trimmed().isEmpty())
            return QVariantMap{{QStringLiteral("available"), false}};
        const QVariantMap forecast = m_solar->pathForecast(grid);
        const QStringList bands = forecast.value(QStringLiteral("bands")).toStringList();
        const QVariantList hours = forecast.value(QStringLiteral("hours")).toList();
        const int hour = forecast.value(QStringLiteral("currentHour"), -1).toInt();
        const int bandIndex = bands.indexOf(band);
        if (bandIndex < 0 || hour < 0 || hour >= hours.size())
            return QVariantMap{{QStringLiteral("available"), false},
                               {QStringLiteral("engine"), forecast.value(QStringLiteral("engine"))}};
        const QVariantList quality = hours.at(hour).toMap().value(QStringLiteral("quality")).toList();
        if (bandIndex >= quality.size() || !quality.at(bandIndex).isValid())
            return QVariantMap{{QStringLiteral("available"), false},
                               {QStringLiteral("engine"), forecast.value(QStringLiteral("engine"))}};
        return QVariantMap{{QStringLiteral("available"), true},
                           {QStringLiteral("quality"), quality.at(bandIndex).toInt()},
                           {QStringLiteral("engine"), forecast.value(QStringLiteral("engine"))},
                           {QStringLiteral("mode"), mode}};
    };
    ctx.decodiumBand = [this] { return clientConnected() ? dialBand() : QString(); };
    ctx.confirmations = [this](bool& lotw, bool& card, bool& eqsl) {
        lotw = m_awardFilter.confirmLotw;
        card = m_awardFilter.confirmCard;
        eqsl = m_awardFilter.confirmEqsl;
    };
    ctx.activity = [this](const QString& category, const QString& text, const QString& level) {
        addActivity(category, text, level);
    };
    ctx.lookup = [this](const QString& call) { setLookupCall(call); };
    ctx.prepareQso = [this](const QVariantMap& fields) { emit qsoPrepared(fields); };
    // Il doppio clic su uno spot porta la radio dove sta il DX: frequenza e
    // modo, tradotto in quello che vuole Hamlib.
    ctx.tuneRadio = [this](double mhz, const QString& mode) {
        auto* rig = qobject_cast<RigController*>(m_rig);
        if (!rig || !rig->connected() || mhz <= 0)
            return false;
        rig->tuneTo(static_cast<qint64>(std::llround(mhz * 1e6)),
                    mode.isEmpty() ? QString() : modes::catFor(mode, mhz));
        return true;
    };
    m_cluster = new ClusterController(std::move(ctx), this);

    // Full Spectrum e Signal RX di Decodium, ricostruiti dal protocollo UDP e
    // colorati con quello che solo il log sa.
    {
        DecodeFeed::Context feed;
        feed.myCalls = [this] {
            QStringList calls;
            if (m_profiles) {
                const QString profile = m_profiles->activeProfile().value(QStringLiteral("stationCallsign")).toString();
                if (!profile.isEmpty())
                    calls << profile;
            }
            if (!m_status.deCall.isEmpty() && !calls.contains(m_status.deCall))
                calls << m_status.deCall;
            return calls;
        };
        feed.classify = [this](const QString& call, const QString& band, const QString& mode) {
            const auto c = m_cluster->classifyCall(call, band, mode);
            return DecodeFeed::Classification{c.status, c.entity, c.azimuth, c.distanceKm};
        };
        feed.bandOf = [](const wsjtx::Status& st) {
            return st.dialFrequencyHz > 0 ? bands::fromMhz(static_cast<double>(st.dialFrequencyHz) / 1e6) : QString();
        };
        feed.statusLabel = [](int status) { return ClusterController::statusText(status); };
        feed.lookup = [this](const QString& call) { setLookupCall(call); };
        feed.prepareQso = [this](const QVariantMap& fields) { emit qsoPrepared(fields); };
        feed.historyPath = [] {
            // %LOCALAPPDATA%\Decodium\Decodium\db.sqlite: le decodifiche di Decodium 4.
            return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                   + QStringLiteral("/Decodium/Decodium/db.sqlite");
        };
        m_decodeFeed = new DecodeFeed(&m_udp, std::move(feed), this);
        // L'elenco di chi e' stato lavorato e' stato rifatto: gli stati vanno ricalcolati.
        connect(m_cluster, &ClusterController::indexRebuilt, m_decodeFeed, &DecodeFeed::restatus);
    }

    QslController::Context qslCtx;
    qslCtx.db = &m_db;
    qslCtx.credentials = m_credentials;
    qslCtx.stationLocation = [this] {
        return m_profiles->activeProfile().value(QStringLiteral("lotwStationLocation")).toString();
    };
    qslCtx.stationCallsign = [this] {
        return m_profiles->activeProfile().value(QStringLiteral("stationCallsign")).toString();
    };
    qslCtx.stationProfileName = [this] {
        return m_profiles->activeProfile().value(QStringLiteral("name")).toString();
    };
    qslCtx.activity = [this](const QString& category, const QString& text, const QString& level) {
        addActivity(category, text, level);
    };
    qslCtx.logChanged = [this] {
        timed(tr("reloading the log table"), [this] { m_model->reload(); });
        emit logChanged();
    };
    m_qsl = new QslController(std::move(qslCtx), this);
    // I conti della pagina QSL si rifanno quando cambia il log, su un altro filo.
    connect(this, &DecoLogController::logChanged, m_qsl, &QslController::countsDirty);

    QslCardController::Context cardCtx;
    cardCtx.db = &m_db;
    cardCtx.credentials = m_credentials;
    // Il Cloud come ponte per le QSL: cosi' la password di una casella non sta
    // sul computer di chi opera, ma solo sul server.
    cardCtx.cloudAccess = [this]() -> QPair<QString, QString> {
        auto* cloud = qobject_cast<CloudController*>(m_cloud);
        if (!cloud)
            return {};
        return {cloud->server(), cloud->token()};
    };
    cardCtx.replyTo = [this] {
        return m_profiles->activeProfile().value(QStringLiteral("email")).toString();
    };
    // L'email del corrispondente per mandargli la cartolina: la sa il callbook.
    cardCtx.emailFor = [this](const QString& call,
                              std::function<void(const QString&, const QString&)> done) {
        emailFor(call, std::move(done));
    };
    cardCtx.station = [this] {
        const QVariantMap profile = m_profiles->activeProfile();
        return QVariantMap{{QStringLiteral("call"), profile.value(QStringLiteral("stationCallsign"))},
                           {QStringLiteral("grid"), myGrid()}};
    };
    cardCtx.activity = [this](const QString& category, const QString& text, const QString& level) {
        addActivity(category, text, level);
    };
    cardCtx.logChanged = [this] {
        timed(tr("reloading the log table"), [this] { m_model->reload(); });
        emit logChanged();
    };
    m_cards = new QslCardController(std::move(cardCtx), this);

    SolarController::Context solarCtx;
    solarCtx.db = &m_db;
    solarCtx.activity = [this](const QString& category, const QString& text, const QString& level) {
        addActivity(category, text, level);
    };
    solarCtx.stationPosition = [this] { return myPosition(); };
    m_solar = new SolarController(std::move(solarCtx), this);

    // Gli aggiornamenti: una volta al giorno si guarda se e' uscita una
    // versione nuova, e se c'e' lo si dice. Scaricare e installare lo decide
    // chi opera.
    UpdateController::Context updCtx;
    updCtx.activity = [this](const QString& category, const QString& text, const QString& level) {
        addActivity(category, text, level);
    };
    updCtx.quit = [] { QCoreApplication::quit(); };
    m_updates = new UpdateController(std::move(updCtx), this);

    SuperCheckController::Context scpCtx;
    scpCtx.db = &m_db;
    scpCtx.activity = [this](const QString& category, const QString& text, const QString& level) {
        addActivity(category, text, level);
    };
    m_scp = new SuperCheckController(std::move(scpCtx), this);
    m_callLists = new CallListController({}, this);
    m_callLists->start();

    ChatController::Context chatCtx;
    chatCtx.credentials = m_credentials;
    chatCtx.activity = [this](const QString& category, const QString& text, const QString& level) {
        addActivity(category, text, level);
    };
    m_chat = new ChatController(std::move(chatCtx), this);

    WorldClockController::Context clockCtx;
    clockCtx.stationGrid = [this] { return myGrid(); };
    clockCtx.stationPosition = [this] { return myPosition(); };
    m_worldClock = new WorldClockController(std::move(clockCtx), this);

    RotorController::Context rotorCtx;
    rotorCtx.activity = [this](const QString& category, const QString& text, const QString& level) {
        addActivity(category, text, level);
    };
    rotorCtx.stationGrid = [this] { return myGrid(); };
    rotorCtx.stationCall = [this] {
        const QString call = m_profiles ? m_profiles->activeProfile().value(QStringLiteral("stationCallsign")).toString()
                                        : QString();
        return call.isEmpty() ? m_status.deCall : call;
    };
    m_rotor = new RotorController(std::move(rotorCtx), this);
    connect(this, &DecoLogController::stationChanged, m_rotor, &RotorController::stationChanged);
    // La banda della radio: col cambio di banda cambia l'antenna, e con lei
    // dove guarda (le antenne per banda del rotore).
    connect(this, &DecoLogController::tuningChanged, m_rotor, [this] {
        bool ok = false;
        const double mhz = shownFrequency().toDouble(&ok);
        if (ok && mhz > 0.0)
            m_rotor->setBand(bands::fromMhz(mhz));
    });
    // Uno spot scelto nel cluster: il rotore ne sa subito la rotta, anche se il
    // QTH della stazione non c'e' (allora conta dal QTH del gateway).
    connect(m_cluster, &ClusterController::spotAimed, this,
            [this](const QString& call, bool hasPosition, double lat, double lon, int azimuth) {
                const double az = azimuth >= 0 ? azimuth : hasPosition ? m_rotor->bearingTo(lat, lon) : -1.0;
                m_rotor->dxBearing(call, az);
            });
    // Il gateway integrato del rotore mette sulla mappa dell'app quello che
    // Decodium sente e che lavora, come faceva DecoRotor ascoltando la 2239.
    connect(&m_udp, &UdpReceiver::decodeReceived, this, [this](const QString&, const wsjtx::Decode& d) {
        if (auto* gw = m_rotor->gateway(); gw && gw->running())
            gw->noteDecode(d.message, d.snr, d.mode, m_status.dialFrequencyHz);
    });
    connect(&m_udp, &UdpReceiver::statusReceived, this, [this](const QString&, const wsjtx::Status& st) {
        if (auto* gw = m_rotor->gateway(); gw && gw->running())
            gw->noteStatus(st.dxCall, st.dxGrid, st.mode, st.dialFrequencyHz);
    });

    So2rController::Context so2rCtx;
    so2rCtx.activity = [this](const QString& category, const QString& text, const QString& level) {
        addActivity(category, text, level);
    };
    m_so2r = new So2rController(std::move(so2rCtx), this);

    VoiceKeyerController::Context dvkCtx;
    dvkCtx.activity = [this](const QString& category, const QString& text, const QString& level) {
        addActivity(category, text, level);
    };
    // Il PTT della radio che ha il fuoco.
    dvkCtx.ptt = [this](bool on) {
        if (m_so2r && m_so2r->radio2HasFocus() && m_so2r->radio2Connected())
            m_so2r->radio2()->setPtt(on);
        else if (auto* rig = qobject_cast<RigController*>(m_rig))
            rig->ptt(on);
    };
    m_dvk = new VoiceKeyerController(std::move(dvkCtx), this);

    RigController::Context rigCtx;
    rigCtx.alternateRig = [this]() -> core::RigLink* {
        return m_so2r && m_so2r->radio2HasFocus() ? m_so2r->radio2() : nullptr;
    };
    rigCtx.activity = [this](const QString& category, const QString& text, const QString& level) {
        addActivity(category, text, level);
    };
    rigCtx.stationCallsign = [this] {
        return m_profiles ? m_profiles->activeProfile().value(QStringLiteral("stationCallsign")).toString()
                          : QString();
    };
    // Le stazioni che lo skimmer CW sente: nuova entita', gia' lavorata… come per gli spot.
    rigCtx.classifyCall = [this](const QString& call, const QString& band, const QString& mode) -> QVariantMap {
        if (!m_cluster)
            return {};
        const auto c = m_cluster->classifyCall(call, band, mode);
        return QVariantMap{
            {QStringLiteral("status"), c.status},
            {QStringLiteral("label"), ClusterController::statusText(c.status)},
            {QStringLiteral("entity"), c.entity},
            {QStringLiteral("azimuth"), c.azimuth},
            {QStringLiteral("distanceKm"), c.distanceKm},
        };
    };
    m_rig = new RigController(std::move(rigCtx), this);
    // Il VFO che si muove e' una notizia quanto un QSO: il Cloud lo sappia.
    // Il CloudController manda al massimo una volta ogni venti secondi, quindi
    // girare la manopola non intasa niente.
    connect(m_rig, SIGNAL(stateChanged()), this, SLOT(reportPresenceToCloud()));
    // La barra in cima mostra la radio: quando la radio si muove, si rifa'.
    connect(m_rig, SIGNAL(stateChanged()), this, SIGNAL(tuningChanged()));
    connect(m_so2r, &So2rController::stateChanged, this, &DecoLogController::tuningChanged);
    connect(m_so2r, &So2rController::focusChanged, this, &DecoLogController::tuningChanged);
    connect(this, &DecoLogController::clientChanged, this, &DecoLogController::tuningChanged);

    CloudController::Context cloudCtx;
    cloudCtx.db = &m_db;
    cloudCtx.credentials = m_credentials;
    cloudCtx.activity = [this](const QString& category, const QString& text, const QString& level) {
        addActivity(category, text, level);
    };
    cloudCtx.logChanged = [this] {
        timed(tr("reloading the log table"), [this] { m_model->reload(); });
        emit logChanged();
    };
    m_cloud = new CloudController(std::move(cloudCtx), this);
    // I profili arrivati dal Cloud vanno riletti come quelli scritti qui.
    connect(m_cloud, &CloudController::profilesChanged, this, [this] { m_profiles->reload(); });
    // Le macro CW arrivate da un altro computer: rilette subito, se no la
    // prossima modifica fatta qui rimetterebbe quelle vecchie.
    connect(m_cloud, &CloudController::settingsApplied, this, [this] {
        if (auto* rig = qobject_cast<RigController*>(m_rig))
            rig->reloadMacros();
        emit newQsoSettingsChanged();
    });

    ActivationController::Context actCtx;
    actCtx.db = &m_db;
    actCtx.stationCall = [this] {
        return m_profiles->activeProfile().value(QStringLiteral("stationCallsign")).toString();
    };
    actCtx.stationGrid = [this] { return myGrid(); };
    actCtx.activeProfileId = [this] { return m_profiles->activeProfileId(); };
    actCtx.activity = [this](const QString& category, const QString& text, const QString& level) {
        addActivity(category, text, level);
    };
    actCtx.logChanged = [this] { emit logChanged(); };
    // Dove sta la propria stazione e dove stanno gli altri: nei contest il
    // valore di un QSO dipende da questo, e il cty.csv lo sa gia'.
    actCtx.dxccName = [this](int dxcc) { return m_countries.nameFor(dxcc); };
    actCtx.locate = [this](const QString& call) {
        core::ContestStation out;
        if (const auto e = m_countries.lookup(call)) {
            out.dxcc = e->dxcc;
            out.continent = e->continent;
            out.cqZone = e->cqZone;
            out.ituZone = e->ituZone;
        }
        return out;
    };
    // La propria stazione: il nominativo del profilo, risolto dal cty.csv.
    actCtx.station = [this] {
        core::ContestStation out;
        const QString call = m_profiles->activeProfile()
                                 .value(QStringLiteral("stationCallsign")).toString();
        if (const auto e = m_countries.lookup(call)) {
            out.dxcc = e->dxcc;
            out.continent = e->continent;
            out.cqZone = e->cqZone;
            out.ituZone = e->ituZone;
        }
        return out;
    };
    // I progressivi condivisi passano dalla rete, che nasce dopo.
    actCtx.takeSerial = [this](int localNext) { return m_net ? m_net->takeSerial(localNext) : localNext; };
    actCtx.shareSerials = [this](bool on) {
        if (m_net)
            m_net->setSerialSharing(on);
    };
    m_activation = new ActivationController(std::move(actCtx), this);
    // Un QSO corretto, cancellato o importato cambia il punteggio della gara:
    // quello in memoria non vale piu'. Collegato qui, prima delle finestre,
    // cosi' le finestre leggono gia' il conto nuovo.
    connect(this, &DecoLogController::logChanged, m_activation,
            [this] { m_activation->invalidateScore(); });
    m_activation->load();
    connect(this, &DecoLogController::logChanged, m_cluster, &ClusterController::logChanged);
    connect(this, &DecoLogController::logChanged, m_scp, &SuperCheckController::logChanged);

    // La rete multi-operatore: nasce qui perche' vuole il cluster e la gara.
    NetController::Context netCtx;
    netCtx.db = &m_db;
    netCtx.activity = [this](const QString& category, const QString& text, const QString& level) {
        addActivity(category, text, level);
    };
    netCtx.insertRemote = [this](const AdifRecord& record, const QString& station) {
        const InsertResult res = m_db.insertQso(record, QStringLiteral("network"),
                                                QStringLiteral("DecoDXLog net %1").arg(station), false,
                                                m_activation->active() && m_activation->session().stationProfileId > 0
                                                    ? m_activation->session().stationProfileId
                                                    : (m_profiles ? m_profiles->activeProfileId() : 0));
        if (res.status != InsertResult::Status::Inserted)
            return false;
        m_model->insertQso(res.id);
        m_activation->qsoLogged();
        addActivity(QStringLiteral("NET"), tr("%1 %2 %3 from %4").arg(record.value(QStringLiteral("CALL")),
                                                                      record.value(QStringLiteral("BAND")),
                                                                      record.value(QStringLiteral("MODE")), station),
                    QStringLiteral("success"));
        emit logChanged();
        return true;
    };
    netCtx.spot = [this](const QString& call, double khz, const QString& comment, const QString& station) {
        if (!m_cluster)
            return;
        QString spotter = station.toUpper();
        spotter.remove(QRegularExpression(QStringLiteral("[^A-Z0-9/-]")));
        m_cluster->injectLine(QStringLiteral("DX de %1: %2 %3 %4 %5Z")
                                  .arg(spotter.isEmpty() ? QStringLiteral("NET") : spotter)
                                  .arg(khz, 0, 'f', 1)
                                  .arg(call, comment.isEmpty() ? QStringLiteral("NET") : comment,
                                       QDateTime::currentDateTimeUtc().toString(QStringLiteral("HHmm"))));
    };
    netCtx.here = [this] {
        const double mhz = shownFrequency().toDouble();
        const QString call = m_profiles ? m_profiles->activeProfile().value(QStringLiteral("stationCallsign")).toString()
                                        : QString();
        return QVariantMap{{QStringLiteral("band"), mhz > 0 ? bandForFrequency(shownFrequency()) : QString()},
                           {QStringLiteral("mode"), shownMode()},
                           {QStringLiteral("freqKhz"), mhz * 1000.0},
                           {QStringLiteral("op"), call}};
    };
    netCtx.sessionStart = [this] {
        return m_activation->active() ? m_activation->session().startedAt : QDateTime();
    };
    m_net = new NetController(std::move(netCtx), this);
    m_net->setSerialSharing(m_activation->active() && m_activation->session().serialEnabled
                            && m_activation->session().sharedSerial);
    connect(this, &DecoLogController::countriesChanged, m_cluster, &ClusterController::logChanged);
    connect(this, &DecoLogController::clientChanged, m_cluster, &ClusterController::decodiumBandChanged);

    m_backupTimer.start();
    m_lotwTimer.start();
    QTimer::singleShot(30'000, this, &DecoLogController::checkLotwSchedule);
    // Dopo LoTW, per non partire tutti insieme all'avvio.
    QTimer::singleShot(60'000, this, &DecoLogController::checkConfirmSchedule);
    // I QSO rimasti nel log di Decodium: presto, perche' sono quelli fatti
    // mentre DecoDXLog era chiuso; poi ogni cinque minuti.
    m_recoveryTimer.start();
    QTimer::singleShot(20'000, this, &DecoLogController::checkDecodiumRecovery);
    // Il cty.xml di Club Log, una volta alla settimana, se c'e' la chiave.
    QTimer::singleShot(90'000, this, [this] {
        const QVariantMap info = clublogCty();
        const int age = info.value(QStringLiteral("age")).toInt();
        if (info.value(QStringLiteral("hasKey")).toBool() && (age < 0 || age >= 7))
            updateClubLogCty();
    });
    return ok;
}

void DecoLogController::startCluster()
{
    if (m_cluster)
        m_cluster->start();
    if (m_scp)
        m_scp->start();
    // La propagazione parte insieme al cluster: sono due cose che si guardano
    // mentre si opera, e nessuna delle due serve prima che il log sia aperto.
    if (m_solar)
        m_solar->start();
    if (m_updates)
        m_updates->start();
}

void DecoLogController::startCloud(bool automatic)
{
    if (m_cloud)
        m_cloud->start(automatic);
}

void DecoLogController::startRotor()
{
    if (m_rotor)
        m_rotor->start();
    if (m_rig)
        m_rig->start();
    if (m_so2r)
        m_so2r->start();
}

void DecoLogController::startDecoLink()
{
    m_decoLink.setIdentity(version(), m_profiles ? m_profiles->activeProfile().value(QStringLiteral("stationCallsign")).toString()
                                                 : QString());
    if (!m_decoLinkEnabled) {
        m_decoLink.stop();
        emit decoLinkChanged();
        return;
    }
    if (m_decoLink.start(static_cast<quint16>(m_decoLinkPort)))
        addActivity(QStringLiteral("LINK"), tr("DecoLink listening on 127.0.0.1:%1").arg(m_decoLinkPort));
    else
        addActivity(QStringLiteral("LINK"),
                    tr("DecoLink cannot listen on %1: %2 — another DecoDXLog is probably open. "
                       "Retrying every %3 seconds.")
                        .arg(m_decoLinkPort).arg(m_decoLink.lastError()).arg(DecoLinkServer::kRetrySeconds),
                    QStringLiteral("warning"));
    emit decoLinkChanged();
}

void DecoLogController::startHamHubLink()
{
    m_hamHubLink.setIdentity(version(), m_profiles ? m_profiles->activeProfile().value(QStringLiteral("stationCallsign")).toString()
                                                   : QString());
    m_hamHubLink.stop();
    m_hamHubLinkNote.clear();
    m_hamHubLinkHasPassword = m_credentials && m_credentials->hasSecret(QStringLiteral("hamhub"));
    if (!m_hamHubLinkEnabled || !m_hamHubLinkHasPassword) {
        // DecoPort non ha una modalita' senza firma: senza password non si parte.
        if (m_hamHubLinkEnabled)
            addActivity(QStringLiteral("HAMHUB"),
                        tr("HH link is waiting for a password: set it under HamHub in Setup, the same one as in HH"),
                        QStringLiteral("warning"));
        emit hamHubLinkChanged();
        return;
    }
    m_credentials->readSecret(QStringLiteral("hamhub"), [this](const QString& secret, const QString& error) {
        if (!m_hamHubLinkEnabled)
            return;
        if (secret.isEmpty()) {
            m_hamHubLinkNote = error.isEmpty() ? tr("the password is not in the system keystore") : error;
        } else if (m_hamHubLink.start(static_cast<quint16>(m_hamHubLinkPort), secret)) {
            addActivity(QStringLiteral("HAMHUB"), tr("HH link listening on 127.0.0.1:%1 (DecoPort)").arg(m_hamHubLinkPort));
        } else {
            addActivity(QStringLiteral("HAMHUB"),
                        tr("HH link cannot listen on %1: %2").arg(m_hamHubLinkPort).arg(m_hamHubLink.lastError()),
                        QStringLiteral("warning"));
        }
        emit hamHubLinkChanged();
    });
}

void DecoLogController::setHamHubLinkEnabled(bool enabled)
{
    if (enabled == m_hamHubLinkEnabled)
        return;
    m_hamHubLinkEnabled = enabled;
    QSettings().setValue(QStringLiteral("hamhublink/enabled"), enabled);
    startHamHubLink();
}

void DecoLogController::setHamHubLinkPort(int port)
{
    if (port == m_hamHubLinkPort || port <= 0 || port > 65535)
        return;
    m_hamHubLinkPort = port;
    QSettings().setValue(QStringLiteral("hamhublink/port"), port);
    startHamHubLink();
}

QString DecoLogController::hamHubLinkError() const
{
    return m_hamHubLink.lastError().isEmpty() ? m_hamHubLinkNote : m_hamHubLink.lastError();
}

void DecoLogController::hamHubQso(const AdifRecord& record, qint64 id)
{
    if (!m_hamHubLink.isListening() || m_hamHubLink.clientCount() == 0)
        return;
    if (m_hamHubLink.publishQso(id, adif::writeRecord(record)) > 0) {
        addActivity(QStringLiteral("HAMHUB"), tr("%1 sent to HH").arg(record.value(QStringLiteral("CALL")).toUpper()),
                    QStringLiteral("success"));
        emit hamHubLinkChanged();
    }
}

void DecoLogController::setDecoLinkEnabled(bool enabled)
{
    if (enabled == m_decoLinkEnabled)
        return;
    m_decoLinkEnabled = enabled;
    QSettings().setValue(QStringLiteral("decolink/enabled"), enabled);
    startDecoLink();
}

void DecoLogController::setDecoLinkPort(int port)
{
    if (port == m_decoLinkPort || port <= 0 || port > 65535)
        return;
    m_decoLinkPort = port;
    QSettings().setValue(QStringLiteral("decolink/port"), port);
    startDecoLink();
}

QVariantList DecoLogController::decoLinkClients() const
{
    QVariantList out;
    for (const auto& c : m_decoLink.clients()) {
        out << QVariantMap{{QStringLiteral("app"), c.app.isEmpty() ? tr("(not introduced yet)") : c.app},
                           {QStringLiteral("version"), c.version},
                           {QStringLiteral("station"), c.station}};
    }
    return out;
}

QJsonObject DecoLogController::decoLinkAward() const
{
    Ft2Award ft2;
    const auto cached = m_statsCache.constFind(QStringLiteral("ft2"));
    if (cached != m_statsCache.constEnd()) {
        const QVariantMap m = cached->toMap();
        ft2.qsos = m.value(QStringLiteral("qsos")).toInt();
        ft2.gridsWorked = m.value(QStringLiteral("gridsWorked")).toInt();
        ft2.gridsConfirmed = m.value(QStringLiteral("gridsConfirmed")).toInt();
    } else {
        ft2 = m_db.ft2Award();
    }
    int ft2Worked = 0, ft2Confirmed = 0, dxccWorked = 0, dxccConfirmed = 0;
    for (const AwardResult& r : globalAwardResults()) {
        if (r.id == QLatin1String("ft2")) {
            ft2Worked = r.worked();
            ft2Confirmed = r.confirmed();
        } else if (r.id == QLatin1String("dxcc")) {
            dxccWorked = r.worked();
            dxccConfirmed = r.confirmed();
        }
    }
    return QJsonObject{
        {QStringLiteral("ft2"), QJsonObject{{QStringLiteral("qsos"), ft2.qsos},
                                            {QStringLiteral("dxccWorked"), ft2Worked},
                                            {QStringLiteral("dxccConfirmed"), ft2Confirmed},
                                            {QStringLiteral("gridsWorked"), ft2.gridsWorked},
                                            {QStringLiteral("gridsConfirmed"), ft2.gridsConfirmed}}},
        {QStringLiteral("dxcc"), QJsonObject{{QStringLiteral("worked"), dxccWorked},
                                             {QStringLiteral("confirmed"), dxccConfirmed}}},
    };
}

QJsonArray DecoLogController::decoLinkQuery(const QJsonObject& query) const
{
    const QString band = query.value(QStringLiteral("band")).toString().trimmed().toLower();
    // Le entita' confermate, secondo le conferme scelte negli award.
    QSet<int> confirmedDxcc;
    for (const AwardResult& r : globalAwardResults()) {
        if (r.id != QLatin1String("dxcc"))
            continue;
        for (const AwardItem& i : r.items) {
            if (i.confirmed())
                confirmedDxcc.insert(i.key.toInt());
        }
    }
    QJsonArray out;
    for (const QJsonValue& v : query.value(QStringLiteral("calls")).toArray()) {
        const QString call = v.toString().trimmed().toUpper();
        if (call.isEmpty())
            continue;
        const WorkedBefore wb = m_db.workedBefore(call);
        QJsonObject result{
            {QStringLiteral("call"), call},
            {QStringLiteral("workedCall"), wb.count > 0},
            {QStringLiteral("workedCallBand"), !band.isEmpty() && wb.bands.contains(band)},
        };
        if (const auto e = m_countries.lookup(call)) {
            const auto worked = m_db.dxccWorked(e->dxcc);
            result.insert(QStringLiteral("dxcc"), e->dxcc);
            result.insert(QStringLiteral("entity"), e->name);
            result.insert(QStringLiteral("workedDxcc"), worked.count > 0);
            result.insert(QStringLiteral("workedDxccBand"), !band.isEmpty() && worked.bands.contains(band));
            result.insert(QStringLiteral("workedDxccFt2"), worked.modes.contains(QStringLiteral("FT2")));
            result.insert(QStringLiteral("confirmedDxcc"), confirmedDxcc.contains(e->dxcc));
        }
        out.append(result);
    }
    return out;
}

void DecoLogController::decoLinkQso(const AdifRecord& record, const QString& status, qint64 id,
                                    const QString& source, const QString& app, const QString& message)
{
    if (m_decoLink.clientCount() == 0)
        return;
    const QString iso = LogDatabase::isoFromAdif(record.value(QStringLiteral("QSO_DATE")),
                                                 record.value(QStringLiteral("TIME_ON")));
    QString band = record.value(QStringLiteral("BAND")).toLower();
    if (band.isEmpty())
        band = bandForFrequency(record.value(QStringLiteral("FREQ")));
    AdifRecord normalized = record;
    adif::normalizeMode(normalized);
    const auto meta = id > 0 ? m_db.meta(id) : std::nullopt;
    QJsonObject msg{
        {QStringLiteral("type"), QStringLiteral("qso")},
        {QStringLiteral("row"), LogDatabase::workedRow(record.value(QStringLiteral("CALL")).toUpper(), band,
                                                       normalized.value(QStringLiteral("MODE")),
                                                       normalized.value(QStringLiteral("SUBMODE")), iso,
                                                       record.value(QStringLiteral("GRIDSQUARE")), false)},
        {QStringLiteral("status"), status},
        {QStringLiteral("source"), source},
        {QStringLiteral("app"), app},
    };
    if (meta)
        msg.insert(QStringLiteral("uuid"), meta->uuid);
    if (!message.isEmpty())
        msg.insert(QStringLiteral("message"), message);
    m_decoLink.broadcast(msg);
}

void DecoLogController::timed(const QString& what, const std::function<void()>& work)
{
    QElapsedTimer clock;
    clock.start();
    work();
    const qint64 spent = clock.elapsed();
    if (spent >= 400) {
        addActivity(QStringLiteral("APP"), tr("%1: %2 s").arg(what, QString::number(spent / 1000.0, 'f', 1)),
                    QStringLiteral("warning"));
    }
}

QVariantMap DecoLogController::about() const
{
    return {
        {QStringLiteral("name"), QStringLiteral("DecoDXLog")},
        {QStringLiteral("version"), version()},
        {QStringLiteral("author"), QStringLiteral("Martino Merola — IU8LMC")},
        {QStringLiteral("email"), QStringLiteral("iu8lmc@gmail.com")},
        {QStringLiteral("license"), QStringLiteral("GPL-3.0-or-later")},
        {QStringLiteral("home"), QStringLiteral("https://github.com/elisir80/DecodxLog")},
        {QStringLiteral("repository"), QStringLiteral(DECODXLOG_DISTRIBUTION_REPOSITORY)},
        {QStringLiteral("commit"), QStringLiteral(DECODXLOG_GIT_COMMIT)},
        {QStringLiteral("channel"), QStringLiteral("fork")},
        {QStringLiteral("family"), QStringLiteral("Decodium")},
        {QStringLiteral("qt"), QStringLiteral(QT_VERSION_STR)},
        {QStringLiteral("built"), QStringLiteral(__DATE__)},
        {QStringLiteral("qsoCount"), m_db.qsoCount()},
    };
}

void DecoLogController::startFreezeWatch()
{
    // Un quarto di secondo fra un battito e l'altro; si dice qualcosa solo
    // oltre il secondo e mezzo, che e' il punto in cui un blocco si sente.
    constexpr int kBeatMs = 250;
    constexpr qint64 kSayItMs = 1500;
    m_freezeClock.start();
    m_lastBeat = m_freezeClock.elapsed();
    m_freezeBeat.setInterval(kBeatMs);
    connect(&m_freezeBeat, &QTimer::timeout, this, [this] {
        const qint64 now = m_freezeClock.elapsed();
        const qint64 late = now - m_lastBeat - m_freezeBeat.interval();
        m_lastBeat = now;
        if (late < kSayItMs)
            return;
        ++m_freezeCount;
        // Cosa stava facendo: l'ultima operazione lunga conclusa dentro il blocco.
        const decolog::SlowOp& slow = decolog::lastSlowOp();
        const char* busyWith = slow.name.load();
        const long long endedAgo = decolog::steadyNowMs() - slow.endMs.load();
        const bool known = busyWith && endedAgo >= 0 && endedAgo <= late + kBeatMs
                           && slow.durationMs.load() >= late / 3;
        const QString seconds = QString::number(late / 1000.0, 'f', 1);
        addActivity(QStringLiteral("APP"),
                    known ? tr("The window stopped answering for %1 s, busy with: %2 (%n time(s) since the start)",
                               nullptr, m_freezeCount)
                                .arg(seconds, QString::fromLatin1(busyWith))
                          : tr("The window stopped answering for %1 s (%n time(s) since the start)",
                               nullptr, m_freezeCount)
                                .arg(seconds),
                    QStringLiteral("warning"));
    });
    m_freezeBeat.start();
}

void DecoLogController::startN1mm()
{
    if (m_n1mmPort <= 0) {
        m_n1mm.stop();
        emit udpChanged();
        return;
    }
    if (m_n1mm.start(static_cast<quint16>(m_n1mmPort)))
        addActivity(QStringLiteral("N1MM"), tr("Listening for N1MM Logger+ on UDP %1").arg(m_n1mmPort));
    else
        addActivity(QStringLiteral("N1MM"), tr("Cannot listen on UDP %1: %2").arg(m_n1mmPort).arg(m_n1mm.lastError()),
                    QStringLiteral("error"));
    emit udpChanged();
}

void DecoLogController::setN1mmPort(int port)
{
    if (port == m_n1mmPort || port < 0 || port > 65535)
        return;
    m_n1mmPort = port;
    QSettings().setValue(QStringLiteral("n1mm/port"), port);
    startN1mm();
}

// N1MM ha corretto un contatto: si ritrova dal suo ID e si riscrive, tenendo
// quello che N1MM non sa (conferme, note, callbook).
void DecoLogController::onN1mmReplaced(const AdifRecord& record, const QString& id)
{
    const QDateTime near = QDateTime::fromString(LogDatabase::isoFromAdif(record.value(QStringLiteral("QSO_DATE")),
                                                             record.value(QStringLiteral("TIME_ON"))), Qt::ISODate);
    const auto found = m_db.findByExtra(QStringLiteral("APP_N1MM_ID"), id, near);
    if (!found) {
        onQsoReceived(record, QStringLiteral("n1mm"), QStringLiteral("N1MM Logger+"));
        return;
    }
    auto merged = m_db.record(*found);
    if (!merged)
        return;
    for (const auto& field : record.fields()) {
        if (!field.value.trimmed().isEmpty())
            merged->set(field.name, field.value);
    }
    applyEntity(*merged);
    const InsertResult r = m_db.updateQso(*found, *merged, -1, QStringLiteral("n1mm"));
    if (r.status != InsertResult::Status::Inserted) {
        addActivity(QStringLiteral("N1MM"), tr("Correction not saved: %1").arg(r.message), QStringLiteral("error"));
        return;
    }
    m_qsl->qsoLogged(*found);
    m_model->refreshQso(*found);
    addActivity(QStringLiteral("N1MM"), tr("Corrected by N1MM: %1").arg(record.value(QStringLiteral("CALL"))));
    emit logChanged();
}

void DecoLogController::onN1mmDeleted(const QString& id, const AdifRecord& record)
{
    const QDateTime near = QDateTime::fromString(LogDatabase::isoFromAdif(record.value(QStringLiteral("QSO_DATE")),
                                                             record.value(QStringLiteral("TIME_ON"))), Qt::ISODate);
    const auto found = m_db.findByExtra(QStringLiteral("APP_N1MM_ID"), id, near);
    if (!found)
        return;
    deleteQsos({QVariant::fromValue(*found)});
}

void DecoLogController::startListening()
{
    QHostAddress group;
    if (!m_multicast.trimmed().isEmpty())
        group = QHostAddress(m_multicast.trimmed());
    if (m_udp.start(static_cast<quint16>(m_udpPort), group)) {
        addActivity(QStringLiteral("UDP"),
                    group.isNull() ? tr("Listening on UDP %1").arg(m_udpPort)
                                   : tr("Listening on UDP %1, multicast %2").arg(m_udpPort).arg(group.toString()));
    } else if (m_udpPort > 0) {
        addActivity(QStringLiteral("UDP"), tr("Cannot listen on UDP %1: %2").arg(m_udpPort).arg(m_udp.lastError()),
                    QStringLiteral("error"));
    }
    emit udpChanged();
}

QString DecoLogController::version() const
{
    return QCoreApplication::applicationVersion();
}

QString DecoLogController::qtVersion() const
{
    return QString::fromLatin1(qVersion());
}

QString DecoLogController::buildInfo() const
{
    return tr("built on %1 · %2 @ %3")
        .arg(QLocale::c().toDate(QString::fromLatin1(__DATE__).simplified(),
                                 QStringLiteral("MMM d yyyy")).toString(Qt::ISODate),
             QStringLiteral(DECODXLOG_DISTRIBUTION_REPOSITORY),
             QStringLiteral(DECODXLOG_GIT_COMMIT));
}

// ── Impostazioni del collegamento ─────────────────────────────────────────────

void DecoLogController::setUdpPort(int port)
{
    if (port == m_udpPort || port < 0 || port > 65535)
        return;
    m_udpPort = port;
    QSettings().setValue(QStringLiteral("udp/port"), port);
    startListening();
}

void DecoLogController::setUdpForward(const QString& targets)
{
    const QString clean = targets.simplified();
    QStringList rejected;
    const auto parsed = UdpReceiver::parseTargets(clean, &rejected);
    m_udpForwardError = rejected.isEmpty() ? QString()
                                           : tr("Not understood: %1 (write address:port)").arg(rejected.join(QStringLiteral(", ")));
    if (clean != m_udpForward) {
        m_udpForward = clean;
        QSettings().setValue(QStringLiteral("udp/forward"), clean);
        m_udp.setForwardTargets(parsed);
        QStringList names;
        for (const auto& t : parsed)
            names << QStringLiteral("%1:%2").arg(t.address.toString()).arg(t.port);
        addActivity(QStringLiteral("UDP"), names.isEmpty() ? tr("UDP forwarding off")
                                                           : tr("UDP forwarded to %1").arg(names.join(QStringLiteral(", "))));
    }
    emit udpChanged();
}

void DecoLogController::setMulticastGroup(const QString& group)
{
    if (group == m_multicast)
        return;
    m_multicast = group;
    QSettings().setValue(QStringLiteral("udp/multicastGroup"), group);
    startListening();
}

void DecoLogController::setPreferLoggedAdif(bool prefer)
{
    if (prefer == m_udp.prefersLoggedAdif())
        return;
    m_udp.setPreferLoggedAdif(prefer);
    QSettings().setValue(QStringLiteral("udp/preferLoggedAdif"), prefer);
    emit udpChanged();
}

void DecoLogController::setDedupDigitalMinutes(int minutes)
{
    if (minutes == dedupDigitalMinutes())
        return;
    m_db.setDedupWindows(minutes * 60, m_db.dedupWindowSeconds(true));
    QSettings().setValue(QStringLiteral("log/dedupDigitalMinutes"), minutes);
    emit udpChanged();
}

void DecoLogController::setDedupManualMinutes(int minutes)
{
    if (minutes == dedupManualMinutes())
        return;
    m_db.setDedupWindows(m_db.dedupWindowSeconds(false), minutes * 60);
    QSettings().setValue(QStringLiteral("log/dedupManualMinutes"), minutes);
    emit udpChanged();
}

void DecoLogController::setFollowDxCall(bool follow)
{
    if (follow == m_followDx)
        return;
    m_followDx = follow;
    QSettings().setValue(QStringLiteral("udp/followDxCall"), follow);
    emit udpChanged();
}

bool DecoLogController::clientConnected() const
{
    return m_clientLastSeen.isValid();
}

void DecoLogController::emailFor(const QString& call,
                                 std::function<void(const QString&, const QString&)> done)
{
    if (!done)
        return;
    const QString c = call.trimmed().toUpper();
    if (c.isEmpty()) {
        done(QString(), tr("no callsign"));
        return;
    }
    // Gia' chiesta prima: si risponde senza disturbare di nuovo il callbook.
    if (const auto it = m_callbookResults.constFind(c); it != m_callbookResults.constEnd()) {
        const QString email = it->value(QStringLiteral("email")).toString().trimmed();
        done(email, email.isEmpty()
                        ? tr("%1 is in the callbook but has no email there").arg(c)
                        : QString());
        return;
    }
    if (m_callbook.provider() == CallbookClient::Provider::None) {
        done(QString(), tr("no callbook is set up: Setup -> Callbook"));
        return;
    }
    // Una ricerca sola per nominativo, anche se ad aspettarla sono in tanti.
    const bool alreadyAsked = m_awaitingEmail.contains(c);
    m_awaitingEmail[c].append(std::move(done));
    if (!alreadyAsked)
        m_callbook.lookup(c);
}

QString DecoLogController::shownFrequency() const
{
    // SO2R: si vede la radio che ha il fuoco.
    if (m_so2r && m_so2r->radio2HasFocus())
        return m_so2r->radio2Hz() > 0 ? QString::number(static_cast<double>(m_so2r->radio2Hz()) / 1e6, 'f', 6) : QString();
    auto* rig = qobject_cast<RigController*>(m_rig);
    if (rig && rig->connected() && rig->frequencyHz() > 0)
        return QString::number(static_cast<double>(rig->frequencyHz()) / 1e6, 'f', 6);
    return dialFrequency();
}

bool DecoLogController::phoneMode() const
{
    const QString m = shownMode().toUpper();
    return m == QLatin1String("USB") || m == QLatin1String("LSB") || m == QLatin1String("SSB")
        || m == QLatin1String("AM") || m == QLatin1String("FM") || m == QLatin1String("PKTFM");
}

void DecoLogController::functionKey(int index, const QVariantMap& context)
{
    const QString asked = context.value(QStringLiteral("mode")).toString().toUpper();
    const bool phone = asked.isEmpty() ? phoneMode()
                                       : (asked == QLatin1String("SSB") || asked == QLatin1String("USB")
                                          || asked == QLatin1String("LSB") || asked == QLatin1String("AM")
                                          || asked == QLatin1String("FM"));
    if (phone && m_dvk) {
        m_dvk->play(index);
        return;
    }
    if (auto* rig = qobject_cast<RigController*>(m_rig))
        rig->sendMacro(index, context);
}

void DecoLogController::stopSending()
{
    if (m_dvk)
        m_dvk->stop();
    if (auto* rig = qobject_cast<RigController*>(m_rig))
        rig->stop();
}

QString DecoLogController::shownMode() const
{
    if (m_so2r && m_so2r->radio2HasFocus())
        return m_so2r->radio2Mode();
    auto* rig = qobject_cast<RigController*>(m_rig);
    const QString fromDecodium = currentMode();
    if (!rig || !rig->connected() || rig->mode().isEmpty())
        return fromDecodium;
    // Se Decodium e la radio stanno sulla stessa cosa, si mostra il nome di
    // Decodium: "FT8" dice piu' di "PKTUSB". Se no comanda la radio, che e'
    // quella che trasmette davvero.
    if (!fromDecodium.isEmpty() && modes::catFor(fromDecodium) == rig->mode())
        return fromDecodium;
    return rig->mode();
}

QString DecoLogController::dialFrequency() const
{
    if (m_status.dialFrequencyHz == 0)
        return {};
    return QString::number(static_cast<double>(m_status.dialFrequencyHz) / 1e6, 'f', 6);
}

QString DecoLogController::dialBand() const
{
    if (m_status.dialFrequencyHz == 0)
        return {};
    return bands::fromMhz(static_cast<double>(m_status.dialFrequencyHz) / 1e6);
}

// ── Il QSO che si completa da solo ────────────────────────────────────────────
//
// Da Decodium arriva l'essenziale: nominativo, rapporto, banda, modo. Il nome di
// chi c'era dall'altra parte, il suo locatore, la citta' e l'indirizzo li sa il
// callbook — ed e' un peccato che restino li' mentre nel log c'e' una riga nuda.
// Appena il QSO e' scritto si chiede, e quello che torna riempie **solo i campi
// vuoti**: quello che ha scritto l'operatore non si tocca mai.

void DecoLogController::completeFromCallbook(qint64 id, const QString& call)
{
    if (id <= 0 || call.isEmpty() || !m_callbookComplete)
        return;
    if (m_callbook.provider() == CallbookClient::Provider::None)
        return;

    // Se l'abbiamo gia' cercato in questa sessione, la risposta e' qui.
    if (const auto it = m_callbookResults.constFind(call); it != m_callbookResults.constEnd()) {
        applyCallbookToQso(id, *it);
        return;
    }
    // Altrimenti si mette in coda: la ricerca e' una sola anche per piu' QSO.
    m_awaitingCallbook[call].append(id);
    m_callbook.lookup(call);
}

QStringList DecoLogController::applyCallbookToQso(qint64 id, const QVariantMap& cb)
{
    const auto current = m_db.record(id);
    if (!current)
        return {};

    AdifRecord updated = *current;
    CallbookRecord found;
    found.name = cb.value(QStringLiteral("name")).toString();
    found.qth = cb.value(QStringLiteral("qth")).toString();
    found.grid = cb.value(QStringLiteral("grid")).toString();
    found.address = cb.value(QStringLiteral("address")).toString();
    found.state = cb.value(QStringLiteral("state")).toString();
    found.county = cb.value(QStringLiteral("county")).toString();
    found.country = cb.value(QStringLiteral("country")).toString();
    found.iota = cb.value(QStringLiteral("iota")).toString();
    found.email = cb.value(QStringLiteral("email")).toString();
    found.qslVia = cb.value(QStringLiteral("qslVia")).toString();
    found.cqZone = cb.value(QStringLiteral("cqZone")).toInt();
    found.ituZone = cb.value(QStringLiteral("ituZone")).toInt();
    found.dxcc = cb.value(QStringLiteral("dxcc")).toInt();
    // La posizione serve per ricavare il locatore quando il callbook non lo scrive.
    found.lat = cb.value(QStringLiteral("lat")).toDouble();
    found.lon = cb.value(QStringLiteral("lon")).toDouble();
    found.hasPosition = cb.value(QStringLiteral("hasPosition")).toBool();

    const QStringList filled = callbook::fillMissing(updated, found);
    if (filled.isEmpty())
        return {};

    const InsertResult r = m_db.updateQso(id, updated, -1, QStringLiteral("callbook"));
    if (r.status != InsertResult::Status::Inserted)
        return {};

    QList<EnrichmentEntry> provenance;
    const QString acquiredAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    for (const QString& field : filled) {
        provenance << EnrichmentEntry{id, field, current->value(field), updated.value(field),
                                      QStringLiteral("callbook"), QStringLiteral("inferred"), acquiredAt,
                                      current->value(QStringLiteral("QSO_DATE")), QString()};
    }
    if (!m_db.recordEnrichment(provenance))
        addActivity(QStringLiteral("CALLBOOK"), tr("Could not record provenance for QSO %1").arg(id),
                    QStringLiteral("warning"));

    m_model->refreshQso(id);
    m_cloud->qsoLogged();
    emit logChanged();
    if (m_lookupCall == updated.value(QStringLiteral("CALL")).toUpper())
        refreshCallInfo();
    return filled;
}

void DecoLogController::reportPresenceToCloud()
{
    if (!m_cloud)
        return;
    // Quello che si vede guardando la radio: dove si ascolta, in che modo, chi
    // si sta lavorando, e se in questo momento si trasmette.
    //
    // La frequenza puo' arrivare da due parti: da Decodium (o WSJT-X) via UDP
    // mentre lavora, oppure dal CAT. Prima si guardava solo l'UDP: chi opera in
    // SSB o in CW, senza un programma che manda lo stato, dal browser risultava
    // senza frequenza — la radio era li' accesa e il Cloud non lo sapeva.
    qint64 frequency = static_cast<qint64>(m_status.dialFrequencyHz);
    QString band = dialBand();
    QString mode = currentMode();
    QString client = m_clientName;
    if (frequency <= 0) {
        if (auto* rig = qobject_cast<RigController*>(m_rig); rig && rig->connected()) {
            frequency = rig->frequencyHz();
            band = frequency > 0 ? bands::fromMhz(static_cast<double>(frequency) / 1e6) : QString();
            if (!rig->mode().isEmpty())
                mode = rig->mode();
            if (client.isEmpty())
                client = tr("radio (CAT)");
        }
    }
    m_cloud->clientStateChanged(QVariantMap{
        {QStringLiteral("frequencyHz"), frequency},
        {QStringLiteral("band"), band},
        {QStringLiteral("mode"), mode},
        {QStringLiteral("dxCall"), m_status.dxCall},
        {QStringLiteral("transmitting"), m_status.transmitting},
        {QStringLiteral("client"), client},
    });
}

QString DecoLogController::subdivisionName(const QString& code, int dxcc) const
{
    // Uno stato USA, una prefettura giapponese: nel log c'e' la sigla o il
    // numero, ma chi guarda vuole leggere il nome.
    const QString text = code.trimmed().toUpper();
    if (text.isEmpty())
        return {};
    static const QSet<int> usa{291, 6, 110};
    if (usa.contains(dxcc) && awards::usStates().contains(text))
        return awards::usStates().value(text);
    if (dxcc == 339) {
        const QString prefecture = awards::japanPrefecture(text);
        if (!prefecture.isEmpty())
            return awards::japanPrefectures().value(prefecture);
    }
    return text;
}

QStringList DecoLogController::bands() const
{
    return bands::all();
}

QString DecoLogController::uiLanguage() const
{
    return QSettings().value(QStringLiteral("ui/language"), QStringLiteral("auto")).toString();
}

void DecoLogController::setUiLanguage(const QString& language)
{
    if (language == uiLanguage())
        return;
    QSettings().setValue(QStringLiteral("ui/language"), language);
    addActivity(QStringLiteral("LOG"), tr("Interface language: %1 — it changes at the next start").arg(language));
    emit uiLanguageChanged();
}

double DecoLogController::bandFrequency(const QString& band, const QString& mode) const
{
    return core::bands::defaultFrequency(band, mode);
}

QString DecoLogController::bandForFrequency(const QString& mhz) const
{
    bool ok = false;
    const double f = QString(mhz).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
    return ok ? bands::fromMhz(f) : QString();
}

// ── Entita' DXCC ──────────────────────────────────────────────────────────────

namespace {

QString countriesOverridePath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("cty.csv"));
}

} // namespace

// Il cty.csv delle risorse, o quello nella cartella dei dati se e' piu' recente:
// AD1C lo aggiorna a ogni DXpedition, una versione di DecoDXLog no.
void DecoLogController::loadCountries()
{
    Countries fromResources;
    QFile bundled(QStringLiteral(":/decolog/cty.csv"));
    if (bundled.open(QIODevice::ReadOnly))
        fromResources.load(bundled.readAll());

    Countries fromFile;
    QFile local(countriesOverridePath());
    if (local.open(QIODevice::ReadOnly) && fromFile.load(local.readAll())
        && fromFile.version() >= fromResources.version()) {
        m_countries = fromFile;
        m_countriesSource = QDir::toNativeSeparators(local.fileName());
    } else {
        m_countries = fromResources;
        m_countriesSource = tr("built-in");
    }
    emit countriesChanged();
}

QString DecoLogController::installCountries(const QUrl& url)
{
    QFile file(url.isLocalFile() ? url.toLocalFile() : url.toString());
    if (!file.open(QIODevice::ReadOnly))
        return file.errorString();
    const QByteArray data = file.readAll();
    Countries candidate;
    if (!candidate.load(data))
        return tr("Not a cty.csv file");
    if (candidate.version() < m_countries.version())
        return tr("%1 is older than the one in use (%2)").arg(candidate.version(), m_countries.version());
    QDir().mkpath(QFileInfo(countriesOverridePath()).absolutePath());
    QFile out(countriesOverridePath());
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return out.errorString();
    out.write(data);
    out.close();
    loadCountries();
    addActivity(QStringLiteral("LOG"), tr("cty.csv %1 installed: %2 DXCC entities")
                                           .arg(m_countries.version()).arg(m_countries.entityCount()),
                QStringLiteral("success"));
    refreshCallInfo();
    return {};
}

bool DecoLogController::applyEntity(AdifRecord& record) const
{
    // Con il cty.xml di Club Log l'entita' e' quella del giorno del QSO.
    if (!m_ctyXml.isEmpty()) {
        const QDateTime when = QDateTime::fromString(
            LogDatabase::isoFromAdif(record.value(QStringLiteral("QSO_DATE")), record.value(QStringLiteral("TIME_ON"))),
            Qt::ISODate);
        const CtyMatch m = m_ctyXml.lookup(record.value(QStringLiteral("CALL")), when);
        if (m.found) {
            const QString existing = record.value(QStringLiteral("DXCC"));
            if (!existing.isEmpty() && existing.toInt() != m.adif)
                return false;
            const auto e = m_countries.lookup(record.value(QStringLiteral("CALL")));
            bool changed = false;
            auto fill = [&record, &changed](const char* field, const QString& value) {
                if (record.value(QLatin1String(field)).isEmpty() && !value.isEmpty()) {
                    record.set(QLatin1String(field), value);
                    changed = true;
                }
            };
            fill("DXCC", QString::number(m.adif));
            // Il nome come lo scrive cty.csv, se e' la stessa entita': Club Log
            // scrive in maiuscolo.
            fill("COUNTRY", e && e->dxcc == m.adif ? e->name : m.name);
            fill("CQZ", m.cqz > 0 ? QString::number(m.cqz) : QString());
            fill("ITUZ", e && e->dxcc == m.adif && e->ituZone > 0 ? QString::number(e->ituZone) : QString());
            fill("CONT", m.cont);
            return changed;
        }
    }
    const auto e = m_countries.lookup(record.value(QStringLiteral("CALL")));
    if (!e)
        return false;
    // Il DXCC decide il resto: se il QSO ne ha gia' uno diverso (una correzione
    // dell'operatore, un'isola che cty.csv non distingue) non si tocca niente.
    const QString existing = record.value(QStringLiteral("DXCC"));
    if (!existing.isEmpty() && existing.toInt() != e->dxcc)
        return false;

    bool changed = false;
    auto fill = [&record, &changed](const char* field, const QString& value) {
        if (record.value(QLatin1String(field)).isEmpty() && !value.isEmpty()) {
            record.set(QLatin1String(field), value);
            changed = true;
        }
    };
    fill("DXCC", QString::number(e->dxcc));
    fill("COUNTRY", e->name);
    fill("CQZ", e->cqZone > 0 ? QString::number(e->cqZone) : QString());
    fill("ITUZ", e->ituZone > 0 ? QString::number(e->ituZone) : QString());
    fill("CONT", e->continent);
    return changed;
}

// ── Il cty.xml di Club Log ────────────────────────────────────────────────────

QString DecoLogController::clublogCtyPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/clublog-cty.xml");
}

AwardCalculator DecoLogController::awardCalculator(const QHash<int, QString>& names) const
{
    // I nomi delle entita' cancellate li sa solo il cty.xml.
    QHash<int, QString> all = names;
    const ClubLogCty cty = m_ctyXml;
    AwardCalculator calc([all, cty](int dxcc) {
        const QString name = all.value(dxcc);
        if (!name.isEmpty())
            return name;
        const CtyEntity* e = cty.entity(dxcc);
        return e ? e->name : QString();
    });
    calc.setRulePackages(localAwardRulePackages());
    if (!cty.isEmpty()) {
        AwardCalculator::DxccRules rules;
        rules.invalid = [cty](const QString& call, const QDateTime& when) { return cty.lookup(call, when).invalid; };
        rules.deleted = [cty](int dxcc) {
            const CtyEntity* e = cty.entity(dxcc);
            return e && e->deleted;
        };
        calc.setDxccRules(rules);
    }
    return calc;
}

void DecoLogController::loadClubLogCty()
{
    const QString path = clublogCtyPath();
    if (!QFileInfo::exists(path)) {
        emit ctyChanged();
        return;
    }
    QPointer<DecoLogController> self(this);
    m_ctyPool.start([self, path] {
        ClubLogCty cty;
        QString error;
        const bool ok = cty.loadFile(path, &error);
        QMetaObject::invokeMethod(
            self.data(),
            [self, cty, ok, error] {
                if (!self)
                    return;
                if (ok) {
                    self->m_ctyXml = cty;
                    self->m_awardsDirty = self->m_globalAwardsDirty = true;
                    emit self->awardsChanged();
                } else {
                    self->m_ctyXmlStatus = tr("Club Log cty.xml not readable: %1").arg(error);
                }
                emit self->ctyChanged();
            },
            Qt::QueuedConnection);
    });
}

QVariantMap DecoLogController::clublogCty() const
{
    const QFileInfo file(clublogCtyPath());
    return QVariantMap{
        {QStringLiteral("loaded"), !m_ctyXml.isEmpty()},
        {QStringLiteral("date"), m_ctyXml.date().isValid() ? dates::show(m_ctyXml.date().date().toString(Qt::ISODate))
                                                          : QString()},
        {QStringLiteral("entities"), m_ctyXml.entityCount()},
        {QStringLiteral("status"), m_ctyXmlStatus},
        {QStringLiteral("busy"), m_ctyXmlBusy},
        {QStringLiteral("hasKey"), m_qsl && !m_qsl->clubLogApiKey().isEmpty()},
        {QStringLiteral("age"), file.exists() ? int(file.lastModified().daysTo(QDateTime::currentDateTime())) : -1},
    };
}

void DecoLogController::updateClubLogCty()
{
    if (m_ctyXmlBusy)
        return;
    const QString key = m_qsl ? m_qsl->clubLogApiKey() : QString();
    if (key.isEmpty()) {
        m_ctyXmlStatus = tr("The Club Log API key is needed (Setup → QSL services → Club Log)");
        emit ctyChanged();
        return;
    }
    m_ctyXmlBusy = true;
    m_ctyXmlStatus = tr("Downloading cty.xml from Club Log…");
    emit ctyChanged();
    auto* net = new QNetworkAccessManager(this);
    QNetworkRequest request(QUrl(QStringLiteral("https://cdn.clublog.org/cty.php?api=") + key));
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("DecoDXLog/%1").arg(QCoreApplication::applicationVersion()));
    request.setTransferTimeout(120'000);
    QNetworkReply* reply = net->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, net, key] {
        reply->deleteLater();
        net->deleteLater();
        m_ctyXmlBusy = false;
        QByteArray data = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            m_ctyXmlStatus = tr("Club Log: %1").arg(qsl::withoutKey(network::safeErrorString(reply), key));
            emit ctyChanged();
            return;
        }
        QString error;
        if (data.size() > 2 && uchar(data.at(0)) == 0x1f && uchar(data.at(1)) == 0x8b)
            data = ClubLogCty::gunzip(data, &error);
        ClubLogCty cty;
        if (data.isEmpty() || !cty.load(data, &error)) {
            m_ctyXmlStatus = tr("Club Log: the file is not a cty.xml (%1)").arg(error.left(120));
            emit ctyChanged();
            return;
        }
        QDir().mkpath(QFileInfo(clublogCtyPath()).absolutePath());
        QFile out(clublogCtyPath());
        if (out.open(QIODevice::WriteOnly)) {
            out.write(data);
            out.close();
        }
        m_ctyXml = cty;
        m_ctyXmlStatus = tr("cty.xml of %1: %2 entities").arg(dates::show(cty.date().date().toString(Qt::ISODate))).arg(cty.entityCount());
        addActivity(QStringLiteral("DXCC"), m_ctyXmlStatus, QStringLiteral("success"));
        m_awardsDirty = m_globalAwardsDirty = true;
        emit awardsChanged();
        emit ctyChanged();
    });
}

void DecoLogController::checkEntitiesByDate()
{
    if (m_ctyXml.isEmpty() || !m_db.isOpen() || m_ctyXmlBusy)
        return;
    m_ctyXmlBusy = true;
    m_entityFixes.clear();
    m_entityFixesInfo = QVariantMap{{QStringLiteral("busy"), true}};
    emit ctyChanged();
    const QString path = m_db.path();
    const ClubLogCty cty = m_ctyXml;
    const Countries countries = m_countries;
    QPointer<DecoLogController> self(this);
    m_ctyPool.start([self, path, cty, countries] {
        QList<EntityFix> fixes;
        QVariantList examples;
        int checked = 0;
        LogDatabase db;
        if (db.open(path)) {
            QSqlQuery q(db.connection());
            q.setForwardOnly(true);
            q.exec(QStringLiteral("SELECT id, call, qso_datetime_on, IFNULL(dxcc, 0) FROM qso NOT INDEXED WHERE deleted = 0"));
            while (q.next()) {
                ++checked;
                const QString call = q.value(1).toString();
                const QDateTime when = QDateTime::fromString(q.value(2).toString(), Qt::ISODate);
                const int stored = q.value(3).toInt();
                const CtyMatch m = cty.lookup(call, when);
                if (!m.found || m.adif == stored)
                    continue;
                // Si corregge solo un DXCC vuoto o messo dal cty.csv di adesso:
                // quello scritto da LoTW o dall'operatore resta.
                const auto current = countries.lookup(call);
                if (stored != 0 && !(current && current->dxcc == stored))
                    continue;
                EntityFix fix;
                fix.id = q.value(0).toLongLong();
                fix.dxcc = m.adif;
                fix.country = current && current->dxcc == m.adif ? current->name : m.name;
                fix.cqz = m.cqz;
                fix.cont = m.cont;
                fixes << fix;
                if (examples.size() < 12) {
                    examples << QVariantMap{
                        {QStringLiteral("call"), call},
                        {QStringLiteral("date"), when.date().toString(Qt::ISODate)},
                        {QStringLiteral("from"), stored},
                        {QStringLiteral("to"), m.adif},
                        {QStringLiteral("name"), fix.country},
                        {QStringLiteral("deleted"), m.deleted},
                    };
                }
            }
            db.close();
        }
        QMetaObject::invokeMethod(
            self.data(),
            [self, fixes, examples, checked] {
                if (!self)
                    return;
                self->m_ctyXmlBusy = false;
                self->m_entityFixes = fixes;
                self->m_entityFixesInfo = QVariantMap{{QStringLiteral("count"), int(fixes.size())},
                                                      {QStringLiteral("checked"), checked},
                                                      {QStringLiteral("examples"), examples}};
                emit self->ctyChanged();
            },
            Qt::QueuedConnection);
    });
}

int DecoLogController::applyEntityFixes()
{
    if (m_entityFixes.isEmpty())
        return 0;
    int done = 0;
    QSqlDatabase db = m_db.connection();
    const bool transaction = db.transaction();
    for (const EntityFix& fix : std::as_const(m_entityFixes)) {
        auto r = m_db.record(fix.id);
        if (!r)
            continue;
        r->set(QStringLiteral("DXCC"), QString::number(fix.dxcc));
        r->set(QStringLiteral("COUNTRY"), fix.country);
        if (fix.cqz > 0)
            r->set(QStringLiteral("CQZ"), QString::number(fix.cqz));
        if (!fix.cont.isEmpty())
            r->set(QStringLiteral("CONT"), fix.cont);
        if (m_db.updateQso(fix.id, *r, -1, QStringLiteral("dxcc-date")).status == InsertResult::Status::Inserted)
            ++done;
    }
    if (transaction)
        db.commit();
    m_entityFixes.clear();
    m_entityFixesInfo.clear();
    addActivity(QStringLiteral("DXCC"), tr("Entity of %n QSO(s) corrected with the date of the QSO (Club Log)", nullptr, done),
                done > 0 ? QStringLiteral("success") : QStringLiteral("info"));
    timed(tr("reloading the log table"), [this] { m_model->reload(); });
    m_awardsDirty = m_globalAwardsDirty = true;
    emit awardsChanged();
    emit logChanged();
    m_decoLink.resendSnapshot();
    emit ctyChanged();
    return done;
}

int DecoLogController::fillMissingDxcc()
{
    int filled = 0;
    const QList<qint64> ids = m_db.idsWithoutDxcc();
    // Una revisione per QSO, ognuna nella sua transazione: se un record non si
    // puo' salvare, gli altri non ne risentono.
    for (qint64 id : ids) {
        auto r = m_db.record(id);
        if (!r || !applyEntity(*r))
            continue;
        if (m_db.updateQso(id, *r).status == InsertResult::Status::Inserted)
            ++filled;
    }
    addActivity(QStringLiteral("LOG"), tr("DXCC filled on %1 of %2 QSO (cty.csv %3)")
                                           .arg(filled).arg(ids.size()).arg(m_countries.version()),
                filled > 0 ? QStringLiteral("success") : QStringLiteral("info"));
    timed(tr("reloading the log table"), [this] { m_model->reload(); });
    emit logChanged();
    m_decoLink.resendSnapshot();
    refreshCallInfo();
    return filled;
}

// ── Award ─────────────────────────────────────────────────────────────────────

const QList<AwardResult>& DecoLogController::awardResults() const
{
    decolog::StartupSpan trace("DecoLogController::awardResults");
    if (m_awardsDirty && m_db.isOpen()) {
        // Su un log in un file si contano su un altro filo: fino al risultato
        // vale quello di prima (vuoto all'avvio), e awardsChanged() lo dice.
        if (backgroundReady()) {
            scheduleStatsRefresh();
            return m_awardCache;
        }
        QHash<int, QString> names;
        for (const auto& entity : m_countries.entities())
            names.insert(entity.dxcc, entity.name);
        m_awardCache = awardCalculator(names).compute(m_db, m_awardFilter);
        m_awardsDirty = false;
    }
    return m_awardCache;
}

const QList<AwardResult>& DecoLogController::globalAwardResults() const
{
    decolog::StartupSpan trace("DecoLogController::globalAwardResults");
    if (m_globalAwardsDirty && m_db.isOpen()) {
        if (backgroundReady()) {
            scheduleStatsRefresh();
            return m_globalAwardCache;
        }
        AwardFilter filter;
        filter.confirmLotw = m_awardFilter.confirmLotw;
        filter.confirmCard = m_awardFilter.confirmCard;
        filter.confirmEqsl = m_awardFilter.confirmEqsl;
        filter.count60m = m_awardFilter.count60m;
        filter.credits = m_awardFilter.credits;
        QHash<int, QString> names;
        for (const auto& entity : m_countries.entities())
            names.insert(entity.dxcc, entity.name);
        m_globalAwardCache = awardCalculator(names).compute(m_db, filter);
        m_globalAwardsDirty = false;
    }
    return m_globalAwardCache;
}

QVariantList DecoLogController::awardSummary() const
{
    decolog::StartupSpan trace("DecoLogController::awardSummary");
    QVariantList out;
    const QStringList bands = awardBands();
    for (const AwardResult& r : awardResults()) {
        const bool officialOnly = r.id == QLatin1String("wac");
        int slotsWorked = 0, slotsConfirmed = 0;
        for (const BandTotal& t : r.bandTotals(bands, officialOnly)) {
            slotsWorked += t.worked;
            slotsConfirmed += t.confirmed;
        }
        out << QVariantMap{
            {QStringLiteral("id"), r.id},
            {QStringLiteral("title"), r.title},
            {QStringLiteral("officialRules"), r.officialRules},
            {QStringLiteral("rulesSource"), r.rulesSource},
            {QStringLiteral("verifier"), r.verifier},
            {QStringLiteral("worked"), r.worked(officialOnly)},
            {QStringLiteral("confirmed"), r.confirmed(officialOnly)},
            // Il traguardo del WAAC non e' un numero inventato: sono tutte le
            // entita' africane che il cty.csv conosce.
            {QStringLiteral("target"), r.id == QLatin1String("waac") && africanEntities() > 0
                                           ? africanEntities() : r.target},
            {QStringLiteral("total"), r.id == QLatin1String("dxcc") && m_countries.entityCount() > 0
                                          ? m_countries.entityCount()
                                          : r.id == QLatin1String("waac") ? africanEntities() : r.total},
            {QStringLiteral("slotsWorked"), slotsWorked},
            {QStringLiteral("slotsConfirmed"), slotsConfirmed},
            {QStringLiteral("official60m"),
             (r.id == QLatin1String("dxcc") || r.id == QLatin1String("challenge")
              || r.id == QLatin1String("was")) && !m_awardFilter.count60m},
            {QStringLiteral("personal60m"),
             (r.id == QLatin1String("dxcc") || r.id == QLatin1String("challenge")
              || r.id == QLatin1String("was")) && m_awardFilter.count60m},
            // Quello che il regolamento chiede oltre al numero: vuoto per quasi
            // tutti, una riga per chi ne ha (il DCI vuole anche le regioni).
            {QStringLiteral("requirement"), r.requirement},
        };

        // Il DXCC Challenge non e' un altro elenco di entita': sono gli stessi
        // DXCC contati banda per banda, dai 160 ai 6 metri. Per l'ARRL sono
        // dieci bande, senza i 60; con i 60 contati sono undici. Mille slot e'
        // il traguardo del primo riconoscimento.
        if (r.id == QLatin1String("dxcc")) {
            QStringList challengeBands{
                QStringLiteral("160m"), QStringLiteral("80m"),
                QStringLiteral("40m"), QStringLiteral("30m"), QStringLiteral("20m"),
                QStringLiteral("17m"), QStringLiteral("15m"), QStringLiteral("12m"),
                QStringLiteral("10m"), QStringLiteral("6m")};
            if (m_awardFilter.count60m)
                challengeBands.insert(2, QStringLiteral("60m"));
            int worked = 0, confirmed = 0;
            for (const BandTotal& t : r.bandTotals(challengeBands)) {
                worked += t.worked;
                confirmed += t.confirmed;
            }
            out << QVariantMap{
                {QStringLiteral("id"), QStringLiteral("challenge")},
                {QStringLiteral("title"), QStringLiteral("DXCC Challenge")},
                {QStringLiteral("worked"), worked},
                {QStringLiteral("confirmed"), confirmed},
                {QStringLiteral("target"), 1000},
                {QStringLiteral("total"), 0},
                {QStringLiteral("slotsWorked"), worked},
                {QStringLiteral("slotsConfirmed"), confirmed},
                // Non e' un award con i suoi elementi: la tabella per banda non
                // lo riguarda, il numero si legge nel riquadro.
                {QStringLiteral("derived"), true},
            };
        }
    }
    return out;
}

int DecoLogController::africanEntities() const
{
    // Quante entita' DXCC stanno in Africa: lo dice il cty.csv, e cambia quando
    // si aggiorna. Si conta una volta sola per file caricato.
    static QString countedFor;
    static int count = 0;
    if (countedFor != m_countries.version() || count == 0) {
        count = 0;
        for (const DxccEntity& e : m_countries.entities()) {
            if (e.continent.trimmed().toUpper() == QLatin1String("AF"))
                ++count;
        }
        countedFor = m_countries.version();
    }
    return count;
}

QStringList DecoLogController::awardBands() const
{
    // Le colonne della tabella: le bande presenti nel log, in ordine.
    return m_db.bandsInLog();
}

bool DecoLogController::awardHasMissing(const QString& awardId) const
{
    return awardId == QLatin1String("dxcc") || awardId == QLatin1String("ft2") || awardId == QLatin1String("waz")
        || awardId == QLatin1String("was");
}

QVariantList DecoLogController::awardBandTotals(const QString& awardId) const
{
    QVariantList out;
    for (const AwardResult& r : awardResults()) {
        if (r.id != awardId)
            continue;
        for (const BandTotal& t : r.bandTotals(awardBands(), r.id == QLatin1String("wac"))) {
            out << QVariantMap{{QStringLiteral("band"), t.band},
                               {QStringLiteral("worked"), t.worked},
                               {QStringLiteral("confirmed"), t.confirmed}};
        }
    }
    return out;
}

QVariantMap DecoLogController::cqMarathonPreview(int year, const QString& category,
                                                  int maxPerEntity) const
{
    core::cqmarathon::Rules rules;
    rules.year = year;
    rules.category = category;
    rules.maxPerEntity = maxPerEntity;

    QString error;
    const QList<core::cqmarathon::Entry> entries = core::cqmarathon::loadEntries(m_db, &error);
    if (!error.isEmpty())
        return QVariantMap{{QStringLiteral("valid"), false},
                           {QStringLiteral("error"), error},
                           {QStringLiteral("selected"), QVariantList()},
                           {QStringLiteral("exclusions"), QStringList()}};

    const core::cqmarathon::Result result = core::cqmarathon::select(entries, rules);
    QVariantList selected;
    for (const auto& entry : result.selected) {
        selected << QVariantMap{{QStringLiteral("qsoId"), entry.qsoId},
                                {QStringLiteral("call"), entry.call},
                                {QStringLiteral("dxcc"), entry.dxcc},
                                {QStringLiteral("cqZone"), entry.cqZone},
                                {QStringLiteral("band"), entry.band},
                                {QStringLiteral("mode"), entry.mode},
                                {QStringLiteral("timestamp"), entry.timestamp.toUTC().toString(Qt::ISODateWithMs)}};
    }
    QString validationError;
    return QVariantMap{{QStringLiteral("valid"), result.isValid(&validationError)},
                       {QStringLiteral("error"), validationError},
                       {QStringLiteral("selected"), selected},
                       {QStringLiteral("exclusions"), result.exclusions},
                       {QStringLiteral("json"), QString::fromUtf8(result.toJson().toJson(QJsonDocument::Indented))},
                       {QStringLiteral("edition"), rules.year},
                       {QStringLiteral("category"), rules.category.trimmed().toLower()},
                       {QStringLiteral("maxPerEntity"), rules.maxPerEntity}};
}

bool DecoLogController::saveCqMarathonExport(const QUrl& file, int year, const QString& category,
                                              int maxPerEntity) const
{
    if (!file.isLocalFile())
        return false;
    core::cqmarathon::Rules rules{year, category, maxPerEntity};
    QString error;
    const auto entries = core::cqmarathon::loadEntries(m_db, &error);
    if (!error.isEmpty())
        return false;
    const auto result = core::cqmarathon::select(entries, rules);
    if (!result.isValid(&error))
        return false;
    QFile output(file.toLocalFile());
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return output.write(result.toJson().toJson(QJsonDocument::Indented)) >= 0;
}

QVariantMap DecoLogController::vuccPreview(const QString& band, const QString& account,
                                            const QString& site, int target) const
{
    core::vucc::Rules rules{account, site, band, target};
    QString error;
    const auto entries = core::vucc::loadEntries(m_db, &error);
    if (!error.isEmpty())
        return QVariantMap{{QStringLiteral("valid"), false}, {QStringLiteral("error"), error},
                           {QStringLiteral("eligible"), QVariantList()}, {QStringLiteral("exclusions"), QStringList()}};
    const auto result = core::vucc::select(entries, rules);
    QVariantList eligible;
    for (const auto& entry : result.eligible)
        eligible << QVariantMap{{QStringLiteral("qsoId"), entry.qsoId},
                                {QStringLiteral("grid4"), entry.grid},
                                {QStringLiteral("band"), entry.band},
                                {QStringLiteral("account"), entry.account},
                                {QStringLiteral("site"), entry.site},
                                {QStringLiteral("evidence"), entry.evidence}};
    QString validationError;
    return QVariantMap{{QStringLiteral("valid"), result.isValid(&validationError)},
                       {QStringLiteral("error"), validationError},
                       {QStringLiteral("officialRulesVerified"), result.officialRulesVerified},
                       {QStringLiteral("eligible"), eligible},
                       {QStringLiteral("exclusions"), result.exclusions},
                       {QStringLiteral("json"), QString::fromUtf8(result.toJson().toJson(QJsonDocument::Indented))}};
}

QVariantList DecoLogController::awardGrids() const
{
    QVariantList out;
    for (const AwardResult& r : awardResults()) {
        if (r.id != QLatin1String("grids"))
            continue;
        for (const AwardItem& i : r.items)
            out << QVariantMap{{QStringLiteral("grid"), i.key}, {QStringLiteral("confirmed"), i.confirmed()}};
    }
    return out;
}

QVariantList DecoLogController::awardItems(const QString& awardId, const QString& search, const QString& view) const
{
    QVariantList out;
    const QString needle = search.trimmed().toUpper();
    auto matches = [&needle](const QString& key, const QString& name) {
        return needle.isEmpty() || key.toUpper().contains(needle) || name.toUpper().contains(needle);
    };

    if (view == QLatin1String("missing")) {
        // L'elenco completo meno quello che c'e' nei risultati.
        QSet<QString> worked;
        for (const AwardResult& r : awardResults()) {
            if (r.id == awardId) {
                for (const AwardItem& i : r.items)
                    worked.insert(i.key);
            }
        }
        auto missing = [&](const QString& key, const QString& name) {
            if (worked.contains(key) || !matches(key, name))
                return;
            out << QVariantMap{
                {QStringLiteral("key"), key}, {QStringLiteral("name"), name},
                {QStringLiteral("bandsWorked"), QStringList()}, {QStringLiteral("bandsConfirmed"), QStringList()},
                {QStringLiteral("qsoCount"), 0}, {QStringLiteral("first"), QString()}, {QStringLiteral("last"), QString()},
                {QStringLiteral("firstQsoId"), 0}, {QStringLiteral("firstCall"), QString()},
                {QStringLiteral("confirmed"), false}, {QStringLiteral("missing"), true},
            };
        };
        if (awardId == QLatin1String("dxcc") || awardId == QLatin1String("ft2")) {
            for (const DxccEntity& e : m_countries.entities())
                missing(QString::number(e.dxcc), QStringLiteral("%1 · %2 · %3").arg(e.name, e.prefix, e.continent));
        } else if (awardId == QLatin1String("waz")) {
            for (int zone = 1; zone <= 40; ++zone)
                missing(QString::number(zone), QString());
        } else if (awardId == QLatin1String("was")) {
            const auto& states = awards::usStates();
            for (auto it = states.cbegin(); it != states.cend(); ++it)
                missing(it.key(), it.value());
        }
        return out;
    }

    const bool onlyUnconfirmed = view == QLatin1String("unconfirmed");
    for (const AwardResult& r : awardResults()) {
        if (r.id != awardId)
            continue;
        for (const AwardItem& i : r.items) {
            if (onlyUnconfirmed && i.confirmed())
                continue;
            if (!needle.isEmpty() && !i.key.toUpper().contains(needle) && !i.name.toUpper().contains(needle)
                && !i.firstCall.contains(needle))
                continue;
            out << QVariantMap{
                {QStringLiteral("key"), i.key},
                {QStringLiteral("name"), i.name},
                {QStringLiteral("bandsWorked"), QStringList(i.bandsWorked.cbegin(), i.bandsWorked.cend())},
                {QStringLiteral("bandsConfirmed"), QStringList(i.bandsConfirmed.cbegin(), i.bandsConfirmed.cend())},
                {QStringLiteral("qsoCount"), i.qsoCount},
                {QStringLiteral("first"), i.first.isValid() ? i.first.toString(dates::format()) : QString()},
                {QStringLiteral("last"), i.last.isValid() ? i.last.toString(dates::format()) : QString()},
                {QStringLiteral("firstQsoId"), i.firstQsoId},
                {QStringLiteral("firstCall"), i.firstCall},
                {QStringLiteral("confirmed"), i.confirmed()},
            };
        }
    }
    return out;
}

void DecoLogController::awardFilterChanged()
{
    QSettings s;
    s.setValue(QStringLiteral("awards/band"), m_awardFilter.band);
    s.setValue(QStringLiteral("awards/modeGroup"), m_awardFilter.modeGroup);
    s.setValue(QStringLiteral("awards/confirmLotw"), m_awardFilter.confirmLotw);
    s.setValue(QStringLiteral("awards/confirmCard"), m_awardFilter.confirmCard);
    s.setValue(QStringLiteral("awards/confirmEqsl"), m_awardFilter.confirmEqsl);
    s.setValue(QStringLiteral("awards/count60m"), m_awardFilter.count60m);
    s.setValue(QStringLiteral("awards/profile"), m_awardFilter.stationProfileId);
    s.setValue(QStringLiteral("awards/tag"), m_awardFilter.tag);
    m_awardsDirty = m_globalAwardsDirty = true;
    emit awardsChanged();
}

void DecoLogController::setAwardBand(const QString& band)
{
    if (band == m_awardFilter.band) return;
    m_awardFilter.band = band;
    awardFilterChanged();
}

void DecoLogController::setAwardModeGroup(const QString& group)
{
    if (group == m_awardFilter.modeGroup) return;
    m_awardFilter.modeGroup = group;
    awardFilterChanged();
}

QVariantMap DecoLogController::awardCredits(const QString& awardId) const
{
    return QVariantMap{
        {QStringLiteral("services"), awards::creditsFor(awardId, m_awardFilter)},
        {QStringLiteral("official"), awards::officialCredits(awardId)},
        {QStringLiteral("custom"), m_awardFilter.credits.contains(awardId)},
    };
}

void DecoLogController::setAwardCredit(const QString& awardId, const QString& service, bool on)
{
    if (!awards::creditServices().contains(service))
        return;
    QStringList list = awards::creditsFor(awardId, m_awardFilter);
    if (on && !list.contains(service))
        list << service;
    if (!on)
        list.removeAll(service);
    m_awardFilter.credits.insert(awardId, list);
    // "Nessuna" si scrive come tale: una lista vuota nel file non si distingue
    // da una che non c'e'.
    QSettings().setValue(QStringLiteral("awards/credits/") + awardId,
                         list.isEmpty() ? QStringLiteral(",") : list.join(QLatin1Char(',')));
    m_awardsDirty = m_globalAwardsDirty = true;
    emit awardsChanged();
}

void DecoLogController::resetAwardCredits(const QString& awardId)
{
    if (!m_awardFilter.credits.remove(awardId))
        return;
    QSettings().remove(QStringLiteral("awards/credits/") + awardId);
    m_awardsDirty = m_globalAwardsDirty = true;
    emit awardsChanged();
}

QVariantList DecoLogController::awardClaims() const
{
    QString error;
    const QList<core::awards::AwardClaim> claims = core::awards::AwardClaimStore::load(m_db, &error);
    QVariantList out;
    for (const auto& claim : claims) {
        QVariantMap item{
            {QStringLiteral("id"), claim.id},
            {QStringLiteral("awardId"), claim.awardId},
            {QStringLiteral("accountId"), claim.accountId},
            {QStringLiteral("category"), claim.category},
            {QStringLiteral("ruleVersion"), claim.ruleVersion},
            {QStringLiteral("state"), core::awards::AwardClaim::stateName(claim.state)},
            {QStringLiteral("createdAt"), claim.createdAt.toUTC().toString(Qt::ISODateWithMs)},
            {QStringLiteral("submittedAt"), claim.submittedAt.isValid() ? claim.submittedAt.toUTC().toString(Qt::ISODateWithMs) : QString()},
            {QStringLiteral("decidedAt"), claim.decidedAt.isValid() ? claim.decidedAt.toUTC().toString(Qt::ISODateWithMs) : QString()},
            {QStringLiteral("qsoIds"), QVariant::fromValue(claim.qsoIds)},
            {QStringLiteral("outcome"), claim.outcome},
            {QStringLiteral("evidence"), claim.evidence},
            {QStringLiteral("externalProvider"), claim.externalProvider},
            {QStringLiteral("creditedQsoIds"), QVariant::fromValue(claim.creditedQsoIds)},
            {QStringLiteral("rejectedQsoIds"), QVariant::fromValue(claim.rejectedQsoIds)}};
        out << item;
    }
    return out;
}

QString DecoLogController::createAwardClaim(const QString& awardId, const QString& ruleVersion,
                                             const QVariantList& qsoIds)
{
    if (awardId.trimmed().isEmpty() || ruleVersion.trimmed().isEmpty() || qsoIds.isEmpty())
        return {};
    core::awards::AwardClaim claim;
    claim.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    claim.awardId = awardId.trimmed();
    claim.ruleVersion = ruleVersion.trimmed();
    claim.createdAt = QDateTime::currentDateTimeUtc();
    for (const QVariant& value : qsoIds) {
        const qint64 id = value.toLongLong();
        if (id > 0)
            claim.qsoIds << id;
    }
    if (claim.qsoIds.isEmpty())
        return {};
    QString error;
    auto claims = core::awards::AwardClaimStore::load(m_db, &error);
    claims << claim;
    if (!core::awards::AwardClaimStore::save(m_db, claims, &error))
        return {};
    emit awardsChanged();
    return claim.id;
}

bool DecoLogController::transitionAwardClaim(const QString& claimId, const QString& state,
                                              const QString& note, const QString& evidence)
{
    const auto target = core::awards::AwardClaim::stateFromName(state);
    if (!target)
        return false;
    QString error;
    auto claims = core::awards::AwardClaimStore::load(m_db, &error);
    for (auto& claim : claims) {
        if (claim.id != claimId)
            continue;
        if (!claim.transitionTo(*target, QDateTime::currentDateTimeUtc(), note, &error))
            return false;
        if (!evidence.isEmpty())
            claim.evidence = evidence;
        if (!core::awards::AwardClaimStore::save(m_db, claims, &error))
            return false;
        emit awardsChanged();
        return true;
    }
    return false;
}

QString DecoLogController::reconcileAwardResult(const QString& json)
{
    QString error;
    if (!core::awards::AwardClaimStore::reconcileExternalResult(m_db, json.toUtf8(), &error))
        return error.isEmpty() ? tr("External result was rejected") : error;
    emit awardsChanged();
    return {};
}

void DecoLogController::setAwardConfirmLotw(bool on)
{
    if (on == m_awardFilter.confirmLotw) return;
    m_awardFilter.confirmLotw = on;
    awardFilterChanged();
}

void DecoLogController::setAwardConfirmCard(bool on)
{
    if (on == m_awardFilter.confirmCard) return;
    m_awardFilter.confirmCard = on;
    awardFilterChanged();
}

void DecoLogController::setAwardProfile(int profileId)
{
    if (profileId == m_awardFilter.stationProfileId) return;
    m_awardFilter.stationProfileId = qMax(0, profileId);
    awardFilterChanged();
}

void DecoLogController::setAwardTag(const QString& tag)
{
    if (tag.simplified() == m_awardFilter.tag) return;
    m_awardFilter.tag = tag.simplified();
    awardFilterChanged();
}

void DecoLogController::setAwardConfirmEqsl(bool on)
{
    if (on == m_awardFilter.confirmEqsl) return;
    m_awardFilter.confirmEqsl = on;
    awardFilterChanged();
}

void DecoLogController::setAwardCount60m(bool on)
{
    if (on == m_awardFilter.count60m) return;
    m_awardFilter.count60m = on;
    awardFilterChanged();
}

// ── Stazione ──────────────────────────────────────────────────────────────────

QString DecoLogController::myGrid() const
{
    const QString grid = m_profiles ? m_profiles->activeProfile().value(QStringLiteral("myGridsquare")).toString()
                                    : QString();
    return grid.isEmpty() ? m_status.deGrid : grid;
}

QVariantMap DecoLogController::myPosition() const
{
    return positionMap(maidenhead::toLatLon(myGrid()));
}

// Al primo avvio non ci sono profili: se Decodium dice chi e' e dove sta, se ne
// crea uno. L'operatore lo ritrova in "Station profiles" e lo puo' correggere.
void DecoLogController::maybeCreateProfileFromDecodium()
{
    if (!m_profiles || m_profiles->count() > 0 || m_status.deCall.isEmpty())
        return;
    QVariantMap p{
        {QStringLiteral("name"), m_status.deGrid.isEmpty()
                                     ? m_status.deCall
                                     : QStringLiteral("%1 %2").arg(m_status.deCall, m_status.deGrid.left(6))},
        {QStringLiteral("stationCallsign"), m_status.deCall},
        {QStringLiteral("myGridsquare"), m_status.deGrid},
        {QStringLiteral("isDefault"), true},
    };
    if (m_profiles->save(p) > 0)
        addActivity(QStringLiteral("LOG"), tr("Station profile created from Decodium: %1").arg(p.value("name").toString()),
                    QStringLiteral("success"));
}

void DecoLogController::applyProfile(AdifRecord& record, qint64 profileId) const
{
    if (!m_profiles || profileId <= 0)
        return;
    const QVariantMap p = m_profiles->byId(profileId);
    auto fill = [&record](const char* field, const QString& value) {
        if (record.value(QLatin1String(field)).isEmpty() && !value.isEmpty())
            record.set(QLatin1String(field), value);
    };
    fill("STATION_CALLSIGN", p.value(QStringLiteral("stationCallsign")).toString());
    fill("OPERATOR", p.value(QStringLiteral("operatorCall")).toString());
    fill("MY_GRIDSQUARE", p.value(QStringLiteral("myGridsquare")).toString());
    fill("MY_RIG", p.value(QStringLiteral("myRig")).toString());
    fill("MY_ANTENNA", p.value(QStringLiteral("myAntenna")).toString());
    const double pwr = p.value(QStringLiteral("defaultTxPwr")).toDouble();
    if (pwr > 0)
        fill("TX_PWR", QString::number(pwr));
}

// ── QSO in arrivo ─────────────────────────────────────────────────────────────

InsertResult DecoLogController::onQsoReceived(const AdifRecord& input, const QString& source, const QString& sourceApp)
{
    if (input.value(QStringLiteral("APP_DECOLOG_DIAGNOSTIC")) == QLatin1String("1")) {
        InsertResult isolated;
        isolated.status = InsertResult::Status::Invalid;
        isolated.message = QStringLiteral("diagnostic QSO is isolated and is not written to the log");
        return isolated;
    }
    // Il profilo viene risolto dall'identita' dichiarata dal programma esterno:
    // prima STATION_CALLSIGN, poi OPERATOR. Se piu' profili hanno lo stesso
    // operatore e manca il nominativo di stazione, il QSO resta senza profilo
    // invece di finire in silenzio nel log sbagliato. Senza identita' si conserva
    // il comportamento storico del profilo attivo.
    const QString stationCall = input.value(QStringLiteral("STATION_CALLSIGN")).trimmed();
    const QString operatorCall = input.value(QStringLiteral("OPERATOR")).trimmed();
    qint64 profileId = m_db.profileForIdentity(stationCall, operatorCall);
    const int liveProfiles = m_db.stationProfiles(false).size();
    if (profileId == 0 && stationCall.isEmpty() && operatorCall.isEmpty() && m_profiles)
        profileId = m_profiles->activeProfileId();
    else if (profileId == 0 && liveProfiles <= 1 && m_profiles)
        profileId = m_profiles->activeProfileId();
    if (profileId == 0 && (!stationCall.isEmpty() || !operatorCall.isEmpty()) && liveProfiles > 1) {
        addActivity(QStringLiteral("LOG"),
                    tr("Incoming QSO %1 has no unique station profile; it was kept unassigned")
                        .arg(input.value(QStringLiteral("CALL")).toUpper()),
                    QStringLiteral("warning"));
    }

    // Decodium manda zone e locatore ma non il numero DXCC: senza, il QSO non
    // conta per l'FT2 Award.
    AdifRecord enriched = input;
    applyEntity(enriched);
    // Dentro un'attivazione il QSO prende la referenza e il numero progressivo, e
    // "duplicato" vuol dire "gia' fatto in questa attivazione".
    m_activation->applyTo(enriched);
    if (m_activation->active() && m_activation->session().stationProfileId > 0)
        profileId = m_activation->session().stationProfileId;
    AdifRecord normalizedForDupe = enriched;
    adif::normalizeMode(normalizedForDupe);
    const QString dupeMode = normalizedForDupe.value(QStringLiteral("SUBMODE")).isEmpty()
                                 ? normalizedForDupe.value(QStringLiteral("MODE"))
                                 : normalizedForDupe.value(QStringLiteral("SUBMODE"));
    InsertResult r;
    if (m_activation->isDuplicate(enriched.value(QStringLiteral("CALL")), enriched.value(QStringLiteral("BAND")), dupeMode)) {
        r.status = InsertResult::Status::Duplicate;
        r.message = tr("%1 %2 %3: already worked in this activation")
                        .arg(enriched.value(QStringLiteral("CALL")).toUpper(),
                             enriched.value(QStringLiteral("BAND")), dupeMode);
    } else {
        r = m_db.insertQso(enriched, source, sourceApp, false, profileId);
    }

    const QString call = input.value(QStringLiteral("CALL")).toUpper();
    AdifRecord normalized = input;
    adif::normalizeMode(normalized);
    const QString submode = normalized.value(QStringLiteral("SUBMODE"));
    const QString mode = submode.isEmpty() ? normalized.value(QStringLiteral("MODE")) : submode;
    const QString freq = input.value(QStringLiteral("FREQ"));
    QVariantMap item{
        {QStringLiteral("time"), nowUtcLabel()},
        {QStringLiteral("call"), call},
        {QStringLiteral("band"), input.value(QStringLiteral("BAND"))},
        {QStringLiteral("freq"), freq.isEmpty() ? QString() : QString::number(freq.toDouble(), 'f', 3)},
        {QStringLiteral("mode"), mode},
        {QStringLiteral("rstSent"), input.value(QStringLiteral("RST_SENT"))},
        {QStringLiteral("rstRcvd"), input.value(QStringLiteral("RST_RCVD"))},
        {QStringLiteral("grid"), input.value(QStringLiteral("GRIDSQUARE"))},
        {QStringLiteral("app"), sourceApp},
        {QStringLiteral("message"), source == QLatin1String("n1mm") ? QStringLiteral("N1MM")
                                    : m_udp.prefersLoggedAdif() && source.startsWith(QLatin1String("udp"))
                                        ? QStringLiteral("LoggedADIF") : QStringLiteral("QSOLogged")},
        {QStringLiteral("id"), r.id},
    };

    switch (r.status) {
    case InsertResult::Status::Inserted: {
        item[QStringLiteral("status")] = QStringLiteral("logged");
        decoLinkQso(enriched, QStringLiteral("logged"), r.id, source, sourceApp);
        hamHubQso(enriched, r.id);
        m_qsl->qsoLogged(r.id);
        m_activation->qsoLogged();
        if (m_net)
            m_net->qsoLogged(r.id);
        m_cloud->qsoLogged();
        m_model->insertQso(r.id);
        const auto meta = m_db.meta(r.id);
        QString text = tr("%1 from %2 → %3 %4 %5 saved (uuid %6)")
                           .arg(item.value(QStringLiteral("message")).toString(), sourceApp, call,
                                item.value(QStringLiteral("band")).toString(), mode,
                                meta ? meta->uuid.left(4) + QStringLiteral("…") + meta->uuid.right(2) : QString());
        const bool newDxcc = mode == QLatin1String("FT2") && m_db.isFirstFt2Dxcc(r.id);
        if (newDxcc)
            text += tr(" · new DXCC on FT2: %1").arg(enriched.value(QStringLiteral("COUNTRY")).isEmpty()
                                                         ? enriched.value(QStringLiteral("DXCC"))
                                                         : enriched.value(QStringLiteral("COUNTRY")));
        item[QStringLiteral("newDxcc")] = newDxcc;
        addActivity(QStringLiteral("UDP"), text, newDxcc ? QStringLiteral("highlight") : QStringLiteral("success"));
        emit logChanged();
        // Decodium manda l'essenziale: nome, locatore e indirizzo li sa il
        // callbook, e il QSO se li prende da solo.
        completeFromCallbook(r.id, call);
        break;
    }
    case InsertResult::Status::Duplicate:
        item[QStringLiteral("status")] = QStringLiteral("duplicate");
        decoLinkQso(enriched, QStringLiteral("duplicate"), r.id, source, sourceApp);
        addActivity(QStringLiteral("UDP"), tr("Duplicate ignored: %1").arg(r.message), QStringLiteral("warning"));
        break;
    case InsertResult::Status::Invalid:
    case InsertResult::Status::Error:
        item[QStringLiteral("status")] = QStringLiteral("error");
        decoLinkQso(enriched, QStringLiteral("error"), 0, source, sourceApp, r.message);
        addActivity(QStringLiteral("UDP"), tr("QSO not logged: %1").arg(r.message), QStringLiteral("error"));
        break;
    }

    m_incoming.prepend(item);
    while (m_incoming.size() > kMaxIncoming)
        m_incoming.removeLast();
    emit incomingChanged();

    if (call == m_lookupCall.toUpper())
        refreshCallInfo();
    return r;
}

// ── L'interfaccia HTTP locale ─────────────────────────────────────────────────
//
// Per chi scrive un programma accanto a DecoDXLog: sapere se un nominativo e'
// gia' stato lavorato, leggere gli ultimi QSO, registrarne uno. Tutto JSON, e
// la chiave in ogni richiesta (X-DecoDXLog-Token o Authorization: Bearer).

void DecoLogController::setApiPort(int port)
{
    if (port == m_apiPort || port < 0 || port > 65535)
        return;
    m_apiPort = port;
    QSettings().setValue(QStringLiteral("api/port"), port);
    startApi();
}

void DecoLogController::newApiToken()
{
    m_apiToken = LocalApiServer::newToken();
    QSettings().setValue(QStringLiteral("api/token"), m_apiToken);
    m_api.setToken(m_apiToken);
    addActivity(QStringLiteral("API"), tr("New key for the local interface: the programs using the old one must be updated"),
                QStringLiteral("warning"));
    emit udpChanged();
}

void DecoLogController::startApi()
{
    if (m_apiPort <= 0) {
        m_api.stop();
        emit udpChanged();
        return;
    }
    if (m_apiToken.isEmpty()) {
        m_apiToken = LocalApiServer::newToken();
        QSettings().setValue(QStringLiteral("api/token"), m_apiToken);
    }
    m_api.setToken(m_apiToken);
    m_api.setHandler([this](const HttpRequest& r) { return handleApi(r); });
    if (m_api.start(static_cast<quint16>(m_apiPort)))
        addActivity(QStringLiteral("API"), tr("Local interface on http://127.0.0.1:%1/api/v1/").arg(m_apiPort));
    else
        addActivity(QStringLiteral("API"), tr("Cannot open the local interface on port %1: %2").arg(m_apiPort).arg(m_api.lastError()),
                    QStringLiteral("error"));
    emit udpChanged();
}

namespace {

QJsonObject qsoJson(qint64 id, const AdifRecord& r)
{
    QJsonObject o{{QStringLiteral("id"), id}};
    for (const auto& f : r.fields()) {
        if (!f.value.isEmpty())
            o.insert(f.name.toLower(), f.value);
    }
    return o;
}

} // namespace

HttpResponse DecoLogController::handleApi(const HttpRequest& request)
{
    const QString path = request.path.endsWith(QLatin1Char('/')) ? request.path.chopped(1) : request.path;
    if (!m_db.isOpen())
        return HttpResponse::error(503, QStringLiteral("no log open"));

    if (path == QLatin1String("/api/v1/status")) {
        if (request.method != "GET")
            return HttpResponse::error(405, QStringLiteral("use GET"));
        return HttpResponse::json(200, QJsonObject{
            {QStringLiteral("app"), QStringLiteral("DecoDXLog")},
            {QStringLiteral("version"), QCoreApplication::applicationVersion()},
            {QStringLiteral("log"), QFileInfo(m_db.path()).completeBaseName()},
            {QStringLiteral("qsos"), m_db.qsoCount()},
            {QStringLiteral("station"), m_profiles ? m_profiles->activeProfile().value(QStringLiteral("stationCallsign")).toString()
                                                   : QString()},
            {QStringLiteral("decodium"), clientConnected()},
        });
    }

    if (path == QLatin1String("/api/v1/worked")) {
        if (request.method != "GET")
            return HttpResponse::error(405, QStringLiteral("use GET"));
        const QString call = request.query.queryItemValue(QStringLiteral("call")).trimmed().toUpper();
        if (call.isEmpty())
            return HttpResponse::error(400, QStringLiteral("call is required"));
        const QString band = request.query.queryItemValue(QStringLiteral("band")).trimmed().toLower();
        const QString mode = request.query.queryItemValue(QStringLiteral("mode")).trimmed().toUpper();
        const WorkedBefore w = m_db.workedBefore(call);
        QJsonObject o{
            {QStringLiteral("call"), call},
            {QStringLiteral("count"), w.count},
            {QStringLiteral("bands"), QJsonArray::fromStringList(w.bands)},
            {QStringLiteral("modes"), QJsonArray::fromStringList(w.modes)},
            {QStringLiteral("last"), w.last.isValid() ? w.last.toUTC().toString(Qt::ISODate) : QString()},
            {QStringLiteral("dxcc"), w.dxcc},
            {QStringLiteral("country"), w.country},
        };
        if (!band.isEmpty())
            o.insert(QStringLiteral("workedBand"), w.bands.contains(band, Qt::CaseInsensitive));
        if (!mode.isEmpty())
            o.insert(QStringLiteral("workedMode"), w.modes.contains(mode, Qt::CaseInsensitive));
        return HttpResponse::json(200, o);
    }

    if (path == QLatin1String("/api/v1/qsos")) {
        if (request.method != "GET")
            return HttpResponse::error(405, QStringLiteral("use GET"));
        const int limit = request.query.hasQueryItem(QStringLiteral("limit"))
                              ? request.query.queryItemValue(QStringLiteral("limit")).toInt()
                              : 20;
        QJsonArray list;
        for (const qint64 id : m_db.latestIds(request.query.queryItemValue(QStringLiteral("call")), limit)) {
            if (const auto r = m_db.record(id))
                list << qsoJson(id, *r);
        }
        return HttpResponse::json(200, QJsonObject{{QStringLiteral("qsos"), list}});
    }

    if (path == QLatin1String("/api/v1/qso")) {
        if (request.method != "POST")
            return HttpResponse::error(405, QStringLiteral("use POST"));
        const QString idempotencyKey = QString::fromUtf8(request.headers.value("idempotency-key")).trimmed();
        if (idempotencyKey.size() > 128)
            return HttpResponse::error(400, QStringLiteral("idempotency key is too long"));
        if (!idempotencyKey.isEmpty()) {
            const auto previous = m_db.apiIdempotency(idempotencyKey);
            if (previous) {
                if (previous->requestBody != request.body)
                    return HttpResponse::error(409, QStringLiteral("idempotency key was used with a different body"));
                return HttpResponse{previous->status, previous->contentType, previous->responseBody};
            }
        }
        // ADIF nel corpo, o JSON {"adif": "..."}.
        QByteArray adifText = request.body;
        if (request.headers.value("content-type").contains("json")) {
            const QJsonObject o = QJsonDocument::fromJson(request.body).object();
            adifText = o.value(QStringLiteral("adif")).toString().toUtf8();
        }
        const AdifDocument doc = adif::parse(adifText);
        if (doc.records.isEmpty())
            return HttpResponse::error(400, QStringLiteral("no ADIF record in the body"));
        const QString app = QString::fromUtf8(request.headers.value("x-app")).trimmed();
        QJsonArray results;
        int saved = 0;
        for (AdifRecord record : doc.records) {
            adif::normalizeMode(record);
            const InsertResult r = onQsoReceived(record, QStringLiteral("api"),
                                                 app.isEmpty() ? QStringLiteral("API") : app.left(60));
            const QString status = r.status == InsertResult::Status::Inserted    ? QStringLiteral("logged")
                                 : r.status == InsertResult::Status::Duplicate ? QStringLiteral("duplicate")
                                                                               : QStringLiteral("error");
            if (r.status == InsertResult::Status::Inserted)
                ++saved;
            results << QJsonObject{{QStringLiteral("status"), status}, {QStringLiteral("id"), r.id},
                                   {QStringLiteral("message"), r.message}};
        }
        const HttpResponse response = HttpResponse::json(saved > 0 ? 201 : 409,
                                                          QJsonObject{{QStringLiteral("results"), results}});
        if (!idempotencyKey.isEmpty())
            m_db.saveApiIdempotency(idempotencyKey,
                                    ApiIdempotencyRecord{request.body, response.status, response.contentType,
                                                         response.body, QDateTime::currentDateTimeUtc().toString(Qt::ISODate)});
        return response;
    }

    return HttpResponse::error(404, QStringLiteral("unknown path: see /api/v1/status, /worked, /qsos, /qso"));
}

// ── Il VFO della barra in alto ────────────────────────────────────────────────
//
// Chi opera gira la manopola: qui la manopola e' la rotellina sopra le cifre, e
// la cifra che cambia e' quella sotto il puntatore. Quello che si decide qui
// va alla radio (via Hamlib) e a Decodium (via DecoLink), cosi' i due restano
// d'accordo invece di raccontarsi due frequenze diverse.

void DecoLogController::tuneTo(double mhz, const QString& mode)
{
    if (mhz <= 0 && mode.isEmpty())
        return;
    const double khz = mhz * 1000.0;
    // SO2R: la sintonia va alla radio che ha il fuoco.
    if (m_so2r && m_so2r->radio2HasFocus() && m_so2r->radio2Connected()) {
        if (mhz > 0)
            m_so2r->radio2()->setFrequency(static_cast<qint64>(std::llround(mhz * 1e6)));
        if (!mode.isEmpty())
            m_so2r->radio2()->setMode(modes::catFor(mode, mhz));
        return;
    }
    auto* rig = qobject_cast<RigController*>(m_rig);
    const bool toRadio = rig && rig->connected();
    // A Decodium si manda solo quando c'e' una frequenza: un "vai" senza dire
    // dove non vuol dire niente.
    const bool toDecodium = m_decoLink.clientCount() > 0 && mhz > 0;

    if (!toRadio && !toDecodium) {
        addActivity(QStringLiteral("RIG"),
                    tr("Nowhere to send the frequency: the radio is not connected and "
                       "Decodium is not there either."),
                    QStringLiteral("warning"));
        return;
    }

    if (toRadio) {
        // Il modo si tocca solo se e' stato chiesto: girando la rotellina si
        // cambia la frequenza, non il modo.
        rig->tuneTo(mhz > 0 ? static_cast<qint64>(std::llround(mhz * 1e6)) : 0,
                    mode.isEmpty() ? QString() : modes::catFor(mode, mhz));
    }

    if (toDecodium) {
        // Per i modi digitali Decodium vuole la frequenza del VFO e il tono
        // nell'audio: se quella scritta cade in una sotto-banda conosciuta, si
        // separano le due cose come fa il cluster.
        const auto tuning = spots::tuningFor(khz, mode);
        m_decoLink.broadcast(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("tune")},
            {QStringLiteral("freqKhz"), khz},
            {QStringLiteral("dialKhz"), tuning.dialKhz},
            {QStringLiteral("audioHz"), tuning.audioHz},
            {QStringLiteral("mode"), mode},
        });
    }

    const QString where = toRadio && toDecodium ? tr("radio and Decodium")
                        : toRadio               ? tr("radio")
                                                : QStringLiteral("Decodium");
    if (mhz > 0) {
        addActivity(QStringLiteral("RIG"),
                    tr("Tuned to %1 MHz %2 (%3)")
                        .arg(QString::number(mhz, 'f', 6), mode.isEmpty() ? QStringLiteral("—") : mode, where));
    } else {
        addActivity(QStringLiteral("RIG"), tr("Mode %1 (%2)").arg(mode, where));
    }
}

QVariantList DecoLogController::operatingModes() const
{
    QVariantList out;
    for (const auto& e : modes::all()) {
        out.append(QVariantMap{{QStringLiteral("name"), e.name},
                               {QStringLiteral("cat"), e.cat},
                               {QStringLiteral("group"), e.group}});
    }
    return out;
}

// ── QSO a mano ────────────────────────────────────────────────────────────────

QVariantMap DecoLogController::utcNow() const
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    return {{QStringLiteral("date"), now.toString(QStringLiteral("yyyy-MM-dd"))},
            {QStringLiteral("time"), now.toString(QStringLiteral("HH:mm"))}};
}

QString DecoLogController::showDate(const QString& isoOrAdif) const
{
    return dates::show(isoOrAdif);
}

QString DecoLogController::readDate(const QString& text) const
{
    return dates::read(text);
}

QString DecoLogController::dateHint() const
{
    if (!dates::dayFirst())
        return tr("yyyy-mm-dd");
    //: How a date is typed, day first; the separator is replaced by the language's own.
    return tr("dd/mm/yyyy").replace(QLatin1Char('/'), dates::format().mid(2, 1));
}

QStringList DecoLogController::newQsoVisibleFields() const
{
    QSettings settings;
    const QString key = QStringLiteral("ui/newQso/visibleFields");
    if (!settings.contains(key))
        return newqso::defaultVisibleFields();
    return newqso::normalizeVisibleFields(settings.value(key).toStringList());
}

QVariantList DecoLogController::newQsoCustomFields() const
{
    return newqso::decodeCustomFields(
        QSettings().value(QStringLiteral("ui/newQso/customFields")).toString());
}

QVariantList DecoLogController::newQsoFieldOptions() const
{
    auto option = [](const QString& key, const QString& label) {
        return QVariantMap{{QStringLiteral("key"), key}, {QStringLiteral("label"), label}};
    };
    return {
        option(QStringLiteral("GRIDSQUARE"), tr("Grid locator")),
        option(QStringLiteral("NAME"), tr("Operator name")),
        option(QStringLiteral("QTH"), tr("QTH")),
        option(QStringLiteral("STATE"), tr("State / province")),
        option(QStringLiteral("TX_PWR"), tr("TX power")),
        option(QStringLiteral("PROP_MODE"), tr("Propagation mode")),
        option(QStringLiteral("SAT_NAME"), tr("Satellite")),
        option(QStringLiteral("SAT_MODE"), tr("Satellite mode")),
        option(QStringLiteral("COMMENT"), tr("Comment")),
    };
}

QString DecoLogController::saveNewQsoPanelSettings(const QStringList& visibleFields,
                                                   const QVariantList& customFields)
{
    const newqso::CustomFieldsResult checked = newqso::validateCustomFields(customFields);
    if (!checked.ok()) {
        switch (checked.error) {
        case newqso::CustomFieldError::InvalidName:
            return tr("Invalid ADIF field name: %1. Use 1–32 letters, digits or underscores, starting with a letter.")
                .arg(checked.field.isEmpty() ? tr("(empty)") : checked.field);
        case newqso::CustomFieldError::Duplicate:
            return tr("The ADIF field %1 is listed more than once.").arg(checked.field);
        case newqso::CustomFieldError::StandardField:
            return tr("%1 is already available as a standard field. Enable it above instead.").arg(checked.field);
        case newqso::CustomFieldError::ProtectedField:
            return tr("%1 is managed by DecoDXLog and cannot be replaced by a custom field.").arg(checked.field);
        case newqso::CustomFieldError::TooMany:
            return tr("You can add at most %1 custom ADIF fields.").arg(newqso::kMaxCustomFields);
        case newqso::CustomFieldError::None:
            break;
        }
    }

    QSettings settings;
    settings.setValue(QStringLiteral("ui/newQso/visibleFields"),
                      newqso::normalizeVisibleFields(visibleFields));
    settings.setValue(QStringLiteral("ui/newQso/customFields"),
                      newqso::encodeCustomFields(checked.fields));
    settings.sync();
    if (settings.status() != QSettings::NoError)
        return tr("The New QSO field settings could not be saved.");
    emit newQsoSettingsChanged();
    return {};
}

void DecoLogController::resetNewQsoPanelSettings()
{
    QSettings settings;
    settings.remove(QStringLiteral("ui/newQso/visibleFields"));
    settings.remove(QStringLiteral("ui/newQso/customFields"));
    settings.sync();
    emit newQsoSettingsChanged();
}

QString DecoLogController::logManualQso(const QVariantMap& fields)
{
    auto text = [&fields](const char* key) { return fields.value(QLatin1String(key)).toString().trimmed(); };

    AdifRecord r;
    r.set(QStringLiteral("CALL"), text("call").toUpper());
    const QDate date = QDate::fromString(dates::read(text("date")), QStringLiteral("yyyy-MM-dd"));
    if (!date.isValid())
        return tr("Enter a valid UTC date");
    QTime time = QTime::fromString(text("time"), QStringLiteral("HH:mm"));
    if (!time.isValid())
        time = QTime::fromString(text("time"), QStringLiteral("HH:mm:ss"));
    if (!time.isValid())
        time = QTime::fromString(text("time"), QStringLiteral("HHmm"));
    if (date.isValid())
        r.set(QStringLiteral("QSO_DATE"), date.toString(QStringLiteral("yyyyMMdd")));
    if (time.isValid())
        r.set(QStringLiteral("TIME_ON"), time.toString(QStringLiteral("HHmmss")));

    QString freq = text("freq");
    freq.replace(QLatin1Char(','), QLatin1Char('.'));
    // In split la frequenza del QSO e' quella di trasmissione, e quella di
    // ricezione va in FREQ_RX: se la frequenza e' quella che la radio sta
    // ascoltando, la trasmissione la sa la radio.
    QString freqRx = text("freq_rx");
    freqRx.replace(QLatin1Char(','), QLatin1Char('.'));
    if (auto* rig = qobject_cast<RigController*>(m_rig); rig && rig->connected() && rig->split()
        && rig->txFrequencyHz() > 0 && freqRx.isEmpty()) {
        const double typed = freq.toDouble();
        const double rx = rig->frequencyHz() / 1e6;
        if (freq.isEmpty() || qAbs(typed - rx) < 0.001) {
            freqRx = QString::number(rx, 'f', 6);
            freq = QString::number(rig->txFrequencyHz() / 1e6, 'f', 6);
        }
    }
    r.set(QStringLiteral("FREQ"), freq);
    if (!freqRx.isEmpty() && freqRx != freq) {
        r.set(QStringLiteral("FREQ_RX"), freqRx);
        r.set(QStringLiteral("BAND_RX"), bandForFrequency(freqRx));
    }
    r.set(QStringLiteral("BAND"), text("band").isEmpty() ? bandForFrequency(freq) : text("band"));
    r.set(QStringLiteral("MODE"), text("mode").toUpper());
    r.set(QStringLiteral("SUBMODE"), text("submode").toUpper());
    r.set(QStringLiteral("RST_SENT"), text("rst_sent"));
    r.set(QStringLiteral("RST_RCVD"), text("rst_rcvd"));
    r.set(QStringLiteral("NAME"), text("name"));
    r.set(QStringLiteral("QTH"), text("qth"));
    r.set(QStringLiteral("GRIDSQUARE"), text("gridsquare").toUpper());
    r.set(QStringLiteral("TX_PWR"), text("tx_pwr"));
    r.set(QStringLiteral("POTA_REF"), text("pota_ref").toUpper());
    r.set(QStringLiteral("SOTA_REF"), text("sota_ref").toUpper());
    r.set(QStringLiteral("IOTA"), text("iota").toUpper());
    r.set(QStringLiteral("WWFF_REF"), text("wwff_ref").toUpper());
    // Il castello si scrive come viene — "na 015", "NA-015" — e si mette a
    // posto qui: il regolamento lo vuole attaccato, "NA015".
    const QString castle = text("dci").toUpper().remove(QLatin1Char(' ')).remove(QLatin1Char('-'));
    if (!castle.isEmpty()) {
        r.set(QStringLiteral("SIG"), QStringLiteral("DCI"));
        r.set(QStringLiteral("SIG_INFO"), castle);
    }
    r.set(QStringLiteral("PROP_MODE"), text("prop_mode").toUpper());
    r.set(QStringLiteral("SAT_NAME"), text("sat_name").toUpper());
    r.set(QStringLiteral("SAT_MODE"), text("sat_mode").toUpper());
    r.set(QStringLiteral("COMMENT"), text("comment"));
    // Quello che dice il callbook e che prima si poteva scrivere solo dopo:
    // nazione, citta', zone, stato e contea entrano subito nel QSO.
    r.set(QStringLiteral("COUNTRY"), text("country"));
    r.set(QStringLiteral("ADDRESS"), text("address"));
    r.set(QStringLiteral("STATE"), text("state").toUpper());
    r.set(QStringLiteral("CNTY"), text("cnty"));
    r.set(QStringLiteral("CONT"), text("cont").toUpper());
    if (!text("cqz").isEmpty())
        r.set(QStringLiteral("CQZ"), text("cqz"));
    if (!text("ituz").isEmpty())
        r.set(QStringLiteral("ITUZ"), text("ituz"));
    if (!text("dxcc").isEmpty())
        r.set(QStringLiteral("DXCC"), text("dxcc"));
    r.set(QStringLiteral("EMAIL"), text("email"));
    r.set(QStringLiteral("QSL_VIA"), text("qsl_via").toUpper());
    r.set(QStringLiteral("APP_DECOLOG_TAGS"), text("tags"));
    // Campi aggiunti dall'operatore nel pannello compatto. I campi fondamentali
    // e quelli standard del pannello non si possono mascherare con una seconda
    // casella dallo stesso nome; i veri campi extra finiscono in adif_extra e
    // tornano nell'export ADIF senza perdere nulla.
    const QVariantMap customFields = fields.value(QStringLiteral("custom_fields")).toMap();
    const QStringList standardPanelFields = newqso::standardFields();
    for (auto it = customFields.cbegin(); it != customFields.cend(); ++it) {
        const QString name = newqso::normalizeAdifName(it.key());
        if (!newqso::isValidAdifName(name))
            return tr("Invalid custom ADIF field: %1").arg(it.key());
        if (newqso::isProtectedAdifField(name))
            return tr("The ADIF field %1 is managed by DecoDXLog.").arg(name);
        if (standardPanelFields.contains(name))
            return tr("Use the standard %1 field instead of a custom field.").arg(name);
        r.set(name, it.value().toString().trimmed());
    }
    // Contest: il numero ricevuto, come lo vuole ADIF.
    if (!text("srx").isEmpty()) {
        r.set(QStringLiteral("SRX"), text("srx"));
        r.set(QStringLiteral("SRX_STRING"), text("srx"));
    }

    qint64 profileId = m_profiles ? m_profiles->activeProfileId() : 0;
    const qint64 requestedProfileId = fields.value(QStringLiteral("station_profile_id")).toLongLong();
    if (requestedProfileId > 0) {
        const auto requested = m_db.stationProfile(requestedProfileId);
        if (!requested || requested->deleted)
            return tr("The selected station profile is not available.");
        profileId = requestedProfileId;
    }
    if (m_activation->active() && m_activation->session().stationProfileId > 0)
        profileId = m_activation->session().stationProfileId;
    applyProfile(r, profileId);
    applyEntity(r);
    m_activation->applyTo(r);
    // A mano non si rifiuta nessun QSO per doppione: chi scrive sa cosa ha gia' nel log, e
    // ripassare la stessa stazione (in un'attivazione, o un'altra volta) e' normale.
    // Il controllo dei doppioni resta per Decodium e per i QSO che arrivano via UDP.
    const InsertResult res = m_db.insertQso(r, QStringLiteral("manual"), QStringLiteral("DecoDXLog ") + version(),
                                            true, profileId, false);
    switch (res.status) {
    case InsertResult::Status::Inserted: {
        decolog::StartupSpan t1("logManual: model insert");
        m_model->insertQso(res.id);
        broadcastN1mmQso(r, res.id);
        hamHubQso(r, res.id);
        }
        {
        decolog::StartupSpan t2("logManual: decolink+qsl+cloud+activation");
        decoLinkQso(r, QStringLiteral("logged"), res.id, QStringLiteral("manual"), QStringLiteral("DecoDXLog"));
        m_qsl->qsoLogged(res.id);
        m_cloud->qsoLogged();
        m_activation->qsoLogged();
        if (m_net)
            m_net->qsoLogged(res.id);
        addActivity(QStringLiteral("LOG"), tr("Logged %1 %2 %3 (manual)")
                                               .arg(r.value(QStringLiteral("CALL")), r.value(QStringLiteral("BAND")),
                                                    r.value(QStringLiteral("MODE"))),
                    QStringLiteral("success"));
        }
        {
        decolog::StartupSpan t3("logManual: logChanged");
        emit logChanged();
        }
        {
        decolog::StartupSpan t4("logManual: lookup+callinfo");
        setLookupCall(r.value(QStringLiteral("CALL")));
        refreshCallInfo();
        }
        // Anche quello scritto a mano si completa: chi lo scrive di fretta,
        // fra un QSO e l'altro, non ha tempo di cercare il locatore.
        completeFromCallbook(res.id, r.value(QStringLiteral("CALL")).toUpper());
        return {};
    case InsertResult::Status::Duplicate:
        return tr("Already in log (within %n minute(s))", nullptr, dedupManualMinutes());
    default:
        return res.message;
    }
}

// ── Scheda QSO ────────────────────────────────────────────────────────────────

QVariantMap DecoLogController::qsoDetail(qint64 id) const
{
    const auto record = m_db.record(id);
    const auto meta = m_db.meta(id);
    if (!record || !meta)
        return {};

    QVariantMap fields;
    QVariantList extra;
    for (const auto& f : record->fields()) {
        fields.insert(f.name, f.value);
        if (!kKnownFields.contains(f.name))
            extra << QVariantMap{{QStringLiteral("name"), f.name}, {QStringLiteral("value"), f.value}};
    }

    QVariantList qsl;
    const QList<QslState> states = m_db.qslStatus(id);
    for (const QString& service : kServices) {
        QslState st;
        st.service = service;
        for (const auto& s : states) {
            if (s.service == service)
                st = s;
        }
        qsl << QVariantMap{
            {QStringLiteral("service"), service},
            {QStringLiteral("label"), serviceLabel(service)},
            {QStringLiteral("sent"), st.sent},
            {QStringLiteral("sentDate"), st.sentDate},
            {QStringLiteral("rcvd"), st.rcvd},
            {QStringLiteral("rcvdDate"), st.rcvdDate},
            {QStringLiteral("lastError"), st.lastError},
            {QStringLiteral("hasRcvd"), service != QLatin1String("clublog")},
        };
    }

    QVariantList history;
    for (const HistoryEntry& h : m_db.history(id)) {
        history << QVariantMap{
            {QStringLiteral("id"), h.id},
            {QStringLiteral("revision"), h.revision},
            {QStringLiteral("reason"), h.reason},
            {QStringLiteral("recordedAt"), h.recordedAt.toString(dates::format() + QStringLiteral(" HH:mm:ss"))},
            {QStringLiteral("summary"), QStringLiteral("%1 %2 %3 %4")
                                            .arg(h.record.value(QStringLiteral("CALL")),
                                                 h.record.value(QStringLiteral("BAND")),
                                                 h.record.value(QStringLiteral("SUBMODE")).isEmpty()
                                                     ? h.record.value(QStringLiteral("MODE"))
                                                     : h.record.value(QStringLiteral("SUBMODE")),
                                                 h.record.value(QStringLiteral("NAME")))},
        };
    }

    QVariantList attachments;
    for (const QslAttachment& a : m_db.qslAttachments(id))
        attachments << QVariantMap{{QStringLiteral("id"), a.id}, {QStringLiteral("sha256"), a.sha256},
                                   {QStringLiteral("filename"), a.filename}, {QStringLiteral("mime"), a.mime},
                                   {QStringLiteral("size"), a.size}, {QStringLiteral("createdAt"), a.createdAt}};

    QVariantMap detail{
        {QStringLiteral("id"), id},
        {QStringLiteral("fields"), fields},
        {QStringLiteral("extra"), extra},
        {QStringLiteral("qsl"), qsl},
        {QStringLiteral("history"), history},
        {QStringLiteral("attachments"), attachments},
        {QStringLiteral("uuid"), meta->uuid},
        {QStringLiteral("revision"), meta->revision},
        {QStringLiteral("source"), meta->source},
        {QStringLiteral("sourceApp"), meta->sourceApp},
        {QStringLiteral("createdAt"), meta->createdAt},
        {QStringLiteral("updatedAt"), meta->updatedAt},
        {QStringLiteral("dirty"), meta->dirty},
        {QStringLiteral("stationProfileId"), meta->stationProfileId},
        {QStringLiteral("firstFt2Dxcc"), m_db.isFirstFt2Dxcc(id)},
    };

    const auto dx = maidenhead::toLatLon(record->value(QStringLiteral("GRIDSQUARE")));
    const QString ownGrid = record->value(QStringLiteral("MY_GRIDSQUARE")).isEmpty()
                                ? myGrid() : record->value(QStringLiteral("MY_GRIDSQUARE"));
    const auto me = maidenhead::toLatLon(ownGrid);
    if (dx && me) {
        detail[QStringLiteral("distanceKm")] = qRound(maidenhead::distanceKm(*me, *dx));
        detail[QStringLiteral("azimuth")] = qRound(maidenhead::azimuthDeg(*me, *dx));
    }
    if (dx || me) {
        // Dal conto gia' fatto: rifarlo a ogni scheda aperta costava secondi
        // sui log grandi.
        detail[QStringLiteral("ft2DxccWorked")] = ft2Award().value(QStringLiteral("dxccWorked")).toInt();
    }
    return detail;
}

QString DecoLogController::importQslAttachment(const QVariantList& qsoIds, const QUrl& file)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly))
        return tr("Cannot read attachment: %1").arg(input.errorString());
    const QByteArray data = input.readAll();
    QList<qint64> ids;
    for (const QVariant& value : qsoIds) {
        const qint64 id = value.toLongLong();
        if (id > 0 && m_db.record(id))
            ids << id;
    }
    if (ids.isEmpty() || data.isEmpty())
        return tr("Choose at least one QSO and a non-empty file");
    const QFileInfo info(path);
    const qint64 id = m_db.addQslAttachment(data, info.fileName(), QMimeDatabase().mimeTypeForFile(info).name(), ids);
    if (id <= 0)
        return tr("Cannot store QSL attachment");
    addActivity(QStringLiteral("QSL"), tr("Attached %1 to %n QSO(s)").arg(info.fileName()).arg(ids.size()), QStringLiteral("info"));
    emit logChanged();
    return {};
}

QString DecoLogController::saveQslAttachment(qint64 attachmentId, const QUrl& file) const
{
    const QByteArray data = m_db.qslAttachmentData(attachmentId);
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (data.isEmpty() || path.isEmpty())
        return tr("Attachment not found");
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly) || output.write(data) != data.size() || !output.commit())
        return tr("Cannot write attachment");
    return {};
}

QVariantList DecoLogController::integrationDiagnostics() const
{
    const bool logOpen = m_db.isOpen();
    const bool decodium = clientConnected();
    const QVariantList qslServices = m_qsl ? m_qsl->services() : QVariantList{};
    int qslReady = 0;
    for (const QVariant& item : qslServices)
        qslReady += item.toMap().value(QStringLiteral("ready")).toBool() ? 1 : 0;
    QVariantList out = {
        QVariantMap{{QStringLiteral("id"), QStringLiteral("receive")}, {QStringLiteral("label"), tr("Receive")},
                    {QStringLiteral("state"), decodium ? QStringLiteral("ok") : QStringLiteral("warning")},
                    {QStringLiteral("detail"), decodium ? tr("Decodium connection is active") : tr("No Decodium client is connected")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("parse")}, {QStringLiteral("label"), tr("Parse")},
                    {QStringLiteral("state"), QStringLiteral("ok")},
                    {QStringLiteral("detail"), tr("ADIF parser is available")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("dedupe")}, {QStringLiteral("label"), tr("Deduplicate")},
                    {QStringLiteral("state"), QStringLiteral("ok")},
                    {QStringLiteral("detail"), tr("Duplicate protection is enabled")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("save")}, {QStringLiteral("label"), tr("Save")},
                    {QStringLiteral("state"), logOpen ? QStringLiteral("ok") : QStringLiteral("error")},
                    {QStringLiteral("detail"), logOpen ? tr("Local log is open") : tr("No local log is open")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("qsl")}, {QStringLiteral("label"), tr("QSL")},
                    {QStringLiteral("state"), qslReady > 0 ? QStringLiteral("ok") : QStringLiteral("info")},
                    {QStringLiteral("detail"), tr("%1 QSL service(s) ready; diagnostic mode never uploads").arg(qslReady)}}
    };
    const bool rigConnected = m_rig && m_rig->connected();
    diagnostics::StationContext station;
    if (m_rig) {
        station.backend = m_rig->link();
        station.model = m_rig->rigModel() > 0 ? QString::number(m_rig->rigModel()) : QStringLiteral("not selected");
        station.firmware = QStringLiteral("not reported by CAT backend");
    }
    const auto checklist = diagnostics::operationalChecklist(logOpen, rigConnected,
                                                              m_rig ? m_rig->frequencyHz() : 0,
                                                              m_rig ? m_rig->features() : 0, station);
    for (const auto& step : checklist)
        out << QVariantMap{{QStringLiteral("id"), step.id},
                           {QStringLiteral("label"), step.id.toUpper()},
                           {QStringLiteral("state"), step.state},
                           {QStringLiteral("detail"), step.detail}};
    return out;
}

QString DecoLogController::diagnosticSupportText() const
{
    QStringList lines;
    lines << QStringLiteral("DecoDXLog diagnostic summary")
          << QStringLiteral("version=%1").arg(QCoreApplication::applicationVersion())
          << QStringLiteral("log_open=%1").arg(m_db.isOpen() ? QStringLiteral("yes") : QStringLiteral("no"));
    for (const QVariant& item : integrationDiagnostics()) {
        const QVariantMap row = item.toMap();
        lines << QStringLiteral("%1: %2 — %3").arg(row.value(QStringLiteral("label")).toString(),
                                                   row.value(QStringLiteral("state")).toString(),
                                                   row.value(QStringLiteral("detail")).toString());
    }
    for (auto it = m_uiLatencySamples.cbegin(); it != m_uiLatencySamples.cend(); ++it)
        lines << core::performance::toText(core::performance::summarize(it.value()),
                                           QStringLiteral("ui.%1").arg(it.key()));
    return diagnostics::redactSecrets(lines.join(QLatin1Char('\n')));
}

QString DecoLogController::diagnosticSupportJson() const
{
    QJsonObject report;
    report.insert(QStringLiteral("schema"), QStringLiteral("decodxlog.diagnostics/v1"));
    report.insert(QStringLiteral("generatedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    report.insert(QStringLiteral("applicationVersion"), QCoreApplication::applicationVersion());
    report.insert(QStringLiteral("verificationScope"), QStringLiteral("local-observation; hardware and external services not exercised automatically"));
    report.insert(QStringLiteral("supportText"), diagnosticSupportText());
    QJsonArray steps;
    for (const QVariant& item : integrationDiagnostics())
        steps.append(QJsonObject::fromVariantMap(item.toMap()));
    report.insert(QStringLiteral("steps"), steps);
    QJsonArray performance;
    for (auto it = m_uiLatencySamples.cbegin(); it != m_uiLatencySamples.cend(); ++it) {
        const auto summary = core::performance::summarize(it.value());
        performance.append(QJsonObject{{QStringLiteral("scenario"), it.key()},
                                       {QStringLiteral("samples"), summary.samples},
                                       {QStringLiteral("p50Ms"), summary.p50Ms},
                                       {QStringLiteral("p95Ms"), summary.p95Ms},
                                       {QStringLiteral("maxMs"), summary.maxMs}});
    }
    report.insert(QStringLiteral("uiLatency"), performance);
    return QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Indented));
}

void DecoLogController::recordUiLatency(const QString& scenario, qint64 elapsedMs)
{
    const QString key = scenario.trimmed().left(80);
    if (key.isEmpty())
        return;
    QList<qint64>& samples = m_uiLatencySamples[key];
    samples.append(qMax<qint64>(0, elapsedMs));
    while (samples.size() > 256)
        samples.removeFirst();
}

QString DecoLogController::saveDiagnosticReport(const QUrl& file) const
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (path.isEmpty())
        return tr("Choose a file first");
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly))
        return tr("Cannot write diagnostic report: %1").arg(output.errorString());
    const QByteArray text = diagnosticSupportText().toUtf8();
    if (output.write(text) != text.size() || !output.commit())
        return tr("Cannot save diagnostic report");
    return {};
}

QString DecoLogController::saveDiagnosticJsonReport(const QUrl& file) const
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (path.isEmpty())
        return tr("Choose a file first");
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly))
        return tr("Cannot write diagnostic report: %1").arg(output.errorString());
    const QByteArray json = diagnosticSupportJson().toUtf8();
    if (output.write(json) != json.size() || !output.commit())
        return tr("Cannot save diagnostic report");
    return {};
}

QVariantMap DecoLogController::qslManagerSuggestion(qint64 qsoId) const
{
    const auto record = m_db.record(qsoId);
    if (!record)
        return {};
    const QString via = record->value(QStringLiteral("QSL_VIA")).trimmed().toUpper();
    QVariantMap out{{QStringLiteral("available"), !via.isEmpty()},
                    {QStringLiteral("manager"), via},
                    {QStringLiteral("requiresApproval"), true},
                    {QStringLiteral("automaticSend"), false}};
    if (via.isEmpty()) {
        out.insert(QStringLiteral("message"), tr("No QSL manager is recorded for this QSO"));
        return out;
    }
    QStringList potentialCredits;
    for (const QslState& state : m_db.qslStatus(qsoId)) {
        if (state.rcvd.trimmed().isEmpty() || state.rcvd == QLatin1String("N"))
            potentialCredits << state.service;
    }
    potentialCredits.removeDuplicates();
    out.insert(QStringLiteral("potentialCredits"), potentialCredits);
    QString source = tr("entered in the QSO");
    QString acquired;
    for (const EnrichmentEntry& e : m_db.enrichments(qsoId)) {
        if (e.field == QLatin1String("QSL_VIA") && e.value.trimmed().compare(via, Qt::CaseInsensitive) == 0) {
            source = e.source;
            acquired = e.acquiredAt;
        }
    }
    const QDateTime seen = QDateTime::fromString(acquired, Qt::ISODate);
    const bool stale = seen.isValid() && seen.daysTo(QDateTime::currentDateTimeUtc()) > 180;
    out.insert(QStringLiteral("source"), source);
    out.insert(QStringLiteral("acquiredAt"), acquired);
    out.insert(QStringLiteral("stale"), stale);
    const QString credits = potentialCredits.isEmpty()
        ? tr("no unconfirmed service is recorded")
        : tr("potentially useful for: %1").arg(potentialCredits.join(QStringLiteral(", ")));
    out.insert(QStringLiteral("message"), stale
                   ? tr("Manager %1 may be outdated; verify the route before sending; %2").arg(via, credits)
                   : tr("Manager %1 could resolve the paper QSL for this QSO; %2; review and approve the route")
                         .arg(via, credits));
    return out;
}

namespace {

// Le colonne del log che si correggono sul posto, e il campo ADIF di ognuna.
QString inlineField(const QString& columnKey)
{
    static const QHash<QString, QString> fields{
        {QStringLiteral("call"), QStringLiteral("CALL")},
        {QStringLiteral("rst_sent"), QStringLiteral("RST_SENT")},
        {QStringLiteral("rst_rcvd"), QStringLiteral("RST_RCVD")},
        {QStringLiteral("grid"), QStringLiteral("GRIDSQUARE")},
        {QStringLiteral("name"), QStringLiteral("NAME")},
        {QStringLiteral("qth"), QStringLiteral("QTH")},
        {QStringLiteral("comment"), QStringLiteral("COMMENT")},
    };
    return fields.value(columnKey);
}

} // namespace

bool DecoLogController::canEditInline(const QString& columnKey) const
{
    return !inlineField(columnKey).isEmpty();
}

QString DecoLogController::editQsoField(qint64 id, const QString& columnKey, const QString& value)
{
    const QString field = inlineField(columnKey);
    if (field.isEmpty())
        return tr("This column cannot be edited here: open the QSO card");
    auto record = m_db.record(id);
    if (!record)
        return tr("QSO %1 not found").arg(id);
    QString text = value.simplified();
    if (field == QLatin1String("CALL") || field == QLatin1String("GRIDSQUARE"))
        text = text.toUpper();
    if (field == QLatin1String("CALL")) {
        if (text.isEmpty())
            return tr("The callsign cannot be empty");
        if (text.contains(QLatin1Char(' ')))
            return tr("A callsign has no spaces");
    }
    // Niente da correggere: non si scrive una revisione per niente.
    if (record->value(field) == text)
        return {};
    record->set(field, text);   // vuoto toglie il campo
    return saveRecord(id, *record, -1);
}

QString DecoLogController::saveQso(qint64 id, const QVariantMap& fields, qint64 stationProfileId)
{
    AdifRecord r;
    for (auto it = fields.cbegin(); it != fields.cend(); ++it)
        r.set(it.key(), it.value().toString().trimmed());
    return saveRecord(id, r, stationProfileId);
}

QString DecoLogController::saveRecord(qint64 id, AdifRecord r, qint64 stationProfileId)
{
    // Un nominativo sbagliato e corretto: quello che veniva dal vecchio (nazione, DXCC, zone,
    // continente) si rifa' dal nuovo — a meno che l'operatore l'abbia toccato nello stesso
    // salvataggio — e i servizi che hanno ricevuto il QSO sbagliato lo rimandano.
    const auto before = m_db.record(id);
    const QString oldCall = before ? before->value(QStringLiteral("CALL")).trimmed().toUpper() : QString();
    const QString newCall = r.value(QStringLiteral("CALL")).trimmed().toUpper();
    const bool callChanged = !oldCall.isEmpty() && !newCall.isEmpty() && oldCall != newCall;
    if (callChanged) {
        r.set(QStringLiteral("CALL"), newCall);
        for (const char* name : {"COUNTRY", "DXCC", "CQZ", "ITUZ", "CONT"}) {
            const QString key = QLatin1String(name);
            if (r.value(key) == before->value(key))
                r.remove(key);
        }
        applyEntity(r);
    }
    const InsertResult res = m_db.updateQso(id, r, stationProfileId);
    if (res.status != InsertResult::Status::Inserted)
        return res.message.isEmpty() ? tr("Cannot save the QSO") : res.message;
    QStringList resent;
    if (callChanged) {
        for (const QString& service : m_db.markSentForResend(id))
            resent << serviceLabel(service);
    }
    // Dove il QSO si puo' correggere (CRX), la correzione va anche li'.
    m_db.queueRemoteEdit(id);
    m_qsl->qsoLogged(id);
    const auto meta = m_db.meta(id);
    const int revision = meta ? meta->revision : 0;
    addActivity(QStringLiteral("LOG"),
                callChanged ? tr("Call corrected: %1 → %2 · revision %3").arg(oldCall, newCall).arg(revision)
                            : tr("Edited %1 · revision %2").arg(r.value(QStringLiteral("CALL"))).arg(revision),
                QStringLiteral("success"));
    if (!resent.isEmpty()) {
        addActivity(QStringLiteral("QSL"),
                    tr("%1 had already gone to %2 with the wrong call: it will be sent again").arg(newCall, resent.join(QStringLiteral(", "))),
                    QStringLiteral("warning"));
    }
    timed(tr("reloading the log table"), [this] { m_model->reload(); });
    emit logChanged();
    m_decoLink.resendSnapshot();
    refreshCallInfo();
    return {};
}

int DecoLogController::broadcastN1mmQso(const AdifRecord& record, qint64 id, bool report)
{
    const auto targets = m_udp.forwardTargets();
    if (targets.isEmpty())
        return 0;

    bool frequencyOk = false;
    const double mhz = record.value(QStringLiteral("FREQ")).toDouble(&frequencyOk);
    const QDateTime timestamp = QDateTime::fromString(
        LogDatabase::isoFromAdif(record.value(QStringLiteral("QSO_DATE")), record.value(QStringLiteral("TIME_ON"))),
        Qt::ISODate);
    const QString call = record.value(QStringLiteral("CALL")).trimmed().toUpper();
    const QString myCall = record.value(QStringLiteral("STATION_CALLSIGN")).trimmed().toUpper();
    if (call.isEmpty() || !timestamp.isValid())
        return 0;
    if (myCall.isEmpty()) {
        if (report)
            addActivity(QStringLiteral("UDP"), tr("N1MM QSO not sent: station callsign is missing"), QStringLiteral("warning"));
        return 0;
    }

    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    QXmlStreamWriter xml(&buffer);
    xml.setAutoFormatting(false);
    xml.writeStartDocument(QStringLiteral("1.0"), true);
    xml.writeStartElement(QStringLiteral("contactinfo"));
    const auto field = [&xml](const QString& name, const QString& value) { xml.writeTextElement(name, value); };
    const QString mode = n1mmMode(record.value(QStringLiteral("SUBMODE")).isEmpty()
                                       ? record.value(QStringLiteral("MODE"))
                                       : record.value(QStringLiteral("SUBMODE")));
    const QString stamp = timestamp.toUTC().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    const qint64 frequency10Hz = frequencyOk ? qRound64(mhz * 100000.0) : 0;
    field(QStringLiteral("app"), QStringLiteral("DecoDXLog"));
    field(QStringLiteral("contestname"), QString());
    field(QStringLiteral("contestnr"), QStringLiteral("0"));
    field(QStringLiteral("timestamp"), stamp);
    field(QStringLiteral("mycall"), myCall);
    field(QStringLiteral("band"), n1mmBand(record.value(QStringLiteral("BAND")), frequencyOk ? mhz : 0.0));
    field(QStringLiteral("rxfreq"), QString::number(frequency10Hz));
    field(QStringLiteral("txfreq"), QString::number(frequency10Hz));
    field(QStringLiteral("operator"), myCall);
    field(QStringLiteral("mode"), mode);
    field(QStringLiteral("call"), call);
    field(QStringLiteral("snt"), record.value(QStringLiteral("RST_SENT")));
    field(QStringLiteral("rcv"), record.value(QStringLiteral("RST_RCVD")));
    field(QStringLiteral("gridsquare"), record.value(QStringLiteral("GRIDSQUARE")).toUpper());
    field(QStringLiteral("comment"), record.value(QStringLiteral("COMMENT")));
    field(QStringLiteral("qth"), record.value(QStringLiteral("QTH")));
    field(QStringLiteral("name"), record.value(QStringLiteral("NAME")));
    field(QStringLiteral("radionr"), QStringLiteral("1"));
    field(QStringLiteral("IsOriginal"), QStringLiteral("True"));
    field(QStringLiteral("ID"), QString::number(id));
    field(QStringLiteral("oldtimestamp"), stamp);
    field(QStringLiteral("oldcall"), call);
    xml.writeEndElement();
    xml.writeEndDocument();

    QUdpSocket socket;
    int sent = 0;
    for (const auto& target : targets) {
        if (socket.writeDatagram(data, target.address, target.port) == data.size())
            ++sent;
    }
    if (report) {
        addActivity(QStringLiteral("UDP"),
                    sent > 0 ? tr("N1MM QSO sent to %n destination(s)", nullptr, sent)
                             : tr("N1MM QSO was not sent"),
                    sent > 0 ? QStringLiteral("success") : QStringLiteral("warning"));
    }
    return sent;
}

int DecoLogController::resendQsosViaUdp(const QVariantList& ids)
{
    if (m_udp.forwardTargets().isEmpty()) {
        addActivity(QStringLiteral("UDP"), tr("QSO not sent: configure a UDP forwarding destination first"),
                    QStringLiteral("warning"));
        return 0;
    }

    QList<qint64> uniqueIds;
    for (const QVariant& value : ids) {
        const qint64 id = value.toLongLong();
        if (id > 0 && !uniqueIds.contains(id))
            uniqueIds.append(id);
    }

    int resent = 0;
    int datagrams = 0;
    for (const qint64 id : uniqueIds) {
        const auto record = m_db.record(id);
        if (!record)
            continue;
        const int sent = broadcastN1mmQso(*record, id, false);
        if (sent > 0) {
            ++resent;
            datagrams += sent;
        }
    }

    addActivity(QStringLiteral("UDP"),
                resent > 0 ? tr("Resent %n QSO via UDP (%1 datagram(s))", nullptr, resent).arg(datagrams)
                           : tr("No selected QSO was sent via UDP"),
                resent > 0 ? QStringLiteral("success") : QStringLiteral("warning"));
    return resent;
}

bool DecoLogController::deleteQso(qint64 id)
{
    return deleteQsos({QVariant::fromValue(id)}) == 1;
}

// Cancellare piu' QSO in un colpo solo: una riga di diario, un ricarico.
int DecoLogController::deleteQsos(const QVariantList& ids)
{
    int done = 0;
    QString lastCall;
    for (const auto& value : ids) {
        const qint64 id = value.toLongLong();
        const auto record = m_db.record(id);
        if (!m_db.softDeleteQso(id))
            continue;
        ++done;
        // Anche la cancellazione, dove si puo' (CRX), parte da sola.
        m_qsl->qsoLogged(id);
        if (record)
            lastCall = record->value(QStringLiteral("CALL"));
    }
    if (done == 0)
        return 0;
    addActivity(QStringLiteral("LOG"),
                done == 1 ? tr("Deleted %1 (kept in history)").arg(lastCall)
                          : tr("Deleted %1 QSO (kept in history)").arg(done),
                QStringLiteral("warning"));
    timed(tr("reloading the log table"), [this] { m_model->reload(); });
    emit logChanged();
    m_decoLink.resendSnapshot();
    refreshCallInfo();
    return done;
}

QString DecoLogController::restoreRevision(qint64 id, qint64 historyId)
{
    const InsertResult res = m_db.restoreRevision(id, historyId);
    if (res.status != InsertResult::Status::Inserted)
        return res.message;
    m_db.queueRemoteEdit(id);
    m_qsl->qsoLogged(id);
    addActivity(QStringLiteral("LOG"), tr("Restored an earlier revision of QSO #%1").arg(id), QStringLiteral("success"));
    timed(tr("reloading the log table"), [this] { m_model->reload(); });
    emit logChanged();
    m_decoLink.resendSnapshot();
    refreshCallInfo();
    return {};
}

// ── Etichette ─────────────────────────────────────────────────────────────────

int DecoLogController::tagQsos(const QVariantList& ids, const QString& tag, bool add)
{
    QList<qint64> list;
    for (const auto& v : ids)
        list << v.toLongLong();
    const QString clean = tag.simplified().remove(QLatin1Char(','));
    if (list.isEmpty() || clean.isEmpty())
        return 0;
    const int changed = m_db.setTag(list, clean, add);
    addActivity(QStringLiteral("LOG"),
                add ? tr("Tag \"%1\" added to %2 QSO (%3 already had it)").arg(clean).arg(changed).arg(list.size() - changed)
                    : tr("Tag \"%1\" removed from %2 QSO").arg(clean).arg(changed),
                changed > 0 ? QStringLiteral("success") : QStringLiteral("info"));
    if (changed > 0) {
        timed(tr("reloading the log table"), [this] { m_model->reload(); });
        emit logChanged();
    }
    return changed;
}

// ── Lavori sul log: modifica in blocco, doppioni ─────────────────────────────

QVariantList DecoLogController::bulkFields() const
{
    auto choice = [](const QString& value, const QString& label) {
        return QVariantMap{{QStringLiteral("value"), value}, {QStringLiteral("label"), label}};
    };
    const QVariantList qsl{choice(QStringLiteral("Y"), tr("Y · yes")), choice(QStringLiteral("N"), tr("N · no")),
                           choice(QStringLiteral("R"), tr("R · requested")), choice(QStringLiteral("Q"), tr("Q · queued")),
                           choice(QStringLiteral("I"), tr("I · ignore"))};
    const QVariantList upload{choice(QStringLiteral("Y"), tr("Y · uploaded")), choice(QStringLiteral("N"), tr("N · not uploaded")),
                              choice(QStringLiteral("M"), tr("M · changed, upload again"))};
    const QVariantList via{choice(QStringLiteral("B"), tr("B · bureau")), choice(QStringLiteral("D"), tr("D · direct")),
                           choice(QStringLiteral("E"), tr("E · electronic"))};
    QVariantList propagation;
    for (const char* p : {"SAT", "EME", "ES", "F2", "TEP", "MS", "TR", "AUR", "RPT", "INTERNET"})
        propagation << choice(QLatin1String(p), QLatin1String(p));
    QVariantList profiles{choice(QStringLiteral("0"), tr("No profile"))};
    for (int i = 0; m_profiles && i < m_profiles->count(); ++i) {
        const QVariantMap p = m_profiles->get(i);
        profiles << choice(p.value(QStringLiteral("id")).toString(), p.value(QStringLiteral("name")).toString());
    }
    auto field = [](const QString& adif, const QString& label, const QVariantList& choices = {}) {
        return QVariantMap{{QStringLiteral("field"), adif}, {QStringLiteral("label"), label},
                           {QStringLiteral("choices"), choices}};
    };
    return {
        // Il locatore di casa per primo: e' quello che manca di piu' dopo un import.
        field(QStringLiteral("MY_GRIDSQUARE"), tr("My locator")),
        field(QStringLiteral("STATION_CALLSIGN"), tr("Station callsign")),
        field(QStringLiteral("OPERATOR"), tr("Operator")),
        field(QStringLiteral("@profile"), tr("Station profile"), profiles),
        field(QStringLiteral("MY_RIG"), tr("My rig")),
        field(QStringLiteral("MY_ANTENNA"), tr("My antenna")),
        field(QStringLiteral("TX_PWR"), tr("Power (W)")),
        field(QStringLiteral("MY_POTA_REF"), tr("My POTA reference")),
        field(QStringLiteral("MY_SOTA_REF"), tr("My SOTA reference")),
        field(QStringLiteral("MY_WWFF_REF"), tr("My WWFF reference")),
        field(QStringLiteral("MY_SIG"), tr("My special activity (MY_SIG)")),
        field(QStringLiteral("MY_SIG_INFO"), tr("My special activity reference (MY_SIG_INFO)")),
        field(QStringLiteral("POTA_REF"), tr("POTA reference")),
        field(QStringLiteral("SOTA_REF"), tr("SOTA reference")),
        field(QStringLiteral("WWFF_REF"), tr("WWFF reference")),
        field(QStringLiteral("IOTA"), tr("IOTA")),
        field(QStringLiteral("SIG"), tr("Special activity (SIG)")),
        field(QStringLiteral("SIG_INFO"), tr("Special activity reference (SIG_INFO)")),
        field(QStringLiteral("CONTEST_ID"), tr("Contest")),
        field(QStringLiteral("PROP_MODE"), tr("Propagation"), propagation),
        field(QStringLiteral("SAT_NAME"), tr("Satellite")),
        field(QStringLiteral("MODE"), tr("Mode")),
        field(QStringLiteral("SUBMODE"), tr("Submode")),
        field(QStringLiteral("RST_SENT"), tr("RST sent")),
        field(QStringLiteral("RST_RCVD"), tr("RST received")),
        field(QStringLiteral("COMMENT"), tr("Comment")),
        field(QStringLiteral("NOTES"), tr("Notes")),
        field(QStringLiteral("QSL_SENT"), tr("Paper QSL sent"), qsl),
        field(QStringLiteral("QSL_RCVD"), tr("Paper QSL received"), qsl),
        field(QStringLiteral("QSL_SENT_VIA"), tr("Paper QSL via"), via),
        field(QStringLiteral("LOTW_QSL_SENT"), tr("LoTW: sent"), qsl),
        field(QStringLiteral("EQSL_QSL_SENT"), tr("eQSL: sent"), qsl),
        field(QStringLiteral("QRZCOM_QSO_UPLOAD_STATUS"), tr("QRZ.com: uploaded"), upload),
        field(QStringLiteral("CLUBLOG_QSO_UPLOAD_STATUS"), tr("Club Log: uploaded"), upload),
        field(QStringLiteral("HRDLOG_QSO_UPLOAD_STATUS"), tr("HRDLog: uploaded"), upload),
    };
}

void DecoLogController::bulkEdit(const QVariantList& ids, const QString& field, const QString& value, bool onlyEmpty)
{
    QList<qint64> list;
    for (const QVariant& v : ids) {
        if (v.toLongLong() > 0)
            list << v.toLongLong();
    }
    if (list.isEmpty() || field.trimmed().isEmpty())
        return;
    if (m_bulkProgress >= 0) {
        addActivity(QStringLiteral("LOG"), tr("A change on many QSO is already running: wait for it to finish."),
                    QStringLiteral("warning"));
        return;
    }
    const QString dbPath = m_db.path();
    // Un log di prova in memoria non si apre da un altro filo.
    if (dbPath.isEmpty() || dbPath == QLatin1String(":memory:")) {
        finishBulk(field, value, m_db.bulkEdit(list, field, value, onlyEmpty));
        return;
    }
    m_bulkProgress = 0;
    emit bulkChanged();
    QPointer<DecoLogController> self(this);
    m_importPool.start([self, dbPath, list, field, value, onlyEmpty] {
        LogDatabase db;
        BulkEditResult result;
        if (!db.open(dbPath)) {
            result.failed = int(list.size());
            result.errors << db.lastError();
        } else {
            result = db.bulkEdit(list, field, value, onlyEmpty, [self](int done, int total) {
                const double progress = total > 0 ? double(done) / total : 1.0;
                QMetaObject::invokeMethod(
                    self.data(), [self, progress] {
                        if (!self)
                            return;
                        self->m_bulkProgress = progress;
                        emit self->bulkChanged();
                    },
                    Qt::QueuedConnection);
            });
        }
        QMetaObject::invokeMethod(
            self.data(), [self, field, value, result] {
                if (self)
                    self->finishBulk(field, value, result);
            },
            Qt::QueuedConnection);
    });
}

void DecoLogController::finishBulk(const QString& field, const QString& value, const BulkEditResult& r)
{
    m_bulkProgress = -1;
    emit bulkChanged();
    QString label = field;
    QString shown = value.trimmed().isEmpty() ? tr("(empty)") : value.trimmed();
    for (const QVariant& f : bulkFields()) {
        const QVariantMap m = f.toMap();
        if (m.value(QStringLiteral("field")).toString() != field)
            continue;
        label = m.value(QStringLiteral("label")).toString();
        for (const QVariant& c : m.value(QStringLiteral("choices")).toList()) {
            if (c.toMap().value(QStringLiteral("value")).toString() == value.trimmed())
                shown = c.toMap().value(QStringLiteral("label")).toString();
        }
    }
    addActivity(QStringLiteral("LOG"),
                tr("%1 → %2 on %3 QSO (%4 unchanged, %5 failed)").arg(label, shown).arg(r.changed).arg(r.unchanged).arg(r.failed),
                r.failed > 0 ? QStringLiteral("warning") : r.changed > 0 ? QStringLiteral("success") : QStringLiteral("info"));
    for (const QString& e : r.errors)
        addActivity(QStringLiteral("LOG"), e, QStringLiteral("error"));
    if (r.changed == 0)
        return;
    // Dove il QSO si puo' correggere (CRX), la correzione va anche li'.
    for (const qint64 id : r.changedIds) {
        m_db.queueRemoteEdit(id);
        m_qsl->qsoLogged(id);
    }
    timed(tr("reloading the log table"), [this] { m_model->reload(); });
    m_awardsDirty = m_globalAwardsDirty = true;
    emit awardsChanged();
    emit logChanged();
    m_decoLink.resendSnapshot();
    refreshCallInfo();
}

namespace {

// I gruppi di doppioni come li mostra la finestra, con quello che conviene
// tenere: il piu' confermato, poi quello con piu' campi, poi il primo.
QVariantList describeDuplicates(const LogDatabase& db, int windowSeconds, bool* limited)
{
    constexpr int kLimit = 2000;
    const QList<QList<qint64>> groups = db.duplicateGroups(windowSeconds, kLimit);
    if (limited)
        *limited = groups.size() >= kLimit;
    QVariantList out;
    for (const QList<qint64>& group : groups) {
        QVariantList rows;
        qint64 keep = 0;
        int best = -1;
        for (const qint64 id : group) {
            const auto r = db.record(id);
            const auto m = db.meta(id);
            if (!r || !m)
                continue;
            QStringList confirmed;
            if (r->value(QStringLiteral("LOTW_QSL_RCVD")) == QLatin1String("Y"))
                confirmed << QStringLiteral("LoTW");
            if (r->value(QStringLiteral("EQSL_QSL_RCVD")) == QLatin1String("Y"))
                confirmed << QStringLiteral("eQSL");
            if (r->value(QStringLiteral("QRZCOM_QSO_DOWNLOAD_STATUS")) == QLatin1String("Y"))
                confirmed << QStringLiteral("QRZ");
            if (r->value(QStringLiteral("QSL_RCVD")) == QLatin1String("Y"))
                confirmed << QStringLiteral("QSL");
            const int fields = int(r->fields().size());
            const int score = int(confirmed.size()) * 1000 + fields;
            if (score > best) {
                best = score;
                keep = id;
            }
            const QString date = r->value(QStringLiteral("QSO_DATE"));
            const QString time = r->value(QStringLiteral("TIME_ON")).leftJustified(6, QLatin1Char('0'));
            const QString submode = r->value(QStringLiteral("SUBMODE"));
            rows << QVariantMap{
                {QStringLiteral("id"), id},
                {QStringLiteral("call"), r->value(QStringLiteral("CALL"))},
                {QStringLiteral("when"), QStringLiteral("%1-%2-%3 %4:%5:%6")
                                             .arg(date.mid(0, 4), date.mid(4, 2), date.mid(6, 2), time.mid(0, 2),
                                                  time.mid(2, 2), time.mid(4, 2))},
                {QStringLiteral("band"), r->value(QStringLiteral("BAND"))},
                {QStringLiteral("mode"), submode.isEmpty() ? r->value(QStringLiteral("MODE")) : submode},
                {QStringLiteral("freq"), r->value(QStringLiteral("FREQ"))},
                {QStringLiteral("source"), m->sourceApp.isEmpty() ? m->source : m->sourceApp},
                {QStringLiteral("confirmed"), confirmed.join(QLatin1Char(' '))},
                {QStringLiteral("fields"), fields},
            };
        }
        if (rows.size() > 1)
            out << QVariantMap{{QStringLiteral("keep"), keep}, {QStringLiteral("rows"), rows}};
    }
    return out;
}

} // namespace

void DecoLogController::findDuplicates(int windowMinutes)
{
    if (m_duplicates.value(QStringLiteral("busy")).toBool())
        return;
    const int minutes = std::clamp(windowMinutes, 1, 24 * 60);
    m_duplicates = {{QStringLiteral("busy"), true}, {QStringLiteral("windowMinutes"), minutes},
                    {QStringLiteral("groups"), QVariantList{}}};
    emit duplicatesChanged();
    auto done = [this, minutes](const QVariantList& groups, bool limited) {
        m_duplicates = {{QStringLiteral("busy"), false}, {QStringLiteral("windowMinutes"), minutes},
                        {QStringLiteral("searched"), true}, {QStringLiteral("limited"), limited},
                        {QStringLiteral("groups"), groups}};
        emit duplicatesChanged();
    };
    const QString dbPath = m_db.path();
    if (dbPath.isEmpty() || dbPath == QLatin1String(":memory:")) {
        bool limited = false;
        const QVariantList groups = describeDuplicates(m_db, minutes * 60, &limited);
        done(groups, limited);
        return;
    }
    // Su un milione di QSO si legge tutto il log: su un altro filo.
    QPointer<DecoLogController> self(this);
    m_importPool.start([self, dbPath, minutes, done] {
        LogDatabase db;
        QVariantList groups;
        bool limited = false;
        if (db.open(dbPath))
            groups = describeDuplicates(db, minutes * 60, &limited);
        QMetaObject::invokeMethod(
            self.data(), [self, groups, limited, done] {
                if (self)
                    done(groups, limited);
            },
            Qt::QueuedConnection);
    });
}

int DecoLogController::mergeDuplicates(const QVariantList& groups)
{
    int merged = 0;
    int removed = 0;
    QStringList errors;
    QList<qint64> kept;
    QList<qint64> gone;
    QSqlDatabase db = m_db.connection();
    const bool transaction = db.transaction();
    for (const QVariant& g : groups) {
        const QVariantMap m = g.toMap();
        const qint64 keep = m.value(QStringLiteral("keep")).toLongLong();
        QList<qint64> others;
        for (const QVariant& v : m.value(QStringLiteral("ids")).toList()) {
            if (v.toLongLong() != keep && v.toLongLong() > 0)
                others << v.toLongLong();
        }
        if (keep <= 0 || others.isEmpty())
            continue;
        const InsertResult r = m_db.mergeQsos(keep, others);
        if (r.status != InsertResult::Status::Inserted) {
            if (errors.size() < 5)
                errors << tr("QSO #%1: %2").arg(keep).arg(r.message);
            continue;
        }
        ++merged;
        removed += int(others.size());
        kept << keep;
        gone << others;
    }
    if (transaction)
        db.commit();
    for (const QString& e : errors)
        addActivity(QStringLiteral("LOG"), e, QStringLiteral("error"));
    if (merged == 0)
        return 0;
    for (const qint64 id : std::as_const(kept)) {
        m_db.queueRemoteEdit(id);
        m_qsl->qsoLogged(id);
    }
    // Anche la cancellazione, dove si puo' (CRX), parte da sola.
    for (const qint64 id : std::as_const(gone))
        m_qsl->qsoLogged(id);
    // Dalla lista restano i gruppi non uniti.
    QVariantList left;
    for (const QVariant& g : m_duplicates.value(QStringLiteral("groups")).toList()) {
        if (!kept.contains(g.toMap().value(QStringLiteral("keep")).toLongLong()))
            left << g;
    }
    m_duplicates[QStringLiteral("groups")] = left;
    emit duplicatesChanged();
    addActivity(QStringLiteral("LOG"),
                tr("Merged %1 group(s) of duplicates: %2 QSO deleted (kept in history)").arg(merged).arg(removed),
                QStringLiteral("success"));
    timed(tr("reloading the log table"), [this] { m_model->reload(); });
    m_awardsDirty = m_globalAwardsDirty = true;
    emit awardsChanged();
    emit logChanged();
    m_decoLink.resendSnapshot();
    refreshCallInfo();
    return merged;
}

void DecoLogController::clearDuplicates()
{
    if (m_duplicates.value(QStringLiteral("busy")).toBool())
        return;
    m_duplicates.clear();
    emit duplicatesChanged();
}

QVariantList DecoLogController::dxccInLog() const
{
    decolog::StartupSpan trace("DecoLogController::dxccInLog");
    QVariantList out;
    for (const auto& row : m_db.countByDxcc()) {
        const int dxcc = row.key.toInt();
        out << QVariantMap{{QStringLiteral("dxcc"), dxcc},
                           {QStringLiteral("name"), m_countries.nameFor(dxcc)},
                           {QStringLiteral("count"), row.count}};
    }
    return out;
}

// ── Import, export, backup ────────────────────────────────────────────────────

void DecoLogController::importAdif(const QUrl& url)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (m_importProgress >= 0) {
        addActivity(QStringLiteral("IMPORT"), tr("An import is already running: wait for it to finish."),
                    QStringLiteral("warning"));
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        addActivity(QStringLiteral("IMPORT"), tr("Cannot read %1: %2").arg(path, file.errorString()), QStringLiteral("error"));
        return;
    }
    file.close();
    const qint64 profile = m_profiles ? m_profiles->activeProfileId() : 0;
    const qint64 startedAt = QDateTime::currentMSecsSinceEpoch();
    const QString dbPath = m_db.path();
    // Un log di prova in memoria non si apre da un altro filo: li' si importa qui.
    if (dbPath.isEmpty() || dbPath == QLatin1String(":memory:")) {
        QString error;
        const QByteArray adif = migration::asAdif(path, &error);
        ImportResult result = adif.isEmpty() ? ImportResult{} : m_db.importAdif(adif, QStringLiteral("import"), profile);
        if (adif.isEmpty()) {
            result.invalid = 1;
            result.errors << error;
        }
        finishImport(path, result);
        recordUiLatency(QStringLiteral("import.adif"), QDateTime::currentMSecsSinceEpoch() - startedAt);
        return;
    }

    // Su un altro filo, con una connessione sua, a blocchi di mille QSO: il
    // programma resta vivo, e i QSO che arrivano dalla radio passano fra un
    // blocco e l'altro.
    m_importProgress = 0;
    emit importChanged();
    addActivity(QStringLiteral("IMPORT"), tr("Importing %1…").arg(QFileInfo(path).fileName()));
    const int digital = m_db.dedupWindowSeconds(false);
    const int manual = m_db.dedupWindowSeconds(true);
    QPointer<DecoLogController> self(this);
    m_importPool.start([self, path, dbPath, profile, digital, manual, startedAt] {
        ImportResult result;
        // ADIF com'e'; un CSV o un database di N1MM convertiti prima.
        QString readError;
        const QByteArray adif = migration::asAdif(path, &readError);
        LogDatabase db;
        if (adif.isEmpty() || !db.open(dbPath)) {
            result.invalid = 1;
            result.errors << (adif.isEmpty() ? readError : db.lastError());
        } else {
            db.setDedupWindows(digital, manual);
            int lastPercent = -1;
            result = db.importAdif(adif, QStringLiteral("import"), profile,
                                   [self, &lastPercent](int done, int total) {
                                       const int percent = total > 0 ? done * 100 / total : 100;
                                       if (percent == lastPercent)
                                           return;
                                       lastPercent = percent;
                                       QMetaObject::invokeMethod(
                                           self.data(), [self, percent] {
                                               if (!self)
                                                   return;
                                               self->m_importProgress = percent / 100.0;
                                               emit self->importChanged();
                                           },
                                           Qt::QueuedConnection);
                                   },
                                   1000);
        }
        QMetaObject::invokeMethod(
            self.data(), [self, path, result, startedAt] {
                if (self) {
                    self->recordUiLatency(QStringLiteral("import.adif"),
                                          QDateTime::currentMSecsSinceEpoch() - startedAt);
                    self->finishImport(path, result);
                }
            },
            Qt::QueuedConnection);
    });
}

QVariantMap DecoLogController::previewImport(const QUrl& url) const
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    QFile file(path);
    QVariantMap out{{QStringLiteral("path"), path}, {QStringLiteral("ok"), false}};
    if (!file.open(QIODevice::ReadOnly)) {
        out.insert(QStringLiteral("error"), file.errorString());
        return out;
    }
    QString error;
    QString kind;
    const QByteArray adif = migration::asAdif(path, &error, &kind);
    if (adif.isEmpty()) {
        out.insert(QStringLiteral("error"), error);
        return out;
    }
    const AdifDocument doc = adif::parse(adif);
    int missingUtc = 0;
    int qsl = 0;
    QSet<QString> extra;
    const QSet<QString> known{QStringLiteral("CALL"), QStringLiteral("QSO_DATE"), QStringLiteral("TIME_ON"),
                              QStringLiteral("BAND"), QStringLiteral("MODE"), QStringLiteral("SUBMODE"),
                              QStringLiteral("LOTW_QSL_RCVD"), QStringLiteral("EQSL_QSL_RCVD"),
                              QStringLiteral("QSL_RCVD"), QStringLiteral("QSL_VIA"), QStringLiteral("DXCC"),
                              QStringLiteral("CQZ"), QStringLiteral("STATE"), QStringLiteral("GRIDSQUARE"),
                              QStringLiteral("RST_SENT"), QStringLiteral("RST_RCVD"), QStringLiteral("NAME"),
                              QStringLiteral("QTH"), QStringLiteral("COMMENT"), QStringLiteral("NOTES")};
    for (const AdifRecord& record : doc.records) {
        if (!record.contains(QStringLiteral("QSO_DATE")) || !record.contains(QStringLiteral("TIME_ON")))
            ++missingUtc;
        if (record.contains(QStringLiteral("LOTW_QSL_RCVD")) || record.contains(QStringLiteral("EQSL_QSL_RCVD"))
            || record.contains(QStringLiteral("QSL_RCVD")))
            ++qsl;
        for (const AdifField& field : record.fields())
            if (!known.contains(field.name) && !field.name.startsWith(QStringLiteral("APP_")))
                extra.insert(field.name);
    }
    out.insert(QStringLiteral("ok"), true);
    out.insert(QStringLiteral("kind"), kind);
    out.insert(QStringLiteral("read"), doc.records.size());
    out.insert(QStringLiteral("missingUtc"), missingUtc);
    out.insert(QStringLiteral("qslRecords"), qsl);
    out.insert(QStringLiteral("extraFields"), QStringList(extra.values()));

    // Dry-run against a private in-memory copy of the current log.  This gives
    // the operator a before/after comparison without writing the real log.
    LogDatabase probe;
    if (probe.open(QStringLiteral(":memory:"))) {
        probe.setDedupWindows(m_db.dedupWindowSeconds(false), m_db.dedupWindowSeconds(true));
        const ImportResult existing = probe.importAdif(m_db.exportAdif(), QStringLiteral("preview"));
        Q_UNUSED(existing);
        const ImportResult estimate = probe.importAdif(adif, QStringLiteral("preview"), 0);
        out.insert(QStringLiteral("estimatedInserted"), estimate.inserted);
        out.insert(QStringLiteral("estimatedDuplicates"), estimate.duplicates);
        out.insert(QStringLiteral("estimatedInvalid"), estimate.invalid);
    }
    return out;
}

void DecoLogController::finishImport(const QString& path, const ImportResult& r)
{
    m_lastImportIds = r.insertedIds;
    m_importProgress = -1;
    emit importChanged();
    addActivity(QStringLiteral("IMPORT"), tr("%1: %2 read, %3 new, %4 duplicates, %5 rejected, %6 with QSL status")
                                              .arg(QFileInfo(path).fileName()).arg(r.read).arg(r.inserted)
                                              .arg(r.duplicates).arg(r.invalid).arg(r.qslRecords),
                r.invalid ? QStringLiteral("warning") : QStringLiteral("success"));
    if (r.missingUtc > 0)
        addActivity(QStringLiteral("IMPORT"), tr("%1 record(s) had no complete UTC date/time")
                                                .arg(r.missingUtc), QStringLiteral("warning"));
    if (!r.preservedExtraFields.isEmpty())
        addActivity(QStringLiteral("IMPORT"), tr("Preserved unmapped ADIF fields: %1")
                                                .arg(r.preservedExtraFields.join(QStringLiteral(", "))),
                    QStringLiteral("info"));
    for (const QString& e : r.errors)
        addActivity(QStringLiteral("IMPORT"), QStringLiteral("  ") + e, QStringLiteral("warning"));
    timed(tr("reloading the log table"), [this] { m_model->reload(); });
    m_profiles->reload();
    emit logChanged();
    m_decoLink.resendSnapshot();
    refreshCallInfo();
}

bool DecoLogController::undoLastImport()
{
    if (m_importProgress >= 0 || m_lastImportIds.isEmpty())
        return false;
    int deleted = 0;
    for (const qint64 id : std::as_const(m_lastImportIds))
        if (m_db.softDeleteQso(id))
            ++deleted;
    if (deleted == 0)
        return false;
    addActivity(QStringLiteral("IMPORT"), tr("Undid the last import: %1 QSO(s) moved to the recycle history.")
                                              .arg(deleted), QStringLiteral("warning"));
    m_lastImportIds.clear();
    m_model->reload();
    m_profiles->reload();
    emit logChanged();
    m_decoLink.resendSnapshot();
    refreshCallInfo();
    return true;
}

QVariantList DecoLogController::qsoEnrichments(qint64 qsoId) const
{
    QVariantList out;
    for (const EnrichmentEntry& e : m_db.enrichments(qsoId))
        out << QVariantMap{{QStringLiteral("field"), e.field},
                           {QStringLiteral("previousValue"), e.previousValue},
                           {QStringLiteral("value"), e.value},
                           {QStringLiteral("source"), e.source},
                           {QStringLiteral("kind"), e.kind},
                           {QStringLiteral("acquiredAt"), e.acquiredAt},
                           {QStringLiteral("validFrom"), e.validFrom},
                           {QStringLiteral("validTo"), e.validTo}};
    return out;
}

bool DecoLogController::undoQsoEnrichment(qint64 qsoId, const QString& field)
{
    if (!m_db.undoEnrichment(qsoId, field))
        return false;
    m_model->refreshQso(qsoId);
    emit logChanged();
    return true;
}

void DecoLogController::exportAdif(const QUrl& url)
{
    exportInBackground({}, true, url.isLocalFile() ? url.toLocalFile() : url.toString());
}

void DecoLogController::exportInBackground(const QList<qint64>& ids, bool all, const QString& path,
                                           const std::optional<std::pair<QString, QString>>& period)
{
    {
        QFile probe(path);
        if (!probe.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            addActivity(QStringLiteral("EXPORT"), tr("Cannot write %1: %2").arg(path, probe.errorString()),
                        QStringLiteral("error"));
            return;
        }
    }
    const QString dbPath = m_db.path();
    const QString programVersion = version();
    // Un log di prova in memoria non si apre da un altro filo: li' si scrive qui.
    if (dbPath.isEmpty() || dbPath == QLatin1String(":memory:")) {
        QFile file(path);
        const QList<qint64> chosen = period ? m_db.qsoIdsBetween(period->first, period->second) : ids;
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            addActivity(QStringLiteral("EXPORT"), tr("Cannot write %1: %2").arg(path, file.errorString()),
                        QStringLiteral("error"));
            return;
        }
        const QByteArray data = all ? m_db.exportAdif(programVersion) : m_db.exportAdif(chosen, programVersion);
        if (file.write(data) != data.size()) {
            addActivity(QStringLiteral("EXPORT"), tr("Cannot write %1: %2").arg(path, file.errorString()),
                        QStringLiteral("error"));
            return;
        }
        addActivity(QStringLiteral("EXPORT"),
                    tr("%n QSO → %1", nullptr, all ? m_db.qsoCount() : static_cast<int>(chosen.size())).arg(path),
                    QStringLiteral("success"));
        return;
    }
    addActivity(QStringLiteral("EXPORT"), tr("Exporting to %1…").arg(QFileInfo(path).fileName()));
    QPointer<DecoLogController> self(this);
    m_importPool.start([self, dbPath, programVersion, ids, all, path, period] {
        QString error;
        int count = 0;
        {
            LogDatabase db;
            QFile file(path);
            if (!db.open(dbPath)) {
                error = db.lastError();
            } else if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                error = file.errorString();
            } else {
                const QList<qint64> chosen = period ? db.qsoIdsBetween(period->first, period->second) : ids;
                count = all ? db.qsoCount() : static_cast<int>(chosen.size());
                const QByteArray data = all ? db.exportAdif(programVersion) : db.exportAdif(chosen, programVersion);
                if (file.write(data) != data.size())
                    error = file.errorString();
            }
        }
        QMetaObject::invokeMethod(
            self.data(), [self, path, error, count] {
                if (!self)
                    return;
                if (error.isEmpty())
                    self->addActivity(QStringLiteral("EXPORT"), tr("%n QSO → %1", nullptr, count).arg(path),
                                      QStringLiteral("success"));
                else
                    self->addActivity(QStringLiteral("EXPORT"), tr("Cannot write %1: %2").arg(path, error),
                                      QStringLiteral("error"));
            },
            Qt::QueuedConnection);
    });
}

void DecoLogController::exportQsos(const QVariantList& ids, const QUrl& url)
{
    QList<qint64> list;
    for (const auto& v : ids)
        list << v.toLongLong();
    exportInBackground(list, false, url.isLocalFile() ? url.toLocalFile() : url.toString());
}

int DecoLogController::countQsoBetween(const QString& fromIso, const QString& toIso) const
{
    return m_db.isOpen() ? m_db.qsoCountBetween(fromIso.trimmed(), toIso.trimmed()) : 0;
}

void DecoLogController::exportPeriod(const QString& fromIso, const QString& toIso, const QUrl& url)
{
    const QDate from = QDate::fromString(fromIso.trimmed(), Qt::ISODate);
    const QDate to = QDate::fromString(toIso.trimmed(), Qt::ISODate);
    if (!from.isValid() && !to.isValid()) {
        addActivity(QStringLiteral("EXPORT"), tr("Choose the first or the last day of the period"),
                    QStringLiteral("warning"));
        return;
    }
    if (from.isValid() && to.isValid() && from > to) {
        addActivity(QStringLiteral("EXPORT"), tr("The period starts after it ends"), QStringLiteral("warning"));
        return;
    }
    // Solo date vere: quello che non lo e' resta aperto, non un confronto fra stringhe.
    exportInBackground({}, false, url.isLocalFile() ? url.toLocalFile() : url.toString(),
                       std::make_pair(from.isValid() ? from.toString(Qt::ISODate) : QString(),
                                      to.isValid() ? to.toString(Qt::ISODate) : QString()));
}

void DecoLogController::setBackupEnabled(bool enabled)
{
    if (enabled == m_backupEnabled)
        return;
    m_backupEnabled = enabled;
    QSettings().setValue(QStringLiteral("backup/enabled"), enabled);
    emit backupChanged();
}

void DecoLogController::setBackupDir(const QString& dir)
{
    if (dir == m_backupDir || dir.trimmed().isEmpty())
        return;
    m_backupDir = dir.trimmed();
    QSettings().setValue(QStringLiteral("backup/dir"), m_backupDir);
    emit backupChanged();
}

void DecoLogController::setBackupTime(const QString& hhmm)
{
    if (hhmm == m_backupTime || !QTime::fromString(hhmm, QStringLiteral("HH:mm")).isValid())
        return;
    m_backupTime = hhmm;
    QSettings().setValue(QStringLiteral("backup/time"), hhmm);
    emit backupChanged();
}

void DecoLogController::setBackupKeep(int keep)
{
    if (keep == m_backupKeep || keep < 1)
        return;
    m_backupKeep = keep;
    QSettings().setValue(QStringLiteral("backup/keep"), keep);
    emit backupChanged();
}

QString DecoLogController::lastBackup() const
{
    const QDateTime at = QDateTime::fromString(m_db.setting(QStringLiteral("backup.last_at")), Qt::ISODate);
    if (!at.isValid())
        return {};
    const QDateTime utc = at.toUTC();
    return utc.date() == QDateTime::currentDateTimeUtc().date()
               ? utc.toString(QStringLiteral("HH:mm")) + QStringLiteral("Z")
               : utc.toString(QStringLiteral("MM-dd HH:mm")) + QStringLiteral("Z");
}

QString DecoLogController::lastBackupInfo() const
{
    const QString file = m_db.setting(QStringLiteral("backup.last_file"));
    if (file.isEmpty())
        return {};
    const QFileInfo info(file);
    return tr("%1 · %2 MB").arg(info.fileName()).arg(QString::number(info.size() / 1048576.0, 'f', 1));
}

namespace {

QList<QPair<QString, QString>> stationBackupResources()
{
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QList<QPair<QString, QString>> resources;
    resources << qMakePair(QDir(appData).filePath(QStringLiteral("MASTER.SCP")),
                           QStringLiteral("contest/MASTER.SCP"));
    resources << qMakePair(QDir(appData).filePath(QStringLiteral("cty.csv")),
                           QStringLiteral("countries/cty.csv"));
    resources << qMakePair(QDir(appData).filePath(QStringLiteral("clublog-cty.xml")),
                           QStringLiteral("countries/clublog-cty.xml"));
    for (int i = 1; i <= 12; ++i)
        resources << qMakePair(QDir(appData).filePath(QStringLiteral("dvk/F%1.wav").arg(i)),
                               QStringLiteral("dvk/F%1.wav").arg(i));
    return resources;
}

QString newStationBackupPath(const QString& backupDir)
{
    const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-ddTHHmmsszzz"));
    QString stem = QStringLiteral("decolog-%1").arg(stamp);
    int suffix = 0;
    while (QFileInfo::exists(QDir(backupDir).filePath(stem + QStringLiteral(".sqlite")))
           || QFileInfo::exists(QDir(backupDir).filePath(stem + QStringLiteral(".sqlite.partial")))
           || QFileInfo::exists(QDir(backupDir).filePath(stem + QStringLiteral(".bundle")))
           || QFileInfo::exists(QDir(backupDir).filePath(stem + QStringLiteral(".bundle.partial")))) {
        stem = QStringLiteral("decolog-%1-%2").arg(stamp).arg(++suffix);
    }
    return QDir(backupDir).filePath(stem + QStringLiteral(".sqlite"));
}

} // namespace

void DecoLogController::backupNow()
{
    if (m_backupRunning)
        return;
    QDir().mkpath(m_backupDir);
    const QString path = newStationBackupPath(m_backupDir);
    const QString name = QFileInfo(path).fileName();
    const QString bundle = QDir(m_backupDir).filePath(QFileInfo(name).completeBaseName() + QStringLiteral(".bundle"));
    const auto resources = stationBackupResources();
    QSettings settings;
    const auto portableSettings = core::cloudsettings::collect(settings);
    const QString dbPath = m_db.path();
    const qint64 startedAt = QDateTime::currentMSecsSinceEpoch();
    // La copia si fa su un altro filo, con una sua connessione: VACUUM INTO di
    // un log da un milione sono secondi, e il programma non si deve fermare.
    if (dbPath.isEmpty() || dbPath == QLatin1String(":memory:")) {
        QString error;
        if (!m_db.backupTo(path))
            error = m_db.lastError();
        else {
            const auto made = logbackup::createBundle(path, bundle, resources, portableSettings);
            if (!made.ok)
                error = made.error;
        }
        finishBackup(path, error);
        recordUiLatency(QStringLiteral("backup.station"), QDateTime::currentMSecsSinceEpoch() - startedAt);
        return;
    }
    m_backupRunning = true;
    QPointer<DecoLogController> self(this);
    m_backupPool.start([self, dbPath, path, bundle, resources, portableSettings, startedAt] {
        QString error;
        {
            LogDatabase db;
            if (!db.open(dbPath) || !db.backupTo(path))
                error = db.lastError();
            else {
                const auto made = logbackup::createBundle(path, bundle, resources, portableSettings);
                if (!made.ok)
                    error = made.error;
            }
        }
        QMetaObject::invokeMethod(
            self.data(), [self, path, error, startedAt] {
                if (!self)
                    return;
                self->m_backupRunning = false;
                self->recordUiLatency(QStringLiteral("backup.station"),
                                      QDateTime::currentMSecsSinceEpoch() - startedAt);
                self->finishBackup(path, error);
            },
            Qt::QueuedConnection);
    });
}

void DecoLogController::finishBackup(const QString& path, const QString& error)
{
    if (!error.isEmpty()) {
        addActivity(QStringLiteral("BACKUP"), tr("Backup failed: %1").arg(error), QStringLiteral("error"));
        return;
    }
    m_db.setSetting(QStringLiteral("backup.last_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    m_db.setSetting(QStringLiteral("backup.last_file"), path);

    // Solo le copie fatte da DecoDXLog, dalla piu' recente: le altre non si toccano.
    QFileInfoList copies = QDir(m_backupDir).entryInfoList({QStringLiteral("decolog-*.sqlite")}, QDir::Files, QDir::Name | QDir::Reversed);
    for (qsizetype i = m_backupKeep; i < copies.size(); ++i) {
        QFile::remove(copies.at(i).absoluteFilePath());
        QDir(QDir(copies.at(i).absolutePath()).filePath(copies.at(i).completeBaseName()
                                                      + QStringLiteral(".bundle"))).removeRecursively();
    }

    addActivity(QStringLiteral("BACKUP"), tr("%1 → %2 (%3 MB)")
                                              .arg(QFileInfo(m_db.path()).fileName(), path,
                                                   QString::number(QFileInfo(path).size() / 1048576.0, 'f', 1)));
    emit backupChanged();
}

namespace {

QString pathFromQml(const QString& pathOrUrl)
{
    const QUrl url(pathOrUrl);
    return url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
}

QString qsoDate(const QDateTime& when)
{
    return when.isValid() ? when.toUTC().toString(dates::format() + QStringLiteral(" HH:mm")) + QStringLiteral("Z")
                          : QString();
}

} // namespace

QVariantList DecoLogController::backupFiles() const
{
    QVariantList out;
    for (const QFileInfo& f : logbackup::backupsIn(m_backupDir)) {
        // Quando esiste il bundle completo, non mostrare anche il suo SQLite
        // interno come una seconda copia selezionabile.
        if (f.isFile() && f.suffix() == QLatin1String("sqlite")
            && QFileInfo(QDir(f.absolutePath()).filePath(f.completeBaseName() + QStringLiteral(".bundle"))).isDir())
            continue;
        out << QVariantMap{
            {QStringLiteral("path"), f.absoluteFilePath()},
            {QStringLiteral("name"), f.fileName()},
            {QStringLiteral("when"), QLocale().toString(f.lastModified(), QLocale::ShortFormat)},
            {QStringLiteral("size"), QLocale().formattedDataSize(f.size())},
            {QStringLiteral("safety"), logbackup::isSafetyCopy(f.fileName())},
        };
    }
    return out;
}

QVariantMap DecoLogController::currentLogInfo() const
{
    QString last;
    QSqlQuery q(m_db.connection());
    if (q.exec(QStringLiteral("SELECT MAX(qso_datetime_on) FROM qso WHERE deleted = 0")) && q.next())
        last = qsoDate(QDateTime::fromString(q.value(0).toString(), Qt::ISODate));
    return {{QStringLiteral("path"), QDir::toNativeSeparators(m_db.path())},
            {QStringLiteral("name"), QFileInfo(m_db.path()).fileName()},
            {QStringLiteral("qsos"), m_db.qsoCount()},
            {QStringLiteral("last"), last}};
}

void DecoLogController::inspectBackup(const QString& pathOrUrl)
{
    const QString path = pathFromQml(pathOrUrl);
    const int current = m_db.qsoCount();
    QPointer<DecoLogController> self(this);
    m_backupPool.start([self, path, current] {
        const QString inspected = logbackup::isBundle(path)
            ? QDir(path).filePath(QStringLiteral("log.sqlite")) : path;
        const logbackup::Snapshot s = logbackup::inspect(inspected);
        const QVariantMap info{
            {QStringLiteral("path"), path},
            {QStringLiteral("name"), QFileInfo(path).fileName()},
            {QStringLiteral("ok"), s.readable},
            {QStringLiteral("problem"), s.problem},
            {QStringLiteral("qsos"), s.qsos},
            {QStringLiteral("first"), qsoDate(s.firstQso)},
            {QStringLiteral("last"), qsoDate(s.lastQso)},
            {QStringLiteral("size"), QLocale().formattedDataSize(s.bytes)},
            {QStringLiteral("diff"), s.qsos - current},
        };
        QMetaObject::invokeMethod(
            self.data(), [self, info] {
                if (self)
                    emit self->backupInspected(info);
            },
            Qt::QueuedConnection);
    });
}

QString DecoLogController::restoreBackup(const QString& pathOrUrl)
{
    const QString path = pathFromQml(pathOrUrl);
    if (!QFileInfo(path).isFile() && !logbackup::isBundle(path))
        return tr("%1 is not there.").arg(QDir::toNativeSeparators(path));
    if (m_db.path().isEmpty() || m_db.path() == QLatin1String(":memory:"))
        return tr("There is no log file to restore over.");
    if (QFileInfo(path).canonicalFilePath() == QFileInfo(m_db.path()).canonicalFilePath())
        return tr("The backup is the log itself.");

    // Il programma riparte e rimette la copia prima di aprire il log, quando
    // questo processo e' uscito. Le impostazioni di prova (--settings, --port)
    // passano al programma nuovo.
    QStringList args{QStringLiteral("--db"), m_db.path(), QStringLiteral("--restore-from"), path,
                     QStringLiteral("--restore-wait-pid"), QString::number(QCoreApplication::applicationPid())};
    const QStringList mine = QCoreApplication::arguments();
    for (const char* keep : {"--settings", "--port"}) {
        const qsizetype at = mine.indexOf(QLatin1String(keep));
        if (at > 0 && at + 1 < mine.size())
            args << mine.at(at)
                 << (at == mine.indexOf(QLatin1String("--settings")) ? QFileInfo(mine.at(at + 1)).absoluteFilePath()
                                                                     : mine.at(at + 1));
    }
    if (!QProcess::startDetached(QCoreApplication::applicationFilePath(), args))
        return tr("The program could not be restarted to restore the backup.");
    QCoreApplication::quit();
    return {};
}

void DecoLogController::reportRestore(const logbackup::RestoreResult& result, const QString& backup)
{
    if (result.ok) {
        addActivity(QStringLiteral("BACKUP"),
                    tr("Log restored from %1: %2 QSO").arg(QDir::toNativeSeparators(backup)).arg(result.qsos),
                    QStringLiteral("success"));
        if (!result.safetyCopy.isEmpty())
            addActivity(QStringLiteral("BACKUP"),
                        tr("The log as it was before is saved in %1").arg(QDir::toNativeSeparators(result.safetyCopy)),
                        QStringLiteral("info"));
    } else {
        addActivity(QStringLiteral("BACKUP"), tr("Restore not done: %1").arg(result.error), QStringLiteral("error"));
    }
}

void DecoLogController::checkBackupSchedule()
{
    if (!m_backupEnabled || !m_db.isOpen())
        return;
    const QTime when = QTime::fromString(m_backupTime, QStringLiteral("HH:mm"));
    const QDateTime now = QDateTime::currentDateTime();
    if (!when.isValid() || now.time() < when)
        return;
    const QDateTime last = QDateTime::fromString(m_db.setting(QStringLiteral("backup.last_at")), Qt::ISODate).toLocalTime();
    // Una copia al giorno, alla prima occasione dopo l'ora scelta: se il PC era
    // spento alle 02:00, la copia si fa appena DecoDXLog e' aperto.
    if (last.isValid() && QDateTime(now.date(), when) <= last)
        return;
    backupNow();
}

// ── LoTW ──────────────────────────────────────────────────────────────────────

QString DecoLogController::lotwLastSync() const
{
    const QDateTime at = QDateTime::fromString(m_db.setting(QStringLiteral("lotw.last_sync_at")), Qt::ISODate);
    if (!at.isValid())
        return {};
    return at.toUTC().toString(dates::format() + QStringLiteral(" HH:mm")) + QStringLiteral("Z");
}

void DecoLogController::setLotwAutoHours(int hours)
{
    if (hours == m_lotwAutoHours || hours < 0)
        return;
    m_lotwAutoHours = hours;
    QSettings().setValue(QStringLiteral("lotw/autoSyncHours"), hours);
    emit lotwChanged();
}

void DecoLogController::checkLotwSchedule()
{
    if (m_lotwAutoHours <= 0 || lotwBusy() || !m_db.isOpen() || m_db.qsoCount() == 0)
        return;
    // Senza credenziali il sync automatico tace: l'operatore non ha chiesto LoTW.
    if (m_credentials->account(QStringLiteral("lotw")).isEmpty() || !m_credentials->hasSecret(QStringLiteral("lotw")))
        return;
    const QDateTime last = QDateTime::fromString(m_db.setting(QStringLiteral("lotw.last_sync_at")), Qt::ISODate);
    if (last.isValid() && last.secsTo(QDateTime::currentDateTimeUtc()) < m_lotwAutoHours * 3600)
        return;
    m_lotwAuto = true;
    syncLotw(false);
}

// ── I colori delle righe del log ──────────────────────────────────────────────

namespace {

struct LogColorDefault {
    const char* key;
    const char* label;
    const char* fg;      // quelli di Decodium 4
    bool fgOn;
    const char* bg;      // un fondo leggero dello stesso colore
};

// Acceso di serie quello che fa notizia (entita', continente, zone); nuovo
// locatore e nuovo nominativo sono quasi tutte le righe di un log, e si
// accendono se li si vuole.
const LogColorDefault kLogColors[] = {
    {"colorNewDxcc", QT_TRANSLATE_NOOP("LogColors", "New DXCC"), "#FF00FF", true, "#55FF00FF"},
    {"colorNewDxccBand", QT_TRANSLATE_NOOP("LogColors", "New DXCC on Band"), "#F8AAD0", true, "#40F8AAD0"},
    {"colorNewContinent", QT_TRANSLATE_NOOP("LogColors", "New Continent"), "#E91E63", true, "#55E91E63"},
    {"colorNewContinentBand", QT_TRANSLATE_NOOP("LogColors", "New Continent on Band"), "#F5B7C7", true, "#40F5B7C7"},
    {"colorNewCqZone", QT_TRANSLATE_NOOP("LogColors", "New CQ Zone"), "#F0A030", true, "#55F0A030"},
    {"colorNewCqZoneBand", QT_TRANSLATE_NOOP("LogColors", "New CQ Zone on Band"), "#F5DDA0", true, "#40F5DDA0"},
    {"colorNewItuZone", QT_TRANSLATE_NOOP("LogColors", "New ITU Zone"), "#9ACD32", true, "#559ACD32"},
    {"colorNewItuZoneBand", QT_TRANSLATE_NOOP("LogColors", "New ITU Zone on Band"), "#D4E89F", true, "#40D4E89F"},
    {"colorNewGrid", QT_TRANSLATE_NOOP("LogColors", "New Grid"), "#FF8C00", false, "#55FF8C00"},
    {"colorNewGridBand", QT_TRANSLATE_NOOP("LogColors", "New Grid on Band"), "#FFCAA0", false, "#40FFCAA0"},
    {"colorNewCall", QT_TRANSLATE_NOOP("LogColors", "New Callsign"), "#00E0E0", false, "#4000E0E0"},
    {"colorNewCallBand", QT_TRANSLATE_NOOP("LogColors", "New Callsign on Band"), "#B5E8E8", false, "#40B5E8E8"},
    {"colorLotwConfirmed", QT_TRANSLATE_NOOP("LogColors", "Confirmed on LoTW"), "#33FF33", false, "#4033FF33"},
    {"colorCardConfirmed", QT_TRANSLATE_NOOP("LogColors", "Confirmed by card (no LoTW)"), "#4FC3F7", false, "#404FC3F7"},
    {"colorEqslConfirmed", QT_TRANSLATE_NOOP("LogColors", "Confirmed only on eQSL / QRZ"), "#FFD54F", false, "#40FFD54F"},
    {"colorB4", QT_TRANSLATE_NOOP("LogColors", "B4 (Worked)"), "#888888", false, "#40888888"},
};

} // namespace

QVariantList DecoLogController::logColorCategories() const
{
    QSettings s;
    QVariantList out;
    for (const LogColorDefault& d : kLogColors) {
        const QString key = QLatin1String(d.key);
        out << QVariantMap{
            {QStringLiteral("key"), key},
            {QStringLiteral("label"), QCoreApplication::translate("LogColors", d.label)},
            {QStringLiteral("fg"), s.value(QStringLiteral("logColors/%1").arg(key), QLatin1String(d.fg)).toString()},
            {QStringLiteral("fgOn"), s.value(QStringLiteral("logColors/%1_on").arg(key), d.fgOn).toBool()},
            {QStringLiteral("bg"), s.value(QStringLiteral("logColors/bg_%1").arg(key), QLatin1String(d.bg)).toString()},
            {QStringLiteral("bgOn"), s.value(QStringLiteral("logColors/bgOn_%1").arg(key), false).toBool()},
            {QStringLiteral("defaultFg"), QLatin1String(d.fg)},
        };
    }
    return out;
}

QVariantMap DecoLogController::logColors() const
{
    QVariantMap out;
    for (const QVariant& v : logColorCategories()) {
        const QVariantMap c = v.toMap();
        const bool fg = c.value(QStringLiteral("fgOn")).toBool();
        const bool bg = c.value(QStringLiteral("bgOn")).toBool();
        if (!fg && !bg)
            continue;
        out.insert(c.value(QStringLiteral("key")).toString(),
                   QVariantMap{{QStringLiteral("fg"), fg ? c.value(QStringLiteral("fg")) : QVariant(QString())},
                               {QStringLiteral("bg"), bg ? c.value(QStringLiteral("bg")) : QVariant(QString())}});
    }
    return out;
}

void DecoLogController::setLogColor(const QString& category, const QString& what, const QVariant& value)
{
    QSettings s;
    if (what == QLatin1String("fg"))
        s.setValue(QStringLiteral("logColors/%1").arg(category), value.toString());
    else if (what == QLatin1String("fgOn"))
        s.setValue(QStringLiteral("logColors/%1_on").arg(category), value.toBool());
    else if (what == QLatin1String("bg"))
        s.setValue(QStringLiteral("logColors/bg_%1").arg(category), value.toString());
    else if (what == QLatin1String("bgOn"))
        s.setValue(QStringLiteral("logColors/bgOn_%1").arg(category), value.toBool());
    else
        return;
    emit logColorsChanged();
}

void DecoLogController::resetLogColors()
{
    QSettings s;
    s.remove(QStringLiteral("logColors"));
    emit logColorsChanged();
}

void DecoLogController::syncLotwRange(const QString& fromIso, const QString& toIso)
{
    const QDate from = QDate::fromString(fromIso.trimmed(), Qt::ISODate);
    const QDate to = QDate::fromString(toIso.trimmed(), Qt::ISODate);
    if (!from.isValid() && !to.isValid()) {
        syncLotw(true);
        return;
    }
    if (from.isValid() && to.isValid() && from > to) {
        m_lotwStatus = tr("LoTW: the period starts after it ends");
        addActivity(QStringLiteral("LOTW"), m_lotwStatus, QStringLiteral("warning"));
        emit lotwChanged();
        return;
    }
    m_lotwFrom = from;
    m_lotwTo = to;
    syncLotw(true);
}

void DecoLogController::syncLotw(bool full)
{
    if (lotwBusy() || !m_db.isOpen())
        return;
    // Un periodo scelto vale per questo scarico soltanto.
    const QDate from = m_lotwFrom;
    const QDate to = m_lotwTo;
    m_lotwFrom = QDate();
    m_lotwTo = QDate();
    m_lotwRange = from.isValid() || to.isValid();
    const QString user = m_credentials->account(QStringLiteral("lotw"));
    if (user.isEmpty() || !m_credentials->hasSecret(QStringLiteral("lotw"))) {
        m_lotwStatus = tr("LoTW: add username and password in Setup → QSL services");
        addActivity(QStringLiteral("LOTW"), m_lotwStatus, QStringLiteral("warning"));
        emit lotwChanged();
        return;
    }
    const QString since = full ? QString() : m_db.setting(QStringLiteral("lotw.last_qsl"));
    m_lotwStarting = true;
    m_lotwStatus = m_lotwRange
        ? tr("LoTW: downloading the confirmations of the QSOs from %1 to %2…")
              .arg(from.isValid() ? dates::show(from.toString(Qt::ISODate)) : QStringLiteral("…"),
                   to.isValid() ? dates::show(to.toString(Qt::ISODate)) : QStringLiteral("…"))
        : since.isEmpty() ? tr("LoTW: downloading all confirmations…")
                          : tr("LoTW: downloading confirmations since %1…").arg(since);
    addActivity(QStringLiteral("LOTW"), m_lotwStatus);
    emit lotwChanged();

    m_credentials->readSecret(QStringLiteral("lotw"), [this, user, since, from, to](const QString& secret, const QString& error) {
        m_lotwStarting = false;
        if (!error.isEmpty() || secret.isEmpty()) {
            lotw::Report failed;
            failed.error = tr("LoTW: password not available (%1)").arg(error);
            onLotwReport(failed);
            return;
        }
        m_lotw.download(user, secret, since, from, to);
        emit lotwChanged();
    });
}

QSet<QString> DecoLogController::confirmedAwardKeys(const QString& awardId) const
{
    QSet<QString> keys;
    for (const AwardResult& r : globalAwardResults()) {
        if (r.id != awardId)
            continue;
        for (const AwardItem& i : r.items) {
            if (awardId == QLatin1String("wac") && !i.countsForAward)
                continue;
            if (i.confirmed())
                keys.insert(i.key);
        }
    }
    return keys;
}

void DecoLogController::onLotwReport(const lotw::Report& report)
{
    const bool automatic = m_lotwAuto;
    m_lotwAuto = false;
    const bool ranged = m_lotwRange;
    m_lotwRange = false;
    if (!report.ok) {
        m_lotwStatus = report.error;
        m_db.setSetting(QStringLiteral("lotw.last_result"), report.error);
        // Un sync automatico fallito si riprova al giro dopo, non a ogni controllo.
        if (automatic)
            m_db.setSetting(QStringLiteral("lotw.last_sync_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        addActivity(QStringLiteral("LOTW"), report.error, QStringLiteral("error"));
        recordQslImport(QStringLiteral("LoTW"), QStringLiteral("lotw"), {}, !automatic, report.error);
        emit lotwChanged();
        return;
    }

    // Quello che era confermato prima, per dire all'operatore cosa c'e' di nuovo.
    const QSet<QString> dxccBefore = confirmedAwardKeys(QStringLiteral("dxcc"));
    const QSet<QString> ft2Before = confirmedAwardKeys(QStringLiteral("ft2"));

    const ConfirmTally tally = applyConfirmations(QStringLiteral("lotw"), report.confirmations, {}, {},
                                                  QStringLiteral("LoTW"));
    recordQslImport(QStringLiteral("LoTW"), QStringLiteral("lotw"), tally, !automatic);
    const int confirmed = tally.confirmed;
    const int already = tally.already;
    const int notFound = tally.notFound;
    const int invalid = tally.invalid;
    const QStringList& missing = tally.missing;

    // Il segno dell'ultimo scarico si sposta solo con lo scarico di tutto: uno
    // scarico per periodo lo porterebbe avanti e il prossimo "solo le nuove"
    // salterebbe le conferme degli altri QSO arrivate nel frattempo.
    if (!report.lastQsl.isEmpty() && !ranged)
        m_db.setSetting(QStringLiteral("lotw.last_qsl"), report.lastQsl);
    m_db.setSetting(QStringLiteral("lotw.last_sync_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    m_lotwStatus = tr("LoTW: %1 new confirmations, %2 already marked, %3 not in the log")
                       .arg(confirmed).arg(already).arg(notFound);
    m_db.setSetting(QStringLiteral("lotw.last_result"), m_lotwStatus);
    addActivity(QStringLiteral("LOTW"), m_lotwStatus, confirmed > 0 ? QStringLiteral("success") : QStringLiteral("info"));
    for (const QString& m : missing)
        addActivity(QStringLiteral("LOTW"), tr("  not in the log: %1").arg(m), QStringLiteral("warning"));
    if (invalid > 0)
        addActivity(QStringLiteral("LOTW"), tr("  %1 records without call, band or date").arg(invalid), QStringLiteral("warning"));

    if (confirmed > 0)
        confirmationsApplied(QStringLiteral("LOTW"), dxccBefore, ft2Before);
    emit lotwChanged();
}

DecoLogController::ConfirmTally DecoLogController::applyConfirmations(const QString& service,
                                                                      const QList<AdifRecord>& list,
                                                                      const QList<qint64>& onlyProfiles,
                                                                      const QList<qint64>& exceptProfiles,
                                                                      const QString& label, const QString& account)
{
    ConfirmTally t;
    QSqlDatabase db = m_db.connection();
    // Le entita' gia' confermate (LoTW o cartolina) prima di questo scarico:
    // una conferma nuova di un'entita' fuori da qui e' un DXCC nuovo.
    const bool countsForDxcc = service == QLatin1String("lotw") || service == QLatin1String("card");
    QSet<int> dxccBefore;
    if (countsForDxcc) {
        QSqlQuery q(db);
        if (q.exec(QStringLiteral("SELECT DISTINCT qso.dxcc FROM qso JOIN qsl_status s ON s.qso_id = qso.id "
                                  "WHERE qso.deleted = 0 AND qso.dxcc > 0 AND s.rcvd = 'Y' "
                                  "AND s.service IN ('lotw', 'card')"))) {
            while (q.next())
                dxccBefore.insert(q.value(0).toInt());
        }
    }
    QSet<int> dxccNow = dxccBefore;
    QSqlQuery one(db);
    one.prepare(QStringLiteral("SELECT IFNULL(dxcc, 0), IFNULL(country, '') FROM qso WHERE id = ?"));
    QSqlQuery known(db);
    known.prepare(QStringLiteral("SELECT COUNT(*) FROM qso WHERE deleted = 0 AND call = ?"));
    // Le righe del riepilogo: tante, ma non infinite (il primo scarico di
    // tutto LoTW sono decine di migliaia di conferme).
    constexpr int kRows = 20000;
    auto row = [&](const AdifRecord& c, const QString& kind, const QString& reason, qint64 id) {
        if (t.rows.size() >= kRows)
            return QVariantMap{};
        const QString iso = LogDatabase::isoFromAdif(c.value(QStringLiteral("QSO_DATE")), c.value(QStringLiteral("TIME_ON")));
        const QDateTime on = QDateTime::fromString(iso, Qt::ISODate);
        QString station = c.value(QStringLiteral("STATION_CALLSIGN")).trimmed().toUpper();
        if (station.isEmpty())
            station = c.value(QStringLiteral("OWNCALL")).trimmed().toUpper();
        QString mode = c.value(QStringLiteral("SUBMODE")).trimmed().toUpper();
        if (mode.isEmpty())
            mode = c.value(QStringLiteral("MODE")).trimmed().toUpper();
        return QVariantMap{
            {QStringLiteral("label"), label.isEmpty() ? service : label},
            {QStringLiteral("service"), service},
            {QStringLiteral("account"), account},
            {QStringLiteral("kind"), kind},
            {QStringLiteral("on"), iso},
            {QStringLiteral("utc"), on.isValid() ? on.toUTC().toString(dates::shortFormat() + QStringLiteral(" HH:mm"))
                                                 : QString()},
            {QStringLiteral("call"), c.value(QStringLiteral("CALL")).trimmed().toUpper()},
            {QStringLiteral("band"), c.value(QStringLiteral("BAND")).trimmed().toLower()},
            {QStringLiteral("mode"), mode},
            // Il modo come lo scrive il servizio: eQSL lo vuole identico per la cartolina.
            {QStringLiteral("rawMode"), c.value(QStringLiteral("MODE")).trimmed().toUpper()},
            {QStringLiteral("station"), station},
            {QStringLiteral("reason"), reason},
            {QStringLiteral("qsoId"), id},
        };
    };
    const bool transaction = db.transaction();
    for (const AdifRecord& c : list) {
        const ConfirmationResult r = m_db.applyConfirmation(service, c, 1800, onlyProfiles, exceptProfiles);
        switch (r.status) {
        case ConfirmationResult::Status::Confirmed: {
            ++t.confirmed;
            QVariantMap m = row(c, QStringLiteral("new"), QString(), r.id);
            if (m.isEmpty())
                break;
            one.bindValue(0, r.id);
            if (one.exec() && one.next()) {
                const int dxcc = one.value(0).toInt();
                m.insert(QStringLiteral("dxcc"), dxcc);
                const QString name = dxcc > 0 ? m_countries.nameFor(dxcc) : QString();
                m.insert(QStringLiteral("country"), name.isEmpty() ? one.value(1).toString() : name);
                if (countsForDxcc && dxcc > 0 && !dxccNow.contains(dxcc)) {
                    m.insert(QStringLiteral("newDxcc"), true);
                    dxccNow.insert(dxcc);
                }
            }
            t.rows << m;
            break;
        }
        case ConfirmationResult::Status::AlreadyConfirmed: ++t.already; break;
        case ConfirmationResult::Status::Ambiguous: {
            ++t.ambiguous;
            const QVariantMap m = row(c, QStringLiteral("ambiguous"), r.message, 0);
            if (!m.isEmpty())
                t.rows << m;
            break;
        }
        case ConfirmationResult::Status::NotFound: {
            ++t.notFound;
            if (t.missing.size() < 10)
                t.missing << r.message;
            // Perche' non si trova: il nominativo non c'e' proprio, o c'e' ma
            // non a quell'ora su quella banda e con quel modo.
            known.bindValue(0, c.value(QStringLiteral("CALL")).trimmed().toUpper());
            const bool callKnown = known.exec() && known.next() && known.value(0).toInt() > 0;
            const QVariantMap m = row(c, QStringLiteral("notfound"),
                                      callKnown ? tr("no QSO with this call within 30 minutes on this band and mode")
                                                : tr("this call is not in the log"),
                                      0);
            if (!m.isEmpty())
                t.rows << m;
            break;
        }
        case ConfirmationResult::Status::Invalid:
        case ConfirmationResult::Status::Error: {
            ++t.invalid;
            const QVariantMap m = row(c, QStringLiteral("invalid"),
                                      r.message.isEmpty() ? tr("call, band or date missing") : r.message, 0);
            if (!m.isEmpty())
                t.rows << m;
            break;
        }
        }
    }
    if (transaction)
        db.commit();
    return t;
}

void DecoLogController::recordQslImport(const QString& label, const QString& service, const ConfirmTally& t,
                                        bool manual, const QString& error)
{
    QVariantList runs;
    for (const QVariant& v : m_qslImport.value(QStringLiteral("runs")).toList()) {
        if (v.toMap().value(QStringLiteral("label")).toString() != label)
            runs << v;
    }
    runs << QVariantMap{{QStringLiteral("label"), label},
                        {QStringLiteral("service"), service},
                        {QStringLiteral("when"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                        {QStringLiteral("whenText"), QDateTime::currentDateTime().toString(dates::shortFormat() + QStringLiteral(" HH:mm"))},
                        {QStringLiteral("confirmed"), t.confirmed},
                        {QStringLiteral("already"), t.already},
                        {QStringLiteral("ambiguous"), t.ambiguous},
                        {QStringLiteral("notFound"), t.notFound},
                        {QStringLiteral("invalid"), t.invalid},
                        // Uno scarico non riuscito resta nel riepilogo, col motivo:
                        // prima la finestra restava vuota e non si capiva perche'.
                        {QStringLiteral("error"), error}};
    QVariantList rows;
    for (const QVariant& v : m_qslImport.value(QStringLiteral("rows")).toList()) {
        if (v.toMap().value(QStringLiteral("label")).toString() != label)
            rows << v;
    }
    rows << t.rows;
    m_qslImport = {{QStringLiteral("runs"), runs}, {QStringLiteral("rows"), rows}};
    m_db.setSetting(QStringLiteral("qsl.import_summary"),
                    QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(m_qslImport)).toJson(QJsonDocument::Compact)));
    emit qslImportChanged();
    if (manual && (!t.rows.isEmpty() || !error.isEmpty()))
        emit qslImportReady();
}

void DecoLogController::clearQslImport()
{
    m_qslImport.clear();
    m_db.setSetting(QStringLiteral("qsl.import_summary"), QString());
    emit qslImportChanged();
}

QString DecoLogController::requestEqslCard(const QVariantMap& row)
{
    QString credential = row.value(QStringLiteral("account")).toString();
    if (credential.isEmpty())
        credential = QStringLiteral("eqsl");
    EqslCardFetcher::Request r;
    r.user = m_credentials->account(credential);
    r.call = row.value(QStringLiteral("call")).toString();
    r.on = QDateTime::fromString(row.value(QStringLiteral("on")).toString(), Qt::ISODate).toUTC();
    r.band = row.value(QStringLiteral("band")).toString();
    r.mode = row.value(QStringLiteral("rawMode")).toString();
    if (r.mode.isEmpty())
        r.mode = row.value(QStringLiteral("mode")).toString();
    const QString key = EqslCardFetcher::keyOf(r);
    if (r.user.isEmpty() || !m_credentials->hasSecret(credential)) {
        QMetaObject::invokeMethod(this, [this, key] {
            emit eqslCardReady(key, QString(), tr("eQSL: no account in Setup → QSL services"));
        }, Qt::QueuedConnection);
        return key;
    }
    m_eqslCards.setCacheDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                            + QStringLiteral("/cache/eqsl"));
    // Gia' scaricata: niente password, niente eQSL.
    if (!m_eqslCards.cachedFile(key).isEmpty()) {
        m_eqslCards.fetch(r);
        return key;
    }
    m_credentials->readSecret(credential, [this, r](const QString& secret, const QString& error) {
        if (secret.isEmpty()) {
            emit eqslCardReady(EqslCardFetcher::keyOf(r), QString(), tr("eQSL: password not available (%1)").arg(error));
            return;
        }
        EqslCardFetcher::Request with = r;
        with.password = secret;
        m_eqslCards.fetch(with);
    });
    return key;
}

void DecoLogController::confirmationsApplied(const QString& category, const QSet<QString>& dxccBefore,
                                             const QSet<QString>& ft2Before)
{
    timed(tr("reloading the log table"), [this] { m_model->reload(); });
    m_awardsDirty = m_globalAwardsDirty = true;
    emit logChanged();
    m_decoLink.resendSnapshot();
    refreshCallInfo();
    // I nuovi DXCC confermati meritano una riga a parte.
    for (const QString& key : confirmedAwardKeys(QStringLiteral("dxcc")) - dxccBefore)
        addActivity(category, tr("New DXCC confirmed: %1").arg(m_countries.nameFor(key.toInt())),
                    QStringLiteral("highlight"));
    for (const QString& key : confirmedAwardKeys(QStringLiteral("ft2")) - ft2Before)
        addActivity(category, tr("New FT2 Award entity confirmed: %1").arg(m_countries.nameFor(key.toInt())),
                    QStringLiteral("highlight"));
}

namespace {

QString confirmLabel(const QString& service)
{
    return service == QLatin1String("qrz") ? QStringLiteral("QRZ") : QStringLiteral("eQSL");
}

} // namespace

QList<DecoLogController::ConfirmAccount> DecoLogController::confirmAccounts(const QString& service) const
{
    // La chiave di QRZ Logbook sta sotto "qrzlogbook"; eQSL vuole anche il nome.
    const QString base = service == QLatin1String("qrz") ? QStringLiteral("qrzlogbook") : service;
    auto usable = [this, &service](const QString& credential) {
        return m_credentials->hasSecret(credential)
               && (service != QLatin1String("eqsl") || !m_credentials->account(credential).isEmpty());
    };
    QList<ConfirmAccount> out;
    QList<qint64> own;
    for (const StationProfile& p : m_db.stationProfiles(false)) {
        const QString credential = CredentialStore::profileService(base, p.id);
        if (!usable(credential))
            continue;
        own << p.id;
        ConfirmAccount a;
        a.service = service;
        a.credential = credential;
        a.key = CredentialStore::profileService(service, p.id);
        a.label = confirmLabel(service) + QLatin1Char(' ') + (p.stationCallsign.isEmpty() ? p.name : p.stationCallsign);
        a.only = {p.id};
        out << a;
    }
    if (usable(base)) {
        ConfirmAccount a;
        a.service = service;
        a.credential = base;
        a.key = service;
        a.label = confirmLabel(service);
        a.except = own;
        out.prepend(a);
    }
    return out;
}

void DecoLogController::syncConfirmations(const QString& service, bool full)
{
    startConfirmations(service, full, QDate(), QDate());
}

void DecoLogController::syncConfirmationsRange(const QString& service, const QString& fromIso, const QString& toIso)
{
    const QDate from = QDate::fromString(fromIso.trimmed(), Qt::ISODate);
    const QDate to = QDate::fromString(toIso.trimmed(), Qt::ISODate);
    if (!from.isValid() && !to.isValid()) {
        startConfirmations(service, true, QDate(), QDate());
        return;
    }
    if (from.isValid() && to.isValid() && from > to) {
        if (confirmBusy())
            return;
        m_confirmFailed = true;
        m_confirmStatus = tr("%1: the period starts after it ends").arg(confirmLabel(service));
        addActivity(confirmLabel(service).toUpper(), m_confirmStatus, QStringLiteral("warning"));
        emit confirmChanged();
        return;
    }
    startConfirmations(service, true, from, to);
}

void DecoLogController::startConfirmations(const QString& service, bool full, const QDate& from, const QDate& to)
{
    if (confirmBusy() || !m_db.isOpen())
        return;
    if (service != QLatin1String("eqsl") && service != QLatin1String("qrz"))
        return;
    const QList<ConfirmAccount> accounts = confirmAccounts(service);
    if (accounts.isEmpty()) {
        m_confirmFailed = true;
        m_confirmStatus = service == QLatin1String("qrz")
                              ? tr("QRZ: add the logbook API key in Setup → QSL services")
                              : tr("eQSL: add username and password in Setup → QSL services");
        addActivity(confirmLabel(service).toUpper(), m_confirmStatus, QStringLiteral("warning"));
        emit confirmChanged();
        return;
    }
    // Il tentativo, riuscito o no, sposta l'orario dello scarico automatico;
    // "da quando" lo sposta solo uno scarico riuscito, account per account.
    m_db.setSetting(service + QStringLiteral(".last_attempt_at"),
                    QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    m_confirmQueue = accounts;
    m_confirmFull = full;
    m_confirmFrom = from;
    m_confirmTo = to;
    m_confirmFailed = false;
    startNextConfirmAccount();
}

void DecoLogController::startNextConfirmAccount()
{
    if (m_confirmQueue.isEmpty()) {
        m_confirmService.clear();
        // Un periodo vale per questo scarico soltanto.
        m_confirmFrom = QDate();
        m_confirmTo = QDate();
        // Finito uno scarico automatico, tocca all'altro servizio se e' ora.
        if (std::exchange(m_confirmAuto, false))
            QTimer::singleShot(10'000, this, &DecoLogController::checkConfirmSchedule);
        emit confirmChanged();
        return;
    }
    m_confirmAccount = m_confirmQueue.takeFirst();
    const ConfirmAccount a = m_confirmAccount;
    // Solo quello arrivato dopo l'ultimo scarico riuscito, con un giorno di margine.
    const QDateTime last = QDateTime::fromString(m_db.setting(a.key + QStringLiteral(".last_sync_at")), Qt::ISODate);
    const bool ranged = m_confirmFrom.isValid() || m_confirmTo.isValid();
    // Un periodo si chiede per intero, non dall'ultimo scarico.
    const QDateTime since = ranged ? QDateTime() : confirmations::downloadSince(last, m_confirmFull);
    const QDate from = m_confirmFrom;
    const QDate to = m_confirmTo;
    m_confirmStarting = a.service;
    m_confirmService = a.service;
    m_confirmStatus = ranged
        ? tr("%1: downloading the confirmations of the QSOs from %2 to %3…")
              .arg(a.label,
                   from.isValid() ? dates::show(from.toString(Qt::ISODate)) : QStringLiteral("…"),
                   to.isValid() ? dates::show(to.toString(Qt::ISODate)) : QStringLiteral("…"))
        : since.isValid()
              ? tr("%1: downloading the confirmations since %2…")
                    .arg(a.label, dates::show(since.date().toString(Qt::ISODate)))
              : tr("%1: downloading all the confirmations…").arg(a.label);
    addActivity(confirmLabel(a.service).toUpper(), m_confirmStatus);
    emit confirmChanged();
    const QString account = m_credentials->account(a.credential);
    m_credentials->readSecret(a.credential, [this, a, account, since, from, to](const QString& secret, const QString& error) {
        m_confirmStarting.clear();
        if (!error.isEmpty() || secret.isEmpty()) {
            confirmations::Report failed;
            failed.service = a.service;
            failed.error = tr("%1: password or key not available (%2)").arg(a.label, error);
            onConfirmationReport(failed);
            return;
        }
        if (a.service == QLatin1String("qrz"))
            m_confirmDownloader.downloadQrz(secret, since.isValid() ? since.date() : QDate(), from, to);
        else
            m_confirmDownloader.downloadEqsl(account, secret, since, from, to);
        emit confirmChanged();
    });
}

// ── I QSO rimasti nel log di Decodium e negli altri log ADIF ──────────────────
//
// Decodium scrive ogni QSO anche in decodium_log.adi. Ogni cinque minuti si
// guardano quelli registrati dall'ultimo controllo (con dieci minuti di
// margine) e si salvano quelli che il log non conosce. Il segno di fin dove si
// e' guardato e' uno solo per tutti i log: i QSO fatti mentre era aperto un
// altro log (una gara) sono arrivati a quello, e non si tirano dentro qui.
//
// Allo stesso modo si tengono d'occhio altri log ADIF scelti dall'operatore:
// fldigi, WSJT-X, JTDX, o qualunque programma che scrive un .adi. Per quelli si
// parte dal momento in cui si aggiungono; il passato si prende a mano.

namespace {

// Quelli degli ultimi tre minuti no: stanno ancora arrivando via UDP.
constexpr int kRecoveryLagSeconds = 180;
constexpr int kRecoveryMarginSeconds = 600;
constexpr int kRecoveryFirstDays = 7;
// Oltre questi, tutti insieme: una transazione, la tabella ricaricata una
// volta, una riga sola nel registro e niente callbook per ognuno.
constexpr int kRecoveryOneByOne = 20;

QString watchKey(const QString& path)
{
    return QString::fromLatin1(QCryptographicHash::hash(QDir::cleanPath(path).toLower().toUtf8(),
                                                        QCryptographicHash::Sha1)
                                   .toHex()
                                   .left(16));
}

QDateTime settingTime(const QString& key)
{
    return QDateTime::fromString(QSettings().value(key).toString(), Qt::ISODate);
}

} // namespace

void DecoLogController::setDecodiumRecovery(bool on)
{
    if (on == m_recoveryEnabled)
        return;
    m_recoveryEnabled = on;
    QSettings().setValue(QStringLiteral("decodium/recoverFromLog"), on);
    emit recoveryChanged();
    if (on)
        QTimer::singleShot(0, this, &DecoLogController::checkDecodiumRecovery);
}

void DecoLogController::setDecodiumLogPath(const QString& path)
{
    const QString clean = path.trimmed();
    if (clean == m_decodiumLogPath)
        return;
    m_decodiumLogPath = clean;
    QSettings().setValue(QStringLiteral("decodium/logPath"), clean);
    emit recoveryChanged();
}

QString DecoLogController::decodiumLogInUse() const
{
    if (!m_decodiumLogPath.isEmpty())
        return QFileInfo(m_decodiumLogPath).isFile() ? m_decodiumLogPath : QString();
    return decodiumlog::candidates().value(0);
}

DecoLogController::RecoverySource DecoLogController::decodiumSource() const
{
    RecoverySource s;
    s.path = decodiumLogInUse();
    s.label = QStringLiteral("Decodium");
    s.untilKey = QStringLiteral("decodium/recoveredUntil");
    s.source = QStringLiteral("decodium_adif");
    s.category = QStringLiteral("DECODIUM");
    s.decodium = true;
    return s;
}

QList<DecoLogController::RecoverySource> DecoLogController::watchSources(bool enabledOnly) const
{
    QList<RecoverySource> out;
    for (const QVariant& v : QSettings().value(QStringLiteral("watch/files")).toList()) {
        const QVariantMap m = v.toMap();
        if (enabledOnly && !m.value(QStringLiteral("enabled"), true).toBool())
            continue;
        RecoverySource s;
        s.path = m.value(QStringLiteral("path")).toString();
        s.label = m.value(QStringLiteral("label")).toString();
        if (s.label.isEmpty())
            s.label = QFileInfo(s.path).fileName();
        s.untilKey = QStringLiteral("watch/until/") + watchKey(s.path);
        s.source = QStringLiteral("adif_watch");
        s.category = QStringLiteral("ADIF");
        out << s;
    }
    return out;
}

QVariantList DecoLogController::adifWatches() const
{
    QVariantList out;
    const QVariantList stored = QSettings().value(QStringLiteral("watch/files")).toList();
    for (const QVariant& v : stored) {
        QVariantMap m = v.toMap();
        const QString path = m.value(QStringLiteral("path")).toString();
        if (m.value(QStringLiteral("label")).toString().isEmpty())
            m.insert(QStringLiteral("label"), QFileInfo(path).fileName());
        m.insert(QStringLiteral("enabled"), m.value(QStringLiteral("enabled"), true).toBool());
        m.insert(QStringLiteral("exists"), QFileInfo(path).isFile());
        m.insert(QStringLiteral("status"), m_recoveryStatuses.value(path));
        m.insert(QStringLiteral("nativePath"), QDir::toNativeSeparators(path));
        out << m;
    }
    return out;
}

QVariantList DecoLogController::adifWatchSuggestions() const
{
    // I log dei programmi che si trovano gia' al loro posto.
    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString home = QDir::homePath();
    const QList<QPair<QString, QString>> known{
#if defined(Q_OS_WIN)
        {QStringLiteral("fldigi"), home + QStringLiteral("/fldigi.files/logs/logbook.adif")},
#else
        {QStringLiteral("fldigi"), home + QStringLiteral("/.fldigi/logs/logbook.adif")},
#endif
        {QStringLiteral("WSJT-X"), data + QStringLiteral("/WSJT-X/wsjtx_log.adi")},
        {QStringLiteral("JTDX"), data + QStringLiteral("/JTDX/wsjtx_log.adi")},
        {QStringLiteral("JS8Call"), data + QStringLiteral("/JS8Call/js8call_log.adi")},
    };
    QStringList watched;
    for (const RecoverySource& s : watchSources(false))
        watched << QDir::cleanPath(s.path).toLower();
    QVariantList out;
    for (const auto& [label, path] : known) {
        if (!QFileInfo(path).isFile() || watched.contains(QDir::cleanPath(path).toLower()))
            continue;
        out << QVariantMap{{QStringLiteral("label"), label}, {QStringLiteral("path"), path}};
    }
    return out;
}

void DecoLogController::addAdifWatch(const QString& path, const QString& label)
{
    const QString clean = QDir::cleanPath(path.trimmed());
    if (clean.isEmpty())
        return;
    QVariantList stored = QSettings().value(QStringLiteral("watch/files")).toList();
    for (const QVariant& v : std::as_const(stored)) {
        if (QDir::cleanPath(v.toMap().value(QStringLiteral("path")).toString()).compare(clean, Qt::CaseInsensitive) == 0)
            return;
    }
    const QString name = label.trimmed().isEmpty() ? QFileInfo(clean).fileName() : label.trimmed();
    stored << QVariantMap{{QStringLiteral("path"), clean}, {QStringLiteral("label"), name},
                          {QStringLiteral("enabled"), true}};
    QSettings s;
    s.setValue(QStringLiteral("watch/files"), stored);
    // Da adesso: il passato si prende a mano, con "tutto il file".
    s.setValue(QStringLiteral("watch/until/") + watchKey(clean),
               QDateTime::currentDateTimeUtc().addSecs(-kRecoveryLagSeconds).toString(Qt::ISODate));
    addActivity(QStringLiteral("ADIF"), tr("Keeping an eye on %1 (%2)").arg(name, QDir::toNativeSeparators(clean)));
    emit recoveryChanged();
}

void DecoLogController::removeAdifWatch(const QString& path)
{
    QVariantList stored = QSettings().value(QStringLiteral("watch/files")).toList();
    for (qsizetype i = stored.size() - 1; i >= 0; --i) {
        if (stored.at(i).toMap().value(QStringLiteral("path")).toString() == path)
            stored.removeAt(i);
    }
    QSettings s;
    s.setValue(QStringLiteral("watch/files"), stored);
    s.remove(QStringLiteral("watch/until/") + watchKey(path));
    m_recoveryStatuses.remove(path);
    emit recoveryChanged();
}

void DecoLogController::setAdifWatchEnabled(const QString& path, bool on)
{
    QVariantList stored = QSettings().value(QStringLiteral("watch/files")).toList();
    for (QVariant& v : stored) {
        QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("path")).toString() == path) {
            m.insert(QStringLiteral("enabled"), on);
            v = m;
        }
    }
    QSettings().setValue(QStringLiteral("watch/files"), stored);
    emit recoveryChanged();
}

QDateTime DecoLogController::recoveryFrom(const RecoverySource& source, const QDateTime& to) const
{
    const QDateTime until = settingTime(source.untilKey);
    // Decodium la prima volta guarda indietro una settimana; gli altri file
    // partono da quando si sono aggiunti.
    QDateTime from = until.isValid()    ? until.addSecs(-kRecoveryMarginSeconds)
                   : source.decodium    ? to.addDays(-kRecoveryFirstDays)
                                        : to.addSecs(-kRecoveryMarginSeconds);
    if (from >= to)
        from = to.addSecs(-kRecoveryMarginSeconds);
    return from;
}

void DecoLogController::checkDecodiumRecovery()
{
    if (m_recoveryRunning || !m_recoveryQueue.isEmpty())
        return;
    const QDateTime to = QDateTime::currentDateTimeUtc().addSecs(-kRecoveryLagSeconds);
    if (m_recoveryEnabled) {
        const RecoverySource decodium = decodiumSource();
        m_recoveryQueue << RecoveryJob{decodium, recoveryFrom(decodium, to), to, false};
    }
    for (const RecoverySource& s : watchSources(true))
        m_recoveryQueue << RecoveryJob{s, recoveryFrom(s, to), to, false};
    runNextRecovery();
}

void DecoLogController::recoverFromDecodium(int days)
{
    const QDateTime to = QDateTime::currentDateTimeUtc().addSecs(-kRecoveryLagSeconds);
    m_recoveryQueue << RecoveryJob{decodiumSource(), days > 0 ? to.addDays(-days) : QDateTime(), to, true};
    runNextRecovery();
}

void DecoLogController::checkAdifWatch(const QString& path, int days)
{
    const QDateTime to = QDateTime::currentDateTimeUtc().addSecs(-kRecoveryLagSeconds);
    for (const RecoverySource& s : watchSources(false)) {
        if (s.path == path)
            m_recoveryQueue << RecoveryJob{s, days > 0 ? to.addDays(-days) : QDateTime(), to, true};
    }
    runNextRecovery();
}

void DecoLogController::runNextRecovery()
{
    if (m_recoveryRunning || m_recoveryQueue.isEmpty())
        return;
    startRecovery(m_recoveryQueue.takeFirst());
}

void DecoLogController::startRecovery(const RecoveryJob& job)
{
    if (!m_db.isOpen() || m_db.path().isEmpty() || m_db.path() == QLatin1String(":memory:")) {
        m_recoveryQueue.clear();
        return;
    }
    const RecoverySource& source = job.source;
    if (source.path.isEmpty() || !QFileInfo(source.path).isFile()) {
        const QString status = source.decodium && m_decodiumLogPath.isEmpty()
                                   ? tr("Decodium log not found")
                                   : tr("%1 does not exist").arg(QDir::toNativeSeparators(
                                         source.path.isEmpty() ? m_decodiumLogPath : source.path));
        if (source.decodium)
            m_recoveryStatus = status;
        else
            m_recoveryStatuses.insert(source.path, status);
        if (job.manual)
            addActivity(source.category, status, QStringLiteral("warning"));
        emit recoveryChanged();
        QTimer::singleShot(0, this, &DecoLogController::runNextRecovery);
        return;
    }
    m_recoveryRunning = true;
    emit recoveryChanged();
    // Il file e il confronto con il log su un altro filo: i file crescono con
    // gli anni, e il log puo' avere un milione di QSO.
    const QString dbPath = m_db.path();
    // Per i QSO di Decodium la finestra dei digitali; gli altri programmi
    // (fldigi, scritti a mano) quella piu' larga dei QSO a mano.
    const int window = m_db.dedupWindowSeconds(!source.decodium);
    const int manualWindow = m_db.dedupWindowSeconds(true);
    // Anche gli altri log dell'elenco: un QSO fatto mentre era aperto il log di
    // una gara e' arrivato a quello, e qui non va.
    QStringList others;
    if (m_logs) {
        const QString current = QFileInfo(dbPath).canonicalFilePath();
        for (const QString& p : m_logs->paths()) {
            const QFileInfo info(p);
            if (info.isFile() && info.canonicalFilePath().compare(current, Qt::CaseInsensitive) != 0)
                others << p;
        }
    }
    QPointer<DecoLogController> self(this);
    m_recoveryPool.start([self, job, dbPath, others, window, manualWindow] {
        decodiumlog::Tail tail = decodiumlog::recent(job.source.path, job.from, job.to);
        QList<AdifRecord> missing;
        if (tail.ok) {
            LogDatabase db;
            if (db.open(dbPath)) {
                db.setDedupWindows(window, manualWindow);
                for (const AdifRecord& r : std::as_const(tail.records)) {
                    if (!db.knowsQso(r))
                        missing << r;
                }
                db.close();
            } else {
                tail.ok = false;
                tail.error = QStringLiteral("cannot open the log");
            }
        }
        for (const QString& other : others) {
            if (missing.isEmpty())
                break;
            LogDatabase db;
            if (!db.open(other))
                continue;
            db.setDedupWindows(window, manualWindow);
            QList<AdifRecord> still;
            for (const AdifRecord& r : std::as_const(missing)) {
                if (!db.knowsQso(r))
                    still << r;
            }
            db.close();
            missing = still;
        }
        QMetaObject::invokeMethod(
            self.data(),
            [self, job, tail, missing] {
                if (self)
                    self->finishRecovery(job, tail, missing);
            },
            Qt::QueuedConnection);
    });
}

void DecoLogController::finishRecovery(const RecoveryJob& job, const decodiumlog::Tail& tail,
                                       const QList<AdifRecord>& missing)
{
    m_recoveryRunning = false;
    const RecoverySource& source = job.source;
    const QString when = QDateTime::currentDateTimeUtc().toString(QStringLiteral("HH:mm")) + QStringLiteral("Z");
    auto setStatus = [this, &source](const QString& status) {
        if (source.decodium)
            m_recoveryStatus = status;
        else
            m_recoveryStatuses.insert(source.path, status);
    };
    if (!tail.ok) {
        // Il segno non si sposta: al prossimo giro si riguarda lo stesso tratto.
        const QString status = source.decodium ? tr("%1 · Decodium log not readable: %2").arg(when, tail.error)
                                               : tr("%1 · %2 not readable: %3").arg(when, source.label, tail.error);
        setStatus(status);
        addActivity(source.category, status, QStringLiteral("warning"));
        emit recoveryChanged();
        runNextRecovery();
        return;
    }
    const bool bulk = missing.size() > kRecoveryOneByOne;
    QSqlDatabase db = m_db.connection();
    const bool transaction = bulk && db.transaction();
    int saved = 0;
    int activationDuplicates = 0;
    for (const AdifRecord& r : missing) {
        switch (saveRecoveredQso(r, bulk, source)) {
        case Recovered::Saved: ++saved; break;
        case Recovered::ActivationDuplicate: ++activationDuplicates; break;
        case Recovered::Known:
        case Recovered::Failed: break;
        }
    }
    if (transaction)
        db.commit();
    if (saved > 0) {
        if (bulk)
            timed(tr("reloading the log table"), [this] { m_model->reload(); });
        emit logChanged();
        m_decoLink.resendSnapshot();
        refreshCallInfo();
    }
    const QDateTime until = settingTime(source.untilKey);
    if (!until.isValid() || job.to > until)
        QSettings().setValue(source.untilKey, job.to.toString(Qt::ISODate));

    QString status;
    if (source.decodium)
        status = saved > 0 ? tr("%1 · %n QSO(s) recovered from the Decodium log", "", saved).arg(when)
                           : tr("%1 · nothing missing (%n QSO(s) checked)", "", int(tail.records.size())).arg(when);
    else
        status = saved > 0 ? tr("%1 · %n QSO(s) recovered from %2", "", saved).arg(when, source.label)
                           : tr("%1 · nothing missing (%n QSO(s) checked)", "", int(tail.records.size())).arg(when);
    // Quelli che l'attivazione (o la gara) aperta rifiuta come doppioni, come
    // farebbe con quelli via UDP: si dice, perche' un'attivazione dimenticata
    // aperta li fa sparire.
    if (activationDuplicates > 0)
        status += tr(" · %n skipped as duplicates of the open activation “%1”", "", activationDuplicates)
                      .arg(m_activation->session().title());
    setStatus(status);
    if (saved > 0 || job.manual || activationDuplicates > 0)
        addActivity(source.category, status,
                    activationDuplicates > 0 ? QStringLiteral("warning")
                    : saved > 0              ? QStringLiteral("success")
                                             : QStringLiteral("info"));
    emit recoveryChanged();
    runNextRecovery();
}

DecoLogController::Recovered DecoLogController::saveRecoveredQso(const AdifRecord& input, bool bulk,
                                                                 const RecoverySource& source)
{
    // Arrivato via UDP mentre si leggeva il file.
    if (m_db.knowsQso(input))
        return Recovered::Known;
    const QString stationCall = input.value(QStringLiteral("STATION_CALLSIGN")).trimmed();
    const QString operatorCall = input.value(QStringLiteral("OPERATOR")).trimmed();
    qint64 profileId = m_db.profileForIdentity(stationCall, operatorCall);
    if (profileId == 0 && stationCall.isEmpty() && operatorCall.isEmpty() && m_profiles)
        profileId = m_profiles->activeProfileId();
    else if (profileId == 0 && m_db.stationProfiles(false).size() <= 1 && m_profiles)
        profileId = m_profiles->activeProfileId();
    AdifRecord enriched = input;
    adif::normalizeMode(enriched);
    applyEntity(enriched);
    // In un'attivazione solo i QSO fatti dopo che e' cominciata.
    const QDateTime at = decodiumlog::loggedAt(input);
    const bool inActivation = m_activation->active() && at.isValid() && m_activation->session().startedAt.isValid()
                              && at >= m_activation->session().startedAt;
    const QString mode = enriched.value(QStringLiteral("SUBMODE")).isEmpty()
                             ? enriched.value(QStringLiteral("MODE"))
                             : enriched.value(QStringLiteral("SUBMODE"));
    const QString call = enriched.value(QStringLiteral("CALL")).trimmed().toUpper();
    if (inActivation) {
        m_activation->applyTo(enriched);
        if (m_activation->isDuplicate(call, enriched.value(QStringLiteral("BAND")), mode))
            return Recovered::ActivationDuplicate;
        if (m_activation->session().stationProfileId > 0)
            profileId = m_activation->session().stationProfileId;
    }
    const QString sourceApp = source.decodium ? QStringLiteral("Decodium") : source.label;
    const InsertResult r = m_db.insertQso(enriched, source.source, sourceApp, false, profileId);
    if (r.status != InsertResult::Status::Inserted) {
        // Un doppione e' normale (Decodium a volte scrive due volte lo stesso
        // QSO); il resto si dice.
        if (r.status != InsertResult::Status::Duplicate)
            addActivity(source.category, tr("Not recovered: %1 (%2)").arg(call, r.message), QStringLiteral("warning"));
        return r.status == InsertResult::Status::Duplicate ? Recovered::Known : Recovered::Failed;
    }
    m_qsl->qsoLogged(r.id);
    if (inActivation)
        m_activation->qsoLogged();
    if (m_net)
        m_net->qsoLogged(r.id);
    m_cloud->qsoLogged();
    if (bulk)
        return Recovered::Saved;
    m_model->insertQso(r.id);
    const QString band = enriched.value(QStringLiteral("BAND")).toLower();
    const QString time = at.isValid() ? at.toString(dates::format() + QStringLiteral(" HH:mm")) + QStringLiteral("Z")
                                      : QString();
    addActivity(source.category,
                source.decodium ? tr("Recovered from the Decodium log → %1 %2 %3 %4").arg(call, band, mode, time)
                                : tr("Recovered from %1 → %2 %3 %4 %5").arg(source.label, call, band, mode, time),
                QStringLiteral("success"));
    completeFromCallbook(r.id, call);
    return Recovered::Saved;
}

void DecoLogController::setConfirmAutoHours(int hours)
{
    if (hours == m_confirmAutoHours || hours < 0)
        return;
    m_confirmAutoHours = hours;
    QSettings().setValue(QStringLiteral("confirmations/autoSyncHours"), hours);
    emit confirmChanged();
}

QVariantMap DecoLogController::confirmLastSync() const
{
    QVariantMap out;
    for (const QString& service : {QStringLiteral("eqsl"), QStringLiteral("qrz")}) {
        const QDateTime at =
            QDateTime::fromString(m_db.setting(service + QStringLiteral(".last_sync_at")), Qt::ISODate);
        out.insert(service, at.isValid() ? at.toUTC().toString(dates::format() + QStringLiteral(" HH:mm"))
                                               + QStringLiteral("Z")
                                         : QString());
    }
    return out;
}

void DecoLogController::checkConfirmSchedule()
{
    if (m_confirmAutoHours <= 0 || confirmBusy() || !m_db.isOpen() || m_db.qsoCount() == 0)
        return;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (const QString& service : {QStringLiteral("eqsl"), QStringLiteral("qrz")}) {
        // Senza credenziali tace: l'operatore quel servizio non lo usa.
        if (confirmAccounts(service).isEmpty())
            continue;
        const QDateTime success =
            QDateTime::fromString(m_db.setting(service + QStringLiteral(".last_sync_at")), Qt::ISODate);
        const QDateTime attempt =
            QDateTime::fromString(m_db.setting(service + QStringLiteral(".last_attempt_at")), Qt::ISODate);
        if (!confirmations::autoDownloadDue(success, attempt, now, m_confirmAutoHours))
            continue;
        // Uno alla volta: l'altro al giro dopo questo (onConfirmationReport).
        m_confirmAuto = true;
        syncConfirmations(service, false);
        if (!confirmBusy())
            m_confirmAuto = false;
        return;
    }
}

void DecoLogController::onConfirmationReport(const confirmations::Report& report)
{
    const QString category = confirmLabel(report.service).toUpper();
    const ConfirmAccount a = m_confirmAccount;
    if (!report.ok) {
        m_confirmFailed = true;
        // Di un account di profilo si dice quale.
        m_confirmStatus = a.only.isEmpty() ? report.error : a.label + QStringLiteral(" · ") + report.error;
        m_db.setSetting(a.key + QStringLiteral(".last_result"), m_confirmStatus);
        addActivity(category, m_confirmStatus, QStringLiteral("error"));
        recordQslImport(a.label, report.service, {}, !m_confirmAuto, report.error);
        startNextConfirmAccount();
        return;
    }
    const QSet<QString> dxccBefore = confirmedAwardKeys(QStringLiteral("dxcc"));
    const QSet<QString> ft2Before = confirmedAwardKeys(QStringLiteral("ft2"));
    const ConfirmTally t = applyConfirmations(report.service, report.confirmations, a.only, a.except, a.label,
                                              a.credential);
    recordQslImport(a.label, report.service, t, !m_confirmAuto);
    // Uno scarico per un periodo non e' un «siamo allineati»: lo scarico di
    // sempre riparte da dove era.
    const bool ranged = m_confirmFrom.isValid() || m_confirmTo.isValid();
    if (!ranged)
        m_db.setSetting(a.key + QStringLiteral(".last_sync_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    m_confirmStatus = ranged
        ? tr("%1: %2 new confirmations in the period, %3 already marked, %4 not in the log")
              .arg(a.label)
              .arg(t.confirmed)
              .arg(t.already)
              .arg(t.notFound)
        : tr("%1: %2 new confirmations, %3 already marked, %4 not in the log")
              .arg(a.label)
              .arg(t.confirmed)
              .arg(t.already)
              .arg(t.notFound);
    m_db.setSetting(a.key + QStringLiteral(".last_result"), m_confirmStatus);
    addActivity(category, m_confirmStatus, t.confirmed > 0 ? QStringLiteral("success") : QStringLiteral("info"));
    for (const QString& m : t.missing)
        addActivity(category, tr("  not in the log: %1").arg(m), QStringLiteral("warning"));
    if (t.invalid > 0)
        addActivity(category, tr("  %1 records without call, band or date").arg(t.invalid), QStringLiteral("warning"));
    if (t.confirmed > 0)
        confirmationsApplied(category, dxccBefore, ft2Before);
    startNextConfirmAccount();
}

void DecoLogController::openDatabaseFolder() const
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_db.path()).absolutePath()));
}

// ── Cloud ─────────────────────────────────────────────────────────────────────

void DecoLogController::setCloudServer(const QString& url)
{
    if (url == m_cloudServer)
        return;
    m_cloudServer = url.trimmed();
    QSettings().setValue(QStringLiteral("cloud/server"), m_cloudServer);
    emit cloudChanged();
}

void DecoLogController::setAutoSync(const QString& mode)
{
    if (mode == m_autoSync)
        return;
    m_autoSync = mode;
    QSettings().setValue(QStringLiteral("cloud/autoSync"), mode);
    emit cloudChanged();
}

void DecoLogController::setConflictPolicy(const QString& policy)
{
    if (policy == m_conflictPolicy)
        return;
    m_conflictPolicy = policy;
    QSettings().setValue(QStringLiteral("cloud/conflictPolicy"), policy);
    emit cloudChanged();
}

// ── Statistiche e pannello del nominativo ─────────────────────────────────────

QVariantMap DecoLogController::ft2Award() const
{
    decolog::StartupSpan trace("DecoLogController::ft2Award");
    const auto cached = m_statsCache.constFind(QStringLiteral("ft2"));
    if (cached != m_statsCache.constEnd())
        return cached->toMap();
    if (backgroundReady()) {
        scheduleStatsRefresh();
        return QVariantMap{{QStringLiteral("qsos"), 0}, {QStringLiteral("dxccWorked"), 0},
                           {QStringLiteral("dxccConfirmed"), 0}, {QStringLiteral("gridsWorked"), 0},
                           {QStringLiteral("gridsConfirmed"), 0}};
    }
    const Ft2Award a = m_db.ft2Award();
    const QVariantMap out{
        {QStringLiteral("qsos"), a.qsos},
        {QStringLiteral("dxccWorked"), a.dxccWorked},
        {QStringLiteral("dxccConfirmed"), a.dxccConfirmed},
        {QStringLiteral("gridsWorked"), a.gridsWorked},
        {QStringLiteral("gridsConfirmed"), a.gridsConfirmed},
    };
    m_statsCache.insert(QStringLiteral("ft2"), out);
    return out;
}

QVariantList DecoLogController::bandStats() const
{
    const auto cached = m_statsCache.constFind(QStringLiteral("bands"));
    if (cached != m_statsCache.constEnd())
        return cached->toList();
    if (backgroundReady()) {
        scheduleStatsRefresh();
        return {};
    }
    QVariantList out;
    for (const auto& row : m_db.countByBand())
        out << QVariantMap{{QStringLiteral("key"), row.key}, {QStringLiteral("count"), row.count}};
    m_statsCache.insert(QStringLiteral("bands"), out);
    return out;
}

QVariantList DecoLogController::modeStats() const
{
    const auto cached = m_statsCache.constFind(QStringLiteral("modes"));
    if (cached != m_statsCache.constEnd())
        return cached->toList();
    if (backgroundReady()) {
        scheduleStatsRefresh();
        return {};
    }
    QVariantList out;
    for (const auto& row : m_db.countByMode())
        out << QVariantMap{{QStringLiteral("key"), row.key}, {QStringLiteral("count"), row.count}};
    m_statsCache.insert(QStringLiteral("modes"), out);
    return out;
}

QVariantList DecoLogController::qslSummary() const
{
    ensureCounts();
    return m_counts.qslSummary;
}

bool DecoLogController::backgroundReady() const
{
    return m_db.isOpen() && !m_db.path().isEmpty() && m_db.path() != QLatin1String(":memory:");
}

void DecoLogController::scheduleStatsRefresh() const
{
    if (m_statsRefreshScheduled)
        return;
    m_statsRefreshScheduled = true;
    auto* self = const_cast<DecoLogController*>(this);
    QMetaObject::invokeMethod(
        self, [self] {
            self->m_statsRefreshScheduled = false;
            self->refreshStatsInBackground();
        },
        Qt::QueuedConnection);
}

void DecoLogController::ensureCounts() const
{
    if (m_counts.valid || !m_db.isOpen())
        return;
    // Su un log in un file i conti si fanno su un altro filo (m_countsTimer):
    // fino ad allora valgono zero, e countsChanged() dice quando ci sono.
    if (backgroundReady()) {
        if (!m_countsTimer.isActive())
            QMetaObject::invokeMethod(const_cast<QTimer*>(&m_countsTimer), qOverload<>(&QTimer::start),
                                      Qt::QueuedConnection);
        return;
    }
    m_counts.qsos = m_db.qsoCount();
    m_counts.dirty = m_db.dirtyCount();
    m_counts.conflicts = m_db.conflictCount();
    m_counts.missingDxcc = static_cast<int>(m_db.idsWithoutDxcc().size());
    m_counts.qslSummary.clear();
    for (QVariantMap row : m_db.qslSummary()) {
        row[QStringLiteral("label")] = serviceLabel(row.value(QStringLiteral("service")).toString());
        m_counts.qslSummary << row;
    }
    m_counts.valid = true;
}

namespace {

QVariantList rowsToList(const QList<CountRow>& rows)
{
    QVariantList out;
    for (const CountRow& r : rows)
        out << QVariantMap{{QStringLiteral("key"), r.key}, {QStringLiteral("count"), r.count}};
    return out;
}

StatsFilter statsFilterFor(const QString& mode, int year)
{
    StatsFilter f;
    f.mode = mode;
    f.year = year;
    // Come nei diplomi: i 60 metri nel DXCC se l'operatore li conta. Si legge
    // dalle impostazioni perche' qui si arriva anche da un altro filo.
    f.count60m = QSettings().value(QStringLiteral("awards/count60m"), false).toBool();
    return f;
}

} // namespace

QVariantList DecoLogController::landmasses() const
{
    if (!m_land.isEmpty())
        return m_land;
    QFile file(QStringLiteral(":/decolog/map/land.json"));
    if (!file.open(QIODevice::ReadOnly))
        return m_land;
    const QJsonArray rings = QJsonDocument::fromJson(file.readAll()).object()
                                 .value(QStringLiteral("rings")).toArray();
    for (const QJsonValue& ring : rings)
        m_land.append(QVariant(ring.toArray().toVariantList()));
    return m_land;
}

QVariantList DecoLogController::coastline() const
{
    if (!m_coastline.isEmpty())
        return m_coastline;
    QFile file(QStringLiteral(":/decolog/map/coastline.json"));
    if (!file.open(QIODevice::ReadOnly))
        return m_coastline;
    const QJsonArray lines = QJsonDocument::fromJson(file.readAll()).object()
                                 .value(QStringLiteral("lines")).toArray();
    for (const QJsonValue& line : lines) {
        // append, non <<: con una lista l'operatore concatena, e le coste
        // diventerebbero duemila punti sciolti invece di centotrentaquattro linee.
        m_coastline.append(QVariant(line.toArray().toVariantList()));
    }
    return m_coastline;
}

namespace {

// Le ventiquattro ore, anche quelle vuote: un buco nel grafico e' un dato.
QVariantList allHours(const QList<CountRow>& hours)
{
    QVariantList out;
    for (int h = 0; h < 24; ++h) {
        const QString key = QStringLiteral("%1").arg(h, 2, 10, QLatin1Char('0'));
        int count = 0;
        for (const CountRow& r : hours) {
            if (r.key == key)
                count = r.count;
        }
        out << QVariantMap{{QStringLiteral("key"), key}, {QStringLiteral("count"), count}};
    }
    return out;
}

QVariantList mapsToList(const QList<QVariantMap>& rows)
{
    QVariantList out;
    for (const QVariantMap& row : rows)
        out << row;
    return out;
}

// Tutto quello che mostra la finestra delle statistiche, dalle due pagine.
QVariantMap computeStats(const LogDatabase& db, const QString& mode, int year)
{
    const StatsFilter f = statsFilterFor(mode, year);
    return {
        {QStringLiteral("mode"), mode},
        {QStringLiteral("year"), year},
        {QStringLiteral("summary"), db.statsSummary(f)},
        {QStringLiteral("years"), db.yearsInLog()},
        {QStringLiteral("byYear"), rowsToList(db.countByYear(statsFilterFor(mode, 0)))},
        {QStringLiteral("byMonth"), rowsToList(db.countByMonth(24, statsFilterFor(mode, 0)))},
        {QStringLiteral("byHour"), allHours(db.countByHour(f))},
        {QStringLiteral("byBand"), rowsToList(db.countByBand(f))},
        {QStringLiteral("byMode"), rowsToList(db.countByMode(statsFilterFor({}, year)))},
        {QStringLiteral("byContinent"), rowsToList(db.countByContinent(f))},
        {QStringLiteral("bandHour"), mapsToList(db.bandByHour(f))},
        {QStringLiteral("progress"), mapsToList(db.awardProgress(statsFilterFor(mode, 0)))},
        {QStringLiteral("entities"), rowsToList(db.countByEntity(f, 15))},
        {QStringLiteral("calls"), rowsToList(db.countByCall(f, 15))},
        {QStringLiteral("bandMode"), mapsToList(db.bandByMode(statsFilterFor(QString(), year)))},
    };
}

} // namespace

void DecoLogController::requestStats(const QString& mode, int year)
{
    const int request = ++(*m_statsLatest);
    const auto latest = m_statsLatest;
    const QString path = m_db.path();
    QPointer<DecoLogController> self(this);
    // Un log di prova in memoria si legge qui, ma la risposta arriva lo stesso
    // dopo, come per quello vero.
    if (path.isEmpty() || path == QLatin1String(":memory:")) {
        const QVariantMap stats = computeStats(m_db, mode, year);
        QMetaObject::invokeMethod(
            this, [self, stats] {
                if (self)
                    emit self->statsReady(stats);
            },
            Qt::QueuedConnection);
        return;
    }
    m_statsViewPool.start([self, path, mode, year, request, latest] {
        // Chi ha cambiato filtro nel frattempo non vuole piu' questo conto.
        if (request != latest->load())
            return;
        QVariantMap stats;
        {
            LogDatabase db;
            if (db.open(path))
                stats = computeStats(db, mode, year);
        }
        QMetaObject::invokeMethod(
            self.data(), [self, stats, request, latest] {
                if (self && request == latest->load())
                    emit self->statsReady(stats);
            },
            Qt::QueuedConnection);
    });
}

QStringList DecoLogController::statsYears() const
{
    return m_db.yearsInLog();
}

QVariantMap DecoLogController::statsSummary(const QString& mode, int year) const
{
    return m_db.statsSummary(statsFilterFor(mode, year));
}

QVariantList DecoLogController::statsByYear(const QString& mode) const
{
    return rowsToList(m_db.countByYear(statsFilterFor(mode, 0)));
}

QVariantList DecoLogController::statsByMonth(int months, const QString& mode) const
{
    return rowsToList(m_db.countByMonth(months, statsFilterFor(mode, 0)));
}

QVariantList DecoLogController::statsByHour(const QString& mode, int year) const
{
    // Tutte le ventiquattro ore, anche quelle vuote: un buco nel grafico e' un
    // dato, non un'assenza.
    QList<CountRow> hours = m_db.countByHour(statsFilterFor(mode, year));
    QVariantList out;
    for (int h = 0; h < 24; ++h) {
        const QString key = QStringLiteral("%1").arg(h, 2, 10, QLatin1Char('0'));
        int count = 0;
        for (const CountRow& r : hours) {
            if (r.key == key)
                count = r.count;
        }
        out << QVariantMap{{QStringLiteral("key"), key}, {QStringLiteral("count"), count}};
    }
    return out;
}

QVariantList DecoLogController::statsByBand(const QString& mode, int year) const
{
    return rowsToList(m_db.countByBand(statsFilterFor(mode, year)));
}

QVariantList DecoLogController::statsByMode(int year) const
{
    return rowsToList(m_db.countByMode(statsFilterFor({}, year)));
}

QVariantList DecoLogController::statsByContinent(const QString& mode, int year) const
{
    return rowsToList(m_db.countByContinent(statsFilterFor(mode, year)));
}

QVariantList DecoLogController::statsTopEntities(const QString& mode, int year, int limit) const
{
    return rowsToList(m_db.countByEntity(statsFilterFor(mode, year), limit));
}

QVariantList DecoLogController::statsTopCalls(const QString& mode, int year, int limit) const
{
    return rowsToList(m_db.countByCall(statsFilterFor(mode, year), limit));
}

QVariantList DecoLogController::statsBandMode(int year) const
{
    QVariantList out;
    for (const QVariantMap& row : m_db.bandByMode(statsFilterFor(QString(), year)))
        out << row;
    return out;
}

QVariantList DecoLogController::statsAwardProgress(const QString& mode) const
{
    QVariantList out;
    for (const QVariantMap& row : m_db.awardProgress(statsFilterFor(mode, 0)))
        out << row;
    return out;
}

QVariantList DecoLogController::statsBandHour(const QString& mode, int year) const
{
    QVariantList out;
    for (const QVariantMap& row : m_db.bandByHour(statsFilterFor(mode, year)))
        out << row;
    return out;
}

QVariantList DecoLogController::gridPoints() const
{
    decolog::StartupSpan trace("DecoLogController::gridPoints");
    // QML sequence access can call this getter for each marker/element.
    // Never run a full SQLite scan on each property read, including empty logs.
    if (m_gridPointsValid)
        return m_gridPointsCache;
    QVariantList out;
    for (const QString& grid : m_db.workedGrids()) {
        if (const auto p = maidenhead::toLatLon(grid))
            out << positionMap(p);
    }
    m_gridPointsCache = out;
    m_gridPointsValid = true;
    return m_gridPointsCache;
}

void DecoLogController::setLookupCall(const QString& call)
{
    const QString c = call.trimmed().toUpper();
    if (c == m_lookupCall)
        return;
    m_lookupCall = c;
    refreshCallInfo();
    if (m_callbook.provider() != CallbookClient::Provider::None)
        m_callbookDebounce.start();
}

void DecoLogController::requestCallbook()
{
    // Almeno una lettera e una cifra: "EA" o "123" a meta' digitazione non si cercano.
    static const QRegularExpression letter(QStringLiteral("[A-Z]"));
    static const QRegularExpression digit(QStringLiteral("[0-9]"));
    const QString call = m_lookupCall;
    if (m_callbook.provider() == CallbookClient::Provider::None || call.size() < 3
        || !call.contains(letter) || !call.contains(digit) || m_callbookResults.contains(call))
        return;
    m_callbookPending = call;
    emit callbookChanged();
    m_callbook.lookup(call);
}

void DecoLogController::setCallbookProvider(const QString& id)
{
    const auto provider = CallbookClient::providerFromId(id);
    if (provider == m_callbook.provider())
        return;
    m_callbook.setProvider(provider);
    m_callbookResults.clear();
    m_callbookErrors.clear();
    m_callbookStatus.clear();
    QSettings().setValue(QStringLiteral("callbook/provider"), CallbookClient::providerId(provider));
    emit callbookChanged();
    refreshCallInfo();
    requestCallbook();
}

void DecoLogController::setCallbookFallback(bool enabled)
{
    if (enabled == m_callbookFallback)
        return;
    m_callbookFallback = enabled;
    m_callbook.setFallbackEnabled(enabled);
    QSettings().setValue(QStringLiteral("callbook/fallback"), enabled);
    emit callbookChanged();
}

void DecoLogController::setCallbookComplete(bool complete)
{
    if (complete == m_callbookComplete)
        return;
    m_callbookComplete = complete;
    QSettings().setValue(QStringLiteral("callbook/completeLogged"), complete);
    emit callbookChanged();
}

int DecoLogController::completeQsoFromCallbook(qint64 id)
{
    const auto record = m_db.record(id);
    if (!record)
        return 0;
    const QString call = record->value(QStringLiteral("CALL")).toUpper();
    if (call.isEmpty() || m_callbook.provider() == CallbookClient::Provider::None)
        return 0;

    // A comando si fa comunque, anche se il completamento automatico e' spento.
    if (const auto it = m_callbookResults.constFind(call); it != m_callbookResults.constEnd())
        return applyCallbookToQso(id, *it).isEmpty() ? 0 : 1;

    m_awaitingCallbook[call].append(id);
    m_callbook.lookup(call);
    return 1;
}

int DecoLogController::completeShownFromCallbook()
{
    // Le righe che si stanno guardando, non tutto il log: una ricerca per
    // nominativo costa, e l'abbonamento ha un limite.
    int asked = 0;
    QSet<QString> seen;
    for (const QVariant& value : m_model->shownIds()) {
        const qint64 id = value.toLongLong();
        const auto record = m_db.record(id);
        if (!record)
            continue;
        // Solo quelli a cui manca qualcosa: gli altri sono gia' a posto.
        if (!record->value(QStringLiteral("NAME")).isEmpty()
            && !record->value(QStringLiteral("GRIDSQUARE")).isEmpty()) {
            continue;
        }
        const QString call = record->value(QStringLiteral("CALL")).toUpper();
        if (call.isEmpty() || seen.contains(call))
            continue;
        seen.insert(call);
        if (completeQsoFromCallbook(id) > 0)
            ++asked;
        if (asked >= 50)
            break;   // un blocco per volta: si ripete, non si esagera
    }
    if (asked > 0) {
        addActivity(QStringLiteral("CALLBOOK"),
                    tr("Completing %n QSO from the callbook…", nullptr, asked),
                    QStringLiteral("info"));
    }
    return asked;
}

int DecoLogController::completeMissingFromCallbook()
{
    const QList<qint64> ids = m_db.idsMissingCallbookData();
    if (ids.isEmpty() && m_callbookQueue.isEmpty()) {
        addActivity(QStringLiteral("CALLBOOK"), tr("Every QSO already has its grid."),
                    QStringLiteral("info"));
        return 0;
    }
    return enqueueCallbook(ids);
}

int DecoLogController::enqueueCallbook(const QList<qint64>& ids)
{
    if (m_callbook.provider() == core::CallbookClient::Provider::None || ids.isEmpty())
        return 0;
    if (m_callbookQueue.isEmpty()) {
        m_callbookQueueDone = 0;
        m_callbookQueueTotal = 0;
    }
    m_callbookQueue += ids;
    m_callbookQueueTotal += static_cast<int>(ids.size());
    addActivity(QStringLiteral("CALLBOOK"),
                tr("%n QSO to complete from the callbook: one search at a time, it takes a while.",
                   nullptr, static_cast<int>(ids.size())),
                QStringLiteral("info"));
    m_callbookQueueTimer.start();
    emit callbookChanged();
    return static_cast<int>(ids.size());
}

void DecoLogController::stopCallbookQueue()
{
    if (m_callbookQueue.isEmpty() && !m_callbookQueueTimer.isActive())
        return;
    m_callbookQueue.clear();
    m_callbookQueueTimer.stop();
    addActivity(QStringLiteral("CALLBOOK"),
                tr("Stopped: %1 of %2 QSO done.").arg(m_callbookQueueDone).arg(m_callbookQueueTotal),
                QStringLiteral("warning"));
    emit callbookChanged();
}

void DecoLogController::serveCallbookQueue()
{
    // Un giro serve un QSO che ha bisogno della rete; quelli che il callbook ha
    // gia' in tasca si fanno tutti insieme, perche' non costano niente.
    int localOnes = 0;
    while (!m_callbookQueue.isEmpty()) {
        const qint64 id = m_callbookQueue.takeFirst();
        ++m_callbookQueueDone;
        const auto record = m_db.record(id);
        if (!record)
            continue;
        const QString call = record->value(QStringLiteral("CALL")).toUpper();
        const bool cached = m_callbookResults.contains(call);
        completeQsoFromCallbook(id);
        if (!cached)
            break;
        if (++localOnes >= 50)
            break;
    }
    if (m_callbookQueue.isEmpty()) {
        m_callbookQueueTimer.stop();
        addActivity(QStringLiteral("CALLBOOK"),
                    tr("Callbook: %1 QSO looked at.").arg(m_callbookQueueDone),
                    QStringLiteral("success"));
    } else if (m_callbookQueueDone % 50 == 0) {
        addActivity(QStringLiteral("CALLBOOK"),
                    tr("Callbook: %1 of %2…").arg(m_callbookQueueDone).arg(m_callbookQueueTotal),
                    QStringLiteral("info"));
    }
    emit callbookChanged();
}

int DecoLogController::damagedFieldCount() const
{
    return static_cast<int>(m_db.idsWithDamagedText().size());
}

int DecoLogController::repairImportedFields()
{
    const QList<qint64> ids = m_db.idsWithDamagedText();
    QList<qint64> emptied;
    int repaired = 0;
    for (const qint64 id : ids) {
        const auto record = m_db.record(id);
        if (!record)
            continue;
        AdifRecord fixed = *record;
        bool changed = false;
        bool emptiedHere = false;
        for (const char* name : {"NAME", "QTH", "ADDRESS", "COMMENT", "NOTES", "COUNTRY", "QSL_VIA"}) {
            const QString value = fixed.value(QLatin1String(name));
            if (value.isEmpty())
                continue;
            const QString clean = core::adif::repairTruncated(value);
            if (clean == value)
                continue;
            fixed.set(QLatin1String(name), clean);
            changed = true;
            if (clean.isEmpty())
                emptiedHere = true;
        }
        if (!changed)
            continue;
        if (m_db.updateQso(id, fixed, -1, QStringLiteral("repair")).status == InsertResult::Status::Inserted) {
            ++repaired;
            if (emptiedHere)
                emptied << id;
        }
    }
    if (repaired > 0) {
        addActivity(QStringLiteral("LOG"),
                    tr("%n QSO cleaned up from a bad old import (the previous text stays in the history).",
                       nullptr, repaired),
                    QStringLiteral("success"));
        timed(tr("reloading the log table"), [this] { m_model->reload(); });
        emit logChanged();
        // Quello che si e' dovuto svuotare lo riscrive il callbook, se lo sa.
        enqueueCallbook(emptied);
    }
    return repaired;
}

void DecoLogController::setCallbookAutofill(bool autofill)
{
    if (autofill == m_callbookAutofill)
        return;
    m_callbookAutofill = autofill;
    QSettings().setValue(QStringLiteral("callbook/autofill"), autofill);
    emit callbookChanged();
}

void DecoLogController::refreshCallInfo()
{
    const WorkedBefore wb = m_db.workedBefore(m_lookupCall);
    QVariantList recent;
    for (const WorkedEntry& e : wb.recent) {
        recent << QVariantMap{
            {QStringLiteral("date"), e.on.toString(dates::format())},
            {QStringLiteral("band"), e.band},
            {QStringLiteral("mode"), e.mode},
            {QStringLiteral("lotw"), e.lotwRcvd == QLatin1String("Y")},
        };
    }

    QVariantMap info{
        {QStringLiteral("call"), m_lookupCall},
        {QStringLiteral("count"), wb.count},
        {QStringLiteral("bands"), wb.bands},
        {QStringLiteral("modes"), wb.modes},
        {QStringLiteral("last"), wb.last.isValid() ? wb.last.toString(dates::format() + QStringLiteral(" HH:mm")) : QString()},
        {QStringLiteral("lastBand"), wb.lastBand},
        {QStringLiteral("lastMode"), wb.lastMode},
        {QStringLiteral("lastId"), wb.lastId},
        {QStringLiteral("name"), wb.name},
        {QStringLiteral("qth"), wb.qth},
        {QStringLiteral("gridsquare"), wb.gridsquare},
        {QStringLiteral("country"), wb.country},
        {QStringLiteral("state"), wb.state},
        {QStringLiteral("dxcc"), wb.dxcc},
        {QStringLiteral("cqz"), wb.cqz},
        {QStringLiteral("ituz"), wb.ituz},
        {QStringLiteral("recent"), recent},
        {QStringLiteral("workedFt2"), wb.modes.contains(QStringLiteral("FT2"))},
        {QStringLiteral("slots"), slotGrid(m_db.bandModeSlotsForCall(m_lookupCall))},
    };

    // Il callbook completa quello che il log non sa: nome, QTH, locatore. Quello
    // che c'e' nel log resta, perche' e' quello che l'operatore ha confermato.
    if (const auto it = m_callbookResults.constFind(m_lookupCall); it != m_callbookResults.constEnd()) {
        const QVariantMap& cb = *it;
        info[QStringLiteral("callbook")] = cb;
        auto prefer = [&info](const char* key, const QVariant& value) {
            if (info.value(QLatin1String(key)).toString().isEmpty() && !value.toString().isEmpty())
                info[QLatin1String(key)] = value;
        };
        prefer("name", cb.value(QStringLiteral("name")));
        prefer("qth", cb.value(QStringLiteral("qth")));
        prefer("gridsquare", cb.value(QStringLiteral("grid")));
        prefer("country", cb.value(QStringLiteral("country")));
        prefer("state", cb.value(QStringLiteral("state")));
        if (wb.cqz == 0 && cb.value(QStringLiteral("cqZone")).toInt() > 0)
            info[QStringLiteral("cqz")] = cb.value(QStringLiteral("cqZone"));
        if (wb.ituz == 0 && cb.value(QStringLiteral("ituZone")).toInt() > 0)
            info[QStringLiteral("ituz")] = cb.value(QStringLiteral("ituZone"));
    } else if (m_callbookErrors.contains(m_lookupCall)) {
        info[QStringLiteral("callbookError")] = m_callbookErrors.value(m_lookupCall);
    }
    if (m_callbook.provider() != CallbookClient::Provider::None)
        info[QStringLiteral("callbookSource")] = m_callbook.provider() == CallbookClient::Provider::Qrz
                                                     ? QStringLiteral("QRZ.com") : QStringLiteral("HamQTH");

    // L'entita' dal nominativo: vale anche per chi non e' ancora nel log.
    std::optional<maidenhead::LatLon> dx = maidenhead::toLatLon(info.value(QStringLiteral("gridsquare")).toString());
    if (const auto e = m_countries.lookup(m_lookupCall)) {
        info[QStringLiteral("entity")] = e->name;
        info[QStringLiteral("entityDxcc")] = e->dxcc;
        info[QStringLiteral("entityCont")] = e->continent;
        if (const auto s = entitySummary(e->dxcc)) {
            info[QStringLiteral("entityWorked")] = s->worked.count;
            info[QStringLiteral("entityBands")] = s->worked.bands;
            info[QStringLiteral("entityModes")] = s->worked.modes;
            info[QStringLiteral("entitySlots")] = slotGrid(s->cells);
        } else {
            // Si stanno contando: la scheda si completa da sola fra un attimo, e
            // intanto non dice "nuovo DXCC" a vuoto.
            info[QStringLiteral("entityCounting")] = true;
        }
        if (info.value(QStringLiteral("country")).toString().isEmpty())
            info[QStringLiteral("country")] = e->name;
        if (info.value(QStringLiteral("cqz")).toInt() == 0)
            info[QStringLiteral("cqz")] = e->cqZone;
        if (info.value(QStringLiteral("ituz")).toInt() == 0)
            info[QStringLiteral("ituz")] = e->ituZone;
        if (!dx) {
            // Senza locatore la posizione e' il centro dell'entita': basta per
            // l'azimut, la distanza e' indicativa.
            dx = maidenhead::LatLon{e->lat, e->lon};
            info[QStringLiteral("positionApprox")] = true;
        }
    }
    const auto me = maidenhead::toLatLon(myGrid());
    if (dx) {
        info[QStringLiteral("position")] = positionMap(dx);
        // Ora locale approssimata dal fuso "solare": basta per capire se dall'altra
        // parte e' notte fonda.
        info[QStringLiteral("utcOffsetHours")] = static_cast<int>(std::lround(dx->lon / 15.0));
    }
    if (dx && me) {
        info[QStringLiteral("distanceKm")] = qRound(maidenhead::distanceKm(*me, *dx));
        info[QStringLiteral("azimuth")] = qRound(maidenhead::azimuthDeg(*me, *dx));
    }

    if (wb.lastId > 0) {
        QVariantList qsl;
        const QList<QslState> states = m_db.qslStatus(wb.lastId);
        for (const QString& service : kServices) {
            QString sent = QStringLiteral("N"), rcvd = QStringLiteral("N");
            for (const auto& s : states) {
                if (s.service == service) {
                    sent = s.sent;
                    rcvd = s.rcvd;
                }
            }
            qsl << QVariantMap{{QStringLiteral("label"), serviceLabel(service)},
                               {QStringLiteral("sent"), sent},
                               {QStringLiteral("rcvd"), rcvd}};
        }
        info[QStringLiteral("qsl")] = qsl;
    }
    m_callInfo = info;
    // Se il rotore deve seguire quello che si lavora, questa e' la rotta buona.
    if (m_rotor && info.contains(QStringLiteral("azimuth"))) {
        m_rotor->dxBearing(info.value(QStringLiteral("call")).toString(),
                           info.value(QStringLiteral("azimuth")).toDouble());
    } else if (m_rotor && dx) {
        // Senza il QTH della stazione, la rotta si conta da quello del
        // gateway del rotore: "Punta il DX" funziona lo stesso.
        m_rotor->dxBearing(info.value(QStringLiteral("call")).toString(), m_rotor->bearingTo(dx->lat, dx->lon));
    }
    emit lookupChanged();
}

std::optional<DecoLogController::EntitySummary> DecoLogController::entitySummary(int dxcc)
{
    const QString dbPath = m_db.path();
    // Un log di prova in memoria non si apre da un altro filo: si conta qui.
    if (dbPath.isEmpty() || dbPath == QLatin1String(":memory:"))
        return EntitySummary{m_logVersion, m_db.dxccWorked(dxcc), m_db.bandModeSlotsForDxcc(dxcc)};
    const auto it = m_entitySummaries.constFind(dxcc);
    const bool fresh = it != m_entitySummaries.constEnd() && it->version == m_logVersion;
    if (!fresh && !m_entityCounting.contains(dxcc)) {
        m_entityCounting.insert(dxcc);
        const quint64 version = m_logVersion;
        QPointer<DecoLogController> self(this);
        m_callInfoPool.start([self, dbPath, dxcc, version] {
            LogDatabase db;
            const bool ok = db.open(dbPath);
            EntitySummary s;
            s.version = version;
            if (ok) {
                s.worked = db.dxccWorked(dxcc);
                s.cells = db.bandModeSlotsForDxcc(dxcc);
            }
            QMetaObject::invokeMethod(
                self.data(), [self, dxcc, s, ok] {
                    if (!self)
                        return;
                    self->m_entityCounting.remove(dxcc);
                    if (!ok)
                        return;
                    self->m_entitySummaries.insert(dxcc, s);
                    // La scheda aperta su quell'entita' si completa.
                    if (self->m_callInfo.value(QStringLiteral("entityDxcc")).toInt() == dxcc)
                        self->refreshCallInfo();
                },
                Qt::QueuedConnection);
        });
    }
    if (it == m_entitySummaries.constEnd())
        return std::nullopt;
    return *it;
}

void DecoLogController::addActivity(const QString& category, const QString& text, const QString& level)
{
    if (!m_activityModel)
        m_activityModel = new ActivityModel(kMaxActivity, this);
    m_activityModel->add(nowUtcLabel(), category, text, level);
    // Per le prove dall'esterno (tst_udppipeline): il registro attivita' anche
    // su stderr, una riga per voce.
    static const bool trace = qEnvironmentVariableIntValue("DECODXLOG_TRACE_ACTIVITY") == 1;
    if (trace)
        fprintf(stderr, "[activity] %s|%s|%s\n", qPrintable(category), qPrintable(level), qPrintable(text));
}

void DecoLogController::clearActivity()
{
    if (m_activityModel)
        m_activityModel->clear();
}

} // namespace decolog::app
