// Il pulsante Stop deve fermare la radio che sta realmente trasmettendo.
#include "app/RigController.h"

#include "core/QslUpload.h"

#include <QElapsedTimer>
#include <QSettings>
#include <QSignalSpy>
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
    }

    void init()
    {
        // Never inherit an enabled serial rig/keyer from a preceding test.
        QSettings().clear();
    }

    // L'elenco delle radio di Hamlib arriva da un processo a parte: chiederlo non ferma la
    // finestra (prima si aspettava rigctld fino a 8 secondi nel filo della UI).
    void rigModelsDoNotBlockTheWindow()
    {
        if (core::qsl::findRigctld().isEmpty())
            QSKIP("Hamlib (rigctld) is not installed here");
        app::RigController controller{app::RigController::Context{}};
        QSignalSpy arrived(&controller, &app::RigController::rigModelsChanged);

        QElapsedTimer clock;
        clock.start();
        const QVariantList first = controller.rigModels();
        QVERIFY2(clock.elapsed() < 150, "rigModels() waited for rigctld");
        QVERIFY(first.isEmpty());

        QVERIFY(arrived.wait(20000));
        const QVariantList models = controller.rigModels();
        QVERIFY(models.size() > 100);
        // Il dummy di Hamlib c'e' sempre, con il numero 1.
        bool dummy = false;
        for (const QVariant& m : models)
            dummy = dummy || m.toMap().value(QStringLiteral("id")).toInt() == 1;
        QVERIFY(dummy);
    }

    void serialCapabilitiesAreAskedWithoutWaiting()
    {
        if (core::qsl::findRigctld().isEmpty())
            QSKIP("Hamlib (rigctld) is not installed here");
        app::RigController controller{app::RigController::Context{}};
        QSignalSpy arrived(&controller, &app::RigController::serialCapabilitiesChanged);

        QElapsedTimer clock;
        clock.start();
        controller.setRigModel(1);   // il dummy
        QVERIFY2(clock.elapsed() < 150, "setRigModel() waited for rigctld -L");
        // Per il dummy `rigctld -L` dice qualcosa sulla seriale: o almeno non si pianta.
        arrived.wait(8000);
        QVERIFY(clock.elapsed() < 9000);
    }

    // Cambiando la porta (o la velocita', il modello...) il rigctld in piedi ha ancora i
    // parametri di prima: nella 1.17.21 la nuova porta non veniva presa se non riavviando il
    // programma. Con il dummy di Hamlib si vede dal TCP di rigctld, che e' nuovo a ogni avvio.
    void changingTheSerialPortRestartsTheLocalRigctld()
    {
        if (core::qsl::findRigctld().isEmpty())
            QSKIP("Hamlib (rigctld) is not installed here");
        QSettings().clear();
        app::RigController controller{app::RigController::Context{}};
        controller.setLink(QStringLiteral("serial"));
        controller.setRigModel(1);   // il dummy: ignora la porta
        controller.setSerialPort(QStringLiteral("dummy-a"));
        controller.setEnabled(true);
        QTRY_VERIFY_WITH_TIMEOUT(controller.connected(), 20000);
        const int first = controller.port();
        QVERIFY(first > 0);

        controller.setSerialPort(QStringLiteral("dummy-b"));
        QTRY_VERIFY_WITH_TIMEOUT(controller.port() != first && controller.connected(), 20000);
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

    void serialYaesuCwIsAllowedAndWarnsAboutMemoryOne()
    {
        QStringList activity;
        app::RigController::Context context;
        context.activity = [&activity](const QString&, const QString& text, const QString&) {
            activity << text;
        };
        app::RigController controller(context);
        controller.setLink(QStringLiteral("serial"));
        controller.setRigModel(1037); // Hamlib: Yaesu FTDX-3000
        controller.setMacro(0, QStringLiteral("F1 CQ"), QStringLiteral("UP UP"));

        QVERIFY(controller.cwUsesKeyerMemory());
        QVERIFY(controller.canKeyCw());
        QVERIFY(!controller.connected());
        controller.sendMacro(0, {});

        // The request now reaches RigControl. No radio is attached in this
        // test: the error must be connectivity, not a memory-protection veto.
        QCOMPARE(activity.size(), 1);
        QVERIFY(activity.constFirst().contains(QStringLiteral("not connected")));
        QVERIFY(!activity.constFirst().contains(QStringLiteral("memory 1")));
    }

    void yaesuMemoryWarningFollowsTheActualCwRoute()
    {
        FakeRig radio2;
        core::RigLink* activeAlternate = nullptr;
        app::RigController::Context context;
        context.alternateRig = [&activeAlternate] { return activeAlternate; };
        app::RigController controller(context);
        controller.setRigModel(1037);
        controller.setLink(QStringLiteral("serial"));
        QVERIFY(controller.cwUsesKeyerMemory());

        activeAlternate = &radio2;
        QVERIFY(!controller.cwUsesKeyerMemory());
        controller.sendText(QStringLiteral("?"), {});
        QCOMPARE(radio2.sent, QStringList({QStringLiteral("?")}));

        activeAlternate = nullptr;
        controller.setLink(QStringLiteral("network"));
        QVERIFY(!controller.cwUsesKeyerMemory()); // external rig model is unknown
        QVERIFY(controller.canKeyCw());
        controller.setLink(QStringLiteral("serial"));
        controller.setRigModel(1); // non-Yaesu dummy
        QVERIFY(!controller.cwUsesKeyerMemory());
        QVERIFY(controller.canKeyCw());
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
