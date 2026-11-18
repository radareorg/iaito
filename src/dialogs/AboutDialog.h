#ifndef ABOUTDIALOG_H
#define ABOUTDIALOG_H

#include "common/ResponsiveDialog.h"
#include <memory>

namespace Ui {
class AboutDialog;
}

class AboutDialog : public ResponsiveDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(QWidget *parent = nullptr);
    ~AboutDialog();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void on_buttonBox_rejected();
    void on_showVersionButton_clicked();
    void on_showPluginsButton_clicked();
    void on_showPackageManagerButton_clicked();

private:
    std::unique_ptr<Ui::AboutDialog> ui;

    QString buildQtVersionString(void);
};

#endif // ABOUTDIALOG_H
