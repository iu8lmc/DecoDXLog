#pragma once

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSettings>
#include <QString>

namespace decolog::core {
// A single non-secret fail-closed checkpoint. Never erase it on uncertain exit.
class LotwUploadJournal {
public:
    explicit LotwUploadJournal(QSettings &settings) : m_settings(settings) {}
    bool pending() const { return m_settings.contains(key()); }
    QJsonObject info() const { return QJsonDocument::fromJson(m_settings.value(key()).toByteArray()).object(); }
    bool begin(const QJsonObject &entry) {
        if (m_settings.fileName().isEmpty() || !QDir().mkpath(QFileInfo(m_settings.fileName()).absolutePath())) return false;
        QLockFile lock(m_settings.fileName() + QStringLiteral(".lotw-submit.lock"));
        if (!lock.tryLock(0)) return false;
        m_settings.sync();
        if (m_settings.status()!=QSettings::NoError || pending() || entry.value(QStringLiteral("nonce")).toString().isEmpty()) return false;
        m_settings.setValue(key(), QJsonDocument(entry).toJson(QJsonDocument::Compact));
        m_settings.sync();
        return m_settings.status()==QSettings::NoError;
    }
    bool clear(const QString &nonce) {
        QLockFile lock(m_settings.fileName() + QStringLiteral(".lotw-submit.lock"));
        if (!lock.tryLock(0)) return false;
        m_settings.sync();
        if (m_settings.status()!=QSettings::NoError || nonce.isEmpty() || !pending() || info().value(QStringLiteral("nonce")).toString()!=nonce) return false;
        m_settings.remove(key()); m_settings.sync();
        return m_settings.status()==QSettings::NoError;
    }
private:
    static QString key() { return QStringLiteral("lotwPendingUpload/checkpoint"); }
    QSettings &m_settings;
};
}
