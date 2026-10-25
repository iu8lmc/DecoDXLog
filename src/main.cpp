// DecoDXLog — il log di stazione della famiglia Decodium.

#include "CrashLog.h"
#include "StartupTrace.h"
#include "app/DecoLogController.h"
#include "core/Dates.h"
#include "app/WindowMirror.h"
#include "app/WorldMapItem.h"
#include "ThemeManager.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QtEndian>
#include <QFileInfo>
#include <QGuiApplication>
#include <QFontDatabase>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickGraphicsConfiguration>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>
#include <QTranslator>

#include <optional>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

#include <atomic>
#include <cstdio>
#include <memory>

namespace {

// La pipeline Qt Quick viene scelta prima di costruire QGuiApplication, come in
// Decodium 4. In questo modo QSG_RHI_BACKEND e QT_QUICK_BACKEND sono gia' visibili
// quando Qt crea il scene graph e non solo quando la prima finestra e' mostrata.
struct GraphicsSelection
{
    QByteArray backend;
    bool explicitSelection = false;
};

QByteArray normalizedGraphicsBackend(QByteArray value)
{
    value = value.trimmed().toLower();
    if (value == "safe" || value == "warp")
        return QByteArrayLiteral("warp");
    if (value == "cpu" || value == "software" || value == "software-renderer")
        return QByteArrayLiteral("software");
    if (value == "auto" || value == "default")
        return QByteArrayLiteral("auto");
    return value;
}

QByteArray normalizedMapRenderer(QByteArray value)
{
    value = value.trimmed().toLower();
    if (value == "safe" || value == "basic" || value == "cpu")
        return QByteArrayLiteral("safe");
    if (value == "canvas" || value == "full")
        return QByteArrayLiteral("canvas");
    return value;
}

QByteArray commandLineGraphicsBackend(int argc, char* argv[], bool* explicitSelection)
{
    QByteArray result;
    bool selected = false;
    for (int i = 1; i < argc; ++i) {
        const QByteArray argument = QByteArray(argv[i]);
        if (argument == "--disable-gpu" || argument == "--software-renderer") {
            result = QByteArrayLiteral("software");
            selected = true;
        } else if (argument == "--safe-graphics") {
            result = QByteArrayLiteral("warp");
            selected = true;
        } else if (argument == "--opengl" || argument == "--vulkan"
                   || argument == "--metal" || argument == "--d3d11"
                   || argument == "--d3d12") {
            result = normalizedGraphicsBackend(argument.mid(2));
            selected = true;
        } else if (argument == "--graphics" || argument == "--render-mode") {
            if (i + 1 < argc) {
                result = normalizedGraphicsBackend(QByteArray(argv[++i]));
                selected = true;
            }
        } else if (argument.startsWith("--graphics=")
                   || argument.startsWith("--render-mode=")) {
            const int separator = argument.indexOf('=');
            result = normalizedGraphicsBackend(argument.mid(separator + 1));
            selected = true;
        }
    }
    if (explicitSelection)
        *explicitSelection = selected;
    return result;
}

bool isSupportedGraphicsBackend(const QByteArray& backend)
{
    return backend == "auto" || backend == "opengl" || backend == "vulkan"
        || backend == "metal" || backend == "d3d11" || backend == "d3d12"
        || backend == "software" || backend == "warp";
}

void graphicsStartupLog(const QByteArray& message)
{
    std::fprintf(stderr, "[Graphics] %s\n", message.constData());
}

GraphicsSelection configureGraphicsEnvironment(int argc, char* argv[])
{
    bool commandLineSelection = false;
    QByteArray backend = commandLineGraphicsBackend(argc, argv, &commandLineSelection);
    if (!commandLineSelection && qEnvironmentVariableIsSet("DECODXLOG_SAFE_GRAPHICS"))
        backend = QByteArrayLiteral("warp");
    if (!commandLineSelection && backend.isEmpty()
        && qEnvironmentVariableIsSet("DECODXLOG_GRAPHICS_BACKEND")) {
        backend = normalizedGraphicsBackend(qgetenv("DECODXLOG_GRAPHICS_BACKEND"));
        commandLineSelection = true;
    }

    if (!backend.isEmpty() && !isSupportedGraphicsBackend(backend)) {
        graphicsStartupLog("backend non riconosciuto: " + backend + "; uso auto");
        backend = QByteArrayLiteral("auto");
        commandLineSelection = true;
    }

    // Un ambiente Qt gia' impostato dall'utente ha precedenza quando non e'
    // stata chiesta una modalita' DecoDXLog esplicita. Su Linux, pero', non
    // lasciamo passare Vulkan alla cieca: alcune GPU Mesa vecchie (in
    // particolare Ivy Bridge) pubblicizzano Vulkan ma il percorso Qt Quick
    // puo' bloccare la GUI quando viene aperta la mappa. Vulkan resta opt-in
    // con DECODXLOG_ALLOW_VULKAN=1 oppure con --graphics vulkan.
    const bool externalQtBackend = qEnvironmentVariableIsSet("QSG_RHI_BACKEND")
        || qEnvironmentVariableIsSet("QT_QUICK_BACKEND");
#if defined(Q_OS_LINUX)
    const bool allowLinuxVulkan = qEnvironmentVariableIntValue("DECODXLOG_ALLOW_VULKAN") != 0;
    const QByteArray externalRhiBackend = normalizedGraphicsBackend(qgetenv("QSG_RHI_BACKEND"));
    if (!commandLineSelection && backend.isEmpty() && externalRhiBackend == "vulkan"
        && !allowLinuxVulkan) {
        qputenv("QSG_RHI_BACKEND", "opengl");
        qunsetenv("QT_QUICK_BACKEND");
        graphicsStartupLog("Vulkan esterno ignorato su Linux; uso OpenGL (per abilitarlo: DECODXLOG_ALLOW_VULKAN=1)");
        return {QByteArrayLiteral("opengl"), false};
    }
#endif
    if (!commandLineSelection && backend.isEmpty() && externalQtBackend) {
        graphicsStartupLog("backend Qt preso dall'ambiente del processo");
        return {};
    }

    if (backend.isEmpty() || backend == "auto") {
#if defined(Q_OS_MACOS)
        // Come Decodium: Metal e' il percorso GPU nativo su macOS.
        if (!externalQtBackend) {
            qputenv("QSG_RHI_BACKEND", "metal");
            graphicsStartupLog("auto -> Metal");
        }
#elif defined(Q_OS_WIN)
        // D3D11, il default di Qt Quick su Windows: con lo stesso log e le
        // stesse finestre D3D12 occupava 845 MB contro 481 (misurato sulla
        // 1.16.31), senza niente in piu' da vedere. --graphics d3d12 resta
        // disponibile per chi lo vuole.
        if (!externalQtBackend) {
            qputenv("QSG_RHI_BACKEND", "d3d11");
            graphicsStartupLog("auto -> D3D11");
        }
#else
        // OpenGL e' il percorso piu' compatibile per Qt Quick su Linux. Il
        // chiamante puo' chiedere esplicitamente Vulkan con --graphics vulkan
        // oppure DECODXLOG_ALLOW_VULKAN=1 nell'ambiente di Qt.
        qunsetenv("QT_QUICK_BACKEND");
        qputenv("QSG_RHI_BACKEND", "opengl");
        graphicsStartupLog("auto -> OpenGL su Linux (Vulkan solo con opt-in esplicito)");
        return {QByteArrayLiteral("opengl"), commandLineSelection};
#endif
        return {QByteArrayLiteral("auto"), commandLineSelection};
    }

    if (backend == "software") {
        qputenv("QT_QUICK_BACKEND", "software");
        qunsetenv("QSG_RHI_BACKEND");
        qunsetenv("QSG_RHI_PREFER_SOFTWARE_RENDERER");
        qputenv("QT_OPENGL", "software");
        graphicsStartupLog("software -> renderer CPU Qt Quick");
        return {backend, true};
    }

    if (backend == "warp") {
#if defined(Q_OS_WIN)
        qunsetenv("QT_QUICK_BACKEND");
        qputenv("QSG_RHI_BACKEND", "d3d11");
        qputenv("QSG_RHI_PREFER_SOFTWARE_RENDERER", "1");
        qputenv("QT_OPENGL", "software");
        graphicsStartupLog("safe -> D3D11 WARP software rasterizer");
        return {backend, true};
#else
        qputenv("QT_QUICK_BACKEND", "software");
        qunsetenv("QSG_RHI_BACKEND");
        qunsetenv("QSG_RHI_PREFER_SOFTWARE_RENDERER");
        qputenv("QT_OPENGL", "software");
        graphicsStartupLog("safe -> renderer CPU Qt Quick");
        return {backend, true};
#endif
    }

    qunsetenv("QT_QUICK_BACKEND");
    qputenv("QSG_RHI_BACKEND", backend);
    qunsetenv("QSG_RHI_PREFER_SOFTWARE_RENDERER");
    if (qgetenv("QT_OPENGL").trimmed().toLower() == "software")
        qunsetenv("QT_OPENGL");
    graphicsStartupLog("backend Qt Quick: " + backend);
    return {backend, true};
}

const char* graphicsApiName(QSGRendererInterface::GraphicsApi api)
{
    switch (api) {
    case QSGRendererInterface::Unknown: return "Unknown";
    case QSGRendererInterface::Software: return "Software";
    case QSGRendererInterface::OpenVG: return "OpenVG";
    case QSGRendererInterface::OpenGL: return "OpenGL";
    case QSGRendererInterface::Direct3D11: return "Direct3D11";
    case QSGRendererInterface::Vulkan: return "Vulkan";
    case QSGRendererInterface::Metal: return "Metal";
    case QSGRendererInterface::Null: return "Null";
#if QT_VERSION >= QT_VERSION_CHECK(6, 11, 0)
    case QSGRendererInterface::Direct3D12: return "Direct3D12";
#endif
    default: return "Unrecognized";
    }
}

QList<QQuickWindow*> quickWindows(QQmlApplicationEngine& engine)
{
    QList<QQuickWindow*> windows;
    for (QObject* root : engine.rootObjects()) {
        if (auto* window = qobject_cast<QQuickWindow*>(root))
            windows.append(window);
        for (QObject* child : root->findChildren<QObject*>()) {
            if (auto* window = qobject_cast<QQuickWindow*>(child))
                windows.append(window);
        }
    }
    return windows;
}

void logQuickWindowGraphics(QQuickWindow* window, const char* context)
{
    QByteArray message("Qt Quick graphics API");
    if (context && *context)
        message += QByteArray(" (") + context + ')';
    message += ": ";
    if (!window || !window->rendererInterface()) {
        message += "<non disponibile>";
    } else {
        const auto api = window->rendererInterface()->graphicsApi();
        message += graphicsApiName(api);
        if (QSGRendererInterface::isApiRhiBased(api))
            message += " / RHI";
    }
    graphicsStartupLog(message);
}

QByteArray nextGraphicsFallback(const QByteArray& activeBackend)
{
#if defined(Q_OS_WIN)
    if (activeBackend == "d3d12" || activeBackend == "auto")
        return QByteArrayLiteral("d3d11");
    if (activeBackend == "d3d11" || activeBackend == "warp")
        return QByteArrayLiteral("software");
#elif defined(Q_OS_LINUX)
    if (activeBackend == "vulkan" || activeBackend == "auto")
        return QByteArrayLiteral("opengl");
    if (activeBackend == "opengl")
        return QByteArrayLiteral("software");
#else
    if (activeBackend == "metal" || activeBackend == "auto")
        return QByteArrayLiteral("software");
#endif
    if (activeBackend != "software")
        return QByteArrayLiteral("software");
    return {};
}

QStringList argumentsWithoutGraphicsOptions(const QStringList& arguments)
{
    QStringList filtered;
    for (int i = 1; i < arguments.size(); ++i) {
        const QString argument = arguments.at(i);
        if (argument == QStringLiteral("--graphics")
            || argument == QStringLiteral("--render-mode")) {
            ++i;
            continue;
        }
        if (argument.startsWith(QStringLiteral("--graphics="))
            || argument.startsWith(QStringLiteral("--render-mode="))
            || argument == QStringLiteral("--disable-gpu")
            || argument == QStringLiteral("--software-renderer")
            || argument == QStringLiteral("--safe-graphics")
            || argument == QStringLiteral("--opengl")
            || argument == QStringLiteral("--vulkan")
            || argument == QStringLiteral("--metal")
            || argument == QStringLiteral("--d3d11")
            || argument == QStringLiteral("--d3d12")) {
            continue;
        }
        filtered.append(argument);
    }
    return filtered;
}

bool restartWithGraphicsFallback(const QByteArray& fallback, const QString& reason)
{
    if (fallback.isEmpty())
        return false;

    QByteArray chain = qgetenv("DECODXLOG_GRAPHICS_FALLBACK_CHAIN").trimmed();
    const QList<QByteArray> attempted = chain.isEmpty() ? QList<QByteArray> {} : chain.split(',');
    if (attempted.contains(fallback))
        return false;
    if (!chain.isEmpty())
        chain += ',';
    chain += fallback;

    const QStringList arguments = argumentsWithoutGraphicsOptions(QCoreApplication::arguments());
    qputenv("DECODXLOG_GRAPHICS_BACKEND", fallback);
    qputenv("DECODXLOG_GRAPHICS_FALLBACK_CHAIN", chain);
    graphicsStartupLog(QStringLiteral("scene graph error: %1; riavvio con %2")
                           .arg(reason, QString::fromLatin1(fallback)).toLocal8Bit());
    return QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                   arguments,
                                   QDir::currentPath());
}

// Copia una cartella intera, senza toccare quello che c'e' gia' di la'.
void copyTree(const QString& from, const QString& to)
{
    QDir source(from);
    if (!source.exists())
        return;
    QDir().mkpath(to);
    for (const QFileInfo& entry : source.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString target = QDir(to).filePath(entry.fileName());
        if (entry.isDir())
            copyTree(entry.absoluteFilePath(), target);
        else if (!QFile::exists(target))
            QFile::copy(entry.absoluteFilePath(), target);
    }
}

// Fino alla 0.7.0 il programma si chiamava DecoLog, e il log, le impostazioni e
// le sue cartelle portavano quel nome. Al primo avvio col nome nuovo ci si
// porta dietro tutto — *copiando*: quello di prima resta dov'e', cosi' se
// qualcosa va storto il log di vent'anni e' ancora al suo posto.
void bringForwardTheOldName()
{
    // Per le prove: DECODXLOG_DATA_ROOT sposta tutto in una cartella qualsiasi,
    // cosi' il trasloco si puo' provare senza mettere le mani nel log vero.
    const QString testRoot = qEnvironmentVariable("DECODXLOG_DATA_ROOT");
    const QString roaming = testRoot.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        : QDir(testRoot).filePath(QStringLiteral("Decodium/DecoDXLog"));
    const QString localData = testRoot.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        : QDir(testRoot).filePath(QStringLiteral("Decodium-local/DecoDXLog"));
    for (const QString& now : {roaming, localData}) {
        if (now.isEmpty() || QDir(now).exists())
            continue;
        const QString before = QDir(now).absolutePath().left(
                                   QDir(now).absolutePath().lastIndexOf(QLatin1Char('/')) + 1)
                               + QStringLiteral("DecoLog");
        if (QDir(before).exists())
            copyTree(before, now);
    }
    // Il log: si chiamava decolog.sqlite. Arriva qui con la cartella, e da
    // quella copia si fa quella col nome nuovo; poi la copia col nome vecchio
    // se ne va — l'originale, quello vero, resta dov'era.
    const QString db = QDir(roaming).filePath(QStringLiteral("decodxlog.sqlite"));
    const QString oldDb = QDir(roaming).filePath(QStringLiteral("decolog.sqlite"));
    if (QFile::exists(oldDb)) {
        if (!QFile::exists(db))
            QFile::copy(oldDb, db);
        if (QFile::exists(db))
            QFile::remove(oldDb);
    }

    // Le impostazioni: un .ini accanto alla cartella, col nome del programma.
    QSettings settings;
    const QString ini = testRoot.isEmpty()
        ? settings.fileName()
        : QDir(testRoot).filePath(QStringLiteral("Decodium/DecoDXLog.ini"));
    if (!ini.isEmpty() && !QFile::exists(ini)) {
        QString oldIni = ini;
        oldIni.replace(QStringLiteral("DecoDXLog.ini"), QStringLiteral("DecoLog.ini"));
        if (oldIni != ini && QFile::exists(oldIni)) {
            QDir().mkpath(QFileInfo(ini).absolutePath());
            QFile::copy(oldIni, ini);
        }
    }
}

} // namespace

int main(int argc, char* argv[])
{
    decolog::startupTrace("main entered");
    // I menu li disegna DecoDXLog, non Windows. Da Qt 6.8 i menu di QML possono
    // diventare menu nativi del sistema: quelli non sanno niente del tema e su
    // uno sfondo scuro scrivono nero su nero — sottomenu, tendine e il menu del
    // tasto destro dentro i campi di testo diventavano illeggibili.
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeMenuWindows);
    // Qt Multimedia serve solo per l'audio (avviso del cluster, voice keyer,
    // decoder CW), ma il backend FFmpeg alla prima occasione prova tutte le
    // accelerazioni video: CUDA, Vulkan, D3D11VA, DXVA2... Ognuna apre il suo
    // driver (su NVIDIA anche il driver Vulkan, nvoglv64.dll, e dietro gli
    // overlay che si agganciano a Vulkan). Nessun video da decodificare:
    // lista vuota, a meno che l'utente non l'abbia gia' impostata.
    for (const char* name : {"QT_FFMPEG_DECODING_HW_DEVICE_TYPES", "QT_FFMPEG_ENCODING_HW_DEVICE_TYPES"}) {
        if (!qEnvironmentVariableIsSet(name))
            qputenv(name, ",");
    }
    const GraphicsSelection graphicsSelection = configureGraphicsEnvironment(argc, argv);
    decolog::startupTrace("QGuiApplication begin");
    QGuiApplication app(argc, argv);
    decolog::startupTrace("QGuiApplication ready; fonts begin");
#if defined(Q_OS_MACOS)
    const QStringList uiFontCandidates = {QStringLiteral("SF Pro Text"), QStringLiteral("Helvetica Neue"), QStringLiteral("Arial")};
#elif defined(Q_OS_WIN)
    const QStringList uiFontCandidates = {QStringLiteral("Segoe UI"), QStringLiteral("Arial")};
#else
    const QStringList uiFontCandidates = {QStringLiteral("Noto Sans"), QStringLiteral("DejaVu Sans"), QStringLiteral("Liberation Sans")};
#endif
    const QStringList installedFonts = QFontDatabase::families();
    decolog::startupTrace("fonts ready");
    for (const QString& family : uiFontCandidates) {
        if (installedFonts.contains(family)) {
            app.setFont(QFont(family));
            break;
        }
    }
    app.setApplicationName(QStringLiteral("DecoDXLog"));
    app.setOrganizationName(QStringLiteral("Decodium"));
    app.setApplicationVersion(QStringLiteral(DECODXLOG_VERSION));
    app.setWindowIcon(QIcon(QStringLiteral(":/decolog/decodxlog.png")));

    // Come Decodium: impostazioni in un .ini leggibile, non nel registro. Va
    // detto subito: la lingua si legge due righe piu' sotto, e prima di questa
    // riga QSettings guardava nel registro — dove la lingua scelta non c'e'
    // mai, cosi' chi sceglieva l'italiano su un Windows inglese si ritrovava
    // l'inglese lo stesso.
    QSettings::setDefaultFormat(QSettings::IniFormat);

    // Per le prove: --settings <cartella> tiene le impostazioni li' dentro
    // invece che fra quelle vere. Si legge a mano dagli argomenti perche' la
    // lingua si sceglie prima che il parser esista.
    const QStringList rawArguments = app.arguments();
    const qsizetype settingsAt = rawArguments.indexOf(QStringLiteral("--settings"));
    if (settingsAt >= 0 && settingsAt + 1 < rawArguments.size()) {
        const QString where = rawArguments.at(settingsAt + 1);
        QDir().mkpath(where);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, where);
    }

    // Prima di leggere qualsiasi cosa: se qui c'e' ancora la roba di quando il
    // programma si chiamava DecoLog, ce la si porta dietro.
    bringForwardTheOldName();

    // Se il programma cade, un file con la strada che l'ha portato li': il
    // registro di Windows tiene solo l'ultimo passo, e non basta per trovarlo.
    // Nelle prove sta nella cartella delle impostazioni di prova.
    decolog::crashlog::install(
        settingsAt >= 0 && settingsAt + 1 < rawArguments.size()
            ? rawArguments.at(settingsAt + 1)
            : QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("crash")),
        QStringLiteral(DECODXLOG_VERSION));

    // Lingua dell'interfaccia: quella scelta, o quella del sistema. L'inglese e'
    // la lingua dei sorgenti, quindi non ha un file da caricare.
    const QString configured = QSettings().value(QStringLiteral("ui/language"), QStringLiteral("auto")).toString();
    // "auto" vuol dire come il sistema. Il cinese va distinto: tradizionale a
    // Taiwan, Hong Kong e Macao, semplificato altrove — e la differenza non si
    // vede dalle prime due lettere.
    QString language = configured;
    if (configured == QLatin1String("auto")) {
        const QString whole = QLocale::system().name();          // it_IT, zh_TW…
        language = whole.left(2);
        if (language == QLatin1String("zh")) {
            language = whole.endsWith(QLatin1String("TW")) || whole.endsWith(QLatin1String("HK"))
                       || whole.endsWith(QLatin1String("MO"))
                       ? QStringLiteral("zh_TW") : QStringLiteral("zh");
        }
    }
    QTranslator appTranslator;
    QTranslator qtTranslator;
    if (language != QLatin1String("en")) {
        // Se la lingua chiesta non c'e', si prova quella senza variante
        // (zh_TW → zh); altrimenti si resta in inglese, mai a meta'.
        if (appTranslator.load(QStringLiteral(":/i18n/decodxlog_") + language)
            || (language.contains(QLatin1Char('_'))
                && appTranslator.load(QStringLiteral(":/i18n/decodxlog_")
                                      + language.section(QLatin1Char('_'), 0, 0))))
            app.installTranslator(&appTranslator);
        // Le finestre di dialogo di Qt (se presenti accanto all'eseguibile).
        if (qtTranslator.load(QStringLiteral("qtbase_") + language.section(QLatin1Char('_'), 0, 0),
                              QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
            app.installTranslator(&qtTranslator);
    }
    // I caratteri vengono dopo la lingua: Consolas e Segoe UI non hanno gli
    // ideogrammi, e in giapponese o in cinese mezza finestra sarebbe una fila
    // di quadratini. In quelle lingue si parte da MS Gothic, NSimSun, MingLiU.
    decodium::ui::ThemeManager::setLanguage(language);
    // E le date nella forma di quella lingua: in italiano giorno/mese/anno.
    decolog::core::dates::setLanguage(language);

    // L'interfaccia disegna le proprie superfici; uno stile di piattaforma
    // combatterebbe i pannelli invece di aiutarli.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("DecoDXLog station logbook"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption graphicsOption(
        QStringList {QStringLiteral("graphics"), QStringLiteral("render-mode")},
        QStringLiteral("Qt Quick graphics backend: auto, opengl, vulkan, metal, d3d11, d3d12 or software."),
        QStringLiteral("backend"));
    QCommandLineOption safeGraphicsOption(
        QStringLiteral("safe-graphics"),
        QStringLiteral("Use the safest available software graphics path (D3D11 WARP on Windows)."));
    QCommandLineOption softwareGraphicsOption(
        QStringList {QStringLiteral("disable-gpu"), QStringLiteral("software-renderer")},
        QStringLiteral("Use the GPU-independent Qt Quick software renderer."));
    QCommandLineOption mapRendererOption(
        QStringLiteral("map-renderer"),
        QStringLiteral("Map renderer: safe (no Canvas) or canvas (full map)."),
        QStringLiteral("renderer"));
    QCommandLineOption openglOption(QStringLiteral("opengl"), QStringLiteral("Use the Qt Quick OpenGL backend."));
    QCommandLineOption vulkanOption(QStringLiteral("vulkan"), QStringLiteral("Use the Qt Quick Vulkan backend."));
    QCommandLineOption metalOption(QStringLiteral("metal"), QStringLiteral("Use the Qt Quick Metal backend (macOS)."));
    QCommandLineOption d3d11Option(QStringLiteral("d3d11"), QStringLiteral("Use the Qt Quick D3D11 backend (Windows)."));
    QCommandLineOption d3d12Option(QStringLiteral("d3d12"), QStringLiteral("Use the Qt Quick D3D12 backend (Windows)."));
    parser.addOption(graphicsOption);
    parser.addOption(safeGraphicsOption);
    parser.addOption(softwareGraphicsOption);
    parser.addOption(mapRendererOption);
    parser.addOption(openglOption);
    parser.addOption(vulkanOption);
    parser.addOption(metalOption);
    parser.addOption(d3d11Option);
    parser.addOption(d3d12Option);
    QCommandLineOption dbOption(QStringLiteral("db"), QStringLiteral("Log database file."), QStringLiteral("path"));
    // Il ripristino di un backup: lo chiede la finestra "Ripristina", che
    // riapre il programma con queste due opzioni.
    QCommandLineOption restoreOption(QStringLiteral("restore-from"),
                                     QStringLiteral("Put this backup in place of the log before opening it."),
                                     QStringLiteral("file"));
    QCommandLineOption restorePidOption(QStringLiteral("restore-wait-pid"),
                                        QStringLiteral("Wait for this process to exit before restoring."),
                                        QStringLiteral("pid"));
    QCommandLineOption portOption(QStringLiteral("port"), QStringLiteral("UDP port (overrides settings)."),
                                  QStringLiteral("port"));
    QCommandLineOption importOption(QStringLiteral("import"), QStringLiteral("Import an ADIF file at startup."),
                                    QStringLiteral("file"));
    // Per provare l'interfaccia e fare le schermate senza cliccare: tema e finestra
    // di dialogo aperta all'avvio.
    QCommandLineOption themeOption(QStringLiteral("theme"), QStringLiteral("Theme to use (saved)."), QStringLiteral("name"));
    QCommandLineOption showOption(QStringLiteral("show"),
                                  QStringLiteral("Open at startup: new, qso:<id>, profiles, setup:<page>."),
                                  QStringLiteral("what"));
    QCommandLineOption grabOption(QStringLiteral("grab"),
                                  QStringLiteral("Save a screenshot of the window to this PNG after startup, then quit."),
                                  QStringLiteral("file"));
    parser.addOption(grabOption);
    // Per le prove: spot presi da un file (una riga "DX de" o JSON HamAlert ciascuna)
    // invece che dalla rete.
    QCommandLineOption spotsOption(QStringLiteral("spots"), QStringLiteral("Feed cluster spots from a file."),
                                   QStringLiteral("file"));
    parser.addOption(spotsOption);
    // Gia' letta prima del parser: qui sta solo perche' il parser non la
    // prenda per un errore.
    QCommandLineOption settingsOption(QStringLiteral("settings"),
                                      QStringLiteral("Keep settings in this folder (for tests)."),
                                      QStringLiteral("folder"));
    parser.addOption(settingsOption);
    // Idem per la propagazione: il XML del Sole letto da un file.
    QCommandLineOption solarOption(QStringLiteral("solar"), QStringLiteral("Read the solar XML from a file."),
                                   QStringLiteral("file"));
    parser.addOption(solarOption);
    // Rotore per una prova: "host:porta", o "rotctld@host:porta".
    QCommandLineOption rotorOption(QStringLiteral("rotor"), QStringLiteral("Use this rotor gateway."),
                                   QStringLiteral("[backend@]host:port"));
    parser.addOption(rotorOption);
    // Radio per una prova: "host:porta" di un rigctld gia' in piedi.
    QCommandLineOption rigOption(QStringLiteral("rig"), QStringLiteral("Use this rigctld (Hamlib)."),
                                 QStringLiteral("host:port"));
    parser.addOption(rigOption);
    // Radio per una prova via TCI: "host:porta", e "/1" per il secondo ricevitore.
    QCommandLineOption tciOption(QStringLiteral("tci"), QStringLiteral("Use this TCI server."),
                                 QStringLiteral("host:port[/trx]"));
    parser.addOption(tciOption);
    // Il decoder CW che ascolta un file invece della scheda audio (WAV mono a
    // 16 bit), per provarlo senza radio.
    QCommandLineOption cwAudioOption(QStringLiteral("cw-audio"), QStringLiteral("Feed this WAV file to the CW decoder."),
                                     QStringLiteral("file"));
    parser.addOption(cwAudioOption);
    // Server del Cloud per una prova, senza toccare le impostazioni.
    QCommandLineOption cloudOption(QStringLiteral("cloud"), QStringLiteral("Use this DecoDXLog Cloud server."),
                                   QStringLiteral("url"));
    parser.addOption(cloudOption);
    // Dove chiedere se c'e' una versione nuova: per le prove, senza andare
    // davvero su GitHub.
    QCommandLineOption updatesOption(QStringLiteral("updates-url"),
                                     QStringLiteral("Ask this address for the latest release."),
                                     QStringLiteral("url"));
    parser.addOption(updatesOption);
    parser.addOption(themeOption);
    parser.addOption(showOption);
    parser.addOption(dbOption);
    parser.addOption(restoreOption);
    parser.addOption(restorePidOption);
    parser.addOption(importOption);
    parser.addOption(portOption);
    parser.process(app);

    QString dbPath = parser.value(dbOption);
    if (dbPath.isEmpty()) {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dir);
        dbPath = QDir(dir).filePath(QStringLiteral("decodxlog.sqlite"));
    }

    decolog::startupTrace("controller begin");
    decolog::app::DecoLogController controller;
    decolog::startupTrace("controller ready; database begin");
    // Un ripristino chiesto dal programma di prima: si fa qui, a log chiuso.
    std::optional<decolog::core::logbackup::RestoreResult> restored;
    if (parser.isSet(restoreOption)) {
        restored = decolog::core::logbackup::restoreAfterExit(parser.value(restorePidOption).toLongLong(),
                                                              parser.value(restoreOption), dbPath,
                                                              controller.backupDir(),
                                                              QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    }
    controller.openDatabase(dbPath);
    if (restored)
        controller.reportRestore(*restored, parser.value(restoreOption));
    decolog::startupTrace("database ready; services begin");
    if (parser.isSet(portOption))
        controller.overrideUdpPort(parser.value(portOption).toInt());
    controller.startListening();
    controller.startN1mm();
    controller.startApi();
    controller.startDecoLink();
    controller.startHamHubLink();
    // Una schermata di prova non si collega ai nodi veri.
    if (!parser.isSet(grabOption) && !parser.isSet(spotsOption))
        controller.startCluster();
    if (parser.isSet(rotorOption)) {
        QString value = parser.value(rotorOption);
        QString backend = QStringLiteral("decorotor");
        if (value.contains(QLatin1Char('@'))) {
            backend = value.section(QLatin1Char('@'), 0, 0);
            value = value.section(QLatin1Char('@'), 1);
        }
        if (auto* rotor = qobject_cast<decolog::app::RotorController*>(controller.rotor())) {
            rotor->overrideConnection(backend, value.section(QLatin1Char(':'), 0, 0),
                                      value.section(QLatin1Char(':'), 1).toInt());
        }
    } else {
        controller.startRotor();
    }
    controller.startCloud(!parser.isSet(grabOption));
    if (parser.isSet(updatesOption)) {
        if (auto* upd = qobject_cast<decolog::app::UpdateController*>(controller.updates()))
            upd->overrideUrl(QUrl(parser.value(updatesOption)));
    }
    if (parser.isSet(rigOption)) {
        const QString value = parser.value(rigOption);
        if (auto* rig = qobject_cast<decolog::app::RigController*>(controller.rig())) {
            rig->overrideConnection(value.section(QLatin1Char(':'), 0, 0),
                                    value.section(QLatin1Char(':'), 1).toInt());
        }
    }
    if (parser.isSet(tciOption)) {
        const QString value = parser.value(tciOption);
        if (auto* rig = qobject_cast<decolog::app::RigController*>(controller.rig()))
            rig->overrideTci(value.section(QLatin1Char('/'), 0, 0), value.section(QLatin1Char('/'), 1).toInt());
    }
    if (parser.isSet(cwAudioOption)) {
        QFile wav(parser.value(cwAudioOption));
        auto* rig = qobject_cast<decolog::app::RigController*>(controller.rig());
        if (rig && wav.open(QIODevice::ReadOnly)) {
            QByteArray pcm = wav.readAll();
            int rate = 8000;
            // L'intestazione di un WAV semplice: la frequenza sta al byte 24,
            // i campioni dopo i 44 byte.
            if (pcm.startsWith("RIFF") && pcm.size() > 44) {
                rate = qFromLittleEndian<qint32>(pcm.constData() + 24);
                pcm.remove(0, 44);
            }
            rig->playTestAudio(pcm, rate);
        }
    }
    if (parser.isSet(spotsOption)) {
        QFile spotFile(parser.value(spotsOption));
        if (spotFile.open(QIODevice::ReadOnly)) {
            auto* cluster = qobject_cast<decolog::app::ClusterController*>(controller.cluster());
            // Una prova non parla dagli altoparlanti.
            cluster->setMuted(parser.isSet(grabOption));
            for (const QByteArray& line : spotFile.readAll().split('\n'))
                cluster->injectLine(QString::fromUtf8(line).trimmed());
        }
    }
    if (parser.isSet(solarOption)) {
        QFile solarFile(parser.value(solarOption));
        if (solarFile.open(QIODevice::ReadOnly)) {
            if (auto* solar = qobject_cast<decolog::app::SolarController*>(controller.solar()))
                solar->injectXml(solarFile.readAll());
        }
    }
    if (parser.isSet(cloudOption)) {
        if (auto* cloud = qobject_cast<decolog::app::CloudController*>(controller.cloud()))
            cloud->overrideServer(parser.value(cloudOption));
    }
    if (parser.isSet(importOption))
        controller.importAdif(QUrl::fromLocalFile(parser.value(importOption)));

    decolog::startupTrace("services ready; QML engine begin");
    QQmlApplicationEngine engine;
    decolog::startupTrace("QML engine ready");

    QByteArray activeGraphicsBackend = graphicsSelection.backend;
    if (activeGraphicsBackend.isEmpty()) {
        if (qEnvironmentVariableIsSet("QT_QUICK_BACKEND")
            && qgetenv("QT_QUICK_BACKEND").trimmed().toLower() == "software") {
            activeGraphicsBackend = QByteArrayLiteral("software");
        } else if (qEnvironmentVariableIsSet("QSG_RHI_BACKEND")) {
            activeGraphicsBackend = normalizedGraphicsBackend(qgetenv("QSG_RHI_BACKEND"));
        } else {
            activeGraphicsBackend = QByteArrayLiteral("auto");
        }
    }
    if (activeGraphicsBackend == "auto") {
#if defined(Q_OS_MACOS)
        activeGraphicsBackend = QByteArrayLiteral("metal");
#elif defined(Q_OS_WIN)
        activeGraphicsBackend = QByteArrayLiteral("d3d11");
#endif
    }

    // La Canvas di Qt Quick apre una strada aggiuntiva verso la GPU: anche con
    // RHI OpenGL stabile, alcuni driver Mesa/KWin smettono di presentare la
    // finestra non appena la Canvas viene resa visibile. Su Linux usiamo
    // percio' la mappa composta da item Quick semplici; non viene istanziato
    // nessun Canvas. Il disegno ricco resta disponibile esplicitamente per chi
    // ha gia' verificato il proprio driver. La stessa mappa sicura e' scelta
    // se l'interfaccia intera gira con il renderer software.
    QByteArray mapRenderer = normalizedMapRenderer(qgetenv("DECODXLOG_MAP_RENDERER"));
    if (parser.isSet(mapRendererOption))
        mapRenderer = normalizedMapRenderer(parser.value(mapRendererOption).toLatin1());
    if (!mapRenderer.isEmpty() && mapRenderer != "safe" && mapRenderer != "canvas") {
        graphicsStartupLog("renderer mappa non riconosciuto: " + mapRenderer + "; uso automatico");
        mapRenderer.clear();
    }
    bool useSafeMapRenderer = activeGraphicsBackend == "software" || activeGraphicsBackend == "warp";
#if defined(Q_OS_LINUX)
    useSafeMapRenderer = true;
#endif
    if (mapRenderer == "safe")
        useSafeMapRenderer = true;
    else if (mapRenderer == "canvas")
        useSafeMapRenderer = false;
    graphicsStartupLog(useSafeMapRenderer
                           ? "mappa -> compatibile (senza Canvas Qt Quick)"
                           : "mappa -> Canvas completa");

    const QString pipelineBackend = QString::fromLatin1(
        activeGraphicsBackend.isEmpty() ? QByteArrayLiteral("auto") : activeGraphicsBackend);
    const QString pipelineCacheDirectory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(pipelineCacheDirectory);
    const QString pipelineCacheFile = QDir(pipelineCacheDirectory).filePath(
        QStringLiteral("qsg_pipeline_cache_%1.bin").arg(pipelineBackend));

    // Decodium applica la cache della pipeline prima dell'inizializzazione del
    // scene graph. Il nome contiene il backend, cosi' un passaggio Vulkan ->
    // OpenGL/software non riusa una cache incompatibile.
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app,
                     [pipelineCacheFile](QObject* object, const QUrl&) {
        if (auto* window = qobject_cast<QQuickWindow*>(object)) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
            QQuickGraphicsConfiguration configuration;
            configuration.setPipelineCacheSaveFile(pipelineCacheFile);
            const bool canLoad = QFileInfo::exists(pipelineCacheFile);
            if (canLoad)
                configuration.setPipelineCacheLoadFile(pipelineCacheFile);
            window->setGraphicsConfiguration(configuration);
            qInfo() << "[Graphics] pipeline cache:" << pipelineCacheFile << "load=" << canLoad;
#else
            Q_UNUSED(pipelineCacheFile)
#endif
        }
    });

    // Un errore del scene graph non deve lasciare l'utente a cambiare variabili
    // a mano: il processo viene riaperto con il backend successivo della catena.
    // Il nuovo processo eredita la scelta tramite DECODXLOG_GRAPHICS_BACKEND.
    const QByteArray fallbackBackend = nextGraphicsFallback(activeGraphicsBackend);
    const auto graphicsRecoveryStarted = std::make_shared<std::atomic_bool>(false);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app,
                     [fallbackBackend, graphicsRecoveryStarted](QObject* object, const QUrl&) {
        auto* window = qobject_cast<QQuickWindow*>(object);
        if (!window)
            return;
        QObject::connect(window, &QQuickWindow::sceneGraphError, window,
                         [fallbackBackend, graphicsRecoveryStarted](QQuickWindow::SceneGraphError,
                                                                      const QString& message) {
            if (graphicsRecoveryStarted->exchange(true))
                return;
            const bool restarted = restartWithGraphicsFallback(fallbackBackend, message);
            if (!restarted)
                graphicsStartupLog("scene graph non inizializzato; nessun fallback ulteriore disponibile");
            QCoreApplication::exit(restarted ? 0 : -1);
        });
    });
    engine.rootContext()->setContextProperty(QStringLiteral("decolog"), &controller);
    // La finestra principale puo' sparire prima che QObject inizi a distruggere
    // i controller. Chiudiamo esplicitamente socket, timer e code mentre Qt e'
    // ancora in grado di consegnare gli ultimi eventi di spegnimento.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &controller, [&controller] {
        std::fprintf(stderr, "[Shutdown] closing controllers\n");
        controller.shutdown();
        std::fprintf(stderr, "[Shutdown] controllers closed\n");
    });
    // La mappa dell'orologio mondiale: disegnata in C++, nitida a ogni scala.
    qmlRegisterType<decolog::app::WorldMapItem>("DecoDXLog.Native", 1, 0, "WorldMapItem");
    qmlRegisterType<decolog::app::WindowMirror>("DecoDXLog.Native", 1, 0, "WindowMirror");
    engine.rootContext()->setContextProperty(QStringLiteral("mapUsesSafeRenderer"), useSafeMapRenderer);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.rootContext()->setContextProperty(QStringLiteral("startupShow"), parser.value(showOption));
    // Il biglietto d'avvio: a ogni partenza normale; nelle prove (--show,
    // --grab) solo se chiesto con --show badge.
    engine.rootContext()->setContextProperty(
        QStringLiteral("startupBadge"),
        parser.value(showOption) == QLatin1String("badge")
            || (!parser.isSet(showOption) && !parser.isSet(grabOption)));
    decolog::startupTrace("QML load begin");
    engine.loadFromModule(QStringLiteral("DecoDXLog"), QStringLiteral("Main"));
    decolog::startupTrace("QML load done");
    const QList<QQuickWindow*> windows = quickWindows(engine);
    if (!windows.isEmpty())
        logQuickWindowGraphics(windows.constFirst(), "dopo engine.load");
    QTimer::singleShot(0, &app, [&engine] {
        decolog::startupTrace("event loop responding");
        const QList<QQuickWindow*> currentWindows = quickWindows(engine);
        if (!currentWindows.isEmpty())
            logQuickWindowGraphics(currentWindows.constFirst(), "event loop start");
    });
    // La spia dei blocchi si accende a interfaccia in piedi: i secondi
    // dell'avvio sono avvio, non un blocco, e segnalarli sarebbe gridare al
    // lupo alla prima riga del registro.
    QTimer::singleShot(3000, &controller, [&controller] { controller.startFreezeWatch(); });
    if (parser.isSet(themeOption)) {
        if (auto* theme = engine.singletonInstance<decodium::ui::ThemeManager*>(QStringLiteral("Decodium.UI"),
                                                                                  QStringLiteral("Theme")))
            theme->setCurrentTheme(parser.value(themeOption));
    }

    // Schermata senza toccare il desktop: con QT_QPA_PLATFORM=offscreen la finestra
    // non compare nemmeno, e nessun clic finisce su un altro programma.
    if (parser.isSet(grabOption)) {
        const QString file = parser.value(grabOption);
        // Tre secondi bastano per quasi tutto; chi deve aspettare di piu' (il
        // decoder CW che impara) lo dice con DECODXLOG_GRAB_MS.
        const int grabMs = qEnvironmentVariableIsSet("DECODXLOG_GRAB_MS")
                               ? qEnvironmentVariableIntValue("DECODXLOG_GRAB_MS") : 3000;
        QTimer::singleShot(grabMs, &app, [&engine, file] {
            if (!engine.rootObjects().isEmpty()) {
                if (auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst()))
                    window->grabWindow().save(file);
            }
            // Le altre finestre aperte (cluster, logbook separato) accanto: file-2.png...
            int n = 2;
            for (QWindow* w : QGuiApplication::topLevelWindows()) {
                auto* quick = qobject_cast<QQuickWindow*>(w);
                if (!quick || !quick->isVisible() || (!engine.rootObjects().isEmpty() && quick == engine.rootObjects().constFirst()))
                    continue;
                const QFileInfo info(file);
                quick->grabWindow().save(info.dir().filePath(QStringLiteral("%1-%2.%3").arg(info.completeBaseName()).arg(n++).arg(info.suffix())));
            }
            QCoreApplication::quit();
        });
    }

    decolog::crashlog::setStage("running");
    // Per provare il biglietto: DECODXLOG_CRASH_TEST=1 fa cadere il programma
    // di proposito, un attimo dopo l'avvio.
    if (qEnvironmentVariableIntValue("DECODXLOG_CRASH_TEST") == 1) {
        QTimer::singleShot(1500, &app, [] {
            volatile int* nowhere = nullptr;
            *nowhere = 1;
        });
    }
    const int code = app.exec();
    decolog::crashlog::setStage("quitting");
    return code;
}
