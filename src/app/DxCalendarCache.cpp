#include "app/DxCalendarCache.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSaveFile>
#include <QStandardPaths>
#include <QVariantMap>

namespace { constexpr qint64 maxBytes = 3 * 1024 * 1024; }
DxCalendarCache::DxCalendarCache(QObject* parent, const QString& path)
    : QObject(parent), m_path(path.isEmpty() ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("dxcalendar-cache.json")) : path)
{
    m_pool.setMaxThreadCount(1);
}
void DxCalendarCache::readCache()
{
    const QString path = m_path;
    QPointer<DxCalendarCache> self(this);
    m_pool.start([self, path] {
        QVariantMap result;
        QFile file(path);
        if (file.exists()) {
            if (!file.open(QIODevice::ReadOnly) || file.size() > maxBytes) result.insert("error", "Cannot read calendar cache");
            else {
                QJsonParseError error;
                const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
                if (error.error != QJsonParseError::NoError || !doc.isObject()) result.insert("error", "Invalid calendar cache");
                else { result.insert("data", doc.object().value("data").toString()); result.insert("updated", doc.object().value("updated").toString()); }
            }
        }
        if (self) QMetaObject::invokeMethod(self.data(), [self, result] {
            if (self) emit self->cacheRead(result.value("data").toString(), result.value("updated").toString(), result.value("error").toString());
        }, Qt::QueuedConnection);
    });
}
void DxCalendarCache::writeCache(const QString& data, const QString& updated)
{
    const QString path = m_path;
    QPointer<DxCalendarCache> self(this);
    m_pool.start([self, path, data, updated] {
        QString error;
        if (data.size() > 2000000) error = QStringLiteral("Calendar cache exceeds size limit");
        else if (!QDir().mkpath(QFileInfo(path).absolutePath())) error = QStringLiteral("Cannot create cache directory");
        else {
            const QByteArray encoded = QJsonDocument(QJsonObject{{"data", data}, {"updated", updated}}).toJson(QJsonDocument::Compact);
            QSaveFile file(path);
            if (encoded.size() > maxBytes) error = QStringLiteral("Calendar cache exceeds size limit");
            else if (!file.open(QIODevice::WriteOnly) || file.write(encoded) != encoded.size() || !file.commit()) error = file.errorString();
        }
        if (self) QMetaObject::invokeMethod(self.data(), [self, error] { if (self) emit self->cacheWritten(error); }, Qt::QueuedConnection);
    });
}
