#ifndef ADDRESSABLE_ITEM_LIST_H
#define ADDRESSABLE_ITEM_LIST_H

#include <memory>
#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QMenu>
#include <QSortFilterProxyModel>

#include "IaitoDockWidget.h"
#include "IaitoTreeView.h"
#include "IaitoTreeWidget.h"
#include "common/AddressableItemModel.h"
#include "core/Iaito.h"
#include "menus/AddressableItemContextMenu.h"

class MainWindow;

template<class BaseListWidget = IaitoTreeView>
class AddressableItemList : public BaseListWidget
{
    static_assert(
        std::is_base_of<QAbstractItemView, BaseListWidget>::value,
        "ParentModel needs to inherit from QAbstractItemModel");

public:
    explicit AddressableItemList(QWidget *parent = nullptr)
        : BaseListWidget(parent)
    {
        this->connect(
            this,
            &QWidget::customContextMenuRequested,
            this,
            &AddressableItemList<BaseListWidget>::showItemContextMenu);
        this->setContextMenuPolicy(Qt::CustomContextMenu);
        this->connect(
            this,
            &QAbstractItemView::activated,
            this,
            &AddressableItemList<BaseListWidget>::onItemActivated);
    }

    void setModel(AddressableItemModelI *model)
    {
        this->addressableModel = model;
        BaseListWidget::setModel(this->addressableModel->asItemModel());

        this->connect(
            this->selectionModel(),
            &QItemSelectionModel::currentChanged,
            this,
            &AddressableItemList<BaseListWidget>::onSelectedItemChanged);
    }

#if 0
    void setModel(QAbstractItemModel *model)
    {
        this->connect(this->selectionModel(), &QItemSelectionModel::currentChanged, this,
                      &AddressableItemList<BaseListWidget>::onSelectedItemChanged);
    }
#endif

    void setAddressModel(AddressableItemModelI *model)
    {
        this->addressableModel = model;

        BaseListWidget::setModel(this->addressableModel->asItemModel());

        this->connect(
            this->selectionModel(),
            &QItemSelectionModel::currentChanged,
            this,
            &AddressableItemList<BaseListWidget>::onSelectedItemChanged);
        IaitoTreeView::setModel((QAbstractItemModel *) model);
    }

    void setMainWindow(MainWindow *mainWindow)
    {
        this->mainWindow = mainWindow;
        setItemContextMenu(new AddressableItemContextMenu(this, mainWindow));
        this->addActions(this->getItemContextMenu()->actions());
    }

    AddressableItemContextMenu *getItemContextMenu() { return itemContextMenu; }
    void setItemContextMenu(AddressableItemContextMenu *menu)
    {
        if (itemContextMenu != menu && itemContextMenu) {
            itemContextMenu->deleteLater();
        }
        itemContextMenu = menu;
    }

protected:
    virtual void showItemContextMenu(const QPoint &pt)
    {
        auto index = this->currentIndex();
        if (index.isValid() && itemContextMenu) {
            auto offset = addressableModel->address(index);
            auto name = addressableModel->name(index);
            itemContextMenu->setTarget(offset, name);
            itemContextMenu->exec(this->mapToGlobal(pt));
        }
    }

    virtual void onItemActivated(const QModelIndex &index)
    {
        if (!index.isValid())
            return;

        auto offset = addressableModel->address(index);
        Core()->seekAndShow(offset);
    }
    virtual void onSelectedItemChanged(const QModelIndex &index) { updateMenuFromItem(index); }
    void updateMenuFromItem(const QModelIndex &index)
    {
        if (index.isValid()) {
            auto offset = addressableModel->address(index);
            auto name = addressableModel->name(index);
            itemContextMenu->setTarget(offset, name);
        } else {
            itemContextMenu->clearTarget();
        }
    }

private:
    AddressableItemModelI *addressableModel = nullptr;
    AddressableItemContextMenu *itemContextMenu = nullptr;
    MainWindow *mainWindow = nullptr;
};

#endif // ADDRESSABLE_ITEM_LIST_H
