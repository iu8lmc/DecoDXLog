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
  public:
    QslOcrAcquisition(LogDatabase *db, TesseractOcr *ocr, QObject *parent = nullptr);
    ~QslOcrAcquisition() override;
    bool busy() const { return m_busy; }
    QString error() const { return m_error; }
    QVariantMap result() const { return m_result; }
    Q_INVOKABLE bool start(const QVariantList &selected, const QUrl &original);
    Q_INVOKABLE void cancel();
    static QVariantMap execute(const QString &dbPath, const QString &snapshot, const QString &sha256,
                               const QString &sourceName, const QString &ocrText,
                               const QVariantList &selected, const std::atomic_bool *cancelled);
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
};
} // namespace decolog::core
