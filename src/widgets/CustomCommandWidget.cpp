#include "widgets/CustomCommandWidget.h"
#include "common/CommandTask.h"
#include "core/MainWindow.h"
#include "ui_CustomCommandWidget.h"
#include <QPlainTextEdit>
#include <QSettings>
#include <QTextCursor>

CustomCommandWidget::CustomCommandWidget(MainWindow *main)
    : IaitoDockWidget(main)
    , ui(new Ui::CustomCommandWidget)
{
    ui->setupUi(this);
    setWindowTitle(tr("Custom Command"));

    connect(ui->runButton, &QPushButton::clicked, this, &CustomCommandWidget::runCommand);
    connect(ui->commandLineEdit, &QLineEdit::returnPressed, this, &CustomCommandWidget::runCommand);
    connect(ui->wrapCheckBox, &QCheckBox::toggled, this, &CustomCommandWidget::setWrap);
    setWrap(ui->wrapCheckBox->isChecked());

    refreshDeferrer = createRefreshDeferrer([this]() {
        if (!ui->commandLineEdit->text().isEmpty()) {
            runCommand();
        }
    });
}

CustomCommandWidget::~CustomCommandWidget() = default;

void CustomCommandWidget::runCommand()
{
    if (!commandTask.isNull()) {
        return;
    }
    const QString cmd = ui->commandLineEdit->text().trimmed();
    if (cmd.isEmpty()) {
        return;
    }
    // Clear previous output when running a new command
    ui->outputTextEdit->clear();
    ui->runButton->setEnabled(false);
    ui->commandLineEdit->setEnabled(false);

    // ui->outputTextEdit->appendPlainText(QString("> %1").arg(cmd));
    ui->outputTextEdit->moveCursor(QTextCursor::End);
    // ui->outputTextEdit->ensureCursorVisible();
#if 1
    QString result = Core()->cmdHtml(cmd.toStdString().c_str());
    ui->outputTextEdit->appendHtml(result);
    ui->runButton->setEnabled(true);
    ui->commandLineEdit->setEnabled(true);
    ui->commandLineEdit->setFocus();
    ui->commandLineEdit->selectAll();
#else
    commandTask = QSharedPointer<CommandTask>(new CommandTask(cmd, CommandTask::MODE_256, true));
    connect(commandTask.data(), &CommandTask::finished, this, &CustomCommandWidget::onCommandFinished);
    Core()->getAsyncTaskManager()->start(commandTask);
#endif
}

void CustomCommandWidget::onCommandFinished(const QString &result)
{
    ui->outputTextEdit->appendHtml(result);
    ui->outputTextEdit->moveCursor(QTextCursor::End);
    ui->outputTextEdit->ensureCursorVisible();

    ui->runButton->setEnabled(true);
    ui->commandLineEdit->setEnabled(true);
    // Clear the current task so the user can run another command
    commandTask.reset();
}

void CustomCommandWidget::setWrap(bool wrap)
{
    ui->outputTextEdit->setLineWrapMode(wrap ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
}
