
#ifndef QUICKFILTERVIEW_H
#define QUICKFILTERVIEW_H

#include "core/IaitoCommon.h"

#include <memory>

#include <QWidget>

namespace Ui {
class QuickFilterView;
}

class QAction;

class IAITO_EXPORT QuickFilterView : public QWidget
{
    Q_OBJECT

public:
    explicit QuickFilterView(QWidget *parent = nullptr, bool defaultOn = true);
    ~QuickFilterView();
    void addActionButton(QAction *action);

public slots:
    void showFilter();
    void closeFilter();
    void clearFilter();

signals:
    void filterTextChanged(const QString &text);
    void filterClosed();

private:
    std::unique_ptr<Ui::QuickFilterView> ui;
    bool hasActions = false;
};

#endif // QUICKFILTERVIEW_H
