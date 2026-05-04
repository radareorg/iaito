#include "FlagDialog.h"
#include "ui_FlagDialog.h"
#include <QAction>
#include <QColor>
#include <QColorDialog>
#include <QEvent>
#include <QMenu>

#include "core/Iaito.h"
#include <QAbstractButton>
#include <QIntValidator>
#include <QPushButton>

// Forwarding constructor: default flag size when only offset & parent provided
FlagDialog::FlagDialog(RVA offset, QWidget *parent)
    : FlagDialog(offset, 1, parent)
{}

// Main constructor: offset, defaultSize (in bytes), parent
FlagDialog::FlagDialog(RVA offset, ut64 defaultSize, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::FlagDialog)
    , offset(offset)
    , flagName("")
    , flagOffset(RVA_INVALID)
{
    // Setup UI
    ui->setupUi(this);
    setWindowFlags(windowFlags() & (~Qt::WindowContextHelpButtonHint));
    // Display the flag offset in a read-only field
    ui->offsetEdit->setText(RAddressString(offset));
    // Initialize color preview if a color is already set
    if (!ui->colorEdit->text().isEmpty()) {
        ui->colorPreview->setStyleSheet(
            QStringLiteral("background-color: %1;").arg(ui->colorEdit->text()));
    }
    // Update the color preview when the color text changes
    connect(ui->colorEdit, &QLineEdit::textChanged, this, [this](const QString &color) {
        ui->colorPreview->setStyleSheet(QStringLiteral("background-color: %1;").arg(color));
    });
    RFlagItem *flag = r_flag_get_i(Core()->core()->flags, offset);
    if (flag) {
        flagName = QString(flag->name);
        flagOffset = flag->addr;
    }

    auto size_validator = new QIntValidator(ui->sizeEdit);
    size_validator->setBottom(1);
    ui->sizeEdit->setValidator(size_validator);
    // Pre-fill size field: use defaultSize for new flags, override with existing for edits
    ui->sizeEdit->setText(QString::number(defaultSize));
    if (flag) {
        ui->sizeEdit->setText(QString::number(flag->size));
        RFlagItemMeta *fim = r_flag_get_meta(Core()->core()->flags, flag->id);
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

    // Connect slots for OK/Cancel
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &FlagDialog::buttonBoxAccepted);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &FlagDialog::buttonBoxRejected);
    // Add Delete button when editing an existing flag
    if (flag) {
        QAbstractButton *deleteBtn
            = ui->buttonBox->addButton(tr("Delete flag"), QDialogButtonBox::DestructiveRole);
        connect(deleteBtn, &QAbstractButton::clicked, this, [this]() {
            Core()->delFlag(flagName);
            accept();
        });
    }
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

std::pair<QAction *, QAction *> FlagDialog::addFlagMenuActions(
    QMenu *menu,
    QWidget *dialogParent,
    RVA addr,
    ut64 defaultSize,
    const QString &addLabel,
    const QString &editLabel,
    std::function<void()> onChanged)
{
    RFlagItem *existing = r_flag_get_in(Core()->core()->flags, addr);

    QAction *addAction = menu->addAction(addLabel);
    addAction->setEnabled(existing == nullptr);
    QObject::connect(
        addAction, &QAction::triggered, dialogParent, [dialogParent, addr, defaultSize, onChanged]() {
            FlagDialog dlg(addr, defaultSize, dialogParent);
            if (dlg.exec() == QDialog::Accepted && onChanged) {
                onChanged();
            }
        });

    QAction *editAction = menu->addAction(editLabel);
    editAction->setEnabled(existing != nullptr);
    QObject::connect(editAction, &QAction::triggered, dialogParent, [dialogParent, addr, onChanged]() {
        FlagDialog dlg(addr, 1, dialogParent);
        if (dlg.exec() == QDialog::Accepted && onChanged) {
            onChanged();
        }
    });

    return {addAction, editAction};
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
