#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "MemoryDockWidget.h"
#include "common/Configuration.h"
#include "common/IOModesController.h"
#include "common/IaitoLayout.h"
#include "common/InitialOptions.h"
#include "core/Iaito.h" // only needed for ut64
#include "dialogs/NewFileDialog.h"
#include "dialogs/WelcomeDialog.h"

#include <memory>

#include <QList>
#include <QMainWindow>

class IaitoCore;
class Omnibar;
class ProgressIndicator;
class PreviewWidget;
class Highlighter;
class AsciiHighlighter;
class VisualNavbar;
class FunctionsWidget;
class ImportsWidget;
class ExportsWidget;
class SymbolsWidget;
class RelocsWidget;
class CommentsWidget;
class StringsWidget;
class FlagsWidget;
class Dashboard;
class QLineEdit;
class SdbWidget;
class QAction;
class SectionsWidget;
class SegmentsWidget;
class ConsoleWidget;
class EntrypointWidget;
class DisassemblerGraphView;
class ClassesWidget;
class ResourcesWidget;
class VTablesWidget;
class TypesWidget;
class HeadersWidget;
class ZignaturesWidget;
class SearchWidget;
class QDockWidget;
class DisassemblyWidget;
class GraphWidget;
class HexdumpWidget;
class DecompilerWidget;
class OverviewWidget;
class R2GraphWidget;
class CallGraphWidget;

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    bool responsive;

    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void displayNewFileDialog();
    void displayWelcomeDialog();
    void closeNewFileDialog();

    void openNewFile(InitialOptions &options, bool skipOptionsDialog = false);
    void openProject(const QString &project_name);
    void openCurrentCore(InitialOptions &options, bool skipOptionsDialog = false);

    /**
     * @param quit whether to show destructive button in dialog
     * @return if quit is true, false if the application should not close
     */
    bool saveProject(bool quit = false);

    /**
     * @param quit whether to show destructive button in dialog
     * @return false if the application should not close
     */
    bool saveProjectAs(bool quit = false);

    void closeEvent(QCloseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void readSettings();
    void saveSettings();
    void setFilename(const QString &fn);
    void refreshOmniBar(const QStringList &flags);

    void addWidget(IaitoDockWidget *widget);
    void addMemoryDockWidget(MemoryDockWidget *widget);
    void removeWidget(IaitoDockWidget *widget);
    void addExtraWidget(IaitoDockWidget *extraDock);
    MemoryDockWidget *addNewMemoryWidget(
        MemoryWidgetType type, RVA address, bool synchronized = true);

    IAITO_DEPRECATED("Action will be ignored. Use "
                     "addPluginDockWidget(IaitoDockWidget*) instead.")
    void addPluginDockWidget(IaitoDockWidget *dockWidget, QAction *)
    {
        addPluginDockWidget(dockWidget);
    }
    void addPluginDockWidget(IaitoDockWidget *dockWidget);
    enum class MenuType { File, Edit, View, Windows, Debug, Help, Plugins };
    /**
     * @brief Getter for MainWindow's different menus
     * @param type The type which represents the desired menu
     * @return The requested menu or nullptr if "type" is invalid
     */
    QMenu *getMenuByType(MenuType type);
    void addMenuFileAction(QAction *action);

    QString getFilename() const { return filename; }
    void messageBoxWarning(QString title, QString message);

    QString getUniqueObjectName(const QString &widgetType) const;
    void showMemoryWidget();
    void showMemoryWidget(MemoryWidgetType type);
    enum class AddressTypeHint { Function, Data, Unknown };
    QMenu *createShowInMenu(
        QWidget *parent, RVA address, AddressTypeHint addressType = AddressTypeHint::Unknown);
    void setCurrentMemoryWidget(MemoryDockWidget *memoryWidget);
    MemoryDockWidget *getLastMemoryWidget();

    /* Context menu plugins */
    enum class ContextMenuType { Disassembly, Addressable };
    /**
     * @brief Fetches the pointer to a context menu extension of type
     * @param type - the type of the context menu
     * @return plugins submenu of the selected context menu
     */
    QMenu *getContextMenuExtensions(ContextMenuType type);

public slots:
    void finalizeOpen();

    void refreshAll();
    void seekToFunctionLastInstruction();
    void seekToFunctionStart();
    void setTabLocation();

    void on_actionTabs_triggered();

    void on_actionAnalyze_triggered();

    void lockDocks(bool lock);

    void on_actionRun_Script_triggered();

    void toggleResponsive(bool maybe);

    void openNewFileFailed();

    void toggleOverview(bool visibility, GraphWidget *targetGraph);
private slots:
    void on_actionAbout_triggered();
    void on_actionIssue_triggered();
    void websiteClicked();
    void fortuneClicked();
    void bookClicked();
    void addExtraGraph();
    void addExtraHexdump();
    void addExtraDisassembly();
    void addExtraDecompiler();

    void on_actionRefresh_Panels_triggered();

    void on_actionDisasAdd_comment_triggered();

    void on_actionDefault_triggered();

    void on_actionNew_triggered();

    void on_actionSave_triggered();
    void on_actionSaveAs_triggered();

    void on_actionBackward_triggered();
    void on_actionForward_triggered();

    void on_actionMap_triggered();

    void on_actionTabs_on_Top_triggered();

    void on_actionReset_settings_triggered();

    void on_actionQuit_triggered();

    void on_actionRefresh_contents_triggered();

    void on_actionPreferences_triggered();

    void on_actionImportPDB_triggered();

    void on_actionExport_as_code_triggered();

    void on_actionGrouped_dock_dragging_triggered(bool checked);

    void projectSaved(bool successfully, const QString &name);

    void updateTasksIndicator();

    void mousePressEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;
    bool event(QEvent *event) override;
    void toggleDebugView();
    void chooseThemeIcons();

    void onZoomIn();
    void onZoomOut();
    void onZoomReset();

    void setAvailableIOModeOptions();

private:
    IaitoCore *core;

    bool tabsOnTop;
    ut64 hexdumpTopOffset;
    ut64 hexdumpBottomOffset;
    QString filename;
    std::unique_ptr<Ui::MainWindow> ui;
    Highlighter *highlighter;
    VisualNavbar *visualNavbar;
    Omnibar *omnibar;
    ProgressIndicator *tasksProgressIndicator;
    QByteArray emptyState;
    IOModesController ioModesController;

    Configuration *configuration;

    QList<IaitoDockWidget *> dockWidgets;
    QList<IaitoDockWidget *> pluginDocks;
    OverviewWidget *overviewDock = nullptr;
    QAction *actionOverview = nullptr;
    EntrypointWidget *entrypointDock = nullptr;
    FunctionsWidget *functionsDock = nullptr;
    ImportsWidget *importsDock = nullptr;
    ExportsWidget *exportsDock = nullptr;
    HeadersWidget *headersDock = nullptr;
    TypesWidget *typesDock = nullptr;
    SearchWidget *searchDock = nullptr;
    SymbolsWidget *symbolsDock = nullptr;
    RelocsWidget *relocsDock = nullptr;
    CommentsWidget *commentsDock = nullptr;
    StringsWidget *stringsDock = nullptr;
    FlagsWidget *flagsDock = nullptr;
    Dashboard *dashboardDock = nullptr;
    SdbWidget *sdbDock = nullptr;
    SectionsWidget *sectionsDock = nullptr;
    SegmentsWidget *segmentsDock = nullptr;
    ZignaturesWidget *zignaturesDock = nullptr;
    ConsoleWidget *consoleDock = nullptr;
    ClassesWidget *classesDock = nullptr;
    ResourcesWidget *resourcesDock = nullptr;
    VTablesWidget *vTablesDock = nullptr;
    IaitoDockWidget *stackDock = nullptr;
    IaitoDockWidget *threadsDock = nullptr;
    IaitoDockWidget *processesDock = nullptr;
    IaitoDockWidget *registersDock = nullptr;
    IaitoDockWidget *backtraceDock = nullptr;
    IaitoDockWidget *memoryMapDock = nullptr;
    NewFileDialog *newFileDialog = nullptr;
    IaitoDockWidget *breakpointDock = nullptr;
    IaitoDockWidget *registerRefsDock = nullptr;
    R2GraphWidget *r2GraphDock = nullptr;
    CallGraphWidget *callGraphDock = nullptr;
    CallGraphWidget *globalCallGraphDock = nullptr;

    QMenu *disassemblyContextMenuExtensions = nullptr;
    QMenu *addressableContextMenuExtensions = nullptr;

    QMap<QString, Iaito::IaitoLayout> layouts;

    void initUI();
    void initToolBar();
    void initDocks();
    void initBackForwardMenu();
    void displayInitialOptionsDialog(
        const InitialOptions &options = InitialOptions(), bool skipOptionsDialog = false);

    Iaito::IaitoLayout getViewLayout();
    Iaito::IaitoLayout getViewLayout(const QString &name);

    void setViewLayout(const Iaito::IaitoLayout &layout);
    void loadLayouts(QSettings &settings);
    void saveLayouts(QSettings &settings);

    void updateMemberPointers();
    void restoreDocks();
    void showZenDocks();
    void showDebugDocks();
    /**
     * @brief Try to guess which is the "main" section of layout and dock there.
     * @param widget that needs to be docked
     */
    void dockOnMainArea(QDockWidget *widget);
    void enableDebugWidgetsMenu(bool enable);
    /**
     * @brief Fill menu with seek history entries.
     * @param menu
     * @param redo set to false for undo history, true for redo.
     */
    void updateHistoryMenu(QMenu *menu, bool redo = false);
    void updateLayoutsMenu();
    void saveNamedLayout();
    void manageLayouts();

    void setOverviewData();
    bool isOverviewActive();
    /**
     * @brief Check if a widget is one of debug specific dock widgets.
     * @param dock
     * @return true for debug specific widgets, false for all other including
     * common dock widgets.
     */
    bool isDebugWidget(QDockWidget *dock) const;
    bool isExtraMemoryWidget(QDockWidget *dock) const;

    MemoryWidgetType getMemoryWidgetTypeToRestore();

    /**
     * @brief Map from a widget type (e.g. DisassemblyWidget::getWidgetType())
     * to the respective contructor of the widget
     */
    QMap<QString, std::function<IaitoDockWidget *(MainWindow *)>> widgetTypeToConstructorMap;

    MemoryDockWidget *lastSyncMemoryWidget = nullptr;
    MemoryDockWidget *lastMemoryWidget = nullptr;
    int functionDockWidthToRestore = 0;
};

#endif // MAINWINDOW_H
