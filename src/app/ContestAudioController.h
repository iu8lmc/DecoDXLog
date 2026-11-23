#pragma once
#include "core/ContestAudioFile.h"
#include <QByteArray>
#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
class QAudioSource;
class QAudioSink;
class QFile;
class QIODevice;
class QMediaDevices;
namespace decolog::app {
class ContestAudioController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList inputs READ inputs NOTIFY devicesChanged)
    Q_PROPERTY(int inputIndex READ inputIndex NOTIFY devicesChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY changed)
    Q_PROPERTY(qint64 bytesWritten READ bytesWritten NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString lastPath READ lastPath NOTIFY changed)
    Q_PROPERTY(QStringList outputs READ outputs NOTIFY devicesChanged)
    Q_PROPERTY(int outputIndex READ outputIndex NOTIFY devicesChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
  public:
    explicit ContestAudioController(QObject *parent=nullptr);
    ~ContestAudioController() override;
    QStringList inputs() const;
    QStringList outputs() const;
    int outputIndex() const;
    bool playing() const;
    int inputIndex() const;
    bool recording() const { return m_recording; }
    qint64 bytesWritten() const { return m_file.bytesWritten(); }
    QString message() const { return m_message; }
    QString lastPath() const { return m_lastPath; }
    Q_INVOKABLE void chooseInput(int index);
    Q_INVOKABLE void chooseOutput(int index);
    Q_INVOKABLE bool playForQso(const QUrl &wav, const QString &qsoUtc);
    Q_INVOKABLE void stopPlayback();
    Q_INVOKABLE bool recoverFile(const QUrl &partial);
    Q_INVOKABLE bool start(const QUrl &directory, int limitMiB);
    Q_INVOKABLE void stop();
    // Allows a locally supplied 16 kHz mono PCM stream, including deterministic tests.
    bool startFromStream(QIODevice *stream, const QString &directory, qint64 maxBytes, const QDateTime &startUtc);
  signals:
    void changed();
    void devicesChanged();
    void finished(bool success, const QString &path, const QString &message);
  private:
    void drain();
    void finish(bool success, const QString &reason);
    core::ContestAudioFile m_file;
    QPointer<QIODevice> m_stream;
    QAudioSource *m_source{nullptr};
    QAudioSink *m_sink{nullptr};
    QFile *m_playbackFile{nullptr};
    QMediaDevices *m_devices{nullptr};
    QTimer m_checkpoint;
    QByteArray m_inputId, m_outputId;
    bool m_defaultOutput{true};
    bool m_defaultInput{true}, m_recording{false};
    QString m_message, m_lastPath;
};
}
