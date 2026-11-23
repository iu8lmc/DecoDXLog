#include "core/ContestAudioFile.h"
#include <QDir>
#include <QDirIterator>
#include <algorithm>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUuid>
#include <QTimeZone>
#include <QtEndian>
#include <limits>

namespace decolog::core {
namespace {
constexpr qint64 maximumDataBytes = 1024LL * 1024 * 1024;
QByteArray header(quint32 size, int sampleRate)
{
    QByteArray h;
    auto u16=[&](quint16 v) { char b[2]; qToLittleEndian(v,b); h.append(b,2); };
    auto u32=[&](quint32 v) { char b[4]; qToLittleEndian(v,b); h.append(b,4); };
    h.append("RIFF",4); u32(36+size); h.append("WAVEfmt ",8); u32(16); u16(1); u16(1);
    u32(quint32(sampleRate)); u32(quint32(sampleRate)*2); u16(2); u16(16); h.append("data",4); u32(size);
    return h;
}
int validatedRate(QByteArray actual)
{
    if (actual.size()!=44) return 0;
    const quint32 rate=qFromLittleEndian<quint32>(actual.constData()+24);
    if (rate<8000 || rate>192000) return 0;
    const QByteArray expected=header(0,int(rate));
    actual.replace(4,4,expected.mid(4,4)); actual.replace(40,4,expected.mid(40,4));
    return actual==expected ? int(rate) : 0;
}
bool finalize(QFile &file, const QString &path, QString *output, QString *error)
{
    const qint64 size=file.size()-44;
    if (size < 0 || size > maximumDataBytes || size % 2) {
        if (error) *error=QStringLiteral("Invalid or incomplete PCM data size"); return false;
    }
    if (!file.seek(0)) { if (error) *error=file.errorString(); return false; }
    const int rate=validatedRate(file.read(44));
    if (!rate) { if (error) *error=QStringLiteral("Unexpected WAV format"); return false; }
    const QString destination=path.left(path.size()-5); // remove .part
    if (QFile::exists(destination)) {
        if (error) *error=QStringLiteral("A recording with this name already exists"); return false;
    }
    if (!file.seek(0) || file.write(header(quint32(size),rate)) != 44 || !file.flush()) {
        if (error) *error=file.errorString(); return false;
    }
    file.close();
    if (!QFile::rename(path,destination)) {
        if (error) *error=QStringLiteral("Cannot move recovered recording to WAV"); return false;
    }
    if (output) *output=destination;
    return true;
}
}
bool ContestAudioFile::start(const QString &directory, qint64 maxDataBytes, const QDateTime &startUtc, int sampleRate)
{
    if (sampleRate < 8000 || sampleRate > 192000 || m_file.isOpen() || maxDataBytes < 2 || maxDataBytes > maximumDataBytes || !startUtc.isValid() || !QDir(directory).exists()) {
        m_error=QStringLiteral("Invalid recording directory, time or size limit"); return false;
    }
    const QString name=QStringLiteral("contest-audio-%1-%2.wav.part")
        .arg(startUtc.toUTC().toString(QStringLiteral("yyyyMMdd'T'HHmmsszzz'Z'")),QUuid::createUuid().toString(QUuid::WithoutBraces));
    m_path=QDir(directory).filePath(name); m_file.setFileName(m_path);
    if (!m_file.open(QIODevice::ReadWrite | QIODevice::NewOnly) || m_file.write(header(0,sampleRate)) != 44 || !m_file.flush()) {
        m_error=m_file.errorString(); m_file.close(); return false;
    }
    m_pending.clear(); m_error.clear(); m_bytes=0; m_limit=maxDataBytes-(maxDataBytes%2);
    return true;
}
ContestAudioFile::WriteResult ContestAudioFile::append(const QByteArray &data)
{
    WriteResult result;
    if (!m_file.isOpen() || !m_error.isEmpty()) { result.error=QStringLiteral("Recording is not writable"); return result; }
    const QByteArray buffer=m_pending+data;
    const qint64 remaining=m_limit-m_bytes;
    const qint64 n=qMin(remaining, qint64(buffer.size()-(buffer.size()%2)));
    if (n > 0) {
        const qint64 written=m_file.write(buffer.constData(),n);
        if (written != n) {
            m_error=m_file.errorString(); result.error=m_error;
            if (written > 0) { m_bytes+=written; result.written=written; }
            return result;
        }
        m_bytes+=written; result.written=written;
    }
    result.limitReached=m_bytes >= m_limit;
    m_pending=result.limitReached ? QByteArray() : buffer.mid(n);
    return result;
}
bool ContestAudioFile::checkpoint()
{
    if (!m_file.isOpen() || !m_error.isEmpty()) return false;
    if (!m_file.flush()) { m_error=m_file.errorString(); return false; }
    return true;
}
bool ContestAudioFile::finish(QString *outputPath)
{
    if (!m_file.isOpen() || !m_error.isEmpty() || !m_pending.isEmpty()) {
        m_error=QStringLiteral("Recording has an incomplete sample or write error"); return false;
    }
    return finalize(m_file,m_path,outputPath,&m_error);
}
void ContestAudioFile::abandon()
{
    if (m_file.isOpen()) { m_file.flush(); m_file.close(); }
}
bool ContestAudioFile::recover(const QString &path, QString *outputPath, QString *error)
{
    static const QRegularExpression expected(QStringLiteral("^contest-audio-[0-9]{8}T[0-9]{9}Z-[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\\.wav\\.part$"));
    const QFileInfo info(path);
    if (info.isSymLink() || !info.isFile() || !expected.match(info.fileName()).hasMatch()) {
        if (error) *error=QStringLiteral("Not a contest audio partial file"); return false;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadWrite)) { if (error) *error=f.errorString(); return false; }
    if (!validatedRate(f.read(44))) { if (error) *error=QStringLiteral("Unexpected or incomplete WAV format"); return false; }
    return finalize(f,path,outputPath,error);
}
ContestAudioFile::Location ContestAudioFile::locate(const QString &path, const QDateTime &qsoUtc)
{
    Location result;
    static const QRegularExpression name(QStringLiteral("^contest-audio-([0-9]{8}T[0-9]{9}Z)-[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\\.wav$"));
    const QFileInfo info(path); const auto match=name.match(info.fileName());
    if (info.isSymLink() || !info.isFile() || !match.hasMatch() || !qsoUtc.isValid()) {
        result.error=QStringLiteral("Invalid recording or QSO timestamp"); return result;
    }
    const auto parsed=QDateTime::fromString(match.captured(1),QStringLiteral("yyyyMMdd'T'HHmmsszzz'Z'"));
    if (!parsed.isValid()) { result.error=QStringLiteral("Invalid recording timestamp"); return result; }
    result.startUtc=QDateTime(parsed.date(),parsed.time(),QTimeZone::UTC);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { result.error=file.errorString(); return result; }
    const qint64 size=file.size()-44;
    const QByteArray actual=file.read(44);
    const int rate=validatedRate(actual);
    if (size < 0 || size > maximumDataBytes || size%2 || !rate || actual!=header(quint32(size),rate)) {
        result.error=QStringLiteral("Invalid or incomplete contest WAV"); return result;
    }
    result.sampleRate=rate;
    result.durationMs=size*1000/(qint64(rate)*2);
    result.offsetMs=result.startUtc.msecsTo(qsoUtc.toUTC());
    if (result.offsetMs<0 || result.offsetMs>=result.durationMs)
        result.error=QStringLiteral("QSO time is outside this recording");
    return result;
}

ContestAudioFile::FolderBudget ContestAudioFile::folderBudget(const QString &directory, qint64 limitBytes, int maxFiles)
{
    FolderBudget result; const QFileInfo folder(directory);
    if (!folder.isDir() || !folder.isReadable() || limitBytes<46 || limitBytes>maximumDataBytes || maxFiles<1 || maxFiles>1000) {
        result.error=QStringLiteral("Invalid audio folder, budget or scan limit"); return result;
    }
    QDirIterator it(directory,{QStringLiteral("contest-audio-*.wav"),QStringLiteral("contest-audio-*.wav.part")},
                    QDir::Files | QDir::NoSymLinks | QDir::Hidden | QDir::System);
    while (it.hasNext()) {
        if (result.files==maxFiles) { result.error=QStringLiteral("Too many audio files to check the folder budget"); return result; }
        it.next(); ++result.files; const qint64 size=it.fileInfo().size();
        if (size<0 || result.usedBytes>std::numeric_limits<qint64>::max()-size) {
            result.error=QStringLiteral("Cannot determine audio folder usage"); return result;
        }
        result.usedBytes+=size;
    }
    result.remainingDataBytes=qMax<qint64>(0,limitBytes-result.usedBytes-44);
    result.remainingDataBytes-=result.remainingDataBytes%2;
    return result;
}
ContestAudioFile::SearchResult ContestAudioFile::findMatching(const QString &directory, const QDateTime &qsoUtc, int maxFiles, int maxResults)
{
    SearchResult result;
    const QFileInfo folder(directory);
    if (!qsoUtc.isValid() || !folder.isDir() || !folder.isReadable() || maxFiles<1 || maxFiles>1000 || maxResults<1 || maxResults>50) {
        result.error=QStringLiteral("Invalid directory, QSO time or search limit"); return result;
    }
    QDirIterator it(directory, {QStringLiteral("contest-audio-*.wav")}, QDir::Files | QDir::NoSymLinks | QDir::Readable);
    while (it.hasNext()) {
        if (result.inspected==maxFiles) { result.truncated=true; break; }
        const QString path=it.next(); ++result.inspected;
        const Location location=locate(path,qsoUtc);
        if (!location.error.isEmpty()) continue;
        if (result.matches.size()==maxResults) { result.truncated=true; continue; }
        result.matches.append({path,location});
    }
    std::sort(result.matches.begin(),result.matches.end(),[](const Match &a,const Match &b) {
        return a.location.startUtc==b.location.startUtc ? a.path<b.path : a.location.startUtc<b.location.startUtc;
    });
    return result;
}

}
