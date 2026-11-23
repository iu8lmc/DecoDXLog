#include "core/LogMigration.h"

#include "core/Bands.h"
#include "core/LogDatabase.h"

#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QThread>
#include <QTime>
#include <QUuid>

namespace decolog::core::migration {

bool estimateImport(const LogDatabase& source,const QByteArray& adif,ImportResult* result,QString* error)
{
    if (error) error->clear();
    auto fail=[&](const QString& message) { if (error) *error=message; return false; };
    if (!source.isOpen() || !result || adif.isEmpty()) return fail(QStringLiteral("Invalid import preview request"));
    LogDatabase probe; if (!probe.open(QStringLiteral(":memory:"))) return fail(probe.lastError());
    probe.setDedupWindows(source.dedupWindowSeconds(false),source.dedupWindowSeconds(true));
    QSqlDatabase src=source.connection(), dst=probe.connection();
    if (!src.transaction()) return fail(src.lastError().text());
    if (!dst.transaction()) { src.rollback(); return fail(dst.lastError().text()); }
    QSqlQuery read(src), write(dst); read.setForwardOnly(true);
    if (!read.exec(QStringLiteral("SELECT id,call,band,mode,submode,qso_datetime_on FROM qso WHERE deleted=0")) ||
        !write.prepare(QStringLiteral("INSERT INTO qso(uuid,call,band,mode,submode,qso_datetime_on,source,created_at,updated_at) VALUES(?,?,?,?,?,?,'preview','preview','preview')"))) {
        const QString message=read.lastError().isValid()?read.lastError().text():write.lastError().text(); dst.rollback(); src.rollback(); return fail(message);
    }
    while (read.next()) {
        write.bindValue(0,QStringLiteral("preview-%1").arg(read.value(0).toLongLong()));
        for (int i=1;i<6;++i) write.bindValue(i,read.value(i));
        if (!write.exec()) { const QString message=write.lastError().text(); dst.rollback(); src.rollback(); return fail(message); }
    }
    if (read.lastError().isValid()) { const QString message=read.lastError().text(); dst.rollback(); src.rollback(); return fail(message); }
    read.finish(); write.finish();
    if (!src.rollback()) { const QString message=src.lastError().text(); dst.rollback(); return fail(message); }
    if (!dst.commit()) { const QString message=dst.lastError().text(); dst.rollback(); return fail(message); }
    *result=probe.importAdif(adif,QStringLiteral("preview"),0);
    const int expected=adif::parse(adif).records.size();
    if (result->read!=expected || result->inserted+result->duplicates+result->invalid!=expected || result->errors.size()>qMin(result->invalid,20))
        return fail(result->errors.isEmpty() ? QStringLiteral("Incomplete import preview") : result->errors.join(QStringLiteral("; ")));
    return true;
}

namespace {

// Il nome di una colonna, senza spazi, trattini e maiuscole: "RST Sent",
// "rst_sent" e "RSTSENT" sono la stessa cosa.
QString squash(const QString& name)
{
    QString out;
    for (const QChar c : name) {
        if (c.isLetterOrNumber())
            out += c.toLower();
    }
    return out;
}

// Le colonne che si riconoscono, verso il campo ADIF.
const QHash<QString, QString>& csvColumns()
{
    static const QHash<QString, QString> map{
        {QStringLiteral("call"), QStringLiteral("CALL")},
        {QStringLiteral("callsign"), QStringLiteral("CALL")},
        {QStringLiteral("nominativo"), QStringLiteral("CALL")},
        {QStringLiteral("date"), QStringLiteral("QSO_DATE")},
        {QStringLiteral("qsodate"), QStringLiteral("QSO_DATE")},
        {QStringLiteral("data"), QStringLiteral("QSO_DATE")},
        {QStringLiteral("time"), QStringLiteral("TIME_ON")},
        {QStringLiteral("timeon"), QStringLiteral("TIME_ON")},
        {QStringLiteral("utc"), QStringLiteral("TIME_ON")},
        {QStringLiteral("ora"), QStringLiteral("TIME_ON")},
        {QStringLiteral("timeoff"), QStringLiteral("TIME_OFF")},
        {QStringLiteral("band"), QStringLiteral("BAND")},
        {QStringLiteral("banda"), QStringLiteral("BAND")},
        {QStringLiteral("freq"), QStringLiteral("FREQ")},
        {QStringLiteral("frequency"), QStringLiteral("FREQ")},
        {QStringLiteral("frequenza"), QStringLiteral("FREQ")},
        {QStringLiteral("mhz"), QStringLiteral("FREQ")},
        {QStringLiteral("mode"), QStringLiteral("MODE")},
        {QStringLiteral("modo"), QStringLiteral("MODE")},
        {QStringLiteral("submode"), QStringLiteral("SUBMODE")},
        {QStringLiteral("rstsent"), QStringLiteral("RST_SENT")},
        {QStringLiteral("rsts"), QStringLiteral("RST_SENT")},
        {QStringLiteral("sent"), QStringLiteral("RST_SENT")},
        {QStringLiteral("rstinviato"), QStringLiteral("RST_SENT")},
        {QStringLiteral("rstrcvd"), QStringLiteral("RST_RCVD")},
        {QStringLiteral("rstr"), QStringLiteral("RST_RCVD")},
        {QStringLiteral("rcvd"), QStringLiteral("RST_RCVD")},
        {QStringLiteral("rstricevuto"), QStringLiteral("RST_RCVD")},
        {QStringLiteral("name"), QStringLiteral("NAME")},
        {QStringLiteral("nome"), QStringLiteral("NAME")},
        {QStringLiteral("qth"), QStringLiteral("QTH")},
        {QStringLiteral("grid"), QStringLiteral("GRIDSQUARE")},
        {QStringLiteral("gridsquare"), QStringLiteral("GRIDSQUARE")},
        {QStringLiteral("locator"), QStringLiteral("GRIDSQUARE")},
        {QStringLiteral("locatore"), QStringLiteral("GRIDSQUARE")},
        {QStringLiteral("comment"), QStringLiteral("COMMENT")},
        {QStringLiteral("comments"), QStringLiteral("COMMENT")},
        {QStringLiteral("commento"), QStringLiteral("COMMENT")},
        {QStringLiteral("note"), QStringLiteral("NOTES")},
        {QStringLiteral("notes"), QStringLiteral("NOTES")},
        {QStringLiteral("country"), QStringLiteral("COUNTRY")},
        {QStringLiteral("paese"), QStringLiteral("COUNTRY")},
        {QStringLiteral("dxcc"), QStringLiteral("DXCC")},
        {QStringLiteral("state"), QStringLiteral("STATE")},
        {QStringLiteral("provincia"), QStringLiteral("STATE")},
        {QStringLiteral("iota"), QStringLiteral("IOTA")},
        {QStringLiteral("pota"), QStringLiteral("POTA_REF")},
        {QStringLiteral("sota"), QStringLiteral("SOTA_REF")},
        {QStringLiteral("power"), QStringLiteral("TX_PWR")},
        {QStringLiteral("txpwr"), QStringLiteral("TX_PWR")},
        {QStringLiteral("potenza"), QStringLiteral("TX_PWR")},
        {QStringLiteral("operator"), QStringLiteral("OPERATOR")},
        {QStringLiteral("mycall"), QStringLiteral("STATION_CALLSIGN")},
        {QStringLiteral("stationcallsign"), QStringLiteral("STATION_CALLSIGN")},
    };
    return map;
}

// Una riga CSV con le virgolette: "a, b",c → [a, b] [c].
QStringList splitCsv(const QString& line, QChar sep)
{
    QStringList out;
    QString cell;
    bool quoted = false;
    for (qsizetype i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (quoted) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"')) {
                    cell += QLatin1Char('"');
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                cell += c;
            }
        } else if (c == QLatin1Char('"')) {
            quoted = true;
        } else if (c == sep) {
            out << cell.trimmed();
            cell.clear();
        } else {
            cell += c;
        }
    }
    out << cell.trimmed();
    return out;
}

QString dateToAdif(const QString& text)
{
    const QString t = text.trimmed();
    for (const char* format : {"yyyy-MM-dd", "yyyyMMdd", "dd/MM/yyyy", "dd.MM.yyyy", "dd-MM-yyyy", "yyyy/MM/dd"}) {
        const QDate d = QDate::fromString(t.left(10), QLatin1String(format));
        if (d.isValid())
            return d.toString(QStringLiteral("yyyyMMdd"));
    }
    // "2026-09-28 12:03:00" o "2026-09-28T12:03": la data e basta.
    const QDateTime dt = QDateTime::fromString(t, Qt::ISODate);
    return dt.isValid() ? dt.date().toString(QStringLiteral("yyyyMMdd")) : QString();
}

QString timeToAdif(const QString& text)
{
    QString t = text.trimmed();
    // Un orario dentro una data completa: "2026-09-28 12:03:00".
    const qsizetype space = t.lastIndexOf(QLatin1Char(' '));
    if (space > 0 && t.left(space).contains(QLatin1Char('-')))
        t = t.mid(space + 1);
    const qsizetype tee = t.indexOf(QLatin1Char('T'));
    if (tee > 0)
        t = t.mid(tee + 1);
    for (const char* format : {"HH:mm:ss", "HH:mm", "HHmmss", "HHmm", "H:mm"}) {
        const QTime h = QTime::fromString(t.left(8).remove(QLatin1Char('Z')), QLatin1String(format));
        if (h.isValid())
            return h.toString(QStringLiteral("HHmmss"));
    }
    return {};
}

} // namespace

QList<AdifRecord> readCsv(const QByteArray& data, QString* error)
{
    QList<AdifRecord> out;
    QString text = QString::fromUtf8(data);
    if (text.contains(QChar::ReplacementCharacter))
        text = QString::fromLatin1(data);
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);
    QStringList lines = text.split(QLatin1Char('\n'));
    while (!lines.isEmpty() && lines.first().trimmed().isEmpty())
        lines.removeFirst();
    if (lines.isEmpty()) {
        if (error)
            *error = QStringLiteral("empty file");
        return out;
    }
    // Il separatore: quello che nella prima riga c'e' di piu'.
    const QString head = lines.first();
    QChar sep = QLatin1Char(',');
    int best = head.count(QLatin1Char(','));
    for (const QChar c : {QChar(QLatin1Char(';')), QChar(QLatin1Char('\t'))}) {
        if (head.count(c) > best) {
            best = head.count(c);
            sep = c;
        }
    }
    QStringList fields;
    for (const QString& column : splitCsv(head, sep)) {
        QString field = csvColumns().value(squash(column));
        // Una colonna che ha gia' il nome ADIF resta com'e'.
        if (field.isEmpty() && column.trimmed().contains(QLatin1Char('_')))
            field = column.trimmed().toUpper();
        if (field.isEmpty() && squash(column) == QLatin1String("call"))
            field = QStringLiteral("CALL");
        fields << field;
    }
    if (!fields.contains(QStringLiteral("CALL"))) {
        if (error)
            *error = QStringLiteral("no Call column in the first line");
        return out;
    }
    for (qsizetype l = 1; l < lines.size(); ++l) {
        if (lines.at(l).trimmed().isEmpty())
            continue;
        const QStringList cells = splitCsv(lines.at(l), sep);
        AdifRecord r;
        for (qsizetype i = 0; i < cells.size() && i < fields.size(); ++i) {
            const QString field = fields.at(i);
            QString value = cells.at(i);
            if (field.isEmpty() || value.isEmpty())
                continue;
            if (field == QLatin1String("QSO_DATE")) {
                // Una colonna "Data" con anche l'ora: l'ora va a TIME_ON se manca.
                if (!r.contains(QStringLiteral("TIME_ON")) && value.size() > 10) {
                    const QString time = timeToAdif(value);
                    if (!time.isEmpty())
                        r.set(QStringLiteral("TIME_ON"), time);
                }
                value = dateToAdif(value);
            } else if (field == QLatin1String("TIME_ON") || field == QLatin1String("TIME_OFF")) {
                value = timeToAdif(value);
            } else if (field == QLatin1String("FREQ")) {
                bool ok = false;
                double mhz = value.replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
                // Chi scrive i kHz: 14074 → 14.074.
                if (ok && mhz > 1000)
                    mhz /= 1000.0;
                value = ok ? QString::number(mhz, 'f', 6) : QString();
            } else if (field == QLatin1String("CALL") || field == QLatin1String("MODE")
                       || field == QLatin1String("GRIDSQUARE")) {
                value = value.toUpper();
            }
            if (!value.isEmpty())
                r.set(field, value);
        }
        if (r.value(QStringLiteral("BAND")).isEmpty() && !r.value(QStringLiteral("FREQ")).isEmpty())
            r.set(QStringLiteral("BAND"), bands::fromMhz(r.value(QStringLiteral("FREQ")).toDouble()));
        if (!r.value(QStringLiteral("CALL")).isEmpty())
            out << r;
    }
    return out;
}

QList<AdifRecord> readN1mmDatabase(const QString& path, QString* error)
{
    QList<AdifRecord> out;
    // Una connessione sua, con un nome che nessun altro usa.
    const QString name = QStringLiteral("n1mm-%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (!db.open()) {
            if (error)
                *error = db.lastError().text();
        } else {
            QSqlQuery q(db);
            if (!q.exec(QStringLiteral("SELECT * FROM DXLOG ORDER BY TS"))) {
                if (error)
                    *error = QStringLiteral("no DXLOG table: is it an N1MM Logger+ database?");
            } else {
                const QSqlRecord columns = q.record();
                auto col = [&columns](const char* n) { return columns.indexOf(QLatin1String(n)); };
                const int ts = col("TS"), call = col("Call"), freq = col("Freq"), mode = col("Mode"),
                          snt = col("Snt"), rcv = col("Rcv"), sntnr = col("SntNr"), rcvnr = col("RcvNr"),
                          exch = col("Exchange1"), sect = col("Sect"), zn = col("Zn"), nameCol = col("Name"),
                          comment = col("Comment"), grid = col("GridSquare"), op = col("Operator"),
                          contest = col("ContestName"), id = col("ID"), station = col("StationPrefix");
                auto text = [&q](int i) { return i >= 0 ? q.value(i).toString().trimmed() : QString(); };
                while (q.next()) {
                    AdifRecord r;
                    r.set(QStringLiteral("CALL"), text(call).toUpper());
                    const QDateTime when = QDateTime::fromString(text(ts), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
                    if (when.isValid()) {
                        r.set(QStringLiteral("QSO_DATE"), when.toString(QStringLiteral("yyyyMMdd")));
                        r.set(QStringLiteral("TIME_ON"), when.toString(QStringLiteral("HHmmss")));
                    }
                    // Freq in kHz nella tabella di N1MM.
                    const double khz = freq >= 0 ? q.value(freq).toDouble() : 0.0;
                    if (khz > 0) {
                        r.set(QStringLiteral("FREQ"), QString::number(khz / 1000.0, 'f', 6));
                        r.set(QStringLiteral("BAND"), bands::fromMhz(khz / 1000.0));
                    }
                    const QString m = text(mode).toUpper();
                    if (m == QLatin1String("USB") || m == QLatin1String("LSB")) {
                        r.set(QStringLiteral("MODE"), QStringLiteral("SSB"));
                        r.set(QStringLiteral("SUBMODE"), m);
                    } else {
                        r.set(QStringLiteral("MODE"), m);
                    }
                    adif::normalizeMode(r);
                    r.set(QStringLiteral("RST_SENT"), text(snt));
                    r.set(QStringLiteral("RST_RCVD"), text(rcv));
                    if (text(sntnr).toInt() > 0)
                        r.set(QStringLiteral("STX"), text(sntnr));
                    if (text(rcvnr).toInt() > 0)
                        r.set(QStringLiteral("SRX"), text(rcvnr));
                    r.set(QStringLiteral("SRX_STRING"), text(exch));
                    r.set(QStringLiteral("ARRL_SECT"), text(sect));
                    if (text(zn).toInt() > 0)
                        r.set(QStringLiteral("CQZ"), text(zn));
                    r.set(QStringLiteral("NAME"), text(nameCol));
                    r.set(QStringLiteral("COMMENT"), text(comment));
                    r.set(QStringLiteral("GRIDSQUARE"), text(grid).toUpper());
                    r.set(QStringLiteral("OPERATOR"), text(op).toUpper());
                    r.set(QStringLiteral("CONTEST_ID"), text(contest));
                    r.set(QStringLiteral("STATION_CALLSIGN"), text(station).toUpper());
                    r.set(QStringLiteral("APP_N1MM_ID"), text(id));
                    // I campi vuoti non si portano.
                    AdifRecord clean;
                    for (const auto& f : r.fields()) {
                        if (!f.value.isEmpty())
                            clean.set(f.name, f.value);
                    }
                    if (!clean.value(QStringLiteral("CALL")).isEmpty())
                        out << clean;
                }
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
    return out;
}

void mapProgramFields(AdifRecord& r)
{
    // Il logbook di QRZ: C = confermato, con la data della conferma.
    if (r.value(QStringLiteral("APP_QRZLOG_STATUS")).trimmed().toUpper() == QLatin1String("C")
        && r.value(QStringLiteral("QRZCOM_QSO_DOWNLOAD_STATUS")).isEmpty()) {
        r.set(QStringLiteral("QRZCOM_QSO_DOWNLOAD_STATUS"), QStringLiteral("Y"));
        const QString date = r.value(QStringLiteral("APP_QRZLOG_QSLDATE")).trimmed();
        if (!date.isEmpty())
            r.set(QStringLiteral("QRZCOM_QSO_DOWNLOAD_DATE"), date);
    }
    // eQSL: l'"Authenticity Guaranteed" non e' una conferma in piu', ma
    // EQSL_QSL_RCVD a volte manca nell'esportazione della casella.
    if (r.value(QStringLiteral("APP_EQSL_QSL_RCVD")).trimmed().toUpper() == QLatin1String("Y")
        && r.value(QStringLiteral("EQSL_QSL_RCVD")).isEmpty())
        r.set(QStringLiteral("EQSL_QSL_RCVD"), QStringLiteral("Y"));
}

QByteArray asAdif(const QString& path, QString* error, QString* kind)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("s3db") || suffix == QLatin1String("db")) {
        if (kind)
            *kind = QStringLiteral("n1mm");
        const QList<AdifRecord> records = readN1mmDatabase(path, error);
        if (records.isEmpty()) {
            if (error && error->isEmpty())
                *error = QStringLiteral("no QSO in the N1MM database");
            return {};
        }
        AdifDocument doc;
        doc.header.set(QStringLiteral("PROGRAMID"), QStringLiteral("N1MM Logger+ (DecoDXLog import)"));
        doc.records = records;
        return adif::writeDocument(doc);
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return {};
    }
    const QByteArray data = f.readAll();
    if (suffix == QLatin1String("csv") || suffix == QLatin1String("tsv")) {
        if (kind)
            *kind = QStringLiteral("csv");
        const QList<AdifRecord> records = readCsv(data, error);
        if (records.isEmpty()) {
            if (error && error->isEmpty())
                *error = QStringLiteral("no QSO in the file");
            return {};
        }
        AdifDocument doc;
        doc.header.set(QStringLiteral("PROGRAMID"), QStringLiteral("CSV (DecoDXLog import)"));
        doc.records = records;
        return adif::writeDocument(doc);
    }
    if (kind)
        *kind = QStringLiteral("adif");
    return data;
}

} // namespace decolog::core::migration
