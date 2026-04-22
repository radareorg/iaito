#include <QFontDialog>
#include <QLabel>

#include "GraphOptionsWidget.h"
#include "ui_GraphOptionsWidget.h"

#include "SettingsDialog.h"

#include "common/Configuration.h"
#include "common/Helpers.h"

GraphOptionsWidget::GraphOptionsWidget(SettingsDialog *dialog)
    : QDialog(dialog)
    , ui(new Ui::GraphOptionsWidget)
{
    ui->setupUi(this);
    ui->checkTransparent->setChecked(Config()->getBitmapTransparentState());
    ui->blockEntryCheckBox->setChecked(Config()->getGraphBlockEntryOffset());
    ui->blockShadowCheckBox->setChecked(Config()->getGraphBlockShadow());
    ui->bitmapGraphScale->setValue(Config()->getBitmapExportScaleFactor() * 100.0);
    updateOptionsFromVars();

    connect<void (QDoubleSpinBox::*)(double)>(
        ui->bitmapGraphScale,
        (&QDoubleSpinBox::valueChanged),
        this,
        &GraphOptionsWidget::bitmapGraphScaleValueChanged);
    connect(
        ui->checkTransparent,
        &QCheckBox::toggled,
        this,
        &GraphOptionsWidget::checkTransparentStateChanged);
    connect(
        ui->blockEntryCheckBox,
        &QCheckBox::toggled,
        this,
        &GraphOptionsWidget::checkGraphBlockEntryOffsetChanged);
    connect(
        ui->blockShadowCheckBox,
        &QCheckBox::toggled,
        this,
        &GraphOptionsWidget::checkGraphBlockShadowChanged);

    connect(Core(), &IaitoCore::graphOptionsChanged, this, &GraphOptionsWidget::updateOptionsFromVars);
    QSpinBox *graphSpacingWidgets[]
        = {ui->horizontalEdgeSpacing,
           ui->horizontalBlockSpacing,
           ui->verticalEdgeSpacing,
           ui->verticalBlockSpacing};
    for (auto widget : graphSpacingWidgets) {
        connect<void (QSpinBox::*)(
            int)>(widget, &QSpinBox::valueChanged, this, &GraphOptionsWidget::layoutSpacingChanged);
    }
}

GraphOptionsWidget::~GraphOptionsWidget() {}

void GraphOptionsWidget::updateOptionsFromVars()
{
    qhelpers::setCheckedWithoutSignals(ui->graphOffsetCheckBox, Config()->getConfigBool("graph.addr"));
    ui->maxColsSpinBox->blockSignals(true);
    ui->maxColsSpinBox->setValue(Config()->getGraphBlockMaxChars());
    ui->maxColsSpinBox->blockSignals(false);
    auto blockSpacing = Config()->getGraphBlockSpacing();
    ui->horizontalBlockSpacing->setValue(blockSpacing.x());
    ui->verticalBlockSpacing->setValue(blockSpacing.y());
    auto edgeSpacing = Config()->getGraphEdgeSpacing();
    ui->horizontalEdgeSpacing->setValue(edgeSpacing.x());
    ui->verticalEdgeSpacing->setValue(edgeSpacing.y());
}

void GraphOptionsWidget::triggerOptionsChanged()
{
    disconnect(
        Core(), &IaitoCore::graphOptionsChanged, this, &GraphOptionsWidget::updateOptionsFromVars);
    Core()->triggerGraphOptionsChanged();
    connect(Core(), &IaitoCore::graphOptionsChanged, this, &GraphOptionsWidget::updateOptionsFromVars);
}

void GraphOptionsWidget::on_maxColsSpinBox_valueChanged(int value)
{
    Config()->setGraphBlockMaxChars(value);
    triggerOptionsChanged();
}

void GraphOptionsWidget::on_graphOffsetCheckBox_toggled(bool checked)
{
    Config()->setConfig("graph.addr", checked);
    emit Core() -> asmOptionsChanged();
    triggerOptionsChanged();
}

void GraphOptionsWidget::checkTransparentStateChanged(bool checked)
{
    Config()->setBitmapTransparentState(checked);
}

void GraphOptionsWidget::bitmapGraphScaleValueChanged(double value)
{
    double value_decimal = value / (double) 100.0;
    Config()->setBitmapExportScaleFactor(value_decimal);
}

void GraphOptionsWidget::layoutSpacingChanged()
{
    QPoint blockSpacing{ui->horizontalBlockSpacing->value(), ui->verticalBlockSpacing->value()};
    QPoint edgeSpacing{ui->horizontalEdgeSpacing->value(), ui->verticalEdgeSpacing->value()};
    Config()->setGraphSpacing(blockSpacing, edgeSpacing);
    triggerOptionsChanged();
}

void GraphOptionsWidget::checkGraphBlockEntryOffsetChanged(bool checked)
{
    Config()->setGraphBlockEntryOffset(checked);
    triggerOptionsChanged();
}

void GraphOptionsWidget::checkGraphBlockShadowChanged(bool checked)
{
    Config()->setGraphBlockShadow(checked);
    triggerOptionsChanged();
}
