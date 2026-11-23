#pragma once
#include "core/ContestAudioFile.h"
#include <QByteArray>
#include <QAudioFormat>
#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QThreadPool>
#include <QUrl>
#include <memory>
class QAudioSource;
class QAudioSink;
class QFile;
class QIODevice;
class QMediaDevices;
class QLockFile;
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
    Q_PROPERTY(qint64 playbackPositionMs READ playbackPositionMs NOTIFY changed)
    Q_PROPERTY(qint64 playbackDurationMs READ playbackDurationMs NOTIFY changed)
    Q_PROPERTY(bool searchBusy READ searchBusy NOTIFY changed)
  public:
    explicit ContestAudioController(QObject *parent=nullptr);
    ~ContestAudioController() override;
    QStringList inputs() const;
    QStringList outputs() const;
    int outputIndex() const;
    bool playing() const;
    qint64 playbackPositionMs() const;
    qint64 playbackDurationMs() const { return m_playbackDurationMs; }
    bool searchBusy() const { return m_searchBusy; }
    int inputIndex() const;
    bool recording() const { return m_recording; }
    qint64 bytesWritten() const { return m_file.bytesWritten(); }
    QString message() const { return m_message; }
    QString lastPath() const { return m_lastPath; }
    Q_INVOKABLE void chooseInput(int index);
    Q_INVOKABLE void chooseOutput(int index);
    Q_INVOKABLE bool playForQso(const QUrl &wav, const QString &qsoUtc);
    Q_INVOKABLE void stopPlayback();
    Q_INVOKABLE bool seekPlayback(int seconds);
    Q_INVOKABLE bool recoverFile(const QUrl &partial);
    Q_INVOKABLE bool findRecordings(const QUrl &folder, const QString &qsoUtc);
    // Reconcile the active streams against a fresh backend device inventory.
    void reconcileInputDevices(const QList<QByteArray> &ids);
    void reconcileOutputDevices(const QList<QByteArray> &ids);
    Q_INVOKABLE bool start(const QUrl &directory, int limitMiB);
    Q_INVOKABLE void stop();
    // Locally supplied PCM stream; an invalid format means 16 kHz mono Int16.
    bool startFromStream(QIODevice *stream, const QString &directory, qint64 maxBytes, const QDateTime &startUtc,
                         const QAudioFormat &format=QAudioFormat());
  signals:
    void changed();
    void devicesChanged();
    void finished(bool success, const QString &path, const QString &message);
    void recordingsFound(const QVariantMap &result);
  private:
    void drain();
    void finish(bool success, const QString &reason);
    core::ContestAudioFile m_file;
    QPointer<QIODevice> m_stream;
    QAudioSource *m_source{nullptr};
    QAudioSink *m_sink{nullptr};
    QFile *m_playbackFile{nullptr};
    QMediaDevices *m_devices{nullptr};
    QTimer m_checkpoint, m_playbackTimer;
    qint64 m_playbackBaseMs{0}, m_playbackDurationMs{0};
    int m_playbackRate{0};
    bool m_seeking{false};
    QThreadPool m_searchPool;
    std::unique_ptr<QLockFile> m_directoryLock;
    bool m_searchBusy{false};
    QByteArray m_inputId, m_outputId, m_activeInputId, m_activeOutputId, m_inputPending;
    QAudioFormat m_inputFormat;
    bool m_defaultOutput{true};
    bool m_defaultInput{true}, m_recording{false};
    QString m_message, m_lastPath;
};
}
