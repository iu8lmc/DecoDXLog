#pragma once

#include <QNetworkRequest>
#include <QUrlQuery>

namespace decolog::core {
inline QNetworkRequest clubLogCtyRequest(const QString& credential, const QString& legacyKey)
{
    const QString token = credential.trimmed();
    if (token.startsWith(QLatin1String("cl_"))) {
        QNetworkRequest request{QUrl(QStringLiteral("https://clublog.org/api/v1/reference/cty"))};
        request.setRawHeader("Authorization", QByteArray("Bearer ") + token.toUtf8());
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        return request;
    }
    if (legacyKey.trimmed().isEmpty()) return QNetworkRequest();
    QUrl url(QStringLiteral("https://cdn.clublog.org/cty.php"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("api"), legacyKey.trimmed());
    url.setQuery(query);
    return QNetworkRequest(url);
}
}
