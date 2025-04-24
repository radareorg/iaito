#include "FlagDialog.h"
#include <QColorDialog>
#include <QEvent>
#include <QColor>
#include "ui_FlagDialog.h"

#include "core/Iaito.h"
#include <QIntValidator>

FlagDialog::FlagDialog(RVA offset, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::FlagDialog)
    , offset(offset)
    , flagName("")
    , flagOffset(RVA_INVALID)
{
    // Setup UI
    ui->setupUi(this);
    setWindowFlags(windowFlags() & (~Qt::WindowContextHelpButtonHint));
    RFlagItem *flag = r_flag_get_i(Core()->core()->flags, offset);
    if (flag) {
        flagName = QString(flag->name);
#if R2_VERSION_NUMBER >= 50909
        flagOffset = flag->addr;
#else
        flagOffset = flag->offset;
#endif
    }

    auto size_validator = new QIntValidator(ui->sizeEdit);
    size_validator->setBottom(1);
    ui->sizeEdit->setValidator(size_validator);
    // Pre-populate fields for existing flag
    if (flag) {
        ui->sizeEdit->setText(QString::number(flag->size));
	RFlagItemMeta *fim = r_flag_get_meta (Core()->core()->flags, flag->id);
	if (fim) {
		if (fim->comment) {
		    ui->commentEdit->setText(QString(fim->comment));
		}
		if (fim->color) {
		    ui->colorEdit->setText(QString(fim->color));
		}
	}
    }
    // Enable color picker on color field
    ui->colorEdit->installEventFilter(this);
    if (flag) {
        ui->nameEdit->setText(flag->name);
        ui->labelAction->setText(tr("Edit flag at %1").arg(RAddressString(offset)));
    } else {
        ui->labelAction->setText(tr("Add flag at %1").arg(RAddressString(offset)));
    }

    // Connect slots
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &FlagDialog::buttonBoxAccepted);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &FlagDialog::buttonBoxRejected);
}

FlagDialog::~FlagDialog() {}

void FlagDialog::buttonBoxAccepted()
{
    RVA size = ui->sizeEdit->text().toULongLong();
    QString name = ui->nameEdit->text();
    QString color = ui->colorEdit->text();
    QString comment = ui->commentEdit->text();
    if (flagOffset != RVA_INVALID) {
        // Existing flag: remove old flag by name
        Core()->delFlag(flagName);
        // If a name is provided, recreate/update the flag with new attributes
        if (!name.isEmpty()) {
            Core()->addFlag(offset, name, size, color, comment);
        }
    } else {
        // New flag: only add if name is provided
        if (!name.isEmpty()) {
            Core()->addFlag(offset, name, size, color, comment);
        }
    }
    close();
    setResult(QDialog::Accepted);
}

void FlagDialog::buttonBoxRejected()
{
    close();
    this->setResult(QDialog::Rejected);
}

bool FlagDialog::eventFilter(QObject *watched, QEvent *event)
{
    // Open color picker when clicking on color field
    if (watched == ui->colorEdit && event->type() == QEvent::MouseButtonPress) {
        QColor initial = QColor(ui->colorEdit->text());
        QColor c = QColorDialog::getColor(initial, this, tr("Select flag color"));
        if (c.isValid()) {
            ui->colorEdit->setText(c.name());
        }
        return true;
    }
    return QDialog::eventFilter(watched, event);
}
