#ifndef ZOOMWIDGET_H
#define ZOOMWIDGET_H

#include <QComboBox>
#include <QScrollArea>
#include <QSpinBox>
#include <QWidget>

#include "IaitoDockWidget.h"

class MainWindow;

class ZoomView : public QWidget
{
    Q_OBJECT

public:
    enum class ColorMode { Greyscale, Rainbow, Theme, ThemePalette };

    struct BlockEntry
    {
        RVA addr;
        int value;
    };

    explicit ZoomView(QWidget *parent = nullptr);

    void setData(const QVector<BlockEntry> &blocks);
    void setColumns(int cols);
    void setColorMode(ColorMode mode);
    void setSeekAddress(RVA addr);
    bool colorModeIs(ColorMode mode) const { return m_colorMode == mode; }
    void rebuildThemePalette();

signals:
    void blockClicked(RVA addr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    static constexpr int CellSize = 10;
    static constexpr int CellGap = 1;

    QVector<BlockEntry> m_blocks;
    int m_columns = 64;
    ColorMode m_colorMode = ColorMode::Greyscale;
    RVA m_seekAddr = RVA_INVALID;

    QVector<QColor> m_themePalette;

    int cellPitch() const { return CellSize + CellGap; }
    int blockIndexAt(const QPoint &pos) const;
    QColor colorForValue(int value) const;
    void updateMinimumSize();
};

class ZoomWidget : public IaitoDockWidget
{
    Q_OBJECT

public:
    explicit ZoomWidget(MainWindow *main);
    ~ZoomWidget();

private slots:
    void fetchData();
    void onSeekChanged(RVA addr);

private:
    QComboBox *modeCombo;
    QComboBox *rangeCombo;
    QSpinBox *columnsSpinBox;
    QSpinBox *blocksSpinBox;
    QComboBox *colorCombo;
    ZoomView *zoomView;
    QScrollArea *scrollArea;
};

#endif // ZOOMWIDGET_H
