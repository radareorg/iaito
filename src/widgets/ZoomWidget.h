#ifndef ZOOMWIDGET_H
#define ZOOMWIDGET_H

#include <QCheckBox>
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

    void setCursorIndex(int idx);
    int cursorIndex() const { return m_cursorIndex; }
    int columns() const { return m_columns; }
    int blockCount() const { return m_blocks.size(); }

signals:
    void blockClicked(RVA addr);
    void columnsChangeRequested(int delta);
    void blocksChangeRequested(int delta);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

public:
    static constexpr int CellSize = 10;
    static constexpr int CellGap = 1;
    int cellPitch() const { return CellSize + CellGap; }

private:
    QVector<BlockEntry> m_blocks;
    int m_columns = 64;
    int m_cursorIndex = -1;
    ColorMode m_colorMode = ColorMode::Greyscale;
    RVA m_seekAddr = RVA_INVALID;

    QVector<QColor> m_themePalette;

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

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void fetchData();
    void onSeekChanged(RVA addr);
    void updateAutoWrapColumns();

private:
    QComboBox *modeCombo;
    QComboBox *rangeCombo;
    QSpinBox *columnsSpinBox;
    QSpinBox *blocksSpinBox;
    QComboBox *colorCombo;
    QCheckBox *autoWrapCheck;
    ZoomView *zoomView;
    QScrollArea *scrollArea = nullptr;
};

#endif // ZOOMWIDGET_H
