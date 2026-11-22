#pragma once
#include <QHostAddress>
#include <QObject>
#include <QTimer>
#include <QVariantMap>
class QUdpSocket;
namespace decolog::core {
// Read-only PREDICT UDP client. It never sends radio or rotor commands.
class PredictTracker : public QObject {
    Q_OBJECT
  public:
    explicit PredictTracker(QObject *parent = nullptr);
    bool request(const QString &host, quint16 port, const QString &satellite);
    QVariantMap snapshot() const { return m_snapshot; }
  signals:
    void finished(bool success, const QString &error);

  private:
    void sendNext();
    void receive();
    void fail(const QString &error);
    QUdpSocket *m_socket{nullptr};
    QTimer m_timeout;
    QHostAddress m_host;
    quint16 m_port{0};
    QString m_satellite;
    int m_stage{0};
    QVariantMap m_pending;
    QVariantMap m_snapshot;
};
} // namespace decolog::core
