#pragma once

#include "core/FilesystemEvidence.h"
#include "widgets/ForensicsTask.h"
#include "widgets/IaitoDockWidget.h"

#include <memory>
#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>

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
    bool saveCaseFile(const QString &path, QString *error = nullptr) const;
    bool openCaseFile(const QString &path, QString *error = nullptr);

public slots:
    void openImage();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    static constexpr int EntryRole = Qt::UserRole + 2;
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
    void exportSearchHit(bool recoverPng);
    void openSearchPath(const FilesystemEvidence::SearchHit &hit, quint64 id = UT64_MAX);
    void bookmarkEntry();
    void bookmarkRange();
    void addBookmark(QJsonObject bookmark);
    void showBookmarks();
    void activateBookmark(QTreeWidgetItem *item);
    void openCase();
    void saveCase();
    void exportCase();
    void verifySource(int index, std::function<void(const QByteArray &)> done = {});
    QByteArray recordedHash(const QString &path) const;
    void recordCaseEvent(const QString &action, const QJsonObject &details);
    QJsonObject caseDocument() const;
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
    QJsonArray caseSources;
    QJsonArray bookmarks;
    QJsonArray caseEvents;
    QHash<QString, QDateTime> verifiedSourceTimes;
    QString casePath;
    bool caseDirty = false;
};
