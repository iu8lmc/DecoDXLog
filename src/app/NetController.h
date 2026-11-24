// DecoDXLog — la rete multi-operatore per il QML: chi c'e', i messaggi fra
// operatori, gli spot interni, e i QSO che viaggiano fra i log.
#pragma once

#include "core/ContestNet.h"
#include "core/SharedSerials.h"
#include "core/ContestNetQsoTransfer.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QThreadPool>
#include <QUuid>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include <atomic>
#include <memory>

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
    Q_PROPERTY(QString durableSerialStatus READ durableSerialStatus NOTIFY durableSerialStatusChanged)
    Q_PROPERTY(bool qsoComparisonBusy READ qsoComparisonBusy NOTIFY qsoComparisonChanged)
    Q_PROPERTY(QVariantMap qsoComparisonSummary READ qsoComparisonSummary NOTIFY qsoComparisonChanged)
    Q_PROPERTY(QVariantList qsoComparisonRows READ qsoComparisonRows NOTIFY qsoComparisonChanged)
    Q_PROPERTY(bool qsoReviewBusy READ qsoReviewBusy NOTIFY qsoReviewChanged)
    Q_PROPERTY(QVariantMap qsoReview READ qsoReview NOTIFY qsoReviewChanged)
    Q_PROPERTY(QVariantMap experimentalQsoPreview READ experimentalQsoPreview NOTIFY experimentalQsoChanged)
    Q_PROPERTY(QString experimentalQsoStatus READ experimentalQsoStatus NOTIFY experimentalQsoChanged)
    Q_PROPERTY(bool experimentalQsoSending READ experimentalQsoSending NOTIFY experimentalQsoChanged)
    Q_PROPERTY(bool experimentalQsoSecure READ experimentalQsoSecure NOTIFY experimentalQsoChanged)
    Q_PROPERTY(QString handoffScope READ handoffScope NOTIFY handoffChanged)
    Q_PROPERTY(QVariantList handoffInbox READ handoffInbox NOTIFY handoffChanged)
    Q_PROPERTY(QVariantList handoffOutbox READ handoffOutbox NOTIFY handoffChanged)
    Q_PROPERTY(QString handoffStatus READ handoffStatus NOTIFY handoffChanged)
    Q_PROPERTY(QString handoffError READ handoffError NOTIFY handoffChanged)

public:
    struct Context {
        core::LogDatabase* db{nullptr};
        std::function<void(const QString& category, const QString& text, const QString& level)> activity;
        // Un QSO di un altro PC da mettere nel log; torna vero se e' entrato.
        std::function<bool(const core::AdifRecord& record, const QString& station)> insertRemote;
        std::function<void(qint64)> reviewedQsoInserted;
        std::function<void(qint64, bool, const QString&)> reviewedQsoChanged;
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
    Q_INVOKABLE QString requestDurableSerial(int minimum);
    Q_INVOKABLE QVariantMap durableSerialGrantForQso() const;
    void durableSerialQsoSaved();
    QString durableSerialStatus() const { return m_durableStatus; }
    Q_INVOKABLE bool startQsoComparison(const QUrl& remoteFile);
    Q_INVOKABLE void cancelQsoComparison();
    Q_INVOKABLE bool reviewQsoComparison(const QString& uuid);
    Q_INVOKABLE QString applyReviewedMissingQso();
    Q_INVOKABLE QString applyReviewedExistingQso();
    Q_INVOKABLE QString setExperimentalQsoKey(const QString& hexKey);
    Q_INVOKABLE QString generateExperimentalQsoKey() const;
    bool experimentalQsoSecure() const { return !m_wireKey.isEmpty(); }
    Q_INVOKABLE QString armExperimentalQsoReceiver(const QString& sender, const QString& transfer);
    Q_INVOKABLE QString sendExperimentalQso(const QString& receiver, const QString& uuid, const QString& transfer);
    Q_INVOKABLE QString requestExperimentalQsoMissing();
    Q_INVOKABLE bool reviewExperimentalQso();
    Q_INVOKABLE void clearExperimentalQso();
    QVariantMap experimentalQsoPreview() const { return m_wirePreview; }
    QString experimentalQsoStatus() const { return m_wireStatus; }
    bool experimentalQsoSending() const { return m_wirePreparing || !m_wireIndices.isEmpty(); }
    Q_INVOKABLE QVariantMap qsoLongFieldPage(int fieldIndex, const QString& part, int offset);
    bool qsoReviewBusy() const { return m_qsoReviewBusy; }
    QVariantMap qsoReview() const { return m_qsoReview; }
    bool qsoComparisonBusy() const { return m_qsoComparisonBusy; }
    QVariantMap qsoComparisonSummary() const { return m_qsoComparisonSummary; }
    QVariantList qsoComparisonRows() const { return m_qsoComparisonRows; }

    // Chiave wire-level usata per deduplicare un datagramma. La sessione fa
    // parte della chiave: dopo un riavvio il contatore puo' ricominciare da 1.
    static QString frameKey(const QString& session, qint64 seq);

    Q_INVOKABLE void sendGab(const QString& text, const QString& to = {});
    Q_INVOKABLE void sendSpot(const QString& call, double khz, const QString& comment = {});
    QString handoffScope() const { return m_handoffScope; }
    QString handoffStatus() const { return m_handoffStatus; }
    QString handoffError() const;
    QVariantList handoffInbox() const;
    QVariantList handoffOutbox() const;
    Q_INVOKABLE QString configureHandoffScope(const QString& scope);
    Q_INVOKABLE QString sendHandoffOffer(const QString& recipient,const QString& call,qint64 frequencyHz);
    Q_INVOKABLE QString retryHandoffOffer(const QString& id);
    // Chiede agli altri i QSO della gara che qui mancano.
    Q_INVOKABLE void requestSync();
    // Per i test: invece del broadcast, a questo indirizzo.
    void setTarget(const QHostAddress& a) { m_net.setTarget(a); }
    Q_INVOKABLE void setLoopbackForProbe() { if (qEnvironmentVariableIntValue("DECODXLOG_TEST_LOOPBACK")==1) m_net.setTarget(QHostAddress::LocalHost); }
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
    void durableSerialRequestSent(const QJsonObject& request);
    void durableSerialStatusChanged();
    void qsoComparisonChanged();
    void qsoReviewChanged();
    void experimentalQsoChanged();
    void experimentalQsoFrameSent(const QString& type, const QJsonObject& body);
    void handoffChanged();
    void handoffOfferSent(const QJsonObject& offer);

private:
    void restart();
    void send(const QString& type, QJsonObject body);
    void onReceived(const QString& from, const QString& type, const QJsonObject& body);
    bool handoffLogCurrent() const;
    QString requestExperimentalQsoMissingImpl(bool extendExpiry);
    bool sendExperimentalWireFrame(const QString& type, const QJsonObject& body);
    void handleExperimentalWireFrame(const QString& from, const QString& type, const QJsonObject& body);
    void serveDurableSerialRequest(const QString& from, const QJsonObject& body);
    void acceptDurableSerialGrant(const QString& from, const QJsonObject& body);
    QString requestDurableSerialImpl(int minimum, const QString& expectedRequestId);
    void resumeDurableSerialRequest();
    void hello();
    void addMessage(const QString& from, const QString& text, bool mine, const QString& to = {});
    QJsonObject qsoMessage(qint64 id) const;

    Context m_ctx;
    core::ContestNet m_net;
    bool m_enabled{false};
    QString m_group;
    QString m_name;
    QString m_id;
    QString m_handoffScope, m_handoffStatus;
    QString m_handoffPath, m_handoffIdentity, m_handoffGroup;
    QByteArray m_handoffFileIdentity;
    quint64 m_handoffGeneration{0};
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
    QString m_durableStatus;
    QTimer m_durableRetry;
    QString m_durableRetryRequestId;
    QString m_durableRetryIdentity;
    QByteArray m_durableRetryFileIdentity;
    quint64 m_durableRetryDbGeneration{0};
    int m_durableRetryMinimum{0};
    quint64 m_durableGeneration{0};
    bool m_durableBusy{false};
    QThreadPool m_qsoWirePool;
    QTimer m_qsoWireTimer, m_qsoWireExpiry, m_qsoWireRetry;
    QElapsedTimer m_wireServeLimit;
    std::shared_ptr<core::contestqso::Encoder> m_wireEncoder;
    std::shared_ptr<core::contestqso::Receiver> m_wireReceiver;
    QList<int> m_wireIndices;
    QString m_wirePeer, m_wireSender, m_wireStatus, m_wireTransferIn, m_wireTransferOut;
    QVariantMap m_wirePreview;
    QByteArray m_wireKey;
    QSet<QByteArray> m_wireSeenNonces;
    quint64 m_wireGeneration{0};
    bool m_wirePreparing{false};
    QThreadPool m_qsoComparisonPool;
    QTimer m_qsoComparisonTimer;
    std::shared_ptr<std::atomic_bool> m_qsoComparisonCancel;
    quint64 m_qsoComparisonGeneration{0};
    bool m_qsoComparisonBusy{false};
    quint64 m_qsoReviewGeneration{0};
    bool m_qsoReviewBusy{false};
    QVariantMap m_qsoReview;
    std::shared_ptr<core::contestqso::Receiver> m_reviewedMissing;
    core::contestqso::Review m_reviewedExisting;
    QHash<QString,int> m_reviewedLongOffsets;
    bool qsoComparisonCurrent() const;
    bool qsoReviewCurrent() const;
    void invalidateNetworkReview();
    bool m_reviewFromNetwork{false};
    std::shared_ptr<core::contestqso::Receiver> m_reviewWireOriginal;
    QString m_reviewWirePath, m_reviewWireIdentity, m_reviewWireStamp;
    QByteArray m_reviewWireFileIdentity;
    quint64 m_reviewWireDbGeneration{0};
    QVariantMap m_qsoComparisonSummary;
    QVariantList m_qsoComparisonRows;
    QString m_qsoComparisonPath, m_qsoComparisonIdentity, m_qsoComparisonStamp, m_qsoComparisonRemotePath;
    QByteArray m_qsoComparisonFileIdentity, m_qsoComparisonRemoteFileIdentity;
    quint64 m_qsoComparisonOpenGeneration{0};
    std::unique_ptr<core::LogDatabase> m_qsoComparisonRemoteWatcher;
    qint64 m_qsoComparisonRemoteVersion{-1};
    void requestSerial();
    void noteSerial(const QString& stx);
};

} // namespace decolog::app
