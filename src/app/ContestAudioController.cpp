#include "app/ContestAudioController.h"
#include "app/AudioDevices.h"
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSource>
#include <QAudioSink>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QLockFile>
#include <QMediaDevices>
#include <QSettings>
#include <QIODevice>
#include <QtEndian>

namespace decolog::app {
ContestAudioController::ContestAudioController(QObject *parent) : QObject(parent), m_devices(new QMediaDevices(this))
{
    QSettings settings;
    m_defaultInput=settings.value(QStringLiteral("contestAudio/inputDefault"),true).toBool();
    m_inputId=settings.value(QStringLiteral("contestAudio/inputId")).toByteArray();
    m_defaultOutput=settings.value(QStringLiteral("contestAudio/outputDefault"),true).toBool();
    m_outputId=settings.value(QStringLiteral("contestAudio/outputId")).toByteArray();
    connect(m_devices,&QMediaDevices::audioInputsChanged,this,[this] {
        QList<QByteArray> ids; for (const auto &device:QMediaDevices::audioInputs()) ids.append(device.id());
        reconcileInputDevices(ids); emit devicesChanged();
    });
    connect(m_devices,&QMediaDevices::audioOutputsChanged,this,[this] {
        QList<QByteArray> ids; for (const auto &device:QMediaDevices::audioOutputs()) ids.append(device.id());
        reconcileOutputDevices(ids); emit devicesChanged();
    });
    m_searchPool.setMaxThreadCount(1);
    m_playbackTimer.setInterval(250);
    connect(&m_playbackTimer,&QTimer::timeout,this,[this] { emit changed(); });
    m_checkpoint.setInterval(5000);
    connect(&m_checkpoint,&QTimer::timeout,this,[this] { if (m_recording && !m_file.checkpoint()) finish(false,m_file.error()); });
}
ContestAudioController::~ContestAudioController() { m_searchPool.waitForDone(); if (m_recording) stop(); stopPlayback(); }
void ContestAudioController::reconcileInputDevices(const QList<QByteArray> &ids)
{
    if (m_recording && m_source && !m_activeInputId.isEmpty() && !ids.contains(m_activeInputId))
        finish(false,tr("Audio input disconnected; recording left partial for recovery."));
}
void ContestAudioController::reconcileOutputDevices(const QList<QByteArray> &ids)
{
    if (m_sink && !m_activeOutputId.isEmpty() && !ids.contains(m_activeOutputId)) {
        stopPlayback(); m_message=tr("Audio output disconnected; playback stopped."); emit changed();
    }
}
QStringList ContestAudioController::inputs() const
{
    QStringList result{tr("System default")};
    result.append(audiodev::labels(audiodev::entriesOf(QMediaDevices::audioInputs())));
    return result;
}
int ContestAudioController::inputIndex() const
{
    if (m_defaultInput) return 0;
    if (m_inputId.isEmpty()) return -1;
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
    if (m_outputId.isEmpty()) return -1;
    const auto list=QMediaDevices::audioOutputs();
    for (int i=0;i<list.size();++i) if (list.at(i).id()==m_outputId) return i+1;
    return -1;
}
bool ContestAudioController::playing() const
{
    return m_sink && m_sink->state()!=QtAudio::StoppedState && m_sink->state()!=QtAudio::IdleState;
}
qint64 ContestAudioController::playbackPositionMs() const
{
    return qBound<qint64>(0,m_playbackBaseMs+(m_sink ? m_sink->processedUSecs()/1000 : 0),m_playbackDurationMs);
}
void ContestAudioController::chooseOutput(int index)
{
    if (playing()) return;
    const auto list=QMediaDevices::audioOutputs();
    if (index<0 || index>list.size()) return;
    m_defaultOutput=index==0; m_outputId=index ? list.at(index-1).id() : QByteArray();
    QSettings settings; settings.setValue(QStringLiteral("contestAudio/outputDefault"),m_defaultOutput);
    settings.setValue(QStringLiteral("contestAudio/outputId"),m_outputId); emit devicesChanged();
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
    else for (const auto &item:QMediaDevices::audioOutputs()) if (!m_outputId.isEmpty() && item.id()==m_outputId) { device=item; break; }
    QAudioFormat format; format.setSampleRate(location.sampleRate); format.setChannelCount(1); format.setSampleFormat(QAudioFormat::Int16);
    if (device.isNull() || !device.isFormatSupported(format)) {
        m_message=tr("Selected playback output is unavailable or does not support this recording format."); emit changed(); return false;
    }
    stopPlayback();
    m_playbackFile=new QFile(wav.toLocalFile(),this);
    const qint64 position=qMax<qint64>(0,location.offsetMs-10000);
    if (!m_playbackFile->open(QIODevice::ReadOnly) || !m_playbackFile->seek(44+(position*location.sampleRate/1000)*2)) {
        m_message=tr("Cannot open or seek the recording."); m_playbackFile->deleteLater(); m_playbackFile=nullptr; emit changed(); return false;
    }
    m_playbackBaseMs=position; m_playbackDurationMs=location.durationMs; m_playbackRate=location.sampleRate;
    m_activeOutputId=device.id();
    m_sink=new QAudioSink(device,format,this);
    connect(m_sink,&QAudioSink::stateChanged,this,[this](QtAudio::State state) {
        if (!m_sink || m_seeking) return;
        if (state==QtAudio::StoppedState && m_sink->error()!=QtAudio::NoError) m_message=tr("Audio playback stopped unexpectedly.");
        else if (state==QtAudio::IdleState) { m_message=tr("Playback finished."); m_playbackTimer.stop(); }
        emit changed();
    });
    m_message=tr("Playing from up to ten seconds before the QSO timestamp; timing is approximate.");
    m_sink->start(m_playbackFile); m_playbackTimer.start(); emit changed(); return true;
}
bool ContestAudioController::seekPlayback(int seconds)
{
    if (!m_sink || !m_playbackFile || m_playbackDurationMs<=0 || seconds < -3600 || seconds > 3600) return false;
    const qint64 target=qBound<qint64>(0,playbackPositionMs()+qint64(seconds)*1000,m_playbackDurationMs-1);
    m_seeking=true; m_sink->stop();
    if (!m_playbackFile->seek(44+(target*m_playbackRate/1000)*2)) {
        m_seeking=false; m_message=tr("Cannot seek the recording."); stopPlayback(); return false;
    }
    m_playbackBaseMs=target; m_sink->start(m_playbackFile); m_seeking=false;
    m_message=tr("Playback position changed; timing is approximate."); m_playbackTimer.start(); emit changed(); return true;
}
void ContestAudioController::stopPlayback()
{
    m_playbackTimer.stop();
    if (m_sink) { auto *sink=m_sink; m_sink=nullptr; sink->stop(); sink->deleteLater(); }
    if (m_playbackFile) { m_playbackFile->close(); m_playbackFile->deleteLater(); m_playbackFile=nullptr; }
    m_playbackBaseMs=0; m_playbackDurationMs=0; m_playbackRate=0; m_activeOutputId.clear();
    emit changed();
}
bool ContestAudioController::findRecordings(const QUrl &folder, const QString &qsoUtc)
{
    const QDateTime when=QDateTime::fromString(qsoUtc,Qt::ISODateWithMs);
    if (m_searchBusy || !folder.isLocalFile() || !qsoUtc.endsWith(QLatin1Char('Z'),Qt::CaseInsensitive) || !when.isValid()) return false;
    m_searchBusy=true; emit changed(); QPointer<ContestAudioController> self(this);
    m_searchPool.start([self,folder,qsoUtc,when] {
        const auto found=core::ContestAudioFile::findMatching(folder.toLocalFile(),when);
        QVariantList matches;
        for (const auto &item:found.matches)
            matches << QVariantMap{{QStringLiteral("url"),QUrl::fromLocalFile(item.path)},
                {QStringLiteral("startUtc"),item.location.startUtc.toString(Qt::ISODateWithMs)},
                {QStringLiteral("durationMs"),item.location.durationMs}, {QStringLiteral("offsetMs"),item.location.offsetMs}};
        const QVariantMap result{{QStringLiteral("folder"),folder},{QStringLiteral("qsoUtc"),qsoUtc},
            {QStringLiteral("matches"),matches},{QStringLiteral("inspected"),found.inspected},
            {QStringLiteral("truncated"),found.truncated},{QStringLiteral("error"),found.error}};
        if (!self) return;
        QMetaObject::invokeMethod(self.data(),[self,result] {
            if (!self) return;
            self->m_searchBusy=false; emit self->changed(); emit self->recordingsFound(result);
        },Qt::QueuedConnection);
    });
    return true;
}
bool ContestAudioController::recoverFile(const QUrl &partial)
{
    if (m_recording || playing() || !partial.isLocalFile()) {
        m_message=tr("Stop recording and playback, then select a local partial file."); emit changed(); return false;
    }
    QLockFile lock(QDir(QFileInfo(partial.toLocalFile()).absolutePath()).filePath(QStringLiteral(".decodxlog-contest-audio.lock")));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) { m_message=tr("Audio folder is in use by another recording or recovery."); emit changed(); return false; }
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
    QSettings settings; settings.setValue(QStringLiteral("contestAudio/inputDefault"),m_defaultInput);
    settings.setValue(QStringLiteral("contestAudio/inputId"),m_inputId); emit devicesChanged();
}
bool ContestAudioController::start(const QUrl &directory, int limitMiB)
{
    if (m_recording || !directory.isLocalFile() || limitMiB < 1 || limitMiB > 1024) {
        m_message=tr("Choose a local folder and a limit between 1 and 1024 MiB."); emit changed(); return false;
    }
    stopPlayback();
    QAudioDevice device;
    if (m_defaultInput) device=QMediaDevices::defaultAudioInput();
    else for (const auto &item : QMediaDevices::audioInputs()) if (!m_inputId.isEmpty() && item.id()==m_inputId) { device=item; break; }
    QAudioFormat format; format.setSampleRate(16000); format.setChannelCount(1); format.setSampleFormat(QAudioFormat::Int16);
    if (device.isNull()) { m_message=tr("Selected audio input is unavailable."); emit changed(); return false; }
    if (!device.isFormatSupported(format)) format=device.preferredFormat();
    if (!audiodev::canConvertToMonoInt16(format) || format.sampleRate()<8000 || format.sampleRate()>192000) {
        m_message=tr("Selected input has an unsupported audio format."); emit changed(); return false;
    }
    auto lock=std::make_unique<QLockFile>(QDir(directory.toLocalFile()).filePath(QStringLiteral(".decodxlog-contest-audio.lock")));
    lock->setStaleLockTime(0);
    if (!lock->tryLock(0)) { m_message=tr("Audio folder is in use or unavailable."); emit changed(); return false; }
    const auto budget=core::ContestAudioFile::folderBudget(directory.toLocalFile(),qint64(limitMiB)*1024*1024);
    if (!budget.error.isEmpty() || budget.remainingDataBytes<2) {
        m_message=budget.error.isEmpty() ? tr("Contest audio folder limit reached; choose a larger limit or another folder.") : budget.error;
        emit changed(); return false;
    }
    m_directoryLock=std::move(lock);
    m_activeInputId=device.id();
    m_source=new QAudioSource(device,format,this);
    connect(m_source,&QAudioSource::stateChanged,this,[this](QtAudio::State state) {
        if (m_recording && state==QtAudio::StoppedState && m_source && m_source->error()!=QtAudio::NoError)
            finish(false,tr("Audio input stopped unexpectedly."));
    });
    QIODevice *stream=m_source->start();
    if (!stream || !startFromStream(stream,directory.toLocalFile(),budget.remainingDataBytes,QDateTime::currentDateTimeUtc(),format)) {
        m_source->stop(); m_source->deleteLater(); m_source=nullptr; m_activeInputId.clear(); m_directoryLock.reset();
        if (m_message.isEmpty()) m_message=tr("Cannot start audio input.");
        emit changed(); return false;
    }
    return true;
}
bool ContestAudioController::startFromStream(QIODevice *stream, const QString &directory, qint64 maxBytes, const QDateTime &startUtc, const QAudioFormat &sourceFormat)
{
    if (m_recording || !stream || !stream->isReadable()) return false;
    m_inputFormat=sourceFormat;
    if (!m_inputFormat.isValid()) { m_inputFormat.setSampleRate(16000); m_inputFormat.setChannelCount(1); m_inputFormat.setSampleFormat(QAudioFormat::Int16); }
    if (!audiodev::canConvertToMonoInt16(m_inputFormat) || m_inputFormat.sampleRate()<8000 || m_inputFormat.sampleRate()>192000) {
        m_message=tr("Unsupported audio format."); emit changed(); return false;
    }
    if (!m_file.start(directory,maxBytes,startUtc,m_inputFormat.sampleRate())) { m_message=m_file.error(); emit changed(); return false; }
    m_inputPending.clear();
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
    m_inputPending.append(m_stream->readAll());
    const auto converted=audiodev::convertToMonoInt16(m_inputPending,m_inputFormat);
    if (!converted.consumedBytes) return;
    m_inputPending.remove(0,converted.consumedBytes);
    QByteArray samples=converted.samples;
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    for (qsizetype i=0;i+1<samples.size();i+=2) { const char first=samples.at(i); samples[i]=samples.at(i+1); samples[i+1]=first; }
#endif
    const auto result=m_file.append(samples);
    if (!result.error.isEmpty()) { finish(false,result.error); return; }
    emit changed();
    if (result.limitReached) { m_inputPending.clear(); finish(true,tr("Recording stopped at the size limit.")); }
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
    m_activeInputId.clear();
    QString path=m_file.partialPath();
    if (success && !m_inputPending.isEmpty()) { success=false; m_file.abandon(); m_message=tr("Incomplete audio frame; recover the partial recording."); }
    else if (success && !m_file.finish(&path)) { success=false; m_message=m_file.error(); m_file.abandon(); }
    else if (!success) { m_file.abandon(); m_message=reason; }
    else m_message=reason;
    m_directoryLock.reset(); m_lastPath=path; emit changed(); emit finished(success,path,m_message);
}
}
