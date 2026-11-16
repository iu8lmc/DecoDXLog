#pragma once

#include <QCryptographicHash>
#include <QDateTime>
#include <QSettings>
#include <QTimeZone>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

namespace decolog::core {
// Keep only a one-way token fingerprint and a restriction, never the token.
// This survives app restarts and covers single, bulk and reconciliation calls.
class ClubLogAccessGuard {
public:
    static QString redact(QString text, const QString& token) {
        if (!token.isEmpty()) text.replace(token, QStringLiteral("[redacted]"), Qt::CaseInsensitive);
        return text;
    }
    static QVariant redactValue(const QVariant& value, const QString& token) {
        if (value.typeId() == QMetaType::QString) return redact(value.toString(), token);
        if (value.typeId() == QMetaType::QVariantList) {
            QVariantList result;
            for (const QVariant& item : value.toList()) result.append(redactValue(item, token));
            return result;
        }
        if (value.typeId() == QMetaType::QVariantMap) {
            QVariantMap result;
            const QVariantMap input = value.toMap();
            for (auto it = input.cbegin(); it != input.cend(); ++it)
                result.insert(redact(it.key(), token), redactValue(it.value(), token));
            return result;
        }
        return value;
    }
    static QString restriction(const QString& token) {
        QSettings settings;
        settings.sync();
        const QString group = key(token);
        const int status = settings.value(group + QStringLiteral("/status")).toInt();
        if (status == 401 || status == 403)
            return token.startsWith(QLatin1String("legacy-"))
                ? QStringLiteral("Club Log: access refused (HTTP %1); correct the legacy credentials before trying again").arg(status)
                : QStringLiteral("Club Log: access refused (HTTP %1); review or replace the token before trying again").arg(status);
        const qint64 until = settings.value(group + QStringLiteral("/until")).toLongLong();
        if (status == 429 && until > QDateTime::currentSecsSinceEpoch())
            return QStringLiteral("Club Log: rate limit; retry after %1 UTC")
                .arg(QDateTime::fromSecsSinceEpoch(until, QTimeZone::UTC).toString(Qt::ISODate));
        return {};
    }
    static void record(const QString& token, int status, const QByteArray& retryAfter) {
        if (token.isEmpty() || (status != 401 && status != 403 && status != 429)) return;
        QSettings settings;
        const QString group = key(token);
        settings.setValue(group + QStringLiteral("/status"), status);
        if (status == 429) {
            bool ok = false;
            qint64 seconds = retryAfter.trimmed().toLongLong(&ok);
            qint64 until = QDateTime::currentSecsSinceEpoch() + 300;
            if (ok && seconds >= 0 && seconds <= 31536000)
                until = QDateTime::currentSecsSinceEpoch() + seconds;
            else {
                const QDateTime date = QDateTime::fromString(QString::fromLatin1(retryAfter), Qt::RFC2822Date);
                if (date.isValid()) until = qMax(until, date.toSecsSinceEpoch());
            }
            settings.setValue(group + QStringLiteral("/until"), until);
        } else {
            settings.remove(group + QStringLiteral("/until"));
        }
        settings.sync();
    }
private:
    static QString key(const QString& token) {
        return QStringLiteral("clubLogRestAccess/") + QString::fromLatin1(
            QCryptographicHash::hash(token.toUtf8(), QCryptographicHash::Sha256).toHex());
    }
};
}
