#include "core/TesseractOcr.h"
#include "core/QslOcrSearch.h"
#include "core/LogDatabase.h"
#include "core/QslOcrAcquisition.h"
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QSqlQuery>
#include <QCryptographicHash>
#include <QTest>
#include <QElapsedTimer>
#include <QThread>
using namespace decolog::core;
class TestTesseractOcr : public QObject {
    Q_OBJECT
  private:
    static QString makeImage(const QTemporaryDir &dir, const QString &name, bool alternate)
    {
        QImage image(1500, 650, QImage::Format_RGB32);
        image.fill(Qt::white);
        QPainter p(&image);
        p.setPen(Qt::black);
        QFont font("Arial", 42, QFont::Bold); p.setFont(font);
        if (alternate) {
            p.drawText(110, 130, "QSL CARD FROM K1ABC");
            p.drawText(110, 260, "DATE 2026-09-23");
            p.drawText(110, 390, "TIME 1430 UTC");
            p.drawText(110, 520, "BAND 20M MODE SSB");
        } else {
            p.drawText(90, 140, "QSL K1ABC 2026-09-23");
            p.drawText(90, 280, "1430 UTC 20M SSB");
            p.drawText(90, 420, "THANK YOU 73");
        }
        p.end();
        const QString path = dir.filePath(name);
        if (!image.save(path)) return {};
        return path;
    }
  private slots:
    void acquisitionOffersGuardedUndo()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        TesseractOcr ocr;
        QslOcrAcquisition acquisition(&db, &ocr);
        QVERIFY(acquisition.metaObject()->indexOfMethod("undo(qlonglong,QString)") >= 0);
    }
    void ocrUsesImmutableImageSnapshot()
    {
        TesseractOcr ocr;
        QVERIFY(ocr.metaObject()->indexOfProperty("snapshotSha256") >= 0);
        if (ocr.executable().isEmpty()) QSKIP("Tesseract is not installed");
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString path = makeImage(dir, "card.png", false); QVERIFY(!path.isEmpty());
        QFile source(path); QVERIFY(source.open(QIODevice::ReadOnly));
        const QByteArray original = source.readAll(); source.close();
        QSignalSpy done(&ocr, &TesseractOcr::finished);
        QVERIFY(ocr.start(QUrl::fromLocalFile(path)));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 30000);
        QVERIFY2(done.first().first().toBool(), qPrintable(ocr.error()));
        QCOMPARE(ocr.snapshotSha256(), QString::fromLatin1(QCryptographicHash::hash(original, QCryptographicHash::Sha256).toHex()));
        QVERIFY(source.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(source.write("changed"), 7); source.close();
        QFile snapshot(ocr.snapshotPath()); QVERIFY(snapshot.open(QIODevice::ReadOnly));
        QCOMPARE(snapshot.readAll(), original);
    }
    void suggestionsPreserveAmbiguousDatesAndSimilarCalls()
    {
        TesseractOcr ocr;
        QVERIFY(ocr.metaObject()->indexOfProperty("suggestions") >= 0);
        QVariantList words;
        for (const QString &word : {"K1ABC", "K1ABO", "03/04/2026", "2026-09-23", "31/02/2026",
                                    "2026", "1430", "UTC", "20M", "SSB"})
            words << QVariantMap{{"text", word}, {"uncertain", word == QLatin1String("K1ABO")}};
        const auto result = TesseractOcr::suggestionsFromWords(words);
        const auto calls = result.value("calls").toList();
        QCOMPARE(calls.size(), 2);
        QCOMPARE(calls.at(0).toMap().value("value").toString(), QString("K1ABC"));
        QCOMPARE(calls.at(1).toMap().value("value").toString(), QString("K1ABO"));
        QVERIFY(calls.at(1).toMap().value("uncertain").toBool());
        const auto dates = result.value("dates").toList();
        QCOMPARE(dates.size(), 3);
        QCOMPARE(dates.at(0).toMap().value("value").toString(), QString("2026-04-03"));
        QCOMPARE(dates.at(1).toMap().value("value").toString(), QString("2026-03-04"));
        QVERIFY(dates.at(0).toMap().value("ambiguous").toBool());
        QCOMPARE(dates.at(2).toMap().value("value").toString(), QString("2026-09-23"));
        QCOMPARE(result.value("times").toList().size(), 1);
        QCOMPARE(result.value("times").toList().first().toMap().value("value").toString(), QString("14:30"));
        QCOMPARE(result.value("bands").toList().first().toMap().value("value").toString(), QString("20m"));
        QCOMPARE(result.value("modes").toList().first().toMap().value("value").toString(), QString("SSB"));
    }
    void rejectsMissingEngineAndNonLocalInput()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString image = makeImage(dir, "card.png", false); QVERIFY(!image.isEmpty());
        TesseractOcr missing("/nonexistent/tesseract");
        QVERIFY(!missing.start(QUrl("https://example.org/card.png")));
        QVERIFY(!missing.start(QUrl::fromLocalFile(image)));
        QVERIFY(!missing.error().isEmpty());
        QVERIFY(missing.text().isEmpty());
    }
    void searchesExactCallWithoutChangingLog()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
        for (const QString &call : {"K1ABC", "K1ABO"}) {
            AdifRecord record{{"CALL", call}, {"QSO_DATE", "20260923"}, {"TIME_ON", "143000"},
                              {"BAND", "20m"}, {"MODE", "SSB"}};
            QCOMPARE(db.insertQso(record, "manual", {}, true, 0, false).status, InsertResult::Status::Inserted);
        }
        QslOcrSearch search(&db); QSignalSpy done(&search, &QslOcrSearch::finished);
        QVERIFY(!search.start("not a call"));
        QVERIFY(search.start("k1abc"));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 5000);
        QVERIFY(done.at(0).first().toBool());
        QCOMPARE(search.matches().size(), 1);
        QCOMPARE(search.matches().first().toMap().value("call").toString(), QString("K1ABC"));
        QVERIFY(search.start("K1ZZZ"));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 2, 5000);
        QVERIFY(done.at(1).first().toBool());
        QVERIFY(search.matches().isEmpty());
        for (int i = 0; i < 103; ++i) {
            AdifRecord record{{"CALL", "K1BIG"}, {"QSO_DATE", "20260923"}, {"TIME_ON", "143000"},
                              {"BAND", "20m"}, {"MODE", "CW"}};
            QCOMPARE(db.insertQso(record, "manual", {}, true, 0, false).status, InsertResult::Status::Inserted);
        }
        QVERIFY(search.start("K1BIG"));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 3, 5000);
        QVERIFY(done.at(2).first().toBool());
        QCOMPARE(search.matches().size(), 100);
        QVERIFY(search.truncated());
        QVERIFY(search.start("K1BIG"));
        search.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 4, 5000);
        QVERIFY(!done.at(3).first().toBool());
        QVERIFY(search.matches().isEmpty());
        QVERIFY(search.start("K1ABC"));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 5, 5000);
        QVERIFY(done.at(4).first().toBool());
        QCOMPARE(search.matches().size(), 1);
        QCOMPARE(db.qsoCount(), 105);
    }
    void searchRejectsReopenedLogAtSamePath()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const QString path=dir.filePath("log.sqlite"); LogDatabase db; QVERIFY(db.open(path));
        QVERIFY(db.insertQso(AdifRecord{{"CALL","K1ABC"},{"QSO_DATE","20261101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}},"fixture",{},true,0,false).id>0);
        QslOcrSearch search(&db); QSignalSpy done(&search,&QslOcrSearch::finished);
        QVERIFY(search.start("K1ABC")); db.close(); QVERIFY(db.open(path));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,5000); QVERIFY(!done.at(0).first().toBool()); QVERIFY(search.matches().isEmpty());
        QVERIFY(search.start("K1ABC")); QTRY_COMPARE_WITH_TIMEOUT(done.size(),2,5000); QVERIFY(done.at(1).first().toBool()); QCOMPARE(search.matches().size(),1);
    }
    void searchRejectsReplacedLogAtSamePath()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const QString path=dir.filePath("log.sqlite"), other=dir.filePath("other.sqlite");
        { LogDatabase replacement; QVERIFY(replacement.open(other)); QVERIFY(replacement.insertQso(AdifRecord{{"CALL","K1ABC"},{"QSO_DATE","20261102"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}},"fixture",{},true,0,false).id>0); }
        LogDatabase db; QVERIFY(db.open(path)); QVERIFY(db.insertQso(AdifRecord{{"CALL","K1ABC"},{"QSO_DATE","20261101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}},"fixture",{},true,0,false).id>0);
        QslOcrSearch search(&db); QSignalSpy done(&search,&QslOcrSearch::finished); QVERIFY(search.start("K1ABC"));
        QVERIFY(QFile::rename(path,dir.filePath("original.sqlite"))); QVERIFY(QFile::rename(other,path));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,5000); QVERIFY(!done.at(0).first().toBool()); QVERIFY(search.matches().isEmpty());
    }
    void completedReviewIsAttributedToOriginalLogSession()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const QString path=dir.filePath("origin.sqlite"); LogDatabase db; QVERIFY(db.open(path));
        const auto inserted=db.insertQso(AdifRecord{{"CALL","K1ABC"},{"QSO_DATE","20261101"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}},"fixture",{},true,0,false); QVERIFY(inserted.id>0);
        QSqlQuery q(db.connection()); QVERIFY(q.exec("SELECT uuid,revision FROM qso")); QVERIFY(q.next()); const QVariantList selected{QVariantMap{{"id",inserted.id},{"uuid",q.value(0)},{"revision",q.value(1)}}}; q.finish();
        const QString image=makeImage(dir,"card.png",false); QFile file(image); QVERIFY(file.open(QIODevice::ReadOnly)); const auto bytes=file.readAll(); file.close();
        const auto hash=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()); std::atomic_bool cancelled{false};
        const auto saved=QslOcrAcquisition::execute(path,image,hash,"card.png","ocr",selected,&cancelled,db.identity(),db.openFileIdentity()); QVERIFY(saved.value("ok").toBool());
        QVERIFY(q.exec("SELECT uuid FROM qsl_ocr_acquisition")); QVERIFY(q.next()); const QString acquisitionUuid=q.value(0).toString(); q.finish();
        QslOcrAcquisition acquisition(&db,nullptr); QSignalSpy done(&acquisition,&QslOcrAcquisition::finished);
        QVERIFY(acquisition.saveReview(saved.value("acquisitionId").toLongLong(),acquisitionUuid,0,"reviewed"));
        QElapsedTimer timer; timer.start(); bool committed=false;
        while (timer.elapsed()<5000 && !committed) { QSqlQuery check(db.connection()); QVERIFY(check.exec("SELECT count(*) FROM qsl_ocr_review")); QVERIFY(check.next()); committed=check.value(0).toInt()==1; if (!committed) QThread::msleep(10); }
        QVERIFY(committed); db.close(); QVERIFY(db.open(path));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,5000); QVERIFY(!done.first().first().toBool());
        QVERIFY(acquisition.result().value("committed").toBool()); QVERIFY(acquisition.result().value("originChanged").toBool());
        QVERIFY(acquisition.error().contains("original log",Qt::CaseInsensitive));
        QSqlQuery verify(db.connection()); QVERIFY(verify.exec("SELECT reviewed_text FROM qsl_ocr_review")); QVERIFY(verify.next()); QCOMPARE(verify.value(0).toString(),QString("reviewed"));
        verify.finish(); QVERIFY(acquisition.saveReview(saved.value("acquisitionId").toLongLong(),acquisitionUuid,0,"stale")); db.close(); QVERIFY(db.open(path));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),2,5000); QVERIFY(!done.last().first().toBool()); QVERIFY(acquisition.result().value("originChanged").toBool());
        QVERIFY(!acquisition.result().value("committed").toBool()); QVERIFY(acquisition.error().contains("failed",Qt::CaseInsensitive));
    }
    void searchesDateAndTimeBeforeApplyingResultLimit()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid()); LogDatabase db; QVERIFY(db.open(dir.filePath("dated.sqlite")));
        for (int i=0;i<110;++i) {
            const auto day= i==0 ? "20260304" : "20260403";
            const auto time= i==0 ? "1430" : "1200";
            QCOMPARE(db.insertQso(AdifRecord{{"CALL","K1ABC"},{"QSO_DATE",day},{"TIME_ON",time},{"BAND","20m"},{"MODE","CW"}},"fixture",{},true,0,false).status,InsertResult::Status::Inserted);
        }
        QslOcrSearch search(&db); QSignalSpy done(&search,&QslOcrSearch::finished);
        QVERIFY(!search.startFiltered("K1ABC","2026-02-30",""));
        QVERIFY(!search.startFiltered("K1ABC","","24:00"));
        QVERIFY(search.startFiltered("k1abc","2026-03-04","14:30"));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,5000); QVERIFY(done.at(0).first().toBool());
        QCOMPARE(search.matches().size(),1); QVERIFY(!search.truncated());
        QCOMPARE(search.matches().first().toMap().value("at").toString().left(16),QString("2026-03-04T14:30"));
        QVERIFY(search.startFiltered("K1ABC","2026-04-03","12:00"));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),2,5000); QCOMPARE(search.matches().size(),100); QVERIFY(search.truncated());
        QVERIFY(search.startFiltered("K1ABC","2026-03-04","12:00"));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),3,5000); QVERIFY(search.matches().isEmpty()); QCOMPARE(db.qsoCount(),qint64(110));
    }
    void acquisitionIsAtomicAndKeepsExistingLinks()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString dbPath = dir.filePath("log #?.sqlite");
        const QString image = makeImage(dir, "card.png", false); QVERIFY(!image.isEmpty());
        QFile file(image); QVERIFY(file.open(QIODevice::ReadOnly)); const QByteArray bytes = file.readAll(); file.close();
        const QString hash = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        LogDatabase db; QVERIFY(db.open(dbPath));
        QVariantList ids;
        for (const QString &call : {"K1AAA", "K1BBB", "K1CCC"}) {
            AdifRecord r{{"CALL", call}, {"QSO_DATE", "20260923"}, {"TIME_ON", "143000"}, {"BAND", "20m"}, {"MODE", "CW"}};
            const auto inserted = db.insertQso(r, "manual", {}, true, 0, false);
            QCOMPARE(inserted.status, InsertResult::Status::Inserted);
            QSqlQuery q(db.connection()); q.prepare("SELECT uuid,revision FROM qso WHERE id=?"); q.addBindValue(inserted.id);
            QVERIFY(q.exec()); QVERIFY(q.next());
            ids << QVariantMap{{"id", inserted.id}, {"uuid", q.value(0)}, {"revision", q.value(1)}};
        }
        const qint64 firstId = ids.at(0).toMap().value("id").toLongLong();
        const qint64 secondId = ids.at(1).toMap().value("id").toLongLong();
        const qint64 thirdId = ids.at(2).toMap().value("id").toLongLong();
        const qint64 attachment = db.addQslAttachment(bytes, "existing.png", "image/png", {firstId}); QVERIFY(attachment > 0);
        std::atomic_bool cancelled{false};
        const QVariantList selection{ids.at(0), ids.at(1)};
        const QVariantList duplicate{ids.at(0), ids.at(0)};
        QVERIFY(!QslOcrAcquisition::execute(dbPath, image, hash, "card.png", "duplicate", duplicate, &cancelled).value("ok").toBool());
        QVERIFY(!QslOcrAcquisition::execute(dbPath, image, hash, "card.png", "wrong origin", selection, &cancelled, db.identity(), QByteArray("different-file")).value("ok").toBool());
        const auto saved = QslOcrAcquisition::execute(dbPath, image, hash, "card.png", "K1AAA K1BBB", selection, &cancelled, db.identity(), db.openFileIdentity());
        QVERIFY2(saved.value("ok").toBool(), qPrintable(saved.value("error").toString()));
        QCOMPARE(saved.value("attachmentId").toLongLong(), attachment);
        QCOMPARE(saved.value("newLinks").toInt(), 1);
        QCOMPARE(db.qslAttachments(firstId).size(), 1);
        QCOMPARE(db.qslAttachments(secondId).size(), 1);
        QCOMPARE(db.qslAttachmentData(attachment), bytes);
        QSqlQuery q(db.connection());
        QVERIFY(q.exec("SELECT ocr_text,sha256 FROM qsl_ocr_acquisition")); QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(), QString("K1AAA K1BBB")); QCOMPARE(q.value(1).toString(), hash);
        QVERIFY(!q.next());
        QVERIFY(q.exec("SELECT id,uuid FROM qsl_ocr_acquisition")); QVERIFY(q.next());
        const qint64 acquisitionId=q.value(0).toLongLong(); const QString acquisitionUuid=q.value(1).toString();
        const auto rejectedLog=QslOcrAcquisition::saveReviewExecute(dbPath,acquisitionId,acquisitionUuid,0,"Wrong log",&cancelled,QStringLiteral("different-log"),db.openFileIdentity());
        QVERIFY(!rejectedLog.value("ok").toBool());
        const auto rejectedFile=QslOcrAcquisition::saveReviewExecute(dbPath,acquisitionId,acquisitionUuid,0,"Wrong file",&cancelled,db.identity(),QByteArray("different-file"));
        QVERIFY(!rejectedFile.value("ok").toBool());
        const auto reviewed=QslOcrAcquisition::saveReviewExecute(dbPath,acquisitionId,acquisitionUuid,0,"Corrected: K1AAA",&cancelled,db.identity(),db.openFileIdentity());
        QVERIFY2(reviewed.value("ok").toBool(),qPrintable(reviewed.value("error").toString()));
        const qint64 reviewId=reviewed.value("reviewId").toLongLong(); QVERIFY(reviewId>0);
        QVERIFY(!QslOcrAcquisition::saveReviewExecute(dbPath,acquisitionId,acquisitionUuid,0,"stale",&cancelled).value("ok").toBool());
        QVERIFY(QslOcrAcquisition::saveReviewExecute(dbPath,acquisitionId,acquisitionUuid,reviewId,"Second correction",&cancelled).value("ok").toBool());
        QVERIFY(!QslOcrAcquisition::saveReviewExecute(dbPath,acquisitionId,acquisitionUuid,reviewId,"",&cancelled).value("ok").toBool());
        QVERIFY(!QslOcrAcquisition::saveReviewExecute(dbPath,acquisitionId,"wrong",reviewId,"wrong acquisition",&cancelled).value("ok").toBool());
        cancelled.store(true);
        QVERIFY(!QslOcrAcquisition::saveReviewExecute(dbPath,acquisitionId,acquisitionUuid,reviewId,"cancelled",&cancelled).value("ok").toBool());
        cancelled.store(false);
        QVERIFY(q.exec("SELECT ocr_text,(SELECT COUNT(*) FROM qsl_ocr_review WHERE acquisition_id=qsl_ocr_acquisition.id) FROM qsl_ocr_acquisition"));
        QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString("K1AAA K1BBB")); QCOMPARE(q.value(1).toInt(),2);
        QslOcrAcquisition history(&db,nullptr); QSignalSpy reviewDone(&history,&QslOcrAcquisition::finished);
        QVERIFY(history.loadReviews(acquisitionId,acquisitionUuid,0)); QTRY_COMPARE_WITH_TIMEOUT(reviewDone.size(),1,5000);
        QVERIFY(reviewDone.first().first().toBool()); QCOMPARE(history.reviews().size(),2);
        QCOMPARE(history.reviews().first().toMap().value("text").toString(),QString("Second correction"));
        QCOMPARE(history.reviews().last().toMap().value("text").toString(),QString("Corrected: K1AAA"));
        QVERIFY(history.loadReviews(acquisitionId,"wrong",0)); QTRY_COMPARE_WITH_TIMEOUT(reviewDone.size(),2,5000);
        QVERIFY(!reviewDone.last().first().toBool()); QVERIFY(history.reviews().isEmpty());
        QVERIFY(q.exec("SELECT MAX(id) FROM qsl_ocr_review")); QVERIFY(q.next()); qint64 latest=q.value(0).toLongLong(); q.finish();
        for (int i=0;i<22;++i) {
            const auto added=QslOcrAcquisition::saveReviewExecute(dbPath,acquisitionId,acquisitionUuid,latest,QString("Revision %1").arg(i),&cancelled);
            QVERIFY(added.value("ok").toBool()); latest=added.value("reviewId").toLongLong();
        }
        QVERIFY(history.loadReviews(acquisitionId,acquisitionUuid,0)); QTRY_COMPARE_WITH_TIMEOUT(reviewDone.size(),3,5000);
        QCOMPARE(history.reviews().size(),21); const qint64 next=history.reviews().at(19).toMap().value("id").toLongLong();
        QVERIFY(history.loadReviews(acquisitionId,acquisitionUuid,next)); QTRY_COMPARE_WITH_TIMEOUT(reviewDone.size(),4,5000);
        QCOMPARE(history.reviews().size(),4); QCOMPARE(history.reviews().last().toMap().value("text").toString(),QString("Corrected: K1AAA"));
        const auto repeated = QslOcrAcquisition::execute(dbPath, image, hash, "card.png", "repeat", selection, &cancelled);
        QVERIFY(repeated.value("ok").toBool()); QCOMPARE(repeated.value("newLinks").toInt(), 0);
        QVERIFY(q.exec("UPDATE qso SET revision=revision+1 WHERE id=" + QString::number(secondId)));
        const QVariantList stale{ids.at(1), ids.at(2)};
        QVERIFY(!QslOcrAcquisition::execute(dbPath, image, hash, "card.png", "stale", stale, &cancelled).value("ok").toBool());
        QVERIFY(db.qslAttachments(thirdId).isEmpty());
        QVERIFY(q.exec("SELECT COUNT(*) FROM qsl_ocr_acquisition")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 2);
        QVERIFY(q.exec("CREATE TRIGGER reject_ocr_link BEFORE INSERT ON qsl_ocr_acquisition_qso WHEN NEW.qso_id=" + QString::number(thirdId) + " BEGIN SELECT RAISE(ABORT,'reject'); END"));
        const QVariantList triggerSelection{ids.at(0), ids.at(2)};
        QVERIFY(!QslOcrAcquisition::execute(dbPath, image, hash, "card.png", "rollback", triggerSelection, &cancelled).value("ok").toBool());
        QVERIFY(db.qslAttachments(thirdId).isEmpty());
        QCOMPARE(db.qslAttachments(firstId).size(), 1);
        QVERIFY(q.exec("SELECT COUNT(*) FROM qsl_ocr_acquisition")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 2);
        const QString otherPath = dir.filePath("other.png");
        QFile other(otherPath); QVERIFY(other.open(QIODevice::WriteOnly));
        const QByteArray otherBytes = bytes + "extra";
        QCOMPARE(other.write(otherBytes), qint64(otherBytes.size())); other.close();
        const QString otherHash = QString::fromLatin1(QCryptographicHash::hash(otherBytes, QCryptographicHash::Sha256).toHex());
        QVERIFY(!QslOcrAcquisition::execute(dbPath, otherPath, otherHash, "other.png", "rollback", triggerSelection, &cancelled).value("ok").toBool());
        QVERIFY(q.exec("SELECT COUNT(*) FROM qsl_attachment")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 1);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate)); file.write("changed"); file.close();
        QVERIFY(!QslOcrAcquisition::execute(dbPath, image, hash, "card.png", "changed", selection, &cancelled).value("ok").toBool());
        cancelled.store(true);
        QVERIFY(!QslOcrAcquisition::execute(dbPath, image, hash, "card.png", "cancelled", selection, &cancelled).value("ok").toBool());
        QCOMPARE(db.qsoCount(), 3);
    }
    void acquisitionUsesRecognizedSnapshotAfterSourceChanges()
    {
        TesseractOcr ocr;
        if (ocr.executable().isEmpty()) QSKIP("Tesseract is not installed");
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString path = makeImage(dir, "card.png", false); QVERIFY(!path.isEmpty());
        QFile source(path); QVERIFY(source.open(QIODevice::ReadOnly)); const QByteArray original = source.readAll(); source.close();
        LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
        AdifRecord record{{"CALL", "K1ABC"}, {"QSO_DATE", "20260923"}, {"TIME_ON", "143000"}, {"BAND", "20m"}, {"MODE", "SSB"}};
        const auto inserted = db.insertQso(record, "manual", {}, true, 0, false);
        QCOMPARE(inserted.status, InsertResult::Status::Inserted);
        QSqlQuery query(db.connection()); query.prepare("SELECT uuid,revision FROM qso WHERE id=?"); query.addBindValue(inserted.id);
        QVERIFY(query.exec()); QVERIFY(query.next());
        const QVariantList selection{QVariantMap{{"id", inserted.id}, {"uuid", query.value(0)}, {"revision", query.value(1)}}};
        query.finish();
        QSignalSpy done(&ocr, &TesseractOcr::finished); QVERIFY(ocr.start(QUrl::fromLocalFile(path)));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 30000); QVERIFY(done.first().first().toBool());
        QVERIFY(source.open(QIODevice::WriteOnly | QIODevice::Truncate)); source.write("different file"); source.close();
        QslOcrAcquisition acquisition(&db, &ocr);
        QVERIFY(!acquisition.start(selection, QUrl::fromLocalFile(dir.filePath("different.png"))));
        QSignalSpy saved(&acquisition, &QslOcrAcquisition::finished);
        QVERIFY(acquisition.start(selection, QUrl::fromLocalFile(path)));
        QTRY_COMPARE_WITH_TIMEOUT(saved.size(), 1, 5000);
        QVERIFY(saved.first().first().toBool());
        const auto result = acquisition.result();
        QVERIFY2(result.value("ok").toBool(), qPrintable(result.value("error").toString()));
        QCOMPARE(db.qslAttachmentData(result.value("attachmentId").toLongLong()), original);
    }
    void guardedUndoPreservesExistingAndLaterLinks()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString image = makeImage(dir, "card.png", false); QVERIFY(!image.isEmpty());
        QFile file(image); QVERIFY(file.open(QIODevice::ReadOnly)); const auto bytes = file.readAll(); file.close();
        const QString hash = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
        QVariantList selected;
        for (const QString &call : {"K1AAA", "K1BBB", "K1CCC"}) {
            AdifRecord r{{"CALL", call}, {"QSO_DATE", "20260923"}, {"TIME_ON", "143000"}, {"BAND", "20m"}, {"MODE", "CW"}};
            const auto inserted = db.insertQso(r, "manual", {}, true, 0, false); QCOMPARE(inserted.status, InsertResult::Status::Inserted);
            QSqlQuery q(db.connection()); q.prepare("SELECT uuid,revision FROM qso WHERE id=?"); q.addBindValue(inserted.id);
            QVERIFY(q.exec()); QVERIFY(q.next()); selected << QVariantMap{{"id", inserted.id}, {"uuid", q.value(0)}, {"revision", q.value(1)}};
        }
        const auto idAt = [&](int i) { return selected.at(i).toMap().value("id").toLongLong(); };
        const qint64 attachment = db.addQslAttachment(bytes, "card.png", "image/png", {idAt(0)}); QVERIFY(attachment > 0);
        std::atomic_bool cancel{false};
        const auto first = QslOcrAcquisition::execute(db.path(), image, hash, "card.png", "ocr", {selected.at(0), selected.at(1)}, &cancel);
        QVERIFY(first.value("ok").toBool()); QCOMPARE(first.value("newLinks").toInt(), 1);
        TesseractOcr ocr; QslOcrAcquisition review(&db, &ocr); QSignalSpy reviewed(&review, &QslOcrAcquisition::finished);
        QVERIFY(review.review(idAt(0), 0)); QTRY_COMPARE_WITH_TIMEOUT(reviewed.size(), 1, 5000);
        QVERIFY(reviewed.first().first().toBool()); QCOMPARE(review.history().size(), 1);
        const auto firstHistory = review.history().first().toMap();
        QCOMPARE(firstHistory.value("targets").toList().size(), 2);
        QCOMPARE(firstHistory.value("text").toString(), QString("ocr"));
        QVERIFY(firstHistory.value("guarded").toBool());
        QSqlQuery q(db.connection());
        q.prepare("SELECT uuid FROM qsl_ocr_acquisition WHERE id=?"); q.addBindValue(first.value("acquisitionId")); QVERIFY(q.exec()); QVERIFY(q.next());
        const QString firstUuid = q.value(0).toString(); q.finish();
        QVERIFY(!QslOcrAcquisition::undoExecute(db.path(), first.value("acquisitionId").toLongLong(), firstUuid, &cancel, QStringLiteral("different-log"), db.openFileIdentity()).value("ok").toBool());
        auto undone = QslOcrAcquisition::undoExecute(db.path(), first.value("acquisitionId").toLongLong(), firstUuid, &cancel, db.identity(), db.openFileIdentity());
        QVERIFY2(undone.value("ok").toBool(), qPrintable(undone.value("error").toString()));
        QCOMPARE(undone.value("removedLinks").toInt(), 1);
        QCOMPARE(db.qslAttachments(idAt(0)).size(), 1); QVERIFY(db.qslAttachments(idAt(1)).isEmpty());
        QCOMPARE(db.qslAttachmentData(attachment), bytes);
        QVERIFY(review.review(idAt(0), 0)); QTRY_COMPARE_WITH_TIMEOUT(reviewed.size(), 2, 5000);
        QVERIFY(!review.history().first().toMap().value("undoneAt").isNull());
        QVERIFY(!QslOcrAcquisition::undoExecute(db.path(), first.value("acquisitionId").toLongLong(), firstUuid, &cancel).value("ok").toBool());
        const auto second = QslOcrAcquisition::execute(db.path(), image, hash, "card.png", "ocr", {selected.at(1), selected.at(2)}, &cancel);
        QVERIFY(second.value("ok").toBool());
        q.prepare("SELECT uuid FROM qsl_ocr_acquisition WHERE id=?"); q.addBindValue(second.value("acquisitionId")); QVERIFY(q.exec()); QVERIFY(q.next());
        const QString secondUuid = q.value(0).toString(); q.finish();
        // A later ordinary attachment action reasserts the same link.
        {
            LogDatabase other; QVERIFY(other.open(db.path()));
            QCOMPARE(other.addQslAttachment(bytes, "card.png", "image/png", {idAt(1)}), attachment);
        }
        QVERIFY(!QslOcrAcquisition::undoExecute(db.path(), second.value("acquisitionId").toLongLong(), secondUuid, &cancel).value("ok").toBool());
        QVERIFY(review.review(idAt(1), 0)); QTRY_COMPARE_WITH_TIMEOUT(reviewed.size(), 3, 5000);
        QVERIFY(!review.history().first().toMap().value("guarded").toBool());
        QCOMPARE(db.qslAttachments(idAt(1)).size(), 1); QCOMPARE(db.qslAttachments(idAt(2)).size(), 1);
        QVERIFY(q.exec("UPDATE qso SET revision=revision+1 WHERE id=" + QString::number(idAt(2))));
        QVERIFY(!QslOcrAcquisition::undoExecute(db.path(), second.value("acquisitionId").toLongLong(), secondUuid, &cancel).value("ok").toBool());
        QCOMPARE(db.qslAttachments(idAt(2)).size(), 1);
        cancel.store(true);
        QVERIFY(!QslOcrAcquisition::undoExecute(db.path(), second.value("acquisitionId").toLongLong(), secondUuid, &cancel).value("ok").toBool());
        QCOMPARE(db.qsoCount(), 3);
    }
    void guardedUndoRollsBackAndRefusesLegacyLinks()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString image = makeImage(dir, "card.png", false); QVERIFY(!image.isEmpty());
        QFile file(image); QVERIFY(file.open(QIODevice::ReadOnly)); const auto bytes = file.readAll();
        const QString hash = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
        QVariantList selected;
        for (const QString &call : {"K1AAA", "K1BBB"}) {
            AdifRecord r{{"CALL", call}, {"QSO_DATE", "20260923"}, {"TIME_ON", "143000"}, {"BAND", "20m"}, {"MODE", "CW"}};
            const auto ins = db.insertQso(r, "manual", {}, true, 0, false); QCOMPARE(ins.status, InsertResult::Status::Inserted);
            QSqlQuery q(db.connection()); q.prepare("SELECT uuid,revision FROM qso WHERE id=?"); q.addBindValue(ins.id); QVERIFY(q.exec()); QVERIFY(q.next());
            selected << QVariantMap{{"id", ins.id}, {"uuid", q.value(0)}, {"revision", q.value(1)}};
        }
        std::atomic_bool cancelled{false};
        const auto acquisition = QslOcrAcquisition::execute(db.path(), image, hash, "card.png", "ocr", selected, &cancelled);
        QVERIFY(acquisition.value("ok").toBool());
        const qint64 acquisitionId = acquisition.value("acquisitionId").toLongLong();
        QSqlQuery q(db.connection()); q.prepare("SELECT uuid FROM qsl_ocr_acquisition WHERE id=?"); q.addBindValue(acquisitionId); QVERIFY(q.exec()); QVERIFY(q.next());
        const QString uuid = q.value(0).toString(); q.finish();
        const qint64 first = selected.at(0).toMap().value("id").toLongLong(), second = selected.at(1).toMap().value("id").toLongLong();
        QVERIFY(q.exec("CREATE TRIGGER reject_undo BEFORE DELETE ON qsl_attachment_qso WHEN OLD.qso_id=" + QString::number(second) + " BEGIN SELECT RAISE(ABORT,'reject'); END"));
        QVERIFY(!QslOcrAcquisition::undoExecute(db.path(), acquisitionId, uuid, &cancelled).value("ok").toBool());
        QCOMPARE(db.qslAttachments(first).size(), 1); QCOMPARE(db.qslAttachments(second).size(), 1);
        QVERIFY(q.exec("DROP TRIGGER reject_undo"));
        QVERIFY(q.exec("UPDATE qsl_ocr_acquisition_qso SET link_token=NULL WHERE qso_id=" + QString::number(first)));
        QVERIFY(!QslOcrAcquisition::undoExecute(db.path(), acquisitionId, uuid, &cancelled).value("ok").toBool());
        QCOMPARE(db.qslAttachments(first).size(), 1); QCOMPARE(db.qslAttachments(second).size(), 1);
    }
    void acquisitionHistoryIsIndexedAndPaged()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString image = makeImage(dir, "card.png", false); QVERIFY(!image.isEmpty());
        QFile file(image); QVERIFY(file.open(QIODevice::ReadOnly)); const auto bytes = file.readAll();
        const QString hash = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
        AdifRecord r{{"CALL", "K1AAA"}, {"QSO_DATE", "20260923"}, {"TIME_ON", "143000"}, {"BAND", "20m"}, {"MODE", "CW"}};
        const auto ins = db.insertQso(r, "manual", {}, true, 0, false); QCOMPARE(ins.status, InsertResult::Status::Inserted);
        QSqlQuery q(db.connection()); q.prepare("SELECT uuid,revision FROM qso WHERE id=?"); q.addBindValue(ins.id); QVERIFY(q.exec()); QVERIFY(q.next());
        const QVariantList selected{QVariantMap{{"id", ins.id}, {"uuid", q.value(0)}, {"revision", q.value(1)}}}; q.finish();
        QVERIFY(q.exec("EXPLAIN QUERY PLAN SELECT acquisition_id FROM qsl_ocr_acquisition_qso INDEXED BY idx_qsl_ocr_acquisition_qso "
                       "WHERE qso_uuid=(SELECT uuid FROM qso WHERE id=1) AND qso_id=1 AND acquisition_id<9999 ORDER BY acquisition_id DESC LIMIT 51"));
        bool indexed = false; while (q.next()) indexed |= q.value(3).toString().contains("idx_qsl_ocr_acquisition_qso"); QVERIFY(indexed);
        std::atomic_bool cancelled{false};
        for (int i = 0; i < 52; ++i)
            QVERIFY(QslOcrAcquisition::execute(db.path(), image, hash, "card.png", "ocr", selected, &cancelled).value("ok").toBool());
        TesseractOcr ocr; QslOcrAcquisition history(&db, &ocr); QSignalSpy done(&history, &QslOcrAcquisition::finished);
        QVERIFY(history.review(ins.id, 0)); QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 5000);
        QCOMPARE(history.history().size(), 51);
        const qint64 anchor = history.history().at(49).toMap().value("id").toLongLong();
        QVERIFY(history.review(ins.id, anchor)); QTRY_COMPARE_WITH_TIMEOUT(done.size(), 2, 5000);
        QCOMPARE(history.history().size(), 2);
        QVERIFY(history.history().first().toMap().value("id").toLongLong() < anchor);
        QCOMPARE(db.qsoCount(), 1);
    }
    void guardedUndoDetectsEditedQsoAndLaterOcrReuse()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString image = makeImage(dir, "card.png", false); QVERIFY(!image.isEmpty());
        QFile f(image); QVERIFY(f.open(QIODevice::ReadOnly)); const auto bytes = f.readAll();
        const QString hash = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        LogDatabase db; QVERIFY(db.open(dir.filePath("log.sqlite")));
        QVariantList rows;
        for (const QString &call : {"K1AAA", "K1BBB"}) {
            AdifRecord r{{"CALL", call}, {"QSO_DATE", "20260923"}, {"TIME_ON", "143000"}, {"BAND", "20m"}, {"MODE", "CW"}};
            const auto ins = db.insertQso(r, "manual", {}, true, 0, false); QCOMPARE(ins.status, InsertResult::Status::Inserted);
            QSqlQuery q(db.connection()); q.prepare("SELECT uuid,revision FROM qso WHERE id=?"); q.addBindValue(ins.id); QVERIFY(q.exec()); QVERIFY(q.next());
            rows << QVariantMap{{"id", ins.id}, {"uuid", q.value(0)}, {"revision", q.value(1)}};
        }
        std::atomic_bool cancel{false};
        const auto first = QslOcrAcquisition::execute(db.path(), image, hash, "card.png", "first", {rows.at(0)}, &cancel);
        QVERIFY(first.value("ok").toBool());
        const auto second = QslOcrAcquisition::execute(db.path(), image, hash, "card.png", "second", {rows.at(0)}, &cancel);
        QVERIFY(second.value("ok").toBool()); QCOMPARE(second.value("newLinks").toInt(), 0);
        const auto third = QslOcrAcquisition::execute(db.path(), image, hash, "card.png", "third", {rows.at(1)}, &cancel);
        QVERIFY(third.value("ok").toBool()); QCOMPARE(third.value("newLinks").toInt(), 1);
        QSqlQuery q(db.connection());
        auto findUuid = [&](qint64 id) { QSqlQuery get(db.connection()); get.prepare("SELECT uuid FROM qsl_ocr_acquisition WHERE id=?"); get.addBindValue(id); return get.exec() && get.next() ? get.value(0).toString() : QString(); };
        const qint64 firstId = first.value("acquisitionId").toLongLong(), thirdId = third.value("acquisitionId").toLongLong();
        QVERIFY(!QslOcrAcquisition::undoExecute(db.path(), firstId, findUuid(firstId), &cancel).value("ok").toBool());
        const qint64 editedId = rows.at(1).toMap().value("id").toLongLong();
        QVERIFY(q.exec("UPDATE qso SET revision=revision+1 WHERE id=" + QString::number(editedId)));
        QVERIFY(!QslOcrAcquisition::undoExecute(db.path(), thirdId, findUuid(thirdId), &cancel).value("ok").toBool());
        QCOMPARE(db.qslAttachments(rows.at(0).toMap().value("id").toLongLong()).size(), 1);
        QCOMPARE(db.qslAttachments(editedId).size(), 1);
    }
    void readsTwoLocalLayoutsWithoutModifyingImages()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        TesseractOcr ocr;
        if (ocr.executable().isEmpty()) QSKIP("Tesseract is not installed");
        for (bool alternate : {false, true}) {
            const QString path = makeImage(dir, alternate ? "vertical.png" : "horizontal.png", alternate);
            QVERIFY(!path.isEmpty());
            QFile source(path); QVERIFY(source.open(QIODevice::ReadOnly));
            const auto before = source.readAll(); source.close();
            QSignalSpy done(&ocr, &TesseractOcr::finished);
            QVERIFY(ocr.start(QUrl::fromLocalFile(path)));
            QVERIFY(ocr.busy());
            QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 30000);
            QVERIFY2(done.first().first().toBool(), qPrintable(ocr.error()));
            QVERIFY2(ocr.text().contains("K1ABC"), qPrintable(ocr.text()));
            QVERIFY2(ocr.text().contains("2026"), qPrintable(ocr.text()));
            QVERIFY(!ocr.words().isEmpty());
            QVERIFY(ocr.words().first().toMap().contains("uncertain"));
            const auto calls = ocr.suggestions().value("calls").toList();
            bool found = false;
            for (const auto &candidate : calls) found |= candidate.toMap().value("value").toString() == QLatin1String("K1ABC");
            QVERIFY(found);
            QVERIFY(source.open(QIODevice::ReadOnly)); QCOMPARE(source.readAll(), before);
        }
    }
#ifndef Q_OS_WIN
    void engineReceivesSnapshotPath()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString image = makeImage(dir, "card.png", false); QVERIFY(!image.isEmpty());
        QFile script(dir.filePath("path-ocr")); QVERIFY(script.open(QIODevice::WriteOnly));
        script.write("#!/bin/sh\nprintf '5\\t1\\t1\\t1\\t1\\t1\\t0\\t0\\t10\\t10\\t91\\t%s\\n' \"$1\"\n");
        script.close(); QVERIFY(script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        TesseractOcr ocr(script.fileName()); QSignalSpy done(&ocr, &TesseractOcr::finished);
        QVERIFY(ocr.start(QUrl::fromLocalFile(image)));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 5000);
        QVERIFY(done.first().first().toBool());
        QCOMPARE(ocr.text(), ocr.snapshotPath());
        QVERIFY(ocr.snapshotPath() != image);
    }
    void marksLowConfidenceWordsAndPreservesLines()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString image = makeImage(dir, "card.png", false); QVERIFY(!image.isEmpty());
        QFile script(dir.filePath("fixture-ocr")); QVERIFY(script.open(QIODevice::WriteOnly));
        script.write("#!/bin/sh\nprintf '5\\t1\\t1\\t1\\t1\\t1\\t0\\t0\\t10\\t10\\t91\\tK1ABC\\n5\\t1\\t1\\t1\\t1\\t2\\t0\\t0\\t10\\t10\\t54\\t1430\\n5\\t1\\t1\\t1\\t2\\t1\\t0\\t0\\t10\\t10\\t-1\\t20M\\n'\n");
        script.close(); QVERIFY(script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        TesseractOcr ocr(script.fileName()); QSignalSpy done(&ocr, &TesseractOcr::finished);
        QVERIFY(ocr.start(QUrl::fromLocalFile(image)));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 5000);
        QVERIFY(done.first().first().toBool());
        QCOMPARE(ocr.text(), QString("K1ABC 1430\n20M"));
        QCOMPARE(ocr.words().size(), 3);
        QVERIFY(!ocr.words().at(0).toMap().value("uncertain").toBool());
        QVERIFY(ocr.words().at(1).toMap().value("uncertain").toBool());
        QVERIFY(ocr.words().at(2).toMap().value("uncertain").toBool());
    }
    void cancellationKillsRunningEngine()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString image = makeImage(dir, "card.png", false); QVERIFY(!image.isEmpty());
        QFile script(dir.filePath("slow-ocr")); QVERIFY(script.open(QIODevice::WriteOnly));
        const QString marker = dir.filePath("engine-started");
        script.write("#!/bin/sh\ntouch \"" + marker.toUtf8() + "\"\nexec sleep 30\n"); script.close();
        QVERIFY(script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        TesseractOcr ocr(script.fileName()); QSignalSpy done(&ocr, &TesseractOcr::finished);
        QVERIFY(ocr.start(QUrl::fromLocalFile(image)));
        QVERIFY(!ocr.start(QUrl::fromLocalFile(image)));
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(marker), 5000);
        ocr.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 5000);
        QVERIFY(!done.first().first().toBool());
        QVERIFY(!ocr.busy()); QVERIFY(ocr.text().isEmpty());
        QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Truncate));
        script.write("#!/bin/sh\nprintf '5\\t1\\t1\\t1\\t1\\t1\\t0\\t0\\t10\\t10\\t91\\tK1ABC\\n'\n");
        script.close();
        QVERIFY(ocr.start(QUrl::fromLocalFile(image)));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 2, 5000);
        QVERIFY(done.at(1).first().toBool());
        QCOMPARE(ocr.text(), QString("K1ABC"));
    }
#endif
};
QTEST_MAIN(TestTesseractOcr)
#include "tst_tesseractocr.moc"
