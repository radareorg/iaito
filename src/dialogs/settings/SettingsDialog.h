
#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include "core/Iaito.h"

#include <QDialog>

#include <memory>

class QPushButton;
class R2PluginsDialog;
class QTreeWidgetItem;

namespace Ui {
class SettingsDialog;
}

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Section { Appearance, Disassembly, Analysis };

    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog();

    void showSection(Section section);

public slots:
    void changePage(QTreeWidgetItem *current, QTreeWidgetItem *previous);

private:
    std::unique_ptr<Ui::SettingsDialog> ui;
    R2PluginsDialog *pluginsDialog = nullptr;
    QPushButton *loadPluginButton = nullptr;

    void chooseThemeIcons();
    void updateLoadPluginButton();
};

#endif // SETTINGSDIALOG_H
