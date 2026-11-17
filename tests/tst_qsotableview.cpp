#include "app/QsoTableModel.h"
#include "core/LogDatabase.h"
#include <QFile>
#include <QQuickView>
#include <QQuickItem>

#include <QQmlContext>
#include <QTemporaryDir>
#include <QTest>

using namespace decolog::core;
using decolog::app::QsoTableModel;

class TestQsoTableView : public QObject {
    Q_OBJECT
private slots:
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
        QCOMPARE(values(0).at(0).toString(), QString("K1NEW"));
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
