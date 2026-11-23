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
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QStringList satellites READ satellites NOTIFY changed)
  public:
    explicit PredictTracker(QObject *parent = nullptr);
    Q_INVOKABLE bool request(const QString &host, int port, const QString &satellite);
    Q_INVOKABLE bool requestSatellites(const QString &host, int port);
    Q_INVOKABLE QVariantMap correctedFrequencies(double txMHz, double rxMHz) const;
    QVariantMap snapshot() const { return m_snapshot; }
    QVariantMap publicStatus() const;
    bool busy() const { return m_busy; }
    QStringList satellites() const { return m_satellites; }
  signals:
    void changed();
    void finished(bool success, const QString &error);
    void satellitesFinished(bool success, const QString &error);

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
    QByteArray m_pendingTle;
    QVariantMap m_snapshot;
    QStringList m_satellites;
    QStringList m_pendingSatellites;
    QString m_listMode;
    bool m_listing{false};
    bool m_busy{false};
};
} // namespace decolog::core
