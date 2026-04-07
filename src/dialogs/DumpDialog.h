#ifndef DUMPDIALOG_H
#define DUMPDIALOG_H

#include "IaitoCommon.h"
#include <QDialog>

namespace Ui {
class DumpDialog;
}

class DumpDialog : public QDialog
{
    Q_OBJECT
public:
    explicit DumpDialog(QWidget *parent = nullptr);
    ~DumpDialog();

    QString getFilePath() const;
    RVA getAddress() const;
    ut64 getLength() const;
    bool useVirtualAddressing() const;

private slots:
    void browseFile();
    void validate();

private:
    Ui::DumpDialog *ui;
};

#endif // DUMPDIALOG_H
