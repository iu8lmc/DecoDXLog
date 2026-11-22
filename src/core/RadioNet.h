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

// Radio round table roster. Participation never creates a QSO.
class RadioNet : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList sessions READ sessions NOTIFY changed)
    Q_PROPERTY(QVariantList members READ members NOTIFY changed)
    Q_PROPERTY(QVariantList events READ events NOTIFY changed)
    Q_PROPERTY(QVariantMap session READ session NOTIFY changed)
    Q_PROPERTY(QVariantMap summary READ summary NOTIFY changed)
    Q_PROPERTY(QVariantMap reminder READ reminder NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool exportBusy READ exportBusy NOTIFY exportChanged)
  public:
    explicit RadioNet(LogDatabase *db, QObject *parent = nullptr);
    ~RadioNet() override;
    void load();
    QVariantList sessions() const;
    QVariantList members() const;
    QVariantList events() const;
    Q_INVOKABLE QVariantList historyPage(qint64 beforeId) const;
    Q_INVOKABLE QVariantList reportedQsoPage(qint64 beforeId) const;
    QVariantMap session() const;
    QVariantMap summary() const;
    QVariantMap reminder() const;
    QString error() const { return m_error; }
    bool exportBusy() const { return m_exportBusy; }
    Q_INVOKABLE bool selectSession(qint64 id);
    Q_INVOKABLE bool start(const QString &title, qint64 stationProfileId = 0);
    Q_INVOKABLE bool checkIn(const QString &call, const QString &name, const QString &notes);
    Q_INVOKABLE bool setStatus(qint64 memberId, const QString &status);
    Q_INVOKABLE bool moveMember(qint64 memberId, int direction);
    Q_INVOKABLE bool nextTurn();
    Q_INVOKABLE bool closeSession();
    Q_INVOKABLE bool reopenSession();
    Q_INVOKABLE bool setReminder(int seconds);
    Q_INVOKABLE bool clearReminder();
    Q_INVOKABLE bool exportSession(const QUrl &file);
    Q_INVOKABLE void cancelExport();
  signals:
    void changed();
    void exportChanged();
    void exportFinished(bool success, const QString &message);

  private:
    bool active() const;
    bool fail(const QString &message);
    bool appendEvent(qint64 memberId, const QString &kind, const QString &detail = {});
    LogDatabase *m_db;
    qint64 m_sessionId{0};
    QString m_error;
    QThreadPool m_exportPool;
    bool m_exportBusy{false};
    std::shared_ptr<std::atomic_bool> m_exportCancel;
};
} // namespace decolog::core
