#include "core/RigControl.h"
#include "core/CwKeyer.h"

#include <QCoreApplication>
#include <QDebug>

#include <cstdlib>

#include <utility>

namespace decolog::core {

namespace {
// Quanto spesso si chiede alla radio dov'e'. Un secondo e mezzo: abbastanza da
// seguire il VFO, poco abbastanza da non intasare la seriale sotto rigctld.
constexpr int kPollMs = 1500;
constexpr int kRetryMs = 5000;
// Quanto si aspetta prima di chiedere se lo split e' stato preso: la radio (o
// Decodium, se il CAT passa da li') ci mette un momento a confermarlo.
constexpr int kSplitCheckMs = 3000;
} // namespace

RigControl::RigControl(QObject* parent)
    : RigLink(parent)
    , m_socket(new QTcpSocket(this))
{
    connect(m_socket, &QTcpSocket::connected, this, [this] {
        setStatus(tr("Radio connected (rigctld %1:%2)").arg(m_host).arg(m_port));
        m_pending.clear();
        m_lines.clear();
        m_buffer.clear();
        m_plainBridge = false;
        m_powerStateKnown = false;
        m_swrKnown = false;
        m_alcKnown = false;
        m_rfPowerKnown = false;
        m_tunerKnown = false;
        refresh();
        m_poll.start();
        emit changed();
    });
    connect(m_socket, &QTcpSocket::readyRead, this, &RigControl::readFromRig);
    connect(m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (!m_wanted)
            return;
        cancelMorseQueue();
        setStatus(tr("Radio not reachable: %1").arg(m_socket->errorString()));
        emit failed(m_status);
        m_poll.stop();
        m_retry.start();
        emit changed();
    });
    connect(m_socket, &QTcpSocket::disconnected, this, [this] {
        cancelMorseQueue();
        m_poll.stop();
        if (m_wanted)
            m_retry.start();
        emit changed();
    });

    m_poll.setInterval(kPollMs);
    connect(&m_poll, &QTimer::timeout, this, &RigControl::refresh);

    m_retry.setInterval(kRetryMs);
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, [this] {
        if (m_wanted && m_socket->state() == QAbstractSocket::UnconnectedState)
            m_socket->connectToHost(m_host, m_port);
    });

    m_morseTimer.setSingleShot(true);
    connect(&m_morseTimer, &QTimer::timeout, this, [this] {
        if (!m_morseInFlight)
            return;
        m_morseInFlight = false;
        m_activeMorse.clear();
        m_activeMorseToken = 0;
        startNextMorse();
    });
}

bool RigControl::connected() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

void RigControl::connectTo(const QString& host, quint16 port)
{
    cancelMorseQueue();
    m_host = host.trimmed().isEmpty() ? QStringLiteral("127.0.0.1") : host.trimmed();
    m_port = port > 0 ? port : 4532;
    m_wanted = true;
    m_retry.stop();
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->abort();
    setStatus(tr("Looking for the radio on %1:%2…").arg(m_host).arg(m_port));
    emit changed();
    m_socket->connectToHost(m_host, m_port);
}

void RigControl::disconnectFromRig()
{
    cancelMorseQueue();
    m_wanted = false;
    m_poll.stop();
    m_retry.stop();
    m_socket->abort();
    m_frequencyHz = 0;
    m_mode.clear();
    m_powerStateKnown = false;
    m_swrKnown = false;
    m_alcKnown = false;
    m_rfPowerKnown = false;
    m_tunerKnown = false;
    setStatus(tr("Radio off"));
    emit changed();
}

void RigControl::setStatus(const QString& text)
{
    m_status = text;
}

void RigControl::send(const QString& kind, const QString& command, int values, const QString& text,
                      quint64 cwToken)
{
    if (!connected())
        return;
    m_pending.enqueue({kind, text, values, ++m_seq, cwToken});
    m_socket->write(QStringLiteral("+%1\n").arg(command).toUtf8());
}

void RigControl::refresh()
{
    // Se la radio non risponde piu', la coda cresce a vuoto: meglio fermarsi.
    if (m_pending.size() > 8)
        return;
    send(QStringLiteral("freq"), QStringLiteral("f"), 1);
    send(QStringLiteral("mode"), QStringLiteral("m"), 2);
    send(QStringLiteral("speed"), QStringLiteral("l KEYSPD"), 1);
    // Un giro e' dedicato ai meter e allo stato di alimentazione/tuner; quello
    // successivo alle funzioni VFO. Cosi' il polling non satura le radio lente.
    const int pollCycle = m_extraPoll++ % 3;
    if (pollCycle == 1) {
        send(QStringLiteral("powerstat"), QStringLiteral("\\get_powerstat"), 1);
        send(QStringLiteral("swr"), QStringLiteral("l SWR"), 1);
        send(QStringLiteral("alc"), QStringLiteral("l ALC"), 1);
        send(QStringLiteral("powerwatts"), QStringLiteral("l RFPOWER_METER_WATTS"), 1);
        send(QStringLiteral("tuner"), QStringLiteral("u TUNER"), 1);
        return;
    }
    if (pollCycle != 0)
        return;
    if (m_features & Split) {
        send(QStringLiteral("split"), QStringLiteral("s"), 2);
        if (m_split)
            send(QStringLiteral("txfreq"), QStringLiteral("i"), 1);
    }
    if (m_features & VfoSelect)
        send(QStringLiteral("vfo"), QStringLiteral("v"), 1);
    if (m_features & Rit)
        send(QStringLiteral("rit"), QStringLiteral("j"), 1);
    if (m_features & Xit)
        send(QStringLiteral("xit"), QStringLiteral("z"), 1);
}

void RigControl::setSplit(bool on, qint64 txHz)
{
    // Con lo split si trasmette sul VFO B.
    send(QStringLiteral("setsplit"), QStringLiteral("S %1 %2").arg(on ? 1 : 0).arg(on ? QStringLiteral("VFOB")
                                                                                     : QStringLiteral("VFOA")), 0);
    m_lastSet.insert(QStringLiteral("split"), m_seq);
    if (on && txHz > 0) {
        send(QStringLiteral("setsplit"), QStringLiteral("I %1").arg(txHz), 0);
        m_lastSet.insert(QStringLiteral("txfreq"), m_seq);
        m_txHz = txHz;
    } else if (!on && m_plainBridge) {
        // Il ponte di Decodium lo split lo spegne con la frequenza TX a zero:
        // "S 0" lo accetta e non fa niente. A rigctld vero non si manda.
        send(QStringLiteral("setsplit"), QStringLiteral("I 0"), 0);
        m_lastSet.insert(QStringLiteral("txfreq"), m_seq);
        m_txHz = 0;
    }
    m_split = on;
    m_splitWanted = on;
    m_splitTxWanted = on ? txHz : 0;
    const quint64 check = ++m_splitCheck;
    QTimer::singleShot(kSplitCheckMs, this, [this, check] {
        if (check != m_splitCheck || !connected())
            return;
        send(QStringLiteral("splitcheck"), QStringLiteral("s"), 2);
        // E dove trasmette: Decodium, se il CAT passa da li', la frequenza di
        // trasmissione se la rifa' da se' (per l'FT8) e la nostra sparisce.
        if (m_splitWanted && m_splitTxWanted > 0)
            send(QStringLiteral("txcheck"), QStringLiteral("i"), 1);
    });
    emit changed();
}

void RigControl::setVfo(const QString& vfo)
{
    const QString v = vfo.trimmed().toUpper();
    if (v != QLatin1String("VFOA") && v != QLatin1String("VFOB"))
        return;
    send(QStringLiteral("setvfo"), QStringLiteral("V %1").arg(v), 0);
    m_lastSet.insert(QStringLiteral("vfo"), m_seq);
    m_vfo = v;
    emit changed();
}

void RigControl::setRit(int hz)
{
    send(QStringLiteral("setrit"), QStringLiteral("J %1").arg(hz), 0);
    m_lastSet.insert(QStringLiteral("rit"), m_seq);
    m_rit = hz;
    emit changed();
}

void RigControl::setXit(int hz)
{
    send(QStringLiteral("setxit"), QStringLiteral("Z %1").arg(hz), 0);
    m_lastSet.insert(QStringLiteral("xit"), m_seq);
    m_xit = hz;
    emit changed();
}

void RigControl::setFrequency(qint64 hz)
{
    if (hz > 0)
        send(QStringLiteral("set"), QStringLiteral("F %1").arg(hz), 0);
}

void RigControl::setMode(const QString& mode)
{
    const QString clean = mode.trimmed().toUpper();
    if (!clean.isEmpty())
        send(QStringLiteral("set"), QStringLiteral("M %1 0").arg(clean), 0);
}

void RigControl::setPtt(bool on)
{
    send(QStringLiteral("set"), QStringLiteral("T %1").arg(on ? 1 : 0), 0);
}

void RigControl::setPowerOn(bool on)
{
    if (!connected())
        return;
    send(QStringLiteral("setpowerstat"), QStringLiteral("\\set_powerstat %1").arg(on ? 1 : 0), 0);
    m_powerOn = on;
    m_powerStateKnown = true;
    emit changed();
}

void RigControl::setTunerOn(bool on)
{
    if (!connected())
        return;
    send(QStringLiteral("settuner"), QStringLiteral("U TUNER %1").arg(on ? 1 : 0), 0);
    m_tunerOn = on;
    m_tunerKnown = true;
    emit changed();
}

void RigControl::setSpeedWpm(int wpm)
{
    const int clamped = qBound(5, wpm, 60);
    m_wpm = clamped;
    send(QStringLiteral("set"), QStringLiteral("L KEYSPD %1").arg(clamped), 0);
    emit changed();
}

QString RigControl::morseText(const QString& text)
{
    // Il manipolatore della radio vuole le maiuscole e i segni del CW: le
    // Yaesu (comando KM di Hamlib) rifiutano tutto il messaggio per una
    // minuscola o un segno che non conoscono, e le altre lo saltano. Il resto
    // si toglie qui, e il messaggio parte.
    static const QString allowed = QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 /?.,=+-");
    QString out;
    for (const QChar c : text.toUpper()) {
        if (allowed.contains(c))
            out += c;
    }
    return out.simplified();
}

void RigControl::sendMorse(const QString& text)
{
    const QString clean = morseText(text);
    if (clean.isEmpty())
        return;
    if (!connected()) {
        emit failed(tr("The radio is not connected: nothing sent in CW"));
        return;
    }
    // Il manipolatore della radio manda solo in CW: in USB o in DATA il testo
    // lo prende e non va in aria. DecoDXLog il modo non lo cambia da solo.
    const QString mode = m_mode.toUpper();
    if (!mode.isEmpty() && !mode.startsWith(QLatin1String("CW")))
        emit failed(tr("The radio is in %1: its keyer sends only in CW. Switch it to CW.").arg(m_mode));
    if (m_yaesuKeyerMemoryMode) {
        // Non lasciare che Hamlib tronchi silenziosamente le macro a 50
        // caratteri. Ogni parte attende la fine stimata della precedente.
        // Conservare anche gli spazi mantiene la separazione delle parole.
        QString remaining = clean;
        while (remaining.size() > 50) {
            const qsizetype space = remaining.lastIndexOf(QLatin1Char(' '), 49);
            const qsizetype length = space > 0 ? space + 1 : 50;
            m_morseQueue.enqueue(remaining.left(length));
            remaining.remove(0, length);
        }
        m_morseQueue.enqueue(remaining);
    } else {
        m_morseQueue.enqueue(clean);
    }
    startNextMorse();
}

void RigControl::stopMorse()
{
    cancelMorseQueue();
    if (m_noStopMorse)
        return;
    // Il nome lungo dei comandi di rigctld vuole la barra rovescia davanti.
    send(QStringLiteral("stopmorse"), QStringLiteral("\\stop_morse"), 0);
}

void RigControl::startNextMorse()
{
    if (m_morseInFlight || m_morseQueue.isEmpty() || !connected())
        return;

    m_morseInFlight = true;
    m_activeMorse = m_morseQueue.dequeue();
    m_activeMorseToken = ++m_morseToken;
    QString payload = m_activeMorse;
    if (m_yaesuKeyerMemoryMode && payload.size() == 1)
        payload += QLatin1Char(' '); // "? " is text; "1 " must not recall memory 1.
    send(QStringLiteral("morse"), QStringLiteral("b %1").arg(payload), 0, m_activeMorse,
         m_activeMorseToken);
}

void RigControl::cancelMorseQueue()
{
    // Invalidare il token e' essenziale: una risposta RPRT della macro
    // annullata puo' arrivare dopo Ferma o dopo una riconnessione.
    ++m_morseToken;
    m_morseQueue.clear();
    m_morseTimer.stop();
    m_morseInFlight = false;
    m_activeMorse.clear();
    m_activeMorseToken = 0;
}

void RigControl::finishMorse(const Pending& what, bool accepted)
{
    if (!m_morseInFlight || what.cwToken == 0 || what.cwToken != m_activeMorseToken)
        return;

    if (!accepted) {
        m_morseInFlight = false;
        m_activeMorse.clear();
        m_activeMorseToken = 0;
        // Un messaggio rifiutato non deve ingoiare quelli dopo: il prossimo
        // parte nel giro successivo, dopo aver mostrato l'errore di questo.
        QTimer::singleShot(0, this, &RigControl::startNextMorse);
        return;
    }

    emit morseSent(what.text);
    // rigctld conferma quando ha passato il testo alla radio, non quando la
    // radio ha finito di batterlo. Aspettare il tempo Morse stimato evita che
    // la macro seguente rimpiazzi il buffer interno prima della fine della
    // precedente, comprese le parti di una macro Yaesu piu' lunga di 50 caratteri.
    const int wpm = m_wpm > 0 ? qBound(5, m_wpm, 60) : 20;
    const int estimated = CwKeyer::millisFor(what.text, wpm);
    m_morseTimer.start(qBound(150, estimated + 180, 120000));
}

void RigControl::emergencyStop()
{
    // Il PTT va giu' prima di tutto quando la socket ordinaria e' disponibile.
    // Se la connessione e' gia' caduta, non si puo' usare quella coda, ma il
    // canale di emergenza qui sotto puo' ancora raggiungere rigctld e la radio.
    // Il vecchio return lasciava proprio questi utenti in TX.
    if (connected()) {
        // Non mettere stop_morse prima del dekey: su alcune radio il comando CW
        // resta occupato finche' non finisce la memoria interna.
        setPtt(false);
        stopMorse();
    } else {
        cancelMorseQueue();
    }
    sendEmergencyStop();
}

void RigControl::setEmergencyCatOffCommand(const QString& command)
{
    const QString clean = command.trimmed();
    // This value becomes one rigctld protocol line.  It is configured only by
    // RigController, but reject separators here as a defence in depth.
    m_emergencyCatOffCommand = clean.contains(QLatin1Char('\n'))
            || clean.contains(QLatin1Char('\r')) ? QString() : clean;
}

void RigControl::sendEmergencyStop()
{
    if (m_host.isEmpty() || m_port == 0)
        return;

    // La socket normale puo' avere davanti interrogazioni CAT o una risposta
    // lenta. Una seconda connessione a rigctld non partecipa alla sua coda
    // locale; manda solo operazioni di arresto idempotenti e si autodistrugge.
    QByteArray commands = QByteArrayLiteral("+T 0\n");
    if (!m_emergencyCatOffCommand.isEmpty())
        commands += QByteArrayLiteral("+w ") + m_emergencyCatOffCommand.toLatin1() + '\n';
    commands += QByteArrayLiteral("+\\stop_morse\n");

    auto* emergency = new QTcpSocket(this);
    connect(emergency, &QTcpSocket::connected, emergency, [emergency, commands] {
        emergency->write(commands);
        emergency->flush();
    });
    connect(emergency, &QTcpSocket::bytesWritten, emergency, [emergency](qint64) {
        if (emergency->bytesToWrite() == 0)
            emergency->disconnectFromHost();
    });
    connect(emergency, &QTcpSocket::disconnected, emergency, &QObject::deleteLater);
    connect(emergency, &QTcpSocket::errorOccurred, emergency,
            [emergency](QAbstractSocket::SocketError) { emergency->deleteLater(); });
    // Anche un rigctld irraggiungibile non deve lasciare una socket appesa.
    QTimer::singleShot(1000, emergency, [emergency] {
        emergency->abort();
        emergency->deleteLater();
    });
    emergency->connectToHost(m_host, m_port);
}

void RigControl::readFromRig()
{
    m_buffer += m_socket->readAll();
    while (true) {
        const int end = m_buffer.indexOf('\n');
        if (end < 0)
            break;
        const QString line = QString::fromUtf8(m_buffer.left(end)).trimmed();
        m_buffer.remove(0, end + 1);
        m_lines << line;

        if (line.startsWith(QLatin1String("RPRT"))) {
            const QStringList block = m_lines;
            m_lines.clear();
            handleReply(block);
            continue;
        }

        // Rigctld "vero" chiude ogni risposta con RPRT; altri ponti CAT — come
        // quello di Decodium — rispondono col valore nudo e basta. Allora si
        // conta: quando sono arrivate tutte le righe che quella domanda si
        // aspettava, la risposta e' finita lo stesso.
        if (m_pending.isEmpty())
            continue;
        const int wanted = m_pending.head().values;
        if (wanted <= 0)
            continue;
        // Una riga coi due punti ("get_level: KEYSPD", "Mode: CW") dice che
        // questa risposta e' di rigctld vero, e quello il RPRT lo manda sempre:
        // qui non si conta niente e si aspetta.
        //
        // Contare lo stesso era un guaio serio: la risposta si chiudeva al
        // valore nudo, e il RPRT che arrivava subito dopo si prendeva la
        // domanda seguente. Da li' in poi ogni risposta finiva sulla domanda
        // sbagliata — e l'errore "questa radio non manipola" spariva del tutto.
        bool extended = false;
        for (const QString& seen : std::as_const(m_lines)) {
            if (seen.contains(QLatin1Char(':')))
                extended = true;
        }
        if (extended)
            continue;
        if (m_lines.size() >= wanted) {
            QStringList block = m_lines;
            block << QStringLiteral("RPRT 0");
            m_lines.clear();
            m_plainBridge = true;
            handleReply(block);
        }
    }
}

void RigControl::handleReply(const QStringList& lines)
{
    if (qEnvironmentVariableIsSet("DECODXLOG_RIG_DEBUG"))
        qDebug() << "reply" << lines << "pending" << m_pending.size();
    if (m_pending.isEmpty())
        return;
    const Pending what = m_pending.dequeue();
    const int result = lines.last().mid(4).trimmed().toInt();

    if (result != 0) {
        // -1 e' "questa radio non lo sa fare": per il CW vuol dire che il
        // manipolatore della radio non si comanda da qui.
        if (what.kind == QLatin1String("morse") && (result == -11 || result == -4)) {
            finishMorse(what, false);
            // -11 e -4 sono "non lo so fare": il ponte CAT o la radio non
            // manipolano, e i tasti si spengono.
            setStatus(tr("This CAT link does not key CW (rigctld: %1)").arg(result));
            emit morseUnsupported();
            emit failed(tr("The radio did not take the CW text (rigctld: %1). Not every radio — and "
                           "not every CAT bridge — can key CW: for the macros you need rigctld "
                           "talking to the radio itself.").arg(result));
        } else if (what.kind == QLatin1String("morse")) {
            finishMorse(what, false);
            // Un rifiuto di questo messaggio, non della radio: i tasti restano
            // accesi. Prima uno solo bastava a spegnerli fino al riavvio.
            emit failed(tr("The radio refused the CW text (rigctld: %1). Check that it is in CW (now %2), "
                           "that break-in (BK-IN) is on, and that the CAT port is the radio's own.")
                            .arg(result)
                            .arg(m_mode.isEmpty() ? QStringLiteral("?") : m_mode));
        } else if (what.kind == QLatin1String("stopmorse")) {
            m_noStopMorse = true;
            emit failed(tr("This radio cannot stop the CW from CAT: the message ends by itself. "
                           "To cut it short use the radio, or a serial keyer."));
        } else if (what.kind == QLatin1String("powerstat") || what.kind == QLatin1String("swr")
                   || what.kind == QLatin1String("alc")
                   || what.kind == QLatin1String("powerwatts") || what.kind == QLatin1String("tuner")) {
            if (what.kind == QLatin1String("powerstat")) m_powerStateKnown = false;
            if (what.kind == QLatin1String("swr")) m_swrKnown = false;
            if (what.kind == QLatin1String("alc")) m_alcKnown = false;
            if (what.kind == QLatin1String("powerwatts")) m_rfPowerKnown = false;
            if (what.kind == QLatin1String("tuner")) m_tunerKnown = false;
            emit changed();
        } else if (what.kind == QLatin1String("setpowerstat") || what.kind == QLatin1String("settuner")) {
            if (what.kind == QLatin1String("setpowerstat")) m_powerStateKnown = false;
            if (what.kind == QLatin1String("settuner")) m_tunerKnown = false;
            emit failed(tr("The radio refused the CAT power/tuner command (rigctld: %1)").arg(result));
            emit changed();
        } else if (what.kind == QLatin1String("split") || what.kind == QLatin1String("txfreq")
                   || what.kind == QLatin1String("setsplit") || what.kind == QLatin1String("splitcheck")
                   || what.kind == QLatin1String("txcheck")) {
            // Questa radio lo split da qui non lo fa: non si chiede piu'.
            m_features &= ~Split;
            if (what.kind == QLatin1String("setsplit"))
                emit failed(tr("The radio does not take split from here (rigctld: %1)").arg(result));
            emit changed();
        } else if (what.kind == QLatin1String("vfo") || what.kind == QLatin1String("setvfo")) {
            m_features &= ~VfoSelect;
            if (what.kind == QLatin1String("setvfo"))
                emit failed(tr("The radio does not change VFO from here (rigctld: %1)").arg(result));
            emit changed();
        } else if (what.kind == QLatin1String("rit") || what.kind == QLatin1String("setrit")) {
            m_features &= ~Rit;
            if (what.kind == QLatin1String("setrit"))
                emit failed(tr("The radio does not take RIT from here (rigctld: %1)").arg(result));
            emit changed();
        } else if (what.kind == QLatin1String("xit") || what.kind == QLatin1String("setxit")) {
            m_features &= ~Xit;
            if (what.kind == QLatin1String("setxit"))
                emit failed(tr("The radio does not take XIT from here (rigctld: %1)").arg(result));
            emit changed();
        } else if (what.kind != QLatin1String("speed")) {
            emit failed(tr("The radio answered with an error (rigctld: %1)").arg(result));
        }
        return;
    }

    auto valueOf = [&lines](const QString& key) {
        for (const QString& line : lines) {
            if (line.startsWith(key + QLatin1Char(':')))
                return line.mid(key.size() + 1).trimmed();
        }
        return QString();
    };

    // Senza etichette (risposta nuda) si prendono le righe in ordine.
    QStringList plain;
    for (const QString& line : lines) {
        if (line.startsWith(QLatin1String("RPRT")) || line.endsWith(QLatin1Char(':'))
            || line.contains(QLatin1String(": ")))
            continue;
        plain << line;
    }

    // Una risposta a una domanda partita prima dell'ultimo comando dice lo
    // stato di prima: si aspetta il giro dopo.
    const QString setKind = what.kind == QLatin1String("splitcheck") ? QStringLiteral("split")
                            : what.kind == QLatin1String("txcheck")  ? QStringLiteral("txfreq")
                                                                     : what.kind;
    if (what.seq < m_lastSet.value(setKind, 0))
        return;

    bool moved = false;
    auto first = [&](const QString& label) {
        const QString v = valueOf(label);
        return v.isEmpty() && !plain.isEmpty() ? plain.first() : v;
    };
    if (what.kind == QLatin1String("split") || what.kind == QLatin1String("splitcheck")) {
        const bool on = first(QStringLiteral("Split")).toInt() != 0;
        if (on != m_split) {
            m_split = on;
            moved = true;
        }
        // La radio non ha fatto quello che si e' chiesto: lo si dice, con
        // la ragione piu' probabile.
        if (what.kind == QLatin1String("splitcheck") && on != m_splitWanted) {
            if (m_splitWanted && m_plainBridge)
                emit failed(tr("The radio did not go split. The CAT goes through Decodium, and Decodium does "
                               "not carry out split asked by other programs: it manages split itself, for "
                               "FT8. For split from DecoDXLog, DecoDXLog must hold the CAT of the radio."));
            else if (m_splitWanted)
                emit failed(tr("The radio did not go split: rigctld took the command, but the radio says "
                               "split is off."));
            else
                emit failed(tr("The radio is still split: switch it off on the radio, or from the program "
                               "that holds the CAT."));
        }
    } else if (what.kind == QLatin1String("txcheck")) {
        const qint64 hz = first(QStringLiteral("TX Frequency")).toLongLong();
        if (hz > 0 && hz != m_txHz) {
            m_txHz = hz;
            moved = true;
        }
        // Lo split c'e', ma la trasmissione non e' dove l'abbiamo messa: la
        // frequenza l'ha cambiata chi tiene il CAT.
        if (m_splitWanted && m_splitTxWanted > 0 && std::llabs(hz - m_splitTxWanted) > 100) {
            const QString where = QString::number(hz / 1e6, 'f', 4);
            if (m_plainBridge)
                emit failed(tr("Decodium moved the transmit frequency to %1 MHz: it manages split itself, "
                               "for FT8, and overrides the one from DecoDXLog. For split from DecoDXLog, "
                               "DecoDXLog must hold the CAT of the radio.").arg(where));
            else
                emit failed(tr("The radio is transmitting on %1 MHz, not where the split put it.").arg(where));
        }
    } else if (what.kind == QLatin1String("txfreq")) {
        const qint64 hz = first(QStringLiteral("TX Frequency")).toLongLong();
        if (hz > 0 && hz != m_txHz) {
            m_txHz = hz;
            moved = true;
        }
    } else if (what.kind == QLatin1String("vfo")) {
        const QString v = first(QStringLiteral("VFO")).toUpper();
        if (!v.isEmpty() && v != m_vfo) {
            m_vfo = v;
            moved = true;
        }
    } else if (what.kind == QLatin1String("rit") || what.kind == QLatin1String("xit")) {
        const bool rit = what.kind == QLatin1String("rit");
        const int hz = first(rit ? QStringLiteral("RIT") : QStringLiteral("XIT")).toInt();
        int& target = rit ? m_rit : m_xit;
        if (hz != target) {
            target = hz;
            moved = true;
        }
    } else if (what.kind == QLatin1String("freq")) {
        const QString label = valueOf(QStringLiteral("Frequency"));
        const qint64 hz = (label.isEmpty() && !plain.isEmpty() ? plain.first() : label).toLongLong();
        if (hz > 0 && hz != m_frequencyHz) {
            m_frequencyHz = hz;
            moved = true;
        }
    } else if (what.kind == QLatin1String("mode")) {
        const QString label = valueOf(QStringLiteral("Mode"));
        const QString mode = label.isEmpty() && !plain.isEmpty() ? plain.first() : label;
        if (!mode.isEmpty() && mode != m_mode) {
            m_mode = mode;
            moved = true;
        }
    } else if (what.kind == QLatin1String("speed")) {
        // Il livello torna come numero nudo, prima di RPRT.
        for (const QString& line : lines) {
            bool ok = false;
            const int value = line.toInt(&ok);
            if (ok && value > 0 && value != m_wpm) {
                m_wpm = value;
                moved = true;
            }
        }
    } else if (what.kind == QLatin1String("powerstat")) {
        const QString v = first(QStringLiteral("Power Status"));
        m_powerOn = (v.isEmpty() && !plain.isEmpty() ? plain.first() : v).toInt() != 0;
        m_powerStateKnown = true;
        moved = true;
    } else if (what.kind == QLatin1String("swr")) {
        const QString v = first(QStringLiteral("Level Value"));
        bool ok = false;
        const double value = (v.isEmpty() && !plain.isEmpty() ? plain.first() : v).toDouble(&ok);
        if (ok && value >= 1.0) {
            m_swr = value;
            m_swrKnown = true;
            moved = true;
        }
    } else if (what.kind == QLatin1String("alc")) {
        const QString v = first(QStringLiteral("Level Value"));
        bool ok = false;
        const double value = (v.isEmpty() && !plain.isEmpty() ? plain.first() : v).toDouble(&ok);
        if (ok && value >= 0.0) {
            m_alc = value;
            m_alcKnown = true;
            moved = true;
        }
    } else if (what.kind == QLatin1String("powerwatts")) {
        const QString v = first(QStringLiteral("Level Value"));
        bool ok = false;
        const double value = (v.isEmpty() && !plain.isEmpty() ? plain.first() : v).toDouble(&ok);
        if (ok && value >= 0.0) {
            m_rfPowerWatts = value;
            m_rfPowerKnown = true;
            moved = true;
        }
    } else if (what.kind == QLatin1String("tuner")) {
        const QString v = first(QStringLiteral("Tuner"));
        m_tunerOn = (v.isEmpty() && !plain.isEmpty() ? plain.first() : v).toInt() != 0;
        m_tunerKnown = true;
        moved = true;
    } else if (what.kind == QLatin1String("setpowerstat") || what.kind == QLatin1String("settuner")) {
        moved = true;
    } else if (what.kind == QLatin1String("morse")) {
        finishMorse(what, true);
    }

    if (moved)
        emit changed();
}

} // namespace decolog::core
