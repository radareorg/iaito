#include "AddressableDockWidget.h"
#include "MainWindow.h"
#include "common/IaitoSeekable.h"
#include <QAction>
#include <QContextMenuEvent>
#include <QEvent>
#include <QLabel>
#include <QMenu>
#include <QStyle>
#include <QToolBar>
#include <QVBoxLayout>

AddressableDockWidget::AddressableDockWidget(MainWindow *parent)
    : IaitoDockWidget(parent)
    , seekable(new IaitoSeekable(this))
    , syncAction(this)
{
    connect(seekable, &IaitoSeekable::syncChanged, this, &AddressableDockWidget::updateWindowTitle);
    connect(seekable, &IaitoSeekable::syncChanged, this, &AddressableDockWidget::updateSyncActionText);
    connect(&syncAction, &QAction::triggered, seekable, &IaitoSeekable::toggleSynchronization);
    updateSyncActionText();

    dockMenu = new QMenu(this);
    dockMenu->addAction(&syncAction);

    connect(seekable, &IaitoSeekable::syncChanged, this, [this]() {
        if (!seekable->isSynchronized()) {
            setupSeekHistoryBar();
        }
        updateSeekHistoryBar();
    });
    connect(
        seekable,
        &IaitoSeekable::historyChanged,
        this,
        &AddressableDockWidget::updateSeekHistoryBar);
    connect(
        seekable,
        &IaitoSeekable::seekableSeekChanged,
        this,
        &AddressableDockWidget::updateSeekHistoryBar);
    connect(this, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible) {
            setupSeekHistoryBar();
        }
    });

    setContextMenuPolicy(Qt::ContextMenuPolicy::DefaultContextMenu);
}

void AddressableDockWidget::setupSeekHistoryBar(bool alwaysVisible)
{
    historyBarAlwaysVisible |= alwaysVisible;
    if (seekHistoryBar || !widget()) {
        updateSeekHistoryBar();
        return;
    }
    QWidget *contents = widget();
    seekHistoryBar = new QToolBar(contents);
    seekHistoryBar->setObjectName(QStringLiteral("seekHistoryBar"));
    seekHistoryBar->setIconSize(QSize(16, 16));
    seekHistoryBar->setMovable(false);
    seekHistoryBar->setFloatable(false);
    seekHistoryBar->setFocusPolicy(Qt::NoFocus);
    seekBackAction
        = seekHistoryBar->addAction(style()->standardIcon(QStyle::SP_ArrowBack), tr("Back"));
    seekBackAction->setObjectName(QStringLiteral("seekBackAction"));
    seekForwardAction
        = seekHistoryBar->addAction(style()->standardIcon(QStyle::SP_ArrowForward), tr("Forward"));
    seekForwardAction->setObjectName(QStringLiteral("seekForwardAction"));
    connect(seekBackAction, &QAction::triggered, seekable, &IaitoSeekable::seekPrev);
    connect(seekForwardAction, &QAction::triggered, seekable, &IaitoSeekable::seekNext);
    for (QAction *action : {seekBackAction, seekForwardAction}) {
        seekHistoryBar->widgetForAction(action)->setFocusPolicy(Qt::NoFocus);
    }
    seekAddressLabel = new QLabel(seekHistoryBar);
    seekAddressLabel->setMargin(3);
    seekHistoryBar->addWidget(seekAddressLabel);

    if (auto *layout = qobject_cast<QVBoxLayout *>(contents->layout())) {
        layout->insertWidget(0, seekHistoryBar);
    } else {
        // Graph-only docks have no content layout in which to insert the bar.
        auto *container = new QWidget(this);
        auto *containerLayout = new QVBoxLayout(container);
        containerLayout->setContentsMargins(0, 0, 0, 0);
        containerLayout->setSpacing(0);
        containerLayout->addWidget(seekHistoryBar);
        containerLayout->addWidget(contents);
        setWidget(container);
        contents->show();
    }
    updateSeekHistoryBar();
}

void AddressableDockWidget::setSeekHistoryBarEnabled(bool enabled)
{
    historyBarEnabled = enabled;
    updateSeekHistoryBar();
}

void AddressableDockWidget::updateSeekHistoryBar()
{
    if (!seekHistoryBar) {
        return;
    }
    const bool synced = seekable->isSynchronized();
    seekHistoryBar->setVisible(historyBarEnabled && (historyBarAlwaysVisible || !synced));
    seekBackAction->setEnabled(seekable->canSeekPrev());
    seekForwardAction->setEnabled(seekable->canSeekNext());
    seekBackAction->setToolTip(synced ? tr("Back in shared seek history") : tr("Back in this view"));
    seekForwardAction->setToolTip(
        synced ? tr("Forward in shared seek history") : tr("Forward in this view"));
    seekAddressLabel->setText(RAddressString(seekable->getOffset()));
    seekAddressLabel->setToolTip(
        synced ? tr("Shared offset") : tr("Independent offset for this view"));
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

void AddressableDockWidget::updateSyncActionText()
{
    syncAction.setText(seekable->isSynchronized() ? tr("Unsync offset") : tr("Sync offset"));
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
