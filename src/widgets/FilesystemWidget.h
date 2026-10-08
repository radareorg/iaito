#pragma once

#include "IaitoDockWidget.h"
#include "core/MainWindow.h"
#include <QAction>
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTreeView>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

struct MountpointInfo
{
    QString path;
    QString plugin;
    quint64 offset;
    QString options;
};

struct PluginInfo
{
    QString name;
    QString description;
};

struct FileInfo
{
    QString name;
    QString type; // 'f' for file, 'd' for directory
    quint64 size;
};

class FilesystemTreeModel : public QStandardItemModel
{
    Q_OBJECT

public:
    FilesystemTreeModel(QObject *parent = nullptr);

    void setRootPath(const QString &path);
    void refresh();
    void populateDirectory(QStandardItem *parentItem, const QString &path);
    QString getRootPath() const { return rootPath; }

    enum Roles { PathRole = Qt::UserRole, LoadedRole };

private:
    QString rootPath;
    QList<FileInfo> readDirectory(const QString &path);
};

class FilesystemWidget : public IaitoDockWidget
{
    Q_OBJECT

public:
    explicit FilesystemWidget(MainWindow *main);
    ~FilesystemWidget();

public slots:
    void showSourceFiles();

private slots:
    void refreshMountpoints();
    void onMountButtonClicked();
    void onUmountButtonClicked();
    void onCreateDirButtonClicked();
    void onCreateFileButtonClicked();
    void onTreeItemDoubleClicked(const QModelIndex &index);
    void onTreeContextMenu(const QPoint &pos);
    void onTreeExpanded(const QModelIndex &index);
    void refresh();
    void onTreeSelectionChanged(const QModelIndex &index);

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
    void setBrowserPath(const QString &path);
    bool isSourceFile(const QString &path) const;
    void clearPreview();

    // Mountpoints section
    QGroupBox *mountpointsGroup;
    QListWidget *mountpointsList;
    QComboBox *fsTypeCombo;
    QLineEdit *mountPathEdit;
    QLineEdit *offsetEdit;
    QLineEdit *optionsEdit;
    QPushButton *mountButton;
    QPushButton *umountButton;

    // Filesystem tree section
    QGroupBox *filesystemGroup;
    QTreeView *filesystemTree;
    FilesystemTreeModel *treeModel;
    QPushButton *createDirButton;
    QPushButton *createFileButton;
    QLineEdit *browserPathEdit;

    QGroupBox *previewGroup;
    QLabel *previewStatus;
    QTabWidget *previewTabs;
    QTreeWidget *sourceFunctions;
    QTreeWidget *sourceLines;
    QPlainTextEdit *fileContents;
    QString previewPath;
    QList<MountpointInfo> currentMountpoints;
    RefreshDeferrer *refreshDeferrer;

    // Context menu actions
    QAction *viewAction;
    QAction *deleteAction;
    QAction *mallocAction;
};
