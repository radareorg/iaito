#ifndef QTDOCKBACKEND_H
#define QTDOCKBACKEND_H

#include "DockBackend.h"

class QMainWindow;

// DockBackend realized on a QMainWindow's native QDockWidget docking. Every
// method forwards to the equivalent QMainWindow/QDockWidget call, so behavior
// stays identical to the inline implementation it replaces.
class QtDockBackend : public DockBackend
{
public:
    explicit QtDockBackend(QMainWindow *host);

    void addDock(QDockWidget *dock, Qt::DockWidgetArea area) override;
    void splitDock(QDockWidget *first, QDockWidget *second, Qt::Orientation orientation) override;
    void tabifyDock(QDockWidget *first, QDockWidget *second) override;
    void removeDock(QDockWidget *dock) override;
    void setDockFloating(QDockWidget *dock, bool floating) override;

    QList<QDockWidget *> tabifiedWith(QDockWidget *dock) const override;
    Qt::DockWidgetArea areaOf(QDockWidget *dock) const override;
    void resizeDocksTo(
        const QList<QDockWidget *> &docks,
        const QList<int> &sizes,
        Qt::Orientation orientation) override;

    QList<QTabBar *> dockTabBars() const override;
    void setTabsOnTop(bool onTop) override;
    void setGroupedDragging(bool enabled) override;

    QByteArray saveState() const override;
    bool restoreState(const QByteArray &state) override;

private:
    QMainWindow *host;
};

#endif // QTDOCKBACKEND_H
