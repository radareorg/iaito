#ifndef VISUALNAVBAR_H
#define VISUALNAVBAR_H

#include <array>
#include <QGraphicsScene>
#include <QToolBar>

#include "core/Iaito.h"

class MainWindow;
class QGraphicsView;

class VisualNavbar : public QToolBar
{
    Q_OBJECT

    enum class DataType : int { Empty, Code, String, Symbol, Count };

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
    DataType dataTypeForBlock(const BlockDescription &block) const;
    QColor colorForDataType(DataType dataType) const;
    std::array<QColor, static_cast<int>(DataType::Count)> resolvedDataTypeColors() const;
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
