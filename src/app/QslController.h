// DecoDXLog — l'invio delle QSL: coda, stato per servizio, risultati.
//
// LoTW parte in blocco (un file ADIF firmato da TQSL), QRZ Logbook ed eQSL un QSO
// alla volta. Quello che il servizio accetta diventa "inviato" nella tabella
// qsl_status; un duplicato conta come inviato, perche' ritentarlo non serve; un
// rifiuto resta scritto sul QSO con il motivo, e non blocca gli altri.
#pragma once

#include "core/QslUpload.h"

#include <QDir>
#include <QHash>
#include <QObject>
#include <QQueue>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

namespace decolog::core {
class CredentialStore;
class LogDatabase;
}

namespace decolog::app {

class QslController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList services READ services NOTIFY changed)
    Q_PROPERTY(QString tqslPath READ tqslPath WRITE setTqslPath NOTIFY changed)
    Q_PROPERTY(QStringList tqslLocations READ tqslLocations NOTIFY changed)
    Q_PROPERTY(QString tqslLocation READ tqslLocation WRITE setTqslLocation NOTIFY changed)
    Q_PROPERTY(bool tqslReady READ tqslReady NOTIFY changed)
    Q_PROPERTY(QString clubLogApiKey READ clubLogApiKey WRITE setClubLogApiKey NOTIFY changed)
    // CRX Logbook: in quale logbook dell'account si scrive, e da che giorno.
    Q_PROPERTY(qint64 crxLogId READ crxLogId WRITE setCrxLogId NOTIFY changed)
    Q_PROPERTY(QString crxLogName READ crxLogName NOTIFY changed)
    Q_PROPERTY(QVariantList crxLogs READ crxLogs NOTIFY changed)
    Q_PROPERTY(QString crxStatus READ crxStatus NOTIFY changed)
    Q_PROPERTY(QString crxSince READ crxSince WRITE setCrxSince NOTIFY changed)
    // Wavelog: la stazione del sito in cui scrivere.
    Q_PROPERTY(QString wavelogStationId READ wavelogStationId WRITE setWavelogStationId NOTIFY changed)
    Q_PROPERTY(QVariantList wavelogStations READ wavelogStations NOTIFY changed)
    Q_PROPERTY(QString wavelogStatus READ wavelogStatus NOTIFY changed)
    Q_PROPERTY(QString tqslStatus READ tqslStatus NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString busyService READ busyService NOTIFY changed)

public:
    struct Context {
        core::LogDatabase* db{nullptr};
        core::CredentialStore* credentials{nullptr};
        std::function<QString()> stationLocation;   // la station location del profilo attivo
        std::function<QString()> stationCallsign;   // il nominativo del profilo attivo
        std::function<QString()> stationProfileName; // nome del profilo attivo
        std::function<void(const QString& category, const QString& text, const QString& level)> activity;
        std::function<void()> logChanged;
    };

    explicit QslController(Context context, QObject* parent = nullptr);

    // I servizi con cui si puo' caricare: id, label, in coda, inviati, confermati,
    // errori, configurato, automatico, ultimo esito.
    QVariantList services() const;
    QString tqslPath() const { return m_tqslPath; }
    void setTqslPath(const QString& path);
    QStringList tqslLocations() const { return core::qsl::tqslStationLocations(); }
    QString tqslLocation() const { return m_tqslLocation; }
    void setTqslLocation(const QString& location);
    bool tqslReady() const;
    QString clubLogApiKey() const { return m_clubLogApiKey; }
    void setClubLogApiKey(const QString& key);
    QString tqslStatus() const;
    qint64 crxLogId() const { return m_crxLogId; }
    void setCrxLogId(qint64 id);
    QString crxLogName() const { return m_crxLogName; }
    QVariantList crxLogs() const { return m_crxLogs; }
    QString crxStatus() const { return m_crxStatus; }
    QString wavelogStationId() const { return m_wavelogStationId; }
    void setWavelogStationId(const QString& id);
    QVariantList wavelogStations() const { return m_wavelogStations; }
    QString wavelogStatus() const { return m_wavelogStatus; }
    Q_INVOKABLE void fetchWavelogStations();
    QString crxSince() const { return m_crxSince.toString(Qt::ISODate); }
    void setCrxSince(const QString& date);
    // Chiede a CRX l'elenco dei logbook dell'account (serve la chiave API).
    Q_INVOKABLE void fetchCrxLogs();
    // Dove TQSL tiene certificati e station location: serve per capirci qualcosa
    // quando qualcosa non torna.
    Q_INVOKABLE QString tqslDataDirectory() const { return QDir::toNativeSeparators(core::qsl::tqslDataDirectory()); }
    bool busy() const { return !m_busyService.isEmpty(); }
    QString busyService() const { return m_busyService; }

    // Manda i QSO in attesa; `limit` 0 = tutti.
    Q_INVOKABLE void uploadPending(const QString& service, int limit = 0);
    // Un QSO solo (dalla sua scheda).
    Q_INVOKABLE void uploadQso(qint64 id, const QString& service);
    // Un gruppo di QSO scelti nel log. Torna quanti ne parte davvero: quelli
    // gia' inviati a quel servizio non si rimandano.
    Q_INVOKABLE int uploadQsos(const QVariantList& ids, const QString& service);
    Q_INVOKABLE void setAutoUpload(const QString& service, bool automatic);
    Q_INVOKABLE bool autoUpload(const QString& service) const;
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void refresh() { emit changed(); }

    // Un QSO appena scritto nel log: se il servizio e' automatico, parte da solo.
    void qsoLogged(qint64 id);
    // Il log e' cambiato: i conti (in coda, inviati, confermati) si rifanno su
    // un altro filo; fino ad allora valgono quelli di prima.
    void countsDirty();

signals:
    void changed();

private:
    void startNext();
    void startNextAutomaticUpload();
    void uploadNextWeb();
    void finishBatch(const core::QslUploadResult& result);
    void markSent(qint64 id, const QString& service, const core::QslUploadResult& result);
    void note(const QString& text, const QString& level);
    void readSecret(const QString& service, std::function<void(const QString&, const QString&)> done);

    Context m_ctx;
    core::TqslUploader m_tqsl;
    core::WebQslUploader m_web;
    QString m_tqslPath;
    QString m_clubLogApiKey;
    qint64 m_crxLogId{0};
    QString m_crxLogName;
    QVariantList m_crxLogs;
    QString m_crxStatus;
    QString m_wavelogStationId;
    QVariantList m_wavelogStations;
    QString m_wavelogStatus;
    QDate m_crxSince;
    // Per CRX si parte da un giorno: senza, al primo collegamento partirebbe
    // tutto il log di una vita.
    QDate sinceFor(const QString& service) const;
    QString m_tqslLocation;
    QString m_busyService;
    QList<qint64> m_batch;          // i QSO dell'invio in corso
    bool m_batchMode{false};        // LoTW e Club Log: tutto il blocco in una volta
    QQueue<qint64> m_pending;       // per QRZ ed eQSL, uno alla volta
    QString m_secret;               // credenziale del servizio in corso, solo durante l'invio
    QString m_account;
    QString m_credential;           // quale account sta usando l'invio (QRZ/eQSL: puo' essere di un profilo)
    QHash<QString, QString> m_lastResult;
    QHash<QString, bool> m_auto;
    QString m_adifFile;             // file temporaneo di TQSL
    int m_accepted{0};
    int m_duplicates{0};
    int m_rejected{0};
    int m_removed{0};
    // CRX: i QSO cancellati qui da togliere anche li' ({id, "log:qso"}), e
    // quello per cui si aspetta la risposta.
    QList<QPair<qint64, QString>> m_deletions;
    qint64 m_deletingId{0};
    QTimer m_autoDelay;
    QList<qint64> m_autoQueue;
    core::qsl::AutomaticUploadQueue m_automaticUploads;
    // I conti della pagina, tenuti qui: services() si legge a ogni changed(),
    // anche durante un invio, e contarli ogni volta su un log grande fermava
    // il programma per secondi.
    bool backgroundReady() const;
    void refreshCounts();
    mutable bool m_countsValid{false};
    mutable bool m_countsScheduled{false};
    QHash<QString, int> m_pendingCounts;
    QList<QVariantMap> m_summaryRows;
    QThreadPool m_countPool;
    int m_countGeneration{0};
};

} // namespace decolog::app
