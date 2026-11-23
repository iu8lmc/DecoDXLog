#include "core/ContestAudioFile.h"
#include <QFile>
#include <QTemporaryDir>
#include <QCoreApplication>
#include <QProcess>
#include <QStandardPaths>
#include <cstdio>
#include <cstdlib>
#include <QTest>
#include <QtEndian>
using namespace decolog::core;
class TestContestAudio : public QObject {
    Q_OBJECT
  private slots:
    void writeAndRecoverInterruptedRecording()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); QString partial;
        {
            ContestAudioFile file;
            QVERIFY(file.start(dir.path(), 8, QDateTime::fromString("2026-11-23T12:00:00Z", Qt::ISODate)));
            partial=file.partialPath(); QVERIFY(QFile::exists(partial));
            auto a=file.append(QByteArray::fromHex("010203")); QCOMPARE(a.written,qint64(2)); QVERIFY(!a.limitReached);
            auto b=file.append(QByteArray::fromHex("0405060708090a")); QCOMPARE(b.written,qint64(6)); QVERIFY(b.limitReached);
            QCOMPARE(file.bytesWritten(),qint64(8));
        }
        QString output,error;
        QVERIFY2(ContestAudioFile::recover(partial,&output,&error),qPrintable(error));
        QVERIFY(!QFile::exists(partial)); QFile wav(output); QVERIFY(wav.open(QIODevice::ReadOnly));
        const QByteArray bytes=wav.readAll(); QCOMPARE(bytes.size(),52); QCOMPARE(bytes.left(4),QByteArray("RIFF"));
        QCOMPARE(qFromLittleEndian<quint32>(bytes.constData()+40),quint32(8));
        QCOMPARE(bytes.mid(44),QByteArray::fromHex("0102030405060708"));
        QVERIFY(output.contains("20261123T120000000Z"));
        QVERIFY(!ContestAudioFile::recover(output,&output,&error));
    }
    void recoversAfterAbruptProcessExit()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QProcess child; child.start(QCoreApplication::applicationFilePath(), {"--abrupt-writer",dir.path()});
        QVERIFY(child.waitForStarted()); QVERIFY(child.waitForFinished()); QCOMPARE(child.exitCode(),0);
        const QString partial=QString::fromUtf8(child.readAllStandardOutput()).trimmed();
        QVERIFY(QFile::exists(partial)); QString recovered,error;
        QVERIFY2(ContestAudioFile::recover(partial,&recovered,&error),qPrintable(error));
        QFile file(recovered); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll().mid(44),QByteArray::fromHex("01020304"));
    }
    void preservesNativeSampleRateAcrossRecoveryAndLookup()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); ContestAudioFile file;
        const auto start=QDateTime::fromString("2026-11-23T12:00:00Z",Qt::ISODate);
        QVERIFY(file.start(dir.path(),96000,start,48000)); QCOMPARE(file.append(QByteArray(96000,'x')).written,qint64(96000));
        const QString partial=file.partialPath(); file.abandon(); QString path,error;
        QVERIFY2(ContestAudioFile::recover(partial,&path,&error),qPrintable(error));
        QFile wav(path); QVERIFY(wav.open(QIODevice::ReadOnly)); const QByteArray h=wav.read(44);
        QCOMPARE(qFromLittleEndian<quint32>(h.constData()+24),quint32(48000));
        const auto location=ContestAudioFile::locate(path,start.addMSecs(500)); QVERIFY(location.error.isEmpty());
        QCOMPARE(location.sampleRate,48000); QCOMPARE(location.offsetMs,qint64(500)); QCOMPARE(location.durationMs,qint64(1000));
    }
    void locatesQsoOnlyWithinValidatedRecording()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); ContestAudioFile file;
        const QDateTime start=QDateTime::fromString("2026-11-23T12:00:00.000Z",Qt::ISODateWithMs);
        QVERIFY(file.start(dir.path(),64000,start)); QCOMPARE(file.append(QByteArray(32000,'x')).written,qint64(32000));
        QString path; QVERIFY(file.finish(&path));
        auto middle=ContestAudioFile::locate(path,start.addMSecs(500)); QVERIFY2(middle.error.isEmpty(),qPrintable(middle.error));
        QCOMPARE(middle.offsetMs,qint64(500)); QCOMPARE(middle.durationMs,qint64(1000));
        QVERIFY(!ContestAudioFile::locate(path,start.addMSecs(-1)).error.isEmpty());
        QVERIFY(!ContestAudioFile::locate(path,start.addMSecs(1000)).error.isEmpty());
        QFile altered(path); QVERIFY(altered.open(QIODevice::ReadWrite)); QVERIFY(altered.seek(40)); altered.write("bad!"); altered.close();
        QVERIFY(!ContestAudioFile::locate(path,start).error.isEmpty());
    }
    void folderBudgetIncludesWavAndPartialsWithoutDeletingFiles()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); ContestAudioFile first,second; const auto now=QDateTime::currentDateTimeUtc();
        QVERIFY(first.start(dir.path(),100,now)); QCOMPARE(first.append(QByteArray(4,'x')).written,qint64(4)); QString wav; QVERIFY(first.finish(&wav));
        QVERIFY(second.start(dir.path(),100,now)); QCOMPARE(second.append(QByteArray(2,'y')).written,qint64(2)); const QString partial=second.partialPath(); second.abandon();
        QFile other(dir.filePath("other.wav")); QVERIFY(other.open(QIODevice::WriteOnly)); QCOMPARE(other.write(QByteArray(500,'z')),qint64(500)); other.close();
        auto budget=ContestAudioFile::folderBudget(dir.path(),150,1000); QVERIFY2(budget.error.isEmpty(),qPrintable(budget.error));
        QCOMPARE(budget.usedBytes,qint64(94)); QCOMPARE(budget.remainingDataBytes,qint64(12)); QCOMPARE(budget.files,2);
        budget=ContestAudioFile::folderBudget(dir.path(),100,1000); QCOMPARE(budget.remainingDataBytes,qint64(0));
        QVERIFY(!ContestAudioFile::folderBudget(dir.path(),150,1).error.isEmpty());
        QVERIFY(QFile::exists(wav)); QVERIFY(QFile::exists(partial)); QVERIFY(QFile::exists(other.fileName()));
    }
    void findsBoundedRecordingsCoveringQsoTime()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const auto qso=QDateTime::fromString("2026-11-23T12:00:01Z",Qt::ISODate);
        auto make=[&](const QDateTime &start) {
            ContestAudioFile writer; if (!writer.start(dir.path(),64000,start)) return QString();
            if (writer.append(QByteArray(64000,'x')).written!=64000) return QString();
            QString path; return writer.finish(&path) ? path : QString();
        };
        const auto first=make(qso.addSecs(-1)), second=make(qso.addMSecs(-500)), outside=make(qso.addSecs(-9));
        QVERIFY(!first.isEmpty() && !second.isEmpty() && !outside.isEmpty());
        QFile noise(dir.filePath("other.wav")); QVERIFY(noise.open(QIODevice::WriteOnly)); noise.write("unrelated"); noise.close();
        const auto found=ContestAudioFile::findMatching(dir.path(),qso,1000,50);
        QVERIFY2(found.error.isEmpty(),qPrintable(found.error)); QCOMPARE(found.matches.size(),2); QVERIFY(!found.truncated);
        QCOMPARE(found.matches[0].path,first); QCOMPARE(found.matches[1].path,second);
        const auto limited=ContestAudioFile::findMatching(dir.path(),qso,1,50); QVERIFY(limited.truncated); QCOMPARE(limited.inspected,1);
        const auto few=ContestAudioFile::findMatching(dir.path(),qso,1000,1); QVERIFY(few.truncated); QCOMPARE(few.matches.size(),1);
        QVERIFY(!ContestAudioFile::findMatching(dir.filePath("missing"),qso,1000,50).error.isEmpty());
    }
    void decodesPcmThroughExternalPlayer()
    {
        const QString ffmpeg=QStandardPaths::findExecutable("ffmpeg");
        if (ffmpeg.isEmpty()) QSKIP("ffmpeg unavailable");
        QTemporaryDir dir; QVERIFY(dir.isValid()); ContestAudioFile file;
        QVERIFY(file.start(dir.path(),32000,QDateTime::currentDateTimeUtc()));
        QByteArray pcm; for (int i=0;i<1600;++i) { const qint16 sample=qint16((i%200-100)*200); char b[2]; qToLittleEndian(sample,b); pcm.append(b,2); }
        QCOMPARE(file.append(pcm).written,qint64(pcm.size())); QString output; QVERIFY(file.finish(&output));
        QProcess decoder; decoder.start(ffmpeg,{"-v","error","-i",output,"-f","s16le","-acodec","pcm_s16le","-"});
        QVERIFY(decoder.waitForStarted()); QVERIFY(decoder.waitForFinished(10000)); QCOMPARE(decoder.exitCode(),0);
        QCOMPARE(decoder.readAllStandardOutput(),pcm);
    }
    void completeRecordingAndRejectInvalidRecovery()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); ContestAudioFile file;
        QVERIFY(!file.start(dir.path(), 1, QDateTime::currentDateTimeUtc()));
        QVERIFY(file.start(dir.path(), 100, QDateTime::currentDateTimeUtc()));
        auto result=file.append(QByteArray::fromHex("10203040")); QCOMPARE(result.written,qint64(4));
        QString output; QVERIFY(file.finish(&output)); QVERIFY(QFile::exists(output));
        QFile bad(dir.filePath("other.wav.part")); QVERIFY(bad.open(QIODevice::WriteOnly)); bad.write("bad"); bad.close();
        QString error; QVERIFY(!ContestAudioFile::recover(bad.fileName(),&output,&error)); QVERIFY(QFile::exists(bad.fileName()));
    }
};
int main(int argc, char **argv)
{
    QCoreApplication app(argc,argv);
    if (app.arguments().size()==3 && app.arguments()[1]==QLatin1String("--abrupt-writer")) {
        ContestAudioFile file;
        if (!file.start(app.arguments()[2],1024,QDateTime::currentDateTimeUtc()) ||
            !file.append(QByteArray::fromHex("01020304")).error.isEmpty() || !file.checkpoint()) return 1;
        const QByteArray path=file.partialPath().toUtf8();
        std::fwrite(path.constData(),1,size_t(path.size()),stdout); std::fflush(stdout);
        std::_Exit(0);
    }
    TestContestAudio test;
    return QTest::qExec(&test,argc,argv);
}
#include "tst_contestaudio.moc"
