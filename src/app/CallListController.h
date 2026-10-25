#pragma once

#include "core/CallList.h"
#include <QObject>
#include <QThreadPool>
#include <QUrl>
#include <functional>

namespace decolog::app {

// Own storage, no QSO database, callbook or contest writes. All I/O and parsing
// run on one worker; immutable snapshots are published on the GUI thread.
class CallListController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList sources READ sources NOTIFY changed)
    Q_PROPERTY(QVariantMap preview READ preview NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(int revision READ revision NOTIFY changed)
public:
    explicit CallListController(const QString& directory = {}, QObject* parent = nullptr);
    ~CallListController() override;
    void start();
    QVariantList sources() const;
    QVariantMap preview() const { return m_preview; }
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    int revision() const { return m_revision; }
    Q_INVOKABLE void inspect(const QUrl& file, const QVariantMap& options);
    Q_INVOKABLE void add(const QUrl& file, const QString& label, const QVariantMap& options);
    Q_INVOKABLE void reload(const QString& id);
    Q_INVOKABLE void setEnabled(const QString& id, bool enabled);
    Q_INVOKABLE void remove(const QString& id);
    Q_INVOKABLE QVariantList lookup(const QString& call) const;
signals:
    void changed();
private:
    struct Source {
        QString id, label, path, cache, importedAt, error;
        bool enabled{true};
        QVariantMap options;
        core::CallListData data;
    };
    struct Result {
        QList<Source> sources;
        QVariantMap preview;
        QString error, status;
        bool replace{false}, inspected{false};
    };
    void run(std::function<Result()> work);
    void importFile(const QUrl& file, const QString& label, const QVariantMap& options, const QString& id);
    static bool save(const QString& directory, const QList<Source>& sources, QString& error);
    static QByteArray read(const QString& path, QString& error);
    QString m_directory;
    QList<Source> m_sources;
    QVariantMap m_preview;
    QString m_status;
    bool m_busy{false};
    int m_revision{0};
    QThreadPool m_pool;
};
}
