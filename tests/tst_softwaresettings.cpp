#include <QDir>
#include <QImage>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

class SoftwareSettingsTest : public QObject
{
    Q_OBJECT
private slots:
    void settingsWindowRenders();
};

void SoftwareSettingsTest::settingsWindowRenders()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    env.insert(QStringLiteral("QT_QUICK_BACKEND"), QStringLiteral("software"));
    env.insert(QStringLiteral("HOME"), dir.path());
    env.insert(QStringLiteral("DECODXLOG_DATA_ROOT"), dir.filePath(QStringLiteral("data")));
    env.insert(QStringLiteral("DECODXLOG_GRAB_MS"), QStringLiteral("6000"));
    QProcess app;
    app.setProcessEnvironment(env);
    app.start(QStringLiteral(DECOLOG_APP), {
        QStringLiteral("--software-renderer"), QStringLiteral("--settings"), dir.filePath(QStringLiteral("settings")),
        QStringLiteral("--db"), dir.filePath(QStringLiteral("test.sqlite")), QStringLiteral("--port"), QStringLiteral("0"),
        QStringLiteral("--show"), QStringLiteral("setup:4:550"), QStringLiteral("--grab"), dir.filePath(QStringLiteral("setup.png"))
    });
    QVERIFY2(app.waitForStarted(), qPrintable(app.errorString()));
    QVERIFY2(app.waitForFinished(30000), qPrintable(app.errorString()));
    QCOMPARE(app.exitStatus(), QProcess::NormalExit);
    QCOMPARE(app.exitCode(), 0);

    // The setup window is captured alongside the main window. A blank software
    // scene has only its background colour in the interior; rendered controls
    // and text introduce many different colours there.
    const QImage setup(dir.filePath(QStringLiteral("setup-2.png")));
    QVERIFY(!setup.isNull());
    QVERIFY(setup.width() >= 800 && setup.height() >= 600);
    QSet<QRgb> colours;
    for (int y = setup.height() / 5; y < setup.height() * 4 / 5; y += 3)
        for (int x = setup.width() / 4; x < setup.width() * 3 / 4; x += 3)
            colours.insert(setup.pixel(x, y));
    QVERIFY2(colours.size() > 20, "The settings window appears to contain only a blank background");
}

QTEST_GUILESS_MAIN(SoftwareSettingsTest)
#include "tst_softwaresettings.moc"
