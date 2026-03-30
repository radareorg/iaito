#include "VisualNavbar.h"
#include "common/TempConfig.h"
#include "core/MainWindow.h"

#include <QComboBox>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMouseEvent>
#include <QSet>
#include <QToolTip>

#include <array>
#include <cmath>

VisualNavbar::VisualNavbar(MainWindow *main, QWidget *parent)
    : QToolBar(main)
    , graphicsView(new QGraphicsView)
    , seekGraphicsItem(nullptr)
    , PCGraphicsItem(nullptr)
    , main(main)
{
    Q_UNUSED(parent);

    setObjectName("visualNavbar");
    setWindowTitle(tr("Visual navigation bar"));
    //    setMovable(false);
    setContentsMargins(0, 0, 0, 0);
    // If line below is used, with the dark theme the paintEvent is not called
    // and the result is wrong. Something to do with overwriting the style sheet
    // :/
    // setStyleSheet("QToolBar { border: 0px; border-bottom: 0px; border-top:
    // 0px; border-width: 0px;}");

    /*
    QComboBox *addsCombo = new QComboBox();
    addsCombo->addItem("");
    addsCombo->addItem("Entry points");
    addsCombo->addItem("Marks");
    */
    addWidget(this->graphicsView);
    // addWidget(addsCombo);

    connect(Core(), &IaitoCore::seekChanged, this, &VisualNavbar::on_seekChanged);
    connect(Core(), &IaitoCore::registersChanged, this, &VisualNavbar::drawPCCursor);
    connect(Core(), &IaitoCore::refreshAll, this, &VisualNavbar::fetchAndPaintData);
    connect(Core(), &IaitoCore::functionsChanged, this, &VisualNavbar::fetchAndPaintData);
    connect(Core(), &IaitoCore::flagsChanged, this, &VisualNavbar::fetchAndPaintData);
    connect(Config(), &Configuration::colorsUpdated, this, &VisualNavbar::updateGraphicsScene);

    graphicsScene = new QGraphicsScene(this);
    graphicsScene->setBackgroundBrush(QBrush(colorForDataType(DataType::Empty)));

    this->graphicsView->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    this->graphicsView->setFrameShape(QFrame::NoFrame);
    this->graphicsView->setRenderHints({});
    this->graphicsView->setScene(graphicsScene);
    this->graphicsView->setRenderHints(QPainter::Antialiasing);
    this->graphicsView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->graphicsView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // So the graphicsView doesn't intercept mouse events.
    this->graphicsView->setEnabled(false);
    this->graphicsView->setMouseTracking(true);
    setMouseTracking(true);

    connect(this, &QToolBar::orientationChanged, this, &VisualNavbar::updateLayoutForOrientation);
    connect(
        Config(),
        &Configuration::visualNavbarThicknessChanged,
        this,
        &VisualNavbar::updateThicknessFromConfig);
    updateLayoutForOrientation(orientation());
}

unsigned int nextPow2(unsigned int n)
{
    unsigned int b = 0;
    while (n) {
        n >>= 1;
        b++;
    }
    return (1u << b);
}

void VisualNavbar::paintEvent(QPaintEvent *event)
{
    QToolBar::paintEvent(event);

    auto w = static_cast<unsigned int>(axisLength());
    if (w == 0) {
        return;
    }
    bool fetch = false;
    if (statsAxisLength < w) {
        statsAxisLength = nextPow2(w);
        fetch = true;
    } else if (statsAxisLength > w * 4) {
        statsAxisLength = statsAxisLength > 0 ? statsAxisLength / 2 : 0;
        fetch = true;
    }

    if (fetch) {
        fetchAndPaintData();
    } else if (previousAxisLength != w) {
        this->previousAxisLength = w;
        updateGraphicsScene();
    }
}

void VisualNavbar::fetchAndPaintData()
{
    fetchStats();
    updateGraphicsScene();
}

void VisualNavbar::fetchStats()
{
    stats = Core()->getBlockStatistics(statsAxisLength);
}

void VisualNavbar::updateGraphicsScene()
{
    graphicsScene->clear();
    axisToAddress.clear();
    seekGraphicsItem = nullptr;
    PCGraphicsItem = nullptr;
    auto colors = resolvedDataTypeColors();
    graphicsScene->setBackgroundBrush(QBrush(colors[static_cast<int>(DataType::Empty)]));

    if (stats.to <= stats.from) {
        return;
    }

    int majorAxisLength = axisLength();
    int crossLength = crossAxisLength();
    if (majorAxisLength <= 0 || crossLength <= 0) {
        return;
    }

    RVA totalSize = stats.to - stats.from;
    RVA beginAddr = stats.from;

    double pixelsPerByte = (double) majorAxisLength / (double) totalSize;
    auto axisFromAddr = [pixelsPerByte, beginAddr](RVA addr) -> double {
        return (addr - beginAddr) * pixelsPerByte;
    };

    std::array<QBrush, static_cast<int>(DataType::Count)> dataTypeBrushes;
    dataTypeBrushes[static_cast<int>(DataType::Code)] = QBrush(
        colors[static_cast<int>(DataType::Code)]);
    dataTypeBrushes[static_cast<int>(DataType::String)] = QBrush(
        colors[static_cast<int>(DataType::String)]);
    dataTypeBrushes[static_cast<int>(DataType::Symbol)] = QBrush(
        colors[static_cast<int>(DataType::Symbol)]);

    DataType lastDataType = DataType::Empty;
    QGraphicsRectItem *dataItem = nullptr;
    QRectF dataItemRect;
    for (const BlockDescription &block : stats.blocks) {
        // Keep track of where which memory segment is mapped so we are able to
        // convert from address to toolbar position and vice versa.
        AxisToAddress x2a;
        x2a.axisStart = axisFromAddr(block.addr);
        x2a.axisEnd = axisFromAddr(block.addr + block.size);
        x2a.address_from = block.addr;
        x2a.address_to = block.addr + block.size;
        axisToAddress.append(x2a);

        DataType dataType = dataTypeForBlock(block);
        if (dataType == DataType::Empty) {
            lastDataType = DataType::Empty;
            continue;
        }

        if (dataType == lastDataType) {
            double axisEnd = axisFromAddr(block.addr + block.size);
            if (isVertical()) {
                if (axisEnd > dataItemRect.bottom()) {
                    dataItemRect.setBottom(axisEnd);
                }
            } else if (axisEnd > dataItemRect.right()) {
                dataItemRect.setRight(axisEnd);
            }
            dataItem->setRect(dataItemRect);
            continue;
        }

        double axisStart = axisFromAddr(block.addr);
        double axisEnd = axisFromAddr(block.addr + block.size);
        dataItemRect = axisRect(axisStart, axisEnd);

        dataItem = new QGraphicsRectItem(dataItemRect);
        dataItem->setPen(Qt::NoPen);
        dataItem->setBrush(dataTypeBrushes[static_cast<int>(dataType)]);
        graphicsScene->addItem(dataItem);

        lastDataType = dataType;
    }

    graphicsScene->setSceneRect(0, 0, graphicsView->width(), graphicsView->height());

    drawSeekCursor();
    drawPCCursor();
}

VisualNavbar::DataType VisualNavbar::dataTypeForBlock(const BlockDescription &block) const
{
    if (block.functions > 0) {
        return DataType::Code;
    }
    if (block.strings > 0) {
        return DataType::String;
    }
    if (block.symbols > 0) {
        return DataType::Symbol;
    }
    if (block.inFunctions > 0) {
        return DataType::Code;
    }
    return DataType::Empty;
}

QColor VisualNavbar::colorForDataType(DataType dataType) const
{
    if (Config()->getVisualNavbarUseThemeColors()) {
        switch (dataType) {
        case DataType::Code:
            return Config()->getColor("ai.exec");
        case DataType::String:
            return Config()->getColor("ai.ascii");
        case DataType::Symbol:
            return Config()->getColor("flag");
        case DataType::Empty:
        case DataType::Count:
            return Config()->getColor("diff.match");
        }
    }

    switch (dataType) {
    case DataType::Code:
        return Config()->getColor("gui.navbar.code");
    case DataType::String:
        return Config()->getColor("gui.navbar.str");
    case DataType::Symbol:
        return Config()->getColor("gui.navbar.sym");
    case DataType::Empty:
    case DataType::Count:
        return Config()->getColor("gui.navbar.empty");
    }

    return QColor();
}

std::array<QColor, static_cast<int>(VisualNavbar::DataType::Count)>
VisualNavbar::resolvedDataTypeColors() const
{
    std::array<QColor, static_cast<int>(DataType::Count)> colors;
    for (int i = 0; i < static_cast<int>(DataType::Count); i++) {
        colors[i] = colorForDataType(static_cast<DataType>(i));
    }

    if (!Config()->getVisualNavbarUseThemeColors()) {
        return colors;
    }

    QSet<QRgb> usedColors;
    auto pickUniqueColor = [&colors, &usedColors](DataType dataType, const QStringList &keys) {
        const int index = static_cast<int>(dataType);

        for (const QString &key : keys) {
            QColor candidate = Config()->getColor(key);
            if (!candidate.isValid() || usedColors.contains(candidate.rgba())) {
                continue;
            }

            colors[index] = candidate;
            usedColors.insert(candidate.rgba());
            return;
        }

        for (const QString &key : keys) {
            QColor candidate = Config()->getColor(key);
            if (!candidate.isValid()) {
                continue;
            }

            colors[index] = candidate;
            usedColors.insert(candidate.rgba());
            return;
        }
    };

    // Prefer radare2 theme colors, but avoid collapsing multiple regions to
    // the same color when the active theme reuses a small palette.
    pickUniqueColor(
        DataType::Code, {"ai.exec", "graph.true", "jmp", "call", "flow", "gui.navbar.code"});
    pickUniqueColor(DataType::String, {"ai.ascii", "comment", "usrcmt", "var.type", "gui.navbar.str"});
    pickUniqueColor(DataType::Symbol, {"flag", "label", "fname", "other", "gui.navbar.sym"});
    pickUniqueColor(DataType::Empty, {"diff.match", "graph.box4", "widget.bg", "gui.navbar.empty"});

    return colors;
}

void VisualNavbar::drawCursor(RVA addr, QColor color, QGraphicsRectItem *&graphicsItem)
{
    double cursorPos = addressToLocalPosition(addr);
    if (graphicsItem != nullptr) {
        graphicsScene->removeItem(graphicsItem);
        delete graphicsItem;
        graphicsItem = nullptr;
    }
    if (std::isnan(cursorPos)) {
        return;
    }
    int crossLength = crossAxisLength();
    graphicsItem = isVertical() ? new QGraphicsRectItem(0, cursorPos, crossLength, 2)
                                : new QGraphicsRectItem(cursorPos, 0, 2, crossLength);
    graphicsItem->setPen(Qt::NoPen);
    graphicsItem->setBrush(QBrush(color));
    graphicsScene->addItem(graphicsItem);
}

void VisualNavbar::drawPCCursor()
{
    drawCursor(Core()->getProgramCounterValue(), Config()->getColor("gui.navbar.pc"), PCGraphicsItem);
}

void VisualNavbar::drawSeekCursor()
{
    drawCursor(Core()->getOffset(), Config()->getColor("gui.navbar.seek"), seekGraphicsItem);
}

void VisualNavbar::on_seekChanged(RVA addr)
{
    Q_UNUSED(addr);
    // Update cursor
    this->drawSeekCursor();
}

void VisualNavbar::mousePressEvent(QMouseEvent *event)
{
    qreal pos = eventAxisPosition(event);
    if (std::isnan(pos)) {
        return;
    }

    RVA address = localPositionToAddress(pos);
    if (address != RVA_INVALID) {
        address = Core()->alignInstructionAddress(address);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        QToolTip::showText(event->globalPosition().toPoint(), toolTipForAddress(address), this);
#else
        QToolTip::showText(event->globalPos(), toolTipForAddress(address), this);
#endif
        if (event->buttons() & Qt::LeftButton) {
            event->accept();
            Core()->seek(address);
        }
    }
}

void VisualNavbar::mouseMoveEvent(QMouseEvent *event)
{
    event->accept();
    mousePressEvent(event);
}

bool VisualNavbar::isVertical() const
{
    return orientation() == Qt::Vertical;
}

int VisualNavbar::axisLength() const
{
    return isVertical() ? graphicsView->height() : graphicsView->width();
}

int VisualNavbar::crossAxisLength() const
{
    return isVertical() ? graphicsView->width() : graphicsView->height();
}

int VisualNavbar::currentThickness() const
{
    return Config()->getVisualNavbarThickness();
}

double VisualNavbar::eventAxisPosition(QMouseEvent *event) const
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QPointF localPos = event->position();
#else
    QPointF localPos = event->localPos();
#endif
    QRect viewGeometry = graphicsView->geometry();
    if (!viewGeometry.contains(localPos.toPoint())) {
        return nan("");
    }

    return isVertical() ? localPos.y() - viewGeometry.top() : localPos.x() - viewGeometry.left();
}

QRectF VisualNavbar::axisRect(double axisStart, double axisEnd) const
{
    double normalizedAxisEnd = qMax(axisStart + 1.0, axisEnd);
    int crossLength = crossAxisLength();
    if (isVertical()) {
        return QRectF(0.0, axisStart, crossLength, normalizedAxisEnd - axisStart);
    }
    return QRectF(axisStart, 0.0, normalizedAxisEnd - axisStart, crossLength);
}

RVA VisualNavbar::localPositionToAddress(double position)
{
    for (const AxisToAddress &x2a : axisToAddress) {
        if ((x2a.axisStart <= position) && (position <= x2a.axisEnd)) {
            double axisSize = x2a.axisEnd - x2a.axisStart;
            if (axisSize <= 0.0) {
                return x2a.address_from;
            }

            double offset = (position - x2a.axisStart) / axisSize;
            double size = x2a.address_to - x2a.address_from;
            return x2a.address_from + (offset * size);
        }
    }
    return RVA_INVALID;
}

double VisualNavbar::addressToLocalPosition(RVA address)
{
    for (const AxisToAddress &x2a : axisToAddress) {
        if ((x2a.address_from <= address) && (address < x2a.address_to)) {
            double addressSize = x2a.address_to - x2a.address_from;
            if (addressSize <= 0.0) {
                return x2a.axisStart;
            }

            double offset = (double) (address - x2a.address_from) / addressSize;
            double axisSize = x2a.axisEnd - x2a.axisStart;
            return x2a.axisStart + (offset * axisSize);
        }
    }
    return nan("");
}

void VisualNavbar::updateLayoutForOrientation(Qt::Orientation orientation)
{
    int thickness = currentThickness();
    bool vertical = orientation == Qt::Vertical;

    graphicsView->setSizePolicy(
        vertical ? QSizePolicy::Fixed : QSizePolicy::Expanding,
        vertical ? QSizePolicy::Expanding : QSizePolicy::Fixed);
    graphicsView->setMinimumSize(vertical ? QSize(thickness, thickness) : QSize(0, thickness));
    graphicsView->setMaximumSize(
        vertical ? QSize(thickness, QWIDGETSIZE_MAX) : QSize(QWIDGETSIZE_MAX, thickness));

    previousAxisLength = 0;
    auto currentAxisLength = static_cast<unsigned int>(qMax(axisLength(), 0));
    if (currentAxisLength > 0) {
        statsAxisLength = qMax(statsAxisLength, nextPow2(currentAxisLength));
    }
    updateGeometry();
    if (currentAxisLength > 0) {
        fetchAndPaintData();
    }
}

void VisualNavbar::updateThicknessFromConfig(int thickness)
{
    Q_UNUSED(thickness);
    updateLayoutForOrientation(orientation());
}

QList<QString> VisualNavbar::sectionsForAddress(RVA address)
{
    QList<QString> ret;
    QList<SectionDescription> sections = Core()->getAllSections();
    for (const SectionDescription &section : sections) {
        if (address >= section.vaddr && address < section.vaddr + section.vsize) {
            ret << section.name;
        }
    }
    return ret;
}

QString VisualNavbar::toolTipForAddress(RVA address)
{
    QString ret = "Address: " + RAddressString(address);

    // Don't append sections when a debug task is in progress to avoid freezing
    // the interface
    if (Core()->isDebugTaskInProgress()) {
        return ret;
    }

    auto sections = sectionsForAddress(address);
    if (sections.count()) {
        ret += "\nSections: \n";
        bool first = true;
        for (const QString &section : sections) {
            if (!first) {
                ret.append(QLatin1Char('\n'));
            } else {
                first = false;
            }
            ret += "  " + section;
        }
    }
    return ret;
}
