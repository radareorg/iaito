#pragma once
#include "common/IOModesController.h"
#include "core/MainWindow.h"
#include <QComboBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QTableView>
#include <QStandardItemModel>

class FilesWidget : public IaitoDockWidget {
    Q_OBJECT
public:
    explicit FilesWidget(MainWindow *main);
    ~FilesWidget() override = default;

private slots:
    void loadOpenedFiles();
    void onCloseButtonClicked();
    void onOpenButtonClicked();

private:
    MainWindow *mainWindow;
    QTableView *filesView;
    QStandardItemModel *filesModel;
    QPushButton *closeButton;
    QComboBox *uriCombo;
    QLineEdit *fileEdit;
    QCheckBox *parseCheck;
    QLineEdit *baseAddrEdit;
    QPushButton *openButton;
};