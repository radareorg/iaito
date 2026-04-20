#include "RefsWidget.h"
#include "common/Helpers.h"
#include "core/MainWindow.h"
#include "ui_ListDockWidget.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

static QString refTypeString(const QString &type)
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

RefsListModel::RefsListModel(QList<XrefDescription> *refs, QObject *parent)
    : AddressableItemModel<QAbstractListModel>(parent)
    , refs(refs)
{}

int RefsListModel::rowCount(const QModelIndex &) const
{
    return refs->count();
}

int RefsListModel::columnCount(const QModelIndex &) const
{
    return RefsListModel::ColumnCount;
}

QVariant RefsListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= refs->count()) {
        return QVariant();
    }

    const XrefDescription &ref = refs->at(index.row());

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case FromColumn:
            return ref.from_str;
        case TypeColumn:
            return refTypeString(ref.type);
        case TargetColumn:
            return ref.to_str;
        case NameColumn:
            return Core()->cmdFunctionAt(ref.to);
        case CodeColumn:
            if (ref.type != "DATA") {
                return Core()->disassembleSingleInstruction(ref.from);
            }
            return QString();
        case CommentColumn:
            return Core()->getCommentAt(ref.to);
        default:
            return QVariant();
        }
    case XrefDescriptionRole:
        return QVariant::fromValue(ref);
    default:
        return QVariant();
    }
}

QVariant RefsListModel::headerData(int section, Qt::Orientation, int role) const
{
    switch (role) {
    case Qt::DisplayRole:
        switch (section) {
        case FromColumn:
            return tr("From");
        case TypeColumn:
            return tr("Type");
        case TargetColumn:
            return tr("To");
        case NameColumn:
            return tr("Name");
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

RVA RefsListModel::address(const QModelIndex &index) const
{
    return refs->at(index.row()).to;
}

RefsProxyModel::RefsProxyModel(RefsListModel *sourceModel, QObject *parent)
    : AddressableFilterProxyModel(sourceModel, parent)
{
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    setSortCaseSensitivity(Qt::CaseInsensitive);
}

bool RefsProxyModel::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    QModelIndex index = sourceModel()->index(row, 0, parent);
    auto ref = index.data(RefsListModel::XrefDescriptionRole).value<XrefDescription>();
    if (ref.from_str.contains(FILTER_REGEX) || ref.to_str.contains(FILTER_REGEX)
        || ref.type.contains(FILTER_REGEX)) {
        return true;
    }
    if (Core()->cmdFunctionAt(ref.to).contains(FILTER_REGEX)) {
        return true;
    }
    return Core()->getCommentAt(ref.to).contains(FILTER_REGEX);
}

bool RefsProxyModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    auto l = left.data(RefsListModel::XrefDescriptionRole).value<XrefDescription>();
    auto r = right.data(RefsListModel::XrefDescriptionRole).value<XrefDescription>();

    switch (left.column()) {
    case RefsListModel::FromColumn:
        return l.from < r.from;
    case RefsListModel::TypeColumn:
        return l.type < r.type;
    case RefsListModel::TargetColumn:
        return l.to < r.to;
    case RefsListModel::NameColumn:
        return Core()->cmdFunctionAt(l.to) < Core()->cmdFunctionAt(r.to);
    case RefsListModel::CommentColumn:
        return Core()->getCommentAt(l.to) < Core()->getCommentAt(r.to);
    default:
        break;
    }
    return false;
}

RefsWidget::RefsWidget(MainWindow *main)
    : ListDockWidget(main)
{
    setWindowTitle(tr("Refs"));
    setObjectName("RefsWidget");

    refsModel = new RefsListModel(&refs, this);
    refsProxyModel = new RefsProxyModel(refsModel, this);
    setModels(refsProxyModel);
    ui->treeView->sortByColumn(RefsListModel::FromColumn, Qt::AscendingOrder);

    connect(Core(), &IaitoCore::seekChanged, this, &RefsWidget::refreshRefs);
    connect(Core(), &IaitoCore::codeRebased, this, &RefsWidget::refreshRefs);
    connect(Core(), &IaitoCore::refreshAll, this, &RefsWidget::refreshRefs);
    connect(Core(), &IaitoCore::commentsChanged, this, [this]() {
        qhelpers::emitColumnChanged(refsModel, RefsListModel::CommentColumn);
    });
}

RefsWidget::~RefsWidget()
{
    delete refsProxyModel;
    delete refsModel;
}

void RefsWidget::refreshRefs()
{
    refsModel->beginResetModel();
    refs.clear();
    RVA offset = Core()->getOffset();
    RVA fcnAddr = Core()->getFunctionStart(offset);
    if (fcnAddr != RVA_INVALID) {
        QJsonArray arr = Core()->cmdj("axffj@" + QString::number(fcnAddr)).array();
        for (const QJsonValue value : arr) {
            QJsonObject o = value.toObject();
            XrefDescription ref;
            ref.type = o.value("type").toString();
            ref.from = o.value("at").toVariant().toULongLong();
            ref.to = o.value("ref").toVariant().toULongLong();
            ref.from_str = RAddressString(ref.from);
            QString name = o.value("name").toString();
            ref.to_str = name.isEmpty() ? RAddressString(ref.to) : name;
            refs << ref;
        }
    }
    refsModel->endResetModel();

    qhelpers::adjustColumns(ui->treeView, RefsListModel::ColumnCount, 0);
}
