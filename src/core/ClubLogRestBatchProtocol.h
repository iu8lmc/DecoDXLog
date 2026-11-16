#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

namespace decolog::core {

inline QString clubLogResponseMessage(const QJsonObject& body)
{
    const QString message = body.value(QStringLiteral("message")).toString();
    return message.isEmpty() ? body.value(QStringLiteral("error")).toObject()
                                   .value(QStringLiteral("message")).toString()
                             : message;
}

// Parse only states guaranteed by Club Log's upload documentation. Unknown or
// malformed responses never prove that a QSO was accepted.
struct ClubLogBatchEnvelope {
    enum class State { Invalid, Queued, Finished, Rejected, RateLimited };
    State state {State::Invalid};
    QString uploadId;
    QString message;
    QJsonObject body;
};

inline ClubLogBatchEnvelope parseClubLogBatchEnvelope(int httpStatus, const QByteArray& data, bool submission)
{
    ClubLogBatchEnvelope result;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return result;
    result.body = document.object();
    result.message = clubLogResponseMessage(result.body);
    if (httpStatus == 429) { result.state = ClubLogBatchEnvelope::State::RateLimited; return result; }
    if (httpStatus == 401 || httpStatus == 403 || httpStatus == 409 || httpStatus == 422) {
        result.state = ClubLogBatchEnvelope::State::Rejected;
        return result;
    }
    const QString state = result.body.value(QStringLiteral("status")).toString();
    if (submission && httpStatus == 202 && state == QLatin1String("queued")) {
        const QJsonValue value = result.body.value(QStringLiteral("upload_id"));
        result.uploadId = value.isString() ? value.toString() : value.toVariant().toString();
        if (!result.uploadId.isEmpty() && result.uploadId.toLongLong() > 0)
            result.state = ClubLogBatchEnvelope::State::Queued;
    } else if (!submission && httpStatus == 200) {
        if (state == QLatin1String("queued")) result.state = ClubLogBatchEnvelope::State::Queued;
        else if (state == QLatin1String("finished")) result.state = ClubLogBatchEnvelope::State::Finished;
    }
    return result;
}
}
