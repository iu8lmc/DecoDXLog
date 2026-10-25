// DecoDXLog — il punto d'incontro fra log, rete e interfaccia.
//
// Riceve i QSO dal protocollo UDP, li scrive nel database, aggiorna la tabella e
// racconta all'operatore cosa e' successo: un QSO scartato come duplicato va
// detto, non taciuto.
#pragma once

#include "app/ClusterController.h"
#include "app/ActivationController.h"
#include "app/LogLibrary.h"
#include "app/QslCardController.h"
#include "app/QslController.h"
#include "app/CloudController.h"
#include "app/RigController.h"
#include "app/RotorController.h"
#include "app/SolarController.h"
#include "app/UpdateController.h"
#include "app/WorldClockController.h"
#include "app/ActivityModel.h"
#include "app/DecodeFeed.h"
#include "app/TrafficMonitor.h"
#include "app/ChatController.h"
#include "app/SuperCheckController.h"
#include "app/CallListController.h"
#include "app/NetController.h"
#include "app/So2rController.h"
#include "app/VoiceKeyerController.h"
#include "app/QsoTableModel.h"
#include "app/StationProfileModel.h"
#include "core/Awards.h"
#include "core/CqMarathon.h"
#include "core/Vucc.h"
#include "core/Callbook.h"
#include "core/Countries.h"
#include "core/DecoLinkServer.h"
#include "core/DecoPortLog.h"
#include "core/CredentialStore.h"
#include "core/LogBackup.h"
#include "core/LogDatabase.h"
#include "core/Lotw.h"
#include "core/DecodiumLog.h"
#include "core/LocalApi.h"
#include "core/ClubLogCty.h"
#include "core/N1mm.h"
#include "core/QslDownload.h"
#include "core/UdpReceiver.h"

#include <QDateTime>
#include <QObject>
#include <QElapsedTimer>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <memory>
#include <optional>
#include <utility>

namespace decolog::app {

class DecoLogController : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString version READ version CONSTANT)
    // Quello che sta nella finestra "Informazioni": chi l'ha fatto, con cosa,
    // e dove si trova il codice. Costanti: si leggono una volta e non cambiano.
    Q_PROPERTY(QVariantMap about READ about CONSTANT)
    Q_PROPERTY(QString qtVersion READ qtVersion CONSTANT)
    Q_PROPERTY(QString buildInfo READ buildInfo CONSTANT)
    Q_PROPERTY(QString databasePath READ databasePath CONSTANT)
    Q_PROPERTY(bool databaseOpen READ databaseOpen CONSTANT)
    Q_PROPERTY(QObject* qsoModel READ qsoModel CONSTANT)
    Q_PROPERTY(QObject* stationProfiles READ stationProfiles CONSTANT)
    Q_PROPERTY(QObject* credentials READ credentials CONSTANT)
    Q_PROPERTY(QObject* cluster READ cluster CONSTANT)
    Q_PROPERTY(QObject* qsl READ qsl CONSTANT)
    Q_PROPERTY(QObject* cards READ cards CONSTANT)
    Q_PROPERTY(QObject* solar READ solar CONSTANT)
    // Gli aggiornamenti: guarda da solo se e' uscita una versione nuova.
    Q_PROPERTY(QObject* updates READ updates CONSTANT)
    Q_PROPERTY(QObject* rotor READ rotor CONSTANT)
    // L'orologio mondiale: UTC, QTH, citta', alba, tramonto e grayline.
    Q_PROPERTY(QObject* worldClock READ worldClock CONSTANT)
    // La chat ON4KST.
    Q_PROPERTY(QObject* chat READ chat CONSTANT)
    // Super Check Partial e N+1 per i contest.
    Q_PROPERTY(QObject* scp READ scp CONSTANT)
    Q_PROPERTY(QObject* callLists READ callLists CONSTANT)
    // La rete della stazione multi-operatore.
    Q_PROPERTY(QObject* net READ net CONSTANT)
    // SO2R: la seconda radio e la scatola OTRSP.
    Q_PROPERTY(QObject* so2r READ so2r CONSTANT)
    // Il keyer vocale (DVK) per la fonia.
    Q_PROPERTY(QObject* dvk READ dvk CONSTANT)
    // La radio via Hamlib, con le macro in CW per i contest.
    Q_PROPERTY(QObject* rig READ rig CONSTANT)
    Q_PROPERTY(QObject* cloud READ cloud CONSTANT)
    Q_PROPERTY(QObject* activation READ activation CONSTANT)
    // I log della stazione: quello aperto e gli altri, per i contest.
    Q_PROPERTY(QObject* logs READ logs CONSTANT)

    // ── Collegamento con Decodium ──────────────────────────────────────────
    Q_PROPERTY(int udpPort READ udpPort WRITE setUdpPort NOTIFY udpChanged)
    Q_PROPERTY(QString multicastGroup READ multicastGroup WRITE setMulticastGroup NOTIFY udpChanged)
    // Ripetitore UDP: a chi inoltrare i pacchetti di Decodium ("host:porta, ...").
    Q_PROPERTY(QString udpForward READ udpForward WRITE setUdpForward NOTIFY udpChanged)
    Q_PROPERTY(QString udpForwardError READ udpForwardError NOTIFY udpChanged)
    // I QSO di N1MM Logger+ (XML su UDP, di solito la 12060). 0 = spento.
    Q_PROPERTY(int n1mmPort READ n1mmPort WRITE setN1mmPort NOTIFY udpChanged)
    Q_PROPERTY(bool n1mmListening READ n1mmListening NOTIFY udpChanged)
    Q_PROPERTY(QString n1mmError READ n1mmError NOTIFY udpChanged)
    // L'interfaccia HTTP locale per altri programmi (solo 127.0.0.1, con chiave).
    Q_PROPERTY(int apiPort READ apiPort WRITE setApiPort NOTIFY udpChanged)
    Q_PROPERTY(QString apiToken READ apiToken NOTIFY udpChanged)
    Q_PROPERTY(bool apiListening READ apiListening NOTIFY udpChanged)
    Q_PROPERTY(QString apiError READ apiError NOTIFY udpChanged)
    Q_PROPERTY(bool preferLoggedAdif READ preferLoggedAdif WRITE setPreferLoggedAdif NOTIFY udpChanged)
    Q_PROPERTY(bool listening READ listening NOTIFY udpChanged)
    Q_PROPERTY(QString udpError READ udpError NOTIFY udpChanged)
    Q_PROPERTY(int dedupDigitalMinutes READ dedupDigitalMinutes WRITE setDedupDigitalMinutes NOTIFY udpChanged)
    Q_PROPERTY(int dedupManualMinutes READ dedupManualMinutes WRITE setDedupManualMinutes NOTIFY udpChanged)
    Q_PROPERTY(bool followDxCall READ followDxCall WRITE setFollowDxCall NOTIFY udpChanged)

    Q_PROPERTY(bool clientConnected READ clientConnected NOTIFY clientChanged)
    Q_PROPERTY(QString clientName READ clientName NOTIFY clientChanged)
    Q_PROPERTY(QString clientVersion READ clientVersion NOTIFY clientChanged)
    Q_PROPERTY(QString dialFrequency READ dialFrequency NOTIFY clientChanged)
    Q_PROPERTY(QString dialBand READ dialBand NOTIFY clientChanged)
    Q_PROPERTY(QString currentMode READ currentMode NOTIFY clientChanged)
    Q_PROPERTY(QString dxCall READ dxCall NOTIFY clientChanged)
    Q_PROPERTY(QString deCall READ deCall NOTIFY clientChanged)
    Q_PROPERTY(bool transmitting READ transmitting NOTIFY clientChanged)

    // ── Log ────────────────────────────────────────────────────────────────
    // I conteggi del log si rifanno poco dopo un cambiamento, una volta sola,
    // e si tengono: prima ogni lettura dal QML era una query su tutto il log, e
    // dopo ogni QSO la finestra restava ferma mezzo secondo a ricontare.
    Q_PROPERTY(int qsoCount READ qsoCount NOTIFY countsChanged)
    Q_PROPERTY(int dirtyCount READ dirtyCount NOTIFY countsChanged)
    Q_PROPERTY(int conflictCount READ conflictCount NOTIFY countsChanged)
    // Le statistiche di tutto il log non cambiano con il QSO: si rifanno un
    // momento dopo, a digitazione finita (statsChanged), e una volta sola.
    Q_PROPERTY(QVariantMap ft2Award READ ft2Award NOTIFY statsChanged)
    Q_PROPERTY(QVariantList bandStats READ bandStats NOTIFY statsChanged)
    Q_PROPERTY(QVariantList modeStats READ modeStats NOTIFY statsChanged)
    Q_PROPERTY(QVariantList qslSummary READ qslSummary NOTIFY countsChanged)
    Q_PROPERTY(QVariantList gridPoints READ gridPoints NOTIFY logChanged)
    Q_PROPERTY(QVariantList incoming READ incoming NOTIFY incomingChanged)
    // Il registro attivita': un modello a righe, cosi' una riga nuova non rifa' la vista.
    Q_PROPERTY(QObject* activity READ activity CONSTANT)
    // Il monitor del traffico con Decodium (UDP, DecoLink, DecoPort), a due vie.
    Q_PROPERTY(QObject* traffic READ traffic CONSTANT)
    // Le due liste di decodifica di Decodium (Full Spectrum e Signal RX) e lo
    // stato di chi le manda, per il pannello Decodium.
    Q_PROPERTY(QObject* decodium READ decodium CONSTANT)

    Q_PROPERTY(QString lookupCall READ lookupCall WRITE setLookupCall NOTIFY lookupChanged)
    Q_PROPERTY(QVariantMap callInfo READ callInfo NOTIFY lookupChanged)
    // Cambiano sia quando parla Decodium sia quando si muove la radio.
    Q_PROPERTY(QString shownFrequency READ shownFrequency NOTIFY tuningChanged)
    Q_PROPERTY(QString shownMode READ shownMode NOTIFY tuningChanged)
    Q_PROPERTY(QString myGrid READ myGrid NOTIFY stationChanged)
    Q_PROPERTY(QVariantMap myPosition READ myPosition NOTIFY stationChanged)

    Q_PROPERTY(QStringList bands READ bands CONSTANT)
    // "auto", "it", "en": si applica al riavvio.
    Q_PROPERTY(QString uiLanguage READ uiLanguage WRITE setUiLanguage NOTIFY uiLanguageChanged)
    // Campi opzionali e campi ADIF liberi del pannello Nuovo QSO. Sono
    // preferenze UI portabili e cambiano subito, senza riavviare.
    Q_PROPERTY(QStringList newQsoVisibleFields READ newQsoVisibleFields NOTIFY newQsoSettingsChanged)
    Q_PROPERTY(QVariantList newQsoCustomFields READ newQsoCustomFields NOTIFY newQsoSettingsChanged)

    // ── Entita' DXCC (cty.csv di AD1C) ─────────────────────────────────────
    Q_PROPERTY(QString countriesVersion READ countriesVersion NOTIFY countriesChanged)
    Q_PROPERTY(int countriesEntities READ countriesEntities NOTIFY countriesChanged)
    Q_PROPERTY(QString countriesSource READ countriesSource NOTIFY countriesChanged)
    Q_PROPERTY(int missingDxccCount READ missingDxccCount NOTIFY countsChanged)

    // ── DecoLink (canale locale con Decodium) ──────────────────────────────
    Q_PROPERTY(bool decoLinkEnabled READ decoLinkEnabled WRITE setDecoLinkEnabled NOTIFY decoLinkChanged)
    Q_PROPERTY(int decoLinkPort READ decoLinkPort WRITE setDecoLinkPort NOTIFY decoLinkChanged)
    Q_PROPERTY(bool decoLinkListening READ decoLinkListening NOTIFY decoLinkChanged)
    Q_PROPERTY(QString decoLinkError READ decoLinkError NOTIFY decoLinkChanged)
    Q_PROPERTY(QVariantList decoLinkClients READ decoLinkClients NOTIFY decoLinkChanged)

    // ── HamHub (HH) su DecoPort: i QSO firmati verso il ponte di HamHub Log ─
    Q_PROPERTY(bool hamHubLinkEnabled READ hamHubLinkEnabled WRITE setHamHubLinkEnabled NOTIFY hamHubLinkChanged)
    Q_PROPERTY(int hamHubLinkPort READ hamHubLinkPort WRITE setHamHubLinkPort NOTIFY hamHubLinkChanged)
    Q_PROPERTY(bool hamHubLinkListening READ hamHubLinkListening NOTIFY hamHubLinkChanged)
    Q_PROPERTY(bool hamHubLinkHasPassword READ hamHubLinkHasPassword NOTIFY hamHubLinkChanged)
    Q_PROPERTY(QString hamHubLinkError READ hamHubLinkError NOTIFY hamHubLinkChanged)
    Q_PROPERTY(int hamHubLinkClients READ hamHubLinkClients NOTIFY hamHubLinkChanged)
    Q_PROPERTY(int hamHubLinkSent READ hamHubLinkSent NOTIFY hamHubLinkChanged)
    Q_PROPERTY(int hamHubLinkAcked READ hamHubLinkAcked NOTIFY hamHubLinkChanged)

    // ── Award ──────────────────────────────────────────────────────────────
    Q_PROPERTY(QVariantList awardSummary READ awardSummary NOTIFY awardsChanged)
    Q_PROPERTY(QStringList awardBands READ awardBands NOTIFY awardsChanged)
    Q_PROPERTY(QString awardBand READ awardBand WRITE setAwardBand NOTIFY awardsChanged)
    Q_PROPERTY(QString awardModeGroup READ awardModeGroup WRITE setAwardModeGroup NOTIFY awardsChanged)
    Q_PROPERTY(bool awardConfirmLotw READ awardConfirmLotw WRITE setAwardConfirmLotw NOTIFY awardsChanged)
    Q_PROPERTY(bool awardConfirmCard READ awardConfirmCard WRITE setAwardConfirmCard NOTIFY awardsChanged)
    Q_PROPERTY(bool awardConfirmEqsl READ awardConfirmEqsl WRITE setAwardConfirmEqsl NOTIFY awardsChanged)
    // I 60 metri nel DXCC, nel Challenge e nel WAS (l'ARRL non li accetta).
    Q_PROPERTY(bool awardCount60m READ awardCount60m WRITE setAwardCount60m NOTIFY awardsChanged)
    Q_PROPERTY(int awardProfile READ awardProfile WRITE setAwardProfile NOTIFY awardsChanged)
    Q_PROPERTY(QString awardTag READ awardTag WRITE setAwardTag NOTIFY awardsChanged)

    // ── Callbook (QRZ.com / HamQTH) ────────────────────────────────────────
    Q_PROPERTY(QString callbookProvider READ callbookProvider WRITE setCallbookProvider NOTIFY callbookChanged)
    Q_PROPERTY(bool callbookAutofill READ callbookAutofill WRITE setCallbookAutofill NOTIFY callbookChanged)
    // Completare i QSO appena scritti con quello che sa il callbook.
    Q_PROPERTY(bool callbookComplete READ callbookComplete WRITE setCallbookComplete NOTIFY callbookChanged)
    // Se il callbook scelto non sa niente, prova l'altro (se ha le credenziali).
    Q_PROPERTY(bool callbookFallback READ callbookFallback WRITE setCallbookFallback NOTIFY callbookChanged)
    Q_PROPERTY(QString callbookStatus READ callbookStatus NOTIFY callbookChanged)
    Q_PROPERTY(bool callbookBusy READ callbookBusy NOTIFY callbookChanged)
    // Quanti QSO aspettano ancora il loro giro di ricerca.
    Q_PROPERTY(int callbookQueued READ callbookQueued NOTIFY callbookChanged)

    // ── LoTW (conferme) ────────────────────────────────────────────────────
    Q_PROPERTY(bool lotwBusy READ lotwBusy NOTIFY lotwChanged)
    Q_PROPERTY(QString lotwStatus READ lotwStatus NOTIFY lotwChanged)
    // Il riepilogo dell'ultimo scarico di ogni servizio (LoTW, eQSL, QRZ, e gli
    // account dei profili): {runs: [{label, service, when, confirmed, already,
    // notFound, invalid}], rows: [{label, service, kind, on, utc, call, band,
    // mode, station, reason, qsoId, dxcc, country, newDxcc, account}]}.
    // kind: "new" (confermato adesso), "notfound", "invalid". Resta nel log.
    Q_PROPERTY(QVariantMap qslImport READ qslImport NOTIFY qslImportChanged)
    Q_PROPERTY(QString lotwLastSync READ lotwLastSync NOTIFY lotwChanged)
    Q_PROPERTY(QString lotwCursor READ lotwCursor NOTIFY lotwChanged)
    Q_PROPERTY(int lotwAutoHours READ lotwAutoHours WRITE setLotwAutoHours NOTIFY lotwChanged)
    // Le conferme di eQSL e di QRZ Logbook: una alla volta, con il loro stato.
    Q_PROPERTY(bool confirmBusy READ confirmBusy NOTIFY confirmChanged)
    Q_PROPERTY(QString confirmBusyService READ confirmBusyService NOTIFY confirmChanged)
    Q_PROPERTY(QString confirmStatus READ confirmStatus NOTIFY confirmChanged)
    // Scarico automatico delle conferme di eQSL e QRZ: ore (0 = spento), e per
    // ogni servizio l'ultimo scarico riuscito, gia' scritto per lo schermo.
    Q_PROPERTY(int confirmAutoHours READ confirmAutoHours WRITE setConfirmAutoHours NOTIFY confirmChanged)
    Q_PROPERTY(QVariantMap confirmLastSync READ confirmLastSync NOTIFY confirmChanged)
    Q_PROPERTY(bool confirmFailed READ confirmFailed NOTIFY confirmChanged)
    // Recupero dal log di Decodium: i QSO registrati la' mentre DecoDXLog era
    // chiuso o non li riceveva. Il file si sceglie a mano o si prende quello
    // che Decodium sta usando.
    Q_PROPERTY(bool decodiumRecovery READ decodiumRecovery WRITE setDecodiumRecovery NOTIFY recoveryChanged)
    Q_PROPERTY(QString decodiumLogPath READ decodiumLogPath WRITE setDecodiumLogPath NOTIFY recoveryChanged)
    Q_PROPERTY(QString decodiumLogInUse READ decodiumLogInUse NOTIFY recoveryChanged)
    Q_PROPERTY(QString recoveryStatus READ recoveryStatus NOTIFY recoveryChanged)
    Q_PROPERTY(bool recoveryBusy READ recoveryBusy NOTIFY recoveryChanged)
    // Gli altri log ADIF tenuti d'occhio (fldigi, WSJT-X, JTDX...): path, label,
    // enabled, exists, status.
    Q_PROPERTY(QVariantList adifWatches READ adifWatches NOTIFY recoveryChanged)
    // Il cty.xml di Club Log (entita' con le date): loaded, date, entities,
    // status; e le correzioni trovate confrontando i QSO con le date.
    Q_PROPERTY(QVariantMap clublogCty READ clublogCty NOTIFY ctyChanged)
    Q_PROPERTY(QVariantMap entityFixes READ entityFixes NOTIFY ctyChanged)
    Q_PROPERTY(QVariantList adifWatchSuggestions READ adifWatchSuggestions NOTIFY recoveryChanged)

    // L'importazione ADIF gira su un altro filo: da 0 a 1 mentre va, -1 ferma.
    Q_PROPERTY(double importProgress READ importProgress NOTIFY importChanged)
    // Anche una modifica in blocco: da 0 a 1 mentre va, -1 ferma.
    Q_PROPERTY(double bulkProgress READ bulkProgress NOTIFY bulkChanged)
    // I doppioni trovati: {busy, windowMinutes, searched, limited, groups:
    // [{keep, rows: [{id, call, when, band, mode, freq, source, confirmed, fields}]}]}.
    Q_PROPERTY(QVariantMap duplicates READ duplicates NOTIFY duplicatesChanged)
    // ── Backup ─────────────────────────────────────────────────────────────
    Q_PROPERTY(bool backupEnabled READ backupEnabled WRITE setBackupEnabled NOTIFY backupChanged)
    Q_PROPERTY(QString backupDir READ backupDir WRITE setBackupDir NOTIFY backupChanged)
    Q_PROPERTY(QString backupTime READ backupTime WRITE setBackupTime NOTIFY backupChanged)
    Q_PROPERTY(int backupKeep READ backupKeep WRITE setBackupKeep NOTIFY backupChanged)
    Q_PROPERTY(QString lastBackup READ lastBackup NOTIFY backupChanged)
    Q_PROPERTY(QString lastBackupInfo READ lastBackupInfo NOTIFY backupChanged)

    // ── Cloud (Fase 3: per ora solo le preferenze) ─────────────────────────
    Q_PROPERTY(QString cloudServer READ cloudServer WRITE setCloudServer NOTIFY cloudChanged)
    Q_PROPERTY(QString autoSync READ autoSync WRITE setAutoSync NOTIFY cloudChanged)
    Q_PROPERTY(QString conflictPolicy READ conflictPolicy WRITE setConflictPolicy NOTIFY cloudChanged)

public:
    explicit DecoLogController(QObject* parent = nullptr);
    ~DecoLogController() override;

    // Chiusura ordinata, mentre l'event loop Qt e' ancora vivo.  Lasciare che
    // tutto venga fatto solo dai distruttori puo' trattenere il processo dopo
    // che l'ultima finestra e' gia' sparita.
    void shutdown();

    bool openDatabase(const QString& path);
    void startListening();
    void startN1mm();
    // Porta da riga di comando: vale per questa sessione, non si salva.
    void overrideUdpPort(int port) { m_udpPort = port; }

    QString version() const;
    QVariantMap about() const;
    QString qtVersion() const;
    // Data della compilazione e piattaforma: serve a chi segnala un problema.
    QString buildInfo() const;
    QString databasePath() const { return m_db.path(); }
    bool databaseOpen() const { return m_db.isOpen(); }
    QObject* qsoModel() const { return m_model; }
    QObject* stationProfiles() const { return m_profiles; }
    QObject* credentials() const { return m_credentials; }
    QObject* cluster() const { return m_cluster; }
    QObject* qsl() const { return m_qsl; }
    QObject* cards() const { return m_cards; }
    QObject* solar() const { return m_solar; }
    QObject* updates() const { return m_updates; }
    QObject* rotor() const { return m_rotor; }
    QObject* worldClock() const { return m_worldClock; }
    QObject* chat() const { return m_chat; }
    QObject* scp() const { return m_scp; }
    QObject* callLists() const { return m_callLists; }
    QObject* net() const { return m_net; }
    QObject* so2r() const { return m_so2r; }
    QObject* dvk() const { return m_dvk; }
    // Si opera in fonia (SSB, AM, FM)? Allora i tasti funzione sono il DVK.
    Q_INVOKABLE bool phoneMode() const;
    // Un tasto funzione: in fonia il messaggio registrato, altrimenti la macro CW.
    Q_INVOKABLE void functionKey(int index, const QVariantMap& context);
    // Esc: ferma CW e voce.
    Q_INVOKABLE void stopSending();
    QObject* rig() const { return m_rig; }
    QObject* cloud() const { return m_cloud; }
    QObject* activation() const { return m_activation; }
    QObject* logs() const { return m_logs; }
    // Dopo openDatabase e startDecoLink: le fonti del cluster si collegano.
    void startCluster();
    // Il rotore si collega anche in una prova: leggere dove guarda l'antenna
    // non muove niente.
    void startRotor();
    // Il sync: `automatic` a false carica il token senza spingere niente.
    void startCloud(bool automatic);

    int udpPort() const { return m_udpPort; }
    void setUdpPort(int port);
    QString multicastGroup() const { return m_multicast; }
    QString udpForward() const { return m_udpForward; }
    void setUdpForward(const QString& targets);
    QString udpForwardError() const { return m_udpForwardError; }
    int n1mmPort() const { return m_n1mmPort; }
    void setN1mmPort(int port);
    bool n1mmListening() const { return m_n1mm.isListening(); }
    QString n1mmError() const { return m_n1mm.lastError(); }
    int apiPort() const { return m_apiPort; }
    void setApiPort(int port);
    QString apiToken() const { return m_apiToken; }
    bool apiListening() const { return m_api.isListening(); }
    QString apiError() const { return m_api.lastError(); }
    // Una chiave nuova: i programmi che usavano la vecchia vanno aggiornati.
    Q_INVOKABLE void newApiToken();
    void startApi();
    void setMulticastGroup(const QString& group);
    bool preferLoggedAdif() const { return m_udp.prefersLoggedAdif(); }
    void setPreferLoggedAdif(bool prefer);
    bool listening() const { return m_udp.isListening(); }
    QString udpError() const { return m_udp.lastError(); }
    int dedupDigitalMinutes() const { return m_db.dedupWindowSeconds(false) / 60; }
    void setDedupDigitalMinutes(int minutes);
    int dedupManualMinutes() const { return m_db.dedupWindowSeconds(true) / 60; }
    void setDedupManualMinutes(int minutes);
    bool followDxCall() const { return m_followDx; }
    void setFollowDxCall(bool follow);

    bool clientConnected() const;
    QString clientName() const { return m_clientName; }
    QString clientVersion() const { return m_clientVersion; }
    QString dialFrequency() const;
    QString dialBand() const;
    QString currentMode() const { return m_status.submode.isEmpty() ? m_status.mode : m_status.submode; }
    // Quello che si mostra in cima: la radio quando c'e', perche' e' lo stato
    // vero; Decodium quando la radio non c'e'. Il modo di Decodium vince solo
    // se dice la stessa cosa della radio con un nome piu' preciso — "FT8"
    // invece di "PKTUSB".
    // L'email del corrispondente secondo il callbook. Se c'e' gia' risponde
    // subito, se no la cerca e richiama quando arriva. Senza callbook, o senza
    // email nella scheda, richiama con l'errore.
    void emailFor(const QString& call,
                  std::function<void(const QString& email, const QString& error)> done);

    QString shownFrequency() const;
    QString shownMode() const;
    QString dxCall() const { return m_status.dxCall; }
    QString deCall() const { return m_status.deCall; }
    bool transmitting() const { return m_status.transmitting; }

    int qsoCount() const { ensureCounts(); return m_counts.qsos; }
    int dirtyCount() const { ensureCounts(); return m_counts.dirty; }
    int conflictCount() const { ensureCounts(); return m_counts.conflicts; }
    QVariantMap ft2Award() const;
    QVariantList bandStats() const;
    QVariantList modeStats() const;
    QVariantList qslSummary() const;
    QVariantList gridPoints() const;
    QVariantList incoming() const { return m_incoming; }
    QObject* activity() const { return m_activityModel; }
    QObject* traffic() const { return m_traffic; }
    QObject* decodium() const { return m_decodeFeed; }

    QString lookupCall() const { return m_lookupCall; }
    void setLookupCall(const QString& call);
    QVariantMap callInfo() const { return m_callInfo; }
    QString myGrid() const;
    QVariantMap myPosition() const;

    QStringList bands() const;
    QString uiLanguage() const;
    void setUiLanguage(const QString& language);

    QString countriesVersion() const { return m_countries.version(); }
    int countriesEntities() const { return m_countries.entityCount(); }
    QString countriesSource() const { return m_countriesSource; }
    int missingDxccCount() const { ensureCounts(); return m_counts.missingDxcc; }

    bool decoLinkEnabled() const { return m_decoLinkEnabled; }
    void setDecoLinkEnabled(bool enabled);
    int decoLinkPort() const { return m_decoLinkPort; }
    void setDecoLinkPort(int port);
    bool decoLinkListening() const { return m_decoLink.isListening(); }
    QString decoLinkError() const { return m_decoLink.lastError(); }
    QVariantList decoLinkClients() const;
    void startDecoLink();

    bool hamHubLinkEnabled() const { return m_hamHubLinkEnabled; }
    void setHamHubLinkEnabled(bool enabled);
    int hamHubLinkPort() const { return m_hamHubLinkPort; }
    void setHamHubLinkPort(int port);
    bool hamHubLinkListening() const { return m_hamHubLink.isListening(); }
    bool hamHubLinkHasPassword() const { return m_hamHubLinkHasPassword; }
    QString hamHubLinkError() const;
    int hamHubLinkClients() const { return m_hamHubLink.clientCount(); }
    int hamHubLinkSent() const { return m_hamHubLink.sentCount(); }
    int hamHubLinkAcked() const { return m_hamHubLink.ackedCount(); }
    // Dopo startDecoLink: legge la password dal portachiavi e apre la sessione.
    void startHamHubLink();
    // La spia dei blocchi: un battito ogni quarto di secondo. Se fra due
    // battiti passa molto piu' tempo, vuol dire che il filo che disegna
    // l'interfaccia e' rimasto fermo — ed e' quello che si vede come finestre
    // che non rispondono. Lo si scrive nel registro, con quanto e' durato,
    // perche' un blocco raccontato a voce non si trova mai.
    void startFreezeWatch();
    // Cronometra un lavoro e, se ha tenuto ferma la finestra piu' del dovuto,
    // lo scrive nel registro col suo nome. Un blocco senza nome non si corregge.
    void timed(const QString& what, const std::function<void()>& work);

    QVariantList awardSummary() const;
    QStringList awardBands() const;
    // Quante entita' DXCC stanno in Africa, secondo il cty.csv in uso: e' il
    // traguardo del WAAC, e non lo decidiamo noi.
    int africanEntities() const;
    QString awardBand() const { return m_awardFilter.band; }
    void setAwardBand(const QString& band);
    QString awardModeGroup() const { return m_awardFilter.modeGroup; }
    void setAwardModeGroup(const QString& group);
    // Le conferme valide per un diploma: services (quelle che contano adesso),
    // official (quelle del regolamento, se ne ha), custom (scelte a mano).
    Q_INVOKABLE QVariantMap awardCredits(const QString& awardId) const;
    Q_INVOKABLE void setAwardCredit(const QString& awardId, const QString& service, bool on);
    Q_INVOKABLE void resetAwardCredits(const QString& awardId);
    // Domande ufficiali: una conferma del log non crea automaticamente un credito.
    Q_INVOKABLE QVariantList awardClaims() const;
    Q_INVOKABLE QString createAwardClaim(const QString& awardId, const QString& ruleVersion,
                                          const QVariantList& qsoIds);
    Q_INVOKABLE bool transitionAwardClaim(const QString& claimId, const QString& state,
                                          const QString& note, const QString& evidence);
    // Importa un esito esterno verificabile; ritorna una stringa vuota solo
    // quando account, categoria, provider e QSO sono stati riconciliati.
    Q_INVOKABLE QString reconcileAwardResult(const QString& json);
    bool awardConfirmLotw() const { return m_awardFilter.confirmLotw; }
    void setAwardConfirmLotw(bool on);
    bool awardConfirmCard() const { return m_awardFilter.confirmCard; }
    void setAwardConfirmCard(bool on);
    bool awardConfirmEqsl() const { return m_awardFilter.confirmEqsl; }
    void setAwardConfirmEqsl(bool on);
    bool awardCount60m() const { return m_awardFilter.count60m; }
    void setAwardCount60m(bool on);
    int awardProfile() const { return static_cast<int>(m_awardFilter.stationProfileId); }
    void setAwardProfile(int profileId);
    QString awardTag() const { return m_awardFilter.tag; }
    void setAwardTag(const QString& tag);
    // Gli elementi di un award per la tabella. `view`: "all", "unconfirmed" o
    // "missing" (quelli mai lavorati, per gli award con un elenco chiuso).
    Q_INVOKABLE QVariantList awardItems(const QString& awardId, const QString& search, const QString& view) const;
    // Per banda (le colonne della tabella): [{band, worked, confirmed}].
    Q_INVOKABLE QVariantList awardBandTotals(const QString& awardId) const;
    // Anteprima/export deterministico dei QSO per CQ DX Marathon. Non modifica
    // il log e non dichiara il risultato ufficiale dell'ente organizzatore.
    Q_INVOKABLE QVariantMap cqMarathonPreview(int year, const QString& category,
                                               int maxPerEntity = 1) const;
    Q_INVOKABLE bool saveCqMarathonExport(const QUrl& file, int year, const QString& category,
                                          int maxPerEntity = 1) const;
    // VUCC resta separato dal conteggio statistico dei locatori.
    Q_INVOKABLE QVariantMap vuccPreview(const QString& band, const QString& account = {},
                                         const QString& site = {}, int target = 100) const;

    // ── Statistiche ─────────────────────────────────────────────────────────
    // `mode` vuoto = tutti i modi, `year` 0 = tutti gli anni. Ogni elenco e'
    // [{key, count}], pronto per le barre del QML.
    // Le coste del mondo per la mappa: [[ [lon, lat], ... ], ...]. Sta qui perche'
    // XMLHttpRequest non legge le risorse dell'eseguibile.
    Q_INVOKABLE QVariantList coastline() const;
    // Gli anelli di terraferma, per la mappa azimutale del rotore.
    Q_INVOKABLE QVariantList landmasses() const;

    // Tutte le statistiche della finestra in una volta, su un altro filo: su un
    // log grande sono secondi, e la finestra non si deve fermare. Il risultato
    // arriva con statsReady(), con mode e year di chi l'ha chiesto.
    Q_INVOKABLE void requestStats(const QString& mode, int year);
    Q_INVOKABLE QStringList statsYears() const;
    Q_INVOKABLE QVariantMap statsSummary(const QString& mode = {}, int year = 0) const;
    Q_INVOKABLE QVariantList statsByYear(const QString& mode = {}) const;
    Q_INVOKABLE QVariantList statsByMonth(int months = 24, const QString& mode = {}) const;
    Q_INVOKABLE QVariantList statsByHour(const QString& mode = {}, int year = 0) const;
    Q_INVOKABLE QVariantList statsByBand(const QString& mode = {}, int year = 0) const;
    Q_INVOKABLE QVariantList statsByMode(int year = 0) const;
    Q_INVOKABLE QVariantList statsByContinent(const QString& mode = {}, int year = 0) const;
    Q_INVOKABLE QVariantList statsBandHour(const QString& mode = {}, int year = 0) const;
    Q_INVOKABLE QVariantList statsTopEntities(const QString& mode = {}, int year = 0, int limit = 15) const;
    Q_INVOKABLE QVariantList statsTopCalls(const QString& mode = {}, int year = 0, int limit = 15) const;
    Q_INVOKABLE QVariantList statsBandMode(int year = 0) const;
    Q_INVOKABLE QVariantList statsAwardProgress(const QString& mode = {}) const;
    // DXCC, FT2, WAZ e WAS hanno un elenco completo: si puo' dire cosa manca.
    Q_INVOKABLE bool awardHasMissing(const QString& awardId) const;
    // I locatori dell'award "grids" per la mappa: [{grid, confirmed}].
    Q_INVOKABLE QVariantList awardGrids() const;

    QString callbookProvider() const { return core::CallbookClient::providerId(m_callbook.provider()); }
    void setCallbookProvider(const QString& id);
    bool callbookAutofill() const { return m_callbookAutofill; }
    bool callbookComplete() const { return m_callbookComplete; }
    void setCallbookComplete(bool complete);
    bool callbookFallback() const { return m_callbookFallback; }
    void setCallbookFallback(bool enabled);
    int callbookQueued() const { return static_cast<int>(m_callbookQueue.size()); }
    // Completa un QSO gia' scritto: dalla scheda del QSO, e dal log per
    // tutte le righe mostrate. Torna quanti ne ha completati (o messi in
    // coda alla ricerca).
    Q_INVOKABLE int completeQsoFromCallbook(qint64 id);
    Q_INVOKABLE int completeShownFromCallbook();
    // Tutto il log, non solo quello che si vede: si mettono in coda e si
    // chiedono uno per volta, per non prendere a badilate il callbook.
    Q_INVOKABLE int completeMissingFromCallbook();
    Q_INVOKABLE void stopCallbookQueue();
    Q_INVOKABLE int damagedFieldCount() const;
    Q_INVOKABLE int repairImportedFields();
    void setCallbookAutofill(bool autofill);
    QString callbookStatus() const { return m_callbookStatus; }
    bool callbookBusy() const { return !m_callbookPending.isEmpty(); }

    bool lotwBusy() const { return m_lotw.busy() || m_lotwStarting; }
    bool confirmBusy() const { return m_confirmDownloader.busy() || !m_confirmStarting.isEmpty(); }
    QString confirmBusyService() const { return m_confirmService; }
    QString confirmStatus() const { return m_confirmStatus; }
    int confirmAutoHours() const { return m_confirmAutoHours; }
    bool confirmFailed() const { return m_confirmFailed; }
    bool decodiumRecovery() const { return m_recoveryEnabled; }
    void setDecodiumRecovery(bool on);
    QString decodiumLogPath() const { return m_decodiumLogPath; }
    void setDecodiumLogPath(const QString& path);
    QString decodiumLogInUse() const;
    QString recoveryStatus() const { return m_recoveryStatus; }
    bool recoveryBusy() const { return m_recoveryRunning; }
    // A mano: i QSO degli ultimi `days` giorni che mancano (0 = tutto il file).
    Q_INVOKABLE void recoverFromDecodium(int days);
    QVariantList adifWatches() const;
    QVariantMap clublogCty() const;
    QVariantMap entityFixes() const { return m_entityFixesInfo; }
    // Scarica il cty.xml con la chiave API di Club Log.
    Q_INVOKABLE void updateClubLogCty();
    // Cerca i QSO che, con la data del QSO, sono di un'altra entita'.
    Q_INVOKABLE void checkEntitiesByDate();
    Q_INVOKABLE int applyEntityFixes();
    QVariantList adifWatchSuggestions() const;
    Q_INVOKABLE void addAdifWatch(const QString& path, const QString& label = {});
    Q_INVOKABLE void removeAdifWatch(const QString& path);
    Q_INVOKABLE void setAdifWatchEnabled(const QString& path, bool on);
    Q_INVOKABLE void checkAdifWatch(const QString& path, int days);
    void setConfirmAutoHours(int hours);
    QVariantMap confirmLastSync() const;
    // Scarica le conferme di "eqsl" o "qrz": solo le nuove dall'ultimo scarico,
    // o tutte con `full`.
    Q_INVOKABLE void syncConfirmations(const QString& service, bool full = false);
    // Le conferme (eQSL o QRZ) dei QSO fatti dal … al …, come per LoTW; una
    // delle due date si puo' lasciare vuota. Date ISO.
    Q_INVOKABLE void syncConfirmationsRange(const QString& service, const QString& fromIso, const QString& toIso);
    Q_INVOKABLE void cancelConfirmations()
    {
        m_confirmQueue.clear();
        m_confirmDownloader.cancel();
    }
    QVariantMap qslImport() const { return m_qslImport; }
    Q_INVOKABLE void clearQslImport();
    // Chiede la cartolina eQSL di una riga del riepilogo (una alla volta, e
    // non piu' di sei al minuto, come vuole eQSL). Torna la chiave con cui
    // arrivera' eqslCardReady.
    Q_INVOKABLE QString requestEqslCard(const QVariantMap& row);
    QString lotwStatus() const { return m_lotwStatus; }
    QString lotwLastSync() const;
    QString lotwCursor() const { return m_db.setting(QStringLiteral("lotw.last_qsl")); }
    int lotwAutoHours() const { return m_lotwAutoHours; }
    void setLotwAutoHours(int hours);
    // Scarica le conferme nuove (o tutte, `full`) e le segna sui QSO.
    Q_INVOKABLE void syncLotw(bool full = false);

    // I colori delle righe del log, come quelli dei decode di Decodium 4: per
    // ogni categoria (nuova entita', nuova zona, gia' lavorato…) il colore del
    // testo e, se si vuole, il fondo. logColorCategories per le impostazioni,
    // logColors per la tabella: {categoria: {fg, bg}} con solo quelli accesi.
    Q_PROPERTY(QVariantList logColorCategories READ logColorCategories NOTIFY logColorsChanged)
    Q_PROPERTY(QVariantMap logColors READ logColors NOTIFY logColorsChanged)
    QVariantList logColorCategories() const;
    QVariantMap logColors() const;
    // `what` e' "fg", "fgOn", "bg" o "bgOn".
    Q_INVOKABLE void setLogColor(const QString& category, const QString& what, const QVariant& value);
    Q_INVOKABLE void resetLogColors();
    // Le conferme dei QSO fatti dal … al … (ISO, "yyyy-MM-dd"; uno dei due puo'
    // mancare). Non sposta il segno dell'ultimo scarico: il prossimo "solo le
    // nuove" riparte da dove era.
    Q_INVOKABLE void syncLotwRange(const QString& fromIso, const QString& toIso);
    Q_INVOKABLE void cancelLotw() { m_lotw.cancel(); }

    double importProgress() const { return m_importProgress; }
    double bulkProgress() const { return m_bulkProgress; }
    QVariantMap duplicates() const { return m_duplicates; }
    bool backupEnabled() const { return m_backupEnabled; }
    void setBackupEnabled(bool enabled);
    QString backupDir() const { return m_backupDir; }
    void setBackupDir(const QString& dir);
    QString backupTime() const { return m_backupTime; }
    void setBackupTime(const QString& hhmm);
    int backupKeep() const { return m_backupKeep; }
    void setBackupKeep(int keep);
    QString lastBackup() const;
    QString lastBackupInfo() const;

    QString cloudServer() const { return m_cloudServer; }
    void setCloudServer(const QString& url);
    QString autoSync() const { return m_autoSync; }
    void setAutoSync(const QString& mode);
    QString conflictPolicy() const { return m_conflictPolicy; }
    void setConflictPolicy(const QString& policy);

    // Campi: call, date (yyyy-MM-dd), time (HH:mm), band, freq, mode, submode,
    // rst_sent, rst_rcvd, name, qth, gridsquare, tx_pwr, pota_ref, sota_ref,
    // iota, wwff_ref, comment. Restituisce un messaggio d'errore, o stringa vuota
    // se il QSO e' stato scritto.
    Q_INVOKABLE QString logManualQso(const QVariantMap& fields);
    QStringList newQsoVisibleFields() const;
    QVariantList newQsoCustomFields() const;
    Q_INVOKABLE QVariantList newQsoFieldOptions() const;
    // Salva la configurazione dopo aver validato i nomi ADIF. Stringa vuota =
    // successo; in caso contrario la UI mostra il motivo e non cambia nulla.
    Q_INVOKABLE QString saveNewQsoPanelSettings(const QStringList& visibleFields,
                                                const QVariantList& customFields);
    Q_INVOKABLE void resetNewQsoPanelSettings();
    Q_INVOKABLE QVariantMap utcNow() const;
    // Le date davanti all'operatore, nella forma della sua lingua (in
    // italiano 25/09/2026); dentro restano ISO. readDate capisce quello che
    // scrive, in quella forma o in ISO, e lo torna ISO (vuoto se non e' una
    // data).
    Q_INVOKABLE QString showDate(const QString& isoOrAdif) const;
    Q_INVOKABLE QString readDate(const QString& text) const;
    // Come si scrive una data, per il suggerimento nei campi ("gg/mm/aaaa").
    Q_PROPERTY(QString dateHint READ dateHint CONSTANT)
    QString dateHint() const;

    // ── Il VFO della barra in alto ───────────────────────────────────────────
    //
    // La frequenza scritta in cima non e' solo un numero da guardare: la
    // rotellina la muove, un clic la fa scrivere, e il modo si sceglie da un
    // elenco. Dove va a finire dipende da chi c'e': la radio, se il CAT e'
    // collegato, e Decodium, se DecoLink ha qualcuno dall'altra parte. Se non
    // c'e' nessuno dei due, si dice invece di far finta.
    Q_INVOKABLE void tuneTo(double mhz, const QString& mode = QString());
    // I modi da mettere nel menu: {name, cat, group}.
    Q_INVOKABLE QVariantList operatingModes() const;

    // Scheda di un QSO: campi ADIF, dati di sync, QSL, storico, effetto sugli award.
    Q_INVOKABLE QVariantMap qsoDetail(qint64 id) const;
    // `fields` e' la mappa ADIF completa (come in qsoDetail().fields).
    Q_INVOKABLE QString saveQso(qint64 id, const QVariantMap& fields, qint64 stationProfileId);
    Q_INVOKABLE bool deleteQso(qint64 id);
    Q_INVOKABLE int deleteQsos(const QVariantList& ids);
    // Rimanda QSO gia' nel log ai destinatari UDP configurati nel formato
    // N1MM/HamAward. Non riscrive il log e non innesca upload QSL.
    Q_INVOKABLE int resendQsosViaUdp(const QVariantList& ids);
    Q_INVOKABLE QString restoreRevision(qint64 id, qint64 historyId);

    // Etichette: aggiunge o toglie `tag` ai QSO indicati. Restituisce quanti sono cambiati.
    Q_INVOKABLE int tagQsos(const QVariantList& ids, const QString& tag, bool add);
    // Lo stesso valore in un campo dei QSO indicati, ognuno con la sua revisione
    // nello storico. I campi che si possono cambiare cosi':
    // [{field, label, choices: [{value, label}]}] (choices vuoto: testo libero).
    Q_INVOKABLE QVariantList bulkFields() const;
    Q_INVOKABLE void bulkEdit(const QVariantList& ids, const QString& field, const QString& value, bool onlyEmpty);
    // I doppioni del log, cercati su un altro filo; poi uniti gruppo per gruppo
    // ([{keep, ids}]): chi resta prende quello che manca, gli altri si cancellano.
    Q_INVOKABLE void findDuplicates(int windowMinutes);
    Q_INVOKABLE int mergeDuplicates(const QVariantList& groups);
    Q_INVOKABLE void clearDuplicates();
    // Le entita' presenti nel log per il filtro: [{dxcc, name, count}].
    Q_INVOKABLE QVariantList dxccInLog() const;
    Q_INVOKABLE QString dxccName(int dxcc) const { return m_countries.nameFor(dxcc); }
    // Il nome di uno stato USA o di una prefettura giapponese dalla sigla che
    // sta nel log; per il resto del mondo torna la sigla com'e'.
    Q_INVOKABLE QString subdivisionName(const QString& code, int dxcc) const;

    Q_INVOKABLE void importAdif(const QUrl& file);
    Q_INVOKABLE QVariantMap previewImport(const QUrl& file) const;
    Q_INVOKABLE bool undoLastImport();
    Q_INVOKABLE QVariantList qsoEnrichments(qint64 qsoId) const;
    Q_INVOKABLE bool undoQsoEnrichment(qint64 qsoId, const QString& field);
    Q_INVOKABLE void exportAdif(const QUrl& file);
    Q_INVOKABLE void exportQsos(const QVariantList& ids, const QUrl& file);
    // Correggere una cella della tabella del log sul posto (F2, menu della riga): le colonne che
    // si scrivono a mano — nominativo, rapporti, locatore, nome, QTH, commento. Per il resto
    // (data, banda, modo, frequenza) c'e' la scheda del QSO. Torna il motivo se non salva.
    Q_INVOKABLE bool canEditInline(const QString& columnKey) const;
    Q_INVOKABLE QString editQsoField(qint64 id, const QString& columnKey, const QString& value);
    // I QSO fatti dal … al … (ISO, UTC, estremi compresi; uno dei due puo' mancare):
    // quanti sono, e l'ADIF di quelli. Cosi' si scelgono come nello scarico da LoTW.
    Q_INVOKABLE int countQsoBetween(const QString& fromIso, const QString& toIso) const;
    Q_INVOKABLE void exportPeriod(const QString& fromIso, const QString& toIso, const QUrl& file);
    Q_INVOKABLE QString bandForFrequency(const QString& mhz) const;
    // Dove portare la radio quando si sceglie banda e modo (MHz), 0 se non si sa.
    Q_INVOKABLE double bandFrequency(const QString& band, const QString& mode) const;
    Q_INVOKABLE void backupNow();
    // Il ripristino. Le copie della cartella dei backup, dalla piu' recente:
    // {path, name, when, size, safety}.
    Q_INVOKABLE QVariantList backupFiles() const;
    // Guarda dentro una copia su un altro filo; la risposta arriva con
    // backupInspected(): {path, ok, problem, qsos, first, last, size, diff}.
    Q_INVOKABLE void inspectBackup(const QString& pathOrUrl);
    // Il log di adesso, per il confronto: {path, name, qsos, last}.
    Q_INVOKABLE QVariantMap currentLogInfo() const;
    // Riapre il programma con --restore-from: la copia si rimette all'avvio, a
    // log chiuso. Torna un errore, o non torna.
    Q_INVOKABLE QString restoreBackup(const QString& pathOrUrl);
    // All'avvio, dopo un ripristino: com'e' andata, nel registro attivita'.
    void reportRestore(const core::logbackup::RestoreResult& result, const QString& backup);
    // Completa DXCC, paese, zone e continente dei QSO che non li hanno. Ogni QSO
    // modificato diventa una nuova revisione. Restituisce quanti ne ha completati.
    Q_INVOKABLE int fillMissingDxcc();
    // Carica un cty.csv scelto dall'operatore e lo copia nella cartella dei dati.
    Q_INVOKABLE QString installCountries(const QUrl& file);
    Q_INVOKABLE void clearActivity();
    Q_INVOKABLE void openDatabaseFolder() const;
    Q_INVOKABLE QString localPath(const QUrl& url) const { return url.toLocalFile(); }
    Q_INVOKABLE QString importQslAttachment(const QVariantList& qsoIds, const QUrl& file);
    Q_INVOKABLE QString saveQslAttachment(qint64 attachmentId, const QUrl& file) const;
    Q_INVOKABLE QVariantMap qslManagerSuggestion(qint64 qsoId) const;
    Q_INVOKABLE QVariantList integrationDiagnostics() const;
    Q_INVOKABLE QString diagnosticSupportText() const;
    Q_INVOKABLE QString diagnosticSupportJson() const;
    Q_INVOKABLE QString saveDiagnosticReport(const QUrl& file) const;
    Q_INVOKABLE QString saveDiagnosticJsonReport(const QUrl& file) const;
    // Misura locale, senza telemetria: la UI registra solo durate aggregate
    // per poter documentare p50/p95 durante un collaudo esplicito.
    Q_INVOKABLE void recordUiLatency(const QString& scenario, qint64 elapsedMs);
    // Per le prove: un evento del mouse vero ("press", "move", "release") nel
    // punto x, y della finestra, come se l'avesse fatto l'operatore. Serve a
    // provare che un clic arrivi davvero al pulsante giusto.
    Q_INVOKABLE void testPointer(QObject* window, const QString& kind, qreal x, qreal y);

signals:
    void udpChanged();
    void clientChanged();
    void logChanged();
    // Un momento dopo l'ultimo QSO: le statistiche di tutto il log sono
    // da rileggere.
    void statsChanged();
    void incomingChanged();
    void activityChanged();
    void countsChanged();
    void lookupChanged();
    // Un DX scelto altrove — per ora dal cluster — da mettere nel riquadro del
    // QSO nuovo: call, mhz, mode, grid.
    void qsoPrepared(const QVariantMap& fields);
    // Frequenza o modo mostrati in cima: cambiati.
    void tuningChanged();
    void stationChanged();
    void backupChanged();
    void importChanged();
    void bulkChanged();
    void duplicatesChanged();
    void backupInspected(const QVariantMap& info);
    void statsReady(const QVariantMap& stats);
    void cloudChanged();
    void countriesChanged();
    void callbookChanged();
    void awardsChanged();
    void decoLinkChanged();
    void hamHubLinkChanged();
    void uiLanguageChanged();
    void newQsoSettingsChanged();
    void logColorsChanged();
    void lotwChanged();
    void qslImportChanged();
    // Uno scarico chiesto a mano ha portato qualcosa: la finestra si apre.
    void qslImportReady();
    // La cartolina eQSL di una riga: il file, o l'errore.
    void eqslCardReady(const QString& key, const QString& fileUrl, const QString& error);
    void confirmChanged();
    void ctyChanged();
    void recoveryChanged();

private:
    core::InsertResult onQsoReceived(const core::AdifRecord& record, const QString& source, const QString& sourceApp);
    core::HttpResponse handleApi(const core::HttpRequest& request);
    void addActivity(const QString& category, const QString& text, const QString& level = QStringLiteral("info"));

    // Dove si e' adesso: frequenza, banda, modo, TX. Va al Cloud perche' lo si
    // veda anche da lontano; il Cloud decide ogni quanto mandarlo davvero.
    Q_SLOT void reportPresenceToCloud();
    // Il QSO e' scritto, ma nudo: Decodium manda nominativo, rapporto, banda e
    // modo, non il nome di chi c'era dall'altra parte. Il callbook lo sa, e
    // quello che sa finisce nel QSO — solo nei campi vuoti, perche' quello che
    // ha scritto l'operatore non si tocca.
    void completeFromCallbook(qint64 id, const QString& call);
    // Contatto N1MM XML per HamConnect/HamAward: parte soltanto quando il QSO
    // viene registrato qui, non per i datagrammi WSJT-X gia' inoltrati.
    int broadcastN1mmQso(const core::AdifRecord& record, qint64 id, bool report = true);
    // Il QSO appena scritto, ai programmi che lo prendono su DecoPort (HH).
    void hamHubQso(const core::AdifRecord& record, qint64 id);
    // Applica al QSO quello che il callbook ha detto. Torna i campi riempiti.
    QStringList applyCallbookToQso(qint64 id, const QVariantMap& record);
    void refreshCallInfo();
    void maybeCreateProfileFromDecodium();
    void checkBackupSchedule();
    // Aggiunge al record i dati del profilo che il record non ha gia'.
    void applyProfile(core::AdifRecord& record, qint64 profileId) const;
    // Aggiunge DXCC, COUNTRY, CQZ, ITUZ e CONT se mancano. true se ha aggiunto qualcosa.
    bool applyEntity(core::AdifRecord& record) const;
    void loadCountries();
    void onLotwReport(const core::lotw::Report& report);
    void onConfirmationReport(const core::confirmations::Report& report);
    // Le conferme di un servizio segnate sul log, in una transazione: quante
    // nuove, gia' segnate, non trovate (le prime dieci), senza dati.
    struct ConfirmTally {
        int confirmed{0};
        int already{0};
        int ambiguous{0};
        int notFound{0};
        int invalid{0};
        QStringList missing;
        QVariantList rows;     // per il riepilogo: le nuove, le non trovate, le sbagliate
    };
    ConfirmTally applyConfirmations(const QString& service, const QList<core::AdifRecord>& list,
                                    const QList<qint64>& onlyProfiles = {}, const QList<qint64>& exceptProfiles = {},
                                    const QString& label = {}, const QString& account = {});
    // Mette uno scarico nel riepilogo (al posto del precedente dello stesso
    // servizio/account) e lo salva nel log.
    void recordQslImport(const QString& label, const QString& service, const ConfirmTally& t, bool manual,
                         const QString& error = {});
    QVariantMap m_qslImport;
    core::EqslCardFetcher m_eqslCards;
    // Un account da cui scaricare le conferme: quello generale (per i QSO dei
    // profili senza un account loro) o quello di un profilo (solo i suoi QSO).
    struct ConfirmAccount {
        QString service;      // "eqsl", "qrz"
        QString credential;   // "eqsl", "eqsl@3", "qrzlogbook@3"
        QString key;          // prefisso nelle impostazioni del log: "eqsl", "eqsl@3"
        QString label;        // "eQSL", "eQSL IU8LMC/P"
        QList<qint64> only;
        QList<qint64> except;
    };
    QList<ConfirmAccount> confirmAccounts(const QString& service) const;
    void startNextConfirmAccount();
    // Dopo: la tabella, i diplomi, Decodium, e i DXCC confermati nuovi.
    void confirmationsApplied(const QString& category, const QSet<QString>& dxccBefore,
                              const QSet<QString>& ft2Before);
    void checkLotwSchedule();
    void checkConfirmSchedule();
    // Da dove si recuperano QSO: il log di Decodium o un altro log ADIF.
    struct RecoverySource {
        QString path;
        QString label;        // "Decodium", "fldigi"...
        QString untilKey;     // fin dove si e' guardato, nelle impostazioni
        QString source;       // colonna qso.source: decodium_adif, adif_watch
        QString category;     // nel registro attivita'
        bool decodium{false};
    };
    struct RecoveryJob {
        RecoverySource source;
        QDateTime from;
        QDateTime to;
        bool manual{false};
    };
    RecoverySource decodiumSource() const;
    QList<RecoverySource> watchSources(bool enabledOnly) const;
    QDateTime recoveryFrom(const RecoverySource& source, const QDateTime& to) const;
    void checkDecodiumRecovery();
    void runNextRecovery();
    void startRecovery(const RecoveryJob& job);
    void finishRecovery(const RecoveryJob& job, const core::decodiumlog::Tail& tail,
                        const QList<core::AdifRecord>& missing);
    enum class Recovered { Saved, Known, ActivationDuplicate, Failed };
    Recovered saveRecoveredQso(const core::AdifRecord& record, bool bulk, const RecoverySource& source);
    QSet<QString> confirmedAwardKeys(const QString& awardId) const;
    void requestCallbook();
    const QList<core::AwardResult>& awardResults() const;
    // Gli stessi award senza i filtri di banda, modo, profilo ed etichetta (solo le
    // conferme scelte): quello che vede Decodium e che decide "nuovo confermato".
    const QList<core::AwardResult>& globalAwardResults() const;
    void awardFilterChanged();
    QJsonObject decoLinkAward() const;
    QJsonArray decoLinkQuery(const QJsonObject& query) const;
    void decoLinkQso(const core::AdifRecord& record, const QString& status, qint64 id,
                     const QString& source, const QString& app, const QString& message = {});

    core::LogDatabase m_db;
    core::Countries   m_countries;
    core::CredentialStore* m_credentials{nullptr};
    core::CallbookClient m_callbook;
    core::AwardFilter m_awardFilter;
    core::DecoLinkServer m_decoLink;
    core::DecoPortLogServer m_hamHubLink;
    bool      m_hamHubLinkEnabled{true};
    int       m_hamHubLinkPort{core::decoportlog::kDefaultSessionPort};
    bool      m_hamHubLinkHasPassword{false};
    QString   m_hamHubLinkNote;
    core::LotwClient  m_lotw;
    // Il periodo chiesto per il prossimo scarico LoTW, e se quello in corso e' per periodo.
    QDate m_lotwFrom;
    QDate m_lotwTo;
    bool m_lotwRange{false};
    bool      m_lotwStarting{false};
    core::ConfirmationDownloader m_confirmDownloader;
    QString m_confirmStarting;     // il servizio mentre si legge la password
    QString m_confirmService;      // quello che sta scaricando
    QString m_confirmStatus;
    bool m_confirmAuto{false};     // lo scarico in corso l'ha fatto partire l'orario
    QList<ConfirmAccount> m_confirmQueue;   // gli account che aspettano il loro turno
    ConfirmAccount m_confirmAccount;        // quello che sta scaricando
    bool m_confirmFull{false};
    // Il periodo dello scarico in corso (vuoti: lo scarico di sempre).
    QDate m_confirmFrom;
    QDate m_confirmTo;
    void startConfirmations(const QString& service, bool full, const QDate& from, const QDate& to);
    bool m_confirmFailed{false};   // l'ultimo stato e' un errore
    bool m_recoveryEnabled{true};
    QString m_decodiumLogPath;     // scelto a mano; vuoto = quello di Decodium
    QString m_recoveryStatus;
    bool m_recoveryRunning{false};
    core::ClubLogCty m_ctyXml;
    QString m_ctyXmlStatus;
    bool m_ctyXmlBusy{false};
    struct EntityFix {
        qint64 id{0};
        int dxcc{0};
        QString country;
        int cqz{0};
        QString cont;
    };
    QList<EntityFix> m_entityFixes;
    QVariantMap m_entityFixesInfo;
    QThreadPool m_ctyPool;
    void loadClubLogCty();
    QString clublogCtyPath() const;
    core::AwardCalculator awardCalculator(const QHash<int, QString>& names) const;
    QList<RecoveryJob> m_recoveryQueue;
    QHash<QString, QString> m_recoveryStatuses;   // file tenuto d'occhio → ultimo esito
    mutable QHash<QString, QList<qint64>> m_uiLatencySamples;
    QTimer m_recoveryTimer;
    QThreadPool m_recoveryPool;
    int m_confirmAutoHours{12};
    bool      m_lotwAuto{false};
    QString   m_lotwStatus;
    int       m_lotwAutoHours{12};
    QTimer    m_lotwTimer;
    bool      m_decoLinkEnabled{true};
    int       m_decoLinkPort{core::DecoLinkServer::kDefaultPort};
    QTimer    m_decoLinkAwardDebounce;
    // Le statistiche di tutto il log si rifanno dopo l'ultimo QSO, non a ogni
    // QSO: in gara ogni QSO le rifaceva decine di volte, e il programma si
    // fermava per secondi mentre la stazione aspettava.
    QTimer    m_statsDebounce;
    mutable QHash<QString, QVariant> m_statsCache;
    // Il calcolo di diplomi e statistiche gira qui, con una connessione sua al
    // log (il log e' in WAL: si legge mentre si scrive). Un thread solo: due
    // calcoli insieme non servono, conta l'ultimo.
    QThreadPool m_statsPool;
    quint64 m_statsGeneration{0};
    void refreshStatsInBackground();
    // La spia dei blocchi.
    QTimer        m_freezeBeat;
    QElapsedTimer m_freezeClock;
    qint64        m_lastBeat{0};
    int           m_freezeCount{0};
    mutable QList<core::AwardResult> m_awardCache;
    mutable bool m_awardsDirty{true};
    mutable QList<core::AwardResult> m_globalAwardCache;
    mutable bool m_globalAwardsDirty{true};
    bool      m_callbookAutofill{true};
    bool      m_callbookComplete{true};
    bool      m_callbookFallback{true};
    // La coda dei QSO da completare, servita a un tot per volta.
    QList<qint64> m_callbookQueue;
    QTimer    m_callbookQueueTimer;
    int       m_callbookQueueDone{0};
    int       m_callbookQueueTotal{0};
    void      serveCallbookQueue();
    int       enqueueCallbook(const QList<qint64>& ids);
    QString   m_callbookStatus;
    QString   m_callbookPending;
    QTimer    m_callbookDebounce;
    QHash<QString, QVariantMap> m_callbookResults;   // per nominativo, sessione corrente
    QHash<QString, QString> m_callbookErrors;
    // I QSO che aspettano una risposta del callbook per completarsi: per
    // nominativo, perche' due QSO con lo stesso corrispondente si accontentano
    // di una ricerca sola.
    QHash<QString, QList<qint64>> m_awaitingCallbook;
    // Chi aspetta di sapere l'email di un nominativo: si risponde a tutti
    // insieme quando il callbook risponde, una ricerca sola per nominativo.
    QHash<QString, QList<std::function<void(const QString&, const QString&)>>> m_awaitingEmail;
    QString           m_countriesSource;
    mutable QVariantList m_coastline;
    mutable QVariantList m_land;
    mutable QVariantList m_gridPointsCache;
    mutable bool m_gridPointsValid{false};
    core::UdpReceiver m_udp;
    QsoTableModel*    m_model{nullptr};
    StationProfileModel* m_profiles{nullptr};
    ClusterController*   m_cluster{nullptr};
    QslController*       m_qsl{nullptr};
    QslCardController*   m_cards{nullptr};
    SolarController*     m_solar{nullptr};
    UpdateController*    m_updates{nullptr};
    RotorController*     m_rotor{nullptr};
    WorldClockController* m_worldClock{nullptr};
    ChatController*      m_chat{nullptr};
    SuperCheckController* m_scp{nullptr};
    CallListController* m_callLists{nullptr};
    NetController*       m_net{nullptr};
    So2rController*      m_so2r{nullptr};
    VoiceKeyerController* m_dvk{nullptr};
    RigController*       m_rig{nullptr};
    CloudController*     m_cloud{nullptr};
    ActivationController* m_activation{nullptr};
    LogLibrary* m_logs{nullptr};

    int       m_udpPort{2237};
    QString   m_udpForward;
    core::N1mmReceiver m_n1mm;
    int       m_n1mmPort{0};
    core::LocalApiServer m_api;
    int       m_apiPort{0};
    QString   m_apiToken;
    void onN1mmReplaced(const core::AdifRecord& record, const QString& id);
    void onN1mmDeleted(const QString& id, const core::AdifRecord& record);
    QString   m_udpForwardError;
    QString   m_multicast;
    bool      m_followDx{true};
    QString   m_clientName;
    QString   m_clientVersion;
    QDateTime m_clientLastSeen;
    core::wsjtx::Status m_status;
    QTimer    m_clientWatch;

    QVariantList m_incoming;
    ActivityModel* m_activityModel{nullptr};
    TrafficMonitor* m_traffic{nullptr};
    DecodeFeed* m_decodeFeed{nullptr};
    // I conteggi del log, rifatti una volta dopo ogni cambiamento.
    struct Counts {
        bool valid{false};
        int qsos{0};
        int dirty{0};
        int conflicts{0};
        int missingDxcc{0};
        QVariantList qslSummary;
    };
    mutable Counts m_counts;
    QTimer m_countsTimer;
    QThreadPool m_countsPool;
    QThreadPool m_backupPool;
    QThreadPool m_linkPool;
    QThreadPool m_importPool;
    QThreadPool m_statsViewPool;
    // L'ultima richiesta di statistiche: le altre, ancora in coda, si saltano.
    std::shared_ptr<std::atomic<int>> m_statsLatest{std::make_shared<std::atomic<int>>(0)};
    double m_importProgress{-1};
    QList<qint64> m_lastImportIds;
    void finishImport(const QString& path, const core::ImportResult& result);
    double m_bulkProgress{-1};
    void finishBulk(const QString& field, const QString& value, const core::BulkEditResult& result);
    QVariantMap m_duplicates;

    // I conti dell'entita' per la scheda del nominativo (quante volte, bande,
    // modi, caselle con le conferme): per un'entita' comune su un log grande
    // sono decine di migliaia di righe — la Germania su un milione di QSO,
    // mezzo secondo a ogni nominativo DL. Su un log in file si fanno su un
    // altro filo; la scheda mostra l'ultimo conto, o aspetta il primo.
    struct EntitySummary {
        quint64 version{0};
        core::LogDatabase::DxccWorked worked;
        QList<core::LogDatabase::BandModeSlot> cells;
    };
    QHash<int, EntitySummary> m_entitySummaries;
    QSet<int> m_entityCounting;
    // Sale a ogni cambio del log: un conto fatto prima e' vecchio.
    quint64 m_logVersion{1};
    QThreadPool m_callInfoPool;
    std::optional<EntitySummary> entitySummary(int dxcc);
    // Esportare su un altro filo: su un log da un milione sono decine di secondi.
    // `period`: dal … al … al posto degli id, scelti dal filo che scrive.
    // Salva un QSO corretto: la scheda e la tabella passano da qui. Se il nominativo cambia,
    // nazione e zone si rifanno dal nuovo, e i servizi che avevano il vecchio lo rimandano.
    QString saveRecord(qint64 id, core::AdifRecord record, qint64 stationProfileId);
    void exportInBackground(const QList<qint64>& ids, bool all, const QString& path,
                            const std::optional<std::pair<QString, QString>>& period = std::nullopt);
    bool m_backupRunning{false};
    void finishBackup(const QString& path, const QString& error);
    void ensureCounts() const;
    // Il log sta in un file e si puo' leggere da un altro filo (non ":memory:").
    bool backgroundReady() const;
    // Diplomi, FT2, bande e modi da ricontare su un altro filo, una volta sola
    // anche se li chiedono dieci getter di seguito.
    void scheduleStatsRefresh() const;
    mutable bool m_statsRefreshScheduled{false};
    QString      m_lookupCall;
    QVariantMap  m_callInfo;

    bool    m_backupEnabled{true};
    QString m_backupDir;
    QString m_backupTime{QStringLiteral("02:00")};
    int     m_backupKeep{14};
    QTimer  m_backupTimer;

    QString m_cloudServer;
    QString m_autoSync;
    QString m_conflictPolicy;
    bool m_shuttingDown{false};
};

} // namespace decolog::app
