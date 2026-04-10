
#include "AsyncTaskDialog.h"
#include "common/AsyncTask.h"
#include "ui_AsyncTaskDialog.h"

#include <QFont>
#include <QPushButton>
#include <QScrollBar>

AsyncTaskDialog::AsyncTaskDialog(AsyncTask::Ptr task, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::AsyncTaskDialog)
    , task(task)
{
    ui->setupUi(this);

    QString title = task->getTitle();
    if (!title.isNull()) {
        setWindowTitle(title);
    }

    // Style the log area: monospaced font, dark terminal look
    QFont mono("Courier");
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(10);
    ui->logTextEdit->setFont(mono);
    ui->logTextEdit->setStyleSheet("QPlainTextEdit { background-color: #1a1a1a; color: #cccccc; }");

    // Replace the default Cancel button with an Interrupt button
    ui->buttonBox->clear();
    QPushButton *interruptBtn
        = ui->buttonBox->addButton(tr("Interrupt"), QDialogButtonBox::RejectRole);
    connect(interruptBtn, &QPushButton::clicked, this, &AsyncTaskDialog::reject);

    // Repeatedly re-send interrupt until the task stops, because r2 analysis
    // phases may clear the break flag via r_cons_break_push/pop.
    interruptTimer.setInterval(100);
    interruptTimer.setSingleShot(false);
    connect(&interruptTimer, &QTimer::timeout, this, [this]() {
        if (this->task->isRunning()) {
            this->task->interrupt();
        } else {
            interruptTimer.stop();
        }
    });

    // Update log when the task reports text output
    connect(task.data(), &AsyncTask::logChanged, this, &AsyncTaskDialog::updateLog);
    // Close dialog when the task signals completion
    connect(task.data(), &AsyncTask::finished, this, [this]() {
        interruptTimer.stop();
        close();
    });

    updateLog(task->getLog());

    connect(&timer, &QTimer::timeout, this, &AsyncTaskDialog::updateProgressTimer);
    timer.setInterval(1000);
    timer.setSingleShot(false);
    timer.start();

    updateProgressTimer();
}

AsyncTaskDialog::~AsyncTaskDialog() {}

void AsyncTaskDialog::updateLog(const QString &log)
{
    ui->logTextEdit->setPlainText(log);
    // Auto-scroll to bottom
    QScrollBar *sb = ui->logTextEdit->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void AsyncTaskDialog::updateProgressTimer()
{
    int secondsElapsed = (task->getElapsedTime() + 500) / 1000;
    int minutesElapsed = secondsElapsed / 60;
    int hoursElapsed = minutesElapsed / 60;

    QString label = tr("Running for") + " ";
    if (hoursElapsed) {
        label += tr("%n hour", "%n hours", hoursElapsed);
        label += " ";
    }
    if (minutesElapsed) {
        label += tr("%n minute", "%n minutes", minutesElapsed % 60);
        label += " ";
    }
    label += tr("%n second", "%n seconds", secondsElapsed % 60);
    ui->timeLabel->setText(label);
}

void AsyncTaskDialog::closeEvent(QCloseEvent *event)
{
    interruptTimer.stop();
    if (interruptOnClose) {
        // Keep re-sending interrupt while waiting, in case r2 clears the
        // break flag between analysis phases.
        while (!task->wait(100)) {
            task->interrupt();
        }
    }

    QWidget::closeEvent(event);
}

void AsyncTaskDialog::reject()
{
    task->interrupt();
    if (!interruptTimer.isActive()) {
        interruptTimer.start();
    }
}
