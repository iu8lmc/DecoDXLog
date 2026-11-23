#pragma once
#include <QDate>
#include <QObject>
#include <QThreadPool>
#include <QUrl>
#include <QVariantMap>
#include <atomic>
#include <memory>

namespace decolog::core {
// A locally supplied SOTA CSV. Its claimed origin is not authenticated.
class SotaCatalogue : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QVariantMap info READ info NOTIFY changed)
public:
    explicit SotaCatalogue(const QString& path, QObject* parent = nullptr);
    ~SotaCatalogue() override;
    bool busy() const { return m_busy; }
    QString path() const { return m_path; }
    static QVariantMap lookupBatch(const QString& path, const QVariantList& requests, const std::shared_ptr<std::atomic_bool>& cancelled = {});
    QVariantMap info() const;
    Q_INVOKABLE bool importFile(const QUrl& source);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE QVariantMap lookup(const QString& reference, const QDate& date) const;
    Q_INVOKABLE QVariantMap lookupIso(const QString& reference, const QString& date) const;
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
