#include "RelocsWidget.h"
#include "ui_ListDockWidget.h"
#include "core/MainWindow.h"
#include "common/Helpers.h"

#include <QShortcut>
#include <QTreeWidget>

RelocsModel::RelocsModel(QList<RelocDescription> *relocs, QObject *parent) :
    AddressableItemModel<QAbstractTableModel>(parent),
    relocs(relocs)
{}

int RelocsModel::rowCount(R_UNUSED const QModelIndex &parent) const
{
    return parent.isValid() ? relocs->count() : 0;
}

int RelocsModel::columnCount(const QModelIndex &) const
{
    return RelocsModel::ColumnCount;
}

QVariant RelocsModel::data(const QModelIndex &index, int role) const
{
    const RelocDescription &reloc = relocs->at(index.row());
    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case RelocsModel::VAddrColumn:
            return RAddressString(reloc.vaddr);
        case RelocsModel::TypeColumn:
            return reloc.type;
        case RelocsModel::NameColumn:
            return reloc.name;
        case RelocsModel::CommentColumn:
            return Core()->getCommentAt(reloc.vaddr);
        default:
            break;
        }
        break;
    case RelocsModel::RelocDescriptionRole:
        return QVariant::fromValue(reloc);
    case RelocsModel::AddressRole:
        return QVariant::fromValue(reloc.vaddr);
    default:
        break;
    }
    return QVariant();
}

QVariant RelocsModel::headerData(int section, Qt::Orientation, int role) const
{
    if (role == Qt::DisplayRole)
        switch (section) {
        case RelocsModel::VAddrColumn:
            return tr("Address");
        case RelocsModel::TypeColumn:
            return tr("Type");
        case RelocsModel::NameColumn:
            return tr("Name");
        case RelocsModel::CommentColumn:
            return tr("Comment");
        }
    return QVariant();
}

RVA RelocsModel::address(const QModelIndex &index) const
{
    const RelocDescription &reloc = relocs->at(index.row());
    return reloc.vaddr;
}

QString RelocsModel::name(const QModelIndex &index) const
{
    const RelocDescription &reloc = relocs->at(index.row());
    return reloc.name;
}

RelocsProxyModel::RelocsProxyModel(RelocsModel *sourceModel, QObject *parent)
    : AddressableFilterProxyModel(sourceModel, parent)
{
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    setSortCaseSensitivity(Qt::CaseInsensitive);
}

bool RelocsProxyModel::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    QModelIndex index = sourceModel()->index(row, 0, parent);
    auto reloc = index.data(RelocsModel::RelocDescriptionRole).value<RelocDescription>();

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return true;
#else
    return reloc.name.contains(filterRegExp());
#endif
}

bool RelocsProxyModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    if (!left.isValid() || !right.isValid())
        return false;

    if (left.parent().isValid() || right.parent().isValid())
        return false;

    auto leftReloc = left.data(RelocsModel::RelocDescriptionRole).value<RelocDescription>();
    auto rightReloc = right.data(RelocsModel::RelocDescriptionRole).value<RelocDescription>();

    switch (left.column()) {
    case RelocsModel::VAddrColumn:
        return leftReloc.vaddr < rightReloc.vaddr;
    case RelocsModel::TypeColumn:
        return leftReloc.type < rightReloc.type;
    case RelocsModel::NameColumn:
        return leftReloc.name < rightReloc.name;
    case RelocsModel::CommentColumn:
        return Core()->getCommentAt(leftReloc.vaddr) < Core()->getCommentAt(rightReloc.vaddr);
    default:
        break;
    }

    return false;
}

RelocsWidget::RelocsWidget(MainWindow *main) :
    ListDockWidget(main),
    relocsModel(new RelocsModel(&relocs, this)),
    relocsProxyModel(new RelocsProxyModel(relocsModel, this))
{
    setWindowTitle(tr("Relocs"));
    setObjectName("RelocsWidget");

    setModels(relocsProxyModel);
    ui->treeView->sortByColumn(RelocsModel::NameColumn, Qt::AscendingOrder);

    connect(Core(), &IaitoCore::codeRebased, this, &RelocsWidget::refreshRelocs);
    connect(Core(), &IaitoCore::refreshAll, this, &RelocsWidget::refreshRelocs);
    connect(Core(), &IaitoCore::commentsChanged, this, [this]() {
        qhelpers::emitColumnChanged(relocsModel, RelocsModel::CommentColumn);
    });
}

RelocsWidget::~RelocsWidget() {}

void RelocsWidget::refreshRelocs()
{
    relocsModel->beginResetModel();
    relocs = Core()->getAllRelocs();
    relocsModel->endResetModel();
    qhelpers::adjustColumns(ui->treeView, 3, 0);
}
