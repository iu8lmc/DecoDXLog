#include "core/CallbookProfileVerifier.h"
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUrlQuery>

using namespace decolog::core;

class FakeQrz : public QTcpServer {
public:
    int requests{0};
    int logins{0};
    int lookups{0};
    bool failLogin{false};
    FakeQrz() {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* socket = nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    const QByteArray head = socket->readAll();
                    const QUrl request(QString::fromLatin1(head.left(head.indexOf("\r\n")).split(' ').value(1)));
                    const QUrlQuery query(request);
                    ++requests;
                    QByteArray body;
                    int delay = 0;
                    if (query.hasQueryItem("username")) {
                        ++logins;
                        body = failLogin ? "<QRZDatabase><Session><Error>bad credentials</Error></Session></QRZDatabase>"
                                         : "<QRZDatabase><Session><Key>test-session</Key></Session></QRZDatabase>";
                    } else {
                        ++lookups;
                        const QString call = query.queryItemValue("callsign");
                        if (call == QLatin1String("K1OLD")) delay = 180;
                        if (call == QLatin1String("K1MISS"))
                            body = "<QRZDatabase><Session><Error>Not found: K1MISS</Error><Key>test-session</Key></Session></QRZDatabase>";
                        else if (call == QLatin1String("K1BAD"))
                            body = "<QRZDatabase><Session><Error>service error</Error><Key>test-session</Key></Session></QRZDatabase>";
                        else if (call == QLatin1String("K1ALIAS"))
                            body = "<QRZDatabase><Callsign><call>K1OTHER</call></Callsign></QRZDatabase>";
                        else
                            body = "<QRZDatabase><Callsign><call>" + call.toUtf8() + "</call></Callsign></QRZDatabase>";
                    }
                    QTimer::singleShot(delay, socket, [socket, body] {
                        socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/xml\r\nConnection: close\r\nContent-Length: "
                                      + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                        socket->disconnectFromHost();
                    });
                });
            }
        });
        listen(QHostAddress::LocalHost);
    }
    QUrl endpoint() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/xml").arg(serverPort())); }
};

class FakeHamQth : public QTcpServer {
public:
    int logins{0};
    int lookups{0};
    FakeHamQth() {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* socket = nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    const QByteArray request = socket->readAll();
                    const QUrl url(QString::fromLatin1(request.left(request.indexOf("\r\n")).split(' ').value(1)));
                    const QUrlQuery q(url);
                    QByteArray body;
                    int delay = 0;
                    if (q.hasQueryItem("u")) {
                        ++logins;
                        body = "<HamQTH><session><session_id>test-hamqth-session</session_id></session></HamQTH>";
                    } else {
                        ++lookups;
                        const QString call = q.queryItemValue("callsign");
                        if (call == QLatin1String("K1OLD")) delay = 180;
                        if (call == QLatin1String("K1MISS"))
                            body = "<HamQTH><session><error>Callsign not found</error></session></HamQTH>";
                        else if (call == QLatin1String("K1ERROR"))
                            body = "<HamQTH><session><error>service unavailable</error></session></HamQTH>";
                        else if (call == QLatin1String("K1ALIAS"))
                            body = "<HamQTH><search><callsign>K1OTHER</callsign></search></HamQTH>";
                        else
                            body = "<HamQTH><search><callsign>" + call.toUtf8() + "</callsign><nick>Test</nick></search></HamQTH>";
                    }
                    QTimer::singleShot(delay, socket, [socket, body] {
                        socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/xml\r\nConnection: close\r\nContent-Length: "
                                      + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                        socket->disconnectFromHost();
                    });
                });
            }
        });
        listen(QHostAddress::LocalHost);
    }
    QUrl endpoint() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/xml").arg(serverPort())); }
};

class TestQrzProfile : public QObject {
    Q_OBJECT
    static void configure(CallbookProfileVerifier& verifier, const QUrl& url) {
        verifier.setDebounceInterval(1);
        verifier.setEndpoint(url);
        verifier.setCredentialReaders([](const QString&) { return QStringLiteral("K1USER"); },
            [](const QString&, auto done) { done(QStringLiteral("fake-password"), {}); });
    }
private slots:
    void positiveNegativeErrorsAndCache() {
        FakeQrz server; QVERIFY(server.isListening());
        CallbookProfileVerifier verifier; configure(verifier, server.endpoint());
        for (const QString& value : {QString(), QStringLiteral("HELLO"), QStringLiteral("12345"), QStringLiteral("K1 ABC"), QStringLiteral("K1?ABC")}) {
            verifier.setCall(value);
            QCOMPARE(verifier.state(), QStringLiteral("idle"));
        }
        QTest::qWait(10); QCOMPARE(server.requests, 0);
        verifier.setCall(" k1abc ");
        QCOMPARE(verifier.call(), QStringLiteral("K1ABC"));
        QTRY_COMPARE_WITH_TIMEOUT(verifier.state(), QStringLiteral("available"), 5000);
        QCOMPARE(server.logins, 1); QCOMPARE(server.lookups, 1);
        verifier.setCall("K1MISS");
        QTRY_COMPARE_WITH_TIMEOUT(verifier.state(), QStringLiteral("unknown"), 5000);
        QCOMPARE(server.lookups, 2);
        verifier.setCall("K1ABC");
        QTRY_COMPARE_WITH_TIMEOUT(verifier.state(), QStringLiteral("available"), 5000);
        QCOMPARE(server.lookups, 2); // verified result cached
        verifier.setCall("K1BAD");
        QTRY_COMPARE_WITH_TIMEOUT(verifier.state(), QStringLiteral("unknown"), 5000);
        verifier.setCall("K1ALIAS");
        QTRY_COMPARE_WITH_TIMEOUT(verifier.state(), QStringLiteral("unknown"), 5000);
    }
    void oldResponseCannotChangeCurrentCall() {
        FakeQrz server; CallbookProfileVerifier verifier; configure(verifier, server.endpoint());
        verifier.setCall("K1OLD");
        QTRY_COMPARE_WITH_TIMEOUT(server.lookups, 1, 5000);
        verifier.setCall("K1NEW");
        QTRY_COMPARE_WITH_TIMEOUT(verifier.state(), QStringLiteral("available"), 5000);
        QTest::qWait(250);
        QCOMPARE(verifier.call(), QStringLiteral("K1NEW"));
        QCOMPARE(verifier.state(), QStringLiteral("available"));
    }
    void slowResponseKeepsEventLoopResponsive() {
        FakeQrz server; CallbookProfileVerifier verifier; configure(verifier, server.endpoint());
        int ticks = 0;
        QTimer heartbeat;
        heartbeat.setInterval(10);
        connect(&heartbeat, &QTimer::timeout, this, [&ticks] { ++ticks; });
        heartbeat.start();
        verifier.setCall("K1OLD");
        QTRY_COMPARE_WITH_TIMEOUT(verifier.state(), QStringLiteral("available"), 5000);
        QVERIFY(ticks >= 5);
    }
    void reusePrimaryAndResumeLookup() {
        FakeQrz server; CallbookProfileVerifier verifier; configure(verifier, server.endpoint());
        verifier.setCall("K1ABC", false);
        QTest::qWait(30); QCOMPARE(server.requests, 0);
        verifier.observeVerified("K1OLD", "K1OLD");
        verifier.observeVerified("K1ABC", "K1OTHER");
        QCOMPARE(verifier.state(), QStringLiteral("checking"));
        verifier.observeVerified("K1ABC", "K1ABC");
        QCOMPARE(verifier.state(), QStringLiteral("available"));
        verifier.observeUnverified("K1ABC");
        QCOMPARE(verifier.state(), QStringLiteral("available"));
        verifier.setCall("K1TEMP", false);
        verifier.setCall("K1ABC", true);
        QCOMPARE(verifier.state(), QStringLiteral("available"));
        QTest::qWait(30);
        QCOMPARE(server.requests, 0); // reuse a positive result from the primary client
        verifier.setCall("K1NEXT", false);
        verifier.observeUnverified("K1NEXT");
        QCOMPARE(verifier.state(), QStringLiteral("unknown"));
        verifier.setCall("K1NEXT", true);
        QTRY_COMPARE_WITH_TIMEOUT(verifier.state(), QStringLiteral("available"), 5000);
        QCOMPARE(server.lookups, 1);
    }
    void credentialInvalidationAndDelayedSecret() {
        FakeQrz server; CallbookProfileVerifier verifier;
        verifier.setDebounceInterval(1); verifier.setEndpoint(server.endpoint());
        std::function<void(const QString&, const QString&)> oldSecret;
        verifier.setCredentialReaders([](const QString&) { return QStringLiteral("K1USER"); },
            [&oldSecret](const QString&, auto done) { oldSecret = std::move(done); });
        verifier.setCall("K1ABC");
        QTRY_VERIFY_WITH_TIMEOUT(bool(oldSecret), 5000);
        verifier.setCredentialReaders([](const QString&) { return QString(); }, {});
        oldSecret(QStringLiteral("old-password"), {});
        QTRY_COMPARE_WITH_TIMEOUT(verifier.state(), QStringLiteral("unknown"), 5000);
        QTest::qWait(30); QCOMPARE(server.requests, 0);
        configure(verifier, server.endpoint());
        QTRY_COMPARE_WITH_TIMEOUT(verifier.state(), QStringLiteral("available"), 5000);
        QCOMPARE(server.logins, 1);
        server.failLogin = true;
        verifier.invalidateCredentials();
        QTRY_COMPARE_WITH_TIMEOUT(verifier.state(), QStringLiteral("unknown"), 5000);
        QCOMPARE(server.logins, 2);
    }
    void hamQthPositiveNegativeErrorAndCache() {
        FakeHamQth server; QVERIFY(server.isListening());
        CallbookProfileVerifier verifier(CallbookClient::Provider::HamQth);
        QString accountService, secretService;
        verifier.setDebounceInterval(1);
        verifier.setEndpoint(server.endpoint());
        verifier.setCredentialReaders([&accountService](const QString& service) {
            accountService = service; return QStringLiteral("K1USER");
        }, [&secretService](const QString& service, auto done) {
            secretService = service; done(QStringLiteral("test-password"), {});
        });
        verifier.setCall(" k1abc ");
        QTRY_COMPARE_WITH_TIMEOUT(verifier.state(), QStringLiteral("available"), 5000);
        QCOMPARE(accountService, QStringLiteral("hamqth"));
        QCOMPARE(secretService, QStringLiteral("hamqth"));
        QCOMPARE(server.logins, 1); QCOMPARE(server.lookups, 1);
        verifier.setCall("K1MISS");
        QTRY_COMPARE_WITH_TIMEOUT(verifier.state(), QStringLiteral("unknown"), 5000);
        QCOMPARE(server.lookups, 2);
        verifier.setCall("K1ABC");
        QTRY_COMPARE_WITH_TIMEOUT(verifier.state(), QStringLiteral("available"), 5000);
        QCOMPARE(server.lookups, 2);
        verifier.setCall("K1ERROR");
        QTRY_COMPARE_WITH_TIMEOUT(verifier.state(), QStringLiteral("unknown"), 5000);
        verifier.setCall("K1ALIAS");
        QTRY_COMPARE_WITH_TIMEOUT(verifier.state(), QStringLiteral("unknown"), 5000);
    }
    void hamQthIgnoresStaleResponsesAndSupportsReuse() {
        FakeHamQth server; CallbookProfileVerifier verifier(CallbookClient::Provider::HamQth);
        configure(verifier, server.endpoint());
        verifier.setCall("K1OLD");
        QTRY_COMPARE_WITH_TIMEOUT(server.lookups, 1, 5000);
        verifier.setCall("K1NEW");
        QTRY_COMPARE_WITH_TIMEOUT(verifier.state(), QStringLiteral("available"), 5000);
        QTest::qWait(250);
        QCOMPARE(verifier.call(), QStringLiteral("K1NEW"));
        QCOMPARE(verifier.state(), QStringLiteral("available"));
        const int before = server.lookups;
        verifier.setCall("K1OTHER", false);
        verifier.observeVerified("K1OTHER", "K1OTHER");
        QCOMPARE(verifier.state(), QStringLiteral("available"));
        QTest::qWait(20);
        QCOMPARE(server.lookups, before);
        verifier.invalidateCredentials();
        QCOMPARE(verifier.state(), QStringLiteral("unknown"));
    }
};
QTEST_GUILESS_MAIN(TestQrzProfile)
#include "tst_callbookprofile.moc"
