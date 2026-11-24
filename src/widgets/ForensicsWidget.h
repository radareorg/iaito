#pragma once

#include "core/FilesystemEvidence.h"
#include "widgets/ForensicsTask.h"
#include "widgets/IaitoDockWidget.h"

#include <memory>

class QTreeWidgetItem;
class QTemporaryDir;

namespace Ui {
class ForensicsWidget;
}

class ForensicsWidget : public IaitoDockWidget
{
    Q_OBJECT

public:
    explicit ForensicsWidget(MainWindow *main);
    ~ForensicsWidget() override;
    bool addImage(const QString &path, QString *error = nullptr);

public slots:
    void openImage();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void showEvidenceItem(QTreeWidgetItem *item);
    void showDirectory();
    void showEntry(QTreeWidgetItem *item);
    void updateFilePreview();
    void updateExtents();
    void displayExtents(const FilesystemEvidence::Mapping &mapping, bool deleted, bool recovery);
    void checkRecovery(FilesystemEvidence::MappingMethod method);
    void exportSelected(FilesystemEvidence::MappingMethod method);
    void dragSelectedFiles();
    void startJob(const QString &title, ForensicsTask::Action work, std::function<void()> done);
    QList<FilesystemEvidence::Entry> selectedFiles() const;
    void findOwners();
    void scanAllocation();
    void updateSearchControls();
    void searchVolume();
    void showSearchResults(const FilesystemEvidence::SearchResult &result);
    void activateSearchHit(QTreeWidgetItem *item);
    void exportSearchHit();
    void goToImageOffset(quint64 offset);
    void filterEntries(const QString &filter);

    std::unique_ptr<Ui::ForensicsWidget> ui;
    QList<std::shared_ptr<FilesystemEvidence>> sources;
    int sourceIndex = -1;
    int volumeIndex = -1;
    QList<FilesystemEvidence::Entry> directories;
    QString volumeProperties;
    quint64 fileOffset = 0;
    quint64 imageOffset = 0;
    QSharedPointer<ForensicsTask> job;
    QList<std::shared_ptr<QTemporaryDir>> dragDirectories;
    QList<QByteArray> dragSelection;
    QStringList draggedPaths;
};
