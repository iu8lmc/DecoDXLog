#pragma once
#include <QObject>
#include <QString>
#include <QThreadPool>

class DxCalendarCache : public QObject {
    Q_OBJECT
public:
    explicit DxCalendarCache(QObject* parent = nullptr, const QString& path = {});
    Q_INVOKABLE void readCache();
    Q_INVOKABLE void writeCache(const QString& data, const QString& updated);
signals:
    void cacheRead(const QString& data, const QString& updated, const QString& error);
    void cacheWritten(const QString& error);
private:
    QString m_path;
    QThreadPool m_pool;
};
