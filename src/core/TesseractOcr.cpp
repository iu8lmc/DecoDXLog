#include "core/TesseractOcr.h"
#include <QFileInfo>
#include <QImageReader>
#include <QStandardPaths>

namespace decolog::core {
namespace {
constexpr qint64 kMaxImageBytes = 32 * 1024 * 1024;
constexpr qsizetype kMaxOutputBytes = 4 * 1024 * 1024;
QString locateTesseract()
{
    QString path = QStandardPaths::findExecutable(QStringLiteral("tesseract"));
#ifdef Q_OS_MACOS
    if (path.isEmpty()) {
        for (const QString &candidate : {QStringLiteral("/opt/homebrew/bin/tesseract"), QStringLiteral("/usr/local/bin/tesseract")}) {
            if (QFileInfo(candidate).isExecutable()) { path = candidate; break; }
        }
    }
#endif
    return path;
}
}
TesseractOcr::TesseractOcr(const QString &executableOverride, QObject *parent)
    : QObject(parent), m_executable(executableOverride.isEmpty() ? locateTesseract() : executableOverride)
{
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (!m_busy) return;
        m_error = tr("OCR timed out");
        m_process.kill();
    });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &TesseractOcr::collect);
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] { m_process.readAllStandardError(); });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && m_busy)
            finish(false, tr("Cannot start the OCR engine"));
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus status) {
        if (!m_busy) return;
        collect();
        if (m_cancelled) { finish(false, tr("OCR cancelled")); return; }
        if (m_tooLarge) { finish(false, tr("OCR output is too large")); return; }
        if (!m_error.isEmpty()) { finish(false, m_error); return; }
        if (status != QProcess::NormalExit || code != 0) { finish(false, tr("OCR engine failed")); return; }
        parse();
        finish(true);
    });
}
TesseractOcr::~TesseractOcr()
{
    if (m_process.state() != QProcess::NotRunning) { m_process.kill(); m_process.waitForFinished(1000); }
}
bool TesseractOcr::start(const QUrl &image)
{
    if (m_busy) return false;
    m_text.clear(); m_words.clear(); m_output.clear(); m_error.clear(); m_cancelled = false; m_tooLarge = false;
    const QFileInfo info(image.toLocalFile());
    if (!image.isLocalFile() || !info.isFile() || !info.isReadable() || info.size() <= 0 || info.size() > kMaxImageBytes) {
        m_error = tr("Choose a readable local image up to 32 MB"); emit changed(); return false;
    }
    QImageReader reader(info.absoluteFilePath());
    const QSize size = reader.size();
    if (!size.isValid() || size.width() > 12000 || size.height() > 12000 || qint64(size.width()) * size.height() > 40000000) {
        m_error = tr("Unsupported image or image dimensions too large"); emit changed(); return false;
    }
    if (m_executable.isEmpty() || !QFileInfo(m_executable).isExecutable()) {
        m_error = tr("Tesseract OCR was not found"); emit changed(); return false;
    }
    m_busy = true;
    emit changed();
    m_process.setProgram(m_executable);
    m_process.setArguments({info.absoluteFilePath(), QStringLiteral("stdout"), QStringLiteral("-l"), QStringLiteral("eng"),
                            QStringLiteral("--psm"), QStringLiteral("3"), QStringLiteral("tsv")});
    m_timeout.start(90000);
    m_process.start();
    return true;
}
void TesseractOcr::cancel()
{
    if (!m_busy) return;
    m_cancelled = true;
    m_process.kill();
}
void TesseractOcr::collect()
{
    const QByteArray chunk = m_process.readAllStandardOutput();
    if (m_output.size() + chunk.size() > kMaxOutputBytes) {
        m_tooLarge = true;
        m_process.kill();
        return;
    }
    m_output += chunk;
}
void TesseractOcr::parse()
{
    QByteArray previous;
    for (const QByteArray &line : m_output.split('\n')) {
        const auto cells = line.split('\t');
        if (cells.size() < 12 || cells[0] != "5") continue;
        const QString word = QString::fromUtf8(cells[11]).trimmed();
        if (word.isEmpty()) continue;
        bool ok = false;
        const double confidence = cells[10].toDouble(&ok);
        const QByteArray key = cells[1] + '/' + cells[2] + '/' + cells[3] + '/' + cells[4];
        if (!m_text.isEmpty()) m_text += key == previous ? QLatin1Char(' ') : QLatin1Char('\n');
        m_text += word;
        previous = key;
        m_words << QVariantMap{{QStringLiteral("text"), word}, {QStringLiteral("confidence"), ok ? confidence : -1.0},
                               {QStringLiteral("uncertain"), !ok || confidence < 75.0}};
    }
}
void TesseractOcr::finish(bool success, const QString &message)
{
    m_timeout.stop();
    m_busy = false;
    m_error = message;
    if (!success) { m_text.clear(); m_words.clear(); }
    emit changed();
    emit finished(success);
}
} // namespace decolog::core
