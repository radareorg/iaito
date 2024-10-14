#pragma once

#include <memory>
#include <QJsonObject>
#include <QStandardItem>
#include <QTableView>

#include "IaitoDockWidget.h"
#include "core/Iaito.h"

class MainWindow;

namespace Ui {
class BacktraceWidget;
}

class BacktraceWidget : public IaitoDockWidget
{
    Q_OBJECT

public:
    explicit BacktraceWidget(MainWindow *main);
    ~BacktraceWidget();

private slots:
    void updateContents();
    void setBacktraceGrid();
    void fontsUpdatedSlot();

private:
    std::unique_ptr<Ui::BacktraceWidget> ui;
    QStandardItemModel *modelBacktrace = new QStandardItemModel(1, 5, this);
    QTableView *viewBacktrace = new QTableView(this);
    RefreshDeferrer *refreshDeferrer;
};
