#pragma once
#include <QHostAddress>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
class QUdpSocket;
namespace decolog::core {
// A single predicted pass, read-only. UDP datagrams are validated and bounded.
class PredictPassPlanner : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap pass READ pass NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
  public:
    explicit PredictPassPlanner(QObject *parent = nullptr);
    Q_INVOKABLE bool request(const QString &host, int port, const QString &satellite, qint64 startUnix);
    Q_INVOKABLE void cancel();
    QVariantMap pass() const { return m_pass; }
    bool busy() const { return m_busy; }
  signals:
    void changed();
    void finished(bool success, const QString &error);

  private:
    void sendNext();
    void receive();
    void fail(const QString &error);
    QHostAddress m_host;
    quint16 m_port{0};
    QString m_satellite;
    qint64 m_start{0};
    QUdpSocket *m_socket{nullptr};
    QTimer m_timeout;
    int m_stage{0};
    bool m_busy{false};
    QByteArray m_tle;
    QByteArray m_stream;
    QVariantList m_points;
    QVariantMap m_pass;
};
} // namespace decolog::core
