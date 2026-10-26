#include "core/CatShare.h"

#include "core/RigLink.h"

#include <QHostAddress>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>

namespace decolog::core {

namespace {

// Le risposte che i client Hamlib si aspettano: 0 accettato, -1 rifiutato,
// -11 non implementato.
const char* const kOk = "RPRT 0\n";
const char* const kRefused = "RPRT -1\n";
const char* const kNotImpl = "RPRT -11\n";

// I nomi di modo sono gia' quelli di Hamlib; un modo che non si sa diventa
// USB, la scelta neutra che ogni client gestisce.
QString hamlibMode(const QString& mode)
{
    const QString u = mode.trimmed().toUpper();
    static const QStringList known{QStringLiteral("USB"),    QStringLiteral("LSB"),    QStringLiteral("CW"),
                                   QStringLiteral("CWR"),    QStringLiteral("AM"),     QStringLiteral("FM"),
                                   QStringLiteral("RTTY"),   QStringLiteral("RTTYR"),  QStringLiteral("PKTUSB"),
                                   QStringLiteral("PKTLSB"), QStringLiteral("PKTFM")};
    return known.contains(u) ? u : QStringLiteral("USB");
}

} // namespace

CatShare::CatShare(QObject* parent)
    : QObject(parent)
{
}

CatShare::~CatShare()
{
    configure(false, m_port, false, false);
}

bool CatShare::listening() const
{
    return m_server && m_server->isListening();
}

bool CatShare::configure(bool enabled, int port, bool allowControl, bool allowPtt)
{
    const bool restart = enabled != m_enabled || port != m_port || (enabled && !listening());
    m_allowControl = allowControl;
    // La trasmissione ha un interruttore suo: senza il controllo non c'e' comunque.
    m_allowPtt = allowPtt && allowControl;
    m_enabled = enabled;
    m_port = port;
    if (restart) {
        if (m_server) {
            for (auto it = m_buffers.begin(); it != m_buffers.end(); ++it)
                it.key()->disconnectFromHost();
            m_buffers.clear();
            m_server->close();
            m_server->deleteLater();
            m_server = nullptr;
        }
        m_lastError.clear();
        if (m_enabled) {
            m_server = new QTcpServer(this);
            connect(m_server, &QTcpServer::newConnection, this, &CatShare::onNewConnection);
            // Solo questo computer, come in Decodium: la rete locale vorrebbe una
            // lista di indirizzi ammessi, Internet autenticazione e cifratura.
            if (!m_server->listen(QHostAddress::LocalHost, static_cast<quint16>(m_port))) {
                m_lastError = m_server->errorString();
                m_server->deleteLater();
                m_server = nullptr;
                emit stateChanged();
                return false;
            }
        }
    }
    emit stateChanged();
    return true;
}

void CatShare::onNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QTcpSocket* s = m_server->nextPendingConnection();
        if (!s)
            break;
        m_buffers.insert(s, QByteArray());
        connect(s, &QTcpSocket::readyRead, this, [this, s] { onReadyRead(s); });
        connect(s, &QTcpSocket::disconnected, this, [this, s] {
            m_buffers.remove(s);
            s->deleteLater();
            emit stateChanged();
        });
        emit stateChanged();
    }
}

void CatShare::onReadyRead(QTcpSocket* s)
{
    auto it = m_buffers.find(s);
    if (it == m_buffers.end())
        return;
    it.value().append(s->readAll());
    // Chi non manda mai una riga intera non fa crescere il buffer all'infinito.
    if (it.value().size() > 8192) {
        s->disconnectFromHost();
        return;
    }
    qsizetype nl;
    while ((nl = it.value().indexOf('\n')) >= 0) {
        const QString line = QString::fromLatin1(it.value().left(nl)).trimmed();
        it.value().remove(0, nl + 1);
        if (line.isEmpty())
            continue;
        const QString reply = handleLine(line);
        if (reply.isNull()) {
            s->write(kOk);
            s->disconnectFromHost();
            return;
        }
        s->write(reply.toLatin1());
    }
}

QString CatShare::dumpState()
{
    // Tale e quale Decodium 4: il modello 1 (dummy), non quello vero, cosi' il
    // client non si aspetta capacita' che questo canale non ha.
    static const char* const kState =
        "1\n"
        "1\n"
        "2\n"
        "30000.000000 56000000.000000 0x2ffffff -1 -1 0x3 0x3\n"
        "0 0 0 0 0 0 0\n"
        "1800000.000000 54000000.000000 0x2ffffff 5000 100000 0x3 0x3\n"
        "0 0 0 0 0 0 0\n"
        "0x2ffffff 1\n"
        "0 0\n"
        "0x82 500\n"
        "0x221 3000\n"
        "0 0\n"
        "0\n0\n0\n0\n"
        "0\n"
        "0\n"
        // TUNER get/set; SWR and forward-power meters are read-only.  KEYSPD
        // is proxied too: clients such as Decodium call rig_set_level() before
        // send_morse, and Hamlib will refuse that call unless dump_state says
        // that this rigctld endpoint implements it.
        "0x40000000\n0x40000000\n0x8130004000\n0x4000\n0x0\n0x0\n"
        "vfo_ops=0x0\n"
        "ptt_type=0x1\n"
        "targetable_vfo=0x0\n"
        "done\n";
    return QString::fromLatin1(kState);
}

bool CatShare::refuseWrite(const QString& command, bool pttCommand)
{
    const bool allowed = pttCommand ? m_allowPtt : m_allowControl;
    emit controlAttempt(command, allowed);
    return !allowed;
}

QString CatShare::handleLine(const QString& line)
{
    // "+" chiede la forma estesa, ";" il separatore: per i comandi di qui la
    // forma semplice va bene a tutti.
    const bool extended = line.startsWith(QLatin1Char('+'));
    QString cleaned = line;
    while (!cleaned.isEmpty() && (cleaned.at(0) == QLatin1Char('+') || cleaned.at(0) == QLatin1Char(';')))
        cleaned.remove(0, 1);
    const QStringList parts = cleaned.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return QString::fromLatin1(kOk);
    const QString cmd = parts.first();

    if (cmd == QLatin1String("q") || cmd == QLatin1String("Q"))
        return QString();
    if (cmd == QLatin1String("\\dump_state"))
        return dumpState();
    // "0", non "CHKVFO 0": con la forma lunga Hamlib rifiuta l'apertura.
    if (cmd == QLatin1String("\\chk_vfo"))
        return QStringLiteral("0\n");
    if (cmd == QLatin1String("\\get_lock_mode"))
        return QStringLiteral("0\nRPRT 0\n");

    RigLink* rig = m_rig ? m_rig() : nullptr;
    const bool up = rig && rig->connected();

    if (cmd == QLatin1String("\\get_powerstat")) {
        const int value = up && rig->powerStateKnown() ? (rig->powerOn() ? 1 : 0) : (up ? 1 : 0);
        return extended ? QStringLiteral("get_powerstat:\nPower Status: %1\nRPRT 0\n").arg(value)
                        : QStringLiteral("%1\n").arg(value);
    }

    // Letture: dallo stato che DecoDXLog ha gia', senza traffico sulla seriale.
    if (cmd == QLatin1String("f") || cmd == QLatin1String("\\get_freq"))
        return QStringLiteral("%1\n").arg(up ? rig->frequencyHz() : 0);
    if (cmd == QLatin1String("m") || cmd == QLatin1String("\\get_mode"))
        return QStringLiteral("%1\n3000\n").arg(hamlibMode(up ? rig->mode() : QString()));
    if (cmd == QLatin1String("t") || cmd == QLatin1String("\\get_ptt"))
        return QStringLiteral("%1\n").arg(up && m_pttOn ? 1 : 0);
    if (cmd == QLatin1String("v") || cmd == QLatin1String("\\get_vfo"))
        return QStringLiteral("VFOA\n");
    if (cmd == QLatin1String("s") || cmd == QLatin1String("\\get_split_vfo"))
        return QStringLiteral("0\nVFOB\n");
    if (cmd == QLatin1String("i") || cmd == QLatin1String("\\get_split_freq"))
        return QStringLiteral("%1\n").arg(up ? rig->frequencyHz() : 0);
    if (cmd == QLatin1String("l") || cmd == QLatin1String("\\get_level")) {
        if (parts.size() < 2 || !up)
            return QString::fromLatin1(kNotImpl);
        const QString level = parts.at(1).trimmed().toUpper();
        auto fail = [&]() {
            return extended ? QStringLiteral("get_level: %1\n").arg(level) + QString::fromLatin1(kNotImpl)
                            : QString::fromLatin1(kNotImpl);
        };
        auto ok = [&](double value) {
            return extended ? QStringLiteral("get_level: %1\nLevel Value: %2\nRPRT 0\n")
                                      .arg(level).arg(value, 0, 'f', 6)
                            : QStringLiteral("%1\n").arg(value, 0, 'f', 6);
        };
        if (level == QLatin1String("SWR"))
            return rig->swrKnown() && rig->swr() >= 1.0 ? ok(rig->swr()) : fail();
        if (level == QLatin1String("ALC"))
            return rig->alcKnown() ? ok(rig->alc()) : fail();
        if (level == QLatin1String("RFPOWER_METER_WATTS"))
            return rig->rfPowerKnown() ? ok(rig->rfPowerWatts()) : fail();
        if (level == QLatin1String("KEYSPD"))
            return rig->speedWpm() > 0 ? ok(rig->speedWpm()) : fail();
        return fail();
    }
    if (cmd == QLatin1String("u") || cmd == QLatin1String("\\get_func")) {
        if (parts.size() < 2 || parts.at(1).trimmed().toUpper() != QLatin1String("TUNER") || !up
            || !rig->tunerKnown())
            return QString::fromLatin1(kNotImpl);
        return extended ? QStringLiteral("get_func: TUNER\n%1\nRPRT 0\n").arg(rig->tunerOn() ? 1 : 0)
                        : QStringLiteral("%1\n").arg(rig->tunerOn() ? 1 : 0);
    }

    // Scritture: solo se consentite.
    if (cmd == QLatin1String("F") || cmd == QLatin1String("\\set_freq")) {
        if (refuseWrite(cmd, false) || parts.size() < 2 || !up)
            return QString::fromLatin1(kRefused);
        rig->setFrequency(static_cast<qint64>(parts.last().toDouble()));
        return QString::fromLatin1(kOk);
    }
    if (cmd == QLatin1String("I") || cmd == QLatin1String("\\set_split_freq")) {
        // Lo split non si gestisce: si accetta senza agire, come la scelta del VFO.
        if (refuseWrite(cmd, false))
            return QString::fromLatin1(kRefused);
        return QString::fromLatin1(kOk);
    }
    if (cmd == QLatin1String("M") || cmd == QLatin1String("\\set_mode")) {
        if (refuseWrite(cmd, false) || parts.size() < 2 || !up)
            return QString::fromLatin1(kRefused);
        rig->setMode(hamlibMode(parts.at(1)));
        return QString::fromLatin1(kOk);
    }
    if (cmd == QLatin1String("L") || cmd == QLatin1String("\\set_level")) {
        if (refuseWrite(cmd, false) || parts.size() < 3 || !up
            || parts.at(1).trimmed().toUpper() != QLatin1String("KEYSPD"))
            return QString::fromLatin1(kRefused);
        bool valid = false;
        const int wpm = parts.last().toInt(&valid);
        if (!valid || wpm < 5 || wpm > 60)
            return QString::fromLatin1(kRefused);
        rig->setSpeedWpm(wpm);
        return QString::fromLatin1(kOk);
    }
    if (cmd == QLatin1String("T") || cmd == QLatin1String("\\set_ptt")) {
        if (refuseWrite(cmd, true) || parts.size() < 2 || !up)
            return QString::fromLatin1(kRefused);
        m_pttOn = parts.last().toInt() != 0;
        rig->setPtt(m_pttOn);
        return QString::fromLatin1(kOk);
    }
    if (cmd == QLatin1String("\\set_powerstat")) {
        if (refuseWrite(cmd, false) || parts.size() < 2 || !up)
            return QString::fromLatin1(kRefused);
        rig->setPowerOn(parts.last().toInt() != 0);
        return QString::fromLatin1(kOk);
    }
    if (cmd == QLatin1String("U") || cmd == QLatin1String("\\set_func")) {
        if (parts.size() < 3 || parts.at(1).trimmed().toUpper() != QLatin1String("TUNER") || !up)
            return QString::fromLatin1(kRefused);
        const bool on = parts.last().toInt() != 0;
        if (refuseWrite(cmd, on) || (!on && !m_allowControl))
            return QString::fromLatin1(kRefused);
        rig->setTunerOn(on);
        return QString::fromLatin1(kOk);
    }
    if (cmd == QLatin1String("b") || cmd == QLatin1String("\\send_morse")) {
        const qsizetype argumentStart = cleaned.indexOf(QLatin1Char(' '));
        const QString text = argumentStart >= 0 ? cleaned.mid(argumentStart + 1).trimmed() : QString();
        if (refuseWrite(cmd, true) || text.isEmpty() || !up)
            return QString::fromLatin1(kRefused);

        // send_morse is valid only in CW/CWR.  A client can have just changed
        // band while DecoDXLog still reports the former DATA mode, so put the
        // mode command in the same underlying rigctld queue before the text.
        // The radio's break-in keyer remains responsible for dots, dashes and
        // gaps: this does not assert a continuous PTT.
        if (!rig->mode().trimmed().toUpper().startsWith(QLatin1String("CW")))
            rig->setMode(QStringLiteral("CW"));
        rig->sendMorse(text);
        return QString::fromLatin1(kOk);
    }
    const bool stopMorse = cmd == QLatin1String("\\stop_morse")
        || (cmd.size() == 1 && cmd.at(0).unicode() == 0x00bb);
    if (stopMorse) {
        if (refuseWrite(cmd, true) || !up)
            return QString::fromLatin1(kRefused);
        // Treat a remote keyer stop as a safety stop as well.  This clears the
        // Morse queue and releases any PTT left by the client or radio link.
        m_pttOn = false;
        rig->emergencyStop();
        return QString::fromLatin1(kOk);
    }
    // VFO e split si accettano senza agire: i client li impostano all'apertura,
    // e rifiutarli farebbe fallire chi non vuole comandare niente.
    if (cmd == QLatin1String("V") || cmd == QLatin1String("\\set_vfo") || cmd == QLatin1String("S")
        || cmd == QLatin1String("\\set_split_vfo"))
        return QString::fromLatin1(kOk);

    return QString::fromLatin1(kNotImpl);
}

} // namespace decolog::core
