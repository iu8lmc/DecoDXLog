#include "core/LogDatabase.h"
#include "core/RadioNet.h"

#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

using namespace decolog::core;
class TestRadioNet : public QObject {
    Q_OBJECT
  private slots:
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
