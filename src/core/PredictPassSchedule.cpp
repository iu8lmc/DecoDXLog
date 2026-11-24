#include "core/PredictPassSchedule.h"
#include <QTimer>

namespace decolog::core {
PredictPassSchedule::PredictPassSchedule(QObject *parent) : QObject(parent)
{
    connect(&m_planner, &PredictPassPlanner::finished, this, [this](bool success, const QString &error) {
        if (!m_busy) return;
        if (!success) { finish(false, error); return; }
        const QVariantMap pass = m_planner.pass();
        const QString fingerprint = pass.value(QStringLiteral("tleFingerprint")).toString();
        if (fingerprint.isEmpty() || (!m_fingerprint.isEmpty() && fingerprint != m_fingerprint)) {
            finish(false, QStringLiteral("Orbital data changed between passes"));
            return;
        }
        if (!m_passes.isEmpty() && pass.value(QStringLiteral("aos")).toLongLong()
                <= m_passes.last().toMap().value(QStringLiteral("los")).toLongLong()) {
            finish(false, QStringLiteral("Tracker returned overlapping passes"));
            return;
        }
        m_fingerprint = fingerprint;
        m_passes.append(pass);
        if (m_passes.size() == m_count) { finish(true, {}); return; }
        const qint64 next = pass.value(QStringLiteral("los")).toLongLong() + 1;
        m_status = QStringLiteral("Predicting %1 of %2").arg(m_passes.size() + 1).arg(m_count);
        emit changed();
        const quint64 generation = m_generation;
        QTimer::singleShot(0, this, [this, generation, next] {
            if (!m_busy || generation != m_generation) return;
            if (!m_planner.request(m_host, m_port, m_satellite, next))
                finish(false, QStringLiteral("Cannot request next pass"));
        });
    });
}
bool PredictPassSchedule::request(const QString &host, int port, const QString &satellite, qint64 startUnix, int count)
{
    if (m_busy || count < 1 || count > 10) return false;
    m_host = host; m_port = port; m_satellite = satellite; m_count = count;
    m_passes.clear(); m_fingerprint.clear(); m_status = QStringLiteral("Predicting 1 of %1").arg(count);
    m_busy = true; ++m_generation;
    if (!m_planner.request(host, port, satellite, startUnix)) {
        m_busy = false; m_status.clear(); emit changed(); return false;
    }
    emit changed();
    return true;
}
void PredictPassSchedule::cancel()
{
    if (!m_busy) return;
    if (m_planner.busy()) m_planner.cancel();
    else finish(false, QStringLiteral("Pass schedule cancelled"));
}
void PredictPassSchedule::finish(bool success, const QString &error)
{
    m_busy = false; ++m_generation;
    m_status = success ? QStringLiteral("Complete") : error;
    emit changed();
    emit finished(success, error);
}
}
