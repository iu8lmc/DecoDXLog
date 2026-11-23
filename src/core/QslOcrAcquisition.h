#pragma once
#include <QObject>
#include <QThreadPool>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <atomic>
#include <memory>
namespace decolog::core {
class LogDatabase;
class TesseractOcr;
class QslOcrAcquisition : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QVariantMap result READ result NOTIFY changed)
    Q_PROPERTY(QVariantList history READ history NOTIFY changed)
    Q_PROPERTY(QVariantList reviews READ reviews NOTIFY changed)
  public:
    QslOcrAcquisition(LogDatabase *db, TesseractOcr *ocr, QObject *parent = nullptr);
    ~QslOcrAcquisition() override;
    bool busy() const { return m_busy; }
    QString error() const { return m_error; }
    QVariantMap result() const { return m_result; }
    QVariantList history() const { return m_history; }
    QVariantList reviews() const { return m_reviews; }
    Q_INVOKABLE bool start(const QVariantList &selected, const QUrl &original);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE bool undo(qint64 acquisitionId, const QString &uuid);
    Q_INVOKABLE bool review(qint64 qsoId, qint64 beforeId);
    Q_INVOKABLE bool loadReviews(qint64 acquisitionId, const QString &uuid, qint64 beforeId);
    Q_INVOKABLE bool saveReview(qint64 acquisitionId, const QString &uuid, qint64 expectedReviewId, const QString &text);
    static QVariantMap saveReviewExecute(const QString &dbPath, qint64 acquisitionId, const QString &uuid,
                                         qint64 expectedReviewId, const QString &text, const std::atomic_bool *cancelled);
    static QVariantMap execute(const QString &dbPath, const QString &snapshot, const QString &sha256,
                               const QString &sourceName, const QString &ocrText,
                               const QVariantList &selected, const std::atomic_bool *cancelled);
    static QVariantMap undoExecute(const QString &dbPath, qint64 acquisitionId, const QString &uuid,
                                   const std::atomic_bool *cancelled);
  signals:
    void changed();
    void finished(bool success);
  private:
    LogDatabase *m_db;
    TesseractOcr *m_ocr;
    QThreadPool m_pool;
    std::shared_ptr<std::atomic_bool> m_cancel;
    bool m_busy{false};
    QString m_error;
    QVariantMap m_result;
    QVariantList m_history;
    QVariantList m_reviews;
};
} // namespace decolog::core
