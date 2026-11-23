#include "app/NetController.h"

#include "core/Adif.h"
#include "core/LogDatabase.h"

#include <QHostInfo>
#include <QSettings>
#include <QRegularExpression>
#include <QSqlQuery>
#include <QUuid>
#include <algorithm>

namespace decolog::app {

using namespace decolog::core;

namespace {
constexpr int kPeerTimeoutSecs = 30;
constexpr int kKeepMessages = 300;
constexpr int kResendBatch = 20;
}

NetController::NetController(Context context, QObject* parent)
    : QObject(parent)
    , m_ctx(std::move(context))
{
    QSettings s;
    m_enabled = s.value(QStringLiteral("net/enabled"), false).toBool();
    m_group = s.value(QStringLiteral("net/group"), QStringLiteral("DECODXLOG")).toString();
    m_name = s.value(QStringLiteral("net/station"), QHostInfo::localHostName()).toString();
    m_port = s.value(QStringLiteral("net/port"), 12060).toInt();
    m_id = s.value(QStringLiteral("net/id")).toString();
    if (m_id.isEmpty()) {
        m_id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
        s.setValue(QStringLiteral("net/id"), m_id);
    }
    connect(&m_net, &ContestNet::received, this,
            [this](const QString& from, const QString& type, const QJsonObject& body, const QHostAddress& sender) {
                if (m_peers.contains(from))
                    m_peers[from].address = sender.toString();
                onReceived(from, type, body);
            });
    m_hello.setInterval(5000);
    connect(&m_hello, &QTimer::timeout, this, &NetController::hello);
    m_resendTimer.setInterval(60);
    connect(&m_resendTimer, &QTimer::timeout, this, [this] {
        for (int i = 0; i < kResendBatch && !m_resend.isEmpty(); ++i) {
            const QJsonObject qso = qsoMessage(m_resend.takeFirst());
            if (!qso.isEmpty())
                send(QStringLiteral("qso"), qso);
        }
        if (m_resend.isEmpty())
            m_resendTimer.stop();
    });
    if (m_enabled)
        QTimer::singleShot(0, this, &NetController::restart);
    m_status = tr("off");
}

void NetController::setEnabled(bool on)
{
    if (on == m_enabled)
        return;
    m_enabled = on;
    QSettings().setValue(QStringLiteral("net/enabled"), on);
    restart();
    emit settingsChanged();
}

void NetController::setGroup(const QString& group)
{
    const QString g = group.trimmed().toUpper();
    if (g.isEmpty() || g == m_group)
        return;
    m_group = g;
    QSettings().setValue(QStringLiteral("net/group"), g);
    if (m_enabled)
        restart();
    emit settingsChanged();
}

void NetController::setStationName(const QString& name)
{
    const QString n = name.trimmed();
    if (n.isEmpty() || n == m_name)
        return;
    m_name = n;
    QSettings().setValue(QStringLiteral("net/station"), n);
    if (m_net.running()) {
        m_status = tr("on the network %1 (UDP %2) as %3").arg(m_group).arg(m_port).arg(m_name);
        hello();
        emit stateChanged();
    }
    emit settingsChanged();
}

void NetController::setPort(int port)
{
    if (port <= 1024 || port > 65535 || port == m_port)
        return;
    m_port = port;
    QSettings().setValue(QStringLiteral("net/port"), port);
    if (m_enabled)
        restart();
    emit settingsChanged();
}

void NetController::restart()
{
    m_net.stop();
    m_hello.stop();
    m_peers.clear();
    if (!m_enabled) {
        m_status = tr("off");
    } else if (m_net.start(static_cast<quint16>(m_port), m_group, m_id)) {
        m_status = tr("on the network %1 (UDP %2) as %3").arg(m_group).arg(m_port).arg(m_name);
        m_hello.start();
        hello();
        // Chi arriva a gara iniziata si fa mandare quello che manca.
        QTimer::singleShot(1500, this, &NetController::requestSync);
        if (m_ctx.activity)
            m_ctx.activity(QStringLiteral("NET"), m_status, QStringLiteral("info"));
    } else {
        m_status = tr("cannot open UDP %1: %2").arg(m_port).arg(m_net.lastError());
        if (m_ctx.activity)
            m_ctx.activity(QStringLiteral("NET"), m_status, QStringLiteral("warning"));
    }
    emit stateChanged();
    emit peersChanged();
}

void NetController::send(const QString& type, QJsonObject body)
{
    if (!m_net.running())
        return;
    body.insert(QStringLiteral("seq"), ++m_seq);
    body.insert(QStringLiteral("session"), m_sessionId);
    body.insert(QStringLiteral("protocol"), 2);
    body.insert(QStringLiteral("name"), m_name);
    m_net.send(type, body);
    ++m_sent;
    emit stateChanged();
}

void NetController::hello()
{
    QVariantMap here = m_ctx.here ? m_ctx.here() : QVariantMap();
    int qsos = 0;
    if (m_ctx.db && m_ctx.sessionStart) {
        const QDateTime start = m_ctx.sessionStart();
        if (start.isValid()) {
            QSqlQuery q(m_ctx.db->connection());
            q.prepare(QStringLiteral("SELECT COUNT(*) FROM qso WHERE deleted = 0 AND qso_datetime_on >= ?"));
            q.addBindValue(start.toString(Qt::ISODate));
            if (q.exec() && q.next())
                qsos = q.value(0).toInt();
        }
    }
    send(QStringLiteral("hello"), QJsonObject{{QStringLiteral("op"), here.value(QStringLiteral("op")).toString()},
                                              {QStringLiteral("band"), here.value(QStringLiteral("band")).toString()},
                                              {QStringLiteral("mode"), here.value(QStringLiteral("mode")).toString()},
                                              {QStringLiteral("khz"), here.value(QStringLiteral("freqKhz")).toDouble()},
                                              {QStringLiteral("qsos"), qsos}});
    // Chi non si fa sentire da mezzo minuto non c'e' piu'.
    const QDateTime now = QDateTime::currentDateTimeUtc();
    bool gone = false;
    for (auto it = m_peers.begin(); it != m_peers.end();) {
        if (it->seen.secsTo(now) > kPeerTimeoutSecs) {
            if (m_ctx.activity)
                m_ctx.activity(QStringLiteral("NET"), tr("%1 left the network").arg(it->name), QStringLiteral("warning"));
            it = m_peers.erase(it);
            gone = true;
        } else {
            ++it;
        }
    }
    if (gone)
        emit peersChanged();
}

QVariantList NetController::peers() const
{
    QList<Peer> list = m_peers.values();
    std::sort(list.begin(), list.end(), [](const Peer& a, const Peer& b) { return a.name < b.name; });
    QVariantList out;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (const Peer& p : list)
        out << QVariantMap{{QStringLiteral("id"), p.id},       {QStringLiteral("name"), p.name},
                           {QStringLiteral("op"), p.op},       {QStringLiteral("band"), p.band},
                           {QStringLiteral("mode"), p.mode},   {QStringLiteral("khz"), p.khz},
                           {QStringLiteral("qsos"), p.qsos},   {QStringLiteral("address"), p.address},
                           {QStringLiteral("age"), int(p.seen.secsTo(now))}};
    return out;
}

QJsonObject NetController::qsoMessage(qint64 id) const
{
    if (!m_ctx.db)
        return {};
    const auto record = m_ctx.db->record(id);
    if (!record)
        return {};
    return QJsonObject{{QStringLiteral("adif"), adif::writeRecord(*record)}};
}

QString NetController::queueCall(const QString& input)
{
    const QString call = input.trimmed().toUpper();
    static const QRegularExpression pattern(QStringLiteral("^[A-Z0-9/?]{1,32}$"));
    if (!pattern.match(call).hasMatch())
        return tr("Enter up to 32 letters, digits, / or ? without spaces.");
    if (m_queuedCalls.contains(call))
        return tr("This callsign is already queued.");
    if (m_queuedCalls.size() >= 100)
        return tr("The callsign queue is full (100 entries).");
    m_queuedCalls.append(call);
    emit queuedCallsChanged();
    return {};
}

bool NetController::moveQueuedCall(int index, int delta)
{
    const int target = index + delta;
    if (index < 0 || index >= m_queuedCalls.size() || (delta != -1 && delta != 1) ||
        target < 0 || target >= m_queuedCalls.size()) return false;
    m_queuedCalls.swapItemsAt(index, target);
    emit queuedCallsChanged();
    return true;
}

bool NetController::removeQueuedCall(int index)
{
    if (index < 0 || index >= m_queuedCalls.size()) return false;
    m_queuedCalls.removeAt(index);
    emit queuedCallsChanged();
    return true;
}

QString NetController::takeQueuedCall()
{
    if (m_queuedCalls.isEmpty()) return {};
    const QString call = m_queuedCalls.takeFirst();
    emit queuedCallsChanged();
    return call;
}

void NetController::qsoLogged(qint64 id)
{
    if (m_ctx.db) {
        if (const auto r = m_ctx.db->record(id))
            noteSerial(r->value(QStringLiteral("STX")));
    }
    if (!m_net.running())
        return;
    const QJsonObject qso = qsoMessage(id);
    if (!qso.isEmpty())
        send(QStringLiteral("qso"), qso);
}

void NetController::noteSerial(const QString& stx)
{
    bool ok = false;
    const int n = stx.trimmed().toInt(&ok);
    if (ok)
        m_serials.note(n);
}

bool NetController::isSerialServer() const
{
    // Il piu' piccolo fra quelli sentiti nell'ultimo minuto.
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QStringList present;
    for (const Peer& p : m_peers) {
        if (p.seen.secsTo(now) < 60)
            present << p.id;
    }
    return core::SharedSerials::isServer(m_id, present);
}

void NetController::requestSerial()
{
    if (!m_serialSharing || !m_net.running() || isSerialServer())
        return;
    send(QStringLiteral("serialreq"), QJsonObject{});
}

void NetController::setSerialSharing(bool on)
{
    if (on == m_serialSharing)
        return;
    m_serialSharing = on;
    if (on)
        requestSerial();
}

int NetController::takeSerial(int localNext)
{
    const bool online = m_serialSharing && m_net.running();
    const bool server = online && isSerialServer();
    const int n = m_serials.take(localNext, server, online);
    // Il prossimo si chiede adesso, per averlo pronto al QSO dopo.
    if (online && !server)
        requestSerial();
    return n;
}

void NetController::sendGab(const QString& text, const QString& to)
{
    const QString t = text.trimmed();
    if (t.isEmpty() || !m_net.running())
        return;
    send(QStringLiteral("gab"), QJsonObject{{QStringLiteral("text"), t}, {QStringLiteral("to"), to}});
    addMessage(m_name, t, true, to);
}

void NetController::sendSpot(const QString& call, double khz, const QString& comment)
{
    const QString c = call.trimmed().toUpper();
    if (c.isEmpty() || khz <= 0 || !m_net.running())
        return;
    send(QStringLiteral("spot"), QJsonObject{{QStringLiteral("call"), c}, {QStringLiteral("khz"), khz},
                                             {QStringLiteral("comment"), comment.trimmed()}});
    addMessage(m_name, tr("spot %1 %2 %3").arg(c).arg(khz, 0, 'f', 1).arg(comment.trimmed()), true);
}

void NetController::requestSync()
{
    if (!m_net.running() || !m_ctx.sessionStart)
        return;
    const QDateTime start = m_ctx.sessionStart();
    if (!start.isValid())
        return;
    send(QStringLiteral("syncreq"), QJsonObject{{QStringLiteral("since"), start.toString(Qt::ISODate)}});
}

void NetController::addMessage(const QString& from, const QString& text, bool mine, const QString& to)
{
    m_messages << QVariantMap{{QStringLiteral("time"), QDateTime::currentDateTimeUtc().toString(QStringLiteral("HH:mm"))},
                              {QStringLiteral("from"), from},
                              {QStringLiteral("to"), to},
                              {QStringLiteral("text"), text},
                              {QStringLiteral("mine"), mine}};
    while (m_messages.size() > kKeepMessages)
        m_messages.removeFirst();
    emit messagesChanged();
}

void NetController::onReceived(const QString& from, const QString& type, const QJsonObject& body)
{
    // Lo stesso datagramma arriva una volta per ogni scheda di rete.
    const qint64 seq = body.value(QStringLiteral("seq")).toInteger();
    const QString session = body.value(QStringLiteral("session")).toString();
    // I client precedenti alla sessione 2 non inviano il campo: li trattiamo
    // come una sessione legacy, mantenendo la compatibilita' senza farli
    // contaminare le sequenze delle nuove istanze.
    const QString frameSession = session.isEmpty() ? QStringLiteral("legacy") : session;
    QSet<QString>& seen = m_seenSeq[from];
    if (seq > 0) {
        const QString frameId = frameKey(frameSession, seq);
        if (seen.contains(frameId))
            return;
        seen.insert(frameId);
        if (seen.size() > 2000)
            seen.clear();
    }
    ++m_received;
    const QString name = body.value(QStringLiteral("name")).toString(from);
    Peer& p = m_peers[from];
    const bool isNew = p.id.isEmpty();
    p.id = from;
    p.name = name;
    p.seen = QDateTime::currentDateTimeUtc();
    if (isNew && m_ctx.activity)
        m_ctx.activity(QStringLiteral("NET"), tr("%1 joined the network").arg(name), QStringLiteral("info"));

    if (type == QLatin1String("hello")) {
        p.op = body.value(QStringLiteral("op")).toString();
        p.band = body.value(QStringLiteral("band")).toString();
        p.mode = body.value(QStringLiteral("mode")).toString();
        p.khz = body.value(QStringLiteral("khz")).toDouble();
        p.qsos = body.value(QStringLiteral("qsos")).toInt();
    } else if (type == QLatin1String("qso")) {
        const AdifDocument doc = adif::parse(body.value(QStringLiteral("adif")).toString().toUtf8());
        for (const AdifRecord& r : doc.records) {
            bool serialOk = false;
            const int serial = r.value(QStringLiteral("STX")).trimmed().toInt(&serialOk);
            const bool collision = serialOk && m_serials.noteRemote(serial);
            if (collision && m_ctx.activity)
                m_ctx.activity(QStringLiteral("NET"),
                               tr("Serial collision detected: %1 was already assigned locally; verify the contest log")
                                   .arg(serial),
                               QStringLiteral("error"));
            if (m_ctx.insertRemote && m_ctx.insertRemote(r, name))
                ++p.qsos;
        }
    } else if (type == QLatin1String("serialreq")) {
        if (m_serialSharing && isSerialServer())
            send(QStringLiteral("serialgrant"), QJsonObject{{QStringLiteral("to"), from},
                                                            {QStringLiteral("n"), m_serials.serve()}});
    } else if (type == QLatin1String("serialgrant")) {
        const int n = body.value(QStringLiteral("n")).toInt();
        // Anche quelli dati agli altri sono gia' presi.
        if (body.value(QStringLiteral("to")).toString() == m_id)
            m_serials.reserve(n);
        else
            m_serials.note(n);
    } else if (type == QLatin1String("gab")) {
        const QString to = body.value(QStringLiteral("to")).toString();
        if (to.isEmpty() || to.compare(m_name, Qt::CaseInsensitive) == 0) {
            const QString text = body.value(QStringLiteral("text")).toString();
            addMessage(name, text, false, to);
            emit gabReceived(name, text);
            if (m_ctx.activity)
                m_ctx.activity(QStringLiteral("NET"), QStringLiteral("%1: %2").arg(name, text), QStringLiteral("highlight"));
        }
    } else if (type == QLatin1String("spot")) {
        const QString call = body.value(QStringLiteral("call")).toString();
        const double khz = body.value(QStringLiteral("khz")).toDouble();
        const QString comment = body.value(QStringLiteral("comment")).toString();
        addMessage(name, tr("spot %1 %2 %3").arg(call).arg(khz, 0, 'f', 1).arg(comment), false);
        if (m_ctx.spot)
            m_ctx.spot(call, khz, comment, name);
    } else if (type == QLatin1String("syncreq") && m_ctx.db) {
        // Si rimandano i QSO della gara fatti qui (non quelli arrivati dalla rete:
        // quelli li rimanda chi li ha fatti).
        const QString since = body.value(QStringLiteral("since")).toString();
        QSqlQuery q(m_ctx.db->connection());
        q.prepare(QStringLiteral("SELECT id FROM qso WHERE deleted = 0 AND qso_datetime_on >= ? AND source <> 'network' "
                                 "ORDER BY qso_datetime_on"));
        q.addBindValue(since);
        if (q.exec()) {
            while (q.next())
                if (!m_resend.contains(q.value(0).toLongLong()))
                    m_resend << q.value(0).toLongLong();
        }
        if (!m_resend.isEmpty())
            m_resendTimer.start();
    }
    emit peersChanged();
    emit stateChanged();
}

QString NetController::frameKey(const QString& session, qint64 seq)
{
    return session + QLatin1Char(':') + QString::number(seq);
}

} // namespace decolog::app
