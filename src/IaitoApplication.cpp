#include "IaitoApplication.h"
#include "IaitoConfig.h"
#include "R2AiDecompiler.h"
#include "R2AnotesDecompiler.h"
#include "R2DecaiDecompiler.h"
#include "R2GhidraCmdDecompiler.h"
#include "R2HermesDecompiler.h"
#include "R2pdcCmdDecompiler.h"
#include "R2retdecDecompiler.h"
#include "common/CrashHandler.h"
#include "common/Decompiler.h"
#include "common/ResourcePaths.h"
#include "plugins/PluginManager.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QEvent>
#include <QFileOpenEvent>
#include <QMenu>
#include <QMessageBox>
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QTextCodec>
#endif
#include <QDir>
#include <QFontDatabase>
#include <QLibraryInfo>
#include <QPluginLoader>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <QTranslator>
#ifdef Q_OS_WIN
#include <QtNetwork/QtNetwork>
#endif // Q_OS_WIN
#include <cstring>

#include <cstdlib>

#if IAITO_R2GHIDRA_STATIC
#include <R2GhidraDecompiler.h>
#endif

static bool versionCheck()
{
    // Check r2 version
    const QString a = r_core_version(); // runtime library version
    const QString b = "" R2_GITTAP;     // compiled version
    const QStringList la = a.split(".");
    const QStringList lb = b.split(".");
    if (la.size() < 2 || lb.size() < 2) {
        R_LOG_WARN("Invalid version string somewhere");
        return false;
    }
    if (la.at(0) != lb.at(0)) {
        R_LOG_WARN("Major version differs");
        return false;
    }
    if (la.at(1) != lb.at(1)) {
        R_LOG_WARN("Minor version differs");
        return false;
    }
    return true;
}

static QtMessageHandler g_previousMessageHandler = nullptr;

static void iaitoMessageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    // The category is on ctx.category, not inside msg. Drop qt.gui.imageio
    // entirely (libpng IDAT spam from PNGs shipped inside Qt itself) and any
    // stray "libpng warning:" text that might arrive without a category.
    const char *cat = ctx.category;
    if (cat && !strncmp(cat, "qt.gui.imageio", 14)) {
        return;
    }
    if (msg.contains(QLatin1String("libpng warning:"))) {
        return;
    }
    if (g_previousMessageHandler) {
        g_previousMessageHandler(type, ctx, msg);
    } else {
        QByteArray local = msg.toLocal8Bit();
        fprintf(stderr, "%s\n", local.constData());
    }
}

IaitoApplication::IaitoApplication(int &argc, char **argv)
    : QApplication(argc, argv)
{
    // Install message handler before widgets load to filter Qt's libpng spam.
    g_previousMessageHandler = qInstallMessageHandler(iaitoMessageHandler);

    // Setup application information
    setApplicationVersion(IAITO_VERSION_FULL);
#ifndef Q_OS_MACOS
    setWindowIcon(QIcon(":/img/iaito.svg"));
#endif
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
    setLayoutDirection(Qt::LeftToRight);

    // WARN!!! Put initialization code below this line. Code above this line is
    // mandatory to be run First
#ifdef Q_OS_WIN
    // Hack to force Iaito load internet connection related DLL's
    QSslSocket s;
    s.sslConfiguration();
#endif // Q_OS_WIN

    // Load translations
    if (!loadTranslations()) {
        qWarning() << "Cannot load translations";
    }

    // Load fonts
    int ret = QFontDatabase::addApplicationFont(":/fonts/AgaveRegular.ttf");
    if (ret == -1) {
        qWarning() << "Cannot load Agave Regular font.";
    }
    ret = QFontDatabase::addApplicationFont(":/fonts/AnonymousPro.ttf");
    if (ret == -1) {
        qWarning() << "Cannot load Anonymous Pro font.";
    }
    ret = QFontDatabase::addApplicationFont(":/fonts/InconsolataRegular.ttf");
    if (ret == -1) {
        qWarning() << "Cannot load Incosolata-Regular font.";
    }
    ret = QFontDatabase::addApplicationFont(":/fonts/IBMPlexMono-Regular.ttf");
    if (ret == -1) {
        qWarning() << "Cannot load IBM Plex Mono Regular font.";
    }
    ret = QFontDatabase::addApplicationFont(":/fonts/IBMPlexMono-Bold.ttf");
    if (ret == -1) {
        qWarning() << "Cannot load IBM Plex Mono Bold font.";
    }
#if 0
    ret = QFontDatabase::addApplicationFont(":/fonts/W95FA.otf");
    if (ret == -1) {
        qWarning() << "Cannot load W95FA font.";
    }
    ret = QFontDatabase::addApplicationFont(":/fonts/CourierPixel.otf");
    if (ret == -1) {
        qWarning() << "Cannot load Courier Pixel font.";
    }
#endif
    ret = QFontDatabase::addApplicationFont(":/fonts/windows.ttf");
    if (ret == -1) {
        qWarning() << "Cannot load Windows font.";
    }

    // Set QString codec to UTF-8
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));
#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
    QTextCodec::setCodecForCStrings(QTextCodec::codecForName("UTF-8"));
    QTextCodec::setCodecForTr(QTextCodec::codecForName("UTF-8"));
#endif
#endif
    if (!parseCommandLineOptions()) {
        QCoreApplication::exit();
        // std::exit(1);
    }

    if (!versionCheck()) {
        QMessageBox msg;
        msg.setIcon(QMessageBox::Critical);
        msg.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        msg.setWindowTitle(QObject::tr("Version mismatch!"));
        QString localVersion = r_core_version();
        QString r2version = R2_GITTAP;
        msg.setText(QString(
                        QObject::tr(
                            "The version used to compile Iaito (%1) does not match the "
                            "binary version of radare2 (%2). This could result in "
                            "unexpected behaviour. Are you sure you want to continue?"))
                        .arg(localVersion, r2version));
        if (msg.exec() == QMessageBox::No) {
            QCoreApplication::exit();
            // std::exit(1);
        }
    }

#ifdef Q_OS_WIN
    // Redefine r_sys_prefix() behaviour
    qputenv("R_ALT_SRC_DIR", "1");
#endif

#ifdef MACOS_R2_BUNDLED
    {
        auto appdir = QDir(QCoreApplication::applicationDirPath()); // Contents/MacOS
        appdir.cdUp();                                              // Contents

        auto r2prefix = appdir;           // Contents
        r2prefix.cd("Resources/radare2"); // Contents/Resources/radare2
        qputenv("R2_PREFIX", r2prefix.absolutePath().toLocal8Bit());

        auto r2bin = appdir; // Contents
        r2bin.cd("Helpers"); // Contents/Helpers
        auto paths = QStringList(QString::fromLocal8Bit(qgetenv("PATH")));
        paths.prepend(r2bin.absolutePath());
        qputenv("PATH", paths.join(QLatin1Char(':')).toLocal8Bit());

        auto sleighHome = appdir; // Contents
        sleighHome.cd(
            "Resources/radare2/share/r2ghidra_sleigh"); // Contents/Resources/radare2/share/r2ghidra_sleigh
        qputenv("SLEIGHHOME", sleighHome.absolutePath().toLocal8Bit());

        // Allow bundled radare2 and its plugins to save its data inside iaito app folders
        qputenv(
            "XDG_CACHE_HOME",
            QStandardPaths::writableLocation(QStandardPaths::CacheLocation).toLocal8Bit());
        qputenv(
            "XDG_CONFIG_HOME",
            QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation).toLocal8Bit());
        qputenv(
            "XDG_DATA_HOME",
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).toLocal8Bit());
    }
#endif

    Core()->initialize(clOptions.enableR2Plugins);
    Core()->setSettings();
    Config()->loadInitial();
    Core()->loadIaitoRC(0);

    Config()->setOutputRedirectionEnabled(clOptions.outputRedirectionEnabled);

    if (R2pdcCmdDecompiler::isAvailable()) {
        Core()->registerDecompiler(new R2pdcCmdDecompiler(Core()));
    }
    if (R2retdecDecompiler::isAvailable()) {
        Core()->registerDecompiler(new R2retdecDecompiler(Core()));
    }
    if (R2DecDecompiler::isAvailable()) {
        Core()->registerDecompiler(new R2DecDecompiler(Core()));
    }
    if (R2DecaiDecompiler::isAvailable()) {
        Core()->registerDecompiler(new R2DecaiDecompiler(Core()));
    }
    if (R2AiDecompiler::isAvailable()) {
        Core()->registerDecompiler(new R2AiDecompiler(Core()));
    }
    if (R2GhidraCmdDecompiler::isAvailable()) {
        Core()->registerDecompiler(new R2GhidraCmdDecompiler(Core()));
    }
    if (R2AnotesDecompiler::isAvailable()) {
        Core()->registerDecompiler(new R2AnotesDecompiler(Core()));
    }
    if (R2HermesDecompiler::isAvailable()) {
        Core()->registerDecompiler(new R2HermesDecompiler(Core()));
    }

#if IAITO_R2GHIDRA_STATIC
    Core()->registerDecompiler(new R2GhidraDecompiler(Core()));
#endif

    Plugins()->loadPlugins(clOptions.enableIaitoPlugins);

    for (auto &plugin : Plugins()->getPlugins()) {
        plugin->registerDecompilers();
    }

    mainWindow = new MainWindow();
    installEventFilter(mainWindow);

    RCore *kore = iaitoPluginCore();
    if (kore) {
        mainWindow->openCurrentCore(clOptions.fileOpenOptions, false);
    } else if (clOptions.args.empty()) {
        // check if this is the first execution of Iaito in this computer
        // Note: the execution after the preferences been reset, will be
        // considered as first-execution
        if (clOptions.showWelcomeDialog || Config()->isFirstExecution()) {
            mainWindow->displayWelcomeDialog();
        }
        mainWindow->displayNewFileDialog();
    } else { // filename specified as positional argument
        bool askOptions = clOptions.analLevel != AutomaticAnalysisLevel::Ask;
        mainWindow->openNewFile(clOptions.fileOpenOptions, askOptions);
    }

#if 0
#ifdef APPIMAGE
    {
        auto appdir = QDir(QCoreApplication::applicationDirPath()); // appdir/bin
        appdir.cdUp(); // appdir

        auto sleighHome = appdir;
        sleighHome.cd("share/radare2/plugins/r2ghidra_sleigh"); // appdir/share/radare2/plugins/r2ghidra_sleigh
        Core()->setConfig("r2ghidra.sleighhome", sleighHome.absolutePath());

        auto r2decHome = appdir;
        r2decHome.cd("share/radare2/plugins/r2dec-js"); // appdir/share/radare2/plugins/r2dec-js
        qputenv("R2DEC_HOME", r2decHome.absolutePath().toLocal8Bit());
    }
#endif

#ifdef IAITO_APPVEYOR_R2DEC
    qputenv("R2DEC_HOME", "lib\\plugins\\r2dec-js");
#endif
#ifdef Q_OS_WIN
    {
        auto sleighHome = QDir(QCoreApplication::applicationDirPath());
        sleighHome.cd("lib/plugins/r2ghidra_sleigh");
        Core()->setConfig("r2ghidra.sleighhome", sleighHome.absolutePath());
    }
#endif
#endif
}

IaitoApplication::~IaitoApplication()
{
    Plugins()->destroyPlugins();
    delete mainWindow;
}

void IaitoApplication::launchNewInstance(const QStringList &args)
{
    QProcess process(this);
    process.setEnvironment(QProcess::systemEnvironment());
    QStringList allArgs;
    if (!clOptions.enableIaitoPlugins) {
        allArgs.push_back("--no-iaito-plugins");
    }
    if (!clOptions.enableR2Plugins) {
        allArgs.push_back("--no-r2-plugins");
    }
    allArgs.append(args);
    process.startDetached(qApp->applicationFilePath(), allArgs);
}

bool IaitoApplication::event(QEvent *e)
{
    if (e->type() == QEvent::FileOpen) {
        RCore *kore = iaitoPluginCore();
        if (kore) {
            return false;
        }
        QFileOpenEvent *openEvent = static_cast<QFileOpenEvent *>(e);
        if (openEvent) {
            if (m_FileAlreadyDropped) {
                // We already dropped a file in macOS, let's spawn another
                // instance (Like the File -> Open)
                QString fileName = openEvent->file();
                launchNewInstance({fileName});
            } else {
                QString fileName = openEvent->file();
                // eprintf ("FILE %s\n", fileName.toStdString().c_str());
                if (fileName == "") {
                    return false;
                }
                m_FileAlreadyDropped = true;
                mainWindow->closeNewFileDialog();
                InitialOptions options;
                options.filename = fileName;
                mainWindow->openNewFile(options);
            }
        }
    }
    return QApplication::event(e);
}

bool IaitoApplication::loadTranslations()
{
    const QString &language = Config()->getCurrLocale().bcp47Name();
    if (language == QStringLiteral("en") || language.startsWith(QStringLiteral("en-"))) {
        return true;
    }
    const auto &allLocales
        = QLocale::matchingLocales(QLocale::AnyLanguage, QLocale::AnyScript, QLocale::AnyCountry);

    bool iaitoTrLoaded = false;

    for (const QLocale &it : allLocales) {
        const QString &langPrefix = it.bcp47Name();

        if (langPrefix == language) {
            QApplication::setLayoutDirection(it.textDirection());
            QLocale::setDefault(it);

            QTranslator *trIaito = new QTranslator;
            QTranslator *trQtBase = new QTranslator;
            QTranslator *trQt = new QTranslator;

            const QStringList &iaitoTrPaths = Iaito::getTranslationsDirectories();

            for (const auto &trPath : iaitoTrPaths) {
                if (trIaito
                    && trIaito->load(it, QLatin1String("iaito"), QLatin1String("_"), trPath)) {
                    installTranslator(trIaito);
                    iaitoTrLoaded = true;
                    trIaito = nullptr;
                }
                if (trQt && trQt->load(it, "qt", "_", trPath)) {
                    installTranslator(trQt);
                    trQt = nullptr;
                }

                if (trQtBase && trQtBase->load(it, "qtbase", "_", trPath)) {
                    installTranslator(trQtBase);
                    trQtBase = nullptr;
                }
            }

            if (trIaito) {
                delete trIaito;
            }
            if (trQt) {
                delete trQt;
            }
            if (trQtBase) {
                delete trQtBase;
            }
            return true;
        }
    }
    if (!iaitoTrLoaded) {
        qWarning() << "Cannot load Iaito's translation for " << language;
    }
    return false;
}

bool IaitoApplication::parseCommandLineOptions()
{
    // Keep this function in sync with documentation

    QCommandLineParser cmd_parser;
    cmd_parser.setApplicationDescription(
        QObject::tr("A Qt and C++ GUI for radare2 reverse engineering framework"));
    cmd_parser.addHelpOption();
    cmd_parser.addVersionOption();
    cmd_parser.addPositionalArgument("filename", QObject::tr("Filename to open."));

    QCommandLineOption analOption(
        {"A", "analysis"},
        QObject::tr(
            "Automatically open file and optionally start analysis. "
            "Needs filename to be specified. May be a value between 0 and 3:"
            " 0 = no analysis, 1 = aa, 2 = aaa, 3 = aaaa (slow)"),
        QObject::tr("level"));
    cmd_parser.addOption(analOption);

    QCommandLineOption formatOption(
        {"F", "format"},
        QObject::tr("Force using a specific file format (bin plugin)"),
        QObject::tr("name"));
    cmd_parser.addOption(formatOption);

    QCommandLineOption baddrOption(
        {"B", "base"},
        QObject::tr("Load binary at a specific base address"),
        QObject::tr("base address"));
    cmd_parser.addOption(baddrOption);

    QCommandLineOption scriptOption("i", QObject::tr("Run script file"), QObject::tr("file"));
    cmd_parser.addOption(scriptOption);

    QCommandLineOption writeModeOption({"w", "writemode"}, QObject::tr("Open file in write mode"));
    cmd_parser.addOption(writeModeOption);

    QCommandLineOption
        binCacheOption({"c", "bincache"}, QObject::tr("Patch relocs (enable bin.cache)"));
    cmd_parser.addOption(binCacheOption);

    QCommandLineOption disableRedirectOption(
        "no-output-redirect",
        QObject::tr(
            "Disable output redirection."
            " Some of the output in console widget will not be visible."
            " Use this option when debuging a crash or freeze and output "
            " redirection is causing some messages to be lost."));
    cmd_parser.addOption(disableRedirectOption);

    QCommandLineOption disablePlugins("no-plugins", QObject::tr("Do not load plugins"));
    cmd_parser.addOption(disablePlugins);

    QCommandLineOption
        disableIaitoPlugins("no-iaito-plugins", QObject::tr("Do not load Iaito plugins"));
    cmd_parser.addOption(disableIaitoPlugins);

    QCommandLineOption disableR2Plugins("no-r2-plugins", QObject::tr("Do not load radare2 plugins"));
    cmd_parser.addOption(disableR2Plugins);

    QCommandLineOption welcomeOption("welcome", QObject::tr("Show welcome dialog."));
    cmd_parser.addOption(welcomeOption);

#ifdef IAITO_ENABLE_DEBUGGER
    QCommandLineOption debugOption(
        {"d", "debug"},
        QObject::tr(
            "Open the positional argument in debug mode. The argument may be a"
            " path to an executable to run or a numeric PID to attach to."));
    cmd_parser.addOption(debugOption);
#endif

    cmd_parser.process(*this);

    IaitoCommandLineOptions opts;
    opts.args = cmd_parser.positionalArguments();

    if (cmd_parser.isSet(analOption)) {
        bool analLevelSpecified = false;
        int analLevel = cmd_parser.value(analOption).toInt(&analLevelSpecified);

        if (!analLevelSpecified || analLevel < 0 || analLevel > 3) {
            fprintf(
                stderr,
                "%s\n",
                QObject::tr("Invalid Analysis Level. May be a value between 0 and 3.")
                    .toLocal8Bit()
                    .constData());
            return false;
        }
        switch (analLevel) {
        case 0:
            opts.analLevel = AutomaticAnalysisLevel::None;
            break;
        case 1:
            opts.analLevel = AutomaticAnalysisLevel::AAA;
            break;
        case 2:
            opts.analLevel = AutomaticAnalysisLevel::AAAA;
            break;
        case 3:
            opts.analLevel = AutomaticAnalysisLevel::AAAAA;
            break;
        }
    }

    if (opts.args.empty() && opts.analLevel != AutomaticAnalysisLevel::Ask) {
        fprintf(
            stderr,
            "%s\n",
            QObject::tr("Filename must be specified to start analysis automatically.")
                .toLocal8Bit()
                .constData());
        return false;
    }

    if (!opts.args.isEmpty()) {
        opts.fileOpenOptions.filename = opts.args[0];
        opts.fileOpenOptions.forceBinPlugin = cmd_parser.value(formatOption);
        if (cmd_parser.isSet(baddrOption)) {
            bool ok;
            RVA baddr = cmd_parser.value(baddrOption).toULongLong(&ok, 0);
            if (ok) {
                opts.fileOpenOptions.binLoadAddr = baddr;
            }
        }
        switch (opts.analLevel) {
        case AutomaticAnalysisLevel::Ask:
            break;
        case AutomaticAnalysisLevel::None:
            opts.fileOpenOptions.analCmd = {};
            break;
        case AutomaticAnalysisLevel::AAA:
            opts.fileOpenOptions.analCmd = {{"aaa", "Auto analysis"}};
            break;
        case AutomaticAnalysisLevel::AAAA:
            opts.fileOpenOptions.analCmd = {{"aaaa", "Advanced analysis"}};
            break;
        case AutomaticAnalysisLevel::AAAAA:
            opts.fileOpenOptions.analCmd = {{"aaaaa", "Auto analysis (experimental)"}};
            break;
        }
        opts.fileOpenOptions.script = cmd_parser.value(scriptOption);

        opts.fileOpenOptions.writeEnabled = cmd_parser.isSet(writeModeOption);

#ifdef IAITO_ENABLE_DEBUGGER
        if (cmd_parser.isSet(debugOption)) {
            // -d: open the target as a debug session. If the positional
            // argument is numeric, treat it as a PID to attach to; otherwise
            // treat it as a path to an executable to run under the debugger.
            const QString &target = opts.args[0];
            bool isPid = false;
            target.toLongLong(&isPid);
            if (isPid) {
                opts.fileOpenOptions.filename = QStringLiteral("dbg://%1").arg(target);
            }
            opts.fileOpenOptions.debug = true;
        }
#endif
    }

    opts.outputRedirectionEnabled = !cmd_parser.isSet(disableRedirectOption);
    if (cmd_parser.isSet(disablePlugins)) {
        opts.enableIaitoPlugins = false;
        opts.enableR2Plugins = false;
    }

    if (cmd_parser.isSet(disableIaitoPlugins)) {
        opts.enableIaitoPlugins = false;
    }

    if (cmd_parser.isSet(disableR2Plugins)) {
        opts.enableR2Plugins = false;
    }

    if (cmd_parser.isSet(welcomeOption)) {
        opts.showWelcomeDialog = true;
    }

    this->clOptions = opts;
    return true;
}

bool IaitoApplication::notify(QObject *receiver, QEvent *event)
{
#if QT_VERSION_CHECK(5, 10, 0) < QT_VERSION
    if (event->type() == QEvent::Polish) {
        if (auto menu = qobject_cast<QMenu *>(receiver)) {
            for (auto action : menu->actions()) {
                action->setShortcutVisibleInContextMenu(true);
            }
        }
    }
#endif
    return QApplication::notify(receiver, event);
}
