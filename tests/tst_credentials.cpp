// Credenziali: il segreto va nel portachiavi e torna uguale, nel file delle
// impostazioni resta solo il nome utente.
//
// Usa il portachiavi vero dell'utente, sotto un nome di servizio proprio del
// test, e alla fine cancella quello che ha scritto. Dove il portachiavi non c'e'
// (CI Linux senza Secret Service) il test si salta, non fallisce.
#include "core/CredentialStore.h"

#include <QCoreApplication>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTest>

using namespace decolog::core;

class TestCredentials : public QObject {
    Q_OBJECT

    CredentialStore* m_store{nullptr};
    const QString m_secret = QStringLiteral("s3cr3t-äöü-%1").arg(QCoreApplication::applicationPid());

    bool waitFinished(QSignalSpy& spy) { return spy.count() > 0 || spy.wait(10000); }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("Decodium"));
        QCoreApplication::setApplicationName(QStringLiteral("DecoDXLog-test"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings().clear();
        m_store = new CredentialStore(QStringLiteral("DecoDXLog-test-%1").arg(QCoreApplication::applicationPid()), this);
    }

    void cleanupTestCase()
    {
        if (m_store && m_store->available()) {
            QSignalSpy spy(m_store, &CredentialStore::finished);
            m_store->remove(QStringLiteral("qrz"));
            waitFinished(spy);
        }
        const QString file = QSettings().fileName();
        QSettings().clear();
        QFile::remove(file);
    }

    void knownServices()
    {
        const auto services = CredentialStore::knownServices();
        QStringList ids;
        for (const auto& s : services)
            ids << s.id;
        for (const char* id : {"cloud", "qrz", "lotw", "clublog", "eqsl"})
            QVERIFY2(ids.contains(QLatin1String(id)), id);
    }

    void profileAccounts()
    {
        // Un profilo con il suo account QRZ Logbook: i suoi QSO usano quello,
        // gli altri profili quello generale.
        QCOMPARE(CredentialStore::profileService(QStringLiteral("qrzlogbook"), 3), QString("qrzlogbook@3"));
        QCOMPARE(CredentialStore::profileService(QStringLiteral("qrzlogbook"), 0), QString("qrzlogbook"));
        QCOMPARE(CredentialStore::profileOf(QStringLiteral("eqsl@12")), qint64(12));
        QCOMPARE(CredentialStore::profileOf(QStringLiteral("eqsl")), qint64(0));
        QVERIFY(CredentialStore::profileServiceBases().contains(QStringLiteral("eqsl")));

        // "stored" e' quello che dice il file delle impostazioni: qui lo si scrive
        // a mano, senza passare dal portachiavi.
        QSettings().setValue(QStringLiteral("credentials/qrzlogbook@3/stored"), true);
        QSettings().setValue(QStringLiteral("credentials/qrzlogbook@3/account"), QStringLiteral("II8XYZ"));
        QCOMPARE(m_store->serviceFor(QStringLiteral("qrzlogbook"), 3), QString("qrzlogbook@3"));
        QCOMPARE(m_store->serviceFor(QStringLiteral("qrzlogbook"), 4), QString("qrzlogbook"));
        QCOMPARE(m_store->serviceFor(QStringLiteral("qrzlogbook"), 0), QString("qrzlogbook"));

        const QVariantList rows = m_store->profileServices(3);
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows.at(0).toMap().value("id").toString(), QString("qrzlogbook@3"));
        QCOMPARE(rows.at(0).toMap().value("account").toString(), QString("II8XYZ"));
        QVERIFY(rows.at(0).toMap().value("stored").toBool());
        QCOMPARE(rows.at(1).toMap().value("id").toString(), QString("eqsl@3"));
        QVERIFY(!rows.at(1).toMap().value("stored").toBool());
        QVERIFY(m_store->profileServices(0).isEmpty());
        QSettings().remove(QStringLiteral("credentials/qrzlogbook@3"));
    }

    void pendingClubLogReplacementCannotReadAnOldToken()
    {
        QSettings settings;
        settings.setValue(QStringLiteral("credentials/clublog/stored"), true);
        settings.setValue(QStringLiteral("credentials/clublog/replacementPending"), true);
        settings.sync();
        QVERIFY(!m_store->hasSecret(QStringLiteral("clublog")));
        bool done = false;
        QString value, error;
        m_store->readSecret(QStringLiteral("clublog"), [&](const QString& secret, const QString& failure) {
            value = secret; error = failure; done = true;
        });
        QVERIFY(done);
        QVERIFY(value.isEmpty());
        QVERIFY(!error.isEmpty());
        settings.remove(QStringLiteral("credentials/clublog"));
    }

    void overlappingClubLogChangesAreRejected()
    {
        const QString service = QStringLiteral("clublog");
        QSettings settings;
        settings.setValue(QStringLiteral("credentials/clublog/account"), QStringLiteral("old-account"));
        settings.setValue(QStringLiteral("credentials/clublog/stored"), true);
        settings.sync();
        m_store->setBusy(service, true);
        QSignalSpy spy(m_store, &CredentialStore::finished);
        m_store->save(service, QStringLiteral("new-account"), QStringLiteral("cl_fixture"));
        QCOMPARE(spy.size(), 1);
        QVERIFY(!spy.takeFirst().at(1).toBool());
        m_store->remove(service);
        QCOMPARE(spy.size(), 1);
        QVERIFY(!spy.takeFirst().at(1).toBool());
        QCOMPARE(m_store->account(service), QStringLiteral("old-account"));
        QVERIFY(m_store->hasSecret(service));
        QVERIFY(!settings.value(QStringLiteral("credentials/clublog/replacementPending")).toBool());
        m_store->setBusy(service, false);
        settings.remove(QStringLiteral("credentials/clublog"));
    }

    void removedClubLogCredentialCannotBeReadFromCache()
    {
        const QString service = QStringLiteral("clublog");
        QSettings settings;
        settings.remove(QStringLiteral("credentials/clublog"));
        settings.sync();
        m_store->m_secretCache.insert(service, QStringLiteral("cl_old_fixture"));
        bool done = false;
        m_store->readSecret(service, [&](const QString& secret, const QString& error) {
            QVERIFY(secret.isEmpty());
            QVERIFY(!error.isEmpty());
            done = true;
        });
        QVERIFY(done);
        m_store->m_secretCache.remove(service);
    }

    void deletingClubLogCredentialBlocksReadsBeforeKeystoreCompletes()
    {
        const QString service = QStringLiteral("clublog");
        QSettings settings;
        settings.setValue(QStringLiteral("credentials/clublog/stored"), true);
        settings.sync();
        CredentialStore isolated(QStringLiteral("DecoDXLog-test-delete-%1").arg(QCoreApplication::applicationPid()));
        isolated.m_secretCache.insert(service, QStringLiteral("cl_old_fixture"));
        isolated.remove(service);
        settings.sync();
        QVERIFY(settings.value(QStringLiteral("credentials/clublog/replacementPending")).toBool());
        QVERIFY(!isolated.hasSecret(service));
        bool done = false;
        isolated.readSecret(service, [&](const QString& secret, const QString& error) {
            QVERIFY(secret.isEmpty());
            QVERIFY(!error.isEmpty());
            done = true;
        });
        QVERIFY(done);
        settings.remove(QStringLiteral("credentials/clublog"));
        settings.sync();
    }

    void saveReadRemove()
    {
        if (!m_store->available())
            QSKIP("built without qtkeychain");

        QSignalSpy spy(m_store, &CredentialStore::finished);
        m_store->save(QStringLiteral("qrz"), QStringLiteral(" IU8LMC "), m_secret);
        if (!waitFinished(spy))
            QSKIP("no response from the system keystore");
        if (!spy.first().at(1).toBool())
            QSKIP(qPrintable("no usable system keystore: " + spy.first().at(2).toString()));

        QCOMPARE(m_store->account(QStringLiteral("qrz")), QString("IU8LMC"));
        QVERIFY(m_store->hasSecret(QStringLiteral("qrz")));

        // Nel file delle impostazioni il segreto non c'e'.
        QSettings().sync();
        QFile ini(QSettings().fileName());
        QVERIFY(ini.open(QIODevice::ReadOnly));
        QVERIFY(!QString::fromUtf8(ini.readAll()).contains(QStringLiteral("s3cr3t")));

        QString read, error;
        bool done = false;
        m_store->readSecret(QStringLiteral("qrz"), [&](const QString& s, const QString& e) {
            read = s;
            error = e;
            done = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(read, m_secret);

        // Un segreto vuoto cambia l'account e lascia il segreto.
        spy.clear();
        m_store->save(QStringLiteral("qrz"), QStringLiteral("IU8LMC/P"), QString());
        QVERIFY(waitFinished(spy));
        QCOMPARE(m_store->account(QStringLiteral("qrz")), QString("IU8LMC/P"));
        QVERIFY(m_store->hasSecret(QStringLiteral("qrz")));

        spy.clear();
        m_store->remove(QStringLiteral("qrz"));
        QVERIFY(waitFinished(spy));
        QVERIFY(spy.first().at(1).toBool());
        QVERIFY(!m_store->hasSecret(QStringLiteral("qrz")));
        QVERIFY(m_store->account(QStringLiteral("qrz")).isEmpty());

        // Dopo la rimozione verify() non lo dichiara piu'.
        spy.clear();
        m_store->verify(QStringLiteral("qrz"));
        QVERIFY(waitFinished(spy));
        QVERIFY(!spy.first().at(1).toBool());
        QVERIFY(!m_store->hasSecret(QStringLiteral("qrz")));
    }
};

QTEST_GUILESS_MAIN(TestCredentials)
#include "tst_credentials.moc"
