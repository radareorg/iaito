#include "ColumnHeaderMenu.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QHeaderView>
#include <QMenu>
#include <QTableView>
#include <QTreeView>

ColumnHeaderMenu::ColumnHeaderMenu(QObject *parent)
    : QObject(parent)
{
    qApp->installEventFilter(this);
}

bool ColumnHeaderMenu::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() != QEvent::Polish && event->type() != QEvent::ContextMenu) {
        return false;
    }

    auto *header = qobject_cast<QHeaderView *>(watched);
    if (!header && event->type() == QEvent::ContextMenu) {
        header = qobject_cast<QHeaderView *>(watched->parent());
        if (header && watched != header->viewport()) {
            return false;
        }
    }
    if (!header || header->orientation() != Qt::Horizontal
        || (!qobject_cast<QTreeView *>(header->parentWidget())
            && !qobject_cast<QTableView *>(header->parentWidget()))) {
        return false;
    }

    if (event->type() == QEvent::Polish) {
        header->setSectionsMovable(true);
        header->setFirstSectionMovable(true);
        return false;
    }
    if (header->count() == 0) {
        return false;
    }

    auto *menu = new QMenu(header);
    connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);
    menu->addSection(tr("Columns"));
    const int visibleCount = header->count() - header->hiddenSectionCount();
    for (int visual = 0; visual < header->count(); ++visual) {
        const int logical = header->logicalIndex(visual);
        QString title = header->model()->headerData(logical, Qt::Horizontal).toString();
        if (title.isEmpty()) {
            title = tr("Column %1").arg(logical + 1);
        }
        auto *action = menu->addAction(title.replace('&', QStringLiteral("&&")));
        action->setData(logical);
        action->setCheckable(true);
        const bool visible = !header->isSectionHidden(logical);
        action->setChecked(visible);
        // Keep a header available so the user can reopen the column menu.
        action->setEnabled(!visible || visibleCount > 1);
        connect(action, &QAction::toggled, header, [header, logical](bool checked) {
            header->setSectionHidden(logical, !checked);
        });
    }

    menu->addSeparator();
    auto *showAll = menu->addAction(tr("Show all columns"));
    showAll->setEnabled(header->hiddenSectionCount() > 0);
    connect(showAll, &QAction::triggered, header, [header] {
        for (int logical = 0; logical < header->count(); ++logical) {
            header->showSection(logical);
        }
    });
    auto *resetOrder = menu->addAction(tr("Reset column order"));
    resetOrder->setEnabled(header->sectionsMoved());
    connect(resetOrder, &QAction::triggered, header, [header] {
        for (int logical = 0; logical < header->count(); ++logical) {
            header->moveSection(header->visualIndex(logical), logical);
        }
    });

    auto *contextEvent = static_cast<QContextMenuEvent *>(event);
    const QPoint position = contextEvent->reason() == QContextMenuEvent::Keyboard
                                ? header->viewport()->mapToGlobal(
                                      header->viewport()->rect().center())
                                : contextEvent->globalPos();
    menu->popup(position);
    contextEvent->accept();
    return true;
}
