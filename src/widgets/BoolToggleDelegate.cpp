#include "BoolToggleDelegate.h"
#include <QEvent>
#include <QMetaType>
#include <QVariant>

namespace {
static inline int variantTypeId(const QVariant &v)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return v.metaType().id();
#else
    return v.userType();
#endif
}
} // namespace

BoolTogggleDelegate::BoolTogggleDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{}

QWidget *BoolTogggleDelegate::createEditor(
    QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    if (variantTypeId(index.data(Qt::EditRole)) == QMetaType::Bool) {
        return nullptr;
    }
    return QStyledItemDelegate::createEditor(parent, option, index);
}

bool BoolTogggleDelegate::editorEvent(
    QEvent *event,
    QAbstractItemModel *model,
    const QStyleOptionViewItem &option,
    const QModelIndex &index)
{
    if (model->flags(index).testFlag(Qt::ItemFlag::ItemIsEditable)) {
        if (event->type() == QEvent::MouseButtonDblClick) {
            auto data = index.data(Qt::EditRole);
            if (variantTypeId(data) == QMetaType::Bool) {
                model->setData(index, !data.toBool());
                return true;
            }
        }
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}
