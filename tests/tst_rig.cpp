// La radio via rigctld: si parla col demone di Hamlib, e qui c'e' un finto
// rigctld che risponde come quello vero (protocollo esteso, "+f" → "get_freq:
// / Frequency: … / RPRT 0"), cosi' la prova non ha bisogno di una radio.
#include "core/RigControl.h"

#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

using namespace decolog::core;

namespace {

class FakeRigctld : public QTcpServer {
public:
    QStringList received;
    QStringList rawReceived; // retain significant trailing spaces in Morse payloads
    qint64 frequency{14074000};
    QString mode{"CW"};
    int keyspd{22};
    bool morseWorks{true};
    // Il codice d'errore di un CW rifiutato: -1 (EINVAL, rifiutato quel
    // testo) o -11 (ENAVAIL, la radio non manipola).
    int morseError{-1};
    bool stopWorks{true};
    // Il ponte CAT di Decodium risponde col valore e basta: niente eco del
    // comando, niente RPRT. DecoDXLog deve capire anche quello.
    bool plainAnswers{false};
    // Split, VFO, RIT e XIT; una radio senza RIT risponde -11.
    bool split{false};
    qint64 txFrequency{0};
    QString vfo{"VFOA"};
    int rit{0};
    int xit{0};
    bool hasRit{true};
    // Il ponte di Decodium: "S" lo accetta e non lo fa; lo split lo accende e
    // lo spegne la frequenza TX ("I"), ma solo se in Decodium lo split non e'
    // "nessuno".
    bool bridgeSplits{false};
    // Decodium in split "Impianto"/"Simula": prende la frequenza TX e poi se
    // la rifa' da se' per l'FT8 (qui: 1 kHz sotto la ricezione).
    bool bridgeOverridesTx{false};

    FakeRigctld()
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* s = nextPendingConnection()) {
                connect(s, &QTcpSocket::readyRead, s, [this, s] {
                    while (s->canReadLine()) {
                        QString raw = QString::fromUtf8(s->readLine());
                        raw.chop(1); // LF only: trailing spaces are part of send_morse
                        rawReceived << raw;
                        const QString line = raw.trimmed();
                        received << line;
                        s->write(answer(raw).toUtf8());
                    }
                });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
        listen(QHostAddress::LocalHost);
    }

    QString answer(const QString& line)
    {
        const QString cmd = line.startsWith(QLatin1Char('+')) ? line.mid(1) : line;
        if (plainAnswers) {
            if (cmd == QLatin1String("f"))
                return QStringLiteral("%1\n").arg(frequency);
            if (cmd == QLatin1String("m"))
                return QStringLiteral("%1\n3000\n").arg(mode);
            if (cmd == QLatin1String("l KEYSPD"))
                return QStringLiteral("%1\n").arg(keyspd);
            if (cmd.startsWith(QLatin1String("b ")))
                return QStringLiteral("RPRT -11\n");
            if (cmd == QLatin1String("s"))
                return QStringLiteral("%1\nVFOB\n").arg(split ? 1 : 0);
            if (cmd == QLatin1String("i"))
                return QStringLiteral("%1\n").arg(txFrequency);
            if (cmd.startsWith(QLatin1String("I ")) && bridgeSplits) {
                txFrequency = cmd.mid(2).toLongLong();
                split = txFrequency > 0;
                if (split && bridgeOverridesTx)
                    txFrequency = frequency - 1000;
            }
            return QStringLiteral("RPRT 0\n");
        }
        if (cmd == QLatin1String("f"))
            return QStringLiteral("get_freq:\nFrequency: %1\nRPRT 0\n").arg(frequency);
        if (cmd == QLatin1String("m"))
            return QStringLiteral("get_mode:\nMode: %1\nPassband: 500\nRPRT 0\n").arg(mode);
        if (cmd == QLatin1String("l KEYSPD"))
            return QStringLiteral("get_level: KEYSPD\n%1\nRPRT 0\n").arg(keyspd);
        if (cmd.startsWith(QLatin1String("F "))) {
            frequency = cmd.mid(2).toLongLong();
            return QStringLiteral("set_freq: %1\nRPRT 0\n").arg(frequency);
        }
        if (cmd.startsWith(QLatin1String("M "))) {
            mode = cmd.section(QLatin1Char(' '), 1, 1);
            return QStringLiteral("set_mode: %1\nRPRT 0\n").arg(mode);
        }
        if (cmd.startsWith(QLatin1String("L KEYSPD "))) {
            keyspd = cmd.section(QLatin1Char(' '), 2, 2).toInt();
            return QStringLiteral("set_level: KEYSPD %1\nRPRT 0\n").arg(keyspd);
        }
        if (cmd == QLatin1String("s"))
            return QStringLiteral("get_split_vfo:\nSplit: %1\nTX VFO: VFOB\nRPRT 0\n").arg(split ? 1 : 0);
        if (cmd.startsWith(QLatin1String("S "))) {
            split = cmd.section(QLatin1Char(' '), 1, 1) == QLatin1String("1");
            return QStringLiteral("set_split_vfo:\nRPRT 0\n");
        }
        if (cmd == QLatin1String("i"))
            return QStringLiteral("get_split_freq:\nTX Frequency: %1\nRPRT 0\n").arg(txFrequency);
        if (cmd.startsWith(QLatin1String("I "))) {
            txFrequency = cmd.mid(2).toLongLong();
            return QStringLiteral("set_split_freq:\nRPRT 0\n");
        }
        if (cmd == QLatin1String("v"))
            return QStringLiteral("get_vfo:\nVFO: %1\nRPRT 0\n").arg(vfo);
        if (cmd.startsWith(QLatin1String("V "))) {
            vfo = cmd.mid(2);
            return QStringLiteral("set_vfo:\nRPRT 0\n");
        }
        if (cmd == QLatin1String("j") || cmd.startsWith(QLatin1String("J "))) {
            if (!hasRit)
                return QStringLiteral("RPRT -11\n");
            if (cmd.startsWith(QLatin1String("J ")))
                rit = cmd.mid(2).toInt();
            return QStringLiteral("get_rit:\nRIT: %1\nRPRT 0\n").arg(rit);
        }
        if (cmd == QLatin1String("z"))
            return QStringLiteral("get_xit:\nXIT: %1\nRPRT 0\n").arg(xit);
        if (cmd.startsWith(QLatin1String("Z "))) {
            xit = cmd.mid(2).toInt();
            return QStringLiteral("set_xit:\nRPRT 0\n");
        }
        if (cmd.startsWith(QLatin1String("b ")))
            return QStringLiteral("send_morse: %1\nRPRT %2\n").arg(cmd.mid(2)).arg(morseWorks ? 0 : morseError);
        if (cmd.contains(QLatin1String("stop_morse")))
            return QStringLiteral("stop_morse:\nRPRT %1\n").arg(stopWorks ? 0 : -11);
        return QStringLiteral("%1:\nRPRT 0\n").arg(cmd);
    }
};

} // namespace

class TestRig : public QObject {
    Q_OBJECT

private slots:
    void readsWhereTheRadioIs()
    {
        FakeRigctld rig;
        RigControl control;
        QSignalSpy changed(&control, &RigControl::changed);
        control.connectTo(QStringLiteral("127.0.0.1"), rig.serverPort());

        QTRY_VERIFY_WITH_TIMEOUT(control.connected(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(control.frequencyHz(), 14074000LL, 5000);
        QCOMPARE(control.mode(), QStringLiteral("CW"));
        QTRY_COMPARE_WITH_TIMEOUT(control.speedWpm(), 22, 5000);
        QVERIFY(changed.size() > 0);

        // Si comanda anche: VFO, modo e velocita' arrivano alla radio.
        control.setFrequency(7025000);
        control.setMode(QStringLiteral("CW"));
        control.setSpeedWpm(28);
        QTRY_VERIFY_WITH_TIMEOUT(rig.received.contains(QStringLiteral("+F 7025000")), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(rig.received.contains(QStringLiteral("+L KEYSPD 28")), 5000);
        QCOMPARE(control.speedWpm(), 28);

        control.disconnectFromRig();
        QVERIFY(!control.connected());
    }

    void splitVfoRitAndXit()
    {
        FakeRigctld rig;
        RigControl control;
        control.connectTo(QStringLiteral("127.0.0.1"), rig.serverPort());
        QTRY_VERIFY_WITH_TIMEOUT(control.connected(), 5000);
        QCOMPARE(control.features(), int(RigLink::Split | RigLink::VfoSelect | RigLink::Rit | RigLink::Xit));

        // Split: in su di 1 kHz, sul VFO B.
        control.setSplit(true, 14075000);
        QTRY_VERIFY_WITH_TIMEOUT(rig.received.contains(QStringLiteral("+S 1 VFOB")), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(rig.received.contains(QStringLiteral("+I 14075000")), 5000);
        QVERIFY(control.split());
        // Quello che dice la radio si legge al giro dopo.
        rig.txFrequency = 14076000;
        QTRY_COMPARE_WITH_TIMEOUT(control.txFrequencyHz(), 14076000LL, 8000);
        control.setSplit(false);
        QTRY_VERIFY_WITH_TIMEOUT(rig.received.contains(QStringLiteral("+S 0 VFOA")), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!control.split(), 8000);

        control.setVfo(QStringLiteral("VFOB"));
        QTRY_VERIFY_WITH_TIMEOUT(rig.received.contains(QStringLiteral("+V VFOB")), 5000);
        QCOMPARE(control.vfo(), QString("VFOB"));

        control.setRit(150);
        control.setXit(-200);
        QTRY_VERIFY_WITH_TIMEOUT(rig.received.contains(QStringLiteral("+J 150")), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(rig.received.contains(QStringLiteral("+Z -200")), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(control.ritHz(), 150, 8000);
        QCOMPARE(control.xitHz(), -200);
    }

    // Col CAT che passa da Decodium e lo split "nessuno" la radio non va in
    // split: la pillola non deve tornare indietro in silenzio.
    void splitThroughDecodiumSaysWhyItDidNotGo()
    {
        FakeRigctld rig;
        rig.plainAnswers = true;
        RigControl control;
        QSignalSpy failed(&control, &RigControl::failed);
        control.connectTo(QStringLiteral("127.0.0.1"), rig.serverPort());
        QTRY_VERIFY_WITH_TIMEOUT(control.connected(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(control.frequencyHz(), 14074000LL, 5000);

        control.setSplit(true, 14075000);
        QTRY_VERIFY_WITH_TIMEOUT(!control.split(), 8000);
        QTRY_VERIFY_WITH_TIMEOUT(failed.count() > 0, 8000);
        QVERIFY(failed.last().first().toString().contains(QStringLiteral("Decodium")));
        QVERIFY(control.features() & RigLink::Split);
    }

    // Con lo split permesso in Decodium va, e si spegne con la frequenza TX a zero.
    void splitThroughDecodiumOnAndOff()
    {
        FakeRigctld rig;
        rig.plainAnswers = true;
        rig.bridgeSplits = true;
        RigControl control;
        QSignalSpy failed(&control, &RigControl::failed);
        control.connectTo(QStringLiteral("127.0.0.1"), rig.serverPort());
        QTRY_VERIFY_WITH_TIMEOUT(control.connected(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(control.frequencyHz(), 14074000LL, 5000);

        control.setSplit(true, 14075000);
        QTRY_VERIFY_WITH_TIMEOUT(rig.split, 5000);
        QTest::qWait(4500);
        QVERIFY(control.split());
        QCOMPARE(failed.count(), 0);

        control.setSplit(false);
        QTRY_VERIFY_WITH_TIMEOUT(rig.received.contains(QStringLiteral("+I 0")), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!rig.split, 5000);
        QTest::qWait(4500);
        QVERIFY(!control.split());
        QCOMPARE(failed.count(), 0);
    }

    // Decodium tiene lo split ma sposta la trasmissione dove vuole lui: si dice.
    void splitThroughDecodiumMovedTxSaysSo()
    {
        FakeRigctld rig;
        rig.plainAnswers = true;
        rig.bridgeSplits = true;
        rig.bridgeOverridesTx = true;
        RigControl control;
        QSignalSpy failed(&control, &RigControl::failed);
        control.connectTo(QStringLiteral("127.0.0.1"), rig.serverPort());
        QTRY_VERIFY_WITH_TIMEOUT(control.connected(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(control.frequencyHz(), 14074000LL, 5000);

        control.setSplit(true, 14075000);
        QTRY_VERIFY_WITH_TIMEOUT(failed.count() > 0, 8000);
        const QString message = failed.last().first().toString();
        QVERIFY(message.contains(QStringLiteral("Decodium")));
        QVERIFY(message.contains(QStringLiteral("14.0730")));
        QCOMPARE(control.txFrequencyHz(), 14073000LL);
    }

    void aRadioWithoutRitStopsBeingAsked()
    {
        FakeRigctld rig;
        rig.hasRit = false;
        RigControl control;
        control.connectTo(QStringLiteral("127.0.0.1"), rig.serverPort());
        QTRY_VERIFY_WITH_TIMEOUT(control.connected(), 5000);
        // Al primo "j" risponde -11: il RIT sparisce, il resto resta.
        QTRY_VERIFY_WITH_TIMEOUT(!(control.features() & RigLink::Rit), 8000);
        QVERIFY(control.features() & RigLink::Split);
        QSignalSpy failed(&control, &RigControl::failed);
        control.setRit(100);
        QTRY_VERIFY_WITH_TIMEOUT(failed.count() > 0, 5000);
    }

    void keysTheTextInCw()
    {
        FakeRigctld rig;
        RigControl control;
        control.connectTo(QStringLiteral("127.0.0.1"), rig.serverPort());
        QTRY_VERIFY_WITH_TIMEOUT(control.connected(), 5000);

        QSignalSpy sent(&control, &RigControl::morseSent);
        control.sendMorse(QStringLiteral("CQ TEST IU8LMC"));
        QVERIFY(sent.wait(5000));
        QCOMPARE(sent.first().at(0).toString(), QStringLiteral("CQ TEST IU8LMC"));
        QVERIFY(rig.received.contains(QStringLiteral("+b CQ TEST IU8LMC")));

        control.stopMorse();
        QTRY_VERIFY_WITH_TIMEOUT(rig.received.contains(QStringLiteral("+\\stop_morse")), 5000);
    }

    void queuesShortCwMessagesInsteadOfOverwritingTheRadioKeyer()
    {
        FakeRigctld rig;
        RigControl control;
        control.connectTo(QStringLiteral("127.0.0.1"), rig.serverPort());
        QTRY_VERIFY_WITH_TIMEOUT(control.connected(), 5000);
        control.setSpeedWpm(60);

        QSignalSpy sent(&control, &RigControl::morseSent);
        control.sendMorse(QStringLiteral("?"));
        control.sendMorse(QStringLiteral("K"));

        QTRY_VERIFY_WITH_TIMEOUT(rig.received.contains(QStringLiteral("+b ?")), 5000);
        // Il secondo testo e' in coda finche' la radio non ha finito di
        // battere il primo. Senza questa attesa alcuni keyer CAT sovrascrivono
        // la memoria e un singolo '?' sembra semplicemente sparire.
        QTest::qWait(100);
        QVERIFY(!rig.received.contains(QStringLiteral("+b K")));
        QTRY_VERIFY_WITH_TIMEOUT(rig.received.contains(QStringLiteral("+b K")), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(sent.count(), 2, 3000);
        QCOMPARE(sent.at(0).at(0).toString(), QStringLiteral("?"));
        QCOMPARE(sent.at(1).at(0).toString(), QStringLiteral("K"));
    }

    void yaesuSingleCharactersAreLiteralText_data()
    {
        QTest::addColumn<QString>("text");
        QTest::newRow("question-mark") << QStringLiteral("?");
        QTest::newRow("letter-K") << QStringLiteral("K");
        QTest::newRow("digit-1-not-memory-1") << QStringLiteral("1");
        QTest::newRow("digit-5-not-memory-5") << QStringLiteral("5");
    }

    void yaesuSingleCharactersAreLiteralText()
    {
        QFETCH(QString, text);
        FakeRigctld rig;
        RigControl control;
        control.setYaesuKeyerMemoryMode(true);
        control.connectTo(QStringLiteral("127.0.0.1"), rig.serverPort());
        QTRY_VERIFY_WITH_TIMEOUT(control.connected(), 5000);
        QSignalSpy sent(&control, &RigControl::morseSent);
        control.sendMorse(text);
        QTRY_COMPARE_WITH_TIMEOUT(sent.count(), 1, 5000);
        QVERIFY(rig.rawReceived.contains(QStringLiteral("+b %1 ").arg(text)));
        QVERIFY(!rig.rawReceived.contains(QStringLiteral("+b %1").arg(text)));
        QCOMPARE(sent.first().at(0).toString(), text);

        // Changing to a different backend must not retain Yaesu formatting.
        control.stopMorse();
        control.setYaesuKeyerMemoryMode(false);
        control.sendMorse(text);
        QTRY_COMPARE_WITH_TIMEOUT(sent.count(), 2, 5000);
        QVERIFY(rig.rawReceived.contains(QStringLiteral("+b %1").arg(text)));
    }

    void longYaesuMacrosAreQueuedWithoutTruncation_data()
    {
        QTest::addColumn<QString>("firstChunk");
        QTest::newRow("split-at-word-boundary") << (QString(49, QLatin1Char('E')) + QLatin1Char(' '));
        QTest::newRow("long-word") << QString(50, QLatin1Char('E'));
    }

    void longYaesuMacrosAreQueuedWithoutTruncation()
    {
        QFETCH(QString, firstChunk);
        FakeRigctld rig;
        rig.keyspd = 60;
        RigControl control;
        control.setYaesuKeyerMemoryMode(true);
        control.connectTo(QStringLiteral("127.0.0.1"), rig.serverPort());
        QTRY_COMPARE_WITH_TIMEOUT(control.speedWpm(), 60, 5000);
        QSignalSpy sent(&control, &RigControl::morseSent);
        control.sendMorse(firstChunk + QStringLiteral("K"));
        QTRY_COMPARE_WITH_TIMEOUT(sent.count(), 1, 5000);
        QCOMPARE(sent.first().at(0).toString(), firstChunk);
        QVERIFY(rig.rawReceived.contains(QStringLiteral("+b ") + firstChunk));
        QTest::qWait(100);
        QVERIFY(!rig.rawReceived.contains(QStringLiteral("+b K ")));
        QTRY_COMPARE_WITH_TIMEOUT(sent.count(), 2, 8000);
        QCOMPARE(sent.at(0).at(0).toString() + sent.at(1).at(0).toString(), firstChunk + QStringLiteral("K"));
        QVERIFY(rig.rawReceived.contains(QStringLiteral("+b K ")));
    }

    void stopDropsCwMessagesStillQueuedForTheRadio_data()
    {
        QTest::addColumn<bool>("yaesu");
        QTest::newRow("generic") << false;
        QTest::newRow("yaesu") << true;
    }

    void stopDropsCwMessagesStillQueuedForTheRadio()
    {
        QFETCH(bool, yaesu);
        FakeRigctld rig;
        RigControl control;
        control.setYaesuKeyerMemoryMode(yaesu);
        control.connectTo(QStringLiteral("127.0.0.1"), rig.serverPort());
        QTRY_VERIFY_WITH_TIMEOUT(control.connected(), 5000);
        control.setSpeedWpm(20);

        control.sendMorse(QStringLiteral("CQ"));
        QTRY_VERIFY_WITH_TIMEOUT(rig.received.contains(QStringLiteral("+b CQ")), 5000);
        control.sendMorse(QStringLiteral("?"));
        control.stopMorse();

        QTRY_VERIFY_WITH_TIMEOUT(rig.received.contains(QStringLiteral("+\\stop_morse")), 5000);
        QTest::qWait(1200);
        QVERIFY(!rig.received.contains(QStringLiteral("+b ?")));
    }

    // Il dekey e' il primo comando di Ferma. Il secondo collegamento urgente
    // puo' duplicare i due comandi, ma non puo' invertire quell'ordine.
    void emergencyStopReleasesPttBeforeStoppingMorse()
    {
        FakeRigctld rig;
        RigControl control;
        control.connectTo(QStringLiteral("127.0.0.1"), rig.serverPort());
        QTRY_VERIFY_WITH_TIMEOUT(control.connected(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(control.speedWpm(), 22, 5000);
        rig.received.clear();

        control.emergencyStop();
        QTRY_VERIFY_WITH_TIMEOUT(rig.received.contains(QStringLiteral("+T 0")), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(rig.received.contains(QStringLiteral("+\\stop_morse")), 5000);
        QVERIFY(rig.received.indexOf(QStringLiteral("+T 0"))
                < rig.received.indexOf(QStringLiteral("+\\stop_morse")));
    }

    void emergencyStopStillWorksAfterNormalSocketDrops()
    {
        FakeRigctld rig;
        RigControl control;
        control.connectTo(QStringLiteral("127.0.0.1"), rig.serverPort());
        QTRY_VERIFY_WITH_TIMEOUT(control.connected(), 5000);
        rig.received.clear();

        // Simulates the CAT socket disappearing while the radio/keyer is still
        // transmitting. The emergency connection must not depend on connected().
        control.disconnectFromRig();
        QVERIFY(!control.connected());
        control.emergencyStop();

        QTRY_VERIFY_WITH_TIMEOUT(rig.received.contains(QStringLiteral("+T 0")), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(rig.received.contains(QStringLiteral("+\\stop_morse")), 5000);
        QVERIFY(rig.received.indexOf(QStringLiteral("+T 0"))
                < rig.received.indexOf(QStringLiteral("+\\stop_morse")));
    }

    void emergencyStopCanForceYaesuCatReceiveWithExternalPtt()
    {
        FakeRigctld rig;
        RigControl control;
        control.setEmergencyCatOffCommand(QStringLiteral("TX0;"));
        control.connectTo(QStringLiteral("127.0.0.1"), rig.serverPort());
        QTRY_VERIFY_WITH_TIMEOUT(control.connected(), 5000);
        rig.received.clear();

        control.emergencyStop();

        QTRY_VERIFY_WITH_TIMEOUT(rig.received.contains(QStringLiteral("+T 0")), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(rig.received.contains(QStringLiteral("+w TX0;")), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(rig.received.contains(QStringLiteral("+\\stop_morse")), 5000);
        const int configuredPttOff = rig.received.indexOf(QStringLiteral("+T 0"));
        const int catPttOff = rig.received.indexOf(QStringLiteral("+w TX0;"));
        // The normal CAT socket also sends an idempotent stop before the
        // emergency socket connects.  The last stop belongs to the complete
        // emergency sequence and must follow its raw Yaesu TX0 command.
        const int stopMorse = rig.received.lastIndexOf(QStringLiteral("+\\stop_morse"));
        QVERIFY(configuredPttOff < catPttOff);
        QVERIFY(catPttOff < stopMorse);
    }

    // rigctld risponde a "get_level" col valore nudo e *poi* con RPRT. Il
    // valore nudo bastava a chiudere la risposta, e il RPRT che arrivava dopo
    // si prendeva la domanda seguente: da li' in poi ogni risposta finiva sulla
    // domanda sbagliata, e l'errore del CW spariva del tutto.
    //
    // Qui si aspetta apposta che la velocita' sia arrivata — cioe' che quella
    // risposta col valore nudo sia stata chiusa — e solo allora si manda il CW.
    void theTailOfOneAnswerIsNotTheAnswerToTheNext()
    {
        FakeRigctld rig;
        rig.morseWorks = false;
        RigControl control;
        control.connectTo(QStringLiteral("127.0.0.1"), rig.serverPort());
        QTRY_VERIFY_WITH_TIMEOUT(control.connected(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(control.speedWpm(), 22, 5000);

        QSignalSpy failed(&control, &RigControl::failed);
        control.sendMorse(QStringLiteral("TEST"));
        QVERIFY2(failed.wait(5000), "la risposta al CW e' stata attribuita a un'altra domanda");
        QVERIFY(failed.first().at(0).toString().contains(QStringLiteral("CW")));

        // E la radio continua a rispondere a tono anche dopo: la coda non e'
        // rimasta sfasata di uno.
        control.setFrequency(7025000);
        QTRY_VERIFY_WITH_TIMEOUT(rig.received.contains(QStringLiteral("+F 7025000")), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(control.frequencyHz(), 7025000LL, 5000);
    }

    void aRadioThatDoesNotKeyCwSaysSo()
    {
        FakeRigctld rig;
        rig.morseWorks = false;
        RigControl control;
        control.connectTo(QStringLiteral("127.0.0.1"), rig.serverPort());
        QTRY_VERIFY_WITH_TIMEOUT(control.connected(), 5000);

        QSignalSpy failed(&control, &RigControl::failed);
        control.sendMorse(QStringLiteral("TEST"));
        QVERIFY(failed.wait(5000));
        QVERIFY(failed.first().at(0).toString().contains(QStringLiteral("CW")));
    }

    // La FTDX10 (e le altre Yaesu con Hamlib): leggeva la frequenza ma il CW
    // non partiva. Il testo arriva in maiuscolo e senza segni strani; un
    // messaggio rifiutato non spegne i tasti per sempre; in USB lo si dice;
    // lo STOP che la radio non sa fare si dice una volta sola.
    void yaesuStyleCw()
    {
        QCOMPARE(RigControl::morseText(QStringLiteral("cq cq de iu8lmc k")), QStringLiteral("CQ CQ DE IU8LMC K"));
        QCOMPARE(RigControl::morseText(QStringLiteral("tu 5nn {nr} ! ok?")), QStringLiteral("TU 5NN NR OK?"));
        QCOMPARE(RigControl::morseText(QStringLiteral("?")), QStringLiteral("?"));

        FakeRigctld rig;
        rig.mode = QStringLiteral("USB");
        RigControl control;
        QSignalSpy failed(&control, &RigControl::failed);
        QSignalSpy unsupported(&control, &RigControl::morseUnsupported);
        control.connectTo(QStringLiteral("127.0.0.1"), rig.serverPort());
        QTRY_VERIFY_WITH_TIMEOUT(control.connected(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(control.mode(), QString("USB"), 5000);

        control.sendMorse(QStringLiteral("cq test iu8lmc"));
        QTRY_VERIFY_WITH_TIMEOUT(rig.received.contains(QStringLiteral("+b CQ TEST IU8LMC")), 5000);
        QVERIFY(failed.size() >= 1);
        QVERIFY(failed.first().first().toString().contains(QStringLiteral("USB")));

        // La macro successiva ora aspetta che il keyer interno sia libero.
        // Qui non stiamo provando la coda (c'e' un test dedicato sopra), ma
        // l'errore del testo rifiutato: interrompiamo quindi quella iniziale.
        control.stopMorse();
        QTRY_VERIFY_WITH_TIMEOUT(rig.received.contains(QStringLiteral("+\\stop_morse")), 5000);

        // Rifiutato quel messaggio (-1): si dice perche', i tasti restano.
        rig.morseWorks = false;
        failed.clear();
        control.sendMorse(QStringLiteral("TEST"));
        QTRY_VERIFY_WITH_TIMEOUT(!failed.isEmpty() && failed.last().first().toString().contains(QStringLiteral("BK-IN")), 5000);
        QCOMPARE(unsupported.size(), 0);

        // Lo STOP: la radio non lo sa fare, lo si dice una volta.
        rig.stopWorks = false;
        failed.clear();
        const qsizetype stopsBeforeUnsupportedStop = rig.received.count(QStringLiteral("+\\stop_morse"));
        control.stopMorse();
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 5000);
        control.stopMorse();
        QTest::qWait(300);
        QCOMPARE(failed.size(), 1);
        QCOMPARE(rig.received.count(QStringLiteral("+\\stop_morse")), stopsBeforeUnsupportedStop + 1);

        // -11: questa radio non manipola, i tasti si spengono.
        rig.morseError = -11;
        control.sendMorse(QStringLiteral("TEST"));
        QTRY_COMPARE_WITH_TIMEOUT(unsupported.size(), 1, 5000);
    }

    void withoutTheRadioNothingGoesOnAir()
    {
        RigControl control;
        QSignalSpy failed(&control, &RigControl::failed);
        control.sendMorse(QStringLiteral("CQ"));
        QCOMPARE(failed.size(), 1);
        QVERIFY(!control.connected());
    }

    void understandsARigctldThatAnswersPlainly()
    {
        // Come il ponte CAT di Decodium: "+f" -> "14084000", senza RPRT.
        FakeRigctld rig;
        rig.plainAnswers = true;
        rig.frequency = 14084000;
        rig.mode = QStringLiteral("PKTUSB");
        RigControl control;
        control.connectTo(QStringLiteral("127.0.0.1"), rig.serverPort());
        QTRY_VERIFY_WITH_TIMEOUT(control.connected(), 5000);

        QTRY_COMPARE_WITH_TIMEOUT(control.frequencyHz(), 14084000LL, 5000);
        QCOMPARE(control.mode(), QStringLiteral("PKTUSB"));
        // E la coda non si inceppa: al giro dopo chiede ancora.
        const qsizetype before = rig.received.size();
        QTRY_VERIFY_WITH_TIMEOUT(rig.received.size() > before + 2, 6000);
    }

    void aCatBridgeThatCannotKeyCwSaysItClearly()
    {
        FakeRigctld rig;
        rig.plainAnswers = true;
        RigControl control;
        control.connectTo(QStringLiteral("127.0.0.1"), rig.serverPort());
        QTRY_VERIFY_WITH_TIMEOUT(control.connected(), 5000);

        QSignalSpy unsupported(&control, &RigControl::morseUnsupported);
        control.sendMorse(QStringLiteral("CQ"));
        QVERIFY(unsupported.wait(5000));
        QVERIFY(control.status().contains(QStringLiteral("CW")));
    }
};

QTEST_MAIN(TestRig)
#include "tst_rig.moc"
