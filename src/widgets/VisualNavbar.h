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

    enum class DataType : int { Unallocated, Code, Data, String, Symbol, Count };
    enum class LaneSourceKind { Analysis, Permissions };

    struct AxisToAddress
    {
        double axisStart;
        double axisEnd;
        RVA address_from;
        RVA address_to;
    };

    struct LaneConfig
    {
        QString sourceId;
    };

    struct LaneBlock
    {
        RVA addr = 0;
        RVA size = 0;
        QColor color;
        QString text;
    };

    struct RangeSegment
    {
        RVA from = 0;
        RVA to = 0;
    };

    struct LaneData
    {
        QString sourceId;
        QString rangeId;
        QString title;
        RVA from = 0;
        RVA to = 0;
        RVA blocksize = 0;
        QList<RangeSegment> ranges;
        QList<LaneBlock> blocks;
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

    QList<LaneConfig> laneConfigs;
    QString rangeId;
    QList<LaneData> lanes;
    QList<RangeSegment> displayRanges;
    RVA displayFrom = 0;
    RVA displayTo = 0;
    unsigned int statsAxisLength = 0;
    unsigned int previousAxisLength = 0;

    QList<AxisToAddress> axisToAddress;

    bool isVertical() const;
    int axisLength() const;
    int crossAxisLength() const;
    int currentThickness() const;
    DataType dataTypeForBlock(const BlockDescription &block) const;
    QColor colorForDataType(DataType dataType) const;
    QColor colorForPermissions(ut8 rwx) const;
    QColor colorForValue(int value) const;
    std::array<QColor, static_cast<int>(DataType::Count)> resolvedDataTypeColors() const;
    double eventAxisPosition(QMouseEvent *event) const;
    double eventCrossAxisPosition(QMouseEvent *event) const;
    int eventLaneIndex(QMouseEvent *event) const;
    QRectF axisRect(double axisStart, double axisEnd, int laneIndex, int laneCount) const;
    RVA localPositionToAddress(double position);
    double addressToLocalPosition(RVA address);
    QList<QString> sectionsForAddress(RVA address);
    QString toolTipForAddress(RVA address, int laneIndex);
    LaneData fetchRangeData(const QString &rangeId);
    LaneData fetchLaneData(const LaneConfig &config);
    LaneData fetchMetadataLane(const LaneConfig &config, LaneSourceKind sourceKind);
    LaneData fetchBarsLane(const LaneConfig &config, const QString &mode, bool minusEntropy);
    QList<RangeSegment> rangesFromJson(
        const QJsonObject &object, RVA fallbackFrom, RVA fallbackTo) const;
    void setLaneSource(int laneIndex, const QString &sourceId);
    void setRange(const QString &rangeId);

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;
    void showRelocateMenu(const QPoint &globalPos);

    bool suppressContextMenuEvents = false;
};

#endif // VISUALNAVBAR_H
