#include "Android.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>

bool Iaito::initializeAndroid(QString *error)
{
    const QString data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString prefix = data + QStringLiteral("/radare2");
    const QString assets = QStringLiteral("assets:/radare2/");
    QFile bundleId(assets + QStringLiteral("bundle-id"));
    if (data.isEmpty() || !bundleId.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("Cannot read the bundled radare2 runtime.");
        return false;
    }
    const QByteArray version = bundleId.readAll();
    const QString markerPath = prefix + QStringLiteral("/bundle-id");
    QFile marker(markerPath);
    const bool installed = marker.open(QIODevice::ReadOnly) && marker.readAll() == version;
    marker.close();
    if (!installed) {
        QFile manifest(assets + QStringLiteral("files.txt"));
        if (!manifest.open(QIODevice::ReadOnly)) {
            *error = QStringLiteral("Cannot read the radare2 asset manifest.");
            return false;
        }
        QTextStream paths(&manifest);
        while (!paths.atEnd()) {
            const QString relative = paths.readLine();
            if (relative.isEmpty()) {
                continue;
            }
            const QString destination = prefix + QLatin1Char('/') + relative;
            QFile input(assets + relative);
            QSaveFile output(destination);
            if (!QDir().mkpath(QFileInfo(destination).absolutePath())
                || !input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) {
                *error = QStringLiteral("Cannot install radare2 asset: %1").arg(relative);
                return false;
            }
            char buffer[65536];
            qint64 size;
            while ((size = input.read(buffer, sizeof(buffer))) > 0) {
                if (output.write(buffer, size) != size) {
                    *error = output.errorString();
                    return false;
                }
            }
            if (size < 0 || !output.commit()) {
                *error = QStringLiteral("Cannot finish installing radare2 asset: %1").arg(relative);
                return false;
            }
        }
        QSaveFile newMarker(markerPath);
        if (!newMarker.open(QIODevice::WriteOnly) || newMarker.write(version) != version.size()
            || !newMarker.commit()) {
            *error = QStringLiteral("Cannot save the radare2 runtime version.");
            return false;
        }
    }
    qputenv("R2_PREFIX", prefix.toUtf8());
    qputenv("XDG_DATA_HOME", data.toUtf8());
    qputenv(
        "XDG_CONFIG_HOME",
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation).toUtf8());
    qputenv(
        "XDG_CACHE_HOME", QStandardPaths::writableLocation(QStandardPaths::CacheLocation).toUtf8());
    return true;
}
