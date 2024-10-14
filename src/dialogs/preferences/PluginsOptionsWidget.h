
#ifndef PLUGINSOPTIONSWIDGET_H
#define PLUGINSOPTIONSWIDGET_H

#include <QDialog>

class PreferencesDialog;

class PluginsOptionsWidget : public QDialog
{
    Q_OBJECT

public:
    explicit PluginsOptionsWidget(PreferencesDialog *dialog);
    ~PluginsOptionsWidget();
};

#endif // IAITO_PLUGINSOPTIONSWIDGET_H
