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
