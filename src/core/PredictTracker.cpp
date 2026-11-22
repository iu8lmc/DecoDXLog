#include "core/PredictTracker.h"
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
bool PredictTracker::request(const QString &host, quint16 port, const QString &satellite)
{
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9 ._+/-]{0,24}$"));
    QHostAddress address;
    if (m_socket || !address.setAddress(host) || !port || !valid.match(satellite).hasMatch())
        return false;
    m_host = address;
    m_port = port;
    m_satellite = satellite;
    m_stage = 0;
    m_pending.clear();
    m_snapshot.clear();
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
    if (m_stage == 5) {
        m_snapshot = m_pending;
        emit finished(true, {});
        return;
    }
    static const char *commands[] = {"GET_MODE", "GET_SAT ", "GET_DOPPLER ", "GET_TLE ", "GET_MODE"};
    QByteArray command(commands[m_stage]);
    if (m_stage > 0 && m_stage < 4)
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
        if (m_stage == 0 || m_stage == 4) {
            const QString mode = QString::fromLatin1(raw).trimmed();
            if (mode != QLatin1String("MULTI") && mode.compare(m_satellite, Qt::CaseInsensitive) != 0) {
                fail(QStringLiteral("PREDICT is not tracking the selected satellite"));
                return;
            }
            m_pending.insert(QStringLiteral("trackingMode"), mode);
        } else if (m_stage == 1) {
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
            m_pending.insert(QStringLiteral("satellite"), m_satellite);
            m_pending.insert(QStringLiteral("azimuth"), az);
            m_pending.insert(QStringLiteral("elevation"), el);
            m_pending.insert(QStringLiteral("nextEventUnix"), next);
        } else if (m_stage == 2) {
            bool ok = false;
            const double shift = raw.trimmed().toDouble(&ok);
            if (!ok || !std::isfinite(shift)) {
                fail(QStringLiteral("Invalid PREDICT Doppler response"));
                return;
            }
            m_pending.insert(QStringLiteral("doppler100MHz"), shift);
        } else if (m_stage == 3) {
            if (lines.size() < 3 ||
                QString::fromLatin1(lines[0]).trimmed().compare(m_satellite, Qt::CaseInsensitive) != 0 ||
                !lines[1].startsWith("1 ") || !lines[2].startsWith("2 ") || lines[1].size() < 32) {
                fail(QStringLiteral("Invalid PREDICT TLE response"));
                return;
            }
            bool yearOk = false, dayOk = false;
            const int yy = lines[1].mid(18, 2).toInt(&yearOk);
            const double day = lines[1].mid(20, 12).toDouble(&dayOk);
            const int year = yy >= 57 ? 1900 + yy : 2000 + yy;
            if (!yearOk || !dayOk || !std::isfinite(day) || day < 1 || day >= (QDate::isLeapYear(year) ? 367 : 366)) {
                fail(QStringLiteral("Invalid PREDICT TLE epoch"));
                return;
            }
            const auto epoch =
                QDateTime(QDate(year, 1, 1), QTime(0, 0), QTimeZone::UTC).addMSecs(qRound64((day - 1.0) * 86400000.0));
            m_pending.insert(QStringLiteral("tleEpoch"), epoch);
            m_pending.insert(QStringLiteral("receivedAt"), QDateTime::currentDateTimeUtc());
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
    m_snapshot.clear();
    emit finished(false, error);
}
} // namespace decolog::core
