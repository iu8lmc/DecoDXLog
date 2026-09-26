#include "app/ClusterController.h"

#include <QElapsedTimer>

#include "core/CredentialStore.h"
#include "core/Dates.h"
#include "core/DecoLinkServer.h"
#include "core/LogDatabase.h"
#include "core/Maidenhead.h"
#include "core/NetworkError.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>

namespace decolog::app {

using namespace decolog::core;

namespace {

constexpr int kMaxConsole = 400;

QString lotwUsersPath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("lotw-user-activity.csv"));
}

QVariantList jsonToList(const QString& json)
{
    return QJsonDocument::fromJson(json.toUtf8()).array().toVariantList();
}

QString listToJson(const QVariantList& list)
{
    return QString::fromUtf8(QJsonDocument(QJsonArray::fromVariantList(list)).toJson(QJsonDocument::Compact));
}

// Il continente dello spotter: "EA5WU-#" e "IK8XXX-2" sono EA5WU e IK8XXX.
QString spotterCall(const QString& spotter)
{
    QString c = spotter;
    const qsizetype dash = c.indexOf(QLatin1Char('-'));
    if (dash > 0)
        c = c.left(dash);
    return c;
}

} // namespace

// ── Regole ────────────────────────────────────────────────────────────────────

QVariantMap ClusterController::AlertRule::toMap() const
{
    return {{QStringLiteral("id"), id},           {QStringLiteral("name"), name},
            {QStringLiteral("enabled"), enabled}, {QStringLiteral("voice"), voice},
            {QStringLiteral("decodium"), decodium}, {QStringLiteral("filter"), filter.toMap()}};
}

ClusterController::AlertRule ClusterController::AlertRule::fromMap(const QVariantMap& m)
{
    AlertRule r;
    r.id = m.value(QStringLiteral("id")).toString();
    if (r.id.isEmpty())
        r.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    r.name = m.value(QStringLiteral("name")).toString().trimmed();
    r.enabled = m.value(QStringLiteral("enabled"), true).toBool();
    r.voice = m.value(QStringLiteral("voice"), true).toBool();
    r.decodium = m.value(QStringLiteral("decodium"), true).toBool();
    r.filter = SpotFilter::fromMap(m.value(QStringLiteral("filter")).toMap());
    return r;
}

// ── Avvio e impostazioni ──────────────────────────────────────────────────────

ClusterController::ClusterController(Context context, QObject* parent)
    : QObject(parent)
    , m_ctx(std::move(context))
{
    QSettings s;
    m_filter = SpotFilter::fromMap(QJsonDocument::fromJson(s.value(QStringLiteral("cluster/filter")).toString().toUtf8())
                                       .object().toVariantMap());
    m_followBand = s.value(QStringLiteral("cluster/followDecodiumBand"), false).toBool();
    m_savedFilters = QJsonDocument::fromJson(s.value(QStringLiteral("cluster/savedFilters")).toString().toUtf8())
                         .object().toVariantMap();
    m_sendToDecodium = s.value(QStringLiteral("cluster/sendToDecodium"), true).toBool();
    m_voiceEnabled = s.value(QStringLiteral("cluster/voice/enabled"), true).toBool();
    m_voiceName = s.value(QStringLiteral("cluster/voice/name")).toString();
    m_voiceRate = s.value(QStringLiteral("cluster/voice/rate"), 0).toInt();
    m_voiceVolume = s.value(QStringLiteral("cluster/voice/volume"), 80).toInt();
    m_voicePhonetic = s.value(QStringLiteral("cluster/voice/phonetic"), false).toBool();
    m_voiceLanguage = s.value(QStringLiteral("cluster/voice/language"), QStringLiteral("it")).toString();
    m_voiceCooldown = s.value(QStringLiteral("cluster/voice/cooldownMinutes"), 20).toInt();
    if (!m_voiceName.isEmpty())
        m_voice.setVoice(m_voiceName);
    m_voice.setRate(m_voiceRate);
    m_voice.setVolume(m_voiceVolume);

    if (s.contains(QStringLiteral("cluster/rules"))) {
        for (const QVariant& v : jsonToList(s.value(QStringLiteral("cluster/rules")).toString()))
            m_rules << AlertRule::fromMap(v.toMap());
    } else {
        // Le due regole che quasi tutti vogliono: un DXCC nuovo si dice a voce, una
        // banda o un modo nuovi si segnalano senza parlare.
        AlertRule dxcc;
        dxcc.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        dxcc.name = tr("New DXCC");
        dxcc.filter.anyStatus = StatusNewDxcc;
        dxcc.filter.maxAgeMinutes = 10;
        AlertRule slot;
        slot.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        slot.name = tr("New band or mode");
        slot.voice = false;
        slot.filter.anyStatus = StatusNewBand | StatusNewMode;
        slot.filter.maxAgeMinutes = 10;
        m_rules << dxcc << slot;
    }

    m_indexDebounce.setSingleShot(true);
    m_indexDebounce.setInterval(1500);
    connect(&m_indexDebounce, &QTimer::timeout, this, [this] {
        rebuildIndex();
        m_model.restatus([this](const EnrichedSpot& e) { return statusOf(e); });
    });
    applyFilterToModel();
}

ClusterController::~ClusterController()
{
    for (ClusterConnection* c : m_connections)
        c->stop();
}

void ClusterController::start()
{
    if (m_started)
        return;
    m_started = true;
    rebuildIndex();
    loadLotwUsers();

    QSettings s;
    QList<ClusterSource> list;
    if (s.contains(QStringLiteral("cluster/sources"))) {
        for (const QVariant& v : jsonToList(s.value(QStringLiteral("cluster/sources")).toString()))
            list << ClusterSource::fromMap(v.toMap());
    } else {
        // Primo avvio: il nodo che usa gia' Decodium, acceso; gli altri si aggiungono.
        ClusterSource first = ClusterSource::presets().first();
        first.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        list << first;
    }
    for (const ClusterSource& src : list)
        createConnection(src);
    saveSources();
    emit sourcesChanged();
}

void ClusterController::createConnection(const ClusterSource& source)
{
    auto* c = new ClusterConnection(source, this);
    c->setLoginProvider([this] { return m_ctx.stationCall ? m_ctx.stationCall() : QString(); });
    c->setSecretReader([this](const QString& service, std::function<void(const QString&, const QString&)> done) {
        if (m_ctx.credentials)
            m_ctx.credentials->readSecret(service, std::move(done));
        else
            done({}, tr("no keystore"));
    });
    connect(c, &ClusterConnection::spotReceived, this, &ClusterController::onSpot);
    connect(c, &ClusterConnection::lineReceived, this, [this, c](const QString& line) {
        addConsole(c->source().name, line);
    });
    connect(c, &ClusterConnection::stateChanged, this, [this, c] {
        addConsole(c->source().name, QStringLiteral("— %1").arg(c->stateText()));
        emit sourcesChanged();
    });
    m_connections << c;
    if (source.enabled)
        c->start();
}

ClusterConnection* ClusterController::connectionFor(const QString& id) const
{
    for (ClusterConnection* c : m_connections) {
        if (c->source().id == id)
            return c;
    }
    return nullptr;
}

void ClusterController::saveSources() const
{
    QVariantList list;
    for (const ClusterConnection* c : m_connections)
        list << c->source().toMap();
    QSettings().setValue(QStringLiteral("cluster/sources"), listToJson(list));
}

void ClusterController::saveRules() const
{
    QVariantList list;
    for (const AlertRule& r : m_rules)
        list << r.toMap();
    QSettings().setValue(QStringLiteral("cluster/rules"), listToJson(list));
}

QVariantList ClusterController::sources() const
{
    QVariantList out;
    for (const ClusterConnection* c : m_connections) {
        QVariantMap m = c->source().toMap();
        m.insert(QStringLiteral("state"), static_cast<int>(c->state()));
        m.insert(QStringLiteral("online"), c->state() == ClusterConnection::State::Online);
        m.insert(QStringLiteral("stateText"), c->stateText());
        m.insert(QStringLiteral("spotCount"), c->spotCount());
        m.insert(QStringLiteral("lastSpot"), c->lastSpotAt().isValid() ? c->lastSpotAt().toString(QStringLiteral("HH:mm")) : QString());
        out << m;
    }
    return out;
}

QVariantList ClusterController::presets() const
{
    QVariantList out;
    for (const ClusterSource& s : ClusterSource::presets())
        out << s.toMap();
    return out;
}

int ClusterController::onlineCount() const
{
    return static_cast<int>(std::count_if(m_connections.cbegin(), m_connections.cend(), [](const ClusterConnection* c) {
        return c->state() == ClusterConnection::State::Online;
    }));
}

QString ClusterController::addSource(const QVariantMap& map)
{
    ClusterSource s = ClusterSource::fromMap(map);
    s.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (s.name.isEmpty())
        s.name = s.host;
    createConnection(s);
    saveSources();
    emit sourcesChanged();
    return s.id;
}

QString ClusterController::addPreset(int index)
{
    const auto list = ClusterSource::presets();
    if (index < 0 || index >= list.size())
        return {};
    return addSource(list.at(index).toMap());
}

void ClusterController::updateSource(const QString& id, const QVariantMap& map)
{
    ClusterConnection* c = connectionFor(id);
    if (!c)
        return;
    ClusterSource s = ClusterSource::fromMap(map);
    s.id = id;
    const bool wasEnabled = c->source().enabled;
    c->setSource(s);
    if (s.enabled && !wasEnabled)
        c->start();
    else if (!s.enabled && wasEnabled)
        c->stop();
    saveSources();
    emit sourcesChanged();
}

void ClusterController::removeSource(const QString& id)
{
    ClusterConnection* c = connectionFor(id);
    if (!c)
        return;
    c->stop();
    m_connections.removeOne(c);
    c->deleteLater();
    saveSources();
    emit sourcesChanged();
}

void ClusterController::setSourceEnabled(const QString& id, bool enabled)
{
    ClusterConnection* c = connectionFor(id);
    if (!c)
        return;
    ClusterSource s = c->source();
    s.enabled = enabled;
    c->setSource(s);
    if (enabled)
        c->start();
    else
        c->stop();
    saveSources();
    emit sourcesChanged();
}

QString ClusterController::sendCommand(const QString& sourceId, const QString& command)
{
    const QString cmd = command.trimmed();
    if (cmd.isEmpty())
        return {};
    ClusterConnection* target = sourceId.isEmpty() ? nullptr : connectionFor(sourceId);
    if (!target) {
        for (ClusterConnection* c : m_connections) {
            if (c->state() == ClusterConnection::State::Online && c->source().type == QLatin1String("cluster")) {
                target = c;
                break;
            }
        }
    }
    if (!target || !target->send(cmd))
        return tr("No cluster node connected");
    addConsole(target->source().name, QStringLiteral("> ") + cmd);
    return {};
}

QString ClusterController::postSpot(const QString& call, const QString& freqKhz, const QString& comment)
{
    const QString c = call.trimmed().toUpper();
    bool ok = false;
    const double f = QString(freqKhz).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
    if (c.size() < 3 || !ok || f <= 0)
        return tr("Call and frequency in kHz are needed");
    return sendCommand({}, QStringLiteral("DX %1 %2 %3").arg(QString::number(f, 'f', 1), c, comment.simplified()));
}

// ── Spot ──────────────────────────────────────────────────────────────────────

void ClusterController::rebuildIndex()
{
    if (!m_ctx.db || !m_ctx.db->isOpen())
        return;
    bool lotw = true, card = true, eqsl = false;
    if (m_ctx.confirmations)
        m_ctx.confirmations(lotw, card, eqsl);
    // Rileggere tutto il log per sapere chi e' gia' stato lavorato costa, e si
    // rifa' a ogni QSO nuovo: se un giorno costasse troppo, lo si scopre da
    // qui invece che dall'operatore che vede la finestra ferma.
    QElapsedTimer clock;
    clock.start();
    m_index.rebuild(*m_ctx.db, lotw, card, eqsl);
    if (clock.elapsed() >= 400 && m_ctx.activity) {
        m_ctx.activity(QStringLiteral("APP"),
                       tr("rebuilding the worked list: %1 s")
                           .arg(QString::number(clock.elapsed() / 1000.0, 'f', 1)),
                       QStringLiteral("warning"));
    }
}

void ClusterController::injectLine(const QString& line)
{
    if (!m_started) {
        rebuildIndex();
        loadLotwUsers();
        m_started = true;
    }
    std::optional<Spot> spot = line.startsWith(QLatin1Char('{')) ? spots::parseHamAlertJson(line.toUtf8())
                                                                  : spots::parseDxLine(line);
    if (spot) {
        spot->time = QDateTime::currentDateTimeUtc().addSecs(-60);
        if (spot->sourceName.isEmpty())
            spot->sourceName = QStringLiteral("demo");
        onSpot(*spot);
    }
}

void ClusterController::logChanged()
{
    m_indexDebounce.start();
}

int ClusterController::statusOf(const EnrichedSpot& e) const
{
    int st = m_index.status(e.spot, e.dxcc);
    if (m_lotwUsers.isActive(e.spot.dxCall))
        st |= StatusLotwUser;
    return st;
}

EnrichedSpot ClusterController::enrich(const Spot& spot) const
{
    EnrichedSpot e;
    e.spot = spot;
    std::optional<maidenhead::LatLon> position;
    if (m_ctx.countries) {
        if (const auto entity = m_ctx.countries->lookup(spot.dxCall)) {
            e.dxcc = entity->dxcc;
            e.entity = entity->name;
            e.continent = entity->continent;
            e.cqZone = entity->cqZone;
            position = maidenhead::LatLon{entity->lat, entity->lon};
        }
        if (const auto spotter = m_ctx.countries->lookup(spotterCall(spot.spotter)))
            e.spotterContinent = spotter->continent;
    }
    if (const auto grid = maidenhead::toLatLon(spot.dxGrid))
        position = grid;
    if (position) {
        e.lat = position->lat;
        e.lon = position->lon;
        e.hasPosition = true;
    }
    const auto me = maidenhead::toLatLon(m_ctx.stationGrid ? m_ctx.stationGrid() : QString());
    if (position && me) {
        e.distanceKm = maidenhead::distanceKm(*me, *position);
        e.azimuth = qRound(maidenhead::azimuthDeg(*me, *position));
    }
    e.status = statusOf(e);
    return e;
}

void ClusterController::onSpot(const Spot& spot)
{
    bool isNew = false;
    const EnrichedSpot& stored = m_model.add(enrich(spot), &isNew);
    const EnrichedSpot e = stored;   // copia: le regole possono toccare il modello
    if (m_ctx.spotSeen)
        m_ctx.spotSeen(e);
    checkRules(e, isNew);
}

void ClusterController::checkRules(const EnrichedSpot& e, bool isNew)
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    bool alerted = false;
    bool speak = false;
    bool toDecodium = false;
    QStringList names;
    for (const AlertRule& r : m_rules) {
        if (!r.enabled || !r.filter.matches(e, now))
            continue;
        alerted = true;
        speak = speak || r.voice;
        toDecodium = toDecodium || r.decodium;
        names << r.name;
    }

    // A Decodium va quello che l'operatore sta guardando, piu' gli avvisi.
    if (m_sendToDecodium && (toDecodium || m_filter.matches(e, now)))
        sendSpotToDecodium(e, alerted);

    if (!alerted)
        return;
    // Lo stesso DX sulla stessa banda e modo si annuncia una volta ogni tanto, non a
    // ogni skimmer che lo sente.
    const QString key = e.key();
    const QDateTime last = m_announced.value(key);
    if (last.isValid() && last.secsTo(now) < m_voiceCooldown * 60)
        return;
    m_announced.insert(key, now);
    Q_UNUSED(isNew);

    ++m_alertCount;
    const QString title = SpotModel::statusLabel(e.status).isEmpty() ? names.first() : SpotModel::statusLabel(e.status);
    const QString text = QStringLiteral("%1 %2 %3 %4 · %5%6")
                             .arg(e.spot.dxCall, QString::number(e.spot.freqKhz, 'f', 1), e.spot.mode,
                                  e.entity.isEmpty() ? QStringLiteral("?") : e.entity, names.join(QStringLiteral(", ")),
                                  e.spot.potaRef.isEmpty() ? QString() : QStringLiteral(" · POTA ") + e.spot.potaRef);
    if (m_ctx.activity)
        m_ctx.activity(QStringLiteral("CLUSTER"), QStringLiteral("%1: %2").arg(title, text), QStringLiteral("highlight"));
    emit alertRaised(title, text);
    if (speak && m_voiceEnabled && !m_muted)
        m_voice.say(announcement(e));
}

QString ClusterController::announcement(const EnrichedSpot& e) const
{
    const bool it = m_voiceLanguage == QLatin1String("it");
    QString status;
    if (e.status & StatusNewDxcc)      status = it ? QStringLiteral("Nuovo DXCC") : QStringLiteral("New DXCC");
    else if (e.status & StatusNewBand) status = it ? QStringLiteral("Nuova banda") : QStringLiteral("New band");
    else if (e.status & StatusNewMode) status = it ? QStringLiteral("Nuovo modo") : QStringLiteral("New mode");
    else if (e.status & StatusNewSlot) status = it ? QStringLiteral("Nuovo slot") : QStringLiteral("New slot");
    else                               status = QStringLiteral("Spot");

    QString band = e.spot.band;
    if (band.endsWith(QLatin1String("cm")))
        band = band.chopped(2) + (it ? QStringLiteral(" centimetri") : QStringLiteral(" centimeters"));
    else if (band.endsWith(QLatin1Char('m')))
        band = band.chopped(1) + (it ? QStringLiteral(" metri") : QStringLiteral(" meters"));

    QString mode = e.spot.mode;
    if (mode.startsWith(QLatin1String("FT")) || mode == QLatin1String("SSB") || mode == QLatin1String("CW"))
        mode = VoiceAnnouncer::spell(mode, false);

    QStringList parts{status};
    if (!e.entity.isEmpty())
        parts << e.entity;
    parts << VoiceAnnouncer::spell(e.spot.dxCall, m_voicePhonetic);
    if (!band.isEmpty())
        parts << band;
    if (!mode.isEmpty())
        parts << mode;
    if (!e.spot.potaRef.isEmpty())
        parts << (it ? QStringLiteral("attivazione POTA") : QStringLiteral("POTA activation"));
    else if (!e.spot.sotaRef.isEmpty())
        parts << (it ? QStringLiteral("attivazione SOTA") : QStringLiteral("SOTA activation"));
    return parts.join(QStringLiteral(". ")) + QLatin1Char('.');
}

void ClusterController::sendSpotToDecodium(const EnrichedSpot& e, bool alert)
{
    if (!m_ctx.decoLink || m_ctx.decoLink->clientCount() == 0)
        return;
    const auto tuning = spots::tuningFor(e.spot.freqKhz, e.spot.mode);
    m_ctx.decoLink->broadcast(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("spot")},
        {QStringLiteral("call"), e.spot.dxCall},
        {QStringLiteral("freqKhz"), e.spot.freqKhz},
        {QStringLiteral("dialKhz"), tuning.dialKhz},
        {QStringLiteral("audioHz"), tuning.audioHz},
        {QStringLiteral("band"), e.spot.band},
        {QStringLiteral("mode"), e.spot.mode},
        {QStringLiteral("spotter"), e.spot.spotter},
        {QStringLiteral("comment"), e.spot.comment},
        {QStringLiteral("time"), e.spot.time.toString(Qt::ISODate)},
        {QStringLiteral("source"), e.spot.source},
        {QStringLiteral("entity"), e.entity},
        {QStringLiteral("dxcc"), e.dxcc},
        {QStringLiteral("status"), SpotModel::statusLabel(e.status)},
        {QStringLiteral("statusBits"), e.status},
        {QStringLiteral("count"), e.count},
        {QStringLiteral("alert"), alert},
    });
}

void ClusterController::tune(const QString& spotKey)
{
    const EnrichedSpot* e = m_model.find(spotKey);
    if (!e)
        return;
    emit spotPicked(e->spot.dxCall, e->spot.band, e->spot.mode, e->spot.freqKhz);
    if (m_ctx.lookup)
        m_ctx.lookup(e->spot.dxCall);

    // Per i modi digitali la frequenza dello spot non e' quella del VFO: il
    // quadrante sta sulla sotto-banda e il segnale e' un tono nell'audio. La
    // radio va sul quadrante, non sullo spot.
    const auto tuning = spots::tuningFor(e->spot.freqKhz, e->spot.mode);

    // Prima la radio: e' quella che l'operatore guarda. Va fatto anche quando
    // Decodium non c'e' — chi lavora in CW o in SSB non ha Decodium aperto, e
    // fino a ieri un doppio clic sullo spot non muoveva un VFO.
    const bool toRadio = m_ctx.tuneRadio && m_ctx.tuneRadio(tuning.dialKhz / 1000.0, e->spot.mode);

    // Il QSO si prepara comunque, anche senza radio e senza Decodium: chi
    // clicca su uno spot vuole quella stazione nel riquadro, e da li' parte il
    // callbook con nome, QTH e locatore.
    if (m_ctx.prepareQso) {
        m_ctx.prepareQso(QVariantMap{
            {QStringLiteral("call"), e->spot.dxCall},
            {QStringLiteral("mhz"), e->spot.freqKhz / 1000.0},
            {QStringLiteral("mode"), e->spot.mode},
            {QStringLiteral("grid"), e->spot.dxGrid},
        });
    }

    const bool toDecodium = m_ctx.decoLink && m_ctx.decoLink->clientCount() > 0;
    if (toDecodium) {
        m_ctx.decoLink->broadcast(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("tune")},
            {QStringLiteral("call"), e->spot.dxCall},
            {QStringLiteral("freqKhz"), e->spot.freqKhz},
            {QStringLiteral("dialKhz"), tuning.dialKhz},
            {QStringLiteral("audioHz"), tuning.audioHz},
            {QStringLiteral("mode"), e->spot.mode},
            {QStringLiteral("grid"), e->spot.dxGrid},
        });
    }

    if (!m_ctx.activity)
        return;
    const QString what = QStringLiteral("%1 %2 kHz %3")
                             .arg(e->spot.dxCall, QString::number(e->spot.freqKhz, 'f', 1), e->spot.mode);
    if (toRadio && toDecodium)
        m_ctx.activity(QStringLiteral("CLUSTER"), tr("Tune radio and Decodium: %1").arg(what), QStringLiteral("info"));
    else if (toRadio)
        m_ctx.activity(QStringLiteral("CLUSTER"), tr("Tune radio: %1").arg(what), QStringLiteral("info"));
    else if (toDecodium)
        m_ctx.activity(QStringLiteral("CLUSTER"), tr("Tune Decodium: %1").arg(what), QStringLiteral("info"));
    else
        m_ctx.activity(QStringLiteral("CLUSTER"),
                       tr("Nowhere to send %1: the radio is not connected and Decodium is not there either.")
                           .arg(e->spot.dxCall),
                       QStringLiteral("warning"));
}

void ClusterController::lookupSpot(const QString& spotKey)
{
    const EnrichedSpot* e = m_model.find(spotKey);
    if (!e)
        return;
    emit spotPicked(e->spot.dxCall, e->spot.band, e->spot.mode, e->spot.freqKhz);
    if (m_ctx.lookup)
        m_ctx.lookup(e->spot.dxCall);
}

QVariantList ClusterController::mapSpots() const
{
    QVariantList out;
    for (const EnrichedSpot& e : m_model.visible()) {
        if (!e.hasPosition)
            continue;
        out << QVariantMap{
            {QStringLiteral("key"), e.key()},
            {QStringLiteral("call"), e.spot.dxCall},
            {QStringLiteral("lat"), e.lat},
            {QStringLiteral("lon"), e.lon},
            {QStringLiteral("band"), e.spot.band},
            {QStringLiteral("mode"), e.spot.mode},
            {QStringLiteral("freq"), e.spot.freqKhz},
            {QStringLiteral("status"), e.status},
            {QStringLiteral("statusLabel"), SpotModel::statusLabel(e.status)},
            {QStringLiteral("entity"), e.entity},
            // Quello che serve alla mappa del rotore: rotta, distanza, eta'.
            {QStringLiteral("grid"), e.spot.dxGrid},
            {QStringLiteral("az"), e.azimuth},
            {QStringLiteral("km"), e.distanceKm < 0 ? 0 : qRound(e.distanceKm)},
            {QStringLiteral("snr"), e.spot.hasSnr ? QVariant(e.spot.snr) : QVariant()},
            {QStringLiteral("age"), e.spot.time.isValid()
                                        ? e.spot.time.secsTo(QDateTime::currentDateTimeUtc()) : 0},
            {QStringLiteral("spotter"), e.spot.spotter},
        };
    }
    return out;
}

void ClusterController::clearSpots()
{
    m_model.clear();
}

void ClusterController::addConsole(const QString& source, const QString& line)
{
    m_console << QStringLiteral("%1 %2 │ %3").arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("HH:mm:ss")),
                                                  source, line);
    while (m_console.size() > kMaxConsole)
        m_console.removeFirst();
    emit consoleChanged();
}

void ClusterController::clearConsole()
{
    m_console.clear();
    emit consoleChanged();
}

// ── Filtri ────────────────────────────────────────────────────────────────────

void ClusterController::applyFilterToModel()
{
    SpotFilter f = m_filter;
    if (m_followBand && m_ctx.decodiumBand) {
        const QString band = m_ctx.decodiumBand();
        if (!band.isEmpty())
            f.bands = {band};
    }
    m_model.setFilter(f);
}

void ClusterController::setFilter(const QVariantMap& map)
{
    m_filter = SpotFilter::fromMap(map);
    QSettings().setValue(QStringLiteral("cluster/filter"),
                         QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(m_filter.toMap())).toJson(QJsonDocument::Compact)));
    applyFilterToModel();
    emit filterChanged();
}

void ClusterController::setFollowDecodiumBand(bool on)
{
    if (on == m_followBand)
        return;
    m_followBand = on;
    QSettings().setValue(QStringLiteral("cluster/followDecodiumBand"), on);
    applyFilterToModel();
    emit filterChanged();
}

void ClusterController::decodiumBandChanged()
{
    if (m_followBand)
        applyFilterToModel();
}

QVariantList ClusterController::statusNames() const
{
    return {
        QVariantMap{{QStringLiteral("bit"), int(StatusNewDxcc)}, {QStringLiteral("label"), QStringLiteral("NEW DXCC")}},
        QVariantMap{{QStringLiteral("bit"), int(StatusNewBand)}, {QStringLiteral("label"), QStringLiteral("NEW BAND")}},
        QVariantMap{{QStringLiteral("bit"), int(StatusNewMode)}, {QStringLiteral("label"), QStringLiteral("NEW MODE")}},
        QVariantMap{{QStringLiteral("bit"), int(StatusNewSlot)}, {QStringLiteral("label"), QStringLiteral("NEW SLOT")}},
        QVariantMap{{QStringLiteral("bit"), int(StatusNewCall)}, {QStringLiteral("label"), QStringLiteral("NEW CALL")}},
        QVariantMap{{QStringLiteral("bit"), int(StatusUnconfirmed)}, {QStringLiteral("label"), QStringLiteral("UNCONFIRMED")}},
        QVariantMap{{QStringLiteral("bit"), int(StatusLotwUser)}, {QStringLiteral("label"), QStringLiteral("LoTW")}},
    };
}

void ClusterController::saveFilter(const QString& name)
{
    const QString n = name.trimmed();
    if (n.isEmpty())
        return;
    QVariantMap f = m_filter.toMap();
    f.insert(QStringLiteral("followDecodiumBand"), m_followBand);
    m_savedFilters.insert(n, f);
    QSettings().setValue(QStringLiteral("cluster/savedFilters"),
                         QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(m_savedFilters)).toJson(QJsonDocument::Compact)));
    emit filterChanged();
}

void ClusterController::applySavedFilter(const QString& name)
{
    if (!m_savedFilters.contains(name))
        return;
    const QVariantMap f = m_savedFilters.value(name).toMap();
    m_followBand = f.value(QStringLiteral("followDecodiumBand"), false).toBool();
    setFilter(f);
}

void ClusterController::deleteSavedFilter(const QString& name)
{
    if (m_savedFilters.remove(name) == 0)
        return;
    QSettings().setValue(QStringLiteral("cluster/savedFilters"),
                         QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(m_savedFilters)).toJson(QJsonDocument::Compact)));
    emit filterChanged();
}

QVariantList ClusterController::alertRules() const
{
    QVariantList out;
    for (const AlertRule& r : m_rules)
        out << r.toMap();
    return out;
}

QString ClusterController::saveAlertRule(const QVariantMap& map)
{
    AlertRule rule = AlertRule::fromMap(map);
    if (rule.name.isEmpty())
        rule.name = tr("Alert");
    bool replaced = false;
    for (AlertRule& r : m_rules) {
        if (r.id == rule.id) {
            r = rule;
            replaced = true;
        }
    }
    if (!replaced)
        m_rules << rule;
    saveRules();
    emit rulesChanged();
    return rule.id;
}

void ClusterController::removeAlertRule(const QString& id)
{
    const auto before = m_rules.size();
    m_rules.removeIf([&id](const AlertRule& r) { return r.id == id; });
    if (m_rules.size() == before)
        return;
    saveRules();
    emit rulesChanged();
}

// ── Voce ──────────────────────────────────────────────────────────────────────

void ClusterController::setSendToDecodium(bool on)
{
    if (on == m_sendToDecodium) return;
    m_sendToDecodium = on;
    QSettings().setValue(QStringLiteral("cluster/sendToDecodium"), on);
    emit settingsChanged();
}

void ClusterController::setVoiceEnabled(bool on)
{
    if (on == m_voiceEnabled) return;
    m_voiceEnabled = on;
    if (!on)
        m_voice.stop();
    QSettings().setValue(QStringLiteral("cluster/voice/enabled"), on);
    emit settingsChanged();
}

void ClusterController::setVoiceName(const QString& name)
{
    if (name == m_voiceName) return;
    m_voiceName = name;
    m_voice.setVoice(name);
    QSettings().setValue(QStringLiteral("cluster/voice/name"), name);
    emit settingsChanged();
}

void ClusterController::setVoiceRate(int rate)
{
    if (rate == m_voiceRate) return;
    m_voiceRate = qBound(-10, rate, 10);
    m_voice.setRate(m_voiceRate);
    QSettings().setValue(QStringLiteral("cluster/voice/rate"), m_voiceRate);
    emit settingsChanged();
}

void ClusterController::setVoiceVolume(int volume)
{
    if (volume == m_voiceVolume) return;
    m_voiceVolume = qBound(0, volume, 100);
    m_voice.setVolume(m_voiceVolume);
    QSettings().setValue(QStringLiteral("cluster/voice/volume"), m_voiceVolume);
    emit settingsChanged();
}

void ClusterController::setVoicePhonetic(bool on)
{
    if (on == m_voicePhonetic) return;
    m_voicePhonetic = on;
    QSettings().setValue(QStringLiteral("cluster/voice/phonetic"), on);
    emit settingsChanged();
}

void ClusterController::setVoiceLanguage(const QString& language)
{
    if (language == m_voiceLanguage) return;
    m_voiceLanguage = language;
    QSettings().setValue(QStringLiteral("cluster/voice/language"), language);
    emit settingsChanged();
}

void ClusterController::setVoiceCooldownMinutes(int minutes)
{
    if (minutes == m_voiceCooldown) return;
    m_voiceCooldown = qBound(1, minutes, 240);
    QSettings().setValue(QStringLiteral("cluster/voice/cooldownMinutes"), m_voiceCooldown);
    emit settingsChanged();
}

void ClusterController::testVoice()
{
    EnrichedSpot e;
    e.spot.dxCall = QStringLiteral("3Y0J");
    e.spot.band = QStringLiteral("20m");
    e.spot.mode = QStringLiteral("CW");
    e.entity = QStringLiteral("Bouvet");
    e.status = StatusNewDxcc;
    m_voice.say(announcement(e));
}

// ── Utenti LoTW ───────────────────────────────────────────────────────────────

void ClusterController::loadLotwUsers()
{
    QFile file(lotwUsersPath());
    const QFileInfo info(file);
    if (file.open(QIODevice::ReadOnly)) {
        m_lotwUsers.load(file.readAll());
        m_lotwUsersInfo = tr("%1 LoTW users · list of %2").arg(m_lotwUsers.size()).arg(info.lastModified().toString(core::dates::format()));
        emit lotwUsersChanged();
    }
    // La lista ARRL cambia piano: una volta alla settimana basta.
    if (!info.exists() || info.lastModified().daysTo(QDateTime::currentDateTime()) >= 7)
        QTimer::singleShot(20'000, this, &ClusterController::refreshLotwUsers);
}

void ClusterController::refreshLotwUsers()
{
    if (!m_net)
        m_net = new QNetworkAccessManager(this);
    QNetworkRequest request(QUrl(QStringLiteral("https://lotw.arrl.org/lotw-user-activity.csv")));
    network::useHttp11(request);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("DecoDXLog/%1").arg(QCoreApplication::applicationVersion()));
    request.setTransferTimeout(120'000);
    m_lotwUsersInfo = tr("downloading the LoTW user list…");
    emit lotwUsersChanged();
    QNetworkReply* reply = m_net->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            m_lotwUsersInfo = tr("LoTW user list: %1").arg(network::safeErrorString(reply));
            emit lotwUsersChanged();
            return;
        }
        const QByteArray data = reply->readAll();
        LotwUsers fresh;
        if (fresh.load(data) < 1000) {
            m_lotwUsersInfo = tr("LoTW user list: unexpected content");
            emit lotwUsersChanged();
            return;
        }
        QDir().mkpath(QFileInfo(lotwUsersPath()).absolutePath());
        QFile out(lotwUsersPath());
        if (out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            out.write(data);
        m_lotwUsers = fresh;
        m_lotwUsersInfo = tr("%1 LoTW users · updated today").arg(m_lotwUsers.size());
        emit lotwUsersChanged();
        m_model.restatus([this](const EnrichedSpot& e) { return statusOf(e); });
    });
}

} // namespace decolog::app
