// flrig: XML-RPC scritto e letto, modi tradotti, e un flrig finto.
#include "core/FlrigControl.h"
#include "core/OmniRigControl.h"
#include "core/Modes.h"

#include <QSignalSpy>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

using namespace decolog::core;

class TestFlrig : public QObject {
    Q_OBJECT

private slots:
    void requestXml()
    {
        const QByteArray r = flrig::request("rig.set_vfo", {14074000.0});
        QVERIFY(r.contains("<methodName>rig.set_vfo</methodName>"));
        QVERIFY(r.contains("<double>14074000.0</double>"));
        QVERIFY(flrig::request("rig.set_mode", {QString("CW")}).contains("<string>CW</string>"));
        QVERIFY(flrig::request("rig.set_ptt", {1}).contains("<i4>1</i4>"));
    }

    void responses()
    {
        QCOMPARE(flrig::parseResponse("<methodResponse><params><param><value>14074000</value></param></params></methodResponse>").toString(),
                 QString("14074000"));
        QCOMPARE(flrig::parseResponse("<methodResponse><params><param><value><string>USB</string></value></param></params></methodResponse>").toString(),
                 QString("USB"));
        const QVariantList modes = flrig::parseResponse(
            "<methodResponse><params><param><value><array><data><value>LSB</value><value>USB</value>"
            "<value>CW</value><value>DATA-U</value></data></array></value></param></params></methodResponse>").toList();
        QCOMPARE(modes.size(), 4);
        QCOMPARE(modes.at(3).toString(), QString("DATA-U"));
        QString fault;
        flrig::parseResponse("<methodResponse><fault><value><struct><member><name>faultCode</name><value><i4>1</i4></value></member>"
                             "<member><name>faultString</name><value>no such method</value></member></struct></value></fault></methodResponse>",
                             &fault);
        QCOMPARE(fault, QString("no such method"));
    }

    void modes()
    {
        QCOMPARE(flrig::toHamlibMode("DATA-U"), QString("PKTUSB"));
        QCOMPARE(flrig::toHamlibMode("USB-D"), QString("PKTUSB"));
        QCOMPARE(flrig::toHamlibMode("CW-R"), QString("CWR"));
        QCOMPARE(flrig::toHamlibMode("CW"), QString("CW"));
        QCOMPARE(flrig::toHamlibMode("LSB"), QString("LSB"));
        QCOMPARE(flrig::fromHamlibMode("PKTUSB", {"LSB", "USB", "CW", "USB-D"}), QString("USB-D"));
        QCOMPARE(flrig::fromHamlibMode("PKTUSB", {"LSB", "USB", "DATA-U"}), QString("DATA-U"));
        QCOMPARE(omnirig::toHamlibMode(omnirig::PM_DIG_U), QString("PKTUSB"));
        QCOMPARE(omnirig::fromHamlibMode("CW"), long(omnirig::PM_CW_U));
    }

    void clusterFtOmniRigCommand()
    {
        for (const auto* mode : {"FT8", "FT4", "FT2"})
            QCOMPARE(omnirig::fromHamlibMode(modes::catForClusterSpot(QLatin1String(mode), 14.074, "omnirig")), long(omnirig::PM_SSB_U));
        QCOMPARE(omnirig::fromHamlibMode(modes::catForClusterSpot("RTTY", 14.09, "omnirig")), long(omnirig::PM_DIG_L));
        QCOMPARE(omnirig::fromHamlibMode(modes::catForClusterSpot("RTTY-R", 14.09, "omnirig")), long(omnirig::PM_DIG_U));
    }

    // Con OmniRig installato e DECODXLOG_OMNIRIG_LIVE=1: legge Rig1, senza scrivere niente.
    void omniRigLive()
    {
        if (qEnvironmentVariableIntValue("DECODXLOG_OMNIRIG_LIVE") != 1)
            QSKIP("OmniRig live test off");
        OmniRigControl rig;
        rig.connectTo(1);
        QTest::qWait(3000);
        qInfo() << "OmniRig:" << rig.status() << rig.connected() << rig.frequencyHz() << rig.mode();
        QVERIFY(!rig.status().isEmpty());
        rig.disconnectFromRig();
    }

    void fakeFlrig()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QStringList methods;
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            while (QTcpSocket* s = server.nextPendingConnection()) {
                connect(s, &QTcpSocket::readyRead, s, [s, &methods] {
                    const QByteArray all = s->property("buf").toByteArray() + s->readAll();
                    s->setProperty("buf", all);
                    const int body = all.indexOf("\r\n\r\n");
                    if (body < 0 || !all.contains("</methodCall>"))
                        return;
                    const int a = all.indexOf("<methodName>") + 12;
                    const QString method = QString::fromUtf8(all.mid(a, all.indexOf("</methodName>") - a));
                    methods << method;
                    QByteArray value = "<value>1</value>";
                    if (method == "rig.get_vfo") value = "<value>7074000</value>";
                    else if (method == "rig.get_mode") value = "<value>USB-D</value>";
                    else if (method == "rig.get_split") value = "<value><i4>1</i4></value>";
                    else if (method == "rig.get_vfoB") value = "<value>7076000</value>";
                    else if (method == "rig.get_AB") value = "<value>B</value>";
                    else if (method == "rig.get_modes") value = "<value><array><data><value>USB</value><value>USB-D</value></data></array></value>";
                    const QByteArray xml = "<?xml version=\"1.0\"?><methodResponse><params><param>" + value + "</param></params></methodResponse>";
                    s->write("HTTP/1.1 200 OK\r\nContent-Type: text/xml\r\nContent-Length: " + QByteArray::number(xml.size())
                             + "\r\nConnection: close\r\n\r\n" + xml);
                    s->disconnectFromHost();
                });
            }
        });
        FlrigControl rig;
        rig.connectTo(QStringLiteral("127.0.0.1:%1").arg(server.serverPort()));
        QTRY_VERIFY(rig.connected());
        QTRY_COMPARE(rig.frequencyHz(), qint64(7074000));
        QTRY_COMPARE(rig.mode(), QString("PKTUSB"));
        rig.setFrequency(14074000);
        QTRY_VERIFY(methods.contains("rig.set_vfo"));
        rig.setMode("PKTUSB");
        QTRY_VERIFY(methods.contains("rig.set_mode"));
        // Split sul VFO B e il VFO scelto: letti e comandati.
        QTRY_VERIFY(rig.split());
        QTRY_COMPARE(rig.txFrequencyHz(), qint64(7076000));
        QTRY_COMPARE(rig.vfo(), QString("VFOB"));
        rig.setSplit(true, 7080000);
        QTRY_VERIFY(methods.contains("rig.set_vfoB"));
        QTRY_VERIFY(methods.contains("rig.set_split"));
        rig.setVfo("VFOA");
        QTRY_VERIFY(methods.contains("rig.set_AB"));
        rig.disconnectFromRig();
        QVERIFY(!rig.connected());
    }

    // cwio_text e' asincrono: se Ferma arriva mentre flrig sta ancora
    // rispondendo, il completamento vecchio non deve poter mandare cwio_send 1.
    void emergencyStopCancelsDeferredCwStart()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QStringList calls;
        QPointer<QTcpSocket> heldText;
        const auto reply = [](QTcpSocket* socket) {
            const QByteArray xml = "<?xml version=\"1.0\"?><methodResponse><params><param>"
                                   "<value><i4>1</i4></value></param></params></methodResponse>";
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/xml\r\nContent-Length: "
                          + QByteArray::number(xml.size()) + "\r\nConnection: close\r\n\r\n" + xml);
            socket->disconnectFromHost();
        };
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            while (QTcpSocket* socket = server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                    const QByteArray all = socket->property("buf").toByteArray() + socket->readAll();
                    socket->setProperty("buf", all);
                    const int body = all.indexOf("\r\n\r\n");
                    if (body < 0 || !all.contains("</methodCall>"))
                        return;
                    const int first = all.indexOf("<methodName>") + 12;
                    const QString method = QString::fromUtf8(all.mid(first, all.indexOf("</methodName>") - first));
                    calls << method + QLatin1Char('|') + QString::fromUtf8(all.mid(body + 4));
                    if (method == QLatin1String("rig.cwio_text")) {
                        heldText = socket;
                        return;
                    }
                    reply(socket);
                });
            }
        });

        FlrigControl rig;
        rig.connectTo(QStringLiteral("127.0.0.1:%1").arg(server.serverPort()));
        QTRY_VERIFY_WITH_TIMEOUT(rig.connected(), 5000);
        calls.clear();

        rig.sendMorse(QStringLiteral("CQ"));
        QTRY_VERIFY_WITH_TIMEOUT(!heldText.isNull(), 5000);
        rig.emergencyStop();

        auto saw = [&calls](const QString& method, const QString& value) {
            for (const QString& call : calls)
                if (call.startsWith(method + QLatin1Char('|')) && call.contains(value))
                    return true;
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(saw(QStringLiteral("rig.set_ptt"), QStringLiteral("<i4>0</i4>")), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(saw(QStringLiteral("rig.cwio_send"), QStringLiteral("<i4>0</i4>")), 5000);

        // La risposta di cwio_text arriva solo adesso. Prima della correzione
        // la sua callback aggiungeva cwio_send 1 e riaccendeva il TX.
        reply(heldText);
        QTest::qWait(250);
        QVERIFY(!saw(QStringLiteral("rig.cwio_send"), QStringLiteral("<i4>1</i4>")));
    }
};

QTEST_GUILESS_MAIN(TestFlrig)
#include "tst_flrig.moc"
