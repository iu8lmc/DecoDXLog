#include "app/CallListController.h"
#include "ThemeManager.h"
#include <QFile>
#include <QFontDatabase>
#include <QImage>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickView>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using decolog::app::CallListController;
namespace {
QList<QQuickItem*> items(QQuickItem* root, const QString& name) {
    QList<QQuickItem*> found;
    if (root->objectName() == name && root->isVisible()) found.append(root);
    for (auto* child : root->childItems()) found.append(items(child, name));
    return found;
}
bool writeFake(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
}
class TestCallListView : public QObject {
    Q_OBJECT
    QTemporaryDir m_settings;
private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("DecoDXLogTest");
        QCoreApplication::setApplicationName("tst_calllistview");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
        QQuickStyle::setStyle("Basic");
        QGuiApplication::setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
    }
    void realViews_data() {
        QTest::addColumn<int>("width"); QTest::addColumn<QString>("theme"); QTest::addColumn<QString>("format");
        QTest::newRow("narrow-classic") << 260 << QString("Ocean Blue") << QString("csv");
        QTest::newRow("wide-classic") << 760 << QString("Ocean Blue") << QString("csv");
        QTest::newRow("narrow-glass") << 320 << QString("Liquid Glass") << QString("csv");
        QTest::newRow("manual-columns") << 400 << QString("Ocean Blue") << QString("mapped");
        QTest::newRow("n1mm") << 480 << QString("Ocean Blue") << QString("n1mm");
    }
    void realViews() {
        QFETCH(int, width); QFETCH(QString, theme); QFETCH(QString, format);
        QTemporaryDir temp;
        const QString path = temp.filePath("fiction.csv");
        QByteArray csv("Call,Name,Club,Number\nZZ0TEST1,Invented name,Imaginary club alpha,001\n"
                       "ZZ0TEST1,<b>literal text</b>,Imaginary club beta,002\n");
        if (format == "mapped") csv = "Person;Association;Id;Identity\nInvented name;Imaginary club alpha;001;ZZ0TEST1\n"
                                       "<b>literal text</b>;Imaginary club beta;002;ZZ0TEST1\n";
        if (format == "n1mm") csv = "!!Order!!,Call,Name,Misc,UserText\nZZ0TEST1,Invented name,001,Imaginary club alpha\n"
                                     "ZZ0TEST1,<b>literal text</b>,002,Imaginary club beta\n";
        QVERIFY(writeFake(path, csv));
        CallListController lists(temp.filePath("cache"));
        QStringList warnings;
        QQuickView settings;
        connect(settings.engine(), &QQmlEngine::warnings, &settings, [&](const QList<QQmlError>& errors) {
            for (const auto& error : errors) warnings.append(error.toString());
        });
        auto* palette = settings.engine()->singletonInstance<decodium::ui::ThemeManager*>("Decodium.UI", "Theme");
        QVERIFY(palette); palette->setCurrentTheme(theme);
        settings.setColor(palette->property("panelColor").value<QColor>());
        settings.setResizeMode(QQuickView::SizeRootObjectToView);
        settings.resize(qMax(400, width), 1100);
        settings.setInitialProperties({{"controller", QVariant::fromValue<QObject*>(&lists)}});
        settings.setSource(QUrl::fromLocalFile(QStringLiteral(QML_DIR "/CallListSettings.qml")));
        QVERIFY2(settings.status() == QQuickView::Ready, qPrintable(warnings.join('\n')));
        settings.show();
        QVERIFY(settings.rootObject()->setProperty("fileUrl", QUrl::fromLocalFile(path)));
        QVERIFY(QMetaObject::invokeMethod(settings.rootObject(), "inspectFile"));
        QTRY_VERIFY(!lists.busy());
        QTRY_VERIFY(settings.rootObject()->property("previewCurrent").toBool());
        auto imports = items(settings.rootObject(), "callListImport");
        QCOMPARE(imports.size(), 1);
        if (format == "mapped") {
            QVERIFY(!imports.first()->isEnabled());
            const auto callColumns = items(settings.rootObject(), "callListCallColumn");
            QCOMPARE(callColumns.size(), 1);
            QVERIFY(callColumns.first()->setProperty("currentIndex", 4));
            for (const QString& role : {QString("nameColumn"), QString("clubColumn"), QString("memberColumn")}) {
                const auto selectors = items(settings.rootObject(), "callListMap_" + role);
                QCOMPARE(selectors.size(), 1);
                const int choice = role == "nameColumn" ? 1 : role == "clubColumn" ? 2 : 3;
                selectors.first()->setProperty("currentIndex", choice);
                QVERIFY(QMetaObject::invokeMethod(selectors.first(), "activated", Q_ARG(int, choice)));
            }
        }
        QVERIFY(imports.first()->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(imports.first(), "clicked"));
        QTRY_VERIFY(!lists.busy());
        QCOMPARE(lists.sources().size(), 1);
        QCOMPARE(lists.lookup("ZZ0TEST1").first().toMap().value("entries").toList().size(), 2);
        if (format == "mapped") {
            const auto entry = lists.lookup("ZZ0TEST1").first().toMap().value("entries").toList().first().toMap();
            QCOMPARE(entry.value("club").toString(), QString("Imaginary club alpha"));
            QVERIFY(entry.value("fields").toList().contains(QVariantMap{{"label", "MEMBER"}, {"value", "001"}}));
        }

        QQuickView matches;
        connect(matches.engine(), &QQmlEngine::warnings, &matches, [&](const QList<QQmlError>& errors) {
            for (const auto& error : errors) warnings.append(error.toString());
        });
        matches.engine()->singletonInstance<decodium::ui::ThemeManager*>("Decodium.UI", "Theme")->setCurrentTheme(theme);
        matches.setColor(palette->property("panelColor").value<QColor>());
        matches.setResizeMode(QQuickView::SizeRootObjectToView);
        matches.resize(width, 440);
        matches.setInitialProperties({{"controller", QVariant::fromValue<QObject*>(&lists)}, {"callsign", "ZZ0TEST1"}});
        matches.setSource(QUrl::fromLocalFile(QStringLiteral(QML_DIR "/CallListMatches.qml")));
        QVERIFY2(matches.status() == QQuickView::Ready, qPrintable(warnings.join('\n')));
        matches.show();
        QTRY_COMPARE(items(matches.rootObject(), "callListEntry").size(), 2);
        QTest::qWait(50);
        const auto entries = items(matches.rootObject(), "callListEntry");
        for (auto* entry : entries) {
            QVERIFY(entry->width() <= width + 1);
            QVERIFY(entry->property("contentWidth").toDouble() <= entry->width() + 1);
            QVERIFY(entry->height() >= entry->property("contentHeight").toDouble() - 1);
        }
        QVERIFY(entries.last()->property("text").toString().contains("<b>literal text</b>"));
        QVERIFY(!matches.grabWindow().isNull());
        const QString screenshots = qEnvironmentVariable("DECODXLOG_TEST_SCREENSHOTS");
        if (!screenshots.isEmpty()) {
            QVERIFY(matches.grabWindow().save(screenshots + "/matches-" + QString::number(width) + ".png"));
            QVERIFY(settings.grabWindow().save(screenshots + "/settings-" + QString::number(width) + ".png"));
        }
        const QString id = lists.sources().first().toMap().value("id").toString();
        lists.setEnabled(id, false); QTRY_VERIFY(!lists.busy());
        QTRY_VERIFY(items(matches.rootObject(), "callListEntry").isEmpty());
        lists.setEnabled(id, true); QTRY_VERIFY(!lists.busy());
        QTRY_COMPARE(items(matches.rootObject(), "callListEntry").size(), 2);
        matches.rootObject()->setProperty("callsign", "ZZ0UNKNOWN");
        QTRY_VERIFY(items(matches.rootObject(), "callListEntry").isEmpty());
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }
};
QTEST_MAIN(TestCallListView)
#include "tst_calllistview.moc"
