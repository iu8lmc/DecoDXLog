#pragma once
#include <QObject>
#include <QSet>
#include <QUrl>
#include <QVariantList>

class QNetworkAccessManager;

namespace decolog::core {
// Direct OQRS requests are informational and are not QSL confirmations.
class ClubLogOqrsClient : public QObject {
    Q_OBJECT
public:
    explicit ClubLogOqrsClient(QObject* parent = nullptr);
    void setBaseUrl(const QUrl& url) { m_baseUrl = url; }
    bool busy() const { return m_busy; }
    void fetch(const QString& callsign, const QString& token);
signals:
    void finished(bool ok, const QVariantList& requests, const QString& error);
private:
    void next(const QString& cursor = {});
    void fail(const QString& message);
    QNetworkAccessManager* m_net;
    QUrl m_baseUrl{QStringLiteral("https://clublog.org/api/v1/")};
    QString m_call, m_token;
    QSet<QString> m_cursors;
    QVariantList m_rows;
    bool m_busy{false};
};
}
