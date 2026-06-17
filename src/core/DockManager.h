#ifndef DOCKMANAGER_H
#define DOCKMANAGER_H

#include <functional>

#include <QList>
#include <QMap>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QString>
#include <QStringList>

class MainWindow;
class IaitoDockWidget;
class QDockWidget;
class QTabBar;
class QToolButton;
class QMouseEvent;
class QEvent;
class QRubberBand;
class QWidget;

// Owns the live set of dock panels and the homegrown chrome/drag layer that
// used to live inline in MainWindow. This is the seam through which all docking
// behavior is funneled, so the structure can later be backed by an explicit
// layout model without touching MainWindow.
class DockManager : public QObject
{
    Q_OBJECT
public:
    explicit DockManager(MainWindow *mainWindow);
    ~DockManager() override;

    const QList<IaitoDockWidget *> &docks() const { return dockWidgets; }
    const QList<IaitoDockWidget *> &pluginDockWidgets() const { return pluginDocks; }
    void addWidget(IaitoDockWidget *widget);
    void removeWidget(IaitoDockWidget *widget);
    void addPluginDock(IaitoDockWidget *widget) { pluginDocks.push_back(widget); }
    QString getUniqueObjectName(const QString &widgetType) const;
    QStringList getPanelNames() const;
    bool focusPanelByName(const QString &name);

    QMap<QString, std::function<IaitoDockWidget *(MainWindow *)>> &constructorMap()
    {
        return widgetTypeToConstructorMap;
    }

    void applyDockPanelChrome();
    void updateDockTabBars();
    void requestUpdateTabBars();

    bool handleEvent(QObject *watched, QEvent *event);

signals:
    void panelAdded(IaitoDockWidget *widget);
    void panelRemoved(IaitoDockWidget *widget);

private:
    void configureDockWidget(QDockWidget *dock);
    bool updateEmptyDockTabBar(QTabBar *tabBar);
    bool configureDockTabBar(QTabBar *tabBar);
    bool isMainDockTabBar(QTabBar *tabBar) const;
    void updateDockTabCloseButtons(QTabBar *tabBar, int hoveredIndex = -1);
    QToolButton *dockTabCloseButton(QTabBar *tabBar);
    void closeDockTab(QTabBar *tabBar, int index);
    IaitoDockWidget *dockForDockTab(QTabBar *tabBar, int index) const;
    IaitoDockWidget *dockForDockDragHandle(QWidget *handle) const;
    bool maybeStartDockTabDrag(QTabBar *tabBar, QMouseEvent *event);
    bool maybeStartDockHandleDrag(QWidget *handle, QMouseEvent *event);
    void startDockWidgetDrag(IaitoDockWidget *dock, const QPoint &globalPos, const QPoint &offset);
    bool updateDockTabDrag(QMouseEvent *event);
    void updateDockDragPreview(const QPoint &globalPos);
    void finishDockTabDrag(const QPoint &globalPos);
    void clearDockDrag();
    IaitoDockWidget *dockDropTargetAt(const QPoint &globalPos) const;

    MainWindow *mainWindow;

    QList<IaitoDockWidget *> dockWidgets;
    QList<IaitoDockWidget *> pluginDocks;
    QMap<QString, std::function<IaitoDockWidget *(MainWindow *)>> widgetTypeToConstructorMap;

    QPointer<QTabBar> dockDragTabBar;
    QPointer<IaitoDockWidget> dockDragWidget;
    QPointer<QRubberBand> dockDragPreview;
    QPoint dockDragStartGlobalPos;
    QPoint dockDragOffset;
    bool dockTabDragActive = false;
    bool dockDragFloatingPreview = false;
};

#endif // DOCKMANAGER_H
