#ifndef FLAGDIALOG_H
#define FLAGDIALOG_H

#include "core/IaitoCommon.h"
#include <functional>
#include <memory>
#include <tuple>
#include <QDialog>

class QAction;
class QMenu;

namespace Ui {
class FlagDialog;
}

class FlagDialog : public QDialog
{
    Q_OBJECT

public:
    // defaultSize pre-fills the byte count for new flags.
    explicit FlagDialog(RVA offset, QWidget *parent = nullptr);
    explicit FlagDialog(RVA offset, ut64 defaultSize, QWidget *parent = nullptr);
    ~FlagDialog();

    // Append add, edit and delete actions at addr, disabling inapplicable actions.
    // onChanged is called after accepting the dialog or deleting the flag.
    static std::tuple<QAction *, QAction *, QAction *> addFlagMenuActions(
        QMenu *menu,
        QWidget *dialogParent,
        RVA addr,
        ut64 defaultSize,
        const QString &addLabel,
        const QString &editLabel,
        std::function<void()> onChanged);

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
