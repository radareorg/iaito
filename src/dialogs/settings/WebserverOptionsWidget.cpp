#include "WebserverOptionsWidget.h"
#include "SettingsDialog.h"
#include "ui_WebserverOptionsWidget.h"

#include "common/Configuration.h"
#include "common/Helpers.h"

WebserverOptionsWidget::WebserverOptionsWidget(SettingsDialog *dialog)
    : QDialog(dialog)
    , ui(new Ui::WebserverOptionsWidget)
{
    ui->setupUi(this);

    // Load current values from r2 config
    ui->portEdit->setText(Core()->getConfig("http.port"));
    ui->bindEdit->setText(Core()->getConfig("http.bind"));
    ui->rootEdit->setText(Core()->getConfig("http.root"));
    ui->sandboxCheckBox->setChecked(Core()->getConfigb("http.sandbox"));
    ui->uploadCheckBox->setChecked(Core()->getConfigb("http.upload"));
    ui->dirlistCheckBox->setChecked(Core()->getConfigb("http.dirlist"));
    ui->verboseCheckBox->setChecked(Core()->getConfigb("http.verbose"));

    connect(ui->portEdit, &QLineEdit::editingFinished, this, &WebserverOptionsWidget::updatePort);
    connect(ui->bindEdit, &QLineEdit::editingFinished, this, &WebserverOptionsWidget::updateBind);
    connect(ui->rootEdit, &QLineEdit::editingFinished, this, &WebserverOptionsWidget::updateRoot);
    connect(ui->sandboxCheckBox, &QCheckBox::toggled, this, &WebserverOptionsWidget::onSandboxToggled);
    connect(ui->uploadCheckBox, &QCheckBox::toggled, this, &WebserverOptionsWidget::onUploadToggled);
    connect(ui->dirlistCheckBox, &QCheckBox::toggled, this, &WebserverOptionsWidget::onDirlistToggled);
    connect(ui->verboseCheckBox, &QCheckBox::toggled, this, &WebserverOptionsWidget::onVerboseToggled);
}

WebserverOptionsWidget::~WebserverOptionsWidget() {}

void WebserverOptionsWidget::updatePort()
{
    Core()->setConfig("http.port", ui->portEdit->text());
}

void WebserverOptionsWidget::updateBind()
{
    Core()->setConfig("http.bind", ui->bindEdit->text());
}

void WebserverOptionsWidget::updateRoot()
{
    Core()->setConfig("http.root", ui->rootEdit->text());
}

void WebserverOptionsWidget::onSandboxToggled(bool checked)
{
    Core()->setConfig("http.sandbox", checked);
}

void WebserverOptionsWidget::onUploadToggled(bool checked)
{
    Core()->setConfig("http.upload", checked);
}

void WebserverOptionsWidget::onDirlistToggled(bool checked)
{
    Core()->setConfig("http.dirlist", checked);
}

void WebserverOptionsWidget::onVerboseToggled(bool checked)
{
    Core()->setConfig("http.verbose", checked);
}
