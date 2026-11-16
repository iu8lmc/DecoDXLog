#include "core/ClubLogOqrsClient.h"
#include "core/ClubLogAccessGuard.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QUrlQuery>
#include <utility>

namespace decolog::core {
ClubLogOqrsClient::ClubLogOqrsClient(QObject* parent) : QObject(parent), m_net(new QNetworkAccessManager(this)) {}

void ClubLogOqrsClient::fetch(const QString& callsign, const QString& token)
{
    if (m_busy) return;
    const QString call = callsign.trimmed().toUpper();
    const QRegularExpression valid(QStringLiteral("^[A-Z0-9]+(?:/[A-Z0-9]+)*$"));
    const QString restriction = ClubLogAccessGuard::restriction(token);
    if (!restriction.isEmpty()) { emit finished(false, {}, restriction); return; }
    if (!valid.match(call).hasMatch() || !token.startsWith(QLatin1String("cl_"))) {
        emit finished(false, {}, tr("A station callsign and Club Log personal token are required")); return;
    }
    m_call = call; m_token = token; m_cursors.clear(); m_rows.clear(); m_busy = true;
    next();
}

void ClubLogOqrsClient::fail(const QString& message)
{
    m_busy = false; m_token.clear(); m_rows.clear(); m_cursors.clear();
    emit finished(false, {}, message);
}

void ClubLogOqrsClient::next(const QString& cursor)
{
    const QString path = QStringLiteral("logs/") + QString::fromLatin1(QUrl::toPercentEncoding(m_call, "/"))
                         + QStringLiteral("/oqrs-matches");
    QUrl url = m_baseUrl.resolved(QUrl(path));
    QUrlQuery query;
    if (!cursor.isEmpty()) query.addQueryItem(QStringLiteral("cursor"), cursor);
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QByteArray("Bearer ") + m_token.toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(30000);
    QNetworkReply* reply = m_net->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        ClubLogAccessGuard::record(m_token, status, reply->rawHeader("Retry-After"));
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(reply->readAll(), &error);
        reply->deleteLater();
        if (status != 200 || error.error != QJsonParseError::NoError || !document.isObject()
            || !document.object().value(QStringLiteral("matches")).isArray()) {
            fail(tr("Club Log OQRS request failed (HTTP %1); previous results preserved").arg(status)); return;
        }
        const QJsonObject object = document.object();
        for (const QJsonValue& value : object.value(QStringLiteral("matches")).toArray()) {
            if (!value.isObject() || value.toObject().value(QStringLiteral("dxcall")).toString().trimmed().isEmpty()) {
                fail(tr("Club Log OQRS returned an invalid record; previous results preserved")); return;
            }
            m_rows.append(ClubLogAccessGuard::redactValue(value.toObject().toVariantMap(), m_token));
        }
        const QJsonValue cursorValue = object.value(QStringLiteral("next_cursor"));
        if (!cursorValue.isUndefined() && !cursorValue.isNull() && !cursorValue.isString()) {
            fail(tr("Club Log OQRS returned an invalid cursor; previous results preserved")); return;
        }
        const QString cursor = cursorValue.toString();
        if (!m_token.isEmpty() && cursor.contains(m_token, Qt::CaseInsensitive)) {
            fail(tr("Club Log OQRS returned an invalid cursor; previous results preserved")); return;
        }
        if (!cursor.isEmpty()) {
            if (m_cursors.contains(cursor) || m_cursors.size() >= 10000) {
                fail(tr("Club Log OQRS pagination failed; previous results preserved")); return;
            }
            m_cursors.insert(cursor); next(cursor); return;
        }
        m_busy = false; m_token.clear(); m_cursors.clear();
        const QVariantList rows = std::exchange(m_rows, {});
        emit finished(true, rows, {});
    });
}
}
