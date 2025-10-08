#include "FilesystemWidget.h"
#include "core/MainWindow.h"
#include "common/Helpers.h"
#include <QMessageBox>
#include <QInputDialog>
#include <QContextMenuEvent>
#include <QLabel>

FilesystemTreeModel::FilesystemTreeModel(QObject *parent)
    : QStandardItemModel(parent)
{
    setHorizontalHeaderLabels(QStringList() << tr("Name") << tr("Size") << tr("Type"));
}

void FilesystemTreeModel::setRootPath(const QString &path)
{
    rootPath = path;
    refresh();
}

void FilesystemTreeModel::refresh()
{
    clear();
    setHorizontalHeaderLabels(QStringList() << tr("Name") << tr("Size") << tr("Type"));
    if (!rootPath.isEmpty()) {
        populateDirectory(invisibleRootItem(), rootPath);
    }
}

QJsonDocument FilesystemTreeModel::parseMdCommand(const QString &path)
{
    QString output = Core()->cmdRaw(QString("mdj %1").arg(path));
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(output.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError) {
        qWarning() << "Failed to parse mdj output:" << error.errorString();
        return QJsonDocument();
    }
    return doc;
}

void FilesystemTreeModel::populateDirectory(QStandardItem *parentItem, const QString &path)
{
    QJsonDocument doc = parseMdCommand(path);
    if (!doc.isArray()) {
        return;
    }

    QJsonArray array = doc.array();
    for (const QJsonValue &value : array) {
        if (!value.isObject()) continue;
        QJsonObject obj = value.toObject();

        QString name = obj["name"].toString();
        QString type = obj["type"].toString();
        quint64 size = obj["size"].toVariant().toULongLong();

        QList<QStandardItem *> items;
        QStandardItem *nameItem = new QStandardItem(name);
        nameItem->setData(path + "/" + name, Qt::UserRole); // Store full path
        items << nameItem;
        items << new QStandardItem(QString::number(size));
        items << new QStandardItem(type);

        if (type == "directory") {
            nameItem->setIcon(QIcon(":/img/icons/folder.svg"));
            // Add a dummy child to make it expandable
            nameItem->appendRow(new QStandardItem("Loading..."));
        } else {
            nameItem->setIcon(QIcon(":/img/icons/file.svg"));
        }

        parentItem->appendRow(items);
    }
}

FilesystemWidget::FilesystemWidget(MainWindow *main)
    : IaitoDockWidget(main), mainWindow(main)
{
    setWindowTitle(tr("Filesystem"));
    setObjectName("FilesystemWidget");

    setupUI();
    setupMountpointsList();
    setupFilesystemTree();
    setupActions();

    refreshMountpoints();
}

FilesystemWidget::~FilesystemWidget()
{
}

void FilesystemWidget::setupUI()
{
    QWidget *container = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(container);

    QSplitter *splitter = new QSplitter(Qt::Vertical, this);

    // Mountpoints section
    mountpointsGroup = new QGroupBox(tr("Mountpoints"), this);
    QVBoxLayout *mountLayout = new QVBoxLayout(mountpointsGroup);

    mountpointsList = new QListWidget(this);
    mountLayout->addWidget(mountpointsList);

    QHBoxLayout *mountControlsLayout = new QHBoxLayout();
    fsTypeCombo = new QComboBox(this);
    mountPathEdit = new QLineEdit(this);
    mountPathEdit->setPlaceholderText(tr("Mount path (e.g., /mnt)"));
    offsetEdit = new QLineEdit(this);
    offsetEdit->setPlaceholderText(tr("Offset (default 0)"));
    mountButton = new QPushButton(tr("Mount"), this);
    umountButton = new QPushButton(tr("Umount"), this);

    mountControlsLayout->addWidget(fsTypeCombo);
    mountControlsLayout->addWidget(mountPathEdit);
    mountControlsLayout->addWidget(offsetEdit);
    mountControlsLayout->addWidget(mountButton);
    mountControlsLayout->addWidget(umountButton);

    mountLayout->addLayout(mountControlsLayout);

    splitter->addWidget(mountpointsGroup);

    // Filesystem tree section
    filesystemGroup = new QGroupBox(tr("Filesystem Browser"), this);
    QVBoxLayout *fsLayout = new QVBoxLayout(filesystemGroup);

    filesystemTree = new QTreeView(this);
    treeModel = new FilesystemTreeModel(this);
    filesystemTree->setModel(treeModel);
    filesystemTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(filesystemTree, &QTreeView::expanded, this, &FilesystemWidget::onTreeExpanded);
    fsLayout->addWidget(filesystemTree);

    QHBoxLayout *fsControlsLayout = new QHBoxLayout();
    createDirButton = new QPushButton(tr("Create Directory"), this);
    createFileButton = new QPushButton(tr("Create File"), this);
    fsControlsLayout->addWidget(createDirButton);
    fsControlsLayout->addWidget(createFileButton);
    fsControlsLayout->addStretch();

    fsLayout->addLayout(fsControlsLayout);

    splitter->addWidget(filesystemGroup);

    mainLayout->addWidget(splitter);
    setWidget(container);

    // Connect signals
    connect(mountButton, &QPushButton::clicked, this, &FilesystemWidget::onMountButtonClicked);
    connect(umountButton, &QPushButton::clicked, this, &FilesystemWidget::onUmountButtonClicked);
    connect(createDirButton, &QPushButton::clicked, this, &FilesystemWidget::onCreateDirButtonClicked);
    connect(createFileButton, &QPushButton::clicked, this, &FilesystemWidget::onCreateFileButtonClicked);
    connect(filesystemTree, &QTreeView::doubleClicked, this, &FilesystemWidget::onTreeItemDoubleClicked);
    connect(filesystemTree, &QTreeView::customContextMenuRequested, this, &FilesystemWidget::onTreeContextMenu);
}

void FilesystemWidget::setupMountpointsList()
{
    // Will be populated in refreshMountpoints
}

void FilesystemWidget::setupFilesystemTree()
{
    treeModel->setRootPath("/");
}

void FilesystemWidget::setupActions()
{
    viewAction = new QAction(tr("View Contents"), this);
    deleteAction = new QAction(tr("Delete"), this);
    mallocAction = new QAction(tr("Load into Malloc"), this);

    connect(viewAction, &QAction::triggered, this, [this]() {
        QModelIndex index = filesystemTree->currentIndex();
        if (index.isValid()) {
            QString path = index.data(Qt::UserRole).toString();
            if (path.isEmpty()) {
                path = index.data(Qt::DisplayRole).toString();
            }
            viewFileContents(path);
        }
    });

    connect(deleteAction, &QAction::triggered, this, [this]() {
        QModelIndex index = filesystemTree->currentIndex();
        if (index.isValid()) {
            QString path = index.data(Qt::UserRole).toString();
            if (path.isEmpty()) {
                path = index.data(Qt::DisplayRole).toString();
            }
            deleteFile(path);
        }
    });

    connect(mallocAction, &QAction::triggered, this, [this]() {
        QModelIndex index = filesystemTree->currentIndex();
        if (index.isValid()) {
            QString path = index.data(Qt::UserRole).toString();
            if (path.isEmpty()) {
                path = index.data(Qt::DisplayRole).toString();
            }
            loadIntoMalloc(path);
        }
    });
}

void FilesystemWidget::refreshMountpoints()
{
    QString output = Core()->cmdRaw("mj");
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(output.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError) {
        qWarning() << "Failed to parse mj output:" << error.errorString();
        return;
    }

    mountpointsList->clear();
    fsTypeCombo->clear();

    if (doc.isObject()) {
        QJsonObject obj = doc.object();

        // Mountpoints
        QJsonArray mountpoints = obj["mountpoints"].toArray();
        for (const QJsonValue &value : mountpoints) {
            if (!value.isObject()) continue;
            QJsonObject mp = value.toObject();
            QString path = mp["path"].toString();
            QString plugin = mp["plugin"].toString();
            quint64 offset = mp["offset"].toVariant().toULongLong();

            QString itemText = QString("%1 (%2 @ 0x%3)").arg(path, plugin, QString::number(offset, 16));
            mountpointsList->addItem(itemText);
        }

        // Plugins
        QJsonArray plugins = obj["plugins"].toArray();
        for (const QJsonValue &value : plugins) {
            if (!value.isObject()) continue;
            QJsonObject plugin = value.toObject();
            QString name = plugin["name"].toString();
            fsTypeCombo->addItem(name);
        }
    }
}

void FilesystemWidget::onMountButtonClicked()
{
    QString fsType = fsTypeCombo->currentText();
    QString path = mountPathEdit->text().trimmed();
    QString offsetStr = offsetEdit->text().trimmed();

    if (fsType.isEmpty() || path.isEmpty()) {
        QMessageBox::warning(this, tr("Error"), tr("Please specify filesystem type and mount path."));
        return;
    }

    QString cmd = QString("m %1 %2").arg(path, fsType);
    if (!offsetStr.isEmpty()) {
        bool ok;
        quint64 offset = offsetStr.toULongLong(&ok, 0);
        if (ok) {
            cmd += QString(" %1").arg(offset);
        }
    }

    Core()->cmdRaw(cmd.toUtf8().constData());
    refreshMountpoints();
    treeModel->refresh();
}

void FilesystemWidget::onUmountButtonClicked()
{
    QListWidgetItem *item = mountpointsList->currentItem();
    if (!item) {
        QMessageBox::warning(this, tr("Error"), tr("Please select a mountpoint to unmount."));
        return;
    }

    // Extract path from item text (format: "/path (plugin @ offset)")
    QString text = item->text();
    int spaceIndex = text.indexOf(' ');
    if (spaceIndex == -1) return;
    QString path = text.left(spaceIndex);

    QString cmd = QString("m-%1").arg(path);
    Core()->cmdRaw(cmd.toUtf8().constData());
    refreshMountpoints();
    treeModel->refresh();
}

void FilesystemWidget::onCreateDirButtonClicked()
{
    QModelIndex current = filesystemTree->currentIndex();
    QString currentPath = "/";
    if (current.isValid()) {
        QString path = current.data(Qt::UserRole).toString();
        if (!path.isEmpty()) {
            currentPath = path;
        } else {
            // If it's a directory, use its path
            QString type = current.sibling(current.row(), 2).data(Qt::DisplayRole).toString();
            if (type == "directory") {
                currentPath = current.data(Qt::DisplayRole).toString();
            }
        }
    }

    bool ok;
    QString dirName = QInputDialog::getText(this, tr("Create Directory"),
                                           tr("Directory name:"), QLineEdit::Normal,
                                           "", &ok);
    if (ok && !dirName.isEmpty()) {
        QString cmd = QString("md+ %1/%2").arg(currentPath, dirName);
        Core()->cmdRaw(cmd.toUtf8().constData());
        treeModel->refresh();
    }
}

void FilesystemWidget::onCreateFileButtonClicked()
{
    QModelIndex current = filesystemTree->currentIndex();
    QString currentPath = "/";
    if (current.isValid()) {
        QString path = current.data(Qt::UserRole).toString();
        if (!path.isEmpty()) {
            currentPath = path;
        } else {
            // If it's a directory, use its path
            QString type = current.sibling(current.row(), 2).data(Qt::DisplayRole).toString();
            if (type == "directory") {
                currentPath = current.data(Qt::DisplayRole).toString();
            }
        }
    }

    bool ok;
    QString fileName = QInputDialog::getText(this, tr("Create File"),
                                            tr("File name:"), QLineEdit::Normal,
                                            "", &ok);
    if (ok && !fileName.isEmpty()) {
        QString cmd = QString("mw %1/%2 \"\"").arg(currentPath, fileName);
        Core()->cmdRaw(cmd.toUtf8().constData());
        treeModel->refresh();
    }
}

void FilesystemWidget::onTreeItemDoubleClicked(const QModelIndex &index)
{
    if (!index.isValid()) return;

    QString type = index.sibling(index.row(), 2).data(Qt::DisplayRole).toString();

    if (type == "file") {
        QString path = index.data(Qt::UserRole).toString();
        if (path.isEmpty()) {
            path = index.data(Qt::DisplayRole).toString();
        }
        viewFileContents(path);
    }
}

void FilesystemWidget::onTreeExpanded(const QModelIndex &index)
{
    if (!index.isValid()) return;

    QStandardItem *item = treeModel->itemFromIndex(index);
    if (!item) return;

    // Check if it has a dummy child
    if (item->rowCount() == 1 && item->child(0)->text() == "Loading...") {
        // Remove dummy and load real children
        item->removeRow(0);
        QString path = item->data(Qt::UserRole).toString();
        if (!path.isEmpty()) {
            treeModel->populateDirectory(item, path);
        }
    }
}

void FilesystemWidget::onTreeContextMenu(const QPoint &pos)
{
    QModelIndex index = filesystemTree->indexAt(pos);
    if (!index.isValid()) return;

    QString type = index.sibling(index.row(), 2).data(Qt::DisplayRole).toString();

    QMenu menu(this);
    if (type == "file") {
        menu.addAction(viewAction);
        menu.addAction(deleteAction);
        menu.addAction(mallocAction);
    }
    menu.exec(filesystemTree->viewport()->mapToGlobal(pos));
}

void FilesystemWidget::viewFileContents(const QString &path)
{
    QString cmd = QString("mc %1").arg(path);
    QString output = Core()->cmdRaw(cmd.toUtf8().constData());
    if (!output.isEmpty()) {
        QMessageBox::information(this, tr("File Contents"), output);
    }
}

void FilesystemWidget::deleteFile(const QString &path)
{
    // File deletion is not directly supported in r_fs
    // Could potentially use 'mw' to overwrite with empty data, but that's not deletion
    QMessageBox::information(this, tr("Not implemented"), tr("File deletion is not supported in the filesystem interface."));
}

void FilesystemWidget::loadIntoMalloc(const QString &path)
{
    QString cmd = QString("mo %1").arg(path);
    Core()->cmdRaw(cmd.toUtf8().constData());
    QMessageBox::information(this, tr("Success"), tr("File loaded into malloc."));
}