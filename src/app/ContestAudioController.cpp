#include "app/ContestAudioController.h"
#include "app/AudioDevices.h"
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSource>
#include <QAudioSink>
#include <QFile>
#include <QMediaDevices>
#include <QIODevice>

namespace decolog::app {
ContestAudioController::ContestAudioController(QObject *parent) : QObject(parent), m_devices(new QMediaDevices(this))
{
    connect(m_devices,&QMediaDevices::audioInputsChanged,this,&ContestAudioController::devicesChanged);
    connect(m_devices,&QMediaDevices::audioOutputsChanged,this,&ContestAudioController::devicesChanged);
    m_checkpoint.setInterval(5000);
    connect(&m_checkpoint,&QTimer::timeout,this,[this] { if (m_recording && !m_file.checkpoint()) finish(false,m_file.error()); });
}
ContestAudioController::~ContestAudioController() { if (m_recording) stop(); stopPlayback(); }
QStringList ContestAudioController::inputs() const
{
    QStringList result{tr("System default")};
    result.append(audiodev::labels(audiodev::entriesOf(QMediaDevices::audioInputs())));
    return result;
}
int ContestAudioController::inputIndex() const
{
    if (m_defaultInput) return 0;
    const auto list=QMediaDevices::audioInputs();
    for (int i=0;i<list.size();++i) if (list.at(i).id()==m_inputId) return i+1;
    return -1;
}
QStringList ContestAudioController::outputs() const
{
    QStringList result{tr("System default")};
    result.append(audiodev::labels(audiodev::entriesOf(QMediaDevices::audioOutputs())));
    return result;
}
int ContestAudioController::outputIndex() const
{
    if (m_defaultOutput) return 0;
    const auto list=QMediaDevices::audioOutputs();
    for (int i=0;i<list.size();++i) if (list.at(i).id()==m_outputId) return i+1;
    return -1;
}
bool ContestAudioController::playing() const
{
    return m_sink && m_sink->state()!=QtAudio::StoppedState && m_sink->state()!=QtAudio::IdleState;
}
void ContestAudioController::chooseOutput(int index)
{
    if (playing()) return;
    const auto list=QMediaDevices::audioOutputs();
    if (index<0 || index>list.size()) return;
    m_defaultOutput=index==0; m_outputId=index ? list.at(index-1).id() : QByteArray(); emit devicesChanged();
}
bool ContestAudioController::playForQso(const QUrl &wav, const QString &qsoUtc)
{
    if (m_recording || !wav.isLocalFile() || !qsoUtc.endsWith(QLatin1Char('Z'),Qt::CaseInsensitive)) {
        m_message=tr("Select a completed local WAV and a valid UTC QSO time; stop recording first."); emit changed(); return false;
    }
    const QDateTime when=QDateTime::fromString(qsoUtc,Qt::ISODateWithMs);
    const auto location=core::ContestAudioFile::locate(wav.toLocalFile(),when);
    if (!location.error.isEmpty()) { m_message=location.error; emit changed(); return false; }
    QAudioDevice device;
    if (m_defaultOutput) device=QMediaDevices::defaultAudioOutput();
    else for (const auto &item:QMediaDevices::audioOutputs()) if (item.id()==m_outputId) { device=item; break; }
    QAudioFormat format; format.setSampleRate(16000); format.setChannelCount(1); format.setSampleFormat(QAudioFormat::Int16);
    if (device.isNull() || !device.isFormatSupported(format)) {
        m_message=tr("Selected playback output is unavailable or cannot play 16 kHz mono PCM."); emit changed(); return false;
    }
    stopPlayback();
    m_playbackFile=new QFile(wav.toLocalFile(),this);
    const qint64 position=qMax<qint64>(0,location.offsetMs-10000);
    if (!m_playbackFile->open(QIODevice::ReadOnly) || !m_playbackFile->seek(44+position*32)) {
        m_message=tr("Cannot open or seek the recording."); m_playbackFile->deleteLater(); m_playbackFile=nullptr; emit changed(); return false;
    }
    m_sink=new QAudioSink(device,format,this);
    connect(m_sink,&QAudioSink::stateChanged,this,[this](QtAudio::State state) {
        if (!m_sink) return;
        if (state==QtAudio::StoppedState && m_sink->error()!=QtAudio::NoError) m_message=tr("Audio playback stopped unexpectedly.");
        else if (state==QtAudio::IdleState) m_message=tr("Playback finished.");
        emit changed();
    });
    m_message=tr("Playing from up to ten seconds before the QSO timestamp; timing is approximate.");
    m_sink->start(m_playbackFile); emit changed(); return true;
}
void ContestAudioController::stopPlayback()
{
    if (m_sink) { auto *sink=m_sink; m_sink=nullptr; sink->stop(); sink->deleteLater(); }
    if (m_playbackFile) { m_playbackFile->close(); m_playbackFile->deleteLater(); m_playbackFile=nullptr; }
    emit changed();
}
bool ContestAudioController::recoverFile(const QUrl &partial)
{
    if (m_recording || playing() || !partial.isLocalFile()) {
        m_message=tr("Stop recording and playback, then select a local partial file."); emit changed(); return false;
    }
    QString output, error;
    if (!core::ContestAudioFile::recover(partial.toLocalFile(),&output,&error)) {
        m_message=tr("Recovery failed: %1").arg(error); emit changed(); return false;
    }
    m_lastPath=output;
    m_message=tr("Recording recovered. Review its timing and audio before use."); emit changed(); return true;
}
void ContestAudioController::chooseInput(int index)
{
    if (m_recording) return;
    const auto list=QMediaDevices::audioInputs();
    if (index < 0 || index > list.size()) return;
    m_defaultInput=index==0; m_inputId=index ? list.at(index-1).id() : QByteArray();
    emit devicesChanged();
}
bool ContestAudioController::start(const QUrl &directory, int limitMiB)
{
    if (m_recording || !directory.isLocalFile() || limitMiB < 1 || limitMiB > 1024) {
        m_message=tr("Choose a local folder and a limit between 1 and 1024 MiB."); emit changed(); return false;
    }
    stopPlayback();
    QAudioDevice device;
    if (m_defaultInput) device=QMediaDevices::defaultAudioInput();
    else for (const auto &item : QMediaDevices::audioInputs()) if (item.id()==m_inputId) { device=item; break; }
    QAudioFormat format; format.setSampleRate(16000); format.setChannelCount(1); format.setSampleFormat(QAudioFormat::Int16);
    if (device.isNull() || !device.isFormatSupported(format)) {
        m_message=tr("Selected input is unavailable or cannot capture 16 kHz mono PCM."); emit changed(); return false;
    }
    m_source=new QAudioSource(device,format,this);
    connect(m_source,&QAudioSource::stateChanged,this,[this](QtAudio::State state) {
        if (m_recording && state==QtAudio::StoppedState && m_source && m_source->error()!=QtAudio::NoError)
            finish(false,tr("Audio input stopped unexpectedly."));
    });
    QIODevice *stream=m_source->start();
    if (!stream || !startFromStream(stream,directory.toLocalFile(),qint64(limitMiB)*1024*1024,QDateTime::currentDateTimeUtc())) {
        m_source->stop(); m_source->deleteLater(); m_source=nullptr;
        if (m_message.isEmpty()) m_message=tr("Cannot start audio input.");
        emit changed(); return false;
    }
    return true;
}
bool ContestAudioController::startFromStream(QIODevice *stream, const QString &directory, qint64 maxBytes, const QDateTime &startUtc)
{
    if (m_recording || !stream || !stream->isReadable()) return false;
    if (!m_file.start(directory,maxBytes,startUtc)) { m_message=m_file.error(); emit changed(); return false; }
    m_stream=stream; m_recording=true; m_lastPath.clear(); m_message=tr("Recording session audio.");
    connect(stream,&QIODevice::readyRead,this,&ContestAudioController::drain);
    connect(stream,&QObject::destroyed,this,[this] { if (m_recording) finish(false,tr("Audio stream ended unexpectedly.")); });
    m_checkpoint.start(); emit changed();
    drain();
    return true;
}
void ContestAudioController::drain()
{
    if (!m_recording || !m_stream) return;
    const QByteArray data=m_stream->readAll();
    if (data.isEmpty()) return;
    const auto result=m_file.append(data);
    if (!result.error.isEmpty()) { finish(false,result.error); return; }
    emit changed();
    if (result.limitReached) finish(true,tr("Recording stopped at the size limit."));
}
void ContestAudioController::stop()
{
    if (!m_recording) return;
    drain();
    if (m_recording) finish(true,tr("Recording stopped."));
}
void ContestAudioController::finish(bool success, const QString &reason)
{
    if (!m_recording) return;
    m_recording=false; m_checkpoint.stop();
    if (m_stream) m_stream->disconnect(this);
    m_stream=nullptr;
    if (m_source) { auto *source=m_source; m_source=nullptr; source->stop(); source->deleteLater(); }
    QString path=m_file.partialPath();
    if (success && !m_file.finish(&path)) { success=false; m_message=m_file.error(); m_file.abandon(); }
    else if (!success) { m_file.abandon(); m_message=reason; }
    else m_message=reason;
    m_lastPath=path; emit changed(); emit finished(success,path,m_message);
}
}
