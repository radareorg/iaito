#ifndef XREFSWIDGET_H
#define XREFSWIDGET_H

#include <memory>
#include <QAbstractListModel>
#include <QSortFilterProxyModel>

#include "IaitoDockWidget.h"
#include "common/AddressableItemModel.h"
#include "core/Iaito.h"
#include "widgets/ListDockWidget.h"

class MainWindow;
class XrefsWidget;

class XrefsListModel : public AddressableItemModel<QAbstractListModel>
{
    Q_OBJECT

    friend XrefsWidget;

private:
    QList<XrefDescription> *xrefs;

public:
    enum Column {
        AddressColumn = 0,
        TypeColumn,
        FunctionColumn,
        CodeColumn,
        CommentColumn,
        ColumnCount
    };
    enum Role { XrefDescriptionRole = Qt::UserRole };

    XrefsListModel(QList<XrefDescription> *xrefs, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(
        int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    RVA address(const QModelIndex &index) const override;
};

class XrefsProxyModel : public AddressableFilterProxyModel
{
    Q_OBJECT

public:
    XrefsProxyModel(XrefsListModel *sourceModel, QObject *parent = nullptr);

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;
};

class XrefsWidget : public ListDockWidget
{
    Q_OBJECT

public:
    explicit XrefsWidget(MainWindow *main);
    ~XrefsWidget();

private slots:
    void refreshXrefs();

private:
    QList<XrefDescription> xrefs;
    XrefsListModel *xrefsModel;
    XrefsProxyModel *xrefsProxyModel;
};

#endif // XREFSWIDGET_H
