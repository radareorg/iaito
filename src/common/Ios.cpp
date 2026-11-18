#include "Ios.h"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

bool Iaito::initializeIos(QString *error)
{
    const QString prefix = QCoreApplication::applicationDirPath() + QStringLiteral("/radare2");
    if (!QDir(prefix + QStringLiteral("/share/radare2")).exists()) {
        *error = QStringLiteral("Cannot find the bundled radare2 runtime.");
        return false;
    }
    qputenv("R2_PREFIX", prefix.toUtf8());
    const struct
    {
        const char *variable;
        QStandardPaths::StandardLocation location;
    } paths[]
        = {{"XDG_DATA_HOME", QStandardPaths::AppDataLocation},
           {"XDG_CONFIG_HOME", QStandardPaths::AppConfigLocation},
           {"XDG_CACHE_HOME", QStandardPaths::CacheLocation}};
    for (const auto &path : paths) {
        const QString directory = QStandardPaths::writableLocation(path.location);
        if (directory.isEmpty() || !QDir().mkpath(directory)) {
            *error = QStringLiteral("Cannot create a writable radare2 directory.");
            return false;
        }
        qputenv(path.variable, directory.toUtf8());
    }
    return true;
}
