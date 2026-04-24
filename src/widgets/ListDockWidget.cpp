#include "ListDockWidget.h"
#include "common/Helpers.h"
#include "core/MainWindow.h"
#include "menus/AddressableItemContextMenu.h"
#include "ui_ListDockWidget.h"

#include <QFont>
#include <QMenu>
#include <QResizeEvent>
#include <QShortcut>

ListDockWidget::ListDockWidget(MainWindow *main, SearchBarPolicy searchBarPolicy)
    : IaitoDockWidget(main)
    , ui(new Ui::ListDockWidget)
    , tree(new IaitoTreeWidget(this))
    , searchBarPolicy(searchBarPolicy)
{
    ui->setupUi(this);
    qhelpers::bindFont(ui->treeView, false, true);

    // Add Status Bar footer
    tree->addStatusBar(ui->verticalLayout);

    if (searchBarPolicy != SearchBarPolicy::Hide) {
        // Ctrl-F to show/hide the filter entry
        QShortcut *searchShortcut = new QShortcut(QKeySequence::Find, this);
        connect(
            searchShortcut,
            &QShortcut::activated,
            ui->quickFilterView,
            &QuickFilterView::showFilter);
        searchShortcut->setContext(Qt::WidgetWithChildrenShortcut);

        // Esc to clear the filter entry
        QShortcut *clearShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
        connect(clearShortcut, &QShortcut::activated, [this]() {
            ui->quickFilterView->clearFilter();
            ui->treeView->setFocus();
        });
        clearShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    }

    qhelpers::setVerticalScrollMode(ui->treeView);

    ui->treeView->setMainWindow(mainWindow);

    if (searchBarPolicy != SearchBarPolicy::ShowByDefault) {
        ui->quickFilterView->closeFilter();
    }
}

ListDockWidget::~ListDockWidget()
{
    // Detach the view from its model early to avoid Qt asking the model
    // for data during widget teardown after our backing data members
    // have already been destroyed in derived classes.
    // The static_cast disambiguates against
    // AddressableItemList::setModel(AddressableItemModelI *).
    if (ui && ui->treeView) {
        ui->treeView->setModel(static_cast<QAbstractItemModel *>(nullptr));
    }
}

void ListDockWidget::showCount(bool show)
{
    tree->showStatusBar(show);
}

void ListDockWidget::setStatusBarSizeGripEnabled(bool enabled)
{
    tree->setStatusBarSizeGripEnabled(enabled);
}

void ListDockWidget::setModels(AddressableFilterProxyModel *objectFilterProxyModel)
{
    this->objectFilterProxyModel = objectFilterProxyModel;

    // AddressableFilterProxyModel inherits both AddressableItemModelI and
    // QSortFilterProxyModel, so pin the QAbstractItemModel overload
    // explicitly against AddressableItemList::setModel(AddressableItemModelI *).
    ui->treeView->setModel(static_cast<QAbstractItemModel *>(objectFilterProxyModel));

    connect(
        ui->quickFilterView,
        &QuickFilterView::filterTextChanged,
        objectFilterProxyModel,
        &QSortFilterProxyModel::setFilterWildcard);
    connect(
        ui->quickFilterView,
        &QuickFilterView::filterClosed,
        ui->treeView,
        static_cast<void (QWidget::*)()>(&QWidget::setFocus));

    connect(ui->quickFilterView, &QuickFilterView::filterTextChanged, this, [this] {
        tree->showItemsNumber(this->objectFilterProxyModel->rowCount());
    });
}
