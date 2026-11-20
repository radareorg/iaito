#include "MetadataOptionsWidget.h"
#include "ui_MetadataOptionsWidget.h"

#include "SettingsDialog.h"
#include "common/Configuration.h"
#include "common/Helpers.h"

MetadataOptionsWidget::MetadataOptionsWidget(SettingsDialog *dialog)
    : QDialog(dialog)
    , ui(new Ui::MetadataOptionsWidget)
{
    ui->setupUi(this);
    ui->jumpLineSpacingComboBox->blockSignals(true);
    for (int spacing : {2, 4, 6, 8, 10, 12, 16, 20, 24, 32}) {
        ui->jumpLineSpacingComboBox->addItem(tr("%1 px").arg(spacing), spacing);
    }
    ui->jumpLineSpacingComboBox->blockSignals(false);
    ui->jumpLineStyleComboBox->blockSignals(true);
    using Style = Configuration::DisassemblyJumpLineStyle;
    ui->jumpLineStyleComboBox->addItem(tr("Square"), int(Style::Square));
    ui->jumpLineStyleComboBox->addItem(tr("Curved"), int(Style::Curved));
    ui->jumpLineStyleComboBox->addItem(tr("Circuit"), int(Style::Circuit));
    ui->jumpLineStyleComboBox->blockSignals(false);

    checkboxes
        = {{ui->varsubCheckBox, "asm.sub.var"},
           {ui->varsubOnlyCheckBox, "asm.sub.varonly"},
           {ui->slowCheckBox, "asm.slow"},
           {ui->linesCheckBox, "asm.lines"},
           {ui->fcnlinesCheckBox, "asm.lines.fcn"},
           {ui->flgoffCheckBox, "asm.flags.offset"},
           {ui->emuCheckBox, "asm.emu"},
           {ui->emuStrCheckBox, "emu.str"},
           {ui->varsumCheckBox, "asm.var.summary"},
           {ui->sizeCheckBox, "asm.size"}};

    for (const auto &checkbox : checkboxes) {
        connect(
            checkbox.checkBox,
            &QCheckBox::toggled,
            this,
            [this, config = checkbox.config](bool checked) {
                Config()->setConfig(config, checked);
                triggerAsmOptionsChanged();
            });
    }

    connect(ui->linesCheckBox, &QCheckBox::toggled, ui->jumpLineSpacingLabel, &QWidget::setEnabled);
    connect(ui->linesCheckBox, &QCheckBox::toggled, ui->jumpLineSpacingComboBox, &QWidget::setEnabled);
    connect(ui->linesCheckBox, &QCheckBox::toggled, ui->jumpLineStyleLabel, &QWidget::setEnabled);
    connect(ui->linesCheckBox, &QCheckBox::toggled, ui->jumpLineStyleComboBox, &QWidget::setEnabled);
    connect(
        ui->linesCheckBox,
        &QCheckBox::toggled,
        ui->adaptiveJumpLineSpacingCheckBox,
        &QWidget::setEnabled);
    connect(
        Core(),
        &IaitoCore::asmOptionsChanged,
        this,
        &MetadataOptionsWidget::updateAsmOptionsFromVars);
    updateAsmOptionsFromVars();
}

MetadataOptionsWidget::~MetadataOptionsWidget() = default;

void MetadataOptionsWidget::updateAsmOptionsFromVars()
{
    for (const auto &checkbox : checkboxes) {
        qhelpers::setCheckedWithoutSignals(
            checkbox.checkBox, Config()->getConfigBool(checkbox.config));
    }
    bool varsubEnabled = Config()->getConfigBool("asm.sub.var");
    ui->varsubOnlyCheckBox->setEnabled(varsubEnabled);

    const bool adaptiveSpacing = Config()->getDisassemblyJumpLineAdaptiveSpacingEnabled();
    qhelpers::setCheckedWithoutSignals(ui->adaptiveJumpLineSpacingCheckBox, adaptiveSpacing);
    qhelpers::setCheckedWithoutSignals(
        ui->followJumpLineCheckBox, Config()->getDisassemblyJumpLineFollowEnabled());
    ui->jumpLineStyleComboBox->blockSignals(true);
    ui->jumpLineStyleComboBox->setCurrentIndex(
        ui->jumpLineStyleComboBox->findData(int(Config()->getDisassemblyJumpLineStyle())));
    ui->jumpLineStyleComboBox->blockSignals(false);
    qhelpers::setCheckedWithoutSignals(
        ui->colorizeJumpLineCheckBox, Config()->getDisassemblyJumpLineColorizeEnabled());
    ui->jumpLineSpacingLabel->setText(
        adaptiveSpacing ? tr("Maximum spacing:") : tr("Fixed spacing:"));
    ui->jumpLineSpacingComboBox->blockSignals(true);
    const int spacingIndex = ui->jumpLineSpacingComboBox->findData(
        Config()->getDisassemblyJumpLineSpacing());
    ui->jumpLineSpacingComboBox->setCurrentIndex(spacingIndex < 0 ? 0 : spacingIndex);
    ui->jumpLineSpacingComboBox->blockSignals(false);
    const bool jumpLinesEnabled = Config()->getConfigBool("asm.lines");
    ui->adaptiveJumpLineSpacingCheckBox->setEnabled(jumpLinesEnabled);
    ui->jumpLineSpacingLabel->setEnabled(jumpLinesEnabled);
    ui->jumpLineSpacingComboBox->setEnabled(jumpLinesEnabled);
    ui->jumpLineStyleLabel->setEnabled(jumpLinesEnabled);
    ui->jumpLineStyleComboBox->setEnabled(jumpLinesEnabled);
}

void MetadataOptionsWidget::resetToDefault()
{
    Config()->resetToDefaultAsmOptions();
    updateAsmOptionsFromVars();
    triggerAsmOptionsChanged();
}

void MetadataOptionsWidget::triggerAsmOptionsChanged()
{
    disconnect(
        Core(),
        &IaitoCore::asmOptionsChanged,
        this,
        &MetadataOptionsWidget::updateAsmOptionsFromVars);
    Core()->triggerAsmOptionsChanged();
    connect(
        Core(),
        &IaitoCore::asmOptionsChanged,
        this,
        &MetadataOptionsWidget::updateAsmOptionsFromVars);
}

void MetadataOptionsWidget::on_jumpLineSpacingComboBox_currentIndexChanged(int index)
{
    if (index < 0) {
        return;
    }
    Config()->setDisassemblyJumpLineSpacing(ui->jumpLineSpacingComboBox->itemData(index).toInt());
    triggerAsmOptionsChanged();
}

void MetadataOptionsWidget::on_adaptiveJumpLineSpacingCheckBox_toggled(bool checked)
{
    Config()->setDisassemblyJumpLineAdaptiveSpacingEnabled(checked);
    ui->jumpLineSpacingLabel->setText(checked ? tr("Maximum spacing:") : tr("Fixed spacing:"));
    triggerAsmOptionsChanged();
}

void MetadataOptionsWidget::on_followJumpLineCheckBox_toggled(bool checked)
{
    Config()->setDisassemblyJumpLineFollowEnabled(checked);
    triggerAsmOptionsChanged();
}

void MetadataOptionsWidget::on_jumpLineStyleComboBox_currentIndexChanged(int index)
{
    if (index < 0) {
        return;
    }
    Config()->setDisassemblyJumpLineStyle(
        static_cast<Configuration::DisassemblyJumpLineStyle>(
            ui->jumpLineStyleComboBox->itemData(index).toInt()));
    triggerAsmOptionsChanged();
}

void MetadataOptionsWidget::on_colorizeJumpLineCheckBox_toggled(bool checked)
{
    Config()->setDisassemblyJumpLineColorizeEnabled(checked);
    triggerAsmOptionsChanged();
}

void MetadataOptionsWidget::on_varsubCheckBox_toggled(bool checked)
{
    Config()->setConfig("asm.sub.var", checked);
    ui->varsubOnlyCheckBox->setEnabled(checked);
    triggerAsmOptionsChanged();
}

void MetadataOptionsWidget::on_buttonBox_clicked(QAbstractButton *button)
{
    switch (ui->buttonBox->buttonRole(button)) {
    case QDialogButtonBox::ButtonRole::ResetRole:
        resetToDefault();
        break;
    default:
        break;
    }
}
