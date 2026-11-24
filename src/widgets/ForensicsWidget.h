#pragma once

#include "core/FilesystemEvidence.h"
#include "widgets/IaitoDockWidget.h"

#include <memory>

class QTreeWidgetItem;

namespace Ui {
class ForensicsWidget;
}

class ForensicsWidget : public IaitoDockWidget
{
    Q_OBJECT

public:
    explicit ForensicsWidget(MainWindow *main);
    ~ForensicsWidget() override;

public slots:
    void openImage();

private:
    void showEvidenceItem(QTreeWidgetItem *item);
    void showDirectory();
    void showEntry(QTreeWidgetItem *item);
    void updateFilePreview();
    void updateExtents();
    void findOwners();
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
};
