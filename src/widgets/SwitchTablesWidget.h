#ifndef SWITCHTABLESWIDGET_H
#define SWITCHTABLESWIDGET_H

#include "ListDockWidget.h"

#include <QAbstractTableModel>

namespace Ui {
class SwitchTablesWidget;
}

class SwitchTablesModel : public AddressableItemModel<QAbstractTableModel>
{
    Q_OBJECT

public:
    enum Column { AddressColumn, FunctionColumn, CasesColumn, TableColumn, ColumnCount };

    explicit SwitchTablesModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(
        int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    RVA address(const QModelIndex &index) const override;
    QString name(const QModelIndex &index) const override;
    const SwitchTableDescription *switchTable(const QModelIndex &index) const;
    void setTables(QList<SwitchTableDescription> tables);

private:
    QList<SwitchTableDescription> tables;
};

class SwitchCasesModel : public AddressableItemModel<QAbstractTableModel>
{
    Q_OBJECT

public:
    enum Column { ValueColumn, TargetColumn, AsciiColumn, ColumnCount };

    explicit SwitchCasesModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(
        int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    RVA address(const QModelIndex &index) const override;
    void setCases(QList<SwitchCaseDescription> cases);

private:
    QList<SwitchCaseDescription> cases;
};

class SwitchTablesWidget : public ListDockWidget
{
    Q_OBJECT

public:
    explicit SwitchTablesWidget(MainWindow *main);
    ~SwitchTablesWidget() override;

private:
    std::unique_ptr<Ui::SwitchTablesWidget> controls;
    SwitchTablesModel *model;
    AddressableFilterProxyModel *proxy;
    SwitchCasesModel *casesModel;
    AddressableFilterProxyModel *casesProxy;
    RefreshDeferrer *refreshDeferrer;

    void refreshTables();
    void updateCases();
    void editSwitch();
    void navigateTo(RVA address);
    const SwitchTableDescription *selectedSwitch() const;
};

#endif // SWITCHTABLESWIDGET_H
