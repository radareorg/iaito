#include "VisualNavbar.h"
#include "common/TempConfig.h"
#include "core/MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLinearGradient>
#include <QMenu>
#include <QMouseEvent>
#include <QSet>
#include <QTimer>
#include <QToolTip>

#include <algorithm>
#include <array>
#include <cmath>

namespace {
struct NavbarChoice
{
    const char *id;
    const char *label;
    const char *mode;
};

const NavbarChoice SourceChoices[] = {
    {"none", QT_TR_NOOP("Disabled"), ""},
    {"analysis", QT_TR_NOOP("Analysis metadata"), ""},
    {"permissions", QT_TR_NOOP("Permissions"), ""},
    {"minus-entropy", QT_TR_NOOP("Entropy (p-)"), ""},
    {"entropy", QT_TR_NOOP("Entropy"), "e"},
    {"strings", QT_TR_NOOP("Strings"), "z"},
    {"printable", QT_TR_NOOP("Printable"), "p"},
    {"unique", QT_TR_NOOP("Unique bytes"), "d"},
    {"zero", QT_TR_NOOP("0x00 bytes"), "0"},
    {"ff", QT_TR_NOOP("0xFF bytes"), "F"},
    {"marks", QT_TR_NOOP("Flags/Marks"), "m"},
    {"jumps", QT_TR_NOOP("Jumps"), "j"},
    {"analysis-bars", QT_TR_NOOP("Analysis density"), "A"},
    {"basicblocks", QT_TR_NOOP("Basic blocks"), "a"},
    {"calls", QT_TR_NOOP("Calls"), "c"},
    {"invalid", QT_TR_NOOP("Invalid instructions"), "i"},
    {"syscalls", QT_TR_NOOP("Syscalls"), "s"},
};

const NavbarChoice RangeChoices[] = {
    {"bin.sections", QT_TR_NOOP("bin.sections"), ""},
    {"bin.sections.rwx", QT_TR_NOOP("bin.sections.rwx"), ""},
    {"bin.sections.r", QT_TR_NOOP("bin.sections.r"), ""},
    {"bin.sections.rw", QT_TR_NOOP("bin.sections.rw"), ""},
    {"bin.sections.rx", QT_TR_NOOP("bin.sections.rx"), ""},
    {"io.maps", QT_TR_NOOP("io.maps"), ""},
    {"io.maps.rwx", QT_TR_NOOP("io.maps.rwx"), ""},
    {"io.maps.r", QT_TR_NOOP("io.maps.r"), ""},
    {"io.maps.rw", QT_TR_NOOP("io.maps.rw"), ""},
    {"io.maps.rx", QT_TR_NOOP("io.maps.rx"), ""},
    {"dbg.maps", QT_TR_NOOP("dbg.maps"), ""},
    {"dbg.maps.rwx", QT_TR_NOOP("dbg.maps.rwx"), ""},
    {"dbg.maps.r", QT_TR_NOOP("dbg.maps.r"), ""},
    {"dbg.maps.rw", QT_TR_NOOP("dbg.maps.rw"), ""},
    {"dbg.maps.rx", QT_TR_NOOP("dbg.maps.rx"), ""},
    {"anal.fcn", QT_TR_NOOP("anal.fcn"), ""},
    {"anal.bb", QT_TR_NOOP("anal.bb"), ""},
    {"raw", QT_TR_NOOP("raw"), ""},
    {"block", QT_TR_NOOP("block"), ""},
};

QString sourceLabel(const QString &id)
{
    for (const NavbarChoice &choice : SourceChoices) {
        if (id == QLatin1String(choice.id)) {
            return QString::fromLatin1(choice.label);
        }
    }
    return id;
}

QString barsModeForSource(const QString &id)
{
    for (const NavbarChoice &choice : SourceChoices) {
        if (id == QLatin1String(choice.id)) {
            return QString::fromLatin1(choice.mode);
        }
    }
    return QString();
}

RVA blockAddress(const QJsonObject &object)
{
    if (object.contains(QStringLiteral("addr"))) {
        return object[QStringLiteral("addr")].toVariant().toULongLong();
    }
    return object[QStringLiteral("offset")].toVariant().toULongLong();
}

ut8 rwxFromString(const QString &perm)
{
    ut8 rwx = 0;
    if (perm.length() >= 1 && perm[0] == QLatin1Char('r')) {
        rwx |= 1 << 0;
    }
    if (perm.length() >= 2 && perm[1] == QLatin1Char('w')) {
        rwx |= 1 << 1;
    }
    if (perm.length() >= 3 && perm[2] == QLatin1Char('x')) {
        rwx |= 1 << 2;
    }
    return rwx;
}

bool isValidRange(RVA from, RVA to)
{
    return to > from;
}

bool canUseZoomRangeFallback(const QString &id)
{
    return id == QLatin1String("raw");
}
} // namespace

VisualNavbar::VisualNavbar(MainWindow *main, QWidget *parent)
    : QToolBar(main)
    , graphicsView(new QGraphicsView)
    , seekGraphicsItem(nullptr)
    , PCGraphicsItem(nullptr)
    , main(main)
{
    Q_UNUSED(parent);

    laneConfigs = {
        {QStringLiteral("marks")},
        {QStringLiteral("entropy")},
    };
    rangeId = QStringLiteral("bin.sections");

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
    graphicsScene->setBackgroundBrush(QBrush(colorForDataType(DataType::Unallocated)));

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

// Parse a radare2-style color string (e.g. "rgb:ff0000", "rgb:f00", "#ff0000",
// or a named HTML color) into a QColor. Returns an invalid QColor on failure.
static QColor parseR2Color(const QString &s)
{
    if (s.isEmpty()) {
        return QColor();
    }
    if (s.startsWith(QLatin1String("rgb:"))) {
        QString rgb = s.mid(4);
        if (rgb.length() == 3) {
            return QColor(QStringLiteral("#%1%1%2%2%3%3").arg(rgb[0]).arg(rgb[1]).arg(rgb[2]));
        }
        if (rgb.length() == 6 || rgb.length() == 8) {
            return QColor(QLatin1Char('#') + rgb);
        }
        return QColor();
    }
    return QColor(s);
}

static QBrush navbarBlockBrush(const QColor &c, bool verticalBar)
{
    QLinearGradient g;
    g.setCoordinateMode(QGradient::ObjectBoundingMode);
    g.setStart(0, 0);
    g.setFinalStop(verticalBar ? QPointF(1, 0) : QPointF(0, 1));
    g.setColorAt(0.0, c.lighter(112));
    g.setColorAt(0.5, c);
    g.setColorAt(1.0, c.darker(112));
    return QBrush(g);
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
    lanes.clear();
    displayRanges.clear();
    displayFrom = 0;
    displayTo = 0;

    const LaneData selectedRange = fetchRangeData(rangeId);
    if (!isValidRange(selectedRange.from, selectedRange.to)) {
        return;
    }
    displayFrom = selectedRange.from;
    displayTo = selectedRange.to;
    displayRanges = selectedRange.ranges;
    if (displayRanges.isEmpty()) {
        displayRanges << RangeSegment{displayFrom, displayTo};
    }

    for (const LaneConfig &config : laneConfigs) {
        LaneData lane = fetchLaneData(config);
        lanes << lane;
    }
}

void VisualNavbar::updateGraphicsScene()
{
    graphicsScene->clear();
    axisToAddress.clear();
    seekGraphicsItem = nullptr;
    PCGraphicsItem = nullptr;
    auto colors = resolvedDataTypeColors();
    graphicsScene->setBackgroundBrush(QBrush(colors[static_cast<int>(DataType::Unallocated)]));

    if (displayTo <= displayFrom || lanes.isEmpty()) {
        return;
    }

    int majorAxisLength = axisLength();
    int crossLength = crossAxisLength();
    if (majorAxisLength <= 0 || crossLength <= 0) {
        return;
    }

    RVA totalSize = displayTo - displayFrom;
    RVA beginAddr = displayFrom;

    const double pixelsPerByte = (double) majorAxisLength / (double) totalSize;
    auto axisFromAddr = [pixelsPerByte, beginAddr](RVA addr) -> double {
        return (addr - beginAddr) * pixelsPerByte;
    };

    axisToAddress.append({0.0, (double) majorAxisLength, displayFrom, displayTo});

    const int laneCount = lanes.count();
    for (int laneIndex = 0; laneIndex < laneCount; laneIndex++) {
        const LaneData &lane = lanes[laneIndex];
        QColor lastBlockColor;
        QGraphicsRectItem *dataItem = nullptr;
        QRectF dataItemRect;

        for (const LaneBlock &block : lane.blocks) {
            if (!block.color.isValid() || block.size == 0) {
                lastBlockColor = QColor();
                dataItem = nullptr;
                continue;
            }

            const RVA blockEnd = block.addr + block.size;
            const RVA visibleStart = qMax(block.addr, displayFrom);
            const RVA visibleEnd = qMin(blockEnd, displayTo);
            if (visibleEnd <= visibleStart) {
                continue;
            }

            double axisStart = axisFromAddr(visibleStart);
            double axisEnd = axisFromAddr(visibleEnd);

            if (dataItem && block.color == lastBlockColor) {
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

            dataItemRect = axisRect(axisStart, axisEnd, laneIndex, laneCount);

            dataItem = new QGraphicsRectItem(dataItemRect);
            dataItem->setPen(Qt::NoPen);
            dataItem->setBrush(navbarBlockBrush(block.color, isVertical()));
            graphicsScene->addItem(dataItem);

            lastBlockColor = block.color;
        }
    }

    QColor separatorColor = Config()->getColor("gui.border");
    if (separatorColor.isValid() && laneCount > 1) {
        for (int laneIndex = 1; laneIndex < laneCount; laneIndex++) {
            double cross = (double) crossLength * laneIndex / laneCount;
            QRectF rect = isVertical() ? QRectF(cross, 0, 1, majorAxisLength)
                                       : QRectF(0, cross, majorAxisLength, 1);
            QGraphicsRectItem *separator = new QGraphicsRectItem(rect);
            separator->setPen(Qt::NoPen);
            separator->setBrush(QBrush(separatorColor));
            graphicsScene->addItem(separator);
        }
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
    if (block.inFunctions > 0) {
        return DataType::Code;
    }
    if (block.blocks > 0) {
        return DataType::Code;
    }
    if (block.strings > 0) {
        return DataType::String;
    }
    if (block.symbols > 0 || block.flags > 0 || block.comments > 0) {
        return DataType::Symbol;
    }
    if (block.rwx != 0) {
        return DataType::Data;
    }
    return DataType::Unallocated;
}

QColor VisualNavbar::colorForDataType(DataType dataType) const
{
    switch (dataType) {
    case DataType::Code:
        return Config()->getColor("ai.exec");
    case DataType::Data:
        return Config()->getColor("ai.read");
    case DataType::String:
        return Config()->getColor("ai.ascii");
    case DataType::Symbol:
        return Config()->getColor("flag");
    case DataType::Unallocated:
    case DataType::Count:
        return Config()->getColor("diff.match");
    }

    return QColor();
}

QColor VisualNavbar::colorForPermissions(ut8 rwx) const
{
    if ((rwx & 0x7) == 0x7) {
        return Config()->getColor("gui.item_unsafe");
    }
    if (rwx & (1 << 2)) {
        return colorForDataType(DataType::Code);
    }
    if (rwx & (1 << 1)) {
        return colorForDataType(DataType::String);
    }
    if (rwx & (1 << 0)) {
        return colorForDataType(DataType::Symbol);
    }
    return colorForDataType(DataType::Unallocated);
}

QColor VisualNavbar::colorForValue(int value) const
{
    value = qBound(0, value, 255);
    if (value == 0) {
        return colorForDataType(DataType::Unallocated);
    }
    int hue = 220 - (value * 220 / 255);
    int saturation = 170 + (value * 60 / 255);
    int light = 80 + (value * 175 / 255);
    return QColor::fromHsv(hue, qBound(0, saturation, 255), qBound(0, light, 255));
}

std::array<QColor, static_cast<int>(VisualNavbar::DataType::Count)>
VisualNavbar::resolvedDataTypeColors() const
{
    std::array<QColor, static_cast<int>(DataType::Count)> colors;
    for (int i = 0; i < static_cast<int>(DataType::Count); i++) {
        colors[i] = colorForDataType(static_cast<DataType>(i));
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
    pickUniqueColor(DataType::Data, {"ai.read", "ai.write", "graph.box", "gui.navbar.data"});
    pickUniqueColor(DataType::String, {"ai.ascii", "comment", "usrcmt", "var.type", "gui.navbar.str"});
    pickUniqueColor(DataType::Symbol, {"flag", "label", "fname", "other", "gui.navbar.sym"});
    pickUniqueColor(
        DataType::Unallocated,
        {"diff.match", "graph.false", "graph.box4", "widget.bg", "gui.navbar.empty"});

    return colors;
}

VisualNavbar::LaneData VisualNavbar::fetchLaneData(const LaneConfig &config)
{
    if (config.sourceId == QLatin1String("none")) {
        LaneData lane;
        lane.sourceId = config.sourceId;
        lane.rangeId = rangeId;
        lane.title = sourceLabel(config.sourceId);
        lane.from = displayFrom;
        lane.to = displayTo;
        return lane;
    }
    if (config.sourceId == QLatin1String("analysis")) {
        return fetchMetadataLane(config, LaneSourceKind::Analysis);
    }
    if (config.sourceId == QLatin1String("permissions")) {
        return fetchMetadataLane(config, LaneSourceKind::Permissions);
    }
    if (config.sourceId == QLatin1String("minus-entropy")) {
        return fetchBarsLane(config, QStringLiteral("e"), true);
    }

    QString mode = barsModeForSource(config.sourceId);
    if (mode.isEmpty()) {
        LaneData lane;
        lane.sourceId = config.sourceId;
        lane.rangeId = rangeId;
        lane.title = sourceLabel(config.sourceId);
        lane.from = displayFrom;
        lane.to = displayTo;
        return lane;
    }
    return fetchBarsLane(config, mode, false);
}

VisualNavbar::LaneData VisualNavbar::fetchRangeData(const QString &rangeId)
{
    LaneData lane;
    lane.rangeId = rangeId;

    QJsonObject statsObj;
    {
        TempConfig tempConfig;
        tempConfig.set(QStringLiteral("search.in"), rangeId);
        statsObj = Core()->cmdj(QStringLiteral("p-j %1").arg(statsAxisLength)).object();
    }

    lane.from = statsObj[QStringLiteral("from")].toVariant().toULongLong();
    lane.to = statsObj[QStringLiteral("to")].toVariant().toULongLong();
    lane.ranges = rangesFromJson(statsObj, lane.from, lane.to);
    if (isValidRange(lane.from, lane.to) || !canUseZoomRangeFallback(rangeId)) {
        return lane;
    }

    QJsonObject barsObj;
    {
        TempConfig tempConfig;
        tempConfig.set(QStringLiteral("zoom.in"), rangeId);
        barsObj = Core()->cmdj(QStringLiteral("p=ej %1").arg(statsAxisLength)).object();
    }

    lane.from = barsObj[QStringLiteral("address")].toVariant().toULongLong();
    lane.to = lane.from + barsObj[QStringLiteral("size")].toVariant().toULongLong();
    lane.ranges = rangesFromJson(barsObj, lane.from, lane.to);
    return lane;
}

QList<VisualNavbar::RangeSegment> VisualNavbar::rangesFromJson(
    const QJsonObject &object, RVA fallbackFrom, RVA fallbackTo) const
{
    QList<RangeSegment> ranges;
    const QJsonArray rangesArray = object[QStringLiteral("ranges")].toArray();
    for (const QJsonValue value : rangesArray) {
        const QJsonObject rangeObj = value.toObject();
        RangeSegment range;
        range.from = rangeObj[QStringLiteral("from")].toVariant().toULongLong();
        range.to = rangeObj[QStringLiteral("to")].toVariant().toULongLong();
        if (isValidRange(range.from, range.to)) {
            ranges << range;
        }
    }

    std::sort(ranges.begin(), ranges.end(), [](const RangeSegment &a, const RangeSegment &b) {
        return a.from < b.from;
    });

    QList<RangeSegment> mergedRanges;
    for (const RangeSegment &range : ranges) {
        if (mergedRanges.isEmpty() || mergedRanges.last().to < range.from) {
            mergedRanges << range;
            continue;
        }
        mergedRanges.last().to = qMax(mergedRanges.last().to, range.to);
    }

    if (mergedRanges.isEmpty() && isValidRange(fallbackFrom, fallbackTo)) {
        mergedRanges << RangeSegment{fallbackFrom, fallbackTo};
    }
    return mergedRanges;
}

VisualNavbar::LaneData VisualNavbar::fetchMetadataLane(
    const LaneConfig &config, LaneSourceKind sourceKind)
{
    LaneData lane;
    lane.sourceId = config.sourceId;
    lane.rangeId = rangeId;
    lane.title = sourceLabel(config.sourceId);

    QJsonObject statsObj;
    {
        TempConfig tempConfig;
        tempConfig.set(QStringLiteral("search.in"), rangeId);
        statsObj = Core()->cmdj(QStringLiteral("p-j %1").arg(statsAxisLength)).object();
    }

    lane.from = statsObj[QStringLiteral("from")].toVariant().toULongLong();
    lane.to = statsObj[QStringLiteral("to")].toVariant().toULongLong();
    lane.blocksize = statsObj[QStringLiteral("blocksize")].toVariant().toULongLong();

    const auto colors = resolvedDataTypeColors();
    const QJsonArray blocksArray = statsObj[QStringLiteral("blocks")].toArray();
    for (const QJsonValue value : blocksArray) {
        const QJsonObject blockObj = value.toObject();
        BlockDescription block;
        block.addr = blockAddress(blockObj);
        block.size = blockObj[QStringLiteral("size")].toVariant().toULongLong();
        if (block.size == 0) {
            block.size = lane.blocksize;
        }
        block.flags = blockObj[QStringLiteral("flags")].toInt(0);
        block.functions = blockObj[QStringLiteral("functions")].toInt(0);
        block.inFunctions = blockObj[QStringLiteral("in_functions")].toInt(0);
        block.comments = blockObj[QStringLiteral("comments")].toInt(0);
        block.symbols = blockObj[QStringLiteral("symbols")].toInt(0);
        block.strings = blockObj[QStringLiteral("strings")].toInt(0);
        block.blocks = blockObj[QStringLiteral("blocks")].toInt(0);
        block.color = blockObj[QStringLiteral("color")].toString();

        QString perm = blockObj[QStringLiteral("rwx")].toString();
        if (perm.isEmpty()) {
            perm = blockObj[QStringLiteral("perm")].toString();
        }
        block.rwx = rwxFromString(perm);

        LaneBlock laneBlock;
        laneBlock.addr = block.addr;
        laneBlock.size = block.size;

        if (sourceKind == LaneSourceKind::Permissions) {
            laneBlock.color = colorForPermissions(block.rwx);
            laneBlock.text = perm;
        } else {
            QColor blockColor = parseR2Color(block.color);
            DataType dataType = dataTypeForBlock(block);
            if (blockColor.isValid()) {
                laneBlock.color = blockColor;
            } else {
                laneBlock.color = colors[static_cast<int>(dataType)];
            }
        }

        if (laneBlock.color.isValid()) {
            lane.blocks << laneBlock;
        }
    }

    return lane;
}

VisualNavbar::LaneData VisualNavbar::fetchBarsLane(
    const LaneConfig &config, const QString &mode, bool minusEntropy)
{
    LaneData lane;
    lane.sourceId = config.sourceId;
    lane.rangeId = rangeId;
    lane.title = sourceLabel(config.sourceId);

    QJsonObject root;
    {
        TempConfig tempConfig;
        tempConfig
            .set(minusEntropy ? QStringLiteral("search.in") : QStringLiteral("zoom.in"), rangeId);
        QString cmd = minusEntropy ? QStringLiteral("p-ej %1").arg(statsAxisLength)
                                   : QStringLiteral("p=%1j %2").arg(mode).arg(statsAxisLength);
        root = Core()->cmdj(cmd).object();
    }

    lane.blocksize = root[QStringLiteral("blocksize")].toVariant().toULongLong();
    lane.from = root[QStringLiteral("address")].toVariant().toULongLong();
    lane.to = lane.from + root[QStringLiteral("size")].toVariant().toULongLong();

    QJsonArray blocksArray;
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        if (it.value().isArray()) {
            blocksArray = it.value().toArray();
            break;
        }
    }

    for (const QJsonValue value : blocksArray) {
        const QJsonObject object = value.toObject();
        LaneBlock block;
        block.addr = blockAddress(object);
        block.size = lane.blocksize;
        int barValue = object[QStringLiteral("value")].toInt();
        block.color = colorForValue(barValue);
        block.text = QString::number(barValue);
        lane.blocks << block;
    }

    return lane;
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

void VisualNavbar::setLaneSource(int laneIndex, const QString &sourceId)
{
    if (laneIndex < 0 || laneIndex >= laneConfigs.count()) {
        return;
    }
    if (laneConfigs[laneIndex].sourceId == sourceId) {
        return;
    }
    laneConfigs[laneIndex].sourceId = sourceId;
    fetchAndPaintData();
}

void VisualNavbar::setRange(const QString &newRangeId)
{
    if (rangeId == newRangeId) {
        return;
    }
    rangeId = newRangeId;
    fetchAndPaintData();
}

void VisualNavbar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        showRelocateMenu(event->globalPosition().toPoint());
#else
        showRelocateMenu(event->globalPos());
#endif
        event->accept();
        return;
    }

    qreal pos = eventAxisPosition(event);
    if (std::isnan(pos)) {
        QToolBar::mousePressEvent(event);
        return;
    }

    RVA address = localPositionToAddress(pos);
    if (address != RVA_INVALID) {
        address = Core()->alignInstructionAddress(address);
        int laneIndex = eventLaneIndex(event);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        QToolTip::showText(
            event->globalPosition().toPoint(), toolTipForAddress(address, laneIndex), this);
#else
        QToolTip::showText(event->globalPos(), toolTipForAddress(address, laneIndex), this);
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

void VisualNavbar::contextMenuEvent(QContextMenuEvent *event)
{
    event->accept();
    showRelocateMenu(event->globalPos());
}

bool VisualNavbar::eventFilter(QObject *object, QEvent *event)
{
    Q_UNUSED(object);

    if (!suppressContextMenuEvents) {
        return QToolBar::eventFilter(object, event);
    }

    if (event->type() == QEvent::ContextMenu) {
        event->accept();
        return true;
    }

    if (event->type() == QEvent::MouseButtonRelease) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::RightButton) {
            event->accept();
            return true;
        }
    }

    return QToolBar::eventFilter(object, event);
}

void VisualNavbar::showRelocateMenu(const QPoint &globalPos)
{
    using Location = Configuration::VisualNavbarLocation;
    QMenu menu(this);

    auto addMenuChoice = [&](QMenu *target, const QString &label, bool isCurrent, auto apply) {
        QAction *a = target->addAction(label);
        a->setCheckable(true);
        a->setChecked(isCurrent);
        connect(a, &QAction::triggered, this, [apply]() { apply(); });
    };

    const std::pair<Location, const char *> locations[] = {
        {Location::SuperTop, QT_TR_NOOP("SuperTop")},
        {Location::Top, QT_TR_NOOP("Top")},
        {Location::Left, QT_TR_NOOP("Left")},
        {Location::Right, QT_TR_NOOP("Right")},
        {Location::Bottom, QT_TR_NOOP("Bottom")},
        {Location::SuperBottom, QT_TR_NOOP("SuperBottom")},
    };
    for (int laneIndex = 0; laneIndex < laneConfigs.count(); laneIndex++) {
        const LaneConfig config = laneConfigs[laneIndex];
        QMenu *laneMenu = menu.addMenu(tr("Lane %1").arg(laneIndex + 1));
        laneMenu->setToolTipsVisible(true);

        for (const NavbarChoice &choice : SourceChoices) {
            const QString sourceId = QString::fromLatin1(choice.id);
            addMenuChoice(
                laneMenu,
                tr(choice.label),
                sourceId == config.sourceId,
                [this, laneIndex, sourceId] { setLaneSource(laneIndex, sourceId); });
        }
    }

    menu.addSeparator();
    const Location currentLoc = Config()->getVisualNavbarLocation();
    QMenu *positionMenu = menu.addMenu(tr("Position"));
    for (const auto &[loc, label] : locations) {
        addMenuChoice(positionMenu, tr(label), loc == currentLoc, [loc] {
            Config()->setVisualNavbarLocation(loc);
        });
    }

    QMenu *rangeMenu = menu.addMenu(tr("Range"));
    for (const NavbarChoice &choice : RangeChoices) {
        const QString choiceRangeId = QString::fromLatin1(choice.id);
        addMenuChoice(rangeMenu, tr(choice.label), choiceRangeId == rangeId, [this, choiceRangeId] {
            setRange(choiceRangeId);
        });
    }

    const std::pair<int, const char *> sizes[] = {
        {8, QT_TR_NOOP("Small")},
        {15, QT_TR_NOOP("Medium")},
        {32, QT_TR_NOOP("Large")},
        {64, QT_TR_NOOP("X-Large")},
        {128, QT_TR_NOOP("XX-Large")},
    };
    const int currentSize = Config()->getVisualNavbarThickness();
    QMenu *thicknessMenu = menu.addMenu(tr("Thickness"));
    for (const auto &[size, label] : sizes) {
        addMenuChoice(thicknessMenu, tr(label), size == currentSize, [size] {
            Config()->setVisualNavbarThickness(size);
        });
    }

    menu.exec(globalPos);

    if (!suppressContextMenuEvents) {
        qApp->installEventFilter(this);
    }
    suppressContextMenuEvents = true;
    QTimer::singleShot(250, this, [this]() {
        suppressContextMenuEvents = false;
        qApp->removeEventFilter(this);
    });
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

double VisualNavbar::eventCrossAxisPosition(QMouseEvent *event) const
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

    return isVertical() ? localPos.x() - viewGeometry.left() : localPos.y() - viewGeometry.top();
}

int VisualNavbar::eventLaneIndex(QMouseEvent *event) const
{
    if (lanes.isEmpty()) {
        return -1;
    }

    double crossPosition = eventCrossAxisPosition(event);
    if (std::isnan(crossPosition)) {
        return -1;
    }

    int crossLength = crossAxisLength();
    if (crossLength <= 0) {
        return -1;
    }

    int laneIndex = (int) (crossPosition * lanes.count() / crossLength);
    return qBound(0, laneIndex, lanes.count() - 1);
}

QRectF VisualNavbar::axisRect(double axisStart, double axisEnd, int laneIndex, int laneCount) const
{
    double normalizedAxisEnd = qMax(axisStart + 1.0, axisEnd);
    int crossLength = crossAxisLength();
    laneCount = qMax(1, laneCount);
    double crossStart = (double) crossLength * laneIndex / laneCount;
    double crossEnd = (double) crossLength * (laneIndex + 1) / laneCount;
    double laneCrossLength = qMax(1.0, crossEnd - crossStart);
    if (isVertical()) {
        return QRectF(crossStart, axisStart, laneCrossLength, normalizedAxisEnd - axisStart);
    }
    return QRectF(axisStart, crossStart, normalizedAxisEnd - axisStart, laneCrossLength);
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

QString VisualNavbar::toolTipForAddress(RVA address, int laneIndex)
{
    QString label = tr("Address");
    if (laneIndex >= 0 && laneIndex < lanes.count()) {
        label = sourceLabel(lanes[laneIndex].sourceId);
    }
    QString ret = label + ": " + RAddressString(address);

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
