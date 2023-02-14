#include "FlagsWidget.h"
#include "ui_FlagsWidget.h"
#include "core/MainWindow.h"
#include "common/Helpers.h"

#include <QComboBox>
#include <QMenu>
#include <QShortcut>
#include <QTreeWidget>
#include <QStandardItemModel>
#include <QInputDialog>

FlagsModel::FlagsModel(QList<FlagDescription> *flags, QObject *parent)
    : AddressableItemModel<QAbstractListModel>(parent),
      flags(flags)
{
}

int FlagsModel::rowCount(const QModelIndex &) const
{
    return flags->count();
}

int FlagsModel::columnCount(const QModelIndex &) const
{
    return Columns::COUNT;
}

QVariant FlagsModel::data(const QModelIndex &index, int role) const
{
    if (index.row() >= flags->count())
        return QVariant();

    const FlagDescription &flag = flags->at(index.row());

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case SIZE:
            return RSizeString(flag.size);
        case OFFSET:
            return RAddressString(flag.offset);
        case NAME:
            return flag.name;
        case REALNAME:
            return flag.realname;
        case COMMENT:
            return Core()->getCommentAt(flag.offset);
        default:
            return QVariant();
        }
    case FlagDescriptionRole:
        return QVariant::fromValue(flag);
    default:
        return QVariant();
    }
}

QVariant FlagsModel::headerData(int section, Qt::Orientation, int role) const
{
    switch (role) {
    case Qt::DisplayRole:
        switch (section) {
        case SIZE:
            return tr("Size");
        case OFFSET:
            return tr("Offset");
        case NAME:
            return tr("Name");
        case REALNAME:
            return tr("Real Name");
        case COMMENT:
            return tr("Comment");
        default:
            return QVariant();
        }
    default:
        return QVariant();
    }
}

RVA FlagsModel::address(const QModelIndex &index) const
{
    const FlagDescription &flag = flags->at(index.row());
    return flag.offset;
}

QString FlagsModel::name(const QModelIndex &index) const
{
    const FlagDescription &flag = flags->at(index.row());
    return flag.name;
}

const FlagDescription *FlagsModel::description(QModelIndex index) const
{
    if (index.row() < flags->size()) {
        return &flags->at(index.row());
    }
    return nullptr;
}

FlagsSortFilterProxyModel::FlagsSortFilterProxyModel(FlagsModel *source_model, QObject *parent)
    : AddressableFilterProxyModel(source_model, parent)
{
}

bool FlagsSortFilterProxyModel::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    QModelIndex index = sourceModel()->index(row, 0, parent);
    FlagDescription flag = index.data(FlagsModel::FlagDescriptionRole).value<FlagDescription>();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return true;
#else
    return flag.name.contains(filterRegExp()) || flag.realname.contains(filterRegExp());
#endif
}

bool FlagsSortFilterProxyModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    auto source = static_cast<FlagsModel *>(sourceModel());
    auto left_flag = source->description(left);
    auto right_flag = source->description(right);

    switch (left.column()) {
    case FlagsModel::SIZE:
        if (left_flag->size != right_flag->size)
            return left_flag->size < right_flag->size;
    // fallthrough
    case FlagsModel::OFFSET:
        if (left_flag->offset != right_flag->offset)
            return left_flag->offset < right_flag->offset;
    // fallthrough
    case FlagsModel::NAME:
        return left_flag->name < right_flag->name;
    
    case FlagsModel::REALNAME:
        return left_flag->realname < right_flag->realname;

    case FlagsModel::COMMENT:
        return Core()->getCommentAt(left_flag->offset) < Core()->getCommentAt(right_flag->offset);

    default:
        break;
    }

    // fallback
    return left_flag->offset < right_flag->offset;
}


FlagsWidget::FlagsWidget(MainWindow *main) :
    IaitoDockWidget(main),
    ui(new Ui::FlagsWidget),
    main(main),
    tree(new IaitoTreeWidget(this))
{
    ui->setupUi(this);

    // Add Status Bar footer
    tree->addStatusBar(ui->verticalLayout);

    flags_model = new FlagsModel(&flags, this);
    flags_proxy_model = new FlagsSortFilterProxyModel(flags_model, this);
    connect(ui->filterLineEdit, &QLineEdit::textChanged,
            flags_proxy_model, &QSortFilterProxyModel::setFilterWildcard);
    ui->flagsTreeView->setMainWindow(mainWindow);
    ui->flagsTreeView->setModel(flags_proxy_model);
    ui->flagsTreeView->sortByColumn(FlagsModel::OFFSET, Qt::AscendingOrder);

    // Ctrl-F to move the focus to the Filter search box
    QShortcut *searchShortcut = new QShortcut(QKeySequence::Find, this);
    connect(searchShortcut, &QShortcut::activated, ui->filterLineEdit, [this]() { ui->filterLineEdit->setFocus(); });
    searchShortcut->setContext(Qt::WidgetWithChildrenShortcut);

    // Esc to clear the filter entry
    QShortcut *clearShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(clearShortcut, &QShortcut::activated, [this] {
        if (ui->filterLineEdit->text().isEmpty()) {
            ui->flagsTreeView->setFocus();
        } else {
            ui->filterLineEdit->setText("");
        }
    });
    clearShortcut->setContext(Qt::WidgetWithChildrenShortcut);

    connect(ui->filterLineEdit, &QLineEdit::textChanged, this, [this] {
        tree->showItemsNumber(flags_proxy_model->rowCount());
    });

    setScrollMode();

    connect(Core(), &IaitoCore::flagsChanged, this, &FlagsWidget::flagsChanged);
    connect(Core(), &IaitoCore::codeRebased, this, &FlagsWidget::flagsChanged);
    connect(Core(), &IaitoCore::refreshAll, this, &FlagsWidget::refreshFlagspaces);
    connect(Core(), &IaitoCore::commentsChanged, this, [this]() {
        qhelpers::emitColumnChanged(flags_model, FlagsModel::COMMENT);
    });

    auto menu = ui->flagsTreeView->getItemContextMenu();
    menu->addSeparator();
    menu->addAction(ui->actionRename);
    menu->addAction(ui->actionDelete);
    addAction(ui->actionRename);
    addAction(ui->actionDelete);
}

FlagsWidget::~FlagsWidget() {}

void FlagsWidget::on_flagspaceCombo_currentTextChanged(const QString &arg1)
{
    Q_UNUSED(arg1);

    refreshFlags();
}

void FlagsWidget::on_actionRename_triggered()
{
    FlagDescription flag = ui->flagsTreeView->selectionModel()->currentIndex().data(
                               FlagsModel::FlagDescriptionRole).value<FlagDescription>();

    bool ok;
    QString newName = QInputDialog::getText(this, tr("Rename flag %1").arg(flag.name),
                            tr("Flag name:"), QLineEdit::Normal, flag.name, &ok);
    if (ok && !newName.isEmpty()) {
        Core()->renameFlag(flag.name, newName);
    }
}

void FlagsWidget::on_actionDelete_triggered()
{
    FlagDescription flag = ui->flagsTreeView->selectionModel()->currentIndex().data(
                               FlagsModel::FlagDescriptionRole).value<FlagDescription>();
    Core()->delFlag(flag.name);
}

void FlagsWidget::flagsChanged()
{
    refreshFlagspaces();
}

void FlagsWidget::refreshFlagspaces()
{
    int cur_idx = ui->flagspaceCombo->currentIndex();
    if (cur_idx < 0)
        cur_idx = 0;

    disableFlagRefresh = true; // prevent duplicate flag refresh caused by flagspaceCombo modifications
    ui->flagspaceCombo->clear();
    ui->flagspaceCombo->addItem(tr("(all)"));

    for (const FlagspaceDescription &i : Core()->getAllFlagspaces()) {
        ui->flagspaceCombo->addItem(i.name, QVariant::fromValue(i));
    }

    if (cur_idx > 0)
        ui->flagspaceCombo->setCurrentIndex(cur_idx);
    disableFlagRefresh = false;

    refreshFlags();
}

void FlagsWidget::refreshFlags()
{
    if (disableFlagRefresh) {
        return;
    }
    QString flagspace;

    QVariant flagspace_data = ui->flagspaceCombo->currentData();
    if (flagspace_data.isValid())
        flagspace = flagspace_data.value<FlagspaceDescription>().name;


    flags_model->beginResetModel();
    flags = Core()->getAllFlags(flagspace);
    flags_model->endResetModel();

    tree->showItemsNumber(flags_proxy_model->rowCount());

    // TODO: this is not a very good place for the following:
    QStringList flagNames;
    for (const FlagDescription &i : flags)
        flagNames.append(i.name);
    main->refreshOmniBar(flagNames);
}

void FlagsWidget::setScrollMode()
{
    qhelpers::setVerticalScrollMode(ui->flagsTreeView);
}
