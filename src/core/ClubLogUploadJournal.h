#pragma once

#include "core/Adif.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QSettings>
#include <QSet>
#include <QStringList>
#include <QUuid>
#include <functional>

namespace decolog::core {

// Durable, non-secret checkpoint: a crash after submission must never cause a
// blind automatic resend. The caller supplies the logbook's own identity so
// records from different local databases cannot be mixed.
class ClubLogUploadJournal {
public:
    explicit ClubLogUploadJournal(QSettings& settings) : m_settings(settings) {}

    // Export headers include a creation timestamp and program version. They
    // are not part of the contacts and change after restart or an upgrade.
    static QString recordsFingerprint(const QByteArray& adif, int expectedCount)
    {
        AdifDocument document = adif::parse(adif);
        if (expectedCount <= 0 || document.records.size() != expectedCount) return {};
        document.header = AdifRecord();
        // The remote result can be committed immediately before a crash. Its
        // local upload flags may then differ when the checkpoint is resumed.
        for (AdifRecord& record : document.records) {
            record.remove(QStringLiteral("CLUBLOG_QSO_UPLOAD_STATUS"));
            record.remove(QStringLiteral("CLUBLOG_QSO_UPLOAD_DATE"));
        }
        return QString::fromLatin1(QCryptographicHash::hash(adif::writeDocument(document), QCryptographicHash::Sha256).toHex());
    }

    bool pending() const { return m_settings.value(key("phase")).toString() != QString(); }
    QString phase() const { return m_settings.value(key("phase")).toString(); }
    QString uploadId() const { return m_settings.value(key("uploadId")).toString(); }
    QString station() const { return m_settings.value(key("station")).toString(); }
    QString logIdentity() const { return m_settings.value(key("logIdentity")).toString(); }
    QStringList localIds() const { return m_settings.value(key("localIds")).toStringList(); }
    QString payloadSha256() const { return m_settings.value(key("payloadSha256")).toString(); }
    QString nonce() const { return m_settings.value(key("nonce")).toString(); }

    QSet<QString> reservedIds(const QString& logIdentity) const
    {
        QSet<QString> ids;
        if (pending() && this->logIdentity() == logIdentity)
            for (const QString& id : localIds()) ids.insert(id);
        m_settings.beginGroup(QStringLiteral("clubLogRestPending"));
        const QStringList groups = m_settings.childGroups();
        for (const QString& group : groups) {
            m_settings.beginGroup(group);
            if (m_settings.value(QStringLiteral("logIdentity")).toString() == logIdentity)
                for (const QString& id : m_settings.value(QStringLiteral("localIds")).toStringList()) ids.insert(id);
            m_settings.endGroup();
        }
        m_settings.endGroup();
        return ids;
    }

    bool begin(const QString& logIdentity, const QString& station,
               const QStringList& ids, const QString& sha256)
    {
        if (logIdentity.isEmpty() || station.isEmpty() || ids.isEmpty() || sha256.isEmpty()) return false;
        if (!QDir().mkpath(QFileInfo(m_settings.fileName()).absolutePath())) return false;
        QLockFile lock(m_settings.fileName() + QStringLiteral(".clublog-submit.lock"));
        if (!lock.tryLock(0)) return false;
        // Refresh after acquiring the cross-process lock; another instance may
        // have written a checkpoint since this QSettings object was created.
        m_settings.sync();
        if (m_settings.status() != QSettings::NoError) return false;
        const QSet<QString> reserved = reservedIds(logIdentity);
        for (const QString& id : ids) if (reserved.contains(id)) return false;
        if (pending()) {
            QString previous = nonce();
            if (previous.isEmpty()) {
                previous = QUuid::createUuid().toString(QUuid::WithoutBraces);
                m_settings.setValue(key("nonce"), previous);
            }
            const QString destination = QStringLiteral("clubLogRestPending/") + previous;
            if (m_settings.contains(destination + QStringLiteral("/phase"))) return false;
            for (const char* field : {"phase", "uploadId", "station", "logIdentity", "localIds", "payloadSha256", "nonce"})
                if (m_settings.contains(key(field))) m_settings.setValue(destination + QLatin1Char('/') + QLatin1String(field), m_settings.value(key(field)));
            m_settings.remove(QStringLiteral("clubLogRestUpload"));
        }
        m_settings.setValue(key("logIdentity"), logIdentity);
        m_settings.setValue(key("station"), station);
        m_settings.setValue(key("localIds"), ids);
        m_settings.setValue(key("payloadSha256"), sha256);
        m_settings.setValue(key("nonce"), QUuid::createUuid().toString(QUuid::WithoutBraces));
        m_settings.setValue(key("phase"), QStringLiteral("submitting"));
        m_settings.sync();
        return m_settings.status() == QSettings::NoError;
    }
    bool queued(const QString& id, const QString& expectedNonce)
    {
        QLockFile lock(m_settings.fileName() + QStringLiteral(".clublog-submit.lock"));
        if (!lock.tryLock(0)) return false;
        m_settings.sync();
        if (m_settings.status() != QSettings::NoError || nonce() != expectedNonce
            || phase() != QLatin1String("submitting") || id.isEmpty()) return false;
        m_settings.setValue(key("uploadId"), id);
        m_settings.setValue(key("phase"), QStringLiteral("queued"));
        m_settings.sync();
        return m_settings.status() == QSettings::NoError;
    }
    bool clearAfterReconciliation(const QString& expectedNonce)
    {
        QLockFile lock(m_settings.fileName() + QStringLiteral(".clublog-submit.lock"));
        if (!lock.tryLock(0)) return false;
        m_settings.sync();
        if (m_settings.status() != QSettings::NoError || !pending() || nonce() != expectedNonce) return false;
        m_settings.remove(QStringLiteral("clubLogRestUpload"));
        restoreNext();
        m_settings.sync();
        return m_settings.status() == QSettings::NoError;
    }
    bool reconcileAndClear(const QString& expectedNonce, const std::function<bool()>& saveLocalResult)
    {
        QLockFile lock(m_settings.fileName() + QStringLiteral(".clublog-submit.lock"));
        if (!lock.tryLock(0)) return false;
        m_settings.sync();
        if (m_settings.status() != QSettings::NoError || !pending() || nonce() != expectedNonce) return false;
        if (!saveLocalResult()) return false;
        m_settings.remove(QStringLiteral("clubLogRestUpload"));
        restoreNext();
        m_settings.sync();
        return m_settings.status() == QSettings::NoError;
    }

    bool reconcilePartial(const QString& expectedNonce, const QStringList& remaining, const QString& fingerprint,
                          const std::function<bool()>& saveLocalResult)
    {
        if (remaining.isEmpty() || fingerprint.isEmpty()) return false;
        QLockFile lock(m_settings.fileName() + QStringLiteral(".clublog-submit.lock"));
        if (!lock.tryLock(0)) return false;
        m_settings.sync();
        if (m_settings.status() != QSettings::NoError || !pending() || nonce() != expectedNonce) return false;
        if (!saveLocalResult()) return false;
        m_settings.setValue(key("localIds"), remaining);
        m_settings.setValue(key("payloadSha256"), fingerprint);
        m_settings.sync();
        return m_settings.status() == QSettings::NoError;
    }

private:
    void restoreNext()
    {
        m_settings.beginGroup(QStringLiteral("clubLogRestPending"));
        const QStringList groups = m_settings.childGroups();
        m_settings.endGroup();
        if (groups.isEmpty()) return;
        const QString source = QStringLiteral("clubLogRestPending/") + groups.first();
        for (const char* field : {"phase", "uploadId", "station", "logIdentity", "localIds", "payloadSha256", "nonce"}) {
            const QString entry = source + QLatin1Char('/') + QLatin1String(field);
            if (m_settings.contains(entry)) m_settings.setValue(key(field), m_settings.value(entry));
        }
        m_settings.remove(source);
    }
    static QString key(const char* name) { return QStringLiteral("clubLogRestUpload/") + QLatin1String(name); }
    QSettings& m_settings;
};
}
