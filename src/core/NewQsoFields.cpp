#include "core/NewQsoFields.h"

#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QVariantMap>
#include <QDate>
#include <cmath>

namespace decolog::core::newqso {

QStringList standardFields()
{
    return {
        QStringLiteral("GRIDSQUARE"),
        QStringLiteral("NAME"),
        QStringLiteral("QTH"),
        QStringLiteral("STATE"),
        QStringLiteral("IOTA"),
        QStringLiteral("TX_PWR"),
        QStringLiteral("PROP_MODE"),
        QStringLiteral("SAT_NAME"),
        QStringLiteral("SAT_MODE"),
        QStringLiteral("COMMENT"),
    };
}

QStringList defaultVisibleFields()
{
    return {
        QStringLiteral("GRIDSQUARE"),
        QStringLiteral("NAME"),
        QStringLiteral("QTH"),
        QStringLiteral("PROP_MODE"),
        QStringLiteral("SAT_NAME"),
        QStringLiteral("SAT_MODE"),
        QStringLiteral("COMMENT"),
    };
}

QStringList normalizeVisibleFields(const QStringList& fields)
{
    QSet<QString> requested;
    for (const QString& field : fields)
        requested.insert(normalizeAdifName(field));

    QStringList out;
    for (const QString& field : standardFields()) {
        if (requested.contains(field))
            out << field;
    }
    return out;
}

QString normalizeAdifName(const QString& name)
{
    return name.trimmed().toUpper();
}

bool isValidAdifName(const QString& name)
{
    static const QRegularExpression valid(QStringLiteral("^[A-Z][A-Z0-9_]{0,31}$"));
    return valid.match(normalizeAdifName(name)).hasMatch();
}

bool isProtectedAdifField(const QString& name)
{
    static const QSet<QString> protectedFields{
        QStringLiteral("CALL"), QStringLiteral("QSO_DATE"), QStringLiteral("TIME_ON"),
        QStringLiteral("QSO_DATE_OFF"), QStringLiteral("TIME_OFF"),
        QStringLiteral("BAND"), QStringLiteral("BAND_RX"),
        QStringLiteral("FREQ"), QStringLiteral("FREQ_RX"),
        QStringLiteral("MODE"), QStringLiteral("SUBMODE"),
        QStringLiteral("RST_SENT"), QStringLiteral("RST_RCVD"),
        QStringLiteral("STATION_CALLSIGN"), QStringLiteral("OPERATOR"),
        QStringLiteral("MY_GRIDSQUARE"), QStringLiteral("MY_CQ_ZONE"),
        QStringLiteral("MY_ITU_ZONE"), QStringLiteral("MY_DXCC"),
        QStringLiteral("APP_DECOLOG_TAGS"),
    };
    return protectedFields.contains(normalizeAdifName(name));
}

CustomFieldsResult validateCustomFields(const QVariantList& fields)
{
    CustomFieldsResult result;
    QSet<QString> seen;
    const QStringList standard = standardFields();

    for (const QVariant& value : fields) {
        const QVariantMap item = value.toMap();
        const QString rawName = item.value(QStringLiteral("name")).toString();
        const QString label = item.value(QStringLiteral("label")).toString().trimmed();
        if (rawName.trimmed().isEmpty() && label.isEmpty())
            continue;

        const QString name = normalizeAdifName(rawName);
        if (!isValidAdifName(name)) {
            result.error = CustomFieldError::InvalidName;
            result.field = rawName.trimmed();
            return result;
        }
        if (isProtectedAdifField(name)) {
            result.error = CustomFieldError::ProtectedField;
            result.field = name;
            return result;
        }
        if (standard.contains(name)) {
            result.error = CustomFieldError::StandardField;
            result.field = name;
            return result;
        }
        if (seen.contains(name)) {
            result.error = CustomFieldError::Duplicate;
            result.field = name;
            return result;
        }
        if (result.fields.size() >= kMaxCustomFields) {
            result.error = CustomFieldError::TooMany;
            result.field = name;
            return result;
        }

        const QString type=item.value(QStringLiteral("type"),QStringLiteral("text")).toString().trimmed().toLower();
        const QStringList types{QStringLiteral("text"),QStringLiteral("number"),QStringLiteral("date"),QStringLiteral("boolean"),QStringLiteral("list")};
        if (!types.contains(type)) { result.error=CustomFieldError::InvalidType; result.field=name; return result; }
        const QString unit=item.value(QStringLiteral("unit")).toString().trimmed();
        if (unit.size()>40) { result.error=CustomFieldError::InvalidUnit; result.field=name; return result; }
        QStringList choices; QSet<QString> unique;
        if (type==QLatin1String("list")) {
            for (const QVariant& v : item.value(QStringLiteral("choices")).toList()) {
                const QString choice=v.toString().trimmed();
                if (choice.isEmpty() || choice.size()>100 || unique.contains(choice) || choices.size()>=100) { result.error=CustomFieldError::InvalidChoices; result.field=name; return result; }
                unique.insert(choice); choices << choice;
            }
            if (choices.isEmpty()) { result.error=CustomFieldError::InvalidChoices; result.field=name; return result; }
        }
        seen.insert(name);
        QVariantMap normalized{{QStringLiteral("name"),name},{QStringLiteral("label"),label.isEmpty()?name:label}};
        if (type!=QLatin1String("text")) normalized.insert(QStringLiteral("type"),type);
        if (!unit.isEmpty()) normalized.insert(QStringLiteral("unit"),unit);
        if (!choices.isEmpty()) normalized.insert(QStringLiteral("choices"),choices);
        const QVariantMap condition=item.value(QStringLiteral("requiredIf")).toMap();
        const QString dependency=normalizeAdifName(condition.value(QStringLiteral("field")).toString());
        const QString expected=condition.value(QStringLiteral("equals")).toString().trimmed();
        if (!dependency.isEmpty() || !expected.isEmpty()) {
            if (!isValidAdifName(dependency) || dependency==name || expected.isEmpty() || expected.size()>100) {
                result.error=CustomFieldError::InvalidCondition; result.field=name; return result;
            }
            normalized.insert(QStringLiteral("requiredIf"),QVariantMap{{QStringLiteral("field"),dependency},{QStringLiteral("equals"),expected}});
        }
        result.fields << normalized;
    }
    for (const QVariant& value : result.fields) {
        const auto item=value.toMap(); const auto condition=item.value(QStringLiteral("requiredIf")).toMap();
        if (!condition.isEmpty() && !seen.contains(condition.value(QStringLiteral("field")).toString())) {
            result.error=CustomFieldError::InvalidCondition; result.field=item.value(QStringLiteral("name")).toString(); return result;
        }
    }
    return result;
}

QString validateCustomValue(const QVariantMap& definition,const QString& value,const QLocale& locale)
{
    const QString text=value.trimmed(); if (text.isEmpty()) return {};
    const QString type=definition.value(QStringLiteral("type"),QStringLiteral("text")).toString();
    if (type==QLatin1String("text")) return {};
    if (type==QLatin1String("number")) {
        QLocale strict=locale; strict.setNumberOptions(strict.numberOptions()|QLocale::RejectGroupSeparator);
        bool ok=false; const double n=strict.toDouble(text,&ok); return ok && std::isfinite(n) ? QString() : QStringLiteral("Invalid number for the current locale");
    }
    if (type==QLatin1String("date")) { const QDate d=QDate::fromString(text,Qt::ISODate); return d.isValid() && d.toString(Qt::ISODate)==text ? QString() : QStringLiteral("Use a valid date YYYY-MM-DD"); }
    if (type==QLatin1String("boolean")) return text==QLatin1String("Y") || text==QLatin1String("N") ? QString() : QStringLiteral("Use Y or N");
    if (type==QLatin1String("list")) return definition.value(QStringLiteral("choices")).toStringList().contains(text) ? QString() : QStringLiteral("Choose an allowed value");
    return QStringLiteral("Unsupported custom field type");
}

QString validateCustomValues(const QVariantList& definitions,const QVariantMap& values,const QLocale& locale)
{
    QVariantMap normalized;
    for (auto it=values.cbegin();it!=values.cend();++it) {
        const QString name=normalizeAdifName(it.key());
        if (normalized.contains(name)) return QStringLiteral("%1: Duplicate field").arg(name);
        normalized.insert(name,it.value().toString().trimmed());
    }
    for (const QVariant& value : definitions) {
        const QVariantMap definition=value.toMap(); const QString name=definition.value(QStringLiteral("name")).toString();
        const QString text=normalized.value(name).toString(); const auto condition=definition.value(QStringLiteral("requiredIf")).toMap();
        if (!condition.isEmpty() && normalized.value(condition.value(QStringLiteral("field")).toString()).toString()==condition.value(QStringLiteral("equals")).toString() && text.isEmpty())
            return QStringLiteral("%1: Required when %2 equals %3").arg(name,condition.value(QStringLiteral("field")).toString(),condition.value(QStringLiteral("equals")).toString());
        const QString error=validateCustomValue(definition,text,locale);
        if (!error.isEmpty()) return QStringLiteral("%1: %2").arg(name,error);
    }
    return {};
}

QString encodeCustomFields(const QVariantList& fields)
{
    const CustomFieldsResult checked = validateCustomFields(fields);
    if (!checked.ok())
        return {};
    return QString::fromUtf8(QJsonDocument::fromVariant(checked.fields).toJson(QJsonDocument::Compact));
}

QVariantList decodeCustomFields(const QString& json, bool* legacyIota)
{
    if (legacyIota)
        *legacyIota = false;
    if (json.trimmed().isEmpty())
        return {};
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray())
        return {};
    // Older versions allowed IOTA as a custom field. Expose it in the
    // standard section and keep accepting the other saved custom fields.
    QVariantList fields = document.toVariant().toList();
    const bool hadIota = fields.removeIf([](const QVariant& field) {
        return normalizeAdifName(field.toMap().value(QStringLiteral("name")).toString())
               == QLatin1String("IOTA");
    }) > 0;
    const CustomFieldsResult checked = validateCustomFields(fields);
    if (!checked.ok())
        return {};
    if (legacyIota)
        *legacyIota = hadIota;
    return checked.fields;
}

} // namespace decolog::core::newqso
