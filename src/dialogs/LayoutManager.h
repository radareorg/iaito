#ifndef LAYOUT_MANAGER_H
#define LAYOUT_MANAGER_H

#include <QDialog>
#include <memory>
#include "core/Iaito.h"
#include "common/IaitoLayout.h"

namespace Ui {
class LayoutManager;
}

class LayoutManager : public QDialog
{
    Q_OBJECT

public:
    LayoutManager(QMap<QString, Iaito::IaitoLayout> &layouts, QWidget *parent);
    ~LayoutManager();

private:
    void refreshNameList(QString selection = "");
    void renameCurrentLayout();
    void deleteLayout();
    void updateButtons();
    std::unique_ptr<Ui::LayoutManager> ui;
    QMap<QString, Iaito::IaitoLayout> &layouts;
};

#endif // LAYOUT_MANAGER_H
