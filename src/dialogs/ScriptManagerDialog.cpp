#include "dialogs/ScriptManagerDialog.h"
#include "dialogs/ScriptManagerWidget.h"

#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QVBoxLayout>

ScriptManagerDialog::ScriptManagerDialog(QWidget *parent)
    : QDialog(parent)
{
    setAttribute(Qt::WA_DeleteOnClose, false);
    setObjectName(QStringLiteral("scriptManagerDialog"));
    setWindowTitle(tr("Script Editor"));
    resize(1000, 680);

    widget = new ScriptManagerWidget(this);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(widget, 1);
    layout->addWidget(buttons);
}

ScriptManagerDialog::~ScriptManagerDialog() = default;

void ScriptManagerDialog::closeEvent(QCloseEvent *event)
{
    if (!widget->maybeSave()) {
        event->ignore();
        return;
    }
    widget->shutdown();
    QDialog::closeEvent(event);
}
