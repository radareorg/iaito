#include "AddressableDockWidget.h"
#include "MainWindow.h"
#include "common/IaitoSeekable.h"
#include <QAction>
#include <QContextMenuEvent>
#include <QEvent>
#include <QMenu>

AddressableDockWidget::AddressableDockWidget(MainWindow *parent)
    : IaitoDockWidget(parent)
    , seekable(new IaitoSeekable(this))
    , syncAction(tr("Sync/unsync offset"), this)
{
    connect(seekable, &IaitoSeekable::syncChanged, this, &AddressableDockWidget::updateWindowTitle);
    connect(&syncAction, &QAction::triggered, seekable, &IaitoSeekable::toggleSynchronization);

    dockMenu = new QMenu(this);
    dockMenu->addAction(&syncAction);

    setContextMenuPolicy(Qt::ContextMenuPolicy::DefaultContextMenu);
}

QVariantMap AddressableDockWidget::serializeViewProprties()
{
    auto result = IaitoDockWidget::serializeViewProprties();
    result["synchronized"] = seekable->isSynchronized();
    return result;
}

void AddressableDockWidget::deserializeViewProperties(const QVariantMap &properties)
{
    QVariant synchronized = properties.value("synchronized", true);
    seekable->setSynchronization(synchronized.toBool());
}

void AddressableDockWidget::updateWindowTitle()
{
    QString name = getWindowTitle();
    QString id = getDockNumber();
    if (!id.isEmpty()) {
        name += " " + id;
    }
    if (!seekable->isSynchronized()) {
        name += IaitoSeekable::tr(" (unsynced)");
    }
    setWindowTitle(name);
}

void AddressableDockWidget::contextMenuEvent(QContextMenuEvent *event)
{
    event->accept();
    dockMenu->exec(mapToGlobal(event->pos()));
}

IaitoSeekable *AddressableDockWidget::getSeekable() const
{
    return seekable;
}
