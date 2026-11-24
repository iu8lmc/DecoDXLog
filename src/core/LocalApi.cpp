#include "core/LocalApi.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

namespace decolog::core {

namespace {

constexpr qsizetype kMaxHeader = 16 * 1024;
constexpr qsizetype kMaxBody = 1024 * 1024;

QByteArray reason(int status)
{
    switch (status) {
    case 200: return "OK";
    case 201: return "Created";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 410: return "Gone";
    case 413: return "Payload Too Large";
    case 503: return "Service Unavailable";
    default: return "Error";
    }
}

// Confronto che non si ferma al primo carattere diverso.
bool sameSecret(const QByteArray& a, const QByteArray& b)
{
    if (a.size() != b.size())
        return false;
    char diff = 0;
    for (qsizetype i = 0; i < a.size(); ++i)
        diff |= char(a.at(i) ^ b.at(i));
    return diff == 0;
}

} // namespace

HttpResponse HttpResponse::json(int status, const QJsonObject& object)
{
    HttpResponse r;
    r.status = status;
    r.body = QJsonDocument(object).toJson(QJsonDocument::Compact);
    return r;
}

HttpResponse HttpResponse::error(int status, const QString& message)
{
    return json(status, QJsonObject{{QStringLiteral("error"), message}});
}

LocalApiServer::LocalApiServer(QObject* parent)
    : QObject(parent)
{
}

LocalApiServer::~LocalApiServer() = default;

QString LocalApiServer::newToken()
{
    QByteArray bytes(16, Qt::Uninitialized);
    for (char& c : bytes)
        c = char(QRandomGenerator::system()->bounded(256));
    return QString::fromLatin1(bytes.toHex());
}

bool LocalApiServer::start(quint16 port)
{
    stop();
    if (port == 0)
        return false;
    m_server = new QTcpServer(this);
    if (!m_server->listen(QHostAddress::LocalHost, port)) {
        m_lastError = m_server->errorString();
        delete m_server;
        m_server = nullptr;
        emit listeningChanged();
        return false;
    }
    m_lastError.clear();
    connect(m_server, &QTcpServer::newConnection, this, [this] {
        while (QTcpSocket* s = m_server->nextPendingConnection()) {
            s->setReadBufferSize(kMaxHeader + kMaxBody + 4);
            m_buffers.insert(s, {});
            connect(s, &QTcpSocket::readyRead, this, [this, s] { readFrom(s); });
            connect(s, &QTcpSocket::disconnected, this, [this, s] {
                m_buffers.remove(s);
                s->deleteLater();
            });
            // Una connessione che non finisce la richiesta non resta aperta.
            QTimer::singleShot(10'000, s, [s] { s->abort(); });
        }
    });
    emit listeningChanged();
    return true;
}

void LocalApiServer::stop()
{
    if (!m_server)
        return;
    m_server->close();
    delete m_server;
    m_server = nullptr;
    emit listeningChanged();
}

bool LocalApiServer::isListening() const
{
    return m_server && m_server->isListening();
}

quint16 LocalApiServer::port() const
{
    return m_server ? m_server->serverPort() : 0;
}

QByteArray LocalApiServer::suppliedToken(const HttpRequest& request) const
{
    QByteArray given = request.headers.value("x-decodxlog-token");
    const QByteArray auth = request.headers.value("authorization");
    if (given.isEmpty() && auth.startsWith("Bearer "))
        given = auth.mid(7).trimmed();
    if (given.isEmpty())
        given = request.query.queryItemValue(QStringLiteral("token")).toLatin1();
    return given;
}

bool LocalApiServer::parseExtensionCredentials(const QByteArray& json, QHash<QString, ExtensionCredential>* credentials)
{
    if (!credentials) return false;
    credentials->clear();
    if (json.isEmpty()) return true;
    if (json.size() > 65536) return false;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isArray() || document.array().size() > 64) return false;
    static const QRegularExpression idPattern(QStringLiteral("^[a-z0-9]+(?:[.-][a-z0-9]+)+$"));
    static const QRegularExpression tokenPattern(QStringLiteral("^[0-9a-f]{32}$"));
    const QSet<QString> allowed{QStringLiteral("qso.read"), QStringLiteral("status.read"),
                                QStringLiteral("worked.read"), QStringLiteral("tracker.read"), QStringLiteral("qso.events")};
    QHash<QString, ExtensionCredential> parsed;
    QSet<QString> identities;
    for (const auto& item : document.array()) {
        if (!item.isObject()) return false;
        const QJsonObject object = item.toObject();
        if (object.size() != 3 || !object.value(QStringLiteral("id")).isString() ||
            !object.value(QStringLiteral("token")).isString() || !object.value(QStringLiteral("permissions")).isArray()) return false;
        const QString id = object.value(QStringLiteral("id")).toString();
        const QString token = object.value(QStringLiteral("token")).toString();
        if (id.size() > 128 || !idPattern.match(id).hasMatch() || !tokenPattern.match(token).hasMatch() ||
            identities.contains(id) || parsed.contains(token)) return false;
        QSet<QString> permissions;
        for (const auto& value : object.value(QStringLiteral("permissions")).toArray()) {
            if (!value.isString() || !allowed.contains(value.toString()) || permissions.contains(value.toString())) return false;
            permissions.insert(value.toString());
        }
        if (permissions.isEmpty()) return false;
        identities.insert(id); parsed.insert(token, ExtensionCredential{id, permissions});
    }
    *credentials = parsed;
    return true;
}

LocalApiServer::Access LocalApiServer::accessFor(const HttpRequest& request) const
{
    const QByteArray given = suppliedToken(request);
    if (given.isEmpty()) return Access::None;
    const bool full = !m_token.isEmpty() && sameSecret(given, m_token.toLatin1());
    const bool read = !m_readOnlyToken.isEmpty() && sameSecret(given, m_readOnlyToken.toLatin1());
    int extensionMatches = 0;
    for (auto it = m_extensionCredentials.cbegin(); it != m_extensionCredentials.cend(); ++it)
        if (!it.key().isEmpty() && sameSecret(given, it.key().toLatin1())) ++extensionMatches;
    if (int(full) + int(read) + extensionMatches != 1) return Access::None;
    return full ? Access::Full : read ? Access::ReadOnly : Access::Extension;
}

bool LocalApiServer::authorized(const HttpRequest& request) const
{
    return accessFor(request) != Access::None;
}

void LocalApiServer::readFrom(QTcpSocket* socket)
{
    if (!m_buffers.contains(socket))
        return;
    QByteArray& buffer = m_buffers[socket];
    buffer += socket->readAll();
    const qsizetype end = buffer.indexOf("\r\n\r\n");
    if (end < 0) {
        if (buffer.size() > kMaxHeader)
            answer(socket, HttpResponse::error(400, QStringLiteral("header too long")));
        return;
    }
    if (end > kMaxHeader) {
        answer(socket, HttpResponse::error(400, QStringLiteral("header too long")));
        return;
    }
    HttpRequest request;
    const QList<QByteArray> lines = buffer.left(end).split('\n');
    const QList<QByteArray> first = lines.value(0).trimmed().split(' ');
    if (first.size() < 2) {
        answer(socket, HttpResponse::error(400, QStringLiteral("bad request line")));
        return;
    }
    request.method = first.at(0).toUpper();
    const QUrl url(QString::fromUtf8(first.at(1)));
    request.path = url.path();
    request.query = QUrlQuery(url);
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        const qsizetype colon = line.indexOf(':');
        if (colon > 0) {
            const QByteArray name = line.left(colon).trimmed().toLower();
            if ((name == "content-length" && request.headers.contains(name)) || name == "transfer-encoding") {
                answer(socket, HttpResponse::error(400, QStringLiteral("unsupported request framing")));
                return;
            }
            request.headers.insert(name, line.mid(colon + 1).trimmed());
        }
    }
    qsizetype length = 0;
    if (request.headers.contains("content-length")) {
        const QByteArray value = request.headers.value("content-length");
        bool valid = !value.isEmpty();
        for (char c : value)
            valid = valid && c >= '0' && c <= '9';
        bool converted = false;
        const qulonglong parsed = value.toULongLong(&converted);
        if (!valid || !converted) {
            answer(socket, HttpResponse::error(400, QStringLiteral("invalid content length")));
            return;
        }
        if (parsed > qulonglong(kMaxBody)) {
            answer(socket, HttpResponse::error(413, QStringLiteral("body too large")));
            return;
        }
        length = qsizetype(parsed);
    }
    if (length > kMaxBody) {
        answer(socket, HttpResponse::error(413, QStringLiteral("body too large")));
        return;
    }
    if (buffer.size() < end + 4 + length)
        return;   // il resto del corpo arriva dopo
    request.body = buffer.mid(end + 4, length);

    const Access access = accessFor(request);
    if (access == Access::None) {
        answer(socket, HttpResponse::error(401, QStringLiteral("missing or wrong token")));
        return;
    }
    if (access == Access::Extension) {
        const QByteArray given = suppliedToken(request);
        for (auto it = m_extensionCredentials.cbegin(); it != m_extensionCredentials.cend(); ++it)
            if (!it.key().isEmpty() && sameSecret(given, it.key().toLatin1())) {
                request.extensionId = it.value().id;
                request.extensionPermissions = it.value().permissions;
            }
        const QString path = request.path.endsWith(QLatin1Char('/')) ? request.path.chopped(1) : request.path;
        QString permission;
        if (path == QLatin1String("/api/v1/status")) permission = QStringLiteral("status.read");
        else if (path == QLatin1String("/api/v1/worked")) permission = QStringLiteral("worked.read");
        else if (path == QLatin1String("/api/v1/qsos")) permission = QStringLiteral("qso.read");
        else if (path == QLatin1String("/api/v1/tracker")) permission = QStringLiteral("tracker.read");
        else if (path == QLatin1String("/api/v1/events")) permission = QStringLiteral("qso.events");
        const bool capabilities = path == QLatin1String("/api/v1/capabilities");
        if (request.method != "GET" || (!capabilities && (permission.isEmpty() || !request.extensionPermissions.contains(permission)))) {
            answer(socket, HttpResponse::error(403, QStringLiteral("extension permission denied")));
            return;
        }
        request.readOnly = true;
        request.extensionCredential = true;
    }
    if (access == Access::ReadOnly) {
        const QString path = request.path.endsWith(QLatin1Char('/')) ? request.path.chopped(1) : request.path;
        const bool permitted = request.method == "GET" &&
            (path == QLatin1String("/api/v1/capabilities") || path == QLatin1String("/api/v1/status") ||
             path == QLatin1String("/api/v1/worked") || path == QLatin1String("/api/v1/qsos") ||
             path == QLatin1String("/api/v1/tracker"));
        if (!permitted) {
            answer(socket, HttpResponse::error(403, QStringLiteral("read-only token")));
            return;
        }
        request.readOnly = true;
    }
    answer(socket, m_handler ? m_handler(request) : HttpResponse::error(503, QStringLiteral("not ready")));
}

void LocalApiServer::answer(QTcpSocket* socket, const HttpResponse& response)
{
    QByteArray out = "HTTP/1.1 " + QByteArray::number(response.status) + ' ' + reason(response.status) + "\r\n";
    out += "Content-Type: " + response.contentType + "\r\n";
    out += "Content-Length: " + QByteArray::number(response.body.size()) + "\r\n";
    out += "Cache-Control: no-store\r\nConnection: close\r\n\r\n";
    out += response.body;
    socket->write(out);
    socket->disconnectFromHost();
    m_buffers.remove(socket);
}

} // namespace decolog::core
