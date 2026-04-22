
#ifndef PLUGINSOPTIONSWIDGET_H
#define PLUGINSOPTIONSWIDGET_H

#include <QDialog>

class SettingsDialog;

class PluginsOptionsWidget : public QDialog
{
    Q_OBJECT

public:
    explicit PluginsOptionsWidget(SettingsDialog *dialog);
    ~PluginsOptionsWidget();
};

#endif // IAITO_PLUGINSOPTIONSWIDGET_H
