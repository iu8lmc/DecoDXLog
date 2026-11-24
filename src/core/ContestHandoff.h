#pragma once

#include <QJsonObject>
#include <QDateTime>
#include <QList>
#include <QString>

namespace decolog::core { class LogDatabase;
namespace contesthandoff {
// A proposal is not a delivery receipt, a QSO, or a radio command.
struct Offer {
    QString id, scope, sender, recipient, call;
    qint64 frequencyHz{0};
    QString createdUtc;
    QString state{};
    QString scheduledUtc{};
};
struct Result { bool ok{false}; bool replayed{false}; QString error; };
struct HistoryEntry { QString previous, state, atUtc; };
QJsonObject offerFrame(const Offer& offer);
Result receiveOffer(LogDatabase& db,const QString& scope,const QString& recipient,const QString& envelopeSender,const QJsonObject& body);
Result storeOutgoing(LogDatabase& db,const Offer& offer);
Result setIncomingState(LogDatabase& db,const QString& scope,const QString& recipient,const QString& id,const QString& state);
Result receiveRemoteState(LogDatabase& db,const QString& scope,const QString& sender,const QString& envelopeSender,const QJsonObject& frame,const QString& state);
Result claimSkedReminder(LogDatabase& db,const QString& scope,const QString& recipient,const QString& id,const QDateTime& nowUtc);
Result claimSkedReminderSnapshot(LogDatabase& db,const QString& scope,const QString& recipient,const Offer& offer,const QDateTime& nowUtc);
QList<Offer> incomingOffers(const LogDatabase& db,const QString& scope,const QString& recipient,QString* error = nullptr);
QList<Offer> outgoingOffers(const LogDatabase& db,const QString& scope,const QString& sender,QString* error = nullptr);
QList<HistoryEntry> history(const LogDatabase& db,const QString& scope,const QString& node,const QString& id,bool outgoing,QString* error = nullptr);
}
}
