#pragma once
#include <QDate>
#include <QObject>
#include <QThreadPool>
#include <QPointer>
#include <QUrl>
#include <QVariantMap>
#include <atomic>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;
class QTemporaryFile;

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
    Q_INVOKABLE bool updateFromOfficial();
    // Non-QML test entry point, restricted to 127.0.0.1 and marked unverified.
    bool updateFromLoopbackForTest(const QUrl& source);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE QVariantMap lookup(const QString& reference, const QDate& date) const;
    Q_INVOKABLE QVariantMap lookupIso(const QString& reference, const QString& date) const;
signals:
    void changed();
    void finished(bool success, const QString& message);
private:
    bool startDownload(const QUrl& url, bool testLoopback);
    void issueRequest(const QUrl& url);
    void finishDownload(const QString& error);
    void launchBuild(const QString& input, const QString& source, const QString& sourceUrl, const QString& fileName);
    bool allowedUrl(const QUrl& url) const;
    QString m_path;
    QNetworkAccessManager* m_network{nullptr};
    QPointer<QNetworkReply> m_reply;
    std::unique_ptr<QTemporaryFile> m_download;
    QUrl m_initialUrl;
    QString m_downloadError;
    qint64 m_downloadBytes{0};
    int m_redirects{0};
    bool m_testLoopback{false};
    QThreadPool m_pool;
    std::shared_ptr<std::atomic_bool> m_cancel;
    bool m_busy{false};
};
}
