// MapsWidget: Manage RIO map banks and mappings via om/omb commands
#pragma once
#include "common/IOModesController.h"
#include "core/MainWindow.h"
#include <QComboBox>
#include <QDockWidget>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTableView>

class MapsWidget : public IaitoDockWidget
{
    Q_OBJECT
public:
    explicit MapsWidget(MainWindow *main);
    ~MapsWidget() override;

private slots:
    void loadBanks();
    void onBankChanged(int idx);
    void onAddBank();
    void onDeleteBank();
    void refreshMaps();
    void onAddMap();
    void onDeleteMap();
    void onEditMap();
    void onDeprioritizeMap();

private:
    MainWindow *mainWindow;
    QComboBox *bankCombo;
    QPushButton *addBankBtn;
    QPushButton *delBankBtn;
    QTableView *mapsView;
    QStandardItemModel *mapsModel;
    QPushButton *addMapBtn;
    QPushButton *delMapBtn;
    QPushButton *editMapBtn;
    QPushButton *depriorMapBtn;
    // Defers refresh of banks and maps until widget is visible
    RefreshDeferrer *refreshDeferrer;
};