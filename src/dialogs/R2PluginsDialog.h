#ifndef PLUGINSDIALOG_H
#define PLUGINSDIALOG_H

#include <QAbstractTableModel>
#include <QDialog>

#include "core/Iaito.h"

namespace Ui {
class R2PluginsDialog;
}

class R2PluginsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit R2PluginsDialog(QWidget *parent = nullptr, bool showDialogButtons = true);
    ~R2PluginsDialog();

public slots:
    void loadPlugin();

private:
    void refreshPluginDescriptions();

    Ui::R2PluginsDialog *ui;
};

#endif // PLUGINSDIALOG_H
