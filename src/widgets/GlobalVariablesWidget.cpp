#include "GlobalVariablesWidget.h"

#include "common/Helpers.h"
#include "ui_GlobalVariablesWidget.h"
#include "ui_ListDockWidget.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>

GlobalVariablesModel::GlobalVariablesModel(QObject *parent)
    : AddressableItemModel<QAbstractListModel>(parent)
{}

int GlobalVariablesModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : variables.size();
}

int GlobalVariablesModel::columnCount(const QModelIndex &) const
{
    return ColumnCount;
}

QVariant GlobalVariablesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= variables.size()) {
        return {};
    }
    const auto &variable = variables.at(index.row());
    if (role == DescriptionRole) {
        return QVariant::fromValue(variable);
    }
    if (role != Qt::DisplayRole && role != Qt::EditRole) {
        return {};
    }
    switch (index.column()) {
    case AddressColumn:
        return role == Qt::EditRole ? QVariant::fromValue(variable.address)
                                    : QVariant(RAddressString(variable.address));
    case NameColumn:
        return variable.name;
    case TypeColumn:
        return variable.type;
    case SizeColumn:
        return role == Qt::EditRole ? QVariant::fromValue(variable.size)
                                    : QVariant(RSizeString(variable.size));
    case CommentColumn:
        return Core()->getCommentAt(variable.address);
    default:
        return {};
    }
}

QVariant GlobalVariablesModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
    case AddressColumn:
        return tr("Address");
    case NameColumn:
        return tr("Name");
    case TypeColumn:
        return tr("Type");
    case SizeColumn:
        return tr("Size");
    case CommentColumn:
        return tr("Comment");
    default:
        return {};
    }
}

RVA GlobalVariablesModel::address(const QModelIndex &index) const
{
    return index.isValid() && index.row() >= 0 && index.row() < variables.size()
               ? variables.at(index.row()).address
               : RVA_INVALID;
}

QString GlobalVariablesModel::name(const QModelIndex &index) const
{
    return index.isValid() && index.row() >= 0 && index.row() < variables.size()
               ? variables.at(index.row()).name
               : QString();
}

void GlobalVariablesModel::setVariables(QList<GlobalVariableDescription> variables)
{
    beginResetModel();
    this->variables = std::move(variables);
    endResetModel();
}

GlobalVariablesWidget::GlobalVariablesWidget(MainWindow *main)
    : ListDockWidget(main)
    , controls(new Ui::GlobalVariablesWidget)
    , model(new GlobalVariablesModel(this))
    , proxy(new AddressableFilterProxyModel(model, this))
{
    setObjectName(QStringLiteral("GlobalVariablesWidget"));
    setWindowTitle(tr("Global Variables"));
    proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxy->setFilterKeyColumn(-1);
    proxy->setSortRole(Qt::EditRole);
    setModels(proxy, GlobalVariablesModel::NameColumn, GlobalVariablesModel::TypeColumn);
    ui->treeView->sortByColumn(GlobalVariablesModel::AddressColumn, Qt::AscendingOrder);

    auto toolbar = new QWidget(this);
    controls->setupUi(toolbar);
    ui->verticalLayout->insertWidget(0, toolbar);
    auto menu = ui->treeView->getItemContextMenu();
    menu->addSeparator();
    auto addAction = menu->addAction(tr("Register…"));
    addAction->setToolTip(tr("Register a global variable"));
    addAction->setObjectName(QStringLiteral("actionAddGlobalVariable"));
    addAction->setProperty("addressIndependent", true);
    editAction = menu->addAction(tr("Edit…"));
    editAction->setToolTip(tr("Edit the global variable name and type"));
    editAction->setObjectName(QStringLiteral("actionEditGlobalVariable"));
    deleteAction = menu->addAction(tr("Delete"));
    deleteAction->setToolTip(tr("Delete the global variable"));
    deleteAction->setObjectName(QStringLiteral("actionDeleteGlobalVariable"));
    auto refreshAction = menu->addAction(tr("Refresh"));
    refreshAction->setObjectName(QStringLiteral("actionRefreshGlobalVariables"));
    refreshAction->setProperty("addressIndependent", true);
    controls->addButton->setDefaultAction(addAction);
    controls->editButton->setDefaultAction(editAction);
    controls->deleteButton->setDefaultAction(deleteAction);
    controls->refreshButton->setDefaultAction(refreshAction);
    editAction->setEnabled(false);
    deleteAction->setEnabled(false);

    connect(addAction, &QAction::triggered, this, [this]() { editVariable(true); });
    connect(editAction, &QAction::triggered, this, [this]() { editVariable(false); });
    connect(deleteAction, &QAction::triggered, this, &GlobalVariablesWidget::deleteVariable);
    connect(refreshAction, &QAction::triggered, this, &GlobalVariablesWidget::refreshVariables);
    const auto updateActions = [this]() {
        const bool valid = currentVariable().address != RVA_INVALID;
        editAction->setEnabled(valid);
        deleteAction->setEnabled(valid);
    };
    connect(ui->treeView->selectionModel(), &QItemSelectionModel::currentChanged, this, updateActions);
    connect(model, &QAbstractItemModel::modelReset, this, updateActions);

    refreshDeferrer = createRefreshDeferrer([this]() { refreshVariables(); });
    connect(Core(), &IaitoCore::flagsChanged, this, &GlobalVariablesWidget::refreshVariables);
    connect(Core(), &IaitoCore::refreshAll, this, &GlobalVariablesWidget::refreshVariables);
    connect(Core(), &IaitoCore::codeRebased, this, &GlobalVariablesWidget::refreshVariables);
    connect(Core(), &IaitoCore::commentsChanged, this, [this]() {
        qhelpers::emitColumnChanged(model, GlobalVariablesModel::CommentColumn);
    });
    connect(
        this, &IaitoDockWidget::becameVisibleToUser, this, &GlobalVariablesWidget::refreshVariables);
    refreshVariables();
}

GlobalVariablesWidget::~GlobalVariablesWidget()
{
    ui->treeView->setModel(static_cast<QAbstractItemModel *>(nullptr));
}

void GlobalVariablesWidget::refreshVariables()
{
    if (!refreshDeferrer->attemptRefresh(nullptr)) {
        return;
    }
    model->setVariables(Core()->getAllGlobalVariables());
    for (int column = 0; column < GlobalVariablesModel::ColumnCount; ++column) {
        ui->treeView->resizeColumnToContents(column);
    }
}

GlobalVariableDescription GlobalVariablesWidget::currentVariable() const
{
    return ui->treeView->currentIndex()
        .data(GlobalVariablesModel::DescriptionRole)
        .value<GlobalVariableDescription>();
}

void GlobalVariablesWidget::editVariable(bool create)
{
    const auto variable = currentVariable();
    if (!create && variable.address == RVA_INVALID) {
        return;
    }
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("globalVariableDialog"));
    dialog.setWindowTitle(create ? tr("Register Global Variable") : tr("Edit Global Variable"));
    auto layout = new QFormLayout(&dialog);
    auto address
        = new QLineEdit(RAddressString(create ? Core()->getOffset() : variable.address), &dialog);
    address->setObjectName(QStringLiteral("globalVariableAddress"));
    address->setReadOnly(!create);
    address->setToolTip(tr("An address or radare2 expression, such as sym.counter or $$ + 8."));
    auto name = new QLineEdit(create ? QString() : variable.name, &dialog);
    name->setObjectName(QStringLiteral("globalVariableName"));
    auto type = new QComboBox(&dialog);
    type->setObjectName(QStringLiteral("globalVariableType"));
    type->setEditable(true);
    QStringList types;
    for (const auto &description : Core()->getAllTypes()) {
        types.append(description.type);
    }
    // Primitive types in current r2 are returned in the types member of tj.
    const auto primitiveTypes = Core()->cmdj("tj").object()[QStringLiteral("types")].toArray();
    for (const auto &value : primitiveTypes) {
        const QString typeName = value.toObject()[QStringLiteral("type")].toString();
        types.append(typeName);
    }
    types.removeDuplicates();
    types.sort();
    for (const auto &typeName : types) {
        type->addItem(typeName, typeName);
    }
    type->setCurrentText(create ? QStringLiteral("int") : variable.type);
    auto error = new QLabel(&dialog);
    error->setObjectName(QStringLiteral("globalVariableError"));
    error->setWordWrap(true);
    error->hide();
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addRow(tr("Address:"), address);
    layout->addRow(tr("Name:"), name);
    layout->addRow(tr("Type:"), type);
    layout->addRow(error);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        RVA at;
        QString message;
        if (!Core()->tryMath(address->text(), at) || at == RVA_INVALID) {
            message = tr("Enter a valid address or radare2 expression.");
        } else {
            if (Core()->setGlobalVariable(
                    at,
                    name->text().trimmed(),
                    type->currentText().trimmed(),
                    create ? QString() : variable.name,
                    &message)) {
                dialog.accept();
                return;
            }
        }
        error->setText(message);
        error->show();
    });
    name->setFocus();
    name->selectAll();
    dialog.exec();
}

void GlobalVariablesWidget::deleteVariable()
{
    const auto variable = currentVariable();
    if (variable.address == RVA_INVALID) {
        return;
    }
    if (!Core()->deleteGlobalVariable(variable.address, variable.name)) {
        QMessageBox::warning(
            this,
            tr("Delete Global Variable"),
            tr("Could not delete the global variable. Refresh the list and check that no other "
               "global variable shares its address."));
    }
}
