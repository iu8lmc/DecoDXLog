#include "core/WsjtxProtocol.h"

#include <QDataStream>
#include <QIODevice>

namespace decolog::core::wsjtx {

namespace {

// Lo schema stabilisce la versione di QDataStream: cambia la codifica di
// QDateTime, quindi va impostata prima di leggere qualunque campo data.
void applySchema(QDataStream& s, quint32 schema)
{
    if (schema <= 1)
        s.setVersion(QDataStream::Qt_5_0);
    else if (schema == 2)
        s.setVersion(QDataStream::Qt_5_2);
    else
        s.setVersion(QDataStream::Qt_5_4);
}

// "utf8" del protocollo: QByteArray serializzato (lunghezza + byte).
QString readUtf8(QDataStream& s)
{
    QByteArray raw;
    s >> raw;
    return QString::fromUtf8(raw);
}

void writeUtf8(QDataStream& s, const QString& value)
{
    s << value.toUtf8();
}

bool ok(const QDataStream& s) { return s.status() == QDataStream::Ok; }

QDataStream& begin(QDataStream& s, Type type, const QString& clientId, quint32 schema)
{
    applySchema(s, schema);
    s << kMagic << schema << static_cast<quint32>(type);
    writeUtf8(s, clientId);
    return s;
}

} // namespace

std::optional<Message> parse(const QByteArray& datagram)
{
    QDataStream in(datagram);
    in.setByteOrder(QDataStream::BigEndian);

    quint32 magic = 0;
    quint32 schema = 0;
    in >> magic >> schema;
    if (!ok(in) || magic != kMagic || schema == 0)
        return std::nullopt;
    applySchema(in, schema);

    quint32 type = 0;
    in >> type;
    Message msg;
    msg.schema = schema;
    msg.clientId = readUtf8(in);
    if (!ok(in))
        return std::nullopt;

    switch (static_cast<Type>(type)) {
    case Type::Heartbeat: {
        Heartbeat hb;
        in >> hb.maxSchema;
        if (!ok(in)) {
            // Prima dello schema 3 il campo non esisteva.
            msg.payload = Heartbeat{};
            return msg;
        }
        hb.version = readUtf8(in);
        hb.revision = readUtf8(in);
        msg.payload = hb;
        return msg;
    }
    case Type::Status: {
        Status st;
        in >> st.dialFrequencyHz;
        st.mode = readUtf8(in);
        st.dxCall = readUtf8(in);
        st.report = readUtf8(in);
        st.txMode = readUtf8(in);
        in >> st.txEnabled >> st.transmitting >> st.decoding;
        if (!ok(in))
            return std::nullopt;
        quint32 rxDf = 0, txDf = 0;
        in >> rxDf >> txDf;
        st.deCall = readUtf8(in);
        st.deGrid = readUtf8(in);
        st.dxGrid = readUtf8(in);
        bool watchdog = false;
        in >> watchdog;
        st.submode = readUtf8(in);
        bool fast = false;
        quint8 special = 0;
        quint32 tolerance = 0, period = 0;
        in >> fast >> special >> tolerance >> period;
        st.configurationName = readUtf8(in);
        // I campi finali sono opzionali: quel che si e' letto resta buono.
        msg.payload = st;
        return msg;
    }
    case Type::QsoLogged: {
        QsoLogged q;
        in >> q.timeOff;
        q.dxCall = readUtf8(in);
        q.dxGrid = readUtf8(in);
        in >> q.txFrequencyHz;
        q.mode = readUtf8(in);
        q.reportSent = readUtf8(in);
        q.reportReceived = readUtf8(in);
        q.txPower = readUtf8(in);
        q.comments = readUtf8(in);
        q.name = readUtf8(in);
        in >> q.timeOn;
        if (!ok(in) || q.dxCall.isEmpty())
            return std::nullopt;
        q.operatorCall = readUtf8(in);
        q.myCall = readUtf8(in);
        q.myGrid = readUtf8(in);
        q.exchangeSent = readUtf8(in);
        q.exchangeReceived = readUtf8(in);
        q.propagationMode = readUtf8(in);
        msg.payload = q;
        return msg;
    }
    case Type::LoggedAdif: {
        LoggedAdif la;
        in >> la.adif;
        if (!ok(in))
            return std::nullopt;
        msg.payload = la;
        return msg;
    }
    case Type::Close:
        msg.payload = Close{};
        return msg;
    case Type::Decode: {
        Decode d;
        in >> d.isNew >> d.time >> d.snr >> d.deltaTime >> d.deltaFrequency;
        d.mode = readUtf8(in);
        d.message = readUtf8(in);
        if (!ok(in))
            return std::nullopt;
        msg.payload = d;
        return msg;
    }
    default:
        msg.payload = Other{type};
        return msg;
    }
}

QByteArray buildHeartbeat(const QString& clientId, const Heartbeat& hb, quint32 schema)
{
    QByteArray buffer;
    QDataStream s(&buffer, QIODevice::WriteOnly);
    begin(s, Type::Heartbeat, clientId, schema);
    s << hb.maxSchema;
    writeUtf8(s, hb.version);
    writeUtf8(s, hb.revision);
    return buffer;
}

QByteArray buildStatus(const QString& clientId, const Status& st, quint32 schema)
{
    QByteArray buffer;
    QDataStream s(&buffer, QIODevice::WriteOnly);
    begin(s, Type::Status, clientId, schema);
    s << st.dialFrequencyHz;
    writeUtf8(s, st.mode);
    writeUtf8(s, st.dxCall);
    writeUtf8(s, st.report);
    writeUtf8(s, st.txMode);
    s << st.txEnabled << st.transmitting << st.decoding << quint32{0} << quint32{0};
    writeUtf8(s, st.deCall);
    writeUtf8(s, st.deGrid);
    writeUtf8(s, st.dxGrid);
    s << false;
    writeUtf8(s, st.submode);
    s << false << quint8{0} << quint32{0} << quint32{0};
    writeUtf8(s, st.configurationName);
    writeUtf8(s, QString());
    return buffer;
}

QByteArray buildDecode(const QString& clientId, const Decode& d, quint32 schema)
{
    QByteArray buffer;
    QDataStream s(&buffer, QIODevice::WriteOnly);
    begin(s, Type::Decode, clientId, schema);
    s << d.isNew << d.time << d.snr << d.deltaTime << d.deltaFrequency;
    writeUtf8(s, d.mode);
    writeUtf8(s, d.message);
    s << false << false;   // low confidence, off air
    return buffer;
}

QByteArray buildQsoLogged(const QString& clientId, const QsoLogged& q, quint32 schema)
{
    QByteArray buffer;
    QDataStream s(&buffer, QIODevice::WriteOnly);
    begin(s, Type::QsoLogged, clientId, schema);
    s << q.timeOff;
    writeUtf8(s, q.dxCall);
    writeUtf8(s, q.dxGrid);
    s << q.txFrequencyHz;
    writeUtf8(s, q.mode);
    writeUtf8(s, q.reportSent);
    writeUtf8(s, q.reportReceived);
    writeUtf8(s, q.txPower);
    writeUtf8(s, q.comments);
    writeUtf8(s, q.name);
    s << q.timeOn;
    writeUtf8(s, q.operatorCall);
    writeUtf8(s, q.myCall);
    writeUtf8(s, q.myGrid);
    writeUtf8(s, q.exchangeSent);
    writeUtf8(s, q.exchangeReceived);
    writeUtf8(s, q.propagationMode);
    return buffer;
}

QByteArray buildLoggedAdif(const QString& clientId, const QByteArray& adif, quint32 schema)
{
    QByteArray buffer;
    QDataStream s(&buffer, QIODevice::WriteOnly);
    begin(s, Type::LoggedAdif, clientId, schema);
    s << adif;
    return buffer;
}

} // namespace decolog::core::wsjtx
