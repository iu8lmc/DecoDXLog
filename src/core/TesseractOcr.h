#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

namespace decolog::core {
// Local, read-only OCR. Results are suggestions; no QSO or QSL state is changed.
class TesseractOcr : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString text READ text NOTIFY changed)
    Q_PROPERTY(QVariantList words READ words NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString executable READ executable NOTIFY changed)
  public:
    explicit TesseractOcr(const QString &executableOverride = {}, QObject *parent = nullptr);
    ~TesseractOcr() override;
    bool busy() const { return m_busy; }
    QString text() const { return m_text; }
    QVariantList words() const { return m_words; }
    QString error() const { return m_error; }
    QString executable() const { return m_executable; }
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
    QTimer m_timeout;
    QByteArray m_output;
    QString m_text;
    QVariantList m_words;
    QString m_error;
    bool m_busy{false};
    bool m_cancelled{false};
    bool m_tooLarge{false};
};
} // namespace decolog::core
