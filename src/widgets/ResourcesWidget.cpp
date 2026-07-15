#include "ResourcesWidget.h"
#include "common/Configuration.h"
#include "common/Helpers.h"
#include "core/MainWindow.h"
#include "ui_ListDockWidget.h"

#include <QAction>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QRegularExpression>

ResourcesModel::ResourcesModel(QList<ResourcesDescription> *resources, QObject *parent)
    : AddressableItemModel<QAbstractListModel>(parent)
    , resources(resources)
{}

int ResourcesModel::rowCount(const QModelIndex &) const
{
    return resources->count();
}

int ResourcesModel::columnCount(const QModelIndex &) const
{
    return ColumnCount;
}

QVariant ResourcesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= resources->count()) {
        return QVariant();
    }
    const ResourcesDescription &res = resources->at(index.row());

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case NameColumn:
            return res.name;
        case TypeColumn:
            return res.type;
        case SizeColumn:
            return qhelpers::formatBytecount(res.size);
        case VaddrColumn:
            return RAddressString(res.vaddr);
        case PaddrColumn:
            return RAddressString(res.paddr);
        case LanguageColumn:
            return res.language;
        case IdColumn:
            return res.id == UT64_MAX ? QVariant() : QVariant::fromValue(res.id);
        case IndexColumn:
            return QString::number(res.index);
        case TypeIdColumn:
            return res.typeId == UT32_MAX ? QVariant() : QVariant::fromValue(res.typeId);
        case LanguageIdColumn:
            return QVariant::fromValue(res.languageId);
        case CodepageColumn:
            return QVariant::fromValue(res.codepage);
        case NamedColumn:
            return res.named ? tr("Yes") : tr("No");
        case TimestampColumn:
            return res.timestamp;
        case OriginColumn:
            return res.origin;
        case CommentColumn:
            return Core()->getCommentAt(res.vaddr);
        default:
            return QVariant();
        }
    case Qt::EditRole:
        switch (index.column()) {
        case NameColumn:
            return res.name;
        case TypeColumn:
            return res.type;
        case SizeColumn:
            return QVariant::fromValue(res.size);
        case VaddrColumn:
            return QVariant::fromValue(res.vaddr);
        case PaddrColumn:
            return QVariant::fromValue(res.paddr);
        case LanguageColumn:
            return res.language;
        case IdColumn:
            return res.id == UT64_MAX ? QVariant() : QVariant::fromValue(res.id);
        case IndexColumn:
            return QVariant::fromValue(res.index);
        case TypeIdColumn:
            return res.typeId == UT32_MAX ? QVariant() : QVariant::fromValue(res.typeId);
        case LanguageIdColumn:
            return QVariant::fromValue(res.languageId);
        case CodepageColumn:
            return QVariant::fromValue(res.codepage);
        case NamedColumn:
            return res.named;
        case TimestampColumn:
            return res.timestamp;
        case OriginColumn:
            return res.origin;
        case CommentColumn:
            return Core()->getCommentAt(res.vaddr);
        default:
            return QVariant();
        }
    case Qt::ToolTipRole:
        if (index.column() == VaddrColumn || index.column() == PaddrColumn) {
            return tr("Click to show this address in the hexdump.");
        }
        return QVariant();
    case ResourceDescriptionRole:
        return QVariant::fromValue(res);
    default:
        return QVariant();
    }
}

QVariant ResourcesModel::headerData(int section, Qt::Orientation, int role) const
{
    switch (role) {
    case Qt::DisplayRole:
        switch (section) {
        case NameColumn:
            return tr("Name");
        case TypeColumn:
            return tr("Type");
        case SizeColumn:
            return tr("Size");
        case VaddrColumn:
            return tr("Vaddr");
        case PaddrColumn:
            return tr("Paddr");
        case LanguageColumn:
            return tr("Language");
        case IdColumn:
            return tr("ID");
        case IndexColumn:
            return tr("Index");
        case TypeIdColumn:
            return tr("Type ID");
        case LanguageIdColumn:
            return tr("Language ID");
        case CodepageColumn:
            return tr("Codepage");
        case NamedColumn:
            return tr("Named");
        case TimestampColumn:
            return tr("Timestamp");
        case OriginColumn:
            return tr("Origin");
        case CommentColumn:
            return tr("Comment");
        default:
            return QVariant();
        }
    default:
        return QVariant();
    }
}

RVA ResourcesModel::address(const QModelIndex &index) const
{
    const ResourcesDescription &res = resources->at(index.row());
    return index.column() == PaddrColumn ? res.paddr : res.vaddr;
}

QString ResourcesModel::name(const QModelIndex &index) const
{
    return resources->at(index.row()).name;
}

ResourcesProxyModel::ResourcesProxyModel(ResourcesModel *sourceModel, QObject *parent)
    : AddressableFilterProxyModel(sourceModel, parent)
{
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    setSortCaseSensitivity(Qt::CaseInsensitive);
    setSortRole(Qt::EditRole);
}

bool ResourcesProxyModel::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    const QModelIndex index = sourceModel()->index(row, ResourcesModel::NameColumn, parent);
    const auto resource
        = index.data(ResourcesModel::ResourceDescriptionRole).value<ResourcesDescription>();
    return resource.name.contains(FILTER_REGEX);
}

ResourcesWidget::ResourcesWidget(MainWindow *main)
    : ListDockWidget(main)
{
    setObjectName("ResourcesWidget");
    setWindowTitle(tr("Resources"));

    model = new ResourcesModel(&resources, this);
    filterModel = new ResourcesProxyModel(model, this);
    setModels(filterModel);
    ui->treeView->sortByColumn(ResourcesModel::NameColumn, Qt::AscendingOrder);
    showCount(false);

    dumpAction = new QAction(tr("Dump resource..."), this);
    ui->treeView->getItemContextMenu()->addSeparator();
    ui->treeView->getItemContextMenu()->addAction(dumpAction);

    connect(dumpAction, &QAction::triggered, this, &ResourcesWidget::dumpSelectedResource);
    connect(ui->treeView, &QAbstractItemView::clicked, this, &ResourcesWidget::showResourceAddress);
    connect(Core(), &IaitoCore::refreshAll, this, &ResourcesWidget::refreshResources);
    connect(Core(), &IaitoCore::codeRebased, this, &ResourcesWidget::refreshResources);
    connect(Core(), &IaitoCore::commentsChanged, this, [this]() {
        qhelpers::emitColumnChanged(model, ResourcesModel::CommentColumn);
    });
}

void ResourcesWidget::refreshResources()
{
    model->beginResetModel();
    resources = Core()->getAllResources();
    model->endResetModel();
    qhelpers::adjustColumns(ui->treeView, ResourcesModel::ColumnCount, 0);
}

void ResourcesWidget::showResourceAddress(const QModelIndex &index)
{
    if (index.column() != ResourcesModel::VaddrColumn
        && index.column() != ResourcesModel::PaddrColumn) {
        return;
    }
    Core()->seek(filterModel->address(index));
    mainWindow->showMemoryWidget(MemoryWidgetType::Hexdump);
}

void ResourcesWidget::dumpSelectedResource()
{
    const QModelIndex index = ui->treeView->currentIndex();
    if (!index.isValid()) {
        return;
    }
    const auto resource
        = index.data(ResourcesModel::ResourceDescriptionRole).value<ResourcesDescription>();

    QString type = resource.type.isEmpty() ? QStringLiteral("unknown") : resource.type;
    type.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("_"));
    const QString identity = resource.id == UT64_MAX ? QStringLiteral("named")
                                                     : QString::number(resource.id);
    const QString suggestedName = QStringLiteral("resource_%1_%2_%3.bin")
                                      .arg(type, identity, QString::number(resource.index));
    const QString suggestedPath = QDir(Config()->getRecentFolder()).filePath(suggestedName);
    const QString fileName = QFileDialog::getSaveFileName(this, tr("Dump resource"), suggestedPath);
    if (fileName.isEmpty()) {
        return;
    }
    Config()->setRecentFolder(QFileInfo(fileName).absolutePath());

    QString errorMessage;
    if (!Core()->dumpResource(resource, fileName, &errorMessage)) {
        QMessageBox::critical(this, tr("Unable to dump resource"), errorMessage);
    }
}

ResourcesWidget::~ResourcesWidget()
{
    delete filterModel;
    delete model;
}
