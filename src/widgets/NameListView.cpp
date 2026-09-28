#include "NameListView.h"
#include "common/NameListModel.h"

#include <QAction>
#include <QHeaderView>
#include <QMenu>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QTreeView>

NameListView::NameListView(
    QTreeView *view,
    QMenu *menu,
    AddressableItemModelI *source,
    int nameColumn,
    const QString &settingsKey)
    : QObject(view)
    , view(view)
    , model(new NameListModel(source, nameColumn, this))
    , nameColumn(nameColumn)
    , flatNamePosition(view->header()->visualIndex(nameColumn))
    , flatIndentation(view->indentation())
    , settingsKey(QStringLiteral("listNames/") + settingsKey)
{
    view->setModel(model);
    menu->addSeparator();
    auto compact = menu->addAction(tr("Compact names"));
    compact->setObjectName(QStringLiteral("actionCompactNames"));
    compact->setCheckable(true);
    compact->setToolTip(tr("Replace a repeated name prefix with \"\". Hover to see the full name."));
    auto hierarchy = menu->addAction(tr("Name hierarchy"));
    hierarchyAction = hierarchy;
    hierarchy->setObjectName(QStringLiteral("actionNameHierarchy"));
    hierarchy->setCheckable(true);
    auto expand = menu->addAction(tr("Expand all"), view, &QTreeView::expandAll);
    auto collapse = menu->addAction(tr("Collapse all"), view, &QTreeView::collapseAll);
    for (auto action : {compact, hierarchy, expand, collapse}) {
        action->setProperty("addressIndependent", true);
    }

    QSettings settings;
    compact->setChecked(settings.value(this->settingsKey + "/compact", false).toBool());
    hierarchy->setChecked(settings.value(this->settingsKey + "/hierarchy", false).toBool());
    auto updateToggle = [hierarchy] {
        hierarchy->setIcon(QIcon(
            hierarchy->isChecked() ? ":/img/icons/name-list.svg" : ":/img/icons/name-tree.svg"));
        hierarchy->setToolTip(
            hierarchy->isChecked() ? tr("Switch to flat list") : tr("Switch to name hierarchy"));
    };
    updateToggle();
    connect(hierarchy, &QAction::toggled, this, updateToggle);
    model->setCompact(compact->isChecked());
    model->setHierarchy(hierarchy->isChecked());
    expand->setVisible(hierarchy->isChecked());
    collapse->setVisible(hierarchy->isChecked());
    updateHierarchy();
    if (compact->isChecked() || hierarchy->isChecked()) {
        view->setColumnWidth(nameColumn, 360);
    }

    connect(compact, &QAction::toggled, this, [this](bool enabled) {
        model->setCompact(enabled);
        QSettings().setValue(this->settingsKey + "/compact", enabled);
        if (enabled) {
            this->view->setColumnWidth(this->nameColumn, 360);
        }
    });
    connect(hierarchy, &QAction::toggled, this, [this, expand, collapse](bool enabled) {
        if (enabled) {
            flatNamePosition = this->view->header()->visualIndex(this->nameColumn);
            flatIndentation = this->view->indentation();
        }
        model->setHierarchy(enabled);
        updateHierarchy();
        expand->setVisible(enabled);
        collapse->setVisible(enabled);
        QSettings().setValue(this->settingsKey + "/hierarchy", enabled);
        if (enabled) {
            this->view->setColumnWidth(this->nameColumn, 360);
        }
    });
    connect(view, &QTreeView::expanded, this, [this](const QModelIndex &index) {
        const QString path = index.data(NameListModel::GroupPathRole).toString();
        if (!restoring && !path.isEmpty()) {
            expandedGroups.insert(path);
        }
    });
    connect(view, &QTreeView::collapsed, this, [this](const QModelIndex &index) {
        if (!restoring) {
            expandedGroups.remove(index.data(NameListModel::GroupPathRole).toString());
        }
    });
    connect(model, &QAbstractItemModel::modelAboutToBeReset, this, [this] {
        currentSource = model->mapToSource(this->view->currentIndex());
    });
    connect(model, &QAbstractItemModel::modelReset, this, &NameListView::restoreView);
}

void NameListView::updateHierarchy()
{
    const int position = view->header()->visualIndex(nameColumn);
    view->header()->moveSection(position, model->isHierarchy() ? 0 : flatNamePosition);
    view->setTreePosition(model->isHierarchy() ? nameColumn : 0);
    view->setIndentation(model->isHierarchy() ? 20 : flatIndentation);
}

void NameListView::restoreView()
{
    restoring = true;
    if (model->isHierarchy()) {
        bool filtered = false;
        if (auto source = qobject_cast<QSortFilterProxyModel *>(model->sourceModel())) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            filtered = !source->filterRegularExpression().pattern().isEmpty();
#else
            filtered = !source->filterRegExp().pattern().isEmpty();
#endif
        }
        if (filtered) {
            view->expandAll();
        } else {
            for (const QString &path : expandedGroups) {
                view->setExpanded(model->groupIndex(path), true);
            }
        }
    }
    auto current = model->mapFromSource(currentSource);
    if (current.isValid()) {
        for (auto parent = current.parent(); parent.isValid(); parent = parent.parent()) {
            view->setExpanded(parent, true);
        }
        view->setCurrentIndex(current);
        view->scrollTo(current);
    }
    restoring = false;
}
