#pragma once
#include <QObject>
#include <QThreadPool>
#include <QUrl>
#include <QVariantMap>
#include <atomic>
#include <memory>

namespace decolog::core {
// Imported snapshot, not a historical record of park eligibility.
class PotaCatalogue : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QVariantMap info READ info NOTIFY changed)
public:
    explicit PotaCatalogue(const QString& path, QObject* parent = nullptr);
    ~PotaCatalogue() override;
    bool busy() const { return m_busy; }
    QString path() const { return m_path; }
    QVariantMap info() const;
    Q_INVOKABLE QVariantMap lookup(const QString& reference) const;
    static QVariantMap lookupBatch(const QString& path, const QVariantList& references, const std::shared_ptr<std::atomic_bool>& cancelled = {});
    Q_INVOKABLE bool importFile(const QUrl& source);
    Q_INVOKABLE void cancel();
signals:
    void changed();
    void finished(bool success, const QString& message);
private:
    QString m_path;
    QThreadPool m_pool;
    std::shared_ptr<std::atomic_bool> m_cancel;
    bool m_busy{false};
};
}
