#ifndef VERSIONINFODIALOG_H
#define VERSIONINFODIALOG_H

#include <memory>
#include <QDialog>

#include "core/Iaito.h"

namespace Ui {
class VersionInfoDialog;
}

class VersionInfoDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VersionInfoDialog(QWidget *parent = nullptr);
    ~VersionInfoDialog();

private:
    std::unique_ptr<Ui::VersionInfoDialog> ui;
    IaitoCore *core;

    void fillVersionInfo();
};

#endif // VERSIONINFODIALOG_H
