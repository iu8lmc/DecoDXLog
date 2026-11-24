#include "core/ContestRules.h"

#include "core/Awards.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <utility>

namespace decolog::core {
namespace contestrules {

namespace {

// lupdate non segue una funzione d'aiuto: cosi' vede il contesto e le frasi
// finiscono dove il programma le cerca.
struct Tr {
    Q_DECLARE_TR_FUNCTIONS(ContestRules)
};

bool cqWwId(const QString& id)
{
    return id==QLatin1String("CQ-WW-CW") || id==QLatin1String("CQ-WW-SSB") || id==QLatin1String("CQ-WW-RTTY");
}
bool cqWpxId(const QString& id)
{
    return id==QLatin1String("CQ-WPX-CW") || id==QLatin1String("CQ-WPX-SSB") || id==QLatin1String("CQ-WPX-RTTY");
}

// Le bande basse del CQ WPX, dove un QSO vale il doppio.
bool lowBand(const QString& band)
{
    const QString b = band.toLower();
    return b == QLatin1String("40m") || b == QLatin1String("80m") || b == QLatin1String("160m");
}

// Le sei bande dei contest HF classici, e le cinque di quelli in RTTY, che i
// 160 metri non li usano.
QStringList sixBands()
{
    return {QStringLiteral("160m"), QStringLiteral("80m"), QStringLiteral("40m"),
            QStringLiteral("20m"), QStringLiteral("15m"), QStringLiteral("10m")};
}

QStringList fiveBands()
{
    return {QStringLiteral("80m"), QStringLiteral("40m"), QStringLiteral("20m"),
            QStringLiteral("15m"), QStringLiteral("10m")};
}

// Un QSO su una banda che la gara non usa non vale niente.
bool offContestBand(const ContestRules& rules, const QString& band)
{
    return !rules.bands.isEmpty() && !rules.bands.contains(band.toLower());
}

// Le aree canadesi con i nomi che si trovano nei log oltre a quelli del
// regolamento: NT per NWT, NL per NF, PE per PEI.
QString canadianArea(const QString& code)
{
    if (code == QLatin1String("NT"))
        return QStringLiteral("NWT");
    if (code == QLatin1String("NL"))
        return QStringLiteral("NF");
    if (code == QLatin1String("PE"))
        return QStringLiteral("PEI");
    return code;
}

bool sameCountry(const ContestQso& qso, const ContestStation& me)
{
    return qso.dxcc > 0 && me.dxcc > 0 && qso.dxcc == me.dxcc;
}

bool sameContinent(const ContestQso& qso, const ContestStation& me)
{
    return !qso.continent.isEmpty() && !me.continent.isEmpty()
           && qso.continent.compare(me.continent, Qt::CaseInsensitive) == 0;
}

bool bothInNorthAmerica(const ContestQso& qso, const ContestStation& me)
{
    return qso.continent.compare(QLatin1String("NA"), Qt::CaseInsensitive) == 0
           && me.continent.compare(QLatin1String("NA"), Qt::CaseInsensitive) == 0;
}

// Italia (248) e Sardegna (225): due entita' DXCC, un paese solo.
bool italianEntity(int dxcc)
{
    return dxcc == 248 || dxcc == 225;
}

QString modeGroup(const QString& mode)
{
    const QString m = mode.toUpper();
    if (m == QLatin1String("CW"))
        return QStringLiteral("CW");
    if (m == QLatin1String("SSB") || m == QLatin1String("AM") || m == QLatin1String("FM"))
        return QStringLiteral("SSB");
    return QStringLiteral("RTTY");   // per i contest ARI il resto e' digitale
}

} // namespace

namespace {
QString arrl10Mode(const QString& value)
{
    const QString mode=value.trimmed().toUpper();
    if (mode==QLatin1String("CW")) return QStringLiteral("CW");
    if (mode==QLatin1String("SSB") || mode==QLatin1String("USB") || mode==QLatin1String("LSB") ||
        mode==QLatin1String("AM") || mode==QLatin1String("FM") || mode==QLatin1String("PH")) return QStringLiteral("PH");
    return {};
}
}

int arrl10Points(const QString& mode)
{
    const QString group=arrl10Mode(mode);
    return group==QLatin1String("CW") ? 4 : group==QLatin1String("PH") ? 2 : 0;
}

QString arrl10Multiplier(Arrl10Origin origin, const QString& areaOrRegion, int dxcc, const QString& mode)
{
    // ARRL multiplier tables v1.2.0 (2023-05-12); aliases are deliberately not inferred.
    static const QStringList usa=QStringLiteral("AL AK AZ AR CA CO CT DC DE FL GA HI ID IL IN IA KS KY LA ME MD MA MI MN MS MO MT NE NV NH NJ NM NY NC ND OH OK OR PA RI SC SD TN TX UT VT VA WA WV WI WY").split(QLatin1Char(' '));
    static const QStringList canada=QStringLiteral("AB BC LB MB NB NF NS NT NU ON PE QC SK YT").split(QLatin1Char(' '));
    static const QStringList mexico=QStringLiteral("AGS BAC BCS CAM CHI CHH CMX COA COL DGO EMX GTO GRO HGO JAL MIC MOR NAY NLE OAX PUE QRO QUI SLP SIN SON TAB TAM TLX VER YUC ZAC").split(QLatin1Char(' '));
    const QString group=arrl10Mode(mode), value=areaOrRegion.trimmed().toUpper();
    if (group.isEmpty()) return {};
    QString category, code;
    switch (origin) {
    case Arrl10Origin::UnitedStates: if (!usa.contains(value)) return {}; category=QStringLiteral("US"); code=value; break;
    case Arrl10Origin::Canada: if (!canada.contains(value)) return {}; category=QStringLiteral("CA"); code=value; break;
    case Arrl10Origin::Mexico: if (!mexico.contains(value)) return {}; category=QStringLiteral("MX"); code=value; break;
    case Arrl10Origin::Dx: if (dxcc<=0) return {}; category=QStringLiteral("DXCC"); code=QString::number(dxcc); break;
    case Arrl10Origin::Maritime:
        if (value!=QLatin1String("1") && value!=QLatin1String("2") && value!=QLatin1String("3")) return {};
        category=QStringLiteral("ITU-REGION"); code=value; break;
    default: return {};
    }
    return category+QLatin1Char('|')+code+QLatin1Char('|')+group;
}

Arrl10Decision arrl10EvaluateContact(const Arrl10Contact& c)
{
    const auto review=[](const char* reason) { return Arrl10Decision{Arrl10Status::Review,0,{},QLatin1String(reason)}; };
    const auto excluded=[](const char* reason) { return Arrl10Decision{Arrl10Status::Excluded,0,{},QLatin1String(reason)}; };
    if (c.call.trimmed().isEmpty()) return review("missing_call");
    const QString band=c.band.trimmed().toLower();
    if (band.isEmpty()) return review("missing_band");
    if (band!=QLatin1String("10m")) return excluded("wrong_band");
    const QString group=arrl10Mode(c.mode);
    if (group.isEmpty()) return c.mode.trimmed().isEmpty() ? review("missing_mode") : excluded("unsupported_mode");
    if (c.signalReport.trimmed().isEmpty()) return review("missing_signal_report");
    if (c.frequencyHz<=0) return review("missing_frequency");
    // Treat the upper edge as outside; occupied bandwidth still needs separate review.
    if (c.frequencyHz<28000000 || c.frequencyHz>=29700000) return excluded("frequency_outside_band");
    if (group==QLatin1String("CW") && c.frequencyHz>=28300000) return excluded("cw_above_limit");
    if (!c.locationReviewed || c.origin==Arrl10Origin::Unspecified) return review("location_unreviewed");
    if (c.origin==Arrl10Origin::Dx) {
        static const QRegularExpression serial(QStringLiteral("^[0-9]+$"));
        const QString value=c.exchange.trimmed(); bool ok=false; const qulonglong number=value.toULongLong(&ok);
        if (!serial.match(value).hasMatch() || !ok || number==0) return review("invalid_serial");
        // The caller must verify the entity against the applicable official list.
        if (!c.dxccVerified || c.dxcc<=0 || c.dxcc==1 || c.dxcc==6 || c.dxcc==50 || c.dxcc==110 || c.dxcc==291)
            return review("dxcc_unverified_or_conflicting");
    }
    const QString multiplier=arrl10Multiplier(c.origin,c.exchange,c.dxcc,c.mode);
    if (multiplier.isEmpty()) return review("invalid_exchange");
    return {Arrl10Status::Usable,arrl10Points(c.mode),multiplier,{}};
}

Arrl10CandidateScore arrl10ScoreCandidates(const QList<Arrl10Contact>& contacts)
{
    Arrl10CandidateScore result; QSet<QString> worked, multipliers;
    static const QRegularExpression identityPattern(QStringLiteral("^[A-Z0-9]{2,24}$"));
    result.decisions.reserve(contacts.size());
    for (const auto& contact : contacts) {
        auto decision=arrl10EvaluateContact(contact);
        if (decision.status==Arrl10Status::Usable) {
            const QString identity=contact.contestCallIdentity.trimmed().toUpper();
            if (!identityPattern.match(identity).hasMatch()) {
                decision={Arrl10Status::Review,0,{},QStringLiteral("identity_unreviewed")};
            } else {
                const QString key=identity+QLatin1Char('|')+arrl10Mode(contact.mode);
                if (worked.contains(key)) decision={Arrl10Status::Duplicate,0,{},QStringLiteral("duplicate_mode")};
                else { worked.insert(key); multipliers.insert(decision.multiplier); result.points+=decision.points; }
            }
        }
        switch (decision.status) {
        case Arrl10Status::Usable: ++result.usable; break;
        case Arrl10Status::Review: ++result.review; break;
        case Arrl10Status::Excluded: ++result.excluded; break;
        case Arrl10Status::Duplicate: ++result.duplicates; break;
        }
        result.decisions << decision;
    }
    result.multiplierCount=multipliers.size(); result.score=result.points*result.multiplierCount;
    return result;
}

ContestRules forId(const QString& contestId)
{
    const QString id = contestId.trimmed().toUpper();
    ContestRules r;
    r.id = id;
    r.valid = true;

    // I CQ in RTTY hanno un regolamento loro, un sito loro e un portale loro:
    // stessi nomi, conti diversi. Tutti i CQ vogliono il log entro 48 ore.
    if (id == QLatin1String("CQ-WW-RTTY")) {
        r.exchange = ContestRules::Exchange::CqZoneQth;
        r.exchangeLabel = Tr::tr("CQ zone (+ W/VE QTH)");
        r.source = QStringLiteral("cqwwrtty.com/rules");
        r.submitUrl = QStringLiteral("https://www.cqwwrtty.com/logcheck/");
        r.submitHours = 48;
        r.bands = fiveBands();
        return r;
    }
    if (cqWwId(id)) {
        r.exchange = ContestRules::Exchange::CqZone;
        r.exchangeLabel = Tr::tr("CQ zone");
        r.source = QStringLiteral("cqww.com/rules");
        r.submitUrl = QStringLiteral("https://www.cqww.com/logcheck/");
        r.submitHours = 48;
        r.bands = sixBands();
        return r;
    }
    if (id == QLatin1String("CQ-WPX-RTTY")) {
        r.exchange = ContestRules::Exchange::Serial;
        r.exchangeLabel = Tr::tr("Number");
        r.source = QStringLiteral("cqwpxrtty.com/rules");
        r.submitUrl = QStringLiteral("https://www.cqwpxrtty.com/logcheck/");
        r.submitHours = 48;
        r.bands = fiveBands();
        // Regolamento: i singoli operatori 30 ore su 48, pause di almeno 60 minuti.
        r.maxOperatingHours = 30;
        r.minOffMinutes = 60;
        return r;
    }
    if (cqWpxId(id)) {
        r.exchange = ContestRules::Exchange::Serial;
        r.exchangeLabel = Tr::tr("Number");
        r.source = QStringLiteral("cqwpx.com/rules");
        r.submitUrl = QStringLiteral("https://www.cqwpx.com/logcheck/");
        r.submitHours = 48;
        r.bands = sixBands();
        // Regolamento: i singoli operatori 36 ore su 48, pause di almeno 60 minuti.
        r.maxOperatingHours = 36;
        r.minOffMinutes = 60;
        return r;
    }
    if (id == QLatin1String("IARU-HF")) {
        r.exchange = ContestRules::Exchange::ItuZone;
        r.exchangeLabel = Tr::tr("ITU zone or HQ");
        r.source = QStringLiteral("contests.arrl.org — IARU HF Rules 1.21");
        r.submitUrl = QStringLiteral("https://contest-log-submission.arrl.org/");
        r.submitDays = 7;
        r.bands = sixBands();
        return r;
    }
    if (id == QLatin1String("ARI-DX")) {
        // Le italiane mandano la provincia, le altre il progressivo: il campo e'
        // uno solo e accetta tutte e due.
        r.exchange = ContestRules::Exchange::Province;
        r.exchangeLabel = Tr::tr("Province or number");
        r.source = QStringLiteral("ARI International DX Contest");
        r.submitUrl = QStringLiteral("https://contest.ari.it/");
        return r;
    }
    if (id == QLatin1String("ARI-SEZIONI")) {
        r.exchange = ContestRules::Exchange::AriSection;
        r.exchangeLabel = Tr::tr("ARI section (ASC)");
        r.source = QStringLiteral("ARI — Contest delle Sezioni 2026");
        r.submitUrl = QStringLiteral("https://contest.ari.it/");
        return r;
    }
    if (id == QLatin1String("ARI-40-80")) {
        r.exchange = ContestRules::Exchange::Province;
        r.exchangeLabel = Tr::tr("Province");
        r.source = QStringLiteral("ARI — Contest 40/80");
        r.submitUrl = QStringLiteral("https://contest.ari.it/");
        return r;
    }

    r.valid = false;
    return r;
}

ContestRules withOverrides(ContestRules rules, const QVariantMap& o)
{
    if (!rules.valid)
        return rules;
    if (o.contains(QStringLiteral("bands"))) {
        QStringList bands;
        for (const QString& b : o.value(QStringLiteral("bands")).toStringList()) {
            const QString clean = b.trimmed().toLower();
            if (!clean.isEmpty())
                bands << clean;
        }
        // Un'edizione puo' restringere le bande del regolamento, non crearne
        // di nuove: altrimenti il calcolatore trasformerebbe una combinazione
        // non prevista in un punteggio apparentemente valido.
        QSet<QString> allowed;
        for (const QString& b : std::as_const(rules.bands))
            allowed.insert(b.toLower());
        const bool supported = !bands.isEmpty()
                               && std::all_of(bands.cbegin(), bands.cend(),
                                              [&allowed](const QString& b) { return allowed.contains(b); });
        if (!supported) {
            rules.valid = false;
            return rules;
        }
        bands.removeDuplicates();
        rules.bands = bands;
    }
    auto number = [&o](const char* key, int& target) {
        const QString k = QLatin1String(key);
        if (o.contains(k))
            target = qMax(0, o.value(k).toInt());
    };
    number("submitHours", rules.submitHours);
    number("submitDays", rules.submitDays);
    number("maxOperatingHours", rules.maxOperatingHours);
    number("minOffMinutes", rules.minOffMinutes);
    return rules;
}

OperatingTime operatingTime(const ContestRules& rules, const QList<QDateTime>& times)
{
    OperatingTime t;
    t.maxMinutes = rules.maxOperatingHours * 60;
    if (times.size() < 2)
        return t;
    // Una pausa piu' corta del minimo e' tempo in aria; senza minimo nel
    // regolamento si contano come pause quelle di mezz'ora.
    const qint64 minimum = qint64(rules.minOffMinutes > 0 ? rules.minOffMinutes : 30) * 60;
    qint64 off = 0;
    for (qsizetype i = 1; i < times.size(); ++i) {
        const qint64 gap = times.at(i - 1).secsTo(times.at(i));
        if (gap >= minimum) {
            off += gap;
            ++t.breaks;
        }
    }
    const qint64 span = times.first().secsTo(times.last());
    t.offMinutes = static_cast<int>(off / 60);
    t.onMinutes = static_cast<int>(qMax<qint64>(0, span - off) / 60);
    return t;
}

QStringList known()
{
    return {QStringLiteral("CQ-WW-SSB"),  QStringLiteral("CQ-WW-CW"),   QStringLiteral("CQ-WW-RTTY"),
            QStringLiteral("CQ-WPX-SSB"), QStringLiteral("CQ-WPX-CW"),  QStringLiteral("CQ-WPX-RTTY"),
            QStringLiteral("IARU-HF"),    QStringLiteral("ARI-DX"),
            QStringLiteral("ARI-SEZIONI"), QStringLiteral("ARI-40-80")};
}

int points(const ContestRules& rules, const ContestQso& qso, const ContestStation& me)
{
    if (!rules.valid || offContestBand(rules, qso.band))
        return 0;
    const QString id = rules.id;

    // CQ WW RTTY (regolamento, QSO Points): stesso paese 1, stesso continente
    // 2, continente diverso 3. Niente eccezione per il Nord America.
    if (id == QLatin1String("CQ-WW-RTTY")) {
        if (sameCountry(qso, me))
            return 1;
        return sameContinent(qso, me) ? 2 : 3;
    }

    // CQ WPX RTTY (regolamento, QSO Points): 1, 2 e 3 punti sulle bande alte,
    // il doppio su 40 e 80 — anche nel proprio paese, a differenza del WPX in
    // CW e SSB. Niente eccezione per il Nord America.
    if (id == QLatin1String("CQ-WPX-RTTY")) {
        const int base = sameCountry(qso, me) ? 1 : sameContinent(qso, me) ? 2 : 3;
        return lowBand(qso.band) ? base * 2 : base;
    }

    // CQ WW (regolamento VII): stesso paese 0, stesso continente 1 (2 fra
    // stazioni del Nord America), continente diverso 3.
    if (cqWwId(id)) {
        if (sameCountry(qso, me))
            return 0;
        if (sameContinent(qso, me))
            return bothInNorthAmerica(qso, me) ? 2 : 1;
        return 3;
    }

    // CQ WPX (regolamento VI): sulle bande alte 3 punti fra continenti diversi,
    // 1 dentro il continente; sulle basse il doppio. Il proprio paese vale 1
    // punto su qualunque banda. Fra stazioni del Nord America il doppio del
    // punteggio "stesso continente".
    if (cqWpxId(id)) {
        if (sameCountry(qso, me))
            return 1;
        const bool low = lowBand(qso.band);
        if (sameContinent(qso, me)) {
            const int base = bothInNorthAmerica(qso, me) ? 2 : 1;
            return low ? base * 2 : base;
        }
        return low ? 6 : 3;
    }

    // IARU HF (regolamento 5.1): stessa zona ITU 1 punto, stazione HQ 1 punto,
    // stessa zona ma altro continente 1, stesso continente altra zona 3,
    // continente e zona diversi 5.
    if (id == QLatin1String("IARU-HF")) {
        static const QRegularExpression onlyDigits(QStringLiteral("^\\d+$"));
        const bool headquarters = !qso.exchange.isEmpty()
                                  && !onlyDigits.match(qso.exchange.trimmed()).hasMatch();
        if (headquarters)
            return 1;
        // La zona che la stazione ha mandato; quella del cty.csv solo se manca.
        const int sent = qso.exchange.trimmed().toInt();
        const int zone = sent >= 1 && sent <= 90 ? sent : qso.ituZone;
        if (zone > 0 && me.ituZone > 0 && zone == me.ituZone)
            return 1;
        if (sameContinent(qso, me))
            return 3;
        return 5;
    }

    // ARI DX: 10 punti con una stazione italiana, 3 con un altro continente,
    // 1 nel proprio continente, 0 nel proprio paese (che vale come
    // moltiplicatore).
    if (id == QLatin1String("ARI-DX")) {
        if (italianEntity(qso.dxcc))
            return 10;
        if (sameCountry(qso, me))
            return 0;
        return sameContinent(qso, me) ? 1 : 3;
    }

    // Contest delle Sezioni ARI (punto 7): i punti stanno nella banda.
    if (id == QLatin1String("ARI-SEZIONI")) {
        const QString b = qso.band.toLower();
        if (b == QLatin1String("40m"))
            return 1;
        if (b == QLatin1String("80m") || b == QLatin1String("20m"))
            return 2;
        if (b == QLatin1String("160m") || b == QLatin1String("15m"))
            return 3;
        if (b == QLatin1String("10m"))
            return 4;
        return 0;
    }

    // Contest 40/80: i punti stanno nel modo.
    if (id == QLatin1String("ARI-40-80")) {
        const QString m = modeGroup(qso.mode);
        if (m == QLatin1String("CW"))
            return 3;
        if (m == QLatin1String("RTTY"))
            return 2;
        return 1;
    }

    return 0;
}

QStringList multipliers(const ContestRules& rules, const ContestQso& qso, const ContestStation& me)
{
    if (!rules.valid || offContestBand(rules, qso.band))
        return {};
    const QString id = rules.id;
    const QString band = qso.band.toLower();

    // CQ WW: la zona e il paese, ognuno una volta per banda. In RTTY anche il
    // QTH delle stazioni W/VE: 48 stati, il DC e 14 aree canadesi, per banda.
    if (cqWwId(id)) {
        QStringList out;
        // Conta la zona mandata, come fa chi controlla i log: quella del
        // cty.csv sbaglia per mezza America (un W6 sta nella 3, non nella 5).
        const int sent = exchangeZone(qso.exchange);
        const int zone = sent >= 1 && sent <= 40 ? sent : qso.cqZone;
        if (zone >= 1 && zone <= 40)
            out << QStringLiteral("zona %1|%2").arg(zone).arg(band);
        if (qso.dxcc > 0)
            out << QStringLiteral("paese %1|%2").arg(qso.dxcc).arg(band);
        if (id == QLatin1String("CQ-WW-RTTY")) {
            const QString qth = wveQth(qso.exchange, qso.state, qso.dxcc);
            if (!qth.isEmpty())
                out << QStringLiteral("qth %1|%2").arg(qth, band);
        }
        return out;
    }

    // CQ WPX: il prefisso, una volta sola su tutte le bande.
    if (cqWpxId(id)) {
        const QString prefix = awards::wpxPrefix(qso.call);
        return prefix.isEmpty() ? QStringList{} : QStringList{QStringLiteral("prefisso %1").arg(prefix)};
    }

    // IARU HF (5.2): zone ITU e stazioni HQ, per banda. Una HQ non porta la zona.
    if (id == QLatin1String("IARU-HF")) {
        static const QRegularExpression onlyDigits(QStringLiteral("^\\d+$"));
        const QString exchange = qso.exchange.trimmed().toUpper();
        if (!exchange.isEmpty() && !onlyDigits.match(exchange).hasMatch())
            return {QStringLiteral("hq %1|%2").arg(exchange, band)};
        const int sent = exchange.toInt();
        const int zone = sent >= 1 && sent <= 90 ? sent : qso.ituZone;
        if (zone >= 1 && zone <= 90)
            return {QStringLiteral("zona %1|%2").arg(zone).arg(band)};
        return {};
    }

    // ARI DX: le province italiane e i paesi DXCC (Italia e Sardegna escluse
    // come paese), una volta per banda.
    if (id == QLatin1String("ARI-DX")) {
        if (italianEntity(qso.dxcc)) {
            const QString province = awards::italianProvince(qso.exchange);
            return province.isEmpty() ? QStringList{}
                                      : QStringList{QStringLiteral("prov %1|%2").arg(province, band)};
        }
        return qso.dxcc > 0 ? QStringList{QStringLiteral("paese %1|%2").arg(qso.dxcc).arg(band)}
                            : QStringList{};
    }

    // Contest delle Sezioni (punto 8): i codici ASC, fino a tre volte per banda
    // — uno per modo.
    if (id == QLatin1String("ARI-SEZIONI")) {
        static const QRegularExpression asc(QStringLiteral("^([A-Z])\\s*(\\d{2})$"));
        const auto match = asc.match(qso.exchange.trimmed().toUpper());
        if (!match.hasMatch())
            return {};
        return {QStringLiteral("sez %1%2|%3|%4")
                    .arg(match.captured(1), match.captured(2), band, modeGroup(qso.mode))};
    }

    // Contest 40/80: le province, fino a tre volte per banda (una per modo).
    if (id == QLatin1String("ARI-40-80")) {
        const QString province = awards::italianProvince(qso.exchange);
        return province.isEmpty() ? QStringList{}
                                  : QStringList{QStringLiteral("prov %1|%2|%3")
                                                    .arg(province, band, modeGroup(qso.mode))};
    }

    Q_UNUSED(me);
    return {};
}

QString checkExchange(const ContestRules& rules, const QString& exchange)
{
    if (!rules.valid)
        return {};
    const QString text = exchange.trimmed().toUpper();
    if (text.isEmpty())
        return Tr::tr("The exchange is missing.");

    switch (rules.exchange) {
    case ContestRules::Exchange::Serial: {
        static const QRegularExpression digits(QStringLiteral("^\\d{1,6}$"));
        return digits.match(text).hasMatch() ? QString() : Tr::tr("A number was expected.");
    }
    case ContestRules::Exchange::CqZone: {
        bool ok = false;
        const int zone = text.toInt(&ok);
        return ok && zone >= 1 && zone <= 40 ? QString() : Tr::tr("A CQ zone goes from 1 to 40.");
    }
    case ContestRules::Exchange::CqZoneQth: {
        // "05", o "05 MA" per chi sta negli USA o in Canada.
        static const QRegularExpression form(QStringLiteral("^(\\d{1,2})(?:\\s+([A-Z]{2,3}))?$"));
        const auto match = form.match(text);
        const int zone = match.hasMatch() ? match.captured(1).toInt() : 0;
        if (zone < 1 || zone > 40)
            return Tr::tr("A CQ zone goes from 1 to 40.");
        const QString qth = match.captured(2);
        if (!qth.isEmpty() && !wveQths().contains(qth) && !wveQths().contains(canadianArea(qth)))
            return Tr::tr("%1 is not a US state or a Canadian area.").arg(qth);
        return {};
    }
    case ContestRules::Exchange::ItuZone: {
        // Una stazione HQ manda la sigla della societa' invece della zona.
        static const QRegularExpression digits(QStringLiteral("^\\d+$"));
        if (!digits.match(text).hasMatch())
            return text.size() <= 6 ? QString() : Tr::tr("An ITU zone or a society abbreviation.");
        const int zone = text.toInt();
        return zone >= 1 && zone <= 90 ? QString() : Tr::tr("An ITU zone goes from 1 to 90.");
    }
    case ContestRules::Exchange::Province: {
        // Le stazioni non italiane mandano il progressivo: va bene lo stesso.
        static const QRegularExpression digits(QStringLiteral("^\\d{1,6}$"));
        if (digits.match(text).hasMatch())
            return {};
        return awards::italianProvince(text).isEmpty()
                   ? Tr::tr("%1 is not an Italian province.").arg(text) : QString();
    }
    case ContestRules::Exchange::AriSection: {
        static const QRegularExpression asc(QStringLiteral("^[A-Z]\\s*\\d{2}$"));
        return asc.match(text).hasMatch() ? QString()
                                          : Tr::tr("An ASC code is a letter and two digits, like L01.");
    }
    case ContestRules::Exchange::Grid: {
        static const QRegularExpression grid(QStringLiteral("^[A-R]{2}\\d{2}([A-X]{2})?$"));
        return grid.match(text).hasMatch() ? QString() : Tr::tr("A locator was expected, like JN70.");
    }
    case ContestRules::Exchange::None:
        return {};
    }
    return {};
}

int exchangeZone(const QString& exchange)
{
    static const QRegularExpression head(QStringLiteral("^\\s*(\\d{1,2})\\b"));
    const auto match = head.match(exchange);
    return match.hasMatch() ? match.captured(1).toInt() : 0;
}

namespace {

// I 48 stati continentali e il DC: l'Alaska e le Hawaii contano solo come paese.
const QMap<QString, QString>& usaQths()
{
    static const QMap<QString, QString> all = [] {
        QMap<QString, QString> m = awards::usStates();
        m.remove(QStringLiteral("AK"));
        m.remove(QStringLiteral("HI"));
        m.insert(QStringLiteral("DC"), QStringLiteral("District of Columbia"));
        return m;
    }();
    return all;
}

// Le 14 aree canadesi, con le sigle del regolamento.
const QMap<QString, QString>& canadaQths()
{
    static const QMap<QString, QString> all{
        {QStringLiteral("NB"), QStringLiteral("New Brunswick (VE9)")},
        {QStringLiteral("NS"), QStringLiteral("Nova Scotia (VE1)")},
        {QStringLiteral("QC"), QStringLiteral("Quebec (VE2)")},
        {QStringLiteral("ON"), QStringLiteral("Ontario (VE3)")},
        {QStringLiteral("MB"), QStringLiteral("Manitoba (VE4)")},
        {QStringLiteral("SK"), QStringLiteral("Saskatchewan (VE5)")},
        {QStringLiteral("AB"), QStringLiteral("Alberta (VE6)")},
        {QStringLiteral("BC"), QStringLiteral("British Columbia (VE7)")},
        {QStringLiteral("NWT"), QStringLiteral("Northwest Territories (VE8)")},
        {QStringLiteral("NF"), QStringLiteral("Newfoundland (VO1)")},
        {QStringLiteral("LB"), QStringLiteral("Labrador (VO2)")},
        {QStringLiteral("NU"), QStringLiteral("Nunavut (VY0)")},
        {QStringLiteral("YT"), QStringLiteral("Yukon (VY1)")},
        {QStringLiteral("PEI"), QStringLiteral("Prince Edward Island (VY2)")},
    };
    return all;
}

} // namespace

const QMap<QString, QString>& wveQths()
{
    static const QMap<QString, QString> all = [] {
        QMap<QString, QString> m = usaQths();
        m.insert(canadaQths());
        return m;
    }();
    return all;
}

QString wveQth(const QString& exchange, const QString& state, int dxcc)
{
    const bool usa = dxcc == 291;
    if (!usa && dxcc != 1)
        return {};
    // Una sigla canadese in un QSO americano, o il contrario, non vale.
    auto valid = [usa](const QString& raw) -> QString {
        const QString code = usa ? raw : canadianArea(raw);
        return (usa ? usaQths() : canadaQths()).contains(code) ? code : QString();
    };
    // Nello scambio: la parola dopo la zona ("05 MA").
    static const QRegularExpression word(QStringLiteral("\\b[A-Z]{2,3}\\b"));
    auto it = word.globalMatch(exchange.trimmed().toUpper());
    while (it.hasNext()) {
        const QString code = valid(it.next().captured(0));
        if (!code.isEmpty())
            return code;
    }
    return valid(state.trimmed().toUpper());
}

} // namespace contestrules
} // namespace decolog::core
