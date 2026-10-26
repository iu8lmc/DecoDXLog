#include "app/CallListController.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

namespace decolog::app {
namespace {
bool validCacheName(const QString& name)
{
    return !name.isEmpty() && name.endsWith(QLatin1String(".txt"))
        && name == QFileInfo(name).fileName() && !name.contains(QLatin1Char('\\'));
}
}

CallListController::CallListController(const QString& directory, QObject* parent) : QObject(parent)
{
    m_directory = directory.isEmpty()
        ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("call-lists"))
        : directory;
    m_pool.setMaxThreadCount(1);
}

CallListController::~CallListController()
{
    // Tasks never wait for the GUI thread. Keep `this` alive until their queued
    // completion has been posted; QObject discards pending callbacks on teardown.
    m_pool.waitForDone();
}

QByteArray CallListController::read(const QString& path, QString& error)
{
    if (!QFileInfo(path).isFile()) { error = tr("Select an existing regular file."); return {}; }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return {}; }
    if (file.size() > core::CallList::maxBytes) { error = tr("File exceeds 32 MiB."); return {}; }
    const QByteArray bytes = file.read(core::CallList::maxBytes + 1);
    if (file.error() != QFileDevice::NoError) error = file.errorString();
    else if (bytes.size() > core::CallList::maxBytes) error = tr("File exceeds 32 MiB.");
    return bytes;
}

void CallListController::run(std::function<Result()> work)
{
    if (m_busy) return;
    m_busy = true;
    m_status = tr("Loading local lists…");
    emit changed();
    m_pool.start([this, work = std::move(work)] {
        Result result = work();
        QMetaObject::invokeMethod(this, [this, result = std::move(result)]() mutable {
            if (result.replace && result.error.isEmpty()) { m_sources = std::move(result.sources); ++m_revision; }
            if (result.inspected) m_preview = std::move(result.preview);
            m_status = result.error.isEmpty() ? result.status
                : tr("Not applied: %1. The saved lists have not been changed.").arg(result.error);
            m_busy = false;
            emit changed();
        }, Qt::QueuedConnection);
    });
}

bool CallListController::save(const QString& directory, const QList<Source>& sources, QString& error)
{
    if (!QDir().mkpath(directory)) { error = tr("Cannot create the list storage directory."); return false; }
    QJsonArray rows;
    for (const auto& s : sources) rows.append(QJsonObject{
        {"id", s.id}, {"label", s.label}, {"path", s.path}, {"cache", s.cache},
        {"importedAt", s.importedAt}, {"enabled", s.enabled}, {"options", QJsonObject::fromVariantMap(s.options)}});
    const QByteArray bytes = QJsonDocument(QJsonObject{{"version", 1}, {"sources", rows}}).toJson();
    QSaveFile file(QDir(directory).filePath(QStringLiteral("lists.json")));
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        error = file.errorString(); return false;
    }
    return true;
}

void CallListController::start()
{
    const QString directory = m_directory;
    run([directory] {
        Result r;
        const QString manifest = QDir(directory).filePath(QStringLiteral("lists.json"));
        if (!QFileInfo::exists(manifest)) { r.replace = true; return r; }
        const QByteArray bytes = read(manifest, r.error);
        if (!r.error.isEmpty()) return r;
        QJsonParseError parseError;
        const auto doc = QJsonDocument::fromJson(bytes, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()
            || doc.object().value("version").toInt() != 1 || !doc.object().value("sources").isArray()) {
            r.error = tr("Invalid list configuration."); return r;
        }
        for (const auto& value : doc.object().value("sources").toArray()) {
            const auto o = value.toObject();
            Source s;
            s.id = o.value("id").toString(); s.label = o.value("label").toString();
            s.path = o.value("path").toString(); s.cache = o.value("cache").toString();
            s.importedAt = o.value("importedAt").toString(); s.enabled = o.value("enabled").toBool(true);
            s.options = o.value("options").toObject().toVariantMap();
            if (s.id.isEmpty() || !validCacheName(s.cache) || r.sources.size() >= 64) {
                r.error = tr("Invalid list configuration."); return r;
            }
            const auto data = read(QDir(directory).filePath(s.cache), s.error);
            if (s.error.isEmpty()) { s.data = core::CallList::parse(data, s.options); s.error = s.data.error; }
            r.sources.append(std::move(s));
        }
        r.replace = true;
        r.status = tr("Local lists restored from their offline copies.");
        return r;
    });
}

void CallListController::inspect(const QUrl& file, const QVariantMap& options)
{
    if (m_busy) return;
    m_preview.clear();
    run([file, options] {
        Result r; r.inspected = true;
        if (!file.isLocalFile()) { r.error = tr("Select a local file."); return r; }
        const auto bytes = read(file.toLocalFile(), r.error);
        if (!r.error.isEmpty()) return r;
        auto data = core::CallList::parse(bytes, options, true);
        r.error = data.error;
        if (r.error.isEmpty()) r.preview = data.preview;
        r.status = tr("Preview ready. Check the columns before importing.");
        return r;
    });
}

void CallListController::add(const QUrl& file, const QString& label, const QVariantMap& options)
{
    importFile(file, label, options, {});
}

void CallListController::importFile(const QUrl& file, const QString& label, const QVariantMap& options, const QString& id)
{
    const auto previous = m_sources;
    const QString directory = m_directory;
    run([file, label, options, id, previous, directory] {
        Result r;
        if (!file.isLocalFile()) { r.error = tr("Select a local file."); return r; }
        if (id.isEmpty() && previous.size() >= 64) { r.error = tr("The limit is 64 lists."); return r; }
        const auto bytes = read(file.toLocalFile(), r.error);
        if (!r.error.isEmpty()) return r;
        Source s;
        s.data = core::CallList::parse(bytes, options);
        if (!s.data.error.isEmpty()) { r.error = s.data.error; return r; }
        s.id = id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : id;
        s.cache = QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".txt");
        s.path = file.toLocalFile();
        s.label = label.trimmed().isEmpty() ? QFileInfo(s.path).completeBaseName() : label.trimmed().left(120);
        s.importedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        s.options = options;
        r.sources = previous;
        QString oldCache;
        bool replaced = false;
        for (auto& old : r.sources) if (old.id == id && !id.isEmpty()) {
            oldCache = old.cache; s.enabled = old.enabled; old = s; replaced = true; break;
        }
        if (!replaced) r.sources.append(s);
        if (!QDir().mkpath(directory)) { r.error = tr("Cannot create the list storage directory."); return r; }
        const QString cachePath = QDir(directory).filePath(s.cache);
        QSaveFile cache(cachePath);
        if (!cache.open(QIODevice::WriteOnly) || cache.write(bytes) != bytes.size() || !cache.commit()) {
            r.error = cache.errorString(); return r;
        }
        if (!save(directory, r.sources, r.error)) { QFile::remove(cachePath); return r; }
        // Only delete our previous snapshot, after the new manifest is durable.
        if (validCacheName(oldCache)) QFile::remove(QDir(directory).filePath(oldCache));
        r.replace = true;
        r.status = tr("Imported %1 entries; %2 skipped or duplicate rows.").arg(s.data.records).arg(s.data.skipped);
        return r;
    });
}

void CallListController::reload(const QString& id)
{
    for (const auto& s : m_sources) if (s.id == id) {
        importFile(QUrl::fromLocalFile(s.path), s.label, s.options, id); return;
    }
}

void CallListController::setEnabled(const QString& id, bool enabled)
{
    auto updated = m_sources;
    bool found = false;
    for (auto& s : updated) if (s.id == id) { s.enabled = enabled; found = true; }
    if (!found) return;
    const QString directory = m_directory;
    run([updated, directory] {
        Result r; r.sources = updated; r.replace = save(directory, updated, r.error);
        r.status = tr("List preferences saved."); return r;
    });
}

void CallListController::remove(const QString& id)
{
    auto updated = m_sources;
    QString cache;
    for (qsizetype i = 0; i < updated.size(); ++i) if (updated.at(i).id == id) {
        cache = updated.at(i).cache; updated.removeAt(i); break;
    }
    if (cache.isEmpty()) return;
    const QString directory = m_directory;
    run([updated, directory, cache] {
        Result r; r.sources = updated; r.replace = save(directory, updated, r.error);
        if (r.replace && validCacheName(cache)) QFile::remove(QDir(directory).filePath(cache));
        r.status = tr("List removed. The original file was not deleted."); return r;
    });
}

QVariantList CallListController::sources() const
{
    QVariantList rows;
    for (const auto& s : m_sources) rows.append(QVariantMap{
        {"id", s.id}, {"label", s.label}, {"path", s.path}, {"enabled", s.enabled}, {"error", s.error},
        {"importedAt", s.importedAt}, {"records", s.data.records}, {"calls", s.data.calls.size()}});
    return rows;
}

QVariantList CallListController::lookup(const QString& value) const
{
    QVariantList groups;
    const QString call = core::CallList::normalizedCall(value);
    if (call.isEmpty()) return groups;
    for (const auto& s : m_sources) {
        if (!s.enabled || !s.data.calls.contains(call)) continue;
        groups.append(QVariantMap{{"id", s.id}, {"source", s.label}, {"importedAt", s.importedAt},
                                 {"entries", s.data.calls.value(call)}});
    }
    return groups;
}
} // namespace decolog::app
