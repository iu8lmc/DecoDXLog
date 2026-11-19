#include "../StartupTrace.h"
#include "core/QslUpload.h"

#include "core/Adif.h"
#include "core/ClubLogAccessGuard.h"
#include "core/ClubLogRestBatchProtocol.h"
#include "core/NetworkError.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimeZone>
#include <QTimer>
#include <QSet>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

#include <cmath>
#include <utility>
#include <QUrlQuery>
#include <QXmlStreamReader>

#ifdef Q_OS_WIN
#include <QSettings>
#endif

namespace decolog::core {

namespace qsl {

void AutomaticUploadQueue::reset(const QStringList& services)
{
    m_services.clear();
    append(services);
}

void AutomaticUploadQueue::append(const QStringList& services)
{
    for (const QString& service : services) {
        const QString id = service.trimmed();
        if (!id.isEmpty() && !m_services.contains(id))
            m_services.append(id);
    }
}

QString AutomaticUploadQueue::takeNext(bool busy)
{
    if (busy || m_services.isEmpty())
        return {};
    return m_services.takeFirst();
}

QString tqslDataDirectory()
{
    QStringList candidates;
#ifdef Q_OS_WIN
    // Su Windows TQSL tiene i suoi dati in %APPDATA%\TrustedQSL — la cartella
    // "Roaming" —: li' stanno il certificato del nominativo, le chiavi e le
    // station location. Prima si guardava solo in "Local", dove non c'e'
    // niente, e cosi' un TQSL a posto sembrava non installato.
    const QString roaming = qEnvironmentVariable("APPDATA");
    if (!roaming.isEmpty())
        candidates << QDir(roaming).filePath(QStringLiteral("TrustedQSL"));
#endif
    candidates << QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
                      .filePath(QStringLiteral("TrustedQSL"));
    // Su Linux e macOS TQSL usa la cartella nascosta nella home.
    candidates << QDir(QDir::homePath()).filePath(QStringLiteral(".tqsl"));
    for (const QString& dir : std::as_const(candidates)) {
        if (QFileInfo::exists(dir))
            return dir;
    }
    return {};
}

QString findRigctld()
{
#ifdef Q_OS_WIN
    // Hamlib si installa in "Program Files\hamlib-w64-<versione>": si prende la
    // piu' recente che si trova.
    QStringList roots{QStringLiteral("C:/Program Files"), QStringLiteral("C:/Program Files (x86)")};
    QStringList found;
    for (const QString& root : roots) {
        QDir dir(root);
        for (const QString& name : dir.entryList({QStringLiteral("hamlib*")}, QDir::Dirs)) {
            const QString exe = dir.filePath(name + QStringLiteral("/bin/rigctld.exe"));
            if (QFileInfo::exists(exe))
                found << QDir::toNativeSeparators(exe);
        }
    }
    found.sort();
    if (!found.isEmpty())
        return found.last();
#endif
    return QStandardPaths::findExecutable(QStringLiteral("rigctld"));
}

QString findTqsl()
{
#ifdef Q_OS_WIN
    // TQSL registra dove si e' installato; e' la via piu' affidabile.
    for (const auto scope : {QSettings::NativeFormat}) {
        for (const char* key : {"HKEY_LOCAL_MACHINE\\SOFTWARE\\TrustedQSL",
                                "HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\TrustedQSL",
                                "HKEY_CURRENT_USER\\SOFTWARE\\TrustedQSL"}) {
            QSettings reg(QLatin1String(key), scope);
            const QString path = reg.value(QStringLiteral("Path")).toString();
            if (!path.isEmpty()) {
                const QString exe = QDir(path).filePath(QStringLiteral("tqsl.exe"));
                if (QFileInfo::exists(exe))
                    return QDir::toNativeSeparators(exe);
            }
        }
    }
    for (const char* candidate : {"C:/Program Files (x86)/TrustedQSL/tqsl.exe",
                                  "C:/Program Files/TrustedQSL/tqsl.exe"}) {
        if (QFileInfo::exists(QLatin1String(candidate)))
            return QDir::toNativeSeparators(QLatin1String(candidate));
    }
#else
    for (const char* candidate : {"/usr/bin/tqsl", "/usr/local/bin/tqsl",
                                  "/Applications/TrustedQSL/tqsl.app/Contents/MacOS/tqsl"}) {
        if (QFileInfo::exists(QLatin1String(candidate)))
            return QLatin1String(candidate);
    }
#endif
    return QStandardPaths::findExecutable(QStringLiteral("tqsl"));
}

QStringList tqslStationLocations()
{
    decolog::StartupSpan trace("TQSL station locations");
    QStringList out;
    const QString dir = tqslDataDirectory();
    if (dir.isEmpty())
        return out;
    QFile file(QDir(dir).filePath(QStringLiteral("station_data")));
    if (!file.open(QIODevice::ReadOnly))
        return out;
    QXmlStreamReader xml(&file);
    while (!xml.atEnd()) {
        if (xml.readNext() == QXmlStreamReader::StartElement
            && xml.name().compare(QLatin1String("StationData"), Qt::CaseInsensitive) == 0) {
            const QString name = xml.attributes().value(QLatin1String("name")).toString();
            if (!name.isEmpty())
                out << name;
        }
    }
    return out;
}

bool tqslHasCertificate()
{
    const QString dir = tqslDataDirectory();
    if (dir.isEmpty())
        return false;
    // In certs/ ci sono sempre "root" e "authorities": quelli li mette TQSL da
    // solo appena installato, e non firmano niente. Il certificato che conta e'
    // quello del nominativo, che sta in "user" e arriva col file .tq6 di ARRL.
    const QFileInfo user(QDir(dir).filePath(QStringLiteral("certs/user")));
    if (user.exists() && user.size() > 0)
        return true;
    const QDir certs(QDir(dir).filePath(QStringLiteral("certs")));
    for (const QString& name : certs.entryList(QDir::Files | QDir::NoDotAndDotDot)) {
        if (name != QLatin1String("root") && name != QLatin1String("authorities"))
            return true;
    }
    return false;
}

QslUploadResult resultFromTqslExit(int exitCode, const QString& output, int qsoCount)
{
    QslUploadResult r;
    r.message = output.trimmed();
    // I codici sono quelli dell'interfaccia a riga di comando di TQSL.
    // I codici 8 e 9 non identificano quali QSO sono stati ignorati: non
    // possiamo quindi marcare l'intero blocco come inviato.
    switch (exitCode) {
    case 0:
        r.ok = true;
        r.accepted = qsoCount;
        if (r.message.isEmpty())
            r.message = QCoreApplication::translate("Qsl", "TQSL uploaded %n QSO; check processing in LoTW Your Activity", nullptr, qsoCount);
        break;
    case 7:
        r.message = QCoreApplication::translate("Qsl", "TQSL: cannot open the output file (%1)").arg(r.message);
        break;
    case 8:
        r.requiresReconciliation = true;
        r.message = QCoreApplication::translate("Qsl", "TQSL processed no QSOs: duplicates or out of date range; verify in LoTW before retrying (%1)").arg(r.message);
        break;
    case 9:
        r.requiresReconciliation = true;
        r.message = QCoreApplication::translate("Qsl", "TQSL processed only part of the batch; individual QSO status is unknown. Verify in LoTW before retrying (%1)").arg(r.message);
        break;
    case 1:
        r.message = QCoreApplication::translate("Qsl", "TQSL: cancelled");
        break;
    case 2:
        r.rejected = qsoCount;
        r.message = QCoreApplication::translate("Qsl", "LoTW rejected the file: %1").arg(r.message);
        break;
    case 5:
        r.message = QCoreApplication::translate("Qsl", "TQSL library error (%1)").arg(r.message);
        break;
    case 10:
        r.message = QCoreApplication::translate("Qsl", "TQSL: command syntax error (%1)").arg(r.message);
        break;
    case 11:
        r.retryLater = true;
        r.message = QCoreApplication::translate("Qsl", "TQSL: cannot reach LoTW (%1)").arg(r.message);
        break;
    default:
        r.message = QCoreApplication::translate("Qsl", "TQSL: error %1 %2").arg(exitCode).arg(r.message);
        break;
    }
    return r;
}

QslUploadResult parseQrzResponse(const QByteArray& body)
{
    QslUploadResult r;
    const QUrlQuery q(QString::fromUtf8(body).trimmed());
    const QString result = q.queryItemValue(QStringLiteral("RESULT"));
    const QString reason = q.queryItemValue(QStringLiteral("REASON"), QUrl::FullyDecoded);
    if (result == QLatin1String("OK")) {
        r.ok = true;
        r.accepted = 1;
        r.remoteId = q.queryItemValue(QStringLiteral("LOGID"));
        r.message = QCoreApplication::translate("Qsl", "QRZ Logbook: QSO %1").arg(r.remoteId);
        return r;
    }
    if (reason.contains(QLatin1String("duplicate"), Qt::CaseInsensitive)) {
        r.ok = true;
        r.duplicates = 1;
        r.message = QCoreApplication::translate("Qsl", "QRZ Logbook: already there");
        return r;
    }
    if (result.isEmpty()) {
        r.retryLater = true;
        r.message = QCoreApplication::translate("Qsl", "QRZ Logbook: unexpected answer");
        return r;
    }
    r.rejected = 1;
    r.message = QCoreApplication::translate("Qsl", "QRZ Logbook: %1").arg(reason.isEmpty() ? result : reason);
    return r;
}

QString crxRemoteKey(qint64 logId, const QString& qsoId)
{
    return qsoId.isEmpty() ? QString() : QStringLiteral("%1:%2").arg(logId).arg(qsoId);
}

qint64 crxRemoteQso(const QString& remoteKey, qint64 logId)
{
    if (remoteKey.isEmpty())
        return 0;
    if (!remoteKey.contains(QLatin1Char(':')))
        return remoteKey.toLongLong();
    if (remoteKey.section(QLatin1Char(':'), 0, 0).toLongLong() != logId)
        return 0;
    return remoteKey.section(QLatin1Char(':'), 1).toLongLong();
}

QJsonObject crxQsoData(const AdifRecord& record, qint64 logId, qint64 remoteId, qint64 localId)
{
    // La frequenza in kHz, come nell'esempio della documentazione ("7025").
    const double mhz = record.value(QStringLiteral("FREQ")).toDouble();
    const QString khz = mhz > 0 ? QString::number(mhz * 1000.0, 'f', mhz * 1000.0 == std::floor(mhz * 1000.0) ? 0 : 1)
                                : QString();
    // Il modo come lo scrivono gli operatori: FT8 e non MFSK.
    QString mode = record.value(QStringLiteral("MODE")).toUpper();
    const QString submode = record.value(QStringLiteral("SUBMODE")).toUpper();
    if (!submode.isEmpty() && mode != QLatin1String("SSB"))
        mode = submode;
    // L'ora del QSO, in secondi Unix come la restituisce get_myqsos. La
    // documentazione di edit_myqso non la elenca: senza, CRX metterebbe l'ora
    // dell'invio, e un QSO mandato dopo avrebbe l'ora sbagliata.
    const QString date = record.value(QStringLiteral("QSO_DATE"));
    QString time = record.value(QStringLiteral("TIME_ON"));
    if (time.size() == 4)
        time += QStringLiteral("00");
    const QDateTime when = QDateTime::fromString(date + time, QStringLiteral("yyyyMMddHHmmss"));
    QJsonObject out{
        {QStringLiteral("qso_id"), remoteId},
        {QStringLiteral("f_log_id"), logId},
        {QStringLiteral("logentry_his_call"), record.value(QStringLiteral("CALL")).toUpper()},
        {QStringLiteral("logentry_band"), record.value(QStringLiteral("BAND")).toLower()},
        {QStringLiteral("logentry_frequency"), khz},
        {QStringLiteral("logentry_mode"), mode},
        // "his report" e' il rapporto dato a lui, "my report" quello ricevuto.
        {QStringLiteral("logentry_his_report"), record.value(QStringLiteral("RST_SENT"))},
        {QStringLiteral("logentry_my_report"), record.value(QStringLiteral("RST_RCVD"))},
    };
    if (when.isValid()) {
        QDateTime utc = when;
        utc.setTimeZone(QTimeZone::UTC);
        out.insert(QStringLiteral("logentry_date"), utc.toSecsSinceEpoch());
    }
    const QString name = record.value(QStringLiteral("NAME"));
    if (!name.isEmpty())
        out.insert(QStringLiteral("logentry_his_name"), name);
    const QString comment = record.value(QStringLiteral("COMMENT"));
    if (!comment.isEmpty())
        out.insert(QStringLiteral("logentry_comment"), comment);
    if (localId > 0)
        out.insert(QStringLiteral("logentry_custom_field38"), QString::number(localId));
    return out;
}

namespace {
// CRX la chiave rifiutata la dice nel corpo, e non sempre con 401: oggi (fine
// settembre 2026) risponde 500 con {"error":"Error : 20-api-key-auth-error"}.
// Presa per "il servizio non risponde", la chiave sbagliata restava nascosta
// dietro un "riprovo dopo" senza fine.
bool crxKeyRefused(int status, const QString& error)
{
    if (status == 401)
        return true;
    for (const char* sign : {"api-key", "apikey", "api key", "auth-error", "auth error", "unauthori"}) {
        if (error.contains(QLatin1String(sign), Qt::CaseInsensitive))
            return true;
    }
    return false;
}
} // namespace

QString crxKeyRefusedMessage(const QString& error)
{
    const QString why = error.trimmed().isEmpty() ? QString() : QStringLiteral(" (%1)").arg(error.trimmed());
    return QCoreApplication::translate("Qsl", "the API key was not accepted%1: check it on crx.cloud "
                                              "(user → My account → my-api) and paste it again in Setup → QSL "
                                              "services").arg(why);
}

QslUploadResult parseCrxResponse(int status, const QByteArray& body)
{
    QslUploadResult r;
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    if (o.value(QStringLiteral("success")).toBool()) {
        r.ok = true;
        r.accepted = 1;
        const QJsonValue id = o.value(QStringLiteral("qso_id"));
        r.remoteId = id.isString() ? id.toString() : QString::number(id.toVariant().toLongLong());
        r.message = QCoreApplication::translate("Qsl", "CRX Logbook: QSO %1").arg(r.remoteId);
        return r;
    }
    const QString error = o.value(QStringLiteral("error")).toString(o.value(QStringLiteral("message")).toString());
    if (crxKeyRefused(status, error)) {
        // Una chiave sbagliata vale per tutti i QSO: ci si ferma qui.
        r.retryLater = true;
        r.message = QCoreApplication::translate("Qsl", "CRX Logbook: %1").arg(crxKeyRefusedMessage(error));
        return r;
    }
    if (status == 0 || status >= 500 || (status == 200 && o.isEmpty())) {
        // Il servizio non ha risposto, o ha risposto con qualcosa che non e'
        // JSON: si riprova dopo, il QSO resta in coda.
        r.retryLater = true;
        r.message = QCoreApplication::translate("Qsl", "CRX Logbook: no answer from the service (%1)")
                        .arg(status > 0 ? QString::number(status) : QStringLiteral("—"));
        return r;
    }
    if (error.contains(QLatin1String("duplicate"), Qt::CaseInsensitive)) {
        r.ok = true;
        r.duplicates = 1;
        r.message = QCoreApplication::translate("Qsl", "CRX Logbook: already there");
        return r;
    }
    r.rejected = 1;
    r.message = QCoreApplication::translate("Qsl", "CRX Logbook: %1")
                    .arg(error.isEmpty() ? QString::number(status) : error);
    return r;
}

QVariantList parseCrxLogs(const QByteArray& body, QString* error)
{
    QVariantList out;
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    if (o.contains(QStringLiteral("error"))) {
        const QString said = o.value(QStringLiteral("error")).toString();
        if (error)
            *error = crxKeyRefused(0, said) ? crxKeyRefusedMessage(said) : said;
        return out;
    }
    for (const QJsonValue& v : o.value(QStringLiteral("logs")).toArray()) {
        const QJsonObject log = v.toObject();
        out << QVariantMap{
            {QStringLiteral("id"), log.value(QStringLiteral("log_id")).toVariant().toLongLong()},
            {QStringLiteral("name"), log.value(QStringLiteral("log_name")).toString()},
            {QStringLiteral("call"), log.value(QStringLiteral("log_activation_call")).toString()},
            {QStringLiteral("description"), log.value(QStringLiteral("log_desc")).toString()},
        };
    }
    if (out.isEmpty() && error && !o.contains(QStringLiteral("logs")))
        *error = QCoreApplication::translate("Qsl", "unexpected answer");
    return out;
}

QString withoutKey(QString text, const QString& key)
{
    if (key.size() >= 4)
        text.replace(key, QStringLiteral("***"), Qt::CaseInsensitive);
    return text;
}

QUrl wavelogApiUrl(const QString& server, const QString& endpoint)
{
    QString base = server.trimmed();
    if (base.isEmpty())
        return {};
    if (!base.contains(QLatin1String("://")))
        base.prepend(QStringLiteral("https://"));
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    // Chi incolla l'indirizzo dell'API intero non deve vederselo raddoppiato.
    const qsizetype api = base.indexOf(QLatin1String("/index.php"));
    if (api >= 0)
        base.truncate(api);
    return QUrl(base + QStringLiteral("/index.php/api/") + endpoint);
}

QslUploadResult parseWavelogResponse(int status, const QByteArray& body)
{
    QslUploadResult r;
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    const QString state = o.value(QStringLiteral("status")).toString().toLower();
    QStringList messages;
    for (const QJsonValue& m : o.value(QStringLiteral("messages")).toArray())
        messages << m.toString();
    const QString reason = o.value(QStringLiteral("reason")).toString();
    const QString said = !reason.isEmpty() ? reason : messages.join(QStringLiteral("; "));
    if (state == QLatin1String("created") || ((status == 200 || status == 201) && state.isEmpty() && !body.isEmpty())) {
        r.ok = true;
        // Il doppione Wavelog lo dice nei messaggi, con la risposta "created".
        const int errors = o.value(QStringLiteral("adif_errors")).toInt();
        if (said.contains(QLatin1String("duplicate"), Qt::CaseInsensitive)) {
            r.duplicates = 1;
            r.message = QCoreApplication::translate("Qsl", "Wavelog: already there");
        } else if (errors > 0) {
            r.rejected = 1;
            r.message = QCoreApplication::translate("Qsl", "Wavelog: %1").arg(said.left(160));
        } else {
            r.accepted = 1;
            r.message = QCoreApplication::translate("Qsl", "Wavelog: sent");
        }
        return r;
    }
    // La chiave sbagliata o senza diritti di scrittura; "la stazione non e'
    // della chiave" invece e' un problema della stazione scelta.
    const bool keyProblem = status == 401 || status == 403
                            || reason.contains(QLatin1String("missing api key"), Qt::CaseInsensitive)
                            || reason.contains(QLatin1String("invalid api key"), Qt::CaseInsensitive)
                            || reason.contains(QLatin1String("no rights"), Qt::CaseInsensitive);
    if (keyProblem) {
        r.message = QCoreApplication::translate("Qsl", "Wavelog: the API key is not valid or cannot write (%1)")
                        .arg(said.isEmpty() ? QString::number(status) : said.left(120));
        return r;
    }
    if (said.contains(QLatin1String("duplicate"), Qt::CaseInsensitive)) {
        r.ok = true;
        r.duplicates = 1;
        r.message = QCoreApplication::translate("Qsl", "Wavelog: already there");
        return r;
    }
    r.rejected = 1;
    r.message = QCoreApplication::translate("Qsl", "Wavelog: %1")
                    .arg(said.isEmpty() ? QStringLiteral("HTTP %1").arg(status) : said.left(160));
    return r;
}

QVariantList parseWavelogStations(const QByteArray& body, QString* error)
{
    QVariantList out;
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isArray()) {
        const QString reason = doc.object().value(QStringLiteral("reason")).toString();
        if (error)
            *error = reason.isEmpty() ? QCoreApplication::translate("Qsl", "unexpected answer") : reason;
        return out;
    }
    for (const QJsonValue& v : doc.array()) {
        const QJsonObject s = v.toObject();
        out << QVariantMap{
            {QStringLiteral("id"), s.value(QStringLiteral("station_id")).toVariant().toString()},
            {QStringLiteral("name"), s.value(QStringLiteral("station_profile_name")).toString()},
            {QStringLiteral("call"), s.value(QStringLiteral("station_callsign")).toString()},
            {QStringLiteral("grid"), s.value(QStringLiteral("station_gridsquare")).toString()},
            {QStringLiteral("active"), s.value(QStringLiteral("station_active")).toVariant().toString() == QLatin1String("1")},
        };
    }
    return out;
}

QslUploadResult parseHrdLogResponse(const QByteArray& body)
{
    QslUploadResult r;
    const QString text = QString::fromUtf8(body);
    static const QRegularExpression insert(QStringLiteral("<insert>\\s*(\\d+)\\s*</insert>"),
                                           QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression error(QStringLiteral("<error>(.*?)</error>"),
                                          QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    const auto e = error.match(text);
    const QString why = e.hasMatch() ? e.captured(1).simplified() : QString();
    if (const auto m = insert.match(text); m.hasMatch() && m.captured(1).toInt() > 0) {
        r.ok = true;
        r.accepted = m.captured(1).toInt();
        r.message = QCoreApplication::translate("Qsl", "HRDLog: sent");
        return r;
    }
    if (why.contains(QLatin1String("duplicate"), Qt::CaseInsensitive)
        || why.contains(QLatin1String("already"), Qt::CaseInsensitive)) {
        r.ok = true;
        r.duplicates = 1;
        r.message = QCoreApplication::translate("Qsl", "HRDLog: already there");
        return r;
    }
    if (!why.isEmpty()) {
        r.rejected = 1;
        r.message = QCoreApplication::translate("Qsl", "HRDLog: %1").arg(why.left(160));
        return r;
    }
    r.retryLater = true;
    r.message = QCoreApplication::translate("Qsl", "HRDLog: unexpected answer");
    return r;
}

QslUploadResult parseEqslResponse(const QByteArray& body)
{
    QslUploadResult r;
    const QString text = QString::fromUtf8(body).remove(QRegularExpression(QStringLiteral("<[^>]*>"))).simplified();
    static const QRegularExpression added(QStringLiteral("Result:\\s*(\\d+)\\s*out of\\s*(\\d+)\\s*record"),
                                          QRegularExpression::CaseInsensitiveOption);
    if (const auto m = added.match(text); m.hasMatch()) {
        r.accepted = m.captured(1).toInt();
        r.ok = r.accepted > 0;
        if (!r.ok && text.contains(QLatin1String("Duplicate"), Qt::CaseInsensitive)) {
            r.ok = true;
            r.duplicates = 1;
            r.message = QCoreApplication::translate("Qsl", "eQSL: already there");
            return r;
        }
        r.message = QCoreApplication::translate("Qsl", "eQSL: %1").arg(text.left(120));
        return r;
    }
    if (text.contains(QLatin1String("Duplicate"), Qt::CaseInsensitive)) {
        r.ok = true;
        r.duplicates = 1;
        r.message = QCoreApplication::translate("Qsl", "eQSL: already there");
        return r;
    }
    if (text.contains(QLatin1String("Bad record"), Qt::CaseInsensitive)
        || text.contains(QLatin1String("Error"), Qt::CaseInsensitive)) {
        r.rejected = 1;
        r.message = QCoreApplication::translate("Qsl", "eQSL: %1").arg(text.left(120));
        return r;
    }
    r.retryLater = true;
    r.message = QCoreApplication::translate("Qsl", "eQSL: unexpected answer");
    return r;
}

QslUploadResult parseClubLogResponse(int status, const QByteArray& body, int qsoCount)
{
    // Club Log risponde in chiaro, una riga: quello che conta e' il codice HTTP,
    // il testo serve a dire all'operatore che cosa ha sbagliato.
    QString text = QString::fromUtf8(body).simplified();
    text.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
    text = text.simplified().left(160);

    QslUploadResult r;
    const int count = qMax(1, qsoCount);
    if (status == 200) {
        if (text.contains(QLatin1String("dupl"), Qt::CaseInsensitive)) {
            r.ok = true;
            r.duplicates = count;
            r.message = QCoreApplication::translate("Qsl", "Club Log: already there");
            return r;
        }
        if (text.contains(QLatin1String("error"), Qt::CaseInsensitive)
            || text.contains(QLatin1String("invalid"), Qt::CaseInsensitive)) {
            r.rejected = count;
            r.message = QCoreApplication::translate("Qsl", "Club Log: %1").arg(text);
            return r;
        }
        r.ok = true;
        r.accepted = count;
        r.message = text.isEmpty() ? QCoreApplication::translate("Qsl", "Club Log: accepted") : text;
        return r;
    }
    if (status == 400 || status == 401 || status == 403) {
        // Chiave sbagliata, password sbagliata, nominativo non autorizzato: sono
        // cose da sistemare a mano, ritentare non serve.
        r.rejected = count;
        r.message = text.isEmpty()
            ? QCoreApplication::translate("Qsl", "Club Log: refused (%1)").arg(status)
            : QCoreApplication::translate("Qsl", "Club Log: %1").arg(text);
        return r;
    }
    r.retryLater = true;
    r.message = status > 0
        ? QCoreApplication::translate("Qsl", "Club Log: server answered %1").arg(status)
        : QCoreApplication::translate("Qsl", "Club Log: no answer");
    return r;
}

} // namespace qsl

// ── TQSL ──────────────────────────────────────────────────────────────────────

TqslUploader::TqslUploader(QObject* parent)
    : QObject(parent)
    , m_program(qsl::findTqsl())
{
}

void TqslUploader::upload(const QString& adifPath, const QString& location, int qsoCount)
{
    if (m_busy)
        return;
    QslUploadResult error;
    if (m_program.isEmpty() || !QFileInfo::exists(m_program)) {
        error.message = tr("TQSL not found: install Trusted QSL, or set its path in Setup → QSL services");
        emit finished(error);
        return;
    }
    if (!qsl::tqslHasCertificate()) {
        error.message = tr("TQSL has no certificate: import your LoTW certificate in TQSL first");
        emit finished(error);
        return;
    }

    // -u carica su LoTW, -d non chiede l'intervallo di date, -a all accetta i
    // duplicati senza fermarsi, -q esce da solo, -x niente finestra alla fine.
    QStringList args{QStringLiteral("-u"), QStringLiteral("-d"),
                     QStringLiteral("-a"), QStringLiteral("all"),
                     QStringLiteral("-q"), QStringLiteral("-x")};
    if (!location.trimmed().isEmpty())
        args << QStringLiteral("-l") << location.trimmed();
    args << QDir::toNativeSeparators(adifPath);

    m_busy = true;
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::finished, this, [this, qsoCount](int code, QProcess::ExitStatus status) {
        const QString output = QString::fromLocal8Bit(m_process->readAll());
        m_process->deleteLater();
        m_process = nullptr;
        m_busy = false;
        if (status == QProcess::CrashExit) {
            QslUploadResult crashed;
            crashed.message = tr("TQSL stopped unexpectedly");
            emit finished(crashed);
            return;
        }
        emit finished(qsl::resultFromTqslExit(code, output, qsoCount));
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (!m_busy)
            return;
        QslUploadResult failed;
        failed.message = tr("Cannot run TQSL: %1").arg(m_process ? m_process->errorString() : QString());
        m_busy = false;
        emit finished(failed);
    });
    m_process->start(m_program, args);
}

void TqslUploader::cancel()
{
    if (m_process)
        m_process->kill();
}

// ── QRZ Logbook, eQSL e Club Log ──────────────────────────────────────────────

WebQslUploader::WebQslUploader(QObject* parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
{
}

void WebQslUploader::setEndpoints(const QUrl& qrz, const QUrl& eqsl)
{
    m_qrzUrl = qrz;
    m_eqslUrl = eqsl;
}

void WebQslUploader::uploadQrz(const QString& apiKey, const QString& adifRecord)
{
    if (m_busy) return;
    if (apiKey.trimmed().isEmpty()) { QslUploadResult result; result.message = tr("QRZ: API key is required"); emit finished(result); return; }
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("KEY"), apiKey);
    form.addQueryItem(QStringLiteral("ACTION"), QStringLiteral("INSERT"));
    form.addQueryItem(QStringLiteral("ADIF"), adifRecord);
    send(Service::QrzLogbook, m_qrzUrl, form.toString(QUrl::FullyEncoded).toUtf8());
}

void WebQslUploader::uploadEqsl(const QString& user, const QString& password, const QString& adifRecord)
{
    if (m_busy) return;
    if (user.trimmed().isEmpty() || password.trimmed().isEmpty()) { QslUploadResult result; result.message = tr("eQSL: username and password are required"); emit finished(result); return; }
    // eQSL vuole utente e password nell'intestazione dell'ADIF.
    const QString document = QStringLiteral("<EQSL_USER:%1>%2<EQSL_PSWD:%3>%4<EOH>\n%5")
                                 .arg(user.size())
                                 .arg(user)
                                 .arg(password.size())
                                 .arg(password)
                                 .arg(adifRecord);
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("ADIFData"), document);
    send(Service::Eqsl, m_eqslUrl, form.toString(QUrl::FullyEncoded).toUtf8());
}

void WebQslUploader::uploadHrdLog(const QString& callsign, const QString& uploadCode, const QString& adifRecord)
{
    if (m_busy) return;
    if (callsign.trimmed().isEmpty() || uploadCode.trimmed().isEmpty()) { QslUploadResult result; result.message = tr("HRDLog: callsign and upload code are required"); emit finished(result); return; }
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("Callsign"), callsign);
    form.addQueryItem(QStringLiteral("Code"), uploadCode);
    form.addQueryItem(QStringLiteral("App"), QStringLiteral("DecoDXLog"));
    form.addQueryItem(QStringLiteral("ADIFData"), adifRecord);
    // QUrlQuery non codifica il '+': in un modulo sarebbe uno spazio.
    QByteArray body = form.toString(QUrl::FullyEncoded).toUtf8();
    body.replace('+', "%2B");
    send(Service::HrdLog, m_hrdLogUrl, body);
}

void WebQslUploader::uploadWavelog(const QString& server, const QString& apiKey, const QString& stationId,
                                   const QString& adifRecord)
{
    if (m_busy)
        return;
    if (apiKey.trimmed().isEmpty() || stationId.trimmed().isEmpty()) { QslUploadResult result; result.message = tr("Wavelog: API key and station are required"); emit finished(result); return; }
    const QUrl url = qsl::wavelogApiUrl(server, QStringLiteral("qso"));
    if (!url.isValid() || url.host().isEmpty()) {
        QslUploadResult failed;
        failed.message = tr("Wavelog: write the address of your Wavelog (Setup → QSL services)");
        emit finished(failed);
        return;
    }
    const QJsonObject body{{QStringLiteral("key"), apiKey},
                           {QStringLiteral("station_profile_id"), stationId},
                           {QStringLiteral("type"), QStringLiteral("adif")},
                           {QStringLiteral("string"), adifRecord}};
    m_busy = true;
    QNetworkRequest request(url);
    network::useHttp11(request);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("DecoDXLog/%1").arg(QCoreApplication::applicationVersion()));
    request.setTransferTimeout(30'000);
    watch(m_net->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact)), Service::Wavelog, 1);
}

void WebQslUploader::listWavelogStations(const QString& server, const QString& apiKey)
{
    if (apiKey.trimmed().isEmpty()) { emit wavelogStationsListed({}, tr("API key is required")); return; }
    const QUrl url = qsl::wavelogApiUrl(server, QStringLiteral("station_info/") + apiKey);
    if (!url.isValid() || url.host().isEmpty()) {
        emit wavelogStationsListed({}, tr("write the address of your Wavelog"));
        return;
    }
    QNetworkRequest request(url);
    network::useHttp11(request);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("DecoDXLog/%1").arg(QCoreApplication::applicationVersion()));
    request.setTransferTimeout(30'000);
    QNetworkReply* reply = m_net->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, apiKey] {
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString error;
        QVariantList stations;
        if (reply->error() != QNetworkReply::NoError && body.isEmpty()) {
            // La chiave sta nell'indirizzo: non deve finire nel messaggio.
            error = qsl::withoutKey(network::safeErrorString(reply), apiKey);
        } else {
            stations = qsl::parseWavelogStations(body, &error);
            if (stations.isEmpty() && error.isEmpty() && status >= 400)
                error = QStringLiteral("HTTP %1").arg(status);
        }
        emit wavelogStationsListed(stations, error);
    });
}

void WebQslUploader::setClubLogEndpoints(const QUrl& realtime, const QUrl& batch)
{
    m_clubLogRealtimeUrl = realtime;
    m_clubLogBatchUrl = batch;
}

void WebQslUploader::submitClubLogRealtimeBatch(const QString& callsign, const QString& token,
                                                  const QByteArray& adifDocument, int expectedCount)
{
    if (m_busy) return;
    const QString restriction = ClubLogAccessGuard::restriction(token);
    if (!restriction.isEmpty()) { QslUploadResult failed; failed.message = restriction; emit finished(failed); return; }
    const QString call = callsign.trimmed().toUpper();
    const auto document = adif::parse(adifDocument);
    const QRegularExpression safeCall(QStringLiteral("^[A-Z0-9]+(?:/[A-Z0-9]+)*$"));
    if (!safeCall.match(call).hasMatch() || !token.startsWith(QLatin1String("cl_"))
        || expectedCount < 1 || expectedCount > 100 || document.records.size() != expectedCount) {
        QslUploadResult failed; failed.message = tr("Club Log: invalid real-time batch"); emit finished(failed); return;
    }
    QJsonArray qsos;
    for (const auto& record : document.records)
        qsos.append(QJsonObject{{QStringLiteral("call"), call}, {QStringLiteral("adif"), adif::writeRecord(record)}});
    QNetworkRequest request(m_clubLogRestBatchBaseUrl.resolved(QUrl(QStringLiteral("qso/batch"))));
    request.setRawHeader("Authorization", QByteArray("Bearer ") + token.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(30000);
    m_busy = true;
    QNetworkReply* reply = m_net->post(request, QJsonDocument(QJsonObject{{QStringLiteral("qsos"), qsos}}).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, token, expectedCount] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        ClubLogAccessGuard::record(token, status, reply->rawHeader("Retry-After"));
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(reply->readAll(), &error);
        reply->deleteLater(); m_busy = false;
        QslUploadResult result; result.httpStatus = status;
        result.requiresReconciliation = status == 0 || status >= 500 || (status >= 200 && status < 300);
        const QJsonValue results = document.object().value(QStringLiteral("results"));
        if (status == 200 && error.error == QJsonParseError::NoError && results.isArray()
            && results.toArray().size() == expectedCount) {
            bool valid = true;
            for (const auto& value : results.toArray()) {
                if (!value.isObject()) { valid = false; break; }
                const auto row = value.toObject();
                const QString outcome = row.value(QStringLiteral("status")).toString();
                if (outcome != QLatin1String("created") && outcome != QLatin1String("updated")
                    && outcome != QLatin1String("duplicate") && outcome != QLatin1String("rejected")) { valid = false; break; }
                result.clubLogBatchRecords.append(ClubLogAccessGuard::redactValue(row.toVariantMap(), token));
            }
            if (!valid) result.clubLogBatchRecords.clear();
        }
        result.message = result.clubLogBatchRecords.isEmpty()
            ? tr("Club Log: real-time batch HTTP %1; verify contacts before retrying").arg(status)
            : tr("Club Log: checking the result of each contact");
        emit finished(result);
    });
}

void WebQslUploader::submitClubLogRestBatch(const QString& callsign, const QString& token,
                                               const QByteArray& adifDocument,
                                               std::function<bool(const QString&)> checkpoint)
{
    if (m_busy) return;
    const QString restriction = ClubLogAccessGuard::restriction(token);
    if (!restriction.isEmpty()) {
        QslUploadResult failed; failed.message = restriction; emit finished(failed); return;
    }
    const QString cleanCall = callsign.trimmed().toUpper();
    const QRegularExpression safeCall(QStringLiteral("^[A-Z0-9]+(?:/[A-Z0-9]+)*$"));
    if (!safeCall.match(cleanCall).hasMatch() || !token.startsWith(QStringLiteral("cl_"))
        || adifDocument.isEmpty() || !checkpoint) {
        QslUploadResult failed;
        failed.message = tr("Club Log REST: invalid callsign, token, ADIF or checkpoint");
        emit finished(failed);
        return;
    }
    const QString encodedCall = QString::fromLatin1(QUrl::toPercentEncoding(cleanCall, "/"));
    const QUrl url = m_clubLogRestBatchBaseUrl.resolved(QUrl(QStringLiteral("logs/") + encodedCall + QStringLiteral("/uploads")));
    auto* multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart file;
    file.setHeader(QNetworkRequest::ContentDispositionHeader, QStringLiteral("form-data; name=\"file\"; filename=\"decodxlog.adi\""));
    file.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
    file.setBody(adifDocument);
    multipart->append(file);
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QByteArray("Bearer ") + token.toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(120000);
    m_busy = true;
    QNetworkReply* reply = m_net->post(request, multipart);
    multipart->setParent(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, token, checkpoint = std::move(checkpoint)] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        ClubLogAccessGuard::record(token, status, reply->rawHeader("Retry-After"));
        const auto envelope = parseClubLogBatchEnvelope(status, reply->readAll(), true);
        reply->deleteLater();
        m_busy = false;
        QslUploadResult result;
        if (envelope.state == ClubLogBatchEnvelope::State::Queued) {
            result.remoteId = envelope.uploadId;
            result.requiresReconciliation = true;
            result.message = checkpoint(envelope.uploadId)
                ? tr("Club Log: upload queued; final processing and reconciliation are still required")
                : tr("Club Log: upload queued but its identifier could not be saved; reconcile manually");
        } else {
            // Never infer success from a malformed or test-mode response. A
            // transport/server failure might have followed a successful POST.
            result.requiresReconciliation = status == 0 || status == 409 || status >= 500 || (status >= 200 && status < 300);
            result.message = tr("Club Log upload: HTTP %1").arg(status);
            if (!envelope.message.isEmpty())
                result.message += QStringLiteral(": ") + ClubLogAccessGuard::redact(envelope.message, token);
        }
        emit finished(result);
    });
}

void WebQslUploader::pollClubLogRestBatch(const QString& callsign, const QString& token,
                                             const QString& uploadId, int expectedCount)
{
    if (m_busy) return;
    const QString restriction = ClubLogAccessGuard::restriction(token);
    if (!restriction.isEmpty()) {
        QslUploadResult pending; pending.requiresReconciliation = true;
        pending.remoteId = uploadId; pending.message = restriction; emit finished(pending); return;
    }
    const QString cleanCall = callsign.trimmed().toUpper();
    const QRegularExpression safeCall(QStringLiteral("^[A-Z0-9]+(?:/[A-Z0-9]+)*$"));
    bool idOk = false;
    const qlonglong numericId = uploadId.toLongLong(&idOk);
    if (!safeCall.match(cleanCall).hasMatch() || !token.startsWith(QStringLiteral("cl_"))
        || !idOk || numericId <= 0 || expectedCount <= 0) {
        QslUploadResult result;
        result.requiresReconciliation = true;
        result.message = tr("Club Log: invalid parameters for pending upload");
        emit finished(result);
        return;
    }
    struct PollState {
        QString call, token, uploadId, cursor;
        QSet<QString> cursors;
        QVariantList records;
        int polls {0};
        int queuedPolls {0};
        int expected {0};
    };
    auto state = std::make_shared<PollState>();
    state->call = cleanCall; state->token = token; state->uploadId = uploadId; state->expected = expectedCount;
    m_busy = true;
    auto poll = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> weakPoll = poll;
    *poll = [this, state, weakPoll] {
        const QString encodedCall = QString::fromLatin1(QUrl::toPercentEncoding(state->call, "/"));
        QUrl url = m_clubLogRestBatchBaseUrl.resolved(QUrl(QStringLiteral("logs/") + encodedCall
                   + QStringLiteral("/uploads/") + state->uploadId));
        QUrlQuery query;
        if (!state->cursor.isEmpty()) query.addQueryItem(QStringLiteral("cursor"), state->cursor);
        url.setQuery(query);
        QNetworkRequest request(url);
        request.setRawHeader("Authorization", QByteArray("Bearer ") + state->token.toUtf8());
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        request.setTransferTimeout(30000);
        QNetworkReply* reply = m_net->get(request);
        // Keep the callback alive only while a reply or scheduled timer exists.
        const auto keepPoll = weakPoll.lock();
        connect(reply, &QNetworkReply::finished, this, [this, state, reply, keepPoll] {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            ClubLogAccessGuard::record(state->token, status, reply->rawHeader("Retry-After"));
            const auto envelope = parseClubLogBatchEnvelope(status, reply->readAll(), false);
            reply->deleteLater();
            auto finishPending = [this, state, status](const QString& message) {
                m_busy = false;
                QslUploadResult result;
                result.remoteId = state->uploadId;
                result.requiresReconciliation = true;
                result.httpStatus = status;
                result.message = ClubLogAccessGuard::redact(message, state->token);
                emit finished(result);
            };
            if (++state->polls > 10000) {
                finishPending(tr("Club Log: upload polling limit reached; resume reconciliation later"));
                return;
            }
            if (envelope.state == ClubLogBatchEnvelope::State::Queued) {
                if (++state->queuedPolls > 120) {
                    finishPending(tr("Club Log: upload still queued; resume reconciliation later"));
                    return;
                }
                if (!keepPoll) { finishPending(tr("Club Log: polling interrupted")); return; }
                QTimer::singleShot(m_clubLogRestPollIntervalMs, this, [keepPoll] { (*keepPoll)(); });
                return;
            }
            if (envelope.state != ClubLogBatchEnvelope::State::Finished) {
                QString message = tr("Club Log: status HTTP %1").arg(status);
                if (!envelope.message.isEmpty()) message += QStringLiteral(": ") + envelope.message;
                else message += tr("; resume later");
                finishPending(message);
                return;
            }
            const QJsonValue rows = envelope.body.value(QStringLiteral("records"));
            if (!rows.isArray()) { finishPending(tr("Club Log: final status lacks record results")); return; }
            for (const QJsonValue& row : rows.toArray()) {
                if (!row.isObject() || state->records.size() >= state->expected) {
                    finishPending(tr("Club Log: invalid or excessive result records")); return;
                }
                state->records.append(ClubLogAccessGuard::redactValue(row.toObject().toVariantMap(), state->token));
            }
            const QJsonValue cursor = envelope.body.value(QStringLiteral("next_cursor"));
            if (!cursor.isUndefined() && !cursor.isNull() && !cursor.isString()) {
                finishPending(tr("Club Log: invalid result cursor")); return;
            }
            const QString next = cursor.toString();
            if (!state->token.isEmpty() && next.contains(state->token, Qt::CaseInsensitive)) {
                finishPending(tr("Club Log: invalid result cursor")); return;
            }
            if (!next.isEmpty()) {
                if (state->cursors.contains(next) || !keepPoll) { finishPending(tr("Club Log: repeated result cursor")); return; }
                state->cursors.insert(next); state->cursor = next;
                QTimer::singleShot(0, this, [keepPoll] { (*keepPoll)(); });
                return;
            }
            if (state->records.size() != state->expected) {
                finishPending(tr("Club Log: incomplete individual results; reconciliation required"));
                return;
            }
            m_busy = false;
            QslUploadResult result;
            result.remoteId = state->uploadId;
            result.clubLogBatchRecords = state->records;
            result.requiresReconciliation = true;
            int accepted = 0, duplicates = 0, rejected = 0, unknown = 0;
            for (const QVariant& value : std::as_const(state->records)) {
                const QString status = value.toMap().value(QStringLiteral("status")).toString().toLower();
                if (status == QLatin1String("created") || status == QLatin1String("updated")) ++accepted;
                else if (status == QLatin1String("duplicate")) ++duplicates;
                else if (status == QLatin1String("rejected")) ++rejected;
                else ++unknown;
            }
            result.message = tr("Club Log: %1 created or updated, %2 duplicates, %3 rejected, %4 unknown; individual reconciliation required")
                                 .arg(accepted).arg(duplicates).arg(rejected).arg(unknown);
            emit finished(result);
        });
    };
    (*poll)();
}

void WebQslUploader::resolveClubLogBatchIdentities(
    const QString& callsign, const QString& token, const QList<QJsonObject>& filters,
    const QSet<qint64>& expectedIds, std::function<void(const QList<QString>&, const QString&)> done,
    bool resolvePresence)
{
    const QString restriction = ClubLogAccessGuard::restriction(token);
    if (!restriction.isEmpty()) { done({}, restriction); return; }
    const QString cleanCall = callsign.trimmed().toUpper();
    const QRegularExpression safeCall(QStringLiteral("^[A-Z0-9]+(?:/[A-Z0-9]+)*$"));
    if (m_busy || !safeCall.match(cleanCall).hasMatch() || !token.startsWith(QLatin1String("cl_"))
        || filters.isEmpty() || expectedIds.size() > filters.size()) {
        done({}, tr("Club Log: cannot resolve batch identities"));
        return;
    }
    struct ResolveState {
        QList<QJsonObject> filters;
        QSet<qint64> expected, used;
        QList<QString> resolved;
        QString call, token;
        int offset {0};
        bool resolvePresence {false};
        std::function<void(const QList<QString>&, const QString&)> done;
    };
    auto state = std::make_shared<ResolveState>();
    state->filters = filters; state->expected = expectedIds; state->resolvePresence = resolvePresence;
    state->call = cleanCall; state->token = token; state->done = std::move(done);
    QSet<QByteArray> filterKeys;
    for (const QJsonObject& filter : filters) {
        const QByteArray key = QJsonDocument(filter).toJson(QJsonDocument::Compact);
        if (filter.isEmpty() || filterKeys.contains(key)) {
            state->done({}, tr("Club Log: multiple local contacts have the same identity"));
            return;
        }
        filterKeys.insert(key);
    }
    m_busy = true;
    auto queryNext = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> weakNext = queryNext;
    *queryNext = [this, state, weakNext] {
        const int count = qMin(100, int(state->filters.size()) - state->offset);
        QJsonArray requested;
        QHash<QByteArray, int> positions;
        for (int i = 0; i < count; ++i) {
            const QJsonObject filter = state->filters.at(state->offset + i);
            requested.append(filter);
            positions.insert(QJsonDocument(filter).toJson(QJsonDocument::Compact), i);
        }
        const QString path = QStringLiteral("logs/")
            + QString::fromLatin1(QUrl::toPercentEncoding(state->call, "/"))
            + QStringLiteral("/qsos/query");
        QNetworkRequest request(m_clubLogRestBatchBaseUrl.resolved(QUrl(path)));
        request.setRawHeader("Authorization", QByteArray("Bearer ") + state->token.toUtf8());
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        request.setTransferTimeout(30000);
        QNetworkReply* reply = m_net->post(request, QJsonDocument(QJsonObject{{QStringLiteral("filters"), requested}}).toJson(QJsonDocument::Compact));
        const auto keepNext = weakNext.lock();
        connect(reply, &QNetworkReply::finished, this, [this, state, reply, count, positions, keepNext] {
            const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            ClubLogAccessGuard::record(state->token, http, reply->rawHeader("Retry-After"));
            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &parseError);
            reply->deleteLater();
            auto fail = [this, state](const QString& message) {
                m_busy = false;
                state->done({}, message);
            };
            if (http != 200 || parseError.error != QJsonParseError::NoError || !doc.isObject()
                || !doc.object().value(QStringLiteral("results")).isArray()) {
                fail(tr("Club Log: identity query failed (HTTP %1); reconciliation remains pending").arg(http));
                return;
            }
            const QJsonArray results = doc.object().value(QStringLiteral("results")).toArray();
            if (results.size() != count) { fail(tr("Club Log: incomplete identity query")); return; }
            QList<QString> chunk;
            chunk.resize(count);
            QSet<int> seenFilters;
            for (const QJsonValue& value : results) {
                const QJsonObject result = value.toObject();
                const QByteArray key = QJsonDocument(result.value(QStringLiteral("filter")).toObject()).toJson(QJsonDocument::Compact);
                const int position = positions.value(key, -1);
                const QJsonValue qsosValue = result.value(QStringLiteral("qsos"));
                if (position < 0 || seenFilters.contains(position) || !qsosValue.isArray()
                    || qsosValue.toArray().size() >= 100) {
                    fail(tr("Club Log: ambiguous identity query")); return;
                }
                seenFilters.insert(position);
                QSet<qint64> candidates;
                for (const QJsonValue& qso : qsosValue.toArray()) {
                    bool ok = false;
                    const qint64 id = qso.toObject().value(QStringLiteral("qsoid")).toVariant().toLongLong(&ok);
                    if (state->resolvePresence && (!qso.isObject() || !ok || id <= 0)) {
                        fail(tr("Club Log: invalid contact identity")); return;
                    }
                    if (ok && id > 0 && (state->resolvePresence || state->expected.contains(id))) candidates.insert(id);
                }
                if (candidates.size() > 1 && state->resolvePresence) continue;
                if (candidates.size() > 1 || (!candidates.isEmpty() && state->used.contains(*candidates.cbegin()))) {
                    fail(tr("Club Log: contact identity is not unique")); return;
                }
                if (!candidates.isEmpty()) {
                    const qint64 id = *candidates.cbegin();
                    state->used.insert(id);
                    chunk[position] = QString::number(id);
                }
            }
            state->resolved.append(chunk);
            state->offset += count;
            if (state->offset == state->filters.size()) {
                if (!state->resolvePresence && state->used != state->expected) {
                    fail(tr("Club Log: incomplete contact identities")); return;
                }
                m_busy = false;
                state->done(state->resolved, {});
            } else if (keepNext) {
                QTimer::singleShot(0, this, [keepNext] { (*keepNext)(); });
            } else {
                fail(tr("Club Log: identity query interrupted"));
            }
        });
    };
    (*queryNext)();
}

void WebQslUploader::uploadClubLog(const ClubLogAuth& auth, const QByteArray& adifDocument, int qsoCount)
{
    if (m_busy)
        return;
    if (auth.password.startsWith(QStringLiteral("cl_"))) {
        const QString restriction = ClubLogAccessGuard::restriction(auth.password);
        if (!restriction.isEmpty()) {
            QslUploadResult failed; failed.message = restriction; emit finished(failed); return;
        }
        const auto document = adif::parse(adifDocument);
        if (auth.callsign.isEmpty() || qsoCount != 1 || document.records.size() != 1) {
            QslUploadResult failed;
            failed.message = tr("Club Log REST: exactly one QSO and a station callsign are required");
            emit finished(failed);
            return;
        }
        QJsonObject body{{QStringLiteral("call"), auth.callsign},
                         {QStringLiteral("adif"), adif::writeRecord(document.records.first())}};
        QNetworkRequest request(m_clubLogRestUrl);
        network::useHttp11(request);
        request.setRawHeader("Authorization", QByteArray("Bearer ") + auth.password.toUtf8());
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        request.setTransferTimeout(30000);
        m_busy = true;
        watch(m_net->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact)), Service::ClubLogRest, 1);
        return;
    }
    if (!auth.complete()) {
        QslUploadResult missing;
        missing.message = tr("Club Log: email, password, callsign and API key are all needed");
        emit finished(missing);
        return;
    }

    // A refused legacy credential must stay blocked across restarts. Keep only
    // a fingerprint of the complete configuration, never credentials in settings.
    const QByteArray legacyIdentity = QJsonDocument(QJsonArray{auth.email, auth.password, auth.callsign, auth.apiKey})
                                          .toJson(QJsonDocument::Compact);
    const QString guardKey = QStringLiteral("legacy-") + QString::fromLatin1(
        QCryptographicHash::hash(legacyIdentity, QCryptographicHash::Sha256).toHex());
    const QString restriction = ClubLogAccessGuard::restriction(guardKey);
    if (!restriction.isEmpty()) {
        QslUploadResult failed; failed.message = restriction; emit finished(failed); return;
    }

    if (qsoCount <= 1) {
        // Un QSO appena fatto: realtime.php lo aggiunge senza rileggere il log.
        QUrlQuery form;
        form.addQueryItem(QStringLiteral("email"), auth.email);
        form.addQueryItem(QStringLiteral("password"), auth.password);
        form.addQueryItem(QStringLiteral("callsign"), auth.callsign);
        form.addQueryItem(QStringLiteral("api"), auth.apiKey);
        form.addQueryItem(QStringLiteral("adif"), QString::fromUtf8(adifDocument));
        m_busy = true;
        QNetworkRequest request(m_clubLogRealtimeUrl);
        network::useHttp11(request);
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
        request.setHeader(QNetworkRequest::UserAgentHeader,
                          QStringLiteral("DecoDXLog/%1").arg(QCoreApplication::applicationVersion()));
        request.setTransferTimeout(30'000);
        QNetworkReply* reply = m_net->post(request, form.toString(QUrl::FullyEncoded).toUtf8());
        reply->setProperty("clubLogLegacyGuard", guardKey);
        watch(reply, Service::ClubLog, 1);
        return;
    }

    // Piu' QSO insieme: un file ADIF, come se lo si caricasse dal sito.
    auto* multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    const auto field = [multi](const QString& name, const QString& value) {
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QStringLiteral("form-data; name=\"%1\"").arg(name));
        part.setBody(value.toUtf8());
        multi->append(part);
    };
    field(QStringLiteral("email"), auth.email);
    field(QStringLiteral("password"), auth.password);
    field(QStringLiteral("callsign"), auth.callsign);
    field(QStringLiteral("api"), auth.apiKey);
    field(QStringLiteral("clear"), QStringLiteral("0"));   // aggiunge, non cancella il log

    QHttpPart file;
    file.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QStringLiteral("form-data; name=\"file\"; filename=\"decolog.adi\""));
    file.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
    file.setBody(adifDocument);
    multi->append(file);

    m_busy = true;
    QNetworkRequest request(m_clubLogBatchUrl);
    network::useHttp11(request);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("DecoDXLog/%1").arg(QCoreApplication::applicationVersion()));
    request.setTransferTimeout(120'000);   // un blocco grosso ci mette di piu'
    QNetworkReply* reply = m_net->post(request, multi);
    reply->setProperty("clubLogLegacyGuard", guardKey);
    multi->setParent(reply);
    watch(reply, Service::ClubLog, qsoCount);
}

namespace {
// Tutte le chiamate a CRX hanno la stessa busta: {"req": {type, query, apikey, ...}}.
QByteArray crxRequest(const QString& query, const QString& apiKey, const QJsonObject& extra)
{
    QJsonObject req{{QStringLiteral("type"), QStringLiteral("radio")},
                    {QStringLiteral("query"), query},
                    {QStringLiteral("apikey"), apiKey}};
    for (auto it = extra.begin(); it != extra.end(); ++it)
        req.insert(it.key(), it.value());
    return QJsonDocument(QJsonObject{{QStringLiteral("req"), req}}).toJson(QJsonDocument::Compact);
}

QNetworkRequest crxHttpRequest(const QUrl& url)
{
    QNetworkRequest request(url);
    network::useHttp11(request);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("DecoDXLog/%1").arg(QCoreApplication::applicationVersion()));
    request.setTransferTimeout(30'000);
    return request;
}
} // namespace

void WebQslUploader::uploadCrx(const QString& apiKeyAsSaved, const QJsonObject& qsoData)
{
    const QString apiKey = apiKeyAsSaved.trimmed();
    if (m_busy)
        return;
    if (apiKey.isEmpty()) { QslUploadResult result; result.message = tr("CRX: API key is required"); emit finished(result); return; }
    m_busy = true;
    const QByteArray body = crxRequest(QStringLiteral("edit_myqso"), apiKey,
                                       QJsonObject{{QStringLiteral("qsoData"), qsoData}});
    watch(m_net->post(crxHttpRequest(m_crxUrl), body), Service::Crx, 1);
}

void WebQslUploader::deleteCrx(const QString& apiKeyAsSaved, qint64 remoteQsoId)
{
    const QString apiKey = apiKeyAsSaved.trimmed();
    if (m_busy)
        return;
    if (apiKey.isEmpty()) { QslUploadResult result; result.message = tr("CRX: API key is required"); emit finished(result); return; }
    m_busy = true;
    const QByteArray body = crxRequest(QStringLiteral("edit_myqso"), apiKey,
                                       QJsonObject{{QStringLiteral("qso_id"), remoteQsoId},
                                                   {QStringLiteral("action"), QStringLiteral("delete")}});
    watch(m_net->post(crxHttpRequest(m_crxUrl), body), Service::Crx, 1);
}

void WebQslUploader::listCrxLogs(const QString& apiKey)
{
    if (apiKey.trimmed().isEmpty()) { emit crxLogsListed({}, tr("API key is required")); return; }
    QNetworkReply* reply = m_net->post(crxHttpRequest(m_crxUrl),
                                       crxRequest(QStringLiteral("get_mylogs"), apiKey.trimmed(), {}));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray answer = reply->readAll();
        QString error;
        QVariantList logs;
        if (reply->error() != QNetworkReply::NoError && status == 0)
            error = network::safeErrorString(reply);
        else if (status == 401)
            error = qsl::crxKeyRefusedMessage({});
        else
            logs = qsl::parseCrxLogs(answer, &error);
        emit crxLogsListed(logs, error);
    });
}

void WebQslUploader::send(Service service, const QUrl& url, const QByteArray& body)
{
    if (m_busy)
        return;
    m_busy = true;
    QNetworkRequest request(url);
    network::useHttp11(request);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("DecoDXLog/%1").arg(QCoreApplication::applicationVersion()));
    request.setTransferTimeout(30'000);
    watch(m_net->post(request, body), service, 1);
}

void WebQslUploader::watch(QNetworkReply* reply, Service service, int qsoCount)
{
    connect(reply, &QNetworkReply::finished, this, [this, reply, service, qsoCount] {
        reply->deleteLater();
        m_busy = false;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray answer = reply->readAll();
        if (service == Service::Crx) {
            // CRX dice il motivo nel corpo JSON anche con 400 o 401.
            if (reply->error() != QNetworkReply::NoError && status == 0) {
                QslUploadResult failed;
                failed.retryLater = true;
                failed.message = network::safeErrorString(reply);
                emit finished(failed);
                return;
            }
            emit finished(qsl::parseCrxResponse(status, answer));
            return;
        }
        if (service == Service::ClubLogRest) {
            const QByteArray authorization = reply->request().rawHeader("Authorization");
            const QString token = authorization.startsWith("Bearer ") ? QString::fromUtf8(authorization.mid(7)) : QString();
            if (authorization.startsWith("Bearer "))
                ClubLogAccessGuard::record(QString::fromUtf8(authorization.mid(7)), status, reply->rawHeader("Retry-After"));
            QslUploadResult result;
            const QJsonObject object = QJsonDocument::fromJson(answer).object();
            const QString outcome = object.value(QStringLiteral("status")).toString();
            result.message = ClubLogAccessGuard::redact(clubLogResponseMessage(object), token);
            const QString qsoId = object.value(QStringLiteral("qsoid")).toVariant().toString();
            const bool validQsoId = qsoId.toLongLong() > 0;
            if (validQsoId && ((status == 201 && outcome == QLatin1String("created")) ||
                (status == 200 && (outcome == QLatin1String("updated") || outcome == QLatin1String("duplicate"))))) {
                result.ok = true;
                result.accepted = outcome == QLatin1String("duplicate") ? 0 : 1;
                result.duplicates = outcome == QLatin1String("duplicate") ? 1 : 0;
                result.remoteId = qsoId;
            } else {
                result.rejected = 1;
                // A transport error after submission has an ambiguous outcome;
                // do not automatically resend a QSO that may already exist.
                result.requiresReconciliation = status == 0 || status >= 500 || (status >= 200 && status < 300);
                if (result.message.isEmpty())
                    result.message = tr("Club Log REST: HTTP %1 (%2)").arg(status).arg(ClubLogAccessGuard::redact(outcome, token));
                else
                    result.message = tr("Club Log REST: HTTP %1: %2").arg(status).arg(result.message);
            }
            emit finished(result);
            return;
        }
        if (service == Service::ClubLog) {
            if (status == 403)
                ClubLogAccessGuard::record(reply->property("clubLogLegacyGuard").toString(), status, {});
            // Club Log dice il motivo nel corpo anche quando risponde 403, e
            // quel motivo serve all'operatore piu' del codice.
            if (reply->error() != QNetworkReply::NoError && status == 0) {
                QslUploadResult failed;
                failed.retryLater = true;
                failed.message = network::safeErrorString(reply);
                emit finished(failed);
                return;
            }
            emit finished(qsl::parseClubLogResponse(status, answer, qsoCount));
            return;
        }
        // Wavelog dice il motivo nel corpo JSON anche con 400.
        if (reply->error() != QNetworkReply::NoError && !(service == Service::Wavelog && status >= 400)) {
            QslUploadResult failed;
            failed.retryLater = true;
            failed.message = network::safeErrorString(reply);
            emit finished(failed);
            return;
        }
        if (service == Service::Wavelog) {
            emit finished(qsl::parseWavelogResponse(status, answer));
            return;
        }
        emit finished(service == Service::QrzLogbook ? qsl::parseQrzResponse(answer)
                      : service == Service::HrdLog   ? qsl::parseHrdLogResponse(answer)
                                                     : qsl::parseEqslResponse(answer));
    });
}

} // namespace decolog::core
