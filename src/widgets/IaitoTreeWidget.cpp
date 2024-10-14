#include "IaitoTreeWidget.h"
#include "core/MainWindow.h"

IaitoTreeWidget::IaitoTreeWidget(QObject *parent)
    : QObject(parent)
    , bar(nullptr)
{}

void IaitoTreeWidget::addStatusBar(QVBoxLayout *pos)
{
    if (!bar) {
        bar = new QStatusBar;
        QSizePolicy sizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
        bar->setSizePolicy(sizePolicy);
        pos->addWidget(bar);
    }
}

void IaitoTreeWidget::showItemsNumber(int count)
{
    if (bar) {
        bar->showMessage(tr("%1 Items").arg(count));
    }
}

void IaitoTreeWidget::showStatusBar(bool show)
{
    bar->setVisible(show);
}

IaitoTreeWidget::~IaitoTreeWidget() {}
