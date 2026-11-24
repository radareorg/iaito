#pragma once

#include "core/FilesystemEvidence.h"

#include <memory>
#include <QWidget>

class FilesystemAllocationModel;
class QSortFilterProxyModel;
class ZoomView;

namespace Ui {
class FilesystemAllocationView;
}

class FilesystemAllocationView : public QWidget
{
    Q_OBJECT

public:
    explicit FilesystemAllocationView(QWidget *parent = nullptr);
    ~FilesystemAllocationView() override;
    void setVolume(const FilesystemEvidence::Volume *volume);
    void showMapping(const FilesystemEvidence::Mapping &mapping);

signals:
    void scanRequested();
    void imageOffsetActivated(quint64 offset);

private:
    void updateGrid();
    void zoom(int steps);
    void selectOffset(quint64 offset);
    void showFullVolume();

    std::unique_ptr<Ui::FilesystemAllocationView> ui;
    ZoomView *grid;
    FilesystemAllocationModel *table;
    QSortFilterProxyModel *sortModel;
    quint64 volumeStart = 0;
    quint64 volumeLength = 0;
    quint64 viewStart = 0;
    quint64 viewLength = 0;
    quint64 selectedOffset = UT64_MAX;
};
