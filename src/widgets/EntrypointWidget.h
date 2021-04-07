#ifndef ENTRYPOINTWIDGET_H
#define ENTRYPOINTWIDGET_H

#include <memory>
#include <QStyledItemDelegate>
#include <QTreeWidgetItem>

#include "IaitoDockWidget.h"

class MainWindow;
class QTreeWidget;

namespace Ui {
class EntrypointWidget;
}

class EntrypointWidget : public IaitoDockWidget
{
    Q_OBJECT

public:
    explicit EntrypointWidget(MainWindow *main);
    ~EntrypointWidget();

private slots:
    void on_entrypointTreeWidget_itemDoubleClicked(QTreeWidgetItem *item, int column);

    void fillEntrypoint();

private:
    std::unique_ptr<Ui::EntrypointWidget> ui;

    void setScrollMode();
};

#endif // ENTRYPOINTWIDGET_H
