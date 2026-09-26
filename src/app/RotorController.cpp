#include "app/RotorController.h"

#include "core/Maidenhead.h"

#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QNetworkInterface>
#include <QSerialPortInfo>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>

namespace decolog::app {

using namespace decolog::core;

namespace {

int defaultPortFor(const QString& backend)
{
    // DecoRotor: WebSocket 8765. rotctld: 4532, perche' sulla 4533 e 4534 ci
    // sono gia' il CAT share e lo spot share di Decodium.
    return backend == QLatin1String("rotctld") ? 4532 : 8765;
}

} // namespace

RotorController::RotorController(Context context, QObject* parent)
    : QObject(parent)
    , m_ctx(std::move(context))
{
    QSettings s;
    m_enabled = s.value(QStringLiteral("rotor/enabled"), false).toBool();
    // Di serie il gateway integrato: DecoDXLog comanda il control box da se'.
    m_backend = s.value(QStringLiteral("rotor/backend"), QStringLiteral("builtin")).toString();
    m_host = s.value(QStringLiteral("rotor/host"), QStringLiteral("127.0.0.1")).toString();
    m_port = s.value(QStringLiteral("rotor/port"), defaultPortFor(m_backend)).toInt();
    m_followDx = s.value(QStringLiteral("rotor/followDx"), false).toBool();
    m_beamwidth = qBound(5, s.value(QStringLiteral("rotor/beamwidth"), 45).toInt(), 180);

    m_httpPort = qBound(1, s.value(QStringLiteral("rotor/httpPort"), 8080).toInt(), 65535);

    // Il gateway integrato: di serie le stesse porte di DecoRotor, cosi' l'app
    // sul telefono e i programmi di stazione non vanno toccati.
    m_gw.serialPort = s.value(QStringLiteral("rotorGateway/serialPort")).toString();
    m_gw.model = s.value(QStringLiteral("rotorGateway/model"), QStringLiteral("auto")).toString();
    m_gw.simulate = s.value(QStringLiteral("rotorGateway/simulate"), false).toBool();
    m_gw.wsPort = s.value(QStringLiteral("rotorGateway/wsPort"), 8765).toInt();
    m_gw.rotctldPort = s.value(QStringLiteral("rotorGateway/rotctldPort"), 4532).toInt();
    m_gw.bind = s.value(QStringLiteral("rotorGateway/bind"), QStringLiteral("0.0.0.0")).toString();
    m_gw.tileCacheDir = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                            .filePath(QStringLiteral("rotor-tiles"));

    connect(&m_link, &RotorLink::stateChanged, this, [this] {
        // Il verso di rotazione non lo dice il gateway: si legge da come
        // cambia l'azimut, come fa il posto di comando.
        const core::RotorState& s = m_link.state();
        if (!s.moving) {
            m_sense = 0;
        } else if (m_lastAz >= 0.0) {
            double delta = s.az - m_lastAz;
            while (delta > 180.0) delta -= 360.0;
            while (delta < -180.0) delta += 360.0;
            if (qAbs(delta) > 0.05)
                m_sense = delta > 0 ? 1 : -1;
        }
        m_lastAz = s.az;
        emit stateChanged();
    });
    connect(&m_link, &RotorLink::presetsChanged, this, &RotorController::presetsChanged);
    connect(&m_link, &RotorLink::trafficChanged, this, &RotorController::trafficChanged);
    connect(&m_link, &RotorLink::historyChanged, this, &RotorController::historyChanged);
    connect(&m_link, &RotorLink::bearingReady, this, [this](const QVariantMap& bearing) {
        m_bearing = bearing;
        emit bearingChanged();
    });
    connect(&m_link, &RotorLink::note, this, [this](const QString& text, const QString& level) {
        note(text, level);
    });
}

void RotorController::start()
{
    if (m_enabled)
        apply();
}

void RotorController::overrideConnection(const QString& backend, const QString& host, int port)
{
    // "builtin@COM3" o "builtin@sim": il gateway integrato, per questa volta.
    if (backend == QLatin1String("builtin")) {
        m_backend = backend;
        if (host == QLatin1String("sim"))
            m_gw.simulate = true;
        else if (!host.trimmed().isEmpty())
            m_gw.serialPort = host.trimmed();
        if (port > 0)
            m_gw.wsPort = port;
        m_enabled = true;
        apply();
        emit stateChanged();
        return;
    }
    m_backend = backend == QLatin1String("rotctld") ? QStringLiteral("rotctld") : QStringLiteral("decorotor");
    if (!host.trimmed().isEmpty())
        m_host = host.trimmed();
    if (port > 0)
        m_port = port;
    m_enabled = true;
    apply();
    emit stateChanged();
}

void RotorController::apply()
{
    if (!m_enabled || !builtin()) {
        if (m_gateway)
            m_gateway->stop();
    }
    if (!m_enabled) {
        m_link.stop();
        emit changed();
        emit stateChanged();
        return;
    }
    const QString token = QSettings().value(QStringLiteral("rotor/token")).toString();
    if (builtin()) {
        // DecoDXLog fa da DecoRotor: apre la seriale e le porte, e poi ci si
        // collega come si collegherebbe l'app.
        if (!m_gateway) {
            m_gateway = new core::RotorGateway(this);
            connect(m_gateway, &core::RotorGateway::note, this,
                    [this](const QString& text, const QString& level) { note(tr("Rotor: %1").arg(text), level); });
            connect(m_gateway, &core::RotorGateway::liveChanged, this, &RotorController::saveGateway);
        }
        core::GatewaySettings settings = m_gw;
        settings.token = token;
        settings.httpPort = m_httpPort;
        core::GatewayLive live = core::GatewayLive::fromJson(
            QJsonDocument::fromJson(QSettings().value(QStringLiteral("rotorGateway/live")).toByteArray()).object());
        if (m_ctx.stationGrid && !m_ctx.stationGrid().trimmed().isEmpty())
            live.locator = m_ctx.stationGrid().trimmed().toUpper();
        if (m_ctx.stationCall)
            live.callsign = m_ctx.stationCall().trimmed().toUpper();
        live.beamwidth = m_beamwidth;
        m_gateway->start(settings, live);
        m_link.start(RotorLink::Backend::DecoRotor, QStringLiteral("127.0.0.1"), m_gw.wsPort, token);
        emit changed();
        emit stateChanged();
        return;
    }
    m_link.start(m_backend == QLatin1String("rotctld") ? RotorLink::Backend::Rotctld
                                                       : RotorLink::Backend::DecoRotor,
                 m_host, m_port, token);
    emit changed();
    emit stateChanged();
}

void RotorController::note(const QString& text, const QString& level)
{
    if (m_ctx.activity)
        m_ctx.activity(QStringLiteral("ROTOR"), text, level);
}

// ── Impostazioni ──────────────────────────────────────────────────────────────

void RotorController::setEnabled(bool enabled)
{
    if (enabled == m_enabled)
        return;
    m_enabled = enabled;
    QSettings().setValue(QStringLiteral("rotor/enabled"), enabled);
    apply();
}

void RotorController::setBackend(const QString& backend)
{
    const QString value = backend == QLatin1String("rotctld") || backend == QLatin1String("builtin")
                              ? backend : QStringLiteral("decorotor");
    if (value == m_backend)
        return;
    // Cambiando modo cambia anche la porta solita: si sposta, a meno che non sia
    // stata scelta a mano una diversa da tutte e due le predefinite.
    const bool defaultPort = m_port == defaultPortFor(m_backend);
    m_backend = value;
    if (defaultPort)
        m_port = defaultPortFor(value);
    QSettings s;
    s.setValue(QStringLiteral("rotor/backend"), m_backend);
    s.setValue(QStringLiteral("rotor/port"), m_port);
    apply();
}

void RotorController::setHost(const QString& host)
{
    const QString value = host.trimmed();
    if (value == m_host)
        return;
    m_host = value;
    QSettings().setValue(QStringLiteral("rotor/host"), value);
    apply();
}

void RotorController::setPort(int port)
{
    if (port <= 0 || port > 65535 || port == m_port)
        return;
    m_port = port;
    QSettings().setValue(QStringLiteral("rotor/port"), port);
    apply();
}

void RotorController::setHttpPort(int port)
{
    const int value = qBound(1, port, 65535);
    if (value == m_httpPort)
        return;
    m_httpPort = value;
    QSettings().setValue(QStringLiteral("rotor/httpPort"), value);
    emit changed();
}

QString RotorController::tileEndpoint() const
{
    // Con DecoRotor in piedi i riquadri arrivano da lui; con rotctld non c'e'
    // nessun gateway, e la mappa si arrangia con quella stradale.
    if (!m_enabled || m_backend == QLatin1String("rotctld"))
        return {};
    if (builtin())
        return QStringLiteral("http://127.0.0.1:%1/tiles/").arg(m_httpPort);
    return QStringLiteral("http://%1:%2/tiles/").arg(m_host).arg(m_httpPort);
}

QString RotorController::uptimeText() const
{
    const qint64 seconds = static_cast<qint64>(m_link.state().uptime);
    if (seconds < 60)
        return tr("%1 s").arg(seconds);
    if (seconds < 3600)
        return tr("%1 m").arg(seconds / 60);
    return tr("%1 h %2 m").arg(seconds / 3600).arg((seconds % 3600) / 60);
}

QVariantList RotorController::endpoints() const
{
    // Le tre porte le serve il gateway: se risponde lui, ci sono tutte.
    const bool up = m_link.state().linkUp;
    const bool deco = m_backend != QLatin1String("rotctld");
    if (builtin()) {
        // Il gateway e' questo computer: al telefono serve il suo indirizzo
        // nella rete di casa.
        QString lan = QStringLiteral("127.0.0.1");
        for (const QHostAddress& a : QNetworkInterface::allAddresses()) {
            if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback() && !a.isLinkLocal()
                && a.toString().startsWith(QLatin1String("192.168."))) {
                lan = a.toString();
                break;
            }
        }
        const QStringList problems = gatewayProblems();
        auto open = [&problems](const QString& prefix) {
            for (const QString& p : problems)
                if (p.startsWith(prefix))
                    return false;
            return true;
        };
        return QVariantList{
            QVariantMap{{QStringLiteral("role"), tr("APP (WebSocket)")},
                        {QStringLiteral("address"), QStringLiteral("%1:%2").arg(lan).arg(m_gw.wsPort)},
                        {QStringLiteral("active"), up && open(QStringLiteral("WebSocket"))}},
            QVariantMap{{QStringLiteral("role"), tr("WEB UI")},
                        {QStringLiteral("address"), QStringLiteral("%1:%2").arg(lan).arg(m_httpPort)},
                        {QStringLiteral("active"), m_gateway && m_gateway->running() && open(QStringLiteral("web"))}},
            QVariantMap{{QStringLiteral("role"), tr("ROTCTLD (Hamlib)")},
                        {QStringLiteral("address"), QStringLiteral("%1:%2").arg(lan).arg(m_gw.rotctldPort)},
                        {QStringLiteral("active"), m_gateway && m_gateway->running() && open(QStringLiteral("rotctld"))}},
        };
    }
    return QVariantList{
        QVariantMap{{QStringLiteral("role"), tr("APP (WebSocket)")},
                    {QStringLiteral("address"), QStringLiteral("%1:%2").arg(m_host).arg(deco ? m_port : 8765)},
                    {QStringLiteral("active"), up && deco}},
        QVariantMap{{QStringLiteral("role"), tr("WEB UI")},
                    {QStringLiteral("address"), QStringLiteral("%1:%2").arg(m_host).arg(m_httpPort)},
                    {QStringLiteral("active"), up && deco}},
        QVariantMap{{QStringLiteral("role"), tr("ROTCTLD (Hamlib)")},
                    {QStringLiteral("address"), QStringLiteral("%1:%2").arg(m_host).arg(deco ? 4532 : m_port)},
                    {QStringLiteral("active"), up}},
    };
}

void RotorController::refreshDiagnostics()
{
    if (!m_enabled)
        return;
    m_link.requestTraffic(60);
    m_link.requestHistory(300);
}

void RotorController::setSetting(const QString& key, const QVariant& value)
{
    if (!m_enabled || key.isEmpty())
        return;
    m_link.setConfig(QVariantMap{{key, value}});
    note(tr("Rotor: %1 set on the gateway").arg(key), QStringLiteral("info"));
}

void RotorController::setLimit(const QString& key, double value)
{
    if (!m_enabled || key.isEmpty())
        return;
    // I finecorsa vanno mandati insieme: il gateway vuole l'oggetto intero.
    const core::RotorState& s = m_link.state();
    QVariantMap limits{{QStringLiteral("az_min"), s.azMin}, {QStringLiteral("az_max"), s.azMax}};
    limits.insert(key, value);
    m_link.setConfig(QVariantMap{{QStringLiteral("limits"), limits}});
}

void RotorController::recallPreset(const QString& name)
{
    if (!m_enabled)
        return;
    m_link.recallPreset(name);
    m_lastTarget = name;
    note(tr("Rotor to %1").arg(name), QStringLiteral("info"));
    emit stateChanged();
}

void RotorController::savePresetHere(const QString& name)
{
    if (!m_enabled || name.trimmed().isEmpty())
        return;
    m_link.savePreset(name, m_link.state().az, -1.0);
    note(tr("Rotor: memory \"%1\" at %2°").arg(name.trimmed()).arg(qRound(m_link.state().az)),
         QStringLiteral("info"));
}

void RotorController::deletePreset(const QString& name)
{
    if (m_enabled)
        m_link.deletePreset(name);
}

void RotorController::askBearing(const QString& locator)
{
    if (m_enabled)
        m_link.requestBearing(locator);
}

void RotorController::gotoPosition(double az, double el)
{
    if (!m_enabled)
        return;
    if (az >= 0.0)
        pointTo(az, QString());
    else if (el >= 0.0)
        m_link.goTo(m_link.state().az, el);
}

void RotorController::setFollowDx(bool follow)
{
    if (follow == m_followDx)
        return;
    m_followDx = follow;
    m_followedCall.clear();
    QSettings().setValue(QStringLiteral("rotor/followDx"), follow);
    emit changed();
}

void RotorController::setBeamwidth(int degrees)
{
    const int value = qBound(5, degrees, 180);
    if (value == m_beamwidth)
        return;
    m_beamwidth = value;
    QSettings().setValue(QStringLiteral("rotor/beamwidth"), value);
    emit changed();
}

// ── Stato ─────────────────────────────────────────────────────────────────────

QVariantMap RotorController::state() const
{
    QVariantMap map = m_link.state().toMap();
    map.insert(QStringLiteral("enabled"), m_enabled);
    // Il lobo lo dice il gateway; quello delle impostazioni di DecoDXLog serve
    // solo quando dall'altra parte c'e' un rotctld, che non lo sa.
    if (!m_link.state().beamwidthKnown)
        map.insert(QStringLiteral("beamwidth"), m_beamwidth);
    map.insert(QStringLiteral("backend"), m_backend);
    return map;
}

QString RotorController::status() const
{
    if (!m_enabled)
        return tr("Rotor off");
    const RotorState& s = m_link.state();
    if (!s.connected && builtin()) {
        // Il gateway e' qui: se il control box non risponde, si dice perche'.
        if (!s.error.isEmpty())
            return s.error;
        return m_gw.simulate ? tr("Simulated control box, starting…")
                             : tr("Opening the control box on %1…").arg(m_gw.serialPort.isEmpty() ? QStringLiteral("—")
                                                                                                    : m_gw.serialPort);
    }
    if (!s.connected) {
        return m_backend == QLatin1String("rotctld")
            ? tr("Looking for rotctld on %1:%2…").arg(m_host).arg(m_port)
            : tr("Looking for DecoRotor on %1:%2…").arg(m_host).arg(m_port);
    }
    if (!s.error.isEmpty())
        return s.error;
    if (s.moving && s.azTarget >= 0.0)
        return tr("Turning to %1°").arg(qRound(s.azTarget));
    return s.modelLabel.isEmpty() ? tr("Rotor connected") : s.modelLabel;
}

// ── Comandi ───────────────────────────────────────────────────────────────────

void RotorController::pointTo(double azimuth, const QString& what)
{
    if (!m_enabled) {
        note(tr("The rotor is off: Setup → Rotor"), QStringLiteral("warning"));
        return;
    }
    const double target = rotor::normalize(azimuth);
    m_link.goTo(target);
    m_lastTarget = what.trimmed().isEmpty() ? tr("%1°").arg(qRound(target))
                                            : tr("%1 · %2°").arg(what.trimmed()).arg(qRound(target));
    note(tr("Rotor to %1").arg(m_lastTarget), QStringLiteral("info"));
    emit stateChanged();
}

void RotorController::pointLocator(const QString& locator, bool longPath)
{
    if (!m_enabled || locator.trimmed().size() < 4)
        return;
    if (m_backend == QLatin1String("rotctld")) {
        note(tr("rotctld does not do locators: point in degrees"), QStringLiteral("warning"));
        return;
    }
    m_link.goToLocator(locator, longPath);
    m_lastTarget = locator.trimmed().toUpper();
    note(tr("Rotor to %1").arg(m_lastTarget), QStringLiteral("info"));
    emit stateChanged();
}

void RotorController::stopNow(bool fast)
{
    if (!m_enabled)
        return;
    m_link.halt(fast);
    note(fast ? tr("Rotor: quick stop") : tr("Rotor: stop"), QStringLiteral("warning"));
}

void RotorController::park()
{
    if (!m_enabled)
        return;
    m_link.park();
    note(tr("Rotor: park"), QStringLiteral("info"));
}

void RotorController::nudge(double degrees)
{
    if (!m_enabled || !m_link.state().connected)
        return;
    const RotorState& s = m_link.state();
    const double from = s.azTarget >= 0.0 ? s.azTarget : s.az;
    m_link.goTo(rotor::normalize(from + degrees));
}

void RotorController::reconnect()
{
    if (m_enabled)
        apply();
}

void RotorController::dxBearing(const QString& call, double azimuth)
{
    if (azimuth < 0.0 || call.trimmed().isEmpty())
        return;
    // Il DX scelto lo si ricorda sempre: il pannello lo mostra, e "Punta il DX"
    // ci va con un clic. Andarci da solo e' solo se "segui" e' acceso.
    const QVariantMap target{{QStringLiteral("call"), call.trimmed().toUpper()},
                             {QStringLiteral("azimuth"), qRound(rotor::normalize(azimuth))}};
    if (target != m_dxTarget) {
        m_dxTarget = target;
        emit dxTargetChanged();
    }
    if (!m_enabled || !m_followDx)
        return;
    const QString who = call.trimmed().toUpper();
    if (who.isEmpty() || who == m_followedCall)
        return;
    m_followedCall = who;
    pointTo(azimuth, who);
}

void RotorController::pointToDx()
{
    if (m_dxTarget.isEmpty())
        return;
    pointTo(m_dxTarget.value(QStringLiteral("azimuth")).toDouble(), m_dxTarget.value(QStringLiteral("call")).toString());
}

double RotorController::bearingTo(double lat, double lon) const
{
    QString grid = m_ctx.stationGrid ? m_ctx.stationGrid().trimmed() : QString();
    if (grid.isEmpty())
        grid = m_link.state().locator;
    const auto home = maidenhead::toLatLon(grid);
    if (!home)
        return -1.0;
    return maidenhead::azimuthDeg(*home, maidenhead::LatLon{lat, lon});
}

// ── Il gateway integrato ──────────────────────────────────────────────────────

void RotorController::setGatewaySerialPort(const QString& port)
{
    if (port.trimmed() == m_gw.serialPort)
        return;
    m_gw.serialPort = port.trimmed();
    QSettings().setValue(QStringLiteral("rotorGateway/serialPort"), m_gw.serialPort);
    if (m_enabled && builtin())
        apply();
    emit changed();
}

void RotorController::setGatewayModel(const QString& model)
{
    const QString value = core::prosistel::modelFor(model) ? model : QStringLiteral("auto");
    if (value == m_gw.model)
        return;
    m_gw.model = value;
    QSettings().setValue(QStringLiteral("rotorGateway/model"), value);
    if (m_enabled && builtin())
        apply();
    emit changed();
}

void RotorController::setGatewaySimulate(bool on)
{
    if (on == m_gw.simulate)
        return;
    m_gw.simulate = on;
    QSettings().setValue(QStringLiteral("rotorGateway/simulate"), on);
    if (m_enabled && builtin())
        apply();
    emit changed();
}

void RotorController::setGatewayWsPort(int port)
{
    if (port <= 0 || port > 65535 || port == m_gw.wsPort)
        return;
    m_gw.wsPort = port;
    QSettings().setValue(QStringLiteral("rotorGateway/wsPort"), port);
    if (m_enabled && builtin())
        apply();
    emit changed();
}

void RotorController::setGatewayRotctldPort(int port)
{
    if (port < 0 || port > 65535 || port == m_gw.rotctldPort)
        return;
    m_gw.rotctldPort = port;
    QSettings().setValue(QStringLiteral("rotorGateway/rotctldPort"), port);
    if (m_enabled && builtin())
        apply();
    emit changed();
}

QStringList RotorController::serialPorts() const
{
    QStringList out;
    for (const QSerialPortInfo& info : QSerialPortInfo::availablePorts())
        out << info.portName();
    std::sort(out.begin(), out.end(), [](const QString& a, const QString& b) {
        return a.size() != b.size() ? a.size() < b.size() : a < b;
    });
    return out;
}

QString RotorController::importDecoRotor(const QString& path)
{
    QStringList candidates;
    if (!path.trimmed().isEmpty())
        candidates << path.trimmed();
    candidates << QStringLiteral("C:/decorotor/gateway/config.json")
               << QDir::home().filePath(QStringLiteral("decorotor/gateway/config.json"))
               << QDir::home().filePath(QStringLiteral("Documents/decorotor/gateway/config.json"));
    for (const QString& candidate : std::as_const(candidates)) {
        QFile file(candidate);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject c = QJsonDocument::fromJson(file.readAll()).object();
        if (c.isEmpty())
            continue;
        QSettings s;
        if (c.contains(QStringLiteral("port"))) {
            m_gw.serialPort = c.value(QStringLiteral("port")).toString();
            s.setValue(QStringLiteral("rotorGateway/serialPort"), m_gw.serialPort);
        }
        if (c.contains(QStringLiteral("model"))) {
            const QString model = c.value(QStringLiteral("model")).toString();
            m_gw.model = core::prosistel::modelFor(model) ? model : QStringLiteral("auto");
            s.setValue(QStringLiteral("rotorGateway/model"), m_gw.model);
        }
        if (c.contains(QStringLiteral("simulate"))) {
            m_gw.simulate = c.value(QStringLiteral("simulate")).toBool();
            s.setValue(QStringLiteral("rotorGateway/simulate"), m_gw.simulate);
        }
        if (c.value(QStringLiteral("ws_port")).toInt() > 0) {
            m_gw.wsPort = c.value(QStringLiteral("ws_port")).toInt();
            s.setValue(QStringLiteral("rotorGateway/wsPort"), m_gw.wsPort);
        }
        if (c.value(QStringLiteral("rotctld_port")).toInt() > 0) {
            m_gw.rotctldPort = c.value(QStringLiteral("rotctld_port")).toInt();
            s.setValue(QStringLiteral("rotorGateway/rotctldPort"), m_gw.rotctldPort);
        }
        if (c.value(QStringLiteral("http_port")).toInt() > 0) {
            m_httpPort = c.value(QStringLiteral("http_port")).toInt();
            s.setValue(QStringLiteral("rotor/httpPort"), m_httpPort);
        }
        if (c.contains(QStringLiteral("token")))
            s.setValue(QStringLiteral("rotor/token"), c.value(QStringLiteral("token")).toString());
        // Finecorsa, riposo, sicurezza e memorie: quello che l'app cambia a caldo.
        core::GatewayLive live = core::GatewayLive::fromJson(
            QJsonDocument::fromJson(s.value(QStringLiteral("rotorGateway/live")).toByteArray()).object());
        QJsonObject merged = live.toJson();
        for (const char* key : {"park_az", "park_el", "tolerance", "stall_timeout", "stop_on_client_loss",
                                "limits", "presets", "beamwidth", "my_locator", "callsign"}) {
            if (c.contains(QLatin1String(key)))
                merged.insert(QLatin1String(key), c.value(QLatin1String(key)));
        }
        s.setValue(QStringLiteral("rotorGateway/live"), QJsonDocument(merged).toJson(QJsonDocument::Compact));
        note(tr("Rotor: settings taken from DecoRotor (%1)").arg(QDir::toNativeSeparators(candidate)),
             QStringLiteral("info"));
        if (m_enabled && builtin())
            apply();
        emit changed();
        return candidate;
    }
    note(tr("Rotor: DecoRotor's config.json not found"), QStringLiteral("warning"));
    return {};
}

QVariantList RotorController::gatewayModels() const
{
    QVariantList out{QVariantMap{{QStringLiteral("key"), QStringLiteral("auto")},
                                 {QStringLiteral("label"), tr("Detect by itself")}}};
    for (const core::prosistel::Model& m : core::prosistel::models())
        out << QVariantMap{{QStringLiteral("key"), m.key}, {QStringLiteral("label"), m.label}};
    return out;
}

void RotorController::saveGateway()
{
    if (!m_gateway)
        return;
    QSettings().setValue(QStringLiteral("rotorGateway/live"),
                         QJsonDocument(m_gateway->live().toJson()).toJson(QJsonDocument::Compact));
}

void RotorController::stationChanged()
{
    if (m_gateway && m_gateway->running() && m_ctx.stationGrid)
        m_gateway->setStation(m_ctx.stationGrid(), m_ctx.stationCall ? m_ctx.stationCall() : QString());
}

} // namespace decolog::app
