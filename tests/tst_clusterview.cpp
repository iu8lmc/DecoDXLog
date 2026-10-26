// Issue #5: exercise the actual cluster QML, not just source-text assertions.
// No external cluster, ARRL download, radio or user settings are used.
#include "app/ClusterController.h"
#include "ThemeManager.h"

#include <QDir>
#include <QFontDatabase>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlPropertyMap>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickView>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace decolog::core;
using decolog::app::ClusterController;
using decolog::app::SpotModel;

class ContestFixture : public QObject {
    Q_OBJECT
public:
    Q_INVOKABLE QVariantMap spotValue(const QString&, const QString&, const QString&) const
    { return {}; }
};

class ViewContext : public QQmlPropertyMap {
    Q_OBJECT
public:
    ViewContext() : QQmlPropertyMap(this, nullptr) {}
};

namespace {
EnrichedSpot spot(const QString& call, int status)
{
    EnrichedSpot result;
    result.spot.dxCall = call;
    result.spot.band = QStringLiteral("20m");
    result.spot.mode = QStringLiteral("CW");
    result.spot.freqKhz = 14025.0;
    result.spot.time = QDateTime::currentDateTimeUtc();
    result.spot.spotter = QStringLiteral("IK8TEST");
    result.entity = QStringLiteral("Test entity");
    result.status = status;
    return result;
}

QList<QQuickItem*> badges(QQuickItem* root)
{
    QList<QQuickItem*> result;
    if (root->objectName() == QLatin1String("clusterLotwBadge") && root->isVisible())
        result.append(root);
    for (auto* child : root->childItems())
        result.append(badges(child));
    return result;
}

QString badgeCall(QQuickItem* badge)
{
    for (auto* sibling : badge->parentItem()->childItems()) {
        if (sibling->objectName() == QLatin1String("clusterCallText"))
            return sibling->property("text").toString();
    }
    return {};
}
} // namespace

class TestClusterView : public QObject {
    Q_OBJECT
    QTemporaryDir m_settings;

private slots:
    void initTestCase()
    {
        QVERIFY(m_settings.isValid());
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("DecoDXLogTest");
        QCoreApplication::setApplicationName("tst_clusterview");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
        QQuickStyle::setStyle(QStringLiteral("Basic"));
        QGuiApplication::setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
    }

    void init() { QSettings().clear(); }

    void lotwRoleMatchesFilterAndUpdates()
    {
        LotwUsers users;
        const QDate today = QDate::currentDate();
        const QByteArray csv = "4O4A," + today.toString(Qt::ISODate).toUtf8() + "\nK1OLD,"
            + today.addDays(-366).toString(Qt::ISODate).toUtf8() + "\n";
        QCOMPARE(users.load(csv), 2);
        SpotModel model;
        for (const QString& call : {QStringLiteral("4O4A"), QStringLiteral("4O4A/P"),
                                    QStringLiteral("K1OLD"), QStringLiteral("K1UNKNOWN")}) {
            model.add(spot(call, StatusWorkedBand | StatusUnconfirmed
                           | (users.isActive(call) ? StatusLotwUser : 0)), nullptr);
        }
        QCOMPARE(model.count(), 4);
        QCOMPARE(model.roleNames().value(SpotModel::LotwRole), QByteArray("lotw"));
        for (int i = 0; i < model.count(); ++i) {
            const QVariantMap row = model.get(i);
            QCOMPARE(row.value("lotw").toBool(), row.value("call").toString().startsWith("4O4A"));
            QCOMPARE(row.value("statusLabel").toString(), QStringLiteral("WORKED"));
        }
        SpotFilter filter;
        filter.onlyLotw = true;
        model.setFilter(filter);
        QCOMPARE(model.count(), 2);
        QCOMPARE(model.totalCount(), 4);
        QSignalSpy changed(&model, &QAbstractItemModel::modelReset);
        model.restatus([](const EnrichedSpot& e) { return e.status & ~StatusLotwUser; });
        QCOMPARE(changed.count(), 1);
        QCOMPARE(model.count(), 0);
        filter.onlyLotw = false;
        model.setFilter(filter);
        QCOMPARE(model.count(), 4);
        for (int i = 0; i < model.count(); ++i)
            QVERIFY(!model.get(i).value("lotw").toBool());
    }

    void badgeInRealPanel_data()
    {
        QTest::addColumn<bool>("compact");
        QTest::addColumn<bool>("contest");
        QTest::addColumn<bool>("hideEntity");
        QTest::addColumn<QString>("theme");
        QTest::newRow("normal") << false << false << false << QStringLiteral("Ocean Blue");
        QTest::newRow("compact-no-entity") << true << false << true << QStringLiteral("Ocean Blue");
        QTest::newRow("contest") << false << true << true << QStringLiteral("Ocean Blue");
        QTest::newRow("glass-no-entity") << false << false << true << QStringLiteral("Liquid Glass");
    }

    void badgeInRealPanel()
    {
        QFETCH(bool, compact);
        QFETCH(bool, contest);
        QFETCH(bool, hideEntity);
        QFETCH(QString, theme);
        if (hideEntity) {
            QSettings().setValue(contest ? "layout/clusterColumnsContest" : "layout/clusterColumns",
                                 QStringLiteral("call,status"));
        }
        int tuneRequests = 0;
        ClusterController::Context context;
        context.tuneRadio = [&tuneRequests](double, const QString&) { ++tuneRequests; return true; };
        ClusterController cluster(context); // Not started: no external connections.
        cluster.setMuted(true);
        auto* model = qobject_cast<SpotModel*>(cluster.spots());
        QVERIFY(model);
        model->add(spot("K1UNKNOWN", StatusNewDxcc), nullptr);
        model->add(spot("4O4A", StatusLotwUser | StatusWorkedBand | StatusUnconfirmed), nullptr);
        auto repeated = spot("4O4A", StatusLotwUser | StatusWorkedBand | StatusUnconfirmed);
        repeated.spot.spotter = "DL1TEST";
        model->add(repeated, nullptr);
        QCOMPARE(model->get(0).value("spotCount").toInt(), 2);
        QVERIFY(!cluster.filter().value("onlyLotw").toBool());

        ContestFixture activation;
        ViewContext decolog;
        decolog.insert("cluster", QVariant::fromValue<QObject*>(&cluster));
        decolog.insert("rotor", QVariantMap{{"enabled", false}});
        decolog.insert("activation", QVariant::fromValue<QObject*>(&activation));
        QStringList qmlWarnings;
        QQuickView view;
        connect(view.engine(), &QQmlEngine::warnings, &view, [&qmlWarnings](const QList<QQmlError>& errors) {
            for (const auto& error : errors)
                qmlWarnings.append(error.toString());
        });
        view.engine()->rootContext()->setContextProperty("decolog", &decolog);
        auto* palette = view.engine()->singletonInstance<decodium::ui::ThemeManager*>("Decodium.UI", "Theme");
        QVERIFY(palette);
        palette->setCurrentTheme(theme);
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.resize(compact ? 620 : 1250, 520);
        view.setInitialProperties({{"compact", compact}, {"contestMode", contest}});
        view.setSource(QUrl::fromLocalFile(QStringLiteral(QML_DIR "/ClusterPanel.qml")));
        QVERIFY2(view.status() == QQuickView::Ready, qPrintable(view.errors().isEmpty()
                     ? QStringLiteral("ClusterPanel not ready") : view.errors().first().toString()));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QQuickItem* panel = view.rootObject();
        QTRY_COMPARE(badges(panel).size(), 1);
        QCOMPARE(badgeCall(badges(panel).first()), QStringLiteral("4O4A"));
        QCOMPARE(model->count(), 2); // No automatic LoTW-only filtering.
        QVERIFY(panel->property("lotwHint").toString().contains("not a QSO confirmation"));
        QQuickItem* badge = badges(panel).first();
        QCOMPARE(badge->width(), 16.0);
        QCOMPARE(badge->height(), 16.0);

        // The marker must not swallow selection or double-click tuning.
        QSignalSpy picked(&cluster, &ClusterController::spotPicked);
        const QPoint badgePoint = badge->mapToScene(QPointF(8, 8)).toPoint();
        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, badgePoint);
        QTRY_COMPARE(picked.count(), 1);
        QCOMPARE(picked.first().first().toString(), QStringLiteral("4O4A"));
        picked.clear();
        QTest::mouseDClick(&view, Qt::LeftButton, Qt::NoModifier, badgePoint);
        QTRY_VERIFY(!picked.isEmpty());
        QCOMPARE(tuneRequests, 1);

        // Optional screenshots for visual review of these exact test rows.
        const QString screenshots = qEnvironmentVariable("DECODXLOG_CLUSTER_SCREENSHOTS");
        if (!screenshots.isEmpty()) {
            QVERIFY(QDir().mkpath(screenshots));
            QVERIFY(view.grabWindow().save(QDir(screenshots).filePath(
                QString::fromLatin1(QTest::currentDataTag()) + ".png")));
        }

        // A user-resized narrow DX column still contains the badge and clips text.
        QVERIFY(QMetaObject::invokeMethod(panel, "setWidth", Q_ARG(QVariant, "call"), Q_ARG(QVariant, 40)));
        QTRY_VERIFY(badges(panel).first()->parentItem()->width() <= 40);
        QVERIFY(badges(panel).first()->parentItem()->clip());
        QVERIFY(badges(panel).first()->x() + 16 <= badges(panel).first()->parentItem()->width());

        // Simulate a late ARRL-list load/refresh: the badge moves to the right row.
        model->restatus([](const EnrichedSpot& e) {
            return (e.status & ~StatusLotwUser)
                | (e.spot.dxCall == QLatin1String("K1UNKNOWN") ? StatusLotwUser : 0);
        });
        QTRY_COMPARE(badges(panel).size(), 1);
        QTRY_COMPARE(badgeCall(badges(panel).first()), QStringLiteral("K1UNKNOWN"));
        auto filter = cluster.filter();
        filter["onlyLotw"] = true;
        cluster.setFilter(filter);
        QTRY_COMPARE(model->count(), 1);
        QTRY_COMPARE(badges(panel).size(), 1);
        model->restatus([](const EnrichedSpot& e) { return e.status & ~StatusLotwUser; });
        QTRY_COMPARE(badges(panel).size(), 0);
        filter["onlyLotw"] = false;
        cluster.setFilter(filter);
        QTRY_COMPARE(model->count(), 2);
        QTRY_COMPARE(badges(panel).size(), 0);
        QVERIFY2(qmlWarnings.isEmpty(), qPrintable(qmlWarnings.join('\n')));
    }
};

QTEST_MAIN(TestClusterView)
#include "tst_clusterview.moc"
