#include "core/DecoPortLog.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QMessageAuthenticationCode>
#include <QNetworkDatagram>
#include <QPasswordDigestor>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStringList>
#include <QUdpSocket>

namespace decolog::core {

namespace decoportlog {

namespace {

constexpr quint32 kMagic = 0x44505254u;   // "DPRT"

void putU8(QByteArray& out, quint8 v) { out.append(static_cast<char>(v)); }
void putU16(QByteArray& out, quint16 v)
{
    out.append(static_cast<char>(v >> 8));
    out.append(static_cast<char>(v & 0xFF));
}
void putU32(QByteArray& out, quint32 v)
{
    for (int shift = 24; shift >= 0; shift -= 8)
        out.append(static_cast<char>((v >> shift) & 0xFF));
}
void putU64(QByteArray& out, quint64 v)
{
    for (int shift = 56; shift >= 0; shift -= 8)
        out.append(static_cast<char>((v >> shift) & 0xFF));
}

quint32 getU32(const QByteArray& in, int at)
{
    quint32 v = 0;
    for (int i = 0; i < 4; ++i)
        v = (v << 8) | static_cast<quint8>(in.at(at + i));
    return v;
}
quint16 getU16(const QByteArray& in, int at)
{
    return static_cast<quint16>((static_cast<quint8>(in.at(at)) << 8) | static_cast<quint8>(in.at(at + 1)));
}
quint64 getU64(const QByteArray& in, int at)
{
    quint64 v = 0;
    for (int i = 0; i < 8; ++i)
        v = (v << 8) | static_cast<quint8>(in.at(at + i));
    return v;
}

void putString(QByteArray& out, const QString& text)
{
    const QByteArray utf8 = text.toUtf8().left(255);
    putU8(out, static_cast<quint8>(utf8.size()));
    out.append(utf8);
}

bool takeString(const QByteArray& in, int& at, QString* out)
{
    if (at >= in.size())
        return false;
    const int n = static_cast<quint8>(in.at(at));
    if (at + 1 + n > in.size())
        return false;
    *out = QString::fromUtf8(in.mid(at + 1, n));
    at += 1 + n;
    return true;
}

QByteArray authTag(const QByteArray& key, const QByteArray& signedBytes)
{
    QMessageAuthenticationCode mac(QCryptographicHash::Sha256, key);
    mac.addData(signedBytes);
    return mac.result().left(kAuthTagBytes);
}

// A tempo costante: un confronto normale racconta quanti byte erano giusti.
bool tagsEqual(const QByteArray& a, const QByteArray& b)
{
    if (a.size() != b.size())
        return false;
    quint8 diff = 0;
    for (int i = 0; i < a.size(); ++i)
        diff |= static_cast<quint8>(a.at(i)) ^ static_cast<quint8>(b.at(i));
    return diff == 0;
}

} // namespace

QString typeName(quint8 type)
{
    switch (type) {
    case LogAnnounce: return QStringLiteral("LOG_ANNOUNCE");
    case LogQso: return QStringLiteral("LOG_QSO");
    case LogAck: return QStringLiteral("LOG_ACK");
    default: return QStringLiteral("TYPE %1").arg(type);
    }
}

QByteArray deriveKey(const QString& password)
{
    const QString trimmed = password.trimmed();
    if (trimmed.isEmpty())
        return {};
    // Il sale e' una costante del formato DecoPort, non un residuo: con un altro
    // la stessa password darebbe un'altra chiave e HH non entrerebbe piu'.
    static const QByteArray salt = QByteArrayLiteral("Decodium-DecoPort-v1");
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256, trimmed.toUtf8(), salt, 200000, 32);
}

QByteArray buildPacket(quint8 type, quint32 streamId, quint32 sequence, const QByteArray& payload,
                       const QByteArray& key, qint64 unixMilliseconds)
{
    QByteArray pkt;
    pkt.reserve(kHeaderBytes + payload.size() + kAuthTagBytes);
    quint16 flags = kFlagTimestamp;
    if (!key.isEmpty())
        flags |= kFlagSigned;
    putU32(pkt, kMagic);
    putU8(pkt, kVersion);
    putU8(pkt, type);
    putU16(pkt, flags);
    putU32(pkt, streamId);
    putU32(pkt, sequence);
    putU32(pkt, static_cast<quint32>(unixMilliseconds / 1000));
    putU32(pkt, static_cast<quint32>((unixMilliseconds % 1000) * 1000000));
    putU16(pkt, static_cast<quint16>(qMin<qsizetype>(payload.size(), 0xFFFF)));
    putU16(pkt, 0);
    pkt.append(payload);
    if (!key.isEmpty())
        pkt.append(authTag(key, pkt));   // firma su intestazione + payload
    return pkt;
}

Verdict parsePacket(const QByteArray& datagram, const QByteArray& key, qint64 nowUnixSeconds, Packet* out)
{
    if (datagram.size() < kHeaderBytes || getU32(datagram, 0) != kMagic
        || static_cast<quint8>(datagram.at(4)) != kVersion)
        return Verdict::Malformed;
    const quint16 flags = getU16(datagram, 6);
    const int length = getU16(datagram, 24);
    if (kHeaderBytes + length > datagram.size())
        return Verdict::Malformed;
    if (!(flags & kFlagSigned) || key.isEmpty())
        return Verdict::Unsigned;
    const int signedLen = kHeaderBytes + length;
    if (datagram.size() < signedLen + kAuthTagBytes)
        return Verdict::Malformed;
    if (!tagsEqual(authTag(key, datagram.left(signedLen)), datagram.mid(signedLen, kAuthTagBytes)))
        return Verdict::BadSignature;
    const qint64 seconds = getU32(datagram, 16);
    if (!(flags & kFlagTimestamp) || qAbs(nowUnixSeconds - seconds) > kReplayWindowSeconds)
        return Verdict::Stale;
    if (out) {
        out->type = static_cast<quint8>(datagram.at(5));
        out->streamId = getU32(datagram, 8);
        out->sequence = getU32(datagram, 12);
        out->unixSeconds = seconds;
        out->payload = datagram.mid(kHeaderBytes, length);
    }
    return Verdict::Ok;
}

QByteArray encodeAnnounce(const Announce& a)
{
    QByteArray out;
    putU32(out, (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3));
    putString(out, a.program);
    putString(out, a.version);
    putString(out, a.station);
    putU16(out, a.sessionPort);
    return out;
}

std::optional<Announce> decodeAnnounce(const QByteArray& payload)
{
    if (payload.size() < 4)
        return std::nullopt;
    const quint32 mask = getU32(payload, 0);
    int at = 4;
    Announce a;
    if ((mask & (1u << 0)) && !takeString(payload, at, &a.program))
        return std::nullopt;
    if ((mask & (1u << 1)) && !takeString(payload, at, &a.version))
        return std::nullopt;
    if ((mask & (1u << 2)) && !takeString(payload, at, &a.station))
        return std::nullopt;
    if (mask & (1u << 3)) {
        if (at + 2 > payload.size())
            return std::nullopt;
        a.sessionPort = getU16(payload, at);
    }
    return a;
}

QByteArray encodeQso(qint64 id, bool resent, const QByteArray& adif)
{
    QByteArray out;
    putU64(out, static_cast<quint64>(id));
    putU8(out, resent ? 1 : 0);
    out.append(adif);
    return out;
}

std::optional<Qso> decodeQso(const QByteArray& payload)
{
    if (payload.size() < 9)
        return std::nullopt;
    Qso q;
    q.id = static_cast<qint64>(getU64(payload, 0));
    q.resent = (static_cast<quint8>(payload.at(8)) & 1) != 0;
    q.adif = payload.mid(9);
    return q;
}

QByteArray encodeAck(qint64 id)
{
    QByteArray out;
    putU64(out, static_cast<quint64>(id));
    return out;
}

std::optional<qint64> decodeAck(const QByteArray& payload)
{
    if (payload.size() < 8)
        return std::nullopt;
    return static_cast<qint64>(getU64(payload, 0));
}

QString summarize(quint8 type, const QByteArray& payload)
{
    switch (type) {
    case LogAnnounce: {
        const auto a = decodeAnnounce(payload);
        if (!a)
            return {};
        QStringList parts;
        parts << QStringLiteral("%1 %2").arg(a->program, a->version).trimmed();
        if (!a->station.isEmpty())
            parts << a->station;
        parts << QStringLiteral("session %1").arg(a->sessionPort);
        return parts.join(QStringLiteral(" · "));
    }
    case LogQso: {
        const auto q = decodeQso(payload);
        if (!q)
            return {};
        QString call;
        const QString text = QString::fromUtf8(q->adif);
        static const QRegularExpression re(QStringLiteral("<CALL:(\\d+)[^>]*>"), QRegularExpression::CaseInsensitiveOption);
        const auto m = re.match(text);
        if (m.hasMatch())
            call = text.mid(m.capturedEnd(), m.captured(1).toInt());
        return QStringLiteral("QSO #%1%2%3").arg(q->id).arg(call.isEmpty() ? QString() : QStringLiteral(" · ") + call,
                                                           q->resent ? QStringLiteral(" · resent") : QString());
    }
    case LogAck: {
        const auto id = decodeAck(payload);
        return id ? QStringLiteral("ack #%1").arg(*id) : QString();
    }
    default:
        return {};
    }
}

} // namespace decoportlog

DecoPortLogServer::DecoPortLogServer(QObject* parent)
    : QObject(parent)
{
    m_streamId = QRandomGenerator::global()->generate();
    connect(&m_timer, &QTimer::timeout, this, &DecoPortLogServer::tick);
}

DecoPortLogServer::~DecoPortLogServer()
{
    stop();
}

void DecoPortLogServer::setIdentity(const QString& version, const QString& station)
{
    m_version = version;
    m_station = station;
}

void DecoPortLogServer::setTimings(int announceMs, int retryMs, int timeoutMs)
{
    m_announceMs = announceMs;
    m_retryMs = retryMs;
    m_timeoutMs = timeoutMs;
    if (m_timer.isActive())
        m_timer.setInterval(qMax(10, qMin(qMin(announceMs, retryMs), timeoutMs) / 2));
}

qint64 DecoPortLogServer::nowMs() const
{
    return QDateTime::currentMSecsSinceEpoch();
}

QString DecoPortLogServer::keyOf(const QHostAddress& address, quint16 port)
{
    bool v4 = false;
    const quint32 ip = address.toIPv4Address(&v4);
    return QStringLiteral("%1:%2").arg(v4 ? QHostAddress(ip).toString() : address.toString()).arg(port);
}

bool DecoPortLogServer::start(quint16 port, const QString& password)
{
    stop();
    m_key = decoportlog::deriveKey(password);
    if (m_key.isEmpty()) {
        m_lastError = tr("no password: DecoPort does not run unsigned");
        return false;
    }
    m_socket = new QUdpSocket(this);
    // Solo loopback: il log non si espone alla rete.
    if (!m_socket->bind(QHostAddress::LocalHost, port)) {
        m_lastError = m_socket->errorString();
        delete m_socket;
        m_socket = nullptr;
        return false;
    }
    m_lastError.clear();
    connect(m_socket, &QUdpSocket::readyRead, this, &DecoPortLogServer::readDatagrams);
    m_lastAnnounceMs = 0;
    m_timer.start(qMax(10, qMin(qMin(m_announceMs, m_retryMs), m_timeoutMs) / 2));
    return true;
}

void DecoPortLogServer::stop()
{
    m_timer.stop();
    if (m_socket) {
        // Chi e' registrato lo sa: un BYE non firmabile da chi se ne va non esiste,
        // ma il client si accorge dal silenzio (dodici secondi).
        m_socket->close();
        delete m_socket;
        m_socket = nullptr;
    }
    const bool hadClients = !m_clients.isEmpty();
    m_clients.clear();
    m_pending.clear();
    m_bad.clear();
    if (hadClients)
        emit clientsChanged();
}

quint16 DecoPortLogServer::port() const
{
    return m_socket ? m_socket->localPort() : 0;
}

void DecoPortLogServer::announce(const QHostAddress& to, quint16 port)
{
    if (!m_socket || port == 0)
        return;
    decoportlog::Announce a;
    a.program = QStringLiteral("DecoDXLog");
    a.version = m_version;
    a.station = m_station;
    a.sessionPort = m_socket->localPort();
    m_socket->writeDatagram(decoportlog::buildPacket(decoportlog::LogAnnounce, m_streamId, m_seqAnnounce++,
                                                     decoportlog::encodeAnnounce(a), m_key, nowMs()),
                            to, port);
}

void DecoPortLogServer::sendTo(const Client& client, quint8 type, const QByteArray& payload)
{
    if (!m_socket)
        return;
    m_socket->writeDatagram(decoportlog::buildPacket(type, m_streamId, m_seqQso++, payload, m_key, nowMs()),
                            client.address, client.port);
}

void DecoPortLogServer::readDatagrams()
{
    while (m_socket && m_socket->hasPendingDatagrams()) {
        const QNetworkDatagram d = m_socket->receiveDatagram();
        const QString sender = keyOf(d.senderAddress(), static_cast<quint16>(d.senderPort()));
        const qint64 now = nowMs();
        if (const auto known = m_bad.constFind(sender); known != m_bad.constEnd() && known->bannedUntilMs > now)
            continue;   // silenzio, come DecoPort: non si dice a chi prova che ha trovato la porta

        decoportlog::Packet p;
        const auto verdict = decoportlog::parsePacket(d.data(), m_key, now / 1000, &p);
        if (verdict == decoportlog::Verdict::BadSignature || verdict == decoportlog::Verdict::Unsigned) {
            if (m_bad.size() > 256)
                m_bad.clear();
            Bad& bad = m_bad[sender];
            bad.times.append(now);
            while (!bad.times.isEmpty() && bad.times.first() < now - kBadWindowMs)
                bad.times.removeFirst();
            if (bad.times.size() >= kBadLimit) {
                bad.bannedUntilMs = now + kBanMs;
                bad.times.clear();
                emit diagnostic(tr("DecoPort: %1 ignored for %2 minutes, wrong or missing signature")
                                    .arg(sender).arg(kBanMs / 60000));
            }
            continue;
        }
        if (verdict != decoportlog::Verdict::Ok)
            continue;

        Client client{d.senderAddress(), static_cast<quint16>(d.senderPort()), now};
        switch (p.type) {
        case decoportlog::Hello:
        case decoportlog::Keepalive: {
            const bool isNew = !m_clients.contains(sender);
            m_clients.insert(sender, client);
            // L'annuncio e' la risposta: il client sa di essere registrato, e
            // a ogni keepalive sa che il log c'e' ancora.
            announce(client.address, client.port);
            if (isNew)
                emit clientsChanged();
            break;
        }
        case decoportlog::Bye:
            if (m_clients.remove(sender) > 0) {
                m_pending.removeIf([&](const Pending& x) { return x.client == sender; });
                emit clientsChanged();
            }
            break;
        case decoportlog::LogAck: {
            const auto id = decoportlog::decodeAck(p.payload);
            const auto known = m_clients.find(sender);
            if (!id || known == m_clients.end())
                break;
            known->lastSeenMs = now;
            const qsizetype removed = m_pending.removeIf([&](const Pending& x) { return x.client == sender && x.id == *id; });
            if (removed > 0) {
                ++m_acked;
                emit qsoAcked(*id);
            }
            break;
        }
        default:
            break;
        }
    }
}

int DecoPortLogServer::publishQso(qint64 id, const QString& adif, bool resent)
{
    if (!m_socket || m_clients.isEmpty())
        return 0;
    const QByteArray bytes = adif.toUtf8();
    if (bytes.isEmpty() || bytes.size() > decoportlog::kMaxAdifBytes) {
        emit diagnostic(tr("DecoPort: QSO %1 not sent, the record is empty or too long").arg(id));
        return 0;
    }
    const qint64 now = nowMs();
    int sent = 0;
    for (auto it = m_clients.cbegin(); it != m_clients.cend(); ++it) {
        sendTo(it.value(), decoportlog::LogQso, decoportlog::encodeQso(id, resent, bytes));
        m_pending.append({it.key(), id, resent, bytes, now, 1});
        ++sent;
        ++m_sent;
    }
    return sent;
}

void DecoPortLogServer::tick()
{
    if (!m_socket)
        return;
    const qint64 now = nowMs();

    // Chi tace da troppo non c'e' piu'.
    bool changed = false;
    for (auto it = m_clients.begin(); it != m_clients.end();) {
        if (now - it.value().lastSeenMs > m_timeoutMs) {
            const QString key = it.key();
            m_pending.removeIf([&](const Pending& x) { return x.client == key; });
            it = m_clients.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (changed)
        emit clientsChanged();

    // Un QSO senza conferma si rimanda; dopo il terzo tentativo si lascia.
    for (int i = 0; i < m_pending.size();) {
        Pending& p = m_pending[i];
        if (now - p.sentMs < m_retryMs) {
            ++i;
            continue;
        }
        if (p.tries >= kMaxTries || !m_clients.contains(p.client)) {
            const qint64 id = p.id;
            m_pending.removeAt(i);
            emit qsoGaveUp(id);
            continue;
        }
        sendTo(m_clients.value(p.client), decoportlog::LogQso, decoportlog::encodeQso(p.id, true, p.adif));
        p.sentMs = now;
        ++p.tries;
        ++i;
    }

    if (now - m_lastAnnounceMs >= m_announceMs) {
        m_lastAnnounceMs = now;
        if (m_announcePort != m_socket->localPort())
            announce(QHostAddress::LocalHost, m_announcePort);
    }
}

} // namespace decolog::core
