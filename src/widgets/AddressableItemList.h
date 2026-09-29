#ifndef ADDRESSABLE_ITEM_LIST_H
#define ADDRESSABLE_ITEM_LIST_H

#include <memory>
#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QMenu>
#include <QSet>
#include <QSortFilterProxyModel>

#include "IaitoDockWidget.h"
#include "IaitoTreeView.h"
#include "IaitoTreeWidget.h"
#include "common/AddressableItemModel.h"
#include "core/Iaito.h"
#include "menus/AddressableItemContextMenu.h"
#include <QItemSelectionModel>
#include <QKeyEvent>

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

    void setAddressModel(AddressableItemModelI *model)
    {
        this->addressableModel = model;
        setModel(model ? model->asItemModel() : nullptr);
    }

    void setModel(QAbstractItemModel *model) override
    {
        BaseListWidget::setModel(model);
        if (this->selectionModel()) {
            this->connect(
                this->selectionModel(),
                &QItemSelectionModel::currentChanged,
                this,
                &AddressableItemList<BaseListWidget>::onSelectedItemChanged);
        }
    }

    RVA itemAddress(const QModelIndex &index) const
    {
        auto provider = addressProvider();
        auto source = mapToProviderIndex(provider, index);
        return source.isValid() ? provider->address(source) : RVA_INVALID;
    }

    QList<RVA> selectedAddresses() const
    {
        QList<RVA> result;
        QSet<RVA> seen;
        if (auto selection = this->selectionModel()) {
            for (const auto &index : selection->selectedIndexes()) {
                const RVA address = itemAddress(index);
                if (address != RVA_INVALID && !seen.contains(address)) {
                    seen.insert(address);
                    result.append(address);
                }
            }
        }
        return result;
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
        if (!itemContextMenu) {
            return;
        }
        const auto index = this->indexAt(pt);
        if (this->selectionModel()->isSelected(index)) {
            this->selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
        } else {
            this->setCurrentIndex(index);
        }
        updateMenuFromItem(index);
        itemContextMenu->exec(this->viewport()->mapToGlobal(pt));
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
        if (offset != RVA_INVALID) {
            Core()->seekAndShow(offset);
        }
    }
    virtual void onSelectedItemChanged(const QModelIndex &index) { updateMenuFromItem(index); }
    void updateMenuFromItem(const QModelIndex &index)
    {
        auto provider = addressProvider();
        if (!provider || !itemContextMenu) {
            return;
        }
        if (index.isValid()) {
            QModelIndex pIndex = mapToProviderIndex(provider, index);
            if (!pIndex.isValid() || provider->address(pIndex) == RVA_INVALID) {
                itemContextMenu->clearTarget();
                return;
            }
            auto offset = provider->address(pIndex);
            auto name = provider->name(pIndex);
            itemContextMenu->setTarget(offset, name);
        } else {
            itemContextMenu->clearTarget();
        }
    }

    // Handle 'j' and 'k' keys to navigate and seek without pressing Enter
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->modifiers() == Qt::NoModifier
            && (event->key() == Qt::Key_J || event->key() == Qt::Key_K)) {
            auto selModel = this->selectionModel();
            auto tree = qobject_cast<QTreeView *>(this);
            if (selModel && tree && this->currentIndex().isValid()) {
                const auto next = event->key() == Qt::Key_J
                                      ? tree->indexBelow(this->currentIndex())
                                      : tree->indexAbove(this->currentIndex());
                if (next.isValid()) {
                    selModel->setCurrentIndex(
                        next, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
                    this->scrollTo(next);
                    onItemActivated(next);
                }
                return;
            }
        }
        BaseListWidget::keyPressEvent(event);
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
