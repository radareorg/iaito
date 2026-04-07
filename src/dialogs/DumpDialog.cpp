#include "DumpDialog.h"
#include "Iaito.h"
#include "ui_DumpDialog.h"

#include <QFileDialog>
#include <QPushButton>
#include <QRegularExpressionValidator>

DumpDialog::DumpDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::DumpDialog)
{
    ui->setupUi(this);

    auto *hexValidator
        = new QRegularExpressionValidator(QRegularExpression("[0-9a-fA-Fx]{1,18}"), this);
    ui->addressLE->setValidator(hexValidator);
    ui->lengthLE->setValidator(hexValidator);

    // Pre-fill address with current seek
    ui->addressLE->setText(RAddressString(Core()->getOffset()));

    // Pre-fill io.va from current setting
    bool ioVa = Core()->getConfigb("io.va");
    ui->vaModeCB->setChecked(ioVa);

    connect(ui->browsePB, &QPushButton::clicked, this, &DumpDialog::browseFile);
    connect(ui->fileLE, &QLineEdit::textChanged, this, &DumpDialog::validate);
    connect(ui->addressLE, &QLineEdit::textChanged, this, &DumpDialog::validate);
    connect(ui->lengthLE, &QLineEdit::textChanged, this, &DumpDialog::validate);

    validate();
}

DumpDialog::~DumpDialog()
{
    delete ui;
}

QString DumpDialog::getFilePath() const
{
    return ui->fileLE->text();
}

RVA DumpDialog::getAddress() const
{
    return Core()->math(ui->addressLE->text());
}

ut64 DumpDialog::getLength() const
{
    return Core()->math(ui->lengthLE->text());
}

bool DumpDialog::useVirtualAddressing() const
{
    return ui->vaModeCB->isChecked();
}

void DumpDialog::browseFile()
{
    QString fileName = QFileDialog::getSaveFileName(this, tr("Dump to file"));
    if (!fileName.isEmpty()) {
        ui->fileLE->setText(fileName);
    }
}

void DumpDialog::validate()
{
    bool valid = !ui->fileLE->text().isEmpty() && !ui->addressLE->text().isEmpty()
                 && !ui->lengthLE->text().isEmpty();
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(valid);
}
