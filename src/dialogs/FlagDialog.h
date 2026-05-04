#ifndef FLAGDIALOG_H
#define FLAGDIALOG_H

#include "core/IaitoCommon.h"
#include <functional>
#include <memory>
#include <utility>
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
    // offset: address of flag; defaultSize: initial size to pre-fill (number of bytes)
    // Constructor: offset is address; defaultSize optional size for new flags
    explicit FlagDialog(RVA offset, QWidget *parent = nullptr);
    explicit FlagDialog(RVA offset, ut64 defaultSize, QWidget *parent = nullptr);
    ~FlagDialog();

    // Append an "Add flag..." / "Edit flag..." action pair to menu, wired to open
    // FlagDialog at addr. Whichever action is inapplicable (add when a flag already
    // exists, edit when none does) is created disabled. onChanged is invoked when
    // the dialog is accepted (typically a refresh callback). Returns the two
    // actions so callers can attach icons, etc.
    static std::pair<QAction *, QAction *> addFlagMenuActions(
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

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
};

#endif // FLAGDIALOG_H
