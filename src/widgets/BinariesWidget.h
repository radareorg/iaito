// BinariesWidget: Manage binary objects via obj commands
#pragma once
#include "core/MainWindow.h"
#include <QLineEdit>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTableView>

class BinariesWidget : public IaitoDockWidget
{
    Q_OBJECT
public:
    explicit BinariesWidget(MainWindow *main);
    ~BinariesWidget() override = default;

private slots:
    void loadBinaries();
    void onSelectCurrentClicked();
    void onSelectAllClicked();
    void onCloseButtonClicked();
    void onBrowseClicked();
    void onOpenButtonClicked();

private:
    MainWindow *mainWindow;
    QTableView *binariesView;
    QStandardItemModel *binariesModel;
    QPushButton *selectCurrentButton;
    QPushButton *selectAllButton;
    QPushButton *closeButton;
    QLineEdit *fileEdit;
    QPushButton *browseButton;
    QPushButton *openButton;
};