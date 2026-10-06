// Il pulsante Stop deve fermare la radio che sta realmente trasmettendo.
#include "app/RigController.h"

#include <QSettings>
#include <QTest>

using namespace decolog;

namespace {

class FakeRig final : public core::RigLink {
public:
    bool connected() const override { return true; }
    qint64 frequencyHz() const override { return 0; }
    QString mode() const override { return QStringLiteral("CW"); }
    int speedWpm() const override { return speed; }
    QString status() const override { return {}; }
    void disconnectFromRig() override {}
    void refresh() override {}
    void setFrequency(qint64) override {}
    void setMode(const QString&) override {}
    void setPtt(bool on) override
    {
        pttStates << on;
        calls << (on ? QStringLiteral("ptt-on") : QStringLiteral("ptt-off"));
    }
    void setSpeedWpm(int value) override { speed = value; }
    void sendMorse(const QString& value) override { sent << value; }
    void stopMorse() override { ++stops; calls << QStringLiteral("stop"); }

    int speed{0};
    int stops{0};
    QStringList sent;
    QList<bool> pttStates;
    QStringList calls;
};

} // namespace

class TestRigController : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("DecoDXLogTest"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_rigcontroller"));
        QSettings().clear();
    }

    void stopTargetsTheRadioThatReceivedTheMacro()
    {
        FakeRig radio2;
        app::RigController::Context context;
        context.alternateRig = [&radio2]() { return &radio2; };
        app::RigController controller(context);

        controller.sendText(QStringLiteral("CQ TEST"), {});
        QCOMPARE(radio2.sent, QStringList({QStringLiteral("CQ TEST")}));

        controller.stop();
        QCOMPARE(radio2.stops, 1);
        QCOMPARE(radio2.pttStates, QList<bool>({false}));
        QCOMPARE(radio2.calls, QStringList({QStringLiteral("ptt-off"), QStringLiteral("stop")}));
    }

    void everyMacroKeepsItsOwnIndexAndStopClearsIt()
    {
        FakeRig radio2;
        app::RigController::Context context;
        context.alternateRig = [&radio2]() { return &radio2; };
        app::RigController controller(context);
        controller.setMacro(0, QStringLiteral("F1 CQ"), QStringLiteral("CQ"));
        controller.setMacro(1, QStringLiteral("F2 Call"), QStringLiteral("{CALL}"));

        controller.sendMacro(1, {{QStringLiteral("call"), QStringLiteral("IU8LMC")}});
        QCOMPARE(radio2.sent, QStringList({QStringLiteral("IU8LMC")}));
        QCOMPARE(controller.activeMacroIndex(), 1);

        controller.stop();
        QCOMPARE(controller.activeMacroIndex(), -1);
        QCOMPARE(radio2.stops, 1);
        QCOMPARE(radio2.pttStates, QList<bool>({false}));
        QCOMPARE(radio2.calls, QStringList({QStringLiteral("ptt-off"), QStringLiteral("stop")}));
    }

    void oneCharacterMacroKeepsItsText()
    {
        FakeRig radio;
        app::RigController::Context context;
        context.alternateRig = [&radio]() { return &radio; };
        app::RigController controller(context);
        controller.setMacro(4, QStringLiteral("F5 ?"), QStringLiteral("?"));
        controller.setMacro(5, QStringLiteral("F6 K"), QStringLiteral("K"));

        controller.sendMacro(4, {});
        controller.sendMacro(5, {});

        QCOMPARE(radio.sent, QStringList({QStringLiteral("?"), QStringLiteral("K")}));
    }

    void serialYaesuCwDoesNotOverwriteKeyerMemoryOne()
    {
        QStringList activity;
        app::RigController::Context context;
        context.activity = [&activity](const QString&, const QString& text, const QString&) {
            activity << text;
        };
        app::RigController controller(context);
        controller.setLink(QStringLiteral("serial"));
        controller.setRigModel(1001); // Hamlib: Yaesu FT-847
        controller.setMacro(0, QStringLiteral("F1 CQ"), QStringLiteral("UP UP"));

        QVERIFY(controller.cwMemoryProtected());
        QVERIFY(!controller.canKeyCw());
        controller.sendMacro(0, {});

        QCOMPARE(controller.activeMacroIndex(), -1);
        QCOMPARE(activity.size(), 1);
        QVERIFY(activity.constFirst().contains(QStringLiteral("memory 1")));
        QVERIFY(activity.constFirst().contains(QStringLiteral("not sent")));
    }

    void decoderToneAndSpeedLocksAreSavedAndAcceptAuto()
    {
        app::RigController::Context context;
        app::RigController controller(context);
        QCOMPARE(controller.decoderToneLock(), 0);
        QCOMPARE(controller.decoderSpeedLock(), 0);

        controller.setDecoderToneLock(625);
        controller.setDecoderSpeedLock(18);
        QCOMPARE(controller.decoderToneLock(), 625);
        QCOMPARE(controller.decoderSpeedLock(), 18);

        app::RigController restored(context);
        QCOMPARE(restored.decoderToneLock(), 625);
        QCOMPARE(restored.decoderSpeedLock(), 18);

        restored.setDecoderToneLock(0);
        restored.setDecoderSpeedLock(0);
        QCOMPARE(restored.decoderToneLock(), 0);
        QCOMPARE(restored.decoderSpeedLock(), 0);
    }
};

QTEST_GUILESS_MAIN(TestRigController)
#include "tst_rigcontroller.moc"
