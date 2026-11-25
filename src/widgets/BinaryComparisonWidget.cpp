#include "BinaryComparisonWidget.h"

#if QT_CONFIG(process)
#include "common/Configuration.h"
#include "common/Decompiler.h"
#include "common/FilePaths.h"
#include "common/Json.h"
#include "common/theme/TouchScrollController.h"
#include "core/MainWindow.h"
#include "ui_BinaryComparisonWidget.h"
#include "widgets/SimpleTextGraphView.h"

#include <QAbstractTableModel>
#include <QDateTime>
#include <QFileDialog>
#include <QHeaderView>
#include <QJsonArray>
#include <QMessageBox>
#include <QSaveFile>
#include <QSettings>
#include <QWheelEvent>

#include <algorithm>
#include <r_util/r_diff.h>

namespace {
constexpr int DataRole = Qt::UserRole;
constexpr int SortRole = Qt::UserRole + 1;
constexpr RVA DiskPageSize = 4096;
constexpr RVA DiskRowSize = 16;
constexpr int PatchPreviewLimit = 256 * 1024;

RVA address(const QJsonObject &obj, const char *key)
{
    return JsonValueGetRVA(obj.value(QLatin1String(key)));
}

QString displayAddress(RVA addr)
{
    return addr == RVA_INVALID ? QString() : RAddressString(addr);
}

QColor tint(const QColor &base, const QColor &accent)
{
    return QColor::fromRgbF(
        0.72 * base.redF() + 0.28 * accent.redF(),
        0.72 * base.greenF() + 0.28 * accent.greenF(),
        0.72 * base.blueF() + 0.28 * accent.blueF());
}

struct ComparisonRow
{
    QString left;
    QString right;
    bool changed = false;
};

class FunctionMatchItem : public QTreeWidgetItem
{
public:
    using QTreeWidgetItem::QTreeWidgetItem;

    bool operator<(const QTreeWidgetItem &other) const override
    {
        return treeWidget()->sortColumn() == 2
                   ? data(2, SortRole).toDouble() < other.data(2, SortRole).toDouble()
                   : QTreeWidgetItem::operator<(other);
    }
};

class ComparisonRows : public QAbstractTableModel
{
public:
    explicit ComparisonRows(QTableView *view)
        : QAbstractTableModel(view)
        , view(view)
    {
        view->setModel(this);
        view->setSelectionBehavior(QAbstractItemView::SelectRows);
        view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
        view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        view->setWordWrap(false);
        view->setShowGrid(false);
        view->verticalHeader()->hide();
        view->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        view->viewport()->installEventFilter(this);
        TouchScrollController::enableTextZoom(view);
        connect(Config(), &Configuration::fontsUpdated, this, &ComparisonRows::updateFont);
        updateFont();
    }

    void setRows(QList<ComparisonRow> rows)
    {
        beginResetModel();
        this->rows = std::move(rows);
        updateVisibleRows();
        endResetModel();
    }

    void setChangeContext(int count)
    {
        if (count == contextRows) {
            return;
        }
        beginResetModel();
        contextRows = count;
        updateVisibleRows();
        endResetModel();
    }

    int rowCount(const QModelIndex &parent) const override
    {
        return parent.isValid() ? 0 : contextRows < 0 ? rows.size() : visibleRows.size();
    }

    int columnCount(const QModelIndex &parent) const override { return parent.isValid() ? 0 : 2; }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid()) {
            return {};
        }
        const auto &row = rows.at(contextRows < 0 ? index.row() : visibleRows.at(index.row()));
        const QString &text = index.column() == 0 ? row.left : row.right;
        if (role == Qt::DisplayRole || role == Qt::ToolTipRole) {
            return text;
        }
        if (role == Qt::FontRole) {
            return font;
        }
        if (role == Qt::BackgroundRole && row.changed && !text.isEmpty()) {
            const QColor accent = index.column() == 0 ? QColor(220, 85, 72) : QColor(63, 171, 99);
            return tint(view->palette().color(QPalette::Base), accent);
        }
        return {};
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
            return section == 0 ? tr("Current") : tr("Reference");
        }
        return {};
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Wheel && watched == view->viewport()) {
            auto *wheel = static_cast<QWheelEvent *>(event);
            if (wheel->modifiers() & Qt::ControlModifier) {
                const int delta = wheel->angleDelta().y();
                if (delta != 0) {
                    Config()->setZoomFactor(Config()->getZoomFactor() + (delta > 0 ? 0.1 : -0.1));
                }
                wheel->accept();
                return true;
            }
        }
        return QAbstractTableModel::eventFilter(watched, event);
    }

private:
    void updateVisibleRows()
    {
        visibleRows.clear();
        if (contextRows < 0) {
            return;
        }
        qsizetype end = 0;
        for (qsizetype i = 0; i < rows.size(); ++i) {
            if (rows.at(i).changed) {
                const qsizetype start = qMax(end, i - contextRows);
                end = qMin(i + contextRows + 1, qsizetype(rows.size()));
                for (qsizetype j = start; j < end; ++j) {
                    visibleRows.append(j);
                }
            }
        }
    }

    void updateFont()
    {
        font = Config()->getFont();
        view->verticalHeader()->setDefaultSectionSize(QFontMetrics(font, view).height() + 4);
        const int count = rowCount(QModelIndex());
        if (count) {
            emit dataChanged(index(0, 0), index(count - 1, 1), {Qt::FontRole});
        }
    }

    QTableView *view;
    QFont font;
    QList<ComparisonRow> rows;
    QList<qsizetype> visibleRows;
    int contextRows = -1;
};

ComparisonRows *rowsOf(QTableView *view)
{
    return static_cast<ComparisonRows *>(view->model());
}

QString hexLine(RVA addr, const QByteArray &bytes)
{
    if (addr == RVA_INVALID || bytes.isEmpty()) {
        return {};
    }
    QString ascii;
    for (const uchar byte : bytes) {
        ascii += QLatin1Char(byte >= 32 && byte < 127 ? byte : '.');
    }
    return QStringLiteral("%1  %2  %3")
        .arg(RAddressString(addr), QString::fromLatin1(bytes.toHex(' ')).leftJustified(47), ascii);
}

struct Instruction
{
    RVA addr;
    QString opcode;
    QString text;
    QByteArray bytes;

    QString line() const { return displayAddress(addr) + QStringLiteral("  ") + text; }
};

QList<Instruction> instructions(const QJsonObject &function)
{
    QList<Instruction> result;
    for (const auto &block : function.value("blocks").toArray()) {
        for (const auto &value : block.toObject().value("ops").toArray()) {
            const auto op = value.toObject();
            const auto opcode = op.value("opcode").toString();
            if (!opcode.isEmpty()) {
                result.append(
                    {address(op, "addr"),
                     opcode,
                     op.value("disasm").toString(opcode),
                     QByteArray::fromHex(op.value("bytes").toString().toLatin1())});
            }
        }
    }
    return result;
}

bool differentInstructions(RLevBuf *a, RLevBuf *b, ut32 i, ut32 j)
{
    return static_cast<Instruction *>(a->buf)[i].opcode
           != static_cast<Instruction *>(b->buf)[j].opcode;
}

template<typename Item, typename Append>
void align(QList<Item> &left, QList<Item> &right, RLevMatches different, Append append)
{
    Item empty;
    RLevBuf first{left.isEmpty() ? &empty : left.data(), static_cast<ut32>(left.size())};
    RLevBuf second{right.isEmpty() ? &empty : right.data(), static_cast<ut32>(right.size())};
    RLevOp *operations = nullptr;
    r_diff_levenshtein_path(&first, &second, 2048, different, &operations);
    qsizetype i = 0, j = 0;
    if (operations) {
        for (const auto *op = operations; *op != LEVEND; ++op) {
            const auto *l = *op == LEVADD || i == left.size() ? nullptr : &left.at(i++);
            const auto *r = *op == LEVDEL || j == right.size() ? nullptr : &right.at(j++);
            append(l, r, *op != LEVNOP);
        }
        r_mem_free(operations);
    }
    while (i < left.size() || j < right.size()) {
        append(
            i < left.size() ? &left.at(i++) : nullptr,
            j < right.size() ? &right.at(j++) : nullptr,
            true);
    }
}

QStringList codeLines(QString code)
{
    code.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    code.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    if (code.endsWith(QLatin1Char('\n'))) {
        code.chop(1);
    }
    return code.split(QLatin1Char('\n'));
}

QString localFilePath(QString path)
{
    if (path.startsWith(QStringLiteral("file://"))) {
        path.remove(0, 7);
    }
    return path.contains(QStringLiteral("://")) ? QString() : path;
}

bool sameFile(const QFileInfo &before)
{
    const QFileInfo now(before.absoluteFilePath());
    return now.isFile() && now.size() == before.size()
           && now.lastModified() == before.lastModified();
}

bool autoName(const QString &name)
{
    return name.startsWith("fcn.") || name.startsWith("sub.") || name.startsWith("func.");
}
} // namespace

class ComparisonGraphView : public SimpleTextGraphView
{
public:
    ComparisonGraphView(QWidget *parent, MainWindow *main)
        : SimpleTextGraphView(parent, main)
    {}

    void setFunction(const QJsonObject &function)
    {
        this->function = function;
        selectedBlock = NO_BLOCK_SELECTED;
        refreshView();
        if (!blocks.empty()) {
            center();
        }
    }

    std::function<void(RVA)> showMatch;

    void showMatchedBlock(RVA addr)
    {
        selectBlockWithId(addr);
        const auto it = blocks.find(addr);
        if (it != blocks.end()) {
            showBlock(it->second, true);
        }
    }

protected:
    void blockClicked(GraphView::GraphBlock &block, QMouseEvent *event, QPoint pos) override
    {
        SimpleTextGraphView::blockClicked(block, event, pos);
        if (event->button() == Qt::LeftButton && showMatch) {
            showMatch(matches.value(block.entry, RVA_INVALID));
        }
    }

    void loadCurrentGraph() override
    {
        blocks.clear();
        blockContent.clear();
        matches.clear();
        for (const auto &value : function.value("blocks").toArray()) {
            const auto obj = value.toObject();
            GraphBlock block;
            block.entry = address(obj, "addr");
            if (block.entry == RVA_INVALID) {
                continue;
            }
            QStringList text{displayAddress(block.entry)};
            if (const RVA target = address(obj, "diff_addr"); target != RVA_INVALID) {
                matches.insert(block.entry, target);
            }
            for (const char *key : {"jump", "fail"}) {
                const RVA target = address(obj, key);
                if (target != RVA_INVALID) {
                    block.edges.emplace_back(target);
                }
            }
            for (const auto &item : obj.value("switchop").toObject().value("cases").toArray()) {
                const RVA target = address(item.toObject(), "jump");
                if (target != RVA_INVALID) {
                    block.edges.emplace_back(target);
                }
            }
            for (const auto &op : obj.value("ops").toArray()) {
                const auto inst = op.toObject();
                text.append(inst.value("disasm").toString(inst.value("opcode").toString()));
            }
            QColor color;
            const QString status = obj.value("diff").toString();
            if (status == "changed" || status == "unmatched") {
                color = tint(
                    disassemblyBackgroundColor,
                    status == "changed" ? QColor(220, 166, 48) : QColor(220, 80, 72));
            }
            addBlock(std::move(block), text.join(QLatin1Char('\n')), RVA_INVALID, color);
        }
        cleanupEdges(blocks);
        computeGraphPlacement();
    }

private:
    QJsonObject function;
    QHash<RVA, RVA> matches;
};

BinaryComparisonWidget::BinaryComparisonWidget(MainWindow *main)
    : IaitoDockWidget(main)
    , ui(new Ui::BinaryComparisonWidget)
    , main(main)
{
    ui->setupUi(this);
    ui->splitter->setStretchFactor(1, 1);
    new ComparisonRows(ui->codeTable);
    new ComparisonRows(ui->decompiledTable);
    new ComparisonRows(ui->hexTable);
    for (const auto *decompiler : Core()->getDecompilers()) {
        if (dynamic_cast<const R2CommandDecompiler *>(decompiler)) {
            ui->decompilerComboBox->addItem(decompiler->getName(), decompiler->getId());
        }
    }
    const int pdc = ui->decompilerComboBox->findData(QStringLiteral("pdc"));
    if (pdc >= 0) {
        ui->decompilerComboBox->setCurrentIndex(pdc);
    }
    connect(
        ui->details,
        &QTabWidget::currentChanged,
        this,
        &BinaryComparisonWidget::refreshDecompiledDiff);
    connect(ui->decompilerComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        rowsOf(ui->decompiledTable)->setRows({});
        ui->status->clear();
        refreshDecompiledDiff();
    });
    connect(
        ui->decompileButton,
        &QPushButton::clicked,
        this,
        &BinaryComparisonWidget::decompileFunctions);
    QSettings settings;
    ui->hexContext->setValue(settings.value("binaryComparison/hexContextRows", 2).toInt());
    ui->hexChangesOnly->setChecked(
        settings.value("binaryComparison/hexChangesOnly", false).toBool());
    updateHexFilter();
    ui->patchFormat->addItem(tr("xpatch"), QStringLiteral("-X"));
    ui->patchFormat->addItem(tr("Unified binary diff"), QStringLiteral("-u"));
    ui->patchFormat->addItem(tr("radare2 write script"), QStringLiteral("-r"));
    ui->patchPreview->setFont(Config()->getFont());
    TouchScrollController::enableTextZoom(ui->patchPreview);
    connect(Config(), &Configuration::fontsUpdated, this, [this] {
        ui->patchPreview->setFont(Config()->getFont());
    });
    connect(
        ui->generatePatchButton,
        &QPushButton::clicked,
        this,
        &BinaryComparisonWidget::generatePatch);
    connect(ui->savePatchButton, &QPushButton::clicked, this, &BinaryComparisonWidget::savePatch);
    connect(ui->patchFormat, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        clearPatch();
        updateControls();
    });
    leftGraph = new ComparisonGraphView(ui->graphs, main);
    rightGraph = new ComparisonGraphView(ui->graphs, main);
    leftGraph->showMatch = [this](RVA addr) { rightGraph->showMatchedBlock(addr); };
    rightGraph->showMatch = [this](RVA addr) { leftGraph->showMatchedBlock(addr); };

    connect(ui->leftBrowse, &QPushButton::clicked, this, [this] { browse(true); });
    connect(ui->rightBrowse, &QPushButton::clicked, this, [this] { browse(false); });
    connect(ui->compareButton, &QPushButton::clicked, this, &BinaryComparisonWidget::compare);
    connect(ui->importButton, &QPushButton::clicked, this, &BinaryComparisonWidget::importNames);
    connect(ui->cancelButton, &QPushButton::clicked, this, [this] {
        cancelled = true;
        process.kill();
    });
    connect(ui->previousPage, &QPushButton::clicked, this, [this] {
        page = page - rangeStart > DiskPageSize ? page - DiskPageSize : rangeStart;
        showDiskPage();
    });
    connect(ui->nextPage, &QPushButton::clicked, this, [this] {
        page += DiskPageSize;
        showDiskPage();
    });
    connect(ui->hexContext, qOverload<int>(&QSpinBox::valueChanged), this, [this](int rows) {
        QSettings().setValue("binaryComparison/hexContextRows", rows);
        updateHexFilter();
        if (diskImages && rangeEnd > page && process.state() == QProcess::NotRunning) {
            showDiskPage();
        }
    });
    connect(ui->hexChangesOnly, &QCheckBox::toggled, this, [this](bool enabled) {
        QSettings().setValue("binaryComparison/hexChangesOnly", enabled);
        updateHexFilter();
    });
    connect(ui->results, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
        inspect(item);
    });
    connect(ui->results, &QTreeWidget::itemSelectionChanged, this, [this] { updateControls(); });
    connect(ui->filter, &QLineEdit::textChanged, this, [this](const QString &filter) {
        for (int i = 0; i < ui->results->topLevelItemCount(); ++i) {
            auto *item = ui->results->topLevelItem(i);
            bool match = false;
            for (int column = 0; column < ui->results->columnCount() && !match; ++column) {
                match = item->text(column).contains(filter, Qt::CaseInsensitive);
            }
            item->setHidden(!match);
            if (!match) {
                item->setSelected(false);
            }
        }
    });
    connect(&process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            ui->status->setText(
                tr("Could not start %1. %2")
                    .arg(QFileInfo(process.program()).baseName(), process.errorString()));
            receive = {};
            updateControls();
        }
    });
    connect(
        &process,
        qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
        this,
        [this](int exitCode, QProcess::ExitStatus exitStatus) {
            const auto output = process.readAllStandardOutput();
            const auto error = QString::fromUtf8(process.readAllStandardError()).trimmed();
            const auto name = QFileInfo(process.program()).baseName();
            auto callback = std::exchange(receive, {});
            if (cancelled) {
                ui->status->setText(tr("Comparison cancelled."));
            } else if (exitCode || exitStatus != QProcess::NormalExit || (callback && !callback(output))) {
                ui->status->setToolTip(error.right(4000));
                ui->status->setText(tr("%1 could not produce comparison data. %2")
                                        .arg(name, error.section(QLatin1Char('\n'), -1).left(200)));
            }
            updateControls();
        });
    updateControls();
}

BinaryComparisonWidget::~BinaryComparisonWidget()
{
    if (process.state() != QProcess::NotRunning) {
        process.disconnect(this);
        process.kill();
        process.waitForFinished();
    }
}

void BinaryComparisonWidget::openComparison(bool disk)
{
    if (process.state() != QProcess::NotRunning) {
        show();
        raise();
        return;
    }
    const QString left = localFilePath(main->getFilename());
    if (diskImages != disk || (!disk && leftFile.filePath() != left)) {
        diskImages = disk;
        leftFile = disk ? QFileInfo() : QFileInfo(left);
        rightFile = QFileInfo();
        reset();
    }
    setWindowTitle(disk ? tr("Compare Disk Images") : tr("Compare Binaries"));
    ui->leftLabel->setText(disk ? tr("Current image") : tr("Current binary"));
    ui->leftPath->setText(leftFile.filePath());
    ui->rightPath->setText(rightFile.filePath());
    ui->leftBrowse->setVisible(disk);
    ui->importButton->setVisible(!disk);
    ui->paging->setVisible(disk);
    for (auto *tab : {ui->codeTab, ui->decompilerTab, ui->graphTab}) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
        ui->details->setTabVisible(ui->details->indexOf(tab), !disk);
#else
        ui->details->setTabEnabled(ui->details->indexOf(tab), !disk);
#endif
    }
    ui->details->setCurrentWidget(disk ? ui->hexTab : ui->codeTab);
    ui->results->setHeaderLabels(
        disk
            ? QStringList{tr("Offset"), tr("Length"), tr("Status")}
            : QStringList{
                  tr("Current Function"), tr("Reference Function"), tr("Similarity"), tr("Status")});
    ui->results->setColumnCount(disk ? 3 : 4);
    updateControls();
    show();
    raise();
}

void BinaryComparisonWidget::reset()
{
    ui->filter->clear();
    ui->results->clear();
    ui->results->setSortingEnabled(false);
    rowsOf(ui->codeTable)->setRows({});
    rowsOf(ui->decompiledTable)->setRows({});
    rowsOf(ui->hexTable)->setRows({});
    leftGraph->setFunction({});
    rightGraph->setFunction({});
    leftFunction = rightFunction = RVA_INVALID;
    rangeStart = rangeEnd = page = 0;
    ui->pageLabel->clear();
    clearPatch();
    ui->status->clear();
    ui->status->setToolTip({});
}

void BinaryComparisonWidget::browse(bool left)
{
    const QString caption = left ? tr("Select Current Disk Image") : tr("Select Reference File");
    const QString path = QFileDialog::getOpenFileName(
        this, caption, Config()->getRecentFolder(), QString(), nullptr, QFILEDIALOG_FLAGS);
    if (path.isEmpty()) {
        return;
    }
    const QFileInfo file(path);
    (left ? leftFile : rightFile) = file;
    (left ? ui->leftPath : ui->rightPath)->setText(file.filePath());
    Config()->setRecentFolder(file.absolutePath());
    reset();
    updateControls();
}

void BinaryComparisonWidget::compare()
{
    if (!diskImages) {
        leftFile = QFileInfo(localFilePath(main->getFilename()));
        ui->leftPath->setText(leftFile.filePath());
    }
    leftFile.refresh();
    rightFile.refresh();
    if (!leftFile.isFile() || !rightFile.isFile()) {
        ui->status->setText(tr("Choose two readable local files to compare."));
        return;
    }
    reset();
    runDiff(
        diskImages ? QStringList{"-x", "-j"} : QStringList{"-A", "-C", "-j"},
        diskImages ? tr("Comparing disk images…") : tr("Analyzing functions…"),
        [this](const QJsonDocument &json) {
            if (diskImages) {
                showRanges(json.object().value("ranges").toArray());
            } else {
                showMatches(json.array());
            }
        });
}

void BinaryComparisonWidget::showMatches(const QJsonArray &data)
{
    for (const auto &value : data) {
        const QJsonObject match = value.toObject();
        const bool referenceOnly = !match.contains("diff_addr");
        const RVA current = referenceOnly ? RVA_INVALID : address(match, "addr");
        const RVA reference = address(match, referenceOnly ? "addr" : "diff_addr");
        const QString leftName = referenceOnly ? QString() : match.value("name").toString();
        const QString rightName = match.value(referenceOnly ? "name" : "diff_name").toString();
        const bool paired = reference != RVA_INVALID && current != RVA_INVALID
                            && !rightName.isEmpty();
        const double score = match.value("dist").toDouble();
        const auto label = [](const QString &name, RVA addr) {
            return name.isEmpty() ? QString()
                                  : QStringLiteral("%1  %2").arg(name, displayAddress(addr));
        };
        auto *item = new FunctionMatchItem(
            ui->results,
            {label(leftName, current),
             label(rightName, reference),
             paired ? tr("%1%").arg(score * 100.0, 0, 'f', 1) : QString(),
             paired ? (score >= 1 ? tr("Equal") : tr("Changed"))
                    : (referenceOnly ? tr("Reference only") : tr("Current only"))});
        item->setData(0, DataRole, QVariant::fromValue(current));
        item->setData(1, DataRole, QVariant::fromValue(paired ? reference : RVA_INVALID));
        item->setData(2, DataRole, rightName);
        item->setData(2, SortRole, paired ? score : -1.0);
    }
    ui->results->setSortingEnabled(true);
    ui->results->sortByColumn(2, Qt::DescendingOrder);
    ui->results->resizeColumnToContents(0);
    ui->results->resizeColumnToContents(1);
    ui->status->setText(
        tr("%n function result(s). Activate a match to inspect it.",
           nullptr,
           ui->results->topLevelItemCount()));
}

void BinaryComparisonWidget::showRanges(const QJsonArray &data)
{
    for (const auto &value : data) {
        const QJsonObject range = value.toObject();
        const RVA offset = address(range, "offset");
        const RVA size = address(range, "size");
        if (offset == RVA_INVALID || size == RVA_INVALID || !size || size > RVA_INVALID - offset) {
            continue;
        }
        const auto status = range.value("status").toString();
        QString label = tr("Changed");
        if (status == "equal") {
            label = tr("Equal bytes (collapsed)");
        } else if (status == "left-only") {
            label = tr("Current image only");
        } else if (status == "right-only") {
            label = tr("Reference image only");
        }
        auto *item
            = new QTreeWidgetItem(ui->results, {displayAddress(offset), RSizeString(size), label});
        item->setData(0, DataRole, QVariant::fromValue(offset));
        item->setData(1, DataRole, QVariant::fromValue(size));
    }
    ui->results->resizeColumnToContents(0);
    ui->results->resizeColumnToContents(1);
    ui->status->setText(
        tr("%n range(s). Activate a range to inspect its bytes.",
           nullptr,
           ui->results->topLevelItemCount()));
}

void BinaryComparisonWidget::inspect(QTreeWidgetItem *item)
{
    if (process.state() != QProcess::NotRunning) {
        return;
    }
    if (!sameFile(leftFile) || !sameFile(rightFile)) {
        ui->status->setText(tr("An input changed. Run the comparison again."));
        return;
    }
    const RVA left = item->data(0, DataRole).toULongLong();
    const RVA right = item->data(1, DataRole).toULongLong();
    if (diskImages) {
        rangeStart = page = left;
        rangeEnd = left + right;
        showDiskPage();
        return;
    }
    leftFunction = left;
    rightFunction = right;
    rowsOf(ui->decompiledTable)->setRows({});
    updateControls();
    if (left != RVA_INVALID && right != RVA_INVALID) {
        runDiff(
            {"-g",
             QStringLiteral("%1,%2").arg(RAddressString(left), RAddressString(right)),
             "-m",
             "p",
             "-A",
             "-e",
             "scr.color=0"},
            tr("Comparing functions…"),
            [this](const QJsonDocument &json) { showFunction(json.object()); });
    } else if (left != RVA_INVALID) {
        Core()->seekAndShow(left);
    }
}

void BinaryComparisonWidget::showFunction(const QJsonObject &data)
{
    const QJsonObject left = data.value("left").toArray().at(0).toObject();
    const QJsonObject right = data.value("right").toArray().at(0).toObject();
    if (left.isEmpty() || right.isEmpty()) {
        ui->status->setText(tr("radiff2 did not return both functions."));
        return;
    }
    leftGraph->setFunction(left);
    rightGraph->setFunction(right);
    auto a = instructions(left);
    auto b = instructions(right);
    QList<ComparisonRow> code, hex;
    align(a, b, differentInstructions, [&](const Instruction *l, const Instruction *r, bool changed) {
        code.append({l ? l->line() : QString(), r ? r->line() : QString(), changed});
        hex.append(
            {l ? hexLine(l->addr, l->bytes) : QString(),
             r ? hexLine(r->addr, r->bytes) : QString(),
             !l || !r || l->bytes != r->bytes});
    });
    rowsOf(ui->codeTable)->setRows(std::move(code));
    rowsOf(ui->hexTable)->setRows(std::move(hex));
    ui->status->setText(
        tr("%1 ↔ %2").arg(left.value("name").toString(), right.value("name").toString()));
    refreshDecompiledDiff();
}

void BinaryComparisonWidget::refreshDecompiledDiff()
{
    if (ui->details->currentWidget() == ui->decompilerTab
        && rowsOf(ui->decompiledTable)->rowCount({}) == 0
        && ui->decompilerComboBox->currentData().toString() == QStringLiteral("pdc")) {
        decompileFunctions();
    }
}

void BinaryComparisonWidget::decompileFunctions()
{
    if (diskImages || process.state() != QProcess::NotRunning || leftFunction == RVA_INVALID
        || rightFunction == RVA_INVALID) {
        return;
    }
    if (!sameFile(leftFile) || !sameFile(rightFile)) {
        ui->status->setText(tr("An input changed. Run the comparison again."));
        return;
    }
    const auto id = ui->decompilerComboBox->currentData().toString();
    const auto *decompiler = dynamic_cast<R2CommandDecompiler *>(Core()->getDecompilerById(id));
    if (decompiler) {
        rowsOf(ui->decompiledTable)->setRows({});
        decompileFunction(decompiler, false, {});
    }
}

void BinaryComparisonWidget::decompileFunction(
    const R2CommandDecompiler *decompiler, bool reference, QString leftCode)
{
    const auto &file = reference ? rightFile : leftFile;
    runTool(
        QStringLiteral("radare2"),
        {"-qq",
         "-N",
         "-e",
         "scr.color=0",
         "-c",
         "aa",
         "-c",
         decompiler->commandAt(reference ? rightFunction : leftFunction),
         "--",
         file.absoluteFilePath()},
        reference ? tr("Decompiling the reference function…")
                  : tr("Decompiling the current function…"),
        [this, decompiler, reference, leftCode = std::move(leftCode)](const QByteArray &output) {
            const QString code = decompiler->codeFromOutput(output);
            if (code.trimmed().isEmpty()) {
                ui->status->setText(
                    reference ? tr("The decompiler returned no reference code.")
                              : tr("The decompiler returned no current code."));
            } else if (!sameFile(leftFile) || !sameFile(rightFile)) {
                ui->status->setText(tr("An input changed. Run the comparison again."));
            } else if (reference) {
                showDecompiledDiff(leftCode, code);
            } else {
                decompileFunction(decompiler, true, code);
            }
            return true;
        });
}

void BinaryComparisonWidget::showDecompiledDiff(const QString &leftCode, const QString &rightCode)
{
    auto left = codeLines(leftCode);
    auto right = codeLines(rightCode);
    QList<ComparisonRow> rows;
    align(
        left,
        right,
        [](RLevBuf *a, RLevBuf *b, ut32 i, ut32 j) {
            return static_cast<QString *>(a->buf)[i] != static_cast<QString *>(b->buf)[j];
        },
        [&](const QString *l, const QString *r, bool changed) {
            rows.append({l ? *l : QString(), r ? *r : QString(), changed});
        });
    rowsOf(ui->decompiledTable)->setRows(std::move(rows));
    ui->status->setText(
        left == right ? tr("The decompiled functions are identical.")
                      : tr("Decompiled comparison ready."));
}

void BinaryComparisonWidget::showDiskPage()
{
    if (!sameFile(leftFile) || !sameFile(rightFile)) {
        ui->status->setText(tr("An input changed. Run the comparison again."));
        return;
    }
    const auto length = qMin(DiskPageSize, rangeEnd - page);
    const RVA context = ui->hexContext->value() * DiskRowSize;
    const RVA start = page - qMin(page, context);
    const RVA end = qMax(leftFile.size(), rightFile.size());
    const RVA windowLength = qMin(end - start, page - start + length + context);
    runDiff(
        {"-f", "hexjson", "-o", RAddressString(start), "-l", QString::number(windowLength)},
        tr("Reading disk image page…"),
        [this, start, length](const QJsonDocument &json) {
            QList<ComparisonRow> rows;
            for (const auto &value : json.object().value("rows").toArray()) {
                const auto obj = value.toObject();
                const RVA offset = address(obj, "offset");
                const auto left = QByteArray::fromHex(obj.value("left").toString().toLatin1());
                const auto right = QByteArray::fromHex(obj.value("right").toString().toLatin1());
                rows.append(
                    {hexLine(start + offset, left), hexLine(start + offset, right), left != right});
            }
            rowsOf(ui->hexTable)->setRows(std::move(rows));
            ui->pageLabel->setText(
                tr("%1–%2").arg(RAddressString(page), RAddressString(page + length - 1)));
            ui->status->clear();
        });
}

void BinaryComparisonWidget::updateHexFilter()
{
    rowsOf(ui->hexTable)
        ->setChangeContext(ui->hexChangesOnly->isChecked() ? ui->hexContext->value() : -1);
}

void BinaryComparisonWidget::clearPatch()
{
    patchData.clear();
    ui->patchPreview->clear();
}

void BinaryComparisonWidget::generatePatch()
{
    clearPatch();
    updateControls();
    if (!sameFile(leftFile) || !sameFile(rightFile)) {
        ui->status->setText(tr("An input changed. Run the comparison again."));
        return;
    }
    if (qMax(leftFile.size(), rightFile.size()) > ST32_MAX) {
        ui->status->setText(tr("Patch generation requires files smaller than 2 GiB."));
        return;
    }
    QStringList args{"-q", ui->patchFormat->currentData().toString()};
    if (leftFile.size() != rightFile.size()) {
        args.prepend("-d");
    }
    runDiffOutput(args, tr("Generating patch…"), [this](const QByteArray &output) {
        patchData = output;
        ui->patchPreview->setPlainText(QString::fromUtf8(output.left(PatchPreviewLimit)));
        if (output.size() > PatchPreviewLimit) {
            ui->status->setText(
                tr("Showing the first 256 KiB. Save Patch writes the complete patch."));
        } else {
            ui->status->setText(
                output.isEmpty() ? tr("The files have no differences.") : tr("Patch ready."));
        }
        return true;
    });
}

void BinaryComparisonWidget::savePatch()
{
    QFileDialog dialog(this, tr("Save Binary Patch"), Config()->getRecentFolder());
    dialog.setOptions(QFILEDIALOG_FLAGS);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setDefaultSuffix(ui->patchFormat->currentData().toString() == "-r" ? "r2" : "patch");
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QString path = dialog.selectedFiles().constFirst();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(patchData) != patchData.size()
        || !file.commit()) {
        QMessageBox::warning(this, tr("Save Binary Patch"), file.errorString());
        return;
    }
    Config()->setRecentFolder(QFileInfo(path).absolutePath());
}

void BinaryComparisonWidget::importNames()
{
    if (QFileInfo(localFilePath(main->getFilename())).absoluteFilePath()
            != leftFile.absoluteFilePath()
        || !sameFile(leftFile) || !sameFile(rightFile)) {
        ui->status->setText(tr("An input changed. Run the comparison again."));
        return;
    }
    auto *core = Core();
    QHash<RVA, QString> functions;
    QSet<QString> names;
    for (const auto &function : core->getAllFunctions()) {
        functions.insert(function.offset, function.name);
        names.insert(function.name);
    }
    static const QRegularExpression validName(QStringLiteral(R"(^[A-Za-z_][A-Za-z0-9_.]*$)"));
    int renamed = 0, skipped = 0;
    for (auto *item : ui->results->selectedItems()) {
        const RVA current = item->data(0, DataRole).toULongLong();
        const QString name = item->data(2, DataRole).toString();
        if (item->data(1, DataRole).toULongLong() == RVA_INVALID || names.contains(name)
            || !autoName(functions.value(current)) || autoName(name)
            || !validName.match(name).hasMatch()) {
            ++skipped;
            continue;
        }
        core->renameFunction(current, name);
        if (core->cmdRawAt("afn", current).trimmed() != name) {
            ++skipped;
            continue;
        }
        names.insert(name);
        item->setText(0, QStringLiteral("%1  %2").arg(name, RAddressString(current)));
        ++renamed;
    }
    if (renamed) {
        emit core->functionsChanged();
    }
    ui->status->setText(tr("Imported %1 name(s). Skipped %2.").arg(renamed).arg(skipped));
}

void BinaryComparisonWidget::updateControls()
{
    const bool busy = process.state() != QProcess::NotRunning;
    ui->compareButton->setEnabled(!busy && leftFile.isFile() && rightFile.isFile());
    ui->cancelButton->setVisible(busy);
    ui->leftBrowse->setEnabled(!busy);
    ui->rightBrowse->setEnabled(!busy);
    ui->hexContext->setEnabled(!busy);
    ui->decompilerComboBox->setEnabled(!busy && ui->decompilerComboBox->count() > 1);
    ui->decompileButton->setEnabled(
        !busy && ui->decompilerComboBox->count() > 0 && leftFunction != RVA_INVALID
        && rightFunction != RVA_INVALID);
    ui->patchFormat->setEnabled(!busy);
    ui->generatePatchButton->setEnabled(ui->compareButton->isEnabled());
    ui->savePatchButton->setEnabled(!busy && !patchData.isEmpty());
    ui->importButton->setEnabled(!busy && !ui->results->selectedItems().isEmpty());
    ui->previousPage->setEnabled(!busy && page > rangeStart);
    ui->nextPage->setEnabled(!busy && rangeEnd - page > DiskPageSize);
}

void BinaryComparisonWidget::runDiff(
    const QStringList &args,
    const QString &message,
    std::function<void(const QJsonDocument &)> callback)
{
    runDiffOutput(args, message, [callback = std::move(callback)](const QByteArray &output) {
        const auto json = QJsonDocument::fromJson(output);
        if (json.isNull()) {
            return false;
        }
        callback(json);
        return true;
    });
}

void BinaryComparisonWidget::runDiffOutput(
    const QStringList &args,
    const QString &message,
    std::function<bool(const QByteArray &)> callback)
{
    runTool(
        QStringLiteral("radiff2"),
        args + QStringList{leftFile.absoluteFilePath(), rightFile.absoluteFilePath()},
        message,
        std::move(callback));
}

void BinaryComparisonWidget::runTool(
    const QString &name,
    const QStringList &args,
    const QString &message,
    std::function<bool(const QByteArray &)> callback)
{
    const QString tool = Iaito::findRadareTool(name);
    if (tool.isEmpty()) {
        ui->status->setText(
            tr("%1 was not found. Install the radare2 command-line tools.").arg(name));
        return;
    }
    cancelled = false;
    receive = std::move(callback);
    ui->status->setToolTip({});
    ui->status->setText(message);
    process.start(tool, args);
    updateControls();
}
#endif
