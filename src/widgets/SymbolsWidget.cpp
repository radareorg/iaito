#include "SymbolsWidget.h"
#include "common/Helpers.h"
#include "common/NameListModel.h"
#include "core/MainWindow.h"
#include "ui_ListDockWidget.h"

#include <QShortcut>
#include <QSignalBlocker>

SymbolsModel::SymbolsModel(QList<SymbolDescription> *symbols, QObject *parent)
    : AddressableItemModel<QAbstractListModel>(parent)
    , symbols(symbols)
{}

int SymbolsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : symbols->count();
}

int SymbolsModel::columnCount(const QModelIndex &) const
{
    return SymbolsModel::ColumnCount;
}

QVariant SymbolsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= symbols->count()) {
        return QVariant();
    }

    const SymbolDescription &symbol = symbols->at(index.row());

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case SymbolsModel::AddressColumn:
            return RAddressString(symbol.vaddr);
        case SymbolsModel::TypeColumn:
            return QStringLiteral("%1 %2").arg(symbol.bind, symbol.type).trimmed();
        case SymbolsModel::NameColumn:
            return name(index);
        case SymbolsModel::CommentColumn:
            return comment(index);
        default:
            return QVariant();
        }
    case SymbolsModel::SymbolDescriptionRole:
        return QVariant::fromValue(symbol);
    case Qt::ToolTipRole:
    case NameListModel::NameToolTipRole:
        if (index.column() == NameColumn) {
            if (!symbol.demangledName.isEmpty() && symbol.demangledName != symbol.name) {
                return tr("<qt><b>Demangled:</b> %1<br><b>Mangled:</b> %2</qt>")
                    .arg(symbol.demangledName.toHtmlEscaped(), symbol.name.toHtmlEscaped());
            }
            return QStringLiteral("<qt>%1</qt>").arg(symbol.name.toHtmlEscaped());
        }
        return {};
    default:
        return QVariant();
    }
}

QVariant SymbolsModel::headerData(int section, Qt::Orientation, int role) const
{
    switch (role) {
    case Qt::DisplayRole:
        switch (section) {
        case SymbolsModel::AddressColumn:
            return tr("Address");
        case SymbolsModel::TypeColumn:
            return tr("Type");
        case SymbolsModel::NameColumn:
            return tr("Name");
        case SymbolsModel::CommentColumn:
            return tr("Comment");
        default:
            return QVariant();
        }
    default:
        return QVariant();
    }
}

RVA SymbolsModel::address(const QModelIndex &index) const
{
    const SymbolDescription &symbol = symbols->at(index.row());
    return symbol.vaddr;
}

QString SymbolsModel::name(const QModelIndex &index) const
{
    const SymbolDescription &symbol = symbols->at(index.row());
    return demangled && !symbol.demangledName.isEmpty() ? symbol.demangledName : symbol.name;
}

QString SymbolsModel::comment(const QModelIndex &index) const
{
    const SymbolDescription &symbol = symbols->at(index.row());
    const QString comment = Core()->getCommentAt(symbol.vaddr);
    // r2 adds the demangled spelling as metadata when loading symbols. Show it in the name
    // tooltip instead; leave the metadata itself (and any actual user comment) untouched.
    return comment == symbol.demangledName ? QString() : comment;
}

void SymbolsModel::setDemangled(bool enabled)
{
    if (demangled == enabled) {
        return;
    }
    demangled = enabled;
    if (!symbols->isEmpty()) {
        emit dataChanged(index(0, NameColumn), index(symbols->size() - 1, NameColumn));
    }
}

SymbolsProxyModel::SymbolsProxyModel(SymbolsModel *sourceModel, QObject *parent)
    : AddressableFilterProxyModel(sourceModel, parent)
{
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    setSortCaseSensitivity(Qt::CaseInsensitive);
}

bool SymbolsProxyModel::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    if (!filterAcceptsType(row, parent)) {
        return false;
    }
    QModelIndex index = sourceModel()->index(row, 0, parent);
    auto symbol = index.data(SymbolsModel::SymbolDescriptionRole).value<SymbolDescription>();
    if (symbol.name.contains(FILTER_REGEX) || symbol.demangledName.contains(FILTER_REGEX)) {
        return true;
    }
    return index.sibling(row, SymbolsModel::CommentColumn).data().toString().contains(FILTER_REGEX);
}

bool SymbolsProxyModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    auto leftSymbol = left.data(SymbolsModel::SymbolDescriptionRole).value<SymbolDescription>();
    auto rightSymbol = right.data(SymbolsModel::SymbolDescriptionRole).value<SymbolDescription>();

    switch (left.column()) {
    case SymbolsModel::AddressColumn:
        return leftSymbol.vaddr < rightSymbol.vaddr;
    case SymbolsModel::TypeColumn:
        return leftSymbol.type < rightSymbol.type;
    case SymbolsModel::NameColumn:
    case SymbolsModel::CommentColumn:
        return left.data().toString() < right.data().toString();
    default:
        break;
    }

    return false;
}

SymbolsWidget::SymbolsWidget(MainWindow *main)
    : ListDockWidget(main)
{
    setWindowTitle(tr("Symbols"));
    setObjectName("SymbolsWidget");

    symbolsModel = new SymbolsModel(&symbols, this);
    symbolsProxyModel = new SymbolsProxyModel(symbolsModel, this);
    setModels(symbolsProxyModel, SymbolsModel::NameColumn, SymbolsModel::TypeColumn);
    ui->treeView->sortByColumn(SymbolsModel::AddressColumn, Qt::AscendingOrder);

    demangledAction = ui->treeView->getItemContextMenu()->addAction(tr("Demangled names"));
    demangledAction->setObjectName(QStringLiteral("actionDemangledNames"));
    demangledAction->setCheckable(true);
    demangledAction->setChecked(Core()->getConfigb("bin.demangle"));
    demangledAction->setProperty("addressIndependent", true);
    symbolsModel->setDemangled(demangledAction->isChecked());
    connect(demangledAction, &QAction::toggled, this, [this](bool enabled) {
        nameDisplayOverridden = true;
        symbolsModel->setDemangled(enabled);
    });

    connect(Core(), &IaitoCore::codeRebased, this, &SymbolsWidget::refreshSymbols);
    connect(Core(), &IaitoCore::refreshAll, this, &SymbolsWidget::refreshSymbols);
    connect(Core(), &IaitoCore::commentsChanged, this, [this]() {
        qhelpers::emitColumnChanged(symbolsModel, SymbolsModel::CommentColumn);
    });
}

SymbolsWidget::~SymbolsWidget()
{
    delete symbolsProxyModel;
    delete symbolsModel;
}

void SymbolsWidget::refreshSymbols()
{
    if (!nameDisplayOverridden) {
        const QSignalBlocker blocker(demangledAction);
        demangledAction->setChecked(Core()->getConfigb("bin.demangle"));
        symbolsModel->setDemangled(demangledAction->isChecked());
    }
    symbolsModel->beginResetModel();
    symbols = Core()->getAllSymbols();
    symbolsModel->endResetModel();

    qhelpers::adjustColumns(ui->treeView, SymbolsModel::ColumnCount, 0);
}
