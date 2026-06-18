#include "core/DockManager.h"
#include "core/MainWindow.h"
#include "core/QtDockBackend.h"
#include "widgets/IaitoDockWidget.h"

#include <QApplication>
#include <QCursor>
#include <QDockWidget>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPoint>
#include <QRect>
#include <QSizeGrip>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QVariant>
#include <QWidget>
#include <QtGlobal>

#include <algorithm>

namespace {

static const char *dockChromeConfiguredProperty = "iaitoDockChromeConfigured";
static const char *dockDragHandleTitleBarProperty = "iaitoDockDragHandleTitleBar";
static const char *dockHandleControlsProperty = "iaitoDockHandleControls";
static const char *dockHandleResizeGripProperty = "iaitoDockHandleResizeGrip";
static const char *dockTabCloseButtonIndexProperty = "iaitoDockTabCloseButtonIndex";
static const char *dockTabBarConfiguredProperty = "iaitoDockTabBarConfigured";

// Translucent child widget that highlights, in accent color, the region a
// dragged panel would occupy if dropped now. Transparent to mouse input so it
// never interferes with the drag.
class DockDropOverlay : public QWidget
{
public:
    explicit DockDropOverlay(QWidget *parent)
        : QWidget(parent)
    {
        // A child of the main window positioned in local coordinates — top-level
        // overlays can't be placed at absolute coordinates on Wayland, which made
        // the preview land in random spots.
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QColor fill = palette().highlight().color();
        fill.setAlpha(64);
        QColor border = palette().highlight().color();
        border.setAlpha(200);
        const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
        painter.setPen(QPen(border, 2));
        painter.setBrush(fill);
        painter.drawRoundedRect(r, 4, 4);
    }
};

QWidget *newDockDragHandleTitleBar(QWidget *parent, bool withControls, bool withResizeGrip)
{
    auto *dock = qobject_cast<QDockWidget *>(parent);
    auto *handle = new QWidget(parent);
    handle->setObjectName(QStringLiteral("dockDragHandleTitleBar"));
    handle->setProperty(dockDragHandleTitleBarProperty, true);
    handle->setProperty(dockHandleControlsProperty, withControls);
    handle->setProperty(dockHandleResizeGripProperty, withResizeGrip);
    handle->setCursor(Qt::SizeAllCursor);
    handle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    handle->setStyleSheet(
        QStringLiteral("QWidget#dockDragHandleTitleBar { border-top: 1px solid palette(mid); }"));

    if (!withControls) {
        // Tabbed panel: the tab already shows the title and the close-on-hover
        // button, so the bar is just a thin grip for dragging the panel around.
        handle->setFixedHeight(8);
        return handle;
    }

    handle->setFixedHeight(18);
    auto *layout = new QHBoxLayout(handle);
    layout->setContentsMargins(6, 0, withResizeGrip ? 0 : 2, 0);
    layout->setSpacing(2);

    // Transparent to mouse so dragging the title still drags the whole bar.
    auto *title = new QLabel(dock ? dock->windowTitle() : QString(), handle);
    title->setObjectName(QStringLiteral("dockDragHandleTitle"));
    title->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout->addWidget(title, 1);

    auto *closeButton = new QToolButton(handle);
    closeButton->setObjectName(QStringLiteral("dockHandleCloseButton"));
    closeButton->setAutoRaise(true);
    closeButton->setCursor(Qt::ArrowCursor);
    closeButton->setFocusPolicy(Qt::NoFocus);
    closeButton->setText(QStringLiteral("x"));
    closeButton->setFixedSize(14, 14);
    closeButton->setToolTip(QObject::tr("Close panel"));
    closeButton->setStyleSheet(QStringLiteral(
        "QToolButton { border: 0px; background: transparent; font-weight: bold; padding: 0px; }"
        "QToolButton:hover { background: palette(window); }"));
    if (dock) {
        QObject::connect(closeButton, &QToolButton::clicked, dock, [dock]() { dock->close(); });
    }
    layout->addWidget(closeButton, 0);

    if (withResizeGrip) {
        auto *grip = new QSizeGrip(handle);
        grip->setObjectName(QStringLiteral("dockFloatingResizeGrip"));
        grip->setCursor(Qt::SizeFDiagCursor);
        grip->setToolTip(QObject::tr("Resize panel"));
        layout->addWidget(grip, 0);
    }

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

DockDropKind dockDropKindForPoint(const QPoint &pos, const QSize &size)
{
    if (size.isEmpty()) {
        return DockDropKind::Tabify;
    }

    const double edge = 0.25;
    const double fx = double(pos.x()) / size.width();
    const double fy = double(pos.y()) / size.height();

    // Use explicit 3x3 zones instead of "nearest edge". Nearest-edge selection
    // makes top/bottom splits feel almost impossible in narrow left/right docks,
    // because a side edge wins even when the cursor is clearly in the top band.
    if (fy < edge) {
        return DockDropKind::SplitTop;
    }
    if (fy > 1.0 - edge) {
        return DockDropKind::SplitBottom;
    }
    if (fx < edge) {
        return DockDropKind::SplitLeft;
    }
    if (fx > 1.0 - edge) {
        return DockDropKind::SplitRight;
    }
    return DockDropKind::Tabify;
}

QRect dockDropPreviewRegion(DockDropKind kind, const QSize &size)
{
    const int halfW = size.width() / 2;
    const int halfH = size.height() / 2;
    switch (kind) {
    case DockDropKind::SplitLeft:
        return QRect(0, 0, halfW, size.height());
    case DockDropKind::SplitRight:
        return QRect(size.width() - halfW, 0, halfW, size.height());
    case DockDropKind::SplitTop:
        return QRect(0, 0, size.width(), halfH);
    case DockDropKind::SplitBottom:
        return QRect(0, size.height() - halfH, size.width(), halfH);
    case DockDropKind::Tabify:
        return QRect(QPoint(0, 0), size);
    default:
        return QRect();
    }
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
    , backend(new QtDockBackend(mainWindow))
{
    dockWidgets.reserve(20);
}

DockManager::~DockManager()
{
    delete dropOverlay;
    delete backend;
}

void DockManager::addWidget(IaitoDockWidget *widget)
{
    dockWidgets.push_back(widget);
    connect(widget, &QDockWidget::visibilityChanged, this, &DockManager::layoutMutated);
    connect(widget, &QDockWidget::topLevelChanged, this, &DockManager::layoutMutated);
    requestUpdateTabBars();
    emit panelAdded(widget);
    emit layoutMutated();
}

void DockManager::removeWidget(IaitoDockWidget *widget)
{
    dockWidgets.removeAll(widget);
    pluginDocks.removeAll(widget);
    requestUpdateTabBars();
    emit panelRemoved(widget);
    emit layoutMutated();
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
    // Coalesce the many mutation sites (add/remove/close/drag/relayout) into a
    // single reconciliation per event-loop turn instead of one full pass each.
    if (updateScheduled) {
        return;
    }
    updateScheduled = true;
    QTimer::singleShot(0, this, [this]() {
        updateScheduled = false;
        updateDockTabBars();
    });
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

    // A tabbed docked panel gets a bare thin grip (its tab already carries the
    // title and close button). A standalone or floating panel gets title +
    // controls in the handle. Floating panels keep this handle so they can be
    // dragged back into the main window; a size grip provides resizing when the
    // platform suppresses native dock-window borders for custom title bars.
    QWidget *titleBar = dock->titleBarWidget();
    const bool floating = dock->isFloating();
    const bool tabbed = !floating && !backend->tabifiedWith(dock).isEmpty();
    const bool wantControls = floating || !tabbed;
    if (titleBar && titleBar->property(dockDragHandleTitleBarProperty).toBool()
        && titleBar->property(dockHandleControlsProperty).toBool() == wantControls
        && titleBar->property(dockHandleResizeGripProperty).toBool() == floating) {
        if (wantControls) {
            if (auto *label = titleBar->findChild<QLabel *>(QStringLiteral("dockDragHandleTitle"))) {
                label->setText(dock->windowTitle());
            }
        }
        return;
    }
    dock->setTitleBarWidget(newDockDragHandleTitleBar(dock, wantControls, floating));
    if (titleBar) {
        titleBar->deleteLater();
    }
}

void DockManager::updateDockTabBars()
{
    for (auto *dockWidget : dockWidgets) {
        configureDockWidget(dockWidget);
    }
    QSet<IaitoDockWidget *> claimed;
    for (auto *tabBar : backend->dockTabBars()) {
        if (updateEmptyDockTabBar(tabBar)) {
            continue;
        }
        if (configureDockTabBar(tabBar)) {
            stampTabBar(tabBar, claimed);
        }
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

void DockManager::stampTabBar(QTabBar *tabBar, QSet<IaitoDockWidget *> &claimed)
{
    // Qt labels each tab with the dock's window title. Resolve every tab to its
    // dock once (claiming across all bars so duplicate titles map bijectively)
    // and record the dock pointer in tabData, so later lookups are exact instead
    // of relying on title-string matching.
    for (int i = 0; i < tabBar->count(); ++i) {
        const QString want = normalizedDockTabText(tabBar->tabText(i));
        if (want.isEmpty()) {
            continue;
        }
        for (auto *dock : dockWidgets) {
            if (claimed.contains(dock)) {
                continue;
            }
            if (normalizedDockTabText(panelDisplayName(dock)) == want) {
                tabBar->setTabData(i, QVariant::fromValue<QObject *>(dock));
                claimed.insert(dock);
                break;
            }
        }
    }
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

bool DockManager::closeCurrentTab()
{
    for (QWidget *widget = QApplication::focusWidget(); widget; widget = widget->parentWidget()) {
        if (auto *tabBar = qobject_cast<QTabBar *>(widget)) {
            if (!isMainDockTabBar(tabBar)) {
                continue;
            }
            closeDockTab(tabBar, tabBar->currentIndex());
            return true;
        }
        if (auto *dock = qobject_cast<IaitoDockWidget *>(widget)) {
            if (!dockWidgets.contains(dock)) {
                continue;
            }
            dock->hide();
            requestUpdateTabBars();
            return true;
        }
    }
    return false;
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

    const QPoint localPos = tabBar->mapFromGlobal(globalPos);
    const QPoint delta = globalPos - dockDragStartGlobalPos;
    if (tabBar->rect().contains(localPos) && qAbs(delta.x()) >= qAbs(delta.y())) {
        return false;
    }

    IaitoDockWidget *dock = dockDragWidget.data();
    if (!dock) {
        return false;
    }

    if (auto *button = dockTabCloseButton(tabBar)) {
        button->hide();
    }

    startDockWidgetDrag(
        dock, globalPos, QPoint(qMin(dock->width() / 2, 120), qMin(tabBar->height(), 24)));
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
    updateDropOverlay(globalPos);
}

bool DockManager::updateDockTabDrag(QMouseEvent *event)
{
    if (!dockTabDragActive || !event || !dockDragWidget) {
        return false;
    }

    const QPoint globalPos = mouseEventGlobalPos(event);
    // The real window is never moved during the drag: only the colored overlay
    // tracks the cursor. Wayland forbids apps from repositioning top-level
    // windows, so moving a floating dock to follow the cursor silently fails;
    // the drop is computed from the cursor position regardless.
    updateDropOverlay(globalPos);

    if (!(event->buttons() & Qt::LeftButton)) {
        finishDockTabDrag(globalPos);
    }
    return true;
}

DockDropPlan DockManager::computeDropPlan(const QPoint &globalPos) const
{
    DockDropPlan plan;
    IaitoDockWidget *draggedDock = dockDragWidget.data();

    // Hit-test in main-window-local coordinates via Qt's own childAt(): absolute
    // (global) coordinates are unreliable on Wayland because the app doesn't know
    // its window's screen position, which made the central panel un-hittable and
    // drops land unpredictably. The preview region is in preview-host-local
    // coordinates for the same reason (the overlay is a child of that panel).
    const QPoint mainLocal = mainWindow->mapFromGlobal(globalPos);
    const bool insideWindow = mainWindow->rect().contains(mainLocal);

    IaitoDockWidget *hoverDock = nullptr;
    IaitoDockWidget *currentTabDock = nullptr;
    IaitoDockWidget *target = nullptr;
    bool viaTabBar = false;
    if (insideWindow) {
        for (QWidget *w = mainWindow->childAt(mainLocal); w; w = w->parentWidget()) {
            if (auto *tabBar = qobject_cast<QTabBar *>(w)) {
                if (isMainDockTabBar(tabBar)) {
                    int index = tabBar->tabAt(tabBar->mapFromGlobal(globalPos));
                    if (index < 0) {
                        index = tabBar->currentIndex();
                    }
                    currentTabDock = dockForDockTab(tabBar, tabBar->currentIndex());
                    hoverDock = dockForDockTab(tabBar, index);
                    target = hoverDock;
                    viaTabBar = true;
                }
                break;
            }
            if (auto *dock = qobject_cast<IaitoDockWidget *>(w)) {
                if (dockWidgets.contains(dock)) {
                    hoverDock = dock;
                    target = dock;
                }
                break;
            }
        }
    }

    IaitoDockWidget *visibleTabHost = currentTabDock && currentTabDock->isVisible() ? currentTabDock
                                                                                    : nullptr;
    if (target && (target->isVisible() || (viaTabBar && visibleTabHost))) {
        IaitoDockWidget *previewHost = target->isVisible() ? target : visibleTabHost;
        const int w = previewHost->width();
        const int h = previewHost->height();
        plan.previewHost = previewHost;
        if (viaTabBar) {
            const bool sameGroup = draggedDock
                                   && (target == draggedDock
                                       || (!draggedDock->isFloating()
                                           && backend->tabifiedWith(draggedDock).contains(target)));
            if (!sameGroup) {
                plan.target = target;
                plan.kind = DockDropKind::Tabify;
                plan.region = QRect(0, 0, w, h);
            }
            return plan;
        }

        const QPoint tl = previewHost->mapFrom(mainWindow, mainLocal);
        plan.kind = dockDropKindForPoint(tl, previewHost->size());
        plan.region = dockDropPreviewRegion(plan.kind, previewHost->size());

        const bool isSplit = plan.kind == DockDropKind::SplitLeft
                             || plan.kind == DockDropKind::SplitRight
                             || plan.kind == DockDropKind::SplitTop
                             || plan.kind == DockDropKind::SplitBottom;
        if (!draggedDock) {
            plan.target = target;
            return plan;
        }

        const QList<QDockWidget *> tabGroup = backend->tabifiedWith(draggedDock);
        const bool sameGroup = target == draggedDock
                               || (!draggedDock->isFloating() && tabGroup.contains(target));
        if (!sameGroup) {
            plan.target = target;
            return plan;
        }

        if (!isSplit) {
            plan.kind = DockDropKind::None;
            plan.region = QRect();
            return plan;
        }

        for (auto *candidate : tabGroup) {
            auto *dock = qobject_cast<IaitoDockWidget *>(candidate);
            if (dock && dock != draggedDock && dockWidgets.contains(dock) && !dock->isFloating()) {
                plan.target = dock;
                return plan;
            }
        }

        plan.kind = DockDropKind::None;
        plan.region = QRect();
        return plan;
    }

    // Inside the window but over no panel (e.g. a gap): tabify into the largest
    // visible docked panel so the layout stays one connected tree.
    if (insideWindow) {
        IaitoDockWidget *largest = nullptr;
        qint64 bestArea = 0;
        for (auto *dock : dockWidgets) {
            if (!dock || dock == draggedDock || !dock->isVisible() || dock->isFloating()) {
                continue;
            }
            const qint64 area = qint64(dock->width()) * dock->height();
            if (area > bestArea) {
                bestArea = area;
                largest = dock;
            }
        }
        if (largest) {
            plan.target = largest;
            plan.previewHost = largest;
            plan.kind = DockDropKind::Tabify;
            plan.region = QRect(0, 0, largest->width(), largest->height());
            return plan;
        }
    }

    // Dropped outside the window entirely: tear off as a floating window.
    plan.kind = DockDropKind::Float;
    return plan;
}

void DockManager::updateDropOverlay(const QPoint &globalPos)
{
    const DockDropPlan plan = computeDropPlan(globalPos);
    if (!plan.previewHost || plan.region.isEmpty() || plan.kind == DockDropKind::None
        || plan.kind == DockDropKind::Float) {
        if (dropOverlay) {
            dropOverlay->hide();
        }
        return;
    }
    // The overlay is a child of the preview host and the region is already in
    // that panel's local coordinates, so there is no global-coordinate math
    // (which is unreliable on Wayland).
    if (!dropOverlay) {
        dropOverlay = new DockDropOverlay(plan.previewHost);
    } else if (dropOverlay->parentWidget() != plan.previewHost) {
        dropOverlay->setParent(plan.previewHost);
    }
    dropOverlay->setGeometry(plan.region);
    dropOverlay->show();
    dropOverlay->raise();
}

void DockManager::finishDockTabDrag(const QPoint &globalPos)
{
    IaitoDockWidget *dock = dockDragWidget.data();
    if (dropOverlay) {
        dropOverlay->hide();
    }
    if (!dock) {
        clearDockDrag();
        return;
    }

    const DockDropPlan plan = computeDropPlan(globalPos);
    IaitoDockWidget *targetDock = plan.target;

    // Dropped on its own area or nothing actionable: leave the panel as it is.
    if (plan.kind == DockDropKind::None) {
        configureDockWidget(dock);
        dock->show();
        dock->raise();
        clearDockDrag();
        requestUpdateTabBars();
        return;
    }

    const bool isSplit = plan.kind == DockDropKind::SplitLeft
                         || plan.kind == DockDropKind::SplitRight
                         || plan.kind == DockDropKind::SplitTop
                         || plan.kind == DockDropKind::SplitBottom;
    bool dockPlaced = false;

    if (targetDock && targetDock != dock && (plan.kind == DockDropKind::Tabify || isSplit)) {
        if (!targetDock->isVisible()) {
            targetDock->show();
            targetDock->raise();
        }
        backend->removeDock(dock);
        dock->setParent(mainWindow);
        backend->setDockFloating(dock, false);
        if (plan.kind == DockDropKind::Tabify) {
            backend->tabifyDock(targetDock, dock);
        } else {
            // splitDockWidget() can insert a detached dock by splitting an
            // existing target in place. Do not remove/re-add the target or its
            // tab group by Qt::DockWidgetArea: iaito's apparent central area is
            // itself a nested dock cell in the left area, and area-level re-adds
            // flatten that tree, moving the central cell below the side docks.
            const Qt::Orientation orientation = (plan.kind == DockDropKind::SplitLeft
                                                 || plan.kind == DockDropKind::SplitRight)
                                                    ? Qt::Horizontal
                                                    : Qt::Vertical;
            QList<QDockWidget *> targetTabGroup;
            QList<bool> targetTabWasVisible;
            const QList<QDockWidget *> targetTabSiblings = backend->tabifiedWith(targetDock);
            targetTabGroup.reserve(targetTabSiblings.size());
            targetTabWasVisible.reserve(targetTabSiblings.size());
            for (auto *tab : targetTabSiblings) {
                if (!tab || tab == dock || tab == targetDock) {
                    continue;
                }
                targetTabGroup.append(tab);
                targetTabWasVisible.append(tab->isVisible());
                backend->removeDock(tab);
                tab->setParent(mainWindow);
                backend->setDockFloating(tab, false);
            }
            backend->splitDock(targetDock, dock, orientation);
            for (int i = 0; i < targetTabGroup.size(); i++) {
                auto *tab = targetTabGroup.at(i);
                backend->tabifyDock(targetDock, tab);
                if (targetTabWasVisible.value(i)) {
                    tab->show();
                } else {
                    tab->hide();
                }
            }
            targetDock->show();
            targetDock->raise();

            // Give the two panels an even share instead of a sliver.
            backend->resizeDocksTo({targetDock, dock}, {1, 1}, orientation);
        }
        dockPlaced = !dock->isFloating() && backend->areaOf(dock) != Qt::NoDockWidgetArea;
    }

    if (!dockPlaced) {
        // Tear off (or recover a failed placement) as a floating window, and make
        // sure it is actually visible so the panel is never lost.
        backend->setDockFloating(dock, true);
        if (dock->size().isEmpty()) {
            dock->resize(480, 360);
        }
        dock->move(globalPos - dockDragOffset);
    }
    configureDockWidget(dock);
    if (targetDock && targetDock != dock) {
        configureDockWidget(targetDock);
    }
    dock->show();
    dock->raise();
    if (dock->isFloating()) {
        dock->activateWindow();
    }

    clearDockDrag();
    requestUpdateTabBars();
}

void DockManager::clearDockDrag()
{
    if (dropOverlay) {
        dropOverlay->hide();
    }
    if (dockDragHandle && QWidget::mouseGrabber() == dockDragHandle) {
        dockDragHandle->releaseMouse();
    }
    dockDragTabBar.clear();
    dockDragHandle.clear();
    dockDragWidget.clear();
    dockDragStartGlobalPos = QPoint();
    dockDragOffset = QPoint();
    dockTabDragActive = false;
}

bool DockManager::handleEvent(QObject *watched, QEvent *event)
{
    if (dockTabDragActive) {
        if (event->type() == QEvent::KeyPress
            && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            // Cancel the drag and leave the panel exactly where it was.
            clearDockDrag();
            return true;
        }
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
            const int index = tabBar->tabAt(mouseEventLocalPos(mouseEvent));
            updateDockTabCloseButtons(tabBar, index);
            if (mouseEvent->button() == Qt::LeftButton && index >= 0) {
                clearDockDrag();
                dockDragTabBar = tabBar;
                dockDragWidget = dockForDockTab(tabBar, index);
                dockDragStartGlobalPos = mouseEventGlobalPos(mouseEvent);
            }
            break;
        }
        case QEvent::MouseMove: {
            // Tabs only switch (click), reorder (native), and reveal the close
            // button on hover until the pointer leaves the tab strip. Then the
            // same custom placement path used by the title-bar grip takes over.
            if (!canHandleDockTabBar()) {
                break;
            }
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            updateDockTabCloseButtons(tabBar, tabBar->tabAt(mouseEventLocalPos(mouseEvent)));
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
                    dockDragHandle = handle;
                    dockDragStartGlobalPos = mouseEventGlobalPos(mouseEvent);
                    if (dockDragWidget) {
                        dockDragOffset = dockDragStartGlobalPos
                                         - dockDragWidget->mapToGlobal(QPoint(0, 0));
                        handle->grabMouse();
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
