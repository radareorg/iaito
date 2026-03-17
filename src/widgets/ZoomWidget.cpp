#include "ZoomWidget.h"
#include "common/TempConfig.h"
#include "core/MainWindow.h"

#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QSet>
#include <QToolTip>
#include <QVBoxLayout>

// ZoomView implementation

ZoomView::ZoomView(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
}

void ZoomView::setData(const QVector<BlockEntry> &blocks)
{
    m_blocks = blocks;
    updateMinimumSize();
    update();
}

void ZoomView::setColumns(int cols)
{
    m_columns = qMax(1, cols);
    updateMinimumSize();
    update();
}

void ZoomView::setColorMode(ColorMode mode)
{
    m_colorMode = mode;
    update();
}

void ZoomView::setSeekAddress(RVA addr)
{
    m_seekAddr = addr;
    update();
}

void ZoomView::updateMinimumSize()
{
    if (m_blocks.isEmpty() || m_columns <= 0) {
        setMinimumSize(0, 0);
        return;
    }
    int rows = (m_blocks.size() + m_columns - 1) / m_columns;
    int w = m_columns * cellPitch();
    int h = rows * cellPitch();
    setMinimumSize(w, h);
    resize(w, h);
}

QColor ZoomView::colorForValue(int value) const
{
    value = qBound(0, value, 255);

    switch (m_colorMode) {
    case ColorMode::Greyscale:
        return QColor(value, value, value);
    case ColorMode::Rainbow:
        if (value == 0) {
            return QColor(10, 10, 10);
        }
        return QColor::fromHsv((value * 300) / 255, 200, 60 + (value * 195) / 255);
    case ColorMode::Theme: {
        QColor base = Config()->getColor("gui.navbar.code");
        if (!base.isValid()) {
            base = QColor(80, 120, 200);
        }
        int h, s, v;
        base.getHsv(&h, &s, &v);
        return QColor::fromHsv(h, s, qMax(10, (value * 250) / 255));
    }
    case ColorMode::ThemePalette: {
        if (m_themePalette.isEmpty()) {
            return QColor(value, value, value);
        }
        int idx = (value * (m_themePalette.size() - 1)) / 255;
        return m_themePalette[qBound(0, idx, m_themePalette.size() - 1)];
    }
    }
    return QColor();
}

void ZoomView::rebuildThemePalette()
{
    m_themePalette.clear();

    // Collect unique colors from the current r2 color theme
    QJsonDocument doc = Core()->cmdj("ecj");
    QJsonObject theme = doc.object();

    QSet<QRgb> seen;
    QVector<QColor> colors;

    for (auto it = theme.constBegin(); it != theme.constEnd(); ++it) {
        QJsonArray rgb = it.value().toArray();
        if (rgb.size() < 3) {
            continue;
        }
        QColor c(rgb[0].toInt(), rgb[1].toInt(), rgb[2].toInt());
        if (!c.isValid() || seen.contains(c.rgb())) {
            continue;
        }
        seen.insert(c.rgb());
        colors.append(c);
    }

    if (colors.isEmpty()) {
        // Fallback: at least two entries so interpolation works
        m_themePalette = {QColor(10, 10, 10), QColor(240, 240, 240)};
        return;
    }

    // Sort by perceived luminance (ITU-R BT.601)
    std::sort(colors.begin(), colors.end(), [](const QColor &a, const QColor &b) {
        auto luma = [](const QColor &c) {
            return 0.299 * c.redF() + 0.587 * c.greenF() + 0.114 * c.blueF();
        };
        return luma(a) < luma(b);
    });

    // Build a 256-entry ramp by interpolating between the sorted theme colors
    if (colors.size() == 1) {
        // Single color: vary its brightness from dark to full
        int h, s, v;
        colors[0].getHsv(&h, &s, &v);
        for (int i = 0; i < 256; i++) {
            m_themePalette.append(QColor::fromHsv(h, s, qMax(10, (i * 250) / 255)));
        }
        return;
    }

    for (int i = 0; i < 256; i++) {
        double pos = (double) i / 255.0 * (colors.size() - 1);
        int lo = qBound(0, (int) pos, colors.size() - 2);
        double frac = pos - lo;
        const QColor &ca = colors[lo];
        const QColor &cb = colors[lo + 1];
        int r = (int) (ca.red() + frac * (cb.red() - ca.red()));
        int g = (int) (ca.green() + frac * (cb.green() - ca.green()));
        int b = (int) (ca.blue() + frac * (cb.blue() - ca.blue()));
        m_themePalette.append(QColor(r, g, b));
    }
}

int ZoomView::blockIndexAt(const QPoint &pos) const
{
    int pitch = cellPitch();
    if (pitch <= 0) {
        return -1;
    }
    int col = pos.x() / pitch;
    int row = pos.y() / pitch;
    // Check if the click is in the gap between cells
    if (pos.x() % pitch >= CellSize || pos.y() % pitch >= CellSize) {
        return -1;
    }
    if (col < 0 || col >= m_columns || row < 0) {
        return -1;
    }
    int idx = row * m_columns + col;
    if (idx < 0 || idx >= m_blocks.size()) {
        return -1;
    }
    return idx;
}

void ZoomView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);

    // Dark background for the grid area
    painter.fillRect(rect(), palette().color(QPalette::Window));

    int pitch = cellPitch();
    int seekIndex = -1;

    for (int i = 0; i < m_blocks.size(); i++) {
        int col = i % m_columns;
        int row = i / m_columns;
        int x = col * pitch;
        int y = row * pitch;

        QColor color = colorForValue(m_blocks[i].value);
        painter.fillRect(x, y, CellSize, CellSize, color);

        // Find seek block
        if (m_seekAddr != RVA_INVALID && m_blocks[i].addr <= m_seekAddr) {
            RVA blockEnd = (i + 1 < m_blocks.size()) ? m_blocks[i + 1].addr : RVA_MAX;
            if (m_seekAddr < blockEnd) {
                seekIndex = i;
            }
        }
    }

    // Draw seek cursor as a bright border
    if (seekIndex >= 0) {
        int col = seekIndex % m_columns;
        int row = seekIndex / m_columns;
        int x = col * pitch;
        int y = row * pitch;

        QPen pen(Qt::white);
        pen.setWidth(2);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(x, y, CellSize - 1, CellSize - 1);
    }
}

void ZoomView::mousePressEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton) {
        int idx = blockIndexAt(event->pos());
        if (idx >= 0 && idx < m_blocks.size()) {
            emit blockClicked(m_blocks[idx].addr);
        }
    }
}

void ZoomView::mouseMoveEvent(QMouseEvent *event)
{
    int idx = blockIndexAt(event->pos());
    if (idx >= 0 && idx < m_blocks.size()) {
        RVA size = 0;
        if (idx + 1 < m_blocks.size()) {
            size = m_blocks[idx + 1].addr - m_blocks[idx].addr;
        }
        QString tip = QString("Address: %1\nSize: %2\nValue: %3")
                          .arg(RAddressString(m_blocks[idx].addr))
                          .arg(RAddressString(size))
                          .arg(m_blocks[idx].value);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        QToolTip::showText(event->globalPosition().toPoint(), tip, this);
#else
        QToolTip::showText(event->globalPos(), tip, this);
#endif
    } else {
        QToolTip::hideText();
    }
}

// ZoomWidget implementation

ZoomWidget::ZoomWidget(MainWindow *main)
    : IaitoDockWidget(main)
{
    setObjectName("ZoomWidget");
    setWindowTitle(tr("Zoom"));

    auto *mainWidget = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(mainWidget);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    // Controls row
    auto *controlsLayout = new QHBoxLayout();
    controlsLayout->setSpacing(4);

    // Mode selector
    controlsLayout->addWidget(new QLabel(tr("Mode:")));
    modeCombo = new QComboBox();
    modeCombo->addItem(tr("Entropy"), "e");
    modeCombo->addItem(tr("Strings"), "z");
    modeCombo->addItem(tr("Printable"), "p");
    modeCombo->addItem(tr("0x00 bytes"), "0");
    modeCombo->addItem(tr("0xFF bytes"), "F");
    modeCombo->addItem(tr("Flags/Marks"), "m");
    modeCombo->addItem(tr("Calls"), "c");
    modeCombo->addItem(tr("Jumps"), "j");
    modeCombo->addItem(tr("Invalid insn"), "i");
    modeCombo->addItem(tr("Syscalls"), "s");
    modeCombo->addItem(tr("Analysis"), "a");
    modeCombo->addItem(tr("Unique bytes"), "d");
    modeCombo->setToolTip(tr("Select what each cell represents (p= mode)"));
    controlsLayout->addWidget(modeCombo);

    // Range selector
    controlsLayout->addWidget(new QLabel(tr("Range:")));
    rangeCombo = new QComboBox();
    rangeCombo->addItem(tr("raw"), "raw");
    rangeCombo->addItem(tr("block"), "block");
    rangeCombo->addItem(tr("bin.section"), "bin.section");
    rangeCombo->addItem(tr("bin.sections"), "bin.sections");
    rangeCombo->addItem(tr("bin.sections.rwx"), "bin.sections.rwx");
    rangeCombo->addItem(tr("bin.sections.r"), "bin.sections.r");
    rangeCombo->addItem(tr("io.map"), "io.map");
    rangeCombo->addItem(tr("io.maps"), "io.maps");
    rangeCombo->addItem(tr("io.maps.rwx"), "io.maps.rwx");
    rangeCombo->addItem(tr("dbg.map"), "dbg.map");
    rangeCombo->addItem(tr("dbg.maps"), "dbg.maps");
    rangeCombo->addItem(tr("anal.fcn"), "anal.fcn");
    rangeCombo->addItem(tr("anal.bb"), "anal.bb");
    rangeCombo->setCurrentIndex(3); // bin.sections
    rangeCombo->setToolTip(tr("Address range to zoom into (zoom.in)"));
    controlsLayout->addWidget(rangeCombo);

    // Columns spinbox
    controlsLayout->addWidget(new QLabel(tr("Cols:")));
    columnsSpinBox = new QSpinBox();
    columnsSpinBox->setRange(4, 512);
    columnsSpinBox->setValue(64);
    columnsSpinBox->setToolTip(tr("Number of cells per row"));
    controlsLayout->addWidget(columnsSpinBox);

    // Blocks spinbox
    controlsLayout->addWidget(new QLabel(tr("Blocks:")));
    blocksSpinBox = new QSpinBox();
    blocksSpinBox->setRange(16, 65536);
    blocksSpinBox->setValue(1024);
    blocksSpinBox->setSingleStep(64);
    blocksSpinBox->setToolTip(tr("Total number of blocks to display (N parameter for p=)"));
    controlsLayout->addWidget(blocksSpinBox);

    // Color mode
    controlsLayout->addWidget(new QLabel(tr("Color:")));
    colorCombo = new QComboBox();
    colorCombo->addItem(tr("Greyscale"), static_cast<int>(ZoomView::ColorMode::Greyscale));
    colorCombo->addItem(tr("Rainbow"), static_cast<int>(ZoomView::ColorMode::Rainbow));
    colorCombo->addItem(tr("ThemeSingle"), static_cast<int>(ZoomView::ColorMode::Theme));
    colorCombo->addItem(tr("ThemePalette"), static_cast<int>(ZoomView::ColorMode::ThemePalette));
    colorCombo->setToolTip(tr("Color mapping mode for cell values"));
    controlsLayout->addWidget(colorCombo);

    // Autowrap checkbox
    autoWrapCheck = new QCheckBox(tr("Autowrap"));
    autoWrapCheck->setToolTip(tr("Automatically set columns to fit the widget width"));
    controlsLayout->addWidget(autoWrapCheck);

    controlsLayout->addStretch();
    mainLayout->addLayout(controlsLayout);

    // Scroll area containing the zoom view
    scrollArea = new QScrollArea();
    zoomView = new ZoomView();
    scrollArea->setWidget(zoomView);
    scrollArea->setWidgetResizable(false);
    scrollArea->setFrameShape(QFrame::NoFrame);
    mainLayout->addWidget(scrollArea);

    setWidget(mainWidget);

    // Connect controls to refresh
    connect(
        modeCombo,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        &ZoomWidget::fetchData);
    connect(
        rangeCombo,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        &ZoomWidget::fetchData);
    connect(columnsSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int cols) {
        zoomView->setColumns(cols);
    });
    connect(blocksSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &ZoomWidget::fetchData);
    connect(colorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        auto mode = static_cast<ZoomView::ColorMode>(colorCombo->currentData().toInt());
        if (mode == ZoomView::ColorMode::ThemePalette) {
            zoomView->rebuildThemePalette();
        }
        zoomView->setColorMode(mode);
    });

    // Connect zoom view clicks to seek
    connect(zoomView, &ZoomView::blockClicked, this, [](RVA addr) { Core()->seek(addr); });

    // Connect core signals
    connect(Core(), &IaitoCore::seekChanged, this, &ZoomWidget::onSeekChanged);
    connect(Core(), &IaitoCore::refreshAll, this, &ZoomWidget::fetchData);
    connect(Config(), &Configuration::colorsUpdated, this, [this]() {
        if (zoomView->colorModeIs(ZoomView::ColorMode::ThemePalette)) {
            zoomView->rebuildThemePalette();
        }
        zoomView->update();
    });

    // Autowrap: disable columns spinbox and recalculate on toggle
    connect(autoWrapCheck, &QCheckBox::toggled, this, [this](bool checked) {
        columnsSpinBox->setEnabled(!checked);
        if (checked) {
            updateAutoWrapColumns();
        }
    });

    // Watch the scroll area viewport for resize events
    scrollArea->viewport()->installEventFilter(this);

    // Initial column setting
    zoomView->setColumns(columnsSpinBox->value());
}

ZoomWidget::~ZoomWidget() {}

void ZoomWidget::fetchData()
{
    QString mode = modeCombo->currentData().toString();
    QString range = rangeCombo->currentData().toString();
    int nblocks = blocksSpinBox->value();

    TempConfig tc;
    tc.set("zoom.in", range);

    QString cmd = QString("p=%1j %2").arg(mode).arg(nblocks);
    QJsonDocument doc = Core()->cmdj(cmd.toUtf8().constData());
    QJsonObject root = doc.object();

    QVector<ZoomView::BlockEntry> blocks;

    // Find the data array in the JSON object (key name varies by mode)
    for (const QString &key : root.keys()) {
        QJsonValue val = root[key];
        if (val.isArray()) {
            QJsonArray arr = val.toArray();
            blocks.reserve(arr.size());
            for (const QJsonValue &entry : arr) {
                QJsonObject obj = entry.toObject();
                ZoomView::BlockEntry block;
                block.addr = obj["addr"].toVariant().toULongLong();
                block.value = obj["value"].toInt();
                blocks.append(block);
            }
            break;
        }
    }

    zoomView->setData(blocks);
    zoomView->setSeekAddress(Core()->getOffset());
}

void ZoomWidget::onSeekChanged(RVA addr)
{
    zoomView->setSeekAddress(addr);
}

bool ZoomWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == scrollArea->viewport() && event->type() == QEvent::Resize) {
        if (autoWrapCheck->isChecked()) {
            updateAutoWrapColumns();
        }
    }
    return IaitoDockWidget::eventFilter(obj, event);
}

void ZoomWidget::updateAutoWrapColumns()
{
    int viewportWidth = scrollArea->viewport()->width();
    int pitch = zoomView->cellPitch();
    int cols = qMax(1, viewportWidth / pitch);
    columnsSpinBox->setValue(cols);
    zoomView->setColumns(cols);
}
