// DecoDXLog — il protocollo UDP di WSJT-X, lato ricevente.
//
// Decodium lo eredita da WSJT-X (Network/NetworkMessage.hpp): magic 0xadbccbda,
// numero di schema, tipo, id del client, poi i campi in QDataStream. DecoDXLog
// legge solo quello che gli serve per tenere il log; tutti gli altri tipi
// (Decode, Clear, WSPRDecode...) vengono riconosciuti e scartati.
//
// Scritto da zero sulla specifica pubblica del formato: nessuna dipendenza dal
// codice di WSJT-X o di Decodium, cosi' funziona uguale con JTDX e MSHV.
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QTime>
#include <QString>
#include <optional>
#include <variant>

namespace decolog::core::wsjtx {

inline constexpr quint32 kMagic = 0xadbccbda;
inline constexpr quint32 kMaxSchema = 3;

enum class Type : quint32 {
    Heartbeat = 0,
    Status = 1,
    Decode = 2,
    Clear = 3,
    Reply = 4,
    QsoLogged = 5,
    Close = 6,
    Replay = 7,
    HaltTx = 8,
    FreeText = 9,
    WsprDecode = 10,
    Location = 11,
    LoggedAdif = 12,
};

struct Heartbeat {
    quint32 maxSchema{2};
    QString version;
    QString revision;
};

// Solo i campi che DecoDXLog mostra. I campi aggiunti nelle versioni successive
// del protocollo possono mancare: restano al valore predefinito.
struct Status {
    quint64 dialFrequencyHz{0};
    QString mode;
    QString dxCall;
    QString report;
    QString txMode;
    bool    txEnabled{false};
    bool    transmitting{false};
    bool    decoding{false};
    QString deCall;
    QString deGrid;
    QString dxGrid;
    QString submode;
    QString configurationName;
};

struct QsoLogged {
    QDateTime timeOff;
    QString   dxCall;
    QString   dxGrid;
    quint64   txFrequencyHz{0};
    QString   mode;
    QString   reportSent;
    QString   reportReceived;
    QString   txPower;
    QString   comments;
    QString   name;
    QDateTime timeOn;
    QString   operatorCall;
    QString   myCall;
    QString   myGrid;
    QString   exchangeSent;
    QString   exchangeReceived;
    QString   propagationMode;
};

// Una riga decodificata: serve a sapere chi si sente, e da dove.
struct Decode {
    bool    isNew{true};
    QTime   time;
    qint32  snr{0};
    double  deltaTime{0.0};
    quint32 deltaFrequency{0};
    QString mode;
    QString message;
};

struct LoggedAdif {
    QByteArray adif;
};

struct Close {};

struct Other {
    quint32 type{0};
};

using Payload = std::variant<Heartbeat, Status, QsoLogged, LoggedAdif, Close, Other, Decode>;

struct Message {
    quint32 schema{0};
    QString clientId;   // "WSJT-X", "Decodium", "JTDX"... piu' l'eventuale nome di configurazione
    Payload payload;
};

// nullopt se il datagramma non e' un messaggio del protocollo o e' troncato
// prima dei campi obbligatori.
std::optional<Message> parse(const QByteArray& datagram);

// Il verso opposto, per gli strumenti di prova e i test: costruisce i datagrammi
// come li manda un client.
QByteArray buildHeartbeat(const QString& clientId, const Heartbeat& hb, quint32 schema = kMaxSchema);
QByteArray buildQsoLogged(const QString& clientId, const QsoLogged& qso, quint32 schema = kMaxSchema);
QByteArray buildLoggedAdif(const QString& clientId, const QByteArray& adif, quint32 schema = kMaxSchema);
QByteArray buildStatus(const QString& clientId, const Status& status, quint32 schema = kMaxSchema);
QByteArray buildDecode(const QString& clientId, const Decode& decode, quint32 schema = kMaxSchema);

} // namespace decolog::core::wsjtx
