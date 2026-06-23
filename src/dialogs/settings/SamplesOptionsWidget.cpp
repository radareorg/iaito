#include "SamplesOptionsWidget.h"
#include "SettingsDialog.h"
#include "ui_SamplesOptionsWidget.h"

#include "common/Configuration.h"
#include "common/SamplesDB.h"

static const char *DEFAULT_LOOKUP_URL = "https://www.virustotal.com/gui/file/";

SamplesOptionsWidget::SamplesOptionsWidget(SettingsDialog *dialog)
    : QDialog(dialog)
    , ui(new Ui::SamplesOptionsWidget)
{
    ui->setupUi(this);

    ui->lookupUrlEdit->setText(Config()->getSha256LookupBaseUrl());
    ui->dbPathLabel->setText(SamplesDB::dbRoot());

    connect(
        ui->lookupUrlEdit,
        &QLineEdit::editingFinished,
        this,
        &SamplesOptionsWidget::updateLookupUrl);
    connect(ui->resetUrlButton, &QPushButton::clicked, this, &SamplesOptionsWidget::resetLookupUrl);
}

SamplesOptionsWidget::~SamplesOptionsWidget() {}

void SamplesOptionsWidget::updateLookupUrl()
{
    Config()->setSha256LookupBaseUrl(ui->lookupUrlEdit->text());
}

void SamplesOptionsWidget::resetLookupUrl()
{
    ui->lookupUrlEdit->setText(QString::fromLatin1(DEFAULT_LOOKUP_URL));
    Config()->setSha256LookupBaseUrl(ui->lookupUrlEdit->text());
}
