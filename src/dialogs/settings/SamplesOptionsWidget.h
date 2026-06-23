#pragma once

#include <memory>

#include <QDialog>

class SettingsDialog;

namespace Ui {
class SamplesOptionsWidget;
}

class SamplesOptionsWidget : public QDialog
{
    Q_OBJECT

public:
    explicit SamplesOptionsWidget(SettingsDialog *dialog);
    ~SamplesOptionsWidget();

private:
    std::unique_ptr<Ui::SamplesOptionsWidget> ui;

private slots:
    void updateLookupUrl();
    void resetLookupUrl();
};
