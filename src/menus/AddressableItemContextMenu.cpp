#include "AddressableItemContextMenu.h"
#include "MainWindow.h"
#include "dialogs/CommentsDialog.h"
#include "dialogs/XrefsDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QJsonArray>
#include <QPainter>
#include <QPolygonF>
#include <QPushButton>
#include <QShortcut>
#include <QtCore>

AddressableItemContextMenu::AddressableItemContextMenu(QWidget *parent, MainWindow *mainWindow)
    : QMenu(parent)
    , mainWindow(mainWindow)
{
    actionShowInMenu = new QAction(tr("Show in"), this);
    actionCopyAddress = new QAction(tr("Copy address"), this);
    actionShowXrefs = new QAction(tr("Show X-Refs"), this);
    actionAddcomment = new QAction(tr("Add comment"), this);

    setActionIcon(actionShowInMenu, MenuIcon::Navigation, QColor(103, 80, 164));
    setActionIcon(actionCopyAddress, MenuIcon::Copy, QColor(56, 142, 60));
    setActionIcon(actionShowXrefs, MenuIcon::XRefs, QColor(0, 137, 190));
    setActionIcon(actionAddcomment, MenuIcon::Comment, QColor(67, 160, 71));

    connect(
        actionCopyAddress,
        &QAction::triggered,
        this,
        &AddressableItemContextMenu::onActionCopyAddress);
    actionCopyAddress->setShortcuts({Qt::CTRL | Qt::SHIFT | Qt::Key_C});
    actionCopyAddress->setShortcutContext(Qt::ShortcutContext::WidgetWithChildrenShortcut);

    connect(
        actionShowXrefs, &QAction::triggered, this, &AddressableItemContextMenu::onActionShowXrefs);
    actionShowXrefs->setShortcut({Qt::Key_X});
    actionShowXrefs->setShortcutContext(Qt::ShortcutContext::WidgetWithChildrenShortcut);

    connect(
        actionAddcomment,
        &QAction::triggered,
        this,
        &AddressableItemContextMenu::onActionAddComment);
    actionAddcomment->setShortcut({Qt::Key_Semicolon});
    actionAddcomment->setShortcutContext(Qt::ShortcutContext::WidgetWithChildrenShortcut);

    addAction(actionShowInMenu);
    addAction(actionCopyAddress);
    addAction(actionShowXrefs);
    addSeparator();
    addAction(actionAddcomment);

    addSeparator();
    pluginMenu = mainWindow->getContextMenuExtensions(MainWindow::ContextMenuType::Addressable);
    pluginMenuAction = addMenu(pluginMenu);
    addSeparator();

    setHasTarget(hasTarget);

    connect(this, &QMenu::aboutToShow, this, &AddressableItemContextMenu::aboutToShowSlot);
}

AddressableItemContextMenu::~AddressableItemContextMenu() {}

QIcon AddressableItemContextMenu::makeMenuIcon(MenuIcon icon, const QColor &color) const
{
    const qreal ratio = devicePixelRatioF();
    const int size = 16;
    QPixmap pixmap(size * ratio, size * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    QColor accent = color;
    accent.setAlpha(205);
    QPen pen(accent, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    switch (icon) {
    case MenuIcon::Navigation:
        painter.drawPolyline(QPolygonF(QVector<QPointF>{QPointF(4, 8), QPointF(11, 8)}));
        painter.drawPolyline(
            QPolygonF(QVector<QPointF>{QPointF(8, 5), QPointF(11, 8), QPointF(8, 11)}));
        break;
    case MenuIcon::Copy:
        painter.drawRect(QRectF(5, 3.5, 7, 9));
        painter.drawRect(QRectF(3, 5.5, 7, 7));
        break;
    case MenuIcon::XRefs:
        painter.drawEllipse(QPointF(5, 8), 2.0, 2.0);
        painter.drawEllipse(QPointF(11, 5), 2.0, 2.0);
        painter.drawEllipse(QPointF(11, 11), 2.0, 2.0);
        painter.drawLine(QPointF(6.7, 7), QPointF(9.3, 5.8));
        painter.drawLine(QPointF(6.7, 9), QPointF(9.3, 10.2));
        break;
    case MenuIcon::Comment:
        painter.drawRoundedRect(QRectF(3.5, 4, 9, 7), 1.5, 1.5);
        painter.drawLine(QPointF(6, 11), QPointF(4.5, 13));
        break;
    case MenuIcon::Rename:
        painter.drawLine(QPointF(4, 12), QPointF(12, 4));
        painter.drawLine(QPointF(10, 4), QPointF(12, 6));
        painter.drawLine(QPointF(4, 12), QPointF(7, 11));
        break;
    case MenuIcon::Delete:
        painter.drawLine(QPointF(4, 5), QPointF(12, 5));
        painter.drawLine(QPointF(6, 3.5), QPointF(10, 3.5));
        painter.drawRoundedRect(QRectF(5, 6, 6, 7), 1.0, 1.0);
        painter.drawLine(QPointF(7, 7.5), QPointF(7, 11.5));
        painter.drawLine(QPointF(9, 7.5), QPointF(9, 11.5));
        break;
    case MenuIcon::Color:
        painter.drawEllipse(QRectF(3.5, 4, 9, 8));
        painter.setBrush(accent);
        painter.drawEllipse(QPointF(6, 7), 0.9, 0.9);
        painter.drawEllipse(QPointF(8.5, 6), 0.9, 0.9);
        painter.drawEllipse(QPointF(10, 8.5), 0.9, 0.9);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QPointF(7.5, 10), 0.9, 0.9);
        break;
    case MenuIcon::Pin:
        painter.drawEllipse(QPointF(8, 4.5), 1.8, 1.8);
        painter.drawLine(QPointF(8, 6.5), QPointF(8, 12.5));
        painter.drawLine(QPointF(5.5, 8), QPointF(10.5, 8));
        painter.drawLine(QPointF(8, 12.5), QPointF(6.8, 14));
        break;
    }

    return QIcon(pixmap);
}

void AddressableItemContextMenu::setActionIcon(QAction *action, MenuIcon icon, const QColor &color)
{
    if (action) {
        action->setIcon(makeMenuIcon(icon, color));
        action->setIconVisibleInMenu(true);
    }
}

void AddressableItemContextMenu::setMenuIcon(QMenu *menu, MenuIcon icon, const QColor &color)
{
    if (menu) {
        setActionIcon(menu->menuAction(), icon, color);
    }
}

void AddressableItemContextMenu::setWholeFunction(bool wholeFunciton)
{
    this->wholeFunction = wholeFunciton;
}

void AddressableItemContextMenu::setOffset(RVA offset)
{
    setTarget(offset);
}

void AddressableItemContextMenu::setTarget(RVA offset, QString name)
{
    this->offset = offset;
    this->name = name;
    setHasTarget(true);
}

void AddressableItemContextMenu::clearTarget()
{
    setHasTarget(false);
}

void AddressableItemContextMenu::onActionCopyAddress()
{
    auto clipboard = QApplication::clipboard();
    clipboard->setText(RAddressString(offset));
}

void AddressableItemContextMenu::onActionShowXrefs()
{
    emit xrefsTriggered();
    XrefsDialog dialog(mainWindow, nullptr, true);
    QString tmpName = name.isEmpty() ? RAddressString(offset) : name;
    dialog.fillRefsForAddress(offset, tmpName, wholeFunction);
    dialog.exec();
}

void AddressableItemContextMenu::onActionAddComment()
{
    CommentsDialog::addOrEditComment(offset, this);
}

void AddressableItemContextMenu::aboutToShowSlot()
{
    if (actionShowInMenu->menu()) {
        actionShowInMenu->menu()->deleteLater();
    }
    actionShowInMenu->setMenu(mainWindow->createShowInMenu(this, offset));

    pluginMenuAction->setVisible(!pluginMenu->isEmpty());
    for (QAction *pluginAction : pluginMenu->actions()) {
        pluginAction->setData(QVariant::fromValue(offset));
    }
}

void AddressableItemContextMenu::setHasTarget(bool hasTarget)
{
    this->hasTarget = hasTarget;
    for (const auto &action : this->actions()) {
        action->setEnabled(hasTarget);
    }
}
