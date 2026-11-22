#include "core/TesseractOcr.h"
#include <QFileInfo>
#include <QImageReader>
#include <QStandardPaths>
#include <QDate>
#include <QRegularExpression>
#include <QSet>
#include <QFile>
#include <QSaveFile>
#include <QCryptographicHash>
#include <QPointer>
#include <QUuid>

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
    if (m_copyCancel) m_copyCancel->store(true);
    m_pool.waitForDone();
    if (m_process.state() != QProcess::NotRunning) { m_process.kill(); m_process.waitForFinished(1000); }
}
bool TesseractOcr::start(const QUrl &image)
{
    if (m_busy) return false;
    if (!m_snapshotPath.isEmpty()) QFile::remove(m_snapshotPath);
    m_snapshotPath.clear(); m_snapshotSha256.clear(); m_sourcePath.clear();
    m_text.clear(); m_words.clear(); m_suggestions.clear(); m_output.clear(); m_error.clear(); m_cancelled = false; m_tooLarge = false;
    const QFileInfo info(image.toLocalFile());
    if (!image.isLocalFile() || !info.isFile() || !info.isReadable() || info.size() <= 0 || info.size() > kMaxImageBytes) {
        m_error = tr("Choose a readable local image up to 32 MB"); emit changed(); return false;
    }
    if (m_executable.isEmpty() || !QFileInfo(m_executable).isExecutable()) {
        m_error = tr("Tesseract OCR was not found"); emit changed(); return false;
    }
    if (!m_tempDir.isValid()) {
        m_error = tr("Cannot create a temporary image copy"); emit changed(); return false;
    }
    m_busy = true;
    m_copyCancel = std::make_shared<std::atomic_bool>(false);
    const auto cancelled = m_copyCancel;
    const QString original = info.absoluteFilePath();
    m_sourcePath = original;
    const QString snapshot = m_tempDir.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));
    emit changed();
    QPointer<TesseractOcr> self(this);
    m_pool.start([self, original, snapshot, cancelled] {
        QString error;
        QString digest;
        QFile input(original);
        QSaveFile output(snapshot);
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) {
            error = tr("Cannot create a temporary image copy");
        } else {
            qint64 total = 0;
            while (!cancelled->load()) {
                const QByteArray block = input.read(256 * 1024);
                if (block.isEmpty()) {
                    if (input.error() != QFile::NoError) error = tr("Cannot read the image");
                    break;
                }
                total += block.size();
                if (total > kMaxImageBytes || output.write(block) != block.size()) {
                    error = tr("Cannot create a temporary image copy"); break;
                }
                hash.addData(block);
            }
            if (!cancelled->load() && error.isEmpty() && total > 0 && output.commit()) {
                QImageReader reader(snapshot);
                const QSize size = reader.size();
                if (!size.isValid() || size.width() > 12000 || size.height() > 12000 ||
                    qint64(size.width()) * size.height() > 40000000)
                    error = tr("Unsupported image or image dimensions too large");
                else
                    digest = QString::fromLatin1(hash.result().toHex());
            } else if (!cancelled->load() && error.isEmpty()) {
                error = tr("Cannot create a temporary image copy");
            }
        }
        if (!error.isEmpty() || cancelled->load()) QFile::remove(snapshot);
        QMetaObject::invokeMethod(self.data(), [self, snapshot, digest, error, cancelled] {
            if (!self) return;
            self->m_copyCancel.reset();
            if (cancelled->load()) { QFile::remove(snapshot); self->finish(false, tr("OCR cancelled")); return; }
            if (!error.isEmpty()) { self->finish(false, error); return; }
            self->m_snapshotPath = snapshot;
            self->m_snapshotSha256 = digest;
            self->m_process.setProgram(self->m_executable);
            self->m_process.setArguments({snapshot, QStringLiteral("stdout"), QStringLiteral("-l"), QStringLiteral("eng"),
                                          QStringLiteral("--psm"), QStringLiteral("3"), QStringLiteral("tsv")});
            self->m_timeout.start(90000);
            self->m_process.start();
        }, Qt::QueuedConnection);
    });
    return true;
}
void TesseractOcr::cancel()
{
    if (!m_busy) return;
    m_cancelled = true;
    if (m_copyCancel) m_copyCancel->store(true);
    if (m_process.state() != QProcess::NotRunning) m_process.kill();
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
    m_suggestions = suggestionsFromWords(m_words);
}
QVariantMap TesseractOcr::suggestionsFromWords(const QVariantList &words)
{
    QVariantList calls, dates, times, bands, modes;
    QSet<QString> seen;
    bool truncated = false;
    const QSet<QString> validBands{QStringLiteral("160M"), QStringLiteral("80M"), QStringLiteral("60M"),
        QStringLiteral("40M"), QStringLiteral("30M"), QStringLiteral("20M"), QStringLiteral("17M"),
        QStringLiteral("15M"), QStringLiteral("12M"), QStringLiteral("10M"), QStringLiteral("6M"),
        QStringLiteral("4M"), QStringLiteral("2M"), QStringLiteral("70CM"), QStringLiteral("23CM")};
    const QSet<QString> validModes{QStringLiteral("CW"), QStringLiteral("SSB"), QStringLiteral("USB"),
        QStringLiteral("LSB"), QStringLiteral("AM"), QStringLiteral("FM"), QStringLiteral("FT8"),
        QStringLiteral("FT4"), QStringLiteral("RTTY")};
    static const QRegularExpression callRe(QStringLiteral("^(?=.{3,15}$)(?=.*[A-Z])(?=.*[0-9])[A-Z0-9]+(?:/[A-Z0-9]+)*$"));
    static const QRegularExpression isoRe(QStringLiteral("^(\\d{4})[-/.](\\d{1,2})[-/.](\\d{1,2})$"));
    static const QRegularExpression localRe(QStringLiteral("^(\\d{1,2})[-/.](\\d{1,2})[-/.](\\d{4})$"));
    static const QRegularExpression timeRe(QStringLiteral("^([01]\\d|2[0-3]):([0-5]\\d)$"));
    static const QRegularExpression compactTimeRe(QStringLiteral("^([01]\\d|2[0-3])([0-5]\\d)$"));
    const auto append = [&](QVariantList &list, const QString &kind, const QString &value,
                            const QString &raw, bool uncertain, bool ambiguous = false) {
        const QString key = kind + QLatin1Char('|') + value + QLatin1Char('|') + raw;
        if (seen.contains(key)) return;
        seen.insert(key);
        if (list.size() >= 100) { truncated = true; return; }
        list << QVariantMap{{QStringLiteral("value"), value}, {QStringLiteral("raw"), raw},
                            {QStringLiteral("uncertain"), uncertain}, {QStringLiteral("ambiguous"), ambiguous}};
    };
    for (qsizetype i = 0; i < words.size(); ++i) {
        const auto item = words.at(i).toMap();
        const QString raw = item.value(QStringLiteral("text")).toString();
        const QString token = raw.trimmed().toUpper();
        const bool uncertain = item.value(QStringLiteral("uncertain"), true).toBool();
        if (validBands.contains(token)) { append(bands, "band", token.toLower(), raw, uncertain); continue; }
        if (validModes.contains(token)) { append(modes, "mode", token, raw, uncertain); continue; }
        auto date = isoRe.match(token);
        if (date.hasMatch()) {
            const QDate d(date.captured(1).toInt(), date.captured(2).toInt(), date.captured(3).toInt());
            if (d.isValid()) append(dates, "date", d.toString(Qt::ISODate), raw, uncertain);
            continue;
        }
        date = localRe.match(token);
        if (date.hasMatch()) {
            const int a = date.captured(1).toInt(), b = date.captured(2).toInt(), y = date.captured(3).toInt();
            const QDate dayFirst(y, b, a), monthFirst(y, a, b);
            const bool ambiguous = dayFirst.isValid() && monthFirst.isValid() && dayFirst != monthFirst;
            if (dayFirst.isValid()) append(dates, "date", dayFirst.toString(Qt::ISODate), raw, uncertain, ambiguous);
            if (monthFirst.isValid() && monthFirst != dayFirst) append(dates, "date", monthFirst.toString(Qt::ISODate), raw, uncertain, ambiguous);
            continue;
        }
        auto time = timeRe.match(token);
        if (!time.hasMatch()) {
            const QString previous = i > 0 ? words.at(i - 1).toMap().value("text").toString().toUpper() : QString();
            const QString next = i + 1 < words.size() ? words.at(i + 1).toMap().value("text").toString().toUpper() : QString();
            if (previous == QLatin1String("TIME") || previous == QLatin1String("UTC") || next == QLatin1String("UTC"))
                time = compactTimeRe.match(token);
        }
        if (time.hasMatch()) { append(times, "time", time.captured(1) + ':' + time.captured(2), raw, uncertain); continue; }
        if (callRe.match(token).hasMatch()) append(calls, "call", token, raw, uncertain);
    }
    return {{QStringLiteral("calls"), calls}, {QStringLiteral("dates"), dates}, {QStringLiteral("times"), times},
            {QStringLiteral("bands"), bands}, {QStringLiteral("modes"), modes}, {QStringLiteral("truncated"), truncated}};
}
void TesseractOcr::finish(bool success, const QString &message)
{
    m_timeout.stop();
    m_busy = false;
    m_error = message;
    if (!success) {
        m_text.clear(); m_words.clear(); m_suggestions.clear(); m_snapshotSha256.clear();
        if (!m_snapshotPath.isEmpty()) QFile::remove(m_snapshotPath);
        m_snapshotPath.clear();
        m_sourcePath.clear();
    }
    emit changed();
    emit finished(success);
}
} // namespace decolog::core
