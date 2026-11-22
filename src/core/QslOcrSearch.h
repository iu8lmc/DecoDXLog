#pragma once
#include <QObject>
#include <QThreadPool>
#include <QVariantList>
#include <atomic>
#include <memory>
namespace decolog::core {
class LogDatabase;
// Advisory exact-call lookup. It never modifies the log or confirms a QSO.
class QslOcrSearch : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QVariantList matches READ matches NOTIFY changed)
    Q_PROPERTY(bool truncated READ truncated NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
  public:
    explicit QslOcrSearch(LogDatabase *db, QObject *parent = nullptr);
    ~QslOcrSearch() override;
    bool busy() const { return m_busy; }
    QVariantList matches() const { return m_matches; }
    bool truncated() const { return m_truncated; }
    QString error() const { return m_error; }
    Q_INVOKABLE bool start(const QString &call);
    Q_INVOKABLE void cancel();
  signals:
    void changed();
    void finished(bool success);
  private:
    LogDatabase *m_db;
    QThreadPool m_pool;
    std::shared_ptr<std::atomic_bool> m_cancel;
    bool m_busy{false};
    bool m_truncated{false};
    QVariantList m_matches;
    QString m_error;
};
} // namespace decolog::core
