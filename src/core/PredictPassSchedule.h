#pragma once
#include "core/PredictPassPlanner.h"
#include <QObject>
#include <QVariantList>

namespace decolog::core {
// Bounded sequential, read-only predictions. Partial results remain visible on failure.
class PredictPassSchedule : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList passes READ passes NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
  public:
    explicit PredictPassSchedule(QObject *parent = nullptr);
    Q_INVOKABLE bool request(const QString &host, int port, const QString &satellite, qint64 startUnix, int count);
    Q_INVOKABLE void cancel();
    QVariantList passes() const { return m_passes; }
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
  signals:
    void changed();
    void finished(bool success, const QString &error);
  private:
    void finish(bool success, const QString &error);
    PredictPassPlanner m_planner;
    QVariantList m_passes;
    QString m_host, m_satellite, m_fingerprint, m_status;
    int m_port{0}, m_count{0};
    quint64 m_generation{0};
    bool m_busy{false};
};
}
