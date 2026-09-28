
#include "QuickFilterView.h"
#include "ui_QuickFilterView.h"

#include <QToolButton>

QuickFilterView::QuickFilterView(QWidget *parent, bool defaultOn)
    : QWidget(parent)
    , ui(new Ui::QuickFilterView())
{
    ui->setupUi(this);

    connect(ui->filterLineEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        emit filterTextChanged(text);
    });

    if (!defaultOn) {
        closeFilter();
    }
}

QuickFilterView::~QuickFilterView() {}

void QuickFilterView::addActionButton(QAction *action)
{
    auto button = new QToolButton(this);
    button->setAutoRaise(true);
    button->setDefaultAction(action);
    ui->horizontalLayout->addWidget(button, 0, Qt::AlignRight);
    hasActions = true;
    show();
}

void QuickFilterView::showFilter()
{
    show();
    ui->filterLineEdit->show();
    ui->filterLineEdit->setFocus();
}

void QuickFilterView::clearFilter()
{
    if (ui->filterLineEdit->text().isEmpty()) {
        closeFilter();
    } else {
        ui->filterLineEdit->setText("");
    }
}

void QuickFilterView::closeFilter()
{
    ui->filterLineEdit->setText("");
    if (hasActions) {
        ui->filterLineEdit->hide();
    } else {
        hide();
    }
    emit filterClosed();
}
