// I campi di serie della cartolina QSL: dove finiscono, e cosa succede a
// premere il pulsante due volte.
//
// Le posizioni sono misurate sulla cartolina vera — righe e colonne della
// tabella trovate guardando i pixel — e qui si controlla che cadano dentro i
// riquadri, non che siano un certo numero: un numero non direbbe niente.
#include "app/QslCardController.h"
#include "core/LogDatabase.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTimer>
#include <QTcpSocket>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QSqlQuery>
#include <QSqlError>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFile>

using namespace decolog;
using namespace decolog::app;

namespace {

// I riquadri della cartolina di prova (1920 x 1080), in frazione: la fascia del
// nominativo e le sette caselle della tabella.
struct Box {
    const char* key;
    double left, right, top, bottom;
};

const Box kBoxes[] = {
    // "Confirming QSO/SWL to:" finisce a 1253, la fascia arriva a 1910.
    {"call",  1253 / 1920.0, 1910 / 1920.0, 518 / 1080.0, 604 / 1080.0},
    // La riga vuota della tabella: da 738 a 851, colonne fra le righe verticali.
    {"day",      3 / 1920.0,  274 / 1920.0, 738 / 1080.0, 851 / 1080.0},
    {"month",  274 / 1920.0,  554 / 1920.0, 738 / 1080.0, 851 / 1080.0},
    {"year",   554 / 1920.0,  779 / 1920.0, 738 / 1080.0, 851 / 1080.0},
    {"time",   779 / 1920.0, 1029 / 1920.0, 738 / 1080.0, 851 / 1080.0},
    {"freq",  1029 / 1920.0, 1306 / 1920.0, 738 / 1080.0, 851 / 1080.0},
    {"mode",  1306 / 1920.0, 1589 / 1920.0, 738 / 1080.0, 851 / 1080.0},
    {"rst",   1589 / 1920.0, 1916 / 1920.0, 738 / 1080.0, 851 / 1080.0},
};

// Un Cloud finto: ascolta su una porta qualunque, risponde 200 e si tiene
// quello che ha ricevuto. Serve a provare che il programma parli davvero la
// lingua che il server si aspetta, invece di fidarsi che la parli.
class FakeCloud : public QTcpServer {
public:
    QByteArray request;
    QByteArray body;

    explicit FakeCloud(QObject* parent = nullptr) : QTcpServer(parent)
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            QTcpSocket* socket = nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                request += socket->readAll();
                const int head = request.indexOf("\r\n\r\n");
                if (head < 0)
                    return;
                // Si aspetta tutto il corpo: una cartolina non sta in un pacchetto.
                body = request.mid(head + 4);
                if (body.size() < contentLength())
                    return;
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                              "Content-Length: 29\r\n\r\n{\"sent\":true,\"remaining\":99}");
                socket->disconnectFromHost();
            });
        });
    }

    QString url() const { return QStringLiteral("http://127.0.0.1:%1").arg(serverPort()); }

    int contentLength() const { return header("Content-Length").toInt(); }

    QByteArray header(const QByteArray& name) const
    {
        const QList<QByteArray> lines = request.left(request.indexOf("\r\n\r\n")).split('\n');
        for (const QByteArray& line : lines) {
            if (line.toLower().startsWith(name.toLower() + ":"))
                return line.mid(name.size() + 1).trimmed();
        }
        return {};
    }
};

QVariantMap fieldFor(const QVariantList& fields, const QString& key)
{
    for (const QVariant& value : fields) {
        const QVariantMap field = value.toMap();
        if (field.value(QStringLiteral("key")).toString() == key)
            return field;
    }
    return {};
}

} // namespace

class TestQslCardController : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;

private slots:
    void plannerQueuePreservesExistingOrChangedState()
    {
        core::LogDatabase db; QVERIFY(db.open(":memory:"));
        auto add=[&](const char* call) { return db.insertQso(core::AdifRecord{{"CALL",call},{"QSO_DATE","20261001"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}},"import").id; };
        const qint64 fresh=add("K1AAA"), sent=add("K1BBB"), received=add("K1CCC"), changed=add("K1DDD");
        QVERIFY(fresh && sent && received && changed);
        core::QslState state; state.service="card"; state.sent="Y"; state.via="B"; QVERIFY(db.setCardState(sent,state));
        state.sent="N"; state.rcvd="Y"; QVERIFY(db.setCardState(received,state));
        state.rcvd="N"; state.via="D"; QVERIFY(db.setCardState(changed,state));
        QslCardController::Context ctx; ctx.db=&db; QslCardController cards(ctx);
        const auto run=[&](qint64 id,const QString& uuid,const QString& route) { return cards.enqueueIfUnsent(id,uuid,db.meta(id)->revision,route,db.path()); };
        QVERIFY(run(fresh,db.meta(fresh)->uuid,"B")); QCOMPARE(db.qslStatus(fresh).first().sent,QString("Q"));
        QVERIFY(!run(fresh,db.meta(fresh)->uuid,"B"));
        QVERIFY(!run(sent,db.meta(sent)->uuid,"B")); QCOMPARE(db.qslStatus(sent).first().sent,QString("Y"));
        QVERIFY(!run(received,db.meta(received)->uuid,"B")); QCOMPARE(db.qslStatus(received).first().rcvd,QString("Y"));
        QVERIFY(!run(changed,db.meta(changed)->uuid,"B")); QCOMPARE(db.qslStatus(changed).first().via,QString("D"));
        QVERIFY(!run(changed,"different-uuid","D")); QVERIFY(!cards.enqueueIfUnsent(changed,db.meta(changed)->uuid,db.meta(changed)->revision+1,"D",db.path()));
        QVERIFY(!cards.enqueueIfUnsent(changed,db.meta(changed)->uuid,db.meta(changed)->revision,"D","other-log"));
    }
    void plannerQueueRunsOnFileBackedLog()
    {
        core::LogDatabase db; QVERIFY(db.open(m_dir.filePath("planner-queue.sqlite")));
        const auto added=db.insertQso(core::AdifRecord{{"CALL","K1ABC"},{"QSO_DATE","20261001"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}},"import");
        QVERIFY(added.id>0); const auto meta=db.meta(added.id); QVERIFY(meta.has_value());
        QslCardController::Context ctx; ctx.db=&db; QslCardController cards(ctx);
        QVERIFY(cards.enqueueIfUnsent(added.id,meta->uuid,meta->revision,"B",db.path()));
        QTRY_VERIFY_WITH_TIMEOUT(!cards.operationBusy(),10000); QCOMPARE(db.qslStatus(added.id).first().sent,QString("Q"));
        QVERIFY(cards.enqueueIfUnsent(added.id,meta->uuid,meta->revision,"B",db.path()));
        QTRY_VERIFY_WITH_TIMEOUT(!cards.operationBusy(),10000); QVERIFY(cards.status().contains("unchanged"));
        QCOMPARE(db.qslStatus(added.id).first().sent,QString("Q"));
    }
    void plannerQueueDoesNotOverwriteConcurrentSentState()
    {
        core::LogDatabase db; QVERIFY(db.open(m_dir.filePath("planner-concurrent.sqlite")));
        const auto added=db.insertQso(core::AdifRecord{{"CALL","K1DEF"},{"QSO_DATE","20261001"},{"TIME_ON","1200"},{"BAND","20m"},{"MODE","CW"}},"import");
        QVERIFY(added.id>0); const auto meta=db.meta(added.id); QVERIFY(meta.has_value());
        QVERIFY(db.connection().transaction()); core::QslState sent; sent.service="card"; sent.sent="Y"; sent.via="B";
        QVERIFY(db.setCardState(added.id,sent));
        QslCardController::Context ctx; ctx.db=&db; QslCardController cards(ctx);
        QVERIFY(cards.enqueueIfUnsent(added.id,meta->uuid,meta->revision,"B",db.path()));
        QTest::qWait(150); QVERIFY(db.connection().commit());
        QTRY_VERIFY_WITH_TIMEOUT(!cards.operationBusy(),10000);
        QCOMPARE(db.qslStatus(added.id).first().sent,QString("Y"));
    }
    void initTestCase()
    {
        // Le impostazioni vanno in un temporaneo: una prova non tocca quelle di
        // chi la sta facendo girare.
        QVERIFY(m_dir.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("DecodiumTest"));
        QCoreApplication::setApplicationName(QStringLiteral("QslCardTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir.path());
    }

    void init()
    {
        QSettings().clear();
    }

    void largePaperOperationsAndCachedRowsStayResponsive()
    {
        core::LogDatabase db;
        QVERIFY(db.open(m_dir.filePath("paper-20k.sqlite")));
        QSqlQuery seed(db.connection());
        QVERIFY2(seed.exec(QStringLiteral(
            "WITH RECURSIVE n(i) AS (VALUES(1) UNION ALL SELECT i+1 FROM n WHERE i<20000) "
            "INSERT INTO qso(id,uuid,call,qso_datetime_on,band,mode,created_at,updated_at) "
            "SELECT i,'fixture-'||i,'K'||i||'ABC','2026-11-18T12:00:00Z','20m','CW',"
            "'2026-11-18T12:00:00Z','2026-11-18T12:00:00Z' FROM n")), qPrintable(seed.lastError().text()));
        QVariantList ids; ids.reserve(20000);
        for (int id = 1; id <= 20000; ++id) ids << id;
        QslCardController::Context ctx; ctx.db = &db;
        QslCardController cards(ctx);
        int ticks = 0; qint64 longestGap = 0;
        QElapsedTimer heartbeatClock; heartbeatClock.start();
        QTimer heartbeat; heartbeat.setInterval(10);
        connect(&heartbeat, &QTimer::timeout, this, [&] {
            ++ticks;
            longestGap = qMax(longestGap, heartbeatClock.restart());
        });
        heartbeat.start();
        QElapsedTimer start; start.start();
        cards.enqueue(ids, "B");
        QVERIFY(cards.operationBusy());
        QVERIFY(start.elapsed() < 1000);
        QTRY_VERIFY_WITH_TIMEOUT(!cards.operationBusy(), 30000);
        const qint64 enqueueMs = start.elapsed();
        QVERIFY(ticks > 2);
        QCOMPARE(db.cardCounts().value("queue").toInt(), 20000);
        const auto chosen = db.cardRows("all", 0, {20000, 1});
        QCOMPARE(chosen.size(), 2);
        QCOMPARE(chosen.at(0).value("call").toString(), QStringLiteral("K1ABC"));
        QCOMPARE(chosen.at(1).value("call").toString(), QStringLiteral("K20000ABC"));
        const QString templatePath = m_dir.filePath("tiny-card.png");
        QImage templateImage(4, 4, QImage::Format_RGB32); templateImage.fill(Qt::white);
        QVERIFY(templateImage.save(templatePath));
        cards.setCardTemplate(QUrl::fromLocalFile(templatePath));
        start.restart();
        cards.writeLabels(QUrl::fromLocalFile(m_dir.filePath("labels-20k.pdf")), "l7160", 4, false);
        QVERIFY(cards.exportBusy()); QVERIFY(start.elapsed() < 1000);
        QTRY_VERIFY_WITH_TIMEOUT(!cards.exportBusy(), 30000);
        const qint64 labelsMs = start.elapsed();
        QVERIFY(QFileInfo(m_dir.filePath("labels-20k.pdf")).size() > 0);
        start.restart();
        cards.writeCardsPdf(QUrl::fromLocalFile(m_dir.filePath("cards-20k.pdf")), ids, 4);
        QVERIFY(cards.exportBusy()); QVERIFY(start.elapsed() < 1000);
        QTRY_VERIFY_WITH_TIMEOUT(!cards.exportBusy(), 30000);
        const qint64 pdfMs = start.elapsed();
        QVERIFY(QFileInfo(m_dir.filePath("cards-20k.pdf")).size() > 0);
        start.restart();
        cards.writeCardsPng(QUrl::fromLocalFile(m_dir.filePath("png-20k")), ids);
        QVERIFY(cards.exportBusy()); QVERIFY(start.elapsed() < 1000);
        QTRY_VERIFY_WITH_TIMEOUT(!cards.exportBusy(), 30000);
        const qint64 pngMs = start.elapsed();
        QVERIFY(QFileInfo::exists(m_dir.filePath("png-20k/K1ABC-20261118-1200.png")));
        QVERIFY(QFileInfo::exists(m_dir.filePath("png-20k/K20000ABC-20261118-1200.png")));
        qInfo() << "paper 20k async outputs ms labels" << labelsMs << "pdf" << pdfMs << "png" << pngMs;
        // Initial rows/counts are populated asynchronously. The old snapshot
        // remains available while a refresh is in progress.
        start.restart();
        cards.rows("queue"); cards.counts();
        QVERIFY(start.elapsed() < 1000);
        QTRY_COMPARE_WITH_TIMEOUT(cards.rows("queue").size(), 20000, 30000);
        QTRY_COMPARE_WITH_TIMEOUT(cards.counts().value("queue").toInt(), 20000, 30000);
        const int beforeTicks = ticks;
        start.restart();
        cards.markSent(ids, "");
        QVERIFY(cards.operationBusy());
        QCOMPARE(cards.rows("queue").size(), 20000);
        QTRY_VERIFY_WITH_TIMEOUT(!cards.operationBusy(), 30000);
        const qint64 sentMs = start.elapsed();
        QVERIFY(ticks > beforeTicks);
        QCOMPARE(db.cardCounts().value("sent").toInt(), 20000);
        QTRY_COMPARE_WITH_TIMEOUT(cards.rows("queue").size(), 0, 30000);
        QTRY_COMPARE_WITH_TIMEOUT(cards.counts().value("sent").toInt(), 20000, 30000);
        start.restart(); cards.drop(ids);
        QVERIFY(cards.operationBusy());
        QTRY_VERIFY_WITH_TIMEOUT(!cards.operationBusy(), 30000);
        const qint64 dropMs = start.elapsed();
        QCOMPARE(db.cardCounts().value("sent").toInt(), 0);
        QVERIFY(seed.exec("UPDATE qsl_status SET rcvd='Y' WHERE service='card' AND qso_id IN (1,2,3)"));
        QVERIFY(seed.exec("UPDATE qsl_status SET sent='Y' WHERE service='card' AND qso_id=1"));
        start.restart(); QCOMPARE(cards.enqueueUnanswered("B"), -1);
        QVERIFY(cards.operationBusy());
        QTRY_VERIFY_WITH_TIMEOUT(!cards.operationBusy(), 30000);
        const qint64 unansweredMs = start.elapsed();
        QCOMPARE(db.cardCounts().value("queue").toInt(), 2);
        QCOMPARE(db.cardCounts().value("sent").toInt(), 1);
        qInfo() << "paper 20k database" << db.path() << "enqueue ms" << enqueueMs
                << "sent ms" << sentMs << "drop ms" << dropMs << "unanswered ms" << unansweredMs
                << "heartbeat ticks" << ticks << "maximum gap ms" << longestGap;
    }

    void paperFailureRollsBackJustTheAffectedQso()
    {
        core::LogDatabase db; QVERIFY(db.open(m_dir.filePath("paper-failure.sqlite")));
        const auto a = db.insertQso({{"CALL", "K1AAA"}, {"QSO_DATE", "20261118"}, {"TIME_ON", "1200"},
                                      {"BAND", "20m"}, {"MODE", "CW"}}, "test").id;
        const auto b = db.insertQso({{"CALL", "K1BBB"}, {"QSO_DATE", "20261118"}, {"TIME_ON", "1201"},
                                      {"BAND", "20m"}, {"MODE", "CW"}}, "test").id;
        QSqlQuery trigger(db.connection());
        QVERIFY(trigger.exec(QStringLiteral("CREATE TRIGGER reject_dirty BEFORE UPDATE OF dirty ON qso "
                                             "WHEN OLD.id=%1 BEGIN SELECT RAISE(ABORT, 'fixture failure'); END").arg(a)));
        QslCardController::Context ctx; ctx.db = &db;
        QslCardController cards(ctx);
        cards.enqueue({a, b}, "B");
        QTRY_VERIFY_WITH_TIMEOUT(!cards.operationBusy(), 10000);
        QVERIFY(cards.status().contains("1 failed"));
        QVERIFY(db.qslStatus(a).isEmpty());
        QCOMPARE(db.qslStatus(b).first().sent, QStringLiteral("Q"));
        cards.markReceived(b, true);
        QVERIFY(cards.operationBusy());
        QTRY_VERIFY_WITH_TIMEOUT(!cards.operationBusy(), 10000);
        QCOMPARE(db.qslStatus(b).first().sent, QStringLiteral("Q"));
        QCOMPARE(db.qslStatus(b).first().rcvd, QStringLiteral("Y"));
    }

    void paperTransactionRollbackDoesNotCommitLaterRows()
    {
        core::LogDatabase db; QVERIFY(db.open(m_dir.filePath("paper-rollback.sqlite")));
        QVariantList ids;
        for (const char* call : {"K1AAA", "K1BBB", "K1CCC"}) {
            const auto id = db.insertQso({{"CALL", call}, {"QSO_DATE", "20261118"}, {"TIME_ON", "1200"},
                                          {"BAND", "20m"}, {"MODE", "CW"}}, "test").id;
            QVERIFY(id > 0);
            ids << id;
        }
        QSqlQuery trigger(db.connection());
        QVERIFY(trigger.exec(QStringLiteral("CREATE TRIGGER abort_paper_transaction BEFORE UPDATE OF dirty ON qso "
                         "WHEN OLD.id=%1 BEGIN SELECT RAISE(ROLLBACK, 'fixture rollback'); END")
                                 .arg(ids.at(1).toLongLong())));
        QslCardController::Context ctx; ctx.db = &db;
        QslCardController cards(ctx);
        cards.enqueue(ids, "B");
        QTRY_VERIFY_WITH_TIMEOUT(!cards.operationBusy(), 10000);
        QVERIFY2(cards.status().contains("3 failed"), qPrintable(cards.status()));
        for (const QVariant& id : ids) QVERIFY(db.qslStatus(id.toLongLong()).isEmpty());
        QCOMPARE(db.cardCounts().value("queue").toInt(), 0);
    }

    void cachedPaperRowsFollowLogSwitchAndExternalInvalidation()
    {
        core::LogDatabase db; QVERIFY(db.open(m_dir.filePath("paper-first.sqlite")));
        const auto id = db.insertQso({{"CALL", "K1AAA"}, {"QSO_DATE", "20261118"}, {"TIME_ON", "1200"},
                                       {"BAND", "20m"}, {"MODE", "CW"}}, "test").id;
        QslCardController::Context ctx; ctx.db = &db;
        QslCardController cards(ctx);
        cards.enqueue({id}, "B");
        QTRY_VERIFY_WITH_TIMEOUT(!cards.operationBusy(), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(cards.rows("queue").size(), 1, 10000);
        core::QslState state; state.service = "card"; state.sent = "Y";
        QVERIFY(db.setCardState(id, state));
        cards.invalidateCache();
        QTRY_COMPARE_WITH_TIMEOUT(cards.rows("queue").size(), 0, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(cards.rows("sent").size(), 1, 10000);
        db.close(); QVERIFY(db.open(m_dir.filePath("paper-second.sqlite")));
        QCOMPARE(cards.rows("sent").size(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(!cards.cacheLoading(), 10000);
    }

    void paperOutputFailureDoesNotReportSuccess()
    {
        core::LogDatabase db; QVERIFY(db.open(m_dir.filePath("paper-output-errors.sqlite")));
        const auto id = db.insertQso({{"CALL", "K1AAA"}, {"QSO_DATE", "20261118"}, {"TIME_ON", "1200"},
                                       {"BAND", "20m"}, {"MODE", "CW"}}, "test").id;
        QslCardController::Context ctx; ctx.db = &db;
        QslCardController cards(ctx); cards.enqueue({id}, "B");
        QTRY_VERIFY_WITH_TIMEOUT(!cards.operationBusy(), 10000);
        const QString blockerPath = m_dir.filePath("paper-not-a-directory");
        QFile blocker(blockerPath); QVERIFY(blocker.open(QIODevice::WriteOnly)); blocker.close();
        const QUrl invalid = QUrl::fromLocalFile(blockerPath + QStringLiteral("/output"));
        cards.writeLabels(invalid, "l7160", 4, false);
        QVERIFY(cards.exportBusy()); QTRY_VERIFY_WITH_TIMEOUT(!cards.exportBusy(), 10000);
        QVERIFY(!cards.status().isEmpty()); QVERIFY(cards.lastFile().isEmpty());
        cards.writeCardsPdf(invalid, {id}, 4);
        QVERIFY(cards.exportBusy()); QTRY_VERIFY_WITH_TIMEOUT(!cards.exportBusy(), 10000);
        QVERIFY(!cards.status().isEmpty()); QVERIFY(cards.lastFile().isEmpty());
        cards.writeCardsPng(invalid, {id});
        QVERIFY(cards.exportBusy()); QTRY_VERIFY_WITH_TIMEOUT(!cards.exportBusy(), 10000);
        QVERIFY(!cards.status().isEmpty()); QVERIFY(cards.lastFile().isEmpty());
    }

    void theUsualFieldsLandInTheirBoxes()
    {
        QslCardController cards{QslCardController::Context{}};
        cards.addStandardCardFields();

        const QVariantList fields = cards.cardFields();
        QCOMPARE(fields.size(), 8);

        for (const Box& box : kBoxes) {
            const QVariantMap field = fieldFor(fields, QString::fromLatin1(box.key));
            QVERIFY2(!field.isEmpty(), box.key);
            const double x = field.value(QStringLiteral("x")).toDouble();
            const double y = field.value(QStringLiteral("y")).toDouble();
            QVERIFY2(x > box.left && x < box.right,
                     qPrintable(QStringLiteral("%1: x %2 fuori da %3..%4")
                                    .arg(QLatin1String(box.key)).arg(x).arg(box.left).arg(box.right)));
            // La y e' il bordo di sopra del testo: deve stare nel riquadro, e il
            // testo che ne esce (circa 58 pixel su 1080) pure.
            QVERIFY2(y > box.top && y + 58.0 / 1080.0 < box.bottom,
                     qPrintable(QStringLiteral("%1: y %2 fuori da %3..%4")
                                    .arg(QLatin1String(box.key)).arg(y).arg(box.top).arg(box.bottom)));
            // Dentro una casella un dato sta in mezzo.
            QCOMPARE(field.value(QStringLiteral("align")).toString(), QStringLiteral("center"));
        }
    }

    void pressingItTwiceDoesNotDoubleTheFields()
    {
        QslCardController cards{QslCardController::Context{}};
        cards.addStandardCardFields();
        cards.addStandardCardFields();
        cards.addStandardCardFields();
        QCOMPARE(cards.cardFields().size(), 8);
    }

    void aFieldAlreadyThereMovesInsteadOfMultiplying()
    {
        QslCardController cards{QslCardController::Context{}};
        // Uno messo a mano nel posto sbagliato, come capita mentre si prova.
        cards.addCardField(QStringLiteral("rst"));
        cards.moveCardField(0, 0.05, 0.05);
        QCOMPARE(cards.cardFields().size(), 1);

        cards.addStandardCardFields();
        // Non se ne trova un secondo: quello che c'era si e' spostato.
        QCOMPARE(cards.cardFields().size(), 8);
        const QVariantMap rst = fieldFor(cards.cardFields(), QStringLiteral("rst"));
        QVERIFY(rst.value(QStringLiteral("x")).toDouble() > 0.8);
    }

    // La via del Cloud
    //
    // La ragione di tutto questo e' che la password di una casella non stia sul
    // computer di chi opera. Quindi la prova guarda proprio quello: che parta
    // una richiesta al Cloud, col token, la cartolina dentro, e nessun segreto.

    void theCloudRouteIsTheOneOutOfTheBox()
    {
        QslCardController cards{QslCardController::Context{}};
        QCOMPARE(cards.mail().value(QStringLiteral("route")).toString(), QStringLiteral("cloud"));
        // Senza Cloud collegato non si manda: non c'e' da dove.
        QCOMPARE(cards.mail().value(QStringLiteral("ready")).toBool(), false);
    }

    void choosingOnesOwnMailboxLooksAtTheMailboxInstead()
    {
        QslCardController::Context ctx;
        ctx.cloudAccess = [] {
            return QPair<QString, QString>{QStringLiteral("http://x"), QStringLiteral("token")};
        };
        QslCardController cards{ctx};
        QCOMPARE(cards.mail().value(QStringLiteral("ready")).toBool(), true);

        cards.setMailRoute(QStringLiteral("mailbox"));
        QCOMPARE(cards.mail().value(QStringLiteral("route")).toString(), QStringLiteral("mailbox"));
        // Il Cloud c'e' lo stesso, ma adesso conta la casella, che non c'e'.
        QCOMPARE(cards.mail().value(QStringLiteral("cloudReady")).toBool(), true);
        QCOMPARE(cards.mail().value(QStringLiteral("ready")).toBool(), false);

        // Una via che non esiste non lascia il programma senza via d'uscita.
        cards.setMailRoute(QStringLiteral("piccioni"));
        QCOMPARE(cards.mail().value(QStringLiteral("route")).toString(), QStringLiteral("cloud"));
    }

    void theCardGoesToTheCloudWithTheTokenAndNoPassword()
    {
        core::LogDatabase db;
        QVERIFY2(db.open(QStringLiteral(":memory:")), qPrintable(db.lastError()));
        const core::InsertResult added = db.insertQso(
            {{"CALL", "dl9zzt"}, {"QSO_DATE", "20260218"}, {"TIME_ON", "101500"},
             {"FREQ", "14.084"}, {"MODE", "FT2"}, {"RST_SENT", "-10"},
             {"STATION_CALLSIGN", "IU8LMC"}},
            QStringLiteral("test"));
        QCOMPARE(added.status, core::InsertResult::Status::Inserted);

        FakeCloud cloud;
        QVERIFY(cloud.listen(QHostAddress::LocalHost));

        QslCardController::Context ctx;
        ctx.db = &db;
        ctx.station = [] {
            return QVariantMap{{QStringLiteral("call"), QStringLiteral("IU8LMC")}};
        };
        ctx.cloudAccess = [&cloud] {
            return QPair<QString, QString>{cloud.url(), QStringLiteral("un-token")};
        };
        ctx.replyTo = [] { return QStringLiteral("iu8lmc@example.it"); };
        ctx.emailFor = [](const QString&, std::function<void(const QString&, const QString&)> done) {
            done(QStringLiteral("dl9zzt@example.de"), QString());
        };

        QslCardController cards{ctx};
        cards.addStandardCardFields();
        cards.enqueue({QVariant(added.id)}, QStringLiteral("E"));

        QSignalSpy done(&cards, &QslCardController::changed);
        cards.sendCardsByEmail({});
        QVERIFY2(done.wait(15000), "il Cloud non ha ricevuto niente");

        const QJsonObject posted = QJsonDocument::fromJson(cloud.body).object();
        QVERIFY(cloud.request.startsWith("POST /v1/qsl/mail "));
        QCOMPARE(cloud.header("Authorization"), QByteArray("Bearer un-token"));
        QCOMPARE(posted.value(QStringLiteral("to")).toString(), QStringLiteral("dl9zzt@example.de"));
        // Chi risponde arriva all'operatore, non alla casella del servizio.
        QCOMPARE(posted.value(QStringLiteral("replyTo")).toString(),
                 QStringLiteral("iu8lmc@example.it"));
        QVERIFY(posted.value(QStringLiteral("attachmentName")).toString()
                    .startsWith(QStringLiteral("DL9ZZT-20260218")));
        // Quello che parte e' una cartolina vera: il server rifiuta tutto il resto.
        const QByteArray png = QByteArray::fromBase64(
            posted.value(QStringLiteral("attachment")).toString().toLatin1());
        QVERIFY(png.startsWith(QByteArray("\x89PNG\r\n\x1a\n", 8)));

        // E il QSO risulta mandato per via elettronica.
        QCOMPARE(cards.rows(QStringLiteral("queue")).size(), 0);
        QCOMPARE(cards.rows(QStringLiteral("sent")).size(), 1);
    }

    void stopDuringACloudSendLeavesTheQueueAlone()
    {
        core::LogDatabase db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        const core::InsertResult added = db.insertQso(
            {{"CALL", "dl9zzt"}, {"QSO_DATE", "20260218"}, {"TIME_ON", "101500"},
             {"FREQ", "14.084"}, {"MODE", "FT2"}, {"STATION_CALLSIGN", "IU8LMC"}},
            QStringLiteral("test"));

        // Un Cloud lento: accetta la connessione e risponde «mandata» mezzo
        // secondo dopo. Se «Ferma» non fermasse davvero la richiesta, quella
        // risposta arriverebbe lo stesso e la QSL risulterebbe partita.
        QTcpServer slow;
        QVERIFY(slow.listen(QHostAddress::LocalHost));
        QObject::connect(&slow, &QTcpServer::newConnection, &slow, [&slow] {
            QTcpSocket* socket = slow.nextPendingConnection();
            QTimer::singleShot(500, socket, [socket] {
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                              "Content-Length: 29\r\n\r\n{\"sent\":true,\"remaining\":99}");
                socket->disconnectFromHost();
            });
        });

        QslCardController::Context ctx;
        ctx.db = &db;
        ctx.cloudAccess = [&slow] {
            return QPair<QString, QString>{
                QStringLiteral("http://127.0.0.1:%1").arg(slow.serverPort()),
                QStringLiteral("un-token")};
        };
        ctx.emailFor = [](const QString&, std::function<void(const QString&, const QString&)> done) {
            done(QStringLiteral("dl9zzt@example.de"), QString());
        };

        QslCardController cards{ctx};
        cards.addStandardCardFields();
        cards.enqueue({QVariant(added.id)}, QStringLiteral("E"));
        cards.sendCardsByEmail({});
        QVERIFY(cards.mailBusy());

        cards.cancelMail();
        QVERIFY(!cards.mailBusy());
        // La richiesta annullata torna qui: non deve contare come mandata ne'
        // come fallita, e la QSL resta in coda per riprovare.
        QTest::qWait(1200);
        QVERIFY(!cards.mailBusy());
        QCOMPARE(cards.rows(QStringLiteral("queue")).size(), 1);
        QCOMPARE(cards.rows(QStringLiteral("sent")).size(), 0);
    }

    void delayedAddressAfterCancelCannotSendOrAffectNextBatch()
    {
        core::LogDatabase db; QVERIFY(db.open(":memory:"));
        const auto id = db.insertQso({{"CALL", "K1AAA"}, {"QSO_DATE", "20261118"}, {"TIME_ON", "1200"},
                                       {"BAND", "20m"}, {"MODE", "CW"}}, "test").id;
        FakeCloud cloud; QVERIFY(cloud.listen(QHostAddress::LocalHost));
        QList<std::function<void(const QString&, const QString&)>> callbacks;
        QslCardController::Context ctx; ctx.db = &db;
        ctx.cloudAccess = [&] { return QPair<QString, QString>{cloud.url(), "fixture-token"}; };
        ctx.emailFor = [&](const QString&, auto done) { callbacks << std::move(done); };
        QslCardController cards(ctx); cards.addStandardCardFields(); cards.enqueue({id}, "E");
        cards.sendCardsByEmail({});
        QVERIFY(cards.mailBusy()); QCOMPARE(callbacks.size(), 1);
        cards.cancelMail(); QVERIFY(!cards.mailBusy());
        cards.sendCardsByEmail({});
        QVERIFY(cards.mailBusy()); QCOMPARE(callbacks.size(), 2);
        callbacks.at(0)(QStringLiteral("old@example.invalid"), {});
        QTest::qWait(100);
        QVERIFY(cards.mailBusy());
        QVERIFY(cloud.request.isEmpty());
        callbacks.at(1)({}, QStringLiteral("no email"));
        QVERIFY(!cards.mailBusy());
        QCOMPARE(cards.rows("queue").size(), 1);
        QCOMPARE(cards.rows("sent").size(), 0);
        QCOMPARE(cards.mailStatus(), QStringLiteral("0 sent, 1 not"));
    }

    void manyMockEmailLookupsYieldAndStopBeforeAnySend()
    {
        core::LogDatabase db; QVERIFY(db.open(m_dir.filePath("mail-20k.sqlite")));
        QSqlQuery seed(db.connection());
        QVERIFY(seed.exec(QStringLiteral(
            "WITH RECURSIVE n(i) AS (VALUES(1) UNION ALL SELECT i+1 FROM n WHERE i<20000) "
            "INSERT INTO qso(id,uuid,call,qso_datetime_on,band,mode,created_at,updated_at) "
            "SELECT i,'fixture-'||i,'K'||i||'ABC','2026-11-18T12:00:00Z','20m','CW',"
            "'2026-11-18T12:00:00Z','2026-11-18T12:00:00Z' FROM n")));
        QVERIFY(seed.exec("INSERT INTO qsl_status(qso_id,service,sent,rcvd) SELECT id,'card','Q','N' FROM qso"));
        FakeCloud cloud; QVERIFY(cloud.listen(QHostAddress::LocalHost));
        QslCardController* active = nullptr;
        int lookups = 0, ticks = 0;
        QslCardController::Context ctx; ctx.db = &db;
        ctx.cloudAccess = [&] { return QPair<QString, QString>{cloud.url(), "fixture-token"}; };
        ctx.emailFor = [&](const QString&, auto done) {
            ++lookups;
            if (lookups == 10) QTimer::singleShot(0, active, [&] { active->cancelMail(); });
            done({}, QStringLiteral("fixture: no address"));
        };
        QslCardController cards(ctx); active = &cards; cards.addStandardCardFields();
        QTimer heartbeat; heartbeat.setInterval(5);
        connect(&heartbeat, &QTimer::timeout, this, [&] { ++ticks; }); heartbeat.start();
        QElapsedTimer start; start.start();
        cards.sendCardsByEmail({});
        QVERIFY(cards.mailBusy()); QVERIFY(start.elapsed() < 1000);
        QTRY_VERIFY_WITH_TIMEOUT(!cards.mailBusy(), 30000);
        QVERIFY(lookups >= 10 && lookups < 20000);
        QVERIFY(ticks > 0);
        QVERIFY(cloud.request.isEmpty());
        QCOMPARE(db.cardCounts().value("queue").toInt(), 20000);
        QCOMPARE(db.cardCounts().value("sent").toInt(), 0);
        QCOMPARE(cards.mailStatus(), QStringLiteral("Sending stopped."));
    }

    void withoutACloudNothingLeavesAndItSaysSo()
    {
        core::LogDatabase db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        const core::InsertResult added = db.insertQso(
            {{"CALL", "dl9zzt"}, {"QSO_DATE", "20260218"}, {"TIME_ON", "101500"},
             {"FREQ", "14.084"}, {"MODE", "FT2"}, {"STATION_CALLSIGN", "IU8LMC"}},
            QStringLiteral("test"));
        QslCardController::Context ctx;
        ctx.db = &db;
        QslCardController cards{ctx};
        cards.addStandardCardFields();
        cards.enqueue({QVariant(added.id)}, QStringLiteral("E"));

        cards.sendCardsByEmail({});
        // Non si prova nemmeno, e non si dice "mandata": si dice cosa manca.
        QVERIFY(!cards.mailStatus().isEmpty());
        QCOMPARE(cards.rows(QStringLiteral("queue")).size(), 1);
    }

    void emailLookupStopsWhenLogChanges()
    {
        core::LogDatabase db;
        const QString first = m_dir.filePath("mail-origin-lookup.sqlite");
        const QString second = m_dir.filePath("mail-other-lookup.sqlite");
        QVERIFY(db.open(first));
        const auto a = db.insertQso({{"CALL", "K1AAA"}, {"QSO_DATE", "20261118"},
                                     {"TIME_ON", "101500"}, {"BAND", "20m"}, {"MODE", "CW"}}, "test");
        core::QslState state; state.service = "card"; state.sent = "Q"; state.via = "E";
        QVERIFY(db.setCardState(a.id, state));
        db.close();
        QVERIFY(db.open(second));
        const auto b = db.insertQso({{"CALL", "K2BBB"}, {"QSO_DATE", "20261118"},
                                     {"TIME_ON", "101500"}, {"BAND", "20m"}, {"MODE", "CW"}}, "test");
        QCOMPARE(a.id, b.id);
        QVERIFY(db.setCardState(b.id, state));
        db.close(); QVERIFY(db.open(first));
        FakeCloud cloud; QVERIFY(cloud.listen(QHostAddress::LocalHost));
        std::function<void(const QString&, const QString&)> callback;
        QslCardController::Context ctx; ctx.db = &db;
        ctx.cloudAccess = [&] { return QPair<QString, QString>{cloud.url(), "fake"}; };
        ctx.emailFor = [&](const QString&, auto done) { callback = std::move(done); };
        QslCardController cards(ctx); cards.addStandardCardFields();
        cards.sendCardsByEmail({QVariant(a.id)});
        QTRY_VERIFY_WITH_TIMEOUT(bool(callback), 15000);
        db.close(); QVERIFY(db.open(second));
        callback(QStringLiteral("test@example.invalid"), {});
        QTRY_VERIFY_WITH_TIMEOUT(!cards.mailBusy(), 5000);
        QVERIFY(cloud.request.isEmpty());
        QCOMPARE(db.qslStatus(b.id).first().sent, QStringLiteral("Q"));
        QVERIFY(cards.mailStatus().contains(QStringLiteral("log changed"), Qt::CaseInsensitive));
    }

    void confirmedEmailIsRecordedOnlyInOriginalLog()
    {
        core::LogDatabase db;
        const QString first = m_dir.filePath("mail-origin-confirm.sqlite");
        const QString second = m_dir.filePath("mail-other-confirm.sqlite");
        QVERIFY(db.open(first));
        const auto a = db.insertQso({{"CALL", "K1AAA"}, {"QSO_DATE", "20261118"},
                                     {"TIME_ON", "101500"}, {"BAND", "20m"}, {"MODE", "CW"}}, "test");
        core::QslState state; state.service = "card"; state.sent = "Q"; state.via = "E";
        QVERIFY(db.setCardState(a.id, state));
        db.close(); QVERIFY(db.open(second));
        const auto b = db.insertQso({{"CALL", "K2BBB"}, {"QSO_DATE", "20261118"},
                                     {"TIME_ON", "101500"}, {"BAND", "20m"}, {"MODE", "CW"}}, "test");
        QCOMPARE(a.id, b.id); QVERIFY(db.setCardState(b.id, state));
        db.close(); QVERIFY(db.open(first));
        QTcpServer cloud; QVERIFY(cloud.listen(QHostAddress::LocalHost));
        QslCardController::Context ctx; ctx.db = &db;
        ctx.cloudAccess = [&] { return QPair<QString, QString>{QStringLiteral("http://127.0.0.1:%1").arg(cloud.serverPort()), "fake"}; };
        ctx.emailFor = [](const QString&, auto done) { done(QStringLiteral("test@example.invalid"), {}); };
        QslCardController cards(ctx); cards.addStandardCardFields();
        cards.sendCardsByEmail({QVariant(a.id)});
        QTRY_VERIFY_WITH_TIMEOUT(cloud.hasPendingConnections(), 15000);
        QTcpSocket* socket = cloud.nextPendingConnection(); QVERIFY(socket);
        QTRY_VERIFY_WITH_TIMEOUT(socket->bytesAvailable() > 0, 5000);
        db.close(); QVERIFY(db.open(second));
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}");
        socket->flush();
        QTRY_VERIFY_WITH_TIMEOUT(!cards.mailBusy(), 15000);
        QCOMPARE(db.qslStatus(b.id).first().sent, QStringLiteral("Q"));
        db.close(); QVERIFY(db.open(first));
        QCOMPARE(db.qslStatus(a.id).first().sent, QStringLiteral("Y"));
    }

    void confirmedEmailDoesNotMarkReplacementQso()
    {
        core::LogDatabase db;
        const QString path = m_dir.filePath("mail-replacement.sqlite");
        QVERIFY(db.open(path));
        const auto a = db.insertQso({{"CALL", "K1AAA"}, {"QSO_DATE", "20261118"},
                                     {"TIME_ON", "101500"}, {"BAND", "20m"}, {"MODE", "CW"}}, "test");
        core::QslState state; state.service = "card"; state.sent = "Q"; state.via = "E";
        QVERIFY(db.setCardState(a.id, state));
        QTcpServer cloud; QVERIFY(cloud.listen(QHostAddress::LocalHost));
        QStringList notes;
        QslCardController::Context ctx; ctx.db = &db;
        ctx.cloudAccess = [&] { return QPair<QString, QString>{QStringLiteral("http://127.0.0.1:%1").arg(cloud.serverPort()), "fake"}; };
        ctx.emailFor = [](const QString&, auto done) { done(QStringLiteral("test@example.invalid"), {}); };
        ctx.activity = [&](const QString&, const QString& message, const QString&) { notes << message; };
        QslCardController cards(ctx); cards.addStandardCardFields();
        cards.sendCardsByEmail({QVariant(a.id)});
        QTRY_VERIFY_WITH_TIMEOUT(cloud.hasPendingConnections(), 15000);
        QTcpSocket* socket = cloud.nextPendingConnection(); QVERIFY(socket);
        QTRY_VERIFY_WITH_TIMEOUT(socket->bytesAvailable() > 0, 5000);
        db.close(); QVERIFY(QFile::rename(path, path + ".original")); QVERIFY(db.open(path));
        const auto b = db.insertQso({{"CALL", "K2BBB"}, {"QSO_DATE", "20261118"},
                                     {"TIME_ON", "101500"}, {"BAND", "20m"}, {"MODE", "CW"}}, "test");
        QCOMPARE(a.id, b.id); QVERIFY(db.setCardState(b.id, state));
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}");
        socket->flush();
        QTRY_VERIFY_WITH_TIMEOUT(!cards.mailBusy(), 15000);
        QCOMPARE(db.qslStatus(b.id).first().sent, QStringLiteral("Q"));
        QVERIFY(notes.join(' ').contains(QStringLiteral("could not be recorded")));
    }

    void whatWasPutByHandAndIsNotUsualStays()
    {
        QslCardController cards{QslCardController::Context{}};
        cards.addCardField(QStringLiteral("text"));
        cards.updateCardField(0, {{QStringLiteral("text"), QStringLiteral("TNX FB QSO")}});
        cards.addStandardCardFields();

        // Gli otto di serie piu' il testo libero, che nessuno ha chiesto di
        // toccare.
        QCOMPARE(cards.cardFields().size(), 9);
        const QVariantMap free = fieldFor(cards.cardFields(), QStringLiteral("text"));
        QCOMPARE(free.value(QStringLiteral("text")).toString(), QStringLiteral("TNX FB QSO"));
    }
};

QTEST_MAIN(TestQslCardController)
#include "tst_qslcardcontroller.moc"
