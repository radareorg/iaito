#include "core/DockManager.h"
#include "core/MainWindow.h"
#include "widgets/IaitoDockWidget.h"

#include <QApplication>
#include <QCursor>
#include <QDockWidget>
#include <QEvent>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPoint>
#include <QRect>
#include <QRubberBand>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QWidget>
#include <QtGlobal>

namespace {

static const char *dockChromeConfiguredProperty = "iaitoDockChromeConfigured";
static const char *dockDragHandleTitleBarProperty = "iaitoDockDragHandleTitleBar";
static const char *dockHiddenTitleBarProperty = "iaitoHiddenDockTitleBar";
static const char *dockTabCloseButtonIndexProperty = "iaitoDockTabCloseButtonIndex";
static const char *dockTabBarConfiguredProperty = "iaitoDockTabBarConfigured";

QWidget *newDockDragHandleTitleBar(QWidget *parent)
{
    auto *handle = new QWidget(parent);
    handle->setObjectName(QStringLiteral("dockDragHandleTitleBar"));
    handle->setProperty(dockDragHandleTitleBarProperty, true);
    handle->setCursor(Qt::SizeAllCursor);
    handle->setFixedHeight(8);
    handle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    handle->setStyleSheet(
        QStringLiteral("QWidget#dockDragHandleTitleBar { border-top: 1px solid palette(mid); }"));
    return handle;
}

QString panelDisplayName(const IaitoDockWidget *dock)
{
    QString name = dock->windowTitle().trimmed();
    if (name.isEmpty() && dock->toggleViewAction()) {
        name = dock->toggleViewAction()->text().trimmed();
    }
    if (name.isEmpty()) {
        name = dock->objectName().trimmed();
    }
    name.remove(QLatin1Char('&'));
    while (name.endsWith(QStringLiteral("..."))) {
        name.chop(3);
        name = name.trimmed();
    }
    return name;
}

QString normalizedDockTabText(QString text)
{
    text.remove(QLatin1Char('&'));
    return text.trimmed();
}

QPoint mouseEventGlobalPos(const QMouseEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->globalPosition().toPoint();
#else
    return event->globalPos();
#endif
}

QPoint mouseEventLocalPos(const QMouseEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position().toPoint();
#else
    return event->pos();
#endif
}

bool canUseNativeDockDragPreview()
{
    return !QGuiApplication::platformName().contains(QStringLiteral("wayland"), Qt::CaseInsensitive);
}

Qt::DockWidgetArea dockAreaForDropPosition(const QRect &globalRect, const QPoint &globalPos)
{
    const QPoint localPos = globalPos - globalRect.topLeft();
    if (localPos.x() < globalRect.width() / 4) {
        return Qt::LeftDockWidgetArea;
    }
    if (localPos.x() > globalRect.width() * 3 / 4) {
        return Qt::RightDockWidgetArea;
    }
    return localPos.y() < globalRect.height() / 2 ? Qt::TopDockWidgetArea
                                                  : Qt::BottomDockWidgetArea;
}

QString normalizedPanelName(const QString &name)
{
    QString normalized;
    for (const QChar &ch : name) {
        if (ch.isLetterOrNumber()) {
            normalized.append(ch.toLower());
        }
    }
    if (normalized.endsWith(QStringLiteral("widget"))) {
        normalized.chop(6);
    }
    return normalized;
}

bool panelNameMatches(const IaitoDockWidget *dock, const QString &normalizedName)
{
    QStringList candidates = {
        panelDisplayName(dock),
        dock->windowTitle(),
        dock->objectName(),
        dock->objectName().split(QLatin1Char(';')).first(),
        QString::fromLatin1(dock->metaObject()->className()),
    };
    if (dock->toggleViewAction()) {
        candidates.append(dock->toggleViewAction()->text());
    }

    for (const QString &candidate : candidates) {
        if (normalizedPanelName(candidate) == normalizedName) {
            return true;
        }
    }
    return false;
}

} // namespace

DockManager::DockManager(MainWindow *mainWindow)
    : QObject(mainWindow)
    , mainWindow(mainWindow)
{
    dockWidgets.reserve(20);
}

DockManager::~DockManager() = default;

void DockManager::addWidget(IaitoDockWidget *widget)
{
    dockWidgets.push_back(widget);
    requestUpdateTabBars();
    emit panelAdded(widget);
}

void DockManager::removeWidget(IaitoDockWidget *widget)
{
    dockWidgets.removeAll(widget);
    pluginDocks.removeAll(widget);
    requestUpdateTabBars();
    emit panelRemoved(widget);
}

QString DockManager::getUniqueObjectName(const QString &widgetType) const
{
    QStringList docks;
    docks.reserve(dockWidgets.size());
    QString name;
    for (const auto &it : dockWidgets) {
        name = it->objectName();
        if (name.split(';').at(0) == widgetType) {
            docks.push_back(name);
        }
    }

    if (docks.isEmpty()) {
        return widgetType;
    }

    int id = 0;
    while (docks.contains(widgetType + ";" + QString::number(id))) {
        id++;
    }

    return widgetType + ";" + QString::number(id);
}

QStringList DockManager::getPanelNames() const
{
    QStringList names;
    names.reserve(dockWidgets.size());
    for (const auto *dock : dockWidgets) {
        const QString name = panelDisplayName(dock);
        if (!name.isEmpty()) {
            names.append(name);
        }
    }
    std::sort(names.begin(), names.end(), [](const QString &a, const QString &b) {
        const int cmp = QString::compare(a, b, Qt::CaseInsensitive);
        return cmp == 0 ? a < b : cmp < 0;
    });
    return names;
}

bool DockManager::focusPanelByName(const QString &name)
{
    const QString normalizedName = normalizedPanelName(name);
    if (normalizedName.isEmpty()) {
        return false;
    }

    for (auto *dock : dockWidgets) {
        if (!panelNameMatches(dock, normalizedName)) {
            continue;
        }
        dock->raiseMemoryWidget();
        dock->activateWindow();
        return true;
    }

    return false;
}

void DockManager::requestUpdateTabBars()
{
    QTimer::singleShot(0, this, &DockManager::updateDockTabBars);
}

void DockManager::applyDockPanelChrome()
{
    for (auto *dockWidget : dockWidgets) {
        configureDockWidget(dockWidget);
    }
    requestUpdateTabBars();
}

void DockManager::configureDockWidget(QDockWidget *dock)
{
    if (!dock) {
        return;
    }

    if (!dock->property(dockChromeConfiguredProperty).toBool()) {
        dock->setProperty(dockChromeConfiguredProperty, true);
        QPointer<QDockWidget> guardedDock(dock);
        connect(dock, &QDockWidget::topLevelChanged, this, [this, guardedDock]() {
            if (!guardedDock) {
                return;
            }
            configureDockWidget(guardedDock);
            requestUpdateTabBars();
        });
    }

    dock->setFeatures(
        QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable
        | QDockWidget::DockWidgetFloatable);

    QWidget *titleBar = dock->titleBarWidget();
    if (dock->isFloating()) {
        if (titleBar
            && (titleBar->property(dockHiddenTitleBarProperty).toBool()
                || titleBar->property(dockDragHandleTitleBarProperty).toBool())) {
            dock->setTitleBarWidget(nullptr);
            titleBar->deleteLater();
        }
        return;
    }

    const bool isSingleDock = mainWindow->tabifiedDockWidgets(dock).isEmpty();
    if (isSingleDock) {
        if (titleBar && titleBar->property(dockDragHandleTitleBarProperty).toBool()) {
            return;
        }

        dock->setTitleBarWidget(newDockDragHandleTitleBar(dock));

        if (titleBar) {
            titleBar->deleteLater();
        }
        return;
    }

    if (titleBar && titleBar->property(dockHiddenTitleBarProperty).toBool()) {
        return;
    }

    auto *hiddenTitleBar = new QWidget(dock);
    hiddenTitleBar->setObjectName(QStringLiteral("hiddenDockTitleBar"));
    hiddenTitleBar->setProperty(dockHiddenTitleBarProperty, true);
    hiddenTitleBar->setFixedHeight(0);
    dock->setTitleBarWidget(hiddenTitleBar);

    if (titleBar) {
        titleBar->deleteLater();
    }
}

void DockManager::updateDockTabBars()
{
    for (auto *dockWidget : dockWidgets) {
        configureDockWidget(dockWidget);
    }
    for (auto *tabBar : mainWindow->findChildren<QTabBar *>()) {
        if (updateEmptyDockTabBar(tabBar)) {
            continue;
        }
        configureDockTabBar(tabBar);
    }
}

bool DockManager::updateEmptyDockTabBar(QTabBar *tabBar)
{
    if (!tabBar) {
        return false;
    }

    const bool managedDockTabBar = tabBar->property(dockTabBarConfiguredProperty).toBool();
    if (tabBar->count() > 0) {
        if (managedDockTabBar && !tabBar->isVisible()) {
            tabBar->show();
        }
        return false;
    }

    if (!managedDockTabBar) {
        return false;
    }

    if (auto *button = tabBar->findChild<QToolButton *>(
            QStringLiteral("dockTabCloseButton"), Qt::FindDirectChildrenOnly)) {
        button->hide();
    }
    tabBar->hide();
    return true;
}

bool DockManager::configureDockTabBar(QTabBar *tabBar)
{
    if (updateEmptyDockTabBar(tabBar)) {
        return false;
    }

    if (!isMainDockTabBar(tabBar)) {
        return false;
    }

    tabBar->setMouseTracking(true);
    tabBar->setAttribute(Qt::WA_Hover, true);
    tabBar->setMovable(true);
    tabBar->setTabsClosable(false);
    dockTabCloseButton(tabBar);

    if (!tabBar->property(dockTabBarConfiguredProperty).toBool()) {
        tabBar->setProperty(dockTabBarConfiguredProperty, true);
        QPointer<QTabBar> guardedTabBar(tabBar);
        connect(tabBar, &QTabBar::currentChanged, this, [this, guardedTabBar]() {
            QTimer::singleShot(0, this, [this, guardedTabBar]() {
                if (guardedTabBar) {
                    updateDockTabCloseButtons(guardedTabBar);
                }
            });
        });
        connect(tabBar, &QTabBar::tabMoved, this, [this, guardedTabBar]() {
            QTimer::singleShot(0, this, [this, guardedTabBar]() {
                if (guardedTabBar) {
                    updateDockTabCloseButtons(guardedTabBar);
                }
            });
        });
    }

    updateDockTabCloseButtons(tabBar);
    return true;
}

bool DockManager::isMainDockTabBar(QTabBar *tabBar) const
{
    if (!tabBar || tabBar->count() == 0 || !tabBar->isVisible()) {
        return false;
    }

    for (int i = 0; i < tabBar->count(); i++) {
        if (!dockForDockTab(tabBar, i)) {
            return false;
        }
    }
    return true;
}

void DockManager::updateDockTabCloseButtons(QTabBar *tabBar, int hoveredIndex)
{
    if (!tabBar) {
        return;
    }

    QToolButton *button = dockTabCloseButton(tabBar);
    if (!button) {
        return;
    }

    if (hoveredIndex < 0) {
        const QPoint cursorPos = tabBar->mapFromGlobal(QCursor::pos());
        if (tabBar->rect().contains(cursorPos)) {
            hoveredIndex = tabBar->tabAt(cursorPos);
        }
    }

    if (hoveredIndex < 0 || !dockForDockTab(tabBar, hoveredIndex)) {
        button->hide();
        return;
    }

    const QRect tabRect = tabBar->tabRect(hoveredIndex);
    const int buttonSize = qBound(14, tabRect.height() - 2, 20);
    button->setFixedSize(buttonSize, buttonSize);
    button->setProperty(dockTabCloseButtonIndexProperty, hoveredIndex);
    const int buttonInset = qMax(2, (tabRect.height() - buttonSize) / 2);
    button->move(QPoint(tabRect.left() + buttonInset, tabRect.center().y() - buttonSize / 2));
    button->show();
    button->raise();
}

QToolButton *DockManager::dockTabCloseButton(QTabBar *tabBar)
{
    if (!tabBar) {
        return nullptr;
    }

    auto *button = tabBar->findChild<QToolButton *>(
        QStringLiteral("dockTabCloseButton"), Qt::FindDirectChildrenOnly);
    if (button) {
        return button;
    }

    button = new QToolButton(tabBar);
    button->setObjectName(QStringLiteral("dockTabCloseButton"));
    button->setAutoRaise(true);
    button->setCursor(Qt::ArrowCursor);
    button->setFocusPolicy(Qt::NoFocus);
    button->setText(QStringLiteral("x"));
    button->setStyleSheet(QStringLiteral(
        "QToolButton { border: 0px; background: transparent; font-weight: bold; padding: 0px; }"
        "QToolButton:hover { background: palette(window); }"));
    button->setToolTip(tr("Close panel"));
    button->hide();
    connect(button, &QToolButton::clicked, this, [this, button]() {
        auto *tabBar = qobject_cast<QTabBar *>(button->parentWidget());
        closeDockTab(tabBar, button->property(dockTabCloseButtonIndexProperty).toInt());
    });
    return button;
}

void DockManager::closeDockTab(QTabBar *tabBar, int index)
{
    if (auto *dock = dockForDockTab(tabBar, index)) {
        dock->hide();
    }
    if (auto *button = dockTabCloseButton(tabBar)) {
        button->hide();
    }
    requestUpdateTabBars();
}

IaitoDockWidget *DockManager::dockForDockTab(QTabBar *tabBar, int index) const
{
    if (!tabBar || index < 0 || index >= tabBar->count()) {
        return nullptr;
    }

    if (QObject *object = tabBar->tabData(index).value<QObject *>()) {
        if (auto *dock = qobject_cast<IaitoDockWidget *>(object)) {
            if (dockWidgets.contains(dock)) {
                return dock;
            }
        }
    }

    const QString requestedText = normalizedDockTabText(tabBar->tabText(index));
    if (requestedText.isEmpty()) {
        return nullptr;
    }

    for (auto *dock : dockWidgets) {
        if (dock && normalizedDockTabText(panelDisplayName(dock)) == requestedText) {
            return dock;
        }
    }
    return nullptr;
}

IaitoDockWidget *DockManager::dockForDockDragHandle(QWidget *handle) const
{
    if (!handle || !handle->property(dockDragHandleTitleBarProperty).toBool()) {
        return nullptr;
    }

    for (QWidget *widget = handle; widget; widget = widget->parentWidget()) {
        if (auto *dock = qobject_cast<IaitoDockWidget *>(widget)) {
            return dockWidgets.contains(dock) ? dock : nullptr;
        }
    }
    return nullptr;
}

bool DockManager::maybeStartDockTabDrag(QTabBar *tabBar, QMouseEvent *event)
{
    if (!tabBar || !event || dockDragTabBar != tabBar || !dockDragWidget) {
        return false;
    }
    if (!(event->buttons() & Qt::LeftButton)) {
        return false;
    }

    const QPoint globalPos = mouseEventGlobalPos(event);
    const int dragDistance = (globalPos - dockDragStartGlobalPos).manhattanLength();
    if (dragDistance < QApplication::startDragDistance()) {
        return false;
    }

    const QPoint delta = globalPos - dockDragStartGlobalPos;
    if (qAbs(delta.x()) >= qAbs(delta.y())) {
        return false;
    }

    IaitoDockWidget *dock = dockDragWidget.data();
    if (!dock) {
        return false;
    }

    if (auto *button = dockTabCloseButton(tabBar)) {
        button->hide();
    }

    startDockWidgetDrag(dock, globalPos, QPoint(qMin(dock->width() / 2, 120), 12));
    return true;
}

bool DockManager::maybeStartDockHandleDrag(QWidget *handle, QMouseEvent *event)
{
    if (!handle || !event || !dockDragWidget || dockDragTabBar) {
        return false;
    }
    if (!(event->buttons() & Qt::LeftButton)) {
        return false;
    }

    const QPoint globalPos = mouseEventGlobalPos(event);
    const int dragDistance = (globalPos - dockDragStartGlobalPos).manhattanLength();
    if (dragDistance < QApplication::startDragDistance()) {
        return false;
    }

    if (dockDragWidget != dockForDockDragHandle(handle)) {
        return false;
    }

    startDockWidgetDrag(dockDragWidget, globalPos, dockDragOffset);
    return true;
}

void DockManager::startDockWidgetDrag(
    IaitoDockWidget *dock, const QPoint &globalPos, const QPoint &offset)
{
    if (!dock) {
        return;
    }

    dockTabDragActive = true;
    dockDragOffset = offset;
    dockDragFloatingPreview = canUseNativeDockDragPreview();
    if (dockDragFloatingPreview) {
        configureDockWidget(dock);
        dock->setFloating(true);
        dock->move(globalPos - dockDragOffset);
        dock->show();
        dock->raise();
        dock->activateWindow();
        dock->grabMouse();
    } else {
        updateDockDragPreview(globalPos);
    }
}

bool DockManager::updateDockTabDrag(QMouseEvent *event)
{
    if (!dockTabDragActive || !event || !dockDragWidget) {
        return false;
    }

    const QPoint globalPos = mouseEventGlobalPos(event);
    if (dockDragFloatingPreview) {
        dockDragWidget->move(globalPos - dockDragOffset);
        dockDragWidget->raise();
    } else {
        updateDockDragPreview(globalPos);
    }

    if (!(event->buttons() & Qt::LeftButton)) {
        finishDockTabDrag(globalPos);
    }
    return true;
}

void DockManager::updateDockDragPreview(const QPoint &globalPos)
{
    if (!dockDragWidget) {
        return;
    }

    if (!dockDragPreview) {
        dockDragPreview = new QRubberBand(QRubberBand::Rectangle, mainWindow);
        dockDragPreview->setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    dockDragPreview->setGeometry(
        QRect(mainWindow->mapFromGlobal(globalPos - dockDragOffset), dockDragWidget->size()));
    dockDragPreview->show();
    dockDragPreview->raise();
}

void DockManager::finishDockTabDrag(const QPoint &globalPos)
{
    IaitoDockWidget *dock = dockDragWidget.data();
    IaitoDockWidget *targetDock = dockDropTargetAt(globalPos);
    bool dockPlaced = false;

    if (dock && dockDragFloatingPreview) {
        dock->releaseMouse();
    }

    if (dockDragPreview) {
        dockDragPreview->hide();
    }

    if (dock && targetDock && dock != targetDock) {
        QRect targetRect(targetDock->mapToGlobal(QPoint(0, 0)), targetDock->size());
        const bool targetWasTabifiedWithDragged
            = mainWindow->tabifiedDockWidgets(dock).contains(targetDock);
        if (targetWasTabifiedWithDragged) {
            if (!targetDock->isVisible() || targetRect.isEmpty()) {
                targetRect = QRect(dock->mapToGlobal(QPoint(0, 0)), dock->size());
            }
            targetDock->show();
            targetDock->raise();
        }
        const QPoint targetPos = globalPos - targetRect.topLeft();
        const int edgeThreshold = qMax(24, qMin(targetRect.width(), targetRect.height()) / 4);
        const int leftDistance = targetPos.x();
        const int rightDistance = targetRect.width() - targetPos.x();
        const int topDistance = targetPos.y();
        const int bottomDistance = targetRect.height() - targetPos.y();
        const int closestHorizontal = qMin(leftDistance, rightDistance);
        const int closestVertical = qMin(topDistance, bottomDistance);
        const bool shouldSplit = targetRect.contains(globalPos)
                                 && qMin(closestHorizontal, closestVertical) <= edgeThreshold;
        const Qt::Orientation splitOrientation = closestHorizontal < closestVertical
                                                     ? Qt::Horizontal
                                                     : Qt::Vertical;
        Qt::DockWidgetArea targetArea = mainWindow->dockWidgetArea(targetDock);
        if (targetArea == Qt::NoDockWidgetArea) {
            targetArea = Qt::TopDockWidgetArea;
        }

        mainWindow->removeDockWidget(dock);
        dock->setParent(mainWindow);
        dock->setFloating(false);
        mainWindow->addDockWidget(targetArea, dock);
        if (shouldSplit) {
            mainWindow->splitDockWidget(targetDock, dock, splitOrientation);
        } else {
            mainWindow->tabifyDockWidget(targetDock, dock);
        }
        dockPlaced = mainWindow->dockWidgetArea(dock) != Qt::NoDockWidgetArea;
    } else if (dock) {
        const QRect mainRect(
            mainWindow->mapToGlobal(mainWindow->rect().topLeft()), mainWindow->rect().size());
        if (mainRect.contains(globalPos)) {
            mainWindow->removeDockWidget(dock);
            dock->setParent(mainWindow);
            dock->setFloating(false);
            mainWindow->addDockWidget(dockAreaForDropPosition(mainRect, globalPos), dock);
            dockPlaced = mainWindow->dockWidgetArea(dock) != Qt::NoDockWidgetArea;
        }
    }

    if (dock) {
        if (!dockPlaced) {
            dock->setFloating(true);
            dock->move(globalPos - dockDragOffset);
        }
        configureDockWidget(dock);
        if (targetDock && targetDock != dock) {
            configureDockWidget(targetDock);
        }
        dock->show();
        dock->raise();
    }

    clearDockDrag();
    requestUpdateTabBars();
}

void DockManager::clearDockDrag()
{
    if (dockDragPreview) {
        dockDragPreview->hide();
    }
    dockDragTabBar.clear();
    dockDragWidget.clear();
    dockDragStartGlobalPos = QPoint();
    dockDragOffset = QPoint();
    dockTabDragActive = false;
    dockDragFloatingPreview = false;
}

IaitoDockWidget *DockManager::dockDropTargetAt(const QPoint &globalPos) const
{
    IaitoDockWidget *draggedDock = dockDragWidget.data();

    for (auto *tabBar : mainWindow->findChildren<QTabBar *>()) {
        if (!isMainDockTabBar(tabBar)) {
            continue;
        }
        const int index = tabBar->tabAt(tabBar->mapFromGlobal(globalPos));
        if (auto *dock = dockForDockTab(tabBar, index)) {
            if (dock != draggedDock) {
                return dock;
            }
        }
    }

    for (auto *dock : dockWidgets) {
        if (!dock || dock == draggedDock || !dock->isVisible() || dock->isFloating()) {
            continue;
        }

        const QRect dockRect(dock->mapToGlobal(QPoint(0, 0)), dock->size());
        if (dockRect.contains(globalPos)) {
            return dock;
        }
    }

    if (draggedDock && !draggedDock->isFloating()) {
        const QRect draggedRect(draggedDock->mapToGlobal(QPoint(0, 0)), draggedDock->size());
        if (draggedRect.contains(globalPos)) {
            for (auto *tabifiedDock : mainWindow->tabifiedDockWidgets(draggedDock)) {
                auto *dock = qobject_cast<IaitoDockWidget *>(tabifiedDock);
                if (dock && dockWidgets.contains(dock) && !dock->isFloating()) {
                    return dock;
                }
            }
        }
    }

    return nullptr;
}

bool DockManager::handleEvent(QObject *watched, QEvent *event)
{
    if (dockTabDragActive) {
        if (event->type() == QEvent::MouseMove) {
            if (updateDockTabDrag(static_cast<QMouseEvent *>(event))) {
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            finishDockTabDrag(mouseEventGlobalPos(static_cast<QMouseEvent *>(event)));
            return true;
        }
    }

    if (auto *tabBar = qobject_cast<QTabBar *>(watched)) {
        auto canHandleDockTabBar = [this, tabBar]() {
            return tabBar->property(dockTabBarConfiguredProperty).toBool()
                   && isMainDockTabBar(tabBar);
        };
        switch (event->type()) {
        case QEvent::Enter:
        case QEvent::HoverEnter:
        case QEvent::HoverMove:
            if (configureDockTabBar(tabBar)) {
                updateDockTabCloseButtons(tabBar);
            }
            break;
        case QEvent::MouseButtonPress: {
            if (!canHandleDockTabBar()) {
                break;
            }
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                const int index = tabBar->tabAt(mouseEventLocalPos(mouseEvent));
                dockDragTabBar = tabBar;
                dockDragWidget = dockForDockTab(tabBar, index);
                dockDragStartGlobalPos = mouseEventGlobalPos(mouseEvent);
                updateDockTabCloseButtons(tabBar, index);
            }
            break;
        }
        case QEvent::MouseMove: {
            if (!canHandleDockTabBar()) {
                break;
            }
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            const int index = tabBar->tabAt(mouseEventLocalPos(mouseEvent));
            updateDockTabCloseButtons(tabBar, index);
            if (maybeStartDockTabDrag(tabBar, mouseEvent)) {
                return true;
            }
            break;
        }
        case QEvent::Leave:
            if (canHandleDockTabBar()) {
                updateDockTabCloseButtons(tabBar, -1);
            }
            break;
        case QEvent::MouseButtonRelease:
            if (!canHandleDockTabBar()) {
                break;
            }
            if (dockTabDragActive) {
                finishDockTabDrag(mouseEventGlobalPos(static_cast<QMouseEvent *>(event)));
                return true;
            }
            clearDockDrag();
            updateDockTabCloseButtons(tabBar);
            break;
        case QEvent::Show:
        case QEvent::Resize:
        case QEvent::LayoutRequest: {
            QPointer<QTabBar> guardedTabBar(tabBar);
            QTimer::singleShot(0, this, [this, guardedTabBar]() {
                if (guardedTabBar) {
                    if (updateEmptyDockTabBar(guardedTabBar)) {
                        return;
                    }
                    configureDockTabBar(guardedTabBar);
                }
            });
        } break;
        default:
            break;
        }
    }

    if (auto *handle = qobject_cast<QWidget *>(watched)) {
        if (handle->property(dockDragHandleTitleBarProperty).toBool()) {
            switch (event->type()) {
            case QEvent::MouseButtonPress: {
                auto *mouseEvent = static_cast<QMouseEvent *>(event);
                if (mouseEvent->button() == Qt::LeftButton) {
                    clearDockDrag();
                    dockDragWidget = dockForDockDragHandle(handle);
                    dockDragStartGlobalPos = mouseEventGlobalPos(mouseEvent);
                    if (dockDragWidget) {
                        dockDragOffset
                            = dockDragStartGlobalPos - dockDragWidget->mapToGlobal(QPoint(0, 0));
                    } else {
                        dockDragOffset = QPoint();
                    }
                    return dockDragWidget != nullptr;
                }
                break;
            }
            case QEvent::MouseMove:
                if (maybeStartDockHandleDrag(handle, static_cast<QMouseEvent *>(event))) {
                    return true;
                }
                break;
            case QEvent::MouseButtonRelease:
                clearDockDrag();
                break;
            default:
                break;
            }
        }
    }

    return false;
}
