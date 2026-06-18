#ifndef OVERVIEWWIDGET_H
#define OVERVIEWWIDGET_H

#include "IaitoDockWidget.h"

class MainWindow;
class OverviewView;
class GraphWidget;
class DisassemblerGraphView;
class IaitoSeekable;

class OverviewWidget : public IaitoDockWidget
{
    Q_OBJECT

public:
    explicit OverviewWidget(MainWindow *main);

private:
    OverviewView *graphView;

    GraphWidget *targetGraphWidget;
    DisassemblerGraphView *fallbackGraphView = nullptr;
    IaitoSeekable *fallbackSeekable = nullptr;

    RefreshDeferrer *graphDataRefreshDeferrer;

    /**
     * @brief this takes care of scaling the overview when the widget is resized
     */
    void resizeEvent(QResizeEvent *event) override;

    void zoomTarget(int d);
    void zoomTargetByFactor(qreal factor);
    DisassemblerGraphView *currentGraphView();
    void resetFallbackGraphView();

private slots:
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

    /**
     * @brief update the view in the target widget when the range rect in the
     * overview is moved
     */
    void updateTargetView();

    /**
     * @brief update the content of the graph (blocks, edges) in the contained
     * graphView from the target widget
     */
    void updateGraphData();
    void syncGraphData();

    /**
     * @brief update the rect to show the current view in the target widget
     */
    void updateRangeRect();

    void targetClosed();

signals:
    /**
     * @brief emit signal to update the rect
     */
    void resized();

public:
    GraphWidget *getTargetGraphWidget() { return targetGraphWidget; }
    void setTargetGraphWidget(GraphWidget *widget);

    OverviewView *getGraphView() const { return graphView; }
    void wheelEvent(QWheelEvent *event) override;
};

#endif // OverviewWIDGET_H
