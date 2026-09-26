#include "core/Prosistel.h"

#include <QRegularExpression>

#include <cmath>

namespace decolog::core::prosistel {

const QList<Model>& models()
{
    static const QList<Model> list{
        {QStringLiteral("d_az"), QStringLiteral("Control box D - solo azimut"), kAzimuth, QChar(), 1},
        {QStringLiteral("d_el"), QStringLiteral("Control box D - solo elevazione"), QChar(), kElevation, 1},
        {QStringLiteral("d_azel"), QStringLiteral("Control box D - azimut + elevazione"), kAzimuth, kElevation, 1},
        {QStringLiteral("combi"), QStringLiteral("Combi-Track / Big-RAS (angoli x10)"), kAzimuth, kSecondUnit, 10},
    };
    return list;
}

const Model* modelFor(const QString& key)
{
    for (const Model& m : models()) {
        if (m.key == key)
            return &m;
    }
    return nullptr;
}

QByteArray encode(QChar axis, QChar verb, const QString& argument)
{
    QByteArray out;
    out += kStx;
    out += QString(axis).toUpper().toLatin1();
    out += QString(verb).toLatin1();
    out += argument.toLatin1();
    out += kCr;
    return out;
}

QByteArray queryPosition(QChar axis)
{
    return encode(axis, QLatin1Char('?'));
}

QByteArray gotoAngle(QChar axis, double degrees, int multiplier)
{
    long raw = std::lround(degrees * multiplier);
    if (raw < 0)
        raw = 0;
    // 997/999 (e 9777/9999 sui Combi) sono gli stop: un goto proprio su quel
    // numero fermerebbe il rotore invece di muoverlo.
    if (raw == kStopSoft || raw == kStopFast || raw == 9777 || raw == 9999)
        raw -= 1;
    return encode(axis, QLatin1Char('G'), QString::number(raw));
}

QByteArray stop(QChar axis, bool fast, int multiplier)
{
    int code = fast ? kStopFast : kStopSoft;
    if (multiplier == 10)
        code = fast ? 9999 : 9777;
    return encode(axis, QLatin1Char('G'), QString::number(code));
}

QByteArray disableCpm(QChar axis)
{
    return encode(axis, QLatin1Char('S'));
}

std::optional<Reply> decode(const QByteArray& frame, int multiplier)
{
    QByteArray payload = frame;
    payload.replace(kStx, QByteArray());
    payload.replace(kCr, QByteArray());
    const QString text = QString::fromLatin1(payload).trimmed();
    static const QRegularExpression re(QStringLiteral(R"(^([A-Z]),([^,]),(-?\d+(?:\.\d+)?),([A-Z])$)"));
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch())
        return std::nullopt;
    Reply r;
    r.axis = m.captured(1).at(0);
    r.verb = m.captured(2).at(0);
    r.value = m.captured(3).toDouble() / (multiplier > 0 ? multiplier : 1);
    r.status = m.captured(4).at(0);
    return r;
}

QString printable(const QByteArray& frame)
{
    QString out;
    for (const char c : frame) {
        if (c == kStx)
            out += QStringLiteral("<STX>");
        else if (c == kCr)
            out += QStringLiteral("<CR>");
        else
            out += QLatin1Char(c);
    }
    return out;
}

} // namespace decolog::core::prosistel
