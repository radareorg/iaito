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
    updateAvailability();
}

R2AIWidget::~R2AIWidget() {}

void R2AIWidget::updateAvailability()
{
    bool available = !Core()->cmd("Lc~^r2ai").trimmed().isEmpty();
    inputLineEdit->setEnabled(available);
    sendButton->setEnabled(available);
    settingsButton->setEnabled(available);
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

    toggleViewAction()->setText(tr("r2ai"));
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

static void setComboToValue(QComboBox *combo, const QString &value)
{
    int i = combo->findText(value);
    if (i >= 0) {
        combo->setCurrentIndex(i);
    }
}

void R2AIWidget::onSettingsClicked()
{
    QDialog *dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(tr("r2ai Settings"));
    QVBoxLayout *dlgLayout = new QVBoxLayout(dlg);

    QString apiVal = Core()->getConfig("r2ai.api");
    QString modelVal = Core()->getConfig("r2ai.model");

    QComboBox *providerCombo = new QComboBox(dlg);
    providerCombo->addItems(Core()->cmd("r2ai -e api=?").split('\n', Qt::SkipEmptyParts));
    setComboToValue(providerCombo, apiVal);

    QComboBox *modelCombo = new QComboBox(dlg);
    auto loadModels = [&]() {
        modelCombo->clear();
        Core()->setConfig("r2ai.api", providerCombo->currentText());
        modelCombo->addItems(Core()->cmd("r2ai -e model=?").split('\n', Qt::SkipEmptyParts));
    };
    loadModels();
    setComboToValue(modelCombo, modelVal);
    connect(providerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [=](int) {
        loadModels();
    });

    QPlainTextEdit *sysEdit = new QPlainTextEdit(dlg);
    sysEdit->setPlainText(Core()->getConfig("r2ai.system"));

    QPlainTextEdit *promptEdit = new QPlainTextEdit(dlg);
    promptEdit->setPlainText(Core()->getConfig("r2ai.prompt"));

    QTableWidget *table = new QTableWidget(dlg);
    table->verticalHeader()->setVisible(false);
    table->setColumnCount(2);
    table->setHorizontalHeaderLabels({tr("Key"), tr("Value")});
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

    QHBoxLayout *panelsLayout = new QHBoxLayout();
    QWidget *leftPanel = new QWidget(dlg);
    QVBoxLayout *leftLayout = new QVBoxLayout(leftPanel);
    QWidget *rightPanel = new QWidget(dlg);
    QVBoxLayout *rightLayout = new QVBoxLayout(rightPanel);

    leftLayout->addWidget(new QLabel(tr("Provider:"), dlg));
    leftLayout->addWidget(providerCombo);
    leftLayout->addWidget(new QLabel(tr("Model:"), dlg));
    leftLayout->addWidget(modelCombo);

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

    rightLayout->addWidget(new QLabel(tr("Other Options:"), dlg));
    rightLayout->addWidget(table);

    panelsLayout->addWidget(leftPanel);
    panelsLayout->addWidget(rightPanel);
    dlgLayout->addLayout(panelsLayout);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    auto *saveBtn = new QPushButton(tr("Save"), dlg);
    auto *cancelBtn = new QPushButton(tr("Cancel"), dlg);
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(saveBtn);
    dlgLayout->addLayout(btnLayout);
    connect(saveBtn, &QPushButton::clicked, dlg, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, dlg, &QDialog::reject);

    if (dlg->exec() == QDialog::Accepted) {
        Core()->setConfig("r2ai.api", providerCombo->currentText());
        Core()->setConfig("r2ai.model", modelCombo->currentText());
        Core()->setConfig("r2ai.system", sysEdit->toPlainText());
        Core()->setConfig("r2ai.prompt", promptEdit->toPlainText());
        for (int i = 0; i < table->rowCount(); i++) {
            QString key = table->item(i, 0)->text();
            QString val;
            if (auto *cb = qobject_cast<QCheckBox *>(table->cellWidget(i, 1))) {
                val = cb->isChecked() ? "true" : "false";
            } else if (auto *it = table->item(i, 1)) {
                val = it->text();
            }
            Core()->setConfig(QString("r2ai.%1").arg(key), val);
        }
    }
}
