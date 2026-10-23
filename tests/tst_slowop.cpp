// Il controllo dei blocchi sa cosa stava facendo il programma: le operazioni lunghe del filo
// della finestra si ricordano (nome e durata), quelle brevi no.
#include "../src/StartupTrace.h"

#include <QTest>
#include <QThread>

class TestSlowOp : public QObject {
    Q_OBJECT

private slots:
    void aShortOperationIsNotRemembered()
    {
        decolog::lastSlowOp().name.store(nullptr);
        decolog::lastSlowOp().durationMs.store(0);
        {
            decolog::StartupSpan span("short thing");
            QThread::msleep(20);
        }
        QVERIFY(decolog::lastSlowOp().name.load() == nullptr);
    }

    void aLongOperationIsRemembered()
    {
        decolog::lastSlowOp().name.store(nullptr);
        decolog::lastSlowOp().durationMs.store(0);
        const long long before = decolog::steadyNowMs();
        {
            decolog::StartupSpan span("long thing");
            QThread::msleep(380);
        }
        const char* name = decolog::lastSlowOp().name.load();
        QVERIFY(name != nullptr);
        QCOMPARE(QString::fromLatin1(name), QStringLiteral("long thing"));
        QVERIFY(decolog::lastSlowOp().durationMs.load() >= 300);
        QVERIFY(decolog::lastSlowOp().endMs.load() >= before);
    }

    void theLongestOneStays()
    {
        decolog::lastSlowOp().name.store(nullptr);
        decolog::lastSlowOp().durationMs.store(0);
        decolog::lastSlowOp().endMs.store(0);
        {
            decolog::StartupSpan span("the long one");
            QThread::msleep(800);
        }
        {
            decolog::StartupSpan span("a shorter one");
            QThread::msleep(350);
        }
        QCOMPARE(QString::fromLatin1(decolog::lastSlowOp().name.load()), QStringLiteral("the long one"));
    }
};

QTEST_GUILESS_MAIN(TestSlowOp)
#include "tst_slowop.moc"
