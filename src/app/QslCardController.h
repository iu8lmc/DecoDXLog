// DecoDXLog — le QSL di carta.
//
// Tre stati e basta: da mandare, mandata, ricevuta. Il programma tiene la coda e
// stampa le etichette; la busta la fa l'operatore. Una etichetta raccoglie fino
// a quattro QSO con lo stesso corrispondente, perche' una QSL sola risponde a
// tutti.
#pragma once

#include "core/MailSender.h"
#include "core/QslDesign.h"

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QPair>
#include <QSize>
#include <QVariantMap>
#include <QThreadPool>
#include <QHash>
#include <atomic>
#include <memory>
#include <functional>

class QNetworkAccessManager;

namespace decolog::core {
class LogDatabase;
class CredentialStore;
}

namespace decolog::app {

class QslCardController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList queue READ queue NOTIFY changed)
    Q_PROPERTY(QVariantMap counts READ counts NOTIFY changed)
    Q_PROPERTY(QVariantList sheets READ sheets CONSTANT)
    Q_PROPERTY(QString lastFile READ lastFile NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool operationBusy READ operationBusy NOTIFY operationChanged)
    Q_PROPERTY(bool cacheLoading READ cacheLoading NOTIFY cacheLoadingChanged)
    Q_PROPERTY(bool exportBusy READ exportBusy NOTIFY exportBusyChanged)
    // La cartolina: il modello (un'immagine) e i campi posati sopra.
    Q_PROPERTY(QString cardTemplate READ cardTemplate NOTIFY cardChanged)
    Q_PROPERTY(QVariantList cardFields READ cardFields NOTIFY cardChanged)
    Q_PROPERTY(QSize cardSize READ cardSize NOTIFY cardChanged)
    // La casella da cui partono le QSL, e a che punto sta un invio.
    Q_PROPERTY(QVariantMap mail READ mail NOTIFY mailChanged)
    Q_PROPERTY(QString mailStatus READ mailStatus NOTIFY mailChanged)
    Q_PROPERTY(bool mailBusy READ mailBusy NOTIFY mailChanged)

public:
    struct Context {
        core::LogDatabase* db{nullptr};
        std::function<QVariantMap()> station;   // call, grid, name del profilo attivo
        std::function<void(const QString& category, const QString& text, const QString& level)> activity;
        std::function<void()> logChanged;
        core::CredentialStore* credentials{nullptr};
        // L'email del corrispondente secondo il callbook: arriva quando arriva.
        std::function<void(const QString& call,
                           std::function<void(const QString& email, const QString& error)>)> emailFor;
        // Il Cloud, per chi preferisce non tenere la password di una casella
        // sul proprio computer: indirizzo del servizio e token, vuoti se non
        // si e' collegati.
        std::function<QPair<QString, QString>()> cloudAccess;
        // Il proprio indirizzo email, a cui devono tornare le risposte.
        std::function<QString()> replyTo;
    };

    explicit QslCardController(Context context, QObject* parent = nullptr);

    QVariantList queue() const { return rows(QStringLiteral("queue")); }
    QVariantMap counts() const;
    QVariantList sheets() const;
    QString lastFile() const { return m_lastFile; }
    QString status() const { return m_status; }
    bool operationBusy() const { return m_operationBusy; }
    bool cacheLoading() const { return !m_rowsPending.isEmpty() || m_countsPending != 0; }
    bool exportBusy() const { return m_exportBusy; }

    // `state`: queue | sent | received | all.
    Q_INVOKABLE QVariantList rows(const QString& state, int limit = 0) const;
    // Mette in coda: `via` e' B (bureau), D (diretta) o E (elettronica).
    Q_INVOKABLE void enqueue(const QVariantList& ids, const QString& via);
    // Explicit planner handoff. True means completed in-memory or accepted for
    // asynchronous execution; status reports the eventual result.
    Q_INVOKABLE bool enqueueIfUnsent(qint64 id, const QString& uuid, int revision, const QString& via, const QString& expectedLogPath);
    // Tutte le QSL ricevute a cui non si e' ancora risposto.
    // Returns the count for an in-memory log; -1 means queued in the worker.
    Q_INVOKABLE int enqueueUnanswered(const QString& via);
    Q_INVOKABLE void markSent(const QVariantList& ids, const QString& via);
    Q_INVOKABLE void markReceived(qint64 id, bool received);
    // Toglie dalla coda senza dire che e' partita ("I" di ignore in ADIF).
    Q_INVOKABLE void drop(const QVariantList& ids);
    // For file-backed logs output is asynchronous: the result is in
    // lastFile/status when exportBusy becomes false (the immediate return is empty).
    Q_INVOKABLE QString writeLabels(const QUrl& file, const QString& sheetId, int perLabel, bool guides);
    Q_INVOKABLE void refresh();
    void invalidateCache();

    // ── La cartolina ────────────────────────────────────────────────────────
    QString cardTemplate() const { return m_card.templatePath; }
    QVariantList cardFields() const;
    // La misura del modello in pixel: serve a QML per tenere le proporzioni.
    QSize cardSize() const { return m_cardSize; }

    // Le chiavi che si possono posare, con l'etichetta gia' tradotta.
    Q_INVOKABLE QVariantList cardKeys() const;
    Q_INVOKABLE void setCardTemplate(const QUrl& file);
    Q_INVOKABLE void addCardField(const QString& key);
    // Gli otto campi che stanno su quasi tutte le QSL, gia' nei riquadri dove
    // stanno di solito. Quelli gia' posati si spostano li' invece di sdoppiarsi.
    Q_INVOKABLE void addStandardCardFields();
    // Trascinamento: x e y sono frazioni del modello (0..1).
    Q_INVOKABLE void moveCardField(int index, double x, double y);
    // Solo le chiavi presenti in `props` cambiano: size, bold, color, align, text.
    Q_INVOKABLE void updateCardField(int index, const QVariantMap& props);
    Q_INVOKABLE void removeCardField(int index);
    // Una cartolina di prova: il primo QSO in coda, o uno inventato se la coda
    // e' vuota — senza, non si vedrebbe dove si stanno mettendo i campi.
    Q_INVOKABLE QVariantMap sampleQso() const;
    Q_INVOKABLE QVariantMap stationInfo() const;
    // Scrive le cartoline dei QSO scelti (o di tutta la coda se `ids` e' vuota).
    Q_INVOKABLE QString writeCardsPdf(const QUrl& file, const QVariantList& ids, int perPage);
    Q_INVOKABLE QString writeCardsPng(const QUrl& folder, const QVariantList& ids);

    // ── Mandare le cartoline per email ──────────────────────────────────────
    QVariantMap mail() const;
    QString mailStatus() const { return m_mailStatus; }
    bool mailBusy() const;
    // host, port, subject, body: quello che non e' segreto. L'indirizzo e la
    // password stanno nel portachiavi, sotto "mail".
    Q_INVOKABLE void setMail(const QVariantMap& settings);
    // Manda la cartolina ai QSO scelti (o a tutta la coda). Per ognuno chiede
    // l'email al callbook, disegna la sua cartolina e la spedisce.
    Q_INVOKABLE void sendCardsByEmail(const QVariantList& ids);
    Q_INVOKABLE void cancelMail();
    // "mailbox" (la casella dell'operatore) o "cloud" (il server fa da ponte).
    Q_INVOKABLE void setMailRoute(const QString& route);

signals:
    void changed();
    void cardChanged();
    void mailChanged();
    void operationChanged();
    void cacheLoadingChanged();
    void exportBusyChanged();

private:
    void note(const QString& text, const QString& level);
    void touch(qint64 id, const QString& sent, const QString& rcvd, const QString& via);
    int changeCards(const QList<qint64>& ids, const QString& sent, const QString& via,
                    bool unanswered = false, const QString& received = {},
                    const QString& expectedUuid = {}, bool onlyUnsent = false, int expectedRevision = -1);
    void ensureCacheIdentity() const;
    void startOutput(int kind, const QString& path, const QVariantList& ids,
                     const QString& sheetId, int perPage, bool guides);

    void loadCard();
    void saveCard();
    void measureTemplate();
    QList<QVariantMap> chosenQsos(const QVariantList& ids) const;

    Context m_ctx;
    QString m_lastFile;
    QString m_status;
    bool m_operationBusy{false};
    bool m_notifyingLogChange{false};
    bool m_exportBusy{false};
    QThreadPool m_cardPool;
    QThreadPool m_exportPool;
    mutable QThreadPool m_readPool;
    mutable QString m_cachePath;
    mutable quint64 m_cacheGeneration{1};
    mutable std::shared_ptr<std::atomic<quint64>> m_cacheLatest{std::make_shared<std::atomic<quint64>>(1)};
    mutable QHash<QString, QVariantList> m_cachedRows;
    mutable QHash<QString, quint64> m_rowsVersion;
    mutable QHash<QString, quint64> m_rowsPending;
    mutable QVariantMap m_cachedCounts;
    mutable quint64 m_countsVersion{0};
    mutable quint64 m_countsPending{0};
    core::qsldesign::Card m_card;
    QSize m_cardSize;

    void loadMail();
    void queueCard(const QVariantMap& qso, const QString& email);
    void sendThroughCloud(const QVariantMap& qso, const QString& email, const QByteArray& png);
    QByteArray drawCard(const QVariantMap& qso) const;
    QString mailRoute() const;
    void lookupAndSend(const QList<QVariantMap>& qsos, quint64 generation);
    void beginMail(const QList<QVariantMap>& qsos, quint64 generation);
    void recordConfirmedMail(const QString& path, const QString& identity, const QByteArray& fileToken, quint64 openGeneration,
                             qint64 id, const QString& uuid, const QString& email);
    bool mailOriginIsCurrent() const;
    QString mailBodyFor(const QVariantMap& qso) const;

    core::MailSender m_mail;
    QString m_mailStatus;
    // Le richieste al Cloud: tenerne il padre serve a poterle fermare tutte
    // quando si preme «Ferma» a meta' di un invio.
    QNetworkAccessManager* m_network{nullptr};
    int m_mailWaiting{0};     // quanti aspettano ancora l'email dal callbook
    int m_mailSent{0};
    int m_mailFailed{0};
    quint64 m_mailGeneration{0};
    bool m_mailPreparing{false};
    struct MailTarget { QString path; QString identity; QByteArray fileToken; quint64 openGeneration{0}; qint64 id{0}; QString uuid; quint64 generation{0}; };
    QHash<qint64, MailTarget> m_mailTargets;
    qint64 m_mailNextTag{0};
    int m_mailPendingRecords{0};
    QString m_mailOriginPath;
    QString m_mailOriginIdentity;
    QByteArray m_mailOriginFileToken;
    quint64 m_mailOriginOpenGeneration{0};
};

} // namespace decolog::app
