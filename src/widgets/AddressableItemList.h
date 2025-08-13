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

    // Avoid hiding the base class overloads of setModel(QAbstractItemModel*)
    using BaseListWidget::setModel;

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
        auto provider = addressProvider();
        if (!provider) {
            return;
        }
        auto index = this->currentIndex();
        if (index.isValid() && itemContextMenu) {
            QModelIndex pIndex = mapToProviderIndex(provider, index);
            if (!pIndex.isValid()) {
                return;
            }
            auto offset = provider->address(pIndex);
            auto name = provider->name(pIndex);
            itemContextMenu->setTarget(offset, name);
            itemContextMenu->exec(this->mapToGlobal(pt));
        }
    }

    virtual void onItemActivated(const QModelIndex &index)
    {
        if (!index.isValid())
            return;

        auto provider = addressProvider();
        if (!provider) {
            return;
        }
        QModelIndex pIndex = mapToProviderIndex(provider, index);
        if (!pIndex.isValid()) {
            return;
        }
        auto offset = provider->address(pIndex);
        Core()->seekAndShow(offset);
    }
    virtual void onSelectedItemChanged(const QModelIndex &index) { updateMenuFromItem(index); }
    void updateMenuFromItem(const QModelIndex &index)
    {
        auto provider = addressProvider();
        if (!provider) {
            return;
        }
        if (index.isValid()) {
            QModelIndex pIndex = mapToProviderIndex(provider, index);
            if (!pIndex.isValid()) {
                return;
            }
            auto offset = provider->address(pIndex);
            auto name = provider->name(pIndex);
            itemContextMenu->setTarget(offset, name);
        } else {
            itemContextMenu->clearTarget();
        }
    }

private:
    AddressableItemModelI *addressableModel = nullptr;
    AddressableItemContextMenu *itemContextMenu = nullptr;
    MainWindow *mainWindow = nullptr;

    AddressableItemModelI *addressProvider() const
    {
        // Prefer the current view model if it implements AddressableItemModelI
        if (auto m = BaseListWidget::model()) {
            if (auto aim = dynamic_cast<AddressableItemModelI *>(m)) {
                return aim;
            }
            // Walk proxy chain to find nearest AddressableItemModelI
            auto cur = m;
            while (auto proxy = qobject_cast<QSortFilterProxyModel *>(cur)) {
                if (auto aim = dynamic_cast<AddressableItemModelI *>(proxy)) {
                    return aim; // AddressableFilterProxyModel
                }
                cur = proxy->sourceModel();
                if (!cur)
                    break;
                if (auto aim2 = dynamic_cast<AddressableItemModelI *>(cur)) {
                    return aim2;
                }
            }
        }
        // Fallback to stored model (may be null)
        return addressableModel;
    }

    QModelIndex mapToProviderIndex(AddressableItemModelI *provider, const QModelIndex &index) const
    {
        if (!provider) {
            return {};
        }
        QAbstractItemModel *target = provider->asItemModel();
        QAbstractItemModel *curModel = BaseListWidget::model();
        QModelIndex curIndex = index;
        // Map down through any proxy chain until we reach provider
        while (curModel && curModel != target) {
            auto proxy = qobject_cast<QSortFilterProxyModel *>(curModel);
            if (!proxy) {
                // Models don't match and we can't map further
                return {};
            }
            curIndex = proxy->mapToSource(curIndex);
            curModel = proxy->sourceModel();
        }
        if (curModel != target) {
            return {};
        }
        return curIndex;
    }
};

#endif // ADDRESSABLE_ITEM_LIST_H
