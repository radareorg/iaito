#include "AddressableItemModel.h"
#include <stdexcept>

#include <stdexcept>

AddressableItemModelI::~AddressableItemModelI()
{ /* nothing to see */
}

AddressableFilterProxyModel::AddressableFilterProxyModel(
    AddressableItemModelI *sourceModel, QObject *parent)
    : AddressableItemModel<QSortFilterProxyModel>(parent)
{
    setSourceModel(sourceModel);
}

RVA AddressableFilterProxyModel::address(const QModelIndex &index) const
{
    const auto source = mapToSource(index);
    return addressableSourceModel && source.isValid() ? addressableSourceModel->address(source)
                                                      : RVA_INVALID;
}

QString AddressableFilterProxyModel::name(const QModelIndex &index) const
{
    const auto source = mapToSource(index);
    return addressableSourceModel && source.isValid() ? addressableSourceModel->name(source)
                                                      : QString();
}

void AddressableFilterProxyModel::setSourceModel(QAbstractItemModel *)
{
    throw std::runtime_error("Not supported");
}

void AddressableFilterProxyModel::setSourceModel(AddressableItemModelI *sourceModel)
{
    addressableSourceModel = sourceModel;
    ParentClass::setSourceModel(sourceModel ? sourceModel->asItemModel() : nullptr);
}

void AddressableFilterProxyModel::setTypeFilterColumn(int column)
{
    if (typeColumn != column) {
        emit filterAboutToChange();
        typeColumn = column;
        invalidateFilter();
        emit filterChanged();
    }
}

void AddressableFilterProxyModel::setTypeFilter(const QVariant &type)
{
    if (selectedType != type) {
        emit filterAboutToChange();
        selectedType = type;
        invalidateFilter();
        emit filterChanged();
    }
}

void AddressableFilterProxyModel::setFilterWildcard(const QString &pattern)
{
    emit filterAboutToChange();
    ParentClass::setFilterWildcard(pattern);
    emit filterChanged();
}

bool AddressableFilterProxyModel::filterAcceptsType(int row, const QModelIndex &parent) const
{
    return typeColumn < 0 || !selectedType.isValid()
           || sourceModel()->index(row, typeColumn, parent).data().toString()
                  == selectedType.toString();
}

bool AddressableFilterProxyModel::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    return filterAcceptsType(row, parent) && ParentClass::filterAcceptsRow(row, parent);
}
