#ifndef SCRIPTMANAGERDIALOG_H
#define SCRIPTMANAGERDIALOG_H

#include <QDialog>

class ScriptManagerWidget;

class ScriptManagerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ScriptManagerDialog(QWidget *parent = nullptr);
    ~ScriptManagerDialog() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    ScriptManagerWidget *widget;
};

#endif // SCRIPTMANAGERDIALOG_H
