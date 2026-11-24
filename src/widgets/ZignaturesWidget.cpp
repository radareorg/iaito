#include "ZignaturesWidget.h"
#include "common/Configuration.h"
#include "common/Helpers.h"
#include "core/MainWindow.h"
#include "ui_ZignaturesWidget.h"

#include <algorithm>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>

ZignaturesModel::ZignaturesModel(QList<ZignatureDescription> *zignatures, QObject *parent)
    : QAbstractListModel(parent)
    , zignatures(zignatures)
{}

int ZignaturesModel::rowCount(const QModelIndex &) const
{
    return zignatures->count();
}

int ZignaturesModel::columnCount(const QModelIndex &) const
{
    return ZignaturesModel::ColumnCount;
}

QVariant ZignaturesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= zignatures->count()) {
        return QVariant();
    }

    const ZignatureDescription &zignature = zignatures->at(index.row());

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case OffsetColumn:
            return zignature.offset == RVA_INVALID ? QString() : RAddressString(zignature.offset);
        case NameColumn:
            return zignature.name;
        case SpaceColumn:
            return zignature.space;
        case ValueColumn:
            return zignature.bytes;
        default:
            return QVariant();
        }

    case ZignatureDescriptionRole:
        return QVariant::fromValue(zignature);

    case Qt::ToolTipRole: {
        QString tmp = QString(
            "Graph:\n\n    Cyclomatic complexity: " + RSizeString(zignature.cc)
            + "\n    Nodes / basicblocks: " + RSizeString(zignature.nbbs)
            + "\n    Edges: " + RSizeString(zignature.edges)
            + "\n    Ebbs: " + RSizeString(zignature.ebbs) + "\n\nRefs:\n");
        for (const QString &ref : zignature.refs) {
            tmp.append("\n    " + ref);
        }
        return tmp;
    }

    default:
        return QVariant();
    }
}

QVariant ZignaturesModel::headerData(int section, Qt::Orientation, int role) const
{
    switch (role) {
    case Qt::DisplayRole:
        switch (section) {
        case OffsetColumn:
            return tr("Offset");
        case NameColumn:
            return tr("Name");
        case SpaceColumn:
            return tr("Zignspace");
        case ValueColumn:
            return tr("Bytes");
        default:
            return QVariant();
        }
    default:
        return QVariant();
    }
}

ZignaturesProxyModel::ZignaturesProxyModel(ZignaturesModel *sourceModel, QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setSourceModel(sourceModel);
}

bool ZignaturesProxyModel::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    QModelIndex index = sourceModel()->index(row, 0, parent);
    ZignatureDescription item
        = index.data(ZignaturesModel::ZignatureDescriptionRole).value<ZignatureDescription>();
    return item.name.contains(FILTER_REGEX) || item.space.contains(FILTER_REGEX);
}

bool ZignaturesProxyModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    ZignatureDescription leftZignature
        = left.data(ZignaturesModel::ZignatureDescriptionRole).value<ZignatureDescription>();
    ZignatureDescription rightZignature
        = right.data(ZignaturesModel::ZignatureDescriptionRole).value<ZignatureDescription>();

    switch (left.column()) {
    case ZignaturesModel::OffsetColumn:
        return leftZignature.offset < rightZignature.offset;
    case ZignaturesModel::NameColumn:
        return leftZignature.name < rightZignature.name;
    case ZignaturesModel::SpaceColumn:
        return leftZignature.space < rightZignature.space;
    case ZignaturesModel::ValueColumn:
        return leftZignature.bytes < rightZignature.bytes;
    default:
        break;
    }

    return leftZignature.offset < rightZignature.offset;
}

ZignaturesWidget::ZignaturesWidget(MainWindow *main)
    : IaitoDockWidget(main)
    , ui(new Ui::ZignaturesWidget)
{
    ui->setupUi(this);

    zignaturesModel = new ZignaturesModel(&zignatures, this);
    zignaturesProxyModel = new ZignaturesProxyModel(zignaturesModel, this);
    ui->zignaturesTreeView->setModel(zignaturesProxyModel);
    ui->zignaturesTreeView->sortByColumn(ZignaturesModel::OffsetColumn, Qt::AscendingOrder);
    connect(ui->filterEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        zignaturesProxyModel->setFilterRegularExpression(QRegularExpression(
            QRegularExpression::escape(text), QRegularExpression::CaseInsensitiveOption));
    });
    connect(ui->generateButton, &QToolButton::clicked, this, [this] {
        Core()->cmdRaw("zg");
        refreshZignatures();
    });

    auto *database = new QMenu(ui->databaseButton);
    ui->databaseButton->setMenu(database);
    database->addAction(tr("Import…"), this, [this] {
        const QString path = selectFile(
            tr("Import Zignature Database"), tr("Zignature databases (*.sdb *.sdb.txt *.json)"));
        if (path.isEmpty()) {
            return;
        }
        if (!Core()->loadZignatureDatabase(path)) {
            QMessageBox::warning(this, tr("Zignatures"), tr("Could not load the database."));
        }
        refreshZignatures();
    });
    database->addAction(tr("Export…"), this, [this] {
        if (zignatures.isEmpty()) {
            QMessageBox::information(
                this, tr("Zignatures"), tr("Generate or load zignatures before exporting."));
            return;
        }
        const QString path
            = selectFile(tr("Export Zignature Database"), tr("Sdb database (*.sdb)"), true);
        if (!path.isEmpty() && !Core()->saveZignatureDatabase(path)) {
            QMessageBox::warning(this, tr("Zignatures"), tr("Could not save the database."));
        }
    });
    database->addAction(tr("Find Matches in Analyzed Functions"), this, [this] {
        const QString result = Core()->cmdRaw("z/f").trimmed();
        Core()->triggerRefreshAll();
        showReport(
            tr("Zignature Matches"),
            result.isEmpty() ? tr("Check the Flags view for zignature matches.") : result);
    });

    auto *flirt = new QMenu(ui->flirtButton);
    ui->flirtButton->setMenu(flirt);
    const auto openFlirt = [this](bool apply) {
        const QString path = selectFile(tr("Open FLIRT Signature"), tr("FLIRT signatures (*.sig)"));
        if (path.isEmpty()) {
            return;
        }
        const QString result = Core()->inspectFlirtSignature(path, apply);
        if (result.isEmpty()) {
            QMessageBox::warning(this, tr("FLIRT"), tr("Could not read the FLIRT signature."));
        } else if (apply) {
            QMessageBox::information(this, tr("FLIRT"), tr("Finished scanning functions."));
        } else {
            showReport(tr("FLIRT Signature"), result);
        }
    };
    flirt->addAction(tr("Inspect File…"), this, [openFlirt] { openFlirt(false); });
    flirt->addAction(tr("Apply File…"), this, [openFlirt] { openFlirt(true); });

    auto *warp = new QMenu(ui->warpButton);
    ui->warpButton->setMenu(warp);
    warp->addAction(tr("Load File…"), this, [this] {
        const QString path = selectFile(tr("Load WARP Signatures"), tr("WARP signatures (*.warp)"));
        if (!path.isEmpty()) {
            showReport(tr("WARP"), Core()->loadWarpDatabase(path));
        }
    });
    warp->addAction(tr("Create from Analyzed Functions"), this, [this] {
        showReport(tr("WARP"), Core()->cmdRaw("zw create -a"));
        Core()->triggerRefreshAll();
    });
    warp->addAction(tr("Apply Matches"), this, [this] {
        showReport(tr("WARP"), Core()->cmdRaw("zw match -a"));
        Core()->triggerRefreshAll();
    });
    warp->addAction(tr("Export…"), this, [this] {
        const QString path
            = selectFile(tr("Export WARP Signatures"), tr("WARP signatures (*.warp)"), true);
        if (!path.isEmpty() && !Core()->saveWarpDatabase(path)) {
            QMessageBox::warning(this, tr("WARP"), tr("Could not save the WARP signatures."));
        }
    });
    warp->addAction(tr("Show Container"), this, [this] {
        showReport(tr("WARP"), Core()->cmdRaw("zw info"));
    });
    warp->addAction(tr("Clear Container"), this, [] { Core()->cmdRaw("zw clear"); });

    setScrollMode();
    updateWarpAvailability();

    connect(Core(), &IaitoCore::refreshAll, this, &ZignaturesWidget::refreshZignatures);
}

ZignaturesWidget::~ZignaturesWidget()
{
    delete zignaturesProxyModel;
    delete zignaturesModel;
}

void ZignaturesWidget::refreshZignatures()
{
    zignaturesModel->beginResetModel();
    zignatures = Core()->getAllZignatures();
    zignaturesModel->endResetModel();

    ui->zignaturesTreeView->resizeColumnToContents(0);
    ui->zignaturesTreeView->resizeColumnToContents(1);
    ui->zignaturesTreeView->resizeColumnToContents(2);
    updateWarpAvailability();
}

QString ZignaturesWidget::selectFile(const QString &title, const QString &filter, bool save)
{
    QFileDialog dialog(this, title, Config()->getRecentFolder(), filter);
    dialog.setOptions(QFILEDIALOG_FLAGS);
    dialog.setAcceptMode(save ? QFileDialog::AcceptSave : QFileDialog::AcceptOpen);
    dialog.setFileMode(save ? QFileDialog::AnyFile : QFileDialog::ExistingFile);
    if (save) {
        dialog.setDefaultSuffix(filter.contains("*.warp") ? "warp" : "sdb");
    }
    if (dialog.exec() != QDialog::Accepted) {
        return {};
    }
    const QString path = dialog.selectedFiles().constFirst();
    Config()->setRecentFolder(QFileInfo(path).absolutePath());
    return path;
}

void ZignaturesWidget::showReport(const QString &title, const QString &report)
{
    QDialog dialog(this);
    dialog.setWindowTitle(title);
    auto *layout = new QVBoxLayout(&dialog);
    auto *output = new QPlainTextEdit(report, &dialog);
    output->setFont(Config()->getBaseFont());
    output->setReadOnly(true);
    layout->addWidget(output);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.resize(680, 420);
    dialog.exec();
}

void ZignaturesWidget::updateWarpAvailability()
{
    const auto plugins = Core()->getRCorePluginDescriptions();
    ui->warpButton->setVisible(std::any_of(plugins.cbegin(), plugins.cend(), [](const auto &plugin) {
        return plugin.name == QStringLiteral("warp");
    }));
}

void ZignaturesWidget::setScrollMode()
{
    qhelpers::setVerticalScrollMode(ui->zignaturesTreeView);
}

void ZignaturesWidget::on_zignaturesTreeView_doubleClicked(const QModelIndex &index)
{
    ZignatureDescription item
        = index.data(ZignaturesModel::ZignatureDescriptionRole).value<ZignatureDescription>();
    if (item.offset != RVA_INVALID) {
        Core()->seekAndShow(item.offset);
    }
}
