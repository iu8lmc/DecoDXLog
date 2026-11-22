#include "core/TesseractOcr.h"
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QSignalSpy>
#include <QTemporaryDir>
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
            QVERIFY(source.open(QIODevice::ReadOnly)); QCOMPARE(source.readAll(), before);
        }
    }
#ifndef Q_OS_WIN
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
        script.write("#!/bin/sh\nexec sleep 30\n"); script.close();
        QVERIFY(script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        TesseractOcr ocr(script.fileName()); QSignalSpy done(&ocr, &TesseractOcr::finished);
        QVERIFY(ocr.start(QUrl::fromLocalFile(image)));
        QVERIFY(!ocr.start(QUrl::fromLocalFile(image)));
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
