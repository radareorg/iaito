#ifndef DATAANALYSISVIEW_H
#define DATAANALYSISVIEW_H

#include "common/DataAnalysis.h"

#include <QColor>
#include <QTimer>
#include <QWidget>

class DataAnalysisView : public QWidget
{
    Q_OBJECT

public:
    enum MarkerType { Section, Segment, Map, Flag, Comment, MarkerCount };
    enum Style { Lines, Areas, Bars };
    struct Marker
    {
        DataAnalysis::Range range;
        QString name;
        MarkerType type;
    };

    explicit DataAnalysisView(bool overview, QWidget *parent = nullptr);
    static QString metricName(int metric);
    static QString metricDescription(int metric);
    static QString metricValue(int metric, double value);
    static QString markerName(MarkerType type);

    DataAnalysis::Range domain;
    DataAnalysis::Range visibleRange;
    DataAnalysis::Range selection;
    QVector<DataAnalysis::Sample> samples;
    QVector<Marker> markers;
    QVector<int> metrics = {DataAnalysis::Entropy, DataAnalysis::Printable, DataAnalysis::Zero};
    std::array<QColor, DataAnalysis::Count> colors;
    std::array<bool, MarkerCount> markerEnabled = {true, false, false, true, true};
    RVA seekAddress = RVA_INVALID;
    Style style = Areas;
    bool separateTracks = false;
    bool loading = false;

    QRectF plotRect() const;
    RVA addressAt(qreal x) const;
    qreal xForAddress(RVA address) const;
    void zoom(double factor, RVA anchor);
    void pan(int direction);

signals:
    void seekRequested(RVA address);
    void selectionRequested(RVA start, RVA end); // Half-open.
    void rangeRequested(RVA start, RVA end);     // Half-open.
    void contextRequested(RVA address, QPoint globalPosition);

protected:
    bool event(QEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void leaveEvent(QEvent *) override;
    void hideEvent(QHideEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;

private:
    bool overview;
    enum Drag { None, Select, Move, LeftEdge, RightEdge };
    Drag drag = None;
    QPoint pressPosition;
    RVA pressAddress = 0;
    DataAnalysis::Range pressRange;
    DataAnalysis::Range dragSelection;
    int hoverX = -1;
    QTimer tooltipTimer;
    QPoint hoverPosition;

    DataAnalysis::Range axisRange() const;
    RVA nearestMarker(qreal x) const;
    void pinchZoom(qreal scaleFactor, qreal x);
    void updateDrag(QPoint position);
    void showTooltip();
    void cancelTooltip();
};

#endif
