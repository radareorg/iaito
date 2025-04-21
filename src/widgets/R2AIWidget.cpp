#include "R2AIWidget.h"
#include "core/Iaito.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QVBoxLayout>

R2AIWidget::R2AIWidget(MainWindow *main)
    : IaitoDockWidget(main)
{
    setObjectName("r2aiWidget");
    setWindowTitle(tr("r2ai"));
    setupUI();
    // Disable controls if the r2ai plugin/command is not available
    updateAvailability();
}

R2AIWidget::~R2AIWidget() {}
// Check if the r2ai plugin/command is available and disable UI if not
void R2AIWidget::updateAvailability()
{
    // Run 'Lc~^r2ai' to see if any commands match r2ai
    QString result = Core()->cmd("Lc~^r2ai").trimmed();
    bool available = !result.isEmpty();
    // Disable input controls if not available
    inputLineEdit->setEnabled(available);
    sendButton->setEnabled(available);
    settingsButton->setEnabled(available);
    // Disable the entry in the Plugins menu
    if (QAction *act = toggleViewAction()) {
        act->setEnabled(available);
    }
}

void R2AIWidget::setupUI()
{
    QWidget *container = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(container);

    outputTextEdit = new QPlainTextEdit(container);
    outputTextEdit->setReadOnly(true);
    layout->addWidget(outputTextEdit);

    QHBoxLayout *hLayout = new QHBoxLayout();
    inputLineEdit = new QLineEdit(container);
    sendButton = new QPushButton(tr("Send"), container);
    settingsButton = new QPushButton(tr("Settings"), container);
    hLayout->addWidget(inputLineEdit);
    hLayout->addWidget(sendButton);
    hLayout->addWidget(settingsButton);
    layout->addLayout(hLayout);

    setWidget(container);

    connect(sendButton, &QPushButton::clicked, this, &R2AIWidget::onSendClicked);
    connect(inputLineEdit, &QLineEdit::returnPressed, this, &R2AIWidget::onSendClicked);
    connect(settingsButton, &QPushButton::clicked, this, &R2AIWidget::onSettingsClicked);

    QAction *toggleAct = toggleViewAction();
    toggleAct->setText(tr("r2ai"));
}

void R2AIWidget::onSendClicked()
{
    QString input = inputLineEdit->text().trimmed();
    if (input.isEmpty()) {
        return;
    }
    QString cmd = QString("r2ai %1").arg(input);
    outputTextEdit->appendPlainText(QString("> %1").arg(cmd));
    inputLineEdit->clear();
    executeCommand(cmd);
}

void R2AIWidget::executeCommand(const QString &command)
{
    if (!commandTask.isNull()) {
        return;
    }
    commandTask = QSharedPointer<CommandTask>(new CommandTask(command, CommandTask::MODE_256, true));
    connect(commandTask.data(), &CommandTask::finished, this, &R2AIWidget::onCommandFinished);
    Core()->getAsyncTaskManager()->start(commandTask);
}

void R2AIWidget::onCommandFinished(const QString &result)
{
    outputTextEdit->appendHtml(result);
    commandTask.clear();
}

void R2AIWidget::onSettingsClicked()
{
    QDialog *dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(tr("r2ai Settings"));
    QVBoxLayout *dlgLayout = new QVBoxLayout(dlg);

    // Fetch current settings
    QString apiVal = Core()->cmd("r2ai -e api").trimmed();
    QString modelVal = Core()->cmd("r2ai -e model").trimmed();
    QString systemVal = Core()->cmd("r2ai -e system").trimmed();
    QString promptVal = Core()->cmd("r2ai -e prompt").trimmed();

    // Provider combobox
    QComboBox *providerCombo = new QComboBox(dlg);
    providerCombo->addItems(Core()->cmd("r2ai -e api=?").split('\n', Qt::SkipEmptyParts));
    if (!apiVal.isEmpty()) {
        int i = providerCombo->findText(apiVal);
        if (i >= 0) {
            providerCombo->setCurrentIndex(i);
        }
    }
    // providerCombo will be added to left panel layout later

    // Model combobox (regenerated when provider changes)
    QComboBox *modelCombo = new QComboBox(dlg);
    auto loadModels = [&]() {
        modelCombo->clear();
        Core()->cmd(QString("r2ai -e api=%1").arg(providerCombo->currentText()));
        modelCombo->addItems(Core()->cmd("r2ai -e model=?").split('\n', Qt::SkipEmptyParts));
    };
    loadModels();
    if (!modelVal.isEmpty()) {
        int j = modelCombo->findText(modelVal);
        if (j >= 0)
            modelCombo->setCurrentIndex(j);
    }
    // modelCombo will be added to left panel layout later
    connect(providerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [=](int) {
        loadModels();
    });

    // System prompt (multiline)
    QPlainTextEdit *sysEdit = new QPlainTextEdit(dlg);
    sysEdit->setPlainText(systemVal);
    // sysEdit will be added to left panel layout later

    // User prompt (multiline)
    QPlainTextEdit *promptEdit = new QPlainTextEdit(dlg);
    promptEdit->setPlainText(promptVal);
    // promptEdit will be added to left panel layout later

    // Other options table (booleans as checkboxes)
    QTableWidget *table = new QTableWidget(dlg);
    // Hide the vertical header (row numbers)
    table->verticalHeader()->setVisible(false);
    table->setColumnCount(2);
    table->setHorizontalHeaderLabels({tr("Key"), tr("Value")});
    // Resize columns to fit their content
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

    QStringList lines = Core()->cmd("r2ai -e").split('\n', Qt::SkipEmptyParts);
    int row = 0;
    for (const QString &ln : lines) {
        QStringList p = ln.split('=', Qt::KeepEmptyParts);
        if (p.size() != 2) {
            continue;
        }
        const QString &v = p[1].trimmed();
        const QString k = p[0].replace("r2ai.", "").trimmed();
        if (k == "api" || k == "model" || k == "system" || k == "prompt") {
            continue;
        }
        table->insertRow(row);
        auto *ki = new QTableWidgetItem(k);
        ki->setFlags(ki->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, 0, ki);

        if (v == "true" || v == "false") {
            auto *cb = new QCheckBox(dlg);
            cb->setChecked(v == "true");
            table->setCellWidget(row, 1, cb);
        } else {
            table->setItem(row, 1, new QTableWidgetItem(v));
        }
        row++;
    }
    // table will be added to right panel layout later

    // Layout panels: left panel for basic settings, right panel for other options
    QHBoxLayout *panelsLayout = new QHBoxLayout();
    QWidget *leftPanel = new QWidget(dlg);
    QVBoxLayout *leftLayout = new QVBoxLayout(leftPanel);
    QWidget *rightPanel = new QWidget(dlg);
    QVBoxLayout *rightLayout = new QVBoxLayout(rightPanel);

    // Left panel: provider, model, system and user prompts
    leftLayout->addWidget(new QLabel(tr("Provider:"), dlg));
    leftLayout->addWidget(providerCombo);
    leftLayout->addWidget(new QLabel(tr("Model:"), dlg));
    leftLayout->addWidget(modelCombo);
    // API Key button
    QPushButton *apiKeyBtn = new QPushButton(tr("API Key"), dlg);
    leftLayout->addWidget(apiKeyBtn);
    connect(apiKeyBtn, &QPushButton::clicked, this, [=]() {
        QString provider = providerCombo->currentText();
        QString filePath = QDir::home().filePath(QString(".r2ai.%1-key").arg(provider));
        QString key;
        QFile keyFile(filePath);
        if (keyFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&keyFile);
            key = in.readLine();
            keyFile.close();
        }
        QDialog keyDlg(dlg);
        keyDlg.setWindowTitle(provider);
        QVBoxLayout *keyDlgLayout = new QVBoxLayout(&keyDlg);
        QLineEdit *keyEdit = new QLineEdit(&keyDlg);
        keyEdit->setText(key);
        keyDlgLayout->addWidget(keyEdit);
        QHBoxLayout *keyBtnLayout = new QHBoxLayout();
        QPushButton *cancelKeyBtn = new QPushButton(tr("Cancel"), &keyDlg);
        QPushButton *saveKeyBtn = new QPushButton(tr("Save"), &keyDlg);
        keyBtnLayout->addWidget(cancelKeyBtn);
        keyBtnLayout->addWidget(saveKeyBtn);
        keyDlgLayout->addLayout(keyBtnLayout);
        connect(cancelKeyBtn, &QPushButton::clicked, &keyDlg, &QDialog::reject);
        connect(saveKeyBtn, &QPushButton::clicked, &keyDlg, &QDialog::accept);
        if (keyDlg.exec() == QDialog::Accepted) {
            QFile outFile(filePath);
            if (outFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
                QTextStream out(&outFile);
                out << keyEdit->text();
                outFile.close();
            }
        }
    });
    leftLayout->addWidget(new QLabel(tr("System Prompt:"), dlg));
    leftLayout->addWidget(sysEdit);
    leftLayout->addWidget(new QLabel(tr("User Prompt:"), dlg));
    leftLayout->addWidget(promptEdit);

    // Right panel: other options table
    rightLayout->addWidget(new QLabel(tr("Other Options:"), dlg));
    rightLayout->addWidget(table);

    panelsLayout->addWidget(leftPanel);
    panelsLayout->addWidget(rightPanel);
    dlgLayout->addLayout(panelsLayout);

    // Save / Cancel buttons
    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    auto *saveBtn = new QPushButton(tr("Save"), dlg);
    auto *cancelBtn = new QPushButton(tr("Cancel"), dlg);
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(saveBtn);
    dlgLayout->addLayout(btnLayout);
    connect(saveBtn, &QPushButton::clicked, dlg, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, dlg, &QDialog::reject);

    // Commit on Save
    if (dlg->exec() == QDialog::Accepted) {
        executeCommand(QString("r2ai -e api=%1").arg(providerCombo->currentText()));
        executeCommand(QString("r2ai -e model=%1").arg(modelCombo->currentText()));
        executeCommand(QString("r2ai -e system=%1").arg(sysEdit->toPlainText()));
        executeCommand(QString("r2ai -e prompt=%1").arg(promptEdit->toPlainText()));
        for (int i = 0; i < table->rowCount(); i++) {
            QString key = table->item(i, 0)->text();
            QString val;
            if (auto *cb = qobject_cast<QCheckBox *>(table->cellWidget(i, 1))) {
                val = cb->isChecked() ? "true" : "false";
            } else if (auto *it = table->item(i, 1)) {
                val = it->text();
            }
            executeCommand(QString("r2ai -e %1=%2").arg(key).arg(val));
        }
    }
}
