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
        const auto saved = QslOcrAcquisition::execute(dbPath, image, hash, "card.png", "K1AAA K1BBB", selection, &cancelled);
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
