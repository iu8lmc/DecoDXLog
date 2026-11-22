#include "core/LogDatabase.h"
#include "core/RadioNet.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

using namespace decolog::core;
class TestRadioNet : public QObject {
    Q_OBJECT
  private slots:
    void reportedQsoHistoryIsPagedAndSessionScoped()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        RadioNet net(&db);
        QVERIFY(net.metaObject()->indexOfMethod("reportedQsoPage(qlonglong)") >= 0);
        QVERIFY(net.start("First"));
        const qint64 first = net.session().value("id").toLongLong();
        QSqlQuery session(db.connection());
        QVERIFY(session.exec("SELECT uuid FROM radio_net_session WHERE id=" + QString::number(first)));
        QVERIFY(session.next());
        const QString uuid = session.value(0).toString();
        QSqlQuery plan(db.connection());
        QVERIFY(plan.exec("EXPLAIN QUERY PLAN SELECT id FROM qso WHERE deleted=0 AND "
                          "(CASE WHEN json_valid(adif_extra) THEN json_extract(adif_extra,'$.APP_DECOLOG_RADIO_NET_SESSION') END) IS NOT NULL AND "
                          "(CASE WHEN json_valid(adif_extra) THEN json_extract(adif_extra,'$.APP_DECOLOG_RADIO_NET_SESSION') END)='x' "
                          "AND id<999 ORDER BY id DESC LIMIT 201"));
        bool indexed = false;
        while (plan.next()) indexed |= plan.value(3).toString().contains("idx_qso_reported_radio_net");
        QVERIFY(indexed);
        for (int i = 0; i < 205; ++i) {
            AdifRecord r{{"CALL", "K1AAA"}, {"QSO_DATE", "20260101"}, {"TIME_ON", "120000"},
                         {"BAND", "20m"}, {"MODE", "CW"}, {"APP_DECOLOG_RADIO_NET_SESSION", uuid},
                         {"APP_DECOLOG_RADIO_NET_CALL", "K1AAA"}};
            QCOMPARE(db.insertQso(r, "manual", {}, true, 0, false).status, InsertResult::Status::Inserted);
        }
        QVariantList firstPage;
        QVERIFY(QMetaObject::invokeMethod(&net, "reportedQsoPage", Q_RETURN_ARG(QVariantList, firstPage), Q_ARG(qlonglong, 0)));
        QCOMPARE(firstPage.size(), 201);
        QVariantList older;
        QVERIFY(QMetaObject::invokeMethod(&net, "reportedQsoPage", Q_RETURN_ARG(QVariantList, older),
                                          Q_ARG(qlonglong, firstPage.at(199).toMap().value("id").toLongLong())));
        QCOMPARE(older.size(), 5);
        QCOMPARE(firstPage.first().toMap().value("reportedCall").toString(), QString("K1AAA"));
        QSqlQuery corrupt(db.connection());
        QVERIFY(corrupt.exec("UPDATE qso SET adif_extra='not json' WHERE id=1"));
        QVERIFY(corrupt.exec("UPDATE qso SET deleted=1 WHERE id=2"));
        QVariantList remaining;
        QVERIFY(QMetaObject::invokeMethod(&net, "reportedQsoPage", Q_RETURN_ARG(QVariantList, remaining),
                                          Q_ARG(qlonglong, firstPage.at(199).toMap().value("id").toLongLong())));
        QCOMPARE(remaining.size(), 3);
        QVERIFY(net.start("Second"));
        QVariantList empty;
        QVERIFY(QMetaObject::invokeMethod(&net, "reportedQsoPage", Q_RETURN_ARG(QVariantList, empty), Q_ARG(qlonglong, 0)));
        QVERIFY(empty.isEmpty());
        QCOMPARE(db.qsoCount(), 204);
    }
    void reminderSurvivesRestartAndCanBeCleared()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("log.sqlite");
        {
            LogDatabase db;
            QVERIFY(db.open(path));
            RadioNet net(&db);
            QVERIFY(net.metaObject()->indexOfMethod("setReminder(int)") >= 0);
            QVERIFY(net.start("Reminder"));
            bool accepted = true;
            QVERIFY(QMetaObject::invokeMethod(&net, "setReminder", Q_RETURN_ARG(bool, accepted), Q_ARG(int, 0)));
            QVERIFY(!accepted);
            QVERIFY(QMetaObject::invokeMethod(&net, "setReminder", Q_RETURN_ARG(bool, accepted), Q_ARG(int, 1)));
            QVERIFY(accepted);
            QVERIFY(net.property("reminder").toMap().value("scheduled").toBool());
            const QString deadline = net.property("reminder").toMap().value("deadline").toString();
            QSqlQuery q(db.connection());
            QVERIFY(q.exec("CREATE TRIGGER reject_reminder BEFORE INSERT ON radio_net_event WHEN "
                           "NEW.event='reminder_set' BEGIN SELECT RAISE(ABORT, 'rejected'); END"));
            QVERIFY(QMetaObject::invokeMethod(&net, "setReminder", Q_RETURN_ARG(bool, accepted), Q_ARG(int, 30)));
            QVERIFY(!accepted);
            QCOMPARE(net.property("reminder").toMap().value("deadline").toString(), deadline);
            QVERIFY(q.exec("DROP TRIGGER reject_reminder"));
            QCOMPARE(db.qsoCount(), 0);
        }
        {
            LogDatabase db;
            QVERIFY(db.open(path));
            RadioNet net(&db);
            net.load();
            QVERIFY(net.property("reminder").toMap().value("scheduled").toBool());
            QTRY_VERIFY_WITH_TIMEOUT(net.property("reminder").toMap().value("due").toBool(), 3000);
            bool cleared = false;
            QVERIFY(QMetaObject::invokeMethod(&net, "clearReminder", Q_RETURN_ARG(bool, cleared)));
            QVERIFY(cleared);
            QVERIFY(!net.property("reminder").toMap().value("scheduled").toBool());
            QCOMPARE(db.qsoCount(), 0);
        }
        {
            LogDatabase db;
            QVERIFY(db.open(path));
            RadioNet net(&db);
            net.load();
            QVERIFY(!net.property("reminder").toMap().value("scheduled").toBool());
        }
    }

    void remindersAreScopedToSessions()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        RadioNet net(&db);
        QVERIFY(net.start("First"));
        const qint64 first = net.session().value("id").toLongLong();
        QVERIFY(net.setReminder(60));
        const QString deadline = net.reminder().value("deadline").toString();
        QVERIFY(net.start("Second"));
        QVERIFY(!net.reminder().value("scheduled").toBool());
        QVERIFY(net.setReminder(120));
        QVERIFY(net.closeSession());
        QVERIFY(!net.setReminder(5));
        QVERIFY(net.clearReminder());
        QVERIFY(!net.reminder().value("scheduled").toBool());
        QVERIFY(net.selectSession(first));
        QCOMPARE(net.reminder().value("deadline").toString(), deadline);
        QVERIFY(net.setReminder(180));
        QVERIFY(net.reminder().value("deadline").toString() != deadline);
        QCOMPARE(db.qsoCount(), 0);
    }

    void pagesCompleteHistoryWithoutChangingRoster()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        RadioNet net(&db);
        QVERIFY(net.metaObject()->indexOfMethod("historyPage(qlonglong)") >= 0);
        QVERIFY(net.start("Paging"));
        QVERIFY(net.checkIn("K1AAA", "Alice", ""));
        const qint64 memberId = net.members().first().toMap().value("id").toLongLong();
        QSqlQuery plan(db.connection());
        QVERIFY(plan.exec("EXPLAIN QUERY PLAN SELECT e.id,e.event,e.recorded_at,e.detail,IFNULL(m.call,'') "
                          "FROM radio_net_event e LEFT JOIN radio_net_member m ON m.id=e.member_id "
                          "WHERE e.session_id=1 AND e.id<100000 ORDER BY e.id DESC LIMIT 201"));
        bool indexed = false;
        while (plan.next())
            indexed |= plan.value(3).toString().contains("idx_radio_net_event_session");
        QVERIFY(indexed);
        for (int i = 0; i < 405; ++i)
            QVERIFY(net.setStatus(memberId, (i % 2) ? "present" : "waiting"));
        QSet<qint64> ids;
        qint64 before = 0;
        qint64 originalNewest = 0;
        int pages = 0;
        for (;;) {
            QVariantList page;
            QVERIFY(QMetaObject::invokeMethod(&net, "historyPage", Q_RETURN_ARG(QVariantList, page),
                                              Q_ARG(qlonglong, before)));
            QVERIFY(page.size() <= 201);
            const int visible = qMin(200, int(page.size()));
            if (pages == 0) {
                originalNewest = page.first().toMap().value("id").toLongLong();
                QVERIFY(net.setStatus(memberId, "present"));
            }
            for (int i = 0; i < visible; ++i) {
                const qint64 id = page.at(i).toMap().value("id").toLongLong();
                QVERIFY(id > 0);
                QVERIFY(!ids.contains(id));
                if (before > 0)
                    QVERIFY(id < before);
                ids.insert(id);
            }
            ++pages;
            if (page.size() <= 200)
                break;
            before = page.at(199).toMap().value("id").toLongLong();
        }
        QCOMPARE(pages, 3);
        QCOMPARE(ids.size(), 407);
        QVERIFY(net.events().first().toMap().value("id").toLongLong() > originalNewest);
        QCOMPARE(net.members().size(), 1);
        QCOMPARE(db.qsoCount(), 0);
    }

    void summaryCountsCurrentStatusesAndReturns()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        RadioNet net(&db);
        QVERIFY(net.metaObject()->indexOfProperty("summary") >= 0);
        QVERIFY(net.start("Round table"));
        QVERIFY(net.checkIn("K1AAA", "Alice", ""));
        QVERIFY(net.checkIn("K1BBB", "Bob", ""));
        const qint64 bob = net.members().at(1).toMap().value("id").toLongLong();
        QVERIFY(net.setStatus(bob, "departed"));
        QVERIFY(net.checkIn("K1BBB", "", ""));
        const auto summary = net.property("summary").toMap();
        QCOMPARE(summary.value("participants").toInt(), 2);
        QCOMPARE(summary.value("present").toInt(), 2);
        QCOMPARE(summary.value("departed").toInt(), 0);
        QCOMPARE(summary.value("checkIns").toInt(), 3);
        QVERIFY(net.setStatus(bob, "waiting"));
        QCOMPARE(net.property("summary").toMap().value("waiting").toInt(), 1);
        QVERIFY(net.setStatus(bob, "absent"));
        QCOMPARE(net.property("summary").toMap().value("absent").toInt(), 1);
        QVERIFY(net.setStatus(bob, "departed"));
        QCOMPARE(net.property("summary").toMap().value("departed").toInt(), 1);
        QVERIFY(net.start("Another net"));
        QCOMPARE(net.property("summary").toMap().value("participants").toInt(), 0);
        QCOMPARE(net.property("summary").toMap().value("checkIns").toInt(), 0);
        QCOMPARE(db.qsoCount(), 0);
    }

    void sessionHasAnExportAction()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        RadioNet net(&db);
        QVERIFY(net.metaObject()->indexOfMethod("exportSession(QUrl)") >= 0);
    }

    void exportIncludesReportedQsoProvenance()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        LogDatabase db;
        QVERIFY(db.open(dir.filePath("log.sqlite")));
        RadioNet net(&db);
        QVERIFY(net.start("Test"));
        QSqlQuery q(db.connection());
        QVERIFY(q.exec("SELECT uuid FROM radio_net_session"));
        QVERIFY(q.next());
        const QString uuid = q.value(0).toString();
        AdifRecord record{{"CALL", "K1AAA"}, {"QSO_DATE", "20260101"}, {"TIME_ON", "120000"},
                          {"BAND", "20m"}, {"MODE", "CW"}, {"APP_DECOLOG_RADIO_NET_SESSION", uuid},
                          {"APP_DECOLOG_RADIO_NET_CALL", "K1BBB"}};
        QCOMPARE(db.insertQso(record, "import", {}, true, 0, false).status, InsertResult::Status::Inserted);
        const QString path = dir.filePath("net.json");
        QSignalSpy finished(&net, &RadioNet::exportFinished);
        QVERIFY(net.exportSession(QUrl::fromLocalFile(path)));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
        QVERIFY2(finished.first().at(0).toBool(), qPrintable(finished.first().at(1).toString()));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto array = QJsonDocument::fromJson(file.readAll()).object().value("reported_qsos").toArray();
        QCOMPARE(array.size(), 1);
        QCOMPARE(array.first().toObject().value("call").toString(), QString("K1AAA"));
        QCOMPARE(array.first().toObject().value("reported_member_call").toString(), QString("K1BBB"));
        QCOMPARE(array.first().toObject().value("source").toString(), QString("import"));
    }

    void exportsCompleteHistoryAndProtectsOpenLog()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString logPath = dir.filePath(QStringLiteral("log.sqlite"));
        LogDatabase db;
        QVERIFY(db.open(logPath));
        RadioNet net(&db);
        QVERIFY(net.start(QStringLiteral("Test net")));
        QVERIFY(net.checkIn(QStringLiteral("K1AAA"), QStringLiteral("Alè"), QStringLiteral("note, \"quoted\"")));
        const qint64 id = net.members().first().toMap().value(QStringLiteral("id")).toLongLong();
        for (int i = 0; i < 205; ++i)
            QVERIFY(net.setStatus(id, (i % 2) ? QStringLiteral("waiting") : QStringLiteral("present")));
        QVERIFY(!net.exportSession(QUrl::fromLocalFile(logPath)));
        QVERIFY(!net.exportSession(QUrl::fromLocalFile(logPath + QStringLiteral("-wal"))));
        QCOMPARE(db.qsoCount(), 0);
        const QString outputPath = dir.filePath(QStringLiteral("session.json"));
        QSignalSpy finished(&net, &RadioNet::exportFinished);
        QVERIFY(net.exportSession(QUrl::fromLocalFile(outputPath)));
        QVERIFY(net.exportBusy());
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
        QVERIFY2(finished.first().at(0).toBool(), qPrintable(finished.first().at(1).toString()));
        QVERIFY(!net.exportBusy());
        QFile output(outputPath);
        QVERIFY(output.open(QIODevice::ReadOnly));
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(output.readAll(), &parseError);
        QCOMPARE(parseError.error, QJsonParseError::NoError);
        const auto json = document.object();
        QCOMPARE(json.value(QStringLiteral("format")).toString(), QStringLiteral("decodxlog-radio-net-1"));
        QCOMPARE(json.value(QStringLiteral("participants")).toArray().size(), 1);
        const auto member = json.value(QStringLiteral("participants")).toArray().first().toObject();
        QCOMPARE(member.value(QStringLiteral("name")).toString(), QStringLiteral("Alè"));
        QCOMPARE(member.value(QStringLiteral("notes")).toString(), QStringLiteral("note, \"quoted\""));
        QCOMPARE(json.value(QStringLiteral("history")).toArray().size(), 207);
        QCOMPARE(net.events().size(), 200);
        QCOMPARE(db.qsoCount(), 0);
    }

    void exportReportsUnwritableDestination()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        LogDatabase db;
        QVERIFY(db.open(dir.filePath(QStringLiteral("log.sqlite"))));
        RadioNet net(&db);
        QVERIFY(net.start(QStringLiteral("Test")));
        QSignalSpy finished(&net, &RadioNet::exportFinished);
        QVERIFY(net.exportSession(QUrl::fromLocalFile(dir.filePath(QStringLiteral("missing/out.json")))));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
        QVERIFY(!finished.first().at(0).toBool());
        QVERIFY(!finished.first().at(1).toString().isEmpty());
        QVERIFY(!net.exportBusy());
        QCOMPARE(db.qsoCount(), 0);
    }

    void cancelledExportPreservesExistingDestination()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        LogDatabase db;
        QVERIFY(db.open(dir.filePath(QStringLiteral("log.sqlite"))));
        RadioNet net(&db);
        QVERIFY(net.start(QStringLiteral("Test")));
        const QVariant sessionId = net.session().value(QStringLiteral("id"));
        QSqlDatabase sql = db.connection();
        QVERIFY(sql.transaction());
        QSqlQuery q(sql);
        QVERIFY(q.prepare("INSERT INTO radio_net_event(session_id,event,recorded_at,detail) VALUES(?, 'turn', "
                          "'2026-01-01T00:00:00Z', '')"));
        for (int i = 0; i < 10000; ++i) {
            q.bindValue(0, sessionId);
            QVERIFY(q.exec());
        }
        QVERIFY(sql.commit());
        const QString path = dir.filePath(QStringLiteral("existing.json"));
        QFile original(path);
        QVERIFY(original.open(QIODevice::WriteOnly));
        QCOMPARE(original.write("original"), qint64(8));
        original.close();
        QSignalSpy finished(&net, &RadioNet::exportFinished);
        QVERIFY(net.exportSession(QUrl::fromLocalFile(path)));
        net.cancelExport();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
        QVERIFY(!finished.first().at(0).toBool());
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), QByteArray("original"));
        QCOMPARE(db.qsoCount(), 0);
    }

    void twoConnectionsShareRosterWithoutDuplicateOrQso()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("shared.sqlite"));
        LogDatabase firstDb, secondDb;
        QVERIFY(firstDb.open(path));
        QVERIFY(secondDb.open(path));
        RadioNet first(&firstDb), second(&secondDb);
        QVERIFY(first.start(QStringLiteral("Local net")));
        second.load();
        QVERIFY(first.checkIn(QStringLiteral("K1AAA"), QStringLiteral("Alice"), QString()));
        QVERIFY(second.checkIn(QStringLiteral("k1aaa"), QString(), QStringLiteral("returned")));
        QCOMPARE(first.members().size(), 1);
        QCOMPARE(second.members().size(), 1);
        QCOMPARE(first.members().first().toMap().value(QStringLiteral("notes")).toString(), QStringLiteral("returned"));
        QVERIFY(second.closeSession());
        QVERIFY(!first.checkIn(QStringLiteral("K1BBB"), QString(), QString()));
        QCOMPARE(firstDb.qsoCount(), 0);
        QCOMPARE(secondDb.qsoCount(), 0);
    }

    void rosterPersistsWithoutCreatingQsos()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("net.sqlite"));
        qint64 sessionId = 0, firstId = 0, secondId = 0;
        {
            LogDatabase db;
            QVERIFY(db.open(path));
            RadioNet net(&db);
            net.load();
            QVERIFY(net.start(QStringLiteral("Sunday net")));
            sessionId = net.session().value(QStringLiteral("id")).toLongLong();
            QVERIFY(net.checkIn(QStringLiteral("k1aaa"), QStringLiteral("Alice"), QStringLiteral("note")));
            QVERIFY(net.checkIn(QStringLiteral("K1AAB"), QStringLiteral("Bob"), QString()));
            QCOMPARE(net.members().size(), 2);
            firstId = net.members().at(0).toMap().value(QStringLiteral("id")).toLongLong();
            secondId = net.members().at(1).toMap().value(QStringLiteral("id")).toLongLong();
            QVERIFY(net.setStatus(firstId, QStringLiteral("departed")));
            QVERIFY(net.checkIn(QStringLiteral("K1AAA"), QString(), QString()));
            QCOMPARE(net.members().size(), 2);
            QCOMPARE(net.members().at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Alice"));
            QVERIFY(net.checkIn(QStringLiteral("K1AAA"), QStringLiteral("Alicia"), QStringLiteral("new note")));
            QVERIFY(net.events()
                        .first()
                        .toMap()
                        .value(QStringLiteral("detail"))
                        .toString()
                        .contains(QStringLiteral("new note")));
            QVERIFY(net.moveMember(secondId, -1));
            QCOMPARE(net.members().at(0).toMap().value(QStringLiteral("id")).toLongLong(), secondId);
            QVERIFY(net.setStatus(firstId, QStringLiteral("absent")));
            QVERIFY(net.nextTurn());
            QCOMPARE(net.session().value(QStringLiteral("currentMemberId")).toLongLong(), secondId);
            QVERIFY(net.setStatus(firstId, QStringLiteral("waiting")));
            QVERIFY(net.nextTurn());
            QCOMPARE(net.session().value(QStringLiteral("currentMemberId")).toLongLong(), firstId);
            QVERIFY(net.setStatus(firstId, QStringLiteral("departed")));
            QVERIFY(net.setStatus(secondId, QStringLiteral("departed")));
            QVERIFY(!net.nextTurn());
            QVERIFY(!net.checkIn(QStringLiteral("K1?"), QString(), QString()));
            QCOMPARE(net.members().size(), 2);
            QCOMPARE(db.qsoCount(), 0);
        }
        {
            LogDatabase db;
            QVERIFY(db.open(path));
            RadioNet net(&db);
            net.load();
            QCOMPARE(net.session().value(QStringLiteral("id")).toLongLong(), sessionId);
            QCOMPARE(net.members().size(), 2);
            QCOMPARE(net.members().at(0).toMap().value(QStringLiteral("id")).toLongLong(), secondId);
            QVERIFY(net.events().size() >= 7);
            QVERIFY(net.closeSession());
            QVERIFY(!net.checkIn(QStringLiteral("K1XYZ"), QString(), QString()));
            QVERIFY(net.reopenSession());
            QVERIFY(net.checkIn(QStringLiteral("K1XYZ"), QString(), QString()));
            QCOMPARE(db.qsoCount(), 0);
        }
    }
    void failedHistoryWriteRollsBackCheckIn()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        RadioNet net(&db);
        net.load();
        QVERIFY(net.start(QStringLiteral("Test")));
        QSqlQuery q(db.connection());
        QVERIFY(q.exec("CREATE TRIGGER reject_radio_net_event BEFORE INSERT ON radio_net_event BEGIN SELECT "
                       "RAISE(ABORT,'injected'); END"));
        QVERIFY(!net.checkIn(QStringLiteral("K1AAA"), QString(), QString()));
        QVERIFY(!net.error().isEmpty());
        QVERIFY(net.members().isEmpty());
        QCOMPARE(db.qsoCount(), 0);
    }
};
QTEST_MAIN(TestRadioNet)
#include "tst_radionet.moc"
