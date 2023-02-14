#include "ImportsWidget.h"
#include "ui_ListDockWidget.h"
#include "WidgetShortcuts.h"
#include "core/MainWindow.h"
#include "common/Helpers.h"

/*
#include <QPainter>
#include <QPen>
#include <QShortcut>
#include <QTreeWidget>
*/

ImportsModel::ImportsModel(QList<ImportDescription> *imp, QObject *parent)
    : AddressableItemModel<QAbstractListModel>(parent),
      imports(imp)
{}

int ImportsModel::rowCount(const QModelIndex &parent) const
{
	return !parent.isValid() ? imports->count() : 0;
	//return imports != NULL ? imports->count() : 0;
}

int ImportsModel::columnCount(const QModelIndex &) const
{
    return ImportsModel::ColumnCount;
}

QVariant ImportsModel::data(const QModelIndex &index, int role) const
{
    const ImportDescription &imp = imports->at(index.row());
eprintf ("IMPORT DATA");
    switch (role) {
    case Qt::ForegroundRole:
        if (index.column() < ImportsModel::ColumnCount) {
            // Red color for unsafe functions
            if (banned.match(imp.name).hasMatch())
                return Config()->getColor("gui.item_unsafe");
            // Grey color for symbols at offset 0 which can only be filled at runtime
            if (imp.plt == 0)
                return Config()->getColor("gui.item_invalid");
        }
        break;
    case Qt::DisplayRole:
        switch (index.column()) {
        case ImportsModel::AddressColumn:
            return RAddressString(imp.plt);
        case ImportsModel::TypeColumn:
            return imp.type;
        case ImportsModel::SafetyColumn:
            return banned.match(imp.name).hasMatch() ? tr("Unsafe") : QStringLiteral("");
        case ImportsModel::LibraryColumn:
            return imp.libname;
        case ImportsModel::NameColumn:
            return imp.name;
        case ImportsModel::CommentColumn:
            return Core()->getCommentAt(imp.plt);
        default:
            break;
        }
        break;
    case ImportsModel::ImportDescriptionRole:
        return QVariant::fromValue(imp);
    case ImportsModel::AddressRole:
        return QVariant::fromValue(imp.plt);
    default:
        break;
    }
    return QVariant();
}

QVariant ImportsModel::headerData(int section, Qt::Orientation, int role) const
{
    if (role == Qt::DisplayRole) {
        switch (section) {
        case ImportsModel::AddressColumn:
            return tr("Address");
        case ImportsModel::TypeColumn:
            return tr("Type");
        case ImportsModel::SafetyColumn:
            return tr("Safety");
        case ImportsModel::LibraryColumn:
            return tr("Library");
        case ImportsModel::NameColumn:
            return tr("Name");
        case ImportsModel::CommentColumn:
            return tr("Comment");
        default:
            break;
        }
    }
    return QVariant();
}

RVA ImportsModel::address(const QModelIndex &index) const
{
    const ImportDescription &import = imports->at(index.row());
    return import.plt;
}

QString ImportsModel::name(const QModelIndex &index) const
{
    const ImportDescription &import = imports->at(index.row());
    return import.name;
}

QString ImportsModel::libname(const QModelIndex &index) const
{
    const ImportDescription &import = imports->at(index.row());
    return import.libname;
}

ImportsProxyModel::ImportsProxyModel(ImportsModel *sourceModel, QObject *parent)
    : AddressableFilterProxyModel(sourceModel, parent)
{
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    setSortCaseSensitivity(Qt::CaseInsensitive);
}

bool ImportsProxyModel::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    QModelIndex index = sourceModel()->index(row, 0, parent);
    auto import = index.data(ImportsModel::ImportDescriptionRole).value<ImportDescription>();

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return true;
#else
    return import.name.contains(filterRegExp());
#endif
}

bool ImportsProxyModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    if (!left.isValid() || !right.isValid())
        return false;

    if (left.parent().isValid() || right.parent().isValid())
        return false;

    auto leftImport = left.data(ImportsModel::ImportDescriptionRole).value<ImportDescription>();
    auto rightImport = right.data(ImportsModel::ImportDescriptionRole).value<ImportDescription>();

    switch (left.column()) {
    case ImportsModel::AddressColumn:
        return leftImport.plt < rightImport.plt;
    case ImportsModel::TypeColumn:
        return leftImport.type < rightImport.type;
    case ImportsModel::SafetyColumn:
        break;
    case ImportsModel::LibraryColumn:
        if (leftImport.libname != rightImport.libname)
            return leftImport.libname < rightImport.libname;
        // Fallthrough. Sort by Library and then by import name
        return leftImport.name < rightImport.name;
    case ImportsModel::NameColumn:
        return leftImport.name < rightImport.name;
    case ImportsModel::CommentColumn:
        return Core()->getCommentAt(leftImport.plt) < Core()->getCommentAt(rightImport.plt);

    default:
        break;
    }

    return false;
}

/*
 * Imports Widget
 */

ImportsWidget::ImportsWidget(MainWindow *main) :
    ListDockWidget(main)
    //, importsModel(new ImportsModel(&imports, this)),
    //importsProxyModel(new ImportsProxyModel(importsModel, this))
{
    setWindowTitle(tr("Imports"));
    setObjectName("ImportsWidget");

    importsModel = new ImportsModel(&imports, this);
    importsProxyModel = new ImportsProxyModel(importsModel, this);
    setModels(importsProxyModel);
    // Sort by library name by default to create a solid context per each group of imports
    ui->treeView->sortByColumn(ImportsModel::LibraryColumn, Qt::AscendingOrder);

    QShortcut *toggle_shortcut = new QShortcut(widgetShortcuts["ImportsWidget"], main);
    connect(toggle_shortcut, &QShortcut::activated, this, [=] (){ 
            toggleDockWidget(true);
    } );

    connect(Core(), &IaitoCore::codeRebased, this, &ImportsWidget::refreshImports);
    connect(Core(), &IaitoCore::refreshAll, this, &ImportsWidget::refreshImports);
/*
    connect(Core(), &IaitoCore::commentsChanged, this, [this]() {
        qhelpers::emitColumnChanged(importsModel, ImportsModel::CommentColumn);
    });
*/
}

ImportsWidget::~ImportsWidget() {}

void ImportsWidget::refreshImports()
{
    importsModel->beginResetModel();
    imports = Core()->getAllImports();
    importsModel->endResetModel();
    qhelpers::adjustColumns(ui->treeView, ImportsModel::ColumnCount, 0);
}
