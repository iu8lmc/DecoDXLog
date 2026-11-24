#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

namespace decolog::core { class LogDatabase;
namespace contesthandoff {
// A proposal is not a delivery receipt, a QSO, or a radio command.
struct Offer {
    QString id, scope, sender, recipient, call;
    qint64 frequencyHz{0};
    QString createdUtc;
};
struct Result { bool ok{false}; bool replayed{false}; QString error; };
QJsonObject offerFrame(const Offer& offer);
Result receiveOffer(LogDatabase& db,const QString& scope,const QString& recipient,const QString& envelopeSender,const QJsonObject& body);
QList<Offer> incomingOffers(const LogDatabase& db,const QString& scope,const QString& recipient,QString* error = nullptr);
}
}
