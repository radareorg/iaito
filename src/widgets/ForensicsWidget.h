#pragma once

#include "widgets/IaitoDockWidget.h"

#include <memory>

class FilesystemEvidence;
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
    void showDirectory(const QByteArray &path);
    void filterEntries(const QString &filter);

    std::unique_ptr<Ui::ForensicsWidget> ui;
    QList<std::shared_ptr<FilesystemEvidence>> sources;
    int sourceIndex = -1;
    int volumeIndex = -1;
    QByteArray directory;
};
