#include "core/CallbookProfileVerifier.h"

#include <QPointer>
#include <QRegularExpression>

namespace decolog::core {

CallbookProfileVerifier::CallbookProfileVerifier(QObject* parent)
    : CallbookProfileVerifier(CallbookClient::Provider::Qrz, parent)
{
}

CallbookProfileVerifier::CallbookProfileVerifier(CallbookClient::Provider provider, QObject* parent)
    : QObject(parent), m_provider(provider)
{
    m_delay.setSingleShot(true);
    m_delay.setInterval(350);
    connect(&m_delay, &QTimer::timeout, this, &CallbookProfileVerifier::begin);
    resetClient();
}

QString CallbookProfileVerifier::normalize(const QString& call)
{
    return call.trimmed().toUpper();
}

bool CallbookProfileVerifier::valid(const QString& call)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Z0-9]+(?:/[A-Z0-9]+)*$"));
    return call.size() >= 3 && call.size() <= 30 && pattern.match(call).hasMatch()
        && call.contains(QRegularExpression(QStringLiteral("[A-Z]")))
        && call.contains(QRegularExpression(QStringLiteral("[0-9]")));
}

void CallbookProfileVerifier::setState(const QString& state)
{
    if (m_state == state) return;
    m_state = state;
    emit changed();
}

void CallbookProfileVerifier::setCall(const QString& call, bool shouldLookup)
{
    const QString next = normalize(call);
    if (next == m_call && shouldLookup == m_shouldLookup) return;
    m_delay.stop();
    m_call = next;
    m_shouldLookup = shouldLookup;
    const auto cached = m_verified.constFind(m_call);
    const bool known = cached != m_verified.constEnd() && cached->secsTo(QDateTime::currentDateTimeUtc()) < 86400;
    m_state = !valid(m_call) ? QStringLiteral("idle") : known ? QStringLiteral("available") : QStringLiteral("checking");
    emit changed();
    if (valid(m_call) && m_shouldLookup && !known) m_delay.start();
}

void CallbookProfileVerifier::observeVerified(const QString& requestedCall, const QString& returnedCall)
{
    if (!valid(m_call) || normalize(requestedCall) != m_call || normalize(returnedCall) != m_call)
        return;
    if (m_verified.size() >= 500 && !m_verified.contains(m_call)) m_verified.clear();
    m_verified.insert(m_call, QDateTime::currentDateTimeUtc());
    m_delay.stop();
    setState(QStringLiteral("available"));
}

void CallbookProfileVerifier::observeUnverified(const QString& requestedCall)
{
    if (normalize(requestedCall) != m_call || m_state == QLatin1String("available")) return;
    m_delay.stop();
    setState(QStringLiteral("unknown"));
}

void CallbookProfileVerifier::begin()
{
    if (m_provider == CallbookClient::Provider::None) {
        setState(QStringLiteral("unknown"));
    } else if (m_client && valid(m_call) && m_shouldLookup)
        m_client->lookup(m_call);
}

void CallbookProfileVerifier::setCredentialReaders(CallbookClient::AccountReader accounts,
                                               CallbookClient::SecretReader secrets)
{
    m_accounts = std::move(accounts);
    m_secrets = std::move(secrets);
    invalidateCredentials();
}

void CallbookProfileVerifier::invalidateCredentials()
{
    m_delay.stop();
    m_verified.clear();
    resetClient();
    if (!valid(m_call)) setState(QStringLiteral("idle"));
    else if (m_shouldLookup) {
        setState(QStringLiteral("checking"));
        m_delay.start();
    } else setState(QStringLiteral("unknown"));
}

void CallbookProfileVerifier::setEndpoint(const QUrl& url)
{
    m_endpoint = url;
    invalidateCredentials();
}

void CallbookProfileVerifier::resetClient()
{
    if (m_client) {
        disconnect(m_client, nullptr, this, nullptr);
        m_client->deleteLater();
    }
    auto* client = new CallbookClient(this);
    m_client = client;
    client->setProvider(m_provider);
    client->setFallbackEnabled(false);
    if (!m_endpoint.isEmpty())
        client->setEndpoints(m_provider == CallbookClient::Provider::Qrz ? m_endpoint : QUrl(),
                             m_provider == CallbookClient::Provider::HamQth ? m_endpoint : QUrl());
    const QPointer<CallbookClient> guard(client);
    const QPointer<CallbookProfileVerifier> owner(this);
    const auto accounts = m_accounts;
    const auto secrets = m_secrets;
    client->setCredentialReaders(accounts, [guard, owner, secrets](const QString& service, auto done) {
        if (!secrets) { if (guard && owner && owner->m_client == guard) done({}, QStringLiteral("no credentials")); return; }
        secrets(service, [guard, owner, done = std::move(done)](const QString& secret, const QString& error) {
            if (guard && owner && owner->m_client == guard) done(secret, error);
        });
    });
    connect(client, &CallbookClient::found, this, [this, client](const QString& requested, const CallbookRecord& record) {
        if (client != m_client) return;
        const QString expectedSource = m_provider == CallbookClient::Provider::Qrz
            ? QStringLiteral("QRZ.com") : QStringLiteral("HamQTH");
        if (record.source == expectedSource) observeVerified(requested, record.call);
        if (normalize(requested) == m_call
            && (record.source != expectedSource || normalize(record.call) != m_call))
            observeUnverified(requested);
    });
    connect(client, &CallbookClient::failed, this, [this, client](const QString& requested, const QString&) {
        if (client == m_client) observeUnverified(requested);
    });
}

} // namespace decolog::core
