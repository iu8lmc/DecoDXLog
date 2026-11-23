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
