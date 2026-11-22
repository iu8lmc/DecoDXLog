#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QTemporaryDir>
#include <QThreadPool>
#include <atomic>
#include <memory>

namespace decolog::core {
// Local, read-only OCR. Results are suggestions; no QSO or QSL state is changed.
class TesseractOcr : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString text READ text NOTIFY changed)
    Q_PROPERTY(QVariantList words READ words NOTIFY changed)
    Q_PROPERTY(QVariantMap suggestions READ suggestions NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString executable READ executable NOTIFY changed)
    Q_PROPERTY(QString snapshotSha256 READ snapshotSha256 NOTIFY changed)
  public:
    explicit TesseractOcr(const QString &executableOverride = {}, QObject *parent = nullptr);
    ~TesseractOcr() override;
    bool busy() const { return m_busy; }
    QString text() const { return m_text; }
    QVariantList words() const { return m_words; }
    QVariantMap suggestions() const { return m_suggestions; }
    static QVariantMap suggestionsFromWords(const QVariantList &words);
    QString error() const { return m_error; }
    QString executable() const { return m_executable; }
    QString snapshotSha256() const { return m_snapshotSha256; }
    QString snapshotPath() const { return m_snapshotPath; }
    QString sourcePath() const { return m_sourcePath; }
    Q_INVOKABLE bool start(const QUrl &image);
    Q_INVOKABLE void cancel();
  signals:
    void changed();
    void finished(bool success);
  private:
    void collect();
    void finish(bool success, const QString &message = {});
    void parse();
    QString m_executable;
    QProcess m_process;
    QThreadPool m_pool;
    QTemporaryDir m_tempDir;
    std::shared_ptr<std::atomic_bool> m_copyCancel;
    QTimer m_timeout;
    QByteArray m_output;
    QString m_text;
    QVariantList m_words;
    QVariantMap m_suggestions;
    QString m_error;
    QString m_snapshotPath;
    QString m_snapshotSha256;
    QString m_sourcePath;
    bool m_busy{false};
    bool m_cancelled{false};
    bool m_tooLarge{false};
};
} // namespace decolog::core
