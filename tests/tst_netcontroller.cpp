#include "app/NetController.h"

#include <QTest>

class TestNetController : public QObject {
    Q_OBJECT
private slots:
    void queuedCallsKeepOrderAndRejectDuplicates()
    {
        decolog::app::NetController net({});
        QVERIFY(net.queueCall(" k1abc ").isEmpty());
        QVERIFY(net.queueCall("DL1XYZ/P").isEmpty());
        QVERIFY(!net.queueCall("K1ABC").isEmpty());
        QVERIFY(!net.queueCall("bad call").isEmpty());
        QCOMPARE(net.queuedCalls(),QStringList({"K1ABC","DL1XYZ/P"}));
        QVERIFY(net.moveQueuedCall(1,-1));
        QCOMPARE(net.takeQueuedCall(),QString("DL1XYZ/P"));
        QCOMPARE(net.queuedCalls(),QStringList({"K1ABC"}));
        QVERIFY(net.removeQueuedCall(0)); QVERIFY(net.takeQueuedCall().isEmpty());
    }
    void sessionMakesSequenceRestartSafe()
    {
        QCOMPARE(decolog::app::NetController::frameKey(QStringLiteral("session-a"), 1),
                 QStringLiteral("session-a:1"));
        QVERIFY(decolog::app::NetController::frameKey(QStringLiteral("session-a"), 1)
                != decolog::app::NetController::frameKey(QStringLiteral("session-b"), 1));
        QVERIFY(decolog::app::NetController::frameKey(QStringLiteral("legacy"), 1)
                != decolog::app::NetController::frameKey(QStringLiteral("session-a"), 1));
    }
};

QTEST_MAIN(TestNetController)
#include "tst_netcontroller.moc"
