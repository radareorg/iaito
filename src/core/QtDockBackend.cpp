#include "QtDockBackend.h"

#include <QDockWidget>
#include <QMainWindow>
#include <QTabBar>
#include <QTabWidget>

QtDockBackend::QtDockBackend(QMainWindow *host)
    : host(host)
{}

void QtDockBackend::addDock(QDockWidget *dock, Qt::DockWidgetArea area)
{
    host->addDockWidget(area, dock);
}

void QtDockBackend::splitDock(QDockWidget *first, QDockWidget *second, Qt::Orientation orientation)
{
    host->splitDockWidget(first, second, orientation);
}

void QtDockBackend::tabifyDock(QDockWidget *first, QDockWidget *second)
{
    host->tabifyDockWidget(first, second);
}

void QtDockBackend::removeDock(QDockWidget *dock)
{
    host->removeDockWidget(dock);
}

void QtDockBackend::setDockFloating(QDockWidget *dock, bool floating)
{
    dock->setFloating(floating);
}

QList<QDockWidget *> QtDockBackend::tabifiedWith(QDockWidget *dock) const
{
    return host->tabifiedDockWidgets(dock);
}

Qt::DockWidgetArea QtDockBackend::areaOf(QDockWidget *dock) const
{
    return host->dockWidgetArea(dock);
}

void QtDockBackend::resizeDocksTo(
    const QList<QDockWidget *> &docks, const QList<int> &sizes, Qt::Orientation orientation)
{
    host->resizeDocks(docks, sizes, orientation);
}

QList<QTabBar *> QtDockBackend::dockTabBars() const
{
    return host->findChildren<QTabBar *>();
}

void QtDockBackend::setTabsOnTop(bool onTop)
{
    host->setTabPosition(
        Qt::AllDockWidgetAreas, onTop ? QTabWidget::North : QTabWidget::South);
}

void QtDockBackend::setGroupedDragging(bool enabled)
{
    QMainWindow::DockOptions options = host->dockOptions();
    options.setFlag(QMainWindow::GroupedDragging, enabled);
    host->setDockOptions(options);
}

QByteArray QtDockBackend::saveState() const
{
    return host->saveState();
}

bool QtDockBackend::restoreState(const QByteArray &state)
{
    return host->restoreState(state);
}
