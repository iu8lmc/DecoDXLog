#pragma once
#include <QDateTime>
#include <QFile>
#include <QString>

namespace decolog::core {
// Mono signed 16-bit little-endian PCM. No audio-device or PTT access.
class ContestAudioFile {
  public:
    struct WriteResult { qint64 written{0}; bool limitReached{false}; QString error; };
    struct Location { qint64 offsetMs{0}; qint64 durationMs{0}; int sampleRate{0}; QDateTime startUtc; QString error; };
    bool start(const QString &directory, qint64 maxDataBytes, const QDateTime &startUtc, int sampleRate=16000);
    WriteResult append(const QByteArray &data);
    bool checkpoint(); // flushes userspace buffers; not a power-loss durability guarantee
    bool finish(QString *outputPath=nullptr);
    void abandon(); // leave the partial file for explicit recovery
    QString partialPath() const { return m_path; }
    QString error() const { return m_error; }
    qint64 bytesWritten() const { return m_bytes; }
    static bool recover(const QString &partialPath, QString *outputPath, QString *error);
    static Location locate(const QString &wavPath, const QDateTime &qsoUtc);
  private:
    QFile m_file;
    QString m_path, m_error;
    QByteArray m_pending;
    qint64 m_limit{0}, m_bytes{0};
};
}
