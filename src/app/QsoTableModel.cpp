#include "app/QsoTableModel.h"
#include "../StartupTrace.h"

#include "core/Awards.h"
#include "core/Bands.h"
#include "core/Dates.h"
#include "core/LogDatabase.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QMetaObject>
#include <QPointer>

#include <algorithm>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QVariantMap>
#include <utility>

namespace decolog::app {

using core::LogDatabase;

namespace {

// Le chiavi delle colonne di sempre, nell'ordine dell'enum Column.
const QStringList& coreKeys()
{
    static const QStringList keys{
        QStringLiteral("utc"), QStringLiteral("call"), QStringLiteral("band"), QStringLiteral("freq"),
        QStringLiteral("mode"), QStringLiteral("rst_sent"), QStringLiteral("rst_rcvd"),
        QStringLiteral("grid"), QStringLiteral("name"), QStringLiteral("comment"), QStringLiteral("qth"),
        QStringLiteral("country"), QStringLiteral("state"), QStringLiteral("county"), QStringLiteral("cqz"),
        QStringLiteral("ituz"), QStringLiteral("iota"), QStringLiteral("dxcc"), QStringLiteral("qsl"),
        QStringLiteral("source"), QStringLiteral("tags")};
    return keys;
}

// Le altre colonne: il campo ADIF che mostrano e come si legge dal database.
// Quello che ha una colonna sua si legge da li'; il resto sta nei campi ADIF
// in piu' (adif_extra, in JSON); gli stati QSL nella tabella qsl_status.
struct ExtraColumn {
    const char* key;
    const char* field;      // il nome ADIF, come lo mostra Logger32 e gli altri
    const char* title;      // da tradurre
    int width;
    const char* sql;        // vuoto: json_extract(adif_extra, '$.CAMPO')
};

QString qslSql(const char* what, const char* service)
{
    return QStringLiteral("(SELECT IFNULL(%1, '') FROM qsl_status s WHERE s.qso_id = qso.id AND s.service = '%2')")
        .arg(QLatin1String(what), QLatin1String(service));
}

const QList<ExtraColumn>& extraColumns()
{
    static const QList<ExtraColumn> list{
        {"qso_date", "QSO_DATE", QT_TRANSLATE_NOOP("QsoTableModel", "Date"), 92, "substr(qso_datetime_on, 1, 10)"},
        {"time_on", "TIME_ON", QT_TRANSLATE_NOOP("QsoTableModel", "Time on"), 70, "substr(qso_datetime_on, 12, 5)"},
        {"time_off", "TIME_OFF", QT_TRANSLATE_NOOP("QsoTableModel", "Time off"), 70, "CASE WHEN IFNULL(qso_datetime_off, '') <> '' THEN substr(qso_datetime_off, 12, 5) "
         "WHEN json_extract(adif_extra, '$.TIME_OFF') IS NOT NULL "
         "THEN substr(json_extract(adif_extra, '$.TIME_OFF'), 1, 2) || ':' || substr(json_extract(adif_extra, '$.TIME_OFF'), 3, 2) "
         "ELSE '' END"},
        {"pfx", "PFX", QT_TRANSLATE_NOOP("QsoTableModel", "Prefix"), 60, ""},
        {"submode", "SUBMODE", QT_TRANSLATE_NOOP("QsoTableModel", "Submode"), 70, "IFNULL(submode, '')"},
        {"band_rx", "BAND_RX", QT_TRANSLATE_NOOP("QsoTableModel", "Band RX"), 60, "IFNULL(band_rx, '')"},
        {"freq_rx", "FREQ_RX", QT_TRANSLATE_NOOP("QsoTableModel", "Freq RX"), 96,
         "CASE WHEN freq_rx IS NULL THEN '' ELSE printf('%.6f', freq_rx) END"},
        {"cont", "CONT", QT_TRANSLATE_NOOP("QsoTableModel", "Continent"), 60, "IFNULL(cont, '')"},
        {"sota_ref", "SOTA_REF", QT_TRANSLATE_NOOP("QsoTableModel", "SOTA"), 90, "IFNULL(sota_ref, '')"},
        {"pota_ref", "POTA_REF", QT_TRANSLATE_NOOP("QsoTableModel", "POTA"), 80, "IFNULL(pota_ref, '')"},
        {"wwff_ref", "WWFF_REF", QT_TRANSLATE_NOOP("QsoTableModel", "WWFF"), 90, "IFNULL(wwff_ref, '')"},
        {"sig", "SIG", QT_TRANSLATE_NOOP("QsoTableModel", "Program"), 70, "IFNULL(sig, '')"},
        {"sig_info", "SIG_INFO", QT_TRANSLATE_NOOP("QsoTableModel", "Reference"), 90, "IFNULL(sig_info, '')"},
        {"notes", "NOTES", QT_TRANSLATE_NOOP("QsoTableModel", "Notes"), 180, "IFNULL(notes, '')"},
        {"prop_mode", "PROP_MODE", QT_TRANSLATE_NOOP("QsoTableModel", "Propagation"), 80, "IFNULL(prop_mode, '')"},
        {"sat_name", "SAT_NAME", QT_TRANSLATE_NOOP("QsoTableModel", "Satellite"), 80, "IFNULL(sat_name, '')"},
        {"sat_mode", "SAT_MODE", QT_TRANSLATE_NOOP("QsoTableModel", "Sat mode"), 70, "IFNULL(sat_mode, '')"},
        {"tx_pwr", "TX_PWR", QT_TRANSLATE_NOOP("QsoTableModel", "TX power"), 70,
         "CASE WHEN tx_pwr IS NULL THEN '' ELSE CAST(tx_pwr AS TEXT) END"},
        {"rx_pwr", "RX_PWR", QT_TRANSLATE_NOOP("QsoTableModel", "RX power"), 70, ""},
        {"station_callsign", "STATION_CALLSIGN", QT_TRANSLATE_NOOP("QsoTableModel", "Station"), 100,
         "COALESCE(json_extract(adif_extra, '$.STATION_CALLSIGN'), "
         "(SELECT station_callsign FROM station_profile p WHERE p.id = qso.station_profile_id), '')"},
        {"operator", "OPERATOR", QT_TRANSLATE_NOOP("QsoTableModel", "Operator"), 90,
         "COALESCE(json_extract(adif_extra, '$.OPERATOR'), "
         "(SELECT operator FROM station_profile p WHERE p.id = qso.station_profile_id), '')"},
        {"my_gridsquare", "MY_GRIDSQUARE", QT_TRANSLATE_NOOP("QsoTableModel", "My grid"), 74,
         "COALESCE(json_extract(adif_extra, '$.MY_GRIDSQUARE'), "
         "(SELECT my_gridsquare FROM station_profile p WHERE p.id = qso.station_profile_id), '')"},
        {"contest_id", "CONTEST_ID", QT_TRANSLATE_NOOP("QsoTableModel", "Contest"), 110, ""},
        {"stx", "STX", QT_TRANSLATE_NOOP("QsoTableModel", "Nr sent"), 60, ""},
        {"srx", "SRX", QT_TRANSLATE_NOOP("QsoTableModel", "Nr rcvd"), 60, ""},
        {"stx_string", "STX_STRING", QT_TRANSLATE_NOOP("QsoTableModel", "Exch sent"), 80, ""},
        {"srx_string", "SRX_STRING", QT_TRANSLATE_NOOP("QsoTableModel", "Exch rcvd"), 80, ""},
        {"arrl_sect", "ARRL_SECT", QT_TRANSLATE_NOOP("QsoTableModel", "ARRL section"), 70, ""},
        {"ten_ten", "TEN_TEN", QT_TRANSLATE_NOOP("QsoTableModel", "Ten-Ten"), 60, ""},
        {"qsl_via", "QSL_VIA", QT_TRANSLATE_NOOP("QsoTableModel", "QSL via"), 100, ""},
        {"qslmsg", "QSLMSG", QT_TRANSLATE_NOOP("QsoTableModel", "QSL message"), 160, ""},
        {"address", "ADDRESS", QT_TRANSLATE_NOOP("QsoTableModel", "Address"), 180, ""},
        {"email", "EMAIL", QT_TRANSLATE_NOOP("QsoTableModel", "Email"), 160, ""},
        {"distance", "DISTANCE", QT_TRANSLATE_NOOP("QsoTableModel", "Distance"), 70, ""},
        {"age", "AGE", QT_TRANSLATE_NOOP("QsoTableModel", "Age"), 50, ""},
        {"rig", "RIG", QT_TRANSLATE_NOOP("QsoTableModel", "His rig"), 120, ""},
        {"sfi", "SFI", QT_TRANSLATE_NOOP("QsoTableModel", "SFI"), 50, ""},
        {"k_index", "K_INDEX", QT_TRANSLATE_NOOP("QsoTableModel", "K"), 40, ""},
        {"a_index", "A_INDEX", QT_TRANSLATE_NOOP("QsoTableModel", "A"), 40, ""},
        {"qsl_sent", "QSL_SENT", QT_TRANSLATE_NOOP("QsoTableModel", "Card sent"), 60, nullptr},
        {"qsl_rcvd", "QSL_RCVD", QT_TRANSLATE_NOOP("QsoTableModel", "Card rcvd"), 60, nullptr},
        {"qslsdate", "QSLSDATE", QT_TRANSLATE_NOOP("QsoTableModel", "Card sent on"), 90, nullptr},
        {"qslrdate", "QSLRDATE", QT_TRANSLATE_NOOP("QsoTableModel", "Card rcvd on"), 90, nullptr},
        {"lotw_qsl_sent", "LOTW_QSL_SENT", QT_TRANSLATE_NOOP("QsoTableModel", "LoTW sent"), 60, nullptr},
        {"lotw_qsl_rcvd", "LOTW_QSL_RCVD", QT_TRANSLATE_NOOP("QsoTableModel", "LoTW rcvd"), 60, nullptr},
        {"eqsl_qsl_sent", "EQSL_QSL_SENT", QT_TRANSLATE_NOOP("QsoTableModel", "eQSL sent"), 60, nullptr},
        {"eqsl_qsl_rcvd", "EQSL_QSL_RCVD", QT_TRANSLATE_NOOP("QsoTableModel", "eQSL rcvd"), 60, nullptr},
        {"clublog_status", "CLUBLOG_QSO_UPLOAD_STATUS", QT_TRANSLATE_NOOP("QsoTableModel", "Club Log"), 60, nullptr},
        {"qrz_status", "QRZCOM_QSO_UPLOAD_STATUS", QT_TRANSLATE_NOOP("QsoTableModel", "QRZ"), 60, nullptr},
    };
    return list;
}

const ExtraColumn* extraColumn(const QString& key)
{
    for (const auto& c : extraColumns()) {
        if (key == QLatin1String(c.key))
            return &c;
    }
    return nullptr;
}

// L'espressione SQL di una colonna non di sempre. "x:CAMPO" e' un campo ADIF
// qualsiasi, quelli che il catalogo non conosce (i campi APP_ di un altro
// programma, per esempio).
QString extraSql(const QString& key)
{
    auto json = [](const QString& field) {
        QString safe = field;
        safe.remove(QLatin1Char('\''));
        return QStringLiteral("IFNULL(CAST(json_extract(adif_extra, '$.%1') AS TEXT), '')").arg(safe);
    };
    if (key.startsWith(QLatin1String("x:")))
        return json(key.mid(2).toUpper());
    const ExtraColumn* c = extraColumn(key);
    if (!c)
        return QStringLiteral("''");
    if (key == QLatin1String("qsl_sent")) return qslSql("sent", "card");
    if (key == QLatin1String("qsl_rcvd")) return qslSql("rcvd", "card");
    if (key == QLatin1String("qslsdate")) return qslSql("sent_date", "card");
    if (key == QLatin1String("qslrdate")) return qslSql("rcvd_date", "card");
    if (key == QLatin1String("lotw_qsl_sent")) return qslSql("sent", "lotw");
    if (key == QLatin1String("lotw_qsl_rcvd")) return qslSql("rcvd", "lotw");
    if (key == QLatin1String("eqsl_qsl_sent")) return qslSql("sent", "eqsl");
    if (key == QLatin1String("eqsl_qsl_rcvd")) return qslSql("rcvd", "eqsl");
    if (key == QLatin1String("clublog_status")) return qslSql("sent", "clublog");
    if (key == QLatin1String("qrz_status")) return qslSql("sent", "qrz");
    if (key == QLatin1String("pfx"))
        return json(QStringLiteral("PFX"));   // se manca, lo calcola rowFromQuery
    return c->sql && *c->sql ? QString::fromLatin1(c->sql) : json(QLatin1String(c->field));
}

// Una lettera per servizio, nell'ordine delle colonne L Q C E: 'c' confermato,
// 's' inviato o in coda, '-' niente.
QString qslCodes(const QString& summary)
{
    QString codes = QStringLiteral("----");
    static const QStringList order{QStringLiteral("lotw"), QStringLiteral("qrz"),
                                   QStringLiteral("clublog"), QStringLiteral("eqsl")};
    for (const QString& item : summary.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QStringList parts = item.split(QLatin1Char(':'));
        if (parts.size() != 3)
            continue;
        const qsizetype i = order.indexOf(parts.at(0));
        if (i < 0)
            continue;
        if (parts.at(2) == QLatin1String("Y"))
            codes[i] = QLatin1Char('c');
        else if (parts.at(1) != QLatin1String("N"))
            codes[i] = QLatin1Char('s');
    }
    return codes;
}

// I campi che si cercano per valore, oltre a quelli che hanno un filtro loro.
// `sql` e' l'espressione del valore (vuota per il prefisso WPX, che si conta a
// parte); `numeric` ordina i valori come numeri, `month` dal piu' recente.
struct FieldFilterDef {
    const char* key;
    const char* label;
    const char* sql;
    bool numeric;
    bool month;
};

QString upperRef(const char* column)
{
    return QStringLiteral("NULLIF(UPPER(TRIM(IFNULL(%1, ''))), '')").arg(QLatin1String(column));
}

QString jsonRef(const char* field)
{
    return QStringLiteral("NULLIF(UPPER(TRIM(IFNULL(CAST(json_extract(adif_extra, '$.%1') AS TEXT), ''))), '')")
        .arg(QLatin1String(field));
}

// Il mese di una data della QSL ("20260922" o "2026-09-22") come "202609".
QString qslMonth(const char* date, const char* service)
{
    return QStringLiteral("(SELECT NULLIF(substr(REPLACE(IFNULL(s.%1, ''), '-', ''), 1, 6), '') FROM qsl_status s "
                          "WHERE s.qso_id = qso.id AND s.service = '%2')")
        .arg(QLatin1String(date), QLatin1String(service));
}

const QList<FieldFilterDef>& fieldFilterDefs()
{
    static const QList<FieldFilterDef> list{
        {"cqz", QT_TRANSLATE_NOOP("QsoTableModel", "CQ zone (WAZ)"), "NULLIF(cqz, 0)", true, false},
        {"ituz", QT_TRANSLATE_NOOP("QsoTableModel", "ITU zone"), "NULLIF(ituz, 0)", true, false},
        {"cont", QT_TRANSLATE_NOOP("QsoTableModel", "Continent (WAC)"), "", false, false},
        {"pfx", QT_TRANSLATE_NOOP("QsoTableModel", "WPX prefix"), "", false, false},
        {"qth", QT_TRANSLATE_NOOP("QsoTableModel", "QTH"), "", false, false},
        {"state", QT_TRANSLATE_NOOP("QsoTableModel", "State / province"), "", false, false},
        {"iota", QT_TRANSLATE_NOOP("QsoTableModel", "IOTA"), "", false, false},
        {"pota_ref", QT_TRANSLATE_NOOP("QsoTableModel", "POTA"), "", false, false},
        {"sota_ref", QT_TRANSLATE_NOOP("QsoTableModel", "SOTA"), "", false, false},
        {"wwff_ref", QT_TRANSLATE_NOOP("QsoTableModel", "WWFF"), "", false, false},
        {"sig_info", QT_TRANSLATE_NOOP("QsoTableModel", "Other award reference (SIG)"), "", false, false},
        {"qsl_via", QT_TRANSLATE_NOOP("QsoTableModel", "QSL manager"), "", false, false},
        {"prop_mode", QT_TRANSLATE_NOOP("QsoTableModel", "Propagation"), "", false, false},
        {"sat_name", QT_TRANSLATE_NOOP("QsoTableModel", "Satellite"), "", false, false},
        {"contest_id", QT_TRANSLATE_NOOP("QsoTableModel", "Contest"), "", false, false},
        {"card_sent", QT_TRANSLATE_NOOP("QsoTableModel", "Card sent (month)"), "", false, true},
        {"card_rcvd", QT_TRANSLATE_NOOP("QsoTableModel", "Card received (month)"), "", false, true},
        {"lotw_rcvd", QT_TRANSLATE_NOOP("QsoTableModel", "LoTW confirmation (month)"), "", false, true},
    };
    return list;
}

const FieldFilterDef* fieldFilterDef(const QString& key)
{
    for (const FieldFilterDef& d : fieldFilterDefs()) {
        if (key == QLatin1String(d.key))
            return &d;
    }
    return nullptr;
}

// L'espressione SQL del valore di un campo: lo stesso testo per contare i
// valori e per filtrare, cosi' i conti tornano con le righe.
QString fieldSql(const QString& key)
{
    if (key == QLatin1String("cont")) return upperRef("cont");
    if (key == QLatin1String("qth")) return QStringLiteral("NULLIF(TRIM(IFNULL(qth, '')), '')");
    if (key == QLatin1String("state")) return upperRef("state");
    if (key == QLatin1String("iota")) return upperRef("iota");
    if (key == QLatin1String("pota_ref")) return upperRef("pota_ref");
    if (key == QLatin1String("sota_ref")) return upperRef("sota_ref");
    if (key == QLatin1String("wwff_ref")) return upperRef("wwff_ref");
    if (key == QLatin1String("sig_info"))
        return QStringLiteral("NULLIF(UPPER(TRIM(IFNULL(sig, '') || ' ' || IFNULL(sig_info, ''))), '')");
    if (key == QLatin1String("qsl_via")) return jsonRef("QSL_VIA");
    if (key == QLatin1String("prop_mode")) return upperRef("prop_mode");
    if (key == QLatin1String("sat_name")) return upperRef("sat_name");
    if (key == QLatin1String("contest_id")) return jsonRef("CONTEST_ID");
    if (key == QLatin1String("card_sent")) return qslMonth("sent_date", "card");
    if (key == QLatin1String("card_rcvd")) return qslMonth("rcvd_date", "card");
    if (key == QLatin1String("lotw_rcvd")) return qslMonth("rcvd_date", "lotw");
    const FieldFilterDef* d = fieldFilterDef(key);
    return d && d->sql && *d->sql ? QString::fromLatin1(d->sql) : QString();
}

// Il prefisso WPX come lo conta il filtro: quello scritto nel QSO (PFX) o, se
// manca, il nominativo fino all'ultima cifra seguita solo da lettere, senza i
// suffissi da portatile (/P, /M, /MM, /AM, /QRP). Chi ha una barra diversa
// (EA8/IU8LMC, W1AW/4) senza PFX scritto resta fuori: lo stesso fa l'SQL qui
// sotto, cosi' i conti del menu e le righe trovate coincidono.
const QStringList& portableSuffixes()
{
    static const QStringList s{QStringLiteral("/QRP"), QStringLiteral("/MM"), QStringLiteral("/AM"),
                               QStringLiteral("/P"), QStringLiteral("/M")};
    return s;
}

QString filterPrefix(const QString& callsign, const QString& stored)
{
    const QString pfx = stored.trimmed().toUpper();
    if (!pfx.isEmpty())
        return pfx;
    QString call = callsign.trimmed().toUpper();
    for (const QString& suffix : portableSuffixes()) {
        if (call.endsWith(suffix)) {
            call.chop(suffix.size());
            break;
        }
    }
    if (call.contains(QLatin1Char('/')))
        return {};
    qsizetype last = -1;
    for (qsizetype i = 0; i < call.size(); ++i) {
        if (call.at(i).isDigit())
            last = i;
    }
    if (last < 0 || last == call.size() - 1)
        return {};
    return call.left(last + 1);
}

QString baseCallSql()
{
    QString sql = QStringLiteral("UPPER(TRIM(call))");
    QString cases;
    for (const QString& suffix : portableSuffixes())
        cases += QStringLiteral(" WHEN UPPER(TRIM(call)) LIKE '%%1' THEN substr(UPPER(TRIM(call)), 1, length(TRIM(call)) - %2)")
                     .arg(suffix).arg(suffix.size());
    return QStringLiteral("(CASE%1 ELSE %2 END)").arg(cases, sql);
}

// Righe per pagina, e pagine tenute: diecimila righe in memoria al massimo,
// piu' di quante ne mostri qualunque schermo.
constexpr int kPage = 200;
constexpr int kPagesKept = 50;
// Non ancora contato (la conta gira su un altro filo).
constexpr quint16 kNoCategory = 0xFFFF;

} // namespace

QsoTableModel::QsoTableModel(LogDatabase* db, QObject* parent)
    : QAbstractTableModel(parent)
    , m_db(db)
    , m_layout(coreKeys())
{
    m_reloadPool.setMaxThreadCount(1);
    rebuildSlots();
    reload();
}

int QsoTableModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_ids.size());
}

void QsoTableModel::dropPages()
{
    m_pages.clear();
    m_pageUse.clear();
}

const QsoTableModel::Row* QsoTableModel::rowAt(int row) const
{
    if (row < 0 || row >= m_ids.size() || !m_db || !m_db->isOpen())
        return nullptr;
    const int page = row / kPage;
    auto it = m_pages.find(page);
    if (it == m_pages.end()) {
        // La pagina intera in una query, nell'ordine della tabella.
        const qsizetype from = static_cast<qsizetype>(page) * kPage;
        const qsizetype n = qMin<qsizetype>(kPage, m_ids.size() - from);
        QStringList marks;
        for (qsizetype i = 0; i < n; ++i)
            marks << QStringLiteral("?");
        QHash<qint64, Row> read;
        QSqlQuery q(m_db->connection());
        q.setForwardOnly(true);
        q.prepare(selectSql(QStringLiteral("AND id IN (%1)").arg(marks.join(QLatin1Char(',')))));
        for (qsizetype i = 0; i < n; ++i)
            q.addBindValue(m_ids.at(from + i));
        if (q.exec()) {
            while (q.next()) {
                Row r = rowFromQuery(q);
                read.insert(r.id, r);
            }
        }
        QVector<Row> rows;
        rows.reserve(n);
        for (qsizetype i = 0; i < n; ++i) {
            const qint64 id = m_ids.at(from + i);
            Row r = read.value(id);
            r.id = id;
            rows << r;
        }
        if (m_pages.size() >= kPagesKept && !m_pageUse.isEmpty())
            m_pages.remove(m_pageUse.takeFirst());
        it = m_pages.insert(page, rows);
        m_pageUse << page;
    } else if (m_pageUse.isEmpty() || m_pageUse.last() != page) {
        m_pageUse.removeOne(page);
        m_pageUse << page;
    }
    const qsizetype at = row - static_cast<qsizetype>(page) * kPage;
    return at < it->size() ? &it->at(at) : nullptr;
}

int QsoTableModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_layout.size());
}

QVariant QsoTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_ids.size())
        return {};
    const qint64 id = m_ids.at(index.row());
    switch (role) {
    case IdRole:
        return id;
    case ColumnKeyRole:
        return columnKey(index.column());
    case IsNewRole:
        return id == m_freshId;
    case CategoryRole: {
        const quint16 firsts = m_category.value(id, kNoCategory);
        if (firsts == kNoCategory)
            return QString();
        const Row* row = rowAt(index.row());
        return categoryFor(firsts, row ? row->qsl : quint8(QslNone));
    }
    case Qt::DisplayRole: {
        const Row* row = rowAt(index.row());
        if (!row)
            return {};
        const Slot s = m_slots.value(index.column());
        if (s.core >= 0)
            return row->values[s.core];
        return s.extra >= 0 ? row->extra.value(s.extra) : QString();
    }
    case ModeRole: {
        const Row* row = rowAt(index.row());
        return row ? row->values[Mode] : QString();
    }
    case QslStateRole: {
        const Row* row = rowAt(index.row());
        return row ? int(row->qsl) : 0;
    }
    default:
        return {};
    }
}

QVariant QsoTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return {};
    return columnTitle(section);
}

QHash<int, QByteArray> QsoTableModel::roleNames() const
{
    return {
        {Qt::DisplayRole, "display"},
        {IdRole, "qsoId"},
        {ColumnKeyRole, "columnKey"},
        {IsNewRole, "isNew"},
        {ModeRole, "modeName"},
        {CategoryRole, "rowCategory"},
        {QslStateRole, "qslState"},
    };
}

QString QsoTableModel::columnKey(int column) const
{
    return m_layout.value(column);
}

namespace {
QString coreTitle(int column)
{
    switch (column) {
    case QsoTableModel::Utc:     return QsoTableModel::tr("UTC");
    case QsoTableModel::Call:    return QsoTableModel::tr("Call");
    case QsoTableModel::Band:    return QsoTableModel::tr("Band");
    case QsoTableModel::Freq:    return QsoTableModel::tr("Freq");
    case QsoTableModel::Mode:    return QsoTableModel::tr("Mode");
    case QsoTableModel::RstSent: return QsoTableModel::tr("S");
    case QsoTableModel::RstRcvd: return QsoTableModel::tr("R");
    case QsoTableModel::Grid:    return QsoTableModel::tr("Grid");
    case QsoTableModel::Name:    return QsoTableModel::tr("Name");
    case QsoTableModel::Qth:     return QsoTableModel::tr("City / QTH");
    case QsoTableModel::Country: return QsoTableModel::tr("Country");
    case QsoTableModel::State:   return QsoTableModel::tr("State");
    case QsoTableModel::County:  return QsoTableModel::tr("County");
    case QsoTableModel::Cqz:     return QsoTableModel::tr("CQ");
    case QsoTableModel::Ituz:    return QsoTableModel::tr("ITU");
    case QsoTableModel::Iota:    return QsoTableModel::tr("IOTA");
    case QsoTableModel::Dxcc:    return QsoTableModel::tr("DXCC");
    case QsoTableModel::Qsl:     return QsoTableModel::tr("QSL");
    case QsoTableModel::Source:  return QsoTableModel::tr("Src");
    case QsoTableModel::Tags:    return QsoTableModel::tr("Tags");
    // Il COMMENT dell'ADIF: quello che gli altri programmi chiamano commento.
    // Le etichette sono un'altra cosa, di DecoDXLog.
    case QsoTableModel::Comment: return QsoTableModel::tr("Comment");
    default:                     return {};
    }
}

int coreWidth(int column)
{
    switch (column) {
    case QsoTableModel::Utc:     return 132;
    case QsoTableModel::Call:    return 118;
    case QsoTableModel::Band:    return 56;
    case QsoTableModel::Freq:    return 96;
    case QsoTableModel::Mode:    return 60;
    case QsoTableModel::RstSent:
    case QsoTableModel::RstRcvd: return 46;
    case QsoTableModel::Grid:    return 74;
    case QsoTableModel::Name:    return 140;
    case QsoTableModel::Comment: return 180;
    case QsoTableModel::Qth:     return 140;
    case QsoTableModel::Country: return 130;
    case QsoTableModel::State:   return 52;
    case QsoTableModel::County:  return 110;
    case QsoTableModel::Cqz:
    case QsoTableModel::Ituz:    return 44;
    case QsoTableModel::Iota:    return 64;
    case QsoTableModel::Dxcc:    return 56;
    case QsoTableModel::Qsl:     return 84;
    case QsoTableModel::Source:  return 52;
    case QsoTableModel::Tags:    return 130;
    default:                     return 80;
    }
}
} // namespace

QString QsoTableModel::titleOf(const QString& key) const
{
    const qsizetype core = coreKeys().indexOf(key);
    if (core >= 0)
        return coreTitle(static_cast<int>(core));
    if (key.startsWith(QLatin1String("x:")))
        return key.mid(2).toUpper();
    if (const ExtraColumn* c = extraColumn(key))
        return QCoreApplication::translate("QsoTableModel", c->title);
    return key;
}

QString QsoTableModel::columnTitle(int column) const
{
    return titleOf(m_layout.value(column));
}

int QsoTableModel::columnWidthHint(int column) const
{
    const QString key = m_layout.value(column);
    const qsizetype core = coreKeys().indexOf(key);
    if (core >= 0)
        return coreWidth(static_cast<int>(core));
    if (const ExtraColumn* c = extraColumn(key))
        return c->width;
    return 100;
}

QStringList QsoTableModel::defaultLayout()
{
    return coreKeys();
}

QVariantList QsoTableModel::availableColumns() const
{
    QVariantList out;
    static const QStringList coreFields{
        QStringLiteral("QSO_DATE+TIME_ON"), QStringLiteral("CALL"), QStringLiteral("BAND"), QStringLiteral("FREQ"),
        QStringLiteral("MODE"), QStringLiteral("RST_SENT"), QStringLiteral("RST_RCVD"), QStringLiteral("GRIDSQUARE"),
        QStringLiteral("NAME"), QStringLiteral("COMMENT"), QStringLiteral("QTH"), QStringLiteral("COUNTRY"),
        QStringLiteral("STATE"), QStringLiteral("CNTY"), QStringLiteral("CQZ"), QStringLiteral("ITUZ"),
        QStringLiteral("IOTA"), QStringLiteral("DXCC"), QStringLiteral("L Q C E"), QString(),
        QStringLiteral("APP_DECOLOG_TAGS")};
    for (qsizetype i = 0; i < coreKeys().size(); ++i) {
        out << QVariantMap{{QStringLiteral("key"), coreKeys().at(i)},
                           {QStringLiteral("title"), coreTitle(static_cast<int>(i))},
                           {QStringLiteral("field"), coreFields.value(i)}};
    }
    for (const auto& c : extraColumns()) {
        out << QVariantMap{{QStringLiteral("key"), QLatin1String(c.key)},
                           {QStringLiteral("title"), QCoreApplication::translate("QsoTableModel", c.title)},
                           {QStringLiteral("field"), QLatin1String(c.field)}};
    }
    // I campi ADIF qualsiasi che l'operatore ha aggiunto.
    for (const QString& key : m_layout) {
        if (key.startsWith(QLatin1String("x:")))
            out << QVariantMap{{QStringLiteral("key"), key},
                               {QStringLiteral("title"), key.mid(2).toUpper()},
                               {QStringLiteral("field"), key.mid(2).toUpper()}};
    }
    return out;
}

void QsoTableModel::setColumnLayout(const QStringList& keys)
{
    QStringList clean;
    for (const QString& raw : keys) {
        const QString key = raw.trimmed();
        const bool known = coreKeys().contains(key) || extraColumn(key)
                           || (key.startsWith(QLatin1String("x:")) && key.size() > 2);
        if (known && !clean.contains(key))
            clean << key;
    }
    // Il nominativo non si toglie: senza, una riga non dice niente.
    if (!clean.contains(QStringLiteral("call")))
        clean.prepend(QStringLiteral("call"));
    if (clean == m_layout)
        return;
    const QStringList oldExtra = m_extra;
    m_layout = clean;
    m_extra.clear();
    for (const QString& key : m_layout) {
        if (!coreKeys().contains(key))
            m_extra << key;
    }
    rebuildSlots();
    // Le righe sono le stesse: si rileggono le pagine con le colonne nuove.
    beginResetModel();
    if (m_extra != oldExtra)
        dropPages();
    endResetModel();
    emit layoutChanged();
}

void QsoTableModel::rebuildSlots()
{
    m_slots.clear();
    for (const QString& key : m_layout) {
        Slot s;
        s.core = static_cast<int>(coreKeys().indexOf(key));
        if (s.core < 0)
            s.extra = static_cast<int>(m_extra.indexOf(key));
        m_slots << s;
    }
}

void QsoTableModel::sortBy(const QString& key)
{
    if (key.isEmpty() || key == QLatin1String("qsl"))
        return;
    // Stessa colonna: si gira. Colonna nuova: l'ora dal piu' recente, il resto
    // dalla A alla Z (o dal piu' piccolo).
    const bool ascending = key == m_sortKey ? !m_sortAscending : key != QLatin1String("utc");
    setSort(key, ascending);
}

void QsoTableModel::setSort(const QString& key, bool ascending)
{
    const QString clean = key.isEmpty() ? QStringLiteral("utc") : key;
    if (clean == m_sortKey && ascending == m_sortAscending)
        return;
    m_sortKey = clean;
    m_sortAscending = ascending;
    reload();
    emit sortChanged();
}

namespace {

// Come si ordina una colonna in SQL: l'espressione, e se va letta come numero.
struct SortColumn {
    QString expr;
    bool numeric{false};
};

SortColumn sortColumn(const QString& key)
{
    static const QHash<QString, SortColumn> core{
        {QStringLiteral("call"), {QStringLiteral("call"), false}},
        {QStringLiteral("band"), {QStringLiteral("band"), false}},
        {QStringLiteral("freq"), {QStringLiteral("freq"), true}},
        {QStringLiteral("mode"),
         {QStringLiteral("(CASE WHEN IFNULL(submode, '') = '' OR mode = 'SSB' THEN mode ELSE submode END)"), false}},
        {QStringLiteral("rst_sent"), {QStringLiteral("rst_sent"), true}},
        {QStringLiteral("rst_rcvd"), {QStringLiteral("rst_rcvd"), true}},
        {QStringLiteral("grid"), {QStringLiteral("gridsquare"), false}},
        {QStringLiteral("name"), {QStringLiteral("name"), false}},
        {QStringLiteral("comment"), {QStringLiteral("comment"), false}},
        {QStringLiteral("qth"), {QStringLiteral("qth"), false}},
        {QStringLiteral("country"), {QStringLiteral("country"), false}},
        {QStringLiteral("state"), {QStringLiteral("state"), false}},
        {QStringLiteral("county"), {QStringLiteral("cnty"), false}},
        {QStringLiteral("cqz"), {QStringLiteral("NULLIF(cqz, 0)"), true}},
        {QStringLiteral("ituz"), {QStringLiteral("NULLIF(ituz, 0)"), true}},
        {QStringLiteral("iota"), {QStringLiteral("iota"), false}},
        {QStringLiteral("dxcc"), {QStringLiteral("dxcc"), true}},
        {QStringLiteral("source"), {QStringLiteral("source"), false}},
        {QStringLiteral("tags"), {QStringLiteral("tags"), false}},
    };
    const auto it = core.constFind(key);
    if (it != core.constEnd())
        return *it;
    static const QSet<QString> numericExtras{
        QStringLiteral("freq_rx"), QStringLiteral("tx_pwr"), QStringLiteral("rx_pwr"), QStringLiteral("stx"),
        QStringLiteral("srx"), QStringLiteral("age"), QStringLiteral("distance"), QStringLiteral("sfi"),
        QStringLiteral("k_index"), QStringLiteral("a_index")};
    return {extraSql(key), numericExtras.contains(key)};
}

} // namespace

QString QsoTableModel::orderSql() const
{
    // A pari merito, il piu' recente prima.
    const QString tie = QStringLiteral("qso_datetime_on DESC, id DESC");
    if (m_sortKey == QLatin1String("utc"))
        return m_sortAscending ? QStringLiteral("qso_datetime_on ASC, id ASC") : tie;
    const QString dir = m_sortAscending ? QStringLiteral("ASC") : QStringLiteral("DESC");
    // Le bande nell'ordine delle frequenze, non dell'alfabeto.
    if (m_sortKey == QLatin1String("band")) {
        QString cases;
        const QStringList order = core::bands::all();
        for (qsizetype i = 0; i < order.size(); ++i)
            cases += QStringLiteral(" WHEN '%1' THEN %2").arg(order.at(i)).arg(i);
        return QStringLiteral("IFNULL(band, '') = '' ASC, (CASE LOWER(band)%1 ELSE 1000000 END) %2, %3")
            .arg(cases, dir, tie);
    }
    const SortColumn c = sortColumn(m_sortKey);
    // I vuoti in fondo, in tutti e due i versi; i numeri come numeri, il
    // resto senza badare alle maiuscole.
    const QString empty = QStringLiteral("(%1 IS NULL OR TRIM(CAST(%1 AS TEXT)) = '') ASC").arg(c.expr);
    return c.numeric ? QStringLiteral("%1, CAST(%2 AS REAL) %3, %4").arg(empty, c.expr, dir, tie)
                     : QStringLiteral("%1, UPPER(%2) %3, %4").arg(empty, c.expr, dir, tie);
}

QString QsoTableModel::valueFor(int row, const QString& key) const
{
    const Row* r = rowAt(row);
    if (!r)
        return {};
    const qsizetype core = coreKeys().indexOf(key);
    if (core >= 0)
        return r->values[core];
    const qsizetype extra = m_extra.indexOf(key);
    return extra >= 0 ? r->extra.value(extra) : QString();
}

QStringList QsoTableModel::categoryKeys()
{
    // In ordine di importanza: vince la prima che vale.
    static const QStringList keys{
        QStringLiteral("colorNewDxcc"), QStringLiteral("colorNewDxccBand"),
        QStringLiteral("colorNewContinent"), QStringLiteral("colorNewContinentBand"),
        QStringLiteral("colorNewCqZone"), QStringLiteral("colorNewCqZoneBand"),
        QStringLiteral("colorNewItuZone"), QStringLiteral("colorNewItuZoneBand"),
        QStringLiteral("colorNewGrid"), QStringLiteral("colorNewGridBand"),
        QStringLiteral("colorNewCall"), QStringLiteral("colorNewCallBand"),
        QStringLiteral("colorLotwConfirmed"), QStringLiteral("colorCardConfirmed"),
        QStringLiteral("colorEqslConfirmed"), QStringLiteral("colorB4")};
    return keys;
}

void QsoTableModel::setActiveCategories(const QStringList& keys)
{
    const QSet<QString> next(keys.cbegin(), keys.cend());
    if (m_activeChosen && next == m_activeCategories)
        return;
    m_activeCategories = next;
    m_activeChosen = true;
    if (rowCount() > 0)
        emit dataChanged(index(0, 0), index(rowCount() - 1, columns() - 1), {CategoryRole});
}

QString QsoTableModel::categoryFor(quint16 firsts, quint8 qsl) const
{
    const QStringList& keys = categoryKeys();
    auto active = [this](const QString& key) { return !m_activeChosen || m_activeCategories.contains(key); };
    // Prima le cose nuove, nell'ordine di importanza; poi la conferma, LoTW
    // prima della cartolina e la cartolina prima di eQSL; poi il gia' lavorato.
    // Una categoria col colore spento si salta: prima la riga restava della
    // prima che le spettava anche spenta, e "Confermato su LoTW" non si vedeva
    // quasi mai (quasi ogni QSO e' un nominativo nuovo sulla banda).
    for (int i = 0; i < 12; ++i) {
        if ((firsts & (1u << i)) && active(keys.at(i)))
            return keys.at(i);
    }
    const QString confirmed = qsl == QslLotwConfirmed ? keys.at(12)
                            : qsl == QslCardConfirmed ? keys.at(13)
                            : qsl == QslOtherConfirmed ? keys.at(14)
                                                       : QString();
    if (!confirmed.isEmpty() && active(confirmed))
        return confirmed;
    if (firsts == 0 && active(keys.at(15)))
        return keys.at(15);
    return {};
}

namespace {

// Le cose nuove di ogni QSO, in un giro solo del log in ordine di tempo: una
// cosa mai vista (entita', entita' sulla banda, continente…) e' un bit. Le
// chiavi "gia' viste" si tengono come impronte a 64 bit, non come testo: su
// un milione di QSO sono trenta megabyte invece di centocinquanta.
quint16 categoryFrom(const QSqlQuery& q)
{
    // Le colonne 1..12 sono i "primi"; nessuno = gia' lavorato.
    quint16 firsts = 0;
    for (int i = 0; i < 12; ++i) {
        if (!q.value(1 + i).isNull() && q.value(1 + i).toLongLong() == 1)
            firsts |= quint16(1u << i);
    }
    return firsts;
}

// Le dodici chiavi di un QSO: entita', continente, zone, locatore e
// nominativo, da soli e sulla banda. 0 = non si sa, non conta.
std::array<quint64, 12> categoryParts(int dxcc, const QString& band, const QString& cont, int cqz, int ituz,
                                      const QString& grid, const QString& call)
{
    const auto h = [](const QString& text) { return static_cast<quint64>(qHash(text, 0x5eed)) | 1; };
    const QString slot = QLatin1Char('|') + band;
    std::array<quint64, 12> parts{};
    if (dxcc > 0) {
        parts[0] = h(QStringLiteral("d") + QString::number(dxcc));
        parts[1] = h(QStringLiteral("d") + QString::number(dxcc) + slot);
    }
    if (!cont.isEmpty()) {
        parts[2] = h(QStringLiteral("c") + cont);
        parts[3] = h(QStringLiteral("c") + cont + slot);
    }
    if (cqz > 0) {
        parts[4] = h(QStringLiteral("z") + QString::number(cqz));
        parts[5] = h(QStringLiteral("z") + QString::number(cqz) + slot);
    }
    if (ituz > 0) {
        parts[6] = h(QStringLiteral("i") + QString::number(ituz));
        parts[7] = h(QStringLiteral("i") + QString::number(ituz) + slot);
    }
    if (grid.size() == 4) {
        parts[8] = h(QStringLiteral("g") + grid);
        parts[9] = h(QStringLiteral("g") + grid + slot);
    }
    parts[10] = h(QStringLiteral("k") + call);
    parts[11] = h(QStringLiteral("k") + call + slot);
    return parts;
}

// La conta di tutto il log. Gira anche su un altro filo, con la sua
// connessione: non tocca niente del modello.
QsoTableModel::CategoryPass countCategories(const QSqlDatabase& db)
{
    QsoTableModel::CategoryPass pass;

    // Il log si legge in ordine di tabella, che e' la lettura veloce, e si
    // mette in ordine di tempo qui: leggerlo gia' in ordine di tempo vuol dire
    // saltare da una pagina all'altra del file, quattro volte piu' lento.
    struct Item {
        QString on;
        qint64 id{0};
        std::array<quint64, 12> parts{};
    };
    QVector<Item> items;
    QSqlQuery q(db);
    q.setForwardOnly(true);
    if (!q.exec(QStringLiteral("SELECT id, IFNULL(dxcc, 0), band, IFNULL(cont, ''), IFNULL(cqz, 0), IFNULL(ituz, 0), "
                               "UPPER(SUBSTR(IFNULL(gridsquare, ''), 1, 4)), call, qso_datetime_on "
                               "FROM qso NOT INDEXED WHERE deleted = 0")))
        return pass;
    while (q.next()) {
        Item it;
        it.id = q.value(0).toLongLong();
        it.on = q.value(8).toString();
        it.parts = categoryParts(q.value(1).toInt(), q.value(2).toString(), q.value(3).toString(), q.value(4).toInt(),
                                 q.value(5).toInt(), q.value(6).toString(), q.value(7).toString());
        items << it;
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        return a.on != b.on ? a.on < b.on : a.id < b.id;
    });
    pass.category.reserve(items.size());
    for (const Item& it : std::as_const(items)) {
        quint16 firsts = 0;
        for (int i = 0; i < 12; ++i) {
            if (!it.parts[i] || pass.seen[i].contains(it.parts[i]))
                continue;
            pass.seen[i].insert(it.parts[i]);
            firsts |= quint16(1u << i);
        }
        pass.category.insert(it.id, firsts);
        pass.lastOn = it.on;
        pass.lastId = it.id;
    }
    pass.valid = true;
    return pass;
}

} // namespace

void QsoTableModel::adoptCategories(CategoryPass&& pass)
{
    m_category = std::move(pass.category);
    for (int i = 0; i < 12; ++i)
        m_seen[i] = std::move(pass.seen[i]);
    m_seenLastOn = pass.lastOn;
    m_seenLastId = pass.lastId;
    m_seenValid = pass.valid;
}

void QsoTableModel::computeCategories()
{
    decolog::StartupSpan trace("QsoTableModel::computeCategories");
    if (!m_db || !m_db->isOpen()) {
        m_category.clear();
        m_categorySignature.clear();
        return;
    }
    // I filtri ricaricano la tabella spesso; le categorie cambiano solo se
    // cambia il log. Si contano di nuovo solo allora. Il segno di un cambio
    // non si conta nel log: su un milione di QSO il MAX(updated_at) di prima
    // costava quasi mezzo secondo a ogni filtro.
    const QString signature = m_db->changeStamp();
    if (!signature.isEmpty() && signature == m_categorySignature)
        return;
    m_categorySignature = signature;

    const QString path = m_db->path();
    if (path.isEmpty() || path == QLatin1String(":memory:")) {
        adoptCategories(countCategories(m_db->connection()));
        return;
    }
    // Su un file si conta su un altro filo: la tabella si vede subito, i
    // colori arrivano appena pronti.
    const int generation = ++m_categoryGeneration;
    m_seenValid = false;
    m_categoryRunning = true;
    m_categoryLater.clear();
    QPointer<QsoTableModel> self(this);
    m_categoryPool.start([self, path, generation] {
        CategoryPass pass;
        {
            core::LogDatabase db;
            if (db.open(path))
                pass = countCategories(db.connection());
        }
        QMetaObject::invokeMethod(
            self.data(),
            [self, generation, pass = std::move(pass)]() mutable {
                if (!self || generation != self->m_categoryGeneration)
                    return;
                self->m_categoryRunning = false;
                self->adoptCategories(std::move(pass));
                // I QSO arrivati mentre si contava: adesso si sa cosa hanno portato.
                const QList<qint64> later = std::exchange(self->m_categoryLater, {});
                for (qint64 id : later)
                    self->m_category.insert(id, self->categoryOf(id));
                if (self->rowCount() > 0)
                    emit self->dataChanged(self->index(0, 0), self->index(self->rowCount() - 1, self->columns() - 1),
                                           {CategoryRole});
            },
            Qt::QueuedConnection);
    });
}

quint16 QsoTableModel::categoryOf(qint64 id) const
{
    if (!m_db || !m_db->isOpen())
        return kNoCategory;
    // La conta di tutto il log sta ancora girando: il QSO si guarda quando finisce.
    if (m_categoryRunning) {
        m_categoryLater << id;
        return kNoCategory;
    }
    // Il QSO appena fatto e' quasi sempre il piu' recente: allora basta
    // guardare cosa si e' gia' visto, senza contare di nuovo il log (le zone e
    // i continenti non hanno indici, e su un log grande il conto costava un
    // quinto di secondo a ogni QSO).
    if (m_seenValid) {
        QSqlQuery one(m_db->connection());
        one.prepare(QStringLiteral("SELECT IFNULL(dxcc, 0), band, IFNULL(cont, ''), IFNULL(cqz, 0), IFNULL(ituz, 0), "
                                   "UPPER(SUBSTR(IFNULL(gridsquare, ''), 1, 4)), call, qso_datetime_on "
                                   "FROM qso WHERE id = ?"));
        one.addBindValue(id);
        if (one.exec() && one.next()) {
            const QString on = one.value(7).toString();
            if (on > m_seenLastOn || (on == m_seenLastOn && id > m_seenLastId)) {
                const std::array<quint64, 12> parts =
                    categoryParts(one.value(0).toInt(), one.value(1).toString(), one.value(2).toString(),
                                  one.value(3).toInt(), one.value(4).toInt(), one.value(5).toString(),
                                  one.value(6).toString());
                quint16 firsts = 0;
                for (int i = 0; i < 12; ++i) {
                    if (!parts[i] || m_seen[i].contains(parts[i]))
                        continue;
                    m_seen[i].insert(parts[i]);
                    firsts |= quint16(1u << i);
                }
                m_seenLastOn = on;
                m_seenLastId = id;
                return firsts;
            }
        }
    }
    // Un QSO piu' vecchio (scritto a mano dopo): si contano quelli fatti prima
    // con la stessa cosa.
    QSqlQuery q(m_db->connection());
    // "C'e' gia' uno prima?", non "quanti prima": NOT EXISTS si ferma al primo
    // che trova. Su un log da un milione il conto costava mezzo minuto per un
    // QSO scritto a mano con l'ora di prima; cosi' e' un attimo, salvo le cose
    // davvero nuove, che vanno cercate fino in fondo.
    auto before = [](const char* condition) {
        return QStringLiteral("(SELECT CASE WHEN %1 THEN NOT EXISTS (SELECT 1 FROM qso o WHERE o.deleted = 0 AND %2 "
                              "AND (o.qso_datetime_on < t.qso_datetime_on OR (o.qso_datetime_on = t.qso_datetime_on "
                              "AND o.id < t.id))) END)")
            .arg(QString::fromLatin1(condition).section(QLatin1Char('|'), 0, 0),
                 QString::fromLatin1(condition).section(QLatin1Char('|'), 1, 1));
    };
    const QStringList parts{
        before("IFNULL(t.dxcc, 0) > 0|o.dxcc = t.dxcc"),
        before("IFNULL(t.dxcc, 0) > 0|o.dxcc = t.dxcc AND o.band = t.band"),
        before("IFNULL(t.cont, '') <> ''|o.cont = t.cont"),
        before("IFNULL(t.cont, '') <> ''|o.cont = t.cont AND o.band = t.band"),
        before("IFNULL(t.cqz, 0) > 0|o.cqz = t.cqz"),
        before("IFNULL(t.cqz, 0) > 0|o.cqz = t.cqz AND o.band = t.band"),
        before("IFNULL(t.ituz, 0) > 0|o.ituz = t.ituz"),
        before("IFNULL(t.ituz, 0) > 0|o.ituz = t.ituz AND o.band = t.band"),
        before("LENGTH(IFNULL(t.gridsquare, '')) >= 4|UPPER(SUBSTR(o.gridsquare, 1, 4)) = UPPER(SUBSTR(t.gridsquare, 1, 4))"),
        before("LENGTH(IFNULL(t.gridsquare, '')) >= 4|UPPER(SUBSTR(o.gridsquare, 1, 4)) = UPPER(SUBSTR(t.gridsquare, 1, 4)) "
               "AND o.band = t.band"),
        before("1|o.call = t.call"),
        before("1|o.call = t.call AND o.band = t.band"),
    };
    q.prepare(QStringLiteral("SELECT t.id, %1 FROM qso t WHERE t.id = ?").arg(parts.join(QStringLiteral(", "))));
    q.addBindValue(id);
    if (!q.exec() || !q.next())
        return kNoCategory;
    return categoryFrom(q);
}

QString QsoTableModel::selectSql(const QString& where) const
{
    QString extras;
    for (const QString& key : m_extra)
        extras += QStringLiteral(", ") + extraSql(key);
    return QStringLiteral(
               "SELECT id, qso_datetime_on, call, band, mode, submode, freq, rst_sent, rst_rcvd, "
               "gridsquare, name, dxcc, source, "
               "(SELECT group_concat(service || ':' || sent || ':' || rcvd) FROM qsl_status s WHERE s.qso_id = qso.id), "
               "IFNULL(tags, ''), "
               "IFNULL(qth, ''), IFNULL(country, ''), IFNULL(state, ''), IFNULL(cnty, ''), "
               "cqz, ituz, IFNULL(iota, ''), IFNULL(comment, '')%2 "
               "FROM qso WHERE deleted = 0 %1 ORDER BY qso_datetime_on DESC, id DESC")
        .arg(where, extras);
}

quint8 QsoTableModel::qslStateFrom(const QString& summary)
{
    // "servizio:inviata:ricevuta" per ogni servizio, separati da virgole.
    bool lotw = false, card = false, other = false, cardSent = false;
    for (const QString& item : summary.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QStringList parts = item.split(QLatin1Char(':'));
        if (parts.size() != 3)
            continue;
        const QString& service = parts.at(0);
        const bool rcvd = parts.at(2) == QLatin1String("Y") || parts.at(2) == QLatin1String("V");
        if (rcvd && service == QLatin1String("lotw"))
            lotw = true;
        else if (rcvd && service == QLatin1String("card"))
            card = true;
        else if (rcvd && (service == QLatin1String("eqsl") || service == QLatin1String("qrz")))
            other = true;
        else if (service == QLatin1String("card") && parts.at(1) == QLatin1String("Y"))
            cardSent = true;
    }
    return lotw ? QslLotwConfirmed : card ? QslCardConfirmed : other ? QslOtherConfirmed
         : cardSent ? QslCardSent : QslNone;
}

QsoTableModel::Row QsoTableModel::rowFromQuery(const QSqlQuery& q) const
{
    Row r;
    r.id = q.value(0).toLongLong();
    r.sortKey = q.value(1).toString();
    const QDateTime on = QDateTime::fromString(r.sortKey, Qt::ISODate);
    // Nella forma della lingua: in italiano 25/09/26 12:18.
    r.values[Utc] = on.toUTC().toString(core::dates::shortFormat() + QStringLiteral(" HH:mm"));
    r.values[Call] = q.value(2).toString();
    r.values[Band] = q.value(3).toString();
    const QString submode = q.value(5).toString();
    // FT2 si legge FT2, non MFSK: il modo ADIF resta nel database, qui si mostra
    // quello che l'operatore riconosce.
    r.values[Mode] = submode.isEmpty() || q.value(4).toString() == QLatin1String("SSB") ? q.value(4).toString() : submode;
    r.values[Freq] = q.value(6).isNull() ? QString() : QString::number(q.value(6).toDouble(), 'f', 6);
    r.values[RstSent] = q.value(7).toString();
    r.values[RstRcvd] = q.value(8).toString();
    r.values[Grid] = q.value(9).toString();
    r.values[Name] = q.value(10).toString();
    r.values[Dxcc] = q.value(11).isNull() ? QString() : q.value(11).toString();
    const QString source = q.value(12).toString();
    if (source.startsWith(QLatin1String("udp")))  r.values[Source] = QStringLiteral("udp");
    else if (source == QLatin1String("manual"))   r.values[Source] = QStringLiteral("man");
    else if (source == QLatin1String("import"))   r.values[Source] = QStringLiteral("imp");
    else if (source == QLatin1String("cloud"))    r.values[Source] = QStringLiteral("cld");
    // Recuperato dal log di Decodium: e' arrivato da Decodium come gli udp.
    else if (source == QLatin1String("decodium_adif") || source == QLatin1String("adif_watch"))
        r.values[Source] = QStringLiteral("rec");
    else r.values[Source] = source.left(3);
    r.values[Qsl] = qslCodes(q.value(13).toString());
    r.qsl = qslStateFrom(q.value(13).toString());
    r.values[Tags] = q.value(14).toString().replace(QLatin1Char(','), QStringLiteral(", "));
    r.values[Qth] = q.value(15).toString();
    r.values[Country] = q.value(16).toString();
    r.values[State] = q.value(17).toString();
    r.values[County] = q.value(18).toString();
    r.values[Cqz] = q.value(19).isNull() || q.value(19).toInt() <= 0 ? QString() : q.value(19).toString();
    r.values[Ituz] = q.value(20).isNull() || q.value(20).toInt() <= 0 ? QString() : q.value(20).toString();
    r.values[Iota] = q.value(21).toString();
    r.values[Comment] = q.value(22).toString();
    for (qsizetype i = 0; i < m_extra.size(); ++i) {
        QString v = q.value(23 + static_cast<int>(i)).toString();
        // Il prefisso WPX: chi non l'ha scritto nel file lo trova lo stesso.
        if (v.isEmpty() && m_extra.at(i) == QLatin1String("pfx"))
            v = core::awards::wpxPrefix(r.values[Call]);
        // Le date (del QSO, delle cartoline, di LoTW, dei campi ADIF a scelta)
        // si leggono come le altre.
        else if (m_extra.at(i).contains(QLatin1String("date"), Qt::CaseInsensitive))
            v = core::dates::show(v);
        r.extra << v;
    }
    return r;
}

bool QsoTableModel::filtered() const
{
    return !m_filter.trimmed().isEmpty() || !m_fields.isEmpty() || !m_bands.isEmpty() || !m_modes.isEmpty() || !m_month.isEmpty()
        || m_dxcc > 0 || !m_qsl.isEmpty() || m_profile > 0 || !m_tag.isEmpty() || !m_dateFrom.isEmpty()
        || !m_dateTo.isEmpty();
}

void QsoTableModel::refreshTotal()
{
    m_total = m_db && m_db->isOpen() ? m_db->qsoCount() : 0;
}

QString QsoTableModel::whereSql(QVariantList& binds, bool otherLogs) const
{
    QStringList where;
    {
        const QString f = m_filter.trimmed().toUpper();
        if (!f.isEmpty()) {
            // Ricerca libera: nominativo, locatore, nome, QTH o commento.
            where << QStringLiteral("(call LIKE ? OR UPPER(gridsquare) LIKE ? OR UPPER(name) LIKE ? "
                                    "OR UPPER(IFNULL(qth, '')) LIKE ? OR UPPER(IFNULL(comment, '')) LIKE ?)");
            const QString like = QLatin1Char('%') + f + QLatin1Char('%');
            binds << like << like << like << like << like;
        }
        if (!m_bands.isEmpty()) {
            QStringList marks;
            for (const QString& b : m_bands) {
                marks << QStringLiteral("?");
                binds << b.toLower();
            }
            where << QStringLiteral("band IN (%1)").arg(marks.join(QLatin1Char(',')));
        }
        if (!m_modes.isEmpty()) {
            QStringList marks;
            for (const QString& m : m_modes) {
                marks << QStringLiteral("?");
                binds << m.toUpper();
            }
            where << QStringLiteral("(CASE WHEN IFNULL(submode, '') = '' OR mode = 'SSB' THEN mode ELSE submode END) IN (%1)")
                         .arg(marks.join(QLatin1Char(',')));
        }
        if (!m_month.isEmpty()) {
            where << QStringLiteral("SUBSTR(qso_datetime_on, 1, 7) = ?");
            binds << m_month;
        }
        if (m_dxcc > 0) {
            where << QStringLiteral("dxcc = ?");
            binds << m_dxcc;
        }
        if (!m_qsl.isEmpty()) {
            const QString confirmedBy = QStringLiteral(
                "EXISTS (SELECT 1 FROM qsl_status s WHERE s.qso_id = qso.id AND s.rcvd = 'Y' AND s.service %1)");
            if (m_qsl == QLatin1String("confirmed")) {
                where << confirmedBy.arg(QStringLiteral("IN ('lotw', 'card', 'eqsl', 'qrz')"));
            } else if (m_qsl == QLatin1String("unconfirmed")) {
                where << QStringLiteral("NOT ") + confirmedBy.arg(QStringLiteral("IN ('lotw', 'card', 'eqsl', 'qrz')"));
            } else {
                where << confirmedBy.arg(QStringLiteral("= ?"));
                binds << m_qsl;
            }
        }
        if (m_profile > 0 && !otherLogs) {
            where << QStringLiteral("station_profile_id = ?");
            binds << m_profile;
        }
        if (!m_tag.isEmpty()) {
            // Un'etichetta intera, non un pezzo: "pota" non trova "potato".
            where << QStringLiteral("instr(',' || LOWER(IFNULL(tags, '')) || ',', ?) > 0");
            binds << QLatin1Char(',') + m_tag.toLower() + QLatin1Char(',');
        }
        // Le date nel database sono ISO-8601 a lunghezza fissa: il confronto fra
        // stringhe basta, e "fino al" comprende tutto quel giorno.
        if (!m_dateFrom.isEmpty()) {
            where << QStringLiteral("qso_datetime_on >= ?");
            binds << m_dateFrom;
        }
        if (!m_dateTo.isEmpty()) {
            const QDate to = QDate::fromString(m_dateTo, QStringLiteral("yyyy-MM-dd"));
            where << QStringLiteral("qso_datetime_on < ?");
            binds << (to.isValid() ? to.addDays(1).toString(QStringLiteral("yyyy-MM-dd")) : m_dateTo);
        }
        for (auto it = m_fields.cbegin(); it != m_fields.cend(); ++it) {
            const QString value = it.value().toString();
            if (it.key() == QLatin1String("pfx")) {
                // Lo stesso conto di filterPrefix, in SQL.
                const QString stored = QStringLiteral("UPPER(TRIM(IFNULL(CAST(json_extract(adif_extra, '$.PFX') AS TEXT), '')))");
                const QString base = baseCallSql();
                where << QStringLiteral("(%1 = ? OR (%1 = '' AND instr(%2, '/') = 0 AND %2 LIKE ? "
                                        "AND length(%2) > ? AND substr(%2, ? + 1) NOT GLOB '*[0-9]*'))")
                             .arg(stored, base);
                QString like = value;
                like.replace(QLatin1Char('%'), QString()).replace(QLatin1Char('_'), QString());
                binds << value << like + QLatin1Char('%') << value.size() << value.size();
                continue;
            }
            const QString sql = fieldSql(it.key());
            if (sql.isEmpty())
                continue;
            where << QStringLiteral("IFNULL(CAST((%1) AS TEXT), '') = ?").arg(sql);
            binds << value;
        }
    }
    return where.isEmpty() ? QString() : QStringLiteral("AND ") + where.join(QStringLiteral(" AND "));
}

bool QsoTableModel::scanWholeLog() const
{
    // La ricerca libera legge tutto il log: in fila (NOT INDEXED) e poi in
    // ordine, non seguendo l'indice del tempo e saltando per il file — su un
    // milione di QSO sono meno di un secondo invece di sette. Lo stesso per
    // tutto il log ordinato per un'altra colonna: l'indice del tempo non serve
    // all'ordine, e seguirlo vuol dire saltare.
    return !m_filter.trimmed().isEmpty() || (m_sortKey != QLatin1String("utc") && !filtered());
}

// Solo gli id, filtrati e ordinati da SQLite: i valori arrivano a pagine
// quando la tabella li mostra.
QString QsoTableModel::idsSql(QVariantList& binds, bool scan) const
{
    const QString where = whereSql(binds);
    return QStringLiteral("SELECT id FROM qso%1 WHERE deleted = 0 %2 ORDER BY %3")
        .arg(scan ? QStringLiteral(" NOT INDEXED") : QString(), where, orderSql());
}

void QsoTableModel::reload()
{
    decolog::StartupSpan trace("QsoTableModel::reload");
    const int generation = ++m_reloadGeneration;
    m_reloadLatest->store(generation);
    const QString path = m_db && m_db->isOpen() ? m_db->path() : QString();
    // Su un log grande in un file, un ordine o un filtro diversi da quelli di
    // sempre costano secondi (un milione di QSO per nominativo: due): si
    // preparano su un altro filo, e intanto la tabella resta com'e'. Il log in
    // ordine di tempo senza filtri legge solo il suo indice, e resta qui.
    if (!path.isEmpty() && path != QLatin1String(":memory:") && m_total >= kBackgroundRows
        && (filtered() || !defaultSort())) {
        QVariantList binds;
        const QString sql = idsSql(binds, scanWholeLog());
        if (!m_busy) {
            m_busy = true;
            emit busyChanged();
        }
        QPointer<QsoTableModel> self(this);
        const auto latest = m_reloadLatest;
        m_reloadPool.start([self, path, sql, binds, generation, latest] {
            // Un altro ricarico chiesto dopo: questo non serve piu'.
            if (latest->load() != generation)
                return;
            QVector<qint64> ids;
            {
                core::LogDatabase db;
                if (db.open(path)) {
                    QSqlQuery q(db.connection());
                    q.setForwardOnly(true);
                    q.prepare(sql);
                    for (const auto& b : binds)
                        q.addBindValue(b);
                    if (q.exec()) {
                        while (q.next())
                            ids.append(q.value(0).toLongLong());
                    }
                }
            }
            QMetaObject::invokeMethod(
                self.data(),
                [self, generation, ids = std::move(ids)]() mutable {
                    if (!self || generation != self->m_reloadGeneration)
                        return;
                    self->adoptIds(std::move(ids));
                },
                Qt::QueuedConnection);
        });
        return;
    }
    QVector<qint64> ids;
    if (m_db && m_db->isOpen()) {
        QVariantList binds;
        QSqlQuery q(m_db->connection());
        q.setForwardOnly(true);
        q.prepare(idsSql(binds, scanWholeLog()));
        for (const auto& b : binds)
            q.addBindValue(b);
        if (q.exec()) {
            while (q.next())
                ids.append(q.value(0).toLongLong());
        }
    }
    adoptIds(std::move(ids));
}

void QsoTableModel::adoptIds(QVector<qint64>&& ids)
{
    beginResetModel();
    m_ids = std::move(ids);
    dropPages();
    if (m_db && m_db->isOpen())
        computeCategories();
    refreshTotal();
    endResetModel();
    emit countChanged();
    if (m_busy) {
        m_busy = false;
        emit busyChanged();
    }
}

void QsoTableModel::refreshQso(qint64 id)
{
    if (!m_db || !m_db->isOpen())
        return;
    const int row = rowForId(id);
    if (row < 0)
        return;
    // Si rilegge la sua pagina quando serve; la categoria subito.
    m_pages.remove(row / kPage);
    m_pageUse.removeOne(row / kPage);
    m_category.insert(id, categoryOf(id));
    emit dataChanged(index(row, 0), index(row, columns() - 1));
}

void QsoTableModel::insertQso(qint64 id)
{
    if (!m_db || !m_db->isOpen())
        return;
    // Con un filtro o un ordine scelto, il posto giusto lo trova il ricarico.
    if (filtered() || !defaultSort()) {
        m_freshId = id;
        reload();
        return;
    }
    // Il posto: quanti QSO sono piu' recenti. Di solito nessuno — il QSO
    // appena fatto e' il primo — ma uno scritto a mano con l'ora di prima no.
    QSqlQuery q(m_db->connection());
    q.prepare(QStringLiteral("SELECT qso_datetime_on FROM qso WHERE id = ? AND deleted = 0"));
    q.addBindValue(id);
    if (!q.exec() || !q.next())
        return;
    const QString on = q.value(0).toString();
    // Una ricerca a meta' sulle righe che la tabella ha gia', in ordine di
    // tempo: una ventina di letture per id invece di contare nel log i QSO piu'
    // recenti — per un QSO del 2015 su un milione erano mezzo milione di righe.
    QSqlQuery at(m_db->connection());
    at.prepare(QStringLiteral("SELECT qso_datetime_on FROM qso WHERE id = ?"));
    auto comesBefore = [&at, &on, id](qint64 other) {
        at.bindValue(0, other);
        if (!at.exec() || !at.next())
            return false;
        const QString o = at.value(0).toString();
        return o > on || (o == on && other > id);
    };
    int lo = 0;
    int hi = static_cast<int>(m_ids.size());
    while (lo < hi) {
        const int mid = lo + (hi - lo) / 2;
        if (m_ids.at(mid) != id && comesBefore(m_ids.at(mid)))
            lo = mid + 1;
        else
            hi = mid;
    }
    const int pos = lo;
    m_category.insert(id, categoryOf(id));

    // Evidenziata solo l'ultima arrivata: la riga di prima torna normale.
    const int oldFresh = m_freshId > 0 ? rowForId(m_freshId) : -1;
    m_freshId = id;
    beginInsertRows({}, pos, pos);
    m_ids.insert(pos, id);
    // Le righe dopo si sono spostate di una: le pagine si rileggono.
    dropPages();
    endInsertRows();
    // TableView puo' conservare delegate gia' visibili dopo lo spostamento:
    // rilegge tutte le colonne e i ruoli delle righe interessate, non solo
    // l'evidenziazione del precedente QSO nuovo.
    if (pos + 1 < rowCount() && columns() > 0)
        emit dataChanged(index(pos + 1, 0), index(rowCount() - 1, columns() - 1));
    if (oldFresh >= 0) {
        const int moved = oldFresh >= pos ? oldFresh + 1 : oldFresh;
        emit dataChanged(index(moved, 0), index(moved, columns() - 1), {IsNewRole});
    }
    // Senza filtri il totale e' uno in piu': niente conteggio del log.
    ++m_total;
    emit countChanged();
}

void QsoTableModel::clearFilters()
{
    m_filter.clear();
    m_bands.clear();
    m_modes.clear();
    m_month.clear();
    m_dxcc = 0;
    m_qsl.clear();
    m_profile = 0;
    m_tag.clear();
    m_dateFrom.clear();
    m_dateTo.clear();
    m_fields.clear();
    emit filtersChanged();
    reload();
}

QVariantMap QsoTableModel::filterState() const
{
    return {
        {QStringLiteral("text"), m_filter},
        {QStringLiteral("bands"), m_bands},
        {QStringLiteral("modes"), m_modes},
        {QStringLiteral("month"), m_month},
        {QStringLiteral("dxcc"), m_dxcc},
        {QStringLiteral("qsl"), m_qsl},
        {QStringLiteral("profile"), m_profile},
        {QStringLiteral("tag"), m_tag},
        {QStringLiteral("dateFrom"), m_dateFrom},
        {QStringLiteral("dateTo"), m_dateTo},
        {QStringLiteral("fields"), m_fields},
    };
}

void QsoTableModel::applyFilterState(const QVariantMap& state)
{
    m_filter = state.value(QStringLiteral("text")).toString();
    m_bands = state.value(QStringLiteral("bands")).toStringList();
    m_modes = state.value(QStringLiteral("modes")).toStringList();
    m_month = state.value(QStringLiteral("month")).toString();
    m_dxcc = state.value(QStringLiteral("dxcc")).toInt();
    m_qsl = state.value(QStringLiteral("qsl")).toString();
    m_profile = state.value(QStringLiteral("profile")).toInt();
    m_tag = state.value(QStringLiteral("tag")).toString();
    m_dateFrom = state.value(QStringLiteral("dateFrom")).toString();
    m_dateTo = state.value(QStringLiteral("dateTo")).toString();
    m_fields.clear();
    const QVariantMap fields = state.value(QStringLiteral("fields")).toMap();
    for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
        if (fieldFilterDef(it.key()) && !it.value().toString().isEmpty())
            m_fields.insert(it.key(), it.value().toString());
    }
    emit filtersChanged();
    reload();
}

void QsoTableModel::setFieldFilter(const QString& key, const QString& value)
{
    if (!fieldFilterDef(key))
        return;
    const QString clean = value.trimmed();
    if (clean.isEmpty() ? !m_fields.contains(key) : m_fields.value(key).toString() == clean)
        return;
    if (clean.isEmpty())
        m_fields.remove(key);
    else
        m_fields.insert(key, clean);
    emit filtersChanged();
    reload();
}

QVariantList QsoTableModel::fieldFilterChoices() const
{
    QVariantList out;
    for (const FieldFilterDef& d : fieldFilterDefs())
        out << QVariantMap{{QStringLiteral("key"), QLatin1String(d.key)},
                           {QStringLiteral("label"), QCoreApplication::translate("QsoTableModel", d.label)}};
    return out;
}

QString QsoTableModel::fieldFilterLabel(const QString& key) const
{
    const FieldFilterDef* d = fieldFilterDef(key);
    return d ? QCoreApplication::translate("QsoTableModel", d->label) : key;
}

QString QsoTableModel::fieldValueLabel(const QString& key, const QString& value) const
{
    const FieldFilterDef* d = fieldFilterDef(key);
    if (d && d->month && value.size() == 6)
        return QStringLiteral("%1/%2").arg(value.mid(4, 2), value.left(4));
    return value;
}

namespace {

// I valori di un campo con i loro QSO, su una connessione qualsiasi.
QVariantList countFieldValues(QSqlDatabase db, const QString& key)
{
    const FieldFilterDef* d = fieldFilterDef(key);
    if (!d)
        return {};
    QList<QPair<QString, int>> counted;
    if (key == QLatin1String("pfx")) {
        QHash<QString, int> counts;
        QSqlQuery q(db);
        q.setForwardOnly(true);
        if (q.exec(QStringLiteral("SELECT call, CAST(json_extract(adif_extra, '$.PFX') AS TEXT) "
                                  "FROM qso NOT INDEXED WHERE deleted = 0"))) {
            while (q.next()) {
                const QString p = filterPrefix(q.value(0).toString(), q.value(1).toString());
                if (!p.isEmpty())
                    ++counts[p];
            }
        }
        for (auto it = counts.cbegin(); it != counts.cend(); ++it)
            counted << qMakePair(it.key(), it.value());
    } else {
        const QString sql = fieldSql(key);
        QSqlQuery q(db);
        q.setForwardOnly(true);
        if (q.exec(QStringLiteral("SELECT CAST((%1) AS TEXT) AS v, COUNT(*) FROM qso NOT INDEXED "
                                  "WHERE deleted = 0 AND (%1) IS NOT NULL GROUP BY v").arg(sql))) {
            while (q.next()) {
                const QString v = q.value(0).toString();
                if (!v.isEmpty())
                    counted << qMakePair(v, q.value(1).toInt());
            }
        }
    }
    std::sort(counted.begin(), counted.end(), [d](const auto& a, const auto& b) {
        if (d->numeric)
            return a.first.toInt() < b.first.toInt();
        if (d->month)
            return a.first > b.first;
        return QString::localeAwareCompare(a.first, b.first) < 0;
    });
    QVariantList out;
    for (const auto& [value, count] : std::as_const(counted))
        out << QVariantMap{{QStringLiteral("value"), value}, {QStringLiteral("count"), count}};
    return out;
}

} // namespace

QVariantList QsoTableModel::fieldValues(const QString& key) const
{
    if (!m_db || !m_db->isOpen())
        return {};
    QVariantList out = countFieldValues(m_db->connection(), key);
    for (QVariant& v : out) {
        QVariantMap m = v.toMap();
        m.insert(QStringLiteral("label"), fieldValueLabel(key, m.value(QStringLiteral("value")).toString()));
        v = m;
    }
    return out;
}

namespace {

struct LogSearch {
    QString sql;
    QVariantList binds;
};

// Un log alla volta, con una connessione sua in sola lettura: il file non si
// tocca (niente aggiornamenti di schema), e un log di una versione vecchia a
// cui manca una colonna lo dice invece di fermare gli altri.
void searchOneLog(const LogSearch& search, const QString& name, const QString& path, QVariantList* rows,
                  QVariantList* perLog)
{
    static std::atomic<int> serial{0};
    const QString connection = QStringLiteral("decolog-search-%1").arg(++serial);
    int found = 0;
    QString error;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(path);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (!db.open()) {
            error = db.lastError().text();
        } else {
            QSqlQuery q(db);
            q.setForwardOnly(true);
            q.prepare(search.sql);
            for (const QVariant& b : search.binds)
                q.addBindValue(b);
            if (!q.exec()) {
                error = q.lastError().text();
            } else {
                while (q.next()) {
                    const QString on = q.value(0).toString();
                    rows->append(QVariantMap{
                        {QStringLiteral("log"), name},
                        {QStringLiteral("on"), on},
                        {QStringLiteral("utc"), QDateTime::fromString(on, Qt::ISODate).toUTC()
                                                    .toString(core::dates::shortFormat() + QStringLiteral(" HH:mm"))},
                        {QStringLiteral("call"), q.value(1).toString()},
                        {QStringLiteral("band"), q.value(2).toString()},
                        {QStringLiteral("mode"), q.value(3).toString()},
                        {QStringLiteral("country"), q.value(4).toString()},
                        {QStringLiteral("qsl"), int(QsoTableModel::qslStateFrom(q.value(5).toString()))},
                    });
                    ++found;
                }
            }
            q.finish();
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(connection);
    perLog->append(QVariantMap{{QStringLiteral("name"), name},
                               {QStringLiteral("path"), path},
                               {QStringLiteral("count"), found},
                               {QStringLiteral("truncated"), found >= QsoTableModel::kSearchLimit},
                               {QStringLiteral("error"), error}});
}

void sortByTime(QVariantList* rows)
{
    std::stable_sort(rows->begin(), rows->end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap().value(QStringLiteral("on")).toString() > b.toMap().value(QStringLiteral("on")).toString();
    });
}

} // namespace

QString QsoTableModel::searchSql(QVariantList& binds) const
{
    return QStringLiteral(
               "SELECT qso_datetime_on, call, band, "
               "(CASE WHEN IFNULL(submode, '') = '' OR mode = 'SSB' THEN mode ELSE submode END), "
               "IFNULL(country, ''), "
               "(SELECT group_concat(service || ':' || sent || ':' || rcvd) FROM qsl_status s WHERE s.qso_id = qso.id) "
               "FROM qso WHERE deleted = 0 %1 ORDER BY qso_datetime_on DESC LIMIT %2")
        .arg(whereSql(binds, true))
        .arg(kSearchLimit);
}

void QsoTableModel::searchLogsNow(const QVariantList& logs, QVariantList* rows, QVariantList* perLog) const
{
    LogSearch search;
    search.sql = searchSql(search.binds);
    for (const QVariant& v : logs) {
        const QVariantMap log = v.toMap();
        searchOneLog(search, log.value(QStringLiteral("name")).toString(), log.value(QStringLiteral("path")).toString(),
                     rows, perLog);
    }
    sortByTime(rows);
}

void QsoTableModel::searchLogs(const QVariantList& logs)
{
    LogSearch search;
    search.sql = searchSql(search.binds);
    QPointer<QsoTableModel> self(this);
    m_reloadPool.start([self, search, logs] {
        QVariantList rows;
        QVariantList perLog;
        for (const QVariant& v : logs) {
            const QVariantMap log = v.toMap();
            searchOneLog(search, log.value(QStringLiteral("name")).toString(),
                         log.value(QStringLiteral("path")).toString(), &rows, &perLog);
        }
        sortByTime(&rows);
        QMetaObject::invokeMethod(
            self.data(),
            [self, rows = std::move(rows), perLog = std::move(perLog)] {
                if (self)
                    emit self->logsSearched(rows, perLog);
            },
            Qt::QueuedConnection);
    });
}

void QsoTableModel::requestFieldValues(const QString& key)
{
    const QString path = m_db && m_db->isOpen() ? m_db->path() : QString();
    // Un log in memoria (le prove) non si apre da un altro filo: si conta qui.
    if (path.isEmpty() || path == QLatin1String(":memory:")) {
        emit fieldValuesReady(key, fieldValues(key));
        return;
    }
    QPointer<QsoTableModel> self(this);
    m_reloadPool.start([self, path, key] {
        QVariantList values;
        {
            core::LogDatabase db;
            if (db.open(path))
                values = countFieldValues(db.connection(), key);
        }
        QMetaObject::invokeMethod(
            self.data(),
            [self, key, values = std::move(values)]() mutable {
                if (!self)
                    return;
                for (QVariant& v : values) {
                    QVariantMap m = v.toMap();
                    m.insert(QStringLiteral("label"),
                             self->fieldValueLabel(key, m.value(QStringLiteral("value")).toString()));
                    v = m;
                }
                emit self->fieldValuesReady(key, values);
            },
            Qt::QueuedConnection);
    });
}

qint64 QsoTableModel::idAt(int row) const
{
    return row >= 0 && row < m_ids.size() ? m_ids.at(row) : 0;
}

QString QsoTableModel::callAt(int row) const
{
    const Row* r = rowAt(row);
    return r ? r->values[Call] : QString();
}

QString QsoTableModel::valueAt(int row, int column) const
{
    return valueFor(row, m_layout.value(column));
}

int QsoTableModel::rowForId(qint64 id) const
{
    return static_cast<int>(m_ids.indexOf(id));
}

QStringList QsoTableModel::bandsInLog() const
{
    if (!m_db || !m_db->isOpen())
        return {};
    return m_db->bandsInLog();
}

QStringList QsoTableModel::modesInLog() const
{
    if (!m_db || !m_db->isOpen())
        return {};
    return m_db->modesInLog();
}

QVariantList QsoTableModel::tagsInLog() const
{
    QVariantList out;
    if (!m_db || !m_db->isOpen())
        return out;
    for (const auto& row : m_db->tagCounts())
        out << QVariantMap{{QStringLiteral("key"), row.key}, {QStringLiteral("count"), row.count}};
    return out;
}

QVariantList QsoTableModel::shownIds() const
{
    QVariantList out;
    out.reserve(m_ids.size());
    for (qint64 id : m_ids)
        out << id;
    return out;
}

void QsoTableModel::setDxccFilter(int dxcc)
{
    if (dxcc == m_dxcc)
        return;
    m_dxcc = qMax(0, dxcc);
    emit filtersChanged();
    reload();
}

void QsoTableModel::setQslFilter(const QString& qsl)
{
    if (qsl == m_qsl)
        return;
    m_qsl = qsl;
    emit filtersChanged();
    reload();
}

void QsoTableModel::setProfileFilter(int profileId)
{
    if (profileId == m_profile)
        return;
    m_profile = qMax(0, profileId);
    emit filtersChanged();
    reload();
}

void QsoTableModel::setTagFilter(const QString& tag)
{
    const QString t = tag.simplified();
    if (t == m_tag)
        return;
    m_tag = t;
    emit filtersChanged();
    reload();
}

void QsoTableModel::setDateFrom(const QString& date)
{
    const QString d = QDate::fromString(date.trimmed(), QStringLiteral("yyyy-MM-dd")).isValid() ? date.trimmed() : QString();
    if (d == m_dateFrom)
        return;
    m_dateFrom = d;
    emit filtersChanged();
    reload();
}

void QsoTableModel::setDateTo(const QString& date)
{
    const QString d = QDate::fromString(date.trimmed(), QStringLiteral("yyyy-MM-dd")).isValid() ? date.trimmed() : QString();
    if (d == m_dateTo)
        return;
    m_dateTo = d;
    emit filtersChanged();
    reload();
}

void QsoTableModel::setFilterText(const QString& text)
{
    if (text == m_filter)
        return;
    m_filter = text;
    emit filtersChanged();
    reload();
}

void QsoTableModel::setBandFilter(const QStringList& bands)
{
    if (bands == m_bands)
        return;
    m_bands = bands;
    emit filtersChanged();
    reload();
}

void QsoTableModel::setModeFilter(const QStringList& modes)
{
    if (modes == m_modes)
        return;
    m_modes = modes;
    emit filtersChanged();
    reload();
}

void QsoTableModel::setMonthFilter(const QString& month)
{
    if (month == m_month)
        return;
    m_month = month;
    emit filtersChanged();
    reload();
}

} // namespace decolog::app
