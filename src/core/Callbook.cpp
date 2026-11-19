#include "core/Callbook.h"

#include "core/Maidenhead.h"

#include "core/NetworkError.h"

#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QXmlStreamReader>

namespace decolog::core {

QVariantMap CallbookRecord::toMap() const
{
    return {
        {QStringLiteral("call"), call},
        {QStringLiteral("name"), name},
        {QStringLiteral("qth"), qth},
        {QStringLiteral("address"), address},
        {QStringLiteral("state"), state},
        {QStringLiteral("county"), county},
        {QStringLiteral("country"), country},
        {QStringLiteral("grid"), grid},
        {QStringLiteral("iota"), iota},
        {QStringLiteral("email"), email},
        {QStringLiteral("qslVia"), qslVia},
        {QStringLiteral("imageUrl"), imageUrl},
        {QStringLiteral("lat"), lat},
        {QStringLiteral("lon"), lon},
        {QStringLiteral("hasPosition"), hasPosition},
        {QStringLiteral("dxcc"), dxcc},
        {QStringLiteral("cqZone"), cqZone},
        {QStringLiteral("ituZone"), ituZone},
        {QStringLiteral("lotw"), lotw},
        {QStringLiteral("eqsl"), eqsl},
        {QStringLiteral("source"), source},
    };
}

namespace callbook {

namespace {

// I due servizi rispondono con XML piatti: basta raccogliere il testo degli
// elementi dentro il blocco che interessa. I namespace si ignorano.
QHash<QString, QString> collect(const QByteArray& xml, const QString& block)
{
    QHash<QString, QString> out;
    QXmlStreamReader r(xml);
    int depth = -1;
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement()) {
            if (depth < 0 && r.name() == block) {
                depth = 0;
                continue;
            }
            if (depth >= 0) {
                const QString name = r.name().toString().toLower();
                const QString text = r.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
                if (!out.contains(name))
                    out.insert(name, text);
            }
        } else if (r.isEndElement() && depth >= 0 && r.name() == block) {
            break;
        }
    }
    return out;
}

QString joinNonEmpty(std::initializer_list<QString> parts, const QString& sep = QStringLiteral(" "))
{
    QStringList kept;
    for (const QString& p : parts) {
        if (!p.trimmed().isEmpty())
            kept << p.trimmed();
    }
    return kept.join(sep);
}

bool yes(const QString& v)
{
    return v.compare(QLatin1String("Y"), Qt::CaseInsensitive) == 0 || v == QLatin1String("1");
}

} // namespace

SessionResult parseQrzSession(const QByteArray& xml)
{
    const auto s = collect(xml, QStringLiteral("Session"));
    SessionResult r;
    r.key = s.value(QStringLiteral("key"));
    r.error = s.value(QStringLiteral("error"));
    // QRZ: "Session Timeout", "Invalid session key".
    r.expired = r.error.contains(QLatin1String("Session Timeout"), Qt::CaseInsensitive)
             || r.error.contains(QLatin1String("Invalid session key"), Qt::CaseInsensitive);
    if (r.key.isEmpty() && r.error.isEmpty())
        r.error = QCoreApplication::translate("Callbook", "Unexpected answer from QRZ.com");
    return r;
}

std::optional<CallbookRecord> parseQrzCallsign(const QByteArray& xml)
{
    const auto c = collect(xml, QStringLiteral("Callsign"));
    if (c.value(QStringLiteral("call")).isEmpty())
        return std::nullopt;
    CallbookRecord rec;
    rec.source = QStringLiteral("QRZ.com");
    rec.call = c.value(QStringLiteral("call")).toUpper();
    // "nickname" e' il nome con cui si fa chiamare; altrimenti nome e cognome.
    rec.name = c.value(QStringLiteral("nickname")).isEmpty()
                   ? joinNonEmpty({c.value(QStringLiteral("fname")), c.value(QStringLiteral("name"))})
                   : joinNonEmpty({c.value(QStringLiteral("nickname")), c.value(QStringLiteral("name"))});
    rec.qth = c.value(QStringLiteral("addr2"));
    rec.address = joinNonEmpty({c.value(QStringLiteral("addr1")), c.value(QStringLiteral("addr2"))}, QStringLiteral(", "));
    rec.state = c.value(QStringLiteral("state"));
    rec.county = c.value(QStringLiteral("county"));
    rec.country = c.value(QStringLiteral("country"));
    rec.grid = c.value(QStringLiteral("grid"));
    rec.iota = c.value(QStringLiteral("iota"));
    rec.email = c.value(QStringLiteral("email"));
    rec.qslVia = c.value(QStringLiteral("qslmgr"));
    rec.imageUrl = c.value(QStringLiteral("image"));
    rec.dxcc = c.value(QStringLiteral("dxcc")).toInt();
    rec.cqZone = c.value(QStringLiteral("cqzone")).toInt();
    rec.ituZone = c.value(QStringLiteral("ituzone")).toInt();
    bool okLat = false, okLon = false;
    rec.lat = c.value(QStringLiteral("lat")).toDouble(&okLat);
    rec.lon = c.value(QStringLiteral("lon")).toDouble(&okLon);
    rec.hasPosition = okLat && okLon;
    rec.lotw = yes(c.value(QStringLiteral("lotw")));
    rec.eqsl = yes(c.value(QStringLiteral("eqsl")));
    return rec;
}

SessionResult parseHamQthSession(const QByteArray& xml)
{
    const auto s = collect(xml, QStringLiteral("session"));
    SessionResult r;
    r.key = s.value(QStringLiteral("session_id"));
    r.error = s.value(QStringLiteral("error"));
    r.expired = r.error.contains(QLatin1String("Session does not exist"), Qt::CaseInsensitive)
             || r.error.contains(QLatin1String("expired"), Qt::CaseInsensitive);
    if (r.key.isEmpty() && r.error.isEmpty())
        r.error = QCoreApplication::translate("Callbook", "Unexpected answer from HamQTH");
    return r;
}

std::optional<CallbookRecord> parseHamQthSearch(const QByteArray& xml)
{
    const auto c = collect(xml, QStringLiteral("search"));
    if (c.value(QStringLiteral("callsign")).isEmpty())
        return std::nullopt;
    CallbookRecord rec;
    rec.source = QStringLiteral("HamQTH");
    rec.call = c.value(QStringLiteral("callsign")).toUpper();
    rec.name = c.value(QStringLiteral("nick")).isEmpty() ? c.value(QStringLiteral("adr_name"))
                                                         : c.value(QStringLiteral("nick"));
    rec.qth = c.value(QStringLiteral("qth")).isEmpty() ? c.value(QStringLiteral("adr_city"))
                                                       : c.value(QStringLiteral("qth"));
    rec.address = joinNonEmpty({c.value(QStringLiteral("adr_street1")), c.value(QStringLiteral("adr_city"))},
                               QStringLiteral(", "));
    rec.state = c.value(QStringLiteral("us_state"));
    rec.county = c.value(QStringLiteral("us_county"));
    rec.country = c.value(QStringLiteral("country"));
    rec.grid = c.value(QStringLiteral("grid"));
    rec.iota = c.value(QStringLiteral("iota"));
    rec.email = c.value(QStringLiteral("email"));
    rec.qslVia = c.value(QStringLiteral("qsl_via"));
    rec.imageUrl = c.value(QStringLiteral("picture"));
    rec.dxcc = c.value(QStringLiteral("adif")).toInt();
    rec.cqZone = c.value(QStringLiteral("cq")).toInt();
    rec.ituZone = c.value(QStringLiteral("itu")).toInt();
    bool okLat = false, okLon = false;
    rec.lat = c.value(QStringLiteral("latitude")).toDouble(&okLat);
    rec.lon = c.value(QStringLiteral("longitude")).toDouble(&okLon);
    rec.hasPosition = okLat && okLon;
    rec.lotw = yes(c.value(QStringLiteral("lotw")));
    rec.eqsl = yes(c.value(QStringLiteral("eqsl")));
    return rec;
}

QStringList fillMissing(AdifRecord& record, const CallbookRecord& found)
{
    QStringList filled;

    const QList<QPair<const char*, QString>> text{
        {"NAME", found.name},
        {"QTH", found.qth},
        {"ADDRESS", found.address},
        {"STATE", found.state},
        {"CNTY", found.county},
        {"COUNTRY", found.country},
        {"IOTA", found.iota},
        {"EMAIL", found.email},
        {"QSL_VIA", found.qslVia},
    };
    for (const auto& [name, value] : text) {
        if (value.trimmed().isEmpty() || !record.value(QLatin1String(name)).isEmpty())
            continue;
        record.set(QLatin1String(name), value.trimmed());
        filled << QString::fromLatin1(name);
    }

    // Il locatore ha due regole in piu'. Se il callbook non lo scrive ma dice
    // dove sta la stazione, si ricava dalla posizione: meglio un quadrato giusto
    // che niente. E se il QSO ne ha uno piu' grossolano — JN61 contro JN61FS,
    // com'e' quando arriva dalla FT8 — si tiene quello preciso, perche' e' lo
    // stesso quadrato detto meglio. Un locatore diverso non si tocca: quello
    // l'ha sentito la radio.
    QString grid = found.grid.trimmed().toUpper();
    if (grid.isEmpty() && found.hasPosition)
        grid = maidenhead::fromLatLon(found.lat, found.lon);
    const QString hasGrid = record.value(QStringLiteral("GRIDSQUARE")).trimmed().toUpper();
    if (!grid.isEmpty() && (hasGrid.isEmpty() || (grid.size() > hasGrid.size() && grid.startsWith(hasGrid)))) {
        record.set(QStringLiteral("GRIDSQUARE"), grid);
        filled << QStringLiteral("GRIDSQUARE");
    }

    // Numeri: valgono solo se il QSO non ne ha gia' uno buono. Il cty.csv ha
    // gia' detto la sua sui QSO che passano da qui, e ha ragione lui.
    const QList<QPair<const char*, int>> numbers{
        {"CQZ", found.cqZone}, {"ITUZ", found.ituZone}, {"DXCC", found.dxcc}};
    for (const auto& [name, value] : numbers) {
        if (value <= 0 || record.value(QLatin1String(name)).toInt() > 0)
            continue;
        record.set(QLatin1String(name), QString::number(value));
        filled << QString::fromLatin1(name);
    }
    return filled;
}

} // namespace callbook

CallbookClient::CallbookClient(QObject* parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
{
}

QString CallbookClient::providerId(Provider p)
{
    switch (p) {
    case Provider::Qrz:    return QStringLiteral("qrz");
    case Provider::HamQth: return QStringLiteral("hamqth");
    default:               return QStringLiteral("off");
    }
}

CallbookClient::Provider CallbookClient::providerFromId(const QString& id)
{
    if (id == QLatin1String("qrz"))    return Provider::Qrz;
    if (id == QLatin1String("hamqth")) return Provider::HamQth;
    return Provider::None;
}

void CallbookClient::setProvider(Provider provider)
{
    if (provider == m_provider)
        return;
    m_provider = provider;
    reset();
}

void CallbookClient::setFallbackEnabled(bool enabled)
{
    m_fallback = enabled;
}

CallbookClient::Provider CallbookClient::otherProvider(Provider p) const
{
    if (p == Provider::Qrz)    return Provider::HamQth;
    if (p == Provider::HamQth) return Provider::Qrz;
    return Provider::None;
}

bool CallbookClient::hasCredentials(Provider p) const
{
    return p != Provider::None && m_accounts && !m_accounts(serviceId(p)).trimmed().isEmpty() && m_secrets;
}

CallbookRecord CallbookClient::merge(const CallbookRecord& base, const CallbookRecord& extra)
{
    // Comanda il primo che ha risposto; il secondo riempie i buchi.
    CallbookRecord out = base;
    auto text = [](QString& field, const QString& other) {
        if (field.trimmed().isEmpty() && !other.trimmed().isEmpty())
            field = other;
    };
    text(out.name, extra.name);
    text(out.qth, extra.qth);
    text(out.address, extra.address);
    text(out.state, extra.state);
    text(out.county, extra.county);
    text(out.country, extra.country);
    text(out.grid, extra.grid);
    text(out.iota, extra.iota);
    text(out.email, extra.email);
    text(out.qslVia, extra.qslVia);
    text(out.imageUrl, extra.imageUrl);
    if (out.dxcc <= 0)    out.dxcc = extra.dxcc;
    if (out.cqZone <= 0)  out.cqZone = extra.cqZone;
    if (out.ituZone <= 0) out.ituZone = extra.ituZone;
    if (!out.hasPosition && extra.hasPosition) {
        out.lat = extra.lat;
        out.lon = extra.lon;
        out.hasPosition = true;
    }
    out.lotw = out.lotw || extra.lotw;
    out.eqsl = out.eqsl || extra.eqsl;
    if (out.source != extra.source && !extra.source.isEmpty())
        out.source = QStringLiteral("%1 + %2").arg(out.source, extra.source);
    return out;
}

bool CallbookClient::askTheOtherForTheGrid(Provider from, const QString& call, const CallbookRecord& sofar)
{
    const Provider other = otherProvider(from);
    if (!m_fallback || other == Provider::None || !hasCredentials(other) || m_pending.contains(call))
        return false;
    const QString key = serviceId(other) + QLatin1Char('|') + call;
    if (const auto it = m_notFound.constFind(key);
        it != m_notFound.constEnd() && it->secsTo(QDateTime::currentDateTimeUtc()) < 3600) {
        return false;
    }
    m_pending.insert(call, sofar);
    ask(other, call, false);
    return true;
}

bool CallbookClient::resolvePending(const QString& call)
{
    const auto it = m_pending.constFind(call);
    if (it == m_pending.constEnd())
        return false;
    // L'altro non ha aggiunto niente: vale quello che si era gia' trovato.
    const CallbookRecord record = *it;
    m_pending.remove(call);
    m_cache.insert(call, {QDateTime::currentDateTimeUtc(), record});
    emit found(call, record);
    return true;
}

bool CallbookClient::tryFallback(Provider from, const QString& call)
{
    const Provider other = otherProvider(from);
    if (!m_fallback || other == Provider::None || !hasCredentials(other))
        return false;
    if (resolvePending(call))
        return true;
    // Se anche l'altro ha gia' detto di no, non si insiste.
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QString key = serviceId(other) + QLatin1Char('|') + call;
    if (const auto it = m_notFound.constFind(key); it != m_notFound.constEnd() && it->secsTo(now) < 3600)
        return false;
    ask(other, call, false);
    return true;
}

void CallbookClient::setCredentialReaders(AccountReader accounts, SecretReader secrets)
{
    m_accounts = std::move(accounts);
    m_secrets = std::move(secrets);
}

void CallbookClient::setEndpoints(const QUrl& qrz, const QUrl& hamqth)
{
    m_qrzUrl = qrz;
    m_hamqthUrl = hamqth;
    reset();
}

void CallbookClient::reset()
{
    m_sessionKeys.clear();
    m_cache.clear();
    m_notFound.clear();
    m_pending.clear();
}

QString CallbookClient::serviceId(Provider p)
{
    return providerId(p);
}

QString CallbookClient::sourceName(Provider p)
{
    return p == Provider::Qrz ? QStringLiteral("QRZ.com") : QStringLiteral("HamQTH");
}

void CallbookClient::lookup(const QString& callsign)
{
    const QString call = callsign.trimmed().toUpper();
    if (m_provider == Provider::None || call.size() < 3)
        return;

    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (const auto it = m_cache.constFind(call); it != m_cache.constEnd() && it->first.secsTo(now) < 86400) {
        emit found(call, it->second);
        return;
    }
    // Un nominativo che non c'era non si richiede di nuovo per un'ora — a quel
    // servizio. L'altro, se c'e', si prova lo stesso.
    const QString mark = serviceId(m_provider) + QLatin1Char('|') + call;
    if (const auto it = m_notFound.constFind(mark); it != m_notFound.constEnd() && it->secsTo(now) < 3600) {
        emit providerResult(call, serviceId(m_provider), {});
        if (!tryFallback(m_provider, call))
            emit failed(call, tr("%1 not found on %2").arg(call, sourceName(m_provider)));
        return;
    }

    ask(m_provider, call, true);
}

void CallbookClient::ask(Provider provider, const QString& call, bool allowFallback)
{
    if (!hasCredentials(provider)) {
        m_sessionKeys.remove(static_cast<int>(provider));
        emit providerResult(call, serviceId(provider), {});
        if (!resolvePending(call) && (!allowFallback || !tryFallback(provider, call)))
            emit failed(call, tr("%1: no credentials. Add them in Setup → Callbook.").arg(sourceName(provider)));
        return;
    }
    if (m_sessionKeys.value(static_cast<int>(provider)).isEmpty()) {
        login(provider, [this, provider, call, allowFallback](const QString& error) {
            if (error.isEmpty()) {
                query(provider, call, false, allowFallback);
            } else {
                emit providerResult(call, serviceId(provider), {});
                if (!resolvePending(call) && (!allowFallback || !tryFallback(provider, call)))
                    emit failed(call, error);
            }
        });
    } else {
        query(provider, call, false, allowFallback);
    }
}

void CallbookClient::login(Provider provider, std::function<void(const QString& error)> done)
{
    const QString service = serviceId(provider);
    const QString user = m_accounts ? m_accounts(service) : QString();
    if (user.trimmed().isEmpty() || !m_secrets) {
        done(tr("%1: no credentials. Add them in Setup → Callbook.").arg(sourceName(provider)));
        return;
    }

    m_secrets(service, [this, provider, user, done](const QString& secret, const QString& error) {
        if (!error.isEmpty() || secret.trimmed().isEmpty() || !hasCredentials(provider)) {
            done(tr("%1: password not available (%2)").arg(sourceName(provider), error));
            return;
        }
        QUrl url = provider == Provider::Qrz ? m_qrzUrl : m_hamqthUrl;
        QUrlQuery q;
        if (provider == Provider::Qrz) {
            q.addQueryItem(QStringLiteral("username"), user);
            q.addQueryItem(QStringLiteral("password"), secret);
            q.addQueryItem(QStringLiteral("agent"), QStringLiteral("DecoDXLog-") + QCoreApplication::applicationVersion());
        } else {
            q.addQueryItem(QStringLiteral("u"), user);
            q.addQueryItem(QStringLiteral("p"), secret);
        }
        // La password va nella query perche' e' cosi' che la chiedono le due API,
        // e solo verso HTTPS. Non finisce in nessun log.
        url.setQuery(q);
        QNetworkRequest request(url);
        network::useHttp11(request);
        request.setTransferTimeout(15000);
        QNetworkReply* reply = m_net->get(request);
        connect(reply, &QNetworkReply::finished, this, [this, provider, reply, done] {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                done(tr("%1: %2").arg(sourceName(provider), network::safeErrorString(reply)));
                return;
            }
            const QByteArray body = reply->readAll();
            const auto session = provider == Provider::Qrz ? callbook::parseQrzSession(body)
                                                           : callbook::parseHamQthSession(body);
            if (session.key.isEmpty()) {
                done(tr("%1: %2").arg(sourceName(provider), session.error));
                return;
            }
            m_sessionKeys.insert(static_cast<int>(provider), session.key);
            done({});
        });
    });
}

void CallbookClient::query(Provider provider, const QString& call, bool retried, bool allowFallback)
{
    QUrl url = provider == Provider::Qrz ? m_qrzUrl : m_hamqthUrl;
    const QString sessionKey = m_sessionKeys.value(static_cast<int>(provider));
    QUrlQuery q;
    if (provider == Provider::Qrz) {
        q.addQueryItem(QStringLiteral("s"), sessionKey);
        q.addQueryItem(QStringLiteral("callsign"), call);
    } else {
        q.addQueryItem(QStringLiteral("id"), sessionKey);
        q.addQueryItem(QStringLiteral("callsign"), call);
        q.addQueryItem(QStringLiteral("prg"), QStringLiteral("DecoDXLog"));
    }
    url.setQuery(q);
    QNetworkRequest request(url);
    network::useHttp11(request);
    request.setTransferTimeout(15000);
    QNetworkReply* reply = m_net->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, provider, reply, call, retried, allowFallback] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit providerResult(call, serviceId(provider), {});
            if (resolvePending(call))
                return;
            if (!allowFallback || !tryFallback(provider, call))
                emit failed(call, tr("%1: %2").arg(sourceName(provider), network::safeErrorString(reply)));
            return;
        }
        const QByteArray body = reply->readAll();
        const auto record = provider == Provider::Qrz ? callbook::parseQrzCallsign(body)
                                                      : callbook::parseHamQthSearch(body);
        if (record) {
            emit providerResult(call, serviceId(provider), record->call);
            CallbookRecord answer = *record;
            if (const auto waiting = m_pending.constFind(call); waiting != m_pending.constEnd()) {
                answer = merge(*waiting, *record);
                m_pending.remove(call);
            } else if (allowFallback && answer.grid.trimmed().isEmpty() && !answer.hasPosition
                       && askTheOtherForTheGrid(provider, call, answer)) {
                // La risposta buona arrivera' quando parla anche l'altro.
                return;
            }
            m_cache.insert(call, {QDateTime::currentDateTimeUtc(), answer});
            emit found(call, answer);
            return;
        }
        const auto session = provider == Provider::Qrz ? callbook::parseQrzSession(body)
                                                       : callbook::parseHamQthSession(body);
        // Sessione scaduta: un nuovo login e un solo nuovo tentativo.
        if ((session.expired || (provider == Provider::Qrz && session.key.isEmpty() && !session.error.contains(QLatin1String("Not found"))))
            && !retried) {
            m_sessionKeys.remove(static_cast<int>(provider));
            login(provider, [this, provider, call, allowFallback](const QString& error) {
                if (error.isEmpty())
                    query(provider, call, true, allowFallback);
                else {
                    emit providerResult(call, serviceId(provider), {});
                    if (!resolvePending(call) && (!allowFallback || !tryFallback(provider, call)))
                        emit failed(call, error);
                }
            });
            return;
        }
        emit providerResult(call, serviceId(provider), {});
        if (session.error.contains(QLatin1String("not found"), Qt::CaseInsensitive))
            m_notFound.insert(serviceId(provider) + QLatin1Char('|') + call, QDateTime::currentDateTimeUtc());
        // Questo non lo sa: lo sapra' l'altro? E se si stava gia' aspettando
        // l'altro, vale quello che aveva detto il primo.
        if (resolvePending(call))
            return;
        if (allowFallback && tryFallback(provider, call))
            return;
        emit failed(call, tr("%1: %2").arg(sourceName(provider),
                                           session.error.isEmpty() ? tr("%1 not found").arg(call) : session.error));
    });
}

} // namespace decolog::core
