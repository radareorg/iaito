#include "SwitchTablesWidget.h"

#include "common/Helpers.h"
#include "core/MainWindow.h"
#include "dialogs/SwitchJumpTableDialog.h"
#include "ui_ListDockWidget.h"
#include "ui_SwitchTablesWidget.h"

#include <QSignalBlocker>
#include <QSplitter>

SwitchTablesModel::SwitchTablesModel(QObject *parent)
    : AddressableItemModel<QAbstractTableModel>(parent)
{}

int SwitchTablesModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : tables.size();
}

int SwitchTablesModel::columnCount(const QModelIndex &) const
{
    return ColumnCount;
}

QVariant SwitchTablesModel::data(const QModelIndex &index, int role) const
{
    const auto table = switchTable(index);
    if (!table || (role != Qt::DisplayRole && role != Qt::EditRole)) {
        return {};
    }
    switch (index.column()) {
    case AddressColumn:
        return role == Qt::EditRole ? QVariant::fromValue(table->address)
                                    : QVariant(RAddressString(table->address));
    case FunctionColumn:
        return table->functionName;
    case CasesColumn:
        return table->cases.size();
    case TableColumn:
        if (table->tableAddress == RVA_INVALID) {
            return {};
        }
        return role == Qt::EditRole ? QVariant::fromValue(table->tableAddress)
                                    : QVariant(RAddressString(table->tableAddress));
    default:
        return {};
    }
}

QVariant SwitchTablesModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
    case AddressColumn:
        return tr("Switch address");
    case FunctionColumn:
        return tr("Function");
    case CasesColumn:
        return tr("Cases");
    case TableColumn:
        return tr("Table address");
    default:
        return {};
    }
}

const SwitchTableDescription *SwitchTablesModel::switchTable(const QModelIndex &index) const
{
    return index.isValid() && index.row() >= 0 && index.row() < tables.size()
               ? &tables.at(index.row())
               : nullptr;
}

RVA SwitchTablesModel::address(const QModelIndex &index) const
{
    const auto table = switchTable(index);
    return table ? table->address : RVA_INVALID;
}

QString SwitchTablesModel::name(const QModelIndex &index) const
{
    const auto table = switchTable(index);
    return table ? table->functionName : QString();
}

void SwitchTablesModel::setTables(QList<SwitchTableDescription> tables)
{
    beginResetModel();
    this->tables = std::move(tables);
    endResetModel();
}

SwitchCasesModel::SwitchCasesModel(QObject *parent)
    : AddressableItemModel<QAbstractTableModel>(parent)
{}

int SwitchCasesModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : cases.size();
}

int SwitchCasesModel::columnCount(const QModelIndex &) const
{
    return ColumnCount;
}

static QString asciiCase(ut64 value)
{
    switch (value) {
    case 0:
        return QStringLiteral("'\\0'");
    case 7:
        return QStringLiteral("'\\a'");
    case 8:
        return QStringLiteral("'\\b'");
    case 9:
        return QStringLiteral("'\\t'");
    case 10:
        return QStringLiteral("'\\n'");
    case 11:
        return QStringLiteral("'\\v'");
    case 12:
        return QStringLiteral("'\\f'");
    case 13:
        return QStringLiteral("'\\r'");
    case '\'':
        return QStringLiteral("'\\\''");
    case '\\':
        return QStringLiteral("'\\\\'");
    default:
        if (value >= 0x20 && value <= 0x7e) {
            return QStringLiteral("'%1'").arg(QChar(static_cast<ushort>(value)));
        }
        if (value <= 0x7f) {
            return QStringLiteral("'\\x%1'").arg(value, 2, 16, QLatin1Char('0'));
        }
        return {};
    }
}

QVariant SwitchCasesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= cases.size()
        || (role != Qt::DisplayRole && role != Qt::EditRole)) {
        return {};
    }
    const auto &entry = cases.at(index.row());
    switch (index.column()) {
    case ValueColumn:
        return QVariant::fromValue(static_cast<qint64>(entry.value));
    case TargetColumn:
        return role == Qt::EditRole ? QVariant::fromValue(entry.target)
                                    : QVariant(RAddressString(entry.target));
    case AsciiColumn:
        return asciiCase(entry.value);
    default:
        return {};
    }
}

QVariant SwitchCasesModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
    case ValueColumn:
        return tr("Case value");
    case TargetColumn:
        return tr("Target address");
    case AsciiColumn:
        return tr("ASCII");
    default:
        return {};
    }
}

RVA SwitchCasesModel::address(const QModelIndex &index) const
{
    return index.isValid() && index.row() >= 0 && index.row() < cases.size()
               ? cases.at(index.row()).target
               : RVA_INVALID;
}

void SwitchCasesModel::setCases(QList<SwitchCaseDescription> cases)
{
    beginResetModel();
    this->cases = std::move(cases);
    endResetModel();
}

SwitchTablesWidget::SwitchTablesWidget(MainWindow *main)
    : ListDockWidget(main)
    , controls(new Ui::SwitchTablesWidget)
    , model(new SwitchTablesModel(this))
    , proxy(new AddressableFilterProxyModel(model, this))
    , casesModel(new SwitchCasesModel(this))
    , casesProxy(new AddressableFilterProxyModel(casesModel, this))
{
    setObjectName(QStringLiteral("SwitchTablesWidget"));
    setWindowTitle(tr("Switch Tables"));
    proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxy->setFilterKeyColumn(-1);
    proxy->setSortRole(Qt::EditRole);
    setModels(proxy);
    ui->treeView->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->treeView->setRootIsDecorated(false);
    ui->treeView->sortByColumn(SwitchTablesModel::AddressColumn, Qt::AscendingOrder);

    auto listing = widget();
    auto splitter = new QSplitter(Qt::Vertical, this);
    splitter->setObjectName(QStringLiteral("switchTablesSplitter"));
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(listing);
    auto details = new QWidget(splitter);
    controls->setupUi(details);
    splitter->addWidget(details);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    setWidget(splitter);

    casesProxy->setSortRole(Qt::EditRole);
    controls->casesView->setModel(static_cast<QAbstractItemModel *>(casesProxy));
    controls->casesView->setMainWindow(main);
    controls->casesView->sortByColumn(SwitchCasesModel::ValueColumn, Qt::AscendingOrder);
    qhelpers::bindFont(controls->casesView, false, true);
    qhelpers::setVerticalScrollMode(controls->casesView);

    auto refreshAction = ui->treeView->getItemContextMenu()->addAction(tr("Refresh"));
    refreshAction->setObjectName(QStringLiteral("actionRefreshSwitchTables"));
    refreshAction->setProperty("addressIndependent", true);
    ui->quickFilterView->addActionButton(refreshAction);
    connect(refreshAction, &QAction::triggered, this, &SwitchTablesWidget::refreshTables);
    connect(controls->editButton, &QPushButton::clicked, this, &SwitchTablesWidget::editSwitch);
    connect(
        ui->treeView->selectionModel(),
        &QItemSelectionModel::currentChanged,
        this,
        &SwitchTablesWidget::updateCases);
    connect(ui->treeView, &QTreeView::clicked, this, [this](const QModelIndex &index) {
        navigateTo(proxy->address(index));
    });
    connect(controls->casesView, &QTreeView::clicked, this, [this](const QModelIndex &index) {
        navigateTo(casesProxy->address(index));
    });
    connect(controls->casesView, &QTreeView::activated, this, [this](const QModelIndex &index) {
        navigateTo(casesProxy->address(index));
    });

    refreshDeferrer = createRefreshDeferrer([this]() { refreshTables(); });
    connect(Core(), &IaitoCore::refreshAll, this, &SwitchTablesWidget::refreshTables);
    connect(Core(), &IaitoCore::functionsChanged, this, &SwitchTablesWidget::refreshTables);
    connect(Core(), &IaitoCore::functionRenamed, this, &SwitchTablesWidget::refreshTables);
    connect(Core(), &IaitoCore::codeRebased, this, &SwitchTablesWidget::refreshTables);
    updateCases();
    refreshTables();
}

SwitchTablesWidget::~SwitchTablesWidget()
{
    controls->casesView->setModel(static_cast<QAbstractItemModel *>(nullptr));
}

const SwitchTableDescription *SwitchTablesWidget::selectedSwitch() const
{
    return model->switchTable(proxy->mapToSource(ui->treeView->currentIndex()));
}

void SwitchTablesWidget::refreshTables()
{
    if (!refreshDeferrer->attemptRefresh(nullptr)) {
        return;
    }
    const auto selected = selectedSwitch();
    const RVA address = selected ? selected->address : RVA_INVALID;
    const auto currentCase = controls->casesView->currentIndex();
    const auto value
        = currentCase.sibling(currentCase.row(), SwitchCasesModel::ValueColumn).data(Qt::EditRole);
    const RVA target = casesProxy->address(currentCase);
    {
        const QSignalBlocker blocker(ui->treeView->selectionModel());
        model->setTables(Core()->getAllSwitchTables());
        for (int row = 0; row < proxy->rowCount(); ++row) {
            const auto index = proxy->index(row, 0);
            if (proxy->address(index) == address) {
                ui->treeView->setCurrentIndex(index);
                break;
            }
        }
    }
    updateCases();
    for (int row = 0; value.isValid() && row < casesProxy->rowCount(); ++row) {
        const auto index = casesProxy->index(row, SwitchCasesModel::ValueColumn);
        if (index.data(Qt::EditRole) == value && casesProxy->address(index) == target) {
            controls->casesView->setCurrentIndex(index);
            break;
        }
    }
    for (int column = 0; column < SwitchTablesModel::ColumnCount; ++column) {
        ui->treeView->resizeColumnToContents(column);
    }
}

void SwitchTablesWidget::updateCases()
{
    const auto table = selectedSwitch();
    controls->editButton->setEnabled(table != nullptr);
    controls->casesLabel->setText(
        table ? tr("Cases for %1 (%2)").arg(RAddressString(table->address), table->functionName)
              : tr("Select a switch table to view its cases."));
    casesModel->setCases(table ? table->cases : QList<SwitchCaseDescription>());
    for (int column = 0; column < SwitchCasesModel::ColumnCount; ++column) {
        controls->casesView->resizeColumnToContents(column);
    }
}

void SwitchTablesWidget::editSwitch()
{
    const auto table = selectedSwitch();
    if (!table) {
        return;
    }
    const RVA address = table->address;
    SwitchJumpTableDialog dialog(mainWindow);
    dialog.setSwitchAddress(address);
    dialog.exec();
    refreshTables();
}

void SwitchTablesWidget::navigateTo(RVA address)
{
    if (address == RVA_INVALID) {
        return;
    }
    if (Core()->getOffset() != address) {
        Core()->seek(address);
    }
    mainWindow->showMemoryWidget(MemoryWidgetType::Disassembly);
}
