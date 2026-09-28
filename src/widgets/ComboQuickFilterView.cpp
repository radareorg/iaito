#include "ComboQuickFilterView.h"

#include <QHBoxLayout>
#include <QLabel>

ComboQuickFilterView::ComboQuickFilterView(QWidget *parent)
    : QuickFilterView(parent)
    , label(new QLabel(this))
    , combo(new QComboBox(this))
{
    label->setObjectName(QStringLiteral("label"));
    combo->setObjectName(QStringLiteral("comboBox"));
    auto row = qobject_cast<QHBoxLayout *>(layout());
    row->addWidget(label);
    row->addWidget(combo);
}

void ComboQuickFilterView::setLabelText(const QString &text)
{
    label->setText(text);
}

QComboBox *ComboQuickFilterView::comboBox()
{
    return combo;
}

void ComboQuickFilterView::clearFilter()
{
    QuickFilterView::clearFilter();
    combo->setCurrentIndex(0);
}

void ComboQuickFilterView::closeFilter()
{
    combo->setCurrentIndex(0);
    QuickFilterView::closeFilter();
}
