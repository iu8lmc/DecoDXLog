#pragma once
#include <QDateTime>
#include <QFile>
#include <QString>

namespace decolog::core {
// Fixed PCM: 16 kHz, mono, signed 16-bit little-endian. No audio-device or PTT access.
class ContestAudioFile {
  public:
    struct WriteResult { qint64 written{0}; bool limitReached{false}; QString error; };
    bool start(const QString &directory, qint64 maxDataBytes, const QDateTime &startUtc);
    WriteResult append(const QByteArray &data);
    bool checkpoint(); // flushes userspace buffers; not a power-loss durability guarantee
    bool finish(QString *outputPath=nullptr);
    void abandon(); // leave the partial file for explicit recovery
    QString partialPath() const { return m_path; }
    QString error() const { return m_error; }
    qint64 bytesWritten() const { return m_bytes; }
    static bool recover(const QString &partialPath, QString *outputPath, QString *error);
  private:
    QFile m_file;
    QString m_path, m_error;
    QByteArray m_pending;
    qint64 m_limit{0}, m_bytes{0};
};
}
