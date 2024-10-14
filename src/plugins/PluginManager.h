
#ifndef PLUGINMANAGER_H
#define PLUGINMANAGER_H

#include <memory>
#include <vector>
#include <QDir>
#include <QObject>

#include "plugins/IaitoPlugin.h"

class PluginManager : public QObject
{
    Q_OBJECT

public:
    static PluginManager *getInstance();

    class PluginTerminator
    {
    public:
        void operator()(IaitoPlugin *) const;
    };
    using PluginPtr = std::unique_ptr<IaitoPlugin, PluginTerminator>;

    PluginManager();
    ~PluginManager();

    /**
     * @brief Load all plugins, should be called once on application start
     * @param enablePlugins set to false if plugin code shouldn't be started
     */
    void loadPlugins(bool enablePlugins = true);

    /**
     * @brief Destroy all loaded plugins, should be called once on application
     * shutdown
     */
    void destroyPlugins();

    const std::vector<PluginPtr> &getPlugins() { return plugins; }

    QVector<QDir> getPluginDirectories() const;
    QString getUserPluginsDirectory() const;

private:
    std::vector<PluginPtr> plugins;

    void loadNativePlugins(const QDir &directory);
    void loadPluginsFromDir(const QDir &pluginsDir, bool writable = false);

#ifdef IAITO_ENABLE_PYTHON_BINDINGS
    void loadPythonPlugins(const QDir &directory);
    IaitoPlugin *loadPythonPlugin(const char *moduleName);
#endif
};

#define Plugins() (PluginManager::getInstance())

#endif // PLUGINMANAGER_H
