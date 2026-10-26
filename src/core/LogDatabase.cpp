#include "core/LogDatabase.h"
#include "../StartupTrace.h"

#include "core/Modes.h"

#include "core/Bands.h"
#include "core/LogMigration.h"

#include <QFile>
#include <QCryptographicHash>
#include <QSaveFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTimeZone>
#include <QUuid>
#include <QVariant>
#include <algorithm>
#include <array>
#include <limits>

namespace decolog::core {

namespace {

enum class Kind { Text, Upper, Lower, Real, Integer };

struct Column {
    const char* adif;
    const char* column;
    Kind kind;
};

// I campi ADIF che hanno una colonna. CALL, BAND e MODE sono obbligatori; date e
// ore sono trattate a parte perche' due campi ADIF diventano una colonna sola.
constexpr std::array kColumns{
    Column{"CALL", "call", Kind::Upper},
    Column{"BAND", "band", Kind::Lower},
    Column{"BAND_RX", "band_rx", Kind::Lower},
    Column{"FREQ", "freq", Kind::Real},
    Column{"FREQ_RX", "freq_rx", Kind::Real},
    Column{"MODE", "mode", Kind::Upper},
    Column{"SUBMODE", "submode", Kind::Upper},
    Column{"RST_SENT", "rst_sent", Kind::Text},
    Column{"RST_RCVD", "rst_rcvd", Kind::Text},
    Column{"GRIDSQUARE", "gridsquare", Kind::Text},
    Column{"NAME", "name", Kind::Text},
    Column{"QTH", "qth", Kind::Text},
    Column{"COUNTRY", "country", Kind::Text},
    Column{"DXCC", "dxcc", Kind::Integer},
    Column{"CQZ", "cqz", Kind::Integer},
    Column{"ITUZ", "ituz", Kind::Integer},
    Column{"CONT", "cont", Kind::Text},
    Column{"STATE", "state", Kind::Text},
    Column{"CNTY", "cnty", Kind::Text},
    Column{"IOTA", "iota", Kind::Text},
    Column{"SOTA_REF", "sota_ref", Kind::Text},
    Column{"POTA_REF", "pota_ref", Kind::Text},
    Column{"WWFF_REF", "wwff_ref", Kind::Text},
    // Il programma di un QSO e il suo riferimento: "DCI" e "PR001", "GMA" e
    // "I/PI-001". I diplomi italiani viaggiano qui dentro.
    Column{"SIG", "sig", Kind::Text},
    Column{"SIG_INFO", "sig_info", Kind::Text},
    Column{"PROP_MODE", "prop_mode", Kind::Text},
    Column{"SAT_NAME", "sat_name", Kind::Text},
    Column{"SAT_MODE", "sat_mode", Kind::Text},
    Column{"TX_PWR", "tx_pwr", Kind::Real},
    Column{"COMMENT", "comment", Kind::Text},
    Column{"NOTES", "notes", Kind::Text},
    Column{"APP_DECOLOG_TAGS", "tags", Kind::Text},
};

// Campi ADIF degli stati QSL, un servizio per riga di qsl_status. Club Log non
// ha un "ricevuto" in ADIF.
struct QslFields {
    const char* service;
    const char* sent;
    const char* sentDate;
    const char* rcvd;
    const char* rcvdDate;
};

constexpr std::array kQslFields{
    QslFields{"lotw", "LOTW_QSL_SENT", "LOTW_QSLSDATE", "LOTW_QSL_RCVD", "LOTW_QSLRDATE"},
    QslFields{"qrz", "QRZCOM_QSO_UPLOAD_STATUS", "QRZCOM_QSO_UPLOAD_DATE",
              "QRZCOM_QSO_DOWNLOAD_STATUS", "QRZCOM_QSO_DOWNLOAD_DATE"},
    QslFields{"clublog", "CLUBLOG_QSO_UPLOAD_STATUS", "CLUBLOG_QSO_UPLOAD_DATE", "", ""},
    QslFields{"hrdlog", "HRDLOG_QSO_UPLOAD_STATUS", "HRDLOG_QSO_UPLOAD_DATE", "", ""},
    QslFields{"eqsl", "EQSL_QSL_SENT", "EQSL_QSLSDATE", "EQSL_QSL_RCVD", "EQSL_QSLRDATE"},
    QslFields{"card", "QSL_SENT", "QSLSDATE", "QSL_RCVD", "QSLRDATE"},
};

const Column* columnFor(const QString& adifName)
{
    for (const auto& c : kColumns) {
        if (adifName == QLatin1String(c.adif))
            return &c;
    }
    return nullptr;
}

QString nowIso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

QDateTime parseIso(const QString& iso)
{
    QDateTime dt = QDateTime::fromString(iso, Qt::ISODate);
    dt.setTimeZone(QTimeZone::UTC);
    return dt;
}

// Il valore nella forma che va in colonna. nullopt = non rappresentabile (es.
// DXCC non numerico): il campo resta intatto in adif_extra.
std::optional<QVariant> toColumnValue(const Column& c, const QString& value)
{
    switch (c.kind) {
    case Kind::Text:
        return QVariant(value);
    case Kind::Upper:
        return QVariant(value.trimmed().toUpper());
    case Kind::Lower:
        return QVariant(value.trimmed().toLower());
    case Kind::Real: {
        bool ok = false;
        const double v = value.trimmed().toDouble(&ok);
        if (!ok)
            return std::nullopt;
        return QVariant(v);
    }
    case Kind::Integer: {
        bool ok = false;
        const qlonglong v = value.trimmed().toLongLong(&ok);
        if (!ok)
            return std::nullopt;
        return QVariant(v);
    }
    }
    return std::nullopt;
}

QString fromColumnValue(const Column& c, const QVariant& v)
{
    if (v.isNull())
        return {};
    switch (c.kind) {
    case Kind::Real:
        // Frequenze in MHz con 6 decimali; le potenze senza zeri inutili.
        if (qstrcmp(c.column, "tx_pwr") == 0)
            return QString::number(v.toDouble());
        return QString::number(v.toDouble(), 'f', 6);
    case Kind::Integer:
        return QString::number(v.toLongLong());
    default:
        return v.toString();
    }
}

void addDateTime(AdifRecord& r, const QString& iso, const char* dateField, const char* timeField)
{
    if (iso.isEmpty())
        return;
    const QDateTime dt = parseIso(iso);
    if (!dt.isValid())
        return;
    r.set(QLatin1String(dateField), dt.toString(QStringLiteral("yyyyMMdd")));
    r.set(QLatin1String(timeField), dt.toString(QStringLiteral("HHmmss")));
}

bool isStatusFlag(const QString& v)
{
    return v.size() == 1 && v.at(0).isLetter();
}

// Il modo come lo legge un operatore: FT2 e non MFSK, ma SSB e non USB.
QString displayMode(const QString& mode, const QString& submode)
{
    return submode.isEmpty() || mode == QLatin1String("SSB") ? mode : submode;
}

AdifRecord recordFromSnapshot(const QString& json)
{
    AdifRecord r;
    const QJsonObject obj = QJsonDocument::fromJson(json.toUtf8()).object();
    const QJsonArray fields = obj.value(QStringLiteral("fields")).toArray();
    for (const auto& f : fields) {
        const QJsonArray pair = f.toArray();
        if (pair.size() == 2)
            r.set(pair.at(0).toString(), pair.at(1).toString());
    }
    return r;
}

} // namespace

// Un record ADIF gia' tradotto in colonne, stati QSL e campi extra.
struct LogDatabase::Prepared {
    QString call;
    QString band;
    QString mode;
    QString submode;
    QString on;
    QString off;
    QList<QPair<QString, QVariant>> columns;   // solo le colonne ADIF di kColumns
    QString extraJson;
    QList<QslState> qsl;
};

LogDatabase::LogDatabase()
    : m_connectionName(QStringLiteral("decolog-") + QUuid::createUuid().toString(QUuid::WithoutBraces))
{
}

LogDatabase::~LogDatabase()
{
    close();
}

QSqlDatabase LogDatabase::connection() const
{
    return QSqlDatabase::database(m_connectionName, false);
}

bool LogDatabase::isOpen() const
{
    return QSqlDatabase::contains(m_connectionName) && connection().isOpen();
}

bool LogDatabase::open(const QString& path)
{
    decolog::StartupSpan trace("LogDatabase::open");
    close();
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(path);
    if (!db.open()) {
        m_lastError = db.lastError().text();
        return false;
    }
    m_path = path;

    {
        QSqlQuery q(db);
        // Per connessione, non per file: vanno ripetute a ogni apertura.
        q.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
        q.exec(QStringLiteral("PRAGMA busy_timeout = 3000"));
    }

    if (!applySchema()) {
        close();
        return false;
    }
    // Le statistiche per il pianificatore di SQLite: senza, un indice che
    // copre quasi tutto il log sembra una scorciatoia e rende lente le letture
    // di tutto il log. analysis_limit le tiene a pochi millisecondi anche su
    // un milione di QSO, e optimize le rifa' solo quando il log e' cambiato
    // molto.
    {
        QSqlQuery q(db);
        q.exec(QStringLiteral("PRAGMA analysis_limit = 400"));
        q.exec(QStringLiteral("PRAGMA optimize = 0x10002"));
    }
    return true;
}

void LogDatabase::close()
{
    if (!QSqlDatabase::contains(m_connectionName))
        return;
    {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
        if (db.isOpen())
            db.close();
    }
    QSqlDatabase::removeDatabase(m_connectionName);
}

int LogDatabase::schemaVersion() const
{
    QSqlQuery q(connection());
    if (q.exec(QStringLiteral("SELECT MAX(version) FROM schema_version")) && q.next())
        return q.value(0).toInt();
    return 0;
}

void LogDatabase::setDedupWindows(int digitalSeconds, int manualSeconds)
{
    m_dedupDigital = qMax(0, digitalSeconds);
    m_dedupManual = qMax(0, manualSeconds);
}

bool LogDatabase::applySchema()
{
    QSqlDatabase db = connection();
    QSqlQuery q(db);
    if (q.exec(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type='table' AND name='schema_version'"))
        && q.next()) {
        return migrate();
    }

    QFile file(QStringLiteral(":/decolog/schema.sql"));
    if (!file.open(QIODevice::ReadOnly)) {
        m_lastError = QStringLiteral("schema.sql missing from resources");
        return false;
    }

    // Via i commenti, poi un'istruzione per ';'. Lo schema non ha trigger, quindi
    // non ci sono ';' dentro le istruzioni.
    QString sql;
    for (const QString& line : QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'))) {
        const qsizetype comment = line.indexOf(QLatin1String("--"));
        sql += (comment >= 0 ? line.left(comment) : line) + QLatin1Char('\n');
    }

    QStringList pragmas;
    QStringList statements;
    for (const QString& raw : sql.split(QLatin1Char(';'))) {
        const QString stmt = raw.trimmed();
        if (stmt.isEmpty())
            continue;
        (stmt.startsWith(QLatin1String("PRAGMA"), Qt::CaseInsensitive) ? pragmas : statements) << stmt;
    }

    // journal_mode non si puo' cambiare dentro una transazione.
    for (const QString& p : pragmas)
        q.exec(p);

    if (!db.transaction()) {
        m_lastError = db.lastError().text();
        return false;
    }
    for (const QString& stmt : statements) {
        if (!q.exec(stmt)) {
            m_lastError = q.lastError().text() + QStringLiteral(" in: ") + stmt.left(80);
            db.rollback();
            return false;
        }
    }
    return db.commit();
}

// Un log creato da una versione precedente si porta avanti un passo alla volta,
// ogni passo nella sua transazione. Mai all'indietro: una versione vecchia di
// DecoDXLog che apre un log nuovo trova solo colonne in piu'.
bool LogDatabase::migrate()
{
    QSqlDatabase db = connection();
    const int version = schemaVersion();
    struct Step {
        int to;
        QStringList statements;
        // Un indice su una colonna che un log molto vecchio non ha: se manca, il
        // passo si segna fatto senza indice.
        const char* needsQsoColumn{nullptr};
    };
    const QList<Step> steps{
        {2, {QStringLiteral("ALTER TABLE qso ADD COLUMN tags TEXT")}},
        {3, {QStringLiteral("ALTER TABLE qsl_status ADD COLUMN via TEXT")}},
        {4, {QStringLiteral("ALTER TABLE qso ADD COLUMN sat_mode TEXT")}},
        {5, {QStringLiteral("ALTER TABLE qso ADD COLUMN sig TEXT"),
             QStringLiteral("ALTER TABLE qso ADD COLUMN sig_info TEXT")}},
        // I conti dell'FT2 cercano per sottomodo: senza indice, su un log da
        // un milione, erano sei secondi a ogni scheda di QSO aperta. E la
        // tabella chiede i QSO non cancellati in ordine di tempo: con un
        // indice solo di quelli, gli id escono senza toccare la tabella.
        {6, {QStringLiteral("CREATE INDEX IF NOT EXISTS idx_qso_submode ON qso(submode)"),
             QStringLiteral("CREATE INDEX IF NOT EXISTS idx_qso_live_time ON qso(qso_datetime_on) WHERE deleted = 0"),
             // Le etichette e i QSO per profilo: indici piccoli, solo delle
             // righe che servono, per i menu e l'elenco dei profili.
             QStringLiteral("CREATE INDEX IF NOT EXISTS idx_qso_tags ON qso(tags) WHERE IFNULL(tags, '') <> ''"),
             QStringLiteral("CREATE INDEX IF NOT EXISTS idx_qso_profile ON qso(station_profile_id) WHERE deleted = 0")},
         "submode"},
        {7, {QStringLiteral("CREATE TABLE IF NOT EXISTS qso_enrichment ("
                         "id INTEGER PRIMARY KEY, qso_id INTEGER NOT NULL REFERENCES qso(id) ON DELETE CASCADE, "
                         "field TEXT NOT NULL, previous_value TEXT, value TEXT NOT NULL, source TEXT NOT NULL, "
                         "kind TEXT NOT NULL, acquired_at TEXT NOT NULL, valid_from TEXT, valid_to TEXT)"),
             QStringLiteral("CREATE INDEX IF NOT EXISTS idx_enrichment_qso ON qso_enrichment(qso_id, field)"),
         }, nullptr},
        {8, {QStringLiteral("CREATE TABLE IF NOT EXISTS qsl_attachment ("
                         "id INTEGER PRIMARY KEY, sha256 TEXT NOT NULL UNIQUE, filename TEXT NOT NULL, "
                         "mime TEXT NOT NULL, size INTEGER NOT NULL, data BLOB NOT NULL, created_at TEXT NOT NULL)"),
             QStringLiteral("CREATE TABLE IF NOT EXISTS qsl_attachment_qso ("
                         "attachment_id INTEGER NOT NULL REFERENCES qsl_attachment(id) ON DELETE CASCADE, "
                         "qso_id INTEGER NOT NULL REFERENCES qso(id) ON DELETE CASCADE, "
                         "PRIMARY KEY (attachment_id, qso_id))"),
             QStringLiteral("CREATE INDEX IF NOT EXISTS idx_qsl_attachment_qso ON qsl_attachment_qso(qso_id)")}, nullptr},
        {9, {QStringLiteral("CREATE TABLE IF NOT EXISTS api_idempotency ("
                         "key TEXT PRIMARY KEY, request_body BLOB NOT NULL, status INTEGER NOT NULL, "
                         "content_type TEXT NOT NULL, response_body BLOB NOT NULL, created_at TEXT NOT NULL)"),
             QStringLiteral("CREATE INDEX IF NOT EXISTS idx_api_idempotency_created ON api_idempotency(created_at)")}, nullptr},
        {10, {QStringLiteral("ALTER TABLE station_profile ADD COLUMN award_account TEXT"),
              QStringLiteral("ALTER TABLE station_profile ADD COLUMN operating_location TEXT"),
              QStringLiteral("ALTER TABLE station_profile ADD COLUMN valid_from TEXT"),
              QStringLiteral("ALTER TABLE station_profile ADD COLUMN valid_to TEXT")}, nullptr},
        {11, {QStringLiteral("CREATE TABLE IF NOT EXISTS operator_profile (id INTEGER PRIMARY KEY, callsign TEXT NOT NULL UNIQUE, display_name TEXT, updated_at TEXT NOT NULL)"),
              QStringLiteral("CREATE TABLE IF NOT EXISTS operating_location (id INTEGER PRIMARY KEY, location_key TEXT NOT NULL UNIQUE, label TEXT, gridsquare TEXT, dxcc INTEGER, updated_at TEXT NOT NULL)"),
              QStringLiteral("CREATE TABLE IF NOT EXISTS dxcc_entity (id INTEGER PRIMARY KEY, name TEXT, updated_at TEXT NOT NULL)"),
              QStringLiteral("ALTER TABLE station_profile ADD COLUMN operator_profile_id INTEGER REFERENCES operator_profile(id)"),
              QStringLiteral("ALTER TABLE station_profile ADD COLUMN operating_location_id INTEGER REFERENCES operating_location(id)"),
              QStringLiteral("ALTER TABLE station_profile ADD COLUMN dxcc_entity_id INTEGER REFERENCES dxcc_entity(id)"),
              QStringLiteral("INSERT OR IGNORE INTO operator_profile(callsign, display_name, updated_at) SELECT DISTINCT operator, operator, strftime('%Y-%m-%dT%H:%M:%SZ','now') FROM station_profile WHERE IFNULL(operator,'') <> ''"),
              QStringLiteral("INSERT OR IGNORE INTO operating_location(location_key, label, gridsquare, dxcc, updated_at) SELECT DISTINCT operating_location, operating_location, my_gridsquare, my_dxcc, strftime('%Y-%m-%dT%H:%M:%SZ','now') FROM station_profile WHERE IFNULL(operating_location,'') <> ''"),
              QStringLiteral("INSERT OR IGNORE INTO dxcc_entity(id, name, updated_at) SELECT DISTINCT my_dxcc, '', strftime('%Y-%m-%dT%H:%M:%SZ','now') FROM station_profile WHERE IFNULL(my_dxcc,0) > 0"),
              QStringLiteral("UPDATE station_profile SET operator_profile_id = (SELECT id FROM operator_profile WHERE callsign = station_profile.operator), operating_location_id = (SELECT id FROM operating_location WHERE location_key = station_profile.operating_location), dxcc_entity_id = my_dxcc WHERE operator_profile_id IS NULL OR operating_location_id IS NULL OR dxcc_entity_id IS NULL")}, nullptr},
    };
    for (const Step& step : steps) {
        if (version >= step.to)
            continue;
        if (!db.transaction()) {
            m_lastError = db.lastError().text();
            return false;
        }
        QSqlQuery q(db);
        QStringList statements = step.statements;
        if (step.to == 10) {
            QSqlQuery exists(db);
            if (!exists.exec(QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='station_profile'"))
                || !exists.next()) {
                db.rollback();
                m_lastError = QStringLiteral("cannot inspect station_profile during migration");
                return false;
            }
            if (exists.value(0).toInt() == 0) {
                statements = {QStringLiteral(
                    "CREATE TABLE station_profile (id INTEGER PRIMARY KEY, uuid TEXT NOT NULL UNIQUE, "
                    "name TEXT NOT NULL, station_callsign TEXT NOT NULL, operator TEXT, my_gridsquare TEXT, "
                    "my_cq_zone INTEGER, my_itu_zone INTEGER, my_dxcc INTEGER, my_rig TEXT, my_antenna TEXT, "
                    "default_tx_pwr REAL, lotw_station_loc TEXT, award_account TEXT, operating_location TEXT, "
                    "valid_from TEXT, valid_to TEXT, is_default INTEGER NOT NULL DEFAULT 0, "
                    "revision INTEGER NOT NULL DEFAULT 1, updated_at TEXT NOT NULL, deleted INTEGER NOT NULL DEFAULT 0, "
                    "dirty INTEGER NOT NULL DEFAULT 1)")};
            } else {
                const QStringList columns{QStringLiteral("award_account"), QStringLiteral("operating_location"),
                                          QStringLiteral("valid_from"), QStringLiteral("valid_to")};
                statements.clear();
                for (const QString& column : columns) {
                    QSqlQuery columnQuery(db);
                    columnQuery.prepare(QStringLiteral("SELECT COUNT(*) FROM pragma_table_info('station_profile') WHERE name = ?"));
                    columnQuery.addBindValue(column);
                    if (!columnQuery.exec() || !columnQuery.next()) {
                        db.rollback();
                        m_lastError = QStringLiteral("cannot inspect station_profile columns during migration");
                        return false;
                    }
                    if (columnQuery.value(0).toInt() == 0)
                        statements << QStringLiteral("ALTER TABLE station_profile ADD COLUMN %1 TEXT").arg(column);
                }
            }
        }
        if (step.to == 11) {
            // Older databases may already contain one or more of the new
            // columns when a migration was interrupted; add only missing
            // columns, while keeping the whole step transactional.
            QStringList filtered;
            for (const QString& statement : statements) {
                if (!statement.startsWith(QStringLiteral("ALTER TABLE station_profile ADD COLUMN"))) {
                    filtered << statement;
                    continue;
                }
                const QStringList tokens = statement.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                const QString column = tokens.value(5);
                if (column.isEmpty()) {
                    db.rollback();
                    m_lastError = QStringLiteral("cannot parse normalized profile column during migration");
                    return false;
                }
                QSqlQuery columnQuery(db);
                columnQuery.prepare(QStringLiteral("SELECT COUNT(*) FROM pragma_table_info('station_profile') WHERE name = ?"));
                columnQuery.addBindValue(column);
                if (!columnQuery.exec() || !columnQuery.next()) {
                    db.rollback();
                    m_lastError = QStringLiteral("cannot inspect normalized profile columns during migration");
                    return false;
                }
                if (columnQuery.value(0).toInt() == 0)
                    filtered << statement;
            }
            statements = filtered;
        }
        if (step.needsQsoColumn) {
            q.prepare(QStringLiteral("SELECT COUNT(*) FROM pragma_table_info('qso') WHERE name = ?"));
            q.addBindValue(QLatin1String(step.needsQsoColumn));
            if (!q.exec() || !q.next() || q.value(0).toInt() == 0)
                statements.clear();
        }
        for (const QString& stmt : statements + QStringList{
                 QStringLiteral("INSERT INTO schema_version (version) VALUES (%1)").arg(step.to)}) {
            if (!q.exec(stmt)) {
                m_lastError = q.lastError().text() + QStringLiteral(" in migration to v%1").arg(step.to);
                db.rollback();
                return false;
            }
        }
        if (!db.commit()) {
            m_lastError = db.lastError().text();
            return false;
        }
    }
    return true;
}

std::optional<ApiIdempotencyRecord> LogDatabase::apiIdempotency(const QString& key) const
{
    if (key.isEmpty() || !isOpen())
        return std::nullopt;
    QSqlQuery cleanup(connection());
    cleanup.prepare(QStringLiteral("DELETE FROM api_idempotency WHERE created_at < ?"));
    cleanup.addBindValue(QDateTime::currentDateTimeUtc().addDays(-30).toString(Qt::ISODate));
    cleanup.exec();

    QSqlQuery q(connection());
    q.prepare(QStringLiteral("SELECT request_body, status, content_type, response_body, created_at "
                             "FROM api_idempotency WHERE key = ?"));
    q.addBindValue(key);
    if (!q.exec() || !q.next())
        return std::nullopt;
    return ApiIdempotencyRecord{q.value(0).toByteArray(), q.value(1).toInt(), q.value(2).toByteArray(),
                                q.value(3).toByteArray(), q.value(4).toString()};
}

bool LogDatabase::saveApiIdempotency(const QString& key, const ApiIdempotencyRecord& record)
{
    if (key.isEmpty() || !isOpen())
        return false;
    const QString createdAt = record.createdAt.isEmpty()
                                  ? QDateTime::currentDateTimeUtc().toString(Qt::ISODate)
                                  : record.createdAt;
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("INSERT INTO api_idempotency "
                             "(key, request_body, status, content_type, response_body, created_at) "
                             "VALUES (?, ?, ?, ?, ?, ?) "
                             "ON CONFLICT(key) DO NOTHING"));
    q.addBindValue(key);
    q.addBindValue(record.requestBody);
    q.addBindValue(record.status);
    q.addBindValue(record.contentType);
    q.addBindValue(record.responseBody);
    q.addBindValue(createdAt);
    if (!q.exec())
        return false;
    if (q.numRowsAffected() > 0)
        return true;

    // Un'altra richiesta ha vinto la gara sulla stessa chiave. Non sostituire
    // mai il suo esito: una chiave riutilizzata con un corpo diverso deve
    // restare un conflitto anche in presenza di richieste concorrenti.
    const auto existing = apiIdempotency(key);
    return existing && existing->requestBody == record.requestBody
        && existing->status == record.status
        && existing->contentType == record.contentType
        && existing->responseBody == record.responseBody;
}

QString LogDatabase::isoFromAdif(const QString& date, const QString& time)
{
    const QDate d = QDate::fromString(date.trimmed(), QStringLiteral("yyyyMMdd"));
    // L'ADIF vuole date dal 1930 in poi. Il 30/12/1899 in particolare e' lo
    // "zero" delle date di Excel e di certi log: un programma che lo scrive ha
    // perso la data vera, e un QSO con quella data non e' un QSO del 1899.
    static const QDate first(1930, 1, 1);
    if (!d.isValid() || d < first)
        return {};
    const QString t = time.trimmed();
    QTime tm;
    if (t.size() == 6)
        tm = QTime::fromString(t, QStringLiteral("HHmmss"));
    else if (t.size() == 4)
        tm = QTime::fromString(t, QStringLiteral("HHmm"));
    if (!tm.isValid())
        return {};
    return QDateTime(d, tm, QTimeZone::UTC).toString(Qt::ISODate);
}

std::optional<qint64> LogDatabase::findDuplicate(const QString& call, const QString& band,
                                                 const QString& mode, const QString& submode,
                                                 const QDateTime& on, int windowSeconds) const
{
    // Il formato ISO a lunghezza fissa si confronta come stringa: l'indice
    // idx_qso_dedup lavora senza conversioni.
    QSqlQuery q(connection());
    q.prepare(QStringLiteral(
        "SELECT id FROM qso WHERE deleted = 0 AND call = ? AND band = ? AND mode = ? "
        "AND IFNULL(submode, '') = ? AND qso_datetime_on BETWEEN ? AND ? LIMIT 1"));
    q.addBindValue(call);
    q.addBindValue(band);
    q.addBindValue(mode);
    // Un sottomodo che manca (FT8, CW, SSB) e' '' e non NULL: con NULL il
    // confronto non e' mai vero, e un QSO senza sottomodo non trovava mai il
    // suo doppione — ne' da WSJT-X che rimanda, ne' importando due volte.
    q.addBindValue(submode.isEmpty() ? QStringLiteral("") : submode);
    q.addBindValue(on.addSecs(-windowSeconds).toString(Qt::ISODate));
    q.addBindValue(on.addSecs(windowSeconds).toString(Qt::ISODate));
    if (q.exec() && q.next())
        return q.value(0).toLongLong();
    return std::nullopt;
}

std::optional<qint64> LogDatabase::findByExtra(const QString& field, const QString& value, const QDateTime& near) const
{
    if (field.isEmpty() || value.isEmpty())
        return std::nullopt;
    QString sql = QStringLiteral("SELECT id, adif_extra FROM qso WHERE deleted = 0 AND adif_extra LIKE ?");
    if (near.isValid())
        sql += QStringLiteral(" AND qso_datetime_on BETWEEN ? AND ?");
    QSqlQuery q(connection());
    q.prepare(sql);
    q.addBindValue(QStringLiteral("%") + value + QStringLiteral("%"));
    if (near.isValid()) {
        q.addBindValue(near.toUTC().addDays(-1).toString(Qt::ISODate));
        q.addBindValue(near.toUTC().addDays(1).toString(Qt::ISODate));
    }
    if (!q.exec())
        return std::nullopt;
    while (q.next()) {
        const QJsonObject extra = QJsonDocument::fromJson(q.value(1).toString().toUtf8()).object();
        if (extra.value(field).toString() == value)
            return q.value(0).toLongLong();
    }
    return std::nullopt;
}

QList<qint64> LogDatabase::latestIds(const QString& call, int limit) const
{
    QList<qint64> out;
    const QString c = call.trimmed().toUpper();
    QSqlQuery q(connection());
    q.prepare(c.isEmpty()
                  ? QStringLiteral("SELECT id FROM qso WHERE deleted = 0 ORDER BY qso_datetime_on DESC LIMIT ?")
                  : QStringLiteral("SELECT id FROM qso WHERE deleted = 0 AND call = ? "
                                   "ORDER BY qso_datetime_on DESC LIMIT ?"));
    if (!c.isEmpty())
        q.addBindValue(c);
    q.addBindValue(qBound(1, limit, 500));
    if (q.exec()) {
        while (q.next())
            out << q.value(0).toLongLong();
    }
    return out;
}

bool LogDatabase::knowsQso(const AdifRecord& input) const
{
    AdifRecord record = input;
    if (record.value(QStringLiteral("BAND")).isEmpty()) {
        bool ok = false;
        const double mhz = record.value(QStringLiteral("FREQ")).toDouble(&ok);
        if (ok)
            record.set(QStringLiteral("BAND"), bands::fromMhz(mhz));
    }
    const QString call = record.value(QStringLiteral("CALL")).trimmed().toUpper();
    const QString band = record.value(QStringLiteral("BAND")).trimmed().toLower();
    const QString iso = isoFromAdif(record.value(QStringLiteral("QSO_DATE")), record.value(QStringLiteral("TIME_ON")));
    if (call.isEmpty() || band.isEmpty() || iso.isEmpty())
        return false;
    const QDateTime on = parseIso(iso);

    // Anche i cancellati: per questo non c'e' "deleted = 0".
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("SELECT id, call FROM qso WHERE qso_datetime_on BETWEEN ? AND ? AND band = ?"));
    q.addBindValue(on.addSecs(-m_dedupDigital).toString(Qt::ISODate));
    q.addBindValue(on.addSecs(m_dedupDigital).toString(Qt::ISODate));
    q.addBindValue(band);
    if (!q.exec())
        return false;
    QList<qint64> others;
    while (q.next()) {
        if (q.value(1).toString().trimmed().toUpper() == call)
            return true;
        others << q.value(0).toLongLong();
    }
    // Un nominativo corretto dopo: quello di prima sta nello storico.
    for (const qint64 id : std::as_const(others)) {
        for (const HistoryEntry& h : history(id)) {
            if (h.record.value(QStringLiteral("CALL")).trimmed().toUpper() == call)
                return true;
        }
    }
    return false;
}

std::optional<LogDatabase::Prepared> LogDatabase::prepare(const AdifRecord& input, InsertResult& error) const
{
    AdifRecord record = input;
    adif::normalizeMode(record);

    // La banda si ricava dalla frequenza quando manca: molti log la omettono.
    if (record.value(QStringLiteral("BAND")).isEmpty()) {
        bool ok = false;
        const double mhz = record.value(QStringLiteral("FREQ")).toDouble(&ok);
        if (ok)
            record.set(QStringLiteral("BAND"), bands::fromMhz(mhz));
    }

    if (record.contains(QStringLiteral("APP_DECOLOG_TAGS")))
        record.set(QStringLiteral("APP_DECOLOG_TAGS"), joinTags(splitTags(record.value(QStringLiteral("APP_DECOLOG_TAGS")))));

    Prepared p;
    p.call = record.value(QStringLiteral("CALL")).trimmed().toUpper();
    p.band = record.value(QStringLiteral("BAND")).trimmed().toLower();
    p.mode = record.value(QStringLiteral("MODE")).trimmed().toUpper();
    p.submode = record.value(QStringLiteral("SUBMODE")).trimmed().toUpper();
    p.on = isoFromAdif(record.value(QStringLiteral("QSO_DATE")), record.value(QStringLiteral("TIME_ON")));
    p.off = isoFromAdif(record.value(QStringLiteral("QSO_DATE_OFF")), record.value(QStringLiteral("TIME_OFF")));

    QStringList missing;
    if (p.call.isEmpty()) missing << QStringLiteral("CALL");
    if (p.on.isEmpty()) {
        const QString date = record.value(QStringLiteral("QSO_DATE")).trimmed();
        missing << (date.isEmpty() ? QStringLiteral("QSO_DATE/TIME_ON")
                                   : QStringLiteral("a valid QSO_DATE/TIME_ON (%1 %2)")
                                         .arg(date, record.value(QStringLiteral("TIME_ON")).trimmed()));
    }
    if (p.band.isEmpty()) missing << QStringLiteral("BAND/FREQ");
    if (p.mode.isEmpty()) missing << QStringLiteral("MODE");
    if (!missing.isEmpty()) {
        error.status = InsertResult::Status::Invalid;
        error.message = QStringLiteral("%1: missing %2")
                            .arg(p.call.isEmpty() ? QStringLiteral("?") : p.call,
                                 missing.join(QStringLiteral(", ")));
        return std::nullopt;
    }

    // Stati QSL: in tabella solo quelli diversi da "N", che e' il valore
    // predefinito. Un "N" esplicito resta in adif_extra, cosi' l'export
    // restituisce esattamente quello che e' entrato.
    QSet<QString> consumed;
    for (const auto& f : kQslFields) {
        QslState st;
        st.service = QLatin1String(f.service);
        bool any = false;
        const QString sent = record.value(QLatin1String(f.sent)).trimmed().toUpper();
        if (isStatusFlag(sent) && sent != QLatin1String("N")) {
            st.sent = sent;
            consumed << QLatin1String(f.sent);
            const QString date = record.value(QLatin1String(f.sentDate));
            if (!date.isEmpty()) {
                st.sentDate = date;
                consumed << QLatin1String(f.sentDate);
            }
            any = true;
        }
        if (*f.rcvd) {
            const QString rcvd = record.value(QLatin1String(f.rcvd)).trimmed().toUpper();
            if (isStatusFlag(rcvd) && rcvd != QLatin1String("N")) {
                st.rcvd = rcvd;
                consumed << QLatin1String(f.rcvd);
                const QString date = record.value(QLatin1String(f.rcvdDate));
                if (!date.isEmpty()) {
                    st.rcvdDate = date;
                    consumed << QLatin1String(f.rcvdDate);
                }
                any = true;
            }
        }
        if (st.service == QLatin1String("card")) {
            const QString via = record.value(QStringLiteral("QSL_SENT_VIA")).trimmed().toUpper();
            if (!via.isEmpty()) {
                st.via = via;
                consumed << QStringLiteral("QSL_SENT_VIA");
                any = true;
            }
        }
        if (any)
            p.qsl << st;
    }

    QJsonObject extra;
    QSet<QString> used;
    for (const auto& f : record.fields()) {
        // Date e ore valide stanno nelle colonne; se non si leggono restano come
        // sono, cosi' l'export non le perde.
        if (f.name == QLatin1String("QSO_DATE") || f.name == QLatin1String("TIME_ON"))
            continue;
        if ((f.name == QLatin1String("QSO_DATE_OFF") || f.name == QLatin1String("TIME_OFF")) && !p.off.isEmpty())
            continue;
        if (consumed.contains(f.name))
            continue;
        const Column* c = columnFor(f.name);
        const auto value = c ? toColumnValue(*c, f.value) : std::nullopt;
        if (c && value && !used.contains(f.name)) {
            used << f.name;
            p.columns.append({QLatin1String(c->column), *value});
        } else {
            extra.insert(f.name, f.value);
        }
    }
    if (!extra.isEmpty())
        p.extraJson = QString::fromUtf8(QJsonDocument(extra).toJson(QJsonDocument::Compact));
    return p;
}

bool LogDatabase::writeQsl(qint64 qsoId, const QList<QslState>& states, bool keepRemote)
{
    QSqlDatabase db = connection();
    QHash<QString, QPair<QString, QString>> remote;
    if (keepRemote) {
        QSqlQuery r(db);
        r.prepare(QStringLiteral("SELECT service, remote_id, last_error FROM qsl_status WHERE qso_id = ?"));
        r.addBindValue(qsoId);
        if (r.exec()) {
            while (r.next())
                remote.insert(r.value(0).toString(), {r.value(1).toString(), r.value(2).toString()});
        }
    }

    QSqlQuery del(db);
    del.prepare(QStringLiteral("DELETE FROM qsl_status WHERE qso_id = ?"));
    del.addBindValue(qsoId);
    if (!del.exec())
        return false;

    QSet<QString> written;
    QSqlQuery ins(db);
    ins.prepare(QStringLiteral(
        "INSERT INTO qsl_status (qso_id, service, sent, sent_date, rcvd, rcvd_date, remote_id, last_error, via) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    auto nullable = [](const QString& s) { return s.isEmpty() ? QVariant() : QVariant(s); };
    for (const QslState& st : states) {
        const auto kept = remote.value(st.service);
        ins.addBindValue(qsoId);
        ins.addBindValue(st.service);
        ins.addBindValue(st.sent);
        ins.addBindValue(nullable(st.sentDate));
        ins.addBindValue(st.rcvd);
        ins.addBindValue(nullable(st.rcvdDate));
        ins.addBindValue(nullable(st.remoteId.isEmpty() ? kept.first : st.remoteId));
        ins.addBindValue(nullable(st.lastError.isEmpty() ? kept.second : st.lastError));
        ins.addBindValue(nullable(st.via));
        if (!ins.exec())
            return false;
        written << st.service;
    }
    // Un errore di upload o un id remoto non si perdono solo perche' lo stato e'
    // tornato "N".
    for (auto it = remote.cbegin(); it != remote.cend(); ++it) {
        if (written.contains(it.key()) || (it.value().first.isEmpty() && it.value().second.isEmpty()))
            continue;
        ins.addBindValue(qsoId);
        ins.addBindValue(it.key());
        ins.addBindValue(QStringLiteral("N"));
        ins.addBindValue(QVariant());
        ins.addBindValue(QStringLiteral("N"));
        ins.addBindValue(QVariant());
        ins.addBindValue(nullable(it.value().first));
        ins.addBindValue(nullable(it.value().second));
        ins.addBindValue(QVariant());
        if (!ins.exec())
            return false;
    }
    return true;
}

InsertResult LogDatabase::insertQso(const AdifRecord& input, const QString& source,
                                    const QString& sourceApp, bool manual, qint64 stationProfileId,
                                    bool checkDuplicate)
{
    InsertResult result;
    if (!isOpen()) {
        result.message = QStringLiteral("database not open");
        return result;
    }

    const auto p = prepare(input, result);
    if (!p)
        return result;

    // Un'attivazione puo' ripassare la stessa stazione: il chiamante decide se il doppione conta.
    if (const auto dup = checkDuplicate ? findDuplicate(p->call, p->band, p->mode, p->submode, parseIso(p->on),
                                                        dedupWindowSeconds(manual))
                                        : std::nullopt) {
        result.status = InsertResult::Status::Duplicate;
        result.id = *dup;
        result.message = QStringLiteral("%1 %2 %3: already in log")
                             .arg(p->call, p->band, displayMode(p->mode, p->submode));
        return result;
    }

    const QString now = nowIso();
    QStringList columns{QStringLiteral("uuid"), QStringLiteral("qso_datetime_on"),
                        QStringLiteral("qso_datetime_off"), QStringLiteral("source"),
                        QStringLiteral("source_app"), QStringLiteral("created_at"),
                        QStringLiteral("updated_at"), QStringLiteral("station_profile_id"),
                        QStringLiteral("adif_extra")};
    QVariantList values{QUuid::createUuid().toString(QUuid::WithoutBraces), p->on,
                        p->off.isEmpty() ? QVariant() : QVariant(p->off), source,
                        sourceApp.isEmpty() ? QVariant() : QVariant(sourceApp), now, now,
                        stationProfileId > 0 ? QVariant(stationProfileId) : QVariant(),
                        p->extraJson.isEmpty() ? QVariant() : QVariant(p->extraJson)};
    for (const auto& [column, value] : p->columns) {
        columns << column;
        values << value;
    }

    QSqlDatabase db = connection();
    const bool ownTransaction = db.transaction();
    QSqlQuery q(db);
    QStringList marks;
    marks.fill(QStringLiteral("?"), columns.size());
    q.prepare(QStringLiteral("INSERT INTO qso (%1) VALUES (%2)")
                  .arg(columns.join(QLatin1Char(',')), marks.join(QLatin1Char(','))));
    for (const auto& v : values)
        q.addBindValue(v);
    if (!q.exec()) {
        if (ownTransaction)
            db.rollback();
        result.status = InsertResult::Status::Error;
        result.message = q.lastError().text();
        m_lastError = result.message;
        return result;
    }
    result.id = q.lastInsertId().toLongLong();
    if (!p->qsl.isEmpty() && !writeQsl(result.id, p->qsl, false)) {
        if (ownTransaction)
            db.rollback();
        result.status = InsertResult::Status::Error;
        result.message = QStringLiteral("cannot write QSL status");
        return result;
    }
    if (ownTransaction)
        db.commit();
    result.status = InsertResult::Status::Inserted;
    return result;
}

QString LogDatabase::snapshotJson(qint64 id) const
{
    const auto r = record(id);
    const auto m = meta(id);
    if (!r || !m)
        return {};
    QJsonArray fields;
    for (const auto& f : r->fields())
        fields.append(QJsonArray{f.name, f.value});
    QJsonObject obj{
        {QStringLiteral("fields"), fields},
        {QStringLiteral("uuid"), m->uuid},
        {QStringLiteral("revision"), m->revision},
        {QStringLiteral("source"), m->source},
        {QStringLiteral("source_app"), m->sourceApp},
        {QStringLiteral("station_profile_id"), m->stationProfileId},
        {QStringLiteral("updated_at"), m->updatedAt},
    };
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

InsertResult LogDatabase::updateQso(qint64 id, const AdifRecord& input, qint64 stationProfileId,
                                    const QString& reason)
{
    InsertResult result;
    result.id = id;
    const auto m = meta(id);
    if (!m) {
        result.message = QStringLiteral("QSO %1 not found").arg(id);
        return result;
    }
    const auto p = prepare(input, result);
    if (!p)
        return result;

    // Dentro una transazione gia' aperta (un import, una correzione in blocco)
    // si lavora in quella; altrimenti se ne apre una propria.
    QSqlDatabase db = connection();
    const bool ownTransaction = db.transaction();

    QSqlQuery h(db);
    h.prepare(QStringLiteral(
        "INSERT INTO qso_history (qso_uuid, revision, snapshot, reason, recorded_at) VALUES (?, ?, ?, ?, ?)"));
    h.addBindValue(m->uuid);
    h.addBindValue(m->revision);
    h.addBindValue(snapshotJson(id));
    h.addBindValue(reason);
    h.addBindValue(nowIso());
    if (!h.exec()) {
        if (ownTransaction)
            db.rollback();
        result.message = h.lastError().text();
        return result;
    }

    // Tutte le colonne ADIF: quelle che il record non ha piu' tornano NULL.
    QStringList sets{QStringLiteral("qso_datetime_on = ?"), QStringLiteral("qso_datetime_off = ?"),
                     QStringLiteral("adif_extra = ?"), QStringLiteral("revision = revision + 1"),
                     QStringLiteral("updated_at = ?"), QStringLiteral("dirty = 1")};
    QVariantList values{p->on, p->off.isEmpty() ? QVariant() : QVariant(p->off),
                        p->extraJson.isEmpty() ? QVariant() : QVariant(p->extraJson), nowIso()};
    for (const auto& c : kColumns) {
        const QLatin1String name(c.column);
        sets << name + QStringLiteral(" = ?");
        QVariant v;
        for (const auto& [column, value] : p->columns) {
            if (column == name) {
                v = value;
                break;
            }
        }
        values << v;
    }
    if (stationProfileId >= 0) {
        sets << QStringLiteral("station_profile_id = ?");
        values << (stationProfileId > 0 ? QVariant(stationProfileId) : QVariant());
    }

    QSqlQuery u(db);
    u.prepare(QStringLiteral("UPDATE qso SET %1 WHERE id = ?").arg(sets.join(QStringLiteral(", "))));
    for (const auto& v : values)
        u.addBindValue(v);
    u.addBindValue(id);
    if (!u.exec() || !writeQsl(id, p->qsl, true)) {
        result.message = u.lastError().text();
        if (ownTransaction)
            db.rollback();
        return result;
    }
    if (ownTransaction && !db.commit()) {
        result.message = db.lastError().text();
        return result;
    }
    result.status = InsertResult::Status::Inserted;
    return result;
}

bool LogDatabase::softDeleteQso(qint64 id)
{
    const auto m = meta(id);
    if (!m || m->deleted)
        return false;
    // Dentro una transazione gia' aperta (l'unione dei doppioni) si lavora in
    // quella: chiuderla qui chiuderebbe anche la sua.
    QSqlDatabase db = connection();
    const bool ownTransaction = db.transaction();
    QSqlQuery h(db);
    h.prepare(QStringLiteral(
        "INSERT INTO qso_history (qso_uuid, revision, snapshot, reason, recorded_at) VALUES (?, ?, ?, 'delete', ?)"));
    h.addBindValue(m->uuid);
    h.addBindValue(m->revision);
    h.addBindValue(snapshotJson(id));
    h.addBindValue(nowIso());
    QSqlQuery u(db);
    u.prepare(QStringLiteral(
        "UPDATE qso SET deleted = 1, dirty = 1, revision = revision + 1, updated_at = ? WHERE id = ?"));
    u.addBindValue(nowIso());
    u.addBindValue(id);
    // Sui servizi che sanno cancellare, il QSO che hanno va tolto anche li':
    // resta in coda ("D") finche' non si riesce a dirglielo.
    QSqlQuery d(db);
    d.prepare(QStringLiteral("UPDATE qsl_status SET sent = 'D' WHERE qso_id = ? AND IFNULL(remote_id, '') <> '' "
                             "AND service IN ('%1')").arg(editableServices().join(QStringLiteral("','"))));
    d.addBindValue(id);
    if (!h.exec() || !u.exec() || !d.exec()) {
        if (ownTransaction)
            db.rollback();
        return false;
    }
    return !ownTransaction || db.commit();
}

bool LogDatabase::recordEnrichment(const QList<EnrichmentEntry>& entries)
{
    if (entries.isEmpty())
        return true;
    QSqlDatabase db = connection();
    const bool ownTransaction = db.transaction();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT INTO qso_enrichment "
                             "(qso_id, field, previous_value, value, source, kind, acquired_at, valid_from, valid_to) "
                             "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    for (const EnrichmentEntry& e : entries) {
        q.addBindValue(e.qsoId); q.addBindValue(e.field); q.addBindValue(e.previousValue);
        q.addBindValue(e.value); q.addBindValue(e.source); q.addBindValue(e.kind);
        q.addBindValue(e.acquiredAt); q.addBindValue(e.validFrom); q.addBindValue(e.validTo);
        if (!q.exec()) {
            m_lastError = q.lastError().text();
            if (ownTransaction) db.rollback();
            return false;
        }
        q.finish();
        q.prepare(QStringLiteral("INSERT INTO qso_enrichment "
                                 "(qso_id, field, previous_value, value, source, kind, acquired_at, valid_from, valid_to) "
                                 "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    }
    return !ownTransaction || db.commit();
}

QList<EnrichmentEntry> LogDatabase::enrichments(qint64 qsoId) const
{
    QList<EnrichmentEntry> out;
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("SELECT qso_id, field, previous_value, value, source, kind, acquired_at, valid_from, valid_to "
                             "FROM qso_enrichment WHERE qso_id = ? ORDER BY id"));
    q.addBindValue(qsoId);
    if (!q.exec())
        return out;
    while (q.next()) {
        EnrichmentEntry e;
        e.qsoId = q.value(0).toLongLong(); e.field = q.value(1).toString();
        e.previousValue = q.value(2).toString(); e.value = q.value(3).toString();
        e.source = q.value(4).toString(); e.kind = q.value(5).toString();
        e.acquiredAt = q.value(6).toString(); e.validFrom = q.value(7).toString();
        e.validTo = q.value(8).toString(); out << e;
    }
    return out;
}

bool LogDatabase::undoEnrichment(qint64 qsoId, const QString& field)
{
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("SELECT previous_value FROM qso_enrichment WHERE qso_id = ? AND field = ? "
                             "ORDER BY id DESC LIMIT 1"));
    q.addBindValue(qsoId); q.addBindValue(field);
    if (!q.exec() || !q.next())
        return false;
    const auto current = record(qsoId);
    if (!current)
        return false;
    AdifRecord restored = *current;
    restored.set(field, q.value(0).toString());
    if (updateQso(qsoId, restored, -1, QStringLiteral("enrichment-undo")).status != InsertResult::Status::Inserted)
        return false;
    QSqlQuery remove(connection());
    remove.prepare(QStringLiteral("DELETE FROM qso_enrichment WHERE qso_id = ? AND field = ?"));
    remove.addBindValue(qsoId); remove.addBindValue(field);
    return remove.exec();
}

// Il campo della data che va con uno stato QSL ("LOTW_QSL_RCVD" →
// "LOTW_QSLRDATE"); vuoto se il campo non e' uno stato QSL.
static QString qslDateFieldFor(const QString& status)
{
    for (const auto& f : kQslFields) {
        if (status == QLatin1String(f.sent))
            return QLatin1String(f.sentDate);
        if (*f.rcvd && status == QLatin1String(f.rcvd))
            return QLatin1String(f.rcvdDate);
    }
    return {};
}

static bool isQslDateField(const QString& name)
{
    for (const auto& f : kQslFields) {
        if (name == QLatin1String(f.sentDate) || (*f.rcvd && name == QLatin1String(f.rcvdDate)))
            return true;
    }
    return false;
}

BulkEditResult LogDatabase::bulkEdit(const QList<qint64>& ids, const QString& field, const QString& value,
                                     bool onlyEmpty, const std::function<void(int, int)>& progress)
{
    BulkEditResult out;
    const bool profile = field == QLatin1String("@profile");
    const QString name = field.trimmed().toUpper();
    const QString clean = value.trimmed();
    const QString dateField = qslDateFieldFor(name);
    const QString today = QDateTime::currentDateTimeUtc().date().toString(QStringLiteral("yyyyMMdd"));
    QSqlDatabase db = connection();
    bool open = db.transaction();
    int done = 0;
    for (const qint64 id : ids) {
        ++done;
        auto r = record(id);
        const auto m = meta(id);
        if (!r || !m || m->deleted) {
            ++out.failed;
            continue;
        }
        InsertResult res;
        if (profile) {
            const qint64 target = clean.toLongLong();
            if (m->stationProfileId == target || (onlyEmpty && m->stationProfileId > 0)) {
                ++out.unchanged;
                continue;
            }
            res = updateQso(id, *r, target, QStringLiteral("bulk"));
        } else {
            const QString current = r->value(name);
            // Uno stato QSL a N e' vuoto: non e' ne' mandato ne' ricevuto.
            const bool empty = current.isEmpty() || (!dateField.isEmpty() && current == QLatin1String("N"));
            if ((onlyEmpty && !empty) || current.compare(clean, Qt::CaseInsensitive) == 0) {
                ++out.unchanged;
                continue;
            }
            r->set(name, clean);
            if (!dateField.isEmpty() && clean.compare(QLatin1String("Y"), Qt::CaseInsensitive) == 0
                && r->value(dateField).isEmpty())
                r->set(dateField, today);
            res = updateQso(id, *r, -1, QStringLiteral("bulk"));
        }
        if (res.status == InsertResult::Status::Inserted) {
            ++out.changed;
            out.changedIds << id;
        } else {
            ++out.failed;
            if (out.errors.size() < 5)
                out.errors << QStringLiteral("%1: %2").arg(r->value(QStringLiteral("CALL")), res.message);
        }
        // Un blocco per volta: chi scrive un QSO dalla radio intanto passa.
        if (done % 500 == 0) {
            if (open)
                db.commit();
            if (progress)
                progress(done, int(ids.size()));
            open = db.transaction();
        }
    }
    if (open)
        db.commit();
    if (progress)
        progress(int(ids.size()), int(ids.size()));
    return out;
}

// I secondi dal 1970 di un'ora ISO ("2026-09-28T12:03:00Z") senza passare da
// QDateTime: su un milione di righe e' la differenza fra un secondo e cinque.
// I giorni dal calendario come in days_from_civil di Howard Hinnant.
static std::optional<qint64> isoSeconds(const QString& s)
{
    if (s.size() < 19)
        return std::nullopt;
    auto num = [&s](int from, int len) {
        int v = 0;
        for (int i = from; i < from + len; ++i) {
            const QChar c = s.at(i);
            if (c < QLatin1Char('0') || c > QLatin1Char('9'))
                return -1;
            v = v * 10 + (c.unicode() - '0');
        }
        return v;
    };
    const int y = num(0, 4), mo = num(5, 2), d = num(8, 2), h = num(11, 2), mi = num(14, 2), se = num(17, 2);
    if (y < 0 || mo < 1 || mo > 12 || d < 1 || h < 0 || mi < 0 || se < 0)
        return std::nullopt;
    const int yy = y - (mo <= 2 ? 1 : 0);
    const int era = (yy >= 0 ? yy : yy - 399) / 400;
    const int yoe = yy - era * 400;
    const int doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const qint64 days = qint64(era) * 146097 + doe - 719468;
    return days * 86400 + h * 3600 + mi * 60 + se;
}

QList<QList<qint64>> LogDatabase::duplicateGroups(int windowSeconds, int limit) const
{
    QList<QList<qint64>> out;
    QSqlQuery q(connection());
    q.setForwardOnly(true);
    if (!q.exec(QStringLiteral("SELECT id, call, band, mode, submode, qso_datetime_on, IFNULL(station_profile_id, 0) "
                               "FROM qso NOT INDEXED WHERE deleted = 0 ORDER BY call, band, qso_datetime_on")))
        return out;
    // Per nominativo e banda, un gruppo che si forma per ogni gruppo di modo
    // e profilo: un FT8 e un CW nella stessa ora non sono doppioni.
    struct Forming {
        QList<qint64> ids;
        qint64 last{0};
    };
    QHash<QString, Forming> forming;
    QString current;
    auto flush = [&out](Forming& g) {
        if (g.ids.size() > 1)
            out << g.ids;
        g.ids.clear();
    };
    while (q.next() && (limit <= 0 || out.size() < limit)) {
        const QString call = q.value(1).toString();
        if (call.isEmpty())
            continue;
        const QString key = call + QLatin1Char('|') + q.value(2).toString();
        if (key != current) {
            for (Forming& g : forming)
                flush(g);
            forming.clear();
            current = key;
        }
        const auto when = isoSeconds(q.value(5).toString());
        if (!when)
            continue;
        Forming& g = forming[adif::modeGroup(q.value(3).toString(), q.value(4).toString()) + QLatin1Char('|')
                             + q.value(6).toString()];
        if (!g.ids.isEmpty() && *when - g.last > windowSeconds)
            flush(g);
        g.ids << q.value(0).toLongLong();
        g.last = *when;
    }
    for (Forming& g : forming)
        flush(g);
    if (limit > 0 && out.size() > limit)
        out.resize(limit);
    return out;
}

InsertResult LogDatabase::mergeQsos(qint64 keep, const QList<qint64>& others)
{
    InsertResult result;
    result.id = keep;
    auto base = record(keep);
    const auto baseMeta = meta(keep);
    if (!base || !baseMeta || baseMeta->deleted) {
        result.message = QStringLiteral("QSO %1 not found").arg(keep);
        return result;
    }
    qint64 profile = -1;
    QList<qint64> merged;
    for (const qint64 id : others) {
        if (id == keep)
            continue;
        const auto other = record(id);
        const auto otherMeta = meta(id);
        if (!other || !otherMeta || otherMeta->deleted)
            continue;
        merged << id;
        for (const AdifField& f : other->fields()) {
            const QString dateField = qslDateFieldFor(f.name);
            if (!dateField.isEmpty()) {
                // Una conferma vince sempre; altrimenti si riempie solo il vuoto.
                const QString mine = base->value(f.name);
                if ((f.value == QLatin1String("Y") && mine != QLatin1String("Y")) || mine.isEmpty()) {
                    base->set(f.name, f.value);
                    base->set(dateField, other->value(dateField));
                }
                continue;
            }
            if (isQslDateField(f.name))
                continue;
            if (f.name == QLatin1String("APP_DECOLOG_TAGS")) {
                QStringList tags = base->value(f.name).split(QLatin1Char(','), Qt::SkipEmptyParts);
                for (QString& t : tags)
                    t = t.trimmed();
                for (const QString& t : f.value.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                    if (!tags.contains(t.trimmed(), Qt::CaseInsensitive))
                        tags << t.trimmed();
                }
                base->set(f.name, tags.join(QLatin1Char(',')));
                continue;
            }
            if (base->value(f.name).isEmpty())
                base->set(f.name, f.value);
        }
        if (baseMeta->stationProfileId <= 0 && otherMeta->stationProfileId > 0 && profile < 0)
            profile = otherMeta->stationProfileId;
    }
    if (merged.isEmpty()) {
        result.message = QStringLiteral("nothing to merge");
        return result;
    }

    QSqlDatabase db = connection();
    const bool ownTransaction = db.transaction();
    result = updateQso(keep, *base, profile, QStringLiteral("merge"));
    if (result.status != InsertResult::Status::Inserted) {
        if (ownTransaction)
            db.rollback();
        return result;
    }
    for (const qint64 id : std::as_const(merged)) {
        // Il riferimento remoto (il QSO che un servizio ha gia') passa a chi
        // resta, se lui non ne ha: altrimenti il servizio cancellerebbe l'unica
        // copia che ha.
        QSqlQuery move(db);
        move.prepare(QStringLiteral(
            "INSERT INTO qsl_status (qso_id, service, sent, rcvd, remote_id) "
            "SELECT ?, service, sent, 'N', remote_id FROM qsl_status WHERE qso_id = ? AND IFNULL(remote_id, '') <> '' "
            "ON CONFLICT(qso_id, service) DO UPDATE SET remote_id = excluded.remote_id, sent = excluded.sent "
            "WHERE IFNULL(qsl_status.remote_id, '') = ''"));
        move.addBindValue(keep);
        move.addBindValue(id);
        QSqlQuery clear(db);
        clear.prepare(QStringLiteral(
            "UPDATE qsl_status SET remote_id = NULL WHERE qso_id = ? AND remote_id IS NOT NULL AND remote_id = "
            "(SELECT k.remote_id FROM qsl_status k WHERE k.qso_id = ? AND k.service = qsl_status.service)"));
        clear.addBindValue(id);
        clear.addBindValue(keep);
        if (!move.exec() || !clear.exec() || !softDeleteQso(id)) {
            result.status = InsertResult::Status::Error;
            result.message = move.lastError().isValid() ? move.lastError().text() : clear.lastError().text();
            if (ownTransaction)
                db.rollback();
            return result;
        }
    }
    if (ownTransaction && !db.commit()) {
        result.status = InsertResult::Status::Error;
        result.message = db.lastError().text();
    }
    return result;
}

namespace {

// Dove sta ogni colonna in un "SELECT * FROM qso": si cerca una volta per
// query, non una per QSO — con un milione di QSO da esportare, cercare i
// nomi delle colonne a ogni riga costava piu' di tutto il resto.
struct RowLayout {
    int call{-1};
    int on{-1};
    int off{-1};
    int extra{-1};
    QList<int> columns;   // una per kColumns

    static RowLayout of(const QSqlRecord& rec)
    {
        RowLayout l;
        l.call = rec.indexOf(QStringLiteral("call"));
        l.on = rec.indexOf(QStringLiteral("qso_datetime_on"));
        l.off = rec.indexOf(QStringLiteral("qso_datetime_off"));
        l.extra = rec.indexOf(QStringLiteral("adif_extra"));
        for (const auto& c : kColumns)
            l.columns << rec.indexOf(QLatin1String(c.column));
        return l;
    }
};

// Le colonne di qsl_status nell'ordine di qslStatus() e delle query a blocchi.
QslState qslFromQuery(const QSqlQuery& q, int offset)
{
    QslState st;
    st.service = q.value(offset).toString();
    st.sent = q.value(offset + 1).toString();
    st.sentDate = q.value(offset + 2).toString();
    st.rcvd = q.value(offset + 3).toString();
    st.rcvdDate = q.value(offset + 4).toString();
    st.remoteId = q.value(offset + 5).toString();
    st.lastError = q.value(offset + 6).toString();
    st.via = q.value(offset + 7).toString();
    return st;
}

const char kQslColumns[] = "service, sent, sent_date, rcvd, rcvd_date, remote_id, last_error, via";

// Il QSO come record ADIF, dalla riga di un "SELECT * FROM qso" e dai suoi stati QSL.
AdifRecord adifFromRow(const QSqlQuery& q, const RowLayout& l, const QList<QslState>& qsl)
{
    AdifRecord r;
    r.set(QStringLiteral("CALL"), q.value(l.call).toString());
    addDateTime(r, q.value(l.on).toString(), "QSO_DATE", "TIME_ON");
    addDateTime(r, q.value(l.off).toString(), "QSO_DATE_OFF", "TIME_OFF");
    for (qsizetype i = 0; i < l.columns.size(); ++i) {
        const auto& c = kColumns[i];
        r.set(QLatin1String(c.adif), fromColumnValue(c, l.columns.at(i) >= 0 ? q.value(l.columns.at(i)) : QVariant()));
    }

    for (const QslState& st : qsl) {
        // Le cartacee portano anche la via: bureau, diretta o elettronica.
        if (st.service == QLatin1String("card") && !st.via.isEmpty())
            r.set(QStringLiteral("QSL_SENT_VIA"), st.via);
        for (const auto& f : kQslFields) {
            if (st.service != QLatin1String(f.service))
                continue;
            if (st.sent != QLatin1String("N"))
                r.set(QLatin1String(f.sent), st.sent);
            r.set(QLatin1String(f.sentDate), st.sentDate);
            if (*f.rcvd) {
                if (st.rcvd != QLatin1String("N"))
                    r.set(QLatin1String(f.rcvd), st.rcvd);
                r.set(QLatin1String(f.rcvdDate), st.rcvdDate);
            }
        }
    }

    const QString extra = l.extra >= 0 ? q.value(l.extra).toString() : QString();
    if (!extra.isEmpty()) {
        const QJsonObject obj = QJsonDocument::fromJson(extra.toUtf8()).object();
        for (auto it = obj.begin(); it != obj.end(); ++it)
            r.set(it.key(), it.value().toString());
    }
    return r;
}

} // namespace

std::optional<AdifRecord> LogDatabase::record(qint64 id) const
{
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("SELECT * FROM qso WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec() || !q.next())
        return std::nullopt;
    return adifFromRow(q, RowLayout::of(q.record()), qslStatus(id));
}

std::optional<QsoMeta> LogDatabase::meta(qint64 id) const
{
    QSqlQuery q(connection());
    q.prepare(QStringLiteral(
        "SELECT uuid, revision, source, source_app, created_at, updated_at, dirty, deleted, station_profile_id "
        "FROM qso WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec() || !q.next())
        return std::nullopt;
    QsoMeta m;
    m.uuid = q.value(0).toString();
    m.revision = q.value(1).toInt();
    m.source = q.value(2).toString();
    m.sourceApp = q.value(3).toString();
    m.createdAt = q.value(4).toString();
    m.updatedAt = q.value(5).toString();
    m.dirty = q.value(6).toBool();
    m.deleted = q.value(7).toBool();
    m.stationProfileId = q.value(8).toLongLong();
    return m;
}

QList<QslState> LogDatabase::qslStatus(qint64 id) const
{
    QList<QslState> out;
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("SELECT %1 FROM qsl_status WHERE qso_id = ?").arg(QLatin1String(kQslColumns)));
    q.addBindValue(id);
    if (!q.exec())
        return out;
    while (q.next())
        out << qslFromQuery(q, 0);
    return out;
}

QList<HistoryEntry> LogDatabase::history(qint64 id) const
{
    QList<HistoryEntry> out;
    const auto m = meta(id);
    if (!m)
        return out;
    QSqlQuery q(connection());
    q.prepare(QStringLiteral(
        "SELECT id, revision, reason, recorded_at, snapshot FROM qso_history WHERE qso_uuid = ? "
        "ORDER BY revision DESC, id DESC"));
    q.addBindValue(m->uuid);
    if (!q.exec())
        return out;
    while (q.next()) {
        HistoryEntry e;
        e.id = q.value(0).toLongLong();
        e.revision = q.value(1).toInt();
        e.reason = q.value(2).toString();
        e.recordedAt = parseIso(q.value(3).toString());
        e.record = recordFromSnapshot(q.value(4).toString());
        out << e;
    }
    return out;
}

InsertResult LogDatabase::restoreRevision(qint64 id, qint64 historyId)
{
    for (const HistoryEntry& e : history(id)) {
        if (e.id == historyId)
            return updateQso(id, e.record, -1, QStringLiteral("edit"));
    }
    InsertResult r;
    r.message = QStringLiteral("revision not found");
    return r;
}

ConfirmationResult LogDatabase::applyConfirmation(const QString& service, const AdifRecord& c, int windowSeconds,
                                                  const QList<qint64>& onlyProfiles,
                                                  const QList<qint64>& exceptProfiles)
{
    ConfirmationResult res;
    const QslFields* fields = nullptr;
    for (const auto& f : kQslFields) {
        if (service == QLatin1String(f.service) && *f.rcvd)
            fields = &f;
    }
    if (!fields) {
        res.status = ConfirmationResult::Status::Error;
        res.message = QStringLiteral("%1 has no confirmations").arg(service);
        return res;
    }

    const QString call = c.value(QStringLiteral("CALL")).trimmed().toUpper();
    QString band = c.value(QStringLiteral("BAND")).trimmed().toLower();
    if (band.isEmpty()) {
        bool ok = false;
        const double mhz = c.value(QStringLiteral("FREQ")).toDouble(&ok);
        if (ok)
            band = bands::fromMhz(mhz);
    }
    const QString iso = isoFromAdif(c.value(QStringLiteral("QSO_DATE")), c.value(QStringLiteral("TIME_ON")));
    const QString group = adif::modeGroup(c.value(QStringLiteral("MODE")), c.value(QStringLiteral("SUBMODE")));
    res.message = QStringLiteral("%1 %2 %3 %4").arg(call, band, c.value(QStringLiteral("MODE")), iso.left(16));
    if (call.isEmpty() || band.isEmpty() || iso.isEmpty()) {
        res.status = ConfirmationResult::Status::Invalid;
        return res;
    }

    const QDateTime on = parseIso(iso);
    // I profili sono numeri: si scrivono nella query cosi' come sono.
    auto idList = [](const QList<qint64>& ids) {
        QStringList out;
        for (const qint64 id : ids)
            out << QString::number(id);
        return out.join(QLatin1Char(','));
    };
    QString profiles;
    if (!onlyProfiles.isEmpty())
        profiles += QStringLiteral(" AND IFNULL(station_profile_id, 0) IN (%1)").arg(idList(onlyProfiles));
    if (!exceptProfiles.isEmpty())
        profiles += QStringLiteral(" AND IFNULL(station_profile_id, 0) NOT IN (%1)").arg(idList(exceptProfiles));
    QSqlQuery q(connection());
    q.prepare(QStringLiteral(
        "SELECT id, mode, IFNULL(submode, ''), qso_datetime_on, "
        "(SELECT rcvd FROM qsl_status s WHERE s.qso_id = qso.id AND s.service = ?) "
        "FROM qso WHERE deleted = 0 AND call = ? AND band = ? AND qso_datetime_on BETWEEN ? AND ?")
              + profiles);
    q.addBindValue(service);
    q.addBindValue(call);
    q.addBindValue(band);
    q.addBindValue(on.addSecs(-windowSeconds).toString(Qt::ISODate));
    q.addBindValue(on.addSecs(windowSeconds).toString(Qt::ISODate));
    if (!q.exec()) {
        res.status = ConfirmationResult::Status::Error;
        res.message = q.lastError().text();
        return res;
    }
    // Il QSO piu' vicino nel tempo fra quelli dello stesso gruppo di modi.
    qint64 bestDistance = std::numeric_limits<qint64>::max();
    QList<QPair<qint64, bool>> bestMatches;
    while (q.next()) {
        if (!group.isEmpty() && adif::modeGroup(q.value(1).toString(), q.value(2).toString()) != group)
            continue;
        const qint64 distance = qAbs(parseIso(q.value(3).toString()).secsTo(on));
        if (distance < bestDistance) {
            bestDistance = distance;
            bestMatches.clear();
        }
        if (distance == bestDistance)
            bestMatches.append({q.value(0).toLongLong(), q.value(4).toString() == QLatin1String("Y")});
    }
    if (bestMatches.isEmpty())
        return res;
    if (bestMatches.size() > 1) {
        for (const auto& match : bestMatches)
            res.candidates << match.first;
        res.status = ConfirmationResult::Status::Ambiguous;
        res.message = QStringLiteral("%1 candidate QSOs have the same matching distance").arg(bestMatches.size());
        return res;
    }
    const qint64 best = bestMatches.first().first;
    const bool bestConfirmed = bestMatches.first().second;
    res.id = best;
    if (bestConfirmed) {
        res.status = ConfirmationResult::Status::AlreadyConfirmed;
        return res;
    }

    auto r = record(best);
    if (!r) {
        res.status = ConfirmationResult::Status::Error;
        return res;
    }
    r->set(QLatin1String(fields->rcvd), QStringLiteral("Y"));
    QString date = c.value(QStringLiteral("QSLRDATE")).trimmed().remove(QLatin1Char('-')).left(8);
    if (!QDate::fromString(date, QStringLiteral("yyyyMMdd")).isValid())
        date = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd"));
    r->set(QLatin1String(fields->rcvdDate), date);
    // Confermato vuol dire che il QSO e' arrivato al servizio.
    const QString sent = r->value(QLatin1String(fields->sent)).toUpper();
    if (sent.isEmpty() || sent == QLatin1String("N") || sent == QLatin1String("R") || sent == QLatin1String("Q"))
        r->set(QLatin1String(fields->sent), QStringLiteral("Y"));

    // I dettagli del corrispondente: solo dove il log non ha niente. Un locatore
    // a quattro caratteri si allunga se la conferma dice lo stesso quadrato.
    const QString grid = r->value(QStringLiteral("GRIDSQUARE")).trimmed().toUpper();
    const QString confirmedGrid = c.value(QStringLiteral("GRIDSQUARE")).trimmed().toUpper();
    if (confirmedGrid.size() >= 4 && (grid.isEmpty() || (confirmedGrid.size() > grid.size() && confirmedGrid.startsWith(grid))))
        r->set(QStringLiteral("GRIDSQUARE"), confirmedGrid);
    for (const char* name : {"DXCC", "COUNTRY", "CQZ", "ITUZ", "STATE", "CNTY", "IOTA"}) {
        const QString field = QLatin1String(name);
        const QString value = c.value(field).trimmed();
        if (r->value(field).trimmed().isEmpty() && !value.isEmpty())
            r->set(field, value);
    }

    const InsertResult u = updateQso(best, *r, -1, service);
    if (u.status != InsertResult::Status::Inserted) {
        res.status = ConfirmationResult::Status::Error;
        res.message = u.message;
        return res;
    }
    res.status = ConfirmationResult::Status::Confirmed;
    return res;
}

ImportResult LogDatabase::importAdif(const QByteArray& data, const QString& source, qint64 stationProfileId)
{
    // Una transazione sola: diecimila QSO in secondi invece che in minuti.
    return importAdif(data, source, stationProfileId, {}, std::numeric_limits<int>::max());
}

ImportResult LogDatabase::importAdif(const QByteArray& data, const QString& source, qint64 stationProfileId,
                                     const ImportProgress& progress, int batchSize)
{
    ImportResult result;
    const AdifDocument doc = adif::parse(data);
    const QString programId = doc.header.value(QStringLiteral("PROGRAMID"));
    const int total = static_cast<int>(doc.records.size());
    const int batch = qMax(1, batchSize);

    QSqlDatabase db = connection();
    int done = 0;
    while (done < total) {
        const int end = total - done > batch ? done + batch : total;
        const bool ownTransaction = db.transaction();
        for (; done < end; ++done) {
            AdifRecord record = doc.records.at(done);
            ++result.read;
            if (!record.contains(QStringLiteral("QSO_DATE")) || !record.contains(QStringLiteral("TIME_ON")))
                ++result.missingUtc;
            if (record.contains(QStringLiteral("LOTW_QSL_RCVD")) || record.contains(QStringLiteral("EQSL_QSL_RCVD"))
                || record.contains(QStringLiteral("QSL_RCVD")) || record.contains(QStringLiteral("CARD_QSL_RCVD")))
                ++result.qslRecords;
            static const QSet<QString> knownFields{
                QStringLiteral("CALL"), QStringLiteral("QSO_DATE"), QStringLiteral("TIME_ON"),
                QStringLiteral("QSO_DATE_OFF"), QStringLiteral("TIME_OFF"), QStringLiteral("BAND"),
                QStringLiteral("FREQ"), QStringLiteral("MODE"), QStringLiteral("SUBMODE"),
                QStringLiteral("RST_SENT"), QStringLiteral("RST_RCVD"), QStringLiteral("NAME"),
                QStringLiteral("QTH"), QStringLiteral("GRIDSQUARE"), QStringLiteral("DXCC"),
                QStringLiteral("CQZ"), QStringLiteral("ITUZ"), QStringLiteral("STATE"),
                QStringLiteral("CNTY"), QStringLiteral("CONT"), QStringLiteral("IOTA"),
                QStringLiteral("POTA_REF"), QStringLiteral("SOTA_REF"), QStringLiteral("WWFF_REF"),
                QStringLiteral("SIG"), QStringLiteral("SIG_INFO"), QStringLiteral("COMMENT"),
                QStringLiteral("NOTES"), QStringLiteral("PROP_MODE"), QStringLiteral("SAT_NAME"),
                QStringLiteral("STATION_CALLSIGN"), QStringLiteral("OPERATOR"),
                QStringLiteral("QSL_RCVD"), QStringLiteral("QSL_RDATE"), QStringLiteral("QSL_VIA"),
                QStringLiteral("LOTW_QSL_SENT"), QStringLiteral("LOTW_QSLSDATE"),
                QStringLiteral("LOTW_QSL_RCVD"), QStringLiteral("LOTW_QSLRDATE"),
                QStringLiteral("EQSL_QSL_SENT"), QStringLiteral("EQSL_QSLSDATE"),
                QStringLiteral("EQSL_QSL_RCVD"), QStringLiteral("EQSL_QSLRDATE"),
                QStringLiteral("CLUBLOG_QSO_SENT"), QStringLiteral("CLUBLOG_QSO_RCVD"),
                QStringLiteral("QRZCOM_QSO_SENT"), QStringLiteral("QRZCOM_QSO_RCVD")};
            for (const AdifField& field : record.fields())
                if (!knownFields.contains(field.name) && !result.preservedExtraFields.contains(field.name)
                    && !field.name.startsWith(QStringLiteral("APP_")))
                    result.preservedExtraFields << field.name;
            // Le conferme scritte nei campi di un altro programma (QRZ) diventano
            // quelle ADIF.
            migration::mapProgramFields(record);
            const InsertResult r = insertQso(record, source, programId, false, stationProfileId);
            switch (r.status) {
            case InsertResult::Status::Inserted:
                ++result.inserted;
                result.insertedIds << r.id;
                break;
            case InsertResult::Status::Duplicate: ++result.duplicates; break;
            case InsertResult::Status::Invalid:
            case InsertResult::Status::Error:
                ++result.invalid;
                if (result.errors.size() < 20)
                    result.errors << r.message;
                break;
            }
        }
        if (ownTransaction)
            db.commit();
        if (progress)
            progress(done, total);
    }
    return result;
}

namespace {

AdifDocument exportHeader(const QString& programVersion)
{
    AdifDocument doc;
    doc.header.set(QStringLiteral("ADIF_VER"), QStringLiteral("3.1.5"));
    doc.header.set(QStringLiteral("PROGRAMID"), QStringLiteral("DecoDXLog"));
    doc.header.set(QStringLiteral("PROGRAMVERSION"), programVersion);
    doc.header.set(QStringLiteral("CREATED_TIMESTAMP"),
                   QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd HHmmss")));
    return doc;
}

} // namespace

QByteArray LogDatabase::exportAdif(const QString& programVersion) const
{
    // Tutto il log in due passate, non due query per QSO: gli stati QSL in
    // memoria, poi i QSO in ordine di tempo.
    AdifDocument doc = exportHeader(programVersion);
    QHash<qint64, QList<QslState>> qsl;
    QSqlQuery s(connection());
    s.setForwardOnly(true);
    if (s.exec(QStringLiteral("SELECT qso_id, %1 FROM qsl_status").arg(QLatin1String(kQslColumns)))) {
        while (s.next())
            qsl[s.value(0).toLongLong()] << qslFromQuery(s, 1);
    }
    QSqlQuery q(connection());
    q.setForwardOnly(true);
    if (q.exec(QStringLiteral("SELECT * FROM qso NOT INDEXED WHERE deleted = 0 ORDER BY qso_datetime_on, id"))) {
        const RowLayout layout = RowLayout::of(q.record());
        const int idColumn = q.record().indexOf(QStringLiteral("id"));
        while (q.next())
            doc.records.append(adifFromRow(q, layout, qsl.value(q.value(idColumn).toLongLong())));
    }
    return adif::writeDocument(doc);
}

QByteArray LogDatabase::exportAdif(const QList<qint64>& ids, const QString& programVersion) const
{
    // A blocchi di cinquecento: una selezione di migliaia di QSO non fa
    // migliaia di query, e l'ordine resta quello chiesto.
    AdifDocument doc = exportHeader(programVersion);
    QHash<qint64, AdifRecord> found;
    for (qsizetype from = 0; from < ids.size(); from += 500) {
        const QList<qint64> chunk = ids.mid(from, 500);
        QStringList marks;
        for (qsizetype i = 0; i < chunk.size(); ++i)
            marks << QStringLiteral("?");
        const QString in = marks.join(QLatin1Char(','));
        QHash<qint64, QList<QslState>> qsl;
        QSqlQuery s(connection());
        s.setForwardOnly(true);
        s.prepare(QStringLiteral("SELECT qso_id, %1 FROM qsl_status WHERE qso_id IN (%2)")
                      .arg(QLatin1String(kQslColumns), in));
        for (qint64 id : chunk)
            s.addBindValue(id);
        if (s.exec()) {
            while (s.next())
                qsl[s.value(0).toLongLong()] << qslFromQuery(s, 1);
        }
        QSqlQuery q(connection());
        q.setForwardOnly(true);
        q.prepare(QStringLiteral("SELECT * FROM qso WHERE id IN (%1)").arg(in));
        for (qint64 id : chunk)
            q.addBindValue(id);
        if (q.exec()) {
            const RowLayout layout = RowLayout::of(q.record());
            const int idColumn = q.record().indexOf(QStringLiteral("id"));
            while (q.next()) {
                const qint64 id = q.value(idColumn).toLongLong();
                found.insert(id, adifFromRow(q, layout, qsl.value(id)));
            }
        }
    }
    for (qint64 id : ids) {
        const auto it = found.constFind(id);
        if (it != found.constEnd())
            doc.records.append(*it);
    }
    return adif::writeDocument(doc);
}

QString LogDatabase::changeStamp() const
{
    // data_version sale quando scrive un'altra connessione, total_changes()
    // conta quello che ha scritto questa.
    QSqlQuery q(connection());
    QString stamp;
    if (q.exec(QStringLiteral("PRAGMA data_version")) && q.next())
        stamp = q.value(0).toString();
    if (q.exec(QStringLiteral("SELECT total_changes()")) && q.next())
        stamp += QLatin1Char('|') + q.value(0).toString();
    return stamp;
}

int LogDatabase::qsoCount() const
{
    decolog::StartupSpan trace("LogDatabase::qsoCount");
    QSqlQuery q(connection());
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM qso WHERE deleted = 0")) && q.next())
        return q.value(0).toInt();
    return 0;
}

namespace {

// «dal … al …» come condizione SQL. Le date nel database sono ISO-8601 a
// lunghezza fissa: il confronto fra stringhe basta, e il giorno di «al» si
// prende intero scegliendo come limite l'inizio del giorno dopo.
QString periodClause(const QString& fromIso, const QString& toIso, QVariantList* binds)
{
    QString sql;
    if (!fromIso.isEmpty()) {
        sql += QStringLiteral(" AND qso_datetime_on >= ?");
        *binds << fromIso;
    }
    if (!toIso.isEmpty()) {
        const QDate to = QDate::fromString(toIso, Qt::ISODate);
        sql += QStringLiteral(" AND qso_datetime_on < ?");
        *binds << (to.isValid() ? to.addDays(1).toString(Qt::ISODate) : toIso);
    }
    return sql;
}

} // namespace

QList<qint64> LogDatabase::qsoIdsBetween(const QString& fromIso, const QString& toIso) const
{
    QVariantList binds;
    const QString where = periodClause(fromIso, toIso, &binds);
    QSqlQuery q(connection());
    q.setForwardOnly(true);
    q.prepare(QStringLiteral("SELECT id FROM qso WHERE deleted = 0%1 ORDER BY qso_datetime_on, id").arg(where));
    for (const QVariant& b : binds)
        q.addBindValue(b);
    QList<qint64> ids;
    if (q.exec()) {
        while (q.next())
            ids << q.value(0).toLongLong();
    }
    return ids;
}

int LogDatabase::qsoCountBetween(const QString& fromIso, const QString& toIso) const
{
    QVariantList binds;
    const QString where = periodClause(fromIso, toIso, &binds);
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM qso WHERE deleted = 0%1").arg(where));
    for (const QVariant& b : binds)
        q.addBindValue(b);
    if (q.exec() && q.next())
        return q.value(0).toInt();
    return 0;
}

int LogDatabase::dirtyCount() const
{
    decolog::StartupSpan trace("LogDatabase::dirtyCount");
    QSqlQuery q(connection());
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM qso WHERE dirty = 1")) && q.next())
        return q.value(0).toInt();
    return 0;
}

int LogDatabase::conflictCount() const
{
    QSqlQuery q(connection());
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM qso_history WHERE reason = 'conflict_lost'")) && q.next())
        return q.value(0).toInt();
    return 0;
}

WorkedBefore LogDatabase::workedBefore(const QString& call) const
{
    decolog::StartupSpan trace("LogDatabase::workedBefore");
    WorkedBefore wb;
    const QString c = call.trimmed().toUpper();
    if (c.isEmpty())
        return wb;

    QSqlQuery q(connection());
    q.prepare(QStringLiteral(
        "SELECT band, mode, submode, qso_datetime_on, name, gridsquare, country, qth, dxcc, cqz, ituz, id, "
        "(SELECT rcvd FROM qsl_status s WHERE s.qso_id = qso.id AND s.service = 'lotw'), IFNULL(state, '') "
        "FROM qso WHERE deleted = 0 AND call = ? ORDER BY qso_datetime_on DESC"));
    q.addBindValue(c);
    if (!q.exec())
        return wb;

    const QStringList order = bands::all();
    while (q.next()) {
        const QString band = q.value(0).toString();
        const QString mode = displayMode(q.value(1).toString(), q.value(2).toString());
        const QDateTime on = parseIso(q.value(3).toString());
        if (wb.count == 0) {
            wb.last = on;
            wb.lastBand = band;
            wb.lastMode = mode;
            wb.lastId = q.value(11).toLongLong();
        }
        if (wb.name.isEmpty()) wb.name = q.value(4).toString();
        if (wb.gridsquare.isEmpty()) wb.gridsquare = q.value(5).toString();
        if (wb.country.isEmpty()) wb.country = q.value(6).toString();
        if (wb.qth.isEmpty()) wb.qth = q.value(7).toString();
        if (wb.dxcc == 0) wb.dxcc = q.value(8).toInt();
        if (wb.cqz == 0) wb.cqz = q.value(9).toInt();
        if (wb.ituz == 0) wb.ituz = q.value(10).toInt();
        if (wb.state.isEmpty()) wb.state = q.value(13).toString();
        if (!wb.bands.contains(band)) wb.bands << band;
        if (!wb.modes.contains(mode)) wb.modes << mode;
        if (wb.recent.size() < 5)
            wb.recent << WorkedEntry{on, band, mode, q.value(12).toString()};
        ++wb.count;
    }
    std::sort(wb.bands.begin(), wb.bands.end(), [&order](const QString& a, const QString& b) {
        return order.indexOf(a) < order.indexOf(b);
    });
    return wb;
}

LogDatabase::DxccWorked LogDatabase::dxccWorked(int dxcc) const
{
    DxccWorked w;
    if (dxcc <= 0)
        return w;
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("SELECT band, mode, submode FROM qso WHERE deleted = 0 AND dxcc = ?"));
    q.addBindValue(dxcc);
    if (!q.exec())
        return w;
    while (q.next()) {
        const QString band = q.value(0).toString();
        const QString mode = displayMode(q.value(1).toString(), q.value(2).toString());
        if (!w.bands.contains(band)) w.bands << band;
        if (!w.modes.contains(mode)) w.modes << mode;
        ++w.count;
    }
    return w;
}

namespace {

// Le caselle della griglia: una query sola, gia' raggruppata dal database, con
// le conferme accanto. Il gruppo del modo (CW, fonia, digitale) lo decide
// modes::groupFor, che e' lo stesso criterio dei diplomi.
QList<LogDatabase::BandModeSlot> slotsFrom(QSqlQuery& q)
{
    QHash<QString, LogDatabase::BandModeSlot> byKey;
    QStringList order;
    while (q.next()) {
        const QString band = q.value(0).toString();
        const QString group = modes::groupFor(displayMode(q.value(1).toString(), q.value(2).toString()));
        const QString key = band + QLatin1Char('|') + group;
        if (!byKey.contains(key)) {
            LogDatabase::BandModeSlot slot;
            slot.band = band;
            slot.group = group;
            byKey.insert(key, slot);
            order << key;
        }
        LogDatabase::BandModeSlot& slot = byKey[key];
        slot.count += q.value(3).toInt();
        slot.lotw    = slot.lotw    || q.value(4).toInt() > 0;
        slot.eqsl    = slot.eqsl    || q.value(5).toInt() > 0;
        slot.card    = slot.card    || q.value(6).toInt() > 0;
        slot.clublog = slot.clublog || q.value(7).toInt() > 0;
        slot.qrz     = slot.qrz     || q.value(8).toInt() > 0;
    }
    QList<LogDatabase::BandModeSlot> out;
    out.reserve(order.size());
    for (const QString& key : order)
        out << byKey.value(key);
    return out;
}

const char* kSlotColumns =
    "SELECT q.band, q.mode, q.submode, COUNT(DISTINCT q.id),"
    " MAX(CASE WHEN s.service = 'lotw'    AND s.rcvd = 'Y' THEN 1 ELSE 0 END),"
    " MAX(CASE WHEN s.service = 'eqsl'    AND s.rcvd = 'Y' THEN 1 ELSE 0 END),"
    " MAX(CASE WHEN s.service = 'card'    AND s.rcvd = 'Y' THEN 1 ELSE 0 END),"
    " MAX(CASE WHEN s.service = 'clublog' AND s.rcvd = 'Y' THEN 1 ELSE 0 END),"
    " MAX(CASE WHEN s.service = 'qrz'     AND s.rcvd = 'Y' THEN 1 ELSE 0 END)"
    " FROM qso q LEFT JOIN qsl_status s ON s.qso_id = q.id"
    " WHERE q.deleted = 0 AND ";

} // namespace

QList<LogDatabase::BandModeSlot> LogDatabase::bandModeSlotsForCall(const QString& call) const
{
    if (call.trimmed().isEmpty())
        return {};
    QSqlQuery q(connection());
    q.prepare(QLatin1String(kSlotColumns)
              + QStringLiteral("q.call = ? GROUP BY q.band, q.mode, q.submode"));
    q.addBindValue(call.trimmed().toUpper());
    if (!q.exec())
        return {};
    return slotsFrom(q);
}

QList<LogDatabase::BandModeSlot> LogDatabase::bandModeSlotsForDxcc(int dxcc) const
{
    if (dxcc <= 0)
        return {};
    QSqlQuery q(connection());
    q.prepare(QLatin1String(kSlotColumns)
              + QStringLiteral("q.dxcc = ? GROUP BY q.band, q.mode, q.submode"));
    q.addBindValue(dxcc);
    if (!q.exec())
        return {};
    return slotsFrom(q);
}

QJsonArray LogDatabase::workedRow(const QString& call, const QString& band, const QString& mode,
                                  const QString& submode, const QString& isoOn, const QString& grid, bool confirmed)
{
    return QJsonArray{call, band, displayMode(mode, submode),
                      isoOn.left(10).remove(QLatin1Char('-')), grid.left(4).toUpper(), confirmed ? 1 : 0};
}

// NOT INDEXED, nelle query che leggono tutto il log: SQLite prenderebbe un
// indice per avere le righe gia' in ordine, e poi salterebbe da una pagina
// all'altra del file per leggerle. Su un log da un milione una lettura in fila
// piu' un ordinamento e' tre-cinque volte piu' veloce.

QSet<qint64> LogDatabase::confirmedIds(bool lotw, bool card, bool eqsl) const
{
    QSet<qint64> out;
    QStringList services;
    if (lotw)
        services << QStringLiteral("'lotw'");
    if (card)
        services << QStringLiteral("'card'");
    if (eqsl)
        services << QStringLiteral("'eqsl'");
    if (services.isEmpty())
        return out;
    QSqlQuery q(connection());
    q.setForwardOnly(true);
    if (q.exec(QStringLiteral("SELECT DISTINCT qso_id FROM qsl_status WHERE rcvd = 'Y' AND service IN (%1)")
                   .arg(services.join(QLatin1Char(','))))) {
        while (q.next())
            out.insert(q.value(0).toLongLong());
    }
    return out;
}

QSet<qint64> LogDatabase::confirmedIds(const QString& service) const
{
    QSet<qint64> out;
    QSqlQuery q(connection());
    q.setForwardOnly(true);
    q.prepare(QStringLiteral("SELECT qso_id FROM qsl_status WHERE rcvd = 'Y' AND service = ?"));
    q.addBindValue(service);
    if (q.exec()) {
        while (q.next())
            out.insert(q.value(0).toLongLong());
    }
    return out;
}

QList<QJsonArray> LogDatabase::workedRows(bool confirmLotw, bool confirmCard, bool confirmEqsl) const
{
    decolog::StartupSpan trace("LogDatabase::workedRows");
    // Le conferme in una query sola: una sottoquery per ogni QSO, su un log da
    // un milione, erano venti secondi.
    QList<QJsonArray> rows;
    const QSet<qint64> confirmed = confirmedIds(confirmLotw, confirmCard, confirmEqsl);
    QSqlQuery q(connection());
    q.setForwardOnly(true);
    if (!q.exec(QStringLiteral("SELECT id, call, band, mode, IFNULL(submode, ''), qso_datetime_on, "
                               "IFNULL(gridsquare, '') FROM qso NOT INDEXED WHERE deleted = 0 ORDER BY qso_datetime_on")))
        return rows;
    while (q.next()) {
        rows << workedRow(q.value(1).toString(), q.value(2).toString(), q.value(3).toString(),
                          q.value(4).toString(), q.value(5).toString(), q.value(6).toString(),
                          confirmed.contains(q.value(0).toLongLong()));
    }
    return rows;
}

int LogDatabase::markAllDirty()
{
    QSqlQuery q(connection());
    if (!q.exec(QStringLiteral("UPDATE qso SET dirty = 1")))
        return 0;
    return q.numRowsAffected();
}

QList<qint64> LogDatabase::idsMissingCallbookData(int limit) const
{
    QList<qint64> ids;
    QSqlQuery q(connection());
    QString sql = QStringLiteral(
        "SELECT id FROM qso WHERE deleted = 0 AND IFNULL(gridsquare, '') = '' "
        "ORDER BY qso_datetime_on DESC");
    if (limit > 0)
        sql += QStringLiteral(" LIMIT %1").arg(limit);
    if (q.exec(sql)) {
        while (q.next())
            ids << q.value(0).toLongLong();
    }
    return ids;
}

QList<qint64> LogDatabase::idsWithDamagedText() const
{
    decolog::StartupSpan trace("LogDatabase::idsWithDamagedText");
    QList<qint64> ids;
    QSqlQuery q(connection());
    // Un '<' o il carattere di sostituzione dentro nome, QTH, indirizzo o note:
    // in un QSO scritto bene non ci stanno.
    if (q.exec(QStringLiteral(
            "SELECT id FROM qso WHERE deleted = 0 AND ("
            "  IFNULL(name, '') LIKE '%<%' OR IFNULL(qth, '') LIKE '%<%'"
            "  OR IFNULL(comment, '') LIKE '%<%' OR IFNULL(notes, '') LIKE '%<%'"
            "  OR IFNULL(country, '') LIKE '%<%'"
            "  OR IFNULL(name, '') LIKE '%' || CHAR(65533) || '%'"
            "  OR IFNULL(qth, '') LIKE '%' || CHAR(65533) || '%'"
            "  OR IFNULL(comment, '') LIKE '%' || CHAR(65533) || '%'"
            "  OR IFNULL(notes, '') LIKE '%' || CHAR(65533) || '%')"
            " ORDER BY id"))) {
        while (q.next())
            ids << q.value(0).toLongLong();
    }
    return ids;
}

QList<qint64> LogDatabase::idsWithoutDxcc() const
{
    decolog::StartupSpan trace("LogDatabase::idsWithoutDxcc");
    QList<qint64> ids;
    QSqlQuery q(connection());
    if (q.exec(QStringLiteral("SELECT id FROM qso WHERE deleted = 0 AND (dxcc IS NULL OR dxcc = 0)"))) {
        while (q.next())
            ids << q.value(0).toLongLong();
    }
    return ids;
}

bool LogDatabase::isFirstFt2Dxcc(qint64 id) const
{
    QSqlQuery q(connection());
    q.prepare(QStringLiteral(
        "SELECT q.dxcc, (SELECT COUNT(*) FROM qso o WHERE o.deleted = 0 AND o.submode = 'FT2' "
        "AND o.dxcc = q.dxcc AND o.id <> q.id AND (o.qso_datetime_on < q.qso_datetime_on "
        "OR (o.qso_datetime_on = q.qso_datetime_on AND o.id < q.id))) "
        "FROM qso q WHERE q.id = ? AND q.submode = 'FT2'"));
    q.addBindValue(id);
    if (!q.exec() || !q.next() || q.value(0).isNull())
        return false;
    return q.value(1).toInt() == 0;
}

Ft2Award LogDatabase::ft2Award() const
{
    decolog::StartupSpan trace("LogDatabase::ft2Award");
    Ft2Award a;
    QSqlQuery q(connection());
    const QString ft2 = QStringLiteral("FROM qso WHERE deleted = 0 AND submode = 'FT2'");
    const QString lotw = QStringLiteral(
        " AND EXISTS (SELECT 1 FROM qsl_status s WHERE s.qso_id = qso.id AND s.service = 'lotw' AND s.rcvd = 'Y')");
    auto scalar = [&q](const QString& sql) {
        return q.exec(sql) && q.next() ? q.value(0).toInt() : 0;
    };
    a.qsos = scalar(QStringLiteral("SELECT COUNT(*) ") + ft2);
    a.dxccWorked = scalar(QStringLiteral("SELECT COUNT(DISTINCT dxcc) ") + ft2 + QStringLiteral(" AND dxcc > 0"));
    a.dxccConfirmed = scalar(QStringLiteral("SELECT COUNT(DISTINCT dxcc) ") + ft2 + QStringLiteral(" AND dxcc > 0") + lotw);
    const QString grid = QStringLiteral("SELECT COUNT(DISTINCT UPPER(SUBSTR(gridsquare, 1, 4))) ");
    const QString hasGrid = QStringLiteral(" AND LENGTH(gridsquare) >= 4");
    a.gridsWorked = scalar(grid + ft2 + hasGrid);
    a.gridsConfirmed = scalar(grid + ft2 + hasGrid + lotw);
    return a;
}

QList<CountRow> LogDatabase::countByBand() const
{
    decolog::StartupSpan trace("LogDatabase::countByBand");
    QList<CountRow> out;
    QSqlQuery q(connection());
    if (q.exec(QStringLiteral("SELECT band, COUNT(*) FROM qso NOT INDEXED WHERE deleted = 0 GROUP BY band"))) {
        while (q.next())
            out << CountRow{q.value(0).toString(), q.value(1).toInt()};
    }
    const QStringList order = bands::all();
    std::sort(out.begin(), out.end(), [&order](const CountRow& a, const CountRow& b) {
        return order.indexOf(a.key) < order.indexOf(b.key);
    });
    return out;
}

QStringList LogDatabase::bandsInLog() const
{
    decolog::StartupSpan trace("LogDatabase::bandsInLog");
    QStringList out;
    QSqlQuery q(connection());
    if (q.exec(QStringLiteral("SELECT DISTINCT band FROM qso WHERE IFNULL(band, '') <> ''"))) {
        while (q.next())
            out << q.value(0).toString();
    }
    const QStringList order = bands::all();
    std::sort(out.begin(), out.end(), [&order](const QString& a, const QString& b) {
        const qsizetype ia = order.indexOf(a), ib = order.indexOf(b);
        if (ia != ib)
            return (ia < 0 ? order.size() : ia) < (ib < 0 ? order.size() : ib);
        return a < b;
    });
    return out;
}

QStringList LogDatabase::modesInLog() const
{
    decolog::StartupSpan trace("LogDatabase::modesInLog");
    QStringList out;
    QSqlQuery q(connection());
    if (q.exec(QStringLiteral("SELECT DISTINCT mode, IFNULL(submode, '') FROM qso"))) {
        while (q.next()) {
            const QString m = displayMode(q.value(0).toString(), q.value(1).toString());
            if (!m.isEmpty() && !out.contains(m))
                out << m;
        }
    }
    // I modi di sempre prima, nell'ordine in cui si usano; gli altri dopo.
    static const QStringList common{QStringLiteral("FT8"), QStringLiteral("FT4"), QStringLiteral("FT2"),
                                    QStringLiteral("CW"), QStringLiteral("SSB"), QStringLiteral("RTTY"),
                                    QStringLiteral("FM"), QStringLiteral("AM"), QStringLiteral("JT65"),
                                    QStringLiteral("PSK31")};
    std::sort(out.begin(), out.end(), [](const QString& a, const QString& b) {
        const qsizetype ia = common.indexOf(a), ib = common.indexOf(b);
        if (ia != ib)
            return (ia < 0 ? common.size() : ia) < (ib < 0 ? common.size() : ib);
        return a < b;
    });
    return out;
}

QList<CountRow> LogDatabase::countByMode() const
{
    decolog::StartupSpan trace("LogDatabase::countByMode");
    QList<CountRow> out;
    QSqlQuery q(connection());
    if (q.exec(QStringLiteral(
            "SELECT CASE WHEN IFNULL(submode, '') = '' OR mode = 'SSB' THEN mode ELSE submode END AS m, COUNT(*) AS n "
            "FROM qso NOT INDEXED WHERE deleted = 0 GROUP BY m ORDER BY n DESC"))) {
        while (q.next())
            out << CountRow{q.value(0).toString(), q.value(1).toInt()};
    }
    return out;
}

QList<CountRow> LogDatabase::countByDxcc() const
{
    QList<CountRow> out;
    QSqlQuery q(connection());
    if (q.exec(QStringLiteral(
            "SELECT dxcc, COUNT(*) AS n FROM qso NOT INDEXED WHERE deleted = 0 AND dxcc > 0 GROUP BY dxcc ORDER BY n DESC, dxcc"))) {
        while (q.next())
            out << CountRow{q.value(0).toString(), q.value(1).toInt()};
    }
    return out;
}

namespace {

// Le condizioni comuni alle statistiche: modo (FT2 e' un sottomodo), banda, anno.
QString statsWhere(const StatsFilter& f, QVariantList& binds)
{
    QString where = QStringLiteral("deleted = 0");
    if (!f.mode.isEmpty()) {
        where += QStringLiteral(" AND (CASE WHEN IFNULL(submode, '') = '' OR mode = 'SSB' THEN mode ELSE submode END) = ?");
        binds << f.mode.toUpper();
    }
    if (!f.band.isEmpty()) {
        where += QStringLiteral(" AND band = ?");
        binds << f.band.toLower();
    }
    if (f.year > 0) {
        where += QStringLiteral(" AND SUBSTR(qso_datetime_on, 1, 4) = ?");
        binds << QString::number(f.year);
    }
    return where;
}

QList<CountRow> groupedCount(const QSqlDatabase& db, const QString& expression, const StatsFilter& filter,
                             const QString& order, int limit = 0)
{
    QList<CountRow> out;
    QVariantList binds;
    const QString where = statsWhere(filter, binds);
    QString sql = QStringLiteral("SELECT %1 AS k, COUNT(*) AS n FROM qso NOT INDEXED WHERE %2 AND k IS NOT NULL AND k <> '' "
                                 "GROUP BY k ORDER BY %3").arg(expression, where, order);
    if (limit > 0)
        sql += QStringLiteral(" LIMIT %1").arg(limit);
    QSqlQuery q(db);
    q.setForwardOnly(true);
    q.prepare(sql);
    for (const QVariant& b : binds)
        q.addBindValue(b);
    if (!q.exec())
        return out;
    while (q.next())
        out << CountRow{q.value(0).toString(), q.value(1).toInt()};
    return out;
}

} // namespace

QList<CountRow> LogDatabase::countByEntity(const StatsFilter& filter, int limit) const
{
    return groupedCount(connection(), QStringLiteral("country"), filter, QStringLiteral("n DESC, k"), limit);
}

QList<CountRow> LogDatabase::countByCall(const StatsFilter& filter, int limit) const
{
    return groupedCount(connection(), QStringLiteral("call"), filter, QStringLiteral("n DESC, k"), limit);
}

QList<QVariantMap> LogDatabase::bandByMode(const StatsFilter& filter) const
{
    QList<QVariantMap> out;
    QVariantList binds;
    const QString where = statsWhere(filter, binds);
    QSqlQuery q(connection());
    q.setForwardOnly(true);
    q.prepare(QStringLiteral("SELECT band, CASE WHEN IFNULL(submode, '') = '' OR mode = 'SSB' THEN mode ELSE submode END AS m, "
                             "COUNT(*) FROM qso NOT INDEXED WHERE %1 AND band <> '' GROUP BY band, m").arg(where));
    for (const QVariant& b : binds)
        q.addBindValue(b);
    if (!q.exec())
        return out;
    while (q.next())
        out << QVariantMap{{QStringLiteral("band"), q.value(0).toString()},
                           {QStringLiteral("mode"), q.value(1).toString()},
                           {QStringLiteral("count"), q.value(2).toInt()}};
    return out;
}

QList<QVariantMap> LogDatabase::awardProgress(const StatsFilter& filter) const
{
    QList<QVariantMap> out;
    StatsFilter all = filter;
    all.year = 0;       // la crescita si vede su tutti gli anni
    QVariantList binds;
    const QString where = statsWhere(all, binds);
    QSqlQuery q(connection());
    q.setForwardOnly(true);
    q.prepare(QStringLiteral(
        "SELECT SUBSTR(qso_datetime_on, 1, 4), IFNULL(dxcc, 0), IFNULL(cqz, 0), UPPER(SUBSTR(IFNULL(gridsquare, ''), 1, 4)), "
        "EXISTS (SELECT 1 FROM qsl_status s WHERE s.qso_id = qso.id AND s.rcvd = 'Y' AND s.service IN ('lotw', 'card')), "
        "LOWER(IFNULL(band, '')) "
        "FROM qso NOT INDEXED WHERE %1 ORDER BY qso_datetime_on").arg(where));
    for (const QVariant& b : binds)
        q.addBindValue(b);
    if (!q.exec())
        return out;
    QSet<int> dxcc;
    QSet<int> confirmed;
    QSet<int> zones;
    QSet<QString> grids;
    QString year;
    int qsos = 0;
    auto flush = [&] {
        if (year.isEmpty())
            return;
        out << QVariantMap{{QStringLiteral("year"), year},
                           {QStringLiteral("dxcc"), int(dxcc.size())},
                           {QStringLiteral("dxccConfirmed"), int(confirmed.size())},
                           {QStringLiteral("zones"), int(zones.size())},
                           {QStringLiteral("grids"), int(grids.size())},
                           {QStringLiteral("qsos"), qsos}};
    };
    static const QRegularExpression gridRe(QStringLiteral("^[A-R]{2}[0-9]{2}$"));
    while (q.next()) {
        const QString y = q.value(0).toString();
        if (y != year) {
            flush();
            year = y;
        }
        ++qsos;
        // Il DXCC: per l'ARRL i 60 metri non valgono, e si tolgono se
        // l'operatore ha scelto il conto ufficiale.
        if (const int d = q.value(1).toInt(); d > 0 && (all.count60m || q.value(5).toString() != QLatin1String("60m"))) {
            dxcc.insert(d);
            if (q.value(4).toBool())
                confirmed.insert(d);
        }
        if (const int z = q.value(2).toInt(); z > 0 && z <= 40)
            zones.insert(z);
        const QString g = q.value(3).toString();
        if (gridRe.match(g).hasMatch())
            grids.insert(g);
    }
    flush();
    return out;
}

QList<CountRow> LogDatabase::countByYear(const StatsFilter& filter) const
{
    return groupedCount(connection(), QStringLiteral("SUBSTR(qso_datetime_on, 1, 4)"), filter, QStringLiteral("k"));
}

QList<CountRow> LogDatabase::countByMonth(int months, const StatsFilter& filter) const
{
    QList<CountRow> all = groupedCount(connection(), QStringLiteral("SUBSTR(qso_datetime_on, 1, 7)"), filter,
                                       QStringLiteral("k DESC"), months);
    std::reverse(all.begin(), all.end());
    return all;
}

QList<CountRow> LogDatabase::countByHour(const StatsFilter& filter) const
{
    return groupedCount(connection(), QStringLiteral("SUBSTR(qso_datetime_on, 12, 2)"), filter, QStringLiteral("k"));
}

QList<CountRow> LogDatabase::countByContinent(const StatsFilter& filter) const
{
    return groupedCount(connection(), QStringLiteral("cont"), filter, QStringLiteral("n DESC"));
}

QList<CountRow> LogDatabase::countByBand(const StatsFilter& filter) const
{
    QList<CountRow> out = groupedCount(connection(), QStringLiteral("band"), filter, QStringLiteral("n DESC"));
    const QStringList order = bands::all();
    std::sort(out.begin(), out.end(), [&order](const CountRow& a, const CountRow& b) {
        return order.indexOf(a.key) < order.indexOf(b.key);
    });
    return out;
}

QList<CountRow> LogDatabase::countByMode(const StatsFilter& filter) const
{
    return groupedCount(connection(),
                        QStringLiteral("CASE WHEN IFNULL(submode, '') = '' OR mode = 'SSB' THEN mode ELSE submode END"),
                        filter, QStringLiteral("n DESC"));
}

QList<QVariantMap> LogDatabase::bandByHour(const StatsFilter& filter) const
{
    QList<QVariantMap> out;
    QVariantList binds;
    const QString where = statsWhere(filter, binds);
    QSqlQuery q(connection());
    q.setForwardOnly(true);
    q.prepare(QStringLiteral("SELECT band, CAST(SUBSTR(qso_datetime_on, 12, 2) AS INTEGER) AS h, COUNT(*) "
                             "FROM qso NOT INDEXED WHERE %1 AND band <> '' GROUP BY band, h").arg(where));
    for (const QVariant& b : binds)
        q.addBindValue(b);
    if (!q.exec())
        return out;
    while (q.next()) {
        out << QVariantMap{{QStringLiteral("band"), q.value(0).toString()},
                           {QStringLiteral("hour"), q.value(1).toInt()},
                           {QStringLiteral("count"), q.value(2).toInt()}};
    }
    return out;
}

QVariantMap LogDatabase::statsSummary(const StatsFilter& filter) const
{
    decolog::StartupSpan trace("LogDatabase::statsSummary");
    QVariantMap out;
    QVariantList binds;
    const QString where = statsWhere(filter, binds);
    QSqlQuery q(connection());
    q.prepare(QStringLiteral(
        "SELECT COUNT(*), COUNT(DISTINCT call), COUNT(DISTINCT CASE WHEN dxcc > 0 THEN dxcc END), "
        "MIN(qso_datetime_on), MAX(qso_datetime_on), "
        "COUNT(DISTINCT UPPER(SUBSTR(gridsquare, 1, 4))) FROM qso NOT INDEXED WHERE %1").arg(where));
    for (const QVariant& b : binds)
        q.addBindValue(b);
    if (q.exec() && q.next()) {
        out.insert(QStringLiteral("qsos"), q.value(0).toInt());
        out.insert(QStringLiteral("calls"), q.value(1).toInt());
        out.insert(QStringLiteral("dxcc"), q.value(2).toInt());
        out.insert(QStringLiteral("first"), q.value(3).toString().left(10));
        out.insert(QStringLiteral("last"), q.value(4).toString().left(10));
        out.insert(QStringLiteral("grids"), q.value(5).toInt());
    }

    // Il giorno con piu' QSO e l'ora piu' produttiva: sono quelli che si raccontano.
    const QList<CountRow> days = groupedCount(connection(), QStringLiteral("SUBSTR(qso_datetime_on, 1, 10)"),
                                              filter, QStringLiteral("n DESC"), 1);
    if (!days.isEmpty()) {
        out.insert(QStringLiteral("bestDay"), days.first().key);
        out.insert(QStringLiteral("bestDayCount"), days.first().count);
    }
    const QList<CountRow> hours = groupedCount(connection(), QStringLiteral("SUBSTR(qso_datetime_on, 12, 2)"),
                                               filter, QStringLiteral("n DESC"), 1);
    if (!hours.isEmpty()) {
        out.insert(QStringLiteral("bestHour"), hours.first().key);
        out.insert(QStringLiteral("bestHourCount"), hours.first().count);
    }
    return out;
}

QStringList LogDatabase::yearsInLog() const
{
    decolog::StartupSpan trace("LogDatabase::yearsInLog");
    QStringList out;
    QSqlQuery q(connection());
    if (q.exec(QStringLiteral("SELECT DISTINCT SUBSTR(qso_datetime_on, 1, 4) AS y FROM qso NOT INDEXED WHERE deleted = 0 "
                              "ORDER BY y DESC"))) {
        while (q.next())
            out << q.value(0).toString();
    }
    return out;
}

QList<QVariantMap> LogDatabase::qslSummary() const
{
    decolog::StartupSpan trace("LogDatabase::qslSummary");
    QList<QVariantMap> out;
    for (const auto& f : kQslFields) {
        QSqlQuery q(connection());
        q.prepare(QStringLiteral(
            "SELECT SUM(sent IN ('R','Q')), SUM(sent = 'Y'), SUM(rcvd = 'Y'), SUM(IFNULL(last_error, '') <> '') "
            "FROM qsl_status s JOIN qso ON qso.id = s.qso_id WHERE qso.deleted = 0 AND s.service = ?"));
        q.addBindValue(QLatin1String(f.service));
        QVariantMap row{{QStringLiteral("service"), QLatin1String(f.service)}};
        if (q.exec() && q.next()) {
            row[QStringLiteral("queued")] = q.value(0).toInt();
            row[QStringLiteral("sent")] = q.value(1).toInt();
            row[QStringLiteral("confirmed")] = q.value(2).toInt();
            row[QStringLiteral("errors")] = q.value(3).toInt();
        }
        out << row;
    }
    return out;
}

QStringList LogDatabase::workedGrids(int limit) const
{
    decolog::StartupSpan trace("LogDatabase::workedGrids");
    QStringList out;
    QSqlQuery q(connection());
    q.prepare(QStringLiteral(
        "SELECT DISTINCT UPPER(SUBSTR(gridsquare, 1, 4)) FROM qso WHERE deleted = 0 "
        "AND LENGTH(gridsquare) >= 4 LIMIT ?"));
    q.addBindValue(limit);
    if (q.exec()) {
        while (q.next())
            out << q.value(0).toString();
    }
    return out;
}

// ── Invio QSL ─────────────────────────────────────────────────────────────────

bool LogDatabase::setCardState(qint64 id, const QslState& state)
{
    QslState card = state;
    card.service = QStringLiteral("card");
    return writeQslState(id, card, true);
}

qint64 LogDatabase::addQslAttachment(const QByteArray& data, const QString& filename, const QString& mime,
                                      const QList<qint64>& qsoIds)
{
    if (data.isEmpty() || qsoIds.isEmpty())
        return 0;
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
    QSqlDatabase db = connection();
    const bool ownTransaction = db.transaction();
    QSqlQuery find(db);
    find.prepare(QStringLiteral("SELECT id FROM qsl_attachment WHERE sha256 = ?"));
    find.addBindValue(hash);
    qint64 id = 0;
    if (find.exec() && find.next()) {
        id = find.value(0).toLongLong();
    } else {
        QSqlQuery insert(db);
        insert.prepare(QStringLiteral("INSERT INTO qsl_attachment (sha256, filename, mime, size, data, created_at) VALUES (?, ?, ?, ?, ?, ?)"));
        insert.addBindValue(hash);
        insert.addBindValue(filename.trimmed().isEmpty() ? QStringLiteral("qsl") : filename.trimmed());
        insert.addBindValue(mime.trimmed().isEmpty() ? QStringLiteral("application/octet-stream") : mime.trimmed());
        insert.addBindValue(data.size());
        insert.addBindValue(data);
        insert.addBindValue(nowIso());
        if (!insert.exec()) {
            if (ownTransaction)
                db.rollback();
            return 0;
        }
        id = insert.lastInsertId().toLongLong();
    }
    QSqlQuery link(db);
    link.prepare(QStringLiteral("INSERT OR IGNORE INTO qsl_attachment_qso (attachment_id, qso_id) VALUES (?, ?)"));
    for (const qint64 qsoId : qsoIds) {
        link.bindValue(0, id);
        link.bindValue(1, qsoId);
        if (!link.exec()) {
            if (ownTransaction)
                db.rollback();
            return 0;
        }
    }
    if (ownTransaction && !db.commit())
        return 0;
    return id;
}

QList<QslAttachment> LogDatabase::qslAttachments(qint64 qsoId) const
{
    QList<QslAttachment> out;
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("SELECT a.id, a.sha256, a.filename, a.mime, a.size, a.created_at FROM qsl_attachment a "
                             "JOIN qsl_attachment_qso x ON x.attachment_id = a.id WHERE x.qso_id = ? ORDER BY a.created_at, a.id"));
    q.addBindValue(qsoId);
    if (!q.exec())
        return out;
    while (q.next())
        out << QslAttachment{q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toString(),
                              q.value(3).toString(), q.value(4).toLongLong(), q.value(5).toString()};
    return out;
}

QByteArray LogDatabase::qslAttachmentData(qint64 attachmentId) const
{
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("SELECT data FROM qsl_attachment WHERE id = ?"));
    q.addBindValue(attachmentId);
    return q.exec() && q.next() ? q.value(0).toByteArray() : QByteArray();
}

bool LogDatabase::setQslState(qint64 id, const QslState& st)
{
    return writeQslState(id, st, false);
}

bool LogDatabase::writeQslState(qint64 id, const QslState& st, bool includeReceived)
{
    QSqlDatabase db = connection();
    const bool ownTransaction = db.transaction();
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO qsl_status (qso_id, service, sent, sent_date, rcvd, rcvd_date, remote_id, last_error, via) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(qso_id, service) DO UPDATE SET sent = excluded.sent, sent_date = excluded.sent_date, "
        "remote_id = COALESCE(excluded.remote_id, qsl_status.remote_id), last_error = excluded.last_error, "
        "via = COALESCE(excluded.via, qsl_status.via)")
        + (includeReceived ? QStringLiteral(", rcvd = excluded.rcvd, rcvd_date = excluded.rcvd_date")
                           : QString()));
    auto nullable = [](const QString& v) { return v.isEmpty() ? QVariant() : QVariant(v); };
    q.addBindValue(id);
    q.addBindValue(st.service);
    q.addBindValue(st.sent.isEmpty() ? QStringLiteral("N") : st.sent);
    q.addBindValue(nullable(st.sentDate));
    q.addBindValue(st.rcvd.isEmpty() ? QStringLiteral("N") : st.rcvd);
    q.addBindValue(nullable(st.rcvdDate));
    q.addBindValue(nullable(st.remoteId));
    q.addBindValue(nullable(st.lastError));
    q.addBindValue(nullable(st.via));
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        if (ownTransaction)
            db.rollback();
        return false;
    }
    // Il QSO e' cambiato per il cloud, ma non e' una revisione nuova.
    QSqlQuery u(db);
    u.prepare(QStringLiteral("UPDATE qso SET dirty = 1, updated_at = ? WHERE id = ?"));
    u.addBindValue(nowIso());
    u.addBindValue(id);
    u.exec();
    if (ownTransaction)
        return db.commit();
    return true;
}

QList<qint64> LogDatabase::qsosToUpload(const QString& service, int limit, const QDate& since) const
{
    decolog::StartupSpan trace("LogDatabase::qsosToUpload");
    QList<qint64> ids;
    QSqlQuery q(connection());
    q.setForwardOnly(true);
    q.prepare(QStringLiteral(
        "SELECT qso.id FROM qso NOT INDEXED LEFT JOIN qsl_status s ON s.qso_id = qso.id AND s.service = ? "
        "WHERE qso.deleted = 0 AND (s.sent IS NULL OR s.sent IN ('N', 'R', 'Q', 'D')) ")
        + (since.isValid() ? QStringLiteral("AND (qso.qso_datetime_on >= ? OR IFNULL(s.remote_id, '') <> '') ")
                           : QString())
        + QStringLiteral("ORDER BY qso.qso_datetime_on, qso.id") + (limit > 0 ? QStringLiteral(" LIMIT ?") : QString()));
    q.addBindValue(service);
    if (since.isValid())
        q.addBindValue(since.toString(Qt::ISODate));
    if (limit > 0)
        q.addBindValue(limit);
    if (!q.exec())
        return ids;
    while (q.next())
        ids << q.value(0).toLongLong();
    return ids;
}

int LogDatabase::uploadPendingCount(const QString& service, const QDate& since) const
{
    decolog::StartupSpan trace("LogDatabase::uploadPendingCount");
    QSqlQuery q(connection());
    if (!since.isValid()) {
        // Tutti i QSO meno quelli gia' andati: si contano solo le righe del
        // servizio, non si passa tutto il log. Su un milione di QSO, da sei
        // secondi a un decimo — e la pagina QSL lo chiede per ogni servizio.
        q.prepare(QStringLiteral(
            "SELECT (SELECT COUNT(*) FROM qso WHERE deleted = 0) - "
            "(SELECT COUNT(*) FROM qsl_status s JOIN qso ON qso.id = s.qso_id "
            " WHERE s.service = ? AND qso.deleted = 0 AND s.sent NOT IN ('N', 'R', 'Q', 'D'))"));
        q.addBindValue(service);
    } else {
        q.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM qso NOT INDEXED LEFT JOIN qsl_status s ON s.qso_id = qso.id AND s.service = ? "
            "WHERE qso.deleted = 0 AND (s.sent IS NULL OR s.sent IN ('N', 'R', 'Q', 'D')) "
            "AND (qso.qso_datetime_on >= ? OR IFNULL(s.remote_id, '') <> '')"));
        q.addBindValue(service);
        q.addBindValue(since.toString(Qt::ISODate));
    }
    const int toSend = q.exec() && q.next() ? q.value(0).toInt() : 0;
    return toSend + static_cast<int>(remoteDeletions(service).size());
}

QStringList LogDatabase::editableServices()
{
    return {QStringLiteral("crx")};
}

void LogDatabase::queueRemoteEdit(qint64 id)
{
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("UPDATE qsl_status SET sent = 'R' WHERE qso_id = ? AND sent = 'Y' "
                             "AND IFNULL(remote_id, '') <> '' AND service IN ('%1')").arg(editableServices().join(QStringLiteral("','"))));
    q.addBindValue(id);
    q.exec();
}

QStringList LogDatabase::markSentForResend(qint64 id)
{
    // Gli stessi servizi della colonna QSL del log; la cartolina e CRX hanno altre vie.
    const QString where = QStringLiteral("qso_id = ? AND sent = 'Y' AND IFNULL(rcvd, '') <> 'Y' "
                                         "AND service IN ('lotw', 'qrz', 'clublog', 'hrdlog', 'eqsl')");
    QStringList touched;
    QSqlQuery s(connection());
    s.prepare(QStringLiteral("SELECT service FROM qsl_status WHERE %1 ORDER BY service").arg(where));
    s.addBindValue(id);
    if (s.exec()) {
        while (s.next())
            touched << s.value(0).toString();
    }
    if (touched.isEmpty())
        return {};
    // Il remote_id resta: se il servizio ne ha uno, tiene il QSO fra i da-mandare anche
    // quando e' piu' vecchio del «da quel giorno».
    QSqlQuery u(connection());
    u.prepare(QStringLiteral("UPDATE qsl_status SET sent = 'R', sent_date = NULL, last_error = NULL WHERE %1").arg(where));
    u.addBindValue(id);
    u.exec();
    return touched;
}

QList<QPair<qint64, QString>> LogDatabase::remoteDeletions(const QString& service) const
{
    QList<QPair<qint64, QString>> out;
    QSqlQuery q(connection());
    q.setForwardOnly(true);
    q.prepare(QStringLiteral(
        "SELECT s.qso_id, s.remote_id FROM qsl_status s JOIN qso ON qso.id = s.qso_id "
        "WHERE s.service = ? AND s.sent = 'D' AND qso.deleted = 1 AND IFNULL(s.remote_id, '') <> '' "
        "ORDER BY s.qso_id"));
    q.addBindValue(service);
    if (!q.exec())
        return out;
    while (q.next())
        out.append({q.value(0).toLongLong(), q.value(1).toString()});
    return out;
}

bool LogDatabase::forgetRemote(qint64 id, const QString& service)
{
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("UPDATE qsl_status SET sent = 'N', sent_date = NULL, remote_id = NULL, "
                             "last_error = NULL WHERE qso_id = ? AND service = ?"));
    q.addBindValue(id);
    q.addBindValue(service);
    return q.exec();
}

// ── Sync con DecoDXLog Cloud ────────────────────────────────────────────────────

QList<qint64> LogDatabase::dirtyQsos(int limit) const
{
    QList<qint64> ids;
    QSqlQuery q(connection());
    q.setForwardOnly(true);
    q.prepare(QStringLiteral("SELECT id FROM qso WHERE dirty = 1 ORDER BY updated_at, id")
              + (limit > 0 ? QStringLiteral(" LIMIT ?") : QString()));
    if (limit > 0)
        q.addBindValue(limit);
    if (!q.exec())
        return ids;
    while (q.next())
        ids << q.value(0).toLongLong();
    return ids;
}

qint64 LogDatabase::idForUuid(const QString& uuid) const
{
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("SELECT id FROM qso WHERE uuid = ?"));
    q.addBindValue(uuid);
    return q.exec() && q.next() ? q.value(0).toLongLong() : 0;
}

QVariantMap LogDatabase::syncRecord(qint64 id) const
{
    const auto m = meta(id);
    const auto r = record(id);
    if (!m || !r)
        return {};

    QVariantMap fields;
    for (const auto& f : r->fields())
        fields.insert(f.name, f.value);

    QSqlQuery q(connection());
    q.prepare(QStringLiteral(
        "SELECT call, band, mode, submode, qso_datetime_on FROM qso WHERE id = ?"));
    q.addBindValue(id);
    QString call;
    QString band;
    QString mode;
    QString submode;
    QString on;
    if (q.exec() && q.next()) {
        call = q.value(0).toString();
        band = q.value(1).toString();
        mode = q.value(2).toString();
        submode = q.value(3).toString();
        on = q.value(4).toString();
    }

    return QVariantMap{
        {QStringLiteral("uuid"), m->uuid},
        {QStringLiteral("revision"), m->revision},
        {QStringLiteral("deleted"), m->deleted},
        // L'ora scritta a mano e' approssimativa: il server allarga la finestra.
        {QStringLiteral("manual"), m->source == QLatin1String("manual")},
        {QStringLiteral("call"), call},
        {QStringLiteral("band"), band},
        {QStringLiteral("mode"), mode},
        {QStringLiteral("submode"), submode},
        {QStringLiteral("startedAt"), on},
        {QStringLiteral("fields"), fields},
    };
}

bool LogDatabase::markSynced(qint64 id, int revision)
{
    QSqlQuery q(connection());
    if (revision > 0) {
        q.prepare(QStringLiteral("UPDATE qso SET dirty = 0, revision = ? WHERE id = ?"));
        q.addBindValue(revision);
    } else {
        q.prepare(QStringLiteral("UPDATE qso SET dirty = 0 WHERE id = ?"));
    }
    q.addBindValue(id);
    return q.exec();
}

bool LogDatabase::adoptUuid(qint64 id, const QString& serverUuid)
{
    if (serverUuid.isEmpty())
        return false;
    const qint64 existing = idForUuid(serverUuid);
    if (existing == id)
        return markSynced(id, 0);
    if (existing > 0) {
        // Quel collegamento e' gia' qui sotto l'uuid del server: questo e' un
        // doppione locale, e si toglie di mezzo com'e' d'uso, in morbido.
        softDeleteQso(id);
        QSqlQuery q(connection());
        q.prepare(QStringLiteral("UPDATE qso SET dirty = 0 WHERE id = ?"));
        q.addBindValue(id);
        return q.exec();
    }
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("UPDATE qso SET uuid = ?, dirty = 0 WHERE id = ?"));
    q.addBindValue(serverUuid);
    q.addBindValue(id);
    return q.exec();
}

LogDatabase::RemoteResult LogDatabase::applyRemote(const QVariantMap& remote)
{
    const QString uuid = remote.value(QStringLiteral("uuid")).toString();
    if (uuid.isEmpty())
        return RemoteResult::Failed;
    const int revision = remote.value(QStringLiteral("revision")).toInt();
    const bool deleted = remote.value(QStringLiteral("deleted")).toBool();

    AdifRecord record;
    const QVariantMap fields = remote.value(QStringLiteral("fields")).toMap();
    for (auto it = fields.cbegin(); it != fields.cend(); ++it)
        record.set(it.key(), it.value().toString());

    qint64 id = idForUuid(uuid);

    if (id > 0) {
        const auto m = meta(id);
        if (m && m->revision > revision) {
            // Qui c'e' gia' qualcosa di piu' nuovo: resta, e ripartira' in coda.
            return RemoteResult::Skipped;
        }
        if (m && m->revision == revision && !m->dirty && !deleted) {
            // Stessa revisione: per il protocollo e' lo stesso contenuto. Succede
            // al giro dopo una spinta, quando il server ci rimanda i nostri.
            return RemoteResult::Skipped;
        }
        if (m && m->dirty && m->revision >= revision) {
            // C'e' una modifica locale ancora da mandare, e il server non ne sa
            // di piu': non si sovrascrive. Parte lei, e il conflitto lo decide
            // il server, che conserva la versione che perde.
            return RemoteResult::Skipped;
        }
        if (deleted) {
            if (m && m->deleted)
                return RemoteResult::Skipped;
            softDeleteQso(id);
        } else {
            InsertResult updated = updateQso(id, record, -1, QStringLiteral("cloud"));
            if (updated.status != InsertResult::Status::Inserted)
                return RemoteResult::Failed;
        }
    } else {
        if (deleted)
            return RemoteResult::Skipped;   // non l'abbiamo mai avuto: niente da fare
        InsertResult inserted = insertQso(record, QStringLiteral("cloud"));
        if (inserted.status == InsertResult::Status::Duplicate && inserted.id > 0) {
            // Lo stesso collegamento c'era gia' con un altro uuid: ci si allinea
            // a quello del server, che e' il nome comune.
            id = inserted.id;
        } else if (inserted.status != InsertResult::Status::Inserted) {
            return RemoteResult::Failed;
        } else {
            id = inserted.id;
        }
    }

    // Quello che arriva dal Cloud non torna in coda, e porta con se' la sua
    // revisione: e' gia' quella del server.
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("UPDATE qso SET uuid = ?, revision = ?, dirty = 0 WHERE id = ?"));
    q.addBindValue(uuid);
    q.addBindValue(revision > 0 ? revision : 1);
    q.addBindValue(id);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return RemoteResult::Failed;
    }
    return deleted ? RemoteResult::Deleted
                   : (id > 0 ? RemoteResult::Updated : RemoteResult::Inserted);
}

QList<QVariantMap> LogDatabase::dirtyProfiles() const
{
    QList<QVariantMap> out;
    for (const StationProfile& p : stationProfiles(true)) {
        if (!p.dirty)
            continue;
        out << QVariantMap{
            {QStringLiteral("kind"), QStringLiteral("profile")},
            {QStringLiteral("key"), p.uuid},
            {QStringLiteral("revision"), p.revision},
            {QStringLiteral("deleted"), p.deleted},
            {QStringLiteral("data"), QVariantMap{
                {QStringLiteral("name"), p.name},
                {QStringLiteral("stationCallsign"), p.stationCallsign},
                {QStringLiteral("operatorCall"), p.operatorCall},
                {QStringLiteral("myGridsquare"), p.myGridsquare},
                {QStringLiteral("myCqZone"), p.myCqZone},
                {QStringLiteral("myItuZone"), p.myItuZone},
                {QStringLiteral("myDxcc"), p.myDxcc},
                {QStringLiteral("myRig"), p.myRig},
                {QStringLiteral("myAntenna"), p.myAntenna},
                {QStringLiteral("defaultTxPwr"), p.defaultTxPwr},
                {QStringLiteral("lotwStationLocation"), p.lotwStationLocation},
                {QStringLiteral("isDefault"), p.isDefault},
            }},
        };
    }
    return out;
}

bool LogDatabase::markProfileSynced(const QString& uuid, int revision)
{
    QSqlQuery q(connection());
    if (revision > 0) {
        q.prepare(QStringLiteral("UPDATE station_profile SET dirty = 0, revision = ? WHERE uuid = ?"));
        q.addBindValue(revision);
    } else {
        q.prepare(QStringLiteral("UPDATE station_profile SET dirty = 0 WHERE uuid = ?"));
    }
    q.addBindValue(uuid);
    return q.exec();
}

LogDatabase::RemoteResult LogDatabase::applyRemoteProfile(const QVariantMap& document)
{
    const QString uuid = document.value(QStringLiteral("key")).toString();
    if (uuid.isEmpty())
        return RemoteResult::Failed;
    const int revision = document.value(QStringLiteral("revision")).toInt();
    const bool deleted = document.value(QStringLiteral("deleted")).toBool();
    const QVariantMap data = document.value(QStringLiteral("data")).toMap();

    QSqlQuery find(connection());
    find.prepare(QStringLiteral("SELECT id, revision, dirty FROM station_profile WHERE uuid = ?"));
    find.addBindValue(uuid);
    const bool exists = find.exec() && find.next();
    const qint64 id = exists ? find.value(0).toLongLong() : 0;
    const int localRevision = exists ? find.value(1).toInt() : 0;
    const bool localDirty = exists && find.value(2).toInt() != 0;

    if (exists) {
        if (localRevision > revision)
            return RemoteResult::Skipped;
        if (localRevision == revision && !localDirty)
            return RemoteResult::Skipped;   // e' lo stesso profilo
        if (localDirty && localRevision >= revision)
            return RemoteResult::Skipped;   // c'e' una modifica locale da mandare
    }

    QSqlQuery q(connection());
    if (exists) {
        q.prepare(QStringLiteral(
            "UPDATE station_profile SET name = ?, station_callsign = ?, operator = ?, my_gridsquare = ?, "
            "my_cq_zone = ?, my_itu_zone = ?, my_dxcc = ?, my_rig = ?, my_antenna = ?, default_tx_pwr = ?, "
            "lotw_station_loc = ?, is_default = ?, updated_at = ?, revision = ?, deleted = ?, dirty = 0 "
            "WHERE uuid = ?"));
    } else {
        q.prepare(QStringLiteral(
            "INSERT INTO station_profile (name, station_callsign, operator, my_gridsquare, my_cq_zone, "
            "my_itu_zone, my_dxcc, my_rig, my_antenna, default_tx_pwr, lotw_station_loc, is_default, "
            "updated_at, revision, deleted, dirty, uuid) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0, ?)"));
    }
    auto text = [&data](const char* key) {
        const QString value = data.value(QLatin1String(key)).toString().trimmed();
        return value.isEmpty() ? QVariant() : QVariant(value);
    };
    auto number = [&data](const char* key) {
        const int value = data.value(QLatin1String(key)).toInt();
        return value > 0 ? QVariant(value) : QVariant();
    };
    q.addBindValue(data.value(QStringLiteral("name")).toString());
    q.addBindValue(data.value(QStringLiteral("stationCallsign")).toString().toUpper());
    q.addBindValue(text("operatorCall"));
    q.addBindValue(text("myGridsquare"));
    q.addBindValue(number("myCqZone"));
    q.addBindValue(number("myItuZone"));
    q.addBindValue(number("myDxcc"));
    q.addBindValue(text("myRig"));
    q.addBindValue(text("myAntenna"));
    const double power = data.value(QStringLiteral("defaultTxPwr")).toDouble();
    q.addBindValue(power > 0 ? QVariant(power) : QVariant());
    q.addBindValue(text("lotwStationLocation"));
    q.addBindValue(data.value(QStringLiteral("isDefault")).toBool() ? 1 : 0);
    q.addBindValue(nowIso());
    q.addBindValue(revision > 0 ? revision : 1);
    q.addBindValue(deleted ? 1 : 0);
    q.addBindValue(uuid);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return RemoteResult::Failed;
    }
    if (deleted)
        return RemoteResult::Deleted;
    return exists && id > 0 ? RemoteResult::Updated : RemoteResult::Inserted;
}

QVariantMap LogDatabase::syncState(const QString& account) const
{
    QSqlQuery q(connection());
    q.prepare(QStringLiteral(
        "SELECT device_id, pull_cursor, last_push_at, last_pull_at, last_error FROM sync_state WHERE account = ?"));
    q.addBindValue(account);
    if (!q.exec() || !q.next())
        return {};
    return QVariantMap{
        {QStringLiteral("device"), q.value(0).toString()},
        {QStringLiteral("cursor"), q.value(1).toString()},
        {QStringLiteral("lastPush"), q.value(2).toString()},
        {QStringLiteral("lastPull"), q.value(3).toString()},
        {QStringLiteral("lastError"), q.value(4).toString()},
    };
}

void LogDatabase::setSyncState(const QString& account, const QVariantMap& values)
{
    const QVariantMap before = syncState(account);
    auto pick = [&](const char* key) {
        const QString name = QLatin1String(key);
        return values.contains(name) ? values.value(name).toString() : before.value(name).toString();
    };
    QSqlQuery q(connection());
    q.prepare(QStringLiteral(
        "INSERT INTO sync_state (account, device_id, pull_cursor, last_push_at, last_pull_at, last_error) "
        "VALUES (?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(account) DO UPDATE SET device_id = excluded.device_id, "
        "pull_cursor = excluded.pull_cursor, last_push_at = excluded.last_push_at, "
        "last_pull_at = excluded.last_pull_at, last_error = excluded.last_error"));
    q.addBindValue(account);
    // device_id e' NOT NULL, e una QString vuota si lega come NULL: meglio un
    // nome qualsiasi che una riga che non entra.
    const QString device = pick("device");
    q.addBindValue(device.isEmpty() ? QStringLiteral("decolog") : device);
    q.addBindValue(pick("cursor"));
    q.addBindValue(pick("lastPush"));
    q.addBindValue(pick("lastPull"));
    q.addBindValue(pick("lastError"));
    if (!q.exec())
        m_lastError = q.lastError().text();
}

// ── QSL di carta ──────────────────────────────────────────────────────────────

QVariantMap LogDatabase::cardCounts() const
{
    decolog::StartupSpan trace("LogDatabase::cardCounts");
    QVariantMap out{{QStringLiteral("queue"), 0}, {QStringLiteral("sent"), 0},
                    {QStringLiteral("received"), 0}, {QStringLiteral("unanswered"), 0}};
    QSqlQuery q(connection());
    if (q.exec(QStringLiteral(
            "SELECT IFNULL(SUM(s.sent IN ('R', 'Q')), 0), IFNULL(SUM(s.sent = 'Y'), 0), IFNULL(SUM(s.rcvd = 'Y'), 0), "
            "IFNULL(SUM(s.rcvd = 'Y' AND s.sent <> 'Y'), 0) "
            "FROM qsl_status s JOIN qso ON qso.id = s.qso_id WHERE s.service = 'card' AND qso.deleted = 0"))
        && q.next()) {
        out[QStringLiteral("queue")] = q.value(0).toInt();
        out[QStringLiteral("sent")] = q.value(1).toInt();
        out[QStringLiteral("received")] = q.value(2).toInt();
        out[QStringLiteral("unanswered")] = q.value(3).toInt();
    }
    return out;
}

QList<QVariantMap> LogDatabase::cardRows(const QString& state, int limit) const
{
    decolog::StartupSpan trace("LogDatabase::cardRows");
    QString where = QStringLiteral("qso.deleted = 0");
    if (state == QLatin1String("queue"))
        where += QStringLiteral(" AND s.sent IN ('R', 'Q')");
    else if (state == QLatin1String("sent"))
        where += QStringLiteral(" AND s.sent = 'Y'");
    else if (state == QLatin1String("received"))
        where += QStringLiteral(" AND s.rcvd = 'Y'");
    else
        where += QStringLiteral(" AND (s.sent IN ('R', 'Q', 'Y') OR s.rcvd = 'Y')");

    QList<QVariantMap> out;
    QSqlQuery q(connection());
    q.setForwardOnly(true);
    q.prepare(QStringLiteral(
        "SELECT qso.id, qso.call, qso.qso_datetime_on, qso.band, qso.mode, qso.submode, qso.freq, "
        "qso.rst_sent, qso.name, qso.country, s.sent, s.sent_date, s.rcvd, s.rcvd_date, s.via "
        "FROM qsl_status s JOIN qso ON qso.id = s.qso_id "
        "WHERE s.service = 'card' AND ") + where
        + QStringLiteral(" ORDER BY qso.call, qso.qso_datetime_on")
        + (limit > 0 ? QStringLiteral(" LIMIT ?") : QString()));
    if (limit > 0)
        q.addBindValue(limit);
    if (!q.exec())
        return out;
    while (q.next()) {
        const QDateTime when = QDateTime::fromString(q.value(2).toString(), Qt::ISODate);
        const QString submode = q.value(5).toString();
        QVariantMap row{
            {QStringLiteral("id"), q.value(0).toLongLong()},
            {QStringLiteral("call"), q.value(1).toString()},
            {QStringLiteral("date"), when.toString(QStringLiteral("yyyy-MM-dd"))},
            {QStringLiteral("time"), when.toString(QStringLiteral("hhmm"))},
            {QStringLiteral("band"), q.value(3).toString()},
            {QStringLiteral("mode"), submode.isEmpty() ? q.value(4).toString() : submode},
            {QStringLiteral("freq"), q.value(6).toString()},
            {QStringLiteral("rst"), q.value(7).toString()},
            {QStringLiteral("name"), q.value(8).toString()},
            {QStringLiteral("country"), q.value(9).toString()},
            {QStringLiteral("sent"), q.value(10).toString()},
            {QStringLiteral("sentDate"), q.value(11).toString()},
            {QStringLiteral("rcvd"), q.value(12).toString()},
            {QStringLiteral("rcvdDate"), q.value(13).toString()},
            {QStringLiteral("via"), q.value(14).toString()},
        };
        out << row;
    }
    return out;
}

// ── Etichette ─────────────────────────────────────────────────────────────────

QStringList LogDatabase::splitTags(const QString& tags)
{
    QStringList out;
    for (const QString& raw : tags.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString tag = raw.simplified();
        if (tag.isEmpty())
            continue;
        const bool seen = std::any_of(out.cbegin(), out.cend(), [&tag](const QString& t) {
            return t.compare(tag, Qt::CaseInsensitive) == 0;
        });
        if (!seen)
            out << tag;
    }
    return out;
}

QString LogDatabase::joinTags(const QStringList& tags)
{
    return tags.join(QLatin1Char(','));
}

QList<CountRow> LogDatabase::tagCounts() const
{
    decolog::StartupSpan trace("LogDatabase::tagCounts");
    QHash<QString, CountRow> counts;
    QSqlQuery q(connection());
    q.setForwardOnly(true);
    if (q.exec(QStringLiteral("SELECT tags FROM qso WHERE deleted = 0 AND IFNULL(tags, '') <> ''"))) {
        while (q.next()) {
            for (const QString& tag : splitTags(q.value(0).toString())) {
                CountRow& row = counts[tag.toLower()];
                if (row.key.isEmpty())
                    row.key = tag;
                ++row.count;
            }
        }
    }
    QList<CountRow> out = counts.values();
    std::sort(out.begin(), out.end(), [](const CountRow& a, const CountRow& b) {
        return a.count != b.count ? a.count > b.count : a.key.compare(b.key, Qt::CaseInsensitive) < 0;
    });
    return out;
}

int LogDatabase::setTag(const QList<qint64>& ids, const QString& tag, bool add)
{
    const QString clean = tag.simplified().remove(QLatin1Char(','));
    if (clean.isEmpty())
        return 0;
    int changed = 0;
    QSqlDatabase db = connection();
    const bool ownTransaction = db.transaction();
    for (qint64 id : ids) {
        auto r = record(id);
        if (!r)
            continue;
        QStringList tags = splitTags(r->value(QStringLiteral("APP_DECOLOG_TAGS")));
        const qsizetype at = std::find_if(tags.cbegin(), tags.cend(), [&clean](const QString& t) {
                                 return t.compare(clean, Qt::CaseInsensitive) == 0;
                             }) - tags.cbegin();
        const bool present = at < tags.size();
        if (add == present)
            continue;
        if (add)
            tags << clean;
        else
            tags.removeAt(at);
        r->set(QStringLiteral("APP_DECOLOG_TAGS"), joinTags(tags));
        if (updateQso(id, *r, -1, QStringLiteral("tag")).status == InsertResult::Status::Inserted)
            ++changed;
    }
    if (ownTransaction)
        db.commit();
    return changed;
}

// ── Profili stazione ───────────────────────────────────────────────────────────

namespace {

StationProfile profileFromQuery(const QSqlQuery& q)
{
    StationProfile p;
    p.id = q.value(0).toLongLong();
    p.uuid = q.value(1).toString();
    p.name = q.value(2).toString();
    p.stationCallsign = q.value(3).toString();
    p.operatorCall = q.value(4).toString();
    p.myGridsquare = q.value(5).toString();
    p.myCqZone = q.value(6).toInt();
    p.myItuZone = q.value(7).toInt();
    p.myDxcc = q.value(8).toInt();
    p.myRig = q.value(9).toString();
    p.myAntenna = q.value(10).toString();
    p.defaultTxPwr = q.value(11).toDouble();
    p.lotwStationLocation = q.value(12).toString();
    p.awardAccount = q.value(13).toString();
    p.operatingLocation = q.value(14).toString();
    p.validFrom = q.value(15).toString();
    p.validTo = q.value(16).toString();
    p.isDefault = q.value(17).toBool();
    p.revision = q.value(18).toInt();
    p.deleted = q.value(19).toBool();
    p.dirty = q.value(20).toBool();
    p.qsoCount = q.value(21).toInt();
    p.operatorProfileId = q.value(22).toLongLong();
    p.operatingLocationId = q.value(23).toLongLong();
    p.dxccEntityId = q.value(24).toInt();
    return p;
}

const QString kProfileSelect = QStringLiteral(
    "SELECT id, uuid, name, station_callsign, operator, my_gridsquare, my_cq_zone, my_itu_zone, my_dxcc, "
    "my_rig, my_antenna, default_tx_pwr, lotw_station_loc, award_account, operating_location, valid_from, valid_to, "
    "is_default, revision, deleted, dirty, "
    "(SELECT COUNT(*) FROM qso WHERE qso.station_profile_id = station_profile.id AND qso.deleted = 0), "
    "operator_profile_id, operating_location_id, dxcc_entity_id "
    "FROM station_profile");

} // namespace

QList<StationProfile> LogDatabase::stationProfiles(bool includeDeleted) const
{
    decolog::StartupSpan trace("LogDatabase::stationProfiles");
    QList<StationProfile> out;
    QSqlQuery q(connection());
    const QString where = includeDeleted ? QString() : QStringLiteral(" WHERE deleted = 0");
    if (q.exec(kProfileSelect + where + QStringLiteral(" ORDER BY deleted, is_default DESC, name COLLATE NOCASE"))) {
        while (q.next())
            out << profileFromQuery(q);
    }
    return out;
}

std::optional<StationProfile> LogDatabase::stationProfile(qint64 id) const
{
    QSqlQuery q(connection());
    q.prepare(kProfileSelect + QStringLiteral(" WHERE id = ?"));
    q.addBindValue(id);
    if (q.exec() && q.next())
        return profileFromQuery(q);
    return std::nullopt;
}

qint64 LogDatabase::saveStationProfile(const StationProfile& p)
{
    QSqlDatabase db = connection();
    if (!db.transaction()) {
        m_lastError = db.lastError().text();
        return 0;
    }
    auto nullableInt = [](int v) { return v > 0 ? QVariant(v) : QVariant(); };
    auto nullableText = [](const QString& s) { return s.trimmed().isEmpty() ? QVariant() : QVariant(s.trimmed()); };
    const QString timestamp = nowIso();

    auto operatorId = [&]() -> qint64 {
        const QString callsign = p.operatorCall.trimmed().toUpper();
        if (callsign.isEmpty()) return 0;
        QSqlQuery normalized(db);
        normalized.prepare(QStringLiteral(
            "INSERT INTO operator_profile(callsign, display_name, updated_at) VALUES (?, ?, ?) "
            "ON CONFLICT(callsign) DO UPDATE SET display_name = excluded.display_name, updated_at = excluded.updated_at"));
        normalized.addBindValue(callsign);
        normalized.addBindValue(callsign);
        normalized.addBindValue(timestamp);
        if (!normalized.exec()) return 0;
        normalized.prepare(QStringLiteral("SELECT id FROM operator_profile WHERE callsign = ?"));
        normalized.addBindValue(callsign);
        return normalized.exec() && normalized.next() ? normalized.value(0).toLongLong() : 0;
    };
    auto locationId = [&]() -> qint64 {
        const QString key = p.operatingLocation.trimmed();
        if (key.isEmpty()) return 0;
        QSqlQuery normalized(db);
        normalized.prepare(QStringLiteral(
            "INSERT INTO operating_location(location_key, label, gridsquare, dxcc, updated_at) VALUES (?, ?, ?, ?, ?) "
            "ON CONFLICT(location_key) DO UPDATE SET label = excluded.label, gridsquare = excluded.gridsquare, "
            "dxcc = excluded.dxcc, updated_at = excluded.updated_at"));
        normalized.addBindValue(key);
        normalized.addBindValue(key);
        normalized.addBindValue(nullableText(p.myGridsquare));
        normalized.addBindValue(nullableInt(p.myDxcc));
        normalized.addBindValue(timestamp);
        if (!normalized.exec()) return 0;
        normalized.prepare(QStringLiteral("SELECT id FROM operating_location WHERE location_key = ?"));
        normalized.addBindValue(key);
        return normalized.exec() && normalized.next() ? normalized.value(0).toLongLong() : 0;
    };
    const qint64 normalizedOperatorId = operatorId();
    const qint64 normalizedLocationId = locationId();
    if ((!p.operatorCall.trimmed().isEmpty() && normalizedOperatorId <= 0)
        || (!p.operatingLocation.trimmed().isEmpty() && normalizedLocationId <= 0)) {
        m_lastError = QStringLiteral("cannot normalize station profile entities");
        db.rollback();
        return 0;
    }
    if (p.myDxcc > 0) {
        QSqlQuery normalized(db);
        normalized.prepare(QStringLiteral(
            "INSERT INTO dxcc_entity(id, name, updated_at) VALUES (?, '', ?) "
            "ON CONFLICT(id) DO UPDATE SET updated_at = excluded.updated_at"));
        normalized.addBindValue(p.myDxcc);
        normalized.addBindValue(timestamp);
        if (!normalized.exec()) {
            m_lastError = normalized.lastError().text();
            db.rollback();
            return 0;
        }
    }

    // Un solo profilo predefinito alla volta.
    if (p.isDefault) {
        QSqlQuery clear(db);
        clear.prepare(QStringLiteral(
            "UPDATE station_profile SET is_default = 0, dirty = 1, revision = revision + 1, updated_at = ? "
            "WHERE is_default = 1 AND id <> ?"));
        clear.addBindValue(nowIso());
        clear.addBindValue(p.id);
        clear.exec();
    }

    QSqlQuery q(db);
    if (p.id <= 0) {
        q.prepare(QStringLiteral(
            "INSERT INTO station_profile (uuid, name, station_callsign, operator, my_gridsquare, my_cq_zone, "
            "my_itu_zone, my_dxcc, my_rig, my_antenna, default_tx_pwr, lotw_station_loc, award_account, "
            "operating_location, valid_from, valid_to, operator_profile_id, operating_location_id, dxcc_entity_id, "
            "is_default, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
        const QString profileUuid = p.uuid.trimmed().isEmpty()
            ? QUuid::createUuid().toString(QUuid::WithoutBraces)
            : p.uuid.trimmed();
        q.addBindValue(profileUuid);
    } else {
        q.prepare(QStringLiteral(
            "UPDATE station_profile SET name = ?, station_callsign = ?, operator = ?, my_gridsquare = ?, "
            "my_cq_zone = ?, my_itu_zone = ?, my_dxcc = ?, my_rig = ?, my_antenna = ?, default_tx_pwr = ?, "
            "lotw_station_loc = ?, award_account = ?, operating_location = ?, valid_from = ?, valid_to = ?, "
            "operator_profile_id = ?, operating_location_id = ?, dxcc_entity_id = ?, is_default = ?, updated_at = ?, "
            "revision = revision + 1, dirty = 1 WHERE id = ?"));
    }
    q.addBindValue(p.name.trimmed());
    q.addBindValue(p.stationCallsign.trimmed().toUpper());
    q.addBindValue(nullableText(p.operatorCall.toUpper()));
    q.addBindValue(nullableText(p.myGridsquare));
    q.addBindValue(nullableInt(p.myCqZone));
    q.addBindValue(nullableInt(p.myItuZone));
    q.addBindValue(nullableInt(p.myDxcc));
    q.addBindValue(nullableText(p.myRig));
    q.addBindValue(nullableText(p.myAntenna));
    q.addBindValue(p.defaultTxPwr > 0 ? QVariant(p.defaultTxPwr) : QVariant());
    q.addBindValue(nullableText(p.lotwStationLocation));
    q.addBindValue(nullableText(p.awardAccount));
    q.addBindValue(nullableText(p.operatingLocation));
    q.addBindValue(nullableText(p.validFrom));
    q.addBindValue(nullableText(p.validTo));
    q.addBindValue(normalizedOperatorId > 0 ? QVariant(normalizedOperatorId) : QVariant());
    q.addBindValue(normalizedLocationId > 0 ? QVariant(normalizedLocationId) : QVariant());
    q.addBindValue(p.myDxcc > 0 ? QVariant(p.myDxcc) : QVariant());
    q.addBindValue(p.isDefault ? 1 : 0);
    q.addBindValue(nowIso());
    if (p.id > 0)
        q.addBindValue(p.id);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        db.rollback();
        return 0;
    }
    db.commit();
    return p.id > 0 ? p.id : q.lastInsertId().toLongLong();
}

bool LogDatabase::deleteStationProfile(qint64 id)
{
    // I QSO restano legati al profilo: cancellarlo non deve cambiare il log.
    QSqlQuery q(connection());
    q.prepare(QStringLiteral(
        "UPDATE station_profile SET deleted = 1, is_default = 0, dirty = 1, revision = revision + 1, "
        "updated_at = ? WHERE id = ?"));
    q.addBindValue(nowIso());
    q.addBindValue(id);
    return q.exec() && q.numRowsAffected() > 0;
}

qint64 LogDatabase::profileForCallsign(const QString& stationCallsign) const
{
    const QString c = stationCallsign.trimmed().toUpper();
    if (c.isEmpty())
        return 0;
    QSqlQuery q(connection());
    q.prepare(QStringLiteral(
        "SELECT id FROM station_profile WHERE deleted = 0 AND station_callsign = ? ORDER BY is_default DESC"));
    q.addBindValue(c);
    // Con piu' profili sullo stesso nominativo vince quello predefinito.
    if (q.exec() && q.next())
        return q.value(0).toLongLong();
    return 0;
}

qint64 LogDatabase::profileForIdentity(const QString& stationCallsign, const QString& operatorCall) const
{
    const QString station = stationCallsign.trimmed();
    if (!station.isEmpty()) {
        const qint64 byStation = profileForCallsign(station);
        if (byStation > 0)
            return byStation;
    }

    const QString op = operatorCall.trimmed();
    if (op.isEmpty())
        return 0;

    QSqlQuery q(connection());
    q.prepare(QStringLiteral(
        "SELECT id FROM station_profile WHERE deleted = 0 AND operator = ? COLLATE NOCASE "
        "ORDER BY id"));
    q.addBindValue(op.toUpper());
    if (!q.exec())
        return 0;

    qint64 only = 0;
    int matches = 0;
    while (q.next()) {
        only = q.value(0).toLongLong();
        ++matches;
    }
    return matches == 1 ? only : 0;
}

// ── Impostazioni e manutenzione ────────────────────────────────────────────────

QString LogDatabase::setting(const QString& key, const QString& fallback) const
{
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("SELECT value FROM app_setting WHERE key = ?"));
    q.addBindValue(key);
    if (q.exec() && q.next())
        return q.value(0).toString();
    return fallback;
}

void LogDatabase::setSetting(const QString& key, const QString& value)
{
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("INSERT INTO app_setting (key, value) VALUES (?, ?) "
                             "ON CONFLICT(key) DO UPDATE SET value = excluded.value"));
    q.addBindValue(key);
    q.addBindValue(value);
    q.exec();
}

bool LogDatabase::backupTo(const QString& filePath)
{
    // Scrivere prima in un temporaneo evita di perdere la copia precedente
    // se VACUUM o il filesystem falliscono. Il commit finale di QSaveFile e'
    // atomico sullo stesso filesystem.
    const QString temporary = filePath + QStringLiteral(".partial");
    QFile::remove(temporary);
    QSqlQuery q(connection());
    q.prepare(QStringLiteral("VACUUM INTO ?"));
    q.addBindValue(temporary);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        QFile::remove(temporary);
        return false;
    }

    QFile source(temporary);
    if (!source.open(QIODevice::ReadOnly)) {
        m_lastError = source.errorString();
        QFile::remove(temporary);
        return false;
    }
    QSaveFile committed(filePath);
    if (!committed.open(QIODevice::WriteOnly)) {
        m_lastError = committed.errorString();
        source.close();
        QFile::remove(temporary);
        return false;
    }
    while (!source.atEnd()) {
        const QByteArray block = source.read(1024 * 1024);
        if (block.isEmpty() && source.error() != QFileDevice::NoError) {
            m_lastError = source.errorString();
            committed.cancelWriting();
            source.close();
            QFile::remove(temporary);
            return false;
        }
        if (committed.write(block) != block.size()) {
            m_lastError = committed.errorString();
            committed.cancelWriting();
            source.close();
            QFile::remove(temporary);
            return false;
        }
    }
    source.close();
    if (!committed.commit()) {
        m_lastError = committed.errorString();
        QFile::remove(temporary);
        return false;
    }
    QFile::remove(temporary);
    return true;
}

} // namespace decolog::core
