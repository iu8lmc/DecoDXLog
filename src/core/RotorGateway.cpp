#include "core/RotorGateway.h"

#include "core/Maidenhead.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QSerialPort>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>
#include <QWebSocket>
#include <QWebSocketServer>

#include <algorithm>
#include <cmath>

namespace decolog::core {

namespace {

constexpr int kHistorySize = 600;     // ~2 minuti a 5 Hz
constexpr int kTrafficSize = 200;
constexpr int kMaxZoom = 19;
constexpr qint64 kMaxTileBytes = 2'000'000;
constexpr double kJogStep = 15.0;      // i movimenti "a mano" di rotctld

QJsonValue round1(const std::optional<double>& v)
{
    if (!v)
        return QJsonValue::Null;
    return std::round(*v * 10.0) / 10.0;
}

double round1(double v)
{
    return std::round(v * 10.0) / 10.0;
}

double nowSeconds()
{
    return static_cast<double>(QDateTime::currentMSecsSinceEpoch()) / 1000.0;
}

QJsonObject bearingJson(const maidenhead::LatLon& from, const maidenhead::LatLon& to)
{
    const double shortPath = maidenhead::azimuthDeg(from, to);
    return QJsonObject{
        {QStringLiteral("short_path"), round1(shortPath)},
        {QStringLiteral("long_path"), round1(std::fmod(shortPath + 180.0, 360.0))},
        {QStringLiteral("distance_km"), std::round(maidenhead::distanceKm(from, to))},
        {QStringLiteral("lat"), std::round(to.lat * 10000.0) / 10000.0},
        {QStringLiteral("lon"), std::round(to.lon * 10000.0) / 10000.0},
    };
}

QByteArray reasonFor(int status)
{
    switch (status) {
    case 200: return "OK";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 404: return "Not Found";
    default: return "Error";
    }
}

} // namespace

// ── configurazione a caldo ─────────────────────────────────────────────────────

QJsonObject GatewayLive::toJson() const
{
    return QJsonObject{
        {QStringLiteral("my_locator"), locator},
        {QStringLiteral("callsign"), callsign},
        {QStringLiteral("beamwidth"), beamwidth},
        {QStringLiteral("park_az"), parkAz},
        {QStringLiteral("park_el"), parkEl},
        {QStringLiteral("tolerance"), tolerance},
        {QStringLiteral("stall_timeout"), stallTimeoutS},
        {QStringLiteral("stop_on_client_loss"), stopOnClientLoss},
        {QStringLiteral("limits"), QJsonObject{{QStringLiteral("az_min"), azMin},
                                               {QStringLiteral("az_max"), azMax},
                                               {QStringLiteral("el_min"), elMin},
                                               {QStringLiteral("el_max"), elMax}}},
        {QStringLiteral("presets"), presets},
    };
}

GatewayLive GatewayLive::fromJson(const QJsonObject& o)
{
    GatewayLive l;
    l.locator = o.value(QStringLiteral("my_locator")).toString(l.locator);
    l.callsign = o.value(QStringLiteral("callsign")).toString();
    l.beamwidth = o.value(QStringLiteral("beamwidth")).toDouble(l.beamwidth);
    l.parkAz = o.value(QStringLiteral("park_az")).toDouble(l.parkAz);
    l.parkEl = o.value(QStringLiteral("park_el")).toDouble(l.parkEl);
    l.tolerance = o.value(QStringLiteral("tolerance")).toDouble(l.tolerance);
    l.stallTimeoutS = o.value(QStringLiteral("stall_timeout")).toDouble(l.stallTimeoutS);
    l.stopOnClientLoss = o.value(QStringLiteral("stop_on_client_loss")).toBool(l.stopOnClientLoss);
    const QJsonObject limits = o.value(QStringLiteral("limits")).toObject();
    l.azMin = limits.value(QStringLiteral("az_min")).toDouble(l.azMin);
    l.azMax = limits.value(QStringLiteral("az_max")).toDouble(l.azMax);
    l.elMin = limits.value(QStringLiteral("el_min")).toDouble(l.elMin);
    l.elMax = limits.value(QStringLiteral("el_max")).toDouble(l.elMax);
    l.presets = o.value(QStringLiteral("presets")).toArray();
    return l;
}

// ── ciclo di vita ─────────────────────────────────────────────────────────────

RotorGateway::RotorGateway(QObject* parent)
    : QObject(parent)
{
    m_clock.start();
    m_txTimer.setSingleShot(true);
    connect(&m_txTimer, &QTimer::timeout, this, [this] {
        if (!m_current)
            return;
        // Muto: si riprova, o si rinuncia a questa domanda.
        if (m_current->triesLeft > 1) {
            --m_current->triesLeft;
            m_rx.clear();
            writeFrame(m_current->frame);
            m_txTimer.start(m_settings.timeoutMs);
            return;
        }
        finishTransaction(std::nullopt);
    });
    connect(&m_pollTimer, &QTimer::timeout, this, &RotorGateway::poll);
    m_reopenTimer.setSingleShot(true);
    connect(&m_reopenTimer, &QTimer::timeout, this, &RotorGateway::openPort);
}

RotorGateway::~RotorGateway()
{
    stop();
}

void RotorGateway::start(const GatewaySettings& settings, const GatewayLive& live)
{
    stop();
    m_settings = settings;
    m_live = live;
    m_model = prosistel::modelFor(settings.model);
    m_running = true;
    m_problems.clear();
    m_counters = {{QStringLiteral("tx"), 0}, {QStringLiteral("rx"), 0},
                  {QStringLiteral("errors"), 0}, {QStringLiteral("reconnects"), 0}};
    m_startedMs = QDateTime::currentMSecsSinceEpoch();
    m_backoffMs = 1000;
    m_history.clear();
    m_traffic.clear();
    m_az = Axis();
    m_el = Axis();
    m_net = new QNetworkAccessManager(this);
    startServers();
    openPort();
}

void RotorGateway::stop()
{
    if (!m_running)
        return;
    // Non si lascia un rotore in movimento senza nessuno che lo guardi.
    if (m_connected && m_live.stopOnClientLoss && (m_az.target || m_el.target)) {
        for (const QString& name : {QStringLiteral("az"), QStringLiteral("el")}) {
            const QChar id = axisId(name);
            if (!id.isNull())
                writeFrame(prosistel::stop(id, false, multiplier()));
        }
        if (m_serial)
            m_serial->waitForBytesWritten(200);
    }
    m_running = false;
    stopServers();
    m_pollTimer.stop();
    m_reopenTimer.stop();
    m_txTimer.stop();
    m_queue.clear();
    m_current.reset();
    if (m_serial) {
        m_serial->close();
        m_serial->deleteLater();
        m_serial = nullptr;
    }
    m_portOpen = false;
    m_connected = false;
    if (m_net) {
        m_net->deleteLater();
        m_net = nullptr;
    }
}

void RotorGateway::setStation(const QString& locator, const QString& callsign)
{
    bool changed = false;
    if (!locator.trimmed().isEmpty() && locator.trimmed().toUpper() != m_live.locator.toUpper()) {
        m_live.locator = locator.trimmed().toUpper();
        changed = true;
    }
    if (callsign.trimmed().toUpper() != m_live.callsign.toUpper()) {
        m_live.callsign = callsign.trimmed().toUpper();
        changed = true;
    }
    if (!changed)
        return;
    for (Spot& spot : m_spots)
        aimSpot(spot);
    publishState();
}

// ── la seriale ────────────────────────────────────────────────────────────────

void RotorGateway::openPort()
{
    if (!m_running)
        return;
    m_rx.clear();
    if (m_settings.simulate) {
        const prosistel::Model* sim = m_model ? m_model : prosistel::modelFor(QStringLiteral("d_azel"));
        m_simPos.clear();
        m_simTarget.clear();
        if (sim->hasAz())
            m_simPos.insert(sim->azId, 0.0);
        if (sim->hasEl())
            m_simPos.insert(sim->elId, 0.0);
        m_simTarget = m_simPos;
        m_simLastMs = m_clock.elapsed();
        m_portOpen = true;
    } else {
        if (!m_serial) {
            m_serial = new QSerialPort(this);
            connect(m_serial, &QSerialPort::readyRead, this, [this] { onBytes(m_serial->readAll()); });
            connect(m_serial, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError e) {
                if (e == QSerialPort::ResourceError || e == QSerialPort::ReadError || e == QSerialPort::WriteError
                    || e == QSerialPort::PermissionError) {
                    if (m_portOpen)
                        closePort(QStringLiteral("collegamento perso su %1: %2")
                                      .arg(m_settings.serialPort, m_serial->errorString()));
                }
            });
        }
        m_serial->setPortName(m_settings.serialPort);
        m_serial->setBaudRate(m_settings.baud);
        m_serial->setDataBits(QSerialPort::Data8);
        m_serial->setParity(QSerialPort::NoParity);
        m_serial->setStopBits(QSerialPort::OneStop);
        m_serial->setFlowControl(QSerialPort::NoFlowControl);
        if (m_settings.serialPort.isEmpty() || !m_serial->open(QIODevice::ReadWrite)) {
            const QString why = m_settings.serialPort.isEmpty()
                                    ? QStringLiteral("nessuna porta seriale scelta")
                                    : QStringLiteral("impossibile aprire %1: %2")
                                          .arg(m_settings.serialPort, m_serial->errorString());
            if (why != m_error)
                emit note(why, QStringLiteral("warning"));
            m_error = why;
            m_connected = false;
            publishState();
            scheduleReopen();
            return;
        }
        m_serial->setDataTerminalReady(true);
        m_serial->clear();
        m_portOpen = true;
    }
    if (!m_model)
        detectModel();
    else
        afterOpen();
}

void RotorGateway::afterOpen()
{
    for (const QChar id : {m_model->azId, m_model->elId}) {
        if (!id.isNull())
            enqueue({prosistel::disableCpm(id), false, 1, 1, {}});
    }
    m_connected = true;
    m_error.clear();
    m_backoffMs = 1000;
    emit note(QStringLiteral("control box collegato su %1")
                  .arg(m_settings.simulate ? QStringLiteral("porta simulata") : m_settings.serialPort),
              QStringLiteral("info"));
    m_pollTimer.start(qMax(50, m_settings.pollMs));
    publishState();
}

void RotorGateway::detectModel()
{
    // Si chiede la posizione a tutti e tre gli ID: chi risponde dice il modello.
    // La 'B' c'e' solo sui Combi-Track, che contano i gradi per dieci.
    auto found = std::make_shared<QHash<QChar, bool>>();
    const QList<QChar> ids{prosistel::kAzimuth, prosistel::kElevation, prosistel::kSecondUnit};
    for (int i = 0; i < ids.size(); ++i) {
        const QChar id = ids.at(i);
        const bool last = i == ids.size() - 1;
        enqueue({prosistel::queryPosition(id), true, 1, 1,
                 [this, found, id, last](std::optional<prosistel::Reply> reply) {
                     found->insert(id, reply.has_value());
                     if (!last || !m_portOpen)
                         return;
                     const char* key = found->value(prosistel::kSecondUnit) ? "combi"
                                     : found->value(prosistel::kAzimuth) && found->value(prosistel::kElevation) ? "d_azel"
                                     : found->value(prosistel::kElevation) ? "d_el"
                                     : "d_az";
                     m_model = prosistel::modelFor(QLatin1String(key));
                     emit note(QStringLiteral("control box rilevato: %1").arg(m_model->label), QStringLiteral("info"));
                     afterOpen();   // con il modello: CPM spento e via col polling
                 }});
    }
}

void RotorGateway::closePort(const QString& why)
{
    m_pollTimer.stop();
    m_txTimer.stop();
    m_queue.clear();
    m_current.reset();
    m_polling = false;
    if (m_serial)
        m_serial->close();
    m_portOpen = false;
    const bool was = m_connected;
    m_connected = false;
    m_error = why;
    if (was) {
        m_counters[QStringLiteral("reconnects")] += 1;
        emit note(why, QStringLiteral("warning"));
    }
    publishState();
    scheduleReopen();
}

void RotorGateway::scheduleReopen()
{
    if (!m_running)
        return;
    m_reopenTimer.start(m_backoffMs);
    m_backoffMs = qMin(m_backoffMs * 2, 10000);
}

void RotorGateway::enqueue(Transaction t, bool front)
{
    if (front)
        m_queue.prepend(std::move(t));
    else
        m_queue.enqueue(std::move(t));
    pump();
}

void RotorGateway::pump()
{
    while (!m_current && !m_queue.isEmpty() && m_portOpen) {
        Transaction t = m_queue.dequeue();
        if (!t.wantReply) {
            writeFrame(t.frame);
            if (t.done)
                t.done(std::nullopt);
            continue;
        }
        m_current = std::move(t);
        m_rx.clear();
        writeFrame(m_current->frame);
        m_txTimer.start(m_settings.timeoutMs);
    }
}

void RotorGateway::writeFrame(const QByteArray& frame)
{
    if (m_settings.simulate)
        simulateWrite(frame);
    else if (m_serial && m_serial->isOpen())
        m_serial->write(frame);
    recordFrame(QStringLiteral("tx"), frame);
}

void RotorGateway::onBytes(const QByteArray& bytes)
{
    if (!m_current) {
        m_rx.clear();   // quello che arriva senza che si sia chiesto niente
        return;
    }
    m_rx += bytes;
    for (;;) {
        const int stx = m_rx.indexOf(prosistel::kStx);
        if (stx < 0) {
            m_rx.clear();   // rumore, o la coda di un frame vecchio
            return;
        }
        if (stx > 0)
            m_rx.remove(0, stx);
        const int cr = m_rx.indexOf(prosistel::kCr);
        if (cr < 0)
            return;         // il resto arriva dopo
        const QByteArray frame = m_rx.left(cr + 1);
        m_rx.remove(0, cr + 1);
        recordFrame(QStringLiteral("rx"), frame);
        if (!m_current)
            return;
        const auto reply = prosistel::decode(frame, m_current->multiplier);
        if (reply) {
            finishTransaction(reply);
            return;
        }
        // Frame sporco: si riprova come per il silenzio.
        if (m_current->triesLeft > 1) {
            --m_current->triesLeft;
            m_rx.clear();
            writeFrame(m_current->frame);
            m_txTimer.start(m_settings.timeoutMs);
            return;
        }
        finishTransaction(std::nullopt);
        return;
    }
}

void RotorGateway::finishTransaction(std::optional<prosistel::Reply> reply)
{
    m_txTimer.stop();
    if (!m_current)
        return;
    Transaction t = std::move(*m_current);
    m_current.reset();
    if (t.done)
        t.done(reply);
    QMetaObject::invokeMethod(this, [this] { pump(); }, Qt::QueuedConnection);
}

QChar RotorGateway::axisId(const QString& name) const
{
    if (!m_model)
        return {};
    return name == QLatin1String("az") ? m_model->azId : m_model->elId;
}

int RotorGateway::multiplier() const
{
    return m_model ? m_model->multiplier : 1;
}

void RotorGateway::poll()
{
    // Una domanda per volta: se la precedente non ha finito, si aspetta.
    if (!m_connected || m_polling || m_current)
        return;
    m_polling = true;
    const bool hasAz = !axisId(QStringLiteral("az")).isNull();
    const bool hasEl = !axisId(QStringLiteral("el")).isNull();
    if (!hasAz && !hasEl) {
        afterPoll();
        return;
    }
    if (hasAz)
        pollAxis(QStringLiteral("az"));
    if (hasEl)
        pollAxis(QStringLiteral("el"));
}

void RotorGateway::pollAxis(const QString& name)
{
    const QChar id = axisId(name);
    const bool lastAxis = name == QLatin1String("el") || axisId(QStringLiteral("el")).isNull();
    enqueue({prosistel::queryPosition(id), true, qMax(1, m_settings.retries), multiplier(),
             [this, name, id, lastAxis](std::optional<prosistel::Reply> reply) {
                 Axis& state = name == QLatin1String("az") ? m_az : m_el;
                 if (!reply) {
                     m_counters[QStringLiteral("errors")] += 1;
                 } else {
                     const qint64 now = m_clock.elapsed();
                     const auto previous = state.position;
                     state.position = reply->value;
                     if (!previous || std::abs(reply->value - *previous) > 0.2)
                         state.lastChangeMs = now;
                     if (!state.target) {
                         state.moving = reply->moving();
                     } else {
                         const bool reached = std::abs(reply->value - *state.target) <= m_live.tolerance;
                         const bool stalled = now - state.lastChangeMs > static_cast<qint64>(m_live.stallTimeoutS * 1000);
                         if (reached) {
                             state.target.reset();
                             state.moving = false;
                         } else if (stalled) {
                             state.target.reset();
                             state.moving = false;
                             m_error = QStringLiteral("%1: nessun movimento entro %2s, stop di sicurezza")
                                           .arg(name).arg(m_live.stallTimeoutS, 0, 'f', 0);
                             emit note(m_error, QStringLiteral("error"));
                             enqueue({prosistel::stop(id, true, multiplier()), false, 1, 1, {}}, true);
                         } else {
                             state.moving = true;
                         }
                     }
                 }
                 if (lastAxis)
                     afterPoll();
             }});
}

void RotorGateway::afterPoll()
{
    m_polling = false;
    if (m_az.position || m_el.position) {
        m_history.push_back(QJsonObject{{QStringLiteral("ts"), nowSeconds()},
                                        {QStringLiteral("az"), round1(m_az.position)},
                                        {QStringLiteral("el"), round1(m_el.position)}});
        while (m_history.size() > kHistorySize)
            m_history.pop_front();
    }
    publishState();
}

void RotorGateway::recordFrame(const QString& direction, const QByteArray& frame)
{
    m_counters[direction] += 1;
    m_traffic.push_back(QJsonObject{{QStringLiteral("ts"), nowSeconds()},
                                    {QStringLiteral("dir"), direction},
                                    {QStringLiteral("frame"), prosistel::printable(frame)},
                                    {QStringLiteral("hex"), QString::fromLatin1(frame.toHex(' '))}});
    while (m_traffic.size() > kTrafficSize)
        m_traffic.pop_front();
}

void RotorGateway::publishState()
{
    emit stateChanged();
    if (m_wsClients.isEmpty())
        return;
    const QString message = QString::fromUtf8(QJsonDocument(snapshot()).toJson(QJsonDocument::Compact));
    for (QWebSocket* client : std::as_const(m_wsClients))
        client->sendTextMessage(message);
}

// Il control box finto: due assi che girano a velocita' costante verso il
// bersaglio, e che rispondono solo agli ID che hanno davvero.
void RotorGateway::simulateAdvance()
{
    const qint64 now = m_clock.elapsed();
    const double step = static_cast<double>(now - m_simLastMs) / 1000.0 * m_settings.simulateSpeed;
    m_simLastMs = now;
    for (auto it = m_simPos.begin(); it != m_simPos.end(); ++it) {
        const double delta = m_simTarget.value(it.key()) - it.value();
        it.value() += std::clamp(delta, -step, step);
    }
}

void RotorGateway::simulateWrite(const QByteArray& frame)
{
    simulateAdvance();
    QByteArray body = frame;
    body.replace(prosistel::kStx, QByteArray());
    body.replace(prosistel::kCr, QByteArray());
    const QString text = QString::fromLatin1(body);
    if (text.size() < 2)
        return;
    const QChar axis = text.at(0);
    const QChar verb = text.at(1);
    const QString arg = text.mid(2);
    if (!m_simPos.contains(axis))
        return;   // asse che questo control box non ha: nessuna risposta
    const prosistel::Model* sim = m_model ? m_model : prosistel::modelFor(QStringLiteral("d_azel"));
    const int mult = sim->multiplier;
    if (verb == QLatin1Char('?')) {
        const long value = std::lround(m_simPos.value(axis) * mult);
        const bool moving = std::abs(m_simTarget.value(axis) - m_simPos.value(axis)) > 0.5;
        QByteArray reply;
        reply += prosistel::kStx;
        reply += QStringLiteral("%1,?,%2,%3").arg(axis).arg(value).arg(moving ? QLatin1Char('M') : QLatin1Char('R')).toLatin1();
        reply += prosistel::kCr;
        QTimer::singleShot(15, this, [this, reply] { onBytes(reply); });
    } else if (verb == QLatin1Char('G')) {
        bool ok = false;
        const int raw = arg.toInt(&ok);
        if (!ok)
            return;
        const bool isStop = mult == 10 ? (raw == 9777 || raw == 9999)
                                       : (raw == prosistel::kStopSoft || raw == prosistel::kStopFast);
        m_simTarget[axis] = isStop ? m_simPos.value(axis) : static_cast<double>(raw) / mult;
    }
}

// ── lo stato e i comandi ──────────────────────────────────────────────────────

QJsonObject RotorGateway::snapshot() const
{
    return QJsonObject{
        {QStringLiteral("type"), QStringLiteral("state")},
        {QStringLiteral("connected"), m_connected},
        {QStringLiteral("port"), m_settings.simulate ? QStringLiteral("SIMULATO") : m_settings.serialPort},
        {QStringLiteral("model"), m_model ? m_model->key : QStringLiteral("auto")},
        {QStringLiteral("model_label"), m_model ? m_model->label : QStringLiteral("rilevamento in corso")},
        {QStringLiteral("has_az"), m_model ? m_model->hasAz() : true},
        {QStringLiteral("has_el"), m_model ? m_model->hasEl() : false},
        {QStringLiteral("az"), round1(m_az.position)},
        {QStringLiteral("az_target"), round1(m_az.target)},
        {QStringLiteral("az_moving"), m_az.moving},
        {QStringLiteral("el"), round1(m_el.position)},
        {QStringLiteral("el_target"), round1(m_el.target)},
        {QStringLiteral("el_moving"), m_el.moving},
        {QStringLiteral("moving"), m_az.moving || m_el.moving},
        {QStringLiteral("error"), m_error.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(m_error)},
        {QStringLiteral("locator"), m_live.locator},
        {QStringLiteral("callsign"), m_live.callsign},
        {QStringLiteral("beamwidth"), m_live.beamwidth},
        {QStringLiteral("clients"), m_clients},
        {QStringLiteral("tx_frames"), m_counters.value(QStringLiteral("tx"))},
        {QStringLiteral("rx_frames"), m_counters.value(QStringLiteral("rx"))},
        {QStringLiteral("errors"), m_counters.value(QStringLiteral("errors"))},
        {QStringLiteral("reconnects"), m_counters.value(QStringLiteral("reconnects"))},
        {QStringLiteral("uptime"), static_cast<double>(QDateTime::currentMSecsSinceEpoch() - m_startedMs) / 1000.0},
        {QStringLiteral("ts"), nowSeconds()},
    };
}

QJsonObject RotorGateway::gotoPosition(std::optional<double> az, std::optional<double> el, QString* error)
{
    QJsonObject applied;
    if (az) {
        if (m_model && !m_model->hasAz()) {
            *error = QStringLiteral("il control box configurato non ha azimut");
            return {};
        }
        const double value = *az >= 360.0 ? std::fmod(*az, 360.0) : *az;
        applied.insert(QStringLiteral("az"), std::clamp(value, m_live.azMin, m_live.azMax));
    }
    if (el) {
        if (m_model && !m_model->hasEl()) {
            *error = QStringLiteral("il control box configurato non ha elevazione");
            return {};
        }
        applied.insert(QStringLiteral("el"), std::clamp(*el, m_live.elMin, m_live.elMax));
    }
    if (applied.isEmpty()) {
        *error = QStringLiteral("nessun asse indicato");
        return {};
    }
    for (const QString& name : {QStringLiteral("az"), QStringLiteral("el")}) {
        if (!applied.contains(name))
            continue;
        const QChar id = axisId(name);
        if (id.isNull())
            continue;
        const double degrees = applied.value(name).toDouble();
        Axis& state = name == QLatin1String("az") ? m_az : m_el;
        state.target = degrees;
        state.lastChangeMs = m_clock.elapsed();
        m_error.clear();
        enqueue({prosistel::gotoAngle(id, degrees, multiplier()), false, 1, 1, {}}, true);
    }
    publishState();
    return applied;
}

void RotorGateway::halt(const QString& axis, bool fast)
{
    const QStringList names = axis == QLatin1String("all") ? QStringList{QStringLiteral("az"), QStringLiteral("el")}
                                                           : QStringList{axis};
    for (const QString& name : names) {
        const QChar id = axisId(name);
        if (id.isNull())
            continue;
        (name == QLatin1String("az") ? m_az : m_el).target.reset();
        enqueue({prosistel::stop(id, fast, multiplier()), false, 1, 1, {}}, true);
    }
    publishState();
}

QJsonObject RotorGateway::park(QString* error)
{
    std::optional<double> az;
    std::optional<double> el;
    if (!m_model || m_model->hasAz())
        az = m_live.parkAz;
    if (m_model && m_model->hasEl())
        el = m_live.parkEl;
    return gotoPosition(az, el, error);
}

QJsonObject RotorGateway::bearingTo(const QString& locator, QString* error) const
{
    const auto home = maidenhead::toLatLon(m_live.locator);
    const QString clean = locator.trimmed();
    static const QRegularExpression valid(QStringLiteral("^[A-R]{2}[0-9]{2}([A-X]{2}([0-9]{2})?)?$"),
                                          QRegularExpression::CaseInsensitiveOption);
    const auto target = valid.match(clean).hasMatch() ? maidenhead::toLatLon(clean) : std::nullopt;
    if (!target) {
        *error = QStringLiteral("locatore non valido: '%1'").arg(locator);
        return {};
    }
    if (!home) {
        *error = QStringLiteral("locatore non valido: '%1'").arg(m_live.locator);
        return {};
    }
    return bearingJson(*home, *target);
}

void RotorGateway::clientAttached()
{
    ++m_clients;
}

void RotorGateway::clientDetached()
{
    m_clients = qMax(0, m_clients - 1);
    if (m_clients == 0 && (m_az.target || m_el.target) && m_live.stopOnClientLoss) {
        emit note(QStringLiteral("nessun client collegato durante un movimento: stop di sicurezza"),
                  QStringLiteral("warning"));
        halt();
    }
}

// ── la rete ───────────────────────────────────────────────────────────────────

void RotorGateway::startServers()
{
    const QHostAddress bind(m_settings.bind.isEmpty() ? QStringLiteral("0.0.0.0") : m_settings.bind);

    m_ws = new QWebSocketServer(QStringLiteral("DecoRotor"), QWebSocketServer::NonSecureMode, this);
    if (m_ws->listen(bind, static_cast<quint16>(m_settings.wsPort))) {
        connect(m_ws, &QWebSocketServer::newConnection, this, &RotorGateway::onWsConnection);
    } else {
        m_problems << QStringLiteral("WebSocket %1: %2").arg(m_settings.wsPort).arg(m_ws->errorString());
    }

    m_rotctld = new QTcpServer(this);
    if (m_settings.rotctldPort > 0 && m_rotctld->listen(bind, static_cast<quint16>(m_settings.rotctldPort)))
        connect(m_rotctld, &QTcpServer::newConnection, this, &RotorGateway::onRotctldConnection);
    else if (m_settings.rotctldPort > 0)
        m_problems << QStringLiteral("rotctld %1: %2").arg(m_settings.rotctldPort).arg(m_rotctld->errorString());

    m_http = new QTcpServer(this);
    if (m_settings.httpPort > 0 && m_http->listen(bind, static_cast<quint16>(m_settings.httpPort)))
        connect(m_http, &QTcpServer::newConnection, this, &RotorGateway::onHttpConnection);
    else if (m_settings.httpPort > 0)
        m_problems << QStringLiteral("web %1: %2").arg(m_settings.httpPort).arg(m_http->errorString());

    for (const QString& p : std::as_const(m_problems))
        emit note(QStringLiteral("porta non disponibile, %1").arg(p), QStringLiteral("warning"));
}

void RotorGateway::stopServers()
{
    for (QWebSocket* client : std::as_const(m_wsClients)) {
        client->disconnect(this);
        client->close();
        client->deleteLater();
    }
    m_wsClients.clear();
    m_wsAuthorized.clear();
    m_clients = 0;
    for (QTcpServer** server : {&m_rotctld, &m_http}) {
        if (*server) {
            (*server)->close();
            (*server)->deleteLater();
            *server = nullptr;
        }
    }
    if (m_ws) {
        m_ws->close();
        m_ws->deleteLater();
        m_ws = nullptr;
    }
}

bool RotorGateway::authorized(const QString& token) const
{
    return m_settings.token.isEmpty() || token == m_settings.token;
}

void RotorGateway::onWsConnection()
{
    while (QWebSocket* socket = m_ws->nextPendingConnection()) {
        const QString token = QUrlQuery(socket->requestUrl()).queryItemValue(QStringLiteral("token"));
        m_wsClients.insert(socket);
        if (authorized(token))
            m_wsAuthorized.insert(socket);
        clientAttached();
        socket->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("hello")},
            {QStringLiteral("version"), QStringLiteral("1.0")},
            {QStringLiteral("auth"), !m_settings.token.isEmpty()},
        }).toJson(QJsonDocument::Compact)));
        socket->sendTextMessage(QString::fromUtf8(QJsonDocument(snapshot()).toJson(QJsonDocument::Compact)));
        connect(socket, &QWebSocket::textMessageReceived, this,
                [this, socket](const QString& text) { onWsMessage(socket, text); });
        connect(socket, &QWebSocket::disconnected, this, [this, socket] {
            if (m_wsClients.remove(socket)) {
                m_wsAuthorized.remove(socket);
                clientDetached();
            }
            socket->deleteLater();
        });
    }
}

void RotorGateway::onWsMessage(QWebSocket* socket, const QString& text)
{
    auto send = [socket](const QJsonObject& o) {
        socket->sendTextMessage(QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
    };
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        send({{QStringLiteral("type"), QStringLiteral("error")}, {QStringLiteral("message"), QStringLiteral("JSON non valido")}});
        return;
    }
    const QJsonObject request = doc.object();
    const QString command = request.value(QStringLiteral("cmd")).toString();
    if (command == QLatin1String("auth")) {
        const bool ok = !m_settings.token.isEmpty()
                            ? request.value(QStringLiteral("token")).toString() == m_settings.token
                            : request.value(QStringLiteral("token")).toString().isEmpty();
        if (ok)
            m_wsAuthorized.insert(socket);
        send({{QStringLiteral("type"), QStringLiteral("ack")}, {QStringLiteral("cmd"), command}, {QStringLiteral("ok"), ok}});
        return;
    }
    if (!m_wsAuthorized.contains(socket)) {
        send({{QStringLiteral("type"), QStringLiteral("error")},
              {QStringLiteral("message"), QStringLiteral("token mancante o errato")}});
        return;
    }
    bool sendState = false;
    const QJsonObject reply = dispatch(command, request, &sendState);
    if (sendState)
        send(snapshot());
    else
        send(reply);
}

QJsonObject RotorGateway::dispatch(const QString& command, const QJsonObject& request, bool* sendState)
{
    QString error;
    QJsonObject ack{{QStringLiteral("type"), QStringLiteral("ack")}, {QStringLiteral("cmd"), command}};
    auto number = [&request](const char* key) -> std::optional<double> {
        const QJsonValue v = request.value(QLatin1String(key));
        if (v.isUndefined() || v.isNull())
            return std::nullopt;
        if (v.isString()) {
            bool ok = false;
            const double d = v.toString().toDouble(&ok);
            return ok ? std::optional<double>(d) : std::nullopt;
        }
        return v.toDouble();
    };

    if (command == QLatin1String("goto")) {
        ack.insert(QStringLiteral("applied"), gotoPosition(number("az"), number("el"), &error));
    } else if (command == QLatin1String("goto_locator")) {
        const QString locator = request.value(QStringLiteral("locator")).toString();
        const QJsonObject bearing = bearingTo(locator, &error);
        if (error.isEmpty()) {
            const bool longPath = request.value(QStringLiteral("long_path")).toBool();
            QJsonObject applied = gotoPosition(
                round1(bearing.value(longPath ? QStringLiteral("long_path") : QStringLiteral("short_path")).toDouble()),
                std::nullopt, &error);
            applied.insert(QStringLiteral("bearing"), bearing);
            applied.insert(QStringLiteral("locator"), locator.toUpper());
            ack.insert(QStringLiteral("applied"), applied);
        }
    } else if (command == QLatin1String("bearing")) {
        ack.insert(QStringLiteral("bearing"), bearingTo(request.value(QStringLiteral("locator")).toString(), &error));
    } else if (command == QLatin1String("stop")) {
        halt(request.value(QStringLiteral("axis")).toString(QStringLiteral("all")),
             request.value(QStringLiteral("fast")).toBool());
    } else if (command == QLatin1String("park")) {
        ack.insert(QStringLiteral("applied"), park(&error));
    } else if (command == QLatin1String("presets")) {
        ack.insert(QStringLiteral("presets"), m_live.presets);
    } else if (command == QLatin1String("preset_save")) {
        const QString name = request.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty()) {
            error = QStringLiteral("la memoria deve avere un nome");
        } else {
            const auto el = number("el");
            QJsonArray kept;
            for (const QJsonValue& p : std::as_const(m_live.presets)) {
                if (p.toObject().value(QStringLiteral("name")).toString() != name)
                    kept.append(p);
            }
            kept.append(QJsonObject{{QStringLiteral("name"), name},
                                    {QStringLiteral("az"), round1(number("az").value_or(0.0))},
                                    {QStringLiteral("el"), el ? QJsonValue(round1(*el)) : QJsonValue(QJsonValue::Null)}});
            QList<QJsonValue> sorted(kept.begin(), kept.end());
            std::sort(sorted.begin(), sorted.end(), [](const QJsonValue& a, const QJsonValue& b) {
                return a.toObject().value(QStringLiteral("name")).toString().toLower()
                     < b.toObject().value(QStringLiteral("name")).toString().toLower();
            });
            m_live.presets = QJsonArray();
            for (const QJsonValue& v : sorted)
                m_live.presets.append(v);
            emit liveChanged();
            ack.insert(QStringLiteral("presets"), m_live.presets);
        }
    } else if (command == QLatin1String("preset_delete")) {
        const QString name = request.value(QStringLiteral("name")).toString();
        QJsonArray kept;
        for (const QJsonValue& p : std::as_const(m_live.presets)) {
            if (p.toObject().value(QStringLiteral("name")).toString() != name)
                kept.append(p);
        }
        m_live.presets = kept;
        emit liveChanged();
        ack.insert(QStringLiteral("presets"), m_live.presets);
    } else if (command == QLatin1String("preset_recall")) {
        const QString name = request.value(QStringLiteral("name")).toString();
        bool found = false;
        for (const QJsonValue& p : std::as_const(m_live.presets)) {
            const QJsonObject o = p.toObject();
            if (o.value(QStringLiteral("name")).toString() != name)
                continue;
            found = true;
            const QJsonValue az = o.value(QStringLiteral("az"));
            const QJsonValue el = o.value(QStringLiteral("el"));
            ack.insert(QStringLiteral("applied"),
                       gotoPosition(az.isNull() || az.isUndefined() ? std::nullopt : std::optional<double>(az.toDouble()),
                                    el.isNull() || el.isUndefined() ? std::nullopt : std::optional<double>(el.toDouble()),
                                    &error));
            break;
        }
        if (!found)
            error = QStringLiteral("memoria sconosciuta: %1").arg(name);
    } else if (command == QLatin1String("traffic")) {
        const int limit = request.value(QStringLiteral("limit")).toInt(50);
        QJsonArray out;
        const int from = qMax(0, static_cast<int>(m_traffic.size()) - limit);
        for (int i = from; i < static_cast<int>(m_traffic.size()); ++i)
            out.append(m_traffic.at(i));
        ack.insert(QStringLiteral("traffic"), out);
    } else if (command == QLatin1String("history")) {
        const int limit = request.value(QStringLiteral("limit")).toInt(300);
        QJsonArray out;
        const int from = qMax(0, static_cast<int>(m_history.size()) - limit);
        for (int i = from; i < static_cast<int>(m_history.size()); ++i)
            out.append(m_history.at(i));
        ack.insert(QStringLiteral("history"), out);
    } else if (command == QLatin1String("config")) {
        ack.insert(QStringLiteral("config"), configPayload());
    } else if (command == QLatin1String("config_set")) {
        const QJsonObject values = request.value(QStringLiteral("values")).toObject();
        for (auto it = values.begin(); it != values.end(); ++it) {
            const QString key = it.key();
            const QJsonValue v = it.value();
            if (key == QLatin1String("my_locator")) m_live.locator = v.toString().toUpper();
            else if (key == QLatin1String("callsign")) m_live.callsign = v.toString().toUpper();
            else if (key == QLatin1String("beamwidth")) m_live.beamwidth = v.toDouble(m_live.beamwidth);
            else if (key == QLatin1String("park_az")) m_live.parkAz = v.toDouble(m_live.parkAz);
            else if (key == QLatin1String("park_el")) m_live.parkEl = v.toDouble(m_live.parkEl);
            else if (key == QLatin1String("tolerance")) m_live.tolerance = v.toDouble(m_live.tolerance);
            else if (key == QLatin1String("stall_timeout")) m_live.stallTimeoutS = v.toDouble(m_live.stallTimeoutS);
            else if (key == QLatin1String("stop_on_client_loss")) m_live.stopOnClientLoss = v.toBool(m_live.stopOnClientLoss);
            else if (key == QLatin1String("limits")) {
                const QJsonObject l = v.toObject();
                m_live.azMin = l.value(QStringLiteral("az_min")).toDouble(m_live.azMin);
                m_live.azMax = l.value(QStringLiteral("az_max")).toDouble(m_live.azMax);
                m_live.elMin = l.value(QStringLiteral("el_min")).toDouble(m_live.elMin);
                m_live.elMax = l.value(QStringLiteral("el_max")).toDouble(m_live.elMax);
            }
        }
        emit liveChanged();
        ack.insert(QStringLiteral("config"), configPayload());
    } else if (command == QLatin1String("state")) {
        *sendState = true;
        return {};
    } else if (command == QLatin1String("ping")) {
        return {{QStringLiteral("type"), QStringLiteral("pong")}};
    } else {
        error = QStringLiteral("comando sconosciuto: '%1'").arg(command);
    }

    if (!error.isEmpty())
        return {{QStringLiteral("type"), QStringLiteral("error")}, {QStringLiteral("message"), error}};
    return ack;
}

QJsonObject RotorGateway::configPayload() const
{
    QJsonObject out = m_live.toJson();
    out.remove(QStringLiteral("presets"));
    out.insert(QStringLiteral("port"), m_settings.serialPort);
    out.insert(QStringLiteral("model"), m_settings.model);
    out.insert(QStringLiteral("simulate"), m_settings.simulate);
    return out;
}

// ── rotctld ───────────────────────────────────────────────────────────────────

void RotorGateway::onRotctldConnection()
{
    while (QTcpSocket* socket = m_rotctld->nextPendingConnection()) {
        clientAttached();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            while (socket->canReadLine()) {
                bool quit = false;
                const QByteArray reply = rotctldLine(QString::fromLatin1(socket->readLine()).trimmed(), &quit);
                if (quit) {
                    socket->disconnectFromHost();
                    return;
                }
                socket->write(reply);
            }
        });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            clientDetached();
            socket->deleteLater();
        });
    }
}

QByteArray RotorGateway::rotctldLine(const QString& input, bool* quit)
{
    static const QByteArray ok("RPRT 0\n");
    static const QByteArray einval("RPRT -1\n");
    static const QByteArray eproto("RPRT -8\n");
    if (quit)
        *quit = false;
    QString line = input.trimmed();
    if (line.isEmpty())
        return {};
    const bool extended = line.startsWith(QLatin1Char('+'));
    if (extended)
        line.remove(0, 1);
    const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return {};
    const QString command = parts.first();
    const QStringList args = parts.mid(1);
    QString error;
    const QJsonObject state = snapshot();

    if (command == QLatin1String("q") || command == QLatin1String("Q") || command == QLatin1String("quit")) {
        if (quit)
            *quit = true;
        return {};
    }
    if (command == QLatin1String("p") || command == QLatin1String("get_pos")) {
        const double az = state.value(QStringLiteral("az")).toDouble(0.0);
        const double el = state.value(QStringLiteral("el")).toDouble(0.0);
        if (extended)
            return QStringLiteral("Azimuth: %1\nElevation: %2\n").arg(az, 0, 'f', 2).arg(el, 0, 'f', 2).toLatin1();
        return QStringLiteral("%1\n%2\n").arg(az, 0, 'f', 2).arg(el, 0, 'f', 2).toLatin1();
    }
    if (command == QLatin1String("P") || command == QLatin1String("set_pos")) {
        if (args.isEmpty())
            return einval;
        bool okAz = false;
        bool okEl = true;
        const double az = args.at(0).toDouble(&okAz);
        const double el = args.size() > 1 ? args.at(1).toDouble(&okEl) : 0.0;
        if (!okAz || !okEl)
            return einval;
        const bool hasAz = state.value(QStringLiteral("has_az")).toBool();
        const bool hasEl = state.value(QStringLiteral("has_el")).toBool();
        gotoPosition(hasAz ? std::optional<double>(az) : std::nullopt,
                     hasEl && args.size() > 1 ? std::optional<double>(el) : std::nullopt, &error);
        return error.isEmpty() ? ok : einval;
    }
    if (command == QLatin1String("S") || command == QLatin1String("stop")) {
        halt();
        return ok;
    }
    if (command == QLatin1String("K") || command == QLatin1String("park")) {
        park(&error);
        return error.isEmpty() ? ok : einval;
    }
    if (command == QLatin1String("R") || command == QLatin1String("reset")) {
        halt(QStringLiteral("all"), true);
        return ok;
    }
    if (command == QLatin1String("M") || command == QLatin1String("move")) {
        if (args.isEmpty())
            return einval;
        bool isInt = false;
        const int direction = args.at(0).toInt(&isInt);
        if (!isInt)
            return einval;
        const QJsonValue az = state.value(QStringLiteral("az"));
        const QJsonValue el = state.value(QStringLiteral("el"));
        if (direction == 16 && !az.isNull())
            gotoPosition(az.toDouble() + kJogStep, std::nullopt, &error);
        else if (direction == 8 && !az.isNull())
            gotoPosition(qMax(0.0, az.toDouble() - kJogStep), std::nullopt, &error);
        else if (direction == 2 && !el.isNull())
            gotoPosition(std::nullopt, el.toDouble() + kJogStep, &error);
        else if (direction == 4 && !el.isNull())
            gotoPosition(std::nullopt, el.toDouble() - kJogStep, &error);
        else
            return einval;
        return error.isEmpty() ? ok : einval;
    }
    if (command == QLatin1String("_") || command == QLatin1String("get_info"))
        return QStringLiteral("Model: DecoRotor / %1\n").arg(state.value(QStringLiteral("model_label")).toString()).toLatin1();
    if (command == QLatin1String("\\dump_state") || command == QLatin1String("dump_state")
        || command == QLatin1String("\\dump_caps") || command == QLatin1String("dump_caps")
        || command == QLatin1String("1")) {
        const bool hasAz = state.value(QStringLiteral("has_az")).toBool();
        const bool hasEl = state.value(QStringLiteral("has_el")).toBool();
        const QString type = hasAz && hasEl ? QStringLiteral("AzEl") : hasAz ? QStringLiteral("Az") : QStringLiteral("El");
        return QStringLiteral("1\n2\n%1\n%2\n%3\n%4\n0\nrot_type=%5\ndone\n")
            .arg(m_live.azMin, 0, 'f', 6).arg(m_live.azMax, 0, 'f', 6)
            .arg(m_live.elMin, 0, 'f', 6).arg(m_live.elMax, 0, 'f', 6).arg(type).toLatin1();
    }
    return eproto;
}

// ── la pagina web, l'API e i riquadri della mappa ─────────────────────────────

void RotorGateway::onHttpConnection()
{
    while (QTcpSocket* socket = m_http->nextPendingConnection()) {
        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
            *buffer += socket->readAll();
            if (buffer->size() > 1'000'000) {
                socket->abort();
                return;
            }
            const int end = buffer->indexOf("\r\n\r\n");
            if (end < 0)
                return;
            static const QRegularExpression lengthRe(QStringLiteral("(?i)\\r\\ncontent-length:\\s*(\\d+)"));
            const auto m = lengthRe.match(QString::fromLatin1(buffer->left(end)));
            const int length = m.hasMatch() ? m.captured(1).toInt() : 0;
            if (buffer->size() < end + 4 + length)
                return;
            const QByteArray request = buffer->left(end + 4 + length);
            buffer->clear();
            handleHttp(socket, request);
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void RotorGateway::sendHttp(QTcpSocket* socket, int status, const QByteArray& type, const QByteArray& body,
                            const QList<QPair<QByteArray, QByteArray>>& headers)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState)
        return;
    QByteArray out = "HTTP/1.1 " + QByteArray::number(status) + ' ' + reasonFor(status) + "\r\n";
    out += "Server: DecoRotor/1.0 (DecoDXLog)\r\n";
    if (!type.isEmpty())
        out += "Content-Type: " + type + "\r\n";
    out += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    for (const auto& h : headers)
        out += h.first + ": " + h.second + "\r\n";
    out += "Connection: close\r\n\r\n";
    out += body;
    socket->write(out);
    socket->disconnectFromHost();
}

void RotorGateway::sendJson(QTcpSocket* socket, const QJsonObject& object, int status)
{
    sendHttp(socket, status, "application/json; charset=utf-8", QJsonDocument(object).toJson(QJsonDocument::Compact),
             {{"Access-Control-Allow-Origin", "*"}, {"Access-Control-Allow-Headers", "Content-Type, X-Token"}});
}

void RotorGateway::handleHttp(QTcpSocket* socket, const QByteArray& request)
{
    const int headerEnd = request.indexOf("\r\n\r\n");
    const QList<QByteArray> lines = request.left(headerEnd).split('\n');
    const QList<QByteArray> first = lines.value(0).trimmed().split(' ');
    const QByteArray method = first.value(0).toUpper();
    const QUrl url(QString::fromLatin1(first.value(1)));
    const QString route = url.path();
    const QUrlQuery query(url);
    QString headerToken;
    for (const QByteArray& line : lines) {
        if (line.toLower().startsWith("x-token:"))
            headerToken = QString::fromLatin1(line.mid(8)).trimmed();
    }
    const bool allowed = authorized(headerToken) || authorized(query.queryItemValue(QStringLiteral("token")));
    const QByteArray body = request.mid(headerEnd + 4);

    if (method == "OPTIONS") {
        sendHttp(socket, 204, {}, {},
                 {{"Access-Control-Allow-Origin", "*"},
                  {"Access-Control-Allow-Methods", "GET, POST, OPTIONS"},
                  {"Access-Control-Allow-Headers", "Content-Type, X-Token"}});
        return;
    }

    if (method == "GET") {
        if (route == QLatin1String("/api/info")) {
            sendJson(socket, {{QStringLiteral("product"), QStringLiteral("DecoRotor")},
                              {QStringLiteral("version"), QStringLiteral("1.0")},
                              {QStringLiteral("ws_port"), m_settings.wsPort},
                              {QStringLiteral("rotctld_port"), m_settings.rotctldPort},
                              {QStringLiteral("auth"), !m_settings.token.isEmpty()}});
            return;
        }
        if (route == QLatin1String("/api/state") || route == QLatin1String("/api/spots")) {
            if (!allowed) {
                sendJson(socket, {{QStringLiteral("error"), QStringLiteral("non autorizzato")}}, 401);
                return;
            }
            if (route == QLatin1String("/api/state")) {
                sendJson(socket, snapshot());
                return;
            }
            const QJsonArray entries = spotEntries();
            sendJson(socket, {{QStringLiteral("spots"), entries},
                              {QStringLiteral("listening"), true},
                              {QStringLiteral("count"), entries.size()},
                              {QStringLiteral("received"), m_received},
                              {QStringLiteral("dial"), static_cast<double>(m_dial)},
                              {QStringLiteral("remote_call"), m_live.callsign},
                              {QStringLiteral("remote_grid"), m_live.locator},
                              {QStringLiteral("working"), m_workingCall},
                              {QStringLiteral("cluster"), QJsonObject{{QStringLiteral("connected"), true}}}});
            return;
        }
        if (route.startsWith(QLatin1String("/tiles/"))) {
            const QStringList parts = route.mid(7).split(QLatin1Char('/'));
            bool okZ = false, okX = false, okY = false;
            const int z = parts.value(0).toInt(&okZ);
            const int x = parts.value(1).toInt(&okX);
            const int y = parts.value(2).section(QLatin1Char('.'), 0, 0).toInt(&okY);
            if (parts.size() != 3 || !okZ || !okX || !okY) {
                sendJson(socket, {{QStringLiteral("error"), QStringLiteral("riquadro malformato")}}, 400);
                return;
            }
            serveTile(socket, z, x, y);
            return;
        }
        if (route.startsWith(QLatin1String("/api/"))) {
            sendJson(socket, {{QStringLiteral("error"), QStringLiteral("endpoint sconosciuto")}}, 404);
            return;
        }
        if (route == QLatin1String("/") || route.isEmpty() || route == QLatin1String("/index.html")) {
            QFile page(QStringLiteral(":/decorotor/index.html"));
            if (page.open(QIODevice::ReadOnly)) {
                sendHttp(socket, 200, "text/html; charset=utf-8", page.readAll(), {{"Cache-Control", "no-cache"}});
                return;
            }
        }
        sendJson(socket, {{QStringLiteral("error"), QStringLiteral("non trovato")}}, 404);
        return;
    }

    if (method == "POST") {
        if (!allowed) {
            sendJson(socket, {{QStringLiteral("error"), QStringLiteral("non autorizzato")}}, 401);
            return;
        }
        const QJsonObject payload = QJsonDocument::fromJson(body).object();
        QString error;
        auto num = [&payload](const char* key) -> std::optional<double> {
            const QJsonValue v = payload.value(QLatin1String(key));
            if (v.isUndefined() || v.isNull())
                return std::nullopt;
            return v.isString() ? v.toString().toDouble() : v.toDouble();
        };
        QJsonObject reply{{QStringLiteral("ok"), true}};
        if (route == QLatin1String("/api/goto")) {
            reply.insert(QStringLiteral("applied"), gotoPosition(num("az"), num("el"), &error));
        } else if (route == QLatin1String("/api/stop")) {
            halt(payload.value(QStringLiteral("axis")).toString(QStringLiteral("all")),
                 payload.value(QStringLiteral("fast")).toBool());
        } else if (route == QLatin1String("/api/park")) {
            reply.insert(QStringLiteral("applied"), park(&error));
        } else {
            sendJson(socket, {{QStringLiteral("error"), QStringLiteral("endpoint sconosciuto")}}, 404);
            return;
        }
        if (!error.isEmpty()) {
            sendJson(socket, {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}}, 400);
            return;
        }
        sendJson(socket, reply);
        return;
    }
    sendJson(socket, {{QStringLiteral("error"), QStringLiteral("metodo non supportato")}}, 400);
}

void RotorGateway::serveTile(QTcpSocket* socket, int z, int x, int y)
{
    const QList<QPair<QByteArray, QByteArray>> headers{{"Cache-Control", "public, max-age=604800"},
                                                       {"Access-Control-Allow-Origin", "*"}};
    const int side = 1 << qBound(0, z, 30);
    if (m_settings.tileCacheDir.isEmpty() || z < 0 || z > kMaxZoom || x < 0 || y < 0 || x >= side || y >= side) {
        sendHttp(socket, 404, {}, {});
        return;
    }
    const QString path = QDir(m_settings.tileCacheDir).filePath(QStringLiteral("%1/%2/%3.img").arg(z).arg(x).arg(y));
    const QFileInfo info(path);
    const double maxAge = m_settings.tileCacheDays * 86400.0;
    if (info.exists() && (maxAge <= 0 || info.lastModified().secsTo(QDateTime::currentDateTime()) < maxAge)) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            sendHttp(socket, 200, "image/jpeg", file.readAll(), headers);
            return;
        }
    }
    if (!m_net) {
        sendHttp(socket, 404, {}, {});
        return;
    }
    QString address = m_settings.tileUrl;
    address.replace(QStringLiteral("{z}"), QString::number(z));
    address.replace(QStringLiteral("{x}"), QString::number(x));
    address.replace(QStringLiteral("{y}"), QString::number(y));
    QNetworkRequest req{QUrl(address)};
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("DecoDXLog/%1 (rotore d'antenna amatoriale)").arg(QCoreApplication::applicationVersion()));
    req.setTransferTimeout(12000);
    QNetworkReply* reply = m_net->get(req);
    QPointer<QTcpSocket> guard(socket);
    connect(reply, &QNetworkReply::finished, this, [this, reply, guard, path, headers] {
        reply->deleteLater();
        const QByteArray data = reply->error() == QNetworkReply::NoError ? reply->read(kMaxTileBytes + 1) : QByteArray();
        if (data.isEmpty() || data.size() > kMaxTileBytes) {
            if (guard)
                sendHttp(guard, 404, {}, {});
            return;
        }
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile part(path + QStringLiteral(".part"));
        if (part.open(QIODevice::WriteOnly)) {
            part.write(data);
            part.close();
            QFile::remove(path);
            QFile::rename(part.fileName(), path);
        }
        if (guard)
            sendHttp(guard, 200, "image/jpeg", data, headers);
    });
}

// ── le stazioni sentite ───────────────────────────────────────────────────────

QPair<QString, QString> RotorGateway::stationFromMessage(const QString& text)
{
    static const QRegularExpression gridRe(QStringLiteral("^[A-R]{2}[0-9]{2}$"));
    static const QRegularExpression callRe(QStringLiteral("^[A-Z0-9][A-Z0-9/]{1,11}$"));
    static const QRegularExpression reportRe(QStringLiteral("^R?[+-][0-9]{1,2}$"));
    static const QSet<QString> salutes{QStringLiteral("RRR"), QStringLiteral("RR73"), QStringLiteral("73"),
                                       QStringLiteral("R"), QStringLiteral("TU"), QStringLiteral("TNX")};
    static const QSet<QString> notCall{QStringLiteral("CQ"), QStringLiteral("DE"), QStringLiteral("QRZ"),
                                       QStringLiteral("DX"), QStringLiteral("TEST"), QStringLiteral("RR73"),
                                       QStringLiteral("RRR"), QStringLiteral("73"), QStringLiteral("NA"),
                                       QStringLiteral("EU"), QStringLiteral("AS"), QStringLiteral("SA"),
                                       QStringLiteral("AF"), QStringLiteral("OC")};
    QString clean = text;
    clean.replace(QLatin1Char('<'), QLatin1Char(' ')).replace(QLatin1Char('>'), QLatin1Char(' '));
    QStringList tokens = clean.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (tokens.size() < 2)
        return {};
    QString grid;
    const QString last = tokens.last().toUpper();
    if (last != QLatin1String("RR73") && gridRe.match(last).hasMatch()) {
        grid = last;
        tokens.removeLast();
    }
    while (!tokens.isEmpty()
           && (salutes.contains(tokens.last().toUpper()) || reportRe.match(tokens.last().toUpper()).hasMatch()))
        tokens.removeLast();
    if (tokens.size() < 2)
        return {};
    const QString call = tokens.last().toUpper();
    bool hasDigit = false;
    for (const QChar c : call)
        hasDigit = hasDigit || c.isDigit();
    if (notCall.contains(call) || !callRe.match(call).hasMatch() || !hasDigit)
        return {};
    return {call, grid};
}

void RotorGateway::noteDecode(const QString& message, int snr, const QString& mode, quint64 dialHz)
{
    ++m_received;
    const auto [call, grid] = stationFromMessage(message);
    recordSpot(call, grid, QStringLiteral("decode"), snr, mode, dialHz ? dialHz : m_dial);
}

void RotorGateway::noteStatus(const QString& dxCall, const QString& dxGrid, const QString& mode, quint64 dialHz)
{
    ++m_received;
    m_dial = dialHz;
    m_workingCall = dxCall.toUpper();
    recordSpot(m_workingCall, dxGrid, QStringLiteral("qso"), std::nullopt, mode, dialHz);
}

void RotorGateway::noteLogged(const QString& dxCall, const QString& dxGrid, const QString& mode, quint64 freqHz)
{
    ++m_received;
    recordSpot(dxCall, dxGrid, QStringLiteral("log"), std::nullopt, mode, freqHz);
}

void RotorGateway::noteCluster(const QString& call, double lat, double lon, const QString& entity,
                               const QString& mode, quint64 freqHz, const QString& comment)
{
    ++m_received;
    recordSpot(call, QString(), QStringLiteral("cluster"), std::nullopt, mode, freqHz,
               QPair<double, double>(lat, lon), entity, comment);
}

void RotorGateway::recordSpot(const QString& rawCall, const QString& rawGrid, const QString& source,
                              std::optional<int> snr, const QString& mode, quint64 frequencyHz,
                              std::optional<QPair<double, double>> position, const QString& entity,
                              const QString& comment)
{
    const QString call = rawCall.trimmed().toUpper();
    if (call.isEmpty() || call == m_live.callsign.trimmed().toUpper())
        return;   // la propria stazione c'e' in ogni QSO, ma non e' un bersaglio
    const QString grid = rawGrid.trimmed().toUpper();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    auto it = m_spots.find(call);
    if (it == m_spots.end()) {
        Spot spot;
        spot.call = call;
        if (position) {
            spot.lat = position->first;
            spot.lon = position->second;
            // Il cluster da' il paese, non il riquadro: si scrive quello che si ricava.
            spot.grid = maidenhead::fromLatLon(spot.lat, spot.lon, 4);
        } else {
            if (grid.isEmpty())
                return;   // senza posizione non c'e' niente da mettere sulla mappa
            const auto p = maidenhead::toLatLon(grid);
            if (!p)
                return;
            spot.lat = p->lat;
            spot.lon = p->lon;
            spot.grid = grid;
        }
        spot.firstSeen = now;
        it = m_spots.insert(call, spot);
    } else if (!grid.isEmpty() && grid != it->grid) {
        if (const auto p = maidenhead::toLatLon(grid)) {
            it->grid = grid;
            it->lat = p->lat;
            it->lon = p->lon;
        }
    }
    it->lastSeen = now;
    it->count += 1;
    it->source = source;
    if (snr)
        it->snr = snr;
    if (!mode.isEmpty())
        it->mode = mode;
    if (frequencyHz)
        it->frequencyHz = frequencyHz;
    if (!entity.isEmpty())
        it->entity = entity;
    if (!comment.isEmpty())
        it->comment = comment;
    aimSpot(*it);
    purgeSpots();
}

void RotorGateway::aimSpot(Spot& spot) const
{
    const auto home = maidenhead::toLatLon(m_live.locator);
    if (!home)
        return;
    const maidenhead::LatLon target{spot.lat, spot.lon};
    spot.azimuth = maidenhead::azimuthDeg(*home, target);
    spot.distanceKm = maidenhead::distanceKm(*home, target);
}

void RotorGateway::purgeSpots() const
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (auto it = m_spots.begin(); it != m_spots.end();) {
        if (now - it->lastSeen > static_cast<qint64>(m_settings.spotTtlS) * 1000)
            it = m_spots.erase(it);
        else
            ++it;
    }
    if (m_spots.size() > m_settings.spotLimit) {
        QList<Spot> all = m_spots.values();
        std::sort(all.begin(), all.end(), [](const Spot& a, const Spot& b) { return a.lastSeen < b.lastSeen; });
        for (int i = 0; i < all.size() - m_settings.spotLimit; ++i)
            m_spots.remove(all.at(i).call);
    }
}

QJsonArray RotorGateway::spotEntries() const
{
    purgeSpots();
    QList<Spot> all = m_spots.values();
    std::sort(all.begin(), all.end(), [](const Spot& a, const Spot& b) { return a.lastSeen > b.lastSeen; });
    const double now = nowSeconds();
    QJsonArray out;
    for (const Spot& s : all) {
        out.append(QJsonObject{
            {QStringLiteral("call"), s.call},
            {QStringLiteral("grid"), s.grid},
            {QStringLiteral("lat"), std::round(s.lat * 10000.0) / 10000.0},
            {QStringLiteral("lon"), std::round(s.lon * 10000.0) / 10000.0},
            {QStringLiteral("az"), round1(s.azimuth)},
            {QStringLiteral("km"), std::round(s.distanceKm)},
            {QStringLiteral("snr"), s.snr ? QJsonValue(*s.snr) : QJsonValue(QJsonValue::Null)},
            {QStringLiteral("mode"), s.mode},
            {QStringLiteral("freq"), static_cast<double>(s.frequencyHz)},
            {QStringLiteral("source"), s.source},
            {QStringLiteral("entity"), s.entity},
            {QStringLiteral("comment"), s.comment},
            {QStringLiteral("ts"), round1(static_cast<double>(s.lastSeen) / 1000.0)},
            {QStringLiteral("age"), round1(now - static_cast<double>(s.lastSeen) / 1000.0)},
            {QStringLiteral("count"), s.count},
        });
    }
    return out;
}

} // namespace decolog::core
