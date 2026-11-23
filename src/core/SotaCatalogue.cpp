#include "core/SotaCatalogue.h"
#include <QCryptographicHash>
#include <QCoreApplication>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringDecoder>
#include <QTemporaryFile>
#include <QUuid>

namespace decolog::core {
namespace {
constexpr qint64 kMaxInput = 128 * 1024 * 1024;
constexpr qint64 kMaxLine = 1024 * 1024;
QString uniqueName() { return QStringLiteral("sota-catalogue-") + QUuid::createUuid().toString(); }
// The published snapshot is one CSV record per line. Reject ambiguous quoting
// instead of installing a truncated or partially parsed catalogue.
bool csvLine(const QString& line, QStringList* values)
{
    values->clear(); QString cell; bool quoted = false, afterQuote = false;
    for (qsizetype i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (quoted) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"')) { cell += c; ++i; }
                else { quoted = false; afterQuote = true; }
            } else cell += c;
        } else if (c == QLatin1Char(',')) { values->append(cell.trimmed()); cell.clear(); afterQuote = false; }
        else if (c == QLatin1Char('"') && cell.isEmpty() && !afterQuote) quoted = true;
        else if (afterQuote && !c.isSpace()) return false;
        else if (!afterQuote) cell += c;
    }
    if (quoted) return false;
    values->append(cell.trimmed()); return true;
}
bool canReplaceCatalogue(const QString& path)
{
    if (!QFileInfo::exists(path)) return true;
    bool ours = false;
    const QString connection = uniqueName();
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=100"));
        db.setDatabaseName(path);
        if (db.open()) {
            QSqlQuery q(db);
            ours = q.exec(QStringLiteral("PRAGMA application_id")) && q.next() && q.value(0).toInt() == 1397707841;
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(connection);
    return ours;
}
QString build(const QString& inputPath, const QString& outputPath, const std::shared_ptr<std::atomic_bool>& cancelled)
{
    const auto tr = [](const char* message) { return QCoreApplication::translate("SotaCatalogue", message); };
    if (!canReplaceCatalogue(outputPath)) return tr("The destination exists and is not a SOTA catalogue.");
    QFile original(inputPath);
    if (!original.open(QIODevice::ReadOnly) || original.size() <= 0 || original.size() > kMaxInput)
        return tr("The CSV is unavailable or exceeds the size limit.");
    QTemporaryFile input;
    if (!input.open()) return tr("Cannot create a CSV snapshot.");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    qint64 copied = 0;
    while (!original.atEnd() && !cancelled->load()) {
        const QByteArray block = original.read(256 * 1024);
        copied += block.size();
        if (block.isEmpty() || copied > kMaxInput || input.write(block) != block.size()) return tr("Cannot copy the CSV snapshot.");
        hash.addData(block);
    }
    if (original.error() != QFile::NoError || cancelled->load() || !input.flush() || !input.seek(0)) return tr("Catalogue import cancelled or cannot read the CSV.");
    const QString digest = QString::fromLatin1(hash.result().toHex());
    QStringDecoder decoder(QStringDecoder::Utf8);
    const auto readLine = [&]() { return QString(decoder.decode(input.readLine(kMaxLine + 1))).trimmed(); };
    const QString title = readLine();
    static const QRegularExpression titlePattern(QStringLiteral("^SOTA Summits List \\(Date=(\\d{2}/\\d{2}/\\d{4})\\)$"));
    const auto match = titlePattern.match(title);
    const QDate version = QDate::fromString(match.captured(1), QStringLiteral("dd/MM/yyyy"));
    QStringList columns;
    if (!match.hasMatch() || !version.isValid() || !csvLine(readLine(), &columns) || decoder.hasError()) return tr("The SOTA CSV header or version is invalid.");
    const QStringList required{QStringLiteral("SummitCode"), QStringLiteral("SummitName"), QStringLiteral("ValidFrom"), QStringLiteral("ValidTo")};
    QList<int> indexes;
    for (const auto& field : required) {
        if (columns.count(field) != 1) return tr("The SOTA CSV is missing required columns.");
        indexes << columns.indexOf(field);
    }
    if (!QDir().mkpath(QFileInfo(outputPath).absolutePath())) return tr("Cannot create the catalogue directory.");
    QTemporaryFile temp(QFileInfo(outputPath).absolutePath() + QStringLiteral("/.sota-stage-XXXXXX"));
    if (!temp.open()) return tr("Cannot create a temporary catalogue.");
    const QString temporaryPath = temp.fileName(); temp.close();
    const QString connection = uniqueName(); QString error; int count = 0;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(temporaryPath);
        if (!db.open() || !db.transaction()) error = tr("Cannot create the temporary catalogue.");
        else {
            QSqlQuery q(db);
            if (!q.exec(QStringLiteral("PRAGMA application_id=1397707841")) ||
                !q.exec(QStringLiteral("CREATE TABLE summit (reference TEXT PRIMARY KEY COLLATE NOCASE,name TEXT NOT NULL,valid_from TEXT NOT NULL,valid_to TEXT NOT NULL)")) ||
                !q.exec(QStringLiteral("CREATE TABLE metadata (key TEXT PRIMARY KEY,value TEXT NOT NULL)"))) error = tr("Cannot initialise the catalogue.");
            QSqlQuery insert(db);
            insert.prepare(QStringLiteral("INSERT INTO summit(reference,name,valid_from,valid_to) VALUES(?,?,?,?)"));
            while (error.isEmpty() && !input.atEnd() && !cancelled->load()) {
                const QString line = readLine();
                if (line.isEmpty() && input.atEnd()) break;
                QStringList cells;
                if (line.size() > kMaxLine || decoder.hasError() || !csvLine(line, &cells) || cells.size() != columns.size()) { error = tr("The CSV contains a malformed record."); break; }
                const QString reference = cells.at(indexes.at(0)).trimmed().toUpper();
                const QDate from = QDate::fromString(cells.at(indexes.at(2)), QStringLiteral("dd/MM/yyyy"));
                const QDate to = QDate::fromString(cells.at(indexes.at(3)), QStringLiteral("dd/MM/yyyy"));
                if (reference.isEmpty() || !from.isValid() || !to.isValid() || from > to) { error = tr("The CSV contains an invalid reference or validity period."); break; }
                insert.bindValue(0, reference); insert.bindValue(1, cells.at(indexes.at(1))); insert.bindValue(2, from.toString(Qt::ISODate)); insert.bindValue(3, to.toString(Qt::ISODate));
                if (!insert.exec()) { error = tr("The CSV contains duplicate references or cannot be stored."); break; }
                ++count;
            }
            if (cancelled->load()) error = tr("Catalogue import cancelled.");
            if (input.error() != QFile::NoError || count == 0) error = tr("The CSV is empty or cannot be read.");
            if (error.isEmpty()) {
                QSqlQuery meta(db); meta.prepare(QStringLiteral("INSERT INTO metadata(key,value) VALUES(?,?)"));
                const QVariantMap values{{QStringLiteral("version"), version.toString(Qt::ISODate)}, {QStringLiteral("sha256"), digest},
                    {QStringLiteral("importedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                    {QStringLiteral("source"), QStringLiteral("local-file-unverified")}, {QStringLiteral("fileName"), QFileInfo(inputPath).fileName()},
                    {QStringLiteral("count"), QString::number(count)}};
                for (auto it = values.begin(); it != values.end(); ++it) {
                    meta.bindValue(0, it.key()); meta.bindValue(1, it.value());
                    if (!meta.exec()) { error = tr("Cannot store catalogue metadata."); break; }
                }
            }
            if (!error.isEmpty() || !db.commit()) { db.rollback(); if (error.isEmpty()) error = tr("Cannot commit the catalogue."); }
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(connection);
    if (!error.isEmpty()) return error;
    if (cancelled->load()) return tr("Catalogue import cancelled.");
    if (!canReplaceCatalogue(outputPath)) return tr("The destination changed and is not a SOTA catalogue.");
    QFile ready(temporaryPath); QSaveFile target(outputPath);
    if (!ready.open(QIODevice::ReadOnly) || !target.open(QIODevice::WriteOnly)) return tr("Cannot save the catalogue.");
    while (!ready.atEnd() && !cancelled->load()) {
        const QByteArray block = ready.read(256 * 1024);
        if (block.isEmpty() || target.write(block) != block.size()) { target.cancelWriting(); return tr("Cannot save the catalogue."); }
    }
    if (cancelled->load() || ready.error() != QFile::NoError) { target.cancelWriting(); return tr("Catalogue import cancelled or cannot read the catalogue."); }
    if (!target.commit()) return tr("Cannot commit the catalogue file.");
    return {};
}
QVariantMap read(const QString& path, const QString& reference, const QDate& date, bool onlyInfo)
{
    QVariantMap out;
    if (!QFileInfo::exists(path) || !canReplaceCatalogue(path)) return {{QStringLiteral("status"), QStringLiteral("unavailable")}};
    const QString connection = uniqueName();
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=100")); db.setDatabaseName(path);
        if (!db.open()) out[QStringLiteral("status")] = QStringLiteral("unavailable");
        else {
            QSqlQuery meta(db);
            if (!meta.exec(QStringLiteral("SELECT key,value FROM metadata"))) out[QStringLiteral("status")] = QStringLiteral("unavailable");
            else {
                while (meta.next()) out[meta.value(0).toString()] = meta.value(1);
                const bool metadataValid = !meta.lastError().isValid() &&
                    QDate::fromString(out.value(QStringLiteral("version")).toString(), Qt::ISODate).isValid() &&
                    out.value(QStringLiteral("count")).toInt() > 0 &&
                    QRegularExpression(QStringLiteral("^[0-9a-f]{64}$")).match(out.value(QStringLiteral("sha256")).toString()).hasMatch() &&
                    out.value(QStringLiteral("source")).toString() == QLatin1String("local-file-unverified");
                if (!metadataValid) { out.clear(); out[QStringLiteral("status")] = QStringLiteral("unavailable"); }
                else if (onlyInfo) out[QStringLiteral("status")] = QStringLiteral("available");
                else if (!date.isValid()) out[QStringLiteral("status")] = QStringLiteral("invalidDate");
                else {
                    QSqlQuery q(db); q.prepare(QStringLiteral("SELECT reference,name,valid_from,valid_to FROM summit WHERE reference=?")); q.addBindValue(reference.trimmed().toUpper());
                    if (!q.exec()) out[QStringLiteral("status")] = QStringLiteral("unavailable");
                    else if (!q.next()) out[QStringLiteral("status")] = QStringLiteral("notFound");
                    else {
                        out[QStringLiteral("reference")] = q.value(0); out[QStringLiteral("name")] = q.value(1);
                        out[QStringLiteral("validFrom")] = q.value(2); out[QStringLiteral("validTo")] = q.value(3);
                        const QDate from = QDate::fromString(q.value(2).toString(), Qt::ISODate), to = QDate::fromString(q.value(3).toString(), Qt::ISODate);
                        out[QStringLiteral("status")] = !from.isValid() || !to.isValid() || from > to ? QStringLiteral("unavailable")
                            : date >= from && date <= to ? QStringLiteral("active") : QStringLiteral("outsideValidity");
                    }
                }
            }
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(connection); return out;
}
} // namespace
SotaCatalogue::SotaCatalogue(const QString& path, QObject* parent) : QObject(parent), m_path(path) { m_pool.setMaxThreadCount(1); }
SotaCatalogue::~SotaCatalogue() { if (m_cancel) m_cancel->store(true); m_pool.waitForDone(); }
QVariantMap SotaCatalogue::info() const { return read(m_path, {}, {}, true); }
QVariantMap SotaCatalogue::lookup(const QString& reference, const QDate& date) const { return read(m_path, reference, date, false); }
QVariantMap SotaCatalogue::lookupIso(const QString& reference, const QString& date) const { return lookup(reference, QDate::fromString(date, Qt::ISODate)); }
bool SotaCatalogue::importFile(const QUrl& source)
{
    if (m_busy || !source.isLocalFile() || source.toLocalFile().isEmpty() || m_path.isEmpty()) return false;
    const QFileInfo from(source.toLocalFile()), to(m_path);
    if (from.absoluteFilePath() == to.absoluteFilePath() || (!from.canonicalFilePath().isEmpty() && from.canonicalFilePath() == to.canonicalFilePath())) return false;
    m_cancel = std::make_shared<std::atomic_bool>(false); const auto cancelled = m_cancel;
    const QString input = source.toLocalFile(), path = m_path; m_busy = true; emit changed(); QPointer<SotaCatalogue> self(this);
    m_pool.start([self, input, path, cancelled] {
        const QString error = build(input, path, cancelled);
        if (!self) return;
        QMetaObject::invokeMethod(self.data(), [self, error] { if (!self) return; self->m_busy = false; self->m_cancel.reset(); emit self->changed(); emit self->finished(error.isEmpty(), error); }, Qt::QueuedConnection);
    });
    return true;
}
void SotaCatalogue::cancel() { if (m_cancel) m_cancel->store(true); }
} // namespace decolog::core
