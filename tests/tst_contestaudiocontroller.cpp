#include "app/ContestAudioController.h"
#include <QFile>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QAudioSource>
#include <QMediaDevices>
#include <QBuffer>
#include <QtEndian>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QCoreApplication>
#include <QSettings>
#include <QTest>
class Feed : public QIODevice {
    Q_OBJECT
  public:
    Feed() { open(QIODevice::ReadOnly); }
    bool isSequential() const override { return true; }
    void push(const QByteArray &data) { buffer+=data; emit readyRead(); }
    qint64 bytesAvailable() const override { return buffer.size()+QIODevice::bytesAvailable(); }
  protected:
    qint64 readData(char *data,qint64 max) override { const qint64 n=qMin(max,qint64(buffer.size())); memcpy(data,buffer.constData(),size_t(n)); buffer.remove(0,n); return n; }
    qint64 writeData(const char*,qint64) override { return -1; }
  private: QByteArray buffer;
};
class TestContestAudioController : public QObject {
    Q_OBJECT
  private slots:
    void virtualLoopbackWhenConfigured()
    {
        const QString deviceName=qEnvironmentVariable("DECOLOG_TEST_LOOPBACK");
        if (deviceName.isEmpty()) QSKIP("Set DECOLOG_TEST_LOOPBACK to an isolated virtual audio device");
        const auto inputs=QMediaDevices::audioInputs(), outputs=QMediaDevices::audioOutputs();
        QAudioDevice input,output; int inputRow=-1;
        for (int i=0;i<inputs.size();++i) if (inputs[i].description()==deviceName) { input=inputs[i]; inputRow=i+1; break; }
        for (const auto &item:outputs) if (item.description()==deviceName) { output=item; break; }
        QVERIFY2(!input.isNull() && !output.isNull(),"Virtual input/output not found");
        QAudioFormat format; format.setSampleRate(16000); format.setChannelCount(1); format.setSampleFormat(QAudioFormat::Int16);
        QVERIFY2(input.isFormatSupported(format) && output.isFormatSupported(format),"Virtual device does not support 16 kHz mono PCM");
        QTemporaryDir dir; QVERIFY(dir.isValid()); decolog::app::ContestAudioController recorder;
        recorder.chooseInput(inputRow); QVERIFY2(recorder.start(QUrl::fromLocalFile(dir.path()),1),qPrintable(recorder.message()));
        QByteArray samples; for (int i=0;i<16000;++i) { char b[2]; qToLittleEndian<qint16>((i%100<50)?12000:-12000,b); samples.append(b,2); }
        QBuffer source(&samples); QVERIFY(source.open(QIODevice::ReadOnly)); QAudioSink sink(output,format); sink.start(&source);
        QTest::qWait(1400); sink.stop(); recorder.stop();
        QFile wav(recorder.lastPath()); QVERIFY2(wav.open(QIODevice::ReadOnly),qPrintable(recorder.message()));
        const auto pcm=wav.readAll().mid(44); QVERIFY(pcm.size()>4000);
        bool signal=false; for (int i=0;i+1<pcm.size();i+=2) if (qAbs(qFromLittleEndian<qint16>(pcm.constData()+i))>1000) { signal=true; break; }
        QVERIFY2(signal,"Virtual loopback recording contains no test signal");
    }
    void playsQsoThroughVirtualOutputWhenConfigured()
    {
        const QString name=qEnvironmentVariable("DECOLOG_TEST_LOOPBACK");
        if (name.isEmpty()) QSKIP("Set DECOLOG_TEST_LOOPBACK to an isolated virtual audio device");
        const auto inputs=QMediaDevices::audioInputs(), outputs=QMediaDevices::audioOutputs();
        QAudioDevice input; int outputRow=-1;
        for (const auto &item:inputs) if (item.description()==name) { input=item; break; }
        for (int i=0;i<outputs.size();++i) if (outputs[i].description()==name) { outputRow=i+1; break; }
        QVERIFY(!input.isNull()); QVERIFY(outputRow>0);
        QAudioFormat format; format.setSampleRate(48000); format.setChannelCount(1); format.setSampleFormat(QAudioFormat::Int16);
        QVERIFY(input.isFormatSupported(format)); QTemporaryDir dir; QVERIFY(dir.isValid());
        decolog::core::ContestAudioFile writer; const auto start=QDateTime::fromString("2026-11-23T12:00:00Z",Qt::ISODate);
        QVERIFY(writer.start(dir.path(),192000,start,48000)); QByteArray samples;
        for (int i=0;i<96000;++i) { char b[2]; qToLittleEndian<qint16>((i%100<50)?12000:-12000,b); samples.append(b,2); }
        QCOMPARE(writer.append(samples).written,qint64(samples.size())); QString path; QVERIFY(writer.finish(&path));
        QAudioSource source(input,format); QIODevice *stream=source.start(); QVERIFY(stream); QByteArray captured;
        connect(stream,&QIODevice::readyRead,&source,[&] { captured+=stream->readAll(); });
        decolog::app::ContestAudioController player; player.chooseOutput(outputRow);
        QVERIFY(!player.playForQso(QUrl::fromLocalFile(path),"2026-11-23T12:00:03Z"));
        QVERIFY2(player.playForQso(QUrl::fromLocalFile(path),"2026-11-23T12:00:01Z"),qPrintable(player.message()));
        QCOMPARE(player.playbackDurationMs(),qint64(2000));
        QVERIFY(player.seekPlayback(1)); QVERIFY(player.playbackPositionMs() >= 1000);
        QVERIFY(player.seekPlayback(-10)); QVERIFY(player.playbackPositionMs() < 100);
        QTest::qWait(1700); player.stopPlayback(); source.stop();
        bool signal=false; for (int i=0;i+1<captured.size();i+=2) if (qAbs(qFromLittleEndian<qint16>(captured.constData()+i))>1000) { signal=true; break; }
        QVERIFY2(signal,"No audio observed from QSO playback through virtual output");
    }
    void activeDeviceLossStopsWithoutFallbackWhenConfigured()
    {
        const QString name=qEnvironmentVariable("DECOLOG_TEST_LOOPBACK");
        if (name.isEmpty()) QSKIP("Set DECOLOG_TEST_LOOPBACK to an isolated virtual audio device");
        const auto inputs=QMediaDevices::audioInputs(),outputs=QMediaDevices::audioOutputs(); int in=-1,out=-1;
        for (int i=0;i<inputs.size();++i) if (inputs[i].description()==name) { in=i+1; break; }
        for (int i=0;i<outputs.size();++i) if (outputs[i].description()==name) { out=i+1; break; }
        QVERIFY(in>0 && out>0); QTemporaryDir dir; QVERIFY(dir.isValid());
        decolog::app::ContestAudioController controller; controller.chooseInput(in); QSignalSpy done(&controller,&decolog::app::ContestAudioController::finished);
        QVERIFY2(controller.start(QUrl::fromLocalFile(dir.path()),1),qPrintable(controller.message()));
        QTest::qWait(300); QVERIFY(controller.bytesWritten()>0);
        controller.reconcileInputDevices({inputs[in-1].id()}); QVERIFY(controller.recording());
        controller.reconcileInputDevices({}); QVERIFY(!controller.recording()); QCOMPARE(done.size(),1); QVERIFY(!done.first().at(0).toBool());
        const QString partial=done.first().at(1).toString(); QVERIFY(partial.endsWith(".wav.part")); QVERIFY(QFile::exists(partial));
        QString recovered,error; QVERIFY2(decolog::core::ContestAudioFile::recover(partial,&recovered,&error),qPrintable(error));
        QVERIFY(QFileInfo(recovered).size()>44);
        decolog::core::ContestAudioFile writer; const auto start=QDateTime::fromString("2026-11-23T12:00:00Z",Qt::ISODate);
        QVERIFY(writer.start(dir.path(),64000,start)); QCOMPARE(writer.append(QByteArray(64000,'x')).written,qint64(64000)); QString path; QVERIFY(writer.finish(&path));
        controller.chooseOutput(out); QVERIFY(controller.playForQso(QUrl::fromLocalFile(path),"2026-11-23T12:00:01Z"));
        controller.reconcileOutputDevices({outputs[out-1].id()}); QVERIFY(controller.playing());
        controller.reconcileOutputDevices({}); QVERIFY(!controller.playing()); QVERIFY(controller.message().contains("disconnected",Qt::CaseInsensitive));
    }
    void capturesFragmentedNativeStereoFloat()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); Feed feed; decolog::app::ContestAudioController recorder;
        QAudioFormat format; format.setSampleRate(48000); format.setChannelCount(2); format.setSampleFormat(QAudioFormat::Float);
        QVERIFY(recorder.startFromStream(&feed,dir.path(),320,QDateTime::currentDateTimeUtc(),format));
        const float samples[]={0.5f,0.5f,-0.5f,-0.5f}; QByteArray data(reinterpret_cast<const char*>(samples),sizeof(samples));
        feed.push(data.left(5)); QCOMPARE(recorder.bytesWritten(),qint64(0)); feed.push(data.mid(5));
        QCOMPARE(recorder.bytesWritten(),qint64(4)); recorder.stop();
        QFile wav(recorder.lastPath()); QVERIFY(wav.open(QIODevice::ReadOnly)); const QByteArray bytes=wav.readAll();
        QCOMPARE(qFromLittleEndian<quint32>(bytes.constData()+24),quint32(48000));
        QCOMPARE(qFromLittleEndian<qint16>(bytes.constData()+44),qint16(16384));
        QCOMPARE(qFromLittleEndian<qint16>(bytes.constData()+46),qint16(-16384));
    }
    void asynchronouslyFindsRecordingsWithoutChangingFiles()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); decolog::core::ContestAudioFile writer;
        const auto start=QDateTime::fromString("2026-11-23T12:00:00Z",Qt::ISODate);
        QVERIFY(writer.start(dir.path(),64000,start)); QCOMPARE(writer.append(QByteArray(64000,'x')).written,qint64(64000));
        QString path; QVERIFY(writer.finish(&path)); decolog::app::ContestAudioController controller;
        QSignalSpy done(&controller,&decolog::app::ContestAudioController::recordingsFound);
        QVERIFY(!controller.findRecordings(QUrl::fromLocalFile(dir.path()),"invalid"));
        QVERIFY(controller.findRecordings(QUrl::fromLocalFile(dir.path()),"2026-11-23T12:00:01Z"));
        QVERIFY(!controller.findRecordings(QUrl::fromLocalFile(dir.path()),"2026-11-23T12:00:01Z"));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,3000); const auto result=done.first().at(0).toMap();
        QVERIFY(result.value("error").toString().isEmpty()); QCOMPARE(result.value("matches").toList().size(),1);
        QCOMPARE(result.value("matches").toList().first().toMap().value("url").toUrl(),QUrl::fromLocalFile(path));
        QCOMPARE(result.value("qsoUtc").toString(),QString("2026-11-23T12:00:01Z")); QVERIFY(QFile::exists(path));
    }
    void missingSavedDevicesAreNotReplacedByDefaults()
    {
        QSettings settings; settings.clear();
        settings.setValue("contestAudio/inputDefault",false); settings.setValue("contestAudio/inputId",QByteArray("missing-input"));
        settings.setValue("contestAudio/outputDefault",false); settings.setValue("contestAudio/outputId",QByteArray("missing-output")); settings.sync();
        { decolog::app::ContestAudioController recorder; QCOMPARE(recorder.inputIndex(),-1); QCOMPARE(recorder.outputIndex(),-1);
          QTemporaryDir dir; QVERIFY(dir.isValid()); QVERIFY(!recorder.start(QUrl::fromLocalFile(dir.path()),1));
          decolog::core::ContestAudioFile writer; const auto start=QDateTime::fromString("2026-11-23T12:00:00Z",Qt::ISODate);
          QVERIFY(writer.start(dir.path(),64000,start)); QCOMPARE(writer.append(QByteArray(64000,'x')).written,qint64(64000));
          QString wav; QVERIFY(writer.finish(&wav)); QVERIFY(!recorder.playForQso(QUrl::fromLocalFile(wav),"2026-11-23T12:00:01Z"));
          recorder.chooseInput(0); recorder.chooseOutput(0); }
        { decolog::app::ContestAudioController recorder; QCOMPARE(recorder.inputIndex(),0); QCOMPARE(recorder.outputIndex(),0); }
        settings.clear(); settings.sync();
    }
    void capturesStreamAndStopsAtLimit()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); Feed feed;
        decolog::app::ContestAudioController recorder;
        QSignalSpy finished(&recorder,&decolog::app::ContestAudioController::finished);
        QVERIFY(recorder.startFromStream(&feed,dir.path(),8,QDateTime::currentDateTimeUtc()));
        QVERIFY(recorder.recording()); feed.push(QByteArray::fromHex("010203"));
        QCOMPARE(recorder.bytesWritten(),qint64(2)); feed.push(QByteArray::fromHex("0405060708"));
        QCOMPARE(finished.size(),1); QVERIFY(!recorder.recording()); QVERIFY(finished.first().at(0).toBool());
        QFile wav(finished.first().at(1).toString()); QVERIFY(wav.open(QIODevice::ReadOnly));
        QCOMPARE(wav.readAll().mid(44),QByteArray::fromHex("0102030405060708"));
    }
    void explicitRecoveryPreservesConflictsAndRejectsWhileRecording()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); decolog::app::ContestAudioController controller;
        Feed feed; QVERIFY(controller.startFromStream(&feed,dir.path(),32,QDateTime::currentDateTimeUtc()));
        feed.push(QByteArray::fromHex("010203")); controller.stop();
        const QString partial=controller.lastPath(); QVERIFY(partial.endsWith(".wav.part"));
        const QString target=partial.left(partial.size()-5); QFile existing(target);
        QVERIFY(existing.open(QIODevice::WriteOnly)); QCOMPARE(existing.write("original"),qint64(8)); existing.close();
        QFile originalPartial(partial); QVERIFY(originalPartial.open(QIODevice::ReadOnly)); const QByteArray originalBytes=originalPartial.readAll(); originalPartial.close();
        QVERIFY(!controller.recoverFile(QUrl::fromLocalFile(partial))); QVERIFY(QFile::exists(partial));
        QVERIFY(originalPartial.open(QIODevice::ReadOnly)); QCOMPARE(originalPartial.readAll(),originalBytes); originalPartial.close();
        QVERIFY(existing.open(QIODevice::ReadOnly)); QCOMPARE(existing.readAll(),QByteArray("original")); existing.close();
        QVERIFY(existing.remove());
        Feed active; QVERIFY(controller.startFromStream(&active,dir.path(),32,QDateTime::currentDateTimeUtc()));
        QVERIFY(!controller.recoverFile(QUrl::fromLocalFile(partial))); QVERIFY(QFile::exists(partial)); controller.stop();
        QVERIFY(controller.recoverFile(QUrl::fromLocalFile(partial))); QVERIFY(QFile::exists(target));
        QFile wav(target); QVERIFY(wav.open(QIODevice::ReadOnly)); QCOMPARE(wav.readAll().mid(44),QByteArray::fromHex("0102"));
    }
    void incompleteSampleIsPreservedAndAnotherRecordingCanStart()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); decolog::app::ContestAudioController recorder;
        QSignalSpy finished(&recorder,&decolog::app::ContestAudioController::finished);
        Feed feed; QVERIFY(recorder.startFromStream(&feed,dir.path(),32,QDateTime::currentDateTimeUtc()));
        feed.push(QByteArray::fromHex("010203")); recorder.stop(); QCOMPARE(finished.size(),1);
        QVERIFY(!finished.first().at(0).toBool()); QString partial=finished.first().at(1).toString(); QVERIFY(QFile::exists(partial));
        QString recovered,error; QVERIFY2(decolog::core::ContestAudioFile::recover(partial,&recovered,&error),qPrintable(error));
        QFile wav(recovered); QVERIFY(wav.open(QIODevice::ReadOnly)); QCOMPARE(wav.readAll().mid(44),QByteArray::fromHex("0102"));
        QVERIFY(recorder.startFromStream(&feed,dir.path(),32,QDateTime::currentDateTimeUtc())); recorder.stop();
        QCOMPARE(finished.size(),2); QVERIFY(finished.last().at(0).toBool());
    }
    void explicitStopAndDestroyedStream()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); decolog::app::ContestAudioController recorder;
        QSignalSpy finished(&recorder,&decolog::app::ContestAudioController::finished);
        Feed *feed=new Feed; QVERIFY(recorder.startFromStream(feed,dir.path(),32,QDateTime::currentDateTimeUtc()));
        feed->push(QByteArray::fromHex("0102")); recorder.stop(); QCOMPARE(finished.size(),1);
        QVERIFY(finished.takeFirst().at(0).toBool()); delete feed;
        feed=new Feed; QVERIFY(recorder.startFromStream(feed,dir.path(),32,QDateTime::currentDateTimeUtc()));
        feed->push(QByteArray::fromHex("0304")); delete feed;
        QCOMPARE(finished.size(),1); QVERIFY(!finished.first().at(0).toBool()); QVERIFY(!recorder.recording());
    }
};
int main(int argc, char **argv)
{
    QCoreApplication app(argc,argv); QTemporaryDir settingsDir;
    if (!settingsDir.isValid()) return 1;
    QCoreApplication::setOrganizationName("DecoAudioFixture"); QCoreApplication::setApplicationName("Tests");
    QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settingsDir.path());
    TestContestAudioController test; return QTest::qExec(&test,argc,argv);
}
#include "tst_contestaudiocontroller.moc"
