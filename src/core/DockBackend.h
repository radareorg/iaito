#ifndef DOCKBACKEND_H
#define DOCKBACKEND_H

#include <QByteArray>
#include <QList>
#include <Qt>

class QDockWidget;
class QTabBar;

// Abstract realization backend for the dock layout model. The DockManager owns
// the structure (the node tree); the backend turns structural intent into a
// concrete widget arrangement. QtDockBackend realizes it onto QMainWindow's
// native docking; a future backend could use QSplitter/QStackedWidget instead
// without touching the model, registry, drag, or MainWindow.
class DockBackend
{
public:
    virtual ~DockBackend() = default;

    virtual void addDock(QDockWidget *dock, Qt::DockWidgetArea area) = 0;
    virtual void splitDock(QDockWidget *first, QDockWidget *second, Qt::Orientation orientation) = 0;
    virtual void tabifyDock(QDockWidget *first, QDockWidget *second) = 0;
    virtual void removeDock(QDockWidget *dock) = 0;
    virtual void setDockFloating(QDockWidget *dock, bool floating) = 0;

    virtual QList<QDockWidget *> tabifiedWith(QDockWidget *dock) const = 0;
    virtual Qt::DockWidgetArea areaOf(QDockWidget *dock) const = 0;
    virtual void resizeDocksTo(
        const QList<QDockWidget *> &docks, const QList<int> &sizes, Qt::Orientation orientation)
        = 0;

    virtual QList<QTabBar *> dockTabBars() const = 0;
    virtual void setTabsOnTop(bool onTop) = 0;
    virtual void setGroupedDragging(bool enabled) = 0;

    virtual QByteArray saveState() const = 0;
    virtual bool restoreState(const QByteArray &state) = 0;
};

#endif // DOCKBACKEND_H
