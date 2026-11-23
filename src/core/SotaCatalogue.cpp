#include "core/SotaCatalogue.h"
#include <QCryptographicHash>
#include <QCoreApplication>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
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
bool officialUrl(const QUrl& url)
{
    return url.isValid() && url.scheme() == QLatin1String("https") && url.userInfo().isEmpty() &&
        !url.hasFragment() && !url.hasQuery() && url.path() == QLatin1String("/summitslist.csv") &&
        (url.port() == -1 || url.port() == 443) &&
        (url.host() == QLatin1String("www.sotadata.org.uk") || url.host() == QLatin1String("storage.sota.org.uk"));
}
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
QString build(const QString& inputPath, const QString& outputPath, const std::shared_ptr<std::atomic_bool>& cancelled,
              const QString& source, const QString& sourceUrl, const QString& fileName)
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
                    {QStringLiteral("source"), source}, {QStringLiteral("sourceUrl"), sourceUrl}, {QStringLiteral("fileName"), fileName},
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
bool validMetadata(const QVariantMap& data)
{
    return QDate::fromString(data.value(QStringLiteral("version")).toString(), Qt::ISODate).isValid() &&
        data.value(QStringLiteral("count")).toInt() > 0 &&
        QRegularExpression(QStringLiteral("^[0-9a-f]{64}$")).match(data.value(QStringLiteral("sha256")).toString()).hasMatch() &&
        (data.value(QStringLiteral("source")).toString() == QLatin1String("local-file-unverified") ||
         data.value(QStringLiteral("source")).toString() == QLatin1String("network-unverified") ||
         (data.value(QStringLiteral("source")).toString() == QLatin1String("official-https") &&
          officialUrl(QUrl(data.value(QStringLiteral("sourceUrl")).toString()))));
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
                const bool metadataValid = !meta.lastError().isValid() && validMetadata(out);
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
QVariantMap SotaCatalogue::lookupBatch(const QString& path, const QVariantList& requests, const std::shared_ptr<std::atomic_bool>& cancelled)
{
    QVariantMap result{{QStringLiteral("status"), QStringLiteral("unavailable")}};
    if (!QFileInfo::exists(path)) return result;
    const QString connection = uniqueName();
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=100")); db.setDatabaseName(path);
        if (db.open() && db.transaction()) {
            QSqlQuery app(db), meta(db);
            const bool owned = app.exec(QStringLiteral("PRAGMA application_id")) && app.next() && app.value(0).toInt() == 1397707841;
            QVariantMap metadata;
            bool ok = owned && meta.exec(QStringLiteral("SELECT key,value FROM metadata"));
            if (ok) { while (meta.next()) metadata[meta.value(0).toString()] = meta.value(1); ok = !meta.lastError().isValid() && validMetadata(metadata); }
            QVariantList rows; QSqlQuery q(db);
            if (ok) ok = q.prepare(QStringLiteral("SELECT name,valid_from,valid_to FROM summit WHERE reference=?"));
            for (const auto& request : requests) {
                if (!ok || (cancelled && cancelled->load())) { ok = false; break; }
                const QVariantMap wanted = request.toMap();
                const QDate date = QDate::fromString(wanted.value(QStringLiteral("day")).toString(), Qt::ISODate);
                q.bindValue(0, wanted.value(QStringLiteral("reference")).toString().trimmed().toUpper());
                if (!q.exec()) { ok = false; break; }
                QVariantMap row;
                if (!date.isValid()) row[QStringLiteral("status")] = QStringLiteral("invalidDate");
                else if (!q.next()) { if (q.lastError().isValid()) { ok = false; break; } row[QStringLiteral("status")] = QStringLiteral("notFound"); }
                else {
                    const QDate from = QDate::fromString(q.value(1).toString(), Qt::ISODate), to = QDate::fromString(q.value(2).toString(), Qt::ISODate);
                    if (!from.isValid() || !to.isValid() || from > to) { ok = false; break; }
                    row[QStringLiteral("name")] = q.value(0); row[QStringLiteral("validFrom")] = q.value(1); row[QStringLiteral("validTo")] = q.value(2);
                    row[QStringLiteral("status")] = date >= from && date <= to ? QStringLiteral("active") : QStringLiteral("outsideValidity");
                }
                rows << row;
            }
            if (ok && db.commit()) { result = metadata; result[QStringLiteral("status")] = QStringLiteral("available"); result[QStringLiteral("rows")] = rows; }
            else { db.rollback(); if (cancelled && cancelled->load()) result[QStringLiteral("status")] = QStringLiteral("cancelled"); }
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(connection);
    return result;
}

SotaCatalogue::SotaCatalogue(const QString& path, QObject* parent) : QObject(parent), m_path(path), m_network(new QNetworkAccessManager(this)) { m_pool.setMaxThreadCount(1); }
SotaCatalogue::~SotaCatalogue() { if (m_cancel) m_cancel->store(true); if (m_reply) { m_reply->disconnect(this); m_reply->abort(); } m_pool.waitForDone(); }
QVariantMap SotaCatalogue::info() const { return read(m_path, {}, {}, true); }
QVariantMap SotaCatalogue::lookup(const QString& reference, const QDate& date) const { return read(m_path, reference, date, false); }
QVariantMap SotaCatalogue::lookupIso(const QString& reference, const QString& date) const { return lookup(reference, QDate::fromString(date, Qt::ISODate)); }
void SotaCatalogue::launchBuild(const QString& input, const QString& source, const QString& sourceUrl, const QString& fileName)
{
    const QString path = m_path; const auto cancelled = m_cancel; QPointer<SotaCatalogue> self(this);
    m_pool.start([self, input, path, source, sourceUrl, fileName, cancelled] {
        const QString error = build(input, path, cancelled, source, sourceUrl, fileName);
        if (!self) return;
        QMetaObject::invokeMethod(self.data(), [self, error] {
            if (!self) return;
            self->m_download.reset(); self->m_busy = false; self->m_cancel.reset(); emit self->changed(); emit self->finished(error.isEmpty(), error);
        }, Qt::QueuedConnection);
    });
}

bool SotaCatalogue::importFile(const QUrl& source)
{
    if (m_busy || !source.isLocalFile() || source.toLocalFile().isEmpty() || m_path.isEmpty()) return false;
    const QFileInfo from(source.toLocalFile()), to(m_path);
    if (from.absoluteFilePath() == to.absoluteFilePath() || (!from.canonicalFilePath().isEmpty() && from.canonicalFilePath() == to.canonicalFilePath())) return false;
    m_cancel = std::make_shared<std::atomic_bool>(false); m_busy = true; emit changed();
    launchBuild(source.toLocalFile(), QStringLiteral("local-file-unverified"), QStringLiteral(""), from.fileName());
    return true;
}

bool SotaCatalogue::allowedUrl(const QUrl& url) const
{
    if (!url.isValid() || !url.userInfo().isEmpty() || url.hasFragment()) return false;
    if (m_testLoopback) return url.scheme() == QLatin1String("http") && url.host() == QLatin1String("127.0.0.1") && url.port() == m_initialUrl.port();
    return officialUrl(url);
}

bool SotaCatalogue::updateFromOfficial()
{
    return startDownload(QUrl(QStringLiteral("https://www.sotadata.org.uk/summitslist.csv")), false);
}

bool SotaCatalogue::updateFromLoopbackForTest(const QUrl& source)
{
    return startDownload(source, true);
}

bool SotaCatalogue::startDownload(const QUrl& url, bool testLoopback)
{
    if (m_busy || m_path.isEmpty()) return false;
    m_initialUrl = url; m_testLoopback = testLoopback;
    if (!allowedUrl(url) || (testLoopback && url.port() <= 0)) return false;
    m_download = std::make_unique<QTemporaryFile>();
    if (!m_download->open()) { m_download.reset(); return false; }
    m_cancel = std::make_shared<std::atomic_bool>(false); m_downloadBytes = 0; m_redirects = 0; m_downloadError.clear();
    m_busy = true; emit changed(); issueRequest(url); return true;
}

void SotaCatalogue::finishDownload(const QString& error)
{
    m_reply = nullptr; m_download.reset(); m_cancel.reset(); m_busy = false;
    emit changed(); emit finished(false, error);
}

void SotaCatalogue::issueRequest(const QUrl& url)
{
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(30000);
    request.setRawHeader("Accept-Encoding", "identity");
    request.setRawHeader("User-Agent", "DecoDXLog/SOTA-catalogue");
    QNetworkReply* reply = m_network->get(request); m_reply = reply;
    connect(reply, &QNetworkReply::metaDataChanged, this, [this, reply] {
        const QVariant size = reply->header(QNetworkRequest::ContentLengthHeader);
        if (size.isValid() && size.toLongLong() > kMaxInput) { m_downloadError = tr("The download exceeds the size limit."); reply->abort(); }
    });
    connect(reply, &QIODevice::readyRead, this, [this, reply] {
        while (reply->bytesAvailable() > 0 && m_downloadError.isEmpty()) {
            const QByteArray block = reply->read(64 * 1024); m_downloadBytes += block.size();
            if (block.isEmpty() || m_downloadBytes > kMaxInput || !m_download || m_download->write(block) != block.size()) {
                m_downloadError = tr("The download is too large or cannot be saved."); reply->abort(); return;
            }
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QUrl redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        const auto networkError = reply->error(); const QUrl currentUrl = reply->url();
        m_reply = nullptr; reply->deleteLater();
        if (m_cancel && m_cancel->load()) { finishDownload(tr("Catalogue update cancelled.")); return; }
        if (!m_downloadError.isEmpty()) { finishDownload(m_downloadError); return; }
        if (httpStatus >= 300 && httpStatus < 400 && redirect.isValid() && !redirect.isEmpty()) {
            const QUrl next = currentUrl.resolved(redirect);
            if (++m_redirects > 5 || !allowedUrl(next)) { finishDownload(tr("The catalogue redirect is not permitted.")); return; }
            if (!m_download || !m_download->resize(0) || !m_download->seek(0)) { finishDownload(tr("Cannot reset the download file.")); return; }
            m_downloadBytes = 0; issueRequest(next); return;
        }
        if (networkError != QNetworkReply::NoError || httpStatus != 200 || m_downloadBytes <= 0 || !m_download || !m_download->flush()) {
            finishDownload(tr("The catalogue download failed or was incomplete.")); return;
        }
        const QString path = m_download->fileName(); m_download->close();
        launchBuild(path, m_testLoopback ? QStringLiteral("network-unverified") : QStringLiteral("official-https"),
                    currentUrl.toString(QUrl::FullyEncoded), QStringLiteral("summitslist.csv"));
    });
}

void SotaCatalogue::cancel()
{
    if (m_cancel) m_cancel->store(true);
    if (m_reply) m_reply->abort();
}
} // namespace decolog::core
