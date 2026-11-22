#include "core/PredictPassPlanner.h"
#include <QDateTime>
#include <QRegularExpression>
#include <QTimeZone>
#include <QUdpSocket>
#include <cmath>
namespace decolog::core {
PredictPassPlanner::PredictPassPlanner(QObject *parent) : QObject(parent)
{
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(6000);
    connect(&m_timeout, &QTimer::timeout, this, [this] { fail(QStringLiteral("Pass prediction timed out")); });
}
bool PredictPassPlanner::request(const QString &host, int port, const QString &satellite, qint64 startUnix)
{
    // PREDICT 2.2.x passes these arguments to a shell. Keep the quoted name
    // strictly within an allowlist, excluding quotes, substitution and metacharacters.
    static const QRegularExpression safe(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9 ._+()/\\-]{0,24}$"));
    QHostAddress ip;
    if (m_busy || !ip.setAddress(host) || port <= 0 || port > 65535 || !safe.match(satellite).hasMatch() ||
        startUnix <= 0 || startUnix > 4102444800LL)
        return false;
    m_host = ip;
    m_port = port;
    m_satellite = satellite;
    m_start = startUnix;
    m_stage = 0;
    m_points.clear();
    m_stream.clear();
    m_pass.clear();
    m_tle.clear();
    m_busy = true;
    emit changed();
    sendNext();
    return true;
}
void PredictPassPlanner::sendNext()
{
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    if (m_stage == 3) {
        int maximum = -90;
        for (const QVariant &item : m_points)
            maximum = qMax(maximum, item.toMap().value(QStringLiteral("elevation")).toInt());
        m_pass.insert(QStringLiteral("satellite"), m_satellite);
        m_pass.insert(QStringLiteral("aos"), m_points.first().toMap().value(QStringLiteral("time")));
        m_pass.insert(QStringLiteral("los"), m_points.last().toMap().value(QStringLiteral("time")));
        m_pass.insert(QStringLiteral("maxElevation"), maximum);
        m_pass.insert(QStringLiteral("points"), m_points);
        m_busy = false;
        emit changed();
        emit finished(true, {});
        return;
    }
    const QByteArray command = m_stage == 1 ? QByteArrayLiteral("PREDICT \"") + m_satellite.toLatin1() +
                                                  QByteArrayLiteral("\" ") + QByteArray::number(m_start) + '\n'
                                            : QByteArrayLiteral("GET_TLE ") + m_satellite.toLatin1() + '\n';
    m_socket = new QUdpSocket(this);
    connect(m_socket, &QUdpSocket::readyRead, this, &PredictPassPlanner::receive);
    if (!m_socket->bind(
            m_host.protocol() == QAbstractSocket::IPv6Protocol ? QHostAddress::AnyIPv6 : QHostAddress::AnyIPv4, 0) ||
        m_socket->writeDatagram(command, m_host, m_port) != command.size()) {
        fail(QStringLiteral("Cannot send pass request"));
        return;
    }
    m_timeout.start();
}
void PredictPassPlanner::receive()
{
    if (!m_socket)
        return;
    while (m_socket->hasPendingDatagrams()) {
        const qint64 size = m_socket->pendingDatagramSize();
        if (size < 0 || size > 8192) {
            fail(QStringLiteral("Invalid pass response size"));
            return;
        }
        QByteArray raw(int(size), Qt::Uninitialized);
        QHostAddress from;
        quint16 port;
        m_socket->readDatagram(raw.data(), raw.size(), &from, &port);
        if (from != m_host || port != m_port)
            continue;
        if (m_stage != 1) {
            const auto lines = raw.trimmed().split('\n');
            if (lines.size() < 3 ||
                QString::fromLatin1(lines[0]).trimmed().compare(m_satellite, Qt::CaseInsensitive) != 0 ||
                !lines[1].startsWith("1 ") || !lines[2].startsWith("2 ") || lines[1].size() < 32) {
                fail(QStringLiteral("Invalid TLE response"));
                return;
            }
            if (m_stage == 0) {
                bool yOk = false, dOk = false;
                int yy = lines[1].mid(18, 2).toInt(&yOk);
                double day = lines[1].mid(20, 12).toDouble(&dOk);
                int year = yy >= 57 ? 1900 + yy : 2000 + yy;
                if (!yOk || !dOk || !std::isfinite(day) || day < 1 || day >= (QDate::isLeapYear(year) ? 367 : 366)) {
                    fail(QStringLiteral("Invalid TLE epoch"));
                    return;
                }
                m_pass.insert(QStringLiteral("tleEpoch"), QDateTime(QDate(year, 1, 1), QTime(0, 0), QTimeZone::UTC)
                                                              .addMSecs(qRound64((day - 1) * 86400000.0)));
                m_tle = raw.trimmed();
            } else if (raw.trimmed() != m_tle) {
                fail(QStringLiteral("Orbital data changed during prediction"));
                return;
            }
            m_timeout.stop();
            ++m_stage;
            sendNext();
            return;
        }
        m_stream.append(raw);
        if (m_stream.size() > 8192) {
            fail(QStringLiteral("Pass response exceeds buffer limit"));
            return;
        }
        while (m_stream.contains('\n')) {
            const int newline = m_stream.indexOf('\n');
            const QByteArray line = m_stream.left(newline).trimmed();
            m_stream.remove(0, newline + 1);
            if (line == QByteArray(1, char(26))) {
                if (!m_stream.trimmed().isEmpty()) {
                    fail(QStringLiteral("Unexpected data after pass terminator"));
                    return;
                }
                if (m_points.size() < 2 ||
                    qAbs(m_points.first().toMap().value(QStringLiteral("elevation")).toInt()) > 1 ||
                    qAbs(m_points.last().toMap().value(QStringLiteral("elevation")).toInt()) > 1) {
                    fail(QStringLiteral("Incomplete pass prediction"));
                    return;
                }
                m_timeout.stop();
                ++m_stage;
                sendNext();
                return;
            }
            const auto tokens =
                QString::fromLatin1(line).split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
            if (tokens.size() < 6 || m_points.size() >= 1440) {
                fail(QStringLiteral("Invalid pass prediction"));
                return;
            }
            bool tOk = false, elOk = false, azOk = false;
            qint64 time = tokens[0].toLongLong(&tOk);
            int el = tokens[4].toInt(&elOk), az = tokens[5].toInt(&azOk);
            const qint64 last =
                m_points.isEmpty() ? 0 : m_points.last().toMap().value(QStringLiteral("time")).toLongLong();
            if (!tOk || !elOk || !azOk || time <= 0 || time > 4102444800LL || el < -1 || el > 90 || az < 0 ||
                az > 360 || (last && (time <= last || time - last > 180))) {
                fail(QStringLiteral("Incomplete or out-of-order pass data"));
                return;
            }
            m_points.append(QVariantMap{
                {QStringLiteral("time"), time}, {QStringLiteral("elevation"), el}, {QStringLiteral("azimuth"), az}});
        }
        // Timeout bounds the full streamed prediction, not each datagram.
    }
}
void PredictPassPlanner::cancel()
{
    if (m_busy)
        fail(QStringLiteral("Pass prediction cancelled"));
}
void PredictPassPlanner::fail(const QString &error)
{
    m_timeout.stop();
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_points.clear();
    m_pass.clear();
    m_busy = false;
    emit changed();
    emit finished(false, error);
}
} // namespace decolog::core
