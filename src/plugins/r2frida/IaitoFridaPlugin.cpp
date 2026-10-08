#include "IaitoFridaPlugin.h"

#include "FridaSession.h"
#include "FridaWidget.h"
#include "core/Iaito.h"
#include "core/MainWindow.h"

#include <QApplication>
#include <QClipboard>
#include <QMenu>
#include <QStatusBar>
#include <QTimer>

void IaitoFridaPlugin::setupPlugin() {}

void IaitoFridaPlugin::terminate()
{
    if (session && session->isAttached()) {
        session->detach();
    }
}

void IaitoFridaPlugin::setupInterface(MainWindow *main)
{
    mainWindow = main;
    session = new FridaSession(main);
    widget = new FridaWidget(session, main);
    main->addPluginDockWidget(widget);
    widget->hide();

    statusButton = new QToolButton(main);
    statusButton->setAutoRaise(true);
    statusButton->setToolTip(tr("r2frida session"));
    main->statusBar()->addPermanentWidget(statusButton);
    connect(statusButton, &QToolButton::clicked, this, [this]() {
        widget->show();
        widget->raise();
    });
    connect(session, &FridaSession::changed, this, &IaitoFridaPlugin::updateStatus);

    QTimer::singleShot(0, this, [this, main]() { buildMenu(main); });
    buildContextMenus(main);
    updateStatus();
}

void IaitoFridaPlugin::buildMenu(MainWindow *main)
{
    QMenu *debugMenu = main->getMenuByType(MainWindow::MenuType::Debug);
    if (!debugMenu) {
        return;
    }
    debugMenu->addSeparator();
    auto *menu = debugMenu->addMenu(tr("r2frida"));
    connectAction = menu->addAction(tr("Connect..."), widget, &FridaWidget::showConnectDialog);
    resumeAction = menu->addAction(tr("Resume"), session, &FridaSession::resume);
    detachAction = menu->addAction(tr("Detach"), session, &FridaSession::detach);
    menu->addSeparator();
    menu->addAction(tr("Show panel"), this, [this]() {
        widget->show();
        widget->raise();
    });
    if (!session->checkAvailable()) {
        connectAction->setEnabled(false);
        connectAction->setToolTip(
            tr("r2frida is not loaded. Install it with r2pm -ci r2frida and restart iaito."));
    }
    updateStatus();
}

void IaitoFridaPlugin::addFridaMenu(QMenu *pluginMenu)
{
    if (!pluginMenu) {
        return;
    }
    auto *menu = pluginMenu->addMenu(tr("Frida"));
    auto *trace = menu->addAction(tr("Trace"));
    auto *hook = menu->addAction(tr("Hook"));
    auto *breakpoint = menu->addAction(tr("Set breakpoint"));
    auto *live = menu->addAction(tr("Read live bytes"));
    auto *copy = menu->addAction(tr("Copy runtime address"));
    auto *mapping = menu->addAction(tr("Show address mapping"));
    const auto enable = [this, menu]() {
        const bool on = session->isAttached();
        for (QAction *action : menu->actions()) {
            action->setEnabled(on);
            action->setData(menu->menuAction()->data());
        }
    };
    connect(menu, &QMenu::aboutToShow, this, enable);
    connect(trace, &QAction::triggered, this, [this, trace]() {
        fridaTrace(session, trace->data().toULongLong());
    });
    connect(hook, &QAction::triggered, this, [this, hook]() {
        fridaShowHookDialog(mainWindow, session, hook->data().toULongLong());
    });
    connect(breakpoint, &QAction::triggered, this, [this, breakpoint]() {
        fridaBreakpoint(session, breakpoint->data().toULongLong());
    });
    connect(live, &QAction::triggered, this, [this, live]() {
        const quint64 addr = live->data().toULongLong();
        fridaShowLiveHex(mainWindow, session, fridaRuntimeAddress(session, addr));
    });
    connect(copy, &QAction::triggered, this, [this, copy]() {
        const quint64 runtime = fridaRuntimeAddress(session, copy->data().toULongLong());
        QApplication::clipboard()->setText(RAddressString(runtime));
    });
    connect(mapping, &QAction::triggered, this, [this, mapping]() {
        Core()->message(session->describe(mapping->data().toULongLong()));
    });
}

void IaitoFridaPlugin::buildContextMenus(MainWindow *main)
{
    addFridaMenu(main->getContextMenuExtensions(MainWindow::ContextMenuType::Disassembly));
    addFridaMenu(main->getContextMenuExtensions(MainWindow::ContextMenuType::Addressable));
}

void IaitoFridaPlugin::updateStatus()
{
    if (!statusButton) {
        return;
    }
    if (!session->checkAvailable()) {
        statusButton->setText(tr("FRIDA"));
        statusButton->setToolTip(
            tr("r2frida is not loaded. Install it with r2pm -ci r2frida and restart iaito."));
    } else if (session->isAttached()) {
        const FridaProcessInfo info = session->process();
        statusButton->setText(tr("FRIDA ● %1 [%2]").arg(info.name).arg(info.pid));
        statusButton->setToolTip(
            tr("%1 | %2 | %3 %4 | slide %5")
                .arg(
                    session->transportLabel(),
                    session->deviceLabel(),
                    info.arch,
                    QString::number(info.bits),
                    RAddressString(session->slide())));
    } else if (session->state() == FridaSessionState::Connecting) {
        statusButton->setText(tr("FRIDA …"));
    } else {
        statusButton->setText(tr("FRIDA"));
        statusButton->setToolTip(tr("r2frida is disconnected"));
    }
    if (resumeAction) {
        resumeAction->setEnabled(session->isSuspended());
    }
    if (detachAction) {
        detachAction->setEnabled(session->isAttached());
    }
}
