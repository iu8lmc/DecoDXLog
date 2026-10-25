#include "app/RigController.h"
#include "app/AudioDevices.h"
#include "../StartupTrace.h"

#include "core/Bands.h"
#include "core/CwCalls.h"
#include "core/QslUpload.h"
#include "core/SerialPorts.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QFileInfo>
#include <QJsonArray>
#include <QMediaDevices>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <algorithm>
#include <memory>
#include <QThread>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QSettings>

namespace decolog::app {

using core::RigControl;

namespace {

// rigctld e' un processo figlio nostro solo quando il CAT e' collegato a una
// seriale. QProcess non lo termina automaticamente alla chiusura della
// finestra: su macOS questo poteva lasciare DecoDXLog senza finestre ma ancora
// vivo nel Terminale. Un arresto con un limite preciso evita inoltre che un
// driver seriale bloccato renda impossibile uscire dall'applicazione.
void stopChildProcess(std::unique_ptr<QProcess>& process)
{
    if (!process)
        return;

    process->disconnect();
    if (process->state() != QProcess::NotRunning) {
        process->terminate();
        if (!process->waitForFinished(700)) {
            process->kill();
            process->waitForFinished(500);
        }
    }
    process.reset();
}

} // namespace

RigController::RigController(Context context, QObject* parent)
    : QObject(parent)
    , m_ctx(std::move(context))
{
    QSettings s;
    m_enabled = s.value(QStringLiteral("rig/enabled"), false).toBool();
    m_host = s.value(QStringLiteral("rig/host"), QStringLiteral("127.0.0.1")).toString();
    m_port = s.value(QStringLiteral("rig/port"), 4532).toInt();
    m_wpm = s.value(QStringLiteral("cw/wpm"), 24).toInt();
    m_link = s.value(QStringLiteral("rig/link"), QStringLiteral("network")).toString();
    m_tciAddress = s.value(QStringLiteral("rig/tciAddress"), m_tciAddress).toString();
    m_tciTrx = s.value(QStringLiteral("rig/tciTrx"), 0).toInt();
    m_flrigAddress = s.value(QStringLiteral("rig/flrigAddress"), m_flrigAddress).toString();
    // La CAT condivisa: la radio di adesso, qualunque sia il collegamento.
    m_share.setRigProvider([this]() -> core::RigLink* { return m_rig; });
    connect(&m_share, &core::CatShare::stateChanged, this, &RigController::shareChanged);
    connect(&m_share, &core::CatShare::controlAttempt, this, [this](const QString& command, bool accepted) {
        if (!accepted && m_ctx.activity)
            m_ctx.activity(QStringLiteral("CAT"),
                           tr("A program on the shared CAT asked for %1: refused (enable control in Settings → Radio)")
                               .arg(command),
                           QStringLiteral("warning"));
    });
    {
        const bool on = s.value(QStringLiteral("rig/share/enabled"), false).toBool();
        const int port = s.value(QStringLiteral("rig/share/port"), 4533).toInt();
        const bool control = s.value(QStringLiteral("rig/share/control"), false).toBool();
        const bool ptt = s.value(QStringLiteral("rig/share/ptt"), false).toBool();
        QTimer::singleShot(0, this, [this, on, port, control, ptt] {
            if (!m_share.configure(on, port, control, ptt) && m_ctx.activity)
                m_ctx.activity(QStringLiteral("CAT"), tr("Shared CAT not started on port %1: %2").arg(port).arg(m_share.lastError()),
                               QStringLiteral("warning"));
        });
    }
    m_omniRigNumber = s.value(QStringLiteral("rig/omniRigNumber"), 1).toInt();
    m_serialPort = s.value(QStringLiteral("rig/serialPort")).toString();
    m_rigModel = s.value(QStringLiteral("rig/model"), 0).toInt();
    m_baud = s.value(QStringLiteral("rig/baud"), 38400).toInt();
    m_dataBits = s.value(QStringLiteral("rig/dataBits"), m_dataBits).toString();
    m_stopBits = s.value(QStringLiteral("rig/stopBits"), m_stopBits).toString();
    m_parity = s.value(QStringLiteral("rig/parity"), m_parity).toString();
    m_handshake = s.value(QStringLiteral("rig/handshake"), m_handshake).toString();
    m_dtrState = s.value(QStringLiteral("rig/dtrState"), m_dtrState).toString();
    m_rtsState = s.value(QStringLiteral("rig/rtsState"), m_rtsState).toString();
    m_civAddress = s.value(QStringLiteral("rig/civAddress")).toString().trimmed();
    m_pttType = s.value(QStringLiteral("rig/pttType"), QStringLiteral("RIG")).toString();
    m_keyerPort = s.value(QStringLiteral("rig/keyerPort")).toString();
    m_keyerLine = s.value(QStringLiteral("rig/keyerLine"), QStringLiteral("DTR")).toString();
    connect(&m_winKeyer, &core::WinKeyer::failed, this, [this](const QString& why) {
        if (m_ctx.activity)
            m_ctx.activity(QStringLiteral("CW"), why, QStringLiteral("warning"));
        emit stateChanged();
    });
    connect(&m_winKeyer, &core::WinKeyer::versionReceived, this, [this](int v) {
        if (m_ctx.activity)
            m_ctx.activity(QStringLiteral("CW"), tr("WinKeyer answers: firmware %1").arg(v), QStringLiteral("info"));
        emit stateChanged();
    });
    connect(&m_winKeyer, &core::WinKeyer::busyChanged, this, [this](bool busy) {
        if (!busy)
            setActiveMacroIndex(-1);
    });
    connect(&m_keyer, &core::CwKeyer::failed, this, [this](const QString& why) {
        if (m_ctx.activity)
            m_ctx.activity(QStringLiteral("CW"), why, QStringLiteral("warning"));
        emit stateChanged();
    });
    connect(&m_keyer, &core::CwKeyer::finished, this, [this] {
        setActiveMacroIndex(-1);
    });
    if (!m_keyerPort.isEmpty())
        QTimer::singleShot(0, this, [this] { openKeyer(); });
    m_pttPort = s.value(QStringLiteral("rig/pttPort")).toString();
    m_audioInput = s.value(QStringLiteral("cw/audioInput")).toString();
    m_audioInputId = s.value(QStringLiteral("cw/audioInputId")).toString();
    m_decoderToneLock = s.value(QStringLiteral("cw/decoderToneLock"), 0).toInt();
    // Le versioni vecchie non hanno questa chiave; valori fuori dal campo
    // utile tornano automaticamente alla ricerca del tono.
    if (m_decoderToneLock < 300 || m_decoderToneLock > 1500)
        m_decoderToneLock = 0;
    m_decoderSpeedLock = s.value(QStringLiteral("cw/decoderSpeedLock"), 0).toInt();
    if (m_decoderSpeedLock < 5 || m_decoderSpeedLock > 59)
        m_decoderSpeedLock = 0;
    m_decoder.setTone(m_decoderToneLock);
    // Con un tono fissato, un passa-banda stretto tiene fuori le stazioni vicine.
    m_decoder.setNarrowFilter(true);
    m_decoder.setSpeed(m_decoderSpeedLock);
    m_skimmerOn = s.value(QStringLiteral("cw/skimmer"), true).toBool();
    m_typeToSend = s.value(QStringLiteral("cw/typeToSend"), false).toBool();
    m_skimmer.setLexicon(s.value(QStringLiteral("cw/lexicon"), true).toBool());
    m_decoder.setLexicon(s.value(QStringLiteral("cw/lexicon"), true).toBool());
    m_decoderModel = s.value(QStringLiteral("cw/neuralModel")).toString();
    if (!m_decoderModel.isEmpty() && !m_decoder.setNeuralModel(m_decoderModel))
        m_decoderModel.clear();   // il file non c'e' piu', o questa build non ha la rete: si torna a ggmorse
    loadMacros();

    for (core::RigLink* link : {static_cast<core::RigLink*>(&m_hamlib), static_cast<core::RigLink*>(&m_tci),
                                static_cast<core::RigLink*>(&m_flrig), static_cast<core::RigLink*>(&m_omniRig)}) {
        connect(link, &core::RigLink::changed, this, &RigController::stateChanged);
        connect(link, &core::RigLink::failed, this, [this](const QString& message) {
            if (m_ctx.activity)
                m_ctx.activity(QStringLiteral("CAT"), message, QStringLiteral("warning"));
            emit stateChanged();
        });
        connect(link, &core::RigLink::morseUnsupported, this, [this] {
            m_canKeyCw = false;
            setActiveMacroIndex(-1);
            emit stateChanged();
        });
        connect(link, &core::RigLink::morseSent, this, [this](const QString& text) {
            m_canKeyCw = true;
            if (m_ctx.activity)
                m_ctx.activity(QStringLiteral("CW"), tr("Sent: %1").arg(text), QStringLiteral("info"));
        });
    }
}

void RigController::start()
{
    // La lista delle radio si prepara da sola: quando si apre Impostazioni e' gia' li'.
    QTimer::singleShot(2500, this, &RigController::loadRigModels);
    if (m_enabled)
        connectNow();
}

QString RigController::frequencyLabel() const
{
    const qint64 hz = m_rig->frequencyHz();
    if (hz <= 0)
        return QStringLiteral("—");
    return QLocale().toString(hz / 1000000.0, 'f', 6) + QStringLiteral(" MHz");
}

QString RigController::txFrequencyLabel() const
{
    const qint64 hz = m_rig->txFrequencyHz();
    if (hz <= 0)
        return QString();
    return QLocale().toString(hz / 1000000.0, 'f', 6) + QStringLiteral(" MHz");
}

void RigController::splitUp(int hz)
{
    const qint64 rx = m_rig->frequencyHz();
    if (rx <= 0)
        return;
    m_rig->setSplit(true, rx + hz);
}

void RigController::swapVfo()
{
    m_rig->setVfo(m_rig->vfo() == QLatin1String("VFOB") ? QStringLiteral("VFOA") : QStringLiteral("VFOB"));
}

void RigController::setEnabled(bool on)
{
    if (on == m_enabled)
        return;
    m_enabled = on;
    QSettings().setValue(QStringLiteral("rig/enabled"), on);
    if (on)
        connectNow();
    else
        m_rig->disconnectFromRig();
    emit changed();
    emit stateChanged();
}

void RigController::setHost(const QString& host)
{
    const QString clean = host.trimmed();
    if (clean == m_host)
        return;
    m_host = clean;
    QSettings().setValue(QStringLiteral("rig/host"), clean);
    if (m_enabled)
        connectNow();
    emit changed();
}

void RigController::setPort(int port)
{
    if (port == m_port || port <= 0 || port > 65535)
        return;
    m_port = port;
    QSettings().setValue(QStringLiteral("rig/port"), port);
    if (m_enabled)
        connectNow();
    emit changed();
}

QVariantMap RigController::decodiumCat() const
{
    // Decodium tiene le sue nel registro, sotto "radio/manual-cat".
    QSettings decodium(QStringLiteral("HKEY_CURRENT_USER\\Software\\Decodium\\DECODIUM SDR\\radio\\manual-cat"),
                       QSettings::NativeFormat);
    QVariantMap out;
    out.insert(QStringLiteral("port"), decodium.value(QStringLiteral("port")).toString());
    out.insert(QStringLiteral("baud"), decodium.value(QStringLiteral("baud")).toInt());
    out.insert(QStringLiteral("driver"), decodium.value(QStringLiteral("driverId")).toString());
    return out;
}

void RigController::probeRadio()
{
    if (m_probeIndex >= 0)
        return;
    const QString exe = core::qsl::findRigctld();
    if (exe.isEmpty()) {
        if (m_ctx.activity)
            m_ctx.activity(QStringLiteral("CAT"), tr("Hamlib not found: install it first"),
                           QStringLiteral("warning"));
        return;
    }
    if (m_rigModel <= 0) {
        if (m_ctx.activity)
            m_ctx.activity(QStringLiteral("CAT"), tr("Pick the radio model first"), QStringLiteral("warning"));
        return;
    }

    // Prima la porta e la velocita' di adesso, poi quella di Decodium, poi
    // tutte le altre: di solito la prima o la seconda basta.
    const QStringList ports = serialPorts();
    QList<int> speeds{m_baud, 38400, 19200, 9600, 115200, 4800};
    m_probe.clear();
    // Mai le porte riservate (quella di Decodium) ne' gli estremi nascosti delle coppie
    // virtuali: sono di chi le ha create. Le porte virtuali condivise (DecoPort di Decodium)
    // vengono prima: rispondono subito e non tolgono la radio a nessuno.
    const QStringList virtualPorts = core::virtualSerialPorts();
    const QStringList order = core::serialProbeOrder(m_serialPort, ports, virtualPorts);
    for (const QString& port : order) {
        QList<int> seen;
        // Su una porta virtuale la velocita' non conta: un solo tentativo.
        const QList<int> candidates = virtualPorts.contains(port) ? QList<int>{m_baud > 0 ? m_baud : 38400} : speeds;
        for (const int baud : candidates) {
            if (baud <= 0 || seen.contains(baud))
                continue;
            seen << baud;
            m_probe << QVariantMap{{QStringLiteral("port"), port}, {QStringLiteral("baud"), baud}};
        }
    }
    if (m_probe.isEmpty()) {
        if (m_ctx.activity)
            m_ctx.activity(QStringLiteral("CAT"), tr("No serial port on this computer"), QStringLiteral("warning"));
        return;
    }

    m_rig->disconnectFromRig();
    m_probeIndex = 0;
    if (m_ctx.activity) {
        m_ctx.activity(QStringLiteral("CAT"),
                       tr("Looking for the radio on %n port(s)…", nullptr, static_cast<int>(order.size())),
                       QStringLiteral("info"));
    }
    emit stateChanged();
    probeNext();
}

// Ogni tentativo (porta, velocita') avvia un rigctld, aspetta che apra la seriale, gli chiede
// la frequenza e va al successivo. Tutto a eventi: con la radio spenta ogni tentativo aspetta
// la sua risposta fino a 2,7 secondi, e prima la finestra restava ferma per tutto il tempo.
void RigController::probeNext()
{
    ++m_probeGeneration;
    m_probeSocket.reset();
    m_probeBuffer.clear();
    if (m_probeProcess && m_probeProcess->state() != QProcess::NotRunning) {
        // Prima la seriale deve tornare libera: si aspetta la fine del processo senza fermare
        // la finestra.
        QProcess* process = m_probeProcess.get();
        process->disconnect();
        connect(process, &QProcess::finished, this, [this, process] {
            if (m_probeProcess.get() != process)
                return;
            m_probeProcess.release()->deleteLater();
            probeNext();
        });
        process->kill();
        return;
    }
    m_probeProcess.reset();
    if (m_probeIndex < 0 || m_probeIndex >= m_probe.size()) {
        probeFinish(false, QString(), 0);
        return;
    }

    const QVariantMap attempt = m_probe.at(m_probeIndex).toMap();
    const QString port = attempt.value(QStringLiteral("port")).toString();
    const int baud = attempt.value(QStringLiteral("baud")).toInt();

    QTcpServer probe;
    probe.listen(QHostAddress::LocalHost, 0);
    const quint16 chosen = probe.serverPort();
    probe.close();

    const quint64 generation = m_probeGeneration;
    m_probeProcess = std::make_unique<QProcess>();
    QProcess* process = m_probeProcess.get();
    connect(process, &QProcess::errorOccurred, this, [this, generation](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && generation == m_probeGeneration)
            probeAdvance();
    });
    // rigctld che se ne va da solo: la porta non c'e' o e' occupata. Inutile aspettare.
    connect(process, &QProcess::finished, this, [this, generation](int, QProcess::ExitStatus) {
        if (generation == m_probeGeneration)
            probeAdvance();
    });
    connect(process, &QProcess::started, this, [this, generation, port, baud, chosen] {
        // Un secondo per aprire la seriale, poi si chiede la frequenza.
        QTimer::singleShot(1200, this, [this, generation, port, baud, chosen] {
            if (generation == m_probeGeneration && m_probeIndex >= 0)
                probeAsk(generation, port, baud, chosen);
        });
    });
    process->start(core::qsl::findRigctld(),
                   {QStringLiteral("-m"), QString::number(m_rigModel),
                    QStringLiteral("-r"), port,
                    QStringLiteral("-s"), QString::number(baud),
                    QStringLiteral("-T"), QStringLiteral("127.0.0.1"),
                    QStringLiteral("-t"), QString::number(chosen)});
}

// Il tentativo e' andato male: si passa al prossimo, ma non da dentro il segnale che l'ha detto.
void RigController::probeAdvance()
{
    ++m_probeGeneration;
    ++m_probeIndex;
    QTimer::singleShot(0, this, &RigController::probeNext);
}

void RigController::probeAsk(quint64 generation, const QString& port, int baud, quint16 tcpPort)
{
    m_probeBuffer.clear();
    m_probeSocket = std::make_unique<QTcpSocket>();
    QTcpSocket* socket = m_probeSocket.get();
    connect(socket, &QTcpSocket::connected, this, [socket] { socket->write("+f\n"); });
    connect(socket, &QTcpSocket::readyRead, this, [this, generation, socket, port, baud] {
        if (generation != m_probeGeneration)
            return;
        m_probeBuffer += socket->readAll();
        qint64 hz = 0;
        for (const QString& line : QString::fromUtf8(m_probeBuffer).split(QLatin1Char('\n'))) {
            const QString clean = line.section(QLatin1Char(':'), -1).trimmed();
            bool ok = false;
            const qint64 value = clean.toLongLong(&ok);
            if (ok && value > 100000)
                hz = value;
        }
        if (hz > 0) {
            ++m_probeGeneration;
            QTimer::singleShot(0, this, [this, port, baud] { probeFinish(true, port, baud); });
        } else if (m_probeBuffer.contains("RPRT")) {
            // Ha risposto, ma con un errore: la radio non e' qui.
            probeAdvance();
        }
    });
    connect(socket, &QTcpSocket::errorOccurred, this, [this, generation](QAbstractSocket::SocketError) {
        if (generation == m_probeGeneration)
            probeAdvance();
    });
    // Senza risposta entro un secondo e mezzo dalla connessione si passa oltre.
    QTimer::singleShot(2700, this, [this, generation] {
        if (generation == m_probeGeneration && m_probeIndex >= 0)
            probeAdvance();
    });
    socket->connectToHost(QStringLiteral("127.0.0.1"), tcpPort);
}

void RigController::probeFinish(bool found, const QString& port, int baud)
{
    ++m_probeGeneration;
    m_probeSocket.reset();
    if (m_probeProcess) {
        // Prima di lasciare la seriale al rigctld vero: kill() e' questione di millisecondi.
        m_probeProcess->disconnect();
        m_probeProcess->kill();
        m_probeProcess->waitForFinished(500);
        m_probeProcess.reset();
    }
    m_probeIndex = -1;
    m_probe.clear();

    if (!found) {
        if (m_ctx.activity) {
            m_ctx.activity(QStringLiteral("CAT"),
                           tr("The radio did not answer on any port. Check that it is on, that the "
                              "CAT is enabled, and that no other program is holding the cable."),
                           QStringLiteral("warning"));
        }
        emit stateChanged();
        return;
    }

    setSerialPort(port);
    setBaud(baud);
    setLink(QStringLiteral("serial"));
    if (m_ctx.activity) {
        m_ctx.activity(QStringLiteral("CAT"), tr("Radio found on %1 at %2 baud").arg(port).arg(baud),
                       QStringLiteral("success"));
    }
    m_enabled = true;
    QSettings().setValue(QStringLiteral("rig/enabled"), true);
    connectNow();
    emit changed();
    emit stateChanged();
}

void RigController::setPttType(const QString& type)
{
    const QString clean = type.trimmed().toUpper();
    if (clean == m_pttType)
        return;
    m_pttType = clean;
    QSettings().setValue(QStringLiteral("rig/pttType"), clean);
    scheduleSerialRestart();
    emit changed();
}

void RigController::setPttPort(const QString& port)
{
    if (port == m_pttPort)
        return;
    m_pttPort = port.trimmed();
    QSettings().setValue(QStringLiteral("rig/pttPort"), m_pttPort);
    scheduleSerialRestart();
    emit changed();
}

void RigController::testPtt(int milliseconds)
{
    if (!m_rig->connected()) {
        if (m_ctx.activity)
            m_ctx.activity(QStringLiteral("CAT"), tr("The radio is not connected: no PTT"),
                           QStringLiteral("warning"));
        return;
    }
    m_rig->setPtt(true);
    if (m_ctx.activity)
        m_ctx.activity(QStringLiteral("CAT"), tr("PTT on for a moment: the radio should transmit"),
                       QStringLiteral("info"));
    QTimer::singleShot(qBound(100, milliseconds, 5000), this, [this] { m_rig->setPtt(false); });
}

void RigController::setWpm(int wpm)
{
    const int clamped = qBound(5, wpm, 60);
    m_wpm = clamped;
    QSettings().setValue(QStringLiteral("cw/wpm"), clamped);
    m_rig->setSpeedWpm(clamped);
    m_winKeyer.setSpeed(clamped);
    emit stateChanged();
}

void RigController::overrideConnection(const QString& host, int port)
{
    m_host = host.trimmed().isEmpty() ? QStringLiteral("127.0.0.1") : host.trimmed();
    m_port = port > 0 ? port : 4532;
    m_enabled = true;
    // --rig e' un rigctld: per questa volta, anche se nelle impostazioni c'e' TCI.
    m_link = QStringLiteral("network");
    connectNow();
    emit changed();
}

void RigController::overrideTci(const QString& address, int trx)
{
    m_tciAddress = address.trimmed().isEmpty() ? QStringLiteral("127.0.0.1:40001") : address.trimmed();
    m_tciTrx = qMax(0, trx);
    m_link = QStringLiteral("tci");
    m_enabled = true;
    connectNow();
    emit changed();
}

void RigController::scheduleSerialRestart()
{
    if (!m_enabled || m_link != QLatin1String("serial"))
        return;
    if (!m_serialRestartTimer) {
        m_serialRestartTimer = new QTimer(this);
        m_serialRestartTimer->setSingleShot(true);
        m_serialRestartTimer->setInterval(700);
        connect(m_serialRestartTimer, &QTimer::timeout, this, &RigController::restartSerialNow);
    }
    m_serialRestartTimer->start();
}

void RigController::restartSerialNow()
{
    if (!m_enabled || m_link != QLatin1String("serial"))
        return;
    // Quello che era in attesa di un avvio precedente non vale piu'.
    ++m_launchGeneration;
    const quint64 generation = m_launchGeneration;
    m_hamlib.disconnectFromRig();
    if (m_rigctld && m_rigctld->state() != QProcess::NotRunning) {
        // La seriale si libera solo quando rigctld e' finito: si aspetta senza fermare la finestra.
        QProcess* process = m_rigctld.get();
        process->disconnect();
        connect(process, &QProcess::finished, this, [this, process, generation] {
            if (m_rigctld.get() != process)
                return;
            m_rigctld.release()->deleteLater();
            if (generation == m_launchGeneration)
                connectNow();
        });
        process->kill();
        return;
    }
    m_rigctld.reset();
    connectNow();
}

void RigController::reconnectSerial()
{
    if (m_link != QLatin1String("serial"))
        return;
    if (!m_enabled) {
        m_enabled = true;
        QSettings().setValue(QStringLiteral("rig/enabled"), true);
    }
    if (m_serialRestartTimer)
        m_serialRestartTimer->stop();
    restartSerialNow();
    emit changed();
}

void RigController::connectNow()
{
    // Quello che era in attesa di un avvio precedente non vale piu'.
    ++m_launchGeneration;
    // Radio nuova, speranza nuova: finche' non dice di no, si prova.
    m_canKeyCw = true;
    // Hamlib's Yaesu keyer starts a radio memory with KY, independently of an
    // RTS/DTR PTT route, and most of those backends have no stop_morse.  Our
    // local rigctld knows the actual model, so its emergency connection may
    // additionally send the documented CAT receive command.  Never guess it
    // for an external/network rigctld whose radio model is unknown.
    m_hamlib.setEmergencyCatOffCommand(
        directYaesuCwUsesKeyerMemory() ? QStringLiteral("TX0;") : QString());
    m_hamlib.setYaesuKeyerMemoryMode(directYaesuCwUsesKeyerMemory());
    if (m_link == QLatin1String("flrig") || m_link == QLatin1String("omnirig")) {
        // flrig o OmniRig: la radio la tiene un altro programma, noi chiediamo a lui.
        m_hamlib.disconnectFromRig();
        m_tci.disconnectFromRig();
        if (m_link == QLatin1String("flrig")) {
            m_omniRig.disconnectFromRig();
            m_rig = &m_flrig;
            m_flrig.connectTo(m_flrigAddress);
        } else {
            m_flrig.disconnectFromRig();
            m_rig = &m_omniRig;
            m_omniRig.connectTo(m_omniRigNumber);
        }
        m_rig->setSpeedWpm(m_wpm);
        emit stateChanged();
        return;
    }
    m_flrig.disconnectFromRig();
    m_omniRig.disconnectFromRig();
    if (m_link == QLatin1String("tci")) {
        // TCI: niente rigctld, si parla al programma della radio.
        m_hamlib.disconnectFromRig();
        m_rig = &m_tci;
        m_tci.connectTo(m_tciAddress, m_tciTrx);
        m_tci.setSpeedWpm(m_wpm);
        emit stateChanged();
        return;
    }
    m_tci.disconnectFromRig();
    m_rig = &m_hamlib;
    if (m_link == QLatin1String("serial")) {
        // rigctld parte su un processo a parte e la connessione segue quando e' pronto: la
        // finestra non aspetta (prima, a ogni modifica di una voce della radio, restava
        // ferma fino a 4 secondi + 0,6 ad ogni avvio).
        startLocalRigctld();
        emit stateChanged();
        return;
    }
    m_hamlib.connectTo(m_host, static_cast<quint16>(m_port));
    emit stateChanged();
}

// La radio attaccata col cavo: DecoDXLog non parla la lingua di ogni radio, ma
// Hamlib si'. Quindi si avvia rigctld su quella porta e gli si parla come
// sempre — per chi opera, e' solo "COM5, questa radio".
void RigController::startLocalRigctld()
{
    const quint64 generation = m_launchGeneration;
    // Gia' in piedi: basta collegarsi (aspettando che abbia finito di aprire la seriale, se e'
    // partito da poco).
    if (m_rigctld && m_rigctld->state() != QProcess::NotRunning) {
        constexpr qint64 kWarmupMs = 600;
        const qint64 age = m_rigctldClock.isValid() ? m_rigctldClock.elapsed() : kWarmupMs;
        connectHamlibLater(generation, qMax<qint64>(0, kWarmupMs - age));
        return;
    }
    const QString exe = core::qsl::findRigctld();
    if (exe.isEmpty()) {
        if (m_ctx.activity) {
            m_ctx.activity(QStringLiteral("CAT"),
                           tr("Hamlib not found: install it, or start rigctld yourself and "
                              "use the network link"),
                           QStringLiteral("warning"));
        }
        connectHamlibLater(generation, 0);
        return;
    }
    if (m_serialPort.isEmpty() || m_rigModel <= 0) {
        if (m_ctx.activity)
            m_ctx.activity(QStringLiteral("CAT"), tr("Pick the radio model and the serial port first"),
                           QStringLiteral("warning"));
        connectHamlibLater(generation, 0);
        return;
    }
    // Una porta di un altro programma (di serie, quella di Decodium) non si apre:
    // se la prende DecoDXLog, Decodium resta senza radio.
    for (const QString& used : {m_serialPort, m_pttType == QLatin1String("RIG") ? QString() : m_pttPort}) {
        if (used.isEmpty() || !core::isSerialPortReserved(used))
            continue;
        if (m_ctx.activity) {
            m_ctx.activity(QStringLiteral("CAT"),
                           tr("%1 is reserved for another program (Decodium): DecoDXLog does not open it. "
                              "Pick another port in Setup → Radio, for instance the shared virtual port "
                              "of Decodium, or change the reservation there").arg(used),
                           QStringLiteral("warning"));
        }
        connectHamlibLater(generation, 0);
        return;
    }
    // L'app puo' avviare la radio prima che la pagina Impostazioni sia stata
    // aperta: in quel caso scopriamo qui le capacita', prima di costruire la
    // riga di comando, non solo quando la UI le deve visualizzare.
    requestCapabilities([this, generation] {
        if (generation == m_launchGeneration)
            launchRigctld(generation);
    });
}

void RigController::launchRigctld(quint64 generation)
{
    const QString exe = core::qsl::findRigctld();
    if (exe.isEmpty() || (m_rigctld && m_rigctld->state() != QProcess::NotRunning)) {
        connectHamlibLater(generation, 0);
        return;
    }

    // Una porta TCP libera, cosi' due programmi non si pestano i piedi.
    QTcpServer probe;
    probe.listen(QHostAddress::LocalHost, 0);
    const quint16 chosen = probe.serverPort();
    probe.close();

    m_host = QStringLiteral("127.0.0.1");
    m_port = chosen;
    m_rigctld = std::make_unique<QProcess>();
    const QStringList arguments = localRigctldArguments(m_serialPort, m_baud, chosen);
    m_rigctld->setProcessChannelMode(QProcess::MergedChannels);
    QProcess* process = m_rigctld.get();
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process] {
        const QString text = QString::fromUtf8(process->readAll());
        for (const QString& line : text.split(QLatin1Char('\n'))) {
            // Le righe che contano sono quelle dove Hamlib dice che non ce la fa.
            if ((line.contains(QLatin1String("error"), Qt::CaseInsensitive)
                 || line.contains(QLatin1String("cannot set"), Qt::CaseInsensitive))
                && m_ctx.activity && !line.trimmed().isEmpty()) {
                m_ctx.activity(QStringLiteral("CAT"), tr("Hamlib: %1").arg(line.trimmed()),
                               QStringLiteral("warning"));
            }
        }
    });
    connect(process, &QProcess::started, this, [this, generation] {
        if (m_ctx.activity) {
            m_ctx.activity(QStringLiteral("CAT"),
                           tr("Hamlib started on %1 (model %2, %3 baud)")
                               .arg(m_serialPort).arg(m_rigModel).arg(m_baud),
                           QStringLiteral("info"));
        }
        // Un attimo: rigctld apre la seriale e poi si mette in ascolto.
        m_rigctldClock.restart();
        connectHamlibLater(generation, 600);
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, generation](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || m_rigctld.get() != process)
            return;
        if (m_ctx.activity)
            m_ctx.activity(QStringLiteral("CAT"), tr("rigctld did not start"), QStringLiteral("warning"));
        m_rigctld.release()->deleteLater();
        connectHamlibLater(generation, 0);
    });
    process->start(exe, arguments);
}

// La connessione a Hamlib, subito o fra un po': solo se nel frattempo nessuno ha ricominciato.
void RigController::connectHamlibLater(quint64 generation, qint64 delayMs)
{
    auto connectIfCurrent = [this, generation] {
        if (generation != m_launchGeneration || m_rig != &m_hamlib)
            return;
        m_hamlib.connectTo(m_host, static_cast<quint16>(m_port));
        emit stateChanged();
    };
    if (delayMs <= 0)
        connectIfCurrent();
    else
        QTimer::singleShot(delayMs, this, connectIfCurrent);
}

void RigController::setLink(const QString& link)
{
    const QString clean = link == QLatin1String("serial") || link == QLatin1String("tci")
                                  || link == QLatin1String("flrig") || link == QLatin1String("omnirig")
                              ? link : QStringLiteral("network");
    if (clean == m_link)
        return;
    m_link = clean;
    QSettings().setValue(QStringLiteral("rig/link"), clean);
    if (m_enabled)
        connectNow();
    emit changed();
    emit stateChanged();
}

void RigController::setTciAddress(const QString& address)
{
    const QString clean = address.trimmed().isEmpty() ? QStringLiteral("127.0.0.1:40001") : address.trimmed();
    if (clean == m_tciAddress)
        return;
    m_tciAddress = clean;
    QSettings().setValue(QStringLiteral("rig/tciAddress"), clean);
    if (m_enabled && m_link == QLatin1String("tci"))
        connectNow();
    emit changed();
}

void RigController::setFlrigAddress(const QString& address)
{
    const QString clean = address.trimmed().isEmpty() ? QStringLiteral("127.0.0.1:12345") : address.trimmed();
    if (clean == m_flrigAddress)
        return;
    m_flrigAddress = clean;
    QSettings().setValue(QStringLiteral("rig/flrigAddress"), clean);
    if (m_enabled && m_link == QLatin1String("flrig"))
        connectNow();
    emit changed();
}

void RigController::setOmniRigNumber(int number)
{
    const int clean = number == 2 ? 2 : 1;
    if (clean == m_omniRigNumber)
        return;
    m_omniRigNumber = clean;
    QSettings().setValue(QStringLiteral("rig/omniRigNumber"), clean);
    if (m_enabled && m_link == QLatin1String("omnirig"))
        connectNow();
    emit changed();
}

void RigController::configureShare(bool enabled, int port, bool allowControl, bool allowPtt)
{
    const int clean = port > 1024 && port <= 65535 ? port : 4533;
    QSettings s;
    s.setValue(QStringLiteral("rig/share/enabled"), enabled);
    s.setValue(QStringLiteral("rig/share/port"), clean);
    s.setValue(QStringLiteral("rig/share/control"), allowControl);
    s.setValue(QStringLiteral("rig/share/ptt"), allowPtt && allowControl);
    const bool ok = m_share.configure(enabled, clean, allowControl, allowPtt);
    if (m_ctx.activity) {
        if (!enabled)
            m_ctx.activity(QStringLiteral("CAT"), tr("Shared CAT off"), QStringLiteral("info"));
        else if (ok)
            m_ctx.activity(QStringLiteral("CAT"), tr("Shared CAT on 127.0.0.1:%1: other programs connect as \"Hamlib NET rigctl\"")
                                                      .arg(clean), QStringLiteral("success"));
        else
            m_ctx.activity(QStringLiteral("CAT"), tr("Shared CAT not started on port %1: %2").arg(clean).arg(m_share.lastError()),
                           QStringLiteral("warning"));
    }
}

void RigController::useSharedCat(const QString& host, int port)
{
    // La propria condivisione non si usa da qui: sarebbe DecoDXLog che parla
    // con se stesso, e la radio non ci sarebbe da nessuna parte.
    const QString h = host.trimmed();
    const bool local = h.isEmpty() || h == QLatin1String("127.0.0.1") || h.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0;
    if (local && m_share.listening() && port == m_share.port()) {
        if (m_ctx.activity)
            m_ctx.activity(QStringLiteral("CAT"),
                           tr("Port %1 is DecoDXLog's own shared CAT: choose the port of the program that holds the radio")
                               .arg(port),
                           QStringLiteral("warning"));
        return;
    }
    m_host = host.trimmed().isEmpty() ? QStringLiteral("127.0.0.1") : host.trimmed();
    m_port = port > 0 ? port : 4533;
    m_link = QStringLiteral("network");
    m_enabled = true;
    QSettings s;
    s.setValue(QStringLiteral("rig/host"), m_host);
    s.setValue(QStringLiteral("rig/port"), m_port);
    s.setValue(QStringLiteral("rig/link"), m_link);
    s.setValue(QStringLiteral("rig/enabled"), true);
    connectNow();
    emit changed();
    emit stateChanged();
}

bool RigController::omniRigAvailable() const
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

void RigController::setTciTrx(int trx)
{
    const int clean = qBound(0, trx, 7);
    if (clean == m_tciTrx)
        return;
    m_tciTrx = clean;
    QSettings().setValue(QStringLiteral("rig/tciTrx"), clean);
    if (m_enabled && m_link == QLatin1String("tci"))
        connectNow();
    emit changed();
}

void RigController::setSerialPort(const QString& port)
{
    if (port == m_serialPort)
        return;
    m_serialPort = port.trimmed();
    QSettings().setValue(QStringLiteral("rig/serialPort"), m_serialPort);
    scheduleSerialRestart();
    emit changed();
}

QString RigController::reservedPorts() const
{
    return core::userReservedSerialPorts().join(QStringLiteral(", "));
}

void RigController::setReservedPorts(const QString& ports)
{
    const QStringList list = ports.split(QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts);
    if (list == core::userReservedSerialPorts())
        return;
    core::setUserReservedSerialPorts(list);
    scheduleSerialRestart();
    emit changed();
}

bool RigController::leaveDecodiumPort() const
{
    return core::leaveDecodiumPort();
}

void RigController::setLeaveDecodiumPort(bool on)
{
    if (on == core::leaveDecodiumPort())
        return;
    core::setLeaveDecodiumPort(on);
    scheduleSerialRestart();
    emit changed();
}

QString RigController::decodiumCatPort() const
{
    return core::decodiumCatPort();
}

bool RigController::isPortReserved(const QString& port) const
{
    return core::isSerialPortReserved(port);
}

QStringList RigController::virtualPorts() const
{
    return core::virtualSerialPorts();
}

void RigController::setRigModel(int model)
{
    if (model == m_rigModel)
        return;
    m_rigModel = model;
    m_capabilitiesForModel = 0;
    QSettings().setValue(QStringLiteral("rig/model"), model);
    refreshSerialCapabilities();
    scheduleSerialRestart();
    emit changed();
    emit stateChanged();
}

void RigController::setBaud(int baud)
{
    if (baud == m_baud || baud <= 0)
        return;
    m_baud = baud;
    QSettings().setValue(QStringLiteral("rig/baud"), baud);
    scheduleSerialRestart();
    emit changed();
}

bool RigController::supportsSerialParameter(const QString& name) const
{
    return m_serialCapabilities.contains(name);
}

void RigController::refreshSerialCapabilities()
{
    requestCapabilities({});
}

// Le opzioni della seriale del modello scelto, da `rigctld -m N -L` su un processo a parte.
// `then` parte quando le capacita' sono quelle del modello di adesso (subito, se gia' lo sono).
void RigController::requestCapabilities(std::function<void()> then)
{
    if (then)
        m_afterCapabilities.append(std::move(then));
    auto flush = [this] {
        const auto waiting = std::exchange(m_afterCapabilities, {});
        for (const auto& f : waiting)
            f();
    };

    if (m_rigModel <= 0) {
        if (!m_serialCapabilities.isEmpty()) {
            m_serialCapabilities.clear();
            m_capabilitiesForModel = 0;
            emit serialCapabilitiesChanged();
        }
        flush();
        return;
    }
    if (m_capabilitiesForModel == m_rigModel) {
        flush();
        return;
    }
    // Una domanda e' gia' in volo: chi aspetta e' in coda, e al suo arrivo si guarda se
    // il modello e' ancora quello.
    if (m_capsProcess)
        return;

    const QString exe = core::qsl::findRigctld();
    if (exe.isEmpty()) {
        m_capabilitiesForModel = m_rigModel;
        if (!m_serialCapabilities.isEmpty()) {
            m_serialCapabilities.clear();
            emit serialCapabilitiesChanged();
        }
        flush();
        return;
    }

    m_capsModel = m_rigModel;
    m_capsProcess = std::make_unique<QProcess>();
    QProcess* process = m_capsProcess.get();
    auto done = [this, process](const QStringList& capabilities) {
        if (m_capsProcess.get() != process)
            return;
        m_capsProcess.release()->deleteLater();
        if (m_capsModel == m_rigModel) {
            QStringList unique = capabilities;
            unique.removeDuplicates();
            m_capabilitiesForModel = m_capsModel;
            if (m_serialCapabilities != unique) {
                m_serialCapabilities = unique;
                emit serialCapabilitiesChanged();
            }
            const auto waiting = std::exchange(m_afterCapabilities, {});
            for (const auto& f : waiting)
                f();
        } else {
            // Nel frattempo e' stato scelto un altro modello: si chiede per quello.
            requestCapabilities({});
        }
    };
    connect(process, &QProcess::finished, this, [process, done](int, QProcess::ExitStatus) {
        QStringList capabilities;
        const QString text = QString::fromUtf8(process->readAllStandardOutput());
        static const QRegularExpression name(QStringLiteral("^[a-z_]+$"));
        for (const QString& line : text.split(QLatin1Char('\n'))) {
            const int colon = line.indexOf(QLatin1Char(':'));
            const QString key = line.left(colon).trimmed();
            if (colon > 0 && name.match(key).hasMatch())
                capabilities << key;
        }
        done(capabilities);
    });
    connect(process, &QProcess::errorOccurred, this, [done](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            done({});
    });
    // Un rigctld che non risponde non tiene in coda per sempre chi aspetta.
    QTimer::singleShot(4000, this, [this, process, done] {
        if (m_capsProcess.get() == process) {
            process->disconnect();
            process->kill();
            done({});
        }
    });
    process->start(exe, {QStringLiteral("-m"), QString::number(m_rigModel), QStringLiteral("-L")});
}

QStringList RigController::localRigctldArguments(const QString& port, int baud, quint16 tcpPort) const
{
    QStringList arguments{QStringLiteral("-m"), QString::number(m_rigModel),
                          QStringLiteral("-r"), port,
                          QStringLiteral("-s"), QString::number(baud),
                          QStringLiteral("-T"), QStringLiteral("127.0.0.1"),
                          QStringLiteral("-t"), QString::number(tcpPort)};
    auto addConfiguration = [&arguments, this](const QString& name, const QString& value,
                                                const QString& defaultValue = QString()) {
        if (supportsSerialParameter(name) && !value.isEmpty() && value != defaultValue)
            arguments << QStringLiteral("-C") << (name + QLatin1Char('=') + value);
    };
    addConfiguration(QStringLiteral("data_bits"), m_dataBits, QStringLiteral("Default"));
    addConfiguration(QStringLiteral("stop_bits"), m_stopBits, QStringLiteral("Default"));
    addConfiguration(QStringLiteral("serial_parity"), m_parity, QStringLiteral("Default"));
    // Con RTS forzato il controllo di flusso hardware non puo' stare (Hamlib non apre la porta
    // e DecoDXLog si collega e si scollega di continuo): si passa a «None».
    addConfiguration(QStringLiteral("serial_handshake"), core::serialHandshakeFor(m_handshake, m_rtsState),
                     QStringLiteral("Default"));
    addConfiguration(QStringLiteral("dtr_state"), m_dtrState, QStringLiteral("Unset"));
    addConfiguration(QStringLiteral("rts_state"), m_rtsState, QStringLiteral("Unset"));
    if (supportsSerialParameter(QStringLiteral("civaddr")) && !m_civAddress.isEmpty())
        arguments << QStringLiteral("-c") << m_civAddress;
    // Il PTT su un'altra porta: e' il caso di tante stazioni, dove il CAT sta
    // su una COM e il PTT alza RTS o DTR sull'altra.
    if (m_pttType != QLatin1String("RIG") && !m_pttType.isEmpty()) {
        arguments << QStringLiteral("-P") << m_pttType;
        if (!m_pttPort.isEmpty())
            arguments << QStringLiteral("-p") << m_pttPort;
    }
    return arguments;
}

void RigController::setDataBits(const QString& value)
{
    const QString clean = value == QLatin1String("7") || value == QLatin1String("8") ? value : QStringLiteral("Default");
    if (clean == m_dataBits) return;
    m_dataBits = clean; QSettings().setValue(QStringLiteral("rig/dataBits"), clean); scheduleSerialRestart(); emit changed();
}

void RigController::setStopBits(const QString& value)
{
    const QString clean = value == QLatin1String("1") || value == QLatin1String("2") ? value : QStringLiteral("Default");
    if (clean == m_stopBits) return;
    m_stopBits = clean; QSettings().setValue(QStringLiteral("rig/stopBits"), clean); scheduleSerialRestart(); emit changed();
}

void RigController::setParity(const QString& value)
{
    const QString clean = QStringList{QStringLiteral("None"), QStringLiteral("Odd"), QStringLiteral("Even"), QStringLiteral("Mark"), QStringLiteral("Space")}.contains(value) ? value : QStringLiteral("Default");
    if (clean == m_parity) return;
    m_parity = clean; QSettings().setValue(QStringLiteral("rig/parity"), clean); scheduleSerialRestart(); emit changed();
}

void RigController::setHandshake(const QString& value)
{
    const QString clean = QStringList{QStringLiteral("None"), QStringLiteral("XONXOFF"), QStringLiteral("Hardware")}.contains(value) ? value : QStringLiteral("Default");
    if (clean == m_handshake) return;
    m_handshake = clean; QSettings().setValue(QStringLiteral("rig/handshake"), clean); scheduleSerialRestart(); emit changed();
}

void RigController::setDtrState(const QString& value)
{
    const QString clean = QStringList{QStringLiteral("ON"), QStringLiteral("OFF")}.contains(value) ? value : QStringLiteral("Unset");
    if (clean == m_dtrState) return;
    m_dtrState = clean; QSettings().setValue(QStringLiteral("rig/dtrState"), clean); scheduleSerialRestart(); emit changed();
}

void RigController::setRtsState(const QString& value)
{
    const QString clean = QStringList{QStringLiteral("ON"), QStringLiteral("OFF")}.contains(value) ? value : QStringLiteral("Unset");
    if (clean == m_rtsState) return;
    m_rtsState = clean; QSettings().setValue(QStringLiteral("rig/rtsState"), clean); scheduleSerialRestart(); emit changed();
}

void RigController::setCivAddress(const QString& value)
{
    const QString clean = value.trimmed();
    if (clean == m_civAddress) return;
    m_civAddress = clean; QSettings().setValue(QStringLiteral("rig/civAddress"), clean); scheduleSerialRestart(); emit changed();
}

QVariantList RigController::rigModels()
{
    if (m_models.isEmpty())
        loadRigModels();
    return m_models;
}

// "rigctld -l" scrive l'elenco di tutte le radio che Hamlib conosce. Si legge su un processo a
// parte: la lista arriva con rigModelsChanged() e la finestra non aspetta nessuno.
void RigController::loadRigModels()
{
    if (m_modelsProcess || !m_models.isEmpty())
        return;
    const QString exe = core::qsl::findRigctld();
    if (exe.isEmpty())
        return;
    m_modelsProcess = std::make_unique<QProcess>();
    QProcess* process = m_modelsProcess.get();
    connect(process, &QProcess::finished, this, [this, process](int, QProcess::ExitStatus) {
        if (m_modelsProcess.get() != process)
            return;
        const QString text = QString::fromUtf8(process->readAllStandardOutput());
        m_modelsProcess.release()->deleteLater();
        QVariantList models;
        for (const QString& line : text.split(QLatin1Char('\n'))) {
            const QString clean = line.trimmed();
            if (clean.isEmpty() || !clean.at(0).isDigit())
                continue;
            const int id = clean.section(QLatin1Char(' '), 0, 0).toInt();
            const QString rest = clean.section(QLatin1Char(' '), 1).simplified();
            if (id > 0 && !rest.isEmpty()) {
                models << QVariantMap{{QStringLiteral("id"), id},
                                      {QStringLiteral("name"), QStringLiteral("%1 — %2").arg(id).arg(rest.left(40))}};
            }
        }
        if (!models.isEmpty()) {
            m_models = models;
            emit rigModelsChanged();
        }
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && m_modelsProcess.get() == process)
            m_modelsProcess.release()->deleteLater();
    });
    QTimer::singleShot(15000, this, [this, process] {
        if (m_modelsProcess.get() == process) {
            process->disconnect();
            process->kill();
            m_modelsProcess.release()->deleteLater();
        }
    });
    process->start(exe, {QStringLiteral("-l")});
}

QStringList RigController::serialPorts() const
{
    return core::availableSerialPorts();
}

// ── Il decoder CW ────────────────────────────────────────────────────────

QStringList RigController::audioInputs() const
{
    decolog::StartupSpan trace("QMediaDevices::audioInputs");
    QStringList out;
    for (const QAudioDevice& device : QMediaDevices::audioInputs())
        out << device.description();
    return out;
}

QVariantList RigController::audioInputDevices() const
{
    // L'elenco cambia solo quando cambiano le schede: si ascolta il sistema
    // invece di rileggerlo a ogni pezzetto di audio che arriva.
    if (!m_mediaDevices) {
        auto* self = const_cast<RigController*>(this);
        m_mediaDevices = new QMediaDevices(self);
        connect(m_mediaDevices, &QMediaDevices::audioInputsChanged, self, [self] {
            emit self->audioDevicesChanged();
            emit self->audioSelectionChanged();
        });
    }
    return app::audiodev::deviceList(app::audiodev::inputDevices());
}

int RigController::audioInputIndex() const
{
    const auto entries = app::audiodev::entriesOf(app::audiodev::inputDevices());
    return app::audiodev::comboIndex(app::audiodev::resolve(entries, {m_audioInputId, m_audioInput}));
}

void RigController::chooseAudioInput(int row)
{
    const auto devices = app::audiodev::inputDevices();
    const app::audiodev::Saved saved = app::audiodev::savedFor(devices, row);
    if (saved.id == m_audioInputId && saved.name == m_audioInput)
        return;
    m_audioInputId = saved.id;
    m_audioInput = saved.name;
    QSettings s;
    s.setValue(QStringLiteral("cw/audioInputId"), m_audioInputId);
    s.setValue(QStringLiteral("cw/audioInput"), m_audioInput);
    if (m_decoderOn) {
        stopAudio();
        startAudio();
        if (!m_decoderOn)
            emit decoderChanged();
    }
    emit audioSelectionChanged();
}

void RigController::setAudioInput(const QString& name)
{
    // Per nome soltanto (impostazioni vecchie, prove): niente identificativo.
    if (name == m_audioInput && m_audioInputId.isEmpty())
        return;
    m_audioInput = name;
    m_audioInputId.clear();
    QSettings s;
    s.setValue(QStringLiteral("cw/audioInput"), name);
    s.setValue(QStringLiteral("cw/audioInputId"), QString());
    if (m_decoderOn) {
        stopAudio();
        startAudio();
    }
    emit audioSelectionChanged();
    emit decoderChanged();
}

void RigController::setDecoderOn(bool on)
{
    if (on == m_decoderOn)
        return;
    m_decoderOn = on;
    if (on)
        startAudio();
    else
        stopAudio();
    emit decoderChanged();
}

void RigController::setDecoderToneLock(int hz)
{
    // Zero e' "Auto". Il CW audio di una radio cade normalmente fra 300 e
    // 1500 Hz; non accettiamo altri numeri per non lasciare il decoder in una
    // configurazione silenziosamente impossibile.
    const int clean = hz <= 0 ? 0 : std::clamp(hz, 300, 1500);
    if (clean == m_decoderToneLock)
        return;
    m_decoderToneLock = clean;
    QSettings().setValue(QStringLiteral("cw/decoderToneLock"), clean);
    m_decoder.setTone(clean);
    // La finestra di analisi contiene ancora il tono precedente: svuotarla
    // impedisce che punti e linee di due frequenze diverse finiscano assieme.
    m_decoder.reset();
    publishScope(true);
    emit decoderChanged();
    if (m_ctx.activity) {
        const QString what = clean > 0 ? tr("CW decoder tone locked at %1 Hz").arg(clean)
                                       : tr("CW decoder tone set to automatic search");
        m_ctx.activity(QStringLiteral("CW"), what, QStringLiteral("info"));
    }
}

void RigController::setDecoderLexicon(bool on)
{
    if (on == m_decoder.lexicon())
        return;
    m_decoder.setLexicon(on);
    m_skimmer.setLexicon(on);
    QSettings().setValue(QStringLiteral("cw/lexicon"), on);
    emit decoderChanged();
}

void RigController::setSkimmerOn(bool on)
{
    if (on == m_skimmerOn)
        return;
    m_skimmerOn = on;
    QSettings().setValue(QStringLiteral("cw/skimmer"), on);
    m_skimmer.reset();
    publishSkimmer(true);
}

QString RigController::decoderHtml(const QString& linkColor) const
{
    return core::cwcalls::toHtml(m_decoderText, linkColor);
}

void RigController::listenTo(double toneHz)
{
    setDecoderToneLock(static_cast<int>(std::lround(toneHz)));
}

QString RigController::stationKhz(double toneHz) const
{
    const qint64 dial = frequencyHz();
    if (dial <= 0 || toneHz <= 0)
        return {};
    const QString m = mode().toUpper();
    double offset = 0;
    if (m.startsWith(QLatin1String("CW"))) {
        // In CW la radio mostra la frequenza della stazione che si sta ascoltando: le altre stanno
        // sopra o sotto di quanto il loro tono dista da quello.
        const double ref = m_decoderToneLock > 0 ? m_decoderToneLock : m_decoder.toneHz();
        offset = ref > 0 ? toneHz - ref : 0.0;
        if (m == QLatin1String("CWR"))
            offset = -offset;
    } else if (m.startsWith(QLatin1String("LSB"))) {
        offset = -toneHz;
    } else {
        offset = toneHz;
    }
    return QString::number((static_cast<double>(dial) + offset) / 1000.0, 'f', 1);
}

void RigController::publishSkimmer(bool force)
{
    if (!force && m_skimmerClock.isValid() && m_skimmerClock.elapsed() < 250)
        return;
    m_skimmerClock.restart();

    QVariantList stations;
    QVariantList spectrum;
    if (m_skimmerOn && m_decoderOn) {
        const QString band = frequencyHz() > 0
                                 ? core::bands::fromMhz(static_cast<double>(frequencyHz()) / 1e6)
                                 : QString();
        const double mainTone = m_decoderToneLock > 0 ? m_decoderToneLock : m_decoder.toneHz();
        for (const core::CwSkimmer::Station& s : m_skimmer.stations()) {
            QString tail = s.text.right(48);
            tail.replace(QLatin1Char('\n'), QLatin1Char(' '));
            QVariantMap row{
                {QStringLiteral("id"), s.id},
                {QStringLiteral("freq"), static_cast<int>(std::lround(s.freq))},
                {QStringLiteral("wpm"), s.wpm},
                {QStringLiteral("level"), static_cast<int>(std::lround(s.level))},
                {QStringLiteral("confidence"), s.confidence},
                {QStringLiteral("text"), tail.trimmed()},
                {QStringLiteral("call"), s.call},
                {QStringLiteral("cq"), s.cq},
                {QStringLiteral("idle"), static_cast<int>(s.idleSeconds)},
                {QStringLiteral("present"), s.present},
                {QStringLiteral("listening"), mainTone > 0 && std::abs(s.freq - mainTone) < 25.0},
                {QStringLiteral("khz"), stationKhz(s.freq)},
            };
            if (!s.call.isEmpty() && m_ctx.classifyCall) {
                const QVariantMap c = m_ctx.classifyCall(s.call, band, QStringLiteral("CW"));
                for (auto it = c.cbegin(); it != c.cend(); ++it)
                    row.insert(it.key(), it.value());
            }
            stations.append(row);
        }
        const QVector<float>& view = m_skimmer.spectrum();
        spectrum.reserve(view.size());
        for (const float v : view)
            spectrum.append(v);
    }
    m_stations = stations;
    m_spectrum = spectrum;
    emit skimmerChanged();
}

bool RigController::chooseDecoderModel(const QUrl& file)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    const bool ok = m_decoder.setNeuralModel(path);
    if (!ok) {
        m_decoderModel.clear();   // ggmorse resta: il pannello non deve dire «modello» se non c'e'
        if (m_ctx.activity)
            m_ctx.activity(QStringLiteral("CW"),
                           core::CwDecoder::neuralAvailable() ? tr("The neural CW model cannot be loaded: %1").arg(path)
                                                              : tr("This build has no neural CW decoder (ONNX Runtime missing)"),
                           QStringLiteral("warning"));
        emit decoderChanged();
        return false;
    }
    m_decoderModel = path;
    QSettings().setValue(QStringLiteral("cw/neuralModel"), path);
    m_decoder.reset();
    publishScope(true);
    emit decoderChanged();
    if (m_ctx.activity)
        m_ctx.activity(QStringLiteral("CW"), tr("CW decoder: neural model %1").arg(QFileInfo(path).fileName()),
                       QStringLiteral("info"));
    return true;
}

void RigController::clearDecoderModel()
{
    m_decoder.setNeuralModel(QString());
    m_decoderModel.clear();
    QSettings().remove(QStringLiteral("cw/neuralModel"));
    m_decoder.reset();
    publishScope(true);
    emit decoderChanged();
    if (m_ctx.activity)
        m_ctx.activity(QStringLiteral("CW"), tr("CW decoder: back to ggmorse"), QStringLiteral("info"));
}

void RigController::setDecoderSpeedLock(int wpm)
{
    // Zero vuol dire Auto. Il decodificatore sotto non cerca oltre 59 WPM:
    // rimanere nel suo intervallo evita una scelta apparentemente valida ma
    // che non produrrebbe mai un fotogramma.
    const int clean = wpm <= 0 ? 0 : std::clamp(wpm, 5, 59);
    if (clean == m_decoderSpeedLock)
        return;
    m_decoderSpeedLock = clean;
    QSettings().setValue(QStringLiteral("cw/decoderSpeedLock"), clean);
    m_decoder.setSpeed(clean);
    // Come per il tono, la finestra contiene misure con il ritmo precedente:
    // ricominciare impedisce di unire le sue lettere alle nuove.
    m_decoder.reset();
    publishScope(true);
    emit decoderChanged();
    if (m_ctx.activity) {
        const QString what = clean > 0 ? tr("CW decoder speed locked at %1 WPM").arg(clean)
                                       : tr("CW decoder speed set to automatic search");
        m_ctx.activity(QStringLiteral("CW"), what, QStringLiteral("info"));
    }
}

void RigController::clearDecoder()
{
    m_decoderText.clear();
    m_decoder.reset();
    m_skimmer.reset();
    emit decoderChanged();
    publishScope(true);
    publishSkimmer(true);
}

void RigController::startAudio()
{
    // Il formato puo' cambiare quando si passa da una scheda all'altra. Non
    // lasciare per un istante nel pannello quello della periferica precedente.
    m_audioBuffer.clear();
    m_audioFormat = QAudioFormat();
    m_audioInputFormat.clear();

    // La scheda scelta, e solo quella: se non si trova non si ripiega su
    // un'altra (il predefinito di sistema e' una scelta, non un ripiego), si
    // dice quale manca e il decoder non parte.
    const QList<QAudioDevice> devices = app::audiodev::inputDevices();
    const auto resolution = app::audiodev::resolve(app::audiodev::entriesOf(devices), {m_audioInputId, m_audioInput});
    QAudioDevice chosen;
    switch (resolution.kind) {
    case app::audiodev::Resolution::SystemDefault:
        chosen = QMediaDevices::defaultAudioInput();
        break;
    case app::audiodev::Resolution::Found:
    case app::audiodev::Resolution::Ambiguous:
        chosen = devices.at(resolution.index);
        break;
    case app::audiodev::Resolution::Missing:
        if (m_ctx.activity)
            m_ctx.activity(QStringLiteral("CW"),
                           tr("The audio input \"%1\" is not available: choose another one in the CW panel "
                              "(the decoder does not fall back to a different card).")
                               .arg(m_audioInput),
                           QStringLiteral("warning"));
        m_decoderOn = false;
        m_audioInUse.clear();
        m_audioInputFormat.clear();
        emit audioSelectionChanged();
        return;
    }
    if (resolution.kind == app::audiodev::Resolution::Ambiguous && m_ctx.activity) {
        m_ctx.activity(QStringLiteral("CW"),
                       tr("Two audio inputs are called \"%1\": using the first one. Choose it again in the CW panel "
                          "to say which.")
                           .arg(m_audioInput),
                       QStringLiteral("warning"));
    }
    // Ritrovata per nome, o rinominata da Windows: si salva com'e' adesso.
    if (!chosen.isNull() && resolution.kind != app::audiodev::Resolution::SystemDefault
        && (QString::fromUtf8(chosen.id()) != m_audioInputId || chosen.description() != m_audioInput)
        && resolution.kind == app::audiodev::Resolution::Found) {
        m_audioInputId = QString::fromUtf8(chosen.id());
        m_audioInput = chosen.description();
        QSettings s;
        s.setValue(QStringLiteral("cw/audioInputId"), m_audioInputId);
        s.setValue(QStringLiteral("cw/audioInput"), m_audioInput);
    }
    if (chosen.isNull()) {
        if (m_ctx.activity)
            m_ctx.activity(QStringLiteral("CW"), tr("No audio input to listen to"), QStringLiteral("warning"));
        m_decoderOn = false;
        m_audioInUse.clear();
        m_audioInputFormat.clear();
        emit audioSelectionChanged();
        return;
    }

    // Acquisire nel formato nativo della periferica e normalizzarlo sotto. Su
    // alcuni backend (in particolare CoreAudio con certe USB Audio CODEC) la
    // conversione richiesta a 8 kHz viene dichiarata supportata ma consegna
    // campioni a un ritmo errato: il CW risulta accelerato e non decodifica.
    // Il convertitore gestisce mono/stereo e Int16/Int32/float, mentre
    // ggmorse ricampiona internamente alla propria frequenza di lavoro.
    QAudioFormat format = chosen.preferredFormat();

    m_audio = std::make_unique<QAudioSource>(chosen, format);
    m_audioDevice = m_audio->start();
    if (!m_audioDevice) {
        m_audio.reset();
        m_decoderOn = false;
        m_audioInUse.clear();
        m_audioInputFormat.clear();
        emit audioSelectionChanged();
        if (m_ctx.activity)
            m_ctx.activity(QStringLiteral("CW"), tr("The audio input did not open"), QStringLiteral("warning"));
        return;
    }
    // QAudioSource puo' negoziare un formato diverso da quello richiesto: e'
    // questo, non il formato desiderato, che arriva da readyRead().
    m_audioFormat = m_audio->format();
    if (!app::audiodev::canConvertToMonoInt16(m_audioFormat)) {
        const QString description = app::audiodev::formatDescription(m_audioFormat);
        m_audio->stop();
        m_audio.reset();
        m_audioDevice = nullptr;
        m_audioFormat = QAudioFormat();
        m_decoderOn = false;
        m_audioInUse.clear();
        m_audioInputFormat.clear();
        emit audioSelectionChanged();
        if (m_ctx.activity)
            m_ctx.activity(QStringLiteral("CW"),
                           tr("The audio input format %1 cannot be converted to mono 16-bit audio")
                               .arg(description),
                           QStringLiteral("warning"));
        return;
    }
    m_decoder.setSampleRate(m_audioFormat.sampleRate());
    m_decoder.reset();
    m_skimmer.setSampleRate(m_audioFormat.sampleRate());
    connect(m_audioDevice, &QIODevice::readyRead, this, [this] {
        consumeAudio(m_audioDevice->readAll());
    });
    m_audioInUse = chosen.description();
    m_audioInputFormat = app::audiodev::formatDescription(m_audioFormat);
    emit audioSelectionChanged();
    if (m_ctx.activity)
        m_ctx.activity(QStringLiteral("CW"), tr("CW decoder listening to %1").arg(chosen.description()),
                       QStringLiteral("info"));
}

void RigController::consumeAudio(const QByteArray& chunk)
{
    if (chunk.isEmpty())
        return;
    m_audioBuffer += chunk;
    const app::audiodev::MonoPcm pcm = app::audiodev::convertToMonoInt16(m_audioBuffer, m_audioFormat);
    if (pcm.samples.isEmpty())
        return;
    const int samples = static_cast<int>(pcm.samples.size() / sizeof(qint16));
    const QString text = m_decoder.feed(reinterpret_cast<const qint16*>(pcm.samples.constData()), samples);
    if (m_skimmerOn) {
        m_skimmer.feed(reinterpret_cast<const qint16*>(pcm.samples.constData()), samples);
        publishSkimmer();
    }
    // Una parola corretta toglie le lettere con cui era stata mostrata, e mette quelle giuste.
    const int retract = m_decoder.takeRetract();
    if (retract > 0)
        m_decoderText.chop(retract);
    m_audioBuffer.remove(0, pcm.consumedBytes);
    if (!text.isEmpty() || retract > 0) {
        m_decoderText += text;
        // Non si tiene una giornata di CW in memoria: gli ultimi 4000
        // caratteri bastano e avanzano.
        if (m_decoderText.size() > 4000)
            m_decoderText = m_decoderText.right(3000);
    }
    emit decoderChanged();
    publishScope();
}

void RigController::publishScope(bool force)
{
    // Il disegno non ha bisogno di tutti i fotogrammi: a 15 al secondo scorre
    // gia' liscio, e l'interfaccia non si carica per niente.
    if (!force && m_scopeClock.isValid() && m_scopeClock.elapsed() < 66)
        return;
    m_scopeClock.restart();

    const core::CwDecoder::Scope& scope = m_decoder.scope();
    // Una colonna per punto del grafico basta e avanza: si tiene il massimo di
    // ogni gruppo, cosi' anche il punto piu' corto resta visibile.
    constexpr int kPoints = 300;
    QVariantList signal;
    const qsizetype n = scope.signal.size();
    if (n > 0) {
        const int points = static_cast<int>(std::min<qsizetype>(n, kPoints));
        signal.reserve(points);
        for (int i = 0; i < points; ++i) {
            const qsizetype from = n * i / points;
            const qsizetype to = std::max(from + 1, n * (i + 1) / points);
            float peak = 0;
            for (qsizetype j = from; j < to; ++j)
                peak = std::max(peak, scope.signal.at(j));
            signal.append(peak);
        }
    }
    // I segni, come {at, length, dah}: in frazione della finestra, per disegnarli sul grafico.
    QVariantList elements;
    elements.reserve(scope.elements.size());
    for (const core::CwDecoder::Scope::Element& e : scope.elements)
        elements.append(QVariantMap{{QStringLiteral("at"), e.at}, {QStringLiteral("length"), e.length},
                                    {QStringLiteral("dah"), e.dah}});
    m_scope = QVariantMap{
        {QStringLiteral("signal"), signal},
        {QStringLiteral("level"), scope.level},
        {QStringLiteral("pitch"), scope.pitch},
        {QStringLiteral("wpm"), scope.speed},
        {QStringLiteral("cost"), scope.cost},
        {QStringLiteral("reading"), scope.reading},
        {QStringLiteral("confidence"), scope.confidence},
        {QStringLiteral("fist"), scope.fist},
        {QStringLiteral("elements"), elements},
        {QStringLiteral("neural"), scope.neural},
    };
    emit decoderScopeChanged();
}

void RigController::playTestAudio(const QByteArray& pcm, int sampleRate)
{
    stopAudio();
    sampleRate = sampleRate > 0 ? sampleRate : 8000;
    m_decoder.setSampleRate(sampleRate);
    m_decoder.reset();
    m_skimmer.setSampleRate(sampleRate);
    // L'audio di prova e' mono a 16 bit: senza dirlo, la conversione non sa cosa fare dei byte.
    QAudioFormat format;
    format.setSampleRate(sampleRate);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    m_audioFormat = format;
    m_decoderOn = true;
    emit decoderChanged();

    // Venti millisecondi alla volta, al passo del tempo vero.
    auto offset = std::make_shared<qsizetype>(0);
    const qsizetype step = static_cast<qsizetype>(sampleRate / 50) * static_cast<qsizetype>(sizeof(qint16));
    m_testAudio = std::make_unique<QTimer>();
    m_testAudio->setInterval(20);
    connect(m_testAudio.get(), &QTimer::timeout, this, [this, pcm, offset, step] {
        if (*offset >= pcm.size()) {
            m_testAudio->stop();
            return;
        }
        consumeAudio(pcm.mid(*offset, step));
        *offset += step;
    });
    m_testAudio->start();
}

void RigController::stopAudio()
{
    // L'ultima lettera sta ancora nel decodificatore: la finestra di analisi e'
    // lunga tre secondi, e spegnendo si chiuderebbe con una lettera in meno.
    if (m_audio || m_testAudio) {
        const QString last = m_decoder.flush();
        const int retract = m_decoder.takeRetract();
        if (retract > 0)
            m_decoderText.chop(retract);
        if (!last.isEmpty())
            m_decoderText += last;
    }
    if (m_audio)
        m_audio->stop();
    m_audio.reset();
    m_testAudio.reset();
    m_audioDevice = nullptr;
    m_audioBuffer.clear();
    m_audioFormat = QAudioFormat();
    m_scope.clear();
    m_skimmer.reset();
    m_stations.clear();
    m_spectrum.clear();
    emit skimmerChanged();
    if (!m_audioInUse.isEmpty()) {
        m_audioInUse.clear();
    }
    m_audioInputFormat.clear();
    emit audioSelectionChanged();
    emit decoderScopeChanged();
}


void RigController::disconnectNow()
{
    m_rig->disconnectFromRig();
    emit stateChanged();
}

void RigController::tuneTo(qint64 hz, const QString& mode)
{
    if (hz > 0)
        m_rig->setFrequency(hz);
    if (!mode.isEmpty())
        m_rig->setMode(mode);
}

QString RigController::expand(const QString& text, const QVariantMap& context) const
{
    QString out = text;
    const QString mine = m_ctx.stationCallsign ? m_ctx.stationCallsign() : QString();
    auto put = [&out](const QString& token, const QString& value) {
        out.replace(QStringLiteral("{") + token + QStringLiteral("}"), value, Qt::CaseInsensitive);
    };
    put(QStringLiteral("MYCALL"), mine.toUpper());
    put(QStringLiteral("CALL"), context.value(QStringLiteral("call")).toString().toUpper());
    put(QStringLiteral("RST"), context.value(QStringLiteral("rst"), QStringLiteral("599")).toString());
    put(QStringLiteral("NR"), context.value(QStringLiteral("nr")).toString());
    put(QStringLiteral("EXCH"), context.value(QStringLiteral("exch")).toString());
    put(QStringLiteral("NAME"), context.value(QStringLiteral("name")).toString());
    // Quello che resta senza risposta se ne va: in aria non si manda una
    // parentesi graffa.
    static const QRegularExpression leftovers(QStringLiteral("\\{[A-Za-z#]+\\}"));
    out.remove(leftovers);
    // In CW le minuscole non esistono, e alcuni manipolatori (le Yaesu via
    // CAT) rifiutano il messaggio intero per una sola.
    return out.toUpper().simplified();
}

void RigController::sendMacro(int index, const QVariantMap& context)
{
    if (index < 0 || index >= m_macros.size())
        return;
    const QString ready = expand(m_macros.at(index).toMap().value(QStringLiteral("text")).toString(), context);
    if (ready.isEmpty())
        return;
    setActiveMacroIndex(index);
    sendExpandedText(ready);
}

void RigController::sendText(const QString& text, const QVariantMap& context)
{
    const QString ready = expand(text, context);
    if (ready.isEmpty())
        return;
    setActiveMacroIndex(-1);
    sendExpandedText(ready);
}

void RigController::setTypeToSend(bool on)
{
    if (on == m_typeToSend)
        return;
    m_typeToSend = on;
    QSettings().setValue(QStringLiteral("cw/typeToSend"), on);
    emit typeToSendChanged();
}

void RigController::sendTyped(const QString& chars)
{
    // Solo quello che il Morse sa dire: il resto si lascia nel campo e non esce. Le minuscole
    // non esistono in CW.
    static const QString allowed = QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 /?.,=+-:'@()");
    QString ready;
    for (const QChar c : chars.toUpper()) {
        if (allowed.contains(c))
            ready += c;
    }
    if (ready.isEmpty())
        return;
    setActiveMacroIndex(-1);
    sendExpandedText(ready, true);
}

void RigController::setActiveMacroIndex(int index)
{
    if (m_activeMacroIndex == index)
        return;
    m_activeMacroIndex = index;
    emit stateChanged();
}

bool RigController::directYaesuCwUsesKeyerMemory() const
{
    // Hamlib assegna i backend Yaesu all'intervallo 1000--1099. Per questi
    // modelli il comando send_morse carica il testo nella memoria 1 della
    // radio; non e' un buffer temporaneo che si possa ripristinare con
    // sicurezza dopo la trasmissione.
    return m_link == QLatin1String("serial") && m_rigModel >= 1000 && m_rigModel < 1100;
}

bool RigController::cwUsesKeyerMemory() const
{
    if (!directYaesuCwUsesKeyerMemory() || keyerOn())
        return false;

    // In SO2R, quando radio 2 ha il fuoco, il CW non viene mandato alla
    // Yaesu configurata qui ma al suo collegamento separato.
    return !(m_ctx.alternateRig && m_ctx.alternateRig());
}

void RigController::sendExpandedText(const QString& ready, bool typed)
{
    // Il manipolatore sulla seriale ha la precedenza: se c'e', e' quello che
    // l'operatore ha attaccato alla radio apposta.
    if (m_winKeyer.isOpen()) {
        m_cwRigInUse = m_ctx.alternateRig ? m_ctx.alternateRig() : nullptr;
        if (!m_cwRigInUse)
            m_cwRigInUse = m_rig;
        m_winKeyer.send(ready, wpm(), !typed);
        if (m_ctx.activity && !typed)
            m_ctx.activity(QStringLiteral("CW"), tr("Sent: %1").arg(ready), QStringLiteral("info"));
        return;
    }
    if (m_keyer.isOpen()) {
        m_cwRigInUse = m_ctx.alternateRig ? m_ctx.alternateRig() : nullptr;
        if (!m_cwRigInUse)
            m_cwRigInUse = m_rig;
        m_keyer.send(ready, wpm());
        return;
    }
    if (core::RigLink* alt = m_ctx.alternateRig ? m_ctx.alternateRig() : nullptr) {
        m_cwRigInUse = alt;
        alt->setSpeedWpm(wpm());
        alt->sendMorse(ready);
        return;
    }

    // Il percorso CAT Yaesu e' consentito: il pannello avvisa che Hamlib
    // riutilizza la memoria 1. Non tentiamo un ripristino non verificabile.
    // connectNow configura il limite e i caratteri singoli solo quando
    // il modello del rigctld locale e' noto (non per collegamenti esterni).
    m_cwRigInUse = m_rig;
    m_rig->sendMorse(ready);
}

void RigController::stop()
{
    m_keyer.stop();
    m_winKeyer.stop();
    setActiveMacroIndex(-1);
    // SO2R puo' aver inviato la macro alla radio 2. Il destinatario si salva
    // anche con un keyer locale: oltre a svuotare il suo buffer, Ferma deve
    // forzare il PTT della stessa radio verso RX.
    core::RigLink* target = m_cwRigInUse;
    m_cwRigInUse = nullptr;
    if (!target && m_ctx.alternateRig)
        target = m_ctx.alternateRig();
    if (!target)
        target = m_rig;
    if (target)
        target->emergencyStop();
    if (m_ctx.activity)
        m_ctx.activity(QStringLiteral("CW"), tr("Stop requested: keyer cleared and PTT release sent"),
                       QStringLiteral("info"));
}

void RigController::shutdown()
{
    stop();
    stopAudio();

    // Prima si fermano i client e i server TCP, cosi' non possono riattivare
    // timer o accodare comandi mentre rigctld sta terminando.
    m_share.configure(false, m_share.port(), false, false);
    for (core::RigLink* link : {static_cast<core::RigLink*>(&m_hamlib),
                                static_cast<core::RigLink*>(&m_tci),
                                static_cast<core::RigLink*>(&m_flrig),
                                static_cast<core::RigLink*>(&m_omniRig)}) {
        link->disconnectFromRig();
    }

    if (m_probeSocket)
        m_probeSocket->abort();
    m_probeSocket.reset();
    m_probe.clear();
    m_probeIndex = -1;
    ++m_launchGeneration;
    ++m_probeGeneration;
    stopChildProcess(m_probeProcess);
    stopChildProcess(m_modelsProcess);
    stopChildProcess(m_capsProcess);
    stopChildProcess(m_rigctld);

    // WinKeyer vive nel thread della UI e la sua close ha gia' un timeout
    // breve. CwKeyer invece ha un worker dedicato: il suo distruttore effettua
    // la chiusura non bloccante e poi aspetta al massimo due secondi.
    m_winKeyer.close();
}

// ── Il manipolatore sulla seriale ───────────────────────────────────────────

void RigController::openKeyer()
{
    m_keyer.close();
    m_winKeyer.close();
    if (m_keyerPort.isEmpty()) {
        emit stateChanged();
        return;
    }
    // Il WinKeyer fa da se' i tempi: gli si manda il testo e basta.
    if (m_keyerLine == QLatin1String("WINKEYER")) {
        m_winKeyer.setSpeed(m_wpm);
        if (m_winKeyer.open(m_keyerPort) && m_ctx.activity)
            m_ctx.activity(QStringLiteral("CW"), tr("WinKeyer on %1").arg(m_keyerPort), QStringLiteral("success"));
        emit stateChanged();
        return;
    }
    if (m_keyer.open(m_keyerPort, m_keyerLine) && m_ctx.activity) {
        m_ctx.activity(QStringLiteral("CW"),
                       tr("CW keyer on %1 (%2): it works with the CAT busy elsewhere")
                           .arg(m_keyerPort, m_keyerLine),
                       QStringLiteral("success"));
    }
    emit stateChanged();
}

void RigController::setKeyerPort(const QString& port)
{
    const QString clean = port.trimmed();
    if (clean == m_keyerPort)
        return;
    m_keyerPort = clean;
    QSettings().setValue(QStringLiteral("rig/keyerPort"), m_keyerPort);
    openKeyer();
    emit changed();
}

void RigController::setKeyerLine(const QString& line)
{
    const QString up = line.trimmed().toUpper();
    const QString clean = up == QLatin1String("RTS")      ? QStringLiteral("RTS")
                        : up == QLatin1String("WINKEYER") ? QStringLiteral("WINKEYER")
                                                          : QStringLiteral("DTR");
    if (clean == m_keyerLine)
        return;
    m_keyerLine = clean;
    QSettings().setValue(QStringLiteral("rig/keyerLine"), m_keyerLine);
    openKeyer();
    emit changed();
}

void RigController::testKeyer()
{
    if (m_winKeyer.isOpen()) {
        m_winKeyer.send(QStringLiteral("VVV"), wpm());
        return;
    }
    if (!m_keyer.isOpen()) {
        if (m_ctx.activity)
            m_ctx.activity(QStringLiteral("CW"), tr("No CW keyer: pick a port first"),
                           QStringLiteral("warning"));
        return;
    }
    m_keyer.send(QStringLiteral("VVV"), wpm());
}

void RigController::setMacro(int index, const QString& label, const QString& text)
{
    if (index < 0 || index >= m_macros.size())
        return;
    QVariantMap macro = m_macros.at(index).toMap();
    macro.insert(QStringLiteral("label"), label.trimmed());
    macro.insert(QStringLiteral("text"), text.trimmed());
    m_macros[index] = macro;
    saveMacros();
    emit macrosChanged();
}

void RigController::reloadMacros()
{
    loadMacros();
    emit macrosChanged();
}

void RigController::addMacro()
{
    if (m_macros.size() >= kMaxMacros)
        return;
    const int n = int(m_macros.size()) + 1;
    // Sui tasti F la scritta di partenza e' il tasto; dopo il dodicesimo, "M13".
    m_macros << QVariantMap{{QStringLiteral("label"), QStringLiteral("%1%2").arg(n <= 12 ? QStringLiteral("F") : QStringLiteral("M")).arg(n)},
                            {QStringLiteral("text"), QString()}};
    saveMacros();
    emit macrosChanged();
}

void RigController::removeMacro(int index)
{
    if (index < 0 || index >= m_macros.size() || m_macros.size() <= 1)
        return;
    m_macros.removeAt(index);
    saveMacros();
    emit macrosChanged();
}

void RigController::resetMacros()
{
    m_macros = defaultMacros();
    saveMacros();
    emit macrosChanged();
}

QVariantList RigController::defaultMacros()
{
    // Quelle di sempre, nell'ordine in cui le tiene ogni log da contest. La
    // scritta del tasto e' tutta qui, "F1" compreso: chi vuole la cambia.
    return {
        QVariantMap{{QStringLiteral("label"), QStringLiteral("F1 CQ")},
                    {QStringLiteral("text"), QStringLiteral("CQ TEST {MYCALL} {MYCALL} TEST")}},
        QVariantMap{{QStringLiteral("label"), QStringLiteral("F2 Call")},
                    {QStringLiteral("text"), QStringLiteral("{CALL}")}},
        QVariantMap{{QStringLiteral("label"), QStringLiteral("F3 Exch")},
                    {QStringLiteral("text"), QStringLiteral("{CALL} 5NN {NR}")}},
        QVariantMap{{QStringLiteral("label"), QStringLiteral("F4 TU")},
                    {QStringLiteral("text"), QStringLiteral("TU {MYCALL} TEST")}},
        QVariantMap{{QStringLiteral("label"), QStringLiteral("F5 ?")},
                    {QStringLiteral("text"), QStringLiteral("?")}},
        QVariantMap{{QStringLiteral("label"), QStringLiteral("F6 AGN")},
                    {QStringLiteral("text"), QStringLiteral("AGN")}},
        QVariantMap{{QStringLiteral("label"), QStringLiteral("F7 NR?")},
                    {QStringLiteral("text"), QStringLiteral("NR?")}},
        QVariantMap{{QStringLiteral("label"), QStringLiteral("F8 73")},
                    {QStringLiteral("text"), QStringLiteral("73 GL")}},
        // F9-F12: quelle che servono in S&P e con l'ESM.
        QVariantMap{{QStringLiteral("label"), QStringLiteral("F9 My call")},
                    {QStringLiteral("text"), QStringLiteral("{MYCALL}")}},
        QVariantMap{{QStringLiteral("label"), QStringLiteral("F10 S&P Exch")},
                    {QStringLiteral("text"), QStringLiteral("5NN {NR}")}},
        QVariantMap{{QStringLiteral("label"), QStringLiteral("F11 QRZ?")},
                    {QStringLiteral("text"), QStringLiteral("QRZ?")}},
        QVariantMap{{QStringLiteral("label"), QStringLiteral("F12 QRL?")},
                    {QStringLiteral("text"), QStringLiteral("QRL?")}},
    };
}

void RigController::loadMacros()
{
    const QString raw = QSettings().value(QStringLiteral("cw/macros")).toString();
    const QJsonArray array = QJsonDocument::fromJson(raw.toUtf8()).array();
    QVariantList out;
    for (const QJsonValue& value : array) {
        const QJsonObject object = value.toObject();
        out << QVariantMap{{QStringLiteral("label"), object.value(QStringLiteral("label")).toString()},
                           {QStringLiteral("text"), object.value(QStringLiteral("text")).toString()}};
    }
    // Chi aveva le otto di prima le tiene, e trova le quattro nuove in fondo —
    // una volta sola. Prima succedeva a ogni avvio: chi ne toglieva quattro
    // e restava con otto se le ritrovava tutte al riavvio.
    const QVariantList defaults = defaultMacros();
    QSettings grown;
    bool grewNow = false;
    if (!grown.value(QStringLiteral("cw/macrosGrownTo12"), false).toBool()) {
        if (out.size() == 8) {
            out += defaults.mid(8);
            grewNow = true;
        }
        grown.setValue(QStringLiteral("cw/macrosGrownTo12"), true);
    }
    // Quante ne ha scelte l'operatore, da una a kMaxMacros.
    if (out.size() > kMaxMacros)
        out = out.mid(0, kMaxMacros);
    m_macros = out.isEmpty() ? defaults : out;
    // Le quattro aggiunte si scrivono subito: il segno "fatto" c'e' gia'.
    if (grewNow)
        saveMacros();

    // Fino alla 1.16.36 il tasto mostrava "F1" e poi la scritta, e "F1" non si
    // cambiava. Adesso la scritta e' tutta della macro: a quelle salvate prima
    // si mette davanti il tasto una volta, cosi' i tasti restano come erano.
    QSettings s;
    if (!s.value(QStringLiteral("cw/macroLabelsWithKey"), false).toBool()) {
        if (!array.isEmpty()) {
            for (int i = 0; i < m_macros.size(); ++i) {
                QVariantMap macro = m_macros.at(i).toMap();
                const QString key = QStringLiteral("F%1").arg(i + 1);
                const QString label = macro.value(QStringLiteral("label")).toString().trimmed();
                if (!label.startsWith(key + QLatin1Char(' ')) && label != key)
                    macro.insert(QStringLiteral("label"), (key + QLatin1Char(' ') + label).trimmed());
                m_macros[i] = macro;
            }
            saveMacros();
        }
        s.setValue(QStringLiteral("cw/macroLabelsWithKey"), true);
    }
}

void RigController::saveMacros()
{
    QJsonArray array;
    for (const QVariant& value : std::as_const(m_macros)) {
        const QVariantMap macro = value.toMap();
        array.append(QJsonObject{{QStringLiteral("label"), macro.value(QStringLiteral("label")).toString()},
                                 {QStringLiteral("text"), macro.value(QStringLiteral("text")).toString()}});
    }
    // Scritte subito sul disco: se il programma si chiude male, la macro
    // cambiata non deve tornare quella di prima.
    QSettings s;
    s.setValue(QStringLiteral("cw/macros"), QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
    s.sync();
}

} // namespace decolog::app
