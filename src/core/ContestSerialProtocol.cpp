#include "core/ContestSerialProtocol.h"

#include <cmath>
#include <limits>

namespace decolog::core::contestserial {
namespace {
bool key(const QString& value) { return !value.trimmed().isEmpty() && value.size()<=256; }
bool number(const QJsonValue& value,int& out)
{
    if (!value.isDouble()) return false;
    const double n=value.toDouble();
    if (!std::isfinite(n) || n<1 || n>std::numeric_limits<int>::max() || std::floor(n)!=n) return false;
    out=int(n); return true;
}
bool frame(const QJsonObject& body,QString& scope,QString& requester,QString& authority,QString& id,int& minimum)
{
    if (body.value(QStringLiteral("serialProtocol")).toInt(-1)!=1) return false;
    scope=body.value(QStringLiteral("scope")).toString(); requester=body.value(QStringLiteral("requester")).toString();
    authority=body.value(QStringLiteral("authority")).toString(); id=body.value(QStringLiteral("requestId")).toString();
    return key(scope) && key(requester) && key(authority) && key(id) && number(body.value(QStringLiteral("minimum")),minimum);
}
}

QJsonObject requestFrame(const ContestSerialRequest& r)
{
    if (!r.ok || r.state!=QLatin1String("pending") || !key(r.scope) || !key(r.requester) || !key(r.authority) || !key(r.requestId) || r.minimum<1) return {};
    return {{QStringLiteral("serialProtocol"),1},{QStringLiteral("scope"),r.scope},{QStringLiteral("requester"),r.requester},
            {QStringLiteral("authority"),r.authority},{QStringLiteral("requestId"),r.requestId},{QStringLiteral("minimum"),r.minimum}};
}

Reply handleRequest(LogDatabase& db,const QString& configuredScope,const QString& localAuthority,const QString& sender,const QJsonObject& body)
{
    Reply result; QString scope,requester,authority,id; int minimum=0;
    if (!frame(body,scope,requester,authority,id,minimum) || scope!=configuredScope || authority!=localAuthority || requester!=sender) {
        result.error=QStringLiteral("Invalid or misrouted serial request"); return result;
    }
    const auto reservation=db.reserveContestSerial(scope,requester,id,minimum);
    if (!reservation.ok) { result.error=reservation.error; return result; }
    result.ok=true; result.replayed=reservation.replayed;
    // Strip untrusted extras from the returned grant.
    result.body=QJsonObject{{QStringLiteral("serialProtocol"),1},{QStringLiteral("scope"),scope},{QStringLiteral("requester"),requester},
        {QStringLiteral("authority"),authority},{QStringLiteral("requestId"),id},{QStringLiteral("minimum"),minimum},{QStringLiteral("serial"),reservation.serial}};
    return result;
}

ContestSerialRequest acceptGrant(LogDatabase& db,const QString& configuredScope,const QString& localRequester,
                                 const QString& configuredAuthority,const QString& sender,const QJsonObject& body)
{
    ContestSerialRequest result; QString scope,requester,authority,id; int minimum=0,serial=0;
    if (!frame(body,scope,requester,authority,id,minimum) || !number(body.value(QStringLiteral("serial")),serial) ||
        scope!=configuredScope || requester!=localRequester || authority!=configuredAuthority || sender!=configuredAuthority) {
        result.error=QStringLiteral("Invalid or misrouted serial grant"); return result;
    }
    const auto stored=db.contestSerialRequest(scope,requester,id);
    if (!stored.ok || stored.authority!=authority || stored.minimum!=minimum) { result.error=QStringLiteral("Grant does not match the recorded request"); return result; }
    return db.acceptContestSerialGrant(scope,requester,id,authority,serial);
}
}
