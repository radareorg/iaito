#include "OverviewWidget.h"
#include "common/ShortcutManager.h"
#include "DisassemblerGraphView.h"
#include "GraphWidget.h"
#include "OverviewView.h"
#include "common/IaitoSeekable.h"
#include "core/MainWindow.h"

#include <QAction>
#include <QTimer>

OverviewWidget::OverviewWidget(MainWindow *main)
    : IaitoDockWidget(main)
{
    setWindowTitle("Graph Overview");
    setObjectName("Graph Overview");
    setAllowedAreas(Qt::AllDockWidgetAreas);
    graphView = new OverviewView(this);
    setWidget(graphView);
    targetGraphWidget = nullptr;

    connect(graphView, &OverviewView::mouseMoved, this, &OverviewWidget::updateTargetView);
    connect(graphView, &OverviewView::pinchZoom, this, &OverviewWidget::zoomTargetByFactor);

    graphDataRefreshDeferrer = createRefreshDeferrer([this]() { updateGraphData(); });
    connect(Core(), &IaitoCore::seekChanged, this, [this](RVA) {
        if (!targetGraphWidget && isVisible()) {
            updateGraphData();
        }
    });

    // Zoom shortcuts
    QShortcut *shortcut_zoom_in
        = ShortcutMgr()->registerShortcut("graph.overviewZoomIn", this, Qt::WidgetWithChildrenShortcut);
    connect(shortcut_zoom_in, &QShortcut::activated, this, [this]() { zoomTarget(1); });
    QShortcut *shortcut_zoom_out
        = ShortcutMgr()->registerShortcut("graph.overviewZoomOut", this, Qt::WidgetWithChildrenShortcut);
    connect(shortcut_zoom_out, &QShortcut::activated, this, [this]() { zoomTarget(-1); });
}

void OverviewWidget::resizeEvent(QResizeEvent *event)
{
    graphView->refreshView();
    updateRangeRect();
    QDockWidget::resizeEvent(event);
    emit resized();
}

void OverviewWidget::showEvent(QShowEvent *event)
{
    IaitoDockWidget::showEvent(event);
    QTimer::singleShot(0, this, [this]() {
        syncGraphData();
        updateRangeRect();
    });
}

void OverviewWidget::closeEvent(QCloseEvent *event)
{
    IaitoDockWidget::closeEvent(event);
    resetFallbackGraphView();
}

void OverviewWidget::zoomTarget(int d)
{
    if (!targetGraphWidget) {
        return;
    }
    targetGraphWidget->getGraphView()->zoom(QPointF(0.5, 0.5), d);
}

void OverviewWidget::zoomTargetByFactor(qreal factor)
{
    if (!targetGraphWidget || factor <= 0.0) {
        return;
    }
    auto view = targetGraphWidget->getGraphView();
    view->setZoom(QPointF(0.5, 0.5), view->getViewScale() * factor);
    graphView->centreRect();
}

void OverviewWidget::setTargetGraphWidget(GraphWidget *widget)
{
    if (widget == targetGraphWidget) {
        return;
    }
    if (targetGraphWidget) {
        auto *targetGraphView = targetGraphWidget->getGraphView();
        disconnect(
            targetGraphView,
            &DisassemblerGraphView::viewRefreshed,
            this,
            &OverviewWidget::updateGraphData);
        disconnect(
            targetGraphView,
            &DisassemblerGraphView::resized,
            this,
            &OverviewWidget::updateRangeRect);
        disconnect(
            targetGraphView, &GraphView::viewOffsetChanged, this, &OverviewWidget::updateRangeRect);
        disconnect(
            targetGraphView, &GraphView::viewScaleChanged, this, &OverviewWidget::updateRangeRect);
        disconnect(targetGraphWidget, &GraphWidget::graphClosed, this, &OverviewWidget::targetClosed);
    }
    targetGraphWidget = widget;
    if (targetGraphWidget) {
        resetFallbackGraphView();
        auto *targetGraphView = targetGraphWidget->getGraphView();
        connect(
            targetGraphView,
            &DisassemblerGraphView::viewRefreshed,
            this,
            &OverviewWidget::updateGraphData);
        connect(
            targetGraphView,
            &DisassemblerGraphView::resized,
            this,
            &OverviewWidget::updateRangeRect);
        connect(
            targetGraphView, &GraphView::viewOffsetChanged, this, &OverviewWidget::updateRangeRect);
        connect(targetGraphView, &GraphView::viewScaleChanged, this, &OverviewWidget::updateRangeRect);
        connect(targetGraphWidget, &GraphWidget::graphClosed, this, &OverviewWidget::targetClosed);

        if (!targetGraphView->hasLoadedGraph()) {
            targetGraphView->onSeekChanged(targetGraphWidget->getSeekable()->getOffset());
        }
    }
    updateGraphData();
    updateRangeRect();
}

void OverviewWidget::wheelEvent(QWheelEvent *event)
{
    zoomTarget(event->angleDelta().y() / 90);
    graphView->centreRect();
}

void OverviewWidget::targetClosed()
{
    setTargetGraphWidget(nullptr);
}

void OverviewWidget::updateTargetView()
{
    if (!targetGraphWidget) {
        return;
    }
    auto *targetGraphView = targetGraphWidget->getGraphView();
    qreal curScale = graphView->getViewScale();
    int rectx = graphView->getRangeRect().x();
    int recty = graphView->getRangeRect().y();
    int overview_offset_x = graphView->getViewOffset().x();
    int overview_offset_y = graphView->getViewOffset().y();
    QPoint newOffset;
    newOffset.rx() = rectx / curScale + overview_offset_x;
    newOffset.ry() = recty / curScale + overview_offset_y;
    targetGraphView->setViewOffset(newOffset);
    targetGraphView->viewport()->update();
}

void OverviewWidget::updateGraphData()
{
    if (!graphDataRefreshDeferrer->attemptRefresh(nullptr)) {
        return;
    }
    syncGraphData();
}

void OverviewWidget::syncGraphData()
{
    DisassemblerGraphView *dataSource = currentGraphView();
    if (dataSource && !dataSource->isGraphEmpty()) {
        graphView->currentFcnAddr = dataSource->currentFcnAddr;
        auto &mainGraphView = *dataSource;
        graphView->setData(
            mainGraphView.getWidth(),
            mainGraphView.getHeight(),
            mainGraphView.getBlocks(),
            mainGraphView.getEdgeConfigurations(),
            mainGraphView.getMinimapBars());
    } else {
        graphView->currentFcnAddr = RVA_INVALID;
        graphView->setData(0, 0, {}, {}, {});
        graphView->setRangeRect(QRectF(0, 0, 0, 0));
    }
}

DisassemblerGraphView *OverviewWidget::currentGraphView()
{
    if (targetGraphWidget) {
        return targetGraphWidget->getGraphView();
    }
    if (!isVisible()) {
        return nullptr;
    }
    if (!fallbackGraphView) {
        fallbackSeekable = new IaitoSeekable(this);
        fallbackSeekable->setSynchronization(false);
        fallbackGraphView
            = new DisassemblerGraphView(this, fallbackSeekable, mainWindow, QList<QAction *>());
        fallbackSeekable->setParent(fallbackGraphView);
        fallbackGraphView->hide();
        connect(
            fallbackGraphView,
            &DisassemblerGraphView::viewRefreshed,
            this,
            [this]() {
                syncGraphData();
                updateRangeRect();
            },
            Qt::QueuedConnection);
    }

    const RVA offset = Core()->getOffset();
    if (!fallbackGraphView->hasLoadedGraph() || fallbackSeekable->getOffset() != offset) {
        fallbackSeekable->seek(offset);
    }
    if (!fallbackGraphView->hasLoadedGraph()) {
        fallbackGraphView->loadCurrentGraph();
    }

    return fallbackGraphView;
}

void OverviewWidget::resetFallbackGraphView()
{
    delete fallbackGraphView;
    fallbackGraphView = nullptr;
    fallbackSeekable = nullptr;
}

void OverviewWidget::updateRangeRect()
{
    if (targetGraphWidget) {
        auto *targetGraphView = targetGraphWidget->getGraphView();
        qreal curScale = graphView->getViewScale();
        qreal baseScale = targetGraphView->getViewScale();
        qreal w = targetGraphView->viewport()->width() * curScale / baseScale;
        qreal h = targetGraphView->viewport()->height() * curScale / baseScale;
        int graph_offset_x = targetGraphView->getViewOffset().x();
        int graph_offset_y = targetGraphView->getViewOffset().y();
        int overview_offset_x = graphView->getViewOffset().x();
        int overview_offset_y = graphView->getViewOffset().y();
        int rangeRectX = graph_offset_x * curScale - overview_offset_x * curScale;
        int rangeRectY = graph_offset_y * curScale - overview_offset_y * curScale;
        graphView->setRangeRect(QRectF(rangeRectX, rangeRectY, w, h));
    } else {
        graphView->setRangeRect(QRectF(0, 0, 0, 0));
    }
}
