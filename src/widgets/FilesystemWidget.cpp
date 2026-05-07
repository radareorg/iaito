#include "FilesystemWidget.h"
#include "common/Helpers.h"
#include "core/Iaito.h"
#include "core/MainWindow.h"
#include <QApplication>
#include <QContextMenuEvent>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>

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
    QString sanitizedPath = IaitoCore::sanitizeStringForCommand(path);
    QString output = Core()->cmdRaw(QString("mdj %1").arg(sanitizedPath));
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
        if (!value.isObject())
            continue;
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
            nameItem->setIcon(qApp->style()->standardIcon(QStyle::SP_DirIcon));
            // Add a dummy child to make it expandable
            nameItem->appendRow(new QStandardItem("Loading..."));
        } else {
            nameItem->setIcon(qApp->style()->standardIcon(QStyle::SP_FileIcon));
        }

        parentItem->appendRow(items);
    }
}

FilesystemWidget::FilesystemWidget(MainWindow *main)
    : IaitoDockWidget(main)
    , mainWindow(main)
{
    setWindowTitle(tr("Filesystem"));
    setObjectName("FilesystemWidget");

    setupUI();
    setupMountpointsList();
    setupFilesystemTree();
    setupActions();

    refreshMountpoints();
}

FilesystemWidget::~FilesystemWidget() {}

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
    optionsEdit = new QLineEdit(this);
    optionsEdit->setPlaceholderText(tr("Options (e.g., tcp:127.0.0.1:9999)"));
    mountButton = new QPushButton(tr("Mount"), this);
    umountButton = new QPushButton(tr("Umount"), this);

    mountControlsLayout->addWidget(fsTypeCombo);
    mountControlsLayout->addWidget(mountPathEdit);
    mountControlsLayout->addWidget(offsetEdit);
    mountControlsLayout->addWidget(optionsEdit);
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
    connect(
        createFileButton, &QPushButton::clicked, this, &FilesystemWidget::onCreateFileButtonClicked);
    connect(
        filesystemTree, &QTreeView::doubleClicked, this, &FilesystemWidget::onTreeItemDoubleClicked);
    connect(
        filesystemTree,
        &QTreeView::customContextMenuRequested,
        this,
        &FilesystemWidget::onTreeContextMenu);
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

QList<MountpointInfo> FilesystemWidget::parseMountpoints(const QJsonDocument &doc)
{
    QList<MountpointInfo> result;
    if (!doc.isObject()) {
        return result;
    }

    QJsonArray mountpoints = doc.object()["mountpoints"].toArray();
    for (const QJsonValue &value : mountpoints) {
        if (!value.isObject()) {
            continue;
        }
        QJsonObject obj = value.toObject();
        MountpointInfo info;
        info.path = obj["path"].toString();
        info.plugin = obj["plugin"].toString();
        info.offset = obj["offset"].toVariant().toULongLong();
        info.options = obj["options"].toString();
        result << info;
    }
    return result;
}

QList<PluginInfo> FilesystemWidget::parsePlugins(const QJsonDocument &doc)
{
    QList<PluginInfo> result;
    if (!doc.isObject()) {
        return result;
    }

    QJsonArray plugins = doc.object()["plugins"].toArray();
    for (const QJsonValue &value : plugins) {
        if (!value.isObject()) {
            continue;
        }
        QJsonObject obj = value.toObject();
        PluginInfo info;
        info.name = obj["name"].toString();
        info.description = obj["description"].toString();
        result << info;
    }
    return result;
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

    const QList<MountpointInfo> mountpoints = parseMountpoints(doc);
    for (const MountpointInfo &mountpoint : mountpoints) {
        QString details
            = QString("%1 @ 0x%2").arg(mountpoint.plugin, QString::number(mountpoint.offset, 16));
        if (!mountpoint.options.isEmpty()) {
            details += QStringLiteral(", ") + mountpoint.options;
        }

        QListWidgetItem *item
            = new QListWidgetItem(QString("%1 (%2)").arg(mountpoint.path, details), mountpointsList);
        item->setData(Qt::UserRole, mountpoint.path);
        if (!mountpoint.options.isEmpty()) {
            item->setToolTip(mountpoint.options);
        }
    }

    const QList<PluginInfo> plugins = parsePlugins(doc);
    for (const PluginInfo &plugin : plugins) {
        fsTypeCombo->addItem(plugin.name);
    }
}

void FilesystemWidget::onMountButtonClicked()
{
    QString fsType = fsTypeCombo->currentText();
    QString path = mountPathEdit->text().trimmed();
    QString offsetStr = offsetEdit->text().trimmed();
    QString options = optionsEdit->text().trimmed();
    options.replace('\n', ' ');
    options.replace('\r', ' ');

    if (fsType.isEmpty() || path.isEmpty()) {
        QMessageBox::warning(this, tr("Error"), tr("Please specify filesystem type and mount path."));
        return;
    }

    quint64 offset = 0;
    if (!offsetStr.isEmpty()) {
        bool ok;
        offset = offsetStr.toULongLong(&ok, 0);
        if (!ok) {
            QMessageBox::warning(this, tr("Error"), tr("Invalid filesystem mount offset."));
            return;
        }
    }

    QByteArray fsTypeBytes = fsType.toUtf8();
    QByteArray pathBytes = path.toUtf8();
    QByteArray optionsBytes = options.toUtf8();
    RFSRoot *root = nullptr;
    {
        RCoreLocked core = Core()->core();
        root = r_fs_mount_with_options(
            core->fs,
            fsTypeBytes.constData(),
            pathBytes.constData(),
            offset,
            optionsBytes.isEmpty() ? nullptr : optionsBytes.constData());
    }
    if (!root) {
        QMessageBox::warning(this, tr("Error"), tr("Failed to mount filesystem."));
        return;
    }

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

    QString path = item->data(Qt::UserRole).toString();
    if (path.isEmpty()) {
        QString text = item->text();
        int spaceIndex = text.indexOf(' ');
        if (spaceIndex == -1)
            return;
        path = text.left(spaceIndex);
    }

    QByteArray pathBytes = path.toUtf8();
    bool unmounted = false;
    {
        RCoreLocked core = Core()->core();
        unmounted = r_fs_umount(core->fs, pathBytes.constData());
    }
    if (!unmounted) {
        QMessageBox::warning(this, tr("Error"), tr("Failed to unmount filesystem."));
        return;
    }

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
    QString dirName = QInputDialog::getText(
        this, tr("Create Directory"), tr("Directory name:"), QLineEdit::Normal, "", &ok);
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
    QString fileName = QInputDialog::getText(
        this, tr("Create File"), tr("File name:"), QLineEdit::Normal, "", &ok);
    if (ok && !fileName.isEmpty()) {
        QString cmd = QString("mw %1/%2 \"\"").arg(currentPath, fileName);
        Core()->cmdRaw(cmd.toUtf8().constData());
        treeModel->refresh();
    }
}

void FilesystemWidget::onTreeItemDoubleClicked(const QModelIndex &index)
{
    if (!index.isValid())
        return;

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
    if (!index.isValid())
        return;

    QStandardItem *item = treeModel->itemFromIndex(index);
    if (!item)
        return;

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
    if (!index.isValid())
        return;

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
    QString sanitizedPath = IaitoCore::sanitizeStringForCommand(path);
    QString cmd = QString("mc %1").arg(sanitizedPath);
    QString output = Core()->cmdRaw(cmd.toUtf8().constData());
    if (!output.isEmpty()) {
        QMessageBox::information(this, tr("File Contents"), output);
    }
}

void FilesystemWidget::deleteFile(const QString &)
{
    // File deletion is not directly supported in r_fs
    // Could potentially use 'mw' to overwrite with empty data, but that's not deletion
    QMessageBox::information(
        this,
        tr("Not implemented"),
        tr("File deletion is not supported in the filesystem interface."));
}

void FilesystemWidget::loadIntoMalloc(const QString &path)
{
    QString sanitizedPath = IaitoCore::sanitizeStringForCommand(path);
    QString cmd = QString("mo %1").arg(sanitizedPath);
    Core()->cmdRaw(cmd.toUtf8().constData());
    QMessageBox::information(this, tr("Success"), tr("File loaded into malloc."));
}
