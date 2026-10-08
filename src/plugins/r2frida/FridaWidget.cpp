#include "FridaWidget.h"

#include "FridaConnectDialog.h"
#include "FridaPanels.h"
#include "FridaSession.h"
#include "core/Iaito.h"
#include "core/MainWindow.h"
#include "widgets/HexdumpWidget.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

quint64 fridaRuntimeAddress(FridaSession *session, quint64 addr)
{
    if (!session) {
        return addr;
    }
    const quint64 runtime = session->toRuntime(addr);
    return runtime == RVA_INVALID ? addr : runtime;
}

void fridaSeekRuntime(FridaSession *session, quint64 runtimeVa)
{
    if (!session) {
        return;
    }
    const quint64 staticVa = session->toStatic(runtimeVa);
    if (staticVa == RVA_INVALID) {
        Core()->message(QObject::tr("No static mapping for %1").arg(RAddressString(runtimeVa)));
        session->note(session->describe(runtimeVa));
        return;
    }
    Core()->seekAndShow(staticVa);
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
        session->addHook(runtime, QObject::tr("Interceptor.attach"));
        session->command(QStringLiteral(". ") + path, [session](const QString &output) {
            session->note(output.trimmed().isEmpty() ? QObject::tr("hook installed") : output);
        });
        dialog->accept();
    });
    dialog->open();
}

void fridaShowLiveHex(QWidget *parent, FridaSession *session, quint64 runtimeAddr)
{
    if (!session || !session->isAttached() || !parent) {
        return;
    }
    auto *main = qobject_cast<MainWindow *>(parent->window());
    if (!main) {
        return;
    }
    HexdumpWidget *hex = nullptr;
    const auto dumps = main->findChildren<HexdumpWidget *>();
    for (HexdumpWidget *dump : dumps) {
        if (!dump->isHidden()) {
            hex = dump;
            break;
        }
    }
    if (!hex && !dumps.isEmpty()) {
        hex = dumps.first();
    }
    if (!hex) {
        return;
    }
    auto *toggle = hex->findChild<QCheckBox *>(QStringLiteral("fridaLiveToggle"));
    if (toggle && !toggle->isChecked()) {
        toggle->setChecked(true);
    } else if (auto *view = hex->findChild<HexWidget *>(QStringLiteral("hexTextView"))) {
        view->refresh();
    }
    const quint64 staticVa = session->toStatic(runtimeAddr);
    hex->show();
    hex->raise();
    if (IaitoSeekable *seekable = hex->getSeekable()) {
        seekable->seek(staticVa == RVA_INVALID ? runtimeAddr : staticVa);
    }
}

void fridaShowExports(QWidget *parent, FridaSession *session, const QString &moduleName)
{
    const QString command = QStringLiteral("iEj ") + moduleName;
    session->command(command, [parent, session, moduleName](const QString &output) {
        const QJsonDocument doc = FridaBackend::parseJsonLoose(output);
        auto *dialog = new QDialog(parent);
        dialog->setWindowTitle(QObject::tr("Exports %1").arg(moduleName));
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->resize(640, 480);
        auto *view = new QTreeWidget(dialog);
        view->setHeaderLabels(
            {QObject::tr("Address"), QObject::tr("Type"), QObject::tr("Name")});
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
        QObject::connect(
            view, &QTreeWidget::itemActivated, session, [session](QTreeWidgetItem *item, int) {
                fridaSeekRuntime(session, item->data(0, Qt::UserRole).toULongLong());
            });
        dialog->open();
    });
}

FridaWidget::FridaWidget(FridaSession *fridaSession, MainWindow *main)
    : IaitoDockWidget(main)
    , session(fridaSession)
{
    setObjectName(QStringLiteral("FridaWidget"));
    setWindowTitle(tr("r2frida"));

    auto *root = new QWidget(this);
    auto *layout = new QVBoxLayout(root);

    auto *header = new QHBoxLayout();
    statusDot = new QWidget(root);
    statusDot->setFixedSize(10, 10);
    stateLabel = new QLabel(root);
    connectButton = new QPushButton(tr("Connect"), root);
    resumeButton = new QPushButton(tr("Resume"), root);
    detachButton = new QPushButton(tr("Detach"), root);
    detachButton->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: #E5534B; color: white; border: none;"
        " border-radius: 4px; padding: 4px 12px; }"
        "QPushButton:disabled { background-color: palette(mid); color: palette(button-text); }"));
    header->addWidget(statusDot, 0, Qt::AlignVCenter);
    header->addWidget(stateLabel, 1);
    header->addWidget(resumeButton);
    header->addWidget(connectButton);
    header->addWidget(detachButton);
    layout->addLayout(header);

    deviceCombo = new QComboBox(root);
    targetCombo = new QComboBox(root);
    pidLabel = new QLabel(root);
    archLabel = new QLabel(root);
    sessionLabel = new QLabel(root);
    sessionLabel->setWordWrap(true);
    sessionLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *targetRow = new QHBoxLayout();
    targetRow->addWidget(targetCombo, 1);
    targetRow->addWidget(pidLabel);
    auto *targetWrap = new QWidget(root);
    targetWrap->setLayout(targetRow);

    auto *form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->addRow(tr("Device"), deviceCombo);
    form->addRow(tr("Target"), targetWrap);
    form->addRow(tr("Architecture"), archLabel);
    form->addRow(tr("Session"), sessionLabel);
    layout->addLayout(form);

    auto *tabs = new QTabWidget(root);
    for (QWidget *page : createFridaPanels(session)) {
        tabs->addTab(page, page->windowTitle());
    }
    layout->addWidget(tabs, 1);
    setWidget(root);

    connect(session, &FridaSession::changed, this, &FridaWidget::refresh);
    connect(session, &FridaSession::errorMessage, this, [this](const QString &message) {
        QMessageBox::warning(this, tr("r2frida"), message);
    });
    connect(connectButton, &QPushButton::clicked, this, &FridaWidget::showConnectDialog);
    connect(resumeButton, &QPushButton::clicked, session, &FridaSession::resume);
    connect(detachButton, &QPushButton::clicked, session, &FridaSession::detach);
    const auto openConnect = [this](int index) {
        if (index == 1) {
            showConnectDialog();
            deviceCombo->setCurrentIndex(0);
            targetCombo->setCurrentIndex(0);
        }
    };
    connect(deviceCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, openConnect);
    connect(targetCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, openConnect);

    refresh();
    hide();
}

void FridaWidget::showConnectDialog()
{
    auto *dialog = new FridaConnectDialog(session, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();
}

void FridaWidget::fillChoice(QComboBox *combo, const QString &current)
{
    const bool blocked = combo->blockSignals(true);
    combo->clear();
    combo->addItem(current.isEmpty() ? tr("Not connected") : current);
    combo->addItem(tr("Connect..."));
    combo->setCurrentIndex(0);
    combo->blockSignals(blocked);
}

void FridaWidget::setLive(bool live)
{
    const QString color = live ? QStringLiteral("#3DDC84") : QStringLiteral("#E5534B");
    statusDot->setStyleSheet(
        QStringLiteral("background-color: %1; border-radius: 5px;").arg(color));
}

void FridaWidget::refresh()
{
    const bool attached = session->isAttached();
    const bool spawned = attached && session->isSuspended();
    QString stateText = tr("Disconnected");
    if (session->state() == FridaSessionState::Connecting) {
        stateText = tr("Connecting");
    } else if (spawned) {
        stateText = tr("Spawned");
    } else if (attached) {
        stateText = tr("Connected");
    } else if (session->state() == FridaSessionState::Error) {
        stateText = tr("Error");
    }
    stateLabel->setText(stateText);
    setLive(attached);

    const FridaProcessInfo info = session->process();
    QString device = session->deviceLabel();
    if (!session->transportLabel().isEmpty()) {
        device = device.isEmpty()
                     ? session->transportLabel()
                     : session->transportLabel() + QStringLiteral(": ") + device;
    }
    fillChoice(deviceCombo, attached ? device : QString());
    fillChoice(targetCombo, attached ? info.name : QString());
    const QString pid = info.pid >= 0 ? QString::number(info.pid) : QStringLiteral("—");
    pidLabel->setText(tr("PID: %1").arg(pid));
    archLabel->setText(info.arch.isEmpty() ? QStringLiteral("—") : info.arch);
    QString sessionText = info.identifier;
    if (sessionText.isEmpty()) {
        sessionText = session->uri().isEmpty() ? QStringLiteral("—") : session->uri();
    }
    sessionLabel->setText(sessionText);
    sessionLabel->setToolTip(session->uri());

    resumeButton->setEnabled(spawned);
    detachButton->setEnabled(attached || session->state() == FridaSessionState::Connecting);
    if (attached && !wasAttached) {
        show();
        raise();
    }
    wasAttached = attached;
}
