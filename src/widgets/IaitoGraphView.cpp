#include "IaitoGraphView.h"

#include "core/Iaito.h"
#include "common/Configuration.h"
#include "dialogs/MultitypeFileSaveDialog.h"
#include "TempConfig.h"

#include <cmath>

#include <QStandardPaths>
#include <QActionGroup>


static const int KEY_ZOOM_IN = Qt::Key_Plus + Qt::ControlModifier;
static const int KEY_ZOOM_OUT = Qt::Key_Minus + Qt::ControlModifier;
static const int KEY_ZOOM_RESET = Qt::Key_Equal + Qt::ControlModifier;

static const uint64_t BITMPA_EXPORT_WARNING_SIZE = 32 * 1024 * 1024;

#ifndef NDEBUG
#define GRAPH_GRID_DEBUG_MODES true
#else
#define GRAPH_GRID_DEBUG_MODES false
#endif

IaitoGraphView::IaitoGraphView(QWidget *parent)
    : GraphView(parent)
    , mFontMetrics(nullptr)
    , actionExportGraph(tr("Export Graph"), this)
    , graphLayout(GraphView::Layout::GridMedium)
{
    connect(Core(), &IaitoCore::graphOptionsChanged, this, &IaitoGraphView::refreshView);
    connect(Config(), &Configuration::colorsUpdated, this, &IaitoGraphView::colorsUpdatedSlot);
    connect(Config(), &Configuration::fontsUpdated, this, &IaitoGraphView::fontsUpdatedSlot);

    initFont();
    updateColors();

    connect(&actionExportGraph, &QAction::triggered, this, &IaitoGraphView::showExportDialog);

    layoutMenu = new QMenu(tr("Layout"), this);
    horizontalLayoutAction = layoutMenu->addAction(tr("Horizontal"));
    horizontalLayoutAction->setCheckable(true);

    static const std::pair<QString, GraphView::Layout> LAYOUT_CONFIG[] = {
        {tr("Grid narrow"), GraphView::Layout::GridNarrow}
        , {tr("Grid medium"), GraphView::Layout::GridMedium}
        , {tr("Grid wide"), GraphView::Layout::GridWide}
#if GRAPH_GRID_DEBUG_MODES
        , {"GridAAA", GraphView::Layout::GridAAA}
        , {"GridAAB", GraphView::Layout::GridAAB}
        , {"GridABA", GraphView::Layout::GridABA}
        , {"GridABB", GraphView::Layout::GridABB}
        , {"GridBAA", GraphView::Layout::GridBAA}
        , {"GridBAB", GraphView::Layout::GridBAB}
        , {"GridBBA", GraphView::Layout::GridBBA}
        , {"GridBBB", GraphView::Layout::GridBBB}
#endif
#ifdef IAITO_ENABLE_GRAPHVIZ
        , {tr("Graphviz polyline"), GraphView::Layout::GraphvizPolyline}
        , {tr("Graphviz ortho"), GraphView::Layout::GraphvizOrtho}
        , {tr("Graphviz sfdp"), GraphView::Layout::GraphvizSfdp}
        , {tr("Graphviz neato"), GraphView::Layout::GraphvizNeato}
        , {tr("Graphviz twopi"), GraphView::Layout::GraphvizTwoPi}
        , {tr("Graphviz circo"), GraphView::Layout::GraphvizCirco}
#endif
    };
    layoutMenu->addSeparator();
    connect(horizontalLayoutAction, &QAction::toggled, this, &IaitoGraphView::updateLayout);
    QActionGroup *layoutGroup = new QActionGroup(layoutMenu);
    for (auto &item : LAYOUT_CONFIG) {
        auto action = layoutGroup->addAction(item.first);
        action->setCheckable(true);
        GraphView::Layout layout = item.second;
        if (layout == this->graphLayout) {
            action->setChecked(true);
        }
        connect(action, &QAction::triggered, this, [this, layout]() {
            this->graphLayout = layout;
            updateLayout();
        });

    }
    layoutMenu->addActions(layoutGroup->actions());

    grabGesture(Qt::PinchGesture);
}

QPoint IaitoGraphView::getTextOffset(int line) const
{
    int padding = static_cast<int>(2 * charWidth);
    return QPoint(padding, padding + line * charHeight);
}

void IaitoGraphView::initFont()
{
    setFont(Config()->getFont());
    QFontMetricsF metrics(font());
    baseline = int(metrics.ascent());
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    charWidth = 16;
#else
    charWidth = metrics.width('X');
#endif
    charHeight = static_cast<int>(metrics.height());
    charOffset = 0;
    mFontMetrics.reset(new CachedFontMetrics<qreal>(font()));
}

void IaitoGraphView::zoom(QPointF mouseRelativePos, double velocity)
{
    qreal newScale = getViewScale() * std::pow(1.25, velocity);
    setZoom(mouseRelativePos, newScale);
}

void IaitoGraphView::setZoom(QPointF mouseRelativePos, double scale)
{
    mouseRelativePos.rx() *= size().width();
    mouseRelativePos.ry() *= size().height();
    mouseRelativePos /= getViewScale();

    auto globalMouse = mouseRelativePos + getViewOffset();
    mouseRelativePos *= getViewScale();
    qreal newScale = scale;
    newScale = std::max(newScale, 0.05);
    mouseRelativePos /= newScale;
    setViewScale(newScale);

    // Adjusting offset, so that zooming will be approaching to the cursor.
    setViewOffset(globalMouse.toPoint() - mouseRelativePos.toPoint());

    viewport()->update();
    emit viewZoomed();
}

void IaitoGraphView::zoomIn()
{
    zoom(QPointF(0.5, 0.5), 1);
}

void IaitoGraphView::zoomOut()
{
    zoom(QPointF(0.5, 0.5), -1);
}

void IaitoGraphView::zoomReset()
{
    setZoom(QPointF(0.5, 0.5), 1);
}

void IaitoGraphView::showExportDialog()
{
    showExportGraphDialog("graph", "", RVA_INVALID);
}

void IaitoGraphView::updateColors()
{
    disassemblyBackgroundColor = ConfigColor("gui.alt_background");
    disassemblySelectedBackgroundColor = ConfigColor("gui.disass_selected");
    mDisabledBreakpointColor = disassemblyBackgroundColor;
    graphNodeColor = ConfigColor("gui.border");
    backgroundColor = ConfigColor("gui.background");
    disassemblySelectionColor = ConfigColor("lineHighlight");
    PCSelectionColor = ConfigColor("highlightPC");

    jmpColor = ConfigColor("graph.trufae");
    brtrueColor = ConfigColor("graph.true");
    brfalseColor = ConfigColor("graph.false");

    mCommentColor = ConfigColor("comment");
}

void IaitoGraphView::colorsUpdatedSlot()
{
    updateColors();
    refreshView();
}

GraphLayout::LayoutConfig IaitoGraphView::getLayoutConfig()
{
    auto blockSpacing = Config()->getGraphBlockSpacing();
    auto edgeSpacing = Config()->getGraphEdgeSpacing();
    GraphLayout::LayoutConfig layoutConfig;
    layoutConfig.blockHorizontalSpacing = blockSpacing.x();
    layoutConfig.blockVerticalSpacing = blockSpacing.y();
    layoutConfig.edgeHorizontalSpacing = edgeSpacing.x();
    layoutConfig.edgeVerticalSpacing = edgeSpacing.y();
    return layoutConfig;
}

void IaitoGraphView::updateLayout()
{
    setGraphLayout(GraphView::makeGraphLayout(graphLayout, horizontalLayoutAction->isChecked()));
    saveCurrentBlock();
    setLayoutConfig(getLayoutConfig());
    computeGraphPlacement();
    restoreCurrentBlock();
    emit viewRefreshed();
}

void IaitoGraphView::fontsUpdatedSlot()
{
    initFont();
    refreshView();
}

// bool IaitoGraphView::event(QEvent *event) // nope
void IaitoGraphView::keyPressEvent(QKeyEvent *keyEvent)
{
    // QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event); // nope
    if (keyEvent == NULL) {
	    return;
    }
    int key = keyEvent->key() | keyEvent->modifiers();
    switch (key) {
    case KEY_ZOOM_IN:
    case KEY_ZOOM_IN | KEY_ZOOM_IN | Qt::ShiftModifier:
            zoomIn();
	    break;
    case KEY_ZOOM_OUT:
            zoomOut();
	    break;
    case KEY_ZOOM_RESET:
            zoomReset();
	    break;
    }
#if 0
    switch (event->type()) {
    case QEvent::ShortcutOverride:
        if (key == KEY_ZOOM_OUT || key == KEY_ZOOM_RESET
                || key == KEY_ZOOM_IN || (key == (KEY_ZOOM_IN | Qt::ShiftModifier))) {
            event->accept();
            return ;
        }
        break;
    }
    GraphView::event(event);
#endif
}

void IaitoGraphView::refreshView()
{
    initFont();
    setLayoutConfig(getLayoutConfig());
}

bool IaitoGraphView::gestureEvent(QGestureEvent *event)
{
    if (!event) {
        return false;
    }

    if (auto gesture =
                static_cast<QPinchGesture *>(event->gesture(Qt::PinchGesture))) {
        auto changeFlags = gesture->changeFlags();

        if (changeFlags & QPinchGesture::ScaleFactorChanged) {
            auto cursorPos = gesture->centerPoint();
            cursorPos.rx() /= size().width();
            cursorPos.ry() /= size().height();

            setZoom(cursorPos, getViewScale() * gesture->scaleFactor());
        }

        event->accept(gesture);
        return true;
    }

    return false;
}

void IaitoGraphView::wheelEvent(QWheelEvent *event)
{
    // when CTRL is pressed, we zoom in/out with mouse wheel
    if (Qt::ControlModifier == event->modifiers()) {
        const QPoint numDegrees = event->angleDelta() / 8;
        if (!numDegrees.isNull()) {
            int numSteps = numDegrees.y() / 15;

#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
            QPointF relativeMousePos = event->pos();
#else
            QPointF relativeMousePos = event->position();
#endif
            relativeMousePos.rx() /= size().width();
            relativeMousePos.ry() /= size().height();

            zoom(relativeMousePos, numSteps);
        }
        event->accept();
    } else {
        // use mouse wheel for scrolling when CTRL is not pressed
        GraphView::wheelEvent(event);
    }
    emit graphMoved();
}

void IaitoGraphView::resizeEvent(QResizeEvent *event)
{
    GraphView::resizeEvent(event);
    emit resized();
}

void IaitoGraphView::saveCurrentBlock()
{
}

void IaitoGraphView::restoreCurrentBlock()
{
}


void IaitoGraphView::mousePressEvent(QMouseEvent *event)
{
    GraphView::mousePressEvent(event);
    emit graphMoved();
}

void IaitoGraphView::mouseMoveEvent(QMouseEvent *event)
{
    GraphView::mouseMoveEvent(event);
    emit graphMoved();
}

void IaitoGraphView::exportGraph(QString filePath, GraphExportType type, QString graphCommand,
                                  RVA address)
{
    bool graphTransparent = Config()->getBitmapTransparentState();
    double graphScaleFactor = Config()->getBitmapExportScaleFactor();
    switch (type) {
    case GraphExportType::Png:
        this->saveAsBitmap(filePath, "png", graphScaleFactor, graphTransparent);
        break;
    case GraphExportType::Jpeg:
        this->saveAsBitmap(filePath, "jpg", graphScaleFactor, false);
        break;
    case GraphExportType::Svg:
        this->saveAsSvg(filePath);
        break;

    case GraphExportType::GVDot:
        exportR2TextGraph(filePath, graphCommand + "d", address);
        break;
    case GraphExportType::R2Json:
        exportR2TextGraph(filePath, graphCommand + "j", address);
        break;
    case GraphExportType::R2Gml:
        exportR2TextGraph(filePath, graphCommand + "g", address);
        break;
    case GraphExportType::R2SDBKeyValue:
        exportR2TextGraph(filePath, graphCommand + "k", address);
        break;

    case GraphExportType::GVJson:
        exportR2GraphvizGraph(filePath, "json", graphCommand, address);
        break;
    case GraphExportType::GVGif:
        exportR2GraphvizGraph(filePath, "gif", graphCommand, address);
        break;
    case GraphExportType::GVPng:
        exportR2GraphvizGraph(filePath, "png", graphCommand, address);
        break;
    case GraphExportType::GVJpeg:
        exportR2GraphvizGraph(filePath, "jpg", graphCommand, address);
        break;
    case GraphExportType::GVPostScript:
        exportR2GraphvizGraph(filePath, "ps", graphCommand, address);
        break;
    case GraphExportType::GVSvg:
        exportR2GraphvizGraph(filePath, "svg", graphCommand, address);
        break;
    }
}

void IaitoGraphView::exportR2GraphvizGraph(QString filePath, QString type, QString graphCommand,
                                            RVA address)
{
    TempConfig tempConfig;
    tempConfig.set("graph.gv.format", type);
    qWarning() << Core()->cmdRawAt(QString("%0w \"%1\"").arg(graphCommand).arg(filePath),  address);
}

void IaitoGraphView::exportR2TextGraph(QString filePath, QString graphCommand, RVA address)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Can't open file";
        return;
    }
    QTextStream fileOut(&file);
    fileOut << Core()->cmdRawAt(QString("%0").arg(graphCommand), address);
}

bool IaitoGraphView::graphIsBitamp(IaitoGraphView::GraphExportType type)
{
    switch (type) {
    case GraphExportType::Png:
    case GraphExportType::Jpeg:
    case GraphExportType::GVGif:
    case GraphExportType::GVPng:
    case GraphExportType::GVJpeg:
        return true;
    default:
        return false;
    }
}


Q_DECLARE_METATYPE(IaitoGraphView::GraphExportType);

void IaitoGraphView::showExportGraphDialog(QString defaultName, QString graphCommand, RVA address)
{
    QVector<MultitypeFileSaveDialog::TypeDescription> types = {
        {tr("PNG (*.png)"), "png", QVariant::fromValue(GraphExportType::Png)},
        {tr("JPEG (*.jpg)"), "jpg", QVariant::fromValue(GraphExportType::Jpeg)},
        {tr("SVG (*.svg)"), "svg", QVariant::fromValue(GraphExportType::Svg)}
    };

    bool r2GraphExports = !graphCommand.isEmpty();
    if (r2GraphExports) {
        types.append({
            {tr("Graphviz dot (*.dot)"), "dot", QVariant::fromValue(GraphExportType::GVDot)},
            {tr("Graph Modelling Language (*.gml)"), "gml", QVariant::fromValue(GraphExportType::R2Gml)},
            {tr("R2 JSON (*.json)"), "json", QVariant::fromValue(GraphExportType::R2Json)},
            {tr("SDB key-value (*.txt)"), "txt", QVariant::fromValue(GraphExportType::R2SDBKeyValue)},
        });
        bool hasGraphviz = !QStandardPaths::findExecutable("dot").isEmpty()
                           || !QStandardPaths::findExecutable("xdot").isEmpty();
        if (hasGraphviz) {
            types.append({
                {tr("Graphviz json (*.json)"), "json", QVariant::fromValue(GraphExportType::GVJson)},
                {tr("Graphviz gif (*.gif)"), "gif", QVariant::fromValue(GraphExportType::GVGif)},
                {tr("Graphviz png (*.png)"), "png", QVariant::fromValue(GraphExportType::GVPng)},
                {tr("Graphviz jpg (*.jpg)"), "jpg", QVariant::fromValue(GraphExportType::GVJpeg)},
                {tr("Graphviz PostScript (*.ps)"), "ps", QVariant::fromValue(GraphExportType::GVPostScript)},
                {tr("Graphviz svg (*.svg)"), "svg", QVariant::fromValue(GraphExportType::GVSvg)}
            });
        }
    }

    MultitypeFileSaveDialog dialog(this, tr("Export Graph"));
    dialog.setTypes(types);
    dialog.selectFile(defaultName);
    if (!dialog.exec()) {
        return;
    }

    auto selectedType = dialog.selectedType();
    if (!selectedType.data.canConvert<GraphExportType>()) {
        qWarning() << "Bad selected type, should not happen.";
        return;
    }
    auto exportType = selectedType.data.value<GraphExportType>();

    if (graphIsBitamp(exportType)) {
        uint64_t bitmapSize = uint64_t(width) * uint64_t(height);
        if (bitmapSize > BITMPA_EXPORT_WARNING_SIZE) {
            auto answer = QMessageBox::question(this,
                                                tr("Graph Export"),
                                                tr("Do you really want to export %1 x %2 = %3 pixel bitmap image? Consider using different format.")
                                                .arg(width).arg(height).arg(bitmapSize));
            if (answer != QMessageBox::Yes) {
                return;
            }
        }
    }

    QString filePath = dialog.selectedFiles().first();
    exportGraph(filePath, exportType, graphCommand, address);

}
