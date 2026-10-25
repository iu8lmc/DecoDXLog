// DecoDXLog — il log su DecoPort: i QSO verso HH, il ponte di HamHub Log.
//
// DecoPort (Decodium-4.0/doc/DECOPORT_PROTOCOL.md) mette in rete una radio:
// intestazione "DPRT" di 28 byte, UDP, ogni pacchetto firmato con HMAC-SHA256
// (16 byte in coda) con la chiave derivata da una password condivisa. Qui il
// log fa la stessa parte del gateway: si annuncia, chi vuole i QSO si registra
// con HELLO e li riceve firmati, e conferma ogni QSO con un ACK. Niente cambia
// per chi parla DecoPort con la radio: i tipi 16-18 sono nuovi, e un client che
// non li conosce li scarta (il parser di riferimento accetta solo 1-9).
//
// Solo su 127.0.0.1: HH sta sulla stessa macchina. Il log non va in rete, e la
// firma serve a far entrare solo chi conosce la password anche tra i processi
// locali. Il formato dei pacchetti e' in docs/DECOPORT-LOG.md.
#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>
#include <optional>

class QUdpSocket;

namespace decolog::core {

namespace decoportlog {

inline constexpr quint8 kVersion = 1;
inline constexpr int kHeaderBytes = 28;
inline constexpr int kAuthTagBytes = 16;
inline constexpr quint16 kFlagTimestamp = 1;
inline constexpr quint16 kFlagSigned = 2;
inline constexpr qint64 kReplayWindowSeconds = 30;
inline constexpr quint16 kAnnouncePort = 5560;
// La sessione del log: la 5559 e' del gateway della radio, che puo' stare sulla
// stessa macchina.
inline constexpr quint16 kDefaultSessionPort = 5561;
inline constexpr int kMaxAdifBytes = 60000;

// I tipi di DecoPort che il log usa (1-9 sono quelli della radio).
enum Type : quint8 {
    Hello = 2,
    Bye = 3,
    Keepalive = 4,
    LogAnnounce = 16,   // log -> chi ascolta: chi sono, su che porta
    LogQso = 17,        // log -> client: un QSO in ADIF
    LogAck = 18,        // client -> log: il QSO e' arrivato
};

QString typeName(quint8 type);

// PBKDF2-HMAC-SHA256, 200000 giri, 32 byte, sale fisso: la stessa chiave di
// DecoPort. Password vuota, chiave vuota.
QByteArray deriveKey(const QString& password);

// Un pacchetto firmato (con chiave vuota, senza firma: solo per le prove).
QByteArray buildPacket(quint8 type, quint32 streamId, quint32 sequence, const QByteArray& payload,
                       const QByteArray& key, qint64 unixMilliseconds);

enum class Verdict { Ok, Malformed, Unsigned, BadSignature, Stale };

struct Packet {
    quint8 type{0};
    quint32 streamId{0};
    quint32 sequence{0};
    qint64 unixSeconds{0};
    QByteArray payload;
};

// Controlla struttura, firma (a tempo costante) e ora (±30 s: una firma sola
// non ferma un pacchetto registrato e rimandato).
Verdict parsePacket(const QByteArray& datagram, const QByteArray& key, qint64 nowUnixSeconds, Packet* out);

// I campi dell'annuncio del log: una maschera, poi i presenti in ordine di bit.
//   bit 0 programma, bit 1 versione, bit 2 stazione (stringhe: lunghezza u8 + UTF-8), bit 3 porta della sessione (u16).
struct Announce {
    QString program;
    QString version;
    QString station;
    quint16 sessionPort{0};
};
QByteArray encodeAnnounce(const Announce& announce);
std::optional<Announce> decodeAnnounce(const QByteArray& payload);

// LogQso: i64 id del QSO nel log, u8 flags (bit 0: rimandato), poi il record ADIF in UTF-8.
QByteArray encodeQso(qint64 id, bool resent, const QByteArray& adif);
struct Qso {
    qint64 id{0};
    bool resent{false};
    QByteArray adif;
};
std::optional<Qso> decodeQso(const QByteArray& payload);

// LogAck: i64 id.
QByteArray encodeAck(qint64 id);
std::optional<qint64> decodeAck(const QByteArray& payload);

// Per il monitor del traffico: cosa dice un pacchetto del log.
QString summarize(quint8 type, const QByteArray& payload);

} // namespace decoportlog

class DecoPortLogServer : public QObject {
    Q_OBJECT

public:
    static constexpr int kClientTimeoutMs = 12000;
    static constexpr int kAnnounceMs = 2000;
    static constexpr int kRetryMs = 3000;
    static constexpr int kMaxTries = 3;
    // Cinque firme sbagliate da uno stesso mittente in 60 s: ignorato per 300 s.
    static constexpr int kBadLimit = 5;
    static constexpr int kBadWindowMs = 60000;
    static constexpr int kBanMs = 300000;

    explicit DecoPortLogServer(QObject* parent = nullptr);
    ~DecoPortLogServer() override;

    void setIdentity(const QString& version, const QString& station);
    // Dove si manda l'annuncio (la 5560 di DecoPort); 0, da nessuna parte.
    void setAnnouncePort(quint16 port) { m_announcePort = port; }
    // Per le prove: il tempo non ha minuti da aspettare.
    void setTimings(int announceMs, int retryMs, int timeoutMs);

    // La sessione su 127.0.0.1:port con la chiave della password. Senza
    // password non parte: DecoPort non ha una modalita' senza firma.
    bool start(quint16 port, const QString& password);
    void stop();
    bool isListening() const { return m_socket != nullptr; }
    quint16 port() const;
    QString lastError() const { return m_lastError; }

    int clientCount() const { return static_cast<int>(m_clients.size()); }
    int pendingCount() const { return static_cast<int>(m_pending.size()); }
    int sentCount() const { return m_sent; }
    int ackedCount() const { return m_acked; }

    // Il QSO a ogni client registrato; quanti sono, 0 se nessuno ascolta. Un
    // QSO senza conferma si prova tre volte in tutto; poi si lascia: HH ha la
    // sua coda solo per cio' che ha ricevuto.
    int publishQso(qint64 id, const QString& adif, bool resent = false);

signals:
    void clientsChanged();
    void qsoAcked(qint64 id);
    void qsoGaveUp(qint64 id);
    void diagnostic(const QString& line);

private:
    struct Client {
        QHostAddress address;
        quint16 port{0};
        qint64 lastSeenMs{0};
    };
    struct Pending {
        QString client;
        qint64 id{0};
        bool resent{false};
        QByteArray adif;
        qint64 sentMs{0};
        int tries{0};
    };
    struct Bad {
        QList<qint64> times;
        qint64 bannedUntilMs{0};
    };

    void readDatagrams();
    void tick();
    void announce(const QHostAddress& to, quint16 port);
    void sendTo(const Client& client, quint8 type, const QByteArray& payload);
    static QString keyOf(const QHostAddress& address, quint16 port);
    qint64 nowMs() const;

    QUdpSocket* m_socket{nullptr};
    QTimer m_timer;
    QByteArray m_key;
    QString m_version;
    QString m_station;
    QString m_lastError;
    quint32 m_streamId{0};
    quint16 m_announcePort{decoportlog::kAnnouncePort};
    quint32 m_seqAnnounce{0};
    quint32 m_seqQso{0};
    int m_announceMs{kAnnounceMs};
    int m_retryMs{kRetryMs};
    int m_timeoutMs{kClientTimeoutMs};
    qint64 m_lastAnnounceMs{0};
    QHash<QString, Client> m_clients;
    QList<Pending> m_pending;
    QHash<QString, Bad> m_bad;
    int m_sent{0};
    int m_acked{0};
};

} // namespace decolog::core
