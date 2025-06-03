#include "widgets/CustomCommandWidget.h"
#include "common/CommandTask.h"
#include "core/MainWindow.h"
#include "ui_CustomCommandWidget.h"
#include <QComboBox>
#include <QFont>
#include <QPlainTextEdit>
#include <QSettings>
#include <QTextCursor>
#include <QTimer>

CustomCommandWidget::CustomCommandWidget(MainWindow *main)
    : IaitoDockWidget(main)
    , ui(new Ui::CustomCommandWidget)
{
    ui->setupUi(this);
    // enforce monospace font for output via style hint
    {
        QFont fixedFont = ui->outputTextEdit->font();
        // fixedFont.setStyleHint(QFont::TypeWriter);
        fixedFont.setStyleHint(QFont::Monospace);
        fixedFont.setFixedPitch(true);
        ui->outputTextEdit->setFont(fixedFont);
    }
    setWindowTitle(tr("Custom Command"));

    connect(ui->runButton, &QPushButton::clicked, this, &CustomCommandWidget::runCommand);
    connect(ui->commandLineEdit, &QLineEdit::returnPressed, this, &CustomCommandWidget::runCommand);
    connect(ui->wrapCheckBox, &QCheckBox::toggled, this, &CustomCommandWidget::setWrap);
    setWrap(ui->wrapCheckBox->isChecked());
    // auto-run timer setup
    autoRunTimer = new QTimer(this);
    connect(autoRunTimer, &QTimer::timeout, this, &CustomCommandWidget::runCommand);
    connect(
        ui->intervalComboBox,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        &CustomCommandWidget::onIntervalChanged);
    autoRunTimer->stop();

    refreshDeferrer = createRefreshDeferrer([this]() {
        if (!ui->commandLineEdit->text().isEmpty()) {
            runCommand();
        }
    });
}

CustomCommandWidget::~CustomCommandWidget() = default;

void CustomCommandWidget::runCommand()
{
    double secs = ui->intervalComboBox->currentText().toDouble();
    if (!commandTask.isNull()) {
        return;
    }
    const QString cmd = ui->commandLineEdit->text().trimmed();
    if (cmd.isEmpty()) {
        return;
    }
    // Clear previous output when running a new command
    ui->outputTextEdit->clear();
    // ui->runButton->setEnabled(false);
    // ui->commandLineEdit->setEnabled(false);

    // ui->outputTextEdit->appendPlainText(QString("> %1").arg(cmd));
    ui->outputTextEdit->moveCursor(QTextCursor::End);
    // ui->outputTextEdit->ensureCursorVisible();
    QString result = Core()->cmdHtml(cmd.toStdString().c_str());
    ui->outputTextEdit->appendHtml(result);
    if (ui->scrollTopCheckBox->isChecked()) {
        ui->outputTextEdit->moveCursor(QTextCursor::Start);
        ui->outputTextEdit->ensureCursorVisible();
    } else {
        ui->outputTextEdit->moveCursor(QTextCursor::End);
        ui->outputTextEdit->ensureCursorVisible();
    }
    // ui->runButton->setEnabled(true);
    // ui->commandLineEdit->setEnabled(true);
    // schedule auto-run based on interval
    if (secs > 0) {
        autoRunTimer->start((int) (secs * 1000));
    } else {
        ui->commandLineEdit->selectAll();
        autoRunTimer->stop();
        ui->commandLineEdit->setFocus();
    }
#if 0
    commandTask = QSharedPointer<CommandTask>(new CommandTask(cmd, CommandTask::MODE_256, true));
    connect(commandTask.data(), &CommandTask::finished, this, &CustomCommandWidget::onCommandFinished);
    Core()->getAsyncTaskManager()->start(commandTask);
#endif
}

void CustomCommandWidget::onCommandFinished(const QString &result)
{
    ui->outputTextEdit->appendHtml(result);
    if (ui->scrollTopCheckBox->isChecked()) {
        ui->outputTextEdit->moveCursor(QTextCursor::Start);
        ui->outputTextEdit->ensureCursorVisible();
    } else {
        ui->outputTextEdit->moveCursor(QTextCursor::End);
        ui->outputTextEdit->ensureCursorVisible();
    }

    ui->runButton->setEnabled(true);
    ui->commandLineEdit->setEnabled(true);
    // Clear the current task so the user can run another command
    commandTask.reset();
}

void CustomCommandWidget::setWrap(bool wrap)
{
    ui->outputTextEdit->setLineWrapMode(wrap ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
}
// handle changes to auto-run interval
void CustomCommandWidget::onIntervalChanged(int index)
{
    int secs = ui->intervalComboBox->itemText(index).toInt();
    if (secs <= 0) {
        autoRunTimer->stop();
    } else if (autoRunTimer->isActive()) {
        autoRunTimer->start(secs * 1000);
    }
}
