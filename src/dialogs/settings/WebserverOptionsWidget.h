#pragma once

#include <memory>

#include "core/Iaito.h"

class SettingsDialog;

namespace Ui {
class WebserverOptionsWidget;
}

class WebserverOptionsWidget : public QDialog
{
    Q_OBJECT

public:
    explicit WebserverOptionsWidget(SettingsDialog *dialog);
    ~WebserverOptionsWidget();

private:
    std::unique_ptr<Ui::WebserverOptionsWidget> ui;

private slots:
    void updatePort();
    void updateBind();
    void updateRoot();
    void onSandboxToggled(bool checked);
    void onUploadToggled(bool checked);
    void onDirlistToggled(bool checked);
    void onVerboseToggled(bool checked);
};
