#pragma once
#include <QDateTime>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
namespace decolog::core {
class LogDatabase;
// Explicit local rover session; no QSO or radio actions are implicit.
class RoverSession : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap session READ session NOTIFY changed)
    Q_PROPERTY(QVariantList positions READ positions NOTIFY changed)
    Q_PROPERTY(QVariantList sessions READ sessions NOTIFY changed)
    Q_PROPERTY(qint64 selectedId READ selectedId NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
  public:
    explicit RoverSession(LogDatabase *db, QObject *parent = nullptr);
    QVariantMap session() const;
    bool readSession(QVariantMap *session, QString *error = nullptr) const;
    QVariantList positions() const;
    QVariantList sessions() const;
    qint64 selectedId() const { return m_selectedId; }
    QString error() const { return m_error; }
    Q_INVOKABLE bool start(const QString &title, const QString &grid, qint64 profileId = 0);
    Q_INVOKABLE bool setPosition(const QString &grid);
    Q_INVOKABLE bool close();
    Q_INVOKABLE bool selectHistory(qint64 id);
    QString gridAt(const QDateTime &when, QString *error = nullptr) const;
  signals:
    void changed();

  private:
    bool fail(const QString &message);
    LogDatabase *m_db;
    QString m_error;
    qint64 m_selectedId{0};
};
} // namespace decolog::core
