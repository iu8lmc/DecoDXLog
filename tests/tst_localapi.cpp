// L'interfaccia HTTP locale: solo con la chiave, la chiave in tre modi, il
// corpo delle POST anche se arriva a pezzi, e mai su un indirizzo di rete.
#include "core/LocalApi.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

using namespace decolog::core;

namespace {

struct Answer {
    int status{0};
    QJsonObject json;
};

Answer call(QNetworkAccessManager& net, const QByteArray& method, const QUrl& url, const QByteArray& body = {},
            const QList<QPair<QByteArray, QByteArray>>& headers = {})
{
    QNetworkRequest request(url);
    for (const auto& [name, value] : headers)
        request.setRawHeader(name, value);
    QNetworkReply* reply = net.sendCustomRequest(request, method, body);
    QSignalSpy done(reply, &QNetworkReply::finished);
    done.wait(5000);
    Answer a;
    a.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    a.json = QJsonDocument::fromJson(reply->readAll()).object();
    reply->deleteLater();
    return a;
}

quint16 freePort()
{
    QTcpServer probe;
    probe.listen(QHostAddress::LocalHost, 0);
    return probe.serverPort();
}

} // namespace

class TestLocalApi : public QObject {
    Q_OBJECT

    LocalApiServer m_server;
    QNetworkAccessManager m_net;
    QString m_token;
    QUrl base(const QString& path) const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(m_server.port()).arg(path));
    }

private slots:
    void initTestCase()
    {
        m_token = LocalApiServer::newToken();
        QCOMPARE(m_token.size(), 32);
        QVERIFY(m_token != LocalApiServer::newToken());
        m_server.setToken(m_token);
        m_server.setHandler([](const HttpRequest& r) {
            return HttpResponse::json(200, QJsonObject{{"method", QString::fromLatin1(r.method)},
                                                       {"path", r.path},
                                                       {"call", r.query.queryItemValue("call")},
                                                       {"body", QString::fromUtf8(r.body)},
                                                       {"extension", r.extensionCredential},
                                                       {"qsoRead", r.extensionPermissions.contains(QStringLiteral("qso.read"))}});
        });
        QVERIFY(m_server.start(freePort()));
        QVERIFY(m_server.isListening());
    }

    void withoutTheKeyNothing()
    {
        const Answer a = call(m_net, "GET", base("/api/v1/status"));
        QCOMPARE(a.status, 401);
        QVERIFY(a.json.value("error").toString().contains("token"));
        const Answer wrong = call(m_net, "GET", base("/api/v1/status"), {}, {{"X-DecoDXLog-Token", "0000"}});
        QCOMPARE(wrong.status, 401);
    }

    void theKeyThreeWays()
    {
        Answer a = call(m_net, "GET", base("/api/v1/worked?call=K1ABC"), {}, {{"X-DecoDXLog-Token", m_token.toLatin1()}});
        QCOMPARE(a.status, 200);
        QCOMPARE(a.json.value("path").toString(), QString("/api/v1/worked"));
        QCOMPARE(a.json.value("call").toString(), QString("K1ABC"));
        a = call(m_net, "GET", base("/api/v1/status"), {}, {{"Authorization", "Bearer " + m_token.toLatin1()}});
        QCOMPARE(a.status, 200);
        a = call(m_net, "GET", base("/api/v1/status?token=" + m_token));
        QCOMPARE(a.status, 200);
    }

    void postBody()
    {
        const QByteArray adif = "<CALL:5>K1ABC<QSO_DATE:8>20260928<TIME_ON:4>1200<BAND:3>20m<MODE:3>FT8<EOR>";
        const Answer a = call(m_net, "POST", base("/api/v1/qso"), adif, {{"X-DecoDXLog-Token", m_token.toLatin1()},
                                                                        {"Content-Type", "text/plain"}});
        QCOMPARE(a.status, 200);
        QCOMPARE(a.json.value("method").toString(), QString("POST"));
        QCOMPARE(a.json.value("body").toString(), QString::fromLatin1(adif));
    }

    void bodyInPieces()
    {
        // Intestazione e corpo in due pacchetti separati.
        QTcpSocket s;
        s.connectToHost(QHostAddress::LocalHost, m_server.port());
        QVERIFY(s.waitForConnected(3000));
        s.write("POST /api/v1/qso HTTP/1.1\r\nHost: x\r\nX-DecoDXLog-Token: " + m_token.toLatin1()
                + "\r\nContent-Length: 10\r\n\r\n01234");
        s.flush();
        QTest::qWait(150);
        s.write("56789");
        QTRY_VERIFY_WITH_TIMEOUT(s.bytesAvailable() > 0 || s.waitForReadyRead(100), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(s.state() == QAbstractSocket::UnconnectedState || s.waitForDisconnected(100), 3000);
        const QByteArray answer = s.readAll();
        QVERIFY2(answer.startsWith("HTTP/1.1 200"), answer.constData());
        QVERIFY(answer.contains("0123456789"));
    }

    void rejectsMalformedRequestFraming_data()
    {
        QTest::addColumn<QByteArray>("extraHeaders");
        QTest::newRow("negative") << QByteArray("Content-Length: -1\r\n");
        QTest::newRow("nonnumeric") << QByteArray("Content-Length: bad\r\n");
        QTest::newRow("duplicate") << QByteArray("Content-Length: 0\r\nContent-Length: 0\r\n");
        QTest::newRow("transfer-encoding") << QByteArray("Transfer-Encoding: chunked\r\n");
        QTest::newRow("oversized-header") << QByteArray("X-Fill: ") + QByteArray(17000,'x') + "\r\n";
    }
    void rejectsMalformedRequestFraming()
    {
        QFETCH(QByteArray,extraHeaders);
        QTcpSocket socket; socket.connectToHost(QHostAddress::LocalHost,m_server.port());
        QVERIFY(socket.waitForConnected(3000));
        socket.write("POST /api/v1/qso HTTP/1.1\r\nHost: x\r\nX-DecoDXLog-Token: " + m_token.toLatin1() + "\r\n" + extraHeaders + "\r\n");
        socket.flush();
        QTRY_VERIFY_WITH_TIMEOUT(socket.bytesAvailable()>0 || socket.waitForReadyRead(100),3000);
        const auto response=socket.readAll();
        QVERIFY2(response.startsWith("HTTP/1.1 400"),response.constData());
    }

    void readOnlyCredentialCannotWrite()
    {
        const QString readToken=LocalApiServer::newToken();
        m_server.setReadOnlyToken(readToken);
        const QList<QPair<QByteArray,QByteArray>> header{{"X-DecoDXLog-Token",readToken.toLatin1()}};
        QCOMPARE(call(m_net,"GET",base("/api/v1/status"),{},header).status,200);
        QCOMPARE(call(m_net,"GET",base("/api/v1/qso"),{},header).status,403);
        QCOMPARE(call(m_net,"GET",base("/api/v1/idempotency"),{},header).status,403);
        QCOMPARE(call(m_net,"POST",base("/api/v1/qso"),"<EOR>",header).status,403);
        QCOMPARE(call(m_net,"GET",base("/api/v1/unknown"),{},header).status,403);
        QCOMPARE(call(m_net,"POST",base("/api/v1/qso"),"<EOR>",{{"X-DecoDXLog-Token",m_token.toLatin1()}}).status,200);
        m_server.setReadOnlyToken({});
        QCOMPARE(call(m_net,"GET",base("/api/v1/status"),{},header).status,401);
        m_server.setReadOnlyToken(m_token);
        QCOMPARE(call(m_net,"GET",base("/api/v1/status"),{},{{"X-DecoDXLog-Token",m_token.toLatin1()}}).status,401);
        m_server.setReadOnlyToken({});
    }

    void scopedExtensionCredentials()
    {
        const QString qsoToken = LocalApiServer::newToken();
        const QString trackerToken = LocalApiServer::newToken();
        // Baseline: the existing shared read-only credential grants both reads.
        m_server.setReadOnlyToken(qsoToken);
        const QList<QPair<QByteArray,QByteArray>> baseline{{"X-DecoDXLog-Token",qsoToken.toLatin1()}};
        QCOMPARE(call(m_net,"GET",base("/api/v1/qsos"),{},baseline).status,200);
        QCOMPARE(call(m_net,"GET",base("/api/v1/tracker"),{},baseline).status,200);
        m_server.setReadOnlyToken({});
        m_server.setExtensionCredentials({{qsoToken, {QStringLiteral("qso.read")}},
                                          {trackerToken, {QStringLiteral("tracker.read")}}});
        const QList<QPair<QByteArray,QByteArray>> qso{{"X-DecoDXLog-Token",qsoToken.toLatin1()}};
        const QList<QPair<QByteArray,QByteArray>> tracker{{"X-DecoDXLog-Token",trackerToken.toLatin1()}};
        const Answer caps = call(m_net,"GET",base("/api/v1/capabilities"),{},qso);
        QCOMPARE(caps.status,200);
        QVERIFY(caps.json.value("extension").toBool());
        QVERIFY(caps.json.value("qsoRead").toBool());
        QCOMPARE(call(m_net,"GET",base("/api/v1/qsos"),{},qso).status,200);
        QCOMPARE(call(m_net,"GET",base("/api/v1/tracker"),{},qso).status,403);
        QCOMPARE(call(m_net,"POST",base("/api/v1/qso"),"<EOR>",qso).status,403);
        QCOMPARE(call(m_net,"GET",base("/api/v1/qsos"),{},tracker).status,403);
        QCOMPARE(call(m_net,"GET",base("/api/v1/tracker"),{},tracker).status,200);
        QCOMPARE(call(m_net,"GET",base("/api/v1/status"),{},tracker).status,403);
        QCOMPARE(call(m_net,"GET",base("/api/v1/unknown"),{},tracker).status,403);
        m_server.setReadOnlyToken(qsoToken);
        QCOMPARE(call(m_net,"GET",base("/api/v1/qsos"),{},qso).status,401);
        m_server.setReadOnlyToken({});
        m_server.setExtensionCredentials({{m_token,{QStringLiteral("qso.read")}}});
        QCOMPARE(call(m_net,"GET",base("/api/v1/qsos"),{},{{"X-DecoDXLog-Token",m_token.toLatin1()}}).status,401);
        m_server.setExtensionCredentials({});
        QCOMPARE(call(m_net,"GET",base("/api/v1/qsos"),{},qso).status,401);
    }

    void extensionCredentialConfigurationFailsClosed()
    {
        QHash<QString, QSet<QString>> parsed;
        const QByteArray good = R"([{"id":"example.report","token":"11111111111111111111111111111111","permissions":["qso.read"]}])";
        QVERIFY(LocalApiServer::parseExtensionCredentials(good, &parsed));
        QCOMPARE(parsed.size(),1);
        QVERIFY(parsed.value(QStringLiteral("11111111111111111111111111111111")).contains(QStringLiteral("qso.read")));
        const QList<QByteArray> bad{
            "not json", "{}",
            R"([{"id":"example.report","token":"11111111111111111111111111111111","permissions":["qso.write"]}])",
            R"([{"id":"example.report","token":"11111111111111111111111111111111","permissions":[]}])",
            R"([{"id":"example.report","token":"11111111111111111111111111111111","permissions":["qso.read"]},{"id":"example.report","token":"22222222222222222222222222222222","permissions":["tracker.read"]}])",
            R"([{"id":"example.report","token":"11111111111111111111111111111111","permissions":["qso.read"]},{"id":"example.tracker","token":"11111111111111111111111111111111","permissions":["tracker.read"]}])"};
        for (const auto& value : bad) {
            parsed.insert(QStringLiteral("sentinel"), {});
            QVERIFY(!LocalApiServer::parseExtensionCredentials(value, &parsed));
            QVERIFY(parsed.isEmpty());
        }
    }

    void noKeyNoServer()
    {
        // Senza chiave anche con la chiave vuota si risponde 401.
        LocalApiServer open;
        QVERIFY(open.start(freePort()));
        HttpRequest r;
        QVERIFY(!open.authorized(r));
        r.headers.insert("x-decodxlog-token", "");
        QVERIFY(!open.authorized(r));
    }

    void aBusyPortIsAnError()
    {
        // La stessa porta non si apre due volte, e l'errore si dice.
        LocalApiServer busy;
        QVERIFY(!busy.start(m_server.port()));
        QVERIFY(!busy.lastError().isEmpty());
        QVERIFY(!busy.isListening());
    }
};

QTEST_GUILESS_MAIN(TestLocalApi)
#include "tst_localapi.moc"
