// DecoDXLog — il rotore: dove guarda l'antenna e dove mandarla.
//
// Tre strade: il gateway integrato (DecoDXLog stesso apre la seriale del
// control box PRO.SIS.TEL e fa da DecoRotor per l'app e per gli altri
// programmi), DecoRotor a parte (WebSocket), o un rotctld qualsiasi. Quello che aggiunge e' il contesto che ha solo lui — la
// rotta di uno spot del cluster, del nominativo che si sta lavorando, del QSO
// aperto — e la possibilita' di seguirlo da solo.
#pragma once

#include "core/RotorGateway.h"
#include "core/RotorLink.h"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

namespace decolog::app {

class RotorController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(QString backend READ backend WRITE setBackend NOTIFY changed)
    Q_PROPERTY(QString host READ host WRITE setHost NOTIFY changed)
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY changed)
    Q_PROPERTY(bool followDx READ followDx WRITE setFollowDx NOTIFY changed)
    Q_PROPERTY(int beamwidth READ beamwidth WRITE setBeamwidth NOTIFY changed)
    Q_PROPERTY(QVariantMap state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(QString lastTarget READ lastTarget NOTIFY stateChanged)
    // Quello che serve al quadrante completo, come nel posto di comando.
    Q_PROPERTY(QVariantList presets READ presets NOTIFY presetsChanged)
    Q_PROPERTY(int rotationSense READ rotationSense NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap bearing READ bearing NOTIFY bearingChanged)
    Q_PROPERTY(int httpPort READ httpPort WRITE setHttpPort NOTIFY changed)
    Q_PROPERTY(QString tileEndpoint READ tileEndpoint NOTIFY changed)
    Q_PROPERTY(QString wsEndpoint READ wsEndpoint NOTIFY changed)
    Q_PROPERTY(QString httpEndpoint READ httpEndpoint NOTIFY changed)
    Q_PROPERTY(QVariantList traffic READ traffic NOTIFY trafficChanged)
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)
    Q_PROPERTY(QString uptimeText READ uptimeText NOTIFY stateChanged)
    Q_PROPERTY(QVariantList endpoints READ endpoints NOTIFY stateChanged)
    // Il gateway integrato: la seriale del control box, il modello, le porte.
    Q_PROPERTY(QString gatewaySerialPort READ gatewaySerialPort WRITE setGatewaySerialPort NOTIFY changed)
    Q_PROPERTY(QString gatewayModel READ gatewayModel WRITE setGatewayModel NOTIFY changed)
    Q_PROPERTY(bool gatewaySimulate READ gatewaySimulate WRITE setGatewaySimulate NOTIFY changed)
    Q_PROPERTY(int gatewayWsPort READ gatewayWsPort WRITE setGatewayWsPort NOTIFY changed)
    Q_PROPERTY(int gatewayRotctldPort READ gatewayRotctldPort WRITE setGatewayRotctldPort NOTIFY changed)
    Q_PROPERTY(QStringList gatewayProblems READ gatewayProblems NOTIFY stateChanged)

public:
    struct Context {
        std::function<void(const QString& category, const QString& text, const QString& level)> activity;
        // Il QTH e il nominativo della stazione: il gateway integrato li usa
        // per le rotte e per le stazioni sulla mappa.
        std::function<QString()> stationGrid;
        std::function<QString()> stationCall;
    };

    explicit RotorController(Context context, QObject* parent = nullptr);

    // Da chiamare quando il resto e' pronto: se e' acceso, si collega.
    void start();
    // Per le prove da riga di comando: accende il rotore solo per questa volta,
    // senza scrivere niente nelle impostazioni dell'operatore.
    void overrideConnection(const QString& backend, const QString& host, int port);

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    // "builtin" | "decorotor" | "rotctld"
    QString backend() const { return m_backend; }
    void setBackend(const QString& backend);
    QString host() const { return m_host; }
    void setHost(const QString& host);
    int port() const { return m_port; }
    void setPort(int port);
    bool followDx() const { return m_followDx; }
    void setFollowDx(bool follow);
    int beamwidth() const { return m_beamwidth; }
    void setBeamwidth(int degrees);

    QVariantMap state() const;
    bool connected() const { return m_link.state().connected; }
    QString status() const;
    QString lastTarget() const { return m_lastTarget; }
    QVariantList presets() const { return m_link.presets(); }
    int rotationSense() const { return m_sense; }
    QVariantMap bearing() const { return m_bearing; }
    int httpPort() const { return m_httpPort; }
    void setHttpPort(int port);
    // Da dove la mappa prende i riquadri: il gateway stesso.
    QString tileEndpoint() const;
    QString wsEndpoint() const { return QStringLiteral("%1:%2").arg(m_host).arg(m_port); }
    QString httpEndpoint() const { return QStringLiteral("%1:%2").arg(m_host).arg(m_httpPort); }
    QVariantList traffic() const { return m_link.traffic(); }
    QVariantList history() const { return m_link.history(); }
    // "2 h 14 m", come sul posto di comando.
    QString uptimeText() const;
    // Gli indirizzi da usare su telefono e software di stazione.
    QVariantList endpoints() const;

    // Punta a gradi. `what` e' quello che si sta puntando, per il registro.
    Q_INVOKABLE void pointTo(double azimuth, const QString& what = {});
    // Punta al centro di un locatore: lo conta DecoRotor, che sa il proprio QTH.
    Q_INVOKABLE void pointLocator(const QString& locator, bool longPath = false);
    Q_INVOKABLE void stopNow(bool fast = false);
    Q_INVOKABLE void park();
    // Sposta il bersaglio di qualche grado (i tasti a freccia del pannello).
    Q_INVOKABLE void nudge(double degrees);
    Q_INVOKABLE void reconnect();
    // Memorie del gateway.
    Q_INVOKABLE void recallPreset(const QString& name);
    Q_INVOKABLE void savePresetHere(const QString& name);
    Q_INVOKABLE void deletePreset(const QString& name);
    // Rotta verso un locatore senza muovere l'antenna.
    Q_INVOKABLE void askBearing(const QString& locator);
    // Azimut ed elevazione insieme (il pannello di puntamento).
    Q_INVOKABLE void gotoPosition(double az, double el);
    // Diagnostica: il gateway manda i frame e lo storico solo se glieli chiedi.
    Q_INVOKABLE void refreshDiagnostics();
    // Configurazione a caldo del gateway (finisce nel suo config.json).
    Q_INVOKABLE void setSetting(const QString& key, const QVariant& value);
    Q_INVOKABLE void setLimit(const QString& key, double value);

    // Il nominativo che Decodium sta lavorando: se "segui" e' acceso e si sa da
    // che parte sta, l'antenna ci va da sola.
    void dxBearing(const QString& call, double azimuth);

    QString gatewaySerialPort() const { return m_gw.serialPort; }
    void setGatewaySerialPort(const QString& port);
    // "auto", "d_az", "d_el", "d_azel", "combi"
    QString gatewayModel() const { return m_gw.model; }
    void setGatewayModel(const QString& model);
    bool gatewaySimulate() const { return m_gw.simulate; }
    void setGatewaySimulate(bool on);
    int gatewayWsPort() const { return m_gw.wsPort; }
    void setGatewayWsPort(int port);
    int gatewayRotctldPort() const { return m_gw.rotctldPort; }
    void setGatewayRotctldPort(int port);
    QStringList gatewayProblems() const { return m_gateway ? m_gateway->problems() : QStringList(); }
    // Le porte seriali di questo computer, e i modelli di control box.
    Q_INVOKABLE QStringList serialPorts() const;
    Q_INVOKABLE QVariantList gatewayModels() const;
    // Prende porta, modello, porte di rete, finecorsa e memorie dal
    // config.json di DecoRotor. Vuoto: lo cerca dove sta di solito.
    // Torna il file letto, o vuoto se non l'ha trovato.
    Q_INVOKABLE QString importDecoRotor(const QString& path = QString());
    // Il profilo della stazione e' cambiato: QTH e nominativo nuovi al gateway.
    void stationChanged();
    // Quello che Decodium e il cluster sentono, per la mappa dell'app.
    core::RotorGateway* gateway() const { return m_gateway; }

signals:
    void changed();
    void stateChanged();
    void presetsChanged();
    void bearingChanged();
    void trafficChanged();
    void historyChanged();

private:
    void apply();
    void note(const QString& text, const QString& level);
    bool builtin() const { return m_backend == QLatin1String("builtin"); }
    void saveGateway();

    Context m_ctx;
    core::RotorLink m_link;
    bool    m_enabled{false};
    QString m_backend{QStringLiteral("decorotor")};
    QString m_host{QStringLiteral("127.0.0.1")};
    int     m_port{8765};
    bool    m_followDx{false};
    int     m_beamwidth{45};
    QString m_lastTarget;
    QString m_followedCall;
    QVariantMap m_bearing;
    int     m_httpPort{8080};
    int     m_sense{0};         // -1 antiorario, +1 orario, 0 fermo
    double  m_lastAz{-1.0};
    // Il gateway integrato, acceso solo con il backend "builtin".
    core::RotorGateway* m_gateway{nullptr};
    core::GatewaySettings m_gw;
};

} // namespace decolog::app
