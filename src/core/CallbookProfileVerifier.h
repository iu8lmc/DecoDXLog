#pragma once

#include "core/Callbook.h"
#include <QObject>
#include <QTimer>
#include <QDateTime>
#include <QHash>

namespace decolog::core {

// A positive response from the selected callbook is evidence of its entry.
// Authentication, networking and inconclusive results remain unknown.
class CallbookProfileVerifier : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString call READ call NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed)
public:
    explicit CallbookProfileVerifier(QObject* parent = nullptr);
    explicit CallbookProfileVerifier(CallbookClient::Provider provider, QObject* parent = nullptr);
    QString call() const { return m_call; }
    QString state() const { return m_state; }

    // shouldLookup=false allows an already-running callbook lookup to be
    // reused via observeVerified/observeUnverified, without a duplicate query.
    void setCall(const QString& call, bool shouldLookup = true);
    void observeVerified(const QString& requestedCall, const QString& returnedCall);
    void observeUnverified(const QString& requestedCall);
    void setCredentialReaders(CallbookClient::AccountReader accounts, CallbookClient::SecretReader secrets);
    void invalidateCredentials();
    void setEndpoint(const QUrl& url);  // test endpoint; empty restores default
    void setDebounceInterval(int milliseconds) { m_delay.setInterval(milliseconds); }

signals:
    void changed();

private:
    void resetClient();
    void setState(const QString& state);
    void begin();
    static QString normalize(const QString& call);
    static bool valid(const QString& call);

    CallbookClient* m_client{nullptr};
    CallbookClient::Provider m_provider{CallbookClient::Provider::Qrz};
    CallbookClient::AccountReader m_accounts;
    CallbookClient::SecretReader m_secrets;
    QUrl m_endpoint;
    QTimer m_delay;
    QString m_call;
    QString m_state{QStringLiteral("idle")};
    QHash<QString, QDateTime> m_verified;
    bool m_shouldLookup{true};
};

} // namespace decolog::core
