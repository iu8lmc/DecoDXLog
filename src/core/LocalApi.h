// DecoDXLog — un'interfaccia HTTP locale per gli altri programmi.
//
// Solo su 127.0.0.1, e ogni richiesta porta la chiave (il "token") scritta
// nelle impostazioni: un sito aperto nel browser puo' parlare con 127.0.0.1,
// ma non conosce la chiave. Le risposte sono JSON; niente CORS, cosi' il
// browser non le fa leggere a nessuna pagina.
//
// Il server e' volutamente piccolo: HTTP/1.1, una richiesta per connessione,
// corpo fino a 1 MB. Cosa rispondere lo decide chi lo usa (DecoLogController).
#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QObject>
#include <QString>
#include <QUrlQuery>
#include <functional>

class QTcpServer;
class QTcpSocket;

namespace decolog::core {

struct HttpRequest {
    QByteArray method;                       // GET, POST...
    QString path;                            // /api/v1/qso
    QUrlQuery query;
    QHash<QByteArray, QByteArray> headers;   // nomi in minuscolo
    QByteArray body;
    bool readOnly{false};
    bool extensionCredential{false};
    QSet<QString> extensionPermissions;
};

struct HttpResponse {
    int status{200};
    QByteArray contentType{"application/json; charset=utf-8"};
    QByteArray body;

    static HttpResponse json(int status, const QJsonObject& object);
    static HttpResponse error(int status, const QString& message);
};

class LocalApiServer : public QObject {
    Q_OBJECT

public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    explicit LocalApiServer(QObject* parent = nullptr);
    ~LocalApiServer() override;

    // Porta 0 = spento. Solo 127.0.0.1.
    bool start(quint16 port);
    void stop();
    bool isListening() const;
    quint16 port() const;
    QString lastError() const { return m_lastError; }

    // Senza chiave il server risponde 401 a tutto: non c'e' un modo "aperto".
    void setToken(const QString& token) { m_token = token; }
    void setReadOnlyToken(const QString& token) { m_readOnlyToken = token; }
    // Volatile grants; provisioning and persistent per-extension management are separate.
    void setExtensionCredentials(const QHash<QString, QSet<QString>>& credentials) { m_extensionCredentials = credentials; }
    void setHandler(Handler handler) { m_handler = std::move(handler); }

    // Una chiave nuova, 32 caratteri esadecimali casuali.
    static QString newToken();
    static bool parseExtensionCredentials(const QByteArray& json, QHash<QString, QSet<QString>>* credentials);

    // Per i test e per chi risponde: la richiesta ha la chiave giusta?
    bool authorized(const HttpRequest& request) const;

signals:
    void listeningChanged();

private:
    enum class Access { None, ReadOnly, Extension, Full };
    QByteArray suppliedToken(const HttpRequest& request) const;
    Access accessFor(const HttpRequest& request) const;
    void readFrom(QTcpSocket* socket);
    void answer(QTcpSocket* socket, const HttpResponse& response);

    QTcpServer* m_server{nullptr};
    QString m_token;
    QString m_readOnlyToken;
    QHash<QString, QSet<QString>> m_extensionCredentials;
    Handler m_handler;
    QString m_lastError;
    QHash<QTcpSocket*, QByteArray> m_buffers;
};

} // namespace decolog::core
