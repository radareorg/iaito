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
class ThreadsWidget;
}

class ThreadsFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    ThreadsFilterModel(QObject *parent = nullptr);

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
};

class ThreadsWidget : public IaitoDockWidget
{
    Q_OBJECT

public:
    explicit ThreadsWidget(MainWindow *main);
    ~ThreadsWidget();

private slots:
    void updateContents();
    void setThreadsGrid();
    void fontsUpdatedSlot();
    void onActivated(const QModelIndex &index);

private:
    QString translateStatus(QString status);
    std::unique_ptr<Ui::ThreadsWidget> ui;
    QStandardItemModel *modelThreads;
    ThreadsFilterModel *modelFilter;
    RefreshDeferrer *refreshDeferrer;
};
