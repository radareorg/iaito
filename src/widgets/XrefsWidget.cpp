#include "XrefsWidget.h"
#include "common/Helpers.h"
#include "core/MainWindow.h"
#include "ui_ListDockWidget.h"

static QString xrefTypeString(const QString &type)
{
    if (type == "CODE") {
        return QStringLiteral("Code");
    } else if (type == "CALL") {
        return QStringLiteral("Call");
    } else if (type == "DATA") {
        return QStringLiteral("Data");
    } else if (type == "STRING") {
        return QStringLiteral("String");
    }
    return type;
}

XrefsListModel::XrefsListModel(QList<XrefDescription> *xrefs, QObject *parent)
    : AddressableItemModel<QAbstractListModel>(parent)
    , xrefs(xrefs)
{}

int XrefsListModel::rowCount(const QModelIndex &) const
{
    return xrefs->count();
}

int XrefsListModel::columnCount(const QModelIndex &) const
{
    return XrefsListModel::ColumnCount;
}

QVariant XrefsListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= xrefs->count()) {
        return QVariant();
    }

    const XrefDescription &xref = xrefs->at(index.row());

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case AddressColumn:
            return xref.from_str;
        case TypeColumn:
            return xrefTypeString(xref.type);
        case FunctionColumn:
            return Core()->cmdFunctionAt(xref.from);
        case CodeColumn:
            return Core()->disassembleSingleInstruction(xref.from);
        case CommentColumn:
            return Core()->getCommentAt(xref.from);
        default:
            return QVariant();
        }
    case XrefDescriptionRole:
        return QVariant::fromValue(xref);
    default:
        return QVariant();
    }
}

QVariant XrefsListModel::headerData(int section, Qt::Orientation, int role) const
{
    switch (role) {
    case Qt::DisplayRole:
        switch (section) {
        case AddressColumn:
            return tr("From");
        case TypeColumn:
            return tr("Type");
        case FunctionColumn:
            return tr("Function");
        case CodeColumn:
            return tr("Code");
        case CommentColumn:
            return tr("Comment");
        default:
            return QVariant();
        }
    default:
        return QVariant();
    }
}

RVA XrefsListModel::address(const QModelIndex &index) const
{
    return xrefs->at(index.row()).from;
}

XrefsProxyModel::XrefsProxyModel(XrefsListModel *sourceModel, QObject *parent)
    : AddressableFilterProxyModel(sourceModel, parent)
{
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    setSortCaseSensitivity(Qt::CaseInsensitive);
}

bool XrefsProxyModel::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    QModelIndex index = sourceModel()->index(row, 0, parent);
    auto xref = index.data(XrefsListModel::XrefDescriptionRole).value<XrefDescription>();
    if (xref.from_str.contains(FILTER_REGEX) || xref.type.contains(FILTER_REGEX)) {
        return true;
    }
    if (Core()->cmdFunctionAt(xref.from).contains(FILTER_REGEX)) {
        return true;
    }
    return Core()->getCommentAt(xref.from).contains(FILTER_REGEX);
}

bool XrefsProxyModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    auto l = left.data(XrefsListModel::XrefDescriptionRole).value<XrefDescription>();
    auto r = right.data(XrefsListModel::XrefDescriptionRole).value<XrefDescription>();

    switch (left.column()) {
    case XrefsListModel::AddressColumn:
        return l.from < r.from;
    case XrefsListModel::TypeColumn:
        return l.type < r.type;
    case XrefsListModel::FunctionColumn:
        return Core()->cmdFunctionAt(l.from) < Core()->cmdFunctionAt(r.from);
    case XrefsListModel::CommentColumn:
        return Core()->getCommentAt(l.from) < Core()->getCommentAt(r.from);
    default:
        break;
    }
    return false;
}

XrefsWidget::XrefsWidget(MainWindow *main)
    : ListDockWidget(main)
{
    setWindowTitle(tr("X-Refs"));
    setObjectName("XrefsWidget");
    setStatusBarSizeGripEnabled(false);

    xrefsModel = new XrefsListModel(&xrefs, this);
    xrefsProxyModel = new XrefsProxyModel(xrefsModel, this);
    setModels(xrefsProxyModel);
    ui->treeView->sortByColumn(XrefsListModel::AddressColumn, Qt::AscendingOrder);

    connect(Core(), &IaitoCore::seekChanged, this, &XrefsWidget::refreshXrefs);
    connect(Core(), &IaitoCore::codeRebased, this, &XrefsWidget::refreshXrefs);
    connect(Core(), &IaitoCore::refreshAll, this, &XrefsWidget::refreshXrefs);
    connect(Core(), &IaitoCore::commentsChanged, this, [this]() {
        qhelpers::emitColumnChanged(xrefsModel, XrefsListModel::CommentColumn);
    });
}

XrefsWidget::~XrefsWidget()
{
    delete xrefsProxyModel;
    delete xrefsModel;
}

void XrefsWidget::refreshXrefs()
{
    xrefsModel->beginResetModel();
    xrefs = Core()->getXRefs(Core()->getOffset(), true, true);
    xrefsModel->endResetModel();

    qhelpers::adjustColumns(ui->treeView, XrefsListModel::ColumnCount, 0);
}
