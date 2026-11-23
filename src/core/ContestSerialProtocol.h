#pragma once

#include "core/LogDatabase.h"
#include <QJsonObject>

namespace decolog::core::contestserial {

// This contract validates routing fields but does not authenticate the UDP sender.
struct Reply { bool ok{false}; bool replayed{false}; QJsonObject body; QString error; };
QJsonObject requestFrame(const ContestSerialRequest& request);
Reply handleRequest(LogDatabase& authorityDb, const QString& configuredScope, const QString& localAuthority,
                    const QString& envelopeSender, const QJsonObject& body);
ContestSerialRequest acceptGrant(LogDatabase& requesterDb, const QString& configuredScope, const QString& localRequester,
                                 const QString& configuredAuthority, const QString& envelopeSender, const QJsonObject& body);
}
