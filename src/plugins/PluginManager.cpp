
#include "PluginManager.h"
#include "IaitoConfig.h"
#include "IaitoPlugin.h"
#include "common/Helpers.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QPluginLoader>
#include <QStandardPaths>

Q_GLOBAL_STATIC(PluginManager, uniqueInstance)

PluginManager *PluginManager::getInstance()
{
    return uniqueInstance;
}

PluginManager::PluginManager() {}

PluginManager::~PluginManager() {}

void PluginManager::loadPlugins(bool enablePlugins)
{
    if (!enablePlugins) {
        // [#2159] list but don't enable the plugins
        return;
    }

    QString userPluginDir = getUserPluginsDirectory();
    if (!userPluginDir.isEmpty()) {
        loadPluginsFromDir(QDir(userPluginDir), true);
    }
    const auto pluginDirs = getPluginDirectories();
    for (auto &dir : pluginDirs) {
        if (dir.absolutePath() == userPluginDir) {
            continue;
        }
        loadPluginsFromDir(dir);
    }
}

void PluginManager::loadPluginsFromDir(const QDir &pluginsDir, bool writable)
{
    qInfo() << "Plugins are loaded from" << pluginsDir.absolutePath();
    int loadedPlugins = plugins.size();
    if (!pluginsDir.exists()) {
        return;
    }

    QDir nativePluginsDir = pluginsDir;
    if (writable) {
        nativePluginsDir.mkdir("native");
    }
    if (nativePluginsDir.cd("native")) {
        loadNativePlugins(nativePluginsDir);
    }

    loadedPlugins = plugins.size() - loadedPlugins;
    // qInfo() << "Loaded" << loadedPlugins << "plugin(s).";
}

void PluginManager::PluginTerminator::operator()(IaitoPlugin *plugin) const
{
    plugin->terminate();
    delete plugin;
}

void PluginManager::destroyPlugins()
{
    plugins.clear();
}

QVector<QDir> PluginManager::getPluginDirectories() const
{
    QVector<QDir> result;
    QStringList locations = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation);
    for (auto &location : locations) {
        result.push_back(QDir(location).filePath("plugins"));
    }

#ifdef APPIMAGE
    {
        auto plugdir = QDir(QCoreApplication::applicationDirPath()); // appdir/bin
        plugdir.cdUp();                                              // appdir
        if (plugdir.cd("share/RadareOrg/Iaito/plugins")) { // appdir/share/RadareOrg/Iaito/plugins
            result.push_back(plugdir);
        }
    }
#endif

#if QT_VERSION < QT_VERSION_CHECK(5, 6, 0) && defined(Q_OS_UNIX)
    QChar listSeparator = ':';
#else
    QChar listSeparator = QDir::listSeparator();
#endif
    QString extra_plugin_dirs = IAITO_EXTRA_PLUGIN_DIRS;
    for (auto &path : extra_plugin_dirs.split(listSeparator, IAITO_QT_SKIP_EMPTY_PARTS)) {
        result.push_back(QDir(path));
    }

    return result;
}

QString PluginManager::getUserPluginsDirectory() const
{
    QString location = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (location.isEmpty()) {
        return QString();
    }
    QDir pluginsDir(location);
    pluginsDir.mkpath("plugins");
    if (!pluginsDir.cd("plugins")) {
        return QString();
    }
    return pluginsDir.absolutePath();
}

void PluginManager::loadNativePlugins(const QDir &directory)
{
    for (const QString &fileName : directory.entryList(QDir::Files)) {
        QPluginLoader pluginLoader(directory.absoluteFilePath(fileName));
        QObject *plugin = pluginLoader.instance();
        if (!plugin) {
            auto errorString = pluginLoader.errorString();
            if (!errorString.isEmpty()) {
                qWarning() << "Load Error for plugin" << fileName << ":" << errorString;
            }
            continue;
        }
        PluginPtr iaitoPlugin{qobject_cast<IaitoPlugin *>(plugin)};
        if (!iaitoPlugin) {
            continue;
        }
        iaitoPlugin->setupPlugin();
        plugins.push_back(std::move(iaitoPlugin));
    }
}
