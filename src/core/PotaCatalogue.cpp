#include "core/PotaCatalogue.h"
#include "core/ReferenceCsv.h"
#include <QCoreApplication>
#include <QCryptographicHash>
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
constexpr int kApplicationId = 1347376193;
QString uniqueName() { return QStringLiteral("pota-catalogue-") + QUuid::createUuid().toString(); }
bool canReplace(const QString& path)
{
    if (!QFileInfo::exists(path)) return true;
    bool ours = false; const QString connection = uniqueName();
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=100")); db.setDatabaseName(path);
        if (db.open()) { QSqlQuery q(db); ours = q.exec(QStringLiteral("PRAGMA application_id")) && q.next() && q.value(0).toInt() == kApplicationId; }
        db.close();
    }
    QSqlDatabase::removeDatabase(connection); return ours;
}
QString build(const QString& source, const QString& destination, const std::shared_ptr<std::atomic_bool>& cancelled)
{
    const auto tr = [](const char* text) { return QCoreApplication::translate("PotaCatalogue", text); };
    if (!canReplace(destination)) return tr("The destination exists and is not a POTA catalogue.");
    QFile original(source);
    if (!original.open(QIODevice::ReadOnly) || original.size() <= 0 || original.size() > kMaxInput) return tr("The CSV is unavailable or exceeds the size limit.");
    QTemporaryFile snapshot; if (!snapshot.open()) return tr("Cannot create a CSV snapshot.");
    QCryptographicHash hash(QCryptographicHash::Sha256); qint64 copied = 0;
    while (!original.atEnd() && !cancelled->load()) {
        const QByteArray block = original.read(256 * 1024); copied += block.size();
        if (block.isEmpty() || copied > kMaxInput || snapshot.write(block) != block.size()) return tr("Cannot copy the CSV snapshot.");
        hash.addData(block);
    }
    if (original.error() != QFile::NoError || cancelled->load() || !snapshot.flush() || !snapshot.seek(0)) return tr("Catalogue import cancelled or cannot read the CSV.");
    const QString digest = QString::fromLatin1(hash.result().toHex());
    QStringDecoder decoder(QStringDecoder::Utf8);
    const auto line = [&]() { return QString(decoder.decode(snapshot.readLine(kMaxLine + 1))).trimmed(); };
    QStringList header;
    if (!referencecsv::parseLine(line(), &header) || decoder.hasError()) return tr("Invalid POTA CSV header.");
    const QStringList required{QStringLiteral("reference"), QStringLiteral("name"), QStringLiteral("active"), QStringLiteral("locationDesc")};
    QList<int> indexes;
    for (const auto& name : required) { if (header.count(name) != 1) return tr("The POTA CSV is missing required columns."); indexes << header.indexOf(name); }
    if (!QDir().mkpath(QFileInfo(destination).absolutePath())) return tr("Cannot create the catalogue directory.");
    QTemporaryFile staging(QFileInfo(destination).absolutePath() + QStringLiteral("/.pota-stage-XXXXXX"));
    if (!staging.open()) return tr("Cannot create a temporary catalogue.");
    const QString stagePath = staging.fileName(); staging.close();
    const QString connection = uniqueName(); QString error; int count = 0;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection); db.setDatabaseName(stagePath);
        if (!db.open() || !db.transaction()) error = tr("Cannot create the temporary catalogue.");
        else {
            QSqlQuery q(db);
            if (!q.exec(QStringLiteral("PRAGMA application_id=1347376193")) ||
                !q.exec(QStringLiteral("CREATE TABLE park(reference TEXT PRIMARY KEY COLLATE NOCASE,name TEXT NOT NULL,active INTEGER NOT NULL,locations TEXT NOT NULL)")) ||
                !q.exec(QStringLiteral("CREATE TABLE metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL)"))) error = tr("Cannot initialise the catalogue.");
            QSqlQuery insert(db); insert.prepare(QStringLiteral("INSERT INTO park(reference,name,active,locations) VALUES(?,?,?,?)"));
            while (error.isEmpty() && !snapshot.atEnd() && !cancelled->load()) {
                const QString raw = line(); if (raw.isEmpty() && snapshot.atEnd()) break;
                QStringList cells;
                if (raw.size() > kMaxLine || decoder.hasError() || !referencecsv::parseLine(raw, &cells) || cells.size() != header.size()) { error = tr("The CSV contains a malformed record."); break; }
                const QString ref = cells.at(indexes.at(0)).trimmed().toUpper(), active = cells.at(indexes.at(2)).trimmed();
                if (ref.isEmpty() || (active != QLatin1String("0") && active != QLatin1String("1"))) { error = tr("The CSV contains an invalid reference or status."); break; }
                insert.bindValue(0, ref); insert.bindValue(1, cells.at(indexes.at(1))); insert.bindValue(2, active.toInt());
                const QString locations = cells.at(indexes.at(3)).toUpper();
                insert.bindValue(3, locations.isNull() ? QStringLiteral("") : locations);
                if (!insert.exec()) { error = tr("The CSV contains duplicate references or cannot be stored."); break; }
                ++count;
            }
            if (cancelled->load()) error = tr("Catalogue import cancelled.");
            if (snapshot.error() != QFile::NoError || count == 0) error = tr("The CSV is empty or cannot be read.");
            if (error.isEmpty()) {
                QSqlQuery meta(db); meta.prepare(QStringLiteral("INSERT INTO metadata(key,value) VALUES(?,?)"));
                const QVariantMap values{{QStringLiteral("sha256"), digest}, {QStringLiteral("importedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                    {QStringLiteral("source"), QStringLiteral("local-file-unverified")}, {QStringLiteral("fileName"), QFileInfo(source).fileName()}, {QStringLiteral("count"), QString::number(count)}};
                for (auto it = values.begin(); it != values.end(); ++it) { meta.bindValue(0, it.key()); meta.bindValue(1, it.value()); if (!meta.exec()) { error = tr("Cannot store catalogue metadata."); break; } }
            }
            if (!error.isEmpty() || !db.commit()) { db.rollback(); if (error.isEmpty()) error = tr("Cannot commit the catalogue."); }
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(connection);
    if (!error.isEmpty()) return error;
    if (cancelled->load()) return tr("Catalogue import cancelled.");
    if (!canReplace(destination)) return tr("The destination changed and is not a POTA catalogue.");
    QFile ready(stagePath); QSaveFile target(destination);
    if (!ready.open(QIODevice::ReadOnly) || !target.open(QIODevice::WriteOnly)) return tr("Cannot save the catalogue.");
    while (!ready.atEnd() && !cancelled->load()) {
        const QByteArray block = ready.read(256 * 1024);
        if (block.isEmpty() || target.write(block) != block.size()) { target.cancelWriting(); return tr("Cannot save the catalogue."); }
    }
    if (cancelled->load() || ready.error() != QFile::NoError) { target.cancelWriting(); return tr("Catalogue import cancelled or cannot read the catalogue."); }
    if (!target.commit()) return tr("Cannot commit the catalogue file.");
    return {};
}
bool validMetadata(const QVariantMap& data)
{
    return data.value(QStringLiteral("count")).toInt() > 0 &&
        QDateTime::fromString(data.value(QStringLiteral("importedAt")).toString(), Qt::ISODate).isValid() &&
        QRegularExpression(QStringLiteral("^[0-9a-f]{64}$")).match(data.value(QStringLiteral("sha256")).toString()).hasMatch() &&
        data.value(QStringLiteral("source")).toString() == QLatin1String("local-file-unverified");
}
} // namespace
PotaCatalogue::PotaCatalogue(const QString& path, QObject* parent) : QObject(parent), m_path(path) { m_pool.setMaxThreadCount(1); }
PotaCatalogue::~PotaCatalogue() { if (m_cancel) m_cancel->store(true); m_pool.waitForDone(); }
QVariantMap PotaCatalogue::lookupBatch(const QString& path, const QVariantList& references, const std::shared_ptr<std::atomic_bool>& cancelled)
{
    QVariantMap result{{QStringLiteral("status"), QStringLiteral("unavailable")}};
    if (!QFileInfo::exists(path)) return result;
    const QString connection = uniqueName();
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=100")); db.setDatabaseName(path);
        if (db.open() && db.transaction()) {
            QSqlQuery app(db), meta(db);
            bool ok = app.exec(QStringLiteral("PRAGMA application_id")) && app.next() && app.value(0).toInt() == kApplicationId && meta.exec(QStringLiteral("SELECT key,value FROM metadata"));
            QVariantMap metadata;
            if (ok) { while (meta.next()) metadata[meta.value(0).toString()] = meta.value(1); ok = !meta.lastError().isValid() && validMetadata(metadata); }
            QVariantList rows; QSqlQuery q(db);
            if (ok) ok = q.prepare(QStringLiteral("SELECT reference,name,active,locations FROM park WHERE reference=?"));
            for (const auto& value : references) {
                if (!ok || (cancelled && cancelled->load())) { ok = false; break; }
                const QString raw = value.toString().trimmed().toUpper(); const QStringList parts = raw.split(QLatin1Char('@'));
                QVariantMap row;
                if (parts.size() > 2 || parts.first().isEmpty() || (parts.size() == 2 && parts.last().isEmpty())) { row[QStringLiteral("status")] = QStringLiteral("invalidReference"); rows << row; continue; }
                q.bindValue(0, parts.first()); if (!q.exec()) { ok = false; break; }
                if (!q.next()) { if (q.lastError().isValid()) { ok = false; break; } row[QStringLiteral("status")] = QStringLiteral("notFound"); }
                else {
                    if (q.value(2).toInt() != 0 && q.value(2).toInt() != 1) { ok = false; break; }
                    row[QStringLiteral("reference")] = q.value(0); row[QStringLiteral("name")] = q.value(1); row[QStringLiteral("locations")] = q.value(3);
                    row[QStringLiteral("status")] = q.value(2).toInt() == 1 ? QStringLiteral("listedActive") : QStringLiteral("listedInactive");
                    if (parts.size() == 2) {
                        const QStringList locations = q.value(3).toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
                        bool found = false; for (const auto& location : locations) if (location.trimmed() == parts.last()) { found = true; break; }
                        row[QStringLiteral("locationStatus")] = locations.isEmpty() ? QStringLiteral("unknown") : found ? QStringLiteral("listed") : QStringLiteral("notListed");
                    }
                }
                rows << row;
            }
            if (ok && db.commit()) { result = metadata; result[QStringLiteral("status")] = QStringLiteral("available"); result[QStringLiteral("rows")] = rows; }
            else { db.rollback(); if (cancelled && cancelled->load()) result[QStringLiteral("status")] = QStringLiteral("cancelled"); }
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(connection); return result;
}
QVariantMap PotaCatalogue::info() const { auto result = lookupBatch(m_path, {}); result.remove(QStringLiteral("rows")); return result; }
QVariantMap PotaCatalogue::lookup(const QString& reference) const
{
    auto result = lookupBatch(m_path, {reference}); const auto rows = result.take(QStringLiteral("rows")).toList();
    if (!rows.isEmpty()) { const auto row = rows.first().toMap(); for (auto it = row.begin(); it != row.end(); ++it) result[it.key()] = it.value(); }
    return result;
}
bool PotaCatalogue::importFile(const QUrl& source)
{
    if (m_busy || !source.isLocalFile() || source.toLocalFile().isEmpty() || m_path.isEmpty()) return false;
    const QFileInfo from(source.toLocalFile()), to(m_path);
    if (from.absoluteFilePath() == to.absoluteFilePath() || (!from.canonicalFilePath().isEmpty() && from.canonicalFilePath() == to.canonicalFilePath())) return false;
    m_cancel = std::make_shared<std::atomic_bool>(false); const auto cancelled = m_cancel;
    const QString input = source.toLocalFile(), path = m_path; m_busy = true; emit changed(); QPointer<PotaCatalogue> self(this);
    m_pool.start([self, input, path, cancelled] {
        const QString error = build(input, path, cancelled);
        if (!self) return;
        QMetaObject::invokeMethod(self.data(), [self, error] { if (!self) return; self->m_busy = false; self->m_cancel.reset(); emit self->changed(); emit self->finished(error.isEmpty(), error); }, Qt::QueuedConnection);
    });
    return true;
}
void PotaCatalogue::cancel() { if (m_cancel) m_cancel->store(true); }
} // namespace decolog::core
