#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

namespace decolog::core {
class LogDatabase;

// Radio round table roster. Participation never creates a QSO.
class RadioNet : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList sessions READ sessions NOTIFY changed)
    Q_PROPERTY(QVariantList members READ members NOTIFY changed)
    Q_PROPERTY(QVariantList events READ events NOTIFY changed)
    Q_PROPERTY(QVariantMap session READ session NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
  public:
    explicit RadioNet(LogDatabase *db, QObject *parent = nullptr);
    void load();
    QVariantList sessions() const;
    QVariantList members() const;
    QVariantList events() const;
    QVariantMap session() const;
    QString error() const { return m_error; }
    Q_INVOKABLE bool selectSession(qint64 id);
    Q_INVOKABLE bool start(const QString &title, qint64 stationProfileId = 0);
    Q_INVOKABLE bool checkIn(const QString &call, const QString &name, const QString &notes);
    Q_INVOKABLE bool setStatus(qint64 memberId, const QString &status);
    Q_INVOKABLE bool moveMember(qint64 memberId, int direction);
    Q_INVOKABLE bool nextTurn();
    Q_INVOKABLE bool closeSession();
    Q_INVOKABLE bool reopenSession();
  signals:
    void changed();

  private:
    bool active() const;
    bool fail(const QString &message);
    bool appendEvent(qint64 memberId, const QString &kind, const QString &detail = {});
    LogDatabase *m_db;
    qint64 m_sessionId{0};
    QString m_error;
};
} // namespace decolog::core
