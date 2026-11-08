#include "WarningLog.h"

#include "core/Diagnostics.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>

namespace decolog::warninglog {

namespace {

constexpr qint64 kMaxBytes = 1024 * 1024; // oltre 1 MB si ruota: il vecchio resta in avvisi.log.1

QtMessageHandler g_previous = nullptr;
QString g_path;
QMutex g_mutex;

const char* levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:    return "DEBUG";
    case QtInfoMsg:     return "INFO";
    case QtWarningMsg:  return "AVVISO";
    case QtCriticalMsg: return "ERRORE";
    case QtFatalMsg:    return "FATALE";
    }
    return "?";
}

void rotateIfNeeded()
{
    if (QFileInfo(g_path).size() <= kMaxBytes)
        return;
    QFile::remove(g_path + QStringLiteral(".1"));
    QFile::rename(g_path, g_path + QStringLiteral(".1"));
}

void handle(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    // Prima la console, come prima: il registro si aggiunge, non sostituisce.
    if (g_previous)
        g_previous(type, context, message);

    // Il debug e le informazioni sono troppi per un file da tenere in mano.
    if (type == QtDebugMsg || type == QtInfoMsg || g_path.isEmpty())
        return;

    const QString category = QString::fromUtf8(context.category ? context.category : "");
    const QString where = context.file
        ? QStringLiteral("%1:%2").arg(QString::fromUtf8(context.file)).arg(context.line)
        : QString();
    const QString line = QStringLiteral("%1 %2 [%3] %4 %5\n")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
             QString::fromLatin1(levelName(type)),
             category.isEmpty() ? QStringLiteral("default") : category,
             decolog::core::diagnostics::redactSecrets(message),
             where);

    QMutexLocker lock(&g_mutex);
    rotateIfNeeded();
    QFile file(g_path);
    if (!file.open(QIODevice::Append | QIODevice::Text))
        return;
    file.write(line.toUtf8());
}

} // namespace

void install(const QString& folder)
{
    QDir().mkpath(folder);
    g_path = QDir(folder).filePath(QStringLiteral("avvisi.log"));
    g_previous = qInstallMessageHandler(handle);
}

QString logPath()
{
    return g_path;
}

} // namespace decolog::warninglog
