#include "core/CredentialStore.h"

#include <QSettings>

#ifdef DECODXLOG_HAVE_KEYCHAIN
#include <qt6keychain/keychain.h>
#endif

namespace decolog::core {

CredentialStore::CredentialStore(const QString& keychainService, QObject* parent)
    : QObject(parent)
    , m_keychainService(keychainService)
{
}

bool CredentialStore::compiledWithKeychain()
{
#ifdef DECODXLOG_HAVE_KEYCHAIN
    return true;
#else
    return false;
#endif
}

QString CredentialStore::backend() const
{
#ifndef DECODXLOG_HAVE_KEYCHAIN
    return tr("not available in this build");
#elif defined(Q_OS_WIN)
    return tr("Windows Credential Manager");
#elif defined(Q_OS_MACOS)
    return tr("macOS Keychain");
#else
    return tr("Secret Service (GNOME Keyring / KWallet)");
#endif
}

QList<CredentialService> CredentialStore::knownServices()
{
    return {
        {QStringLiteral("cloud"), QStringLiteral("DecoDXLog Cloud"), tr("Account"), tr("Token"),
         tr("Sync between devices (Phase 3)")},
        {QStringLiteral("qrz"), QStringLiteral("QRZ.com"), tr("Username"), tr("Password"),
         tr("Callbook lookups (XML subscription)")},
        {QStringLiteral("qrzlogbook"), QStringLiteral("QRZ Logbook"), tr("Callsign"), tr("API key"),
         tr("Upload and confirmations of the QRZ Logbook")},
        {QStringLiteral("lotw"), QStringLiteral("LoTW"), tr("Username"), tr("Password"),
         tr("Downloading confirmations; upload goes through the local TQSL")},
        {QStringLiteral("clublog"), QStringLiteral("Club Log"), tr("Email"), tr("App password"),
         tr("Real-time upload and OQRS")},
        {QStringLiteral("eqsl"), QStringLiteral("eQSL"), tr("Username"), tr("Password"),
         tr("Upload and eQSL confirmations")},
        {QStringLiteral("crx"), QStringLiteral("CRX Logbook"), tr("Callsign"), tr("API key"),
         tr("Upload to the CRX Logbook (crx.cloud): the key starts with HAM-")},
        {QStringLiteral("hrdlog"), QStringLiteral("HRDLog.net"), tr("Callsign"), tr("Upload code"),
         tr("Upload to HRDLog.net: the upload code is in your HRDLog profile, it is not the password")},
        {QStringLiteral("wavelog"), QStringLiteral("Wavelog"), tr("Site address"), tr("API key"),
         tr("Upload to your Wavelog (or Cloudlog): the address of the site and a read/write API key")},
        {QStringLiteral("on4kst"), QStringLiteral("ON4KST Chat"), tr("Callsign"), tr("Password"),
         tr("The VHF, EME and low band chat (www.on4kst.info)")},
        {QStringLiteral("hamqth"), QStringLiteral("HamQTH"), tr("Username"), tr("Password"),
         tr("Free callbook lookups")},
        {QStringLiteral("hamalert"), QStringLiteral("HamAlert"), tr("Username"), tr("Password"),
         tr("Spots from your HamAlert triggers (telnet)")},
        {QStringLiteral("hamhub"), QStringLiteral("HamHub HH"), tr("Callsign"), tr("Password"),
         tr("The QSOs go to HH, the HamHub Log bridge, over DecoPort. Type the same password in HH: nothing is sent without it")},
        {QStringLiteral("mail"), tr("Outgoing mailbox"), tr("Address"), tr("Password"),
         tr("Sending QSL cards by email. With Gmail it wants an app password, not the account one.")},
    };
}

QString CredentialStore::settingsKey(const QString& service, const char* field) const
{
    return QStringLiteral("credentials/%1/%2").arg(service, QLatin1String(field));
}

QString CredentialStore::account(const QString& service) const
{
    return QSettings().value(settingsKey(service, "account")).toString();
}

bool CredentialStore::hasSecret(const QString& service) const
{
    return QSettings().value(settingsKey(service, "stored"), false).toBool();
}

QStringList CredentialStore::profileServiceBases()
{
    return {QStringLiteral("qrzlogbook"), QStringLiteral("eqsl")};
}

QString CredentialStore::profileService(const QString& base, qint64 profileId)
{
    return profileId > 0 ? QStringLiteral("%1@%2").arg(base).arg(profileId) : base;
}

qint64 CredentialStore::profileOf(const QString& service)
{
    const qsizetype at = service.indexOf(QLatin1Char('@'));
    return at > 0 ? service.mid(at + 1).toLongLong() : 0;
}

QString CredentialStore::serviceFor(const QString& base, qint64 profileId) const
{
    const QString own = profileService(base, profileId);
    if (profileId > 0 && hasSecret(own))
        return own;
    return base;
}

QVariantList CredentialStore::profileServices(qint64 profileId) const
{
    QVariantList out;
    if (profileId <= 0)
        return out;
    for (const CredentialService& s : knownServices()) {
        if (!profileServiceBases().contains(s.id))
            continue;
        const QString id = profileService(s.id, profileId);
        out << QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("label"), s.label},
            {QStringLiteral("accountLabel"), s.accountLabel},
            {QStringLiteral("secretLabel"), s.secretLabel},
            {QStringLiteral("hint"), tr("Only for the QSOs of this profile; empty = the general account")},
            {QStringLiteral("account"), account(id)},
            {QStringLiteral("stored"), hasSecret(id)},
            {QStringLiteral("busy"), m_busy.value(id, false)},
            {QStringLiteral("error"), m_errors.value(id)},
        };
    }
    return out;
}

QVariantList CredentialStore::services() const
{
    QVariantList out;
    for (const CredentialService& s : knownServices()) {
        out << QVariantMap{
            {QStringLiteral("id"), s.id},
            {QStringLiteral("label"), s.label},
            {QStringLiteral("accountLabel"), s.accountLabel},
            {QStringLiteral("secretLabel"), s.secretLabel},
            {QStringLiteral("hint"), s.hint},
            {QStringLiteral("account"), account(s.id)},
            {QStringLiteral("stored"), hasSecret(s.id)},
            {QStringLiteral("busy"), m_busy.value(s.id, false)},
            {QStringLiteral("error"), m_errors.value(s.id)},
        };
    }
    return out;
}

void CredentialStore::setError(const QString& service, const QString& error)
{
    if (error.isEmpty())
        m_errors.remove(service);
    else
        m_errors.insert(service, error);
}

void CredentialStore::setBusy(const QString& service, bool busy)
{
    m_busy.insert(service, busy);
    emit changed();
}

void CredentialStore::save(const QString& service, const QString& accountName, const QString& secret)
{
    QSettings().setValue(settingsKey(service, "account"), accountName.trimmed());
    if (secret.isEmpty()) {
        setError(service, {});
        emit changed();
        emit finished(service, true, tr("Account saved"));
        return;
    }

#ifdef DECODXLOG_HAVE_KEYCHAIN
    auto* job = new QKeychain::WritePasswordJob(m_keychainService, this);
    job->setAutoDelete(true);
    job->setKey(service);
    job->setTextData(secret);
    connect(job, &QKeychain::Job::finished, this, [this, service, secret](QKeychain::Job* j) {
        const bool ok = j->error() == QKeychain::NoError;
        if (ok) {
            QSettings().setValue(settingsKey(service, "stored"), true);
            m_secretCache.insert(service, secret);
        } else {
            m_secretCache.remove(service);
        }
        setError(service, ok ? QString() : j->errorString());
        setBusy(service, false);
        emit finished(service, ok, ok ? tr("Stored in the system keystore") : j->errorString());
    });
    setBusy(service, true);
    job->start();
#else
    setError(service, tr("No system keystore in this build: the secret was not saved"));
    emit changed();
    emit finished(service, false, m_errors.value(service));
#endif
}

void CredentialStore::remove(const QString& service)
{
    m_secretCache.remove(service);

    QSettings s;
    s.remove(QStringLiteral("credentials/%1").arg(service));

#ifdef DECODXLOG_HAVE_KEYCHAIN
    auto* job = new QKeychain::DeletePasswordJob(m_keychainService, this);
    job->setAutoDelete(true);
    job->setKey(service);
    connect(job, &QKeychain::Job::finished, this, [this, service](QKeychain::Job* j) {
        // Un segreto che non c'era gia' e' comunque tolto.
        const bool ok = j->error() == QKeychain::NoError || j->error() == QKeychain::EntryNotFound;
        setError(service, ok ? QString() : j->errorString());
        setBusy(service, false);
        emit finished(service, ok, ok ? tr("Removed") : j->errorString());
    });
    setBusy(service, true);
    job->start();
#else
    setError(service, {});
    emit changed();
    emit finished(service, true, tr("Removed"));
#endif
}

void CredentialStore::readSecret(const QString& service,
                                 std::function<void(const QString&, const QString&)> done)
{
    if (m_secretCache.contains(service)) {
        done(m_secretCache.value(service), {});
        return;
    }

#ifdef DECODXLOG_HAVE_KEYCHAIN
    auto* job = new QKeychain::ReadPasswordJob(m_keychainService, this);
    job->setAutoDelete(true);
    job->setKey(service);
    connect(job, &QKeychain::Job::finished, this, [this, job, service, done](QKeychain::Job* j) {
        if (j->error() == QKeychain::NoError) {
            const QString secret = job->textData();
            if (!secret.isEmpty())
                m_secretCache.insert(service, secret);
            done(secret, {});
        } else {
            m_secretCache.remove(service);
            done({}, j->errorString());
        }
    });
    job->start();
#else
    Q_UNUSED(service);
    done({}, tr("No system keystore in this build"));
#endif
}

void CredentialStore::verify(const QString& service)
{
    setBusy(service, true);
    readSecret(service, [this, service](const QString& secret, const QString& error) {
        const bool ok = error.isEmpty() && !secret.isEmpty();
        // Il portachiavi fa fede: se il segreto non c'e' piu', non lo si dichiara.
        QSettings().setValue(settingsKey(service, "stored"), ok);
        setError(service, ok ? QString() : error);
        setBusy(service, false);
        emit finished(service, ok, ok ? tr("Secret present in the keystore") : error);
    });
}

} // namespace decolog::core
