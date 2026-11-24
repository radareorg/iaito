#include "widgets/FilesystemAllocationView.h"
#include "ui_FilesystemAllocationView.h"
#include "widgets/ZoomWidget.h"

#include <algorithm>
#include <QAbstractTableModel>
#include <QEvent>
#include <QHeaderView>
#include <QLocale>
#include <QSortFilterProxyModel>

namespace {
using Evidence = FilesystemEvidence;
constexpr int Cells = 960;
constexpr int SortRole = Qt::UserRole;
enum State { Unmapped, Free, Allocated, Orphan, Bad, Unknown, Mixed };

State allocationState(quint32 flags)
{
    if (flags & Evidence::Bad) {
        return Bad;
    }
    if (flags & Evidence::Unknown) {
        return Unknown;
    }
    if (flags & Evidence::Orphan) {
        return Orphan;
    }
    if (flags & Evidence::Free) {
        return Free;
    }
    return flags & Evidence::Allocated ? Allocated : Unmapped;
}

QString stateName(State state)
{
    using View = FilesystemAllocationView;
    switch (state) {
    case Free:
        return View::tr("Free");
    case Allocated:
        return View::tr("Allocated");
    case Orphan:
        return View::tr("Orphan");
    case Bad:
        return View::tr("Bad");
    case Unknown:
        return View::tr("Unknown");
    case Mixed:
        return View::tr("Mixed");
    case Unmapped:
        return View::tr("Unmapped");
    }
    return {};
}

QColor stateColor(State state)
{
    const QColor colors[] = {
        QColor("#696e77"),
        QColor("#4caf87"),
        QColor("#4b85cf"),
        QColor("#edaf58"),
        QColor("#d66067"),
        QColor("#9b72c8"),
        QColor("#607b94"),
    };
    return colors[state];
}
} // namespace

class FilesystemAllocationModel : public QAbstractTableModel
{
public:
    explicit FilesystemAllocationModel(QObject *parent)
        : QAbstractTableModel(parent)
    {}

    void setRanges(const QList<Evidence::Range> &value)
    {
        beginResetModel();
        ranges = value;
        endResetModel();
    }

    int rowCount(const QModelIndex &parent) const override
    {
        return parent.isValid() ? 0 : int(ranges.size());
    }

    int columnCount(const QModelIndex &parent) const override { return parent.isValid() ? 0 : 3; }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
            return {};
        }
        const QString columns[] = {
            FilesystemAllocationView::tr("Image offset"),
            FilesystemAllocationView::tr("Bytes"),
            FilesystemAllocationView::tr("State"),
        };
        return columns[section];
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid()) {
            return {};
        }
        const auto &range = ranges.at(index.row());
        if (role == SortRole) {
            return index.column() == 2
                       ? QVariant(stateName(allocationState(range.flags)))
                       : QVariant::fromValue(index.column() == 0 ? range.physical : range.length);
        }
        if (role != Qt::DisplayRole) {
            return {};
        }
        switch (index.column()) {
        case 0:
            return RAddressString(range.physical);
        case 1:
            return QLocale().toString(range.length);
        default:
            return stateName(allocationState(range.flags));
        }
    }

    QList<Evidence::Range>::const_iterator startingAt(quint64 offset) const
    {
        return std::lower_bound(
            ranges.cbegin(), ranges.cend(), offset, [](const Evidence::Range &range, quint64 at) {
                return range.physical + range.length <= at;
            });
    }

    QList<Evidence::Range> ranges;
};

FilesystemAllocationView::FilesystemAllocationView(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::FilesystemAllocationView)
    , grid(new ZoomView)
    , table(new FilesystemAllocationModel(this))
    , sortModel(new QSortFilterProxyModel(this))
{
    ui->setupUi(this);
    ui->mapScroll->setWidget(grid);
    ui->mapScroll->viewport()->installEventFilter(this);
    grid->setColumns(40);
    grid->setCellSize(9);
    grid->setAccessibleName(tr("Allocation block grid"));
    ui->splitter->setSizes({360, 380});
    sortModel->setSourceModel(table);
    sortModel->setSortRole(SortRole);
    ui->ranges->setModel(sortModel);
    ui->ranges->sortByColumn(0, Qt::AscendingOrder);
    ui->ranges->verticalHeader()->hide();
    ui->ranges->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->ranges->horizontalHeader()->setStretchLastSection(true);
    QStringList labels;
    for (const auto state : {Free, Allocated, Orphan, Bad, Unknown, Unmapped, Mixed}) {
        labels.append(QStringLiteral("<span style='color:%1'>■</span> %2")
                          .arg(stateColor(state).name(), stateName(state).toHtmlEscaped()));
    }
    ui->legend->setText(labels.join(QStringLiteral(" &nbsp; ")));
    connect(ui->scan, &QPushButton::clicked, this, &FilesystemAllocationView::scanRequested);
    connect(ui->zoomIn, &QToolButton::clicked, this, [this]() { zoom(1); });
    connect(ui->zoomOut, &QToolButton::clicked, this, [this]() { zoom(-1); });
    connect(ui->fullVolume, &QToolButton::clicked, this, &FilesystemAllocationView::showFullVolume);
    connect(grid, &ZoomView::blocksChangeRequested, this, &FilesystemAllocationView::zoom);
    connect(grid, &ZoomView::blockClicked, this, &FilesystemAllocationView::selectOffset);
    connect(ui->openOffset, &QPushButton::clicked, this, [this]() {
        if (selectedOffset != UT64_MAX) {
            emit imageOffsetActivated(selectedOffset);
        }
    });
    connect(ui->ranges, &QTableView::activated, this, [this](const QModelIndex &proxy) {
        const auto index = sortModel->mapToSource(proxy);
        emit imageOffsetActivated(table->ranges.at(index.row()).physical);
    });
    connect(ui->ranges, &QTableView::clicked, this, [this](const QModelIndex &proxy) {
        const auto index = sortModel->mapToSource(proxy);
        selectedOffset = table->ranges.at(index.row()).physical;
        grid->setSeekAddress(selectedOffset);
        ui->openOffset->setEnabled(true);
    });
    setVolume(nullptr);
}

FilesystemAllocationView::~FilesystemAllocationView() = default;

bool FilesystemAllocationView::eventFilter(QObject *object, QEvent *event)
{
    if (object == ui->mapScroll->viewport() && event->type() == QEvent::Resize) {
        const int columns = qMax(1, ui->mapScroll->viewport()->width() / grid->cellPitch());
        if (grid->columns() != columns) {
            grid->setColumns(columns);
        }
    }
    return QWidget::eventFilter(object, event);
}

void FilesystemAllocationView::setVolume(const Evidence::Volume *volume)
{
    const bool supported = volume && volume->deletedEntries && R2_ABIVERSION >= 188;
    volumeStart = volume ? volume->start : 0;
    volumeLength = volume ? volume->length : 0;
    selectedOffset = UT64_MAX;
    ui->openOffset->setEnabled(false);
    ui->scan->setEnabled(supported);
    table->setRanges({});
    showFullVolume();
    ui->status->setText(
        supported ? tr("Scan the selected volume to see its allocation ranges.")
                  : tr("Select a volume with allocation support."));
}

void FilesystemAllocationView::showMapping(const Evidence::Mapping &mapping)
{
    table->setRanges(mapping.ranges);
    showFullVolume();
    switch (mapping.status) {
    case Evidence::WalkStatus::Complete:
        ui->status->setText(tr("%n allocation ranges", nullptr, int(mapping.ranges.size())));
        break;
    case Evidence::WalkStatus::Unsupported:
        ui->status->setText(tr("This volume does not provide allocation ranges."));
        break;
    default:
        ui->status->setText(tr("Allocation scan incomplete. Unmapped bytes have unknown state."));
        break;
    }
}

void FilesystemAllocationView::showFullVolume()
{
    viewStart = volumeStart;
    viewLength = volumeLength;
    updateGrid();
}

void FilesystemAllocationView::zoom(int steps)
{
    if (!viewLength || table->ranges.isEmpty()) {
        return;
    }
    quint64 length = viewLength;
    for (int i = 0; i < qMin(16, qAbs(steps)); ++i) {
        length = steps > 0                    ? qMax<quint64>(1, length / 2)
                 : length <= volumeLength / 2 ? length * 2
                                              : volumeLength;
    }
    const quint64 anchor = selectedOffset >= viewStart && selectedOffset - viewStart < viewLength
                               ? selectedOffset
                               : viewStart + viewLength / 2;
    viewStart = qBound(
        volumeStart,
        anchor > length / 2 ? anchor - length / 2 : 0,
        volumeStart + volumeLength - length);
    viewLength = length;
    updateGrid();
}

void FilesystemAllocationView::selectOffset(quint64 offset)
{
    selectedOffset = offset;
    ui->openOffset->setEnabled(true);
    grid->setSeekAddress(offset);
    const auto range = table->startingAt(offset);
    if (range != table->ranges.cend() && range->physical <= offset) {
        const auto index = table->index(int(range - table->ranges.cbegin()), 0);
        const auto proxy = sortModel->mapFromSource(index);
        ui->ranges->setCurrentIndex(proxy);
        ui->ranges->scrollTo(proxy);
    } else {
        ui->ranges->clearSelection();
    }
}

void FilesystemAllocationView::updateGrid()
{
    const bool hasMap = viewLength && !table->ranges.isEmpty();
    ui->zoomIn->setEnabled(hasMap && viewLength > 1);
    ui->zoomOut->setEnabled(hasMap && viewLength < volumeLength);
    ui->fullVolume->setEnabled(hasMap && viewLength < volumeLength);
    QVector<ZoomView::BlockEntry> cells;
    if (hasMap) {
        const quint64 span = viewLength / Cells + (viewLength % Cells != 0);
        const quint64 end = viewStart + viewLength;
        auto range = table->startingAt(viewStart);
        for (quint64 offset = viewStart; offset < end;) {
            const quint64 finish = offset + qMin(span, end - offset);
            quint64 covered = 0;
            unsigned states = 0;
            for (; range != table->ranges.cend() && range->physical < finish; ++range) {
                const quint64 rangeEnd = range->physical + range->length;
                const quint64 intersection = qMin(rangeEnd, finish) - qMax(range->physical, offset);
                covered += intersection;
                states |= 1U << allocationState(range->flags);
                if (rangeEnd > finish) {
                    break;
                }
            }
            if (covered < finish - offset) {
                states |= 1U << Unmapped;
            }
            State state = Mixed;
            if (!(states & (states - 1))) {
                for (const auto category : {Unmapped, Free, Allocated, Orphan, Bad, Unknown}) {
                    if (states == (1U << category)) {
                        state = category;
                        break;
                    }
                }
            }
            cells.append(
                {offset,
                 0,
                 stateColor(state),
                 tr("%1\n%2 bytes\n%3")
                     .arg(RAddressString(offset))
                     .arg(QLocale().toString(finish - offset))
                     .arg(stateName(state))});
            offset = finish;
        }
    }
    grid->setData(cells);
    grid->setSeekAddress(selectedOffset);
}
