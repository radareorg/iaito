#pragma once

#include "IaitoDockWidget.h"
#include "core/MainWindow.h"
#include <QWidget>
#include <QTreeWidget>
#include <QListWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSplitter>
#include <QGroupBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QTreeView>
#include <QHeaderView>
#include <QAction>
#include <QMenu>
#include <QLabel>

struct MountpointInfo {
    QString path;
    QString plugin;
    quint64 offset;
};

struct PluginInfo {
    QString name;
    QString description;
};

struct FileInfo {
    QString name;
    QString type; // 'f' for file, 'd' for directory
    quint64 size;
    QString timestamp; // if available
};

class FilesystemTreeModel : public QStandardItemModel {
    Q_OBJECT

public:
    FilesystemTreeModel(QObject *parent = nullptr);

    void setRootPath(const QString &path);
    void refresh();
    void populateDirectory(QStandardItem *parentItem, const QString &path);

private:
    QString rootPath;
    QJsonDocument parseMdCommand(const QString &path);
};

class FilesystemWidget : public IaitoDockWidget {
    Q_OBJECT

public:
    explicit FilesystemWidget(MainWindow *main);
    ~FilesystemWidget();

private slots:
    void refreshMountpoints();
    void onMountButtonClicked();
    void onUmountButtonClicked();
    void onCreateDirButtonClicked();
    void onCreateFileButtonClicked();
    void onTreeItemDoubleClicked(const QModelIndex &index);
    void onTreeContextMenu(const QPoint &pos);
    void onTreeExpanded(const QModelIndex &index);

private:
    void setupUI();
    void setupMountpointsList();
    void setupFilesystemTree();
    void setupActions();

    QList<MountpointInfo> parseMountpoints(const QJsonDocument &doc);
    QList<PluginInfo> parsePlugins(const QJsonDocument &doc);

    void viewFileContents(const QString &path);
    void deleteFile(const QString &path);
    void loadIntoMalloc(const QString &path);

    MainWindow *mainWindow;

    // Mountpoints section
    QGroupBox *mountpointsGroup;
    QListWidget *mountpointsList;
    QComboBox *fsTypeCombo;
    QLineEdit *mountPathEdit;
    QLineEdit *offsetEdit;
    QPushButton *mountButton;
    QPushButton *umountButton;

    // Filesystem tree section
    QGroupBox *filesystemGroup;
    QTreeView *filesystemTree;
    FilesystemTreeModel *treeModel;
    QPushButton *createDirButton;
    QPushButton *createFileButton;

    // Context menu actions
    QAction *viewAction;
    QAction *deleteAction;
    QAction *mallocAction;
};