
#ifndef GRAPHOPTIONSWIDGET_H
#define GRAPHOPTIONSWIDGET_H

#include <memory>
#include <QDialog>
#include <QPushButton>

#include "core/Iaito.h"

class PreferencesDialog;

namespace Ui {
class GraphOptionsWidget;
}

class GraphOptionsWidget : public QDialog
{
    Q_OBJECT

public:
    explicit GraphOptionsWidget(PreferencesDialog *dialog);
    ~GraphOptionsWidget();

private:
    std::unique_ptr<Ui::GraphOptionsWidget> ui;

    void triggerOptionsChanged();

private slots:
    void updateOptionsFromVars();

    void on_maxColsSpinBox_valueChanged(int value);
    void on_graphOffsetCheckBox_toggled(bool checked);

    void checkTransparentStateChanged(int checked);
    void bitmapGraphScaleValueChanged(double value);
    void checkGraphBlockEntryOffsetChanged(bool checked);
    void layoutSpacingChanged();
};

#endif // GRAPHOPTIONSWIDGET_H
