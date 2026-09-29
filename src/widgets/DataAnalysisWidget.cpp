#include "DataAnalysisWidget.h"
#include "common/IaitoSeekable.h"
#include "core/MainWindow.h"
#include "dialogs/CommentsDialog.h"
#include "dialogs/FlagDialog.h"
#include "ui_DataAnalysisWidget.h"

#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSet>
#include <QTextStream>
#include <QToolButton>

#include <algorithm>

using namespace DataAnalysis;

namespace {
RVA number(const QJsonObject &object, const char *key)
{
    return object[QLatin1String(key)].toVariant().toULongLong();
}

QByteArray readData(RVA address, int length)
{
    auto core = Core()->core();
    QByteArray bytes(length, '\0');
    int count = 0;
    while (count < length) {
        // nread distinguishes short/failed reads from actual 0xff bytes. A read
        // can end at a submap boundary even when the following map is readable.
        const int n = r_io_nread_at(
            core->io,
            address + count,
            reinterpret_cast<ut8 *>(bytes.data()) + count,
            length - count);
        if (n <= 0) {
            break;
        }
        count += n;
    }
    bytes.resize(count);
    return bytes;
}
} // namespace

DataAnalysisWidget::DataAnalysisWidget(MainWindow *main)
    : AddressableDockWidget(main)
    , ui(new Ui::DataAnalysisWidget)
{
    setObjectName("DataAnalysisWidget");
    updateWindowTitle();
    auto *contents = new QWidget(this);
    ui->setupUi(contents);
    ui->legend->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    setWidget(contents);
    plot = new DataAnalysisView(false, contents);
    plot->setObjectName("analysisPlot");
    overview = new DataAnalysisView(true, contents);
    overview->setObjectName("analysisOverview");
    overview->setFixedHeight(80);
    auto *scroll = new QScrollArea(contents);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(plot);
    ui->plotLayout->addWidget(scroll, 1);
    ui->plotLayout->addWidget(overview);
    ui->verticalLayout->setStretch(3, 1);
    setupActions();
    updateColors();

    refreshDeferrer = createRefreshDeferrer([this]() { reloadTimer.start(); });
    reloadTimer.setSingleShot(true);
    reloadTimer.setInterval(100);
    rangeTimer.setSingleShot(true);
    rangeTimer.setInterval(80);
    sampleTimer.setInterval(0);
    connect(&reloadTimer, &QTimer::timeout, this, &DataAnalysisWidget::reload);
    connect(&rangeTimer, &QTimer::timeout, this, [this]() {
        if (!samplingOverview) {
            beginSampling(false);
        }
    });
    connect(&sampleTimer, &QTimer::timeout, this, &DataAnalysisWidget::sampleBatch);
    connect(plot, &DataAnalysisView::seekRequested, seekable, &IaitoSeekable::seek);
    connect(plot, &DataAnalysisView::selectionRequested, this, [this](RVA start, RVA end) {
        selectRange({start, end}, true);
    });
    for (auto *view : {plot, overview}) {
        connect(view, &DataAnalysisView::rangeRequested, this, [this](RVA start, RVA end) {
            setRange({start, end});
        });
        connect(view, &DataAnalysisView::contextRequested, this, &DataAnalysisWidget::showContextMenu);
    }
    connect(seekable, &IaitoSeekable::seekableSeekChanged, this, &DataAnalysisWidget::onSeekChanged);
    connect(Core(), &IaitoCore::addressRangeSelectionChanged, this, [this](RVA start, RVA end) {
        if (!publishingSelection && seekable->isSynchronized()
            && Config()->getAddressRangeSelectionSyncEnabled()) {
            selectRange(
                start != RVA_INVALID && end != RVA_INVALID && end >= start ? Range{start, end + 1}
                                                                           : Range{},
                false);
        }
    });
    connect(Core(), &IaitoCore::refreshAll, this, &DataAnalysisWidget::scheduleReload);
    connect(Core(), &IaitoCore::codeRebased, this, &DataAnalysisWidget::scheduleReload);
    connect(Core(), &IaitoCore::ioModeChanged, this, &DataAnalysisWidget::scheduleReload);
    connect(Core(), &IaitoCore::ioCacheChanged, this, &DataAnalysisWidget::scheduleReload);
    connect(Core(), &IaitoCore::flagsChanged, this, &DataAnalysisWidget::scheduleReload);
    connect(Core(), &IaitoCore::commentsChanged, this, &DataAnalysisWidget::scheduleReload);
    connect(Core(), &IaitoCore::refreshCodeViews, this, &DataAnalysisWidget::scheduleReload);
    connect(Core(), &IaitoCore::instructionChanged, this, &DataAnalysisWidget::scheduleReload);
    connect(Core(), &IaitoCore::debugTaskStateChanged, this, &DataAnalysisWidget::scheduleReload);
    connect(Config(), &Configuration::colorsUpdated, this, &DataAnalysisWidget::updateColors);
    connect(this, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (!visible) {
            metadataDirty |= sampleTimer.isActive() || rangeTimer.isActive();
            sampleTimer.stop();
            rangeTimer.stop();
            reloadTimer.stop();
        } else if (metadataDirty) {
            scheduleReload();
        }
    });
    scheduleReload();
}

DataAnalysisWidget::~DataAnalysisWidget() = default;

QWidget *DataAnalysisWidget::widgetToFocusOnRaise()
{
    return plot;
}

void DataAnalysisWidget::setupActions()
{
    auto menuButton = [this](const QString &label, QMenu *menu) {
        auto *button = new QToolButton(ui->toolbar);
        button->setText(label);
        button->setPopupMode(QToolButton::InstantPopup);
        button->setMenu(menu);
        ui->toolbar->addWidget(button);
    };
    auto *ranges = new QMenu(this);
    ranges->addAction(tr("All data"), this, [this]() { setRange(plot->domain); });
    ranges->addAction(tr("Current section"), this, [this]() {
        fitMarker(DataAnalysisView::Section);
    });
    ranges->addAction(tr("Current segment"), this, [this]() {
        fitMarker(DataAnalysisView::Segment);
    });
    ranges->addAction(tr("Current map"), this, [this]() { fitMarker(DataAnalysisView::Map); });
    ranges->addAction(tr("Current block"), this, [this]() {
        const int size = Core()->core()->blocksize;
        setRange(sizedRange(seekable->getOffset(), qMax(1, size)));
    });
    selectionAction
        = ranges->addAction(tr("Fit selection"), this, &DataAnalysisWidget::fitSelection);
    selectionAction->setEnabled(false);
    ranges->addSeparator();
    ranges->addAction(tr("Edit range…"), this, &DataAnalysisWidget::editRange);
    menuButton(tr("Range"), ranges);
    auto *algorithms = new QMenu(this);
    for (int i = 0; i < Count; ++i) {
        auto *action = algorithms->addAction(DataAnalysisView::metricName(i));
        action->setCheckable(true);
        action->setChecked(plot->metrics.contains(i));
        metricActions[i] = action;
        action->setToolTip(DataAnalysisView::metricDescription(i));
        auto *entry = ui->legend->addAction(DataAnalysisView::metricName(i));
        entry->setToolTip(
            DataAnalysisView::metricDescription(i) + tr("\nClick to configure this algorithm."));
        legendActions[i] = entry;
        connect(entry, &QAction::triggered, this, [this, i]() { configureMetric(i); });
        connect(action, &QAction::toggled, this, &DataAnalysisWidget::updateMetrics);
    }
    menuButton(tr("Algorithms"), algorithms);
    ui->toolbar
        ->addAction(
            tr("+"),
            this,
            [this]() { plot->zoom(0.5, plot->visibleRange.start + plot->visibleRange.size() / 2); })
        ->setToolTip(tr("Zoom in (+)"));
    ui->toolbar
        ->addAction(
            tr("−"),
            this,
            [this]() { plot->zoom(2, plot->visibleRange.start + plot->visibleRange.size() / 2); })
        ->setToolTip(tr("Zoom out (-)"));
    ui->toolbar->addAction(tr("Fit"), this, [this]() { setRange(plot->domain); })
        ->setToolTip(tr("Fit all data (Home)"));
    followAction = ui->toolbar->addAction(tr("Follow"));
    followAction->setCheckable(true);
    followAction->setChecked(true);
    followAction->setToolTip(tr("Pan to keep the current disassembly or hexdump address visible"));
    connect(followAction, &QAction::toggled, this, [this](bool checked) {
        if (checked) {
            onSeekChanged(seekable->getOffset());
        }
    });

    optionsMenu = new QMenu(this);
    tracksAction = optionsMenu->addAction(tr("Separate algorithm tracks"));
    tracksAction->setCheckable(true);
    connect(tracksAction, &QAction::toggled, this, [this](bool checked) {
        plot->separateTracks = checked;
        plot->setMinimumHeight(checked ? qMax(180, int(plot->metrics.size()) * 90) : 180);
        plot->update();
    });
    auto *styles = optionsMenu->addMenu(tr("Drawing style"));
    auto *styleGroup = new QActionGroup(styles);
    const QStringList styleNames = {tr("Lines"), tr("Filled lines"), tr("Vertical bars")};
    for (int i = 0; i < styleNames.size(); ++i) {
        auto *action = styles->addAction(styleNames[i]);
        styleGroup->addAction(action);
        action->setCheckable(true);
        action->setData(i);
        action->setChecked(i == plot->style);
        connect(action, &QAction::triggered, this, [this, i]() {
            plot->style = static_cast<DataAnalysisView::Style>(i);
            plot->update();
        });
    }
    styles->setObjectName("analysisStyles");
    auto *colors = optionsMenu->addMenu(tr("Colors"));
    auto *colorGroup = new QActionGroup(colors);
    const QStringList modes
        = {tr("Greyscale"), tr("Rainbow"), tr("Theme single color"), tr("Theme palette")};
    for (int i = 0; i < modes.size(); ++i) {
        auto *action = colors->addAction(modes[i]);
        colorGroup->addAction(action);
        action->setCheckable(true);
        action->setData(i);
        action->setChecked(i == colorMode);
        connect(action, &QAction::triggered, this, [this, i]() {
            colorMode = i;
            updateColors();
        });
    }
    colors->setObjectName("analysisColors");
    colors->addSeparator();
    auto *custom = colors->addMenu(tr("Customize algorithm colors"));
    for (int i = 0; i < Count; ++i) {
        custom->addAction(DataAnalysisView::metricName(i), this, [this, i]() {
            const QColor color
                = QColorDialog::getColor(plot->colors[i], this, DataAnalysisView::metricName(i));
            if (color.isValid()) {
                customColors[i] = color;
                updateColors();
            }
        });
    }
    custom->addSeparator();
    custom->addAction(tr("Reset custom colors"), this, [this]() {
        customColors.fill(QColor());
        updateColors();
    });
    auto *markers = optionsMenu->addMenu(tr("Markers"));
    markers->setObjectName("analysisMarkers");
    for (int i = 0; i < DataAnalysisView::MarkerCount; ++i) {
        auto *action = markers->addAction(
            DataAnalysisView::markerName(static_cast<DataAnalysisView::MarkerType>(i)));
        action->setCheckable(true);
        action->setData(i);
        action->setChecked(plot->markerEnabled[i]);
        connect(action, &QAction::toggled, this, [this, i](bool checked) {
            plot->markerEnabled[i] = overview->markerEnabled[i] = checked;
            plot->update();
            overview->update();
        });
    }
    auto *samples = optionsMenu->addMenu(tr("Resolution (buckets)"));
    samples->setObjectName("analysisResolution");
    auto *sampleGroup = new QActionGroup(samples);
    for (int n : {32, 64, 128, 256, 512, 1024, 2048, 4096}) {
        auto *action = samples->addAction(QString::number(n));
        sampleGroup->addAction(action);
        action->setCheckable(true);
        action->setData(n);
        action->setChecked(n == resolution);
        connect(action, &QAction::triggered, this, [this, n]() {
            resolution = n;
            beginSampling(samplingOverview);
        });
    }
    optionsMenu->addSeparator();
    optionsMenu->addAction(&syncAction);
    optionsMenu->addAction(tr("Find marker…"), this, &DataAnalysisWidget::findMarker);
    optionsMenu->addAction(tr("Refresh"), this, &DataAnalysisWidget::scheduleReload);
    exportAction = optionsMenu->addAction(
        tr("Export visible samples as CSV…"), this, &DataAnalysisWidget::exportCsv);
    exportAction->setEnabled(false);
    menuButton(tr("Options"), optionsMenu);
    ui->legend->setToolTip(
        tr("Click a legend entry to configure its color and visibility."
           " Overlaid values use a 0–100% scale; entropy is divided by 8."
           " Separate tracks use native units."));
    updateMetrics();
}

void DataAnalysisWidget::updateMetrics()
{
    plot->metrics.clear();
    for (int i = 0; i < Count; ++i) {
        legendActions[i]->setVisible(metricActions[i]->isChecked());
        if (metricActions[i]->isChecked()) {
            plot->metrics.append(i);
        }
    }
    overview->metrics = plot->metrics;
    plot->setMinimumHeight(plot->separateTracks ? qMax(180, int(plot->metrics.size()) * 90) : 180);
    plot->update();
}

void DataAnalysisWidget::updateColors()
{
    const char *keys[]
        = {"gui.navbar.code",
           "gui.navbar.str",
           "gui.navbar.data",
           "num",
           "flag",
           "comment",
           "flow",
           "func_var",
           "reg",
           "invalid",
           "other"};
    QColor base = Config()->getColor("gui.navbar.code");
    if (!base.isValid()) {
        base = QColor(120, 150, 230);
    }
    QSet<QRgb> used;
    for (int i = 0; i < Count; ++i) {
        QColor color;
        switch (colorMode) {
        case 0:
            color = QColor::fromHsv(0, 0, 85 + i * 16);
            break;
        case 1:
            color = QColor::fromHsv((270 + i * 137) % 360, 165, 220);
            break;
        case 2:
            color = QColor::fromHsv(qMax(0, base.hue()), 70 + i * 16, 235 - i * 9);
            break;
        default:
            color = Config()->getColor(keys[i]);
            if (!color.isValid() || used.contains(color.rgb())
                || color == palette().color(QPalette::Base)) {
                color = QColor::fromHsv((270 + i * 137) % 360, 155, 210);
            }
            break;
        }
        if (customColors[i].isValid()) {
            color = customColors[i];
        }
        used.insert(color.rgb());
        plot->colors[i] = overview->colors[i] = color;
        QPixmap swatch(12, 12);
        swatch.fill(color);
        metricActions[i]->setIcon(QIcon(swatch));
        legendActions[i]->setIcon(QIcon(swatch));
    }
    plot->update();
    overview->update();
}

void DataAnalysisWidget::configureMetric(int metric)
{
    QDialog dialog(this);
    dialog.setObjectName("analysisMetricSettings");
    dialog.setWindowTitle(tr("%1 settings").arg(DataAnalysisView::metricName(metric)));
    QFormLayout layout(&dialog);
    QLabel description(DataAnalysisView::metricDescription(metric));
    description.setWordWrap(true);
    description.setMaximumWidth(420);
    QCheckBox visible(tr("Show this algorithm"));
    visible.setObjectName("metricVisible");
    visible.setChecked(metricActions[metric]->isChecked());
    QColor chosen = customColors[metric];
    QPushButton colorButton(tr("Change color…"));
    colorButton.setIcon(legendActions[metric]->icon());
    QPushButton resetButton(tr("Use palette color"));
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout.addRow(&description);
    layout.addRow(tr("Color:"), &colorButton);
    layout.addRow(&resetButton);
    layout.addRow(&visible);
    layout.addRow(&buttons);
    connect(&colorButton, &QPushButton::clicked, &dialog, [&]() {
        const QColor color = QColorDialog::getColor(
            chosen.isValid() ? chosen : plot->colors[metric],
            &dialog,
            DataAnalysisView::metricName(metric));
        if (color.isValid()) {
            chosen = color;
            QPixmap swatch(12, 12);
            swatch.fill(color);
            colorButton.setIcon(QIcon(swatch));
            colorButton.setText(color.name());
        }
    });
    connect(&resetButton, &QPushButton::clicked, &dialog, [&]() {
        chosen = QColor();
        colorButton.setIcon(QIcon());
        colorButton.setText(tr("Palette default"));
    });
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted) {
        customColors[metric] = chosen;
        metricActions[metric]->setChecked(visible.isChecked());
        updateColors();
    }
}

void DataAnalysisWidget::scheduleReload()
{
    metadataDirty = true;
    if (refreshDeferrer->attemptRefresh(nullptr)) {
        sampleTimer.stop();
        reloadTimer.start();
    }
}

void DataAnalysisWidget::loadMetadata()
{
    using View = DataAnalysisView;
    virtualAddresses = Core()->getConfigb("io.va");
    readable.clear();
    plot->markers.clear();
    auto addMarker = [this](Range range, const QString &name, View::MarkerType type) {
        if (range.valid()) {
            plot->markers.append({range, name, type});
        }
    };
    for (const auto &value : Core()->cmdj("omj").array()) {
        const QJsonObject map = value.toObject();
        const RVA start = number(map, "from"), last = number(map, "to");
        const Range range = last >= start ? Range{start, last == RVA_MAX ? RVA_MAX : last + 1}
                                          : Range{};
        if (virtualAddresses) {
            addMarker(range, map["name"].toString(), View::Map);
            if (range.valid() && map["perm"].toString().contains('r')) {
                readable.append(range);
            }
        }
    }
    if (!virtualAddresses) {
        auto core = Core()->core();
        const RVA size = r_io_size(core->io);
        if (size != RVA_MAX && size) {
            readable.append({0, size});
        }
    }
    readable = mergeRanges(readable);
    const Range domain = readable.isEmpty() ? Range{}
                                            : Range{readable.first().start, readable.last().end};
    const bool changed = domain != plot->domain;
    plot->domain = overview->domain = domain;
    if (changed || !plot->visibleRange.valid()) {
        plot->visibleRange = overview->visibleRange = domain;
    }
    for (const auto &entry :
         {qMakePair(QString("iSj"), View::Section), qMakePair(QString("iSSj"), View::Segment)}) {
        const QJsonDocument document = Core()->cmdj(entry.first);
        const QJsonArray objects
            = document.isArray()
                  ? document.array()
                  : document.object()[entry.second == View::Section ? "sections" : "segments"]
                        .toArray();
        for (const auto &value : objects) {
            const QJsonObject object = value.toObject();
            const RVA start = number(object, virtualAddresses ? "vaddr" : "paddr");
            const RVA size = number(object, virtualAddresses ? "vsize" : "size");
            addMarker(sizedRange(start, size), object["name"].toString(), entry.second);
        }
    }
    // @F temporarily selects all flag spaces without changing the user's space.
    for (const auto &value : Core()->cmdj("fj @F:*").array()) {
        const QJsonObject flag = value.toObject();
        const RVA address = number(flag, flag.contains("offset") ? "offset" : "addr");
        addMarker(
            sizedRange(address, qMax<RVA>(1, number(flag, "size"))),
            flag["name"].toString(),
            View::Flag);
    }
    for (const auto &comment : Core()->getAllComments("CCu")) {
        addMarker(sizedRange(comment.offset, 1), comment.name, View::Comment);
    }
    std::stable_sort(
        plot->markers.begin(),
        plot->markers.end(),
        [](const View::Marker &a, const View::Marker &b) { return a.range.start < b.range.start; });
    overview->markers = plot->markers;
    metadataDirty = false;
}

void DataAnalysisWidget::reload()
{
    if (!refreshDeferrer->attemptRefresh(nullptr) || Core()->isDebugTaskInProgress()) {
        return;
    }
    sampleTimer.stop();
    rangeTimer.stop();
    loadMetadata();
    plot->seekAddress = overview->seekAddress = seekable->getOffset();
    if (requestedRange.valid()) {
        const Range requested = intersect(requestedRange, plot->domain);
        if (requested.valid()) {
            plot->visibleRange = overview->visibleRange = requested;
        }
        requestedRange = {};
    }
    if (seekable->isSynchronized() && Config()->getAddressRangeSelectionSyncEnabled()
        && Core()->hasAddressRangeSelection()) {
        selectRange(
            {Core()->getAddressRangeSelectionStart(), Core()->getAddressRangeSelectionEnd() + 1},
            false);
    }
    plot->samples.clear();
    overview->samples.clear();
    beginSampling(true);
}

void DataAnalysisWidget::beginSampling(bool mini)
{
    rangeTimer.stop();
    sampleTimer.stop();
    if (!isVisibleToUser()) {
        metadataDirty = true;
        refreshDeferrer->attemptRefresh(nullptr);
        return;
    }
    samplingOverview = mini;
    pendingRange = mini ? plot->domain : plot->visibleRange;
    pending.clear();
    nextBucket = 0;
    plot->loading = pendingRange.valid();
    exportAction->setEnabled(false);
    if (pendingRange.valid()) {
        const int count = int(qMin<RVA>(mini ? 256 : resolution, pendingRange.size()));
        pending.resize(count);
        sampleTimer.start();
    }
    updateLabels();
    plot->update();
    overview->update();
}

void DataAnalysisWidget::sampleBatch()
{
    if (!isVisibleToUser() || Core()->isDebugTaskInProgress()) {
        sampleTimer.stop();
        metadataDirty = true;
        refreshDeferrer->attemptRefresh(nullptr);
        return;
    }
    QElapsedTimer timer;
    timer.start();
    // Yield to navigation and painting every few milliseconds. A range change
    // cancels the old job; no stale worker result can overwrite the new range.
    do {
        pending[nextBucket]
            = sample(bucket(pendingRange, nextBucket, pending.size()), readable, readData);
        ++nextBucket;
    } while (nextBucket < pending.size() && timer.elapsed() < 6);
    if (nextBucket < pending.size()) {
        ui->statusLabel->setText(tr("Analyzing %1… %2%")
                                     .arg(samplingOverview ? tr("overview") : tr("visible range"))
                                     .arg(nextBucket * 100 / pending.size()));
        return;
    }
    sampleTimer.stop();
    if (samplingOverview) {
        overview->samples = pending;
        overview->update();
        beginSampling(false);
    } else {
        plot->samples = pending;
        plot->loading = false;
        exportAction->setEnabled(!plot->samples.isEmpty());
        updateLabels();
        plot->update();
    }
}

void DataAnalysisWidget::setRange(Range range)
{
    range = intersect(range, plot->domain);
    if (!range.valid() || range == plot->visibleRange) {
        return;
    }
    plot->visibleRange = overview->visibleRange = range;
    plot->samples.clear();
    plot->loading = true;
    exportAction->setEnabled(false);
    if (!samplingOverview) {
        sampleTimer.stop();
    }
    rangeTimer.start();
    updateLabels();
    plot->update();
    overview->update();
}

void DataAnalysisWidget::showRange(RVA start, RVA end)
{
    if (start == RVA_INVALID || end == RVA_INVALID || end < start) {
        return;
    }
    requestedRange = {start, end + 1};
    raiseMemoryWidget();
    seekable->seek(start);
    if (metadataDirty) {
        scheduleReload();
    } else {
        setRange(requestedRange);
        requestedRange = {};
    }
}

void DataAnalysisWidget::onSeekChanged(RVA address)
{
    plot->seekAddress = overview->seekAddress = address;
    if (followAction->isChecked() && plot->domain.contains(address)
        && !plot->visibleRange.contains(address)) {
        setRange(centered(plot->domain, address, plot->visibleRange.size()));
    }
    updateLabels();
    plot->update();
    overview->update();
}

void DataAnalysisWidget::selectRange(Range range, bool publish)
{
    plot->selection = overview->selection = range;
    selectionAction->setEnabled(range.valid());
    if (publish && seekable->isSynchronized() && Config()->getAddressRangeSelectionSyncEnabled()) {
        publishingSelection = true;
        if (range.valid()) {
            Core()->setAddressRangeSelection(range.start, range.end - 1);
        } else {
            Core()->clearAddressRangeSelection();
        }
        publishingSelection = false;
    }
    updateLabels();
    plot->update();
    overview->update();
}

void DataAnalysisWidget::updateLabels()
{
    const Range range = plot->visibleRange;
    if (!range.valid()) {
        ui->rangeLabel->setText(tr("No readable data in the current address space"));
        ui->statusLabel->setText(tr("Open a file or refresh after mapping data."));
        return;
    }
    QString text = tr("%1: %2 – %3 · %4 bytes · Cursor %5")
                       .arg(
                           virtualAddresses ? tr("Addresses") : tr("File offsets"),
                           RAddressString(range.start),
                           RAddressString(range.end - 1))
                       .arg(range.size())
                       .arg(RAddressString(seekable->getOffset()));
    if (plot->selection.valid()) {
        text += tr("\nSelection: %1 – %2 · %3 bytes")
                    .arg(
                        RAddressString(plot->selection.start),
                        RAddressString(plot->selection.end - 1))
                    .arg(plot->selection.size());
    }
    ui->rangeLabel->setText(text);
    if (plot->loading) {
        ui->statusLabel->setText(tr("Analyzing…"));
    } else {
        RVA bytes = 0, covered = 0;
        for (const auto &sample : plot->samples) {
            bytes += sample.bytes;
            covered += sample.covered;
        }
        if (!bytes) {
            ui->statusLabel->setText(tr("No readable bytes in the visible range."));
            return;
        }
        ui->statusLabel->setText(
            tr("%1 buckets · %2 / %3 mapped bytes read (%4) · Overview: entropy")
                .arg(plot->samples.size())
                .arg(bytes)
                .arg(covered)
                .arg(bytes == covered ? tr("complete") : tr("sampled or unreadable")));
        ui->statusLabel->setToolTip(
            tr("Each bucket reads up to 4096 bytes from centered windows in readable spans."
               " Zoom in for complete coverage. Unmapped bytes are excluded."
               " Shannon entropy: 0–8 bits/byte. Overlays use normalized percentages."));
    }
}

void DataAnalysisWidget::fitSelection()
{
    if (plot->selection.valid()) {
        setRange(plot->selection);
    }
}

void DataAnalysisWidget::fitMarker(DataAnalysisView::MarkerType type)
{
    Range best;
    for (const auto &marker : plot->markers) {
        if (marker.type == type && marker.range.contains(seekable->getOffset())
            && (!best.valid() || marker.range.size() < best.size())) {
            best = marker.range;
        }
    }
    if (best.valid()) {
        setRange(best);
    } else {
        ui->statusLabel->setText(
            tr("No %1 contains the current address.").arg(DataAnalysisView::markerName(type)));
    }
}

void DataAnalysisWidget::editRange()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Data analysis range"));
    QFormLayout layout(&dialog);
    QLineEdit start(RAddressString(plot->visibleRange.start));
    QLineEdit end(RAddressString(plot->visibleRange.valid() ? plot->visibleRange.end - 1 : 0));
    QLabel hint(tr("Enter addresses or radare2 expressions. Both endpoints are inclusive."));
    hint.setWordWrap(true);
    QLabel error;
    error.setWordWrap(true);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout.addRow(&hint);
    layout.addRow(tr("Start:"), &start);
    layout.addRow(tr("End (inclusive):"), &end);
    layout.addRow(&error);
    layout.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        RVA first, last;
        if (!Core()->tryMath(start.text(), first) || !Core()->tryMath(end.text(), last)
            || first == RVA_INVALID || last == RVA_INVALID || first > last) {
            error.setText(tr("Enter a valid start and an end greater than or equal to it."));
            return;
        }
        const Range requested{first, last + 1};
        if (intersect(requested, plot->domain) != requested) {
            error.setText(
                tr("The range must be within %1 – %2.")
                    .arg(RAddressString(plot->domain.start), RAddressString(plot->domain.end - 1)));
            return;
        }
        setRange(requested);
        dialog.accept();
    });
    dialog.exec();
}

void DataAnalysisWidget::findMarker()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Find section, map, flag or comment"));
    dialog.resize(620, 400);
    QVBoxLayout layout(&dialog);
    QLineEdit filter;
    filter.setPlaceholderText(tr("Filter by name, comment, type or hexadecimal address…"));
    QListWidget list;
    QLabel hint(
        tr("Activate a result to seek. Section, segment and map results also fit the range."
           " Showing up to 500 matches."));
    hint.setWordWrap(true);
    QDialogButtonBox buttons(QDialogButtonBox::Close);
    layout.addWidget(&filter);
    layout.addWidget(&list);
    layout.addWidget(&hint);
    layout.addWidget(&buttons);
    // Keep result indices stable while the modal loop receives metadata updates.
    const auto markers = plot->markers;
    auto fill = [&]() {
        list.clear();
        for (int i = 0; i < markers.size(); ++i) {
            const auto &marker = markers[i];
            const QString label = QString("%1 · %2 · %3")
                                      .arg(
                                          RAddressString(marker.range.start),
                                          DataAnalysisView::markerName(marker.type),
                                          marker.name);
            if (label.contains(filter.text(), Qt::CaseInsensitive)) {
                auto *item = new QListWidgetItem(label, &list);
                item->setData(Qt::UserRole, i);
                if (list.count() >= 500) {
                    break;
                }
            }
        }
    };
    connect(&filter, &QLineEdit::textChanged, &dialog, fill);
    connect(&list, &QListWidget::itemActivated, &dialog, [&](QListWidgetItem *item) {
        const int index = item->data(Qt::UserRole).toInt();
        if (index < 0 || index >= markers.size()) {
            return;
        }
        const auto marker = markers[index];
        seekable->seek(marker.range.start);
        if (marker.type <= DataAnalysisView::Map) {
            setRange(marker.range);
        } else if (!plot->visibleRange.contains(marker.range.start)) {
            setRange(centered(plot->domain, marker.range.start, plot->visibleRange.size()));
        }
        dialog.accept();
    });
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    fill();
    dialog.exec();
}

void DataAnalysisWidget::showContextMenu(RVA address, QPoint position)
{
    QMenu menu(this);
    menu.addAction(RAddressString(address))->setEnabled(false);
    menu.addAction(tr("Seek here"), this, [this, address]() { seekable->seek(address); });
    if (mainWindow) {
        auto *showIn = mainWindow->createShowInMenu(&menu, address);
        showIn->setTitle(tr("Show in"));
        menu.addMenu(showIn);
    }
    menu.addAction(tr("Copy address"), this, [address]() {
        QApplication::clipboard()->setText(RAddressString(address));
    });
    menu.addAction(tr("Add or edit comment…"), this, [this, address]() {
        CommentsDialog::addOrEditComment(address, this);
    });
    const Range selection = plot->selection;
    const RVA flagSize = selection.contains(address) ? selection.end - address : 1;
    menu.addAction(tr("Add or edit flag…"), this, [this, address, flagSize]() {
        FlagDialog dialog(address, flagSize, this);
        dialog.exec();
    });
    menu.addSeparator();
    menu.addAction(selectionAction);
    menu.addAction(tr("Clear selection"), this, [this]() { selectRange({}, true); });
    menu.addAction(tr("Edit range…"), this, &DataAnalysisWidget::editRange);
    menu.addAction(tr("Find marker…"), this, &DataAnalysisWidget::findMarker);
    menu.addAction(tr("Fit all data"), this, [this]() { setRange(plot->domain); });
    menu.addSeparator();
    menu.addAction(&syncAction);
    menu.exec(position);
}

void DataAnalysisWidget::exportCsv()
{
    const auto samples = plot->samples;
    const auto metrics = plot->metrics;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export data analysis"), QString(), tr("CSV files (*.csv)"));
    if (path.isEmpty()) {
        return;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Export failed"), file.errorString());
        return;
    }
    QTextStream out(&file);
    out << "start,end_exclusive,bytes_read,mapped_bytes";
    for (int metric : metrics) {
        QString name = DataAnalysisView::metricName(metric);
        name.replace('"', "\"\"");
        out << ",\"" << name << " (normalized 0-1)\"";
    }
    out << '\n';
    for (const auto &sample : samples) {
        out << RAddressString(sample.range.start) << ',' << RAddressString(sample.range.end) << ','
            << sample.bytes << ',' << sample.covered;
        for (int metric : metrics) {
            out << ',';
            if (sample.bytes) {
                out << QString::number(sample.values[metric], 'g', 12);
            }
        }
        out << '\n';
    }
    out.flush();
    if (!file.commit()) {
        QMessageBox::warning(this, tr("Export failed"), file.errorString());
    }
}

QVariantMap DataAnalysisWidget::serializeViewProprties()
{
    auto properties = AddressableDockWidget::serializeViewProprties();
    QVariantList metrics, colors, markers;
    for (int i = 0; i < Count; ++i) {
        if (metricActions[i]->isChecked()) {
            metrics.append(i);
        }
        colors.append(customColors[i].isValid() ? customColors[i].name() : QString());
    }
    for (bool enabled : plot->markerEnabled) {
        markers.append(enabled);
    }
    properties["metrics"] = metrics;
    properties["colors"] = colors;
    properties["markers"] = markers;
    properties["style"] = int(plot->style);
    properties["colorMode"] = colorMode;
    properties["resolution"] = resolution;
    properties["tracks"] = tracksAction->isChecked();
    properties["follow"] = followAction->isChecked();
    return properties;
}

void DataAnalysisWidget::deserializeViewProperties(const QVariantMap &properties)
{
    AddressableDockWidget::deserializeViewProperties(properties);
    const QVariantList metrics
        = properties.value("metrics", QVariantList{Entropy, Printable, Zero}).toList();
    const QVariantList colors = properties.value("colors").toList();
    for (int i = 0; i < Count; ++i) {
        metricActions[i]->setChecked(metrics.contains(i));
        customColors[i] = i < colors.size() ? QColor(colors[i].toString()) : QColor();
    }
    auto restoreMenu = [this](const char *name, int selected) {
        for (auto *action : optionsMenu->findChild<QMenu *>(name)->actions()) {
            if (action->isCheckable() && action->data().toInt() == selected) {
                action->trigger();
            }
        }
    };
    restoreMenu("analysisStyles", qBound(0, properties.value("style", 1).toInt(), 2));
    restoreMenu("analysisColors", qBound(0, properties.value("colorMode", 3).toInt(), 3));
    int count = properties.value("resolution", 1024).toInt();
    if (!QList<int>{32, 64, 128, 256, 512, 1024, 2048, 4096}.contains(count)) {
        count = 1024;
    }
    restoreMenu("analysisResolution", count);
    const QVariantList markers
        = properties.value("markers", QVariantList{true, false, false, true, true}).toList();
    for (auto *action : optionsMenu->findChild<QMenu *>("analysisMarkers")->actions()) {
        const int index = action->data().toInt();
        action->setChecked(index < markers.size() && markers[index].toBool());
    }
    tracksAction->setChecked(properties.value("tracks", false).toBool());
    followAction->setChecked(properties.value("follow", true).toBool());
    updateColors();
}
