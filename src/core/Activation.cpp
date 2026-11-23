#include "core/Activation.h"

#include <QCoreApplication>
#include <QDate>
#include <QTimeZone>
#include <QRegularExpression>
#include <QSet>

namespace decolog::core {

namespace {

struct KindInfo {
    Activation::Kind kind;
    const char* id;
    const char* label;
    int required;
};

const KindInfo kKinds[] = {
    {Activation::Kind::None, "none", "—", 0},
    {Activation::Kind::Pota, "pota", "POTA", 10},
    {Activation::Kind::Sota, "sota", "SOTA", 4},
    {Activation::Kind::Wwff, "wwff", "WWFF", 44},
    {Activation::Kind::Iota, "iota", "IOTA", 0},
    {Activation::Kind::Contest, "contest", "Contest", 0},
    {Activation::Kind::Free, "free", "Sessione", 0},
};

const KindInfo& infoFor(Activation::Kind kind)
{
    for (const auto& k : kKinds) {
        if (k.kind == kind)
            return k;
    }
    return kKinds[0];
}

} // namespace

QString Activation::kindId(Kind kind)
{
    return QLatin1String(infoFor(kind).id);
}

Activation::Kind Activation::kindFromId(const QString& id)
{
    for (const auto& k : kKinds) {
        if (id == QLatin1String(k.id))
            return k.kind;
    }
    return Kind::None;
}

bool Activation::normalizePrograms(const QVariantMap& input, QVariantMap* normalized, QString* error)
{
    if (normalized) normalized->clear();
    if (error) error->clear();
    QVariantMap out;
    static const QMap<QString, QRegularExpression> patterns{
        {QStringLiteral("pota"), QRegularExpression(QStringLiteral("^[A-Z0-9]{1,4}-[0-9]{4,5}(?:@[A-Z]{2}-[A-Z0-9]{1,3})?$"))},
        {QStringLiteral("sota"), QRegularExpression(QStringLiteral("^[A-Z0-9]{1,10}/[A-Z0-9]{1,10}-[0-9]{1,6}$"))},
        {QStringLiteral("wwff"), QRegularExpression(QStringLiteral("^[A-Z0-9]{1,4}FF-[0-9]{4}$"))},
        {QStringLiteral("iota"), QRegularExpression(QStringLiteral("^(AF|AN|AS|EU|NA|OC|SA)-(?!000)[0-9]{3}$"))}};
    for (auto it = input.cbegin(); it != input.cend(); ++it) {
        const QString key = it.key().trimmed().toLower();
        if (!patterns.contains(key) || out.contains(key) || it.value().metaType().id() != QMetaType::QString) {
            if (error) *error = QCoreApplication::translate("Activation", "Unsupported or invalid additional program: %1").arg(it.key());
            return false;
        }
        const QString raw = it.value().toString().trimmed().toUpper();
        if (raw.isEmpty()) continue;
        const QStringList parts = raw.split(QLatin1Char(','));
        if (parts.size() > (key == QLatin1String("pota") ? 100 : 1)) {
            if (error) *error = QCoreApplication::translate("Activation", "Too many references for %1").arg(key.toUpper());
            return false;
        }
        QStringList refs;
        QSet<QString> seen;
        for (const QString& part : parts) {
            const QString ref = part.trimmed();
            if (!patterns.value(key).match(ref).hasMatch() || seen.contains(ref)) {
                if (error) *error = QCoreApplication::translate("Activation", "Invalid or duplicate %1 reference: %2").arg(key.toUpper(), ref);
                return false;
            }
            seen.insert(ref);
            refs << ref;
        }
        out.insert(key, refs.join(QLatin1Char(',')));
    }
    if (normalized) *normalized = out;
    return true;
}

int Activation::requiredQsos() const
{
    return infoFor(kind).required;
}

QString Activation::defaultTag() const
{
    if (kind == Kind::Contest)
        return contestId.isEmpty() ? QStringLiteral("contest") : contestId.toLower();
    const QString id = kindId(kind);
    return id == QLatin1String("none") || id == QLatin1String("free") ? QString() : id;
}

QString Activation::title() const
{
    // In UTF-8: la lineetta di "nessuna sessione" letta come Latin-1 usciva
    // come tre caratteri senza senso.
    const QString label = QString::fromUtf8(infoFor(kind).label);
    if (!reference.isEmpty())
        return QStringLiteral("%1 %2").arg(label, reference);
    if (!contestId.isEmpty())
        return QStringLiteral("%1 %2").arg(label, contestId);
    return name.isEmpty() ? label : QStringLiteral("%1 %2").arg(label, name);
}

void Activation::applyTo(AdifRecord& record, int serial) const
{
    if (!active)
        return;
    auto fill = [&record](const char* field, const QString& value) {
        if (!value.isEmpty() && record.value(QLatin1String(field)).isEmpty())
            record.set(QLatin1String(field), value);
    };

    // I campi dell'attivatore: MY_*.
    switch (kind) {
    case Kind::Pota:
        fill("MY_SIG", QStringLiteral("POTA"));
        fill("MY_SIG_INFO", reference);
        fill("MY_POTA_REF", reference);
        break;
    case Kind::Wwff:
        fill("MY_SIG", QStringLiteral("WWFF"));
        fill("MY_SIG_INFO", reference);
        fill("MY_WWFF_REF", reference);
        break;
    case Kind::Sota:
        fill("MY_SOTA_REF", reference);
        break;
    case Kind::Iota:
        fill("MY_IOTA", reference);
        break;
    case Kind::Contest:
        fill("CONTEST_ID", contestId);
        break;
    case Kind::None:
    case Kind::Free:
        break;
    }

    QVariantMap extras;
    if (normalizePrograms(programs, &extras)) {
        fill("MY_POTA_REF", extras.value(QStringLiteral("pota")).toString());
        fill("MY_SOTA_REF", extras.value(QStringLiteral("sota")).toString());
        fill("MY_WWFF_REF", extras.value(QStringLiteral("wwff")).toString());
        fill("MY_IOTA", extras.value(QStringLiteral("iota")).toString());
    }

    fill("MY_GRIDSQUARE", myGrid);
    if (serialEnabled && serial > 0) {
        fill("STX", QString::number(serial));
        if (record.value(QStringLiteral("STX_STRING")).isEmpty() && !contestId.isEmpty())
            record.set(QStringLiteral("STX_STRING"), QString::number(serial));
    }

    const QString label = tag.isEmpty() ? defaultTag() : tag;
    if (!label.isEmpty()) {
        const QString existing = record.value(QStringLiteral("APP_DECOLOG_TAGS"));
        const QStringList tags = existing.split(QLatin1Char(','), Qt::SkipEmptyParts);
        bool present = false;
        for (const QString& t : tags)
            present = present || t.simplified().compare(label, Qt::CaseInsensitive) == 0;
        if (!present)
            record.set(QStringLiteral("APP_DECOLOG_TAGS"),
                       existing.isEmpty() ? label : existing + QLatin1Char(',') + label);
    }
}

QString Activation::exportFileName(const QString& stationCall, const QDate& day) const
{
    const QString call = stationCall.trimmed().toUpper().replace(QLatin1Char('/'), QLatin1Char('_'));
    const QString date = day.toString(QStringLiteral("yyyyMMdd"));
    if (!reference.isEmpty() && (kind == Kind::Pota || kind == Kind::Wwff || kind == Kind::Sota || kind == Kind::Iota)) {
        // Il formato che POTA chiede e che gli altri accettano.
        return QStringLiteral("%1@%2-%3.adi").arg(call, QString(reference).replace(QLatin1Char('/'), QLatin1Char('-')), date);
    }
    if (kind == Kind::Contest && !contestId.isEmpty())
        return QStringLiteral("%1-%2-%3.adi").arg(call, contestId, date);
    return QStringLiteral("%1-%2.adi").arg(call, date);
}

QVariantMap Activation::toMap() const
{
    return {
        {QStringLiteral("active"), active},
        {QStringLiteral("kind"), kindId(kind)},
        {QStringLiteral("reference"), reference},
        {QStringLiteral("programs"), programs},
        {QStringLiteral("name"), name},
        {QStringLiteral("contestId"), contestId},
        {QStringLiteral("myGrid"), myGrid},
        {QStringLiteral("stationProfileId"), stationProfileId},
        {QStringLiteral("tag"), tag},
        {QStringLiteral("band"), band},
        {QStringLiteral("mode"), mode},
        {QStringLiteral("nextSerial"), nextSerial},
        {QStringLiteral("serialEnabled"), serialEnabled},
        {QStringLiteral("sharedSerial"), sharedSerial},
        {QStringLiteral("rules"), rules},
        {QStringLiteral("callHistory"), callHistory},
        {QStringLiteral("startedAt"), startedAt.isValid() ? startedAt.toString(Qt::ISODate) : QString()},
        {QStringLiteral("requiredQsos"), requiredQsos()},
        {QStringLiteral("title"), title()},
    };
}

Activation Activation::fromMap(const QVariantMap& m)
{
    Activation a;
    a.active = m.value(QStringLiteral("active"), false).toBool();
    a.kind = kindFromId(m.value(QStringLiteral("kind")).toString());
    a.reference = m.value(QStringLiteral("reference")).toString().trimmed().toUpper();
    a.programs = m.value(QStringLiteral("programs")).toMap();
    a.name = m.value(QStringLiteral("name")).toString().trimmed();
    a.contestId = m.value(QStringLiteral("contestId")).toString().trimmed().toUpper();
    a.myGrid = m.value(QStringLiteral("myGrid")).toString().trimmed().toUpper();
    a.stationProfileId = m.value(QStringLiteral("stationProfileId")).toLongLong();
    a.tag = m.value(QStringLiteral("tag")).toString().trimmed();
    a.band = m.value(QStringLiteral("band")).toString().trimmed().toLower();
    a.mode = m.value(QStringLiteral("mode")).toString().trimmed().toUpper();
    a.nextSerial = qMax(1, m.value(QStringLiteral("nextSerial"), 1).toInt());
    a.serialEnabled = m.value(QStringLiteral("serialEnabled"), a.kind == Kind::Contest).toBool();
    a.sharedSerial = m.value(QStringLiteral("sharedSerial"), false).toBool();
    a.rules = m.value(QStringLiteral("rules")).toMap();
    a.callHistory = m.value(QStringLiteral("callHistory")).toString();
    a.startedAt = QDateTime::fromString(m.value(QStringLiteral("startedAt")).toString(), Qt::ISODate);
    if (a.startedAt.isValid())
        a.startedAt.setTimeZone(QTimeZone::UTC);
    return a;
}

} // namespace decolog::core
