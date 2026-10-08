#include "IaitoFridaPlugin.h"

#include "FridaSession.h"
#include "FridaWidget.h"
#include "core/Iaito.h"
#include "core/MainWindow.h"
#include "widgets/ConsoleWidget.h"
#include "widgets/HexdumpWidget.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QLabel>
#include <QMenu>
#include <QPointer>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

namespace {
ConsoleWidget *sessionConsole(MainWindow *main)
{
    return main ? main->findChild<ConsoleWidget *>() : nullptr;
}
} // namespace

void IaitoFridaPlugin::setupPlugin() {}

void IaitoFridaPlugin::terminate()
{
    HexdumpWidget::setCustomizeHook(nullptr);
    if (mainWindow) {
        if (ConsoleWidget *console = sessionConsole(mainWindow)) {
            console->setCommandHandler(nullptr);
        }
        for (HexdumpWidget *dump : mainWindow->findChildren<HexdumpWidget *>()) {
            if (auto *view = dump->findChild<HexWidget *>(QStringLiteral("hexTextView"))) {
                view->setDataReader({});
            }
        }
    }
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
    statusButton->setAutoRaise(false);
    statusButton->setToolTip(tr("r2frida session"));
    statusDetail = new QLabel(main);
    main->statusBar()->addPermanentWidget(statusButton);
    main->statusBar()->addPermanentWidget(statusDetail);
    connect(statusButton, &QToolButton::clicked, this, [this]() {
        widget->show();
        widget->raise();
    });
    connect(session, &FridaSession::changed, this, &IaitoFridaPlugin::updateStatus);

    if (ConsoleWidget *console = sessionConsole(main)) {
        console->setCommandHandler(
            [this](const QString &command) { return handleConsoleCommand(command); });
        connect(session, &FridaSession::consoleMessage, console, &ConsoleWidget::addOutput);
    }
    HexdumpWidget::setCustomizeHook(
        [this](HexdumpWidget *hexdump) { installLiveHexdump(hexdump); });

    QTimer::singleShot(0, this, [this, main]() { buildMenu(main); });
    buildContextMenus(main);
    updateStatus();
}

bool IaitoFridaPlugin::handleConsoleCommand(const QString &command)
{
    const QString line = command.trimmed();
    if (!line.startsWith(QLatin1Char(':'))) {
        return false;
    }
    ConsoleWidget *console = sessionConsole(mainWindow);
    if (!console) {
        return false;
    }
    const QString frida = line.mid(1);
    if (frida.contains(QLatin1Char(';')) || frida.contains(QLatin1Char('|'))
        || frida.contains(QLatin1Char('`'))) {
        console->addOutput(tr("That command is not sent to r2frida."));
        return true;
    }
    if (!session->isAttached()) {
        console->addOutput(tr("r2frida is not attached."));
        return true;
    }
    QPointer<ConsoleWidget> target = console;
    session->command(frida, [target](const QString &output) {
        if (target) {
            target->addOutput(output);
        }
    });
    return true;
}

QByteArray IaitoFridaPlugin::readLiveBytes(uint64_t addr, int len) const
{
    if (!session || !session->isAttached() || len <= 0) {
        return QByteArray(qMax(len, 0), '\0');
    }
    quint64 runtime = RVA_INVALID;
    if (session->containsStatic(addr)) {
        runtime = session->toRuntime(addr);
    } else if (session->containsRuntime(addr)) {
        runtime = addr;
    }
    if (runtime == RVA_INVALID) {
        return QByteArray(len, '\0');
    }
    QByteArray bytes = session->readBytes(runtime, len);
    if (bytes.size() < len) {
        bytes.append(QByteArray(len - bytes.size(), '\0'));
    } else if (bytes.size() > len) {
        bytes.truncate(len);
    }
    return bytes;
}

void IaitoFridaPlugin::installLiveHexdump(HexdumpWidget *hexdump)
{
    if (!hexdump || hexdump->findChild<QCheckBox *>(QStringLiteral("fridaLiveToggle"))) {
        return;
    }
    auto *live = new QCheckBox(tr("Live process"), hexdump);
    live->setObjectName(QStringLiteral("fridaLiveToggle"));
    live->setToolTip(tr("Show bytes from the attached r2frida target"));
    if (QLayout *layout = hexdump->widget() ? hexdump->widget()->layout() : nullptr) {
        if (auto *box = qobject_cast<QVBoxLayout *>(layout)) {
            box->insertWidget(0, live);
        }
    }
    auto *view = hexdump->findChild<HexWidget *>(QStringLiteral("hexTextView"));
    connect(live, &QCheckBox::toggled, this, [this, live, view](bool on) {
        if (!view) {
            return;
        }
        if (!on) {
            view->setDataReader({});
            return;
        }
        if (!session || !session->isAttached()) {
            const QSignalBlocker blocker(live);
            live->setChecked(false);
            if (ConsoleWidget *console = sessionConsole(mainWindow)) {
                console->addOutput(tr("r2frida is not attached."));
            }
            return;
        }
        view->setDataReader([this](uint64_t addr, int len) { return readLiveBytes(addr, len); });
    });
    connect(session, &FridaSession::changed, this, [this, live, view]() {
        if (!session->isAttached()) {
            if (live->isChecked()) {
                live->setChecked(false);
            }
            return;
        }
        if (live->isChecked() && view) {
            view->refresh();
        }
    });
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
    const bool live = session->isAttached();
    const QString color = live ? QStringLiteral("#2E7D32") : QStringLiteral("#C62828");
    statusButton->setText(tr("FRIDA"));
    statusButton->setStyleSheet(QStringLiteral(
        "QToolButton { color: white; background-color: %1; border: none;"
        " border-radius: 3px; padding: 0 6px; font-weight: 600; }")
                                     .arg(color));
    if (!session->checkAvailable()) {
        statusButton->setToolTip(
            tr("r2frida is not loaded. Install it with r2pm -ci r2frida and restart iaito."));
        statusDetail->clear();
    } else if (live) {
        const FridaProcessInfo info = session->process();
        QString arch = info.arch;
        if (!arch.isEmpty()) {
            arch = arch.toUpper();
        }
        statusDetail->setText(
            tr("%1   %2 [%3]   %4   %5")
                .arg(
                    session->deviceLabel(),
                    info.name,
                    QString::number(info.pid),
                    arch,
                    session->transportLabel().toUpper()));
        statusButton->setToolTip(tr("%1 | %2 | slide %3")
                                     .arg(
                                         session->transportLabel(),
                                         session->deviceLabel(),
                                         RAddressString(session->slide())));
    } else if (session->state() == FridaSessionState::Connecting) {
        statusButton->setToolTip(tr("r2frida is connecting"));
        statusDetail->setText(tr("connecting"));
    } else {
        statusButton->setToolTip(tr("r2frida is disconnected"));
        statusDetail->clear();
    }
    if (resumeAction) {
        resumeAction->setEnabled(session->isSuspended());
    }
    if (detachAction) {
        detachAction->setEnabled(session->isAttached());
    }
}
