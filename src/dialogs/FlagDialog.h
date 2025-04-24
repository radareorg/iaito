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
    // offset: address of flag; defaultSize: initial size to pre-fill (number of bytes)
    // Constructor: offset is address; defaultSize optional size for new flags
    explicit FlagDialog(RVA offset, QWidget *parent = nullptr);
    explicit FlagDialog(RVA offset, ut64 defaultSize, QWidget *parent = nullptr);
    ~FlagDialog();

private slots:
    void buttonBoxAccepted();
    void buttonBoxRejected();

private:
    std::unique_ptr<Ui::FlagDialog> ui;
    RVA offset;
    QString flagName;
    ut64 flagOffset;
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
};

#endif // FLAGDIALOG_H
