#ifndef RESOURCESWIDGET_H
#define RESOURCESWIDGET_H

#include "widgets/ListDockWidget.h"

#include <QAbstractListModel>

class MainWindow;
class ResourcesWidget;
class QAction;

class ResourcesModel : public AddressableItemModel<QAbstractListModel>
{
    Q_OBJECT

    friend ResourcesWidget;

private:
    QList<ResourcesDescription> *resources;

public:
    enum Column {
        NameColumn = 0,
        TypeColumn,
        SizeColumn,
        VaddrColumn,
        PaddrColumn,
        LanguageColumn,
        IdColumn,
        IndexColumn,
        TypeIdColumn,
        LanguageIdColumn,
        CodepageColumn,
        NamedColumn,
        TimestampColumn,
        OriginColumn,
        CommentColumn,
        ColumnCount
    };
    enum Role { ResourceDescriptionRole = Qt::UserRole };

    explicit ResourcesModel(QList<ResourcesDescription> *resources, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(
        int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    RVA address(const QModelIndex &index) const override;
    QString name(const QModelIndex &index) const override;
};

class ResourcesProxyModel : public AddressableFilterProxyModel
{
    Q_OBJECT

public:
    explicit ResourcesProxyModel(ResourcesModel *sourceModel, QObject *parent = nullptr);

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
};

class ResourcesWidget : public ListDockWidget
{
    Q_OBJECT

private:
    ResourcesModel *model;
    ResourcesProxyModel *filterModel;
    QList<ResourcesDescription> resources;
    QAction *dumpAction;

public:
    explicit ResourcesWidget(MainWindow *main);
    ~ResourcesWidget();

private slots:
    void refreshResources();
    void showResourceAddress(const QModelIndex &index);
    void dumpSelectedResource();
};

#endif // RESOURCESWIDGET_H
