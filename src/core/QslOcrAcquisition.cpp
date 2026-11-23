#include "core/QslOcrAcquisition.h"
#include "core/LogDatabase.h"
#include "core/TesseractOcr.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QPointer>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <limits>
namespace decolog::core {
QslOcrAcquisition::QslOcrAcquisition(LogDatabase *db, TesseractOcr *ocr, QObject *parent)
    : QObject(parent), m_db(db), m_ocr(ocr) {}
QslOcrAcquisition::~QslOcrAcquisition() { if (m_cancel) m_cancel->store(true); m_pool.waitForDone(); }
bool QslOcrAcquisition::start(const QVariantList &selected, const QUrl &original)
{
    if (m_busy) return false;
    m_result.clear(); m_error.clear();
    if (!m_db || !m_db->isOpen() || m_db->path().isEmpty() || m_db->path() == QLatin1String(":memory:") ||
        !m_ocr || m_ocr->busy() || m_ocr->snapshotPath().isEmpty() || m_ocr->snapshotSha256().isEmpty() ||
        !original.isLocalFile() || QFileInfo(original.toLocalFile()).absoluteFilePath() != m_ocr->sourcePath() ||
        selected.isEmpty() || selected.size() > 100) {
        m_error = tr("Choose a completed OCR image and one to 100 QSO"); emit changed(); return false;
    }
    const QString dbPath = m_db->path(), snapshot = m_ocr->snapshotPath(), hash = m_ocr->snapshotSha256();
    const QString filename = QFileInfo(original.toLocalFile()).fileName(), ocrText = m_ocr->text();
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const auto cancelled = m_cancel;
    m_busy = true; emit changed();
    QPointer<QslOcrAcquisition> self(this);
    m_pool.start([self, dbPath, snapshot, hash, filename, ocrText, selected, cancelled] {
        const QVariantMap result = execute(dbPath, snapshot, hash, filename, ocrText, selected, cancelled.get());
        QMetaObject::invokeMethod(self.data(), [self, result] {
            if (!self) return;
            self->m_busy = false; self->m_cancel.reset(); self->m_result = result;
            self->m_error = result.value(QStringLiteral("error")).toString();
            emit self->changed(); emit self->finished(result.value(QStringLiteral("ok")).toBool());
        }, Qt::QueuedConnection);
    });
    return true;
}
void QslOcrAcquisition::cancel() { if (m_cancel) m_cancel->store(true); }
bool QslOcrAcquisition::review(qint64 qsoId, qint64 beforeId)
{
    if (m_busy) return false;
    m_result.clear(); m_history.clear(); m_error.clear();
    if (!m_db || !m_db->isOpen() || m_db->path().isEmpty() || m_db->path() == QLatin1String(":memory:") || qsoId <= 0) {
        m_error = tr("Choose a QSO in an open file-backed log"); emit changed(); return false;
    }
    const QString path = m_db->path();
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const auto cancelled = m_cancel;
    m_busy = true; emit changed();
    QPointer<QslOcrAcquisition> self(this);
    m_pool.start([self, path, qsoId, beforeId, cancelled] {
        QVariantList rows;
        QString error;
        const QString connection = QStringLiteral("qsl-ocr-review-") + QUuid::createUuid().toString();
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
            db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=100"));
            db.setDatabaseName(path);
            if (!db.open()) error = tr("Cannot open the log for reviewing acquisitions");
            else if (!db.transaction()) error = tr("Cannot review OCR acquisitions");
            else {
                QSqlQuery q(db);
                q.prepare(QStringLiteral("SELECT j.acquisition_id FROM qsl_ocr_acquisition_qso j INDEXED BY idx_qsl_ocr_acquisition_qso "
                                         "WHERE j.qso_uuid=(SELECT uuid FROM qso WHERE id=?) AND j.qso_id=? "
                                         "AND j.acquisition_id<? ORDER BY j.acquisition_id DESC LIMIT 51"));
                q.addBindValue(qsoId); q.addBindValue(qsoId);
                q.addBindValue(beforeId > 0 ? beforeId : std::numeric_limits<qint64>::max());
                if (!q.exec()) error = tr("Cannot review OCR acquisitions");
                while (error.isEmpty() && !cancelled->load() && q.next()) {
                    const qint64 id = q.value(0).toLongLong();
                    QSqlQuery header(db);
                    header.prepare(QStringLiteral("SELECT uuid,created_at,source_name,sha256,substr(ocr_text,1,4096),"
                                                  "length(ocr_text),undone_at,attachment_id FROM qsl_ocr_acquisition WHERE id=?"));
                    header.addBindValue(id);
                    if (!header.exec() || !header.next()) { error = tr("Cannot review OCR acquisitions"); break; }
                    const qint64 attachment = header.value(7).toLongLong();
                    QVariantList targets;
                    bool guarded = true;
                    QSqlQuery detail(db);
                    detail.prepare(QStringLiteral("SELECT j.qso_id,j.qso_uuid,j.qso_revision,j.newly_linked,j.link_token,j.link_revision,"
                                                  "q.call,q.uuid,q.revision,q.deleted,l.link_token,l.link_revision "
                                                  "FROM qsl_ocr_acquisition_qso j LEFT JOIN qso q ON q.id=j.qso_id "
                                                  "LEFT JOIN qsl_attachment_qso l ON l.attachment_id=? AND l.qso_id=j.qso_id "
                                                  "WHERE j.acquisition_id=? ORDER BY j.qso_id"));
                    detail.addBindValue(attachment); detail.addBindValue(id);
                    if (!detail.exec()) { error = tr("Cannot review OCR acquisitions"); break; }
                    while (!cancelled->load() && detail.next()) {
                        const bool created = detail.value(3).toBool();
                        const bool unchanged = !detail.value(4).isNull() && !detail.value(5).isNull() &&
                            detail.value(7).toString() == detail.value(1).toString() && detail.value(8).toLongLong() == detail.value(2).toLongLong() &&
                            !detail.value(9).toBool() && detail.value(10).toString() == detail.value(4).toString() &&
                            detail.value(11).toLongLong() == detail.value(5).toLongLong();
                        if (created && !unchanged) guarded = false;
                        targets << QVariantMap{{QStringLiteral("id"), detail.value(0)}, {QStringLiteral("call"), detail.value(6)},
                                               {QStringLiteral("uuid"), detail.value(1)}, {QStringLiteral("created"), created},
                                               {QStringLiteral("unchanged"), unchanged}};
                    }
                    if (detail.lastError().isValid()) { error = tr("Cannot review OCR acquisitions"); break; }
                    if (targets.isEmpty()) guarded = false;
                    QSqlQuery revision(db);
                    revision.prepare(QStringLiteral("SELECT id,reviewed_text,created_at FROM qsl_ocr_review WHERE acquisition_id=? ORDER BY id DESC LIMIT 1"));
                    revision.addBindValue(id);
                    if (!revision.exec()) { error = tr("Cannot review OCR acquisitions"); break; }
                    const bool hasReview=revision.next();
                    rows << QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("uuid"), header.value(0)},
                                        {QStringLiteral("createdAt"), header.value(1)}, {QStringLiteral("sourceName"), header.value(2)},
                                        {QStringLiteral("sha256"), header.value(3)}, {QStringLiteral("text"), header.value(4)},
                                        {QStringLiteral("textTruncated"), header.value(5).toInt() > 4096},
                                        {QStringLiteral("undoneAt"), header.value(6)}, {QStringLiteral("targets"), targets},
                                        {QStringLiteral("reviewId"),hasReview ? revision.value(0) : QVariant(qint64(0))},
                                        {QStringLiteral("reviewedText"),hasReview ? revision.value(1) : QVariant(QString())},
                                        {QStringLiteral("reviewedAt"),hasReview ? revision.value(2) : QVariant(QString())},
                                        {QStringLiteral("guarded"), guarded}};
                }
                if (q.lastError().isValid()) error = tr("Cannot review OCR acquisitions");
                q.finish();
                if (error.isEmpty() && !cancelled->load()) {
                    if (!db.commit()) error = tr("Cannot review OCR acquisitions");
                } else {
                    db.rollback();
                }
            }
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
        QMetaObject::invokeMethod(self.data(), [self, path, rows, error, cancelled] {
            if (!self) return;
            self->m_busy = false; self->m_cancel.reset();
            const bool sameLog = self->m_db && self->m_db->isOpen() && self->m_db->path() == path;
            const bool success = !cancelled->load() && error.isEmpty() && sameLog;
            self->m_history = success ? rows : QVariantList{};
            self->m_error = cancelled->load() ? tr("Review cancelled") : !sameLog ? tr("The open log changed") : error;
            self->m_result = {{QStringLiteral("ok"), success}, {QStringLiteral("operation"), QStringLiteral("review")}};
            emit self->changed(); emit self->finished(success);
        }, Qt::QueuedConnection);
    });
    return true;
}
bool QslOcrAcquisition::saveReview(qint64 acquisitionId,const QString &uuid,qint64 expectedReviewId,const QString &text)
{
    if (m_busy) return false;
    m_result.clear(); m_error.clear();
    if (!m_db || !m_db->isOpen() || m_db->path().isEmpty() || m_db->path()==QLatin1String(":memory:")) {
        m_error=tr("Choose an open file-backed log"); emit changed(); return false;
    }
    const QString path=m_db->path(); m_cancel=std::make_shared<std::atomic_bool>(false); const auto cancelled=m_cancel;
    m_busy=true; emit changed(); QPointer<QslOcrAcquisition> self(this);
    m_pool.start([self,path,acquisitionId,uuid,expectedReviewId,text,cancelled] {
        const QVariantMap result=saveReviewExecute(path,acquisitionId,uuid,expectedReviewId,text,cancelled.get());
        if (!self) return;
        QMetaObject::invokeMethod(self.data(),[self,result] {
            if (!self) return; self->m_busy=false; self->m_cancel.reset(); self->m_result=result;
            self->m_error=result.value(QStringLiteral("error")).toString(); emit self->changed(); emit self->finished(result.value(QStringLiteral("ok")).toBool());
        },Qt::QueuedConnection);
    });
    return true;
}
QVariantMap QslOcrAcquisition::saveReviewExecute(const QString &path,qint64 acquisitionId,const QString &uuid,qint64 expectedReviewId,const QString &text,const std::atomic_bool *cancelled)
{
    auto failed=[](const QString& error) { return QVariantMap{{QStringLiteral("ok"),false},{QStringLiteral("operation"),QStringLiteral("saveReview")},{QStringLiteral("error"),error}}; };
    if (!cancelled || acquisitionId<=0 || uuid.isEmpty() || uuid.size()>80 || expectedReviewId<0 || text.trimmed().isEmpty() || text.size()>65536 || !QFileInfo(path).isFile())
        return failed(tr("Choose an acquisition and a reviewed text of at most 65536 characters"));
    if (cancelled->load()) return failed(tr("Review cancelled"));
    const QString connection=QStringLiteral("qsl-ocr-text-")+QUuid::createUuid().toString(); QVariantMap result;
    {
        QSqlDatabase db=QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),connection);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_URI;QSQLITE_BUSY_TIMEOUT=250"));
        db.setDatabaseName(QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()).toString(QUrl::FullyEncoded)+QStringLiteral("?mode=rw"));
        QSqlQuery q(db); bool transaction=false; bool ok=db.open();
        if (ok) ok=q.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
        if (ok) { ok=q.exec(QStringLiteral("BEGIN IMMEDIATE")); transaction=ok; }
        if (ok) {
            q.prepare(QStringLiteral("SELECT COALESCE((SELECT MAX(id) FROM qsl_ocr_review WHERE acquisition_id=a.id),0) FROM qsl_ocr_acquisition a WHERE id=? AND uuid=? AND undone_at IS NULL"));
            q.addBindValue(acquisitionId); q.addBindValue(uuid);
            if (!q.exec() || !q.next()) { ok=false; result=failed(tr("Acquisition is unavailable or undone")); }
            else if (q.value(0).toLongLong()!=expectedReviewId) { ok=false; result=failed(tr("Review changed; refresh before saving")); }
        }
        qint64 reviewId=0;
        if (ok && !cancelled->load()) {
            q.prepare(QStringLiteral("INSERT INTO qsl_ocr_review(acquisition_id,reviewed_text,created_at) VALUES(?,?,?)"));
            q.addBindValue(acquisitionId); q.addBindValue(text); q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
            ok=q.exec(); if (ok) reviewId=q.lastInsertId().toLongLong();
        }
        if (ok && !cancelled->load()) { ok=q.exec(QStringLiteral("COMMIT")); if (ok) transaction=false; }
        else ok=false;
        if (transaction) q.exec(QStringLiteral("ROLLBACK"));
        if (ok) result={{QStringLiteral("ok"),true},{QStringLiteral("operation"),QStringLiteral("saveReview")},{QStringLiteral("reviewId"),reviewId}};
        else if (result.isEmpty()) result=failed(cancelled->load()?tr("Review cancelled"):tr("Cannot save the reviewed text"));
        db.close();
    }
    QSqlDatabase::removeDatabase(connection); return result;
}
bool QslOcrAcquisition::undo(qint64 acquisitionId, const QString &uuid)
{
    if (m_busy) return false;
    m_result.clear(); m_error.clear();
    if (!m_db || !m_db->isOpen() || m_db->path().isEmpty() || m_db->path() == QLatin1String(":memory:") ||
        acquisitionId <= 0 || uuid.isEmpty() || uuid.size() > 80) {
        m_error = tr("Choose a saved OCR acquisition"); emit changed(); return false;
    }
    const QString path = m_db->path();
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const auto cancelled = m_cancel;
    m_busy = true; emit changed();
    QPointer<QslOcrAcquisition> self(this);
    m_pool.start([self, path, acquisitionId, uuid, cancelled] {
        const QVariantMap result = undoExecute(path, acquisitionId, uuid, cancelled.get());
        QMetaObject::invokeMethod(self.data(), [self, result] {
            if (!self) return;
            self->m_busy = false; self->m_cancel.reset(); self->m_result = result;
            self->m_error = result.value(QStringLiteral("error")).toString();
            emit self->changed(); emit self->finished(result.value(QStringLiteral("ok")).toBool());
        }, Qt::QueuedConnection);
    });
    return true;
}
QVariantMap QslOcrAcquisition::execute(const QString &dbPath, const QString &snapshot, const QString &sha256,
                                       const QString &sourceName, const QString &ocrText,
                                       const QVariantList &selected, const std::atomic_bool *cancelled)
{
    auto failed = [](const QString &error) { return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("operation"), QStringLiteral("attach")}, {QStringLiteral("error"), error}}; };
    if (!cancelled || selected.isEmpty() || selected.size() > 100 || sha256.size() != 64 || ocrText.size() > 4 * 1024 * 1024)
        return failed(tr("Invalid OCR acquisition"));
    if (!QFileInfo(dbPath).isFile()) return failed(tr("The selected log is unavailable"));
    QSet<QString> uuids;
    for (const QVariant &entry : selected) {
        const auto qso = entry.toMap();
        const QString uuid = qso.value(QStringLiteral("uuid")).toString();
        if (qso.value(QStringLiteral("id")).toLongLong() <= 0 || uuid.isEmpty() || uuid.size() > 80 ||
            qso.value(QStringLiteral("revision")).toLongLong() <= 0 || uuids.contains(uuid))
            return failed(tr("Invalid or duplicate QSO selection"));
        uuids.insert(uuid);
    }
    QFile input(snapshot);
    if (!input.open(QIODevice::ReadOnly) || input.size() <= 0 || input.size() > 32 * 1024 * 1024)
        return failed(tr("The OCR image copy is unavailable"));
    QByteArray data;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!cancelled->load()) {
        const QByteArray block = input.read(256 * 1024);
        if (block.isEmpty()) break;
        if (data.size() + block.size() > 32 * 1024 * 1024) return failed(tr("The OCR image copy is unavailable"));
        data += block; hash.addData(block);
    }
    if (cancelled->load()) return failed(tr("Acquisition cancelled"));
    if (input.error() != QFile::NoError || data.isEmpty() || QString::fromLatin1(hash.result().toHex()) != sha256)
        return failed(tr("The OCR image copy changed; read the image again"));
    const QString connection = QStringLiteral("qsl-ocr-save-") + QUuid::createUuid().toString();
    QVariantMap result;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_URI;QSQLITE_BUSY_TIMEOUT=250"));
        db.setDatabaseName(QUrl::fromLocalFile(QFileInfo(dbPath).absoluteFilePath()).toString(QUrl::FullyEncoded) + QStringLiteral("?mode=rw"));
        bool transaction = false;
        bool ok = db.open();
        QSqlQuery q(db);
        if (ok) ok = q.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
        if (ok) { ok = q.exec(QStringLiteral("BEGIN IMMEDIATE")); transaction = ok; }
        for (const QVariant &entry : selected) {
            if (!ok || cancelled->load()) { ok = false; break; }
            const auto qso = entry.toMap();
            q.prepare(QStringLiteral("SELECT 1 FROM qso WHERE id=? AND uuid=? AND revision=? AND deleted=0"));
            q.addBindValue(qso.value(QStringLiteral("id"))); q.addBindValue(qso.value(QStringLiteral("uuid")));
            q.addBindValue(qso.value(QStringLiteral("revision")));
            if (!q.exec() || !q.next()) { result = failed(tr("A selected QSO changed or is no longer available; search again")); ok = false; }
        }
        const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        if (ok && !cancelled->load()) {
            q.prepare(QStringLiteral("INSERT OR IGNORE INTO qsl_attachment(sha256,filename,mime,size,data,created_at) VALUES(?,?,?,?,?,?)"));
            q.addBindValue(sha256); q.addBindValue(sourceName.isEmpty() ? QStringLiteral("qsl") : sourceName.left(255));
            q.addBindValue(QMimeDatabase().mimeTypeForData(data).name()); q.addBindValue(data.size()); q.addBindValue(data); q.addBindValue(now);
            ok = q.exec();
        }
        qint64 attachmentId = 0, acquisitionId = 0;
        if (ok && !cancelled->load()) {
            q.prepare(QStringLiteral("SELECT id FROM qsl_attachment WHERE sha256=?")); q.addBindValue(sha256);
            ok = q.exec() && q.next(); if (ok) attachmentId = q.value(0).toLongLong();
        }
        if (ok && !cancelled->load()) {
            q.prepare(QStringLiteral("INSERT INTO qsl_ocr_acquisition(uuid,attachment_id,sha256,source_name,ocr_text,created_at) VALUES(?,?,?,?,?,?)"));
            q.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces)); q.addBindValue(attachmentId); q.addBindValue(sha256);
            q.addBindValue(sourceName.left(255)); q.addBindValue(ocrText); q.addBindValue(now);
            ok = q.exec(); if (ok) acquisitionId = q.lastInsertId().toLongLong();
        }
        int newLinks = 0;
        for (const QVariant &entry : selected) {
            if (!ok || cancelled->load()) { ok = false; break; }
            const auto qso = entry.toMap();
            const QString linkToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
            q.prepare(QStringLiteral("INSERT OR IGNORE INTO qsl_attachment_qso(attachment_id,qso_id,link_token) VALUES(?,?,?)"));
            q.addBindValue(attachmentId); q.addBindValue(qso.value(QStringLiteral("id"))); q.addBindValue(linkToken);
            ok = q.exec();
            const int added = ok && q.numRowsAffected() > 0 ? 1 : 0;
            newLinks += added;
            if (ok && !added) {
                q.prepare(QStringLiteral("UPDATE qsl_attachment_qso SET link_revision=link_revision+1 WHERE attachment_id=? AND qso_id=?"));
                q.addBindValue(attachmentId); q.addBindValue(qso.value(QStringLiteral("id"))); ok = q.exec() && q.numRowsAffected() == 1;
            }
            if (ok) {
                q.prepare(QStringLiteral("INSERT INTO qsl_ocr_acquisition_qso(acquisition_id,qso_id,qso_uuid,qso_revision,newly_linked,link_token,link_revision) VALUES(?,?,?,?,?,?,?)"));
                q.addBindValue(acquisitionId); q.addBindValue(qso.value(QStringLiteral("id"))); q.addBindValue(qso.value(QStringLiteral("uuid")));
                q.addBindValue(qso.value(QStringLiteral("revision"))); q.addBindValue(added);
                q.addBindValue(added ? QVariant(linkToken) : QVariant()); q.addBindValue(added ? QVariant(1) : QVariant()); ok = q.exec();
            }
        }
        if (ok && !cancelled->load()) {
            ok = q.exec(QStringLiteral("COMMIT"));
            if (ok) { transaction = false; result = {{QStringLiteral("ok"), true}, {QStringLiteral("operation"), QStringLiteral("attach")}, {QStringLiteral("acquisitionId"), acquisitionId},
                                                     {QStringLiteral("attachmentId"), attachmentId}, {QStringLiteral("selected"), selected.size()},
                                                     {QStringLiteral("newLinks"), newLinks}, {QStringLiteral("sha256"), sha256}}; }
        }
        if (transaction) q.exec(QStringLiteral("ROLLBACK"));
        if (result.isEmpty()) result = failed(cancelled->load() ? tr("Acquisition cancelled") : tr("Cannot save the OCR acquisition"));
        db.close();
    }
    QSqlDatabase::removeDatabase(connection);
    return result;
}
QVariantMap QslOcrAcquisition::undoExecute(const QString &dbPath, qint64 acquisitionId, const QString &uuid,
                                           const std::atomic_bool *cancelled)
{
    const auto failed = [](const QString &error) {
        return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("operation"), QStringLiteral("undo")},
                           {QStringLiteral("error"), error}};
    };
    if (!cancelled || acquisitionId <= 0 || uuid.isEmpty() || uuid.size() > 80 || !QFileInfo(dbPath).isFile())
        return failed(tr("Choose a saved OCR acquisition"));
    if (cancelled->load()) return failed(tr("Undo cancelled"));
    const QString connection = QStringLiteral("qsl-ocr-undo-") + QUuid::createUuid().toString();
    QVariantMap result;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_URI;QSQLITE_BUSY_TIMEOUT=250"));
        db.setDatabaseName(QUrl::fromLocalFile(QFileInfo(dbPath).absoluteFilePath()).toString(QUrl::FullyEncoded) + QStringLiteral("?mode=rw"));
        bool transaction = false;
        bool ok = db.open();
        QSqlQuery q(db);
        if (ok) ok = q.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
        if (ok) { ok = q.exec(QStringLiteral("BEGIN IMMEDIATE")); transaction = ok; }
        qint64 attachmentId = 0;
        if (ok) {
            q.prepare(QStringLiteral("SELECT attachment_id FROM qsl_ocr_acquisition WHERE id=? AND uuid=? AND undone_at IS NULL"));
            q.addBindValue(acquisitionId); q.addBindValue(uuid);
            if (!q.exec() || !q.next()) { ok = false; result = failed(tr("Acquisition is unavailable or already undone")); }
            else attachmentId = q.value(0).toLongLong();
        }
        QVariantList links;
        if (ok) {
            q.prepare(QStringLiteral("SELECT j.qso_id,j.qso_uuid,j.qso_revision,j.newly_linked,j.link_token,j.link_revision,"
                                     "q.uuid,q.revision,q.deleted,l.link_token,l.link_revision "
                                     "FROM qsl_ocr_acquisition_qso j LEFT JOIN qso q ON q.id=j.qso_id "
                                     "LEFT JOIN qsl_attachment_qso l ON l.attachment_id=? AND l.qso_id=j.qso_id "
                                     "WHERE j.acquisition_id=? ORDER BY j.qso_id"));
            q.addBindValue(attachmentId); q.addBindValue(acquisitionId);
            if (!q.exec()) ok = false;
            int count = 0;
            while (ok && q.next()) {
                ++count;
                if (cancelled->load()) { ok = false; break; }
                if (!q.value(3).toBool()) continue;
                if (q.value(4).isNull() || q.value(5).isNull() || q.value(6).toString() != q.value(1).toString() ||
                    q.value(7).toLongLong() != q.value(2).toLongLong() || q.value(8).toBool() ||
                    q.value(9).toString() != q.value(4).toString() || q.value(10).toLongLong() != q.value(5).toLongLong()) {
                    result = failed(tr("A QSO or attachment link changed; undo requires manual review")); ok = false; break;
                }
                links << QVariantMap{{QStringLiteral("id"), q.value(0)}, {QStringLiteral("token"), q.value(4)},
                                     {QStringLiteral("revision"), q.value(5)}};
            }
            if (q.lastError().isValid()) ok = false;
            if (ok && count == 0) { result = failed(tr("Acquisition has no recorded QSO")); ok = false; }
        }
        for (const auto &entry : links) {
            if (!ok || cancelled->load()) { ok = false; break; }
            const auto link = entry.toMap();
            q.prepare(QStringLiteral("DELETE FROM qsl_attachment_qso WHERE attachment_id=? AND qso_id=? AND link_token=? AND link_revision=?"));
            q.addBindValue(attachmentId); q.addBindValue(link.value(QStringLiteral("id")));
            q.addBindValue(link.value(QStringLiteral("token"))); q.addBindValue(link.value(QStringLiteral("revision")));
            ok = q.exec() && q.numRowsAffected() == 1;
        }
        if (ok && !cancelled->load()) {
            q.prepare(QStringLiteral("UPDATE qsl_ocr_acquisition SET undone_at=? WHERE id=? AND uuid=? AND undone_at IS NULL"));
            q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
            q.addBindValue(acquisitionId); q.addBindValue(uuid); ok = q.exec() && q.numRowsAffected() == 1;
        }
        if (ok && !cancelled->load()) {
            ok = q.exec(QStringLiteral("COMMIT"));
            if (ok) { transaction = false; result = {{QStringLiteral("ok"), true}, {QStringLiteral("operation"), QStringLiteral("undo")},
                                                     {QStringLiteral("acquisitionId"), acquisitionId}, {QStringLiteral("removedLinks"), links.size()}}; }
        }
        if (transaction) q.exec(QStringLiteral("ROLLBACK"));
        if (result.isEmpty()) result = failed(cancelled->load() ? tr("Undo cancelled") : tr("Cannot undo the OCR acquisition"));
        db.close();
    }
    QSqlDatabase::removeDatabase(connection);
    return result;
}
} // namespace decolog::core
