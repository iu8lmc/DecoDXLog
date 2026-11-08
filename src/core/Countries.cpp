#include "core/Countries.h"

#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <algorithm>

namespace decolog::core {

namespace {

// Suffissi che non cambiano l'entità: portatile, mobile, QRP, beacon...
const QSet<QString> kIgnoredSuffixes{
    QStringLiteral("P"), QStringLiteral("M"), QStringLiteral("QRP"), QStringLiteral("QRPP"),
    QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("LH"), QStringLiteral("J"),
    QStringLiteral("R"), QStringLiteral("T"), QStringLiteral("N"), QStringLiteral("E"),
    QStringLiteral("SK"), QStringLiteral("SOTA"), QStringLiteral("POTA"), QStringLiteral("YL"),
    QStringLiteral("2K"), QStringLiteral("FF")};

} // namespace

bool Countries::load(const QByteArray& csv)
{
    m_entities.clear();
    m_exact.clear();
    m_prefixes.clear();
    m_names.clear();
    m_primary.clear();
    m_longestPrefix = 0;
    m_version.clear();

    // Le sostituzioni per prefisso: (CQ) [ITU] <lat/lon> {continente} ~fuso~.
    static const QRegularExpression overrides(QStringLiteral(R"(\((\d+)\)|\[(\d+)\]|<[^>]*>|\{[^}]*\}|~[^~]*~)"));

    const QList<QByteArray> lines = csv.split('\n');
    for (const QByteArray& raw : lines) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.isEmpty())
            continue;
        // Dieci campi; l'ultimo (i prefissi) non contiene virgole.
        const QStringList f = line.split(QLatin1Char(','));
        if (f.size() < 10)
            continue;

        DxccEntity e;
        QString primary = f.at(0).trimmed();
        const bool waeOnly = primary.startsWith(QLatin1Char('*'));
        if (waeOnly)
            primary.remove(0, 1);
        e.prefix = primary;
        e.name = f.at(1).trimmed();
        e.dxcc = f.at(2).toInt();
        e.continent = f.at(3).trimmed();
        e.cqZone = f.at(4).toInt();
        e.ituZone = f.at(5).toInt();
        e.lat = f.at(6).toDouble();
        e.lon = -f.at(7).toDouble();
        e.utcOffset = -f.at(8).toDouble();
        if (e.dxcc <= 0)
            continue;
        const int index = static_cast<int>(m_entities.size());
        if (!waeOnly && !m_names.contains(e.dxcc)) {
            m_names.insert(e.dxcc, e.name);
            m_primary.append(index);
        }

        m_entities.append(e);

        QString prefixes = f.mid(9).join(QLatin1Char(','));
        prefixes.remove(QLatin1Char(';'));
        for (const QString& token : prefixes.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            Match m;
            m.entityIndex = index;
            QString key = token;
            auto it = overrides.globalMatch(token);
            while (it.hasNext()) {
                const auto match = it.next();
                if (!match.captured(1).isEmpty())
                    m.cqZone = match.captured(1).toInt();
                if (!match.captured(2).isEmpty())
                    m.ituZone = match.captured(2).toInt();
            }
            key.remove(overrides);
            if (key.startsWith(QLatin1Char('='))) {
                key.remove(0, 1);
                if (key.startsWith(QLatin1String("VER")) && key.size() == 11 && key.mid(3).toInt() > 0) {
                    m_version = key;
                    continue;
                }
                m_exact.insert(key.toUpper(), m);
            } else if (!key.isEmpty()) {
                // Il primo che dichiara un prefisso vince: le entità WAE
                // (asterisco) vengono dopo quella DXCC e non la scavalcano.
                if (!m_prefixes.contains(key.toUpper()) || !waeOnly)
                    m_prefixes.insert(key.toUpper(), m);
                m_longestPrefix = qMax(m_longestPrefix, static_cast<int>(key.size()));
            }
        }
    }
    return !m_entities.isEmpty();
}

int Countries::addExceptions(const QByteArray& text)
{
    int applied = 0;
    for (const QByteArray& raw : text.split('\n')) {
        QString line = QString::fromUtf8(raw).trimmed();
        if (const int hash = line.indexOf(QLatin1Char('#')); hash >= 0)
            line = line.left(hash).trimmed();
        const QStringList fields = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (fields.size() != 2)
            continue;
        bool ok = false;
        const int dxcc = fields.at(1).toInt(&ok);
        if (!ok)
            continue;
        // La prima voce di ogni entita' DXCC e' quella che conta.
        int index = -1;
        for (int i : m_primary) {
            if (m_entities.at(i).dxcc == dxcc) {
                index = i;
                break;
            }
        }
        if (index < 0)
            continue;

        Match m;
        m.entityIndex = index;
        QString key = fields.at(0).toUpper();
        if (key.startsWith(QLatin1Char('='))) {
            key.remove(0, 1);
            if (key.isEmpty())
                continue;
            m_exact.insert(key, m);
        } else {
            m_prefixes.insert(key, m);
            m_longestPrefix = qMax(m_longestPrefix, static_cast<int>(key.size()));
        }
        ++applied;
    }
    return applied;
}

int Countries::entityCount() const
{
    return static_cast<int>(m_names.size());
}

std::optional<Countries::Match> Countries::matchPrefix(const QString& text) const
{
    for (qsizetype n = qMin<qsizetype>(text.size(), m_longestPrefix); n > 0; --n) {
        const auto it = m_prefixes.constFind(text.left(n));
        if (it != m_prefixes.constEnd())
            return *it;
    }
    return std::nullopt;
}

DxccEntity Countries::resolve(const Match& m) const
{
    DxccEntity e = m_entities.at(m.entityIndex);
    if (m.cqZone > 0)
        e.cqZone = m.cqZone;
    if (m.ituZone > 0)
        e.ituZone = m.ituZone;
    // Le entità solo WAE (IG9, GM/s...) contano per il DXCC di appartenenza.
    const QString dxccName = m_names.value(e.dxcc);
    if (!dxccName.isEmpty())
        e.name = dxccName;
    return e;
}

std::optional<DxccEntity> Countries::lookup(const QString& callsign) const
{
    const QString call = callsign.trimmed().toUpper();
    if (call.isEmpty() || m_entities.isEmpty())
        return std::nullopt;

    if (const auto it = m_exact.constFind(call); it != m_exact.constEnd())
        return resolve(*it);

    QStringList parts = call.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    // In mare e in volo non si e' in nessuna entita'.
    for (const QString& p : parts) {
        if (p == QLatin1String("MM") || p == QLatin1String("AM"))
            return std::nullopt;
    }
    // Via i suffissi che non cambiano l'entita' e le cifre di area (W1AW/4).
    QStringList kept;
    for (const QString& p : parts) {
        if (kIgnoredSuffixes.contains(p))
            continue;
        if (p.size() == 1 && p.at(0).isDigit())
            continue;
        kept << p;
    }
    if (kept.isEmpty())
        return std::nullopt;

    QString base = kept.first();
    if (kept.size() >= 2) {
        // EA8/OH2XX o OH2XX/EA8: il prefisso e' la parte piu' corta. Se la parte
        // corta non e' un prefisso noto, conta il nominativo.
        const QString a = kept.at(0);
        const QString b = kept.at(1);
        const QString shortPart = a.size() <= b.size() ? a : b;
        const QString longPart = a.size() <= b.size() ? b : a;
        if (const auto m = matchPrefix(shortPart))
            return resolve(*m);
        base = longPart;
    }

    if (const auto m = matchPrefix(base))
        return resolve(*m);
    return std::nullopt;
}

QList<DxccEntity> Countries::entities() const
{
    QList<DxccEntity> out;
    out.reserve(m_primary.size());
    for (int i : m_primary)
        out << m_entities.at(i);
    std::sort(out.begin(), out.end(), [](const DxccEntity& a, const DxccEntity& b) { return a.dxcc < b.dxcc; });
    return out;
}

} // namespace decolog::core
