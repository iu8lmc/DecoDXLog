#include "app/QsoTableModel.h"
#include "core/LogDatabase.h"
#include <QFile>
#include <QQuickView>
#include <QQuickItem>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QSqlQuery>
#include <utility>

#include <QQmlContext>
#include <QTemporaryDir>
#include <QTest>

using namespace decolog::core;
using decolog::app::QsoTableModel;

class TestQsoTableView : public QObject {
    Q_OBJECT
private slots:
    void qslCompletionAtDeepScrollAB()
    {
        LogDatabase db; QVERIFY(db.open(":memory:"));
        QByteArray adif;
        for (int i = 0; i < 20000; ++i) {
            const QByteArray call = "K" + QByteArray::number(i) + "ABC";
            adif += "<CALL:" + QByteArray::number(call.size()) + ">" + call
                    + "<QSO_DATE:8>20261117<TIME_ON:6>120000<BAND:3>20m<MODE:2>CW<EOR>\n";
        }
        QCOMPARE(db.importAdif(adif).inserted, 20000);
        QsoTableModel model(&db);
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QFile qml(dir.filePath("Table.qml")); QVERIFY(qml.open(QIODevice::WriteOnly));
        qml.write(R"QML(import QtQuick
TableView {
    width: 900; height: 400; model: qsoModel
    columnWidthProvider: function(c) { return 80 }
    rowHeightProvider: function(r) { return 30 }
    delegate: Rectangle { required property var display; implicitWidth: 80; implicitHeight: 30 }
    function scrollDeep() { contentY = 300000 }
    function currentTopRow() { return rowAtIndex(0) }
})QML"); qml.close();
        QQuickView view;
        view.rootContext()->setContextProperty("qsoModel", &model);
        view.setSource(QUrl::fromLocalFile(qml.fileName()));
        QCOMPARE(view.status(), QQuickView::Ready);
        view.show();
        QTest::qWait(150);
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "scrollDeep"));
        QTRY_VERIFY_WITH_TIMEOUT(view.rootObject()->property("contentY").toDouble() > 100000, 5000);
        QTest::qWait(150);
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        QElapsedTimer elapsed;
        elapsed.start(); model.reload(); QCoreApplication::processEvents();
        const qint64 oldMs = elapsed.elapsed();
        QCOMPARE(resets.size(), 1);
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "scrollDeep"));
        QTest::qWait(150);
        const qint64 id = model.idAt(10000);
        QslState state; state.service = "eqsl"; state.sent = "Y";
        QVERIFY(db.setQslState(id, state));
        resets.clear();
        elapsed.restart(); model.reloadAfterQslUpdate(); QCoreApplication::processEvents();
        const qint64 newMs = elapsed.elapsed();
        QCOMPARE(resets.size(), 0);
        QVERIFY(view.rootObject()->property("contentY").toDouble() > 100000);

        qInfo() << "20k deep scroll QSL refresh A reset ms" << oldMs << "B preserved ms" << newMs;

        QList<qint64> deleted;
        for (int row = 10000; row < 10005; ++row) deleted << model.idAt(row);
        for (qint64 qsoId : std::as_const(deleted)) QVERIFY(db.softDeleteQso(qsoId));
        model.removeDeletedIds(deleted);
        QCoreApplication::processEvents();
        QCOMPARE(resets.size(), 0);
        QCOMPARE(model.count(), 19995);
        for (qint64 qsoId : std::as_const(deleted)) QCOMPARE(model.rowForId(qsoId), -1);
        QVERIFY(view.rootObject()->property("contentY").toDouble() > 100000);

        // Sparse selections exercise many separate row-removal notifications.
        QList<qint64> sparse;
        for (int row = 0; row < model.count(); ++row)
            if (model.idAt(row) % 2 == 0) sparse << model.idAt(row);
        QSqlQuery remove(db.connection());
        QVERIFY(remove.exec("UPDATE qso SET deleted = 1 WHERE id % 2 = 0"));
        elapsed.restart();
        model.removeDeletedIds(sparse);
        QCoreApplication::processEvents();
        qInfo() << "20k sparse deleted rows" << sparse.size() << "UI processing ms" << elapsed.elapsed();
        QCOMPARE(resets.size(), 0);
    }
    void insertUpdatesVisibleCells()
    {
        LogDatabase db;
        QVERIFY(db.open(":memory:"));
        auto add = [&db](const char* call, const char* time) {
            return db.insertQso({{"CALL", call}, {"QSO_DATE", "20261117"}, {"TIME_ON", time},
                                 {"BAND", "20m"}, {"MODE", "CW"}}, "manual").id;
        };
        QVERIFY(add("K1OLD", "1200") > 0);
        QsoTableModel model(&db);
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QFile qml(dir.filePath("Table.qml")); QVERIFY(qml.open(QIODevice::WriteOnly));
        qml.write(R"QML(import QtQuick
TableView {
    id: table
    width: 900; height: 400
    model: qsoModel
    columnWidthProvider: function(c) { return 80 }
    rowHeightProvider: function(r) { return 30 }
    delegate: Rectangle {
        required property var display
        required property var qsoId
        required property int row
        implicitWidth: 80; implicitHeight: 30
    }
    Connections { target: qsoModel; function onRowsInserted() { Qt.callLater(function() { table.forceLayout(); qsoModel.refreshAfterInsert() }) } }
    function visibleValues(row) {
        const item = itemAtCell(Qt.point(1, row));
        return item ? [String(item.display), String(item.qsoId), item.row] : [];
    }
})QML"); qml.close();
        QQuickView view;
        view.rootContext()->setContextProperty("qsoModel", &model);
        view.setSource(QUrl::fromLocalFile(qml.fileName()));
        QCOMPARE(view.status(), QQuickView::Ready);
        view.show();
        auto values = [&view](int row) {
            QVariant result;
            QMetaObject::invokeMethod(view.rootObject(), "visibleValues", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, row));
            return result.toList();
        };
        QTRY_COMPARE_WITH_TIMEOUT(values(0).size(), 3, 3000);
        QCOMPARE(values(0).at(0).toString(), QString("K1OLD"));
        const qint64 fresh = add("K1NEW", "1201"); QVERIFY(fresh > 0);
        model.insertQso(fresh);
        QTRY_COMPARE_WITH_TIMEOUT(values(1).size(), 3, 3000);
        QCOMPARE(model.count(), 2);
        QTRY_COMPARE_WITH_TIMEOUT(values(0).at(0).toString(), QString("K1NEW"), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(values(1).at(0).toString(), QString("K1OLD"), 3000);
        QCOMPARE(values(0).at(1).toLongLong(), fresh);
        QVERIFY(values(1).at(1).toLongLong() != fresh);
        const qint64 newest = add("K1TOP", "1202"); QVERIFY(newest > 0);
        model.insertQso(newest);
        QTRY_COMPARE_WITH_TIMEOUT(values(2).size(), 3, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(values(0).at(0).toString(), QString("K1TOP"), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(values(1).at(0).toString(), QString("K1NEW"), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(values(2).at(0).toString(), QString("K1OLD"), 3000);
        QCOMPARE(model.count(), 3);
    }
};
QTEST_MAIN(TestQsoTableView)
#include "tst_qsotableview.moc"
