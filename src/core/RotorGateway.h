// DecoDXLog — il gateway del rotore, dentro DecoDXLog.
//
// Quello che faceva DecoRotor, il programma a parte in Python, adesso lo fa
// DecoDXLog: apre la seriale del control box PRO.SIS.TEL, lo interroga cinque
// volte al secondo, lo manda dove gli si dice (con i finecorsa software, lo
// stop se non si muove e lo stop se sparisce l'ultimo che lo comandava), e lo
// pubblica in rete esattamente come faceva lui:
//
//   · WebSocket (8765) per l'app sul telefono e per DecoDXLog stesso;
//   · rotctld di Hamlib (4532) per N1MM+, Log4OM, PstRotator, gpredict…;
//   · la pagina web (8080), con l'API REST, le stazioni sentite (/api/spots)
//     e i riquadri della mappa satellitare in cache (/tiles/z/x/y).
//
// Il protocollo di rete e' lo stesso di DecoRotor 1.0, parola per parola:
// l'app e i programmi che gia' ci parlavano non si accorgono del cambio.
//
// Le stazioni per la mappa non arrivano piu' da una porta UDP e da un TCP a
// parte: gliele passa DecoDXLog, che i decode di Decodium e gli spot del
// cluster li ha gia'.
#pragma once

#include "core/Prosistel.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QTimer>

#include <deque>
#include <functional>
#include <optional>

class QNetworkAccessManager;
class QSerialPort;
class QTcpServer;
class QTcpSocket;
class QWebSocket;
class QWebSocketServer;

namespace decolog::core {

// Quello che si decide una volta, nelle impostazioni di DecoDXLog.
struct GatewaySettings {
    QString serialPort;
    int baud{9600};
    int timeoutMs{3000};
    int retries{3};
    QString model{QStringLiteral("auto")};
    int pollMs{200};
    QString bind{QStringLiteral("0.0.0.0")};
    int wsPort{8765};
    int httpPort{8080};
    int rotctldPort{4532};
    QString token;
    bool simulate{false};
    double simulateSpeed{4.0};   // gradi al secondo del rotore finto, come un PST-61D
    // Dove tenere i riquadri della mappa; vuoto: niente mappa.
    QString tileCacheDir;
    QString tileUrl{QStringLiteral(
        "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}")};
    double tileCacheDays{30.0};
    double tileCacheMb{400.0};
    int spotTtlS{900};
    int spotLimit{120};
};

// Quello che l'app puo' cambiare a caldo (config_set), piu' le memorie.
struct GatewayLive {
    QString locator{QStringLiteral("JN70")};
    QString callsign;
    double beamwidth{60.0};
    double parkAz{0.0};
    double parkEl{0.0};
    double tolerance{1.0};
    double stallTimeoutS{8.0};
    bool stopOnClientLoss{true};
    double azMin{0.0};
    double azMax{360.0};
    double elMin{0.0};
    double elMax{90.0};
    QJsonArray presets;   // [{name, az, el}]

    QJsonObject toJson() const;
    static GatewayLive fromJson(const QJsonObject& object);
};

class RotorGateway : public QObject {
    Q_OBJECT

public:
    explicit RotorGateway(QObject* parent = nullptr);
    ~RotorGateway() override;

    void start(const GatewaySettings& settings, const GatewayLive& live);
    void stop();
    bool running() const { return m_running; }
    const GatewaySettings& settings() const { return m_settings; }
    const GatewayLive& live() const { return m_live; }
    // Il QTH e il nominativo cambiano con il profilo della stazione.
    void setStation(const QString& locator, const QString& callsign);
    // Le porte che non si sono potute aprire, gia' spiegate.
    QStringList problems() const { return m_problems; }

    // Lo stato come lo manda DecoRotor ({"type": "state", ...}).
    QJsonObject snapshot() const;

    // I comandi, come arrivano dall'app. Solleva errori come testo in
    // `error`; torna quello che va nell'ack.
    QJsonObject gotoPosition(std::optional<double> az, std::optional<double> el, QString* error);
    void halt(const QString& axis = QStringLiteral("all"), bool fast = false);
    QJsonObject park(QString* error);
    QJsonObject bearingTo(const QString& locator, QString* error) const;

    // Le stazioni da mettere sulla mappa dell'app.
    void noteDecode(const QString& message, int snr, const QString& mode, quint64 dialHz);
    void noteStatus(const QString& dxCall, const QString& dxGrid, const QString& mode, quint64 dialHz);
    void noteLogged(const QString& dxCall, const QString& dxGrid, const QString& mode, quint64 freqHz);
    void noteCluster(const QString& call, double lat, double lon, const QString& entity, const QString& mode,
                     quint64 freqHz, const QString& comment);
    QJsonArray spotEntries() const;
    // Chi ha trasmesso e da quale riquadro, dal testo di un decode FT8/FT4.
    static QPair<QString, QString> stationFromMessage(const QString& text);

    // Per le prove: una riga di rotctld e la risposta.
    QByteArray rotctldLine(const QString& line, bool* quit = nullptr);

signals:
    void stateChanged();
    // L'app ha cambiato configurazione o memorie: da salvare.
    void liveChanged();
    void note(const QString& text, const QString& level);

private:
    // ── seriale ──────────────────────────────────────────────────────────────
    struct Axis {
        std::optional<double> position;
        std::optional<double> target;
        bool moving{false};
        qint64 lastChangeMs{0};
    };
    struct Transaction {
        QByteArray frame;
        bool wantReply{false};
        int triesLeft{1};
        int multiplier{1};
        std::function<void(std::optional<prosistel::Reply>)> done;
    };

    void openPort();
    void afterOpen();
    void closePort(const QString& why);
    void scheduleReopen();
    void detectModel();
    void enqueue(Transaction t, bool front = false);
    void pump();
    void writeFrame(const QByteArray& frame);
    void onBytes(const QByteArray& bytes);
    void finishTransaction(std::optional<prosistel::Reply> reply);
    void poll();
    void pollAxis(const QString& name);
    void afterPoll();
    void recordFrame(const QString& direction, const QByteArray& frame);
    void publishState();
    QChar axisId(const QString& name) const;
    int multiplier() const;
    void simulateWrite(const QByteArray& frame);
    void simulateAdvance();
    void clientAttached();
    void clientDetached();

    // ── rete ────────────────────────────────────────────────────────────────
    void startServers();
    void stopServers();
    void onWsConnection();
    void onWsMessage(QWebSocket* socket, const QString& text);
    QJsonObject dispatch(const QString& command, const QJsonObject& request, bool* sendState);
    QJsonObject configPayload() const;
    void onRotctldConnection();
    void onHttpConnection();
    void handleHttp(QTcpSocket* socket, const QByteArray& request);
    void sendHttp(QTcpSocket* socket, int status, const QByteArray& type, const QByteArray& body,
                  const QList<QPair<QByteArray, QByteArray>>& headers = {});
    void sendJson(QTcpSocket* socket, const QJsonObject& object, int status = 200);
    void serveTile(QTcpSocket* socket, int z, int x, int y);
    bool authorized(const QString& token) const;

    // ── stazioni ────────────────────────────────────────────────────────────
    struct Spot {
        QString call;
        QString grid;
        double lat{0};
        double lon{0};
        double azimuth{0};
        double distanceKm{0};
        std::optional<int> snr;
        QString mode;
        quint64 frequencyHz{0};
        QString source;
        QString entity;
        QString comment;
        qint64 firstSeen{0};   // ms dall'epoca
        qint64 lastSeen{0};
        int count{0};
    };
    void recordSpot(const QString& call, const QString& grid, const QString& source, std::optional<int> snr,
                    const QString& mode, quint64 frequencyHz, std::optional<QPair<double, double>> position = {},
                    const QString& entity = {}, const QString& comment = {});
    void aimSpot(Spot& spot) const;
    void purgeSpots() const;

    GatewaySettings m_settings;
    GatewayLive m_live;
    bool m_running{false};
    QStringList m_problems;

    const prosistel::Model* m_model{nullptr};
    bool m_connected{false};
    QString m_error;
    Axis m_az;
    Axis m_el;
    QSerialPort* m_serial{nullptr};
    bool m_portOpen{false};
    QByteArray m_rx;
    QQueue<Transaction> m_queue;
    std::optional<Transaction> m_current;
    QTimer m_txTimer;
    QTimer m_pollTimer;
    QTimer m_reopenTimer;
    int m_backoffMs{1000};
    bool m_polling{false};
    int m_clients{0};
    QElapsedTimer m_clock;
    qint64 m_startedMs{0};
    QHash<QString, int> m_counters;
    std::deque<QJsonObject> m_history;
    std::deque<QJsonObject> m_traffic;

    // Il control box finto: posizione, bersaglio e la risposta pronta.
    QHash<QChar, double> m_simPos;
    QHash<QChar, double> m_simTarget;
    qint64 m_simLastMs{0};

    QWebSocketServer* m_ws{nullptr};
    QSet<QWebSocket*> m_wsClients;
    QSet<QWebSocket*> m_wsAuthorized;
    QTcpServer* m_rotctld{nullptr};
    QTcpServer* m_http{nullptr};
    QNetworkAccessManager* m_net{nullptr};

    mutable QHash<QString, Spot> m_spots;
    int m_received{0};
    quint64 m_dial{0};
    QString m_workingCall;
};

} // namespace decolog::core
