// DecoDXLog — la radio e le macro in CW.
//
// Il collegamento lo fa Hamlib — DecoDXLog parla a rigctld, che sta gia' sul
// computer di chi opera — oppure TCI, il WebSocket delle SDR, come in
// Decodium. Qui sopra ci sono le macro del contest — otto tasti
// con dentro il testo che si manda, con i buchi da riempire ({CALL}, {NR},
// {MYCALL}) — e la velocita' del manipolatore.
#pragma once

#include "core/CwDecoder.h"
#include "core/CwSkimmer.h"
#include "core/CwKeyer.h"
#include "core/WinKeyer.h"
#include "core/RigControl.h"
#include "core/TciControl.h"
#include "core/FlrigControl.h"
#include "core/OmniRigControl.h"
#include "core/CatShare.h"

#include <QAudioSource>
#include <QMediaDevices>
#include <QElapsedTimer>
#include <QTimer>
#include <QProcess>
#include <memory>

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

namespace decolog::app {

class RigController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(QString host READ host WRITE setHost NOTIFY changed)
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY changed)
    Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(qint64 frequencyHz READ frequencyHz NOTIFY stateChanged)
    Q_PROPERTY(QString frequencyLabel READ frequencyLabel NOTIFY stateChanged)
    Q_PROPERTY(QString mode READ mode NOTIFY stateChanged)
    // Split, VFO, RIT e XIT: features dice cosa sa fare il collegamento
    // (1 split, 2 scelta del VFO, 4 RIT, 8 XIT).
    Q_PROPERTY(int features READ features NOTIFY stateChanged)
    Q_PROPERTY(bool split READ split NOTIFY stateChanged)
    Q_PROPERTY(qint64 txFrequencyHz READ txFrequencyHz NOTIFY stateChanged)
    Q_PROPERTY(QString txFrequencyLabel READ txFrequencyLabel NOTIFY stateChanged)
    Q_PROPERTY(QString vfo READ vfo NOTIFY stateChanged)
    Q_PROPERTY(int ritHz READ ritHz NOTIFY stateChanged)
    Q_PROPERTY(int xitHz READ xitHz NOTIFY stateChanged)
    Q_PROPERTY(int wpm READ wpm WRITE setWpm NOTIFY stateChanged)
    // Questo collegamento il CW non lo sa mandare (per esempio il ponte CAT
    // di Decodium): le macro restano spente e si dice perche'.
    Q_PROPERTY(bool canKeyCw READ canKeyCw NOTIFY stateChanged)
    // Alcune Yaesu, quando Hamlib le pilota direttamente sulla seriale,
    // usano la memoria 1 del keyer come buffer del testo CW. La UI avvisa
    // della sovrascrittura, senza impedire l'invio via CAT.
    Q_PROPERTY(bool cwUsesKeyerMemory READ cwUsesKeyerMemory NOTIFY stateChanged)
    // Il manipolatore su porta seriale: alza e abbassa DTR o RTS di una porta
    // tutta sua, e non gli importa di chi tiene il CAT. E' la via per chi opera
    // con Decodium aperto: il CAT resta li' dov'e', il CW passa di qua.
    Q_PROPERTY(QString keyerPort READ keyerPort WRITE setKeyerPort NOTIFY changed)
    Q_PROPERTY(QString keyerLine READ keyerLine WRITE setKeyerLine NOTIFY changed)
    Q_PROPERTY(bool keyerOn READ keyerOn NOTIFY stateChanged)
    // L'ultima macro mandata: serve alla UI per illuminare il tasto giusto,
    // non sempre F1. -1 significa che non c'e' una macro in corso.
    Q_PROPERTY(int activeMacroIndex READ activeMacroIndex NOTIFY stateChanged)
    // Le otto macro, come {label, text}.
    Q_PROPERTY(QVariantList macros READ macros NOTIFY macrosChanged)
    // Il decoder CW: ascolta l'audio che esce dalla radio e scrive quello che
    // sente. Non serve che la radio sappia decodificare.
    Q_PROPERTY(bool decoderOn READ decoderOn WRITE setDecoderOn NOTIFY decoderChanged)
    Q_PROPERTY(QString decoderText READ decoderText NOTIFY decoderChanged)
    Q_PROPERTY(int decoderWpm READ decoderWpm NOTIFY decoderChanged)
    Q_PROPERTY(int decoderTone READ decoderTone NOTIFY decoderChanged)
    // 0 = cerca il tono da solo; altrimenti ascolta solo quella frequenza.
    // Serve quando una banda affollata fa inseguire al decoder una portante
    // vicina invece della stazione che l'operatore ha sintonizzato.
    Q_PROPERTY(int decoderToneLock READ decoderToneLock WRITE setDecoderToneLock NOTIFY decoderChanged)
    // 0 = misura da solo la velocita'; altrimenti conserva il ritmo indicato.
    // E' indipendente dai WPM con cui si trasmette: il corrispondente puo'
    // andare a una velocita' completamente diversa.
    Q_PROPERTY(int decoderSpeedLock READ decoderSpeedLock WRITE setDecoderSpeedLock NOTIFY decoderChanged)
    // Il decodificatore neurale (CTC): il modello scelto, se e' acceso, e se questa build lo sa fare.
    // La correzione delle parole (Q-code, abbreviazioni, parole comuni): attiva di serie.
    Q_PROPERTY(bool decoderLexicon READ decoderLexicon WRITE setDecoderLexicon NOTIFY decoderChanged)
    Q_PROPERTY(QString decoderModel READ decoderModel NOTIFY decoderChanged)
    Q_PROPERTY(bool decoderNeural READ decoderNeural NOTIFY decoderChanged)
    Q_PROPERTY(bool neuralDecoderAvailable READ neuralDecoderAvailable CONSTANT)
    // Il grafico del decoder, come quello di ggmorse: {signal: [0..1],
    // level, pitch, wpm, cost, reading}. Si aggiorna una quindicina di volte
    // al secondo, non a ogni pezzetto di audio.
    Q_PROPERTY(QVariantMap decoderScope READ decoderScope NOTIFY decoderScopeChanged)
    // Le stazioni CW in banda, tutte insieme (lo «skimmer»): ognuna con tono, velocita', nominativo
    // e ultimo testo, piu' lo spettro della banda per sceglierle. Attivo di serie quando il
    // decoder ascolta.
    Q_PROPERTY(bool skimmerOn READ skimmerOn WRITE setSkimmerOn NOTIFY skimmerChanged)
    Q_PROPERTY(QVariantList skimmerStations READ skimmerStations NOTIFY skimmerChanged)
    Q_PROPERTY(QVariantList skimmerSpectrum READ skimmerSpectrum NOTIFY skimmerChanged)
    Q_PROPERTY(int skimmerLowHz READ skimmerLowHz CONSTANT)
    Q_PROPERTY(int skimmerHighHz READ skimmerHighHz CONSTANT)
    Q_PROPERTY(QStringList audioInputs READ audioInputs NOTIFY audioDevicesChanged)
    Q_PROPERTY(QString audioInput READ audioInput WRITE setAudioInput NOTIFY audioSelectionChanged)
    // L'ingresso del decoder, scelto in modo preciso: l'elenco con gli
    // identificativi, la riga scelta (0 predefinito, -1 la scelta non si
    // trova), e quale scheda sta ascoltando adesso.
    Q_PROPERTY(QVariantList audioInputDevices READ audioInputDevices NOTIFY audioDevicesChanged)
    Q_PROPERTY(int audioInputIndex READ audioInputIndex NOTIFY audioSelectionChanged)
    Q_PROPERTY(QString audioInputInUse READ audioInputInUse NOTIFY audioSelectionChanged)
    // Il formato reale che il backend audio ha aperto; puo' essere diverso da
    // 8 kHz mono Int16 e viene normalizzato prima del decoder.
    Q_PROPERTY(QString audioInputFormat READ audioInputFormat NOTIFY audioSelectionChanged)
    // Come si arriva alla radio: "network" (un rigctld gia' acceso),
    // "serial" (la porta della radio, e rigctld lo avvia DecoDXLog) oppure
    // "tci" (il WebSocket TCI delle SDR, come in Decodium).
    Q_PROPERTY(QString link READ link WRITE setLink NOTIFY changed)
    // TCI: dove sta il server ("127.0.0.1:40001") e quale ricevitore (0, 1).
    Q_PROPERTY(QString tciAddress READ tciAddress WRITE setTciAddress NOTIFY changed)
    // "flrig": XML-RPC di flrig; "omnirig": il server COM OmniRig (Windows).
    Q_PROPERTY(QString flrigAddress READ flrigAddress WRITE setFlrigAddress NOTIFY changed)
    Q_PROPERTY(int omniRigNumber READ omniRigNumber WRITE setOmniRigNumber NOTIFY changed)
    // La CAT condivisa, come in Decodium 4: la radio che tiene DecoDXLog si
    // rivende su 127.0.0.1 con il protocollo di rigctld ("Hamlib NET rigctl").
    Q_PROPERTY(bool shareEnabled READ shareEnabled NOTIFY shareChanged)
    Q_PROPERTY(int sharePort READ sharePort NOTIFY shareChanged)
    Q_PROPERTY(bool shareControl READ shareControl NOTIFY shareChanged)
    Q_PROPERTY(bool sharePtt READ sharePtt NOTIFY shareChanged)
    Q_PROPERTY(bool shareListening READ shareListening NOTIFY shareChanged)
    Q_PROPERTY(int shareClients READ shareClients NOTIFY shareChanged)
    Q_PROPERTY(QString shareError READ shareError NOTIFY shareChanged)
    Q_PROPERTY(bool omniRigAvailable READ omniRigAvailable CONSTANT)
    Q_PROPERTY(int tciTrx READ tciTrx WRITE setTciTrx NOTIFY changed)
    Q_PROPERTY(QString tciDevice READ tciDevice NOTIFY stateChanged)
    Q_PROPERTY(QString serialPort READ serialPort WRITE setSerialPort NOTIFY changed)
    // Le porte che DecoDXLog non apre mai perche' sono di un altro programma
    // (di serie quella su cui Decodium tiene il CAT).
    Q_PROPERTY(QString reservedPorts READ reservedPorts WRITE setReservedPorts NOTIFY changed)
    Q_PROPERTY(bool leaveDecodiumPort READ leaveDecodiumPort WRITE setLeaveDecodiumPort NOTIFY changed)
    Q_PROPERTY(int rigModel READ rigModel WRITE setRigModel NOTIFY changed)
    Q_PROPERTY(int baud READ baud WRITE setBaud NOTIFY changed)
    // I parametri della seriale non sono universali: serialCapabilities viene
    // letto da `rigctld -L` per il modello Hamlib scelto e la UI mostra solo
    // quelli che quel backend dichiara di saper configurare.
    Q_PROPERTY(QStringList serialCapabilities READ serialCapabilities NOTIFY serialCapabilitiesChanged)
    // I modelli di radio che conosce Hamlib: arrivano da "rigctld -l" su un processo a parte, e
    // quando sono pronti la lista cambia. Prima si aspettava la risposta nel filo della
    // finestra, e un rigctld lento (antivirus, disco freddo) la teneva ferma per secondi.
    Q_PROPERTY(QVariantList rigModelList READ rigModelList NOTIFY rigModelsChanged)
    Q_PROPERTY(QString dataBits READ dataBits WRITE setDataBits NOTIFY changed)
    Q_PROPERTY(QString stopBits READ stopBits WRITE setStopBits NOTIFY changed)
    Q_PROPERTY(QString parity READ parity WRITE setParity NOTIFY changed)
    Q_PROPERTY(QString handshake READ handshake WRITE setHandshake NOTIFY changed)
    Q_PROPERTY(QString dtrState READ dtrState WRITE setDtrState NOTIFY changed)
    Q_PROPERTY(QString rtsState READ rtsState WRITE setRtsState NOTIFY changed)
    Q_PROPERTY(QString civAddress READ civAddress WRITE setCivAddress NOTIFY changed)
    // Il PTT: tante stazioni hanno due porte, una per il CAT e una per il PTT.
    // "RIG" = lo fa il CAT stesso, "RTS"/"DTR" = il piedino della seconda porta.
    Q_PROPERTY(QString pttType READ pttType WRITE setPttType NOTIFY changed)
    Q_PROPERTY(QString pttPort READ pttPort WRITE setPttPort NOTIFY changed)

public:
    struct Context {
        std::function<void(const QString& category, const QString& text, const QString& level)> activity;
        // Il nominativo con cui si sta operando, per {MYCALL}.
        std::function<QString()> stationCallsign;
        // SO2R: la radio 2, quando ha lei il fuoco. Il CW va li' se non c'e'
        // un manipolatore seriale (che la scatola SO2R gira da sola).
        std::function<core::RigLink*()> alternateRig;
        // Lo stato di un nominativo sentito in aria rispetto al log (nuova entita', nuova banda,
        // gia' lavorato…): {status, label, entity, azimuth, distanceKm}. Vuoto: non si dice.
        std::function<QVariantMap(const QString& call, const QString& band, const QString& mode)> classifyCall;
    };

    explicit RigController(Context context, QObject* parent = nullptr);

    void start();
    // Per le prove da riga di comando: si collega a quel rigctld solo per
    // questa volta, senza scrivere niente nelle impostazioni dell'operatore.
    void overrideConnection(const QString& host, int port);
    // Lo stesso, per un server TCI ("host:porta", ricevitore).
    void overrideTci(const QString& address, int trx);

    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);
    QString host() const { return m_host; }
    void setHost(const QString& host);
    int port() const { return m_port; }
    void setPort(int port);
    bool connected() const { return m_rig->connected(); }
    QString status() const { return m_rig->status(); }
    qint64 frequencyHz() const { return m_rig->frequencyHz(); }
    QString frequencyLabel() const;
    int features() const { return connected() ? m_rig->features() : 0; }
    bool split() const { return m_rig->split(); }
    qint64 txFrequencyHz() const { return m_rig->txFrequencyHz(); }
    QString txFrequencyLabel() const;
    QString vfo() const { return m_rig->vfo(); }
    int ritHz() const { return m_rig->ritHz(); }
    int xitHz() const { return m_rig->xitHz(); }
    Q_INVOKABLE void setSplit(bool on, qint64 txHz = 0) { m_rig->setSplit(on, txHz); }
    // Split acceso, trasmissione `hz` sopra (o sotto) la ricezione: "UP 1".
    Q_INVOKABLE void splitUp(int hz);
    // Il VFO dall'altra parte: A → B, B → A.
    Q_INVOKABLE void swapVfo();
    Q_INVOKABLE void setRit(int hz) { m_rig->setRit(hz); }
    Q_INVOKABLE void setXit(int hz) { m_rig->setXit(hz); }
    QString mode() const { return m_rig->mode(); }
    int wpm() const { return m_rig->speedWpm() > 0 ? m_rig->speedWpm() : m_wpm; }
    // Il CW parte se sa manipolarlo il CAT oppure se c'e' il manipolatore
    // sulla seriale: basta uno dei due.
    bool canKeyCw() const { return m_canKeyCw || keyerOn(); }
    bool cwUsesKeyerMemory() const;
    QString keyerPort() const { return m_keyerPort; }
    void setKeyerPort(const QString& port);
    QString keyerLine() const { return m_keyerLine; }
    void setKeyerLine(const QString& line);
    bool keyerOn() const { return m_keyer.isOpen() || m_winKeyer.isOpen(); }
    int activeMacroIndex() const { return m_activeMacroIndex; }
    // Il WinKeyer, se e' quello scelto: la versione del firmware (0 = non risponde).
    Q_PROPERTY(int winKeyerVersion READ winKeyerVersion NOTIFY stateChanged)
    int winKeyerVersion() const { return m_winKeyer.version(); }
    // Manda "VVV" per sentire se la radio va in aria davvero.
    Q_INVOKABLE void testKeyer();
    // Il PTT, per il keyer vocale.
    Q_INVOKABLE void ptt(bool on) { m_rig->setPtt(on); }
    void setWpm(int wpm);
    QVariantList macros() const { return m_macros; }

    // Le macro: testo con i buchi, e i buchi riempiti da quello che sta
    // succedendo adesso ({CALL}, {RST}, {NR}, {EXCH}, {NAME}, {MYCALL}).
    Q_INVOKABLE QString expand(const QString& text, const QVariantMap& context) const;
    Q_INVOKABLE void sendMacro(int index, const QVariantMap& context);
    Q_INVOKABLE void sendText(const QString& text, const QVariantMap& context);
    // «Invia mentre scrivi»: con il flag acceso ogni lettera battuta nel campo libero esce subito in
    // CW. Qui arrivano i caratteri appena scritti, senza espandere le {macro} e senza chiudere la
    // parola: le lettere si accodano al manipolatore una dopo l'altra.
    Q_PROPERTY(bool typeToSend READ typeToSend WRITE setTypeToSend NOTIFY typeToSendChanged)
    bool typeToSend() const { return m_typeToSend; }
    void setTypeToSend(bool on);
    Q_INVOKABLE void sendTyped(const QString& chars);
    Q_INVOKABLE void stop();
    // Arresto definitivo dell'applicazione: oltre al CW chiude tutte le
    // connessioni CAT e i processi rigctld che abbiamo avviato noi.  Non e'
    // invocabile dalla UI: il tasto Stop deve interrompere solo la macro CW.
    void shutdown();
    Q_INVOKABLE void setMacro(int index, const QString& label, const QString& text);
    Q_INVOKABLE void resetMacros();
    // Le macro non sono piu' dodici fisse: se ne aggiunge una in fondo o se ne
    // toglie una (ne resta sempre almeno una). Le prime dodici vanno sui tasti
    // F1-F12, le altre si mandano col clic.
    static constexpr int kMaxMacros = 24;
    Q_INVOKABLE void addMacro();
    Q_INVOKABLE void removeMacro(int index);
    // Le macro rilette dalle impostazioni: quando le ha cambiate il Cloud.
    void reloadMacros();
    Q_INVOKABLE void connectNow();
    // Sulla seriale: ferma il rigctld di adesso e ne avvia uno con le impostazioni di ora.
    Q_INVOKABLE void reconnectSerial();
    // Cerca la radio da sola: prova le porte e le velocita' una per una,
    // finche' una risponde. Quella che risponde si tiene.
    Q_INVOKABLE void probeRadio();
    Q_PROPERTY(bool probing READ probing NOTIFY stateChanged)
    bool probing() const { return m_probeIndex >= 0; }
    // Le impostazioni del CAT che usa Decodium su questo computer, per chi
    // vuole le stesse: {port, baud, driver}.
    Q_INVOKABLE QVariantMap decodiumCat() const;
    Q_INVOKABLE void disconnectNow();
    // Porta la radio dove dice lo spot: frequenza in Hz e, se c'e', il modo.
    Q_INVOKABLE void tuneTo(qint64 hz, const QString& mode = QString());

    bool decoderOn() const { return m_decoderOn; }
    void setDecoderOn(bool on);
    QString decoderText() const { return m_decoderText; }
    int decoderWpm() const { return m_decoder.wpm(); }
    int decoderTone() const { return static_cast<int>(m_decoder.toneHz()); }
    int decoderToneLock() const { return m_decoderToneLock; }
    void setDecoderToneLock(int hz);
    int decoderSpeedLock() const { return m_decoderSpeedLock; }
    void setDecoderSpeedLock(int wpm);
    bool decoderLexicon() const { return m_decoder.lexicon(); }
    void setDecoderLexicon(bool on);
    QString decoderModel() const { return m_decoderModel; }
    bool decoderNeural() const { return m_decoder.neural(); }
    bool neuralDecoderAvailable() const { return core::CwDecoder::neuralAvailable(); }
    // Sceglie il modello ONNX del decodificatore neurale (vuoto: si torna a ggmorse). Torna
    // false se il file non si carica o se questa build non ha ONNX Runtime.
    Q_INVOKABLE bool chooseDecoderModel(const QUrl& file);
    Q_INVOKABLE void clearDecoderModel();
    QVariantMap decoderScope() const { return m_scope; }
    bool skimmerOn() const { return m_skimmerOn; }
    void setSkimmerOn(bool on);
    QVariantList skimmerStations() const { return m_stations; }
    QVariantList skimmerSpectrum() const { return m_spectrum; }
    int skimmerLowHz() const { return m_skimmer.lowHz(); }
    int skimmerHighHz() const { return m_skimmer.highHz(); }
    // Il testo decodificato come HTML, con i nominativi cliccabili ("call:K1ABC") nel colore dato.
    Q_INVOKABLE QString decoderHtml(const QString& linkColor) const;
    // La frequenza della stazione al tono dato, in kHz con un decimale, dalla frequenza della
    // radio e dal modo: serve a segnalare quella stazione.
    Q_INVOKABLE QString stationKhz(double toneHz) const;
    // Ascolta quella stazione: il decoder principale si ferma sul suo tono.
    Q_INVOKABLE void listenTo(double toneHz);
    // Per le prove: fa ascoltare al decoder un file audio (WAV o PCM a 16 bit,
    // mono) al passo del tempo vero, come se arrivasse dalla scheda audio.
    void playTestAudio(const QByteArray& pcm, int sampleRate);
    QStringList audioInputs() const;
    QString audioInput() const { return m_audioInput; }
    void setAudioInput(const QString& name);
    QVariantList audioInputDevices() const;
    int audioInputIndex() const;
    QString audioInputInUse() const { return m_audioInUse; }
    QString audioInputFormat() const { return m_audioInputFormat; }
    // Dalla riga del menu a tendina (0 = predefinito del sistema): la scelta si
    // salva con l'identificativo della scheda e vale finche' non se ne sceglie
    // un'altra.
    Q_INVOKABLE void chooseAudioInput(int row);
    Q_INVOKABLE void clearDecoder();

    QString link() const { return m_link; }
    void setLink(const QString& link);
    QString tciAddress() const { return m_tciAddress; }
    void setTciAddress(const QString& address);
    int tciTrx() const { return m_tciTrx; }
    void setTciTrx(int trx);
    QString tciDevice() const { return m_tci.device(); }
    QString flrigAddress() const { return m_flrigAddress; }
    void setFlrigAddress(const QString& address);
    int omniRigNumber() const { return m_omniRigNumber; }
    void setOmniRigNumber(int number);
    bool omniRigAvailable() const;
    bool shareEnabled() const { return m_share.enabled(); }
    int sharePort() const { return m_share.port(); }
    bool shareControl() const { return m_share.allowControl(); }
    bool sharePtt() const { return m_share.allowPtt(); }
    bool shareListening() const { return m_share.listening(); }
    int shareClients() const { return m_share.clientCount(); }
    QString shareError() const { return m_share.lastError(); }
    // Accende, spegne o cambia la condivisione, e la ricorda.
    Q_INVOKABLE void configureShare(bool enabled, int port, bool allowControl, bool allowPtt);
    // Usa la CAT che condivide un altro programma (Decodium): rigctld di rete
    // su host:porta, collegato subito.
    Q_INVOKABLE void useSharedCat(const QString& host, int port);
    QString serialPort() const { return m_serialPort; }
    void setSerialPort(const QString& port);
    QString reservedPorts() const;
    void setReservedPorts(const QString& ports);
    bool leaveDecodiumPort() const;
    void setLeaveDecodiumPort(bool on);
    int rigModel() const { return m_rigModel; }
    void setRigModel(int model);
    int baud() const { return m_baud; }
    void setBaud(int baud);
    QStringList serialCapabilities() const { return m_serialCapabilities; }
    Q_INVOKABLE void refreshSerialCapabilities();
    QString dataBits() const { return m_dataBits; }
    void setDataBits(const QString& value);
    QString stopBits() const { return m_stopBits; }
    void setStopBits(const QString& value);
    QString parity() const { return m_parity; }
    void setParity(const QString& value);
    QString handshake() const { return m_handshake; }
    void setHandshake(const QString& value);
    QString dtrState() const { return m_dtrState; }
    void setDtrState(const QString& value);
    QString rtsState() const { return m_rtsState; }
    void setRtsState(const QString& value);
    QString civAddress() const { return m_civAddress; }
    void setCivAddress(const QString& value);
    QString pttType() const { return m_pttType; }
    void setPttType(const QString& type);
    QString pttPort() const { return m_pttPort; }
    void setPttPort(const QString& port);
    // Preme e rilascia il PTT per un attimo, per sentire se la radio va in
    // trasmissione davvero.
    Q_INVOKABLE void testPtt(int milliseconds = 900);
    // I modelli che conosce Hamlib, letti da "rigctl -l": {id, name}. Quello che si ha
    // adesso (vuoto la prima volta): la lista vera arriva con rigModelsChanged().
    Q_INVOKABLE QVariantList rigModels();
    QVariantList rigModelList() { return rigModels(); }
    // Le porte seriali di questo computer, senza quelle riservate.
    Q_INVOKABLE QStringList serialPorts() const;
    // La porta su cui Decodium tiene il CAT, e se una porta e' riservata.
    Q_INVOKABLE QString decodiumCatPort() const;
    Q_INVOKABLE bool isPortReserved(const QString& port) const;
    // Le porte virtuali (com0com): quelle che DecoPort di Decodium offre, nel
    // dialetto della radio, a chi non puo' avere la seriale vera.
    Q_INVOKABLE QStringList virtualPorts() const;

signals:
    void changed();
    void serialCapabilitiesChanged();
    void rigModelsChanged();
    void stateChanged();
    void shareChanged();
    void macrosChanged();
    void decoderChanged();
    void audioDevicesChanged();
    void audioSelectionChanged();
    void decoderScopeChanged();
    void skimmerChanged();
    void typeToSendChanged();

private:
    void loadMacros();
    void publishSkimmer(bool force = false);
    void setActiveMacroIndex(int index);
    void sendExpandedText(const QString& ready, bool typed = false);
    bool directYaesuCwUsesKeyerMemory() const;
    void saveMacros();
    static QVariantList defaultMacros();

    void startAudio();
    void stopAudio();
    void consumeAudio(const QByteArray& chunk);
    void publishScope(bool force = false);
    void startLocalRigctld();
    QStringList localRigctldArguments(const QString& port, int baud, quint16 tcpPort) const;
    bool supportsSerialParameter(const QString& name) const;

    Context m_ctx;
    // Le due strade per la radio; m_rig e' quella in uso.
    core::RigControl m_hamlib;
    core::TciControl m_tci;
    core::FlrigControl m_flrig;
    core::OmniRigControl m_omniRig;
    core::CatShare m_share;
    QString m_flrigAddress{QStringLiteral("127.0.0.1:12345")};
    int m_omniRigNumber{1};
    core::RigLink* m_rig{&m_hamlib};
    QString m_tciAddress{QStringLiteral("127.0.0.1:40001")};
    int m_tciTrx{0};
    core::CwDecoder m_decoder{8000};
    std::unique_ptr<QAudioSource> m_audio;
    QIODevice* m_audioDevice{nullptr};
    QByteArray m_audioBuffer;
    QAudioFormat m_audioFormat;
    QString m_decoderText;
    int m_decoderToneLock{0};
    int m_decoderSpeedLock{0};
    QString m_decoderModel;
    QVariantMap m_scope;
    QElapsedTimer m_scopeClock;
    core::CwSkimmer m_skimmer{8000};
    bool m_typeToSend{false};
    bool m_skimmerOn{true};
    QVariantList m_stations;
    QVariantList m_spectrum;
    QElapsedTimer m_skimmerClock;
    std::unique_ptr<QTimer> m_testAudio;
    QString m_audioInput;
    QString m_audioInputId;
    QString m_audioInUse;
    QString m_audioInputFormat;
    mutable QMediaDevices* m_mediaDevices{nullptr};
    bool m_decoderOn{false};
    // rigctld avviato da noi quando la radio sta su una seriale.
    std::unique_ptr<QProcess> m_rigctld;
    // Dopo un cambio alla seriale (porta, velocita', modello, controllo di flusso...)
    // il rigctld in piedi ha ancora i parametri di prima: lo si ferma e se ne avvia un
    // altro, un attimo dopo l'ultima modifica e senza fermare la finestra.
    void scheduleSerialRestart();
    void restartSerialNow();
    QTimer* m_serialRestartTimer{nullptr};
    QString m_link{QStringLiteral("network")};
    QString m_serialPort;
    int m_rigModel{0};
    int m_baud{38400};
    QStringList m_serialCapabilities;
    int m_capabilitiesForModel{0};
    QString m_dataBits{QStringLiteral("Default")};
    QString m_stopBits{QStringLiteral("Default")};
    QString m_parity{QStringLiteral("Default")};
    QString m_handshake{QStringLiteral("Default")};
    QString m_dtrState{QStringLiteral("Unset")};
    QString m_rtsState{QStringLiteral("Unset")};
    QString m_civAddress;
    QString m_pttType{QStringLiteral("RIG")};
    QString m_pttPort;
    QVariantList m_models;
    // Le domande a Hamlib (modelli, capacita') partono su un processo a parte e si
    // aspettano senza bloccare la finestra.
    std::unique_ptr<QProcess> m_modelsProcess;
    std::unique_ptr<QProcess> m_capsProcess;
    int m_capsModel{0};
    QList<std::function<void()>> m_afterCapabilities;
    void loadRigModels();
    void requestCapabilities(std::function<void()> then);
    // L'avvio di rigctld sulla seriale: ogni connectNow() apre una "generazione"; quel che
    // resta in attesa di una generazione vecchia non fa piu' niente.
    quint64 m_launchGeneration{0};
    QElapsedTimer m_rigctldClock;
    void launchRigctld(quint64 generation);
    void connectHamlibLater(quint64 generation, qint64 delayMs);
    // La ricerca della radio: elenco di tentativi (porta, velocita').
    QVariantList m_probe;
    int m_probeIndex{-1};
    quint64 m_probeGeneration{0};
    QByteArray m_probeBuffer;
    std::unique_ptr<QProcess> m_probeProcess;
    std::unique_ptr<QTcpSocket> m_probeSocket;
    void probeNext();
    void probeAsk(quint64 generation, const QString& port, int baud, quint16 tcpPort);
    void probeAdvance();
    void probeFinish(bool found, const QString& port, int baud);
    QVariantList m_macros;
    QString m_host{QStringLiteral("127.0.0.1")};
    int m_port{4532};
    int m_wpm{24};
    bool m_canKeyCw{true};
    core::CwKeyer m_keyer;
    core::WinKeyer m_winKeyer;
    // La radio che ha ricevuto l'ultima macro CW via CAT. Con SO2R puo'
    // essere radio 2, non necessariamente m_rig: Stop deve tornare allo
    // stesso destinatario anche se nel frattempo cambia il fuoco.
    core::RigLink* m_cwRigInUse{nullptr};
    int m_activeMacroIndex{-1};
    QString m_keyerPort;
    QString m_keyerLine{QStringLiteral("DTR")};
    void openKeyer();
    bool m_enabled{false};
};

} // namespace decolog::app
