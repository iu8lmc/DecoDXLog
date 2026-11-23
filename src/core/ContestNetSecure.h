#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace decolog::core::contestsecure {
struct Sealed { bool ok{false}; QJsonObject frame; QString error; };
struct Opened { bool ok{false}; QString type; QJsonObject body; QString error; };
// key must be 32 secret random bytes. Nonces are generated per datagram.
// This authenticates key possession, not the identity of a particular holder.
Sealed seal(const QByteArray& key,const QString& group,const QString& sender,const QString& recipient,
            const QString& type,const QJsonObject& body);
Opened open(const QByteArray& key,const QString& group,const QString& sender,const QString& recipient,const QJsonObject& frame);
}
