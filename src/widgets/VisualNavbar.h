#ifndef VISUALNAVBAR_H
#define VISUALNAVBAR_H

#include <QGraphicsScene>
#include <QToolBar>

#include "core/Iaito.h"

class MainWindow;
class QGraphicsView;

class VisualNavbar : public QToolBar
{
    Q_OBJECT

    struct AxisToAddress
    {
        double axisStart;
        double axisEnd;
        RVA address_from;
        RVA address_to;
    };

public:
    explicit VisualNavbar(MainWindow *main, QWidget *parent = nullptr);

public slots:
    void paintEvent(QPaintEvent *event) override;
    void updateGraphicsScene();

private slots:
    void fetchAndPaintData();
    void fetchStats();
    void drawSeekCursor();
    void drawPCCursor();
    void drawCursor(RVA addr, QColor color, QGraphicsRectItem *&graphicsItem);
    void on_seekChanged(RVA addr);
    void updateLayoutForOrientation(Qt::Orientation orientation);
    void updateThicknessFromConfig(int thickness);

private:
    QGraphicsView *graphicsView;
    QGraphicsScene *graphicsScene;
    QGraphicsRectItem *seekGraphicsItem;
    QGraphicsRectItem *PCGraphicsItem;
    MainWindow *main;

    BlockStatistics stats;
    unsigned int statsAxisLength = 0;
    unsigned int previousAxisLength = 0;

    QList<AxisToAddress> axisToAddress;

    bool isVertical() const;
    int axisLength() const;
    int crossAxisLength() const;
    int currentThickness() const;
    double eventAxisPosition(QMouseEvent *event) const;
    QRectF axisRect(double axisStart, double axisEnd) const;
    RVA localPositionToAddress(double position);
    double addressToLocalPosition(RVA address);
    QList<QString> sectionsForAddress(RVA address);
    QString toolTipForAddress(RVA address);

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
};

#endif // VISUALNAVBAR_H
