#include "core/Awards.h"
#include "../StartupTrace.h"

#include "core/LogDatabase.h"
#include "core/Modes.h"

#include <QCoreApplication>
#include <QDir>
#include <QHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QSqlQuery>
#include <QTimeZone>
#include <algorithm>

namespace decolog::core {

namespace awards {

QString AwardClaim::stateName(State state)
{
    switch (state) {
    case State::Draft: return QStringLiteral("draft");
    case State::Submitted: return QStringLiteral("submitted");
    case State::Verified: return QStringLiteral("verified");
    case State::Credited: return QStringLiteral("credited");
    case State::Rejected: return QStringLiteral("rejected");
    }
    return {};
}

std::optional<AwardClaim::State> AwardClaim::stateFromName(const QString& name)
{
    const QString value = name.trimmed().toLower();
    for (const State state : {State::Draft, State::Submitted, State::Verified, State::Credited, State::Rejected})
        if (stateName(state) == value)
            return state;
    return std::nullopt;
}

bool AwardClaim::transitionTo(State next, const QDateTime& when, const QString& note, QString* error)
{
    const bool allowed = (state == State::Draft && next == State::Submitted)
                         || (state == State::Submitted && (next == State::Verified || next == State::Rejected))
                         || (state == State::Verified && next == State::Credited);
    if (!allowed) {
        if (error)
            *error = QStringLiteral("invalid award claim transition: %1 -> %2")
                         .arg(stateName(state), stateName(next));
        return false;
    }
    state = next;
    if (next == State::Submitted)
        submittedAt = when;
    else
        decidedAt = when;
    if (!note.isEmpty())
        outcome = note;
    return true;
}

QJsonObject AwardClaim::toJson() const
{
    QJsonArray ids;
    for (const qint64 idValue : qsoIds)
        ids.append(static_cast<double>(idValue));
    QJsonArray credited;
    for (const qint64 idValue : creditedQsoIds) credited.append(static_cast<double>(idValue));
    QJsonArray rejected;
    for (const qint64 idValue : rejectedQsoIds) rejected.append(static_cast<double>(idValue));
    return {{QStringLiteral("id"), id},
            {QStringLiteral("awardId"), awardId},
            {QStringLiteral("accountId"), accountId},
            {QStringLiteral("category"), category},
            {QStringLiteral("ruleVersion"), ruleVersion},
            {QStringLiteral("qsoIds"), ids},
            {QStringLiteral("state"), stateName(state)},
            {QStringLiteral("createdAt"), createdAt.toUTC().toString(Qt::ISODateWithMs)},
            {QStringLiteral("submittedAt"), submittedAt.isValid() ? submittedAt.toUTC().toString(Qt::ISODateWithMs) : QString()},
            {QStringLiteral("decidedAt"), decidedAt.isValid() ? decidedAt.toUTC().toString(Qt::ISODateWithMs) : QString()},
            {QStringLiteral("outcome"), outcome},
            {QStringLiteral("evidence"), evidence},
            {QStringLiteral("externalProvider"), externalProvider},
            {QStringLiteral("creditedQsoIds"), credited},
            {QStringLiteral("rejectedQsoIds"), rejected}};
}

std::optional<AwardClaim> AwardClaim::fromJson(const QJsonObject& object, QString* error)
{
    AwardClaim claim;
    claim.id = object.value(QStringLiteral("id")).toString();
    claim.awardId = object.value(QStringLiteral("awardId")).toString();
    claim.accountId = object.value(QStringLiteral("accountId")).toString().trimmed();
    claim.category = object.value(QStringLiteral("category")).toString().trimmed();
    claim.ruleVersion = object.value(QStringLiteral("ruleVersion")).toString();
    claim.createdAt = QDateTime::fromString(object.value(QStringLiteral("createdAt")).toString(), Qt::ISODateWithMs);
    claim.submittedAt = QDateTime::fromString(object.value(QStringLiteral("submittedAt")).toString(), Qt::ISODateWithMs);
    claim.decidedAt = QDateTime::fromString(object.value(QStringLiteral("decidedAt")).toString(), Qt::ISODateWithMs);
    const auto state = stateFromName(object.value(QStringLiteral("state")).toString());
    if (claim.id.isEmpty() || claim.awardId.isEmpty() || claim.ruleVersion.isEmpty() || !claim.createdAt.isValid() || !state) {
        if (error)
            *error = QStringLiteral("invalid award claim");
        return std::nullopt;
    }
    claim.state = *state;
    for (const QJsonValue& value : object.value(QStringLiteral("qsoIds")).toArray())
        if (value.isDouble() && value.toDouble() > 0)
            claim.qsoIds << static_cast<qint64>(value.toDouble());
    claim.outcome = object.value(QStringLiteral("outcome")).toString();
    claim.evidence = object.value(QStringLiteral("evidence")).toString();
    claim.externalProvider = object.value(QStringLiteral("externalProvider")).toString();
    for (const QJsonValue& value : object.value(QStringLiteral("creditedQsoIds")).toArray())
        if (value.isDouble() && value.toDouble() > 0) claim.creditedQsoIds << static_cast<qint64>(value.toDouble());
    for (const QJsonValue& value : object.value(QStringLiteral("rejectedQsoIds")).toArray())
        if (value.isDouble() && value.toDouble() > 0) claim.rejectedQsoIds << static_cast<qint64>(value.toDouble());
    return claim;
}

QList<AwardClaim> AwardClaimStore::load(const LogDatabase& db, QString* error)
{
    QList<AwardClaim> claims;
    const QString encoded = db.setting(QStringLiteral("awards/claims"));
    if (encoded.isEmpty())
        return claims;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(encoded.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        if (error)
            *error = QStringLiteral("invalid persisted award claims: %1").arg(parseError.errorString());
        return {};
    }
    for (const QJsonValue& value : document.array()) {
        QString claimError;
        const auto claim = AwardClaim::fromJson(value.toObject(), &claimError);
        if (!claim) {
            if (error)
                *error = claimError;
            return {};
        }
        claims << *claim;
    }
    return claims;
}

bool AwardClaimStore::save(LogDatabase& db, const QList<AwardClaim>& claims, QString* error)
{
    if (!db.isOpen()) {
        if (error)
            *error = QStringLiteral("the log database is not open");
        return false;
    }
    QJsonArray values;
    for (const AwardClaim& claim : claims)
        values.append(claim.toJson());
    db.setSetting(QStringLiteral("awards/claims"),
                  QString::fromUtf8(QJsonDocument(values).toJson(QJsonDocument::Compact)));
    return true;
}

bool AwardClaimStore::reconcileExternalResult(LogDatabase& db, const QByteArray& json,
                                              QString* error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("invalid external claim result JSON");
        return false;
    }
    const QJsonObject object = document.object();
    const QString claimId = object.value(QStringLiteral("claimId")).toString().trimmed();
    const QString provider = object.value(QStringLiteral("provider")).toString().trimmed();
    const QString accountId = object.value(QStringLiteral("accountId")).toString().trimmed();
    const QString category = object.value(QStringLiteral("category")).toString().trimmed();
    const QString decision = object.value(QStringLiteral("decision")).toString().trimmed().toLower();
    const QString evidence = object.value(QStringLiteral("evidence")).toString().trimmed();
    if (claimId.isEmpty() || provider.isEmpty() || evidence.isEmpty()
        || !QSet<QString>{QStringLiteral("verified"), QStringLiteral("credited"), QStringLiteral("rejected")}.contains(decision)) {
        if (error) *error = QStringLiteral("claim result requires claimId, provider, evidence and a supported decision");
        return false;
    }
    auto claims = load(db, error);
    for (AwardClaim& claim : claims) {
        if (claim.id != claimId)
            continue;
        if (!claim.externalProvider.isEmpty() && claim.externalProvider != provider) {
            if (error) *error = QStringLiteral("claim already reconciled by another provider");
            return false;
        }
        if (!claim.accountId.isEmpty() && claim.accountId != accountId) {
            if (error) *error = QStringLiteral("external result account does not match the claim");
            return false;
        }
        if (!claim.category.isEmpty() && claim.category != category) {
            if (error) *error = QStringLiteral("external result category does not match the claim");
            return false;
        }
        const QDateTime now = QDateTime::currentDateTimeUtc();
        const auto target = AwardClaim::stateFromName(decision);
        if (!target) {
            if (error) *error = QStringLiteral("unsupported external decision");
            return false;
        }
        if (claim.state != *target && !claim.transitionTo(*target, now, decision, error))
            return false;
        claim.externalProvider = provider;
        claim.evidence = evidence;
        claim.outcome = object.value(QStringLiteral("outcome")).toString().trimmed();
        claim.creditedQsoIds.clear();
        claim.rejectedQsoIds.clear();
        for (const QJsonValue& value : object.value(QStringLiteral("creditedQsoIds")).toArray())
            if (value.isDouble() && value.toDouble() > 0) claim.creditedQsoIds << static_cast<qint64>(value.toDouble());
        for (const QJsonValue& value : object.value(QStringLiteral("rejectedQsoIds")).toArray())
            if (value.isDouble() && value.toDouble() > 0) claim.rejectedQsoIds << static_cast<qint64>(value.toDouble());
        return save(db, claims, error);
    }
    if (error) *error = QStringLiteral("claim not found");
    return false;
}

namespace {

const QSet<QString> kRuleFields{
    QStringLiteral("DXCC"), QStringLiteral("STATE"), QStringLiteral("CQZ"),
    QStringLiteral("GRID4"), QStringLiteral("BAND"), QStringLiteral("CALL_PREFIX")};

void ruleError(QString* error, const QString& message)
{
    if (error)
        *error = message;
}

} // namespace

bool AwardRulePackage::isValid(QString* error) const
{
    static const QRegularExpression idRx(QStringLiteral("^[a-z][a-z0-9_-]{1,31}$"));
    static const QRegularExpression versionRx(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,31}$"));
    if (!idRx.match(id).hasMatch()) {
        ruleError(error, QStringLiteral("invalid package id"));
        return false;
    }
    if (title.trimmed().isEmpty() || source.trimmed().isEmpty() || !versionRx.match(version).hasMatch()) {
        ruleError(error, QStringLiteral("title, source and version are required"));
        return false;
    }
    if (!kRuleFields.contains(keyField)) {
        ruleError(error, QStringLiteral("unsupported key field"));
        return false;
    }
    if (conditions.size() > 16) {
        ruleError(error, QStringLiteral("too many rule conditions"));
        return false;
    }
    for (const Condition& condition : conditions) {
        if (!kRuleFields.contains(condition.field) || condition.values.isEmpty()) {
            ruleError(error, QStringLiteral("unsupported or empty rule condition"));
            return false;
        }
        for (const QString& value : condition.values) {
            if (value.trimmed().isEmpty() || value.size() > 80) {
                ruleError(error, QStringLiteral("invalid rule condition value"));
                return false;
            }
        }
    }
    if (!validFrom.isValid() || !validTo.isValid() || validFrom > validTo) {
        ruleError(error, QStringLiteral("invalid validity interval"));
        return false;
    }
    if (target < 0 || total < 0 || (total > 0 && target > total)) {
        ruleError(error, QStringLiteral("invalid target or total"));
        return false;
    }
    return true;
}

std::optional<AwardRulePackage> AwardRulePackage::fromJson(const QByteArray& json, QString* error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        ruleError(error, QStringLiteral("invalid JSON rule package"));
        return std::nullopt;
    }
    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("format")).toString() != QLatin1String("decodxlog-award-rule-1")) {
        ruleError(error, QStringLiteral("unsupported rule package format"));
        return std::nullopt;
    }
    AwardRulePackage package;
    package.id = object.value(QStringLiteral("id")).toString();
    package.title = object.value(QStringLiteral("title")).toString();
    package.source = object.value(QStringLiteral("source")).toString();
    package.version = object.value(QStringLiteral("version")).toString();
    package.validFrom = QDate::fromString(object.value(QStringLiteral("validFrom")).toString(), Qt::ISODate);
    package.validTo = QDate::fromString(object.value(QStringLiteral("validTo")).toString(), Qt::ISODate);
    package.keyField = object.value(QStringLiteral("keyField")).toString().toUpper();
    const QJsonArray conditions = object.value(QStringLiteral("conditions")).toArray();
    for (const QJsonValue& value : conditions) {
        const QJsonObject conditionObject = value.toObject();
        AwardRulePackage::Condition condition;
        condition.field = conditionObject.value(QStringLiteral("field")).toString().toUpper();
        const QJsonValue values = conditionObject.value(QStringLiteral("values"));
        if (values.isArray()) {
            for (const QJsonValue& item : values.toArray())
                condition.values << item.toString();
        } else if (values.isString()) {
            condition.values << values.toString();
        }
        package.conditions << condition;
    }
    package.target = object.value(QStringLiteral("target")).toInt();
    package.total = object.value(QStringLiteral("total")).toInt();
    // Il fingerprint rappresenta il contenuto semantico, non spaziatura o
    // ordine accidentale del JSON ricevuto.
    QJsonArray canonicalConditions;
    for (const Condition& condition : package.conditions) {
        QJsonArray values;
        for (const QString& value : condition.values)
            values.append(value.trimmed());
        canonicalConditions.append(QJsonObject{{QStringLiteral("field"), condition.field},
                                               {QStringLiteral("values"), values}});
    }
    const QJsonObject canonical{{QStringLiteral("format"), QStringLiteral("decodxlog-award-rule-1")},
                                {QStringLiteral("id"), package.id.trimmed()},
                                {QStringLiteral("title"), package.title.trimmed()},
                                {QStringLiteral("source"), package.source.trimmed()},
                                {QStringLiteral("version"), package.version.trimmed()},
                                {QStringLiteral("validFrom"), package.validFrom.toString(Qt::ISODate)},
                                {QStringLiteral("validTo"), package.validTo.toString(Qt::ISODate)},
                                {QStringLiteral("keyField"), package.keyField},
                                {QStringLiteral("conditions"), canonicalConditions},
                                {QStringLiteral("target"), package.target},
                                {QStringLiteral("total"), package.total}};
    package.fingerprint = QString::fromLatin1(
        QCryptographicHash::hash(QJsonDocument(canonical).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
    if (!package.isValid(error))
        return std::nullopt;
    return package;
}

bool AwardRuleCatalog::add(const AwardRulePackage& package, QString* error)
{
    if (!package.isValid(error))
        return false;
    for (const AwardRulePackage& existing : m_packages) {
        if (existing.id != package.id)
            continue;
        if (existing.version == package.version) {
            if (existing.fingerprint == package.fingerprint)
                return true;
            ruleError(error, QStringLiteral("the same rule version has different content"));
            return false;
        }
        if (existing.validFrom <= package.validTo && package.validFrom <= existing.validTo) {
            ruleError(error, QStringLiteral("rule versions have overlapping validity intervals"));
            return false;
        }
    }
    m_packages << package;
    return true;
}

AwardRuleCatalog AwardRuleCatalog::loadDirectory(const QString& directory, QStringList* errors)
{
    AwardRuleCatalog catalog;
    const QDir root(directory);
    const auto report = [errors](const QString& message) {
        if (errors)
            errors->append(message);
    };
    const auto load = [&catalog, &report](const QFileInfo& info) {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) {
            report(QStringLiteral("cannot read %1").arg(info.fileName()));
            return;
        }
        QString error;
        const auto package = AwardRulePackage::fromJson(file.readAll(), &error);
        if (!package || !catalog.add(*package, &error))
            report(QStringLiteral("%1: %2").arg(info.fileName(), error));
    };

    const QFileInfoList history = QDir(root.filePath(QStringLiteral(".history")))
                                      .entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QFileInfo& info : history)
        load(info);

    const QFileInfoList files = root.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QFileInfo& info : files) {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) {
            report(QStringLiteral("cannot read %1").arg(info.fileName()));
            continue;
        }
        const QByteArray bytes = file.readAll();
        QString error;
        const auto package = AwardRulePackage::fromJson(bytes, &error);
        if (!package || !catalog.add(*package, &error)) {
            report(QStringLiteral("%1: %2").arg(info.fileName(), error));
            continue;
        }
        const QString historyDir = root.filePath(QStringLiteral(".history"));
        if (!QDir().mkpath(historyDir)) {
            report(QStringLiteral("cannot create rule history directory"));
            continue;
        }
        const QString archive = QDir(historyDir).filePath(
            package->id + QStringLiteral("-") + package->version + QStringLiteral("-")
            + package->fingerprint.left(16) + QStringLiteral(".json"));
        if (!QFileInfo::exists(archive)) {
            QFile archived(archive);
            if (!archived.open(QIODevice::WriteOnly) || archived.write(bytes) != bytes.size())
                report(QStringLiteral("cannot archive %1").arg(info.fileName()));
        }
    }
    return catalog;
}

namespace {

const QSet<QString> kIgnoredSuffixes{
    QStringLiteral("P"), QStringLiteral("M"), QStringLiteral("MM"), QStringLiteral("AM"),
    QStringLiteral("QRP"), QStringLiteral("QRPP"), QStringLiteral("A"), QStringLiteral("B"),
    QStringLiteral("LH"), QStringLiteral("J"), QStringLiteral("R"), QStringLiteral("T")};

// Il prefisso di un nominativo senza barre: tutto fino all'ultima cifra seguita
// solo da lettere (WB2ABC → WB2, 2E0ABC → 2E0, 4X1AB → 4X1).
QString basePrefix(const QString& call)
{
    for (qsizetype i = call.size() - 1; i >= 0; --i) {
        if (call.at(i).isDigit()) {
            bool lettersAfter = true;
            for (qsizetype j = i + 1; j < call.size(); ++j) {
                if (!call.at(j).isLetter()) {
                    lettersAfter = false;
                    break;
                }
            }
            if (lettersAfter && i + 1 < call.size())
                return call.left(i + 1);
        }
    }
    // Nessuna cifra: si aggiunge lo zero (regola WPX), sulle prime due lettere.
    return call.left(2) + QLatin1Char('0');
}

} // namespace

QString wpxPrefix(const QString& callsign)
{
    const QString call = callsign.trimmed().toUpper();
    if (call.isEmpty())
        return {};

    QStringList parts;
    for (const QString& p : call.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        if (!kIgnoredSuffixes.contains(p))
            parts << p;
    }
    if (parts.isEmpty())
        return {};

    // Una sola cifra dopo la barra cambia l'area: W1AW/4 → W4.
    QString areaDigit;
    if (parts.size() >= 2 && parts.last().size() == 1 && parts.last().at(0).isDigit()) {
        areaDigit = parts.takeLast();
    }

    QString prefix;
    if (parts.size() >= 2) {
        // EA8/OH2XX o OH2XX/EA8: il prefisso e' la parte piu' corta. Se non ha
        // cifre (LX/DL1ABC) si aggiunge lo zero.
        const QString shortPart = parts.at(0).size() <= parts.at(1).size() ? parts.at(0) : parts.at(1);
        prefix = shortPart;
        if (!std::any_of(prefix.begin(), prefix.end(), [](QChar c) { return c.isDigit(); }))
            prefix += QLatin1Char('0');
    } else {
        prefix = basePrefix(parts.first());
    }

    if (!areaDigit.isEmpty()) {
        // Si sostituisce l'ultima cifra del prefisso.
        for (qsizetype i = prefix.size() - 1; i >= 0; --i) {
            if (prefix.at(i).isDigit()) {
                prefix = prefix.left(i) + areaDigit;
                break;
            }
        }
    }
    return prefix;
}

const QMap<QString, QString>& usStates()
{
    static const QMap<QString, QString> states{
        {"AL", "Alabama"}, {"AK", "Alaska"}, {"AZ", "Arizona"}, {"AR", "Arkansas"}, {"CA", "California"},
        {"CO", "Colorado"}, {"CT", "Connecticut"}, {"DE", "Delaware"}, {"FL", "Florida"}, {"GA", "Georgia"},
        {"HI", "Hawaii"}, {"ID", "Idaho"}, {"IL", "Illinois"}, {"IN", "Indiana"}, {"IA", "Iowa"},
        {"KS", "Kansas"}, {"KY", "Kentucky"}, {"LA", "Louisiana"}, {"ME", "Maine"}, {"MD", "Maryland"},
        {"MA", "Massachusetts"}, {"MI", "Michigan"}, {"MN", "Minnesota"}, {"MS", "Mississippi"},
        {"MO", "Missouri"}, {"MT", "Montana"}, {"NE", "Nebraska"}, {"NV", "Nevada"}, {"NH", "New Hampshire"},
        {"NJ", "New Jersey"}, {"NM", "New Mexico"}, {"NY", "New York"}, {"NC", "North Carolina"},
        {"ND", "North Dakota"}, {"OH", "Ohio"}, {"OK", "Oklahoma"}, {"OR", "Oregon"}, {"PA", "Pennsylvania"},
        {"RI", "Rhode Island"}, {"SC", "South Carolina"}, {"SD", "South Dakota"}, {"TN", "Tennessee"},
        {"TX", "Texas"}, {"UT", "Utah"}, {"VT", "Vermont"}, {"VA", "Virginia"}, {"WA", "Washington"},
        {"WV", "West Virginia"}, {"WI", "Wisconsin"}, {"WY", "Wyoming"}};
    return states;
}

const QMap<QString, QString>& italianProvinces()
{
    static const QMap<QString, QString> list{
        {"AG", "Agrigento"}, {"AL", "Alessandria"}, {"AN", "Ancona"}, {"AO", "Aosta"},
        {"AP", "Ascoli Piceno"}, {"AQ", "L'Aquila"}, {"AR", "Arezzo"}, {"AT", "Asti"},
        {"AV", "Avellino"}, {"BA", "Bari"}, {"BG", "Bergamo"}, {"BI", "Biella"}, {"BL", "Belluno"},
        {"BN", "Benevento"}, {"BO", "Bologna"}, {"BR", "Brindisi"}, {"BS", "Brescia"},
        {"BT", "Barletta-Andria-Trani"}, {"BZ", "Bolzano"}, {"CA", "Cagliari"},
        {"CB", "Campobasso"}, {"CE", "Caserta"}, {"CH", "Chieti"}, {"CL", "Caltanissetta"},
        {"CN", "Cuneo"}, {"CO", "Como"}, {"CR", "Cremona"}, {"CS", "Cosenza"}, {"CT", "Catania"},
        {"CZ", "Catanzaro"}, {"EN", "Enna"}, {"FC", "Forli-Cesena"}, {"FE", "Ferrara"},
        {"FG", "Foggia"}, {"FI", "Firenze"}, {"FM", "Fermo"}, {"FR", "Frosinone"}, {"GE", "Genova"},
        {"GO", "Gorizia"}, {"GR", "Grosseto"}, {"IM", "Imperia"}, {"IS", "Isernia"},
        {"KR", "Crotone"}, {"LC", "Lecco"}, {"LE", "Lecce"}, {"LI", "Livorno"}, {"LO", "Lodi"},
        {"LT", "Latina"}, {"LU", "Lucca"}, {"MB", "Monza e Brianza"}, {"MC", "Macerata"},
        {"ME", "Messina"}, {"MI", "Milano"}, {"MN", "Mantova"}, {"MO", "Modena"},
        {"MS", "Massa-Carrara"}, {"MT", "Matera"}, {"NA", "Napoli"}, {"NO", "Novara"},
        {"NU", "Nuoro"}, {"OG", "Ogliastra"}, {"OR", "Oristano"}, {"OT", "Olbia-Tempio"},
        {"PA", "Palermo"}, {"PC", "Piacenza"}, {"PD", "Padova"}, {"PE", "Pescara"},
        {"PG", "Perugia"}, {"PI", "Pisa"}, {"PN", "Pordenone"}, {"PO", "Prato"}, {"PR", "Parma"},
        {"PT", "Pistoia"}, {"PU", "Pesaro-Urbino"}, {"PV", "Pavia"}, {"PZ", "Potenza"},
        {"RA", "Ravenna"}, {"RC", "Reggio Calabria"}, {"RE", "Reggio Emilia"}, {"RG", "Ragusa"},
        {"RI", "Rieti"}, {"RM", "Roma"}, {"RN", "Rimini"}, {"RO", "Rovigo"}, {"SA", "Salerno"},
        {"SI", "Siena"}, {"SO", "Sondrio"}, {"SP", "La Spezia"}, {"SR", "Siracusa"},
        {"SS", "Sassari"}, {"SU", "Sud Sardegna"}, {"SV", "Savona"}, {"TA", "Taranto"},
        {"TE", "Teramo"}, {"TN", "Trento"}, {"TO", "Torino"}, {"TP", "Trapani"}, {"TR", "Terni"},
        {"TS", "Trieste"}, {"TV", "Treviso"}, {"UD", "Udine"}, {"VA", "Varese"},
        {"VB", "Verbano-Cusio-Ossola"}, {"VC", "Vercelli"}, {"VE", "Venezia"}, {"VI", "Vicenza"},
        {"VR", "Verona"}, {"VS", "Medio Campidano"}, {"VT", "Viterbo"}, {"VV", "Vibo Valentia"},};
    return list;
}

QString italianProvince(const QString& state)
{
    // I log scrivono "NA", ma anche "I-NA" o "NA Napoli": si guardano tutti i
    // gruppi di lettere e si tiene il primo che e' una sigla vera. Piu' di cosi'
    // non si indovina, e indovinare qui vorrebbe dire contare una provincia che
    // nessuno ha lavorato.
    // Un'espressione sola per tutto il programma: costruirla a ogni QSO costava
    // piu' del resto del conto dei diplomi.
    static const QRegularExpression separators(QStringLiteral("[^A-Z]+"));
    if (state.isEmpty())
        return {};
    for (const QString& group : state.toUpper().split(separators, Qt::SkipEmptyParts)) {
        if (group.size() != 2)
            continue;
        // Carbonia-Iglesias non esiste piu': il suo territorio e' Sud Sardegna,
        // e li' vanno i QSO di prima del 2016, invece di restare senza provincia.
        const QString code = group == QLatin1String("CI") ? QStringLiteral("SU") : group;
        if (italianProvinces().contains(code))
            return code;
    }
    return {};
}

const QMap<QString, QString>& italianRegions()
{
    static const QMap<QString, QString> list{
        {"AG", "Sicilia"}, {"AL", "Piemonte"}, {"AN", "Marche"}, {"AO", "Valle d'Aosta"},
        {"AP", "Marche"}, {"AQ", "Abruzzo"}, {"AR", "Toscana"}, {"AT", "Piemonte"},
        {"AV", "Campania"}, {"BA", "Puglia"}, {"BG", "Lombardia"}, {"BI", "Piemonte"},
        {"BL", "Veneto"}, {"BN", "Campania"}, {"BO", "Emilia-Romagna"}, {"BR", "Puglia"},
        {"BS", "Lombardia"}, {"BT", "Puglia"}, {"BZ", "Trentino-Alto Adige"}, {"CA", "Sardegna"},
        {"CB", "Molise"}, {"CE", "Campania"}, {"CH", "Abruzzo"}, {"CL", "Sicilia"},
        {"CN", "Piemonte"}, {"CO", "Lombardia"}, {"CR", "Lombardia"}, {"CS", "Calabria"},
        {"CT", "Sicilia"}, {"CZ", "Calabria"}, {"EN", "Sicilia"}, {"FC", "Emilia-Romagna"},
        {"FE", "Emilia-Romagna"}, {"FG", "Puglia"}, {"FI", "Toscana"}, {"FM", "Marche"},
        {"FR", "Lazio"}, {"GE", "Liguria"}, {"GO", "Friuli-Venezia Giulia"}, {"GR", "Toscana"},
        {"IM", "Liguria"}, {"IS", "Molise"}, {"KR", "Calabria"}, {"LC", "Lombardia"},
        {"LE", "Puglia"}, {"LI", "Toscana"}, {"LO", "Lombardia"}, {"LT", "Lazio"},
        {"LU", "Toscana"}, {"MB", "Lombardia"}, {"MC", "Marche"}, {"ME", "Sicilia"},
        {"MI", "Lombardia"}, {"MN", "Lombardia"}, {"MO", "Emilia-Romagna"}, {"MS", "Toscana"},
        {"MT", "Basilicata"}, {"NA", "Campania"}, {"NO", "Piemonte"}, {"NU", "Sardegna"},
        {"OG", "Sardegna"}, {"OR", "Sardegna"}, {"OT", "Sardegna"}, {"PA", "Sicilia"},
        {"PC", "Emilia-Romagna"}, {"PD", "Veneto"}, {"PE", "Abruzzo"}, {"PG", "Umbria"},
        {"PI", "Toscana"}, {"PN", "Friuli-Venezia Giulia"}, {"PO", "Toscana"},
        {"PR", "Emilia-Romagna"}, {"PT", "Toscana"}, {"PU", "Marche"}, {"PV", "Lombardia"},
        {"PZ", "Basilicata"}, {"RA", "Emilia-Romagna"}, {"RC", "Calabria"},
        {"RE", "Emilia-Romagna"}, {"RG", "Sicilia"}, {"RI", "Lazio"}, {"RM", "Lazio"},
        {"RN", "Emilia-Romagna"}, {"RO", "Veneto"}, {"SA", "Campania"}, {"SI", "Toscana"},
        {"SO", "Lombardia"}, {"SP", "Liguria"}, {"SR", "Sicilia"}, {"SS", "Sardegna"},
        {"SU", "Sardegna"}, {"SV", "Liguria"}, {"TA", "Puglia"}, {"TE", "Abruzzo"},
        {"TN", "Trentino-Alto Adige"}, {"TO", "Piemonte"}, {"TP", "Sicilia"}, {"TR", "Umbria"},
        {"TS", "Friuli-Venezia Giulia"}, {"TV", "Veneto"}, {"UD", "Friuli-Venezia Giulia"},
        {"VA", "Lombardia"}, {"VB", "Piemonte"}, {"VC", "Piemonte"}, {"VE", "Veneto"},
        {"VI", "Veneto"}, {"VR", "Veneto"}, {"VS", "Sardegna"}, {"VT", "Lazio"}, {"VV", "Calabria"},};
    return list;
}

QString dciReference(const QString& sig, const QString& sigInfo,
                     const QString& comment, const QString& notes)
{
    // Dentro SIG_INFO il riferimento e' gia' solo: basta che sia fatto bene.
    static const QRegularExpression bare(QStringLiteral("^([A-Z]{2})[ -]?(\\d{3})$"));
    // Nel testo libero invece si pretende la parola DCI davanti: senza, un
    // "TNX 001" qualunque diventerebbe un castello.
    static const QRegularExpression tagged(
        QStringLiteral("\\bDCI[ :-]*([A-Z]{2})[ -]?(\\d{3})\\b"));

    if (sig.trimmed().compare(QLatin1String("DCI"), Qt::CaseInsensitive) == 0) {
        const QRegularExpressionMatch m = bare.match(sigInfo.trimmed().toUpper());
        if (m.hasMatch() && italianProvinces().contains(m.captured(1)))
            return m.captured(1) + m.captured(2);
    }
    for (const QString& text : {sigInfo, comment, notes}) {
        // Senza la parola DCI l'espressione non serve: si guarda prima quella.
        if (!text.contains(QLatin1String("DCI"), Qt::CaseInsensitive))
            continue;
        const QRegularExpressionMatch m = tagged.match(text.toUpper());
        if (m.hasMatch() && italianProvinces().contains(m.captured(1)))
            return m.captured(1) + m.captured(2);
    }
    return {};
}

QString sigReference(const QString& award, const QString& sig, const QString& sigInfo,
                     const QString& comment, const QString& notes)
{
    static const QRegularExpression bare(QStringLiteral("^([A-Z0-9]{1,4})[ /-]?(\\d{1,4})$"));
    if (sig.trimmed().compare(award, Qt::CaseInsensitive) == 0) {
        const QRegularExpressionMatch m = bare.match(sigInfo.trimmed().toUpper());
        if (m.hasMatch())
            return m.captured(1) + QLatin1Char('-') + m.captured(2);
    }
    // Nel testo libero ci vuole il nome del diploma davanti. L'espressione si
    // costruisce una volta per diploma, e si usa solo se il nome c'e': farla a
    // ogni QSO costava secondi su un log grande.
    static QHash<QString, QRegularExpression> taggedByAward;
    const QString name = award.toUpper();
    auto found = taggedByAward.constFind(name);
    if (found == taggedByAward.constEnd())
        found = taggedByAward.insert(name, QRegularExpression(QStringLiteral("\\b%1[ :-]*([A-Z0-9]{1,4})[ /-]?(\\d{1,4})\\b")
                                                                   .arg(QRegularExpression::escape(name))));
    const QRegularExpression tagged = found.value();
    for (const QString& text : {sigInfo, comment, notes}) {
        if (!text.contains(name, Qt::CaseInsensitive))
            continue;
        const QRegularExpressionMatch m = tagged.match(text.toUpper());
        if (m.hasMatch())
            return m.captured(1) + QLatin1Char('-') + m.captured(2);
    }
    return {};
}

QStringList italianRegionNames()
{
    QStringList out;
    for (const QString& r : italianRegions())
        if (!out.contains(r))
            out << r;
    out.sort();
    return out;
}

const QMap<QString, QString>& continents()
{
    static const QMap<QString, QString> list{
        {"EU", QStringLiteral("Europa")},        {"NA", QStringLiteral("Nord America")},
        {"SA", QStringLiteral("Sud America")},   {"AS", QStringLiteral("Asia")},
        {"AF", QStringLiteral("Africa")},        {"OC", QStringLiteral("Oceania")},
        {"AN", QStringLiteral("Antartide")}};
    return list;
}

const QMap<QString, QString>& japanPrefectures()
{
    // I numeri sono quelli di ADIF (Primary Administrative Subdivision, Japan).
    static const QMap<QString, QString> list{
        {"01", "Hokkaido"},  {"02", "Aomori"},    {"03", "Iwate"},     {"04", "Akita"},
        {"05", "Yamagata"},  {"06", "Miyagi"},    {"07", "Fukushima"}, {"08", "Niigata"},
        {"09", "Nagano"},    {"10", "Tokyo"},     {"11", "Kanagawa"},  {"12", "Chiba"},
        {"13", "Saitama"},   {"14", "Ibaraki"},   {"15", "Tochigi"},   {"16", "Gunma"},
        {"17", "Yamanashi"}, {"18", "Shizuoka"},  {"19", "Gifu"},      {"20", "Aichi"},
        {"21", "Mie"},       {"22", "Kyoto"},     {"23", "Shiga"},     {"24", "Nara"},
        {"25", "Osaka"},     {"26", "Wakayama"},  {"27", "Hyogo"},     {"28", "Toyama"},
        {"29", "Fukui"},     {"30", "Ishikawa"},  {"31", "Okayama"},   {"32", "Shimane"},
        {"33", "Yamaguchi"}, {"34", "Tottori"},   {"35", "Hiroshima"}, {"36", "Kagawa"},
        {"37", "Tokushima"}, {"38", "Ehime"},     {"39", "Kochi"},     {"40", "Fukuoka"},
        {"41", "Saga"},      {"42", "Nagasaki"},  {"43", "Kumamoto"},  {"44", "Oita"},
        {"45", "Miyazaki"},  {"46", "Kagoshima"}, {"47", "Okinawa"}};
    return list;
}

QString japanPrefecture(const QString& state)
{
    // Nei log si trova "12", "JA12", "12 Chiba": conta il numero.
    const QString text = state.trimmed().toUpper();
    QString digits;
    for (const QChar c : text) {
        if (c.isDigit())
            digits += c;
        else if (!digits.isEmpty())
            break;
    }
    if (digits.isEmpty())
        return {};
    const int number = digits.toInt();
    if (number < 1 || number > 47)
        return {};
    const QString key = QStringLiteral("%1").arg(number, 2, 10, QLatin1Char('0'));
    return japanPrefectures().contains(key) ? key : QString();
}

QString japanDistrict(const QString& callsign)
{
    // Il distretto e' la cifra del nominativo giapponese: JA1AA → 1, 7K4XYZ → 4.
    // Si guarda la parte principale, saltando i suffissi con la barra.
    const QString call = callsign.trimmed().toUpper();
    QString base = call;
    for (const QString& part : call.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        if (part.size() >= 3) {
            base = part;
            break;
        }
    }
    for (qsizetype i = 0; i < base.size(); ++i) {
        if (!base.at(i).isDigit())
            continue;
        // La cifra del distretto e' quella che separa prefisso e suffisso:
        // dopo di lei ci sono solo lettere.
        bool lettersAfter = i + 1 < base.size();
        for (qsizetype j = i + 1; j < base.size(); ++j) {
            if (!base.at(j).isLetter()) {
                lettersAfter = false;
                break;
            }
        }
        if (lettersAfter)
            return base.mid(i, 1);
    }
    return {};
}

QString japanJarlCode(const QString& county)
{
    QString digits;
    for (const QChar c : county) {
        if (c.isDigit())
            digits += c;
    }
    if (digits.size() < 4 || digits.size() > 6)
        return {};
    const int prefecture = digits.left(2).toInt();
    if (prefecture < 1 || prefecture > 47)
        return {};
    return digits;
}

bool isJapanGun(const QString& jarlCode)
{
    return jarlCode.size() == 5;
}

} // namespace awards

int AwardResult::worked(bool officialOnly) const
{
    if (!officialOnly)
        return static_cast<int>(items.size());
    return static_cast<int>(std::count_if(items.cbegin(), items.cend(), [](const AwardItem& i) {
        return i.countsForAward;
    }));
}

int AwardResult::confirmed(bool officialOnly) const
{
    return static_cast<int>(std::count_if(items.cbegin(), items.cend(), [officialOnly](const AwardItem& i) {
        return (!officialOnly || i.countsForAward) && i.confirmed();
    }));
}

QList<BandTotal> AwardResult::bandTotals(const QStringList& bands, bool officialOnly) const
{
    QList<BandTotal> out;
    for (const QString& band : bands) {
        BandTotal t;
        t.band = band;
        for (const AwardItem& i : items) {
            if (officialOnly && !i.countsForAward)
                continue;
            if (i.bandsWorked.contains(band))
                ++t.worked;
            if (i.bandsConfirmed.contains(band))
                ++t.confirmed;
        }
        out << t;
    }
    return out;
}

AwardCalculator::AwardCalculator(DxccName dxccName)
    : m_dxccName(std::move(dxccName))
{
}

QStringList AwardCalculator::awardIds()
{
    return {QStringLiteral("dxcc"), QStringLiteral("ft2"), QStringLiteral("wac"), QStringLiteral("waac"),
            QStringLiteral("waz"), QStringLiteral("was"), QStringLiteral("waip"),
            QStringLiteral("waja"), QStringLiteral("ajd"),
            QStringLiteral("jcc"), QStringLiteral("jcg"), QStringLiteral("wpx"),
            QStringLiteral("grids"), QStringLiteral("iota"), QStringLiteral("pota"), QStringLiteral("sota"),
            QStringLiteral("wwff"), QStringLiteral("dci"), QStringLiteral("dcpc"),
            QStringLiteral("wair"), QStringLiteral("iia"), QStringLiteral("difi"), QStringLiteral("dai")};
}

namespace {

bool modeMatches(const QString& group, const QString& mode, const QString& submode)
{
    if (group.isEmpty())
        return true;
    const QString effective = modes::displayName(mode, submode);
    if (group == QLatin1String("FT2"))
        return effective == QLatin1String("FT2");
    if (group == QLatin1String("FT8"))
        return effective == QLatin1String("FT8");
    if (group == QLatin1String("CW"))
        return modes::groupFor(effective) == QLatin1String("CW");
    if (group == QLatin1String("PHONE"))
        return modes::groupFor(effective) == QLatin1String("PHONE");
    if (group == QLatin1String("DIGITAL"))
        return modes::groupFor(effective) == QLatin1String("DATA");
    return true;
}

// I QSO che il DCI considera validi (regolamento, punti 3 e 4): dal 1 gennaio
// 2001, dai 160 ai 2 metri, in SSB, CW o digitale. Un castello lavorato in FM,
// o prima che il diploma esistesse, non si conta: meglio dirlo qui che vederselo
// scartare da chi rilascia il diploma.
bool dciCounts(const QDateTime& when, const QString& band, const QString& mode)
{
    static const QDate start(2001, 1, 1);
    if (when.isValid() && when.date() < start)
        return false;
    static const QSet<QString> bands{
        QStringLiteral("160m"), QStringLiteral("80m"), QStringLiteral("60m"), QStringLiteral("40m"),
        QStringLiteral("30m"), QStringLiteral("20m"), QStringLiteral("17m"), QStringLiteral("15m"),
        QStringLiteral("12m"), QStringLiteral("10m"), QStringLiteral("6m"), QStringLiteral("4m"),
        QStringLiteral("2m")};
    if (!band.isEmpty() && !bands.contains(band.toLower()))
        return false;
    return mode != QLatin1String("FM") && mode != QLatin1String("AM");
}

} // namespace

namespace awards {

QStringList creditServices()
{
    return {QStringLiteral("lotw"), QStringLiteral("card"), QStringLiteral("eqsl"), QStringLiteral("qrz")};
}

QStringList officialCredits(const QString& awardId)
{
    // ARRL: per il DXCC e il WAS valgono LoTW e la cartolina, non eQSL ne' QRZ.
    if (awardId == QLatin1String("dxcc") || awardId == QLatin1String("was"))
        return {QStringLiteral("lotw"), QStringLiteral("card")};
    return {};
}

QStringList creditsFor(const QString& awardId, const AwardFilter& filter)
{
    if (filter.credits.contains(awardId))
        return filter.credits.value(awardId);
    const QStringList official = officialCredits(awardId);
    QStringList general;
    if (filter.confirmLotw)
        general << QStringLiteral("lotw");
    if (filter.confirmCard)
        general << QStringLiteral("card");
    if (filter.confirmEqsl)
        general << QStringLiteral("eqsl");
    if (official.isEmpty())
        return general;
    // Le regole del diploma, dentro quello che l'operatore accetta in generale:
    // chi spegne LoTW lo spegne anche per il DXCC.
    QStringList out;
    for (const QString& s : official) {
        if (general.contains(s))
            out << s;
    }
    return out;
}

} // namespace awards

namespace {

// Chiave di ordinamento: numeri come numeri, il resto come testo.
bool keyLess(const AwardItem& a, const AwardItem& b)
{
    bool okA = false, okB = false;
    const int na = a.key.toInt(&okA);
    const int nb = b.key.toInt(&okB);
    if (okA && okB)
        return na < nb;
    return a.key < b.key;
}

} // namespace

QList<AwardResult> AwardCalculator::compute(const LogDatabase& db, const AwardFilter& filter,
                                            AwardContributions* contributions) const
{
    decolog::StartupSpan trace("AwardCalculator::compute");
    if (contributions) contributions->qsoIds.clear();
    struct Builder {
        AwardResult result;
        QHash<QString, AwardItem> items;
    };
    QHash<QString, Builder> builders;
    auto define = [&builders](const char* id, const QString& title, int target, int total) {
        Builder b;
        b.result.id = QLatin1String(id);
        b.result.title = title;
        b.result.target = target;
        b.result.total = total;
        builders.insert(b.result.id, b);
    };
    QStringList allAwardIds = awardIds();
    QHash<QString, awards::AwardRulePackage> rulePackages;
    define("dxcc", QStringLiteral("DXCC"), 100, 340);
    define("ft2", QStringLiteral("FT2 Award"), 100, 0);
    // I sei continenti dell'IARU. L'Antartide non fa numero per il WAC, ma chi
    // ce l'ha vuole vederla: entra nell'elenco e non nel traguardo.
    define("wac", QStringLiteral("WAC"), 6, 6);
    // Worked All Africa: le entita' DXCC del continente africano. Quante siano
    // lo dice il cty.csv, quindi traguardo e totale si mettono a posto dopo.
    define("waac", QStringLiteral("WAAC"), 0, 0);
    define("waz", QStringLiteral("WAZ"), 40, 40);
    define("was", QStringLiteral("WAS"), 50, 50);
    // Le 47 prefetture giapponesi (WAJA) e i 10 distretti (AJD): il Giappone
    // mette la prefettura in STATE e il distretto nella cifra del nominativo.
    define("waja", QStringLiteral("WAJA"), 47, 47);
    define("ajd", QStringLiteral("AJD"), 10, 10);
    // JCC e JCG: le citta' e i distretti del JARL, col numero che sta in CNTY.
    // Il primo traguardo e' cento dell'uno e cento dell'altro; quante siano in
    // tutto lo decide il JARL e cambia, quindi il totale resta senza numero.
    define("jcc", QStringLiteral("JCC"), 100, 0);
    define("jcg", QStringLiteral("JCG"), 100, 0);
    define("wpx", QStringLiteral("WPX"), 300, 0);
    define("grids", QCoreApplication::translate("Awards", "Grids"), 100, 0);
    define("iota", QStringLiteral("IOTA"), 100, 0);
    define("pota", QStringLiteral("POTA"), 0, 0);
    define("sota", QStringLiteral("SOTA"), 0, 0);
    define("wwff", QStringLiteral("WWFF"), 44, 0);
    // WAIP: le 110 province italiane. Il traguardo del diploma base e' 75 per
    // gli italiani e 60 per gli altri; qui sta il piu' alto dei due, cosi' la
    // barra non da' per preso un diploma che per il regolamento non lo e'.
    define("waip", QStringLiteral("WAIP"), 75, 110);
    // DCI, i castelli d'Italia (regolamento del 15 settembre 2025, punto 2):
    // il diploma si prende con 30 castelli in almeno 5 regioni diverse, e uno
    // dev'essere della provincia di Cuneo. Per le stazioni non italiane ne
    // bastano 20, sempre in 5 regioni. Quanti castelli esistano lo decide chi
    // tiene l'elenco e cresce ogni anno, quindi niente totale.
    define("dci", QStringLiteral("DCI"), 30, 0);
    // DCPC, i castelli della sola provincia di Cuneo (punto 7): dieci, e si
    // chiede dopo aver preso il DCI.
    define("dcpc", QStringLiteral("DCPC"), 10, 0);
    // WAIR: le venti regioni italiane, dalle province del WAIP.
    define("wair", QStringLiteral("WAIR"), 20, 20);
    // I diplomi a riferimento che si segnano con SIG/SIG_INFO: isole
    // italiane (IIA), fari (DIFI), abbazie (DAI). Quanti riferimenti esistano
    // lo decide chi tiene l'elenco: niente totale, traguardo base di chi li
    // rilascia.
    define("iia", QStringLiteral("IIA"), 20, 0);
    define("difi", QStringLiteral("DIFI"), 25, 0);
    define("dai", QStringLiteral("DAI"), 25, 0);
    // A generic four-character locator count is useful, but it is not VUCC:
    // VUCC additionally depends on band, station-location/account rules and
    // award evidence. Keep that distinction visible to callers.
    builders[QStringLiteral("grids")].result.title =
        QCoreApplication::translate("Awards", "Grid statistics");
    builders[QStringLiteral("grids")].result.rulesSource =
        QStringLiteral("https://www.arrl.org/vucc");
    builders[QStringLiteral("grids")].result.verifier =
        QStringLiteral("ARRL VUCC award manager");
    for (const awards::AwardRulePackage& package : m_rulePackages) {
        QString error;
        if (!package.isValid(&error) || allAwardIds.contains(package.id) || rulePackages.contains(package.id))
            continue;
        define(package.id.toUtf8().constData(), package.title, package.target, package.total);
        rulePackages.insert(package.id, package);
        allAwardIds << package.id;
    }

    QSqlQuery q(db.connection());
    q.setForwardOnly(true);
    q.exec(QStringLiteral(
        "SELECT qso.id, qso.call, qso.band, qso.mode, IFNULL(qso.submode, ''), qso.dxcc, qso.cqz, qso.state, qso.gridsquare, qso.iota, qso.pota_ref, qso.sota_ref, "
        "wwff_ref, qso_datetime_on, IFNULL(station_profile_id, 0), IFNULL(tags, ''), "
        "IFNULL(cont, ''), IFNULL(cnty, ''), "
        "IFNULL(sig, ''), IFNULL(sig_info, ''), IFNULL(comment, ''), IFNULL(notes, ''), "
        "IFNULL(prop_mode, ''), IFNULL(sat_name, ''), IFNULL(station_profile.uuid, ''), "
        "IFNULL(station_profile.valid_from, ''), IFNULL(station_profile.valid_to, ''), "
        "IFNULL(station_profile.award_account, '') "
        "FROM qso LEFT JOIN station_profile ON station_profile.id = qso.station_profile_id "
        "WHERE qso.deleted = 0 ORDER BY qso.qso_datetime_on"));
    // Le conferme in un insieme per servizio, non una sottoquery per ogni QSO:
    // su un log da un milione era la meta' del tempo. Solo i servizi che
    // qualche diploma accetta.
    QHash<QString, QSet<qint64>> confirmedBy;
    QHash<QString, QList<const QSet<qint64>*>> creditSets;
    for (const QString& award : allAwardIds) {
        for (const QString& service : awards::creditsFor(award, filter)) {
            if (!confirmedBy.contains(service))
                confirmedBy.insert(service, db.confirmedIds(service));
        }
    }
    for (const QString& award : allAwardIds) {
        QList<const QSet<qint64>*> sets;
        for (const QString& service : awards::creditsFor(award, filter))
            sets << &confirmedBy[service];
        creditSets.insert(award, sets);
    }
    auto confirmedFor = [&creditSets](const QString& award, qint64 id) {
        for (const QSet<qint64>* set : creditSets.value(award)) {
            if (set->contains(id))
                return true;
        }
        return false;
    };

    // Inferred callbook/geocoding values are useful context, but a current
    // residence must not silently become historical award evidence.
    QHash<qint64, QSet<QString>> inferredFields;
    {
        QSqlQuery enrichmentQuery(db.connection());
        enrichmentQuery.setForwardOnly(true);
        if (enrichmentQuery.exec(QStringLiteral(
                "SELECT qso_id, field FROM qso_enrichment WHERE kind = 'inferred'"))) {
            while (enrichmentQuery.next())
                inferredFields[enrichmentQuery.value(0).toLongLong()].insert(enrichmentQuery.value(1).toString());
        }
    }

    const QSet<int> usaEntities{291, 6, 110};   // USA, Alaska, Hawaii
    int dxccSkipped = 0;   // QSO fuori dal DXCC di adesso (cty.xml)
    static const QRegularExpression iotaRef(QStringLiteral("^(AF|AN|AS|EU|NA|OC|SA)-\\d{3}$"));

    while (q.next()) {
        const QString band = q.value(2).toString();
        const QString mode = q.value(3).toString();
        const QString submode = q.value(4).toString();
        const QString propagation = q.value(22).toString().trimmed().toUpper();
        const QString satelliteName = q.value(23).toString().trimmed();
        const bool satellite = propagation == QLatin1String("SAT") || !satelliteName.isEmpty();
        if (!filter.band.isEmpty() && band != filter.band)
            continue;
        if (!modeMatches(filter.modeGroup, mode, submode))
            continue;
        if (filter.stationProfileId > 0 && q.value(14).toLongLong() != filter.stationProfileId)
            continue;
        if (!filter.tag.isEmpty()) {
            const QStringList tags = LogDatabase::splitTags(q.value(15).toString());
            if (!std::any_of(tags.cbegin(), tags.cend(), [&filter](const QString& t) {
                    return t.compare(filter.tag, Qt::CaseInsensitive) == 0;
                }))
                continue;
        }

        const qint64 id = q.value(0).toLongLong();
        const QString call = q.value(1).toString();
        const QSet<QString> inferred = inferredFields.value(id);
        const int dxcc = inferred.contains(QStringLiteral("DXCC")) ? 0 : q.value(5).toInt();
        const int cqzValue = inferred.contains(QStringLiteral("CQZ")) ? 0 : q.value(6).toInt();
        const QString stateValue = inferred.contains(QStringLiteral("STATE")) ? QString() : q.value(7).toString();
        const QString gridValue = inferred.contains(QStringLiteral("GRIDSQUARE")) ? QString() : q.value(8).toString();
        const QDateTime on = QDateTime::fromString(q.value(13).toString(), Qt::ISODate).toUTC();
        const QString profileUuid = q.value(24).toString();
        const QString validFrom = q.value(25).toString();
        const QString validTo = q.value(26).toString();
        const QString awardAccount = q.value(27).toString();
        if (!filter.awardAccountUuid.isEmpty()
            && profileUuid != filter.awardAccountUuid
            && awardAccount != filter.awardAccountUuid)
            continue;
        if (!filter.awardAccountUuid.isEmpty()
            && ((!validFrom.isEmpty() && on.date() < QDate::fromString(validFrom, Qt::ISODate))
                || (!validTo.isEmpty() && on.date() > QDate::fromString(validTo, Qt::ISODate))))
            continue;
        // Le regole dell'ARRL: i QSO sui 60 metri non valgono per nessun suo
        // diploma, DXCC compreso (le conferme valide le dice creditsFor). Si
        // contano solo nell'override personale (count60m).
        const bool arrlBand = filter.count60m || band.compare(QLatin1String("60m"), Qt::CaseInsensitive) != 0;

        auto addAs = [&](const char* award, const QString& key, const QString& name, bool isConfirmed,
                         bool countsForAward = true) {
            if (key.isEmpty())
                return;
            if (countsForAward && contributions && contributions->awards.contains(QLatin1String(award)))
                contributions->qsoIds[QLatin1String(award)][key].insert(id);
            Builder& b = builders[QLatin1String(award)];
            AwardItem& item = b.items[key];
            if (item.qsoCount == 0) {
                item.key = key;
                item.name = name;
                item.first = on;
                item.firstQsoId = id;
                item.firstCall = call;
                item.countsForAward = countsForAward;
            }
            item.last = on;
            ++item.qsoCount;
            item.bandsWorked.insert(band);
            if (isConfirmed)
                item.bandsConfirmed.insert(band);
        };
        auto add = [&](const char* award, const QString& key, const QString& name, bool countsForAward = true) {
            addAs(award, key, name, confirmedFor(QLatin1String(award), id), countsForAward);
        };

        auto addRulePackage = [&](const awards::AwardRulePackage& package) {
            if (on.date() < package.validFrom || on.date() > package.validTo)
                return;
            const auto conditionValue = [&](const QString& field) {
                if (field == QLatin1String("DXCC")) return dxcc > 0 ? QString::number(dxcc) : QString();
                if (field == QLatin1String("STATE")) return stateValue.trimmed().toUpper();
                if (field == QLatin1String("CQZ")) return cqzValue >= 1 && cqzValue <= 40 ? QString::number(cqzValue) : QString();
                if (field == QLatin1String("GRID4")) return gridValue.trimmed().toUpper().left(4);
                if (field == QLatin1String("BAND")) return band;
                if (field == QLatin1String("CALL_PREFIX")) return awards::wpxPrefix(call);
                return QString();
            };
            for (const auto& condition : package.conditions) {
                const QString actual = conditionValue(condition.field);
                bool match = false;
                for (const QString& expected : condition.values) {
                    if (actual.compare(expected.trimmed(), Qt::CaseInsensitive) == 0) {
                        match = true;
                        break;
                    }
                }
                if (!match)
                    return;
            }
            QString key;
            if (package.keyField == QLatin1String("DXCC"))
                key = dxcc > 0 ? QString::number(dxcc) : QString();
            else if (package.keyField == QLatin1String("STATE"))
                key = stateValue.trimmed().toUpper();
            else if (package.keyField == QLatin1String("CQZ"))
                key = cqzValue >= 1 && cqzValue <= 40 ? QString::number(cqzValue) : QString();
            else if (package.keyField == QLatin1String("GRID4"))
                key = gridValue.trimmed().toUpper().left(4);
            else if (package.keyField == QLatin1String("BAND"))
                key = band;
            else if (package.keyField == QLatin1String("CALL_PREFIX"))
                key = awards::wpxPrefix(call);
            if (!key.isEmpty())
                addAs(package.id.toUtf8().constData(), key, QString(), confirmedFor(package.id, id));
        };

        if (dxcc > 0) {
            const QString name = m_dxccName ? m_dxccName(dxcc) : QString();
            const bool dxccValid = !(m_rules.invalid && m_rules.invalid(call, on))
                                   && !(m_rules.deleted && m_rules.deleted(dxcc));
            if (!dxccValid)
                ++dxccSkipped;
            if (arrlBand && !satellite && dxccValid)
                add("dxcc", QString::number(dxcc), name);
            if (submode == QLatin1String("FT2"))
                add("ft2", QString::number(dxcc), name);
        }
        const QString continent = q.value(16).toString().trimmed().toUpper();
        if (awards::continents().contains(continent))
            add("wac", continent, awards::continents().value(continent), continent != QLatin1String("AN"));
        // Worked All Africa: un'entita' africana per volta, col suo nome.
        if (continent == QLatin1String("AF") && dxcc > 0) {
            add("waac", QString::number(dxcc),
                m_dxccName ? m_dxccName(dxcc) : QString());
        }
        const int cqz = cqzValue;
        if (cqz >= 1 && cqz <= 40)
            add("waz", QString::number(cqz), QString());
        const QString state = stateValue.trimmed().toUpper();
        if (arrlBand && !satellite && usaEntities.contains(dxcc) && awards::usStates().contains(state))
            add("was", state, awards::usStates().value(state));
        // Giappone: la prefettura sta in STATE (ADIF la scrive col numero,
        // "12" o "JA12"), il distretto e' la cifra del nominativo.
        if (dxcc == 339) {
            const QString prefecture = awards::japanPrefecture(state);
            if (!prefecture.isEmpty())
                add("waja", prefecture, awards::japanPrefectures().value(prefecture));
            const QString district = awards::japanDistrict(call);
            if (!district.isEmpty())
                add("ajd", district, QString());
            // La citta' o il distretto: il numero JARL sta nel campo CNTY, e le
            // prime due cifre dicono la prefettura, che qui fa da nome.
            const QString jarl = awards::japanJarlCode(q.value(17).toString());
            if (!jarl.isEmpty()) {
                const QString prefecture = awards::japanPrefectures().value(jarl.left(2));
                add(awards::isJapanGun(jarl) ? "jcg" : "jcc", jarl, prefecture);
            }
        }
        // Italia (248) e Sardegna (225) sono due entita' DXCC ma un paese solo:
        // il WAIP conta le province di tutte e due.
        if (dxcc == 248 || dxcc == 225) {
            const QString province = awards::italianProvince(state);
            if (!province.isEmpty()) {
                add("waip", province, awards::italianProvinces().value(province));
                const QString region = awards::italianRegions().value(province);
                if (!region.isEmpty())
                    add("wair", region, region);
            }
        }
        for (const char* sigAward : {"iia", "difi", "dai"}) {
            const QString ref = awards::sigReference(QLatin1String(sigAward), q.value(18).toString(),
                                                     q.value(19).toString(), q.value(20).toString(),
                                                     q.value(21).toString());
            if (!ref.isEmpty())
                add(sigAward, ref, awards::italianProvinces().value(ref.section(QLatin1Char('-'), 0, 0)));
        }
        // Il castello: il riferimento sta in SIG_INFO quando il log e' fatto
        // bene, nel commento o nelle note quando e' fatto come capita.
        const QString castle = awards::dciReference(q.value(18).toString(), q.value(19).toString(),
                                                    q.value(20).toString(), q.value(21).toString());
        if (!castle.isEmpty() && dciCounts(on, band, mode)) {
            const QString province = castle.left(2);
            add("dci", castle, awards::italianProvinces().value(province));
            if (province == QLatin1String("CN"))
                add("dcpc", castle, awards::italianRegions().value(province));
        }
        add("wpx", awards::wpxPrefix(call), QString());
        const QString grid = gridValue.trimmed().toUpper();
        if (grid.size() >= 4)
            add("grids", grid.left(4), QString());
        const QString iota = q.value(9).toString().trimmed().toUpper();
        if (iotaRef.match(iota).hasMatch())
            add("iota", iota, QString());
        add("pota", q.value(10).toString().trimmed().toUpper(), QString());
        add("sota", q.value(11).toString().trimmed().toUpper(), QString());
        add("wwff", q.value(12).toString().trimmed().toUpper(), QString());
        for (const awards::AwardRulePackage& package : std::as_const(rulePackages))
            addRulePackage(package);
    }

    QList<AwardResult> out;
    for (const QString& id : allAwardIds) {
        Builder& b = builders[id];
        b.result.items = b.items.values();
        std::sort(b.result.items.begin(), b.result.items.end(), keyLess);
        if (id == QLatin1String("dci")) {
            // Il numero da solo non basta a sapere se il diploma si puo'
            // chiedere: servono le regioni, e un castello della provincia di
            // Cuneo. Meglio leggerlo che contarselo a mano.
            QSet<QString> regions;
            bool cuneo = false;
            for (const AwardItem& item : std::as_const(b.result.items)) {
                const QString province = item.key.left(2);
                const QString region = awards::italianRegions().value(province);
                if (!region.isEmpty())
                    regions.insert(region);
                if (province == QLatin1String("CN"))
                    cuneo = true;
            }
            b.result.requirement =
                QCoreApplication::translate(
                    "Awards",
                    "Diploma: 30 castles in 5 different regions (20 for stations outside Italy), "
                    "at least one in the province of Cuneo. So far: %1 in %2 regions, Cuneo %3.")
                    .arg(b.result.items.size())
                    .arg(regions.size())
                    .arg(cuneo ? QCoreApplication::translate("Awards", "yes")
                               : QCoreApplication::translate("Awards", "not yet"));
        } else if (id == QLatin1String("dcpc")) {
            b.result.requirement = QCoreApplication::translate(
                "Awards", "Ten different castles in the province of Cuneo, once you hold the DCI.");
        } else if (id == QLatin1String("wair")) {
            QStringList missing = awards::italianRegionNames();
            for (const AwardItem& item : std::as_const(b.result.items))
                missing.removeAll(item.key);
            b.result.requirement = missing.isEmpty()
                ? QCoreApplication::translate("Awards", "All twenty regions worked.")
                : QCoreApplication::translate("Awards", "Missing: %1.").arg(missing.join(QStringLiteral(", ")));
        } else if (id == QLatin1String("iia") || id == QLatin1String("difi") || id == QLatin1String("dai")) {
            b.result.requirement = QCoreApplication::translate(
                "Awards", "The reference is read from SIG/SIG_INFO (SIG = %1) or from a comment like \"%1 LI-001\".")
                                       .arg(b.result.title);
        } else if (id == QLatin1String("wac")) {
            b.result.requirement = QCoreApplication::translate(
                "Awards", "Official WAC counts six continents; Antarctica is shown but not counted.");
        } else if (id == QLatin1String("dxcc") || id == QLatin1String("was")) {
            // Che cosa si conta dei 60 metri, detto ogni volta: il numero cambia.
            QStringList said;
            said << (filter.count60m
                         ? QCoreApplication::translate("Awards", "QSOs on 60 m are counted here, but the ARRL does not "
                                                                 "accept them: for the official count switch off \"60 m\".")
                         : QCoreApplication::translate("Awards", "ARRL rules: QSOs on 60 m do not count."));
            if (id == QLatin1String("dxcc"))
                said << QCoreApplication::translate("Awards", "eQSL is not a confirmation for DXCC.");
            if (id == QLatin1String("dxcc") && dxccSkipped > 0)
                said << QCoreApplication::translate(
                    "Awards", "%n QSO(s) with deleted entities or operations not accepted by the ARRL (Club Log) are left out.",
                    nullptr, dxccSkipped);
            said << QCoreApplication::translate(
                "Awards", "Satellite QSOs stay in the log but are excluded from terrestrial DXCC/WAS credit.");
            b.result.requirement = said.join(QLatin1Char(' '));
        } else if (id == QLatin1String("waip")) {
            b.result.requirement = QCoreApplication::translate(
                "Awards", "Diploma: 75 provinces for Italian stations, 60 for the others.");
        }
        out << b.result;
    }
    return out;
}

} // namespace decolog::core
