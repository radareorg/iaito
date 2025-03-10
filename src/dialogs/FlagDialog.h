#ifndef FLAGDIALOG_H
#define FLAGDIALOG_H

#include "core/IaitoCommon.h"
#include <memory>
#include <QDialog>

namespace Ui {
class FlagDialog;
}

class FlagDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FlagDialog(RVA offset, QWidget *parent = nullptr);
    ~FlagDialog();

private slots:
    void buttonBoxAccepted();
    void buttonBoxRejected();

private:
    std::unique_ptr<Ui::FlagDialog> ui;
    RVA offset;
    QString flagName;
    ut64 flagOffset;
};

#endif // FLAGDIALOG_H
