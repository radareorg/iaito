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

    graphicsScene = new QGraphicsScene(this);

    const QBrush bg = QBrush(QColor(74, 74, 74));

    graphicsScene->setBackgroundBrush(bg);

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

enum class DataType : int { Empty, Code, String, Symbol, Count };

void VisualNavbar::updateGraphicsScene()
{
    graphicsScene->clear();
    axisToAddress.clear();
    seekGraphicsItem = nullptr;
    PCGraphicsItem = nullptr;
    graphicsScene->setBackgroundBrush(QBrush(Config()->getColor("gui.navbar.empty")));

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
        Config()->getColor("gui.navbar.code"));
    dataTypeBrushes[static_cast<int>(DataType::String)] = QBrush(
        Config()->getColor("gui.navbar.str"));
    dataTypeBrushes[static_cast<int>(DataType::Symbol)] = QBrush(
        Config()->getColor("gui.navbar.sym"));

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

        DataType dataType;
        if (block.functions > 0) {
            dataType = DataType::Code;
        } else if (block.strings > 0) {
            dataType = DataType::String;
        } else if (block.symbols > 0) {
            dataType = DataType::Symbol;
        } else if (block.inFunctions > 0) {
            dataType = DataType::Code;
        } else {
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
    constexpr int Thickness = 15;
    bool vertical = orientation == Qt::Vertical;

    graphicsView->setSizePolicy(
        vertical ? QSizePolicy::Fixed : QSizePolicy::Expanding,
        vertical ? QSizePolicy::Expanding : QSizePolicy::Fixed);
    graphicsView->setMinimumSize(vertical ? QSize(Thickness, Thickness) : QSize(0, Thickness));
    graphicsView->setMaximumSize(
        vertical ? QSize(Thickness, QWIDGETSIZE_MAX) : QSize(QWIDGETSIZE_MAX, Thickness));

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
