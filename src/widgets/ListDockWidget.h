#ifndef LISTDOCKWIDGET_H
#define LISTDOCKWIDGET_H

#include <memory>
#include <QAbstractItemModel>
#include <QMenu>
#include <QSortFilterProxyModel>

#include "IaitoDockWidget.h"
#include "IaitoTreeWidget.h"
#include "common/AddressableItemModel.h"
#include "core/Iaito.h"
#include "menus/AddressableItemContextMenu.h"

class MainWindow;
class QTreeWidgetItem;
class CommentsWidget;

namespace Ui {
class ListDockWidget;
}

class IAITO_EXPORT ListDockWidget : public IaitoDockWidget
{
    Q_OBJECT

public:
    enum class SearchBarPolicy {
        ShowByDefault,
        HideByDefault,
        Hide,
    };

    explicit ListDockWidget(
        MainWindow *main, SearchBarPolicy searchBarPolicy = SearchBarPolicy::ShowByDefault);
    ~ListDockWidget() override;

    void showCount(bool show);

protected:
    void setModels(AddressableFilterProxyModel *objectFilterProxyModel);

    std::unique_ptr<Ui::ListDockWidget> ui;

private:
    AddressableFilterProxyModel *objectFilterProxyModel = nullptr;
    IaitoTreeWidget *tree;
    SearchBarPolicy searchBarPolicy;
};

#endif // LISTDOCKWIDGET_H
