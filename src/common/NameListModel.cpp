#include "NameListModel.h"

#include <QStringList>

namespace {

QString compactName(const QString &name, const QString &previous)
{
    int prefix = 0;
    const int limit = qMin(name.size(), previous.size());
    while (prefix < limit && name.at(prefix) == previous.at(prefix)) {
        ++prefix;
    }
    if (prefix > 0 && prefix < name.size() && name.at(prefix).isLowSurrogate()) {
        --prefix;
    }
    return prefix > 2 && prefix < name.size() ? QStringLiteral("\" ") + name.mid(prefix) : name;
}

QStringList nameParts(const QString &name)
{
    QStringList parts;
    int start = 0;
    int templates = 0;
    int parentheses = 0;
    int brackets = 0;
    for (int i = 0; i < name.size(); ++i) {
        const QChar c = name.at(i);
        // Operators and their signatures belong to the leaf, including operator< and operator().
        if (!templates && !parentheses && i == start && name.mid(i, 8) == QLatin1String("operator")
            && (i + 8 == name.size()
                || (!name.at(i + 8).isLetterOrNumber() && name.at(i + 8) != QLatin1Char('_')))) {
            break;
        }
        if (c == QLatin1Char('<')) {
            ++templates;
        } else if (c == QLatin1Char('>') && templates) {
            --templates;
        } else if (c == QLatin1Char('(')) {
            ++parentheses;
        } else if (c == QLatin1Char(')') && parentheses) {
            --parentheses;
        } else if (c == QLatin1Char('[')) {
            ++brackets;
        } else if (c == QLatin1Char(']') && brackets) {
            --brackets;
        }
        if (templates || parentheses || brackets) {
            continue;
        }
        int separator = 0;
        if (c == QLatin1Char(':') && i + 1 < name.size() && name.at(i + 1) == c) {
            separator = 2;
        } else if (c == QLatin1Char('-') && i + 1 < name.size() && name.at(i + 1) == QLatin1Char('>')) {
            separator = 2;
        } else if (c == QLatin1Char('.') || c == QLatin1Char('/') || c == QLatin1Char('$')) {
            separator = 1;
        }
        if (separator) {
            i += separator - 1;
            if (i + 1 < name.size()) {
                parts.append(name.mid(start, i + 1 - start));
                start = i + 1;
            }
        }
    }
    parts.append(name.mid(start));
    return parts;
}

} // namespace

NameListModel::NameListModel(AddressableItemModelI *source, int nameColumn, QObject *parent)
    : AddressableItemModel<QAbstractProxyModel>(parent)
    , nameColumn(nameColumn)
{
    setSourceModel(source->asItemModel());
}

NameListModel::~NameListModel() = default;

void NameListModel::setSourceModel(QAbstractItemModel *source)
{
    beginRebuild();
    if (sourceModel()) {
        disconnect(sourceModel(), nullptr, this, nullptr);
    }
    QAbstractProxyModel::setSourceModel(source);
    sourceProvider = dynamic_cast<AddressableItemModelI *>(source);
    if (source) {
        connect(source, &QAbstractItemModel::modelAboutToBeReset, this, &NameListModel::beginRebuild);
        connect(source, &QAbstractItemModel::modelReset, this, &NameListModel::endRebuild);
        connect(
            source, &QAbstractItemModel::layoutAboutToBeChanged, this, &NameListModel::beginRebuild);
        connect(source, &QAbstractItemModel::layoutChanged, this, &NameListModel::endRebuild);
        connect(
            source, &QAbstractItemModel::rowsAboutToBeInserted, this, &NameListModel::beginRebuild);
        connect(source, &QAbstractItemModel::rowsInserted, this, &NameListModel::endRebuild);
        connect(source, &QAbstractItemModel::rowsAboutToBeRemoved, this, &NameListModel::beginRebuild);
        connect(source, &QAbstractItemModel::rowsRemoved, this, &NameListModel::endRebuild);
        connect(source, &QAbstractItemModel::rowsAboutToBeMoved, this, &NameListModel::beginRebuild);
        connect(source, &QAbstractItemModel::rowsMoved, this, &NameListModel::endRebuild);
        connect(source, &QAbstractItemModel::dataChanged, this, &NameListModel::sourceDataChanged);
        connect(
            source, &QAbstractItemModel::headerDataChanged, this, &NameListModel::headerDataChanged);
        connect(source, &QObject::destroyed, this, [this] {
            sourceProvider = nullptr;
            beginRebuild();
            endRebuild();
        });
    }
    endRebuild();
}

NameListModel::Node *NameListModel::node(const QModelIndex &index) const
{
    return index.isValid() ? static_cast<Node *>(index.internalPointer()) : nullptr;
}

QModelIndex NameListModel::nodeIndex(Node *item, int column) const
{
    return item && item != &root ? createIndex(item->row, column, item) : QModelIndex();
}

QModelIndex NameListModel::index(int row, int column, const QModelIndex &parent) const
{
    if (row < 0 || column < 0 || column >= columnCount() || (parent.isValid() && parent.column())) {
        return {};
    }
    const Node *item = parent.isValid() ? node(parent) : &root;
    return row < int(item->children.size()) ? nodeIndex(item->children[row].get(), column)
                                            : QModelIndex();
}

QModelIndex NameListModel::parent(const QModelIndex &index) const
{
    auto item = node(index);
    return item ? nodeIndex(item->parent) : QModelIndex();
}

int NameListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() && parent.column()) {
        return 0;
    }
    const Node *item = parent.isValid() ? node(parent) : &root;
    return int(item->children.size());
}

int NameListModel::columnCount(const QModelIndex &) const
{
    return sourceProvider ? sourceModel()->columnCount() : 0;
}

bool NameListModel::hasChildren(const QModelIndex &parent) const
{
    return rowCount(parent) > 0;
}

QModelIndex NameListModel::mapToSource(const QModelIndex &index) const
{
    auto item = node(index);
    return sourceProvider && item && item->source.isValid()
               ? item->source.sibling(item->source.row(), index.column())
               : QModelIndex();
}

QModelIndex NameListModel::mapFromSource(const QModelIndex &index) const
{
    return index.isValid()
               ? nodeIndex(sourceNodes.value(index.sibling(index.row(), 0)), index.column())
               : QModelIndex();
}

QVariant NameListModel::data(const QModelIndex &index, int role) const
{
    auto item = node(index);
    if (!item) {
        return {};
    }
    if (!item->source.isValid()) {
        if (role == GroupPathRole) {
            return item->groupPath;
        }
        if (index.column() == nameColumn) {
            if (role == Qt::DisplayRole) {
                return item->label;
            }
            if (role == Qt::ToolTipRole) {
                return QStringLiteral("<qt>%1</qt>").arg(item->groupPath.toHtmlEscaped());
            }
        }
        return {};
    }
    if (index.column() == nameColumn && item->named) {
        if (role == Qt::ToolTipRole) {
            const auto tooltip = mapToSource(index).data(NameToolTipRole);
            if (tooltip.isValid()) {
                return tooltip;
            }
            return QStringLiteral("<qt>%1</qt>").arg(item->fullName.toHtmlEscaped());
        }
        if (role == Qt::DisplayRole) {
            QString label = item->label;
            if (compact && item->row > 0) {
                auto previous = item->parent->children[item->row - 1].get();
                if (previous->named) {
                    label = compactName(label, previous->label);
                }
            }
            const QString original = mapToSource(index).data().toString();
            // Keep decorations represented as text, such as a function's pin emoji.
            if (original.endsWith(item->fullName)) {
                return original.left(original.size() - item->fullName.size()) + label;
            }
            return label;
        }
    }
    return mapToSource(index).data(role);
}

Qt::ItemFlags NameListModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    auto source = mapToSource(index);
    return source.isValid() ? source.flags() : Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

QVariant NameListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    return sourceProvider ? sourceModel()->headerData(section, orientation, role) : QVariant();
}

void NameListModel::sort(int column, Qt::SortOrder order)
{
    if (sourceProvider) {
        sourceModel()->sort(column, order);
    }
}

RVA NameListModel::address(const QModelIndex &index) const
{
    const auto source = mapToSource(index);
    return sourceProvider && source.isValid() ? sourceProvider->address(source) : RVA_INVALID;
}

QString NameListModel::name(const QModelIndex &index) const
{
    const auto source = mapToSource(index);
    return sourceProvider && source.isValid() ? sourceProvider->name(source) : QString();
}

void NameListModel::beginRebuild()
{
    if (!resetting) {
        resetting = true;
        beginResetModel();
    }
}

void NameListModel::endRebuild()
{
    if (resetting) {
        rebuild();
        resetting = false;
        endResetModel();
    }
}

void NameListModel::appendSource(
    Node *parent, const QModelIndex &source, const QString &label, bool named)
{
    auto item = std::make_unique<Node>();
    item->parent = parent;
    item->row = int(parent->children.size());
    item->source = source;
    item->label = label;
    item->named = named;
    if (named) {
        item->fullName = sourceProvider->name(source);
    }
    auto ptr = item.get();
    sourceNodes.insert(source, ptr);
    parent->children.push_back(std::move(item));
    // Preserve existing class/member relationships and unnamed function detail rows.
    if (sourceModel()->hasChildren(source)) {
        for (int row = 0; row < sourceModel()->rowCount(source); ++row) {
            auto child = sourceModel()->index(row, 0, source);
            QString childName = sourceProvider->name(child);
            const bool childNamed = !childName.isEmpty() && childName != ptr->fullName;
            if (childNamed) {
                for (const QString &separator :
                     {QStringLiteral("::"),
                      QStringLiteral("."),
                      QStringLiteral("/"),
                      QStringLiteral("$"),
                      QStringLiteral("->")}) {
                    const QString prefix = ptr->fullName + separator;
                    if (childName.startsWith(prefix)) {
                        childName.remove(0, prefix.size());
                        break;
                    }
                }
            }
            appendSource(ptr, child, childName, childNamed);
        }
    }
}

void NameListModel::rebuild()
{
    sourceNodes.clear();
    groups.clear();
    root.children.clear();
    if (!sourceProvider) {
        return;
    }
    for (int row = 0; row < sourceModel()->rowCount(); ++row) {
        auto source = sourceModel()->index(row, 0);
        const QString fullName = sourceProvider->name(source);
        Node *parent = &root;
        QString label = fullName;
        if (hierarchy) {
            const auto parts = nameParts(fullName);
            QString path;
            for (int part = 0; part + 1 < parts.size(); ++part) {
                path += parts[part];
                auto group = groups.value(path);
                if (!group) {
                    auto item = std::make_unique<Node>();
                    item->parent = parent;
                    item->row = int(parent->children.size());
                    item->label = parts[part];
                    item->groupPath = path;
                    group = item.get();
                    parent->children.push_back(std::move(item));
                    groups.insert(path, group);
                }
                parent = group;
            }
            label = parts.last();
        }
        appendSource(parent, source, label, true);
    }
}

void NameListModel::sourceDataChanged(const QModelIndex &first, const QModelIndex &last)
{
    if (resetting) {
        return;
    }
    for (int row = first.row(); row <= last.row(); ++row) {
        auto source = sourceModel()->index(row, 0, first.parent());
        auto item = sourceNodes.value(source);
        if (item && item->named && item->fullName != sourceProvider->name(source)) {
            beginRebuild();
            endRebuild();
            return;
        }
    }
    for (int row = first.row(); row <= last.row(); ++row) {
        auto source = sourceModel()->index(row, 0, first.parent());
        auto item = sourceNodes.value(source);
        if (item) {
            emit dataChanged(nodeIndex(item, first.column()), nodeIndex(item, last.column()));
        }
    }
}

void NameListModel::namesChanged(Node *parent)
{
    if (!parent->children.empty()) {
        emit dataChanged(
            nodeIndex(parent->children.front().get(), nameColumn),
            nodeIndex(parent->children.back().get(), nameColumn),
            {Qt::DisplayRole});
        for (const auto &child : parent->children) {
            namesChanged(child.get());
        }
    }
}

void NameListModel::setCompact(bool enabled)
{
    if (compact != enabled) {
        compact = enabled;
        namesChanged(&root);
    }
}

void NameListModel::setHierarchy(bool enabled)
{
    if (hierarchy != enabled) {
        beginRebuild();
        hierarchy = enabled;
        endRebuild();
    }
}

QModelIndex NameListModel::groupIndex(const QString &path) const
{
    return nodeIndex(groups.value(path));
}
