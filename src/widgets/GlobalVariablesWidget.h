#ifndef GLOBALVARIABLESWIDGET_H
#define GLOBALVARIABLESWIDGET_H

#include "ListDockWidget.h"

#include <QAbstractListModel>

namespace Ui {
class GlobalVariablesWidget;
}

class GlobalVariablesModel : public AddressableItemModel<QAbstractListModel>
{
    Q_OBJECT

public:
    enum Column { AddressColumn, NameColumn, TypeColumn, SizeColumn, CommentColumn, ColumnCount };
    enum Role { DescriptionRole = Qt::UserRole };

    explicit GlobalVariablesModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(
        int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    RVA address(const QModelIndex &index) const override;
    QString name(const QModelIndex &index) const override;
    void setVariables(QList<GlobalVariableDescription> variables);

private:
    QList<GlobalVariableDescription> variables;
};

class GlobalVariablesWidget : public ListDockWidget
{
    Q_OBJECT

public:
    explicit GlobalVariablesWidget(MainWindow *main);
    ~GlobalVariablesWidget() override;

private:
    std::unique_ptr<Ui::GlobalVariablesWidget> controls;
    GlobalVariablesModel *model;
    AddressableFilterProxyModel *proxy;
    RefreshDeferrer *refreshDeferrer;
    QAction *editAction;
    QAction *deleteAction;

    void refreshVariables();
    void editVariable(bool create);
    void deleteVariable();
    GlobalVariableDescription currentVariable() const;
};

#endif // GLOBALVARIABLESWIDGET_H
