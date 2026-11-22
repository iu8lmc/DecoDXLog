// DecoDXLog — il DX cluster: fonti, spot arricchiti dal log, filtri, avvisi e voce.
//
// Tutte le fonti (nodi telnet, RBN, HamAlert, POTA) finiscono in una lista sola.
// Ogni spot si confronta col log (nuovo DXCC, nuova banda, gia' lavorato...), si
// filtra, e le regole d'avviso possono annunciarlo a voce o mandarlo a
// Decodium con DecoLink. Un doppio clic sintonizza
// Decodium sullo spot.
#pragma once

#include "app/SpotModel.h"
#include "core/ClusterConnection.h"
#include "core/Countries.h"
#include "core/Spots.h"
#include "core/DxAssistant.h"
#include "core/VoiceAnnouncer.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QSoundEffect>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

class QNetworkAccessManager;

namespace decolog::core {
class CredentialStore;
class DecoLinkServer;
class LogDatabase;
}

namespace decolog::app {

class ClusterController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* spots READ spots CONSTANT)
    Q_PROPERTY(QVariantList sources READ sources NOTIFY sourcesChanged)
    Q_PROPERTY(QVariantList presets READ presets CONSTANT)
    Q_PROPERTY(int onlineCount READ onlineCount NOTIFY sourcesChanged)
    Q_PROPERTY(QVariantMap filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(bool followDecodiumBand READ followDecodiumBand WRITE setFollowDecodiumBand NOTIFY filterChanged)
    Q_PROPERTY(QVariantMap savedFilters READ savedFilters NOTIFY filterChanged)
    Q_PROPERTY(QVariantList alertRules READ alertRules NOTIFY rulesChanged)
    Q_PROPERTY(bool logSpotAlerts READ logSpotAlerts WRITE setLogSpotAlerts NOTIFY settingsChanged)
    Q_PROPERTY(QStringList console READ console NOTIFY consoleChanged)
    Q_PROPERTY(QVariantList statusNames READ statusNames CONSTANT)
    Q_PROPERTY(bool sendToDecodium READ sendToDecodium WRITE setSendToDecodium NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap assistantPreferences READ assistantPreferences WRITE setAssistantPreferences NOTIFY settingsChanged)

    Q_PROPERTY(bool voiceAvailable READ voiceAvailable CONSTANT)
    Q_PROPERTY(QString voiceBackend READ voiceBackend CONSTANT)
    Q_PROPERTY(QStringList voices READ voices CONSTANT)
    Q_PROPERTY(bool voiceEnabled READ voiceEnabled WRITE setVoiceEnabled NOTIFY settingsChanged)
    Q_PROPERTY(QString voiceName READ voiceName WRITE setVoiceName NOTIFY settingsChanged)
    Q_PROPERTY(int voiceRate READ voiceRate WRITE setVoiceRate NOTIFY settingsChanged)
    Q_PROPERTY(int voiceVolume READ voiceVolume WRITE setVoiceVolume NOTIFY settingsChanged)
    Q_PROPERTY(bool voicePhonetic READ voicePhonetic WRITE setVoicePhonetic NOTIFY settingsChanged)
    Q_PROPERTY(QString voiceLanguage READ voiceLanguage WRITE setVoiceLanguage NOTIFY settingsChanged)
    Q_PROPERTY(int voiceCooldownMinutes READ voiceCooldownMinutes WRITE setVoiceCooldownMinutes NOTIFY settingsChanged)

    Q_PROPERTY(int lotwUsers READ lotwUsers NOTIFY lotwUsersChanged)
    Q_PROPERTY(QString lotwUsersInfo READ lotwUsersInfo NOTIFY lotwUsersChanged)
    Q_PROPERTY(int lastAlertCount READ lastAlertCount NOTIFY alertRaised)
    // Cresce a ogni spot arrivato o ricalcolato: la band map si rinfresca.
    Q_PROPERTY(int spotRevision READ spotRevision NOTIFY spotsUpdated)

public:
    struct Context {
        core::LogDatabase* db{nullptr};
        const core::Countries* countries{nullptr};
        core::CredentialStore* credentials{nullptr};
        core::DecoLinkServer* decoLink{nullptr};
        std::function<QString()> stationCall;                 // nominativo del profilo attivo
        std::function<QString()> stationGrid;                 // locatore della stazione
        std::function<QString()> decodiumBand;                // banda su cui e' Decodium, se collegato
        std::function<void(bool& lotw, bool& card, bool& eqsl)> confirmations;
        std::function<void(const QString& category, const QString& text, const QString& level)> activity;
        std::function<void(const QString& call)> lookup;
        // Porta la radio sulla frequenza e sul modo dello spot. Torna false se
        // la radio non c'e': allora si prova comunque con Decodium.
        std::function<bool(double mhz, const QString& mode)> tuneRadio;
        // Mette il DX nel riquadro del QSO nuovo: nominativo, banda, modo,
        // frequenza. Da li' il callbook riempie nome, QTH e locatore.
        std::function<void(const QVariantMap& fields)> prepareQso;
        // Ogni spot arrivato, gia' con entita' e posizione: il gateway del
        // rotore lo mette sulla mappa dell'app.
        std::function<void(const core::EnrichedSpot& spot)> spotSeen;
        // Previsione per uno spot gia' osservato: sola lettura, nessun CAT/TX.
        std::function<QVariantMap(const QString& grid, const QString& band,
                                   const QString& mode)> propagationForecast;
    };

    explicit ClusterController(Context context, QObject* parent = nullptr);
    ~ClusterController() override;

    // Da chiamare quando il database e' aperto: carica le fonti e si collega.
    void start();
    // Il log e' cambiato: stati degli spot da ricalcolare (con calma).
    void logChanged();
    void decodiumBandChanged();
    // Una riga come se arrivasse da un nodo (prove e demo).
    Q_INVOKABLE void injectLine(const QString& line);
    // Silenzio per questa sessione, senza cambiare l'impostazione (schermate di prova).
    void setMuted(bool muted) { m_muted = muted; }

    // Lo stato di un nominativo sentito in aria (Decodium, WSJT-X) rispetto al
    // log, con la stessa regola degli spot: entita' nuova, banda nuova,
    // nominativo nuovo, gia' lavorato… Serve al pannello delle decodifiche.
    struct CallClass {
        int status{0};
        QString entity;
        int dxcc{0};
        QString continent;
        int azimuth{-1};
        double distanceKm{-1.0};
    };
    CallClass classifyCall(const QString& call, const QString& band, const QString& mode) const;
    static QString statusText(int status);

    QObject* spots() { return &m_model; }
    QVariantList sources() const;
    QVariantList presets() const;
    int onlineCount() const;
    QVariantMap filter() const { return m_filter.toMap(); }
    void setFilter(const QVariantMap& map);
    bool followDecodiumBand() const { return m_followBand; }
    void setFollowDecodiumBand(bool on);
    QVariantMap savedFilters() const { return m_savedFilters; }
    QVariantList alertRules() const;
    QStringList console() const { return m_console; }
    QVariantList statusNames() const;
    bool sendToDecodium() const { return m_sendToDecodium; }
    void setSendToDecodium(bool on);
    bool logSpotAlerts() const { return m_logSpotAlerts; }
    void setLogSpotAlerts(bool on);
    QVariantMap assistantPreferences() const;
    void setAssistantPreferences(const QVariantMap& preferences);

    bool voiceAvailable() const { return m_voice.available(); }
    QString voiceBackend() const { return m_voice.backend(); }
    QStringList voices() const { return m_voice.voices(); }
    bool voiceEnabled() const { return m_voiceEnabled; }
    void setVoiceEnabled(bool on);
    QString voiceName() const { return m_voiceName; }
    void setVoiceName(const QString& name);
    int voiceRate() const { return m_voiceRate; }
    void setVoiceRate(int rate);
    int voiceVolume() const { return m_voiceVolume; }
    void setVoiceVolume(int volume);
    bool voicePhonetic() const { return m_voicePhonetic; }
    void setVoicePhonetic(bool on);
    QString voiceLanguage() const { return m_voiceLanguage; }
    void setVoiceLanguage(const QString& language);
    int voiceCooldownMinutes() const { return m_voiceCooldown; }
    void setVoiceCooldownMinutes(int minutes);

    int lotwUsers() const { return m_lotwUsers.size(); }
    QString lotwUsersInfo() const { return m_lotwUsersInfo; }
    int lastAlertCount() const { return m_alertCount; }
    int spotRevision() const { return m_spotRevision; }

    // Fonti.
    Q_INVOKABLE QString addSource(const QVariantMap& source);
    Q_INVOKABLE void updateSource(const QString& id, const QVariantMap& source);
    Q_INVOKABLE void removeSource(const QString& id);
    Q_INVOKABLE void setSourceEnabled(const QString& id, bool enabled);
    // Le password dei nodi telnet non finiscono mai nelle impostazioni della
    // fonte: stanno nel portachiavi, una per id della fonte.
    Q_INVOKABLE void saveSourcePassword(const QString& id, const QString& password);
    Q_INVOKABLE void removeSourcePassword(const QString& id);
    // «Perche' non si collega?»: il nome, la porta, la prima parola del nodo. Il
    // risultato torna nella fonte (checkText, checking) e dice dove si ferma.
    Q_INVOKABLE void checkSource(const QString& id);
    Q_INVOKABLE QString addPreset(int index);
    // Un comando a una fonte (id vuoto = la prima fonte telnet collegata).
    Q_INVOKABLE QString sendCommand(const QString& sourceId, const QString& command);
    // Manda uno spot al cluster: "DX <freq> <call> <commento>", al nodo scelto (id vuoto =
    // il primo collegato). Torna il motivo se non parte, vuoto se e' partito.
    Q_INVOKABLE QString postSpot(const QString& call, const QString& freqKhz, const QString& comment,
                                 const QString& sourceId = {});
    // Prima di mandare: nominativo che somiglia a un nominativo, frequenza in kHz (sotto 100 si
    // legge come MHz) dentro le bande amatoriali, commento al massimo di 30 caratteri — il
    // limite dei nodi —, e se qualcuno ha gia' segnalato la stessa stazione da poco. Torna
    // {ok, error, call, freqKhz, comment, band, duplicate}.
    Q_INVOKABLE QVariantMap checkSpot(const QString& call, const QString& freqKhz, const QString& comment) const;

    // Spot.
    Q_INVOKABLE void tune(const QString& spotKey);
    Q_INVOKABLE void lookupSpot(const QString& spotKey);
    Q_INVOKABLE void clearSpots();
    // Gli spot mostrati, con la posizione, per la mappa.
    Q_INVOKABLE QVariantList mapSpots() const;
    // Ranking descrittivo degli spot attuali. Non sintonizza e non trasmette.
    Q_INVOKABLE QVariantList assistantRecommendations() const;
    // La band map di una banda: {band, lowKhz, highKhz, spots: [{key, call, freqKhz,
    // mode, status, statusLabel, ageMinutes, count, entity, comment}]}. Con
    // `useFilter` passano solo gli spot che il filtro del cluster lascia vedere
    // (bande a parte: la banda e' quella chiesta).
    Q_INVOKABLE QVariantMap bandMap(const QString& band, bool useFilter) const;
    // La banda di una frequenza (kHz), per la band map che segue la radio.
    Q_INVOKABLE QString bandOf(double khz) const;
    Q_INVOKABLE void clearConsole();

    // Filtri salvati.
    Q_INVOKABLE void saveFilter(const QString& name);
    Q_INVOKABLE void applySavedFilter(const QString& name);
    Q_INVOKABLE void deleteSavedFilter(const QString& name);

    // Regole d'avviso: {id, name, enabled, voice, decodium, filter}.
    Q_INVOKABLE QString saveAlertRule(const QVariantMap& rule);
    Q_INVOKABLE void removeAlertRule(const QString& id);

    Q_INVOKABLE void testVoice();
    Q_INVOKABLE void stopVoice() { m_voice.stop(); }
    Q_INVOKABLE void refreshLotwUsers();

signals:
    void sourcesChanged();
    void filterChanged();
    void rulesChanged();
    void consoleChanged();
    void settingsChanged();
    void lotwUsersChanged();
    void spotsUpdated();
    // L'elenco di chi e' gia' stato lavorato e' stato rifatto: gli stati
    // calcolati prima non valgono piu'.
    void indexRebuilt();
    // Uno spot ha fatto scattare una regola: per il QML (lampeggio, notifica).
    void alertRaised(const QString& title, const QString& text);
    // Lo stesso, quando la regola chiede il riquadro o il suono.
    void alertNotify(const QString& title, const QString& text, const QString& spotKey, bool popup, bool sound);
    // L'operatore ha scelto uno spot (clic o doppio clic): in gara il
    // nominativo, la banda e il modo vanno nell'inserimento veloce.
    void spotPicked(const QString& call, const QString& band, const QString& mode, double freqKhz);
    // Lo stesso spot, con dove sta: il rotore ne prende la rotta.
    void spotAimed(const QString& call, bool hasPosition, double lat, double lon, int azimuth);

private:
    struct AlertRule {
        QString id;
        QString name;
        bool enabled{true};
        bool voice{true};
        bool decodium{true};
        bool popup{false};              // un riquadro sullo schermo
        bool sound{false};              // un suono
        core::SpotFilter filter;
        QVariantMap toMap() const;
        static AlertRule fromMap(const QVariantMap& map);
    };

    void onSpot(const core::Spot& spot);
    core::EnrichedSpot enrich(const core::Spot& spot) const;
    int statusOf(const core::EnrichedSpot& e) const;
    void checkRules(const core::EnrichedSpot& e, bool isNew);
    QString announcement(const core::EnrichedSpot& e) const;
    void sendSpotToDecodium(const core::EnrichedSpot& e, bool alert);
    void addConsole(const QString& source, const QString& line);
    core::ClusterConnection* connectionFor(const QString& id) const;
    void createConnection(const core::ClusterSource& source);
    void applyFilterToModel();
    void rebuildIndex();
    void saveSources() const;
    void saveRules() const;
    void loadLotwUsers();

    Context m_ctx;
    SpotModel m_model;
    core::LogIndex m_index;
    QThreadPool m_indexPool;
    quint64 m_indexGeneration{0};
    void rebuildIndexInBackground();
    core::LotwUsers m_lotwUsers;
    QString m_lotwUsersInfo;
    core::VoiceAnnouncer m_voice;
    QSoundEffect m_alertSound;
    int m_spotRevision{0};
    QList<core::ClusterConnection*> m_connections;
    QHash<QString, QString> m_checkText;   // l'ultimo controllo di ogni fonte
    QHash<QString, QString> m_passwordStatus;
    QSet<QString> m_restartAfterPasswordSave;
    QSet<QString> m_checking;
    core::SpotFilter m_filter;
    bool m_followBand{false};
    QVariantMap m_savedFilters;
    QList<AlertRule> m_rules;
    QStringList m_console;
    QHash<QString, QDateTime> m_announced;
    QTimer m_indexDebounce;
    QNetworkAccessManager* m_net{nullptr};
    bool m_started{false};
    bool m_sendToDecodium{true};
    bool m_logSpotAlerts{false};
    core::dxassistant::Preferences m_assistantPreferences;
    bool m_voiceEnabled{true};
    QString m_voiceName;
    int m_voiceRate{0};
    int m_voiceVolume{80};
    bool m_voicePhonetic{false};
    QString m_voiceLanguage{QStringLiteral("it")};
    int m_voiceCooldown{20};
    int m_alertCount{0};
    bool m_muted{false};
};

} // namespace decolog::app
