#ifndef NAMELISTVIEW_H
#define NAMELISTVIEW_H

#include <QObject>
#include <QPersistentModelIndex>
#include <QSet>

class QTreeView;
class QMenu;
class QAction;
class NameListModel;
class AddressableItemModelI;

class NameListView : public QObject
{
public:
    NameListView(
        QTreeView *view,
        QMenu *menu,
        AddressableItemModelI *source,
        int nameColumn,
        const QString &settingsKey);
    QAction *toggleAction() const { return hierarchyAction; }

private:
    QTreeView *view;
    NameListModel *model;
    QAction *hierarchyAction;
    int nameColumn;
    int flatNamePosition;
    int flatIndentation;
    QString settingsKey;
    QSet<QString> expandedGroups;
    QPersistentModelIndex currentSource;
    bool restoring = false;

    void updateHierarchy();
    void restoreView();
};

#endif
