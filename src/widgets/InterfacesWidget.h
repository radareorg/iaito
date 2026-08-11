#ifndef INTERFACESWIDGET_H
#define INTERFACESWIDGET_H

#include "ListDockWidget.h"

#include <QAbstractListModel>

class MainWindow;

struct InterfaceEntry
{
    QString language;
    RVA address = RVA_INVALID;
    QString text;
};

class InterfacesModel : public AddressableItemModel<QAbstractListModel>
{
    Q_OBJECT

private:
    QList<InterfaceEntry> *entries;

public:
    enum Column { AddressColumn = 0, LanguageColumn, TextColumn, ColumnCount };
    enum Role { InterfaceEntryRole = Qt::UserRole };

    InterfacesModel(QList<InterfaceEntry> *entries, QObject *parent = nullptr);
    void setEntries(const QList<InterfaceEntry> &entries);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(
        int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    RVA address(const QModelIndex &index) const override;
    QString name(const QModelIndex &index) const override;
};

class InterfacesProxyModel : public AddressableFilterProxyModel
{
    Q_OBJECT

public:
    InterfacesProxyModel(InterfacesModel *sourceModel, QObject *parent = nullptr);

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;
};

class InterfacesWidget : public ListDockWidget
{
    Q_OBJECT

public:
    explicit InterfacesWidget(MainWindow *main);
    ~InterfacesWidget();

private slots:
    void refreshInterfaces();

private:
    InterfacesModel *interfacesModel;
    InterfacesProxyModel *interfacesProxyModel;
    QList<InterfaceEntry> entries;
};

Q_DECLARE_METATYPE(InterfaceEntry)

#endif // INTERFACESWIDGET_H
