#include "core/PredictTracker.h"
#include "core/Tle.h"
#include <QDateTime>
#include <QRegularExpression>
#include <QTimeZone>
#include <QUdpSocket>
#include <cmath>
namespace decolog::core {
PredictTracker::PredictTracker(QObject *parent) : QObject(parent)
{
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(1200);
    connect(&m_timeout, &QTimer::timeout, this, [this] { fail(QStringLiteral("PREDICT request timed out")); });
}
QVariantMap PredictTracker::publicStatus() const
{
    const QDateTime now=QDateTime::currentDateTimeUtc();
    const QDateTime received=m_snapshot.value(QStringLiteral("receivedAt")).toDateTime();
    const QDateTime epoch=m_snapshot.value(QStringLiteral("tleEpoch")).toDateTime();
    const bool available=!m_snapshot.isEmpty() && received.isValid();
    const bool fresh=available && !m_busy && received<=now && received.msecsTo(now)<=10000;
    const bool orbitalFresh=epoch.isValid() && epoch<=now && epoch.secsTo(now)<=14*86400;
    QVariantMap result{{QStringLiteral("source"),QStringLiteral("predict_udp")},{QStringLiteral("available"),available},
        {QStringLiteral("busy"),m_busy},{QStringLiteral("fresh"),fresh},{QStringLiteral("orbitalFresh"),orbitalFresh}};
    if (available) {
        result.insert(QStringLiteral("satellite"),m_snapshot.value(QStringLiteral("satellite")));
        result.insert(QStringLiteral("azimuth"),m_snapshot.value(QStringLiteral("azimuth")));
        result.insert(QStringLiteral("elevation"),m_snapshot.value(QStringLiteral("elevation")));
        result.insert(QStringLiteral("doppler100MHz"),m_snapshot.value(QStringLiteral("doppler100MHz")));
        result.insert(QStringLiteral("nextEventUnix"),m_snapshot.value(QStringLiteral("nextEventUnix")));
        result.insert(QStringLiteral("receivedAt"),received.toUTC().toString(Qt::ISODateWithMs));
        result.insert(QStringLiteral("tleEpoch"),epoch.toUTC().toString(Qt::ISODateWithMs));
        result.insert(QStringLiteral("tleCatalogue"),m_snapshot.value(QStringLiteral("tleCatalogue")));
    }
    return result;
}

QVariantMap PredictTracker::nominalTransponderPair(double txCenterMHz, double rxCenterMHz,
                                                    double offsetKhz, double halfWidthKhz,
                                                    const QString& mapping) const
{
    const auto failResult=[](const QString& error) { return QVariantMap{{QStringLiteral("ok"),false},{QStringLiteral("error"),error}}; };
    if (!std::isfinite(txCenterMHz) || !std::isfinite(rxCenterMHz) || !std::isfinite(offsetKhz) ||
        !std::isfinite(halfWidthKhz) || txCenterMHz<=0 || rxCenterMHz<=0 || txCenterMHz>1000000 || rxCenterMHz>1000000)
        return failResult(tr("Enter finite positive nominal frequencies."));
    double tx=txCenterMHz, rx=rxCenterMHz;
    if (mapping == QLatin1String("independent")) {
        if (offsetKhz != 0) return failResult(tr("Independent frequencies do not use a transponder offset."));
    } else if (mapping == QLatin1String("inverting") || mapping == QLatin1String("noninverting")) {
        if (halfWidthKhz<=0 || halfWidthKhz>1000000 || std::abs(offsetKhz)>halfWidthKhz)
            return failResult(tr("Enter a positive half bandwidth and an offset inside it."));
        tx += offsetKhz/1000.0;
        rx += (mapping == QLatin1String("inverting") ? -offsetKhz : offsetKhz)/1000.0;
    } else return failResult(tr("Unsupported transponder mapping."));
    if (!std::isfinite(tx) || !std::isfinite(rx) || tx<=0 || rx<=0 || tx>1000000 || rx>1000000)
        return failResult(tr("The resulting nominal frequencies are invalid."));
    return {{QStringLiteral("ok"),true},{QStringLiteral("txMHz"),tx},{QStringLiteral("rxMHz"),rx}};
}

QVariantMap PredictTracker::correctedFrequencies(double txMHz, double rxMHz) const
{
    const auto failResult = [](const QString &error) {
        return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
    };
    if (m_busy || m_snapshot.isEmpty())
        return failResult(tr("A current PREDICT reading is required"));
    if (!std::isfinite(txMHz) || !std::isfinite(rxMHz) || txMHz <= 0 || rxMHz <= 0 || txMHz > 1000000 ||
        rxMHz > 1000000)
        return failResult(tr("Enter valid positive TX and RX frequencies in MHz"));
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QDateTime received = m_snapshot.value(QStringLiteral("receivedAt")).toDateTime();
    const QDateTime epoch = m_snapshot.value(QStringLiteral("tleEpoch")).toDateTime();
    if (!received.isValid() || received > now || received.msecsTo(now) > 10000)
        return failResult(tr("The tracker reading has expired; read it again"));
    if (!epoch.isValid() || epoch > now || epoch.secsTo(now) > 14 * 86400)
        return failResult(tr("Orbital data is old or dated in the future"));
    const double doppler = m_snapshot.value(QStringLiteral("doppler100MHz")).toDouble();
    const double correctedTx = txMHz - (doppler * txMHz / 100.0) / 1000000.0;
    const double correctedRx = rxMHz + (doppler * rxMHz / 100.0) / 1000000.0;
    if (!std::isfinite(correctedTx) || !std::isfinite(correctedRx) || correctedTx <= 0 || correctedRx <= 0)
        return failResult(tr("Doppler correction produced an invalid frequency"));
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("txMHz"), correctedTx},
            {QStringLiteral("rxMHz"), correctedRx},
            {QStringLiteral("satellite"), m_satellite}};
}
bool PredictTracker::request(const QString &host, int port, const QString &satellite)
{
    static const QRegularExpression valid(QStringLiteral("^[\\x21-\\x7e][\\x20-\\x7e]{0,24}$"));
    QHostAddress address;
    if (m_busy || !address.setAddress(host) || port <= 0 || port > 65535 || !valid.match(satellite).hasMatch())
        return false;
    m_host = address;
    m_port = port;
    m_satellite = satellite;
    m_stage = 0;
    m_listing = false;
    m_pending.clear();
    m_pendingTle.clear();
    m_snapshot.clear();
    m_busy = true;
    emit changed();
    sendNext();
    return true;
}
bool PredictTracker::requestSatellites(const QString &host, int port)
{
    QHostAddress address;
    if (m_busy || !address.setAddress(host) || port <= 0 || port > 65535)
        return false;
    m_host = address;
    m_port = port;
    m_stage = 0;
    m_listing = true;
    m_satellites.clear();
    m_pendingSatellites.clear();
    m_listMode.clear();
    m_busy = true;
    emit changed();
    sendNext();
    return true;
}
void PredictTracker::sendNext()
{
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    if (m_listing && m_stage == 3) {
        m_satellites = m_pendingSatellites;
        m_busy = false;
        emit changed();
        emit satellitesFinished(true, {});
        return;
    }
    if (!m_listing && m_stage == 6) {
        m_snapshot = m_pending;
        m_busy = false;
        emit changed();
        emit finished(true, {});
        return;
    }
    static const char *commands[] = {"GET_MODE", "GET_TLE ", "GET_SAT ", "GET_DOPPLER ", "GET_TLE ", "GET_MODE"};
    QByteArray command = m_listing ? (m_stage == 1 ? QByteArrayLiteral("GET_LIST") : QByteArrayLiteral("GET_MODE"))
                                   : QByteArray(commands[m_stage]);
    if (!m_listing && m_stage > 0 && m_stage < 5)
        command += m_satellite.toLatin1();
    command += '\n';
    m_socket = new QUdpSocket(this);
    connect(m_socket, &QUdpSocket::readyRead, this, &PredictTracker::receive);
    if (!m_socket->bind(
            m_host.protocol() == QAbstractSocket::IPv6Protocol ? QHostAddress::AnyIPv6 : QHostAddress::AnyIPv4, 0)) {
        fail(QStringLiteral("Cannot bind PREDICT socket"));
        return;
    }
    if (m_socket->writeDatagram(command, m_host, m_port) != command.size()) {
        fail(QStringLiteral("Cannot send PREDICT request"));
        return;
    }
    m_timeout.start();
}
void PredictTracker::receive()
{
    if (!m_socket)
        return;
    while (m_socket->hasPendingDatagrams()) {
        const auto size = m_socket->pendingDatagramSize();
        if (size < 0 || size > 8192) {
            fail(QStringLiteral("Invalid PREDICT response size"));
            return;
        }
        QByteArray raw(int(size), Qt::Uninitialized);
        QHostAddress from;
        quint16 port;
        m_socket->readDatagram(raw.data(), raw.size(), &from, &port);
        if (from != m_host || port != m_port)
            continue;
        m_timeout.stop();
        const auto lines = raw.trimmed().split('\n');
        if (m_listing) {
            if (m_stage == 0 || m_stage == 2) {
                const QString mode = QString::fromLatin1(raw).trimmed();
                static const QRegularExpression valid(QStringLiteral("^[\\x21-\\x7e][\\x20-\\x7e]{0,24}$"));
                if (!valid.match(mode).hasMatch() || mode == QLatin1String("NONE") || mode == QLatin1String("Huh?") ||
                    (m_stage == 2 && mode != m_listMode)) {
                    fail(QStringLiteral("PREDICT tracking mode unavailable or changed"));
                    return;
                }
                m_listMode = mode;
            } else {
                static const QRegularExpression valid(QStringLiteral("^[\\x21-\\x7e][\\x20-\\x7e]{0,24}$"));
                for (const QByteArray &line : lines) {
                    const QString name = QString::fromLatin1(line).trimmed();
                    if (name.isEmpty())
                        continue;
                    if (!valid.match(name).hasMatch() || name == QLatin1String("Huh?")) {
                        fail(QStringLiteral("Invalid PREDICT satellite list"));
                        return;
                    }
                    if (!m_pendingSatellites.contains(name, Qt::CaseInsensitive) &&
                        (m_listMode == QLatin1String("MULTI") || m_listMode.compare(name, Qt::CaseInsensitive) == 0))
                        m_pendingSatellites.append(name);
                }
                if (m_pendingSatellites.isEmpty()) {
                    fail(QStringLiteral("PREDICT returned no tracked satellites"));
                    return;
                }
            }
        } else if (m_stage == 0 || m_stage == 5) {
            const QString mode = QString::fromLatin1(raw).trimmed();
            if ((mode != QLatin1String("MULTI") && mode.compare(m_satellite, Qt::CaseInsensitive) != 0) ||
                (m_stage == 5 && mode.compare(m_pending.value(QStringLiteral("trackingMode")).toString(), Qt::CaseInsensitive) != 0)) {
                fail(QStringLiteral("PREDICT is not tracking the selected satellite"));
                return;
            }
            m_pending.insert(QStringLiteral("trackingMode"), mode);
        } else if (m_stage == 2) {
            if (lines.size() < 12 ||
                QString::fromLatin1(lines[0]).trimmed().compare(m_satellite, Qt::CaseInsensitive) != 0) {
                fail(QStringLiteral("Invalid PREDICT position response"));
                return;
            }
            bool azOk = false, elOk = false, nextOk = false;
            double az = lines[3].trimmed().toDouble(&azOk), el = lines[4].trimmed().toDouble(&elOk);
            qint64 next = lines[5].trimmed().toLongLong(&nextOk);
            if (!azOk || !elOk || !nextOk || !std::isfinite(az) || !std::isfinite(el) || az < 0 || az > 360 ||
                el < -90 || el > 90 || next < 0) {
                fail(QStringLiteral("Invalid PREDICT position values"));
                return;
            }
            m_pending.insert(QStringLiteral("receivedAt"), QDateTime::currentDateTimeUtc());
            m_pending.insert(QStringLiteral("satellite"), m_satellite);
            m_pending.insert(QStringLiteral("azimuth"), az);
            m_pending.insert(QStringLiteral("elevation"), el);
            m_pending.insert(QStringLiteral("nextEventUnix"), next);
        } else if (m_stage == 3) {
            bool ok = false;
            const double shift = raw.trimmed().toDouble(&ok);
            if (!ok || !std::isfinite(shift)) {
                fail(QStringLiteral("Invalid PREDICT Doppler response"));
                return;
            }
            m_pending.insert(QStringLiteral("doppler100MHz"), shift);
        } else if (m_stage == 1 || m_stage == 4) {
            if (lines.size() < 3 ||
                QString::fromLatin1(lines[0]).trimmed().compare(m_satellite, Qt::CaseInsensitive) != 0 ||
                !lines[1].startsWith("1 ") || !lines[2].startsWith("2 ") || lines[1].size() < 32) {
                fail(QStringLiteral("Invalid PREDICT TLE response"));
                return;
            }
            const auto checked=tle::inspect(lines[1].trimmed(),lines[2].trimmed());
            if (!checked.ok) {
                fail(QStringLiteral("Invalid PREDICT TLE: %1").arg(checked.error));
                return;
            }
            const QByteArray currentTle=lines[1].trimmed() + '\n' + lines[2].trimmed();
            if (m_stage == 1) {
                m_pendingTle=currentTle;
                m_pending.insert(QStringLiteral("tleEpoch"),checked.epoch);
                m_pending.insert(QStringLiteral("tleCatalogue"),checked.catalogue);
            } else if (currentTle!=m_pendingTle) {
                fail(QStringLiteral("Orbital data changed during tracker reading"));
                return;
            }
        }
        ++m_stage;
        sendNext();
        return;
    }
}
void PredictTracker::fail(const QString &error)
{
    m_timeout.stop();
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_pending.clear();
    m_pendingTle.clear();
    if (m_listing) {
        m_pendingSatellites.clear();
        m_satellites.clear();
    } else {
        m_snapshot.clear();
    }
    m_busy = false;
    emit changed();
    if (m_listing)
        emit satellitesFinished(false, error);
    else
        emit finished(false, error);
}
} // namespace decolog::core
