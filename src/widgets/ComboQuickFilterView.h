#ifndef COMBOQUICKFILTERVIEW_H
#define COMBOQUICKFILTERVIEW_H

#include "QuickFilterView.h"

#include <QComboBox>
class QLabel;

class IAITO_EXPORT ComboQuickFilterView : public QuickFilterView
{
    Q_OBJECT

public:
    explicit ComboQuickFilterView(QWidget *parent = nullptr);

    void setLabelText(const QString &text);
    QComboBox *comboBox();

public slots:
    void clearFilter();
    void closeFilter();

private:
    QLabel *label;
    QComboBox *combo;
};

#endif // COMBOQUICKFILTERVIEW_H
