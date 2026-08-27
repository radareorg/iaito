
#include "IaitoApplication.h"
#include "IaitoConfig.h"
#include "common/CrashHandler.h"
#include "core/MainWindow.h"

#include <iostream>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>

#include <cstdio>
#include <cstring>

#ifdef Q_OS_WIN
static void connectToConsole();
#endif

static bool printEnvironmentVariables(int argc, char *argv[])
{
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--")) {
            return false;
        }
        if (std::strncmp(argv[i], "-H", 2)) {
            continue;
        }
        const char *selectedVariable = nullptr;
        if (argv[i][2]) {
            selectedVariable = argv[i] + 2;
        } else if (i + 1 < argc && argv[i + 1][0] != '-') {
            selectedVariable = argv[i + 1];
        }
#ifdef Q_OS_WIN
        connectToConsole();
#endif
        QByteArray extraPluginDirs = IAITO_EXTRA_PLUGIN_DIRS;
        const QByteArray environmentPluginDirs = qgetenv("IAITO_EXTRA_PLUGIN_DIRS");
        if (!environmentPluginDirs.isEmpty()) {
            if (!extraPluginDirs.isEmpty()) {
                extraPluginDirs.append(QDir::listSeparator().toLatin1());
            }
            extraPluginDirs.append(environmentPluginDirs);
        }
        struct EnvironmentVariable
        {
            const char *name;
            QByteArray value;
        };
        const EnvironmentVariable variables[] = {
            {"IAITO_VERSION", IAITO_VERSION_FULL},
            {"IAITO_VERSION_MAJOR", QByteArray::number(IAITO_VERSION_MAJOR)},
            {"IAITO_VERSION_MINOR", QByteArray::number(IAITO_VERSION_MINOR)},
            {"IAITO_VERSION_PATCH", QByteArray::number(IAITO_VERSION_PATCH)},
            {"IAITO_EXTRA_PLUGIN_DIRS", extraPluginDirs},
            {"R2_DEBUG", qgetenv("R2_DEBUG")},
            {"R2_NOPLUGINS", qgetenv("R2_NOPLUGINS")},
        };
        for (const auto &variable : variables) {
            const char *shortName = std::strchr(variable.name, '_');
            shortName = shortName ? shortName + 1 : variable.name;
            if (selectedVariable && std::strcmp(selectedVariable, variable.name)
                && std::strcmp(selectedVariable, shortName)) {
                continue;
            }
            if (selectedVariable) {
                std::fprintf(stdout, "%s\n", variable.value.constData());
            } else {
                std::fprintf(stdout, "%s=%s\n", variable.name, variable.value.constData());
            }
        }
        return true;
    }
    return false;
}

/**
 * @brief Attempt to connect to a parent console and configure outputs.
 *
 * @note Doesn't do anything if the exe wasn't executed from a console.
 */
#ifdef Q_OS_WIN
static void connectToConsole()
{
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        return;
    }

    // Avoid reconfiguring stderr/stdout if one of them is already connected to
    // a stream. This can happen when running with stdout/stderr redirected to a
    // file.
    if (0 > fileno(stdout)) {
        // Overwrite FD 1 and 2 for the benefit of any code that uses the FDs
        // directly.  This is safe because the CRT allocates FDs 0, 1 and
        // 2 at startup even if they don't have valid underlying Windows
        // handles.  This means we won't be overwriting an FD created by
        // _open() after startup.
        _close(1);

        if (freopen("CONOUT$", "a+", stdout)) {
            // Avoid buffering stdout/stderr since IOLBF is replaced by IOFBF in
            // Win32.
            setvbuf(stdout, nullptr, _IONBF, 0);
        }
    }
    if (0 > fileno(stderr)) {
        _close(2);

        if (freopen("CONOUT$", "a+", stderr)) {
            setvbuf(stderr, nullptr, _IONBF, 0);
        }
    }

    // Fix all cout, wcout, cin, wcin, cerr, wcerr, clog and wclog.
    std::ios::sync_with_stdio();
}
#endif

int main(int argc, char *argv[])
{
    if (printEnvironmentVariables(argc, argv)) {
        return 0;
    }

    if (argc >= 3 && QString::fromLocal8Bit(argv[1]) == "--start-crash-handler") {
        QApplication app(argc, argv);
        QString dumpLocation = QString::fromLocal8Bit(argv[2]);
        showCrashDialog(dumpLocation);
        return 0;
    }

    initCrashHandler();

#ifdef Q_OS_WIN
    connectToConsole();
#endif

    qRegisterMetaType<QList<StringDescription>>();
    qRegisterMetaType<QList<FunctionDescription>>();

    QCoreApplication::setOrganizationName("radareorg");
    QCoreApplication::setOrganizationDomain("radare.org");
    QCoreApplication::setApplicationName("iaito");
    QGuiApplication::setDesktopFileName("org.radare.iaito");

    QCoreApplication::setAttribute(
        Qt::AA_ShareOpenGLContexts); // needed for QtWebEngine inside Plugins
#ifdef Q_OS_WIN
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#endif
#endif

    IaitoApplication a(argc, argv);

    return a.exec();
}

int main_iaito()
{
    char *argv[] = {(char *) "iaito"};
    return main(1, argv);
}
