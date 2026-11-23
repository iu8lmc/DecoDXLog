// DecoDXLog — la rete multi-operatore per il QML: chi c'e', i messaggi fra
// operatori, gli spot interni, e i QSO che viaggiano fra i log.
#pragma once

#include "core/ContestNet.h"
#include "core/SharedSerials.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QThreadPool>
#include <QUuid>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

namespace decolog::core {
class AdifRecord;
class LogDatabase;
}

namespace decolog::app {

class NetController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY settingsChanged)
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(QString group READ group WRITE setGroup NOTIFY settingsChanged)
    Q_PROPERTY(QString stationName READ stationName WRITE setStationName NOTIFY settingsChanged)
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY settingsChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(QVariantList peers READ peers NOTIFY peersChanged)
    Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged)
    Q_PROPERTY(int received READ received NOTIFY stateChanged)
    Q_PROPERTY(int sent READ sent NOTIFY stateChanged)
    Q_PROPERTY(QStringList queuedCalls READ queuedCalls NOTIFY queuedCallsChanged)
    Q_PROPERTY(QString nodeId READ id CONSTANT)

public:
    struct Context {
        core::LogDatabase* db{nullptr};
        std::function<void(const QString& category, const QString& text, const QString& level)> activity;
        // Un QSO di un altro PC da mettere nel log; torna vero se e' entrato.
        std::function<bool(const core::AdifRecord& record, const QString& station)> insertRemote;
        // Uno spot interno: nel cluster, come gli altri.
        std::function<void(const QString& call, double khz, const QString& comment, const QString& station)> spot;
        // Dove sta questo PC: {band, mode, freqKhz, op}.
        std::function<QVariantMap()> here;
        // Da quando valgono i QSO della gara in corso (invalido = nessuna gara).
        std::function<QDateTime()> sessionStart;
    };

    explicit NetController(Context context, QObject* parent = nullptr);
    ~NetController() override;

    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);
    bool running() const { return m_net.running(); }
    QString group() const { return m_group; }
    void setGroup(const QString& group);
    QString stationName() const { return m_name; }
    void setStationName(const QString& name);
    int port() const { return m_port; }
    void setPort(int port);
    QString status() const { return m_status; }
    QVariantList peers() const;
    QVariantList messages() const { return m_messages; }
    int received() const { return m_received; }
    int sent() const { return m_sent; }

    QStringList queuedCalls() const { return m_queuedCalls; }
    Q_INVOKABLE QString queueCall(const QString& call);
    Q_INVOKABLE bool moveQueuedCall(int index, int delta);
    Q_INVOKABLE bool removeQueuedCall(int index);
    Q_INVOKABLE QString takeQueuedCall();

    // Un QSO appena fatto qui: agli altri.
    void qsoLogged(qint64 id);

    // I progressivi condivisi (multi-operatore, una sequenza sola): li
    // distribuisce il PC con l'identita' piu' piccola fra quelli presenti; gli
    // altri ne chiedono uno in anticipo, cosi' al momento del QSO ce l'hanno
    // gia'. Senza rete: il piu' alto visto piu' uno.
    void setSerialSharing(bool on);
    int takeSerial(int localNext);
    bool isSerialServer() const;
    QString id() const { return m_id; }
    Q_INVOKABLE QString configureDurableSerialAuthority(const QString& scope, const QString& authority);

    // Chiave wire-level usata per deduplicare un datagramma. La sessione fa
    // parte della chiave: dopo un riavvio il contatore puo' ricominciare da 1.
    static QString frameKey(const QString& session, qint64 seq);

    Q_INVOKABLE void sendGab(const QString& text, const QString& to = {});
    Q_INVOKABLE void sendSpot(const QString& call, double khz, const QString& comment = {});
    // Chiede agli altri i QSO della gara che qui mancano.
    Q_INVOKABLE void requestSync();
    // Per i test: invece del broadcast, a questo indirizzo.
    void setTarget(const QHostAddress& a) { m_net.setTarget(a); }
    void setIdForTests(const QString& id) { m_id = id; }

signals:
    void settingsChanged();
    void stateChanged();
    void peersChanged();
    void messagesChanged();
    void queuedCallsChanged();
    void gabReceived(const QString& from, const QString& text);
    void durableSerialGrantReady(const QJsonObject& grant);
    void durableSerialGrantAccepted(const QJsonObject& grant);

private:
    void restart();
    void send(const QString& type, QJsonObject body);
    void onReceived(const QString& from, const QString& type, const QJsonObject& body);
    void serveDurableSerialRequest(const QString& from, const QJsonObject& body);
    void acceptDurableSerialGrant(const QString& from, const QJsonObject& body);
    void hello();
    void addMessage(const QString& from, const QString& text, bool mine, const QString& to = {});
    QJsonObject qsoMessage(qint64 id) const;

    Context m_ctx;
    core::ContestNet m_net;
    bool m_enabled{false};
    QString m_group;
    QString m_name;
    QString m_id;
    int m_port{12060};
    QString m_status;
    struct Peer {
        QString id;
        QString name;
        QString op;
        QString band;
        QString mode;
        double khz{0};
        int qsos{0};
        QString address;
        QDateTime seen;
    };
    QHash<QString, Peer> m_peers;
    // La sequenza riparte a ogni avvio del processo. La sessione evita che un
    // nuovo messaggio venga confuso con un vecchio frame dello stesso nodo.
    QHash<QString, QSet<QString>> m_seenSeq;
    qint64 m_seq{0};
    const QString m_sessionId{QUuid::createUuid().toString(QUuid::WithoutBraces)};
    QVariantList m_messages;
    QStringList m_queuedCalls;
    int m_received{0};
    int m_sent{0};
    QTimer m_hello;
    // I QSO da rimandare a chi ha chiesto la sincronia, a gruppi.
    QList<qint64> m_resend;
    QTimer m_resendTimer;
    bool m_serialSharing{false};
    core::SharedSerials m_serials;
    QThreadPool m_durablePool;
    QString m_durableScope;
    QString m_durableAuthority;
    quint64 m_durableGeneration{0};
    bool m_durableBusy{false};
    void requestSerial();
    void noteSerial(const QString& stx);
};

} // namespace decolog::app
