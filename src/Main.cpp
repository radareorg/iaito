
#include "IaitoApplication.h"
#include "core/MainWindow.h"
#include "common/UpdateWorker.h"
#include "IaitoConfig.h"
#include "common/CrashHandler.h"
#include "common/SettingsUpgrade.h"

#include <QJsonObject>
#include <QJsonArray>
#include <iostream>


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

    // Avoid reconfiguring stderr/stdout if one of them is already connected to a stream.
    // This can happen when running with stdout/stderr redirected to a file.
    if (0 > fileno(stdout)) {
        // Overwrite FD 1 and 2 for the benefit of any code that uses the FDs
        // directly.  This is safe because the CRT allocates FDs 0, 1 and
        // 2 at startup even if they don't have valid underlying Windows
        // handles.  This means we won't be overwriting an FD created by
        // _open() after startup.
        _close(1);

        if (freopen("CONOUT$", "a+", stdout)) {
            // Avoid buffering stdout/stderr since IOLBF is replaced by IOFBF in Win32.
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
    QCoreApplication::setApplicationName("iaito");
    QGuiApplication::setDesktopFileName("org.radare.iaito");

    Iaito::initializeSettings();

    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts); // needed for QtWebEngine inside Plugins
#ifdef Q_OS_WIN
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    #if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    #endif
#endif

    IaitoApplication a(argc, argv);

    Iaito::migrateThemes();

    if (Config()->getAutoUpdateEnabled()) {
#if IAITO_UPDATE_WORKER_AVAILABLE
        UpdateWorker *updateWorker = new UpdateWorker;
        QObject::connect(updateWorker, &UpdateWorker::checkComplete,
                         [=](const QVersionNumber &version, const QString & error) {
            if (error.isEmpty() && version > UpdateWorker::currentVersionNumber()) {
                updateWorker->showUpdateDialog(true);
            }
            updateWorker->deleteLater();
        });
        updateWorker->checkCurrentVersion(7000);
#endif
    }
    return a.exec();
}

int main_iaito() {
	char *argv[] = {(char *)"iaito"};
	return main(1, argv);
}
