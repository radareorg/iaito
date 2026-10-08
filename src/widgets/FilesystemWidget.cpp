#include "FilesystemWidget.h"
#include "common/Helpers.h"
#include "common/SourceLineReference.h"
#include "core/Iaito.h"
#include "core/MainWindow.h"
#include <algorithm>
#include <QApplication>
#include <QContextMenuEvent>
#include <QFontDatabase>
#include <QGridLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QSet>
#include <QSignalBlocker>
#include <QStyle>

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
    removeRows(0, rowCount());
    if (!rootPath.isEmpty()) {
        populateDirectory(invisibleRootItem(), rootPath);
    }
}

QList<FileInfo> FilesystemTreeModel::readDirectory(const QString &path)
{
    // Use the API so source paths, including spaces and command metacharacters, stay intact.
    QList<FileInfo> entries;
    RCoreLocked core = Core()->core();
    RListIter *iter;
    if (path == QStringLiteral("/")) {
        RFSRoot *root;
        IaitoRListForeach(core->fs->roots, iter, RFSRoot, root)
        {
            const QString mountPath = QString::fromUtf8(root->path);
            if (mountPath != QStringLiteral("/")) {
                entries.append({mountPath, QStringLiteral("directory"), 0});
            }
        }
    }
    const QByteArray pathBytes = path.toUtf8();
    RList *files = r_fs_dir(core->fs, pathBytes.constData());
    RFSFile *file;
    IaitoRListForeach(files, iter, RFSFile, file)
    {
        const QString name = QString::fromUtf8(file->name);
        if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral("..")) {
            continue;
        }
        entries.append(
            {name,
             file->type == 'd' || file->type == 'm' ? QStringLiteral("directory")
                                                    : QStringLiteral("file"),
             file->size});
    }
    r_list_free(files);
    std::sort(entries.begin(), entries.end(), [](const FileInfo &a, const FileInfo &b) {
        if (a.type != b.type) {
            return a.type == QStringLiteral("directory");
        }
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    return entries;
}

void FilesystemTreeModel::populateDirectory(QStandardItem *parentItem, const QString &path)
{
    for (const auto &entry : readDirectory(path)) {
        const QString &name = entry.name;
        const QString &type = entry.type;

        QList<QStandardItem *> items;
        QStandardItem *nameItem = new QStandardItem(name);
        const QString fullPath = name.startsWith(QLatin1Char('/'))
                                     ? name
                                     : QDir::cleanPath(path + QLatin1Char('/') + name);
        nameItem->setData(fullPath, PathRole);
        nameItem->setToolTip(fullPath);
        items << nameItem;
        items << new QStandardItem(QString::number(entry.size));
        items << new QStandardItem(type);

        if (type == "directory") {
            nameItem->setIcon(qApp->style()->standardIcon(QStyle::SP_DirIcon));
            nameItem->setData(false, LoadedRole);
            // Add a dummy child to make it expandable
            auto *placeholder = new QStandardItem(tr("Loading..."));
            placeholder->setEnabled(false);
            nameItem->appendRow(placeholder);
        } else {
            nameItem->setIcon(qApp->style()->standardIcon(QStyle::SP_FileIcon));
        }

        for (auto *item : items) {
            item->setEditable(false);
        }
        parentItem->appendRow(items);
    }
}

FilesystemWidget::FilesystemWidget(MainWindow *main)
    : IaitoDockWidget(main)
{
    setWindowTitle(tr("Filesystem"));
    setObjectName("FilesystemWidget");

    setupUI();
    setupMountpointsList();
    setupFilesystemTree();
    setupActions();

    refreshDeferrer = createRefreshDeferrer([this]() { refresh(); });
    connect(Core(), &IaitoCore::refreshAll, this, &FilesystemWidget::refresh);
    connect(Core(), &IaitoCore::functionsChanged, this, &FilesystemWidget::refresh);
    connect(Core(), &IaitoCore::functionRenamed, this, &FilesystemWidget::refresh);
    connect(Core(), &IaitoCore::codeRebased, this, &FilesystemWidget::refresh);
    refresh();
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
    mountpointsList->setMaximumHeight(80);
    mountLayout->addWidget(mountpointsList);

    auto *mountControlsLayout = new QGridLayout();
    fsTypeCombo = new QComboBox(this);
    mountPathEdit = new QLineEdit(this);
    mountPathEdit->setPlaceholderText(tr("Mount path (e.g., /mnt)"));
    offsetEdit = new QLineEdit(this);
    offsetEdit->setPlaceholderText(tr("Offset (default 0)"));
    optionsEdit = new QLineEdit(this);
    optionsEdit->setPlaceholderText(tr("Options (e.g., tcp:127.0.0.1:9999)"));
    mountButton = new QPushButton(tr("Mount"), this);
    umountButton = new QPushButton(tr("Umount"), this);

    mountControlsLayout->addWidget(fsTypeCombo, 0, 0);
    mountControlsLayout->addWidget(mountPathEdit, 0, 1);
    mountControlsLayout->addWidget(mountButton, 0, 2);
    mountControlsLayout->addWidget(offsetEdit, 1, 0);
    mountControlsLayout->addWidget(optionsEdit, 1, 1);
    mountControlsLayout->addWidget(umountButton, 1, 2);

    mountLayout->addLayout(mountControlsLayout);

    splitter->addWidget(mountpointsGroup);

    // Filesystem tree section
    filesystemGroup = new QGroupBox(tr("Filesystem Browser"), this);
    QVBoxLayout *fsLayout = new QVBoxLayout(filesystemGroup);

    auto *navigation = new QHBoxLayout();
    auto *upButton = new QPushButton(tr("Up"), this);
    auto *sourcesButton = new QPushButton(tr("Source Files"), this);
    auto *refreshButton = new QPushButton(tr("Refresh"), this);
    browserPathEdit = new QLineEdit(QStringLiteral("/"), this);
    browserPathEdit->setObjectName(QStringLiteral("filesystemPath"));
    navigation->addWidget(upButton);
    navigation->addWidget(browserPathEdit, 1);
    navigation->addWidget(sourcesButton);
    navigation->addWidget(refreshButton);
    fsLayout->addLayout(navigation);
    connect(upButton, &QPushButton::clicked, this, [this]() {
        setBrowserPath(QDir::cleanPath(treeModel->getRootPath() + QStringLiteral("/..")));
    });
    connect(browserPathEdit, &QLineEdit::returnPressed, this, [this]() {
        setBrowserPath(browserPathEdit->text());
    });
    connect(sourcesButton, &QPushButton::clicked, this, &FilesystemWidget::showSourceFiles);
    connect(refreshButton, &QPushButton::clicked, this, &FilesystemWidget::refresh);

    filesystemTree = new QTreeView(this);
    filesystemTree->setObjectName(QStringLiteral("filesystemTree"));
    treeModel = new FilesystemTreeModel(this);
    filesystemTree->setModel(treeModel);
    filesystemTree->setContextMenuPolicy(Qt::CustomContextMenu);
    filesystemTree->setSelectionBehavior(QAbstractItemView::SelectRows);
    filesystemTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    filesystemTree->setUniformRowHeights(true);
    filesystemTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    filesystemTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    filesystemTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    filesystemTree->header()->setStretchLastSection(false);
    connect(filesystemTree, &QTreeView::expanded, this, &FilesystemWidget::onTreeExpanded);
    connect(
        filesystemTree->selectionModel(),
        &QItemSelectionModel::currentChanged,
        this,
        &FilesystemWidget::onTreeSelectionChanged);
    fsLayout->addWidget(filesystemTree);

    QHBoxLayout *fsControlsLayout = new QHBoxLayout();
    createDirButton = new QPushButton(tr("Create Directory"), this);
    createFileButton = new QPushButton(tr("Create File"), this);
    fsControlsLayout->addWidget(createDirButton);
    fsControlsLayout->addWidget(createFileButton);
    fsControlsLayout->addStretch();

    fsLayout->addLayout(fsControlsLayout);

    splitter->addWidget(filesystemGroup);

    previewGroup = new QGroupBox(tr("File Preview"), this);
    auto *previewLayout = new QVBoxLayout(previewGroup);
    previewStatus = new QLabel(tr("Select a file to preview it."), previewGroup);
    previewStatus->setTextFormat(Qt::PlainText);
    previewStatus->setWordWrap(true);
    previewLayout->addWidget(previewStatus);
    previewTabs = new QTabWidget(previewGroup);
    sourceFunctions = new QTreeWidget(previewTabs);
    sourceFunctions->setObjectName(QStringLiteral("sourceFunctions"));
    sourceFunctions->setHeaderLabels({tr("Function"), tr("Address")});
    sourceLines = new QTreeWidget(previewTabs);
    sourceLines->setObjectName(QStringLiteral("sourceLines"));
    sourceLines->setHeaderLabels({tr("Address"), tr("Line"), tr("Source")});
    sourceLines->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    auto navigate = [](QTreeWidgetItem *item, int) {
        const QVariant address = item->data(0, Qt::UserRole);
        if (address.isValid()) {
            Core()->seekAndShow(address.toULongLong());
        }
    };
    for (auto *tree : {sourceFunctions, sourceLines}) {
        tree->setRootIsDecorated(false);
        tree->setSelectionBehavior(QAbstractItemView::SelectRows);
        tree->setUniformRowHeights(true);
        connect(tree, &QTreeWidget::itemClicked, this, navigate);
        connect(tree, &QTreeWidget::itemActivated, this, navigate);
    }
    sourceLines->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    sourceLines->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    fileContents = new QPlainTextEdit(previewTabs);
    fileContents->setObjectName(QStringLiteral("filesystemContents"));
    fileContents->setReadOnly(true);
    fileContents->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    fileContents->setLineWrapMode(QPlainTextEdit::NoWrap);
    previewTabs->addTab(sourceFunctions, tr("Functions"));
    previewTabs->addTab(sourceLines, tr("Source Lines"));
    previewTabs->addTab(fileContents, tr("Contents"));
    previewLayout->addWidget(previewTabs);
    splitter->addWidget(previewGroup);
    splitter->setStretchFactor(1, 2);
    splitter->setStretchFactor(2, 1);
    splitter->setSizes({170, 400, 230});

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
    connect(mountpointsList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        setBrowserPath(item->data(Qt::UserRole).toString());
    });
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
        index = index.sibling(index.row(), 0);
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
        index = index.sibling(index.row(), 0);
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
        index = index.sibling(index.row(), 0);
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
    const QJsonDocument doc = Core()->cmdj("mj");
    if (!doc.isObject()) {
        return;
    }

    mountpointsList->clear();
    fsTypeCombo->clear();

    currentMountpoints = parseMountpoints(doc);
    for (const MountpointInfo &mountpoint : currentMountpoints) {
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

void FilesystemWidget::refresh()
{
    if (!refreshDeferrer->attemptRefresh(nullptr)) {
        return;
    }
    const QString selectedPath = previewPath;
    QSet<QString> expandedPaths;
    auto visit = [this](auto &&self, const QModelIndex &parent, const auto &action) -> void {
        for (int row = 0; row < treeModel->rowCount(parent); ++row) {
            const QModelIndex index = treeModel->index(row, 0, parent);
            action(index);
            self(self, index, action);
        }
    };
    visit(visit, QModelIndex(), [this, &expandedPaths](const QModelIndex &index) {
        if (filesystemTree->isExpanded(index)) {
            expandedPaths.insert(index.data(FilesystemTreeModel::PathRole).toString());
        }
    });
    const QSignalBlocker selectionBlocker(filesystemTree->selectionModel());
    refreshMountpoints();
    treeModel->refresh();
    bool selectedFileExists = false;
    visit(
        visit,
        QModelIndex(),
        [this, &expandedPaths, &selectedPath, &selectedFileExists](const QModelIndex &index) {
            const QString path = index.data(FilesystemTreeModel::PathRole).toString();
            if (expandedPaths.contains(path)) {
                filesystemTree->expand(index);
            }
            if (!selectedPath.isEmpty() && path == selectedPath) {
                filesystemTree->setCurrentIndex(index);
                selectedFileExists = true;
            }
        });
    if (selectedFileExists) {
        viewFileContents(selectedPath);
    } else {
        clearPreview();
    }
}

void FilesystemWidget::setBrowserPath(const QString &path)
{
    QString absolutePath = path;
    if (!absolutePath.startsWith(QLatin1Char('/'))) {
        absolutePath.prepend(QLatin1Char('/'));
    }
    absolutePath = QDir::cleanPath(absolutePath);
    browserPathEdit->setText(absolutePath);
    clearPreview();
    treeModel->setRootPath(absolutePath);
    const bool sourceDirectory = isSourceFile(absolutePath + QLatin1Char('/'));
    createDirButton->setEnabled(!sourceDirectory);
    createFileButton->setEnabled(!sourceDirectory);
}

bool FilesystemWidget::isSourceFile(const QString &path) const
{
    for (const auto &mount : currentMountpoints) {
        const QString prefix = mount.path == QStringLiteral("/")
                                   ? QStringLiteral("/cl/")
                                   : mount.path + QStringLiteral("/cl/");
        if (mount.plugin == QStringLiteral("r2") && path.startsWith(prefix)) {
            return true;
        }
    }
    return false;
}

void FilesystemWidget::showSourceFiles()
{
    refreshMountpoints();
    QString mountPath;
    QSet<QString> usedPaths;
    for (const auto &mount : currentMountpoints) {
        usedPaths.insert(mount.path);
        if (mount.plugin == QStringLiteral("r2") && mountPath.isEmpty()) {
            mountPath = mount.path;
        }
    }
    if (mountPath.isEmpty()) {
        mountPath = QStringLiteral("/r2");
        for (int suffix = 1; usedPaths.contains(mountPath); ++suffix) {
            mountPath = QStringLiteral("/r2_%1").arg(suffix);
        }
        const QByteArray pathBytes = mountPath.toUtf8();
        RCoreLocked core = Core()->core();
        if (!r_fs_mount(core->fs, "r2", pathBytes.constData(), 0)) {
            previewStatus->setText(tr("Failed to mount the r2 filesystem."));
            return;
        }
    }
    refreshMountpoints();
    setBrowserPath(mountPath);
    bool hasSourceDirectory = false;
    for (int row = 0; row < treeModel->rowCount(); ++row) {
        hasSourceDirectory |= treeModel->index(row, 0).data().toString() == QStringLiteral("cl");
    }
    if (!hasSourceDirectory) {
        previewStatus->setText(tr("This r2 filesystem does not provide a /cl source directory."));
        return;
    }
    const QString sourcePath = QDir::cleanPath(mountPath + QStringLiteral("/cl"));
    setBrowserPath(sourcePath);
    if (treeModel->rowCount() == 0) {
        previewStatus->setText(
            tr("No source references are available. The binary needs source "
               "line information and an r2 filesystem with /cl support."));
    }
}

void FilesystemWidget::clearPreview()
{
    previewPath.clear();
    sourceFunctions->clear();
    sourceLines->clear();
    fileContents->clear();
    previewStatus->setText(tr("Select a file to preview it."));
    previewTabs->setTabEnabled(0, false);
    previewTabs->setTabEnabled(1, false);
    previewTabs->setCurrentWidget(fileContents);
}

void FilesystemWidget::onTreeSelectionChanged(const QModelIndex &index)
{
    const QModelIndex nameIndex = index.sibling(index.row(), 0);
    if (nameIndex.isValid()
        && nameIndex.sibling(nameIndex.row(), 2).data().toString() == QStringLiteral("file")) {
        viewFileContents(nameIndex.data(FilesystemTreeModel::PathRole).toString());
    } else {
        clearPreview();
    }
}

void FilesystemWidget::onMountButtonClicked()
{
    QString fsType = fsTypeCombo->currentText();
    QString path = mountPathEdit->text().trimmed();
    QString offsetStr = offsetEdit->text().trimmed();
#if R2_ABIVERSION >= 95
    QString options = optionsEdit->text().trimmed();
    options.replace('\n', ' ');
    options.replace('\r', ' ');
#endif

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
#if R2_ABIVERSION >= 95
    QByteArray optionsBytes = options.toUtf8();
#endif
    RFSRoot *root = nullptr;
    {
        RCoreLocked core = Core()->core();
#if R2_ABIVERSION >= 95
        root = r_fs_mount_with_options(
            core->fs,
            fsTypeBytes.constData(),
            pathBytes.constData(),
            offset,
            optionsBytes.isEmpty() ? nullptr : optionsBytes.constData());
#else
        root = r_fs_mount(core->fs, fsTypeBytes.constData(), pathBytes.constData(), offset);
#endif
    }
    if (!root) {
        QMessageBox::warning(this, tr("Error"), tr("Failed to mount filesystem."));
        return;
    }

    refreshMountpoints();
    setBrowserPath(path);
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
    setBrowserPath(QStringLiteral("/"));
}

void FilesystemWidget::onCreateDirButtonClicked()
{
    QModelIndex current = filesystemTree->currentIndex();
    current = current.sibling(current.row(), 0);
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
    current = current.sibling(current.row(), 0);
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

    const QString path = index.sibling(index.row(), 0).data(Qt::UserRole).toString();
    if (type == "directory") {
        setBrowserPath(path);
    } else if (type == "file") {
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
    if (!item->data(FilesystemTreeModel::LoadedRole).toBool()) {
        item->setData(true, FilesystemTreeModel::LoadedRole);
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
    filesystemTree->setCurrentIndex(index.sibling(index.row(), 0));

    QMenu menu(this);
    if (type == "file") {
        menu.addAction(viewAction);
        const QString path = index.sibling(index.row(), 0).data(Qt::UserRole).toString();
        if (!isSourceFile(path)) {
            menu.addAction(deleteAction);
            menu.addAction(mallocAction);
        }
    }
    menu.exec(filesystemTree->viewport()->mapToGlobal(pos));
}

void FilesystemWidget::viewFileContents(const QString &path)
{
    QWidget *previousTab = previewPath == path ? previewTabs->currentWidget() : nullptr;
    clearPreview();
    if (path.isEmpty()) {
        return;
    }
    const bool sourceFile = isSourceFile(path);
    const QByteArray pathBytes = path.toUtf8();
    QByteArray contents;
    bool truncated = false;
    {
        RCoreLocked core = Core()->core();
        RFSFile *file = r_fs_open(core->fs, pathBytes.constData(), false);
        if (!file) {
            previewStatus->setText(tr("Cannot open %1").arg(path));
            return;
        }
        // Virtual r2 files are already populated by open. Other plugins need a read.
        const int previewLimit = 1024 * 1024;
        const int length = int(qMin<quint64>(file->size, previewLimit));
        int bytesRead = length;
        if (!file->data && length > 0) {
            bytesRead = r_fs_read(core->fs, file, 0, length);
        }
        if (file->data && bytesRead >= 0) {
            const int available = int(qMin<quint64>(file->size, bytesRead));
            contents = QByteArray(reinterpret_cast<const char *>(file->data), available);
            truncated = file->size > quint64(available);
        }
        r_fs_close(core->fs, file);
        r_fs_file_free(file);
        if (bytesRead < 0) {
            previewStatus->setText(tr("Cannot read %1").arg(path));
            return;
        }
    }
    previewPath = path;
    const QString text = QString::fromUtf8(contents);
    fileContents->setPlainText(text);
    previewStatus->setText(truncated ? tr("%1 (preview truncated to 1 MiB)").arg(path) : path);
    if (!sourceFile) {
        return;
    }

    QSet<RVA> functions;
    const auto references = SourceLineReference::parse(text);
    {
        RCoreLocked core = Core()->core();
        for (const auto &reference : references) {
            auto *line = new QTreeWidgetItem(
                sourceLines,
                {RAddressString(reference.address),
                 QString::number(reference.line),
                 reference.code});
            line->setData(0, Qt::UserRole, QVariant::fromValue(reference.address));
            RAnalFunction *function = r_anal_get_fcn_in(core->anal, reference.address, 0);
            if (function && !functions.contains(function->addr)) {
                functions.insert(function->addr);
                auto *item = new QTreeWidgetItem(
                    sourceFunctions,
                    {QString::fromUtf8(function->name), RAddressString(function->addr)});
                item->setData(0, Qt::UserRole, QVariant::fromValue(RVA(function->addr)));
            }
        }
    }
    previewTabs->setTabEnabled(0, true);
    previewTabs->setTabEnabled(1, true);
    previewTabs->setCurrentWidget(
        previousTab ? previousTab : (functions.isEmpty() ? sourceLines : sourceFunctions));
    sourceFunctions->resizeColumnToContents(0);
    if (functions.isEmpty() && !references.isEmpty()) {
        previewStatus->setText(
            tr("%1 — no analyzed functions; click a source address to view code.").arg(path));
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
