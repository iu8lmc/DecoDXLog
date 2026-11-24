#include "core/QslDownload.h"

#include "core/NetworkError.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrlQuery>

namespace decolog::core {

namespace confirmations {
QVariantMap ambiguousCandidateSummary(const QList<qint64>& candidates)
{
    QVariantList ids;
    for (const qint64 id : candidates) { if (ids.size() >= 50) break; ids << id; }
    return {{QStringLiteral("candidateIds"),ids},{QStringLiteral("candidateCount"),candidates.size()}};
}

namespace {

struct Tr {
    Q_DECLARE_TR_FUNCTIONS(QslDownload)
};

// Il testo di una pagina HTML, senza tag e spazi doppi: per dire cosa ha
// risposto il servizio quando non ha dato quello che si chiedeva.
QString pageText(const QByteArray& html)
{
    QString text = QString::fromUtf8(html);
    static const QRegularExpression scripts(QStringLiteral("<(script|style)[^>]*>.*?</\\1>"),
                                            QRegularExpression::CaseInsensitiveOption
                                                | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression tags(QStringLiteral("<[^>]*>"));
    text.remove(scripts);
    text.replace(tags, QStringLiteral(" "));
    text.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
    return text.simplified();
}

} // namespace

QUrl eqslFileLink(const QByteArray& html, const QUrl& page, QString* error)
{
    static const QRegularExpression link(QStringLiteral("href\\s*=\\s*[\"']?([^\"'\\s>]+\\.adi)"),
                                         QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = link.match(QString::fromUtf8(html));
    if (m.hasMatch())
        return page.resolved(QUrl(m.captured(1)));
    if (error) {
        const QString text = pageText(html);
        static const QRegularExpression said(QStringLiteral("(Error[^.]*\\.?)"), QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch e = said.match(text);
        *error = e.hasMatch() ? e.captured(1).left(200) : text.left(200);
    }
    return {};
}

QUrl eqslCardImage(const QByteArray& html, const QUrl& page, QString* error)
{
    // Il primo <img> non e' per forza la cartolina: la pagina puo' averne altri
    // (loghi, segnaposto) e un "src" senza un file vero dietro ("and") porta a
    // un 404 che non dice niente. Vale solo quello che ha l'aspetto di
    // un'immagine: un file .jpg/.png/.gif, o il servlet delle immagini di eQSL.
    static const QRegularExpression img(
        QStringLiteral(R"(<img\b[^>]*?\bsrc\s*=\s*["']?([^"'\s>]+))"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression looksLikeImage(
        QStringLiteral(R"((\.(jpe?g|png|gif|bmp)(\?.*)?$|CFFileServlet|_cf_image))"),
        QRegularExpression::CaseInsensitiveOption);
    const QString text = QString::fromUtf8(html);
    QString firstOther;
    QRegularExpressionMatchIterator it = img.globalMatch(text);
    while (it.hasNext()) {
        const QString src = it.next().captured(1);
        if (looksLikeImage.match(src).hasMatch())
            return page.resolved(QUrl(src));
        if (firstOther.isEmpty())
            firstOther = src;
    }
    if (error) {
        if (!firstOther.isEmpty()) {
            *error = QCoreApplication::translate("Qsl", "eQSL sent no card picture for this QSO (found: %1)")
                         .arg(firstOther.left(80));
        } else {
            const QString plain = pageText(html);
            static const QRegularExpression said(QStringLiteral(R"((Error:[^.<]*\.?))"), QRegularExpression::CaseInsensitiveOption);
            const QRegularExpressionMatch e = said.match(plain);
            *error = e.hasMatch() ? e.captured(1).trimmed().left(200) : plain.left(200);
        }
    }
    return {};
}

QList<AdifRecord> eqslConfirmations(const QByteArray& adif)
{
    QList<AdifRecord> out;
    for (AdifRecord r : adif::parse(adif).records) {
        if (r.value(QStringLiteral("APP_EQSL_SWL")).trimmed().toUpper() == QLatin1String("Y"))
            continue;
        // Nella casella: QSL_SENT = Y e' l'eQSL che l'altro ha mandato.
        const QString sent = r.value(QStringLiteral("QSL_SENT")).trimmed().toUpper();
        const QString rcvd = r.value(QStringLiteral("EQSL_QSL_RCVD")).trimmed().toUpper();
        if ((!sent.isEmpty() && sent != QLatin1String("Y")) || (!rcvd.isEmpty() && rcvd != QLatin1String("Y")))
            continue;
        r.set(QStringLiteral("QSLRDATE"), r.value(QStringLiteral("EQSL_QSLRDATE")));
        out << r;
    }
    return out;
}

QrzPage parseQrzFetch(const QByteArray& body)
{
    QrzPage page;
    // Coppie nome=valore separate da &; l'ADIF sta in ADIF= e si scrive con
    // &lt; e &gt;, quindi le & dentro l'ADIF non sono separatori: si prende
    // tutto quello che viene dopo ADIF= fino all'ultimo <eor>.
    const qsizetype at = body.indexOf("ADIF=");
    const QByteArray head = at >= 0 ? body.left(at) : body;
    QHash<QString, QString> fields;
    for (const QByteArray& pair : head.split('&')) {
        const qsizetype eq = pair.indexOf('=');
        if (eq > 0)
            fields.insert(QString::fromUtf8(pair.left(eq)).trimmed().toUpper(), QString::fromUtf8(pair.mid(eq + 1)).trimmed());
    }
    page.count = fields.value(QStringLiteral("COUNT")).toInt();
    const QString result = fields.value(QStringLiteral("RESULT")).toUpper();
    const QString reason = fields.value(QStringLiteral("REASON"));
    if (result != QLatin1String("OK")) {
        // Nessun QSO che risponda alla richiesta non e' un errore: e' zero.
        if (page.count == 0 && reason.contains(QLatin1String("no log entries"), Qt::CaseInsensitive)) {
            page.ok = true;
            return page;
        }
        page.error = reason.isEmpty()
                         ? Tr::tr("QRZ: unexpected answer")
                         : reason.contains(QLatin1String("invalid api key"), Qt::CaseInsensitive)
                               || reason.contains(QLatin1String("auth"), Qt::CaseInsensitive)
                               ? Tr::tr("QRZ: the logbook API key is not valid (%1)").arg(reason)
                               : Tr::tr("QRZ: %1").arg(reason);
        return page;
    }
    if (at >= 0) {
        QByteArray adif = body.mid(at + 5);
        adif.replace("&lt;", "<").replace("&gt;", ">").replace("&amp;", "&");
        const qsizetype end = adif.toLower().lastIndexOf("<eor>");
        if (end >= 0)
            adif.truncate(end + 5);
        page.records = adif::parse(adif).records;
    }
    for (const AdifRecord& r : std::as_const(page.records))
        page.lastLogId = qMax(page.lastLogId, r.value(QStringLiteral("APP_QRZLOG_LOGID")).toLongLong());
    page.ok = true;
    return page;
}

QString withoutSecret(QString text, const QString& secret)
{
    const QString bare = QString(secret).remove(QLatin1Char('-')).trimmed();
    for (const QString& form : {secret.trimmed(), bare}) {
        if (form.size() >= 4)
            text.replace(form, QStringLiteral("***"), Qt::CaseInsensitive);
    }
    return text;
}

QList<AdifRecord> qrzConfirmations(const QList<AdifRecord>& records)
{
    QList<AdifRecord> out;
    for (AdifRecord r : records) {
        if (r.value(QStringLiteral("APP_QRZLOG_STATUS")).trimmed().toUpper() != QLatin1String("C"))
            continue;
        r.set(QStringLiteral("QSLRDATE"), r.value(QStringLiteral("APP_QRZLOG_QSLDATE")));
        out << r;
    }
    return out;
}

bool mayAdvanceCursor(bool ranged, int databaseErrors)
{
    return !ranged && databaseErrors == 0;
}

bool mayAdvanceLotwCursor(bool ranged, int databaseErrors, int ambiguous, int invalid)
{
    return mayAdvanceCursor(ranged, databaseErrors) && ambiguous == 0 && invalid == 0;
}

QString lotwCursorAfterSync(const QString& previous, const QString& lastQsl, bool full, bool ranged,
                            int databaseErrors, int ambiguous, int invalid)
{
    if (ranged) return previous;
    if (databaseErrors || ambiguous || invalid)
        return full ? QStringLiteral("1900-01-01") : previous;
    return lastQsl.isEmpty() ? previous : lastQsl;
}

QDateTime downloadSince(const QDateTime& lastSuccess, bool everything)
{
    if (everything || !lastSuccess.isValid())
        return {};
    return lastSuccess.addDays(-1);
}

bool autoDownloadDue(const QDateTime& lastSuccess, const QDateTime& lastAttempt, const QDateTime& now, int hours,
                     bool enabled)
{
    if (!enabled || hours <= 0)
        return false;
    QDateTime last = lastSuccess;
    if (lastAttempt.isValid() && (!last.isValid() || lastAttempt > last))
        last = lastAttempt;
    return !last.isValid() || last.secsTo(now) >= qint64(hours) * 3600;
}

} // namespace confirmations

namespace {
struct Tr {
    Q_DECLARE_TR_FUNCTIONS(QslDownload)
};
} // namespace

ConfirmationDownloader::ConfirmationDownloader(QObject* parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
{
}

void ConfirmationDownloader::setEndpoints(const QUrl& eqslInbox, const QUrl& qrzApi)
{
    m_eqslUrl = eqslInbox;
    m_qrzUrl = qrzApi;
}

void ConfirmationDownloader::finish(confirmations::Report report)
{
    m_reply = nullptr;
    m_qrzKey.clear();
    m_qrzConfirmations.clear();
    emit finished(report);
}

void ConfirmationDownloader::cancel()
{
    if (m_reply)
        m_reply->abort();
}

QList<AdifRecord> confirmations::inPeriod(const QList<AdifRecord>& records, const QDate& from, const QDate& to)
{
    if (!from.isValid() && !to.isValid())
        return records;
    QList<AdifRecord> out;
    for (const AdifRecord& r : records) {
        const QDate day = QDate::fromString(r.value(QStringLiteral("QSO_DATE")).trimmed(), QStringLiteral("yyyyMMdd"));
        if (!day.isValid())
            continue;
        if ((from.isValid() && day < from) || (to.isValid() && day > to))
            continue;
        out.append(r);
    }
    return out;
}

void ConfirmationDownloader::downloadEqsl(const QString& user, const QString& password, const QDateTime& since,
                                          const QDate& from, const QDate& to)
{
    if (m_reply)
        return;
    if (user.trimmed().isEmpty() || password.trimmed().isEmpty()) {
        confirmations::Report report;
        report.service = QStringLiteral("eqsl");
        report.error = Tr::tr("eQSL: username and password are required");
        finish(report);
        return;
    }
    QUrl url = m_eqslUrl;
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("UserName"), user);
    q.addQueryItem(QStringLiteral("Password"), password);
    // Le segnalazioni degli SWL non sono QSO.
    q.addQueryItem(QStringLiteral("HamOnly"), QStringLiteral("1"));
    if (since.isValid())
        q.addQueryItem(QStringLiteral("RcvdSince"), since.toUTC().toString(QStringLiteral("yyyyMMddHHmm")));
    // Il periodo dei QSO, come lo chiede eQSL: MM/GG/AAAA.
    if (from.isValid())
        q.addQueryItem(QStringLiteral("LimitDateLo"), from.toString(QStringLiteral("MM/dd/yyyy")));
    if (to.isValid())
        q.addQueryItem(QStringLiteral("LimitDateHi"), to.toString(QStringLiteral("MM/dd/yyyy")));
    url.setQuery(q);
    QNetworkRequest request(url);
    network::useHttp11(request);
    request.setRawHeader("User-Agent", "DecoDXLog");
    request.setTransferTimeout(120000);
    m_reply = m_net->get(request);
    QNetworkReply* page = m_reply;
    connect(page, &QNetworkReply::finished, this, [this, page, url, password, from, to] {
        page->deleteLater();
        confirmations::Report report;
        report.service = QStringLiteral("eqsl");
        if (page->error() != QNetworkReply::NoError) {
            report.error = page->error() == QNetworkReply::OperationCanceledError
                               ? Tr::tr("eQSL: download stopped")
                               : Tr::tr("eQSL: %1").arg(network::safeErrorString(page));
            finish(report);
            return;
        }
        const QByteArray html = page->readAll();
        QString said;
        const QUrl file = confirmations::eqslFileLink(html, url, &said);
        // La pagina non dovrebbe ripetere la password; se lo fa, non va avanti.
        if (!password.isEmpty())
            said.replace(password, QStringLiteral("***"));
        if (file.isEmpty()) {
            // Niente di nuovo arrivato: eQSL lo dice con una pagina senza file.
            if (said.contains(QLatin1String("no entries"), Qt::CaseInsensitive)
                || said.contains(QLatin1String("You have no"), Qt::CaseInsensitive)) {
                report.ok = true;
                finish(report);
                return;
            }
            // "Error: No such Callsign found", o la password sbagliata.
            report.error = said.contains(QLatin1String("password"), Qt::CaseInsensitive)
                                   || said.contains(QLatin1String("username"), Qt::CaseInsensitive)
                                   || said.contains(QLatin1String("callsign"), Qt::CaseInsensitive)
                               ? Tr::tr("eQSL: username or password incorrect (%1)").arg(said)
                               : Tr::tr("eQSL: the file was not prepared (%1)").arg(said);
            finish(report);
            return;
        }
        QNetworkRequest request(file);
        network::useHttp11(request);
        request.setRawHeader("User-Agent", "DecoDXLog");
        request.setTransferTimeout(120000);
        m_reply = m_net->get(request);
        QNetworkReply* adi = m_reply;
        connect(adi, &QNetworkReply::finished, this, [this, adi, from, to] {
            adi->deleteLater();
            confirmations::Report report;
            report.service = QStringLiteral("eqsl");
            if (adi->error() != QNetworkReply::NoError) {
                report.error = Tr::tr("eQSL: %1").arg(network::safeErrorString(adi));
                finish(report);
                return;
            }
            report.confirmations = confirmations::inPeriod(confirmations::eqslConfirmations(adi->readAll()), from, to);
            report.ok = true;
            finish(report);
        });
    });
}

void ConfirmationDownloader::downloadQrz(const QString& apiKey, const QDate& since, const QDate& from,
                                         const QDate& to)
{
    if (m_reply)
        return;
    if (apiKey.trimmed().isEmpty()) {
        confirmations::Report report;
        report.service = QStringLiteral("qrz");
        report.error = Tr::tr("QRZ: logbook API key is required");
        finish(report);
        return;
    }
    m_qrzKey = apiKey.trimmed();
    m_qrzSince = since;
    m_qrzFrom = from;
    m_qrzTo = to;
    m_qrzAfter = 0;
    m_qrzPages = 0;
    m_qrzConfirmations.clear();
    fetchQrzPage();
}

void ConfirmationDownloader::fetchQrzPage()
{
    QStringList options{QStringLiteral("STATUS:CONFIRMED"), QStringLiteral("MAX:%1").arg(kQrzPage)};
    if (m_qrzSince.isValid())
        options << QStringLiteral("MODSINCE:%1").arg(m_qrzSince.toString(Qt::ISODate));
    if (m_qrzAfter > 0)
        options << QStringLiteral("AFTERLOGID:%1").arg(m_qrzAfter);
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("KEY"), m_qrzKey);
    form.addQueryItem(QStringLiteral("ACTION"), QStringLiteral("FETCH"));
    form.addQueryItem(QStringLiteral("OPTION"), options.join(QLatin1Char(',')));
    QNetworkRequest request(m_qrzUrl);
    network::useHttp11(request);
    request.setRawHeader("User-Agent", "DecoDXLog");
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    request.setTransferTimeout(120000);
    m_reply = m_net->post(request, form.toString(QUrl::FullyEncoded).toUtf8());
    QNetworkReply* reply = m_reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        confirmations::Report report;
        report.service = QStringLiteral("qrz");
        if (reply->error() != QNetworkReply::NoError) {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 401 || status == 403) {
                report.error = Tr::tr("QRZ Logbook refused the confirmation request (HTTP %1). "
                                      "Check the Logbook API key and subscription.").arg(status);
            } else {
                report.error = reply->error() == QNetworkReply::OperationCanceledError
                                   ? Tr::tr("QRZ: download stopped")
                                   : Tr::tr("QRZ: %1").arg(network::safeErrorString(reply));
            }
            finish(report);
            return;
        }
        const confirmations::QrzPage page = confirmations::parseQrzFetch(reply->readAll());
        if (!page.ok) {
            report.error = page.error;
            report.error = confirmations::withoutSecret(report.error, m_qrzKey);
            finish(report);
            return;
        }
        m_qrzConfirmations << confirmations::qrzConfirmations(page.records);
        ++m_qrzPages;
        // Un'altra pagina se questa era piena, e se ci si puo' spostare avanti
        // (AFTERLOGID vale "da questo in poi": si parte dal successivo). Un
        // limite, per non girare per sempre se il server risponde strano.
        if (page.records.size() >= kQrzPage && page.lastLogId >= m_qrzAfter && m_qrzPages < 2000) {
            m_qrzAfter = page.lastLogId + 1;
            fetchQrzPage();
            return;
        }
        report.confirmations = confirmations::inPeriod(m_qrzConfirmations, m_qrzFrom, m_qrzTo);
        m_qrzConfirmations.clear();
        report.ok = true;
        finish(report);
    });
}

// ── La cartolina eQSL ────────────────────────────────────────────────────────

EqslCardFetcher::EqslCardFetcher(QObject* parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
{
}

QString EqslCardFetcher::keyOf(const Request& r)
{
    const QString text = QStringLiteral("%1|%2|%3|%4|%5")
                             .arg(r.user.trimmed().toUpper(), r.call.trimmed().toUpper(),
                                  r.on.toUTC().toString(QStringLiteral("yyyyMMddHHmm")), r.band.trimmed().toLower(),
                                  r.mode.trimmed().toUpper());
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1).toHex().left(24));
}

QString EqslCardFetcher::cachedFile(const QString& key) const
{
    if (m_dir.isEmpty())
        return {};
    for (const char* ext : {".jpg", ".png", ".gif"}) {
        const QString file = QDir(m_dir).filePath(key + QLatin1String(ext));
        if (QFileInfo::exists(file))
            return file;
    }
    return {};
}

void EqslCardFetcher::fetch(const Request& r)
{
    const QString key = keyOf(r);
    const QString cached = cachedFile(key);
    if (!cached.isEmpty()) {
        QMetaObject::invokeMethod(this, [this, key, cached] { emit ready(key, cached, QString()); },
                                  Qt::QueuedConnection);
        return;
    }
    m_pending = r;
    m_hasPending = true;
    if (!m_busy)
        startNext();
}

void EqslCardFetcher::startNext()
{
    if (!m_hasPending || m_busy)
        return;
    // Una ogni dieci secondi: eQSL ne vuole meno di sei al minuto.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 wait = m_lastAsk + m_gapMs - now;
    if (m_lastAsk > 0 && wait > 0) {
        m_busy = true;
        QTimer::singleShot(int(wait), this, [this] {
            m_busy = false;
            startNext();
        });
        return;
    }
    const Request r = m_pending;
    m_hasPending = false;
    m_busy = true;
    m_lastAsk = now;
    const QString key = keyOf(r);
    const QDateTime on = r.on.toUTC();
    QUrl url = m_url;
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("Username"), r.user);
    q.addQueryItem(QStringLiteral("Password"), r.password);
    q.addQueryItem(QStringLiteral("CallsignFrom"), r.call.trimmed().toUpper());
    q.addQueryItem(QStringLiteral("QSOYear"), on.toString(QStringLiteral("yyyy")));
    q.addQueryItem(QStringLiteral("QSOMonth"), on.toString(QStringLiteral("MM")));
    q.addQueryItem(QStringLiteral("QSODay"), on.toString(QStringLiteral("dd")));
    q.addQueryItem(QStringLiteral("QSOHour"), on.toString(QStringLiteral("HH")));
    q.addQueryItem(QStringLiteral("QSOMinute"), on.toString(QStringLiteral("mm")));
    q.addQueryItem(QStringLiteral("QSOBand"), r.band.trimmed());
    q.addQueryItem(QStringLiteral("QSOMode"), r.mode.trimmed().toUpper());
    url.setQuery(q);
    QNetworkRequest request(url);
    network::useHttp11(request);
    request.setRawHeader("User-Agent", "DecoDXLog");
    request.setTransferTimeout(60000);
    QNetworkReply* page = m_net->get(request);
    const QString password = r.password;
    connect(page, &QNetworkReply::finished, this, [this, page, url, key, password] {
        page->deleteLater();
        if (page->error() != QNetworkReply::NoError) {
            done(key, {}, confirmations::withoutSecret(network::safeErrorString(page), password));
            return;
        }
        QString error;
        const QUrl image = confirmations::eqslCardImage(page->readAll(), url, &error);
        if (image.isEmpty()) {
            done(key, {}, confirmations::withoutSecret(error, password));
            return;
        }
        QNetworkRequest get(image);
        network::useHttp11(get);
        get.setRawHeader("User-Agent", "DecoDXLog");
        get.setTransferTimeout(60000);
        QNetworkReply* file = m_net->get(get);
        connect(file, &QNetworkReply::finished, this, [this, file, key, image] {
            file->deleteLater();
            const QByteArray bytes = file->readAll();
            if (file->error() != QNetworkReply::NoError || bytes.isEmpty()) {
                done(key, {}, network::safeErrorString(file));
                return;
            }
            QString ext = QFileInfo(image.path()).suffix().toLower();
            if (ext != QLatin1String("png") && ext != QLatin1String("gif"))
                ext = QStringLiteral("jpg");
            QDir().mkpath(m_dir);
            const QString path = QDir(m_dir).filePath(key + QLatin1Char('.') + ext);
            QFile out(path);
            if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size()) {
                done(key, {}, out.errorString());
                return;
            }
            out.close();
            done(key, path, {});
        });
    });
}

void EqslCardFetcher::done(const QString& key, const QString& file, const QString& error)
{
    m_busy = false;
    emit ready(key, file, error);
    startNext();
}

} // namespace decolog::core
