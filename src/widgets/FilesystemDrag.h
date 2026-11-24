#pragma once

#include <functional>
#include <utility>
#include <QTreeView>
#include <QTreeWidget>

class QDir;
class QTemporaryDir;

namespace FilesystemDrag {

QString uniquePath(const QDir &directory, QString name);
Qt::DropAction copyFiles(QWidget *widget, const QStringList &paths);

template<class Tree>
class View : public Tree
{
public:
    using Tree::Tree;

    void setDragHandler(std::function<void()> handler)
    {
        dragHandler = std::move(handler);
        this->setDragDropMode(QAbstractItemView::DragOnly);
    }

protected:
    void startDrag(Qt::DropActions) override
    {
        if (dragHandler) {
            dragHandler();
        }
    }

private:
    std::function<void()> dragHandler;
};

} // namespace FilesystemDrag

using ForensicsEntryTree = FilesystemDrag::View<QTreeWidget>;
