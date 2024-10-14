#include "common/ResourcePaths.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QLibraryInfo>

#ifdef APPIMAGE
static QDir appimageRoot()
{
    auto dir = QDir(QCoreApplication::applicationDirPath()); // appimage/usr/bin
    dir.cdUp();                                              // /usr
    dir.cdUp();                                              // /
    return dir;
}
#endif

static QString substitutePath(QString path)
{
#ifdef APPIMAGE
    if (path.startsWith("/usr")) {
        return appimageRoot().absoluteFilePath("." + path);
    }
#endif
    return path;
}

/**
 * @brief Substitute or filter paths returned by standardLocations based on
 * Iaito package kind.
 * @param paths list of paths to process
 * @return
 */
static QStringList substitutePaths(const QStringList &paths)
{
    QStringList result;
    result.reserve(paths.size());
    for (auto &path : paths) {
        // consider ignoring some of system folders for portable packages here
        // or standardLocations if it depends on path type
        result.push_back(substitutePath(path));
    }
    return result;
}

QStringList Iaito::locateAll(
    QStandardPaths::StandardLocation type,
    const QString &fileName,
    QStandardPaths::LocateOptions options)
{
    // This function is reimplemented here instead of forwarded to Qt becauase
    // existence check needs to be done after substitutions
    QStringList result;
    for (auto path : standardLocations(type)) {
        QString filePath = path + QLatin1Char('/') + fileName;
        bool exists = false;
        if (options & QStandardPaths::LocateDirectory) {
            exists = QDir(filePath).exists();
        } else {
            exists = QFileInfo(filePath).isFile();
        }
        if (exists) {
            result.append(filePath);
        }
    }
    return result;
}

QStringList Iaito::standardLocations(QStandardPaths::StandardLocation type)
{
    return substitutePaths(QStandardPaths::standardLocations(type));
}

QString Iaito::writableLocation(QStandardPaths::StandardLocation type)
{
    return substitutePath(QStandardPaths::writableLocation(type));
}

QStringList Iaito::getTranslationsDirectories()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QStringList result;
#else
    auto result
        = locateAll(QStandardPaths::DataLocation, "translations", QStandardPaths::LocateDirectory);
#endif
    // loading from iaito home
#if R2_VERSION_NUMBER < 50709
    char *home = r_str_home(".local/share/iaito/translations");
#else
    char *home = r_xdg_datadir("iaito/translations");
#endif
    result << home;
    printf("Loading translations path %s\n", home);
    free(home);
    // loading from system
    result << "/usr/local/share/iaito/translations";
    result << "/usr/share/iaito/translations";
    result << "/app/share/iaito/translations";
    // loading from qt
    auto qtpath = QLibraryInfo::location(QLibraryInfo::TranslationsPath);
    result << qtpath;
    printf("Loading translations path %s\n", qtpath.toStdString().c_str());
    return result;
}
