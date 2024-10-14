#include "IaitoTreeView.h"
#include "ui_IaitoTreeView.h"

IaitoTreeView::IaitoTreeView(QWidget *parent)
    : QTreeView(parent)
    , ui(new Ui::IaitoTreeView())
{
    ui->setupUi(this);
    this->setSelectionMode(QAbstractItemView::ExtendedSelection);
    this->setUniformRowHeights(true);
}

IaitoTreeView::~IaitoTreeView() {}
