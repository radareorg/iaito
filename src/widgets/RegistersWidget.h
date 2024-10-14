#pragma once

#include <memory>
#include <QGridLayout>
#include <QJsonObject>
#include <QPlainTextEdit>
#include <QTextEdit>

#include "IaitoDockWidget.h"
#include "core/Iaito.h"
#include "menus/AddressableItemContextMenu.h"

class MainWindow;

namespace Ui {
class RegistersWidget;
}

class RegistersWidget : public IaitoDockWidget
{
    Q_OBJECT

public:
    explicit RegistersWidget(MainWindow *main);
    ~RegistersWidget();

private slots:
    void updateContents();
    void setRegisterGrid();
    void openContextMenu(QPoint point, QString address);

private:
    std::unique_ptr<Ui::RegistersWidget> ui;
    QGridLayout *registerLayout = new QGridLayout;
    AddressableItemContextMenu addressContextMenu;
    int numCols = 2;
    int registerLen = 0;
    RefreshDeferrer *refreshDeferrer;
};
