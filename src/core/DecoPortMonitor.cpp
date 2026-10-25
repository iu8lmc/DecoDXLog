#include "core/DecoPortMonitor.h"

#include "core/DecoPortLog.h"

#include <QDataStream>
#include <QNetworkDatagram>
#include <QStringList>
#include <QUdpSocket>

namespace decolog::core {

namespace decoport {

namespace {

QString typeName(quint8 type)
{
    switch (type) {
    case 1: return QStringLiteral("ANNOUNCE");
    case 2: return QStringLiteral("HELLO");
    case 3: return QStringLiteral("BYE");
    case 4: return QStringLiteral("KEEPALIVE");
    case 5: return QStringLiteral("CONTEXT");
    case 6: return QStringLiteral("COMMAND");
    case 7: return QStringLiteral("AUDIO_RX");
    case 8: return QStringLiteral("AUDIO_TX");
    case 9: return QStringLiteral("STATUS");
    case 16: case 17: case 18: return decoportlog::typeName(type);
    default: return QStringLiteral("TYPE %1").arg(type);
    }
}

QString modeName(quint8 mode)
{
    static const char* names[] = {"?", "USB", "LSB", "CW", "CW-R", "AM", "FM", "DIG-U", "DIG-L", "RTTY", "RTTY-R", "PKT-FM"};
    return mode < sizeof(names) / sizeof(names[0]) ? QLatin1String(names[mode]) : QStringLiteral("mode %1").arg(mode);
}

// I campi di contesto: una maschera, poi i presenti in ordine di bit.
QString contextSummary(QDataStream& in)
{
    quint32 mask = 0;
    in >> mask;
    if (in.status() != QDataStream::Ok)
        return {};
    QStringList parts;
    auto has = [mask](int bit) { return (mask & (1u << bit)) != 0; };
    if (has(0)) {
        qint64 hz = 0;
        in >> hz;
        parts << QStringLiteral("%1 MHz").arg(static_cast<double>(hz) / 1e6, 0, 'f', 6);
    }
    if (has(1)) {
        quint8 mode = 0;
        in >> mode;
        parts << modeName(mode);
    }
    if (has(2)) {
        quint8 ptt = 0;
        in >> ptt;
        parts << (ptt ? QStringLiteral("PTT ON") : QStringLiteral("PTT off"));
    }
    if (has(3)) {
        qint16 s = 0;
        in >> s;
        parts << QStringLiteral("S %1 dBm").arg(s / 10.0, 0, 'f', 1);
    }
    if (has(4)) {
        quint32 rate = 0;
        in >> rate;
        parts << QStringLiteral("%1 Hz").arg(rate);
    }
    if (has(5)) {
        quint8 channels = 0;
        in >> channels;
        parts << QStringLiteral("%1 ch").arg(channels);
    }
    if (has(6)) {
        quint32 bw = 0;
        in >> bw;
        parts << QStringLiteral("BW %1 Hz").arg(bw);
    }
    if (has(7)) {
        quint8 len = 0;
        in >> len;
        QByteArray label(len, '\0');
        in.readRawData(label.data(), len);
        parts << QStringLiteral("\"%1\"").arg(QString::fromUtf8(label));
    }
    if (has(8)) {
        quint32 flags = 0;
        in >> flags;
        QStringList f;
        if (flags & 1u) f << QStringLiteral("CAT");
        if (flags & 2u) f << QStringLiteral("audio in");
        if (flags & 4u) f << QStringLiteral("audio out");
        if (flags & 8u) f << QStringLiteral("can TX");
        if (flags & 16u) f << QStringLiteral("TX taken");
        parts << QStringLiteral("[%1]").arg(f.join(QStringLiteral(", ")));
    }
    if (has(9)) {
        quint16 lead = 0;
        in >> lead;
        parts << QStringLiteral("lead %1 ms").arg(lead);
    }
    if (has(10)) {
        quint16 port = 0;
        in >> port;
        parts << QStringLiteral("session %1").arg(port);
    }
    if (has(11)) { quint16 v = 0; in >> v; parts << QStringLiteral("%1 W").arg(v / 10.0, 0, 'f', 1); }
    if (has(12)) { quint16 v = 0; in >> v; parts << QStringLiteral("SWR %1").arg(v / 100.0, 0, 'f', 2); }
    if (has(13)) { qint16 v = 0; in >> v; parts << QStringLiteral("ALC %1%").arg(v / 10.0, 0, 'f', 1); }
    if (has(14)) { quint16 v = 0; in >> v; parts << QStringLiteral("%1 V").arg(v / 100.0, 0, 'f', 2); }
    if (has(15)) { quint16 v = 0; in >> v; parts << QStringLiteral("%1 A").arg(v / 100.0, 0, 'f', 2); }
    if (has(16)) { qint16 v = 0; in >> v; parts << QStringLiteral("%1 °C").arg(v / 10.0, 0, 'f', 1); }
    if (has(17)) { quint16 v = 0; in >> v; parts << QStringLiteral("comp %1 dB").arg(v / 10.0, 0, 'f', 1); }
    if (has(18)) { quint16 v = 0; in >> v; parts << QStringLiteral("power %1%").arg(v / 10.0, 0, 'f', 1); }
    return parts.join(QStringLiteral(" · "));
}

} // namespace

Description describe(const QByteArray& packet)
{
    Description d;
    if (packet.size() < kHeaderBytes) {
        d.typeName = QStringLiteral("?");
        d.summary = QStringLiteral("%1 bytes, too short for DecoPort").arg(packet.size());
        return d;
    }
    QDataStream in(packet);
    in.setByteOrder(QDataStream::BigEndian);
    quint32 magic = 0;
    quint8 version = 0;
    quint16 flags = 0;
    quint32 seconds = 0, nanos = 0;
    quint16 length = 0, reserved = 0;
    in >> magic >> version >> d.type >> flags >> d.streamId >> d.sequence >> seconds >> nanos >> length >> reserved;
    if (magic != kMagic) {
        d.typeName = QStringLiteral("?");
        d.summary = QStringLiteral("%1 bytes, not DecoPort").arg(packet.size());
        return d;
    }
    d.valid = true;
    d.authenticated = (flags & 2u) != 0;
    d.typeName = typeName(d.type);
    QString body;
    switch (d.type) {
    case 1: case 5: case 6: case 9:
        body = contextSummary(in);
        break;
    case 7: case 8:
        // PCM 16 bit: dieci millisecondi per pacchetto.
        body = QStringLiteral("%1 bytes of audio").arg(length);
        break;
    case 16: case 17: case 18:
        // Il log (docs/DECOPORT-LOG.md): annuncio, QSO, conferma.
        body = decoportlog::summarize(d.type, packet.mid(kHeaderBytes, length));
        break;
    default:
        break;
    }
    // Il flusso sta nella colonna del programma: qui solo il numero.
    QStringList parts;
    parts << QStringLiteral("#%1").arg(d.sequence);
    if (!body.isEmpty())
        parts << body;
    if (d.authenticated)
        parts << QStringLiteral("signed");
    d.summary = parts.join(QStringLiteral(" · "));
    return d;
}

QByteArray encodeContext(const Context& c)
{
    QByteArray out;
    QDataStream s(&out, QIODevice::WriteOnly);
    s.setByteOrder(QDataStream::BigEndian);
    const QByteArray label = c.rigLabel.toUtf8().left(255);
    const quint32 mask = (1u << 0) | (1u << 1) | (1u << 7) | (1u << 8) | (1u << 10);
    s << mask << c.rfFrequencyHz << c.mode << static_cast<quint8>(label.size());
    s.writeRawData(label.constData(), static_cast<int>(label.size()));
    s << c.stateFlags << c.sessionPort;
    return out;
}

QByteArray buildPacket(quint8 type, quint32 streamId, quint32 sequence, const QByteArray& payload)
{
    QByteArray out;
    QDataStream s(&out, QIODevice::WriteOnly);
    s.setByteOrder(QDataStream::BigEndian);
    s << kMagic << quint8(1) << type << quint16(0) << streamId << sequence << quint32(0) << quint32(0)
      << static_cast<quint16>(payload.size()) << quint16(0);
    out.append(payload);
    return out;
}

} // namespace decoport

DecoPortListener::DecoPortListener(QObject* parent)
    : QObject(parent)
{
}

DecoPortListener::~DecoPortListener()
{
    stop();
}

bool DecoPortListener::start(quint16 port)
{
    stop();
    m_socket = new QUdpSocket(this);
    // Porta condivisa: gli altri client DecoPort (un Decodium remoto sulla
    // stessa macchina) devono continuare a sentire gli annunci.
    if (!m_socket->bind(QHostAddress(QHostAddress::AnyIPv4), port,
                        QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        m_lastError = m_socket->errorString();
        delete m_socket;
        m_socket = nullptr;
        return false;
    }
    m_lastError.clear();
    connect(m_socket, &QUdpSocket::readyRead, this, [this] {
        while (m_socket && m_socket->hasPendingDatagrams()) {
            const QNetworkDatagram d = m_socket->receiveDatagram();
            bool v4 = false;
            const quint32 ip = d.senderAddress().toIPv4Address(&v4);
            const QString from = (v4 ? QHostAddress(ip) : d.senderAddress()).toString();
            emit traffic(QStringLiteral("in"), QStringLiteral("%1:%2").arg(from).arg(d.senderPort()), d.data());
        }
    });
    return true;
}

void DecoPortListener::stop()
{
    if (!m_socket)
        return;
    m_socket->close();
    m_socket->deleteLater();
    m_socket = nullptr;
}

bool DecoPortListener::listening() const
{
    return m_socket != nullptr;
}

} // namespace decolog::core
