#include "FridaWidget.h"

#include "FridaConnectDialog.h"
#include "FridaSession.h"
#include "core/Iaito.h"
#include "core/MainWindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTextBlock>
#include <QTreeView>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
QString formatSize(quint64 size)
{
    if (size >= (1024ull * 1024ull * 1024ull)) {
        return QString::number(size / (1024.0 * 1024.0 * 1024.0), 'f', 1) + QStringLiteral(" GB");
    }
    if (size >= (1024ull * 1024ull)) {
        return QString::number(size / (1024.0 * 1024.0), 'f', 1) + QStringLiteral(" MB");
    }
    if (size >= 1024ull) {
        return QString::number(size / 1024.0, 'f', 1) + QStringLiteral(" KB");
    }
    return QString::number(size) + QStringLiteral(" B");
}

quint64 roleAddress(const QModelIndex &index)
{
    return index.data(Qt::UserRole).toULongLong();
}

QStandardItem *addressItem(const QString &text, quint64 runtime)
{
    auto *item = new QStandardItem(text);
    item->setData(QVariant::fromValue(runtime), Qt::UserRole);
    item->setEditable(false);
    return item;
}

QStandardItem *textItem(const QString &text)
{
    auto *item = new QStandardItem(text);
    item->setEditable(false);
    return item;
}
}

quint64 fridaRuntimeAddress(FridaSession *session, quint64 addr)
{
    if (!session) {
        return addr;
    }
    const quint64 runtime = session->toRuntime(addr);
    return runtime == RVA_INVALID ? addr : runtime;
}

void fridaTrace(FridaSession *session, quint64 addr)
{
    if (!session || !session->isAttached()) {
        return;
    }
    const quint64 runtime = fridaRuntimeAddress(session, addr);
    const QString command = QStringLiteral("dt ") + RAddressString(runtime);
    session->command(command, [session](const QString &output) {
        session->note(output.trimmed().isEmpty() ? QObject::tr("trace installed") : output);
    });
}

void fridaBreakpoint(FridaSession *session, quint64 addr)
{
    if (!session || !session->isAttached()) {
        return;
    }
    const quint64 runtime = fridaRuntimeAddress(session, addr);
    const QString command = QStringLiteral("db ") + RAddressString(runtime);
    session->command(command, [session](const QString &output) {
        session->note(output.trimmed().isEmpty() ? QObject::tr("breakpoint set") : output);
    });
}

void fridaShowHookDialog(QWidget *parent, FridaSession *session, quint64 addr)
{
    if (!session || !session->isAttached()) {
        return;
    }
    const quint64 runtime = fridaRuntimeAddress(session, addr);
    auto *dialog = new QDialog(parent);
    dialog->setWindowTitle(QObject::tr("Hook %1").arg(RAddressString(runtime)));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    auto *layout = new QVBoxLayout(dialog);
    layout->addWidget(new QLabel(session->describe(addr), dialog));
    auto *logArgs = new QCheckBox(QObject::tr("Log arguments"), dialog);
    auto *logRet = new QCheckBox(QObject::tr("Log return value"), dialog);
    auto *backtrace = new QCheckBox(QObject::tr("Backtrace"), dialog);
    logArgs->setChecked(true);
    logRet->setChecked(true);
    layout->addWidget(logArgs);
    layout->addWidget(logRet);
    layout->addWidget(backtrace);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("Install Hook"));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, [=]() {
        QString body = QStringLiteral("Interceptor.attach(ptr('%1'), {\n")
                           .arg(RAddressString(runtime));
        body += QStringLiteral("  onEnter(args) {\n");
        if (logArgs->isChecked()) {
            body += QStringLiteral(
                "    console.log('hook', this.context.pc, args[0], args[1], args[2], args[3]);\n");
        }
        if (backtrace->isChecked()) {
            body += QStringLiteral(
                "    console.log(Thread.backtrace(this.context, Backtracer.ACCURATE)"
                ".map(DebugSymbol.fromAddress).join('\\n'));\n");
        }
        body += QStringLiteral("  },\n  onLeave(retval) {\n");
        if (logRet->isChecked()) {
            body += QStringLiteral("    console.log('return', retval);\n");
        }
        body += QStringLiteral("  }\n});\n");
        const QString path = session->writeScript(body);
        if (path.isEmpty()) {
            return;
        }
        session->command(QStringLiteral(". ") + path, [session](const QString &output) {
            session->note(output.trimmed().isEmpty() ? QObject::tr("hook installed") : output);
        });
        dialog->accept();
    });
    dialog->open();
}

void fridaShowLiveHex(QWidget *parent, FridaSession *session, quint64 runtimeAddr)
{
    if (!session || !session->isAttached()) {
        return;
    }
    auto *dialog = new QDialog(parent);
    dialog->setWindowTitle(QObject::tr("Live %1").arg(RAddressString(runtimeAddr)));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->resize(720, 420);
    auto *view = new QPlainTextEdit(dialog);
    view->setReadOnly(true);
    view->setPlainText(QObject::tr("Reading live memory..."));
    auto *layout = new QVBoxLayout(dialog);
    layout->addWidget(view);
    const QString source
        = QStringLiteral("console.log(hexdump(ptr('%1'), {length: 256, ansi: false}));\n")
              .arg(RAddressString(runtimeAddr));
    const QString path = session->writeScript(source);
    session->command(QStringLiteral(". ") + path, [view](const QString &output) {
        view->setPlainText(output);
    });
    dialog->open();
}

FridaWidget::FridaWidget(FridaSession *fridaSession, MainWindow *main)
    : IaitoDockWidget(main)
    , session(fridaSession)
{
    setObjectName(QStringLiteral("FridaWidget"));
    setWindowTitle(tr("r2frida"));

    auto *root = new QWidget(this);
    auto *layout = new QVBoxLayout(root);
    stateLabel = new QLabel(root);
    deviceLabel = new QLabel(root);
    targetLabel = new QLabel(root);
    slideLabel = new QLabel(root);
    layout->addWidget(stateLabel);
    layout->addWidget(deviceLabel);
    layout->addWidget(targetLabel);
    layout->addWidget(slideLabel);

    auto *buttons = new QHBoxLayout();
    auto *connectButton = new QPushButton(tr("Connect"), root);
    resumeButton = new QPushButton(tr("Resume"), root);
    detachButton = new QPushButton(tr("Detach"), root);
    buttons->addWidget(connectButton);
    buttons->addWidget(resumeButton);
    buttons->addWidget(detachButton);
    buttons->addStretch();
    layout->addLayout(buttons);

    addressMode = new QComboBox(root);
    addressMode->addItem(tr("Runtime"), int(FridaAddressMode::Runtime));
    addressMode->addItem(tr("Static"), int(FridaAddressMode::Static));
    addressMode->addItem(tr("Module + offset"), int(FridaAddressMode::ModuleOffset));
    layout->addWidget(addressMode);

    filter = new QLineEdit(root);
    filter->setPlaceholderText(tr("Filter modules and maps"));
    layout->addWidget(filter);

    moduleModel = new QStandardItemModel(this);
    mapModel = new QStandardItemModel(this);
    moduleModel->setHorizontalHeaderLabels({tr("Name"), tr("Address"), tr("Size"), tr("Path")});
    mapModel->setHorizontalHeaderLabels({tr("Start"), tr("End"), tr("Perm"), tr("Path")});
    moduleProxy = new QSortFilterProxyModel(this);
    mapProxy = new QSortFilterProxyModel(this);
    moduleProxy->setSourceModel(moduleModel);
    mapProxy->setSourceModel(mapModel);
    moduleProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    mapProxy->setFilterKeyColumn(-1);
    mapProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    mapProxy->setFilterKeyColumn(-1);

    modules = new QTreeView(root);
    maps = new QTreeView(root);
    modules->setModel(moduleProxy);
    maps->setModel(mapProxy);
    modules->setRootIsDecorated(false);
    maps->setRootIsDecorated(false);
    modules->setAlternatingRowColors(true);
    maps->setAlternatingRowColors(true);
    modules->setContextMenuPolicy(Qt::CustomContextMenu);
    maps->setContextMenuPolicy(Qt::CustomContextMenu);
    modules->header()->setStretchLastSection(true);
    maps->header()->setStretchLastSection(true);

    threads = new QPlainTextEdit(root);
    traces = new QPlainTextEdit(root);
    runtime = new QPlainTextEdit(root);
    editor = new QPlainTextEdit(root);
    console = new QPlainTextEdit(root);
    threads->setReadOnly(true);
    traces->setReadOnly(true);
    runtime->setReadOnly(true);
    console->setReadOnly(true);
    editor->setPlaceholderText(tr("Frida JavaScript or TypeScript"));

    auto *threadsPage = new QWidget(root);
    auto *threadsLayout = new QVBoxLayout(threadsPage);
    auto *threadsRefresh = new QPushButton(tr("Refresh threads"), threadsPage);
    threadsLayout->addWidget(threadsRefresh);
    threadsLayout->addWidget(threads);

    auto *tracesPage = new QWidget(root);
    auto *tracesLayout = new QVBoxLayout(tracesPage);
    auto *tracesRefresh = new QPushButton(tr("Refresh traces"), tracesPage);
    auto *tracesSeek = new QPushButton(tr("Seek address on line"), tracesPage);
    auto *tracesButtons = new QHBoxLayout();
    tracesButtons->addWidget(tracesRefresh);
    tracesButtons->addWidget(tracesSeek);
    tracesButtons->addStretch();
    tracesLayout->addLayout(tracesButtons);
    tracesLayout->addWidget(traces);

    auto *runtimePage = new QWidget(root);
    auto *runtimeLayout = new QVBoxLayout(runtimePage);
    auto *runtimeRefresh = new QPushButton(tr("Load classes"), runtimePage);
    runtimeLayout->addWidget(runtimeRefresh);
    runtimeLayout->addWidget(runtime);

    auto *scriptsPage = new QWidget(root);
    auto *scriptsLayout = new QVBoxLayout(scriptsPage);
    auto *scriptButtons = new QHBoxLayout();
    auto *openScript = new QPushButton(tr("Open"), scriptsPage);
    auto *runScript = new QPushButton(tr("Run"), scriptsPage);
    auto *eternalizeScript = new QPushButton(tr("Eternalize"), scriptsPage);
    scriptButtons->addWidget(openScript);
    scriptButtons->addWidget(runScript);
    scriptButtons->addWidget(eternalizeScript);
    scriptButtons->addStretch();
    scriptsLayout->addLayout(scriptButtons);
    scriptsLayout->addWidget(editor);

    auto *consolePage = new QWidget(root);
    auto *consoleLayout = new QVBoxLayout(consolePage);
    consoleInput = new QLineEdit(consolePage);
    consoleInput->setPlaceholderText(tr(":dm, :i, :dt foo, :. script.js"));
    consoleLayout->addWidget(console);
    consoleLayout->addWidget(consoleInput);

    tabs = new QTabWidget(root);
    tabs->addTab(modules, tr("Modules"));
    tabs->addTab(maps, tr("Memory"));
    tabs->addTab(threadsPage, tr("Threads"));
    tabs->addTab(tracesPage, tr("Traces"));
    tabs->addTab(runtimePage, tr("Runtime"));
    tabs->addTab(scriptsPage, tr("Scripts"));
    tabs->addTab(consolePage, tr("Console"));
    layout->addWidget(tabs, 1);
    setWidget(root);

    connect(session, &FridaSession::changed, this, &FridaWidget::refresh);
    connect(session, &FridaSession::consoleMessage, this, &FridaWidget::appendConsole);
    connect(session, &FridaSession::errorMessage, this, [this](const QString &message) {
        appendConsole(message);
        QMessageBox::warning(this, tr("r2frida"), message);
    });
    connect(connectButton, &QPushButton::clicked, this, &FridaWidget::showConnectDialog);
    connect(resumeButton, &QPushButton::clicked, session, &FridaSession::resume);
    connect(detachButton, &QPushButton::clicked, session, &FridaSession::detach);
    connect(addressMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        const auto mode = static_cast<FridaAddressMode>(addressMode->itemData(index).toInt());
        session->setAddressMode(mode);
    });
    connect(filter, &QLineEdit::textChanged, this, [this](const QString &text) {
        moduleProxy->setFilterFixedString(text);
        mapProxy->setFilterFixedString(text);
    });
    connect(modules, &QTreeView::activated, this, [this](const QModelIndex &index) {
        seekRuntime(roleAddress(index.sibling(index.row(), 0)));
    });
    connect(maps, &QTreeView::activated, this, [this](const QModelIndex &index) {
        seekRuntime(roleAddress(index.sibling(index.row(), 0)));
    });
    connect(modules, &QTreeView::customContextMenuRequested, this, [this](const QPoint &pos) {
        const QModelIndex index = modules->indexAt(pos);
        if (!index.isValid()) {
            return;
        }
        const QModelIndex nameIndex = index.sibling(index.row(), 0);
        const quint64 runtime = roleAddress(nameIndex);
        const QString name = nameIndex.data().toString();
        QMenu menu(this);
        menu.addAction(tr("Seek static address"), this, [this, runtime]() {
            seekRuntime(runtime);
        });
        menu.addAction(tr("Copy runtime address"), this, [runtime]() {
            QApplication::clipboard()->setText(RAddressString(runtime));
        });
        menu.addAction(tr("Live hexdump"), this, [this, runtime]() {
            fridaShowLiveHex(this, session, runtime);
        });
        menu.addAction(tr("Show exports"), this, [this, name]() { showExports(name); });
        menu.addAction(tr("Import symbols into analysis"), this, [this, name]() {
            session->importExports(name);
        });
        menu.exec(modules->viewport()->mapToGlobal(pos));
    });
    connect(maps, &QTreeView::customContextMenuRequested, this, [this](const QPoint &pos) {
        const QModelIndex index = maps->indexAt(pos);
        if (!index.isValid()) {
            return;
        }
        const quint64 runtime = roleAddress(index.sibling(index.row(), 0));
        QMenu menu(this);
        menu.addAction(tr("Seek static address"), this, [this, runtime]() {
            seekRuntime(runtime);
        });
        menu.addAction(tr("Live hexdump"), this, [this, runtime]() {
            fridaShowLiveHex(this, session, runtime);
        });
        menu.addAction(tr("Copy address"), this, [runtime]() {
            QApplication::clipboard()->setText(RAddressString(runtime));
        });
        menu.exec(maps->viewport()->mapToGlobal(pos));
    });
    connect(threadsRefresh, &QPushButton::clicked, this, [this]() {
        loadText(QStringLiteral("dpt"), threads);
    });
    connect(tracesRefresh, &QPushButton::clicked, this, [this]() {
        loadText(QStringLiteral("dt\n:dtl"), traces);
    });
    connect(tracesSeek, &QPushButton::clicked, this, [this]() {
        const QString line = traces->textCursor().block().text();
        static const QRegularExpression addressRe(QStringLiteral("0x[0-9a-fA-F]+"));
        const QRegularExpressionMatch match = addressRe.match(line);
        if (!match.hasMatch()) {
            appendConsole(tr("No address on this trace line."));
            return;
        }
        bool ok = false;
        const quint64 addr = match.captured(0).toULongLong(&ok, 16);
        if (ok) {
            seekRuntime(addr);
        }
    });
    connect(runtimeRefresh, &QPushButton::clicked, this, [this]() {
        loadText(QStringLiteral("icl"), runtime);
    });
    connect(openScript, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Frida script"), QString(), tr("Scripts (*.js *.ts)"));
        if (path.isEmpty()) {
            return;
        }
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            editor->setPlainText(QString::fromUtf8(file.readAll()));
        }
    });
    connect(runScript, &QPushButton::clicked, this, [this]() { runEditor(false); });
    connect(eternalizeScript, &QPushButton::clicked, this, [this]() { runEditor(true); });
    connect(consoleInput, &QLineEdit::returnPressed, this, &FridaWidget::runConsole);
    connect(traces, &QPlainTextEdit::cursorPositionChanged, this, [this]() {
        const QString line = traces->textCursor().block().text();
        static const QRegularExpression addressRe(QStringLiteral("0x[0-9a-fA-F]+"));
        const QRegularExpressionMatch match = addressRe.match(line);
        if (!match.hasMatch()) {
            return;
        }
        traces->setToolTip(session->describe(match.captured(0).toULongLong(nullptr, 16)));
    });

    refresh();
    hide();
}

void FridaWidget::showConnectDialog()
{
    auto *dialog = new FridaConnectDialog(session, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();
}

void FridaWidget::appendConsole(const QString &text)
{
    console->appendPlainText(text);
}

void FridaWidget::refresh()
{
    QString stateText = tr("Disconnected");
    if (session->state() == FridaSessionState::Connecting) {
        stateText = tr("Connecting");
    } else if (session->state() == FridaSessionState::Error) {
        stateText = tr("Error");
    } else if (session->isSuspended()) {
        stateText = tr("Spawned, suspended");
    } else if (session->isAttached()) {
        stateText = tr("Attached");
    }
    stateLabel->setText(stateText);
    deviceLabel->setText(
        session->deviceLabel().isEmpty()
            ? session->transportLabel()
            : session->transportLabel() + QStringLiteral(": ") + session->deviceLabel());
    const FridaProcessInfo info = session->process();
    if (session->isAttached()) {
        targetLabel->setText(
            tr("%1  pid %2  %3 %4-bit")
                .arg(info.name)
                .arg(info.pid)
                .arg(info.arch)
                .arg(info.bits));
        slideLabel->setText(
            session->hasStaticMapping()
                ? tr("slide %1").arg(RAddressString(session->slide()))
                : tr("No module matches the open file"));
    } else {
        targetLabel->clear();
        slideLabel->setText(session->lastError());
    }
    resumeButton->setEnabled(session->isSuspended());
    detachButton->setEnabled(
        session->isAttached() || session->state() == FridaSessionState::Connecting);
    refreshTables();
    const bool attached = session->isAttached();
    if (attached && !wasAttached) {
        show();
        raise();
    }
    wasAttached = attached;
}

void FridaWidget::refreshTables()
{
    moduleModel->removeRows(0, moduleModel->rowCount());
    mapModel->removeRows(0, mapModel->rowCount());
    for (const FridaModuleInfo &module : session->modules()) {
        QList<QStandardItem *> row;
        auto *name = textItem(module.name);
        name->setData(QVariant::fromValue(module.base), Qt::UserRole);
        name->setToolTip(session->describe(module.base));
        row << name << addressItem(session->formatRuntime(module.base), module.base)
            << textItem(formatSize(module.size)) << textItem(module.path);
        moduleModel->appendRow(row);
    }
    for (const FridaMapInfo &map : session->maps()) {
        QList<QStandardItem *> row;
        auto *start = addressItem(session->formatRuntime(map.start), map.start);
        start->setToolTip(session->describe(map.start));
        row << start << addressItem(session->formatRuntime(map.end), map.end)
            << textItem(map.permission) << textItem(map.path);
        mapModel->appendRow(row);
    }
}

void FridaWidget::seekRuntime(quint64 runtimeVa)
{
    const quint64 staticVa = session->toStatic(runtimeVa);
    if (staticVa == RVA_INVALID) {
        Core()->message(tr("No static mapping for %1").arg(RAddressString(runtimeVa)));
        appendConsole(session->describe(runtimeVa));
        return;
    }
    Core()->seekAndShow(staticVa);
}

void FridaWidget::loadText(const QString &command, QPlainTextEdit *view)
{
    if (!session->isAttached()) {
        view->setPlainText(tr("Not attached."));
        return;
    }
    view->setPlainText(tr("Loading..."));
    session->command(command, [view](const QString &output) {
        QString text = output;
        if (text.size() > 1000000) {
            text.truncate(1000000);
            text += QObject::tr("\n... truncated");
        }
        view->setPlainText(text);
    });
}

void FridaWidget::runEditor(bool eternalize)
{
    const QString path = session->writeScript(editor->toPlainText());
    if (path.isEmpty()) {
        return;
    }
    const QString command = (eternalize ? QStringLiteral(".. ") : QStringLiteral(". ")) + path;
    session->command(command, [this](const QString &output) {
        appendConsole(output.trimmed().isEmpty() ? tr("script finished") : output);
        tabs->setCurrentIndex(tabs->count() - 1);
    });
}

void FridaWidget::runConsole()
{
    QString line = consoleInput->text().trimmed();
    consoleInput->clear();
    if (line.startsWith(QLatin1Char(':'))) {
        line = line.mid(1);
    }
    if (line.isEmpty()) {
        return;
    }
    if (line.contains(QLatin1Char(';')) || line.contains(QLatin1Char('|'))
        || line.contains(QLatin1Char('`'))) {
        appendConsole(tr("Refusing ; | ` in the r2frida console."));
        return;
    }
    appendConsole(QStringLiteral("> :") + line);
    session->command(line, [this](const QString &output) { appendConsole(output); });
}

void FridaWidget::showExports(const QString &moduleName)
{
    const QString command = QStringLiteral("iEj ") + moduleName;
    session->command(command, [this, moduleName](const QString &output) {
        const QJsonDocument doc = FridaBackend::parseJsonLoose(output);
        auto *dialog = new QDialog(this);
        dialog->setWindowTitle(tr("Exports %1").arg(moduleName));
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->resize(640, 480);
        auto *view = new QTreeWidget(dialog);
        view->setHeaderLabels({tr("Address"), tr("Type"), tr("Name")});
        view->setRootIsDecorated(false);
        for (const QJsonValue value : doc.array()) {
            const QJsonObject exp = value.toObject();
            const quint64 runtime
                = FridaBackend::parseAddress(exp.value(QStringLiteral("address")));
            auto *item = new QTreeWidgetItem(view);
            item->setText(0, session->formatRuntime(runtime));
            item->setText(1, exp.value(QStringLiteral("type")).toString());
            item->setText(2, exp.value(QStringLiteral("name")).toString());
            item->setData(0, Qt::UserRole, QVariant::fromValue(runtime));
        }
        auto *layout = new QVBoxLayout(dialog);
        layout->addWidget(view);
        connect(view, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item, int) {
            seekRuntime(item->data(0, Qt::UserRole).toULongLong());
        });
        dialog->open();
    });
}
