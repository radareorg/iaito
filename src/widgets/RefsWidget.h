#ifndef REFSWIDGET_H
#define REFSWIDGET_H

#include <memory>
#include <QAbstractListModel>
#include <QSortFilterProxyModel>

#include "IaitoDockWidget.h"
#include "common/AddressableItemModel.h"
#include "core/Iaito.h"
#include "widgets/ListDockWidget.h"

class MainWindow;
class RefsWidget;

class RefsListModel : public AddressableItemModel<QAbstractListModel>
{
    Q_OBJECT

    friend RefsWidget;

private:
    QList<XrefDescription> *refs;

public:
    enum Column {
        FromColumn = 0,
        TypeColumn,
        TargetColumn,
        NameColumn,
        CodeColumn,
        CommentColumn,
        ColumnCount
    };
    enum Role { XrefDescriptionRole = Qt::UserRole };

    RefsListModel(QList<XrefDescription> *refs, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(
        int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    RVA address(const QModelIndex &index) const override;
};

class RefsProxyModel : public AddressableFilterProxyModel
{
    Q_OBJECT

public:
    RefsProxyModel(RefsListModel *sourceModel, QObject *parent = nullptr);

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;
};

class RefsWidget : public ListDockWidget
{
    Q_OBJECT

public:
    explicit RefsWidget(MainWindow *main);
    ~RefsWidget();

private slots:
    void refreshRefs();

private:
    QList<XrefDescription> refs;
    RefsListModel *refsModel;
    RefsProxyModel *refsProxyModel;
};

#endif // REFSWIDGET_H
