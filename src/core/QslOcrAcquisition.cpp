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
QVariantMap QslOcrAcquisition::execute(const QString &dbPath, const QString &snapshot, const QString &sha256,
                                       const QString &sourceName, const QString &ocrText,
                                       const QVariantList &selected, const std::atomic_bool *cancelled)
{
    auto failed = [](const QString &error) { return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}}; };
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
            q.prepare(QStringLiteral("INSERT OR IGNORE INTO qsl_attachment_qso(attachment_id,qso_id) VALUES(?,?)"));
            q.addBindValue(attachmentId); q.addBindValue(qso.value(QStringLiteral("id")));
            ok = q.exec();
            const int added = ok && q.numRowsAffected() > 0 ? 1 : 0;
            newLinks += added;
            if (ok) {
                q.prepare(QStringLiteral("INSERT INTO qsl_ocr_acquisition_qso(acquisition_id,qso_id,qso_uuid,qso_revision,newly_linked) VALUES(?,?,?,?,?)"));
                q.addBindValue(acquisitionId); q.addBindValue(qso.value(QStringLiteral("id"))); q.addBindValue(qso.value(QStringLiteral("uuid")));
                q.addBindValue(qso.value(QStringLiteral("revision"))); q.addBindValue(added); ok = q.exec();
            }
        }
        if (ok && !cancelled->load()) {
            ok = q.exec(QStringLiteral("COMMIT"));
            if (ok) { transaction = false; result = {{QStringLiteral("ok"), true}, {QStringLiteral("acquisitionId"), acquisitionId},
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
} // namespace decolog::core
