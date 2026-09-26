#include "app/DecoLogController.h"

#include "core/Bands.h"
#include "core/Dates.h"
#include "core/Maidenhead.h"
#include "core/Modes.h"
#include "core/Spots.h"
#include "ThemeManager.h"

#include <QMetaObject>
#include <QPointer>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QWindow>
#include <QFileInfo>
#include <QHostAddress>
#include <QLocale>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QRegularExpression>
#include <QSettings>
#include <QSqlDatabase>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>
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

const QStringList kServices{QStringLiteral("lotw"), QStringLiteral("qrz"), QStringLiteral("clublog"),
                            QStringLiteral("eqsl"), QStringLiteral("card")};

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
    QSettings s;
    m_udpPort = s.value(QStringLiteral("udp/port"), 2237).toInt();
    m_multicast = s.value(QStringLiteral("udp/multicastGroup")).toString();
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
    m_awardFilter.stationProfileId = s.value(QStringLiteral("awards/profile"), 0).toLongLong();
    m_awardFilter.tag = s.value(QStringLiteral("awards/tag")).toString();
    // Gli award si ricalcolano quando il log cambia, e solo quando qualcuno li guarda.
    // I diplomi e le statistiche di tutto il log si rifanno quando si smette di
    // scrivere, non a ogni QSO: rifarli subito voleva dire tenere il programma
    // fermo proprio mentre si registra, che in gara e' il momento peggiore.
    m_statsDebounce.setSingleShot(true);
    m_statsDebounce.setInterval(1200);
    m_statsPool.setMaxThreadCount(1);
    connect(&m_statsDebounce, &QTimer::timeout, this, [this] {
        // Lo stato dei diplomi per DecoLink resta a richiesta: lo calcola chi lo
        // chiede, e lo chiede di rado.
        m_globalAwardsDirty = true;
        refreshStatsInBackground();
    });
    connect(this, &DecoLogController::logChanged, this, [this] { m_statsDebounce.start(); });
    // DecoLink: il log verso Decodium. I dati li fornisce il controller.
    m_decoLinkEnabled = s.value(QStringLiteral("decolink/enabled"), true).toBool();
    m_decoLinkPort = s.value(QStringLiteral("decolink/port"), DecoLinkServer::kDefaultPort).toInt();
    m_decoLink.workedRows = [this] {
        return m_db.workedRows(m_awardFilter.confirmLotw, m_awardFilter.confirmCard, m_awardFilter.confirmEqsl);
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
    // Lo stato dell'award non a ogni QSO di un import: al piu' uno al secondo.
    m_decoLinkAwardDebounce.setSingleShot(true);
    m_decoLinkAwardDebounce.setInterval(1000);
    connect(&m_decoLinkAwardDebounce, &QTimer::timeout, this, [this] { m_decoLink.broadcastAward(); });
    connect(this, &DecoLogController::logChanged, this, [this] {
        if (m_decoLink.clientCount() > 0)
            m_decoLinkAwardDebounce.start();
    });

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
    connect(&m_lotw, &LotwClient::finished, this, &DecoLogController::onLotwReport);
    connect(&m_lotw, &LotwClient::progress, this, [this](qint64 bytes) {
        m_lotwStatus = tr("LoTW: downloading… %1 kB").arg(bytes / 1024);
        emit lotwChanged();
    });
    // Il sync automatico si controlla ogni dieci minuti; il primo poco dopo
    // l'avvio, quando la finestra e' gia' su.
    m_lotwTimer.setInterval(10 * 60'000);
    connect(&m_lotwTimer, &QTimer::timeout, this, &DecoLogController::checkLotwSchedule);
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
    m_decoLink.stop();
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
    QPointer<DecoLogController> self(this);

    m_statsPool.start([self, path, filter, names, generation] {
        LogDatabase db;
        if (!db.open(path))
            return;
        const AwardCalculator calc([&names](int dxcc) { return names.value(dxcc); });
        const QList<AwardResult> awards = calc.compute(db, filter);
        const Ft2Award ft2 = db.ft2Award();
        const QList<CountRow> bands = db.countByBand();
        const QList<CountRow> modes = db.countByMode();
        db.close();

        QMetaObject::invokeMethod(
            self.data(),
            [self, filter, awards, ft2, bands, modes, generation] {
                if (!self || generation != self->m_statsGeneration)
                    return;
                const AwardFilter& now = self->m_awardFilter;
                const bool sameFilter = now.band == filter.band && now.modeGroup == filter.modeGroup
                                        && now.confirmLotw == filter.confirmLotw
                                        && now.confirmCard == filter.confirmCard
                                        && now.confirmEqsl == filter.confirmEqsl
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
            },
            Qt::QueuedConnection);
    });
}

bool DecoLogController::openDatabase(const QString& path)
{
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

    QslController::Context qslCtx;
    qslCtx.db = &m_db;
    qslCtx.credentials = m_credentials;
    qslCtx.stationLocation = [this] {
        return m_profiles->activeProfile().value(QStringLiteral("lotwStationLocation")).toString();
    };
    qslCtx.stationCallsign = [this] {
        return m_profiles->activeProfile().value(QStringLiteral("stationCallsign")).toString();
    };
    qslCtx.activity = [this](const QString& category, const QString& text, const QString& level) {
        addActivity(category, text, level);
    };
    qslCtx.logChanged = [this] {
        timed(tr("reloading the log table"), [this] { m_model->reload(); });
        emit logChanged();
    };
    m_qsl = new QslController(std::move(qslCtx), this);

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

    RigController::Context rigCtx;
    rigCtx.activity = [this](const QString& category, const QString& text, const QString& level) {
        addActivity(category, text, level);
    };
    rigCtx.stationCallsign = [this] {
        return m_profiles ? m_profiles->activeProfile().value(QStringLiteral("stationCallsign")).toString()
                          : QString();
    };
    m_rig = new RigController(std::move(rigCtx), this);
    // Il VFO che si muove e' una notizia quanto un QSO: il Cloud lo sappia.
    // Il CloudController manda al massimo una volta ogni venti secondi, quindi
    // girare la manopola non intasa niente.
    connect(m_rig, SIGNAL(stateChanged()), this, SLOT(reportPresenceToCloud()));
    // La barra in cima mostra la radio: quando la radio si muove, si rifa'.
    connect(m_rig, SIGNAL(stateChanged()), this, SIGNAL(tuningChanged()));
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
    m_activation = new ActivationController(std::move(actCtx), this);
    // Un QSO corretto, cancellato o importato cambia il punteggio della gara:
    // quello in memoria non vale piu'. Collegato qui, prima delle finestre,
    // cosi' le finestre leggono gia' il conto nuovo.
    connect(this, &DecoLogController::logChanged, m_activation,
            [this] { m_activation->invalidateScore(); });
    m_activation->load();
    connect(this, &DecoLogController::logChanged, m_cluster, &ClusterController::logChanged);
    connect(this, &DecoLogController::countriesChanged, m_cluster, &ClusterController::logChanged);
    connect(this, &DecoLogController::clientChanged, m_cluster, &ClusterController::decodiumBandChanged);

    m_backupTimer.start();
    m_lotwTimer.start();
    QTimer::singleShot(30'000, this, &DecoLogController::checkLotwSchedule);
    return ok;
}

void DecoLogController::startCluster()
{
    if (m_cluster)
        m_cluster->start();
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
    const Ft2Award ft2 = m_db.ft2Award();
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
        {QStringLiteral("home"), QStringLiteral("https://github.com/iu8lmc/DecoDXLog")},
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
        addActivity(QStringLiteral("APP"),
                    tr("The window stopped answering for %1 s (%n time(s) since the start)",
                       nullptr, m_freezeCount)
                        .arg(QString::number(late / 1000.0, 'f', 1)),
                    QStringLiteral("warning"));
    });
    m_freezeBeat.start();
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
    return tr("built on %1").arg(QLocale::c().toDate(QString::fromLatin1(__DATE__).simplified(),
                                                     QStringLiteral("MMM d yyyy")).toString(Qt::ISODate));
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
    auto* rig = qobject_cast<RigController*>(m_rig);
    if (rig && rig->connected() && rig->frequencyHz() > 0)
        return QString::number(static_cast<double>(rig->frequencyHz()) / 1e6, 'f', 6);
    return dialFrequency();
}

QString DecoLogController::shownMode() const
{
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
    if (m_awardsDirty && m_db.isOpen()) {
        const AwardCalculator calc([this](int dxcc) { return m_countries.nameFor(dxcc); });
        m_awardCache = calc.compute(m_db, m_awardFilter);
        m_awardsDirty = false;
    }
    return m_awardCache;
}

const QList<AwardResult>& DecoLogController::globalAwardResults() const
{
    if (m_globalAwardsDirty && m_db.isOpen()) {
        AwardFilter filter;
        filter.confirmLotw = m_awardFilter.confirmLotw;
        filter.confirmCard = m_awardFilter.confirmCard;
        filter.confirmEqsl = m_awardFilter.confirmEqsl;
        const AwardCalculator calc([this](int dxcc) { return m_countries.nameFor(dxcc); });
        m_globalAwardCache = calc.compute(m_db, filter);
        m_globalAwardsDirty = false;
    }
    return m_globalAwardCache;
}

QVariantList DecoLogController::awardSummary() const
{
    QVariantList out;
    const QStringList bands = awardBands();
    for (const AwardResult& r : awardResults()) {
        int slotsWorked = 0, slotsConfirmed = 0;
        for (const BandTotal& t : r.bandTotals(bands)) {
            slotsWorked += t.worked;
            slotsConfirmed += t.confirmed;
        }
        out << QVariantMap{
            {QStringLiteral("id"), r.id},
            {QStringLiteral("title"), r.title},
            {QStringLiteral("worked"), r.worked()},
            {QStringLiteral("confirmed"), r.confirmed()},
            // Il traguardo del WAAC non e' un numero inventato: sono tutte le
            // entita' africane che il cty.csv conosce.
            {QStringLiteral("target"), r.id == QLatin1String("waac") && africanEntities() > 0
                                           ? africanEntities() : r.target},
            {QStringLiteral("total"), r.id == QLatin1String("dxcc") && m_countries.entityCount() > 0
                                          ? m_countries.entityCount()
                                          : r.id == QLatin1String("waac") ? africanEntities() : r.total},
            {QStringLiteral("slotsWorked"), slotsWorked},
            {QStringLiteral("slotsConfirmed"), slotsConfirmed},
            // Quello che il regolamento chiede oltre al numero: vuoto per quasi
            // tutti, una riga per chi ne ha (il DCI vuole anche le regioni).
            {QStringLiteral("requirement"), r.requirement},
        };

        // Il DXCC Challenge non e' un altro elenco di entita': sono gli stessi
        // DXCC contati banda per banda, dai 160 ai 6 metri (undici bande, 60
        // compresi). Mille slot e' il traguardo del primo riconoscimento.
        if (r.id == QLatin1String("dxcc")) {
            static const QStringList challengeBands{
                QStringLiteral("160m"), QStringLiteral("80m"), QStringLiteral("60m"),
                QStringLiteral("40m"), QStringLiteral("30m"), QStringLiteral("20m"),
                QStringLiteral("17m"), QStringLiteral("15m"), QStringLiteral("12m"),
                QStringLiteral("10m"), QStringLiteral("6m")};
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
    QStringList out;
    for (const auto& row : m_db.countByBand())
        out << row.key;
    return out;
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
        for (const BandTotal& t : r.bandTotals(awardBands())) {
            out << QVariantMap{{QStringLiteral("band"), t.band},
                               {QStringLiteral("worked"), t.worked},
                               {QStringLiteral("confirmed"), t.confirmed}};
        }
    }
    return out;
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

void DecoLogController::onQsoReceived(const AdifRecord& input, const QString& source, const QString& sourceApp)
{
    // Il profilo lo indica il nominativo di stazione del QSO; se non corrisponde a
    // nessuno, vale quello attivo. I campi del profilo non si aggiungono: il QSO
    // resta come l'ha mandato Decodium.
    qint64 profileId = m_db.profileForCallsign(input.value(QStringLiteral("STATION_CALLSIGN")));
    if (profileId == 0 && m_profiles)
        profileId = m_profiles->activeProfileId();

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
        {QStringLiteral("message"), m_udp.prefersLoggedAdif() && source.startsWith(QLatin1String("udp"))
                                        ? QStringLiteral("LoggedADIF") : QStringLiteral("QSOLogged")},
        {QStringLiteral("id"), r.id},
    };

    switch (r.status) {
    case InsertResult::Status::Inserted: {
        item[QStringLiteral("status")] = QStringLiteral("logged");
        decoLinkQso(enriched, QStringLiteral("logged"), r.id, source, sourceApp);
        m_qsl->qsoLogged(r.id);
        m_activation->qsoLogged();
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

QString DecoLogController::logManualQso(const QVariantMap& fields)
{
    auto text = [&fields](const char* key) { return fields.value(QLatin1String(key)).toString().trimmed(); };

    AdifRecord r;
    r.set(QStringLiteral("CALL"), text("call").toUpper());
    const QDate date = QDate::fromString(dates::read(text("date")), QStringLiteral("yyyy-MM-dd"));
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
    r.set(QStringLiteral("FREQ"), freq);
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
    // Contest: il numero ricevuto, come lo vuole ADIF.
    if (!text("srx").isEmpty()) {
        r.set(QStringLiteral("SRX"), text("srx"));
        r.set(QStringLiteral("SRX_STRING"), text("srx"));
    }

    qint64 profileId = m_profiles ? m_profiles->activeProfileId() : 0;
    if (m_activation->active() && m_activation->session().stationProfileId > 0)
        profileId = m_activation->session().stationProfileId;
    applyProfile(r, profileId);
    applyEntity(r);
    m_activation->applyTo(r);
    {
        AdifRecord normalized = r;
        adif::normalizeMode(normalized);
        const QString mode = normalized.value(QStringLiteral("SUBMODE")).isEmpty()
                                 ? normalized.value(QStringLiteral("MODE"))
                                 : normalized.value(QStringLiteral("SUBMODE"));
        if (m_activation->isDuplicate(r.value(QStringLiteral("CALL")), r.value(QStringLiteral("BAND")), mode))
            return tr("Already worked in this activation");
    }

    const InsertResult res = m_db.insertQso(r, QStringLiteral("manual"), QStringLiteral("DecoDXLog ") + version(),
                                            true, profileId);
    switch (res.status) {
    case InsertResult::Status::Inserted:
        m_model->insertQso(res.id);
        decoLinkQso(r, QStringLiteral("logged"), res.id, QStringLiteral("manual"), QStringLiteral("DecoDXLog"));
        m_qsl->qsoLogged(res.id);
        m_cloud->qsoLogged();
        m_activation->qsoLogged();
        addActivity(QStringLiteral("LOG"), tr("Logged %1 %2 %3 (manual)")
                                               .arg(r.value(QStringLiteral("CALL")), r.value(QStringLiteral("BAND")),
                                                    r.value(QStringLiteral("MODE"))),
                    QStringLiteral("success"));
        emit logChanged();
        setLookupCall(r.value(QStringLiteral("CALL")));
        refreshCallInfo();
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

    QVariantMap detail{
        {QStringLiteral("id"), id},
        {QStringLiteral("fields"), fields},
        {QStringLiteral("extra"), extra},
        {QStringLiteral("qsl"), qsl},
        {QStringLiteral("history"), history},
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
        const Ft2Award a = m_db.ft2Award();
        detail[QStringLiteral("ft2DxccWorked")] = a.dxccWorked;
    }
    return detail;
}

QString DecoLogController::saveQso(qint64 id, const QVariantMap& fields, qint64 stationProfileId)
{
    AdifRecord r;
    for (auto it = fields.cbegin(); it != fields.cend(); ++it)
        r.set(it.key(), it.value().toString().trimmed());
    const InsertResult res = m_db.updateQso(id, r, stationProfileId);
    if (res.status != InsertResult::Status::Inserted)
        return res.message.isEmpty() ? tr("Cannot save the QSO") : res.message;
    // Dove il QSO si puo' correggere (CRX), la correzione va anche li'.
    m_db.queueRemoteEdit(id);
    m_qsl->qsoLogged(id);
    const auto meta = m_db.meta(id);
    addActivity(QStringLiteral("LOG"), tr("Edited %1 · revision %2").arg(r.value(QStringLiteral("CALL"))).arg(meta ? meta->revision : 0),
                QStringLiteral("success"));
    timed(tr("reloading the log table"), [this] { m_model->reload(); });
    emit logChanged();
    m_decoLink.resendSnapshot();
    refreshCallInfo();
    return {};
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

QVariantList DecoLogController::dxccInLog() const
{
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
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        addActivity(QStringLiteral("IMPORT"), tr("Cannot read %1: %2").arg(path, file.errorString()), QStringLiteral("error"));
        return;
    }
    const ImportResult r = m_db.importAdif(file.readAll(), QStringLiteral("import"),
                                           m_profiles ? m_profiles->activeProfileId() : 0);
    addActivity(QStringLiteral("IMPORT"), tr("%1: %2 new, %3 duplicates, %4 rejected")
                                              .arg(QFileInfo(path).fileName()).arg(r.inserted).arg(r.duplicates).arg(r.invalid),
                r.invalid ? QStringLiteral("warning") : QStringLiteral("success"));
    for (const QString& e : r.errors)
        addActivity(QStringLiteral("IMPORT"), QStringLiteral("  ") + e, QStringLiteral("warning"));
    timed(tr("reloading the log table"), [this] { m_model->reload(); });
    m_profiles->reload();
    emit logChanged();
    m_decoLink.resendSnapshot();
    refreshCallInfo();
}

void DecoLogController::exportAdif(const QUrl& url)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        addActivity(QStringLiteral("EXPORT"), tr("Cannot write %1: %2").arg(path, file.errorString()), QStringLiteral("error"));
        return;
    }
    file.write(m_db.exportAdif(version()));
    addActivity(QStringLiteral("EXPORT"), tr("%n QSO → %1", nullptr, m_db.qsoCount()).arg(path), QStringLiteral("success"));
}

void DecoLogController::exportQsos(const QVariantList& ids, const QUrl& url)
{
    QList<qint64> list;
    for (const auto& v : ids)
        list << v.toLongLong();
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        addActivity(QStringLiteral("EXPORT"), tr("Cannot write %1: %2").arg(path, file.errorString()), QStringLiteral("error"));
        return;
    }
    file.write(m_db.exportAdif(list, version()));
    addActivity(QStringLiteral("EXPORT"), tr("%n QSO → %1", nullptr, static_cast<int>(list.size())).arg(path),
                QStringLiteral("success"));
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

void DecoLogController::backupNow()
{
    QDir().mkpath(m_backupDir);
    const QString name = QStringLiteral("decolog-%1.sqlite")
                             .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-ddTHHmm")));
    const QString path = QDir(m_backupDir).filePath(name);
    if (!m_db.backupTo(path)) {
        addActivity(QStringLiteral("BACKUP"), tr("Backup failed: %1").arg(m_db.lastError()), QStringLiteral("error"));
        return;
    }
    m_db.setSetting(QStringLiteral("backup.last_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    m_db.setSetting(QStringLiteral("backup.last_file"), path);

    // Solo le copie fatte da DecoDXLog, dalla piu' recente: le altre non si toccano.
    QFileInfoList copies = QDir(m_backupDir).entryInfoList({QStringLiteral("decolog-*.sqlite")}, QDir::Files, QDir::Name | QDir::Reversed);
    for (qsizetype i = m_backupKeep; i < copies.size(); ++i)
        QFile::remove(copies.at(i).absoluteFilePath());

    addActivity(QStringLiteral("BACKUP"), tr("%1 → %2 (%3 MB)")
                                              .arg(QFileInfo(m_db.path()).fileName(), path,
                                                   QString::number(QFileInfo(path).size() / 1048576.0, 'f', 1)));
    emit backupChanged();
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
        emit lotwChanged();
        return;
    }

    // Quello che era confermato prima, per dire all'operatore cosa c'e' di nuovo.
    const QSet<QString> dxccBefore = confirmedAwardKeys(QStringLiteral("dxcc"));
    const QSet<QString> ft2Before = confirmedAwardKeys(QStringLiteral("ft2"));

    int confirmed = 0, already = 0, notFound = 0, invalid = 0;
    QStringList missing;
    QSqlDatabase db = m_db.connection();
    const bool transaction = db.transaction();
    for (const AdifRecord& c : report.confirmations) {
        const ConfirmationResult r = m_db.applyConfirmation(QStringLiteral("lotw"), c);
        switch (r.status) {
        case ConfirmationResult::Status::Confirmed:        ++confirmed; break;
        case ConfirmationResult::Status::AlreadyConfirmed: ++already; break;
        case ConfirmationResult::Status::NotFound:
            ++notFound;
            if (missing.size() < 10)
                missing << r.message;
            break;
        case ConfirmationResult::Status::Invalid:
        case ConfirmationResult::Status::Error:
            ++invalid;
            break;
        }
    }
    if (transaction)
        db.commit();

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

    if (confirmed > 0) {
        timed(tr("reloading the log table"), [this] { m_model->reload(); });
        m_awardsDirty = m_globalAwardsDirty = true;
        emit logChanged();
        m_decoLink.resendSnapshot();
        refreshCallInfo();
        // I nuovi DXCC confermati meritano una riga a parte.
        for (const QString& key : confirmedAwardKeys(QStringLiteral("dxcc")) - dxccBefore)
            addActivity(QStringLiteral("LOTW"), tr("New DXCC confirmed: %1").arg(m_countries.nameFor(key.toInt())),
                        QStringLiteral("highlight"));
        for (const QString& key : confirmedAwardKeys(QStringLiteral("ft2")) - ft2Before)
            addActivity(QStringLiteral("LOTW"), tr("New FT2 Award entity confirmed: %1").arg(m_countries.nameFor(key.toInt())),
                        QStringLiteral("highlight"));
    }
    emit lotwChanged();
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
    const auto cached = m_statsCache.constFind(QStringLiteral("ft2"));
    if (cached != m_statsCache.constEnd())
        return cached->toMap();
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
    QVariantList out;
    for (const auto& row : m_db.countByMode())
        out << QVariantMap{{QStringLiteral("key"), row.key}, {QStringLiteral("count"), row.count}};
    m_statsCache.insert(QStringLiteral("modes"), out);
    return out;
}

QVariantList DecoLogController::qslSummary() const
{
    QVariantList out;
    for (QVariantMap row : m_db.qslSummary()) {
        row[QStringLiteral("label")] = serviceLabel(row.value(QStringLiteral("service")).toString());
        out << row;
    }
    return out;
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

QVariantList DecoLogController::statsBandHour(const QString& mode, int year) const
{
    QVariantList out;
    for (const QVariantMap& row : m_db.bandByHour(statsFilterFor(mode, year)))
        out << row;
    return out;
}

QVariantList DecoLogController::gridPoints() const
{
    QVariantList out;
    for (const QString& grid : m_db.workedGrids()) {
        if (const auto p = maidenhead::toLatLon(grid))
            out << positionMap(p);
    }
    return out;
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
        const auto worked = m_db.dxccWorked(e->dxcc);
        info[QStringLiteral("entity")] = e->name;
        info[QStringLiteral("entityDxcc")] = e->dxcc;
        info[QStringLiteral("entityCont")] = e->continent;
        info[QStringLiteral("entityWorked")] = worked.count;
        info[QStringLiteral("entityBands")] = worked.bands;
        info[QStringLiteral("entityModes")] = worked.modes;
        info[QStringLiteral("entitySlots")] = slotGrid(m_db.bandModeSlotsForDxcc(e->dxcc));
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
    }
    emit lookupChanged();
}

void DecoLogController::addActivity(const QString& category, const QString& text, const QString& level)
{
    m_activity.prepend(QVariantMap{
        {QStringLiteral("time"), nowUtcLabel()},
        {QStringLiteral("category"), category},
        {QStringLiteral("text"), text},
        {QStringLiteral("level"), level},
    });
    while (m_activity.size() > kMaxActivity)
        m_activity.removeLast();
    emit activityChanged();
}

void DecoLogController::clearActivity()
{
    m_activity.clear();
    emit activityChanged();
}

} // namespace decolog::app
