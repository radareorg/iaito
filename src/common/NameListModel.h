#ifndef NAMELISTMODEL_H
#define NAMELISTMODEL_H

#include "AddressableItemModel.h"

#include <memory>
#include <vector>
#include <QAbstractProxyModel>
#include <QHash>

// Presentation only: all addresses, descriptions and edits belong to the source model.
class IAITO_EXPORT NameListModel : public AddressableItemModel<QAbstractProxyModel>
{
public:
    enum {
        GroupPathRole = Qt::UserRole + 0x1000,
        // A complete, HTML-escaped name tooltip supplied by models with alternate spellings.
        NameToolTipRole
    };

    NameListModel(AddressableItemModelI *source, int nameColumn, QObject *parent = nullptr);
    ~NameListModel() override;

    void setSourceModel(QAbstractItemModel *source) override;
    QModelIndex mapToSource(const QModelIndex &index) const override;
    QModelIndex mapFromSource(const QModelIndex &index) const override;
    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &index) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    bool hasChildren(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(
        int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;
    RVA address(const QModelIndex &index) const override;
    QString name(const QModelIndex &index) const override;

    void setCompact(bool enabled);
    void setHierarchy(bool enabled);
    bool isHierarchy() const { return hierarchy; }
    QModelIndex groupIndex(const QString &path) const;

private:
    struct Node
    {
        Node *parent = nullptr;
        int row = 0;
        QModelIndex source;
        QString label;
        QString fullName;
        bool named = false;
        QString groupPath;
        std::vector<std::unique_ptr<Node>> children;
    };

    AddressableItemModelI *sourceProvider = nullptr;
    int nameColumn;
    bool compact = false;
    bool hierarchy = false;
    bool resetting = false;
    int filterChangeDepth = 0;
    Node root;
    QHash<QModelIndex, Node *> sourceNodes;
    QHash<QString, Node *> groups;

    Node *node(const QModelIndex &index) const;
    QModelIndex nodeIndex(Node *node, int column = 0) const;
    void beginRebuild();
    void endRebuild();
    void rebuild();
    void collapseSingleGroups(Node *parent);
    void appendSource(Node *parent, const QModelIndex &source, const QString &label, bool named);
    void sourceDataChanged(const QModelIndex &first, const QModelIndex &last);
    void namesChanged(Node *parent);
};

#endif
