#include "core/MainWindow.h"
#include "ui_MainWindow.h"

// Common Headers
#include "IaitoApplication.h"
#include "common/BugReporting.h"
#include "common/Helpers.h"
#include "common/ProgressIndicator.h"
#include "common/RunScriptTask.h"
#include "common/TempConfig.h"
#include "plugins/IaitoPlugin.h"
#include "plugins/PluginManager.h"

// Dialogs
#include "dialogs/AboutDialog.h"
#include "dialogs/AsyncTaskDialog.h"
#include "dialogs/CommentsDialog.h"
#include "dialogs/InitialOptionsDialog.h"
#include "dialogs/LayoutManager.h"
#include "dialogs/FortuneDialog.h"
#include "dialogs/MapFileDialog.h"
#include "dialogs/NewFileDialog.h"
#include "dialogs/SaveProjectDialog.h"
#include "dialogs/WelcomeDialog.h"
#include "dialogs/preferences/PreferencesDialog.h"

// Widgets Headers
#include "widgets/BacktraceWidget.h"
#include "widgets/BreakpointWidget.h"
#include "widgets/CallGraph.h"
#include "widgets/ClassesWidget.h"
#include "widgets/CommentsWidget.h"
#include "widgets/ConsoleWidget.h"
#include "widgets/Dashboard.h"
#include "widgets/DebugActions.h"
#include "widgets/DecompilerWidget.h"
#include "widgets/DisassemblerGraphView.h"
#include "widgets/DisassemblyWidget.h"
#include "widgets/EntrypointWidget.h"
#include "widgets/ExportsWidget.h"
#include "widgets/FlagsWidget.h"
#include "widgets/FunctionsWidget.h"
#include "widgets/GraphView.h"
#include "widgets/GraphWidget.h"
#include "widgets/HeadersWidget.h"
#include "widgets/HexdumpWidget.h"
#include "widgets/ImportsWidget.h"
#include "widgets/MemoryMapWidget.h"
#include "widgets/Omnibar.h"
#include "widgets/OverviewWidget.h"
#include "widgets/ProcessesWidget.h"
#include "widgets/R2GraphWidget.h"
#include "widgets/RegisterRefsWidget.h"
#include "widgets/RegistersWidget.h"
#include "widgets/RelocsWidget.h"
#include "widgets/ResourcesWidget.h"
#include "widgets/SdbWidget.h"
#include "widgets/SearchWidget.h"
#include "widgets/SectionsWidget.h"
#include "widgets/SegmentsWidget.h"
#include "widgets/StackWidget.h"
#include "widgets/StringsWidget.h"
#include "widgets/SymbolsWidget.h"
#include "widgets/ThreadsWidget.h"
#include "widgets/TypesWidget.h"
#include "widgets/VTablesWidget.h"
#include "widgets/VisualNavbar.h"
#include "widgets/ZignaturesWidget.h"

// Qt Headers
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QCompleter>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QFontDialog>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMessageBox>
#include <QProcess>
#include <QPropertyAnimation>
#include <QSysInfo>

#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QStringListModel>
#include <QStyleFactory>
#include <QStyledItemDelegate>
#include <QSvgRenderer>
#include <QTextCursor>
#include <QToolButton>
#include <QToolTip>
#include <QTreeWidgetItem>
#include <QtGlobal>

// Graphics
#include <QGraphicsEllipseItem>
#include <QGraphicsScene>
#include <QGraphicsView>

template<class T>
T *getNewInstance(MainWindow *m)
{
    return new T(m);
}

using namespace Iaito;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , core(Core())
    , ui(new Ui::MainWindow)
{
    tabsOnTop = true; // default option
    configuration = Config();

    initUI();
}

MainWindow::~MainWindow() {}

void MainWindow::initUI()
{
    ui->setupUi(this);

    // Initialize context menu extensions for plugins
    disassemblyContextMenuExtensions = new QMenu(tr("Plugins"), this);
    addressableContextMenuExtensions = new QMenu(tr("Plugins"), this); // XXX both plugins?

    connect(ui->actionExtraDecompiler, &QAction::triggered, this, &MainWindow::addExtraDecompiler);
    connect(ui->actionExtraGraph, &QAction::triggered, this, &MainWindow::addExtraGraph);
    connect(ui->actionExtraDisassembly, &QAction::triggered, this, &MainWindow::addExtraDisassembly);
    connect(ui->actionExtraHexdump, &QAction::triggered, this, &MainWindow::addExtraHexdump);
    connect(ui->actionCommitChanges, &QAction::triggered, this, []() {
        Core()->commitWriteCache();
    });
    ui->actionCommitChanges->setEnabled(false);
    connect(Core(), &IaitoCore::ioCacheChanged, ui->actionCommitChanges, &QAction::setEnabled);

    widgetTypeToConstructorMap.insert(GraphWidget::getWidgetType(), getNewInstance<GraphWidget>);
    widgetTypeToConstructorMap
        .insert(DisassemblyWidget::getWidgetType(), getNewInstance<DisassemblyWidget>);
    widgetTypeToConstructorMap.insert(HexdumpWidget::getWidgetType(), getNewInstance<HexdumpWidget>);
    widgetTypeToConstructorMap
        .insert(DecompilerWidget::getWidgetType(), getNewInstance<DecompilerWidget>);

    initToolBar();
    initDocks();

    emptyState = saveState();
    /*
     *  Some global shortcuts
     */

    // Period goes to command entry
    QShortcut *cmd_shortcut = new QShortcut(QKeySequence(Qt::Key_Period), this);
    connect(cmd_shortcut, &QShortcut::activated, consoleDock, &ConsoleWidget::focusInputLineEdit);

    // G and S goes to goto entry
    QShortcut *goto_shortcut = new QShortcut(QKeySequence(Qt::Key_G), this);
    connect(goto_shortcut, &QShortcut::activated, this->omnibar, [this]() {
        this->omnibar->setFocus();
    });
    QShortcut *seek_shortcut = new QShortcut(QKeySequence(Qt::Key_S), this);
    connect(seek_shortcut, &QShortcut::activated, this->omnibar, [this]() {
        this->omnibar->setFocus();
    });
    QShortcut *seek_to_func_end_shortcut = new QShortcut(QKeySequence(Qt::Key_Dollar), this);
    connect(
        seek_to_func_end_shortcut,
        &QShortcut::activated,
        this,
        &MainWindow::seekToFunctionLastInstruction);
    QShortcut *seek_to_func_start_shortcut = new QShortcut(QKeySequence(Qt::Key_AsciiCircum), this);
    connect(
        seek_to_func_start_shortcut, &QShortcut::activated, this, &MainWindow::seekToFunctionStart);

    QShortcut *refresh_shortcut = new QShortcut(QKeySequence(QKeySequence::Refresh), this);
    connect(refresh_shortcut, &QShortcut::activated, this, &MainWindow::refreshAll);

    connect(ui->actionZoomIn, &QAction::triggered, this, &MainWindow::onZoomIn);
    connect(ui->actionZoomOut, &QAction::triggered, this, &MainWindow::onZoomOut);
    connect(ui->actionZoomReset, &QAction::triggered, this, &MainWindow::onZoomReset);

    connect(core, &IaitoCore::projectSaved, this, &MainWindow::projectSaved);
    connect(core, &IaitoCore::toggleDebugView, this, &MainWindow::toggleDebugView);
    connect(core, &IaitoCore::newMessage, this->consoleDock, &ConsoleWidget::addOutput);
    connect(core, &IaitoCore::newDebugMessage, this->consoleDock, &ConsoleWidget::addDebugOutput);
    connect(
        core,
        &IaitoCore::showMemoryWidgetRequested,
        this,
        static_cast<void (MainWindow::*)()>(&MainWindow::showMemoryWidget));

    updateTasksIndicator();
    connect(
        core->getAsyncTaskManager(),
        &AsyncTaskManager::tasksChanged,
        this,
        &MainWindow::updateTasksIndicator);

    // Undo and redo seek
    ui->actionBackward->setShortcut(QKeySequence::Back);
    ui->actionForward->setShortcut(QKeySequence::Forward);

    initBackForwardMenu();

    connect(core, &IaitoCore::ioModeChanged, this, &MainWindow::setAvailableIOModeOptions);

    QActionGroup *ioModeActionGroup = new QActionGroup(this);

    ioModeActionGroup->addAction(ui->actionCacheMode);
    ioModeActionGroup->addAction(ui->actionWriteMode);
    ioModeActionGroup->addAction(ui->actionReadOnly);

    connect(ui->actionCacheMode, &QAction::triggered, this, [this]() {
        ioModesController.setIOMode(IOModesController::Mode::CACHE);
        setAvailableIOModeOptions();
    });

    connect(ui->actionWriteMode, &QAction::triggered, this, [this]() {
        ioModesController.setIOMode(IOModesController::Mode::WRITE);
        setAvailableIOModeOptions();
    });

    connect(ui->actionReadOnly, &QAction::triggered, this, [this]() {
        ioModesController.setIOMode(IOModesController::Mode::READ_ONLY);
        setAvailableIOModeOptions();
    });

    connect(ui->actionSaveLayout, &QAction::triggered, this, &MainWindow::saveNamedLayout);
    connect(ui->actionManageLayouts, &QAction::triggered, this, &MainWindow::manageLayouts);
    connect(ui->actionWebsite, &QAction::triggered, this, &MainWindow::websiteClicked);
    connect(ui->actionFortune, &QAction::triggered, this, &MainWindow::fortuneClicked);
    connect(ui->actionBook, &QAction::triggered, this, &MainWindow::bookClicked);

    /* Setup plugins interfaces */
    const auto &plugins = Plugins()->getPlugins();
    for (auto &plugin : plugins) {
        plugin->setupInterface(this);
    }

    // Check if plugins are loaded and display tooltips accordingly
    ui->menuWindows->setToolTipsVisible(true);
    if (plugins.empty()) {
        ui->menuPlugins->menuAction()->setToolTip(
            tr("No plugins are installed. Check the plugins section on Iaito "
               "documentation to learn more."));
        ui->menuPlugins->setEnabled(false);
    } else if (ui->menuPlugins->isEmpty()) {
        ui->menuPlugins->menuAction()->setToolTip(
            tr("The installed plugins didn't add entries to this menu."));
        ui->menuPlugins->setEnabled(false);
    }

    connect(ui->actionUnlock, &QAction::toggled, this, [this](bool unlock) { lockDocks(!unlock); });

#if QT_VERSION < QT_VERSION_CHECK(5, 7, 0)
    ui->actionGrouped_dock_dragging->setVisible(false);
#endif

    enableDebugWidgetsMenu(false);
    readSettings();

    // Display tooltip for the Analyze Program action
    ui->actionAnalyze->setToolTip("Run the analysis");
    ui->menuFile->setToolTipsVisible(true);
}

void MainWindow::initToolBar()
{
    chooseThemeIcons();

    // Separator between undo/redo and goto lineEdit
    QWidget *spacer3 = new QWidget();
    spacer3->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    spacer3->setStyleSheet("background-color: rgba(0,0,0,0)");
    spacer3->setMinimumSize(20, 20);
    spacer3->setMaximumWidth(100);
    ui->mainToolBar->addWidget(spacer3);

    DebugActions *debugActions = new DebugActions(ui->mainToolBar, this);

    // Debug menu
    auto debugViewAction = ui->menuDebug->addAction(tr("View"));
    debugViewAction->setMenu(ui->menuAddDebugWidgets);
    ui->menuDebug->addSeparator();
    ui->menuDebug->addAction(debugActions->rarunProfile);
    ui->menuDebug->addSeparator();
    ui->menuDebug->addAction(debugActions->actionStart);
    ui->menuDebug->addAction(debugActions->actionStartEmul);
    ui->menuDebug->addAction(debugActions->actionAttach);
    ui->menuDebug->addAction(debugActions->actionStartRemote);
    ui->menuDebug->addSeparator();
    ui->menuDebug->addAction(debugActions->actionStep);
    ui->menuDebug->addAction(debugActions->actionStepOver);
    ui->menuDebug->addAction(debugActions->actionStepOut);
    ui->menuDebug->addSeparator();
    ui->menuDebug->addAction(debugActions->actionContinue);
    ui->menuDebug->addAction(debugActions->actionContinueUntilCall);
    ui->menuDebug->addAction(debugActions->actionContinueUntilSyscall);

    // Sepparator between undo/redo and goto lineEdit
    QWidget *spacer4 = new QWidget();
    spacer4->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    spacer4->setStyleSheet("background-color: rgba(0,0,0,0)");
    spacer4->setMinimumSize(10, 10);
    spacer4->setMaximumWidth(10);
    ui->mainToolBar->addWidget(spacer4);

    // Omnibar LineEdit
    this->omnibar = new Omnibar(this);
    ui->mainToolBar->addWidget(this->omnibar);

    // Add special separators to the toolbar that expand to separate groups of
    // elements
    QWidget *spacer2 = new QWidget();
    spacer2->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    spacer2->setStyleSheet("background-color: rgba(0,0,0,0)");
    spacer2->setMinimumSize(10, 10);
    spacer2->setMaximumWidth(300);
    ui->mainToolBar->addWidget(spacer2);

    // Separator between back/forward and undo/redo buttons
    QWidget *spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    spacer->setStyleSheet("background-color: rgba(0,0,0,0)");
    spacer->setMinimumSize(20, 20);
    ui->mainToolBar->addWidget(spacer);

    tasksProgressIndicator = new ProgressIndicator();
    tasksProgressIndicator->setStyleSheet("background-color: rgba(0,0,0,0)");
    ui->mainToolBar->addWidget(tasksProgressIndicator);

    QWidget *spacerEnd = new QWidget();
    spacerEnd->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    spacerEnd->setStyleSheet("background-color: rgba(0,0,0,0)");
    spacerEnd->setMinimumSize(4, 0);
    spacerEnd->setMaximumWidth(4);
    ui->mainToolBar->addWidget(spacerEnd);

    // Visual navigation tool bar
    this->visualNavbar = new VisualNavbar(this);
    this->visualNavbar->setMovable(false);
    addToolBarBreak(Qt::TopToolBarArea);
    addToolBar(visualNavbar);
    QObject::connect(configuration, &Configuration::colorsUpdated, this, [this]() {
        this->visualNavbar->updateGraphicsScene();
    });
    QObject::connect(
        configuration, &Configuration::interfaceThemeChanged, this, &MainWindow::chooseThemeIcons);
}

void MainWindow::initDocks()
{
    dockWidgets.reserve(20);
    consoleDock = new ConsoleWidget(this);

    overviewDock = new OverviewWidget(this);
    overviewDock->hide();
    actionOverview = overviewDock->toggleViewAction();
    connect(overviewDock, &OverviewWidget::isAvailableChanged, this, [this](bool isAvailable) {
        actionOverview->setEnabled(isAvailable);
    });
    actionOverview->setEnabled(overviewDock->getIsAvailable());
    actionOverview->setChecked(overviewDock->getUserOpened());

    dashboardDock = new Dashboard(this);
    functionsDock = new FunctionsWidget(this);
    typesDock = new TypesWidget(this);
    searchDock = new SearchWidget(this);
    commentsDock = new CommentsWidget(this);
    stringsDock = new StringsWidget(this);

    QList<IaitoDockWidget *> debugDocks
        = {stackDock = new StackWidget(this),
           threadsDock = new ThreadsWidget(this),
           processesDock = new ProcessesWidget(this),
           backtraceDock = new BacktraceWidget(this),
           registersDock = new RegistersWidget(this),
           memoryMapDock = new MemoryMapWidget(this),
           breakpointDock = new BreakpointWidget(this),
           registerRefsDock = new RegisterRefsWidget(this)};

    QList<IaitoDockWidget *> infoDocks = {
        classesDock = new ClassesWidget(this),
        entrypointDock = new EntrypointWidget(this),
        exportsDock = new ExportsWidget(this),
        flagsDock = new FlagsWidget(this),
        headersDock = new HeadersWidget(this),
        importsDock = new ImportsWidget(this),
        relocsDock = new RelocsWidget(this),
        resourcesDock = new ResourcesWidget(this),
        sdbDock = new SdbWidget(this),
        sectionsDock = new SectionsWidget(this),
        segmentsDock = new SegmentsWidget(this),
        symbolsDock = new SymbolsWidget(this),
        vTablesDock = new VTablesWidget(this),
        zignaturesDock = new ZignaturesWidget(this),
        r2GraphDock = new R2GraphWidget(this),
        callGraphDock = new CallGraphWidget(this, false),
        globalCallGraphDock = new CallGraphWidget(this, true),
    };

    auto makeActionList = [this](QList<IaitoDockWidget *> docks) {
        QList<QAction *> result;
        for (auto dock : docks) {
            if (dock != nullptr) {
                result.push_back(dock->toggleViewAction());
            } else {
                auto separator = new QAction(this);
                separator->setSeparator(true);
                result.push_back(separator);
            }
        }
        return result;
    };

    QList<IaitoDockWidget *> windowDocks = {
        dashboardDock,
        nullptr,
        functionsDock,
        overviewDock,
        nullptr,
        searchDock,
        stringsDock,
        typesDock,
        nullptr,
    };
    ui->menuWindows->insertActions(ui->actionExtraDecompiler, makeActionList(windowDocks));
    QList<IaitoDockWidget *> windowDocks2 = {
        consoleDock,
        commentsDock,
        nullptr,
    };
    ui->menuWindows->addActions(makeActionList(windowDocks2));
    ui->menuAddInfoWidgets->addActions(makeActionList(infoDocks));
    ui->menuAddDebugWidgets->addActions(makeActionList(debugDocks));

    auto uniqueDocks = windowDocks + windowDocks2 + infoDocks + debugDocks;
    for (auto dock : uniqueDocks) {
        if (dock) { // ignore nullptr used as separators
            addWidget(dock);
        }
    }
}

void MainWindow::toggleOverview(bool visibility, GraphWidget *targetGraph)
{
    if (!overviewDock) {
        return;
    }
    if (visibility) {
        overviewDock->setTargetGraphWidget(targetGraph);
    }
}

void MainWindow::updateTasksIndicator()
{
    bool running = core->getAsyncTaskManager()->getTasksRunning();
    tasksProgressIndicator->setProgressIndicatorVisible(running);
}

void MainWindow::addExtraGraph()
{
    auto *extraDock = new GraphWidget(this);
    addExtraWidget(extraDock);
}

void MainWindow::addExtraHexdump()
{
    auto *extraDock = new HexdumpWidget(this);
    addExtraWidget(extraDock);
}

void MainWindow::addExtraDisassembly()
{
    auto *extraDock = new DisassemblyWidget(this);
    addExtraWidget(extraDock);
}

void MainWindow::addExtraDecompiler()
{
    auto *extraDock = new DecompilerWidget(this);
    addExtraWidget(extraDock);
}

void MainWindow::addExtraWidget(IaitoDockWidget *extraDock)
{
    extraDock->setTransient(true);
    dockOnMainArea(extraDock);
    addWidget(extraDock);
    extraDock->show();
    extraDock->raise();
}

QMenu *MainWindow::getMenuByType(MenuType type)
{
    switch (type) {
    case MenuType::File:
        return ui->menuFile;
    case MenuType::Edit:
        return ui->menuEdit;
    case MenuType::View:
        return ui->menuView;
    case MenuType::Windows:
        return ui->menuWindows;
    case MenuType::Debug:
        return ui->menuDebug;
    case MenuType::Help:
        return ui->menuHelp;
    case MenuType::Plugins:
        return ui->menuPlugins;
    default:
        return nullptr;
    }
}

void MainWindow::addPluginDockWidget(IaitoDockWidget *dockWidget)
{
    addWidget(dockWidget);
    ui->menuPlugins->addAction(dockWidget->toggleViewAction());
    addDockWidget(Qt::DockWidgetArea::TopDockWidgetArea, dockWidget);
    pluginDocks.push_back(dockWidget);
}

void MainWindow::addMenuFileAction(QAction *action)
{
    ui->menuFile->addAction(action);
}

void MainWindow::openCurrentCore(InitialOptions &options, bool skipOptionsDialog)
{
    RCore *core = iaitoPluginCore();
    if (core == nullptr) {
        return;
    }
    // filename taken from r_core
    options.filename = r_core_cmd_strf(core, "i~^file[1]");
    setFilename(options.filename);

    finalizeOpen();
    /* Show analysis options dialog */
    // displayInitialOptionsDialog(options, skipOptionsDialog);
}

void MainWindow::openNewFile(InitialOptions &options, bool skipOptionsDialog)
{
    setFilename(options.filename);

    /* Prompt to load filename.r2 script */
    if (options.script.isEmpty()) {
        QString script = QStringLiteral("%1.r2").arg(this->filename);
        if (r_file_exists(script.toStdString().data())) {
            QMessageBox mb;
            mb.setWindowTitle(tr("Script loading"));
            mb.setText(tr("Do you want to load the '%1' script?").arg(script));
            mb.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
            if (mb.exec() == QMessageBox::Yes) {
                options.script = script;
            }
        }
    }

    /* Show analysis options dialog */
    displayInitialOptionsDialog(options, skipOptionsDialog);
}

void MainWindow::openNewFileFailed()
{
    displayNewFileDialog();
    QMessageBox mb(this);
    mb.setIcon(QMessageBox::Critical);
    mb.setStandardButtons(QMessageBox::Ok);
    mb.setWindowTitle(tr("Cannot open file!"));
    mb.setText(tr("Could not open the file! Make sure the file exists and that "
                  "you have the correct permissions."));
    mb.exec();
}

/**
 * @brief displays the WelocmeDialog
 *
 * Upon first execution of Iaito, the WelcomeDialog would be showed to the user.
 * The Welcome dialog would be showed after a reset of Iaito's preferences by
 * the user.
 */

void MainWindow::displayWelcomeDialog()
{
    WelcomeDialog w;
    w.exec();
}

void MainWindow::displayNewFileDialog()
{
    NewFileDialog *n = new NewFileDialog(this);
    newFileDialog = n;
    n->setAttribute(Qt::WA_DeleteOnClose);
    n->show();
}

void MainWindow::closeNewFileDialog()
{
    if (newFileDialog) {
        newFileDialog->close();
    }
    newFileDialog = nullptr;
}

void MainWindow::displayInitialOptionsDialog(const InitialOptions &options, bool skipOptionsDialog)
{
    auto o = new InitialOptionsDialog(this);
    o->setAttribute(Qt::WA_DeleteOnClose);
    o->loadOptions(options);

    if (skipOptionsDialog) {
        o->setupAndStartAnalysis();
    } else {
        o->show();
    }
}

void MainWindow::openProject(const QString &project_name)
{
    QString filename = core->cmdRaw("Pi " + project_name);
    setFilename(filename.trimmed());

    core->openProject(project_name);

    finalizeOpen();
}

void MainWindow::finalizeOpen()
{
    core->getOpcodes();
    core->updateSeek();
    refreshAll();
    // Add fortune message
    core->message("\n" + core->cmdRaw("fo"));

    // hide all docks before showing window to avoid false positive for
    // refreshDeferrer
    for (auto dockWidget : dockWidgets) {
        dockWidget->hide();
    }

    QSettings settings;
    auto geometry = settings.value("geometry").toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
        show();
    } else {
        showMaximized();
    }

    Config()->adjustColorThemeDarkness();
    setViewLayout(getViewLayout(LAYOUT_DEFAULT));

    // Set focus to disasm or graph widget
    // Graph with function in it has focus priority over DisasmWidget.
    // If there are no graph/disasm widgets focus on MainWindow

    setFocus();
    bool graphContainsFunc = false;
    for (auto dockWidget : dockWidgets) {
        auto graphWidget = qobject_cast<GraphWidget *>(dockWidget);
        if (graphWidget && dockWidget->isVisibleToUser()) {
            graphContainsFunc = !graphWidget->getGraphView()->getBlocks().empty();
            if (graphContainsFunc) {
                dockWidget->raiseMemoryWidget();
                break;
            }
        }
        auto disasmWidget = qobject_cast<DisassemblyWidget *>(dockWidget);
        if (disasmWidget && dockWidget->isVisibleToUser()) {
            disasmWidget->raiseMemoryWidget();
            // continue looping in case there is a graph widget
        }
        auto decompilerWidget = qobject_cast<DecompilerWidget *>(dockWidget);
        if (decompilerWidget && dockWidget->isVisibleToUser()) {
            decompilerWidget->raiseMemoryWidget();
            // continue looping in case there is a graph widget
        }
    }
    consoleDock->show();
}

bool MainWindow::saveProject(bool quit)
{
    QString projectName = core->getConfig("prj.name");
    if (projectName.isEmpty()) {
        return saveProjectAs(quit);
    }
    if (core->getConfigb("cfg.debug")) {
        QMessageBox::warning(this, tr("Error"), tr("You can't save a project while debugging"));
        return false;
    }
    core->saveProject(projectName);
    return true;
}

bool MainWindow::saveProjectAs(bool quit)
{
    if (core->getConfigb("cfg.debug")) {
        QMessageBox::warning(this, tr("Error"), tr("You can't save a project while debugging"));
        return false;
    }
    SaveProjectDialog dialog(quit, this);
    return SaveProjectDialog::Rejected != dialog.exec();
}

void MainWindow::refreshOmniBar(const QStringList &flags)
{
    omnibar->refresh(flags);
}

void MainWindow::setFilename(const QString &fn)
{
    // Add file name to window title
    this->filename = fn;
    this->setWindowTitle(APPNAME " – " + fn);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    RCore *kore = iaitoPluginCore();
    if (kore != nullptr) {
        // TODO: restore configs :)
        consoleDock->unredirectOutput();
        // qApp->quit();
        r_config_set_i(kore->config, "scr.color", 2);
        r_config_set_i(kore->config, "scr.html", 0);
        QMainWindow::closeEvent(event);
        return;
    }
    if (this->filename == "") {
        QMainWindow::closeEvent(event);
        return;
    }

    // Check if there are uncommitted changes
    if (!ioModesController.askCommitUnsavedChanges()) {
        // if false, Cancel was chosen
        event->ignore();
        return;
    }

    QMessageBox::StandardButton ret = QMessageBox::question(
        this,
        APPNAME,
        tr("Do you really want to exit?\nSave your project before closing!"),
        (QMessageBox::StandardButtons)(
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel));
    if (ret == QMessageBox::Cancel) {
        event->ignore();
        return;
    }
    if (ret == QMessageBox::Discard) {
        event->ignore();
        QMainWindow::closeEvent(event);
        return;
    }

    if (ret == QMessageBox::Save && !saveProject(true)) {
        event->ignore();
        return;
    }
    // discard (do not save)
    if (core->currentlyDebugging) {
        core->stopDebug();
    } else {
        saveSettings();
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::paintEvent(QPaintEvent *event)
{
    QMainWindow::paintEvent(event);
    /*
     * Dirty hack
     * Just to adjust the width of Functions Widget to fixed size.
     * After everything is drawn, restore the max width limit.
     */
    if (functionsDock && functionDockWidthToRestore) {
        functionsDock->setMaximumWidth(functionDockWidthToRestore);
        functionDockWidthToRestore = 0;
    }
}

void MainWindow::readSettings()
{
    QSettings settings;

    responsive = settings.value("responsive").toBool();
    lockDocks(settings.value("panelLock").toBool());
    tabsOnTop = settings.value("tabsOnTop", true).toBool();
    setTabLocation();
    bool dockGroupedDragging = settings.value("docksGroupedDragging", false).toBool();
    ui->actionGrouped_dock_dragging->setChecked(dockGroupedDragging);
    on_actionGrouped_dock_dragging_triggered(dockGroupedDragging);

    loadLayouts(settings);
}

void MainWindow::saveSettings()
{
    QSettings settings;

    settings.setValue("panelLock", !ui->actionUnlock->isChecked());
    settings.setValue("tabsOnTop", tabsOnTop);
    settings.setValue("docksGroupedDragging", ui->actionGrouped_dock_dragging->isChecked());
    settings.setValue("geometry", saveGeometry());

    layouts[Core()->currentlyDebugging ? LAYOUT_DEBUG : LAYOUT_DEFAULT] = getViewLayout();
    saveLayouts(settings);
}

void MainWindow::setTabLocation()
{
    if (tabsOnTop) {
        this->setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
        ui->actionTabs_on_Top->setChecked(true);
    } else {
        this->setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::South);
        ui->actionTabs_on_Top->setChecked(false);
    }
}

void MainWindow::refreshAll()
{
    core->triggerRefreshAll();
}

void MainWindow::lockDocks(bool lock)
{
    if (ui->actionUnlock->isChecked() == lock) {
        ui->actionUnlock->setChecked(!lock);
    }
    if (lock) {
        for (QDockWidget *dockWidget : findChildren<QDockWidget *>()) {
            dockWidget->setFeatures(QDockWidget::NoDockWidgetFeatures);
        }
    } else {
        for (QDockWidget *dockWidget : findChildren<QDockWidget *>()) {
            dockWidget->setFeatures(
                QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable
                | QDockWidget::DockWidgetFloatable);
        }
    }
}

void MainWindow::restoreDocks()
{
    // Initial structure
    // func | main area | debug
    //      |___________|
    //      | console   |
    addDockWidget(Qt::LeftDockWidgetArea, functionsDock);
    splitDockWidget(functionsDock, dashboardDock, Qt::Horizontal);
    splitDockWidget(dashboardDock, stackDock, Qt::Horizontal);
    splitDockWidget(dashboardDock, consoleDock, Qt::Vertical);

    // overview bellow func
    splitDockWidget(functionsDock, overviewDock, Qt::Vertical);

    // main area
    tabifyDockWidget(dashboardDock, entrypointDock);
    tabifyDockWidget(dashboardDock, flagsDock);
    tabifyDockWidget(dashboardDock, stringsDock);
    tabifyDockWidget(dashboardDock, relocsDock);
    tabifyDockWidget(dashboardDock, importsDock);
    tabifyDockWidget(dashboardDock, exportsDock);
    tabifyDockWidget(dashboardDock, typesDock);
    tabifyDockWidget(dashboardDock, searchDock);
    tabifyDockWidget(dashboardDock, headersDock);
    tabifyDockWidget(dashboardDock, zignaturesDock);
    tabifyDockWidget(dashboardDock, symbolsDock);
    tabifyDockWidget(dashboardDock, classesDock);
    tabifyDockWidget(dashboardDock, resourcesDock);
    tabifyDockWidget(dashboardDock, vTablesDock);
    tabifyDockWidget(dashboardDock, sdbDock);
    tabifyDockWidget(dashboardDock, memoryMapDock);
    tabifyDockWidget(dashboardDock, breakpointDock);
    tabifyDockWidget(dashboardDock, registerRefsDock);
    tabifyDockWidget(dashboardDock, r2GraphDock);
    tabifyDockWidget(dashboardDock, callGraphDock);
    tabifyDockWidget(dashboardDock, globalCallGraphDock);
    for (const auto &it : dockWidgets) {
        // Check whether or not current widgets is graph, hexdump or disasm
        if (isExtraMemoryWidget(it)) {
            tabifyDockWidget(dashboardDock, it);
        }
    }

    // Console | Sections/segments/comments
    splitDockWidget(consoleDock, sectionsDock, Qt::Horizontal);
    tabifyDockWidget(sectionsDock, segmentsDock);
    tabifyDockWidget(sectionsDock, commentsDock);

    // Add Stack, Registers, Threads and Backtrace vertically stacked
    splitDockWidget(stackDock, registersDock, Qt::Vertical);
    tabifyDockWidget(stackDock, backtraceDock);
    tabifyDockWidget(backtraceDock, threadsDock);
    tabifyDockWidget(threadsDock, processesDock);

    for (auto dock : pluginDocks) {
        dockOnMainArea(dock);
    }
    lockDocks(false);
}

bool MainWindow::isDebugWidget(QDockWidget *dock) const
{
    return dock == stackDock || dock == registersDock || dock == backtraceDock
           || dock == threadsDock || dock == memoryMapDock || dock == breakpointDock
           || dock == processesDock || dock == registerRefsDock;
}

bool MainWindow::isExtraMemoryWidget(QDockWidget *dock) const
{
    return qobject_cast<GraphWidget *>(dock) || qobject_cast<HexdumpWidget *>(dock)
           || qobject_cast<DisassemblyWidget *>(dock) || qobject_cast<DecompilerWidget *>(dock);
}

MemoryWidgetType MainWindow::getMemoryWidgetTypeToRestore()
{
    if (lastSyncMemoryWidget) {
        return lastSyncMemoryWidget->getType();
    }
    return MemoryWidgetType::Disassembly;
}

QString MainWindow::getUniqueObjectName(const QString &widgetType) const
{
    QStringList docks;
    docks.reserve(dockWidgets.size());
    QString name;
    for (const auto &it : dockWidgets) {
        name = it->objectName();
        if (name.split(';').at(0) == widgetType) {
            docks.push_back(name);
        }
    }

    if (docks.isEmpty()) {
        return widgetType;
    }

    int id = 0;
    while (docks.contains(widgetType + ";" + QString::number(id))) {
        id++;
    }

    return widgetType + ";" + QString::number(id);
}

void MainWindow::showMemoryWidget()
{
    if (lastSyncMemoryWidget) {
        if (lastSyncMemoryWidget->tryRaiseMemoryWidget()) {
            return;
        }
    }
    showMemoryWidget(MemoryWidgetType::Disassembly);
}

void MainWindow::showMemoryWidget(MemoryWidgetType type)
{
    for (auto &dock : dockWidgets) {
        if (auto memoryWidget = qobject_cast<MemoryDockWidget *>(dock)) {
            if (memoryWidget->getType() == type && memoryWidget->getSeekable()->isSynchronized()) {
                memoryWidget->tryRaiseMemoryWidget();
                return;
            }
        }
    }
    auto memoryDockWidget = addNewMemoryWidget(type, Core()->getOffset());
    memoryDockWidget->raiseMemoryWidget();
}

QMenu *MainWindow::createShowInMenu(QWidget *parent, RVA address, AddressTypeHint addressType)
{
    QMenu *menu = new QMenu(parent);
    // Memory dock widgets
    for (auto &dock : dockWidgets) {
        if (auto memoryWidget = qobject_cast<MemoryDockWidget *>(dock)) {
            if (memoryWidget->getType() == MemoryWidgetType::Graph
                || memoryWidget->getType() == MemoryWidgetType::Decompiler) {
                if (addressType == AddressTypeHint::Data) {
                    continue;
                }
            }
            QAction *action = new QAction(memoryWidget->windowTitle(), menu);
            connect(action, &QAction::triggered, this, [memoryWidget, address]() {
                memoryWidget->getSeekable()->seek(address);
                memoryWidget->raiseMemoryWidget();
            });
            menu->addAction(action);
        }
    }
    menu->addSeparator();
    // Rest of the AddressableDockWidgets that weren't added already
    for (auto &dock : dockWidgets) {
        if (auto memoryWidget = qobject_cast<AddressableDockWidget *>(dock)) {
            if (qobject_cast<MemoryDockWidget *>(dock)) {
                continue;
            }
            QAction *action = new QAction(memoryWidget->windowTitle(), menu);
            connect(action, &QAction::triggered, this, [memoryWidget, address]() {
                memoryWidget->getSeekable()->seek(address);
                memoryWidget->raiseMemoryWidget();
            });
            menu->addAction(action);
        }
    }
    menu->addSeparator();
    auto createAddNewWidgetAction = [this, menu, address](QString label, MemoryWidgetType type) {
        QAction *action = new QAction(label, menu);
        connect(action, &QAction::triggered, this, [this, address, type]() {
            addNewMemoryWidget(type, address, false);
        });
        menu->addAction(action);
    };
    createAddNewWidgetAction(tr("New disassembly"), MemoryWidgetType::Disassembly);
    if (addressType != AddressTypeHint::Data) {
        createAddNewWidgetAction(tr("New graph"), MemoryWidgetType::Graph);
    }
    createAddNewWidgetAction(tr("New hexdump"), MemoryWidgetType::Hexdump);
    createAddNewWidgetAction(tr("New Decompiler"), MemoryWidgetType::Decompiler);
    return menu;
}

void MainWindow::setCurrentMemoryWidget(MemoryDockWidget *memoryWidget)
{
    if (memoryWidget->getSeekable()->isSynchronized()) {
        lastSyncMemoryWidget = memoryWidget;
    }
    lastMemoryWidget = memoryWidget;
}

MemoryDockWidget *MainWindow::getLastMemoryWidget()
{
    return lastMemoryWidget;
}

MemoryDockWidget *MainWindow::addNewMemoryWidget(
    MemoryWidgetType type, RVA address, bool synchronized)
{
    MemoryDockWidget *memoryWidget = nullptr;
    switch (type) {
    case MemoryWidgetType::Graph:
        memoryWidget = new GraphWidget(this);
        break;
    case MemoryWidgetType::Hexdump:
        memoryWidget = new HexdumpWidget(this);
        break;
    case MemoryWidgetType::Disassembly:
        memoryWidget = new DisassemblyWidget(this);
        break;
    case MemoryWidgetType::Decompiler:
        memoryWidget = new DecompilerWidget(this);
        break;
    }
    auto seekable = memoryWidget->getSeekable();
    seekable->setSynchronization(synchronized);
    seekable->seek(address);
    addExtraWidget(memoryWidget);
    return memoryWidget;
}

void MainWindow::initBackForwardMenu()
{
    auto prepareButtonMenu = [this](QAction *action) -> QMenu * {
        QToolButton *button = qobject_cast<QToolButton *>(ui->mainToolBar->widgetForAction(action));
        if (!button) {
            return nullptr;
        }
        QMenu *menu = new QMenu(button);
        button->setMenu(menu);
        button->setPopupMode(QToolButton::DelayedPopup);
        button->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(
            button, &QWidget::customContextMenuRequested, button, [menu, button](const QPoint &pos) {
                menu->exec(button->mapToGlobal(pos));
            });

        QFontMetrics metrics(fontMetrics());
        // Roughly 10-16 lines depending on padding size, no need to calculate
        // more precisely
        menu->setMaximumHeight(metrics.lineSpacing() * 20);

        menu->setToolTipsVisible(true);
        return menu;
    };

    if (auto menu = prepareButtonMenu(ui->actionBackward)) {
        menu->setObjectName("historyMenu");
        connect(menu, &QMenu::aboutToShow, menu, [this, menu]() { updateHistoryMenu(menu, false); });
    }
    if (auto menu = prepareButtonMenu(ui->actionForward)) {
        menu->setObjectName("forwardHistoryMenu");
        connect(menu, &QMenu::aboutToShow, menu, [this, menu]() { updateHistoryMenu(menu, true); });
    }
}

void MainWindow::updateHistoryMenu(QMenu *menu, bool redo)
{
    // Not too long so that whole screen doesn't get covered,
    // not too short so that reasonable length c++ names can be seen most of the
    // time
    const int MAX_NAME_LENGTH = 64;

    auto hist = Core()->cmdj("sj");
    bool history = true;
    QList<QAction *> actions;
    for (auto item : Core()->cmdj("sj").array()) {
        QJsonObject obj = item.toObject();
        QString name = obj["name"].toString();
        RVA offset = obj["offset"].toVariant().toULongLong();
        bool current = obj["current"].toBool(false);
        if (current) {
            history = false;
        }
        if (history != redo || current) { // Include current in both directions
            QString addressString = RAddressString(offset);

            QString toolTip
                = QStringLiteral("%1 %2").arg(addressString, name); // show non truncated name in tooltip

            name.truncate(MAX_NAME_LENGTH); // TODO:#1904 use common name
                                            // shortening function
            QString label = QStringLiteral("%1 (%2)").arg(name, addressString);
            if (current) {
                label = QStringLiteral("current position (%1)").arg(addressString);
            }
            QAction *action = new QAction(label, menu);
            action->setToolTip(toolTip);
            actions.push_back(action);
            if (current) {
                QFont font;
                font.setBold(true);
                action->setFont(font);
            }
        }
    }
    if (!redo) {
        std::reverse(actions.begin(), actions.end());
    }
    menu->clear();
    menu->addActions(actions);
    int steps = 0;
    for (QAction *item : menu->actions()) {
        if (redo) {
            connect(item, &QAction::triggered, item, [steps]() {
                for (int i = 0; i < steps; i++) {
                    Core()->seekNext();
                }
            });
        } else {
            connect(item, &QAction::triggered, item, [steps]() {
                for (int i = 0; i < steps; i++) {
                    Core()->seekPrev();
                }
            });
        }
        ++steps;
    }
}

void MainWindow::updateLayoutsMenu()
{
    ui->menuLayouts->clear();
    for (auto it = layouts.begin(), end = layouts.end(); it != end; ++it) {
        QString name = it.key();
        if (isBuiltinLayoutName(name)) {
            continue;
        }
        auto action = new QAction(it.key(), ui->menuLayouts);
        connect(action, &QAction::triggered, this, [this, name]() {
            setViewLayout(getViewLayout(name));
        });
        ui->menuLayouts->addAction(action);
    }
}

void MainWindow::saveNamedLayout()
{
    bool ok = false;
    QString name;
    QStringList names = layouts.keys();
    names.removeAll(LAYOUT_DEBUG);
    names.removeAll(LAYOUT_DEFAULT);
    while (name.isEmpty() || isBuiltinLayoutName(name)) {
        if (ok) {
            QMessageBox::warning(
                this, tr("Save layout error"), tr("'%1' is not a valid name.").arg(name));
        }
        name = QInputDialog::getItem(this, tr("Save layout"), tr("Enter name"), names, -1, true, &ok);
        if (!ok) {
            return;
        }
    }
    layouts[name] = getViewLayout();
    updateLayoutsMenu();
    saveSettings();
}

void MainWindow::manageLayouts()
{
    LayoutManager layoutManger(layouts, this);
    layoutManger.exec();
    updateLayoutsMenu();
}

void MainWindow::addWidget(IaitoDockWidget *widget)
{
    dockWidgets.push_back(widget);
}

void MainWindow::addMemoryDockWidget(MemoryDockWidget *widget)
{
    connect(widget, &QDockWidget::visibilityChanged, this, [this, widget](bool visibility) {
        if (visibility) {
            setCurrentMemoryWidget(widget);
        }
    });
}

void MainWindow::removeWidget(IaitoDockWidget *widget)
{
    dockWidgets.removeAll(widget);
    pluginDocks.removeAll(widget);
    if (lastSyncMemoryWidget == widget) {
        lastSyncMemoryWidget = nullptr;
    }
    if (lastMemoryWidget == widget) {
        lastMemoryWidget = nullptr;
    }
}

void MainWindow::showZenDocks()
{
    const QList<QDockWidget *> zenDocks
        = {functionsDock, dashboardDock, stringsDock, searchDock, importsDock};
    functionDockWidthToRestore = functionsDock->maximumWidth();
    functionsDock->setMaximumWidth(200);
    for (auto w : dockWidgets) {
        if (zenDocks.contains(w) || isExtraMemoryWidget(w)) {
            w->show();
        }
    }
    dashboardDock->raise();
}

void MainWindow::showDebugDocks()
{
    const QList<QDockWidget *> debugDocks
        = {functionsDock,
           stringsDock,
           searchDock,
           stackDock,
           registersDock,
           backtraceDock,
           threadsDock,
           memoryMapDock,
           breakpointDock};
    functionDockWidthToRestore = functionsDock->maximumWidth();
    functionsDock->setMaximumWidth(200);
    auto registerWidth = qhelpers::forceWidth(registersDock, std::min(500, this->width() / 4));
    auto registerHeight = qhelpers::forceHeight(registersDock, std::max(100, height() / 2));
    QDockWidget *widgetToFocus = nullptr;
    for (auto w : dockWidgets) {
        if (debugDocks.contains(w) || isExtraMemoryWidget(w)) {
            w->show();
        }
        if (qobject_cast<DisassemblyWidget *>(w)) {
            widgetToFocus = w;
        }
    }
    registerHeight.restoreHeight(registersDock);
    registerWidth.restoreWidth(registersDock);
    if (widgetToFocus) {
        widgetToFocus->raise();
    }
}

void MainWindow::dockOnMainArea(QDockWidget *widget)
{
    QDockWidget *best = nullptr;
    float bestScore = 1;
    // choose best existing area for placing the new widget
    for (auto dock : dockWidgets) {
        if (dock->isHidden() || dock == widget || dock->isFloating()
            ||                              // tabifying onto floating dock using code
                                            // doesn't work well
            dock->parentWidget() != this) { // floating group isn't considered floating
            continue;
        }
        float newScore = 0;
        if (isExtraMemoryWidget(dock)) {
            newScore += 10000000; // prefer existing disssasembly and graph widgets
        }
        newScore += dock->width() * dock->height(); // the bigger the better

        if (newScore > bestScore) {
            bestScore = newScore;
            best = dock;
        }
    }
    if (best) {
        tabifyDockWidget(best, widget);
    } else {
        addDockWidget(Qt::TopDockWidgetArea, widget, Qt::Orientation::Horizontal);
    }
}

void MainWindow::enableDebugWidgetsMenu(bool enable)
{
    for (QAction *action : ui->menuAddDebugWidgets->actions()) {
        // The breakpoints menu should be available outside of debug
        if (!action->text().contains("Breakpoints")) {
            action->setEnabled(enable);
        }
    }
}

IaitoLayout MainWindow::getViewLayout()
{
    IaitoLayout layout;
    layout.geometry = saveGeometry();
    layout.state = saveState();

    for (auto dock : dockWidgets) {
        QVariantMap properties;
        if (auto cutterDock = qobject_cast<IaitoDockWidget *>(dock)) {
            properties = cutterDock->serializeViewProprties();
        }
        layout.viewProperties.insert(dock->objectName(), std::move(properties));
    }
    return layout;
}

IaitoLayout MainWindow::getViewLayout(const QString &name)
{
    auto it = layouts.find(name);
    if (it != layouts.end()) {
        return *it;
    }
    return {};
}

void MainWindow::setViewLayout(const IaitoLayout &layout)
{
    bool isDefault = layout.state.isEmpty() || layout.geometry.isEmpty();
    bool isDebug = Core()->currentlyDebugging;

    // make a copy to avoid iterating over container from which items are being
    // removed
    auto widgetsToClose = dockWidgets;

    for (auto dock : widgetsToClose) {
        dock->hide();
        dock->close();
        dock->setFloating(false); // tabifyDockWidget doesn't work if dock is floating
        removeDockWidget(dock);
    }

    QStringList docksToCreate;
    if (isDefault) {
        docksToCreate = QStringList{
            DisassemblyWidget::getWidgetType(),
            GraphWidget::getWidgetType(),
            HexdumpWidget::getWidgetType(),
            DecompilerWidget::getWidgetType()};
    } else {
        docksToCreate = layout.viewProperties.keys();
    }

    for (const auto &it : docksToCreate) {
        if (std::none_of(dockWidgets.constBegin(), dockWidgets.constEnd(), [&it](QDockWidget *w) {
                return w->objectName() == it;
            })) {
            auto className = it.split(';').at(0);
            if (widgetTypeToConstructorMap.contains(className)) {
                auto widget = widgetTypeToConstructorMap[className](this);
                widget->setObjectName(it);
                addExtraWidget(widget);
            }
        }
    }

    restoreDocks();

    QList<QDockWidget *> newDocks;

    for (auto dock : dockWidgets) {
        auto properties = layout.viewProperties.find(dock->objectName());
        if (properties != layout.viewProperties.end()) {
            dock->deserializeViewProperties(*properties);
        } else {
            dock->deserializeViewProperties({}); // call with empty properties to reset the widget
            newDocks.push_back(dock);
        }
        dock->ignoreVisibilityStatus(true);
    }

    if (!isDefault) {
        restoreState(layout.state);

        for (auto dock : newDocks) {
            dock->hide(); // hide to prevent dockOnMainArea putting them on each
                          // other
        }
        for (auto dock : newDocks) {
            dockOnMainArea(dock);
            // Show any new docks by default.
            // Showing new builtin docks helps discovering features added in
            // latest release. Installing a new plugin hints that usre will
            // likely want to use it.
            dock->show();
        }
    } else {
        if (isDebug) {
            showDebugDocks();
        } else {
            showZenDocks();
        }
    }

    for (auto dock : dockWidgets) {
        dock->ignoreVisibilityStatus(false);
    }
    lastSyncMemoryWidget = nullptr;
    lastMemoryWidget = nullptr;
}

void MainWindow::loadLayouts(QSettings &settings)
{
    this->layouts.clear();
    int size = settings.beginReadArray("layouts");
    for (int i = 0; i < size; i++) {
        IaitoLayout layout;
        settings.setArrayIndex(i);
        QString name = settings.value("name", "layout").toString();
        layout.geometry = settings.value("geometry").toByteArray();
        layout.state = settings.value("state").toByteArray();

        auto docks = settings.value("docks").toMap();
        for (auto it = docks.begin(), end = docks.end(); it != end; it++) {
            layout.viewProperties.insert(it.key(), it.value().toMap());
        }

        layouts.insert(name, std::move(layout));
    }
    settings.endArray();
    updateLayoutsMenu();
}

void MainWindow::saveLayouts(QSettings &settings)
{
    settings.beginWriteArray("layouts", layouts.size());
    int arrayIndex = 0;
    for (auto it = layouts.begin(), end = layouts.end(); it != end; ++it, ++arrayIndex) {
        settings.setArrayIndex(arrayIndex);
        settings.setValue("name", it.key());
        auto &layout = it.value();
        settings.setValue("state", layout.state);
        settings.setValue("geometry", layout.geometry);
        QVariantMap properties;
        for (auto it = layout.viewProperties.begin(), end = layout.viewProperties.end(); it != end;
             ++it) {
            properties.insert(it.key(), it.value());
        }
        settings.setValue("docks", properties);
    }
    settings.endArray();
}

void MainWindow::on_actionDefault_triggered()
{
    if (core->currentlyDebugging) {
        layouts[LAYOUT_DEBUG] = {};
        setViewLayout(layouts[LAYOUT_DEBUG]);
    } else {
        layouts[LAYOUT_DEFAULT] = {};
        setViewLayout(layouts[LAYOUT_DEFAULT]);
    }
}

/**
 * @brief MainWindow::on_actionNew_triggered
 * Open a new Iaito session.
 */
void MainWindow::on_actionNew_triggered()
{
    // Create a new Iaito process
    static_cast<IaitoApplication *>(qApp)->launchNewInstance();
}

void MainWindow::on_actionSave_triggered()
{
    saveProject();
}

void MainWindow::on_actionSaveAs_triggered()
{
    saveProjectAs();
}

void MainWindow::on_actionRun_Script_triggered()
{
    QFileDialog dialog(this);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setViewMode(QFileDialog::Detail);
    dialog.setDirectory(QDir::home());

    auto fname = dialog.getOpenFileName(
        this, tr("Select radare2 script"), QString(), QString(), 0, QFILEDIALOG_FLAGS);
    const QString &fileName = QDir::toNativeSeparators(fname);
    if (fileName.isEmpty()) // Cancel was pressed
        return;

    RunScriptTask *runScriptTask = new RunScriptTask();
    runScriptTask->setFileName(fileName);

    AsyncTask::Ptr runScriptTaskPtr(runScriptTask);

    AsyncTaskDialog *taskDialog = new AsyncTaskDialog(runScriptTaskPtr, this);
    taskDialog->setInterruptOnClose(true);
    taskDialog->setAttribute(Qt::WA_DeleteOnClose);
    taskDialog->show();

    Core()->getAsyncTaskManager()->start(runScriptTaskPtr);
}

/**
 * @brief MainWindow::on_actionOpen_triggered
 * Open a file as in "load (add) a file in current session".
 */
void MainWindow::on_actionMap_triggered()
{
    MapFileDialog dialog(this);
    dialog.exec();
}

void MainWindow::toggleResponsive(bool maybe)
{
    this->responsive = maybe;
    // Save options in settings
    QSettings settings;
    settings.setValue("responsive", this->responsive);
}

void MainWindow::on_actionTabs_on_Top_triggered()
{
    this->on_actionTabs_triggered();
}

void MainWindow::on_actionReset_settings_triggered()
{
    QMessageBox::StandardButton ret = (QMessageBox::StandardButton) QMessageBox::question(
        this,
        APPNAME,
        tr("Do you really want to clear all settings?"),
        QMessageBox::Ok | QMessageBox::Cancel);
    if (ret == QMessageBox::Ok) {
        Config()->resetAll();
        readSettings();
        setViewLayout(getViewLayout(Core()->currentlyDebugging ? LAYOUT_DEBUG : LAYOUT_DEFAULT));
    }
}

void MainWindow::on_actionQuit_triggered()
{
    close();
}

void MainWindow::on_actionBackward_triggered()
{
    core->seekPrev();
}

void MainWindow::on_actionForward_triggered()
{
    core->seekNext();
}

void MainWindow::on_actionDisasAdd_comment_triggered()
{
    CommentsDialog c(this);
    c.exec();
}

void MainWindow::on_actionRefresh_contents_triggered()
{
    refreshAll();
}

void MainWindow::on_actionPreferences_triggered()
{
    if (!findChild<PreferencesDialog *>()) {
        auto dialog = new PreferencesDialog(this);
        dialog->show();
    }
}

void MainWindow::on_actionTabs_triggered()
{
    tabsOnTop = !tabsOnTop;
    setTabLocation();
}

void MainWindow::on_actionAbout_triggered()
{
    AboutDialog *a = new AboutDialog(this);
    a->setAttribute(Qt::WA_DeleteOnClose);
    a->open();
}

void MainWindow::on_actionIssue_triggered()
{
    openIssue();
}

void MainWindow::websiteClicked()
{
    QDesktopServices::openUrl(QUrl("https://www.radare.org"));
}

void MainWindow::fortuneClicked()
{
    FortuneDialog dialog;
    dialog.setWindowTitle("Fortune Message");
    dialog.resize(300, 150);
    dialog.exec();
}

void MainWindow::bookClicked()
{
    QDesktopServices::openUrl(QUrl("https://book.rada.re"));
}

void MainWindow::on_actionRefresh_Panels_triggered()
{
    this->refreshAll();
}

/**
 * @brief A signal that creates an AsyncTask to re-analyze the current file
 */
void MainWindow::on_actionAnalyze_triggered()
{
#if MONOTHREAD
    R_LOG_ERROR("monothread for auto-reanalysis disabled");
#else
    InitialOptions options;
    options.analCmd = {{"aaa", "Auto analysis"}};
    auto *analTask = new AnalTask();
    AsyncTask::Ptr analTaskPtr(analTask);

    auto *taskDialog = new AsyncTaskDialog(analTaskPtr);
    taskDialog->setInterruptOnClose(true);
    taskDialog->setAttribute(Qt::WA_DeleteOnClose);
    taskDialog->show();
    connect(analTask, &AnalTask::finished, this, &MainWindow::refreshAll);

    Core()->getAsyncTaskManager()->start(analTaskPtr);
#endif
}

void MainWindow::on_actionImportPDB_triggered()
{
    QFileDialog dialog(this);
    dialog.setWindowTitle(tr("Select PDB file"));
    dialog.setNameFilters({tr("PDB file (*.pdb)"), tr("All files (*)")});

    if (!dialog.exec()) {
        return;
    }

    const QString &pdbFile = QDir::toNativeSeparators(dialog.selectedFiles().first());

    if (!pdbFile.isEmpty()) {
        core->loadPDB(pdbFile);
        core->message(tr("%1 loaded.").arg(pdbFile));
        this->refreshAll();
    }
}

void MainWindow::on_actionExport_as_code_triggered()
{
    QStringList filters;
    QMap<QString, QString> cmdMap;

    filters << tr("C uin8_t array (*.c)");
    cmdMap[filters.last()] = "pc";
    filters << tr("C uin16_t array (*.c)");
    cmdMap[filters.last()] = "pch";
    filters << tr("C uin32_t array (*.c)");
    cmdMap[filters.last()] = "pcw";
    filters << tr("C uin64_t array (*.c)");
    cmdMap[filters.last()] = "pcd";
    filters << tr("C string (*.c)");
    cmdMap[filters.last()] = "pcs";
    filters << tr("Shell-script that reconstructs the bin (*.sh)");
    cmdMap[filters.last()] = "pcS";
    filters << tr("JSON array (*.json)");
    cmdMap[filters.last()] = "pcj";
    filters << tr("JavaScript array (*.js)");
    cmdMap[filters.last()] = "pcJ";
    filters << tr("Python array (*.py)");
    cmdMap[filters.last()] = "pcp";
    filters << tr("Print 'wx' r2 commands (*.r2)");
    cmdMap[filters.last()] = "pc*";
    filters << tr("GAS .byte blob (*.asm, *.s)");
    cmdMap[filters.last()] = "pca";
    filters << tr(".bytes with instructions in comments (*.txt)");
    cmdMap[filters.last()] = "pcA";
    filters << tr("Disassembly of the current Section (*.asm)");
    cmdMap[filters.last()] = "pD $SS@e:asm.lines=0@$$@e:emu.str=true@e:scr.color=0";

    QFileDialog dialog(this, tr("Export as code"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setNameFilters(filters);
    dialog.selectFile("dump");
    dialog.setDefaultSuffix("c");
    if (!dialog.exec())
        return;

    QFile file(dialog.selectedFiles()[0]);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Can't open file";
        return;
    }
    bool pamode = true;
    QTextStream fileOut(&file);
    QString &cmd = cmdMap[dialog.selectedNameFilter()];
    if (cmd.contains("@")) {
        pamode = false;
    }
    if (pamode) {
        TempConfig tempConfig; // TODO Use RConfigHold
        tempConfig.set("io.va", false);
        QString &cmd = cmdMap[dialog.selectedNameFilter()];
        // Use cmd because cmdRaw would not handle such input
        fileOut << Core()->cmd(cmd + " $s @ 0");
    } else {
        // Use cmd because cmdRaw would not handle such input
        fileOut << Core()->cmd(cmd);
    }
}

void MainWindow::on_actionGrouped_dock_dragging_triggered(bool checked)
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 7, 0)
    auto options = dockOptions();
    options.setFlag(QMainWindow::DockOption::GroupedDragging, checked);
    setDockOptions(options);
#else
    Q_UNUSED(checked);
#endif
}

void MainWindow::seekToFunctionLastInstruction()
{
    Core()->seek(Core()->getLastFunctionInstruction(Core()->getOffset()));
}

void MainWindow::seekToFunctionStart()
{
    Core()->seek(Core()->getFunctionStart(Core()->getOffset()));
}

void MainWindow::projectSaved(bool successfully, const QString &name)
{
    if (successfully)
        core->message(tr("Project saved: %1").arg(name));
    else
        core->message(tr("Failed to save project: %1").arg(name));
}

void MainWindow::toggleDebugView()
{
    MemoryWidgetType memType = getMemoryWidgetTypeToRestore();
    if (Core()->currentlyDebugging) {
        layouts[LAYOUT_DEFAULT] = getViewLayout();
        setViewLayout(getViewLayout(LAYOUT_DEBUG));
        enableDebugWidgetsMenu(true);
    } else {
        layouts[LAYOUT_DEBUG] = getViewLayout();
        setViewLayout(getViewLayout(LAYOUT_DEFAULT));
        enableDebugWidgetsMenu(false);
    }
    showMemoryWidget(memType);
}

void MainWindow::mousePressEvent(QMouseEvent *event)
{
    switch (event->button()) {
    case Qt::BackButton:
        core->seekPrev();
        break;
    case Qt::ForwardButton:
        core->seekNext();
        break;
    default:
        break;
    }
}

bool MainWindow::eventFilter(QObject *, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::ForwardButton || mouseEvent->button() == Qt::BackButton) {
            mousePressEvent(mouseEvent);
            return true;
        }
    }
    return false;
}

bool MainWindow::event(QEvent *event)
{
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange
        || event->type() == QEvent::PaletteChange) {
#if QT_VERSION < QT_VERSION_CHECK(5, 10, 0)
        QMetaObject::invokeMethod(Config(), "refreshFont", Qt::ConnectionType::QueuedConnection);
#else
        QMetaObject::invokeMethod(
            Config(), &Configuration::refreshFont, Qt::ConnectionType::QueuedConnection);
#endif
    }
    return QMainWindow::event(event);
}

/**
 * @brief Show a warning message box.
 *
 * This API can either be used in Iaito internals, or by Python plugins.
 */
void MainWindow::messageBoxWarning(QString title, QString message)
{
    QMessageBox mb(this);
    mb.setIcon(QMessageBox::Warning);
    mb.setStandardButtons(QMessageBox::Ok);
    mb.setWindowTitle(title);
    mb.setText(message);
    mb.exec();
}

/**
 * @brief When theme changed, change icons which have a special version for the
 * theme.
 */
void MainWindow::chooseThemeIcons()
{
    // List of QActions which have alternative icons in different themes
    const QList<QPair<void *, QString>> kSupportedIconsNames{
        {ui->actionForward, QStringLiteral("arrow_right.svg")},
        {ui->actionBackward, QStringLiteral("arrow_left.svg")},
    };

    // Set the correct icon for the QAction
    qhelpers::setThemeIcons(kSupportedIconsNames, [](void *obj, const QIcon &icon) {
        static_cast<QAction *>(obj)->setIcon(icon);
    });
}

void MainWindow::onZoomIn()
{
    Config()->setZoomFactor(Config()->getZoomFactor() + 0.1);
}

void MainWindow::onZoomOut()
{
    Config()->setZoomFactor(Config()->getZoomFactor() - 0.1);
}

void MainWindow::onZoomReset()
{
    Config()->setZoomFactor(1.0);
}

QMenu *MainWindow::getContextMenuExtensions(ContextMenuType type)
{
    switch (type) {
    case ContextMenuType::Disassembly:
        return disassemblyContextMenuExtensions;
    case ContextMenuType::Addressable:
        return addressableContextMenuExtensions;
    default:
        return nullptr;
    }
}

void MainWindow::setAvailableIOModeOptions()
{
    switch (ioModesController.getIOMode()) {
    case IOModesController::Mode::WRITE:
        ui->actionWriteMode->setChecked(true);
        break;
    case IOModesController::Mode::CACHE:
        ui->actionCacheMode->setChecked(true);
        break;
    default:
        ui->actionReadOnly->setChecked(true);
    }
}
