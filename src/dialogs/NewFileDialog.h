#ifndef NEWFILEDIALOG_H
#define NEWFILEDIALOG_H

#include "../widgets/ClickableSvgWidget.h"
#include <memory>
#include <QDialog>
#include <QListWidgetItem>
#include <QSpacerItem>

#include "core/Iaito.h"

#ifdef IAITO_ENABLE_DEBUGGER
class ProcessModel;
class ProcessProxyModel;
#endif

namespace Ui {
class NewFileDialog;
}

class MainWindow;

class NewFileDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NewFileDialog(MainWindow *main);
    ~NewFileDialog();

private slots:
    void on_loadFileButton_clicked();
    void on_checkBox_FilelessOpen_clicked();
    void on_selectFileButton_clicked();
    void on_newFileEdit_textChanged(const QString &text);

    void on_selectProjectsDirButton_clicked();
    void on_loadProjectButton_clicked();
    void on_shellcodeButton_clicked();
    void on_shellcodeText_textChanged();

    void on_logoSvgWidget_clicked();

    void on_recentsListWidget_itemClicked(QListWidgetItem *item);
    void on_recentsListWidget_itemDoubleClicked(QListWidgetItem *item);

    void on_projectsListWidget_itemSelectionChanged();
    void on_projectsListWidget_itemDoubleClicked(QListWidgetItem *item);

    void on_actionRemove_item_triggered();
    void on_actionClear_all_triggered();
    void on_actionRemove_project_triggered();

    void on_importProjectButton_clicked();
    void on_exportProjectButton_clicked();

    void on_tabWidget_currentChanged(int index);

#ifdef IAITO_ENABLE_DEBUGGER
    void on_attachButton_clicked();
    void on_attachProcView_doubleClicked(const QModelIndex &index);
    void on_refreshProcessesButton_clicked();
#endif

protected:
    void dragEnterEvent(QDragEnterEvent *event);
    void dropEvent(QDropEvent *event);

private:
    std::unique_ptr<Ui::NewFileDialog> ui;

    MainWindow *main;

#ifdef IAITO_ENABLE_DEBUGGER
    ProcessModel *processModel = nullptr;
    ProcessProxyModel *processProxyModel = nullptr;
    bool attachTabPopulated = false;
#endif

    /**
     * @return true if list is not empty
     */
    bool fillRecentFilesList();

    /**
     * @return true if list is not empty
     */
    bool fillProjectsList();
    void fillIOPluginsList();
#ifdef IAITO_ENABLE_DEBUGGER
    void setupAttachTab();
    void initAttachModel();
#endif

    void loadFile(const QString &filename);
    void loadProject(const QString &project);
    void loadShellcode(const QString &shellcode, const int size);
#ifdef IAITO_ENABLE_DEBUGGER
    void attachProcess(int pid);
#endif

    void setDisableAndHideWidget(QWidget *w, bool disable_and_hide = true);
    void setSpacerEnabled(QSpacerItem *s, bool enabled, int w = 10, int h = 10);

    static const int MaxRecentFiles = 5;
};

#endif // NEWFILEDIALOG_H
