// KeyboardOptionsWidget allows configuration of radare2 function key eval variables (key.f1..key.f12)
#pragma once

#include <memory>
#include <QDialog>

class SettingsDialog;

namespace Ui {
class KeyboardOptionsWidget;
}

class KeyboardOptionsWidget : public QDialog
{
    Q_OBJECT

public:
    explicit KeyboardOptionsWidget(SettingsDialog *parent);
    ~KeyboardOptionsWidget();

private:
    std::unique_ptr<Ui::KeyboardOptionsWidget> ui;
};