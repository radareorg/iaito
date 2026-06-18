#ifndef DOCKMANAGER_H
#define DOCKMANAGER_H

#include <functional>

#include <QList>
#include <QMap>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QSet>
#include <QString>
#include <QStringList>

class MainWindow;
class DockBackend;
class IaitoDockWidget;
class QDockWidget;
class QTabBar;
class QToolButton;
class QMouseEvent;
class QEvent;
class QWidget;

// Where a dragged panel would land relative to the panel under the cursor.
enum class DockDropKind {
    None,
    Float,
    Tabify,
    SplitLeft,
    SplitRight,
    SplitTop,
    SplitBottom,
};

struct DockDropPlan
{
    IaitoDockWidget *target = nullptr;
    DockDropKind kind = DockDropKind::None;
    IaitoDockWidget *previewHost = nullptr;
    QRect region; // previewHost-local coords, for the preview overlay
};

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
    bool closeCurrentTab();

    bool handleEvent(QObject *watched, QEvent *event);

signals:
    void panelAdded(IaitoDockWidget *widget);
    void panelRemoved(IaitoDockWidget *widget);
    // Emitted whenever the docked arrangement may have changed (add/remove,
    // visibility, float/unfloat) so the owner can re-check layout invariants.
    void layoutMutated();

private:
    void configureDockWidget(QDockWidget *dock);
    bool updateEmptyDockTabBar(QTabBar *tabBar);
    bool configureDockTabBar(QTabBar *tabBar);
    bool isMainDockTabBar(QTabBar *tabBar) const;
    void stampTabBar(QTabBar *tabBar, QSet<IaitoDockWidget *> &claimed);
    void updateDockTabCloseButtons(QTabBar *tabBar, int hoveredIndex = -1);
    QToolButton *dockTabCloseButton(QTabBar *tabBar);
    void closeDockTab(QTabBar *tabBar, int index);
    IaitoDockWidget *dockForDockTab(QTabBar *tabBar, int index) const;
    IaitoDockWidget *dockForDockDragHandle(QWidget *handle) const;
    bool maybeStartDockTabDrag(QTabBar *tabBar, QMouseEvent *event);
    bool maybeStartDockHandleDrag(QWidget *handle, QMouseEvent *event);
    void startDockWidgetDrag(IaitoDockWidget *dock, const QPoint &globalPos, const QPoint &offset);
    bool updateDockTabDrag(QMouseEvent *event);
    DockDropPlan computeDropPlan(const QPoint &globalPos) const;
    void updateDropOverlay(const QPoint &globalPos);
    void finishDockTabDrag(const QPoint &globalPos);
    void clearDockDrag();

    MainWindow *mainWindow;
    DockBackend *backend;

    QList<IaitoDockWidget *> dockWidgets;
    QList<IaitoDockWidget *> pluginDocks;
    QMap<QString, std::function<IaitoDockWidget *(MainWindow *)>> widgetTypeToConstructorMap;

    QPointer<QTabBar> dockDragTabBar;
    QPointer<QWidget> dockDragHandle;
    QPointer<IaitoDockWidget> dockDragWidget;
    QPointer<QWidget> dropOverlay;
    QPoint dockDragStartGlobalPos;
    QPoint dockDragOffset;
    bool dockTabDragActive = false;
    bool updateScheduled = false;
};

#endif // DOCKMANAGER_H
