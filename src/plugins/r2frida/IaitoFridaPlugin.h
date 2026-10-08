#pragma once

#include "IaitoPlugin.h"

#include <QByteArray>
#include <QObject>

class FridaSession;
class FridaWidget;
class HexdumpWidget;
class MainWindow;
class QAction;
class QMenu;
class QLabel;
class QToolButton;

class IaitoFridaPlugin : public QObject, public IaitoPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.radare.iaito.plugins.IaitoPlugin")
    Q_INTERFACES(IaitoPlugin)

public:
    void setupPlugin() override;
    void setupInterface(MainWindow *main) override;
    void terminate() override;

    QString getName() const override { return QStringLiteral("r2frida"); }
    QString getAuthor() const override { return QStringLiteral("radareorg"); }
    QString getDescription() const override
    {
        return QStringLiteral("Dynamic instrumentation through r2frida.");
    }
    QString getVersion() const override { return QStringLiteral("1.0"); }

private:
    void buildMenu(MainWindow *main);
    void buildContextMenus(MainWindow *main);
    void updateStatus();
    void addFridaMenu(QMenu *pluginMenu);
    void installLiveHexdump(HexdumpWidget *hexdump);
    bool handleConsoleCommand(const QString &command);
    QByteArray readLiveBytes(uint64_t addr, int len) const;

    FridaSession *session = nullptr;
    FridaWidget *widget = nullptr;
    MainWindow *mainWindow = nullptr;
    QToolButton *statusButton = nullptr;
    QLabel *statusDetail = nullptr;
    QAction *connectAction = nullptr;
    QAction *detachAction = nullptr;
    QAction *resumeAction = nullptr;
};
