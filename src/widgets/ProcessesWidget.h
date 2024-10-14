#pragma once

#include <memory>
#include <QJsonObject>
#include <QSortFilterProxyModel>
#include <QStandardItem>
#include <QTableView>

#include "IaitoDockWidget.h"
#include "core/Iaito.h"

class MainWindow;

namespace Ui {
class ProcessesWidget;
}

class ProcessesFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    ProcessesFilterModel(QObject *parent = nullptr);

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
};

class ProcessesWidget : public IaitoDockWidget
{
    Q_OBJECT

public:
    explicit ProcessesWidget(MainWindow *main);
    ~ProcessesWidget();

private slots:
    void updateContents();
    void setProcessesGrid();
    void fontsUpdatedSlot();
    void onActivated(const QModelIndex &index);

private:
    QString translateStatus(QString status);
    std::unique_ptr<Ui::ProcessesWidget> ui;
    QStandardItemModel *modelProcesses;
    ProcessesFilterModel *modelFilter;
    RefreshDeferrer *refreshDeferrer;
};
